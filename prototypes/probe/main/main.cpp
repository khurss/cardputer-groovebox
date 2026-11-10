// Throwaway hardware probe. Answers three questions: does full-duplex audio work while writing
// to SD, how many voices fit at 32 kHz, and can samples be memory-mapped from a data partition.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <sys/stat.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

namespace {

constexpr uint32_t kRate = 32000;
constexpr uint32_t kFrames = 256;
constexpr uint32_t kBlockBytes = kFrames * 4;
constexpr uint32_t kRecordSeconds = 15;
constexpr uint32_t kPlaybackSeconds = 5;
constexpr char kDir[] = "/sd/groovebox_probe";
constexpr char kFile[] = "/sd/groovebox_probe/mic.raw";

i2c_master_dev_handle_t codec;
i2s_chan_handle_t tx, rx;
volatile uint32_t txOverflows, rxOverflows;
int16_t outBlock[kFrames * 2];
int16_t inBlock[kFrames * 2];
char summary[1600];
size_t summaryLength;

void report(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(summary + summaryLength, sizeof(summary) - summaryLength, format, args);
    va_end(args);
    if (n > 0 && summaryLength + n < sizeof(summary)) summaryLength += n;
    printf("%s", summary + summaryLength - (n > 0 ? n : 0));
}

bool writeCodec(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(codec, data, sizeof(data), 100) == ESP_OK;
}

bool IRAM_ATTR onTxOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    txOverflows = txOverflows + 1;
    return false;
}

bool IRAM_ATTR onRxOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    rxOverflows = rxOverflows + 1;
    return false;
}

bool initAudio() {
    i2c_master_bus_config_t busConfig = {};
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.sda_io_num = GPIO_NUM_8;
    busConfig.scl_io_num = GPIO_NUM_9;
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&busConfig, &bus) != ESP_OK) return false;

    i2c_device_config_t deviceConfig = {};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = 0x18;
    deviceConfig.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &deviceConfig, &codec) != ESP_OK) return false;

    i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channelConfig.dma_desc_num = 6;
    channelConfig.dma_frame_num = kFrames;
    channelConfig.auto_clear = true;
    if (i2s_new_channel(&channelConfig, &tx, &rx) != ESP_OK) return false;

    i2s_std_config_t std = {};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate);
    std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std.gpio_cfg.bclk = GPIO_NUM_41;
    std.gpio_cfg.ws = GPIO_NUM_43;
    std.gpio_cfg.dout = GPIO_NUM_42;
    std.gpio_cfg.din = GPIO_NUM_46;
    if (i2s_channel_init_std_mode(tx, &std) != ESP_OK) return false;
    if (i2s_channel_init_std_mode(rx, &std) != ESP_OK) return false;

    i2s_event_callbacks_t txCallbacks = {};
    txCallbacks.on_send_q_ovf = onTxOverflow;
    i2s_channel_register_event_callback(tx, &txCallbacks, nullptr);
    i2s_event_callbacks_t rxCallbacks = {};
    rxCallbacks.on_recv_q_ovf = onRxOverflow;
    i2s_channel_register_event_callback(rx, &rxCallbacks, nullptr);

    if (i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) return false;

    // Register 0x01 combines the DAC-only (0xB5) and ADC-only (0xBA) clock masks.
    static const uint8_t init[][2] = {
        {0x00, 0x80}, {0x01, 0xBF}, {0x02, 0x18}, {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00},
        {0x13, 0x10}, {0x14, 0x18}, {0x17, 0xBF}, {0x1C, 0x6A}, {0x32, 0xBF}, {0x37, 0x08},
    };
    bool ok = true;
    for (const auto& item : init) ok &= writeCodec(item[0], item[1]);

    bool armed = false;
    for (int i = 0; i < 3; ++i) {
        vTaskDelay(pdMS_TO_TICKS(30));
        armed = writeCodec(0x0D, 0x01);
    }
    vTaskDelay(pdMS_TO_TICKS(1200));  // The analog stage needs about a second to warm up.
    return ok && armed;
}

bool duplexBlock() {
    size_t done = 0;
    if (i2s_channel_write(tx, outBlock, kBlockBytes, &done, pdMS_TO_TICKS(200)) != ESP_OK) return false;
    return i2s_channel_read(rx, inBlock, kBlockBytes, &done, pdMS_TO_TICKS(200)) == ESP_OK;
}

