#include "hardware.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_attr.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gb/drum_ui.h"
#include "sdmmc_cmd.h"

namespace hw {

namespace {

constexpr uint8_t kCodecAddress = 0x18;
constexpr uint8_t kKeyboardAddress = 0x34;
constexpr uint8_t kRegCfg = 0x01;
constexpr uint8_t kRegIntStat = 0x02;
constexpr uint8_t kRegEventCount = 0x03;
constexpr uint8_t kRegEventA = 0x04;
constexpr uint8_t kRegKeypadRows = 0x1D;
constexpr uint8_t kRegKeypadColumnsLow = 0x1E;
constexpr uint8_t kRegKeypadColumnsHigh = 0x1F;
constexpr int kKeyColumns = 14;
constexpr int kKeyRows = 4;

// Key layout printed on the Cardputer, indexed by row and column.
const char kKeyMap[kKeyRows][kKeyColumns] = {
    {gb::key::Esc, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', gb::key::Backspace},
    {gb::key::Tab, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
    {gb::key::Fn, gb::key::Shift, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', gb::key::Enter},
    {gb::key::Ctrl, gb::key::Opt, gb::key::Alt, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '},
};

i2c_master_bus_handle_t bus;
i2c_master_dev_handle_t codec;
i2c_master_dev_handle_t keyboard;
i2s_chan_handle_t tx, rx;
volatile uint32_t txOverflows, rxOverflows;

bool addDevice(uint8_t address, i2c_master_dev_handle_t* device) {
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &config, device) == ESP_OK;
}

bool writeRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), 100) == ESP_OK;
}

bool readRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t& value) {
    return i2c_master_transmit_receive(device, &reg, 1, &value, 1, 100) == ESP_OK;
}

bool IRAM_ATTR onTxOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    txOverflows = txOverflows + 1;
    return false;
}

bool IRAM_ATTR onRxOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    rxOverflows = rxOverflows + 1;
    return false;
}

}  // namespace

bool initBus() {
    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = GPIO_NUM_8;
    config.scl_io_num = GPIO_NUM_9;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, &bus) == ESP_OK;
}

bool initAudio() {
    if (!addDevice(kCodecAddress, &codec)) return false;

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = kBlockFrames;
    channel.auto_clear = true;
    if (i2s_new_channel(&channel, &tx, &rx) != ESP_OK) return false;

    i2s_std_config_t standard = {};
    standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    standard.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    standard.gpio_cfg.bclk = GPIO_NUM_41;
    standard.gpio_cfg.ws = GPIO_NUM_43;
    standard.gpio_cfg.dout = GPIO_NUM_42;
    standard.gpio_cfg.din = GPIO_NUM_46;
    if (i2s_channel_init_std_mode(tx, &standard) != ESP_OK) return false;
    if (i2s_channel_init_std_mode(rx, &standard) != ESP_OK) return false;

    i2s_event_callbacks_t txCallbacks = {};
    txCallbacks.on_send_q_ovf = onTxOverflow;
    i2s_channel_register_event_callback(tx, &txCallbacks, nullptr);
    i2s_event_callbacks_t rxCallbacks = {};
    rxCallbacks.on_recv_q_ovf = onRxOverflow;
    i2s_channel_register_event_callback(rx, &rxCallbacks, nullptr);
    if (i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) return false;

    // Register 0x01 combines the DAC-only (0xB5) and ADC-only (0xBA) clock masks.
    // ES8311 register 0x32: 0xBF is unity (0 dB), NOT a 75% volume limit.
    // 0xFF would add +32 dB after our limiter. Master volume stays in the DSP.
    // See ES8311 User Guide, section 11.3 (DAC volume and DRC).
    static const uint8_t init[][2] = {
        {0x00, 0x80}, {0x01, 0xBF}, {0x02, 0x18}, {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00},
        {0x13, 0x10}, {0x14, 0x18}, {0x17, 0xBF}, {0x1C, 0x6A}, {0x32, 0xBF}, {0x37, 0x08},
    };
    const auto configure = [](uint8_t reg, uint8_t value) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (writeRegister(codec, reg, value)) return true;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return false;
    };
    bool ok = true;
    for (const auto& item : init) {
        ok = configure(item[0], item[1]) && ok;
        if (item[0] == 0x00) vTaskDelay(pdMS_TO_TICKS(10)); // Let codec reset finish before clock setup.
    }
    // The capture path arms only on a 0x0D write made while the I2S clock is running.
    for (int i = 0; i < 3; ++i) {
        vTaskDelay(pdMS_TO_TICKS(30));
        ok = configure(0x0D, 0x01) && ok;
    }
    return ok;
}

bool initKeyboard() {
    if (!addDevice(kKeyboardAddress, &keyboard)) return false;
    bool ok = writeRegister(keyboard, kRegKeypadRows, 0x7F);
    ok = writeRegister(keyboard, kRegKeypadColumnsLow, 0xFF) && ok;
    ok = writeRegister(keyboard, kRegKeypadColumnsHigh, 0x00) && ok;
    ok = writeRegister(keyboard, kRegCfg, 0x01) && ok;
    uint8_t ignored = 0;
    for (int i = 0; i < 10; ++i) ok = readRegister(keyboard, kRegEventA, ignored) && ok;  // Empties the FIFO.
    return writeRegister(keyboard, kRegIntStat, 0x03) && ok;
}