void fillTone(float& phase, float amplitude) {
    const float step = 2.0f * static_cast<float>(M_PI) * 440.0f / kRate;
    for (uint32_t i = 0; i < kFrames; ++i) {
        const auto sample = static_cast<int16_t>(sinf(phase) * amplitude * 32767.0f);
        outBlock[i * 2] = outBlock[i * 2 + 1] = sample;
        phase += step;
        if (phase > 2.0f * static_cast<float>(M_PI)) phase -= 2.0f * static_cast<float>(M_PI);
    }
}

struct Voice {
    float phase, increment, low, band, envelope;
};

void benchVoices() {
    static Voice voices[16];
    for (int count : {1, 2, 4, 6, 8, 10, 12, 16}) {
        for (int v = 0; v < count; ++v) voices[v] = {0.0f, (110.0f + 37.0f * v) / kRate, 0.0f, 0.0f, 1.0f};
        volatile float sink = 0.0f;
        const uint32_t blocks = 200;
        const uint32_t start = esp_cpu_get_cycle_count();
        for (uint32_t block = 0; block < blocks; ++block) {
            for (uint32_t frame = 0; frame < kFrames; ++frame) {
                float mix = 0.0f;
                for (int v = 0; v < count; ++v) {
                    Voice& voice = voices[v];
                    voice.phase += voice.increment;
                    if (voice.phase >= 1.0f) voice.phase -= 1.0f;
                    const float saw = 2.0f * voice.phase - 1.0f;
                    voice.low += 0.2f * voice.band;
                    const float high = saw - voice.low - 0.5f * voice.band;
                    voice.band += 0.2f * high;
                    voice.envelope *= 0.99999f;
                    mix += voice.low * voice.envelope;
                }
                sink = mix;
            }
        }
        const uint32_t cycles = esp_cpu_get_cycle_count() - start;
        (void)sink;
        const double available = 240e6 * blocks * kFrames / kRate;
        report("BENCH voices=%d load=%.1f%%\n", count, 100.0 * cycles / available);
    }
}

sdmmc_card_t* mountCard() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_14;
    bus.miso_io_num = GPIO_NUM_39;
    bus.sclk_io_num = GPIO_NUM_40;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4000;
    if (spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA) != ESP_OK) return nullptr;

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
    const esp_err_t error = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mount, &card);
    if (error != ESP_OK) {
        report("SD mount failed: %s\n", esp_err_to_name(error));
        return nullptr;
    }
    return card;
}

struct Writer {
    StreamBufferHandle_t stream;
    FILE* file;
    volatile bool finish;
    volatile bool done;
    volatile uint32_t written;
    volatile uint32_t maxWriteMs;
};

void writerTask(void* argument) {
    auto* writer = static_cast<Writer*>(argument);
    static uint8_t chunk[4096];
    while (true) {
        const size_t size = xStreamBufferReceive(writer->stream, chunk, sizeof(chunk), pdMS_TO_TICKS(50));
        if (size > 0) {
            const int64_t start = esp_timer_get_time();
            writer->written = writer->written + fwrite(chunk, 1, size, writer->file);
            const auto elapsed = static_cast<uint32_t>((esp_timer_get_time() - start) / 1000);
            if (elapsed > writer->maxWriteMs) writer->maxWriteMs = elapsed;
        } else if (writer->finish) {
            break;
        }
    }
    writer->done = true;
    vTaskDelete(nullptr);
}

void recordWithPlayback() {
    mkdir(kDir, 0777);
    FILE* file = fopen(kFile, "wb");
    if (!file) {
        report("DUPLEX could not create %s\n", kFile);
        return;
    }
    Writer writer = {};
    writer.file = file;
    writer.stream = xStreamBufferCreate(64 * 1024, 1);
    if (!writer.stream) {
        fclose(file);
        report("DUPLEX could not allocate the ring buffer\n");
        return;
    }
    xTaskCreatePinnedToCore(writerTask, "writer", 6144, &writer, 5, nullptr, 0);

    printf("PROBE: speak into the microphone for %u seconds\n", static_cast<unsigned>(kRecordSeconds));
    txOverflows = 0;
    rxOverflows = 0;
    float phase = 0.0f;
    double energyLeft = 0.0, energyRight = 0.0;
    uint32_t dropped = 0, failures = 0;
    const uint32_t blocks = kRecordSeconds * kRate / kFrames;
    static int16_t mono[kFrames];
    for (uint32_t block = 0; block < blocks; ++block) {
        fillTone(phase, 0.12f);
        if (!duplexBlock()) ++failures;
        for (uint32_t i = 0; i < kFrames; ++i) {
            mono[i] = inBlock[i * 2];
            energyLeft += static_cast<double>(inBlock[i * 2]) * inBlock[i * 2];
            energyRight += static_cast<double>(inBlock[i * 2 + 1]) * inBlock[i * 2 + 1];
        }
        if (xStreamBufferSend(writer.stream, mono, sizeof(mono), 0) != sizeof(mono)) ++dropped;
    }
    writer.finish = true;
    while (!writer.done) vTaskDelay(pdMS_TO_TICKS(10));
    fclose(file);
    vStreamBufferDelete(writer.stream);

    struct stat info = {};
    stat(kFile, &info);
    const double samples = static_cast<double>(blocks) * kFrames;
    report("DUPLEX expected=%u written=%u filesize=%ld dropped_blocks=%u io_failures=%u\n",
           static_cast<unsigned>(blocks * sizeof(mono)), static_cast<unsigned>(writer.written),
           static_cast<long>(info.st_size), static_cast<unsigned>(dropped), static_cast<unsigned>(failures));
    report("DUPLEX tx_overflows=%u rx_overflows=%u max_sd_write_ms=%u\n", static_cast<unsigned>(txOverflows),
           static_cast<unsigned>(rxOverflows), static_cast<unsigned>(writer.maxWriteMs));
    report("DUPLEX mic_rms left=%.0f right=%.0f\n", sqrt(energyLeft / samples), sqrt(energyRight / samples));

    printf("PROBE: playing the last %u seconds back\n", static_cast<unsigned>(kPlaybackSeconds));
    file = fopen(kFile, "rb");
    if (!file) return;
    const long tail = static_cast<long>(kPlaybackSeconds * kRate * sizeof(int16_t));
    fseek(file, info.st_size > tail ? info.st_size - tail : 0, SEEK_SET);
    while (fread(mono, sizeof(int16_t), kFrames, file) == kFrames) {
        for (uint32_t i = 0; i < kFrames; ++i) outBlock[i * 2] = outBlock[i * 2 + 1] = mono[i];
        duplexBlock();
    }
    fclose(file);
    memset(outBlock, 0, sizeof(outBlock));
    duplexBlock();
}

void testPartition() {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!partition) {
        report("FLASH no data partition\n");
        return;
    }
    char magic[4] = {};
    esp_partition_read(partition, 0, magic, sizeof(magic));
    if (memcmp(magic, "GBSM", sizeof(magic)) != 0) {  // Only touch a partition this probe owns.
        report("FLASH partition %s has no probe marker, untouched\n", partition->label);
        return;
    }
    static uint8_t pattern[4096];
    for (size_t i = 0; i < sizeof(pattern); ++i) pattern[i] = static_cast<uint8_t>(i * 31);
    const size_t offset = 0x10000;
    bool ok = esp_partition_erase_range(partition, offset, sizeof(pattern)) == ESP_OK;
    ok = ok && esp_partition_write(partition, offset, pattern, sizeof(pattern)) == ESP_OK;
    const void* mapped = nullptr;
    esp_partition_mmap_handle_t handle;
    ok = ok && esp_partition_mmap(partition, offset, sizeof(pattern), ESP_PARTITION_MMAP_DATA, &mapped, &handle) == ESP_OK;
    const bool same = ok && memcmp(mapped, pattern, sizeof(pattern)) == 0;
    if (ok) esp_partition_munmap(handle);
    report("FLASH label=%s offset=0x%x size=0x%x mmap_ok=%d\n", partition->label,
           static_cast<unsigned>(partition->address), static_cast<unsigned>(partition->size), same ? 1 : 0);
}

void probeTask(void*) {
    report("PROBE audio_init=%d\n", initAudio() ? 1 : 0);
    report("PROBE free_internal=%u\n", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));

    float phase = 0.0f;
    for (uint32_t block = 0; block < 3 * kRate / kFrames; ++block) {
        fillTone(phase, 0.25f);
        duplexBlock();
    }
    memset(outBlock, 0, sizeof(outBlock));
    duplexBlock();

    benchVoices();
    if (mountCard()) recordWithPlayback();
    testPartition();

    while (true) {
        printf("PROBE-SUMMARY\n%sPROBE-END\n", summary);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

}  // namespace

extern "C" void app_main() {
    printf("PROBE: starting in 5 seconds\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
    xTaskCreatePinnedToCore(probeTask, "probe", 8192, nullptr, 20, nullptr, 1);
}