bool exchangeAudio(const int16_t* output, int16_t* input) {
    size_t done = 0;
    const size_t bytes = kBlockFrames * 2 * sizeof(int16_t);
    if (i2s_channel_write(tx, output, bytes, &done, pdMS_TO_TICKS(200)) != ESP_OK) return false;
    return i2s_channel_read(rx, input, bytes, &done, pdMS_TO_TICKS(200)) == ESP_OK;
}

AudioStats audioStats() { return {txOverflows, rxOverflows}; }
bool readAudioOutputConfig(AudioOutputConfig& config) {
    return codec && readRegister(codec,0x32,config.volume) &&
        readRegister(codec,0x31,config.control) && readRegister(codec,0x34,config.drc) &&
        readRegister(codec,0x37,config.eq);
}

bool pollKey(KeyEvent& event) {
    uint8_t count = 0;
    if (!readRegister(keyboard, kRegEventCount, count) || (count & 0x0F) == 0) return false;
    uint8_t raw = 0;
    if (!readRegister(keyboard, kRegEventA, raw) || raw == 0) return false;
    writeRegister(keyboard, kRegIntStat, 0x01);
    const int code = (raw & 0x7F) - 1;
    if (code < 0) return false;
    const int chipRow = code / 10;
    const int chipColumn = code % 10;
    const int column = chipRow * 2 + (chipColumn > 3 ? 1 : 0);
    const int row = chipColumn % 4;
    if (row >= kKeyRows || column >= kKeyColumns) return false;
    event = {kKeyMap[row][column], (raw & 0x80) != 0};
    return true;
}

bool mountCard() {
    spi_bus_config_t spi = {};
    spi.mosi_io_num = GPIO_NUM_14;
    spi.miso_io_num = GPIO_NUM_39;
    spi.sclk_io_num = GPIO_NUM_40;
    spi.quadwp_io_num = -1;
    spi.quadhd_io_num = -1;
    spi.max_transfer_sz = 4000;
    if (spi_bus_initialize(SPI2_HOST, &spi, SDSPI_DEFAULT_DMA) != ESP_OK) return false;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_12;
    slot.host_id = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;  // Never format the user's card.
    mount.max_files = 3;
    mount.allocation_unit_size = 16 * 1024;
    sdmmc_card_t* card = nullptr;
    return esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mount, &card) == ESP_OK;
}

int cardFilesystem() {
    FATFS* filesystem = nullptr;
    DWORD freeClusters = 0;
    if (f_getfree("0:", &freeClusters, &filesystem) != FR_OK || !filesystem) return 0;
    return filesystem->fs_type;
}

}  // namespace hw

// The Stamp-S3A RGB data pin is G21. G38 is shared with LCD backlight;
// the UI sets full backlight while RGB is enabled (M5Stack Cardputer-Adv pin map).
#include "driver/rmt_tx.h"
namespace hw {
namespace {
rmt_channel_handle_t rgbChannel = nullptr;
rmt_encoder_handle_t rgbEncoder = nullptr;
rmt_symbol_word_t rgbSymbols[25];
}
bool initRgb() {
    rmt_tx_channel_config_t config = {};
    config.gpio_num = GPIO_NUM_21;
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 10000000;
    config.mem_block_symbols = 48;
    config.trans_queue_depth = 1;
    if (rmt_new_tx_channel(&config, &rgbChannel) != ESP_OK) return false;
    rmt_copy_encoder_config_t encoder = {};
    if (rmt_new_copy_encoder(&encoder, &rgbEncoder) != ESP_OK) {
        rmt_del_channel(rgbChannel); rgbChannel = nullptr; return false;
    }
    if (rmt_enable(rgbChannel) != ESP_OK) {
        rmt_del_encoder(rgbEncoder); rmt_del_channel(rgbChannel);
        rgbChannel = nullptr; rgbEncoder = nullptr; return false;
    }
    return setRgb(0);
}
bool setRgb(uint32_t rgb) {
    if (!rgbChannel || !rgbEncoder) return false;
    // Do not reuse the payload until the previous transaction has completed.
    if (rmt_tx_wait_all_done(rgbChannel, 10) != ESP_OK) return false;
    uint32_t grb = ((rgb & 0xff00) << 8) | ((rgb >> 8) & 0xff00) | (rgb & 0xff);
    for (unsigned i=0;i<24;++i) {
        bool one = grb & (1u << (23-i));
        rgbSymbols[i].level0 = 1; rgbSymbols[i].duration0 = one ? 8 : 4;
        rgbSymbols[i].level1 = 0; rgbSymbols[i].duration1 = one ? 4 : 8;
    }
    rgbSymbols[24].level0 = rgbSymbols[24].level1 = 0;
    rgbSymbols[24].duration0 = rgbSymbols[24].duration1 = 2000; // 400 us reset
    rmt_transmit_config_t txConfig = {};
    if (rmt_transmit(rgbChannel, rgbEncoder, rgbSymbols, sizeof(rgbSymbols), &txConfig) != ESP_OK) return false;
    return rmt_tx_wait_all_done(rgbChannel, 10) == ESP_OK;
}
}
