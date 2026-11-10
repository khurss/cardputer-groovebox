# Groovebox: hardware prototypes and sequencer core implementation plan

> **Historical implementation plan.** Code samples and instructions describe the original stage, not the current firmware. Use the [current guides](../../README.md) for controls, installation and the 5% audio-test ceiling.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** confirm real Cardputer ADV hardware limits and build a host-tested timing core: grids, swing, patterns, scenes and song.

**Architecture:** install one disposable probe beside Launcher and GLIDE to test three risky assumptions. The hardware-independent C++17 `gb_core` is tested on a PC and later integrated as an ESP-IDF component.

**Tech Stack:** ESP-IDF v5.5.1 in Docker (original local image `cbc2e2ec2afe`), C++17, host g++/make and Python 3 standard library. For current setup, use the root README.

**Spec:** `docs/superpowers/specs/2026-11-09-groovebox-design.md`

## Global Constraints

- Do not use GLIDE code, sounds or branding.
- Device: ESP32-S3, 8 MB flash, no PSRAM, port `/dev/ttyACM0`.
- Do not overwrite Launcher (0x10000) or GLIDE (0x170000). Never use `idf.py flash`: it would erase Launcher.
- The application must never format the microSD card.
- Time: 96 ticks per quarter note, 384 per bar.
- Grids: 1/4 (96), 1/8 (48), 1/16 (24), 1/32 (12), 1/8T (32), 1/16T (16).
- Swing 50–75%, affecting alternate steps on 1/8, 1/16 and 1/32 grids.
- Pins: I2C SDA 8, SCL 9, ES8311 address 0x18; I2S BCK 41, WS 43, DOUT 42, DIN 46, no MCLK; SD SCK 40, MOSI 14, MISO 39, CS 12.
- Enable ES8311 capture by writing 0x0D only while I2S clocks run (around +30, +60, +90 ms).
- Target audio format: 32 kHz, 16-bit.

## Roadmap

This plan covers stage 1 and the stage 2 timing core. Detailed stage 2–7 plans follow Task 4 because sample rate, voice count and sample storage depend on its results.

| Stage | Scope | Grid and swing behavior |
|---|---|---|
| 1 | Prototypes (Tasks 1–4) | |
| 2 | Drum machine: timing core (Tasks 5–6), then drum synthesis, grid view and saving | Screen controls for grid/swing; step remapping on grid change |
| 3 | Instruments: synth, live recording, piano roll | Quantize to current grid; note swing |
| 4 | Scenes and Song screens | |
| 5 | Mixer and effects | Exclude tracks from swing |
| 6 | SD samples | |
| 7 | Microphone recording and slicing | |

---

### Task 1: Docker build and installation beside Launcher

**Files:**
- Create: `.gitignore`, `tools/idf.sh`, `tools/slot.py`
- Test: `tools/test_slot.py`

**Interfaces:**
- Produces: `tools/idf.sh <project-directory> <idf.py-arguments>`; `tools/slot.py install <bin> | select <label> | remove-probe`.

- [ ] **Step 1: Create `.gitignore` and the build wrapper**

<!-- file: .gitignore -->
```gitignore
build/
sdkconfig
sdkconfig.old
managed_components/
prototypes/probe/components/fatfs/
.tmp/
__pycache__/
```

<!-- file: tools/idf.sh -->
```bash
#!/usr/bin/env bash
# Runs idf.py from the local ESP-IDF image. FatFs is copied into the project with exFAT on,
# because ESP-IDF hard-codes FF_FS_EXFAT to 0 and the user's 200 GB card is probably exFAT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${GB_IDF_IMAGE:-cbc2e2ec2afe}"
PROJECT="${1:?usage: tools/idf.sh <project-dir> <idf.py args...>}"
shift
exec docker run --rm -v "$ROOT:/work" -w "/work/$PROJECT" -u "$(id -u):$(id -g)" -e HOME=/tmp "$IMAGE" bash -c '
  set -e
  if [ ! -d components/fatfs ]; then
    mkdir -p components
    cp -r "$IDF_PATH/components/fatfs" components/fatfs
    sed -i "s/^#define FF_FS_EXFAT.*/#define FF_FS_EXFAT\t\t1/" components/fatfs/src/ffconf.h
  fi
  exec idf.py "$@"
' bash "$@"
```

Run: `chmod +x tools/idf.sh`

- [ ] **Step 2: Write a failing partition-table test**

<!-- file: tools/test_slot.py -->
```python
import hashlib
import struct
import unittest

import slot


def entry(type_, subtype, offset, size, label):
    return struct.pack("<HBBII16sI", 0x50AA, type_, subtype, offset, size, label.encode(), 0)


DEVICE = [
    entry(1, 0x02, 0x9000, 0x5000, "nvs"),
    entry(1, 0x00, 0xE000, 0x2000, "otadata"),
    entry(0, 0x20, 0x10000, 0x150000, "app0"),
    entry(1, 0x03, 0x160000, 0x10000, "coredump"),
    entry(0, 0x10, 0x170000, 0xF0000, "glide"),
]


class TableTests(unittest.TestCase):
    def test_build_appends_md5_and_pads(self):
        data = slot.build(DEVICE)
        body = b"".join(DEVICE)
        self.assertEqual(data[:160], body)
        self.assertEqual(data[160:176], b"\xeb\xeb" + b"\xff" * 14)
        self.assertEqual(data[176:192], hashlib.md5(body).digest())
        self.assertEqual(len(data), 0x1000)
        self.assertEqual(slot.parse(data), DEVICE)

    def test_add_probe_uses_next_ota_subtype(self):
        entries = slot.add_probe(DEVICE)
        fields = struct.unpack("<HBBII16sI", entries[-1])
        self.assertEqual(fields[1:5], (0, 0x11, 0x260000, 0x100000))
        self.assertEqual(slot.label(entries[-1]), "probe")

    def test_add_probe_is_idempotent(self):
        self.assertEqual(slot.add_probe(slot.add_probe(DEVICE)), slot.add_probe(DEVICE))

    def test_add_probe_rejects_overlap(self):
        taken = DEVICE + [entry(1, 0x82, 0x2A0000, 0x1000, "other")]
        with self.assertRaises(ValueError):
            slot.add_probe(taken)

    def test_remove_probe_restores_table(self):
        self.assertEqual(slot.remove_probe(slot.add_probe(DEVICE)), DEVICE)

    def test_otadata_for_first_slot_has_known_crc(self):
        sector = slot.otadata_sector(0)
        self.assertEqual(sector[:4], struct.pack("<I", 1))
        self.assertEqual(sector[28:32], struct.pack("<I", 0x4743989A))
        self.assertEqual(len(sector), 0x1000)

    def test_ota_index_of_label(self):
        self.assertEqual(slot.ota_index(slot.add_probe(DEVICE), "glide"), 0)
        self.assertEqual(slot.ota_index(slot.add_probe(DEVICE), "probe"), 1)
        with self.assertRaises(ValueError):
            slot.ota_index(DEVICE, "missing")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Verify the test fails**

Run: `python3 tools/test_slot.py`
Expected: FAIL, `ModuleNotFoundError: No module named 'slot'`

- [ ] **Step 4: Implement `slot.py`**

<!-- file: tools/slot.py -->
```python
#!/usr/bin/env python3
"""Installs the hardware probe into a spare OTA slot beside Launcher and GLIDE."""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

TABLE_OFFSET = 0x8000
OTADATA_OFFSET = 0xE000
PROBE_OFFSET = 0x260000
PROBE_SIZE = 0x100000
PORT = "/dev/ttyACM0"
ROOT = Path(__file__).resolve().parent.parent
FORMAT = "<HBBII16sI"


def parse(data):
    entries = []
    for pos in range(0, len(data), 32):
        chunk = data[pos : pos + 32]
        if chunk[:2] != b"\xaa\x50":
            break
        entries.append(chunk)
    return entries


def build(entries):
    body = b"".join(entries)
    return (body + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest()).ljust(0x1000, b"\xff")


def label(entry):
    return struct.unpack(FORMAT, entry)[5].rstrip(b"\0").decode()


def remove_probe(entries):
    return [e for e in entries if label(e) != "probe"]


def add_probe(entries):
    kept = remove_probe(entries)
    subtypes = []
    for e in kept:
        _, type_, subtype, offset, size, _, _ = struct.unpack(FORMAT, e)
        if offset < PROBE_OFFSET + PROBE_SIZE and PROBE_OFFSET < offset + size:
            raise ValueError(f"probe slot overlaps partition {label(e)}")
        if type_ == 0 and 0x10 <= subtype <= 0x1F:
            subtypes.append(subtype)
    next_subtype = max(subtypes, default=0x0F) + 1
    return kept + [struct.pack(FORMAT, 0x50AA, 0, next_subtype, PROBE_OFFSET, PROBE_SIZE, b"probe", 0)]


def ota_index(entries, name):
    for e in entries:
        _, type_, subtype, _, _, _, _ = struct.unpack(FORMAT, e)
        if label(e) == name and type_ == 0 and 0x10 <= subtype <= 0x1F:
            return subtype - 0x10
    raise ValueError(f"no OTA app labelled {name}")


def otadata_sector(index):
    seq = struct.pack("<I", index + 1)
    crc = zlib.crc32(seq, 0xFFFFFFFF)
    return (seq + b"\xff" * 24 + struct.pack("<I", crc)).ljust(0x1000, b"\xff")


def esptool(*args):
    gid = os.stat(PORT).st_gid
    image = os.environ.get("GB_IDF_IMAGE", "cbc2e2ec2afe")
    command = [
        "docker", "run", "--rm", f"--device={PORT}", f"--group-add={gid}",
        "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/tmp",
        "-v", f"{ROOT}:/work", "-w", "/work", image,
        "esptool.py", "--chip", "esp32s3", "--port", PORT, *args,
    ]
    subprocess.run(command, check=True)


def write(work, offset, data):
    path = Path(work) / f"write-{offset:x}.bin"
    path.write_bytes(data)
    esptool("write_flash", hex(offset), "/work/" + str(path.relative_to(ROOT)))


def read_entries(work):
    path = Path(work) / "table.bin"
    esptool("read_flash", hex(TABLE_OFFSET), "0xC00", "/work/" + str(path.relative_to(ROOT)))
    return parse(path.read_bytes())


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: slot.py install <bin> | select <label> | remove-probe")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as work:
        entries = read_entries(work)
        if argv[1] == "install":
            app = Path(argv[2]).read_bytes()
            if len(app) > PROBE_SIZE:
                sys.exit("probe image does not fit its slot")
            table = add_probe(entries)
            write(work, PROBE_OFFSET, app)
            write(work, TABLE_OFFSET, build(table))
            write(work, OTADATA_OFFSET, otadata_sector(ota_index(table, "probe")))
        elif argv[1] == "select":
            write(work, OTADATA_OFFSET, otadata_sector(ota_index(entries, argv[2])))
        elif argv[1] == "remove-probe":
            write(work, OTADATA_OFFSET, otadata_sector(ota_index(entries, "glide")))
            write(work, TABLE_OFFSET, build(remove_probe(entries)))
        else:
            sys.exit(f"unknown command {argv[1]}")


if __name__ == "__main__":
    main(sys.argv)
```

Run: `chmod +x tools/slot.py`

- [ ] **Step 5: Verify tests pass**

Run: `python3 tools/test_slot.py`
Expected: `OK`, 7 tests.

- [ ] **Step 6: Commit**

```bash
git add .gitignore tools
git commit -m "build: add docker wrapper and side-by-side probe installer"
```

---

### Task 2: Probe firmware

**Files:**
- Create: `prototypes/probe/CMakeLists.txt`, `prototypes/probe/sdkconfig.defaults`, `prototypes/probe/partitions.csv`, `prototypes/probe/main/CMakeLists.txt`, `prototypes/probe/main/main.cpp`

**Interfaces:**
- Produces: `prototypes/probe/build/probe.bin`. Every five seconds the USB console prints a block from `PROBE-SUMMARY` through `PROBE-END`.

- [ ] **Step 1: Create the project configuration**

<!-- file: prototypes/probe/CMakeLists.txt -->
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(probe)
```

<!-- file: prototypes/probe/sdkconfig.defaults -->
```text
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_USE_LABEL=y
```

<!-- file: prototypes/probe/partitions.csv -->
```text
nvs,      data, nvs,    0x9000,   0x5000,
otadata,  data, ota,    0xE000,   0x2000,
app0,     app,  ota_0,  0x10000,  0x100000,
samples,  data, spiffs, 0x110000, 0x400000,
```

<!-- file: prototypes/probe/main/CMakeLists.txt -->
```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_i2s esp_driver_sdspi esp_driver_spi sdmmc fatfs vfs esp_partition esp_timer
)
```

- [ ] **Step 2: Write the probe**

<!-- file: prototypes/probe/main/main.cpp -->
```cpp
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
```

- [ ] **Step 3: Build**

Run: `tools/idf.sh prototypes/probe set-target esp32s3 && tools/idf.sh prototypes/probe build`
Expected: `Project build complete`; `prototypes/probe/build/probe.bin` is smaller than 0x100000 bytes.

- [ ] **Step 4: Commit**

```bash
git add prototypes
git commit -m "feat: add hardware probe firmware"
```

---

### Task 3: Run the probe on hardware (prototypes 1 and 2)

**Files:**
- Modify: `docs/superpowers/specs/2026-11-09-groovebox-design.md` (Prototype results section)

- [ ] **Step 1: Verify that microSD is inserted in the Cardputer**

Ask the user. The probe creates only `/groovebox_probe` and `mic.raw` inside it.

- [ ] **Step 2: Install the probe**

Run: `python3 tools/slot.py install prototypes/probe/build/probe.bin`
Expected: three `Hash of data verified` messages. The device screen stays dark; this is normal.

- [ ] **Step 3: Read the console and ask the user to speak**

Tell the user first: after about ten seconds, a tone plays for three seconds, followed by fifteen seconds of recording (speak into the microphone over the quiet tone), then playback of the last five seconds.

Run: `stty -F /dev/ttyACM0 115200 raw -echo && timeout 60 cat /dev/ttyACM0 | tee .tmp/probe.log`
Expected: a block from `PROBE-SUMMARY` through `PROBE-END`.

- [ ] **Step 4: Evaluate results**

| Check | Success criterion | If unsuccessful |
|---|---|---|
| `audio_init` | 1 | Debug codec initialization before continuing |
| Tone | User heard it | Debug output |
| `BENCH voices=8` | `load` at most 50% | Switch to 22.05 kHz or reduce voices |
| `SD mount` | No error | Ask whether the card is inserted; investigate the error |
| `written` and `expected` | Match | Increase buffering or reduce SD load |
| `dropped_blocks`, `tx_overflows`, `rx_overflows` | All zero | Permit microphone recording only while stopped |
| `mic_rms` | At least one channel clearly above silence; user reports intelligible recording | Adjust gain 0x14 or the channel |
| `max_sd_write_ms` | Record the value | Size the stage 7 buffer |

- [ ] **Step 5: Record results in the specification**

Add this section to the specification:

```markdown
## Prototype results

- Test date: <run date>
- Selected rate: <32 kHz or 22.05 kHz according to BENCH>
- Load: 8 voices <value>, 16 voices <value>
- Simultaneous recording/playback: <yes/no>, max_sd_write_ms <value>
- Microphone channel: <left/right>, gain 0x14 = <value>
- exFAT on the user card: <mounted/not mounted>
```

- [ ] **Step 6: Restore GLIDE and commit**

Run: `python3 tools/slot.py remove-probe`
Expected: two `Hash of data verified` messages; the device boots GLIDE.

```bash
git add docs
git commit -m "docs: record hardware probe results"
```

---

### Task 4: Launcher installation with a sample partition (prototype 3)

**Files:**
- Create: `tools/package.py`
- Test: `tools/test_package.py`
- Modify: `docs/superpowers/specs/2026-11-09-groovebox-design.md` (Prototype results section)

**Interfaces:**
- Consumes: `prototypes/probe/build/` from Task 2.
- Produces: `prototypes/probe/build/probe-package.bin`, an image for Launcher WebUI upload.

- [ ] **Step 1: Write a failing test**

<!-- file: tools/test_package.py -->
```python
import unittest

import package


class PackageTests(unittest.TestCase):
    def test_places_every_part_at_its_flash_offset(self):
        data = package.merge(b"B" * 16, b"\xaa\x50\x01" + b"T" * 13, b"\xe9" + b"A" * 15)
        self.assertEqual(data[0:16], b"B" * 16)
        self.assertEqual(data[0x8000:0x8003], b"\xaa\x50\x01")
        self.assertEqual(data[0x10000], 0xE9)
        self.assertEqual(data[package.SAMPLES_OFFSET : package.SAMPLES_OFFSET + 4], b"GBSM")

    def test_ends_right_after_the_marker_sector(self):
        data = package.merge(b"B", b"T", b"A")
        self.assertEqual(len(data), package.SAMPLES_OFFSET + 0x1000)

    def test_gap_is_erased_flash(self):
        data = package.merge(b"B", b"T", b"A")
        self.assertEqual(data[1], 0xFF)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Verify the test fails**

Run: `python3 tools/test_package.py`
Expected: FAIL, `ModuleNotFoundError: No module named 'package'`

- [ ] **Step 3: Implement packaging**

<!-- file: tools/package.py -->
```python
#!/usr/bin/env python3
"""Builds the image Launcher's WebUI needs to create the probe's samples partition."""
import sys
from pathlib import Path

SAMPLES_OFFSET = 0x110000


def merge(bootloader, table, app):
    image = bytearray(b"\xff" * (SAMPLES_OFFSET + 0x1000))
    image[0 : len(bootloader)] = bootloader
    image[0x8000 : 0x8000 + len(table)] = table
    image[0x10000 : 0x10000 + len(app)] = app
    # Launcher ignores a data partition with no payload, so the marker also makes it real.
    image[SAMPLES_OFFSET : SAMPLES_OFFSET + 4] = b"GBSM"
    return bytes(image)


def main(build_dir):
    build = Path(build_dir)
    image = merge(
        (build / "bootloader" / "bootloader.bin").read_bytes(),
        (build / "partition_table" / "partition-table.bin").read_bytes(),
        (build / "probe.bin").read_bytes(),
    )
    (build / "probe-package.bin").write_bytes(image)


if __name__ == "__main__":
    main(sys.argv[1])
```

- [ ] **Step 4: Verify tests pass and build the image**

Run: `python3 tools/test_package.py && python3 tools/package.py prototypes/probe/build`
Expected: `OK`, 3 tests; `prototypes/probe/build/probe-package.bin` exists.

- [ ] **Step 5: Install through WebUI with the user**

Ask the user to open **WUI → my Network** on Cardputer, visit `http://launcher.local` (`admin` / `launcher`), choose **↑ OTA**, select `prototypes/probe/build/probe-package.bin` and confirm installation.

- [ ] **Step 6: Read the result**

Run: `stty -F /dev/ttyACM0 115200 raw -echo && timeout 60 cat /dev/ttyACM0 | tee .tmp/probe-launcher.log`
Expected: `FLASH label=<label> offset=<address> size=0x400000 mmap_ok=1`.

If size is below 0x400000 or the line is absent, use the specified fallback: create the sample partition during cable installation.

- [ ] **Step 7: Record the result and remove the probe**

Append `Sample partition through Launcher: <works/does not work>, size <value>` to Prototype results. Ask the user to remove the probe in Launcher and confirm that GLIDE boots.

```bash
git add tools docs
git commit -m "feat: verify samples partition install through Launcher"
```

---

### Task 5: Project model, grid and swing

**Files:**
- Create: `Makefile`, `components/gb_core/include/gb/model.h`, `tests/test.h`, `tests/main.cpp`
- Test: `tests/test_model.cpp`

**Interfaces:**
- Produces: `gb::Grid`, `gb::gridTicks(Grid)`, `gb::patternTicks(length, Grid)`, `gb::swingOffset(step, Grid, swing)`, and structures `DrumPattern`, `Note`, `InstrumentPattern`, `Scene`, `SongEntry`, `Project`.

- [ ] **Step 1: Create the test build**

<!-- file: Makefile -->
```make
CXX := g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Icomponents/gb_core/include -Itests
SOURCES := $(wildcard components/gb_core/src/*.cpp) $(wildcard tests/*.cpp)
HEADERS := $(wildcard components/gb_core/include/gb/*.h) tests/test.h

.PHONY: test clean
test: build/tests
	./build/tests

build/tests: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

clean:
	rm -rf build
```

<!-- file: tests/test.h -->
```cpp
#pragma once
#include <cstdio>
#include <vector>

struct TestCase {
    const char* name;
    void (*run)();
};

inline std::vector<TestCase>& testCases() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& testFailures() {
    static int failures = 0;
    return failures;
}

struct TestRegistrar {
    TestRegistrar(const char* name, void (*run)()) { testCases().push_back({name, run}); }
};

#define TEST(name)                                  \
    static void name();                             \
    static TestRegistrar registrar_##name(#name, name); \
    static void name()

#define CHECK_EQ(actual, expected)                                                                  \
    do {                                                                                            \
        const auto a_ = (actual);                                                                   \
        const auto e_ = (expected);                                                                 \
        if (!(a_ == e_)) {                                                                          \
            std::printf("%s:%d: %s: got %lld, expected %lld\n", __FILE__, __LINE__, #actual,       \
                        static_cast<long long>(a_), static_cast<long long>(e_));                    \
            ++testFailures();                                                                       \
        }                                                                                           \
    } while (0)
```

<!-- file: tests/main.cpp -->
```cpp
#include "test.h"

int main() {
    for (const TestCase& test : testCases()) test.run();
    std::printf("%zu tests, %d failures\n", testCases().size(), testFailures());
    return testFailures() == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Write a failing test**

<!-- file: tests/test_model.cpp -->
```cpp
#include "gb/model.h"
#include "test.h"

using namespace gb;

TEST(gridSizesAreIntegerTicks) {
    CHECK_EQ(gridTicks(Grid::Quarter), 96);
    CHECK_EQ(gridTicks(Grid::Eighth), 48);
    CHECK_EQ(gridTicks(Grid::Sixteenth), 24);
    CHECK_EQ(gridTicks(Grid::ThirtySecond), 12);
    CHECK_EQ(gridTicks(Grid::EighthTriplet), 32);
    CHECK_EQ(gridTicks(Grid::SixteenthTriplet), 16);
}

TEST(patternLengthDependsOnGrid) {
    CHECK_EQ(patternTicks(16, Grid::Sixteenth), 384);
    CHECK_EQ(patternTicks(4, Grid::Quarter), 384);
    CHECK_EQ(patternTicks(8, Grid::ThirtySecond), 96);
    CHECK_EQ(patternTicks(16, Grid::Quarter), 1536);
}

TEST(swingDelaysOnlyEverySecondStep) {
    CHECK_EQ(swingOffset(0, Grid::Sixteenth, 75), 0);
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 75), 12);
    CHECK_EQ(swingOffset(2, Grid::Sixteenth, 75), 0);
    CHECK_EQ(swingOffset(3, Grid::Eighth, 66), 15);
    CHECK_EQ(swingOffset(1, Grid::ThirtySecond, 75), 6);
}

TEST(straightSwingHasNoOffset) {
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 50), 0);
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 10), 0);
}

TEST(swingIsClampedToSeventyFive) {
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 99), 12);
}

TEST(quarterAndTripletGridsIgnoreSwing) {
    CHECK_EQ(swingOffset(1, Grid::Quarter, 75), 0);
    CHECK_EQ(swingOffset(1, Grid::EighthTriplet, 75), 0);
    CHECK_EQ(swingOffset(1, Grid::SixteenthTriplet, 75), 0);
}

TEST(newProjectIsEmptyAndStraight) {
    static Project project;
    CHECK_EQ(project.bpm, 120);
    CHECK_EQ(project.swing, 50);
    CHECK_EQ(project.scenes[0].pattern[0], kEmptyPattern);
    CHECK_EQ(project.drums[0].length, 16);
    CHECK_EQ(project.swingEnabled[4], true);
}
```

- [ ] **Step 3: Verify the test fails**

Run: `make test`
Expected: compilation error, `gb/model.h: No such file or directory`.

- [ ] **Step 4: Implement the model**

<!-- file: components/gb_core/include/gb/model.h -->
```cpp
#pragma once
#include <cstdint>

namespace gb {

constexpr uint16_t kTicksPerQuarter = 96;
constexpr uint16_t kTicksPerBar = 384;
constexpr int kDrumLanes = 8;
constexpr int kMaxSteps = 16;
constexpr int kInstrumentTracks = 4;
constexpr int kTracks = kInstrumentTracks + 1;  // Track 0 is the drum rack.
constexpr int kPatternsPerTrack = 16;
constexpr int kMaxNotes = 64;
constexpr int kScenes = 16;
constexpr int kSongEntries = 64;
constexpr int8_t kEmptyPattern = -1;

enum class Grid : uint8_t { Quarter, Eighth, Sixteenth, ThirtySecond, EighthTriplet, SixteenthTriplet };

constexpr uint16_t gridTicks(Grid grid) {
    switch (grid) {
        case Grid::Quarter: return 96;
        case Grid::Eighth: return 48;
        case Grid::Sixteenth: return 24;
        case Grid::ThirtySecond: return 12;
        case Grid::EighthTriplet: return 32;
        case Grid::SixteenthTriplet: return 16;
    }
    return 24;
}

constexpr uint32_t patternTicks(uint8_t length, Grid grid) { return uint32_t{length} * gridTicks(grid); }

// Ticks that step `step` is delayed by. A pair of steps lasts 2 * stepTicks; swing is the share
// of that pair taken by the first step.
constexpr uint16_t swingOffset(uint16_t step, Grid grid, uint8_t swing) {
    const bool swingable = grid == Grid::Eighth || grid == Grid::Sixteenth || grid == Grid::ThirtySecond;
    if (!swingable || step % 2 == 0 || swing <= 50) return 0;
    const uint32_t amount = swing > 75 ? 75 : swing;
    return static_cast<uint16_t>(gridTicks(grid) * (2 * amount - 100) / 100);
}

struct DrumPattern {
    uint8_t length = 16;
    Grid grid = Grid::Sixteenth;
    uint8_t velocity[kDrumLanes][kMaxSteps] = {};  // 0 means the step is off.
};

struct Note {
    uint16_t startTick = 0;
    uint16_t lengthTicks = 0;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
};

struct InstrumentPattern {
    uint8_t length = 16;
    Grid grid = Grid::Sixteenth;
    uint8_t noteCount = 0;
    Note notes[kMaxNotes];
};

struct Scene {
    int8_t pattern[kTracks] = {kEmptyPattern, kEmptyPattern, kEmptyPattern, kEmptyPattern, kEmptyPattern};
};

struct SongEntry {
    uint8_t scene = 0;
    uint8_t repeats = 1;
};

struct Project {
    uint16_t bpm = 120;
    uint8_t swing = 50;
    bool swingEnabled[kTracks] = {true, true, true, true, true};
    DrumPattern drums[kPatternsPerTrack];
    InstrumentPattern instruments[kInstrumentTracks][kPatternsPerTrack];
    Scene scenes[kScenes];
    SongEntry song[kSongEntries];
    uint8_t songLength = 0;
};

}  // namespace gb
```

- [ ] **Step 5: Verify tests pass**

Run: `make test`
Expected: `7 tests, 0 failures`

- [ ] **Step 6: Commit**

```bash
git add Makefile components tests
git commit -m "feat: add project model with selectable grid and swing"
```

---

### Task 6: Sequencer

**Files:**
- Create: `components/gb_core/include/gb/sequencer.h`, `components/gb_core/src/sequencer.cpp`
- Test: `tests/test_sequencer.cpp`

**Interfaces:**
- Consumes: everything in `gb/model.h` (Task 5).
- Produces: `gb::Sequencer(const Project&, uint32_t sampleRate)`, `playScene(uint8_t)`, `playSong(uint8_t)`, `stop()`, `playing()`, `render(uint32_t frames, Event* out, uint32_t capacity) -> uint32_t`; `gb::Event { uint32_t frame; EventType type; uint8_t track; uint8_t data; uint8_t velocity; }`; `EventType { DrumHit, NoteOn, NoteOff, AllNotesOff }`.

- [ ] **Step 1: Write a failing test**

<!-- file: tests/test_sequencer.cpp -->
```cpp
#include <vector>

#include "gb/sequencer.h"
#include "test.h"

using namespace gb;

namespace {

constexpr uint32_t kRate = 32000;

struct Recorded {
    uint32_t frame;
    EventType type;
    uint8_t track, data;
};

// At 120 BPM and 32 kHz one quarter is exactly 16000 frames.
std::vector<Recorded> run(Sequencer& sequencer, uint32_t frames, uint32_t base = 0) {
    std::vector<Recorded> result;
    Event events[64];
    const uint32_t block = 256;
    for (uint32_t done = 0; done < frames; done += block) {
        const uint32_t count = sequencer.render(block, events, 64);
        for (uint32_t i = 0; i < count; ++i)
            result.push_back({base + done + events[i].frame, events[i].type, events[i].track, events[i].data});
    }
    return result;
}

Project& fresh() {
    static Project project;
    project = Project{};
    project.scenes[0].pattern[0] = 0;
    return project;
}

std::vector<uint32_t> framesOf(const std::vector<Recorded>& events, EventType type) {
    std::vector<uint32_t> frames;
    for (const Recorded& event : events)
        if (event.type == type) frames.push_back(event.frame);
    return frames;
}

}  // namespace

TEST(drumStepFiresOnItsFrame) {
    Project& project = fresh();
    project.drums[0].velocity[2][4] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto events = run(sequencer, 20000);
    CHECK_EQ(events.size(), 1u);
    CHECK_EQ(events[0].frame, 16000u);
    CHECK_EQ(events[0].track, 0);
    CHECK_EQ(events[0].data, 2);
}

TEST(gridChangesStepTime) {
    Project& project = fresh();
    project.drums[0].grid = Grid::Eighth;
    project.drums[0].velocity[0][1] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 10000)[0].frame, 8000u);
}

TEST(tripletGridStepTime) {
    Project& project = fresh();
    project.drums[0].grid = Grid::EighthTriplet;
    project.drums[0].velocity[0][3] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 20000)[0].frame, 16000u);
}

TEST(swingDelaysSecondStep) {
    Project& project = fresh();
    project.swing = 75;
    project.drums[0].velocity[0][1] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 8000)[0].frame, 6000u);
}

TEST(swingCanBeDisabledPerTrack) {
    Project& project = fresh();
    project.swing = 75;
    project.swingEnabled[0] = false;
    project.drums[0].velocity[0][1] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 8000)[0].frame, 4000u);
}

TEST(shortPatternLoopsInsideLongerScene) {
    Project& project = fresh();
    project.scenes[0].pattern[1] = 0;
    project.instruments[0][0].length = 16;
    project.drums[0].length = 4;
    project.drums[0].velocity[0][0] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto frames = framesOf(run(sequencer, 64000), EventType::DrumHit);
    CHECK_EQ(frames.size(), 4u);
    CHECK_EQ(frames[0], 0u);
    CHECK_EQ(frames[1], 16000u);
    CHECK_EQ(frames[3], 48000u);
}

TEST(noteOnAndOffUseTicks) {
    Project& project = fresh();
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.noteCount = 1;
    pattern.notes[0].startTick = 96;
    pattern.notes[0].lengthTicks = 48;
    pattern.notes[0].pitch = 60;
    pattern.notes[0].velocity = 90;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto events = run(sequencer, 30000);
    CHECK_EQ(framesOf(events, EventType::NoteOn)[0], 16000u);
    CHECK_EQ(framesOf(events, EventType::NoteOff)[0], 24000u);
    CHECK_EQ(events[0].track, 1);
    CHECK_EQ(events[0].data, 60);
}

TEST(swingMovesOnlyGridAlignedNotes) {
    Project& project = fresh();
    project.swing = 75;
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.noteCount = 2;
    pattern.notes[0] = {24, 6, 60, 90};
    pattern.notes[1] = {70, 6, 62, 90};
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto frames = framesOf(run(sequencer, 16000), EventType::NoteOn);
    CHECK_EQ(frames.size(), 2u);
    CHECK_EQ(frames[0], 6000u);
    CHECK_EQ(frames[1], 11667u);
}

TEST(queuedSceneStartsOnBarBoundary) {
    Project& project = fresh();
    project.scenes[1].pattern[0] = 1;
    project.drums[0].velocity[0][0] = 100;
    project.drums[1].velocity[1][0] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    auto events = run(sequencer, 256);
    sequencer.playScene(1);
    events = run(sequencer, 70000, 256);
    const auto hits = framesOf(events, EventType::DrumHit);
    CHECK_EQ(hits.size(), 1u);
    CHECK_EQ(hits[0], 64000u);
    CHECK_EQ(framesOf(events, EventType::AllNotesOff)[0], 64000u);
}

TEST(songPlaysEntriesAndStops) {
    Project& project = fresh();
    project.scenes[1].pattern[0] = 1;
    project.drums[0].length = 4;
    project.drums[0].velocity[0][0] = 100;
    project.drums[1].length = 4;
    project.drums[1].velocity[1][0] = 100;
    project.song[0] = {0, 2};
    project.song[1] = {1, 1};
    project.songLength = 2;
    Sequencer sequencer(project, kRate);
    sequencer.playSong(0);
    const auto events = run(sequencer, 60000);
    const auto hits = framesOf(events, EventType::DrumHit);
    CHECK_EQ(hits.size(), 3u);
    CHECK_EQ(hits[1], 16000u);
    CHECK_EQ(hits[2], 32000u);
    CHECK_EQ(events[2].type == EventType::AllNotesOff, true);  // Released at the scene change.
    CHECK_EQ(events[3].data, 1);
    CHECK_EQ(sequencer.playing(), false);
    CHECK_EQ(framesOf(events, EventType::AllNotesOff).back(), 48000u);
}

TEST(songStartsFromAnyEntry) {
    Project& project = fresh();
    project.scenes[1].pattern[0] = 1;
    project.drums[1].length = 4;
    project.drums[1].velocity[1][0] = 100;
    project.song[0] = {0, 1};
    project.song[1] = {1, 1};
    project.songLength = 2;
    Sequencer sequencer(project, kRate);
    sequencer.playSong(1);
    const auto events = run(sequencer, 1000);
    CHECK_EQ(events[0].data, 1);
}

TEST(tempoChangeAppliesFromCurrentPosition) {
    Project& project = fresh();
    project.drums[0].velocity[0][8] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    Event events[64];
    for (int i = 0; i < 16; ++i) sequencer.render(1000, events, 64);
    project.bpm = 240;
    uint32_t frame = 0;
    for (int i = 0; i < 16 && frame == 0; ++i) {
        const uint32_t count = sequencer.render(1000, events, 64);
        if (count > 0) frame = 16000 + 1000 * i + events[0].frame;
    }
    CHECK_EQ(frame, 24000u);
}

TEST(stopReleasesNotes) {
    Project& project = fresh();
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    run(sequencer, 256);
    sequencer.stop();
    const auto events = run(sequencer, 256);
    CHECK_EQ(events.size(), 1u);
    CHECK_EQ(events[0].type == EventType::AllNotesOff, true);
    CHECK_EQ(sequencer.playing(), false);
}

TEST(eventsBeyondCapacityAreDropped) {
    Project& project = fresh();
    for (int lane = 0; lane < kDrumLanes; ++lane) project.drums[0].velocity[lane][0] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    Event events[3];
    CHECK_EQ(sequencer.render(256, events, 3), 3u);
}
```

- [ ] **Step 2: Verify the test fails**

Run: `make test`
Expected: compilation error, `gb/sequencer.h: No such file or directory`.

- [ ] **Step 3: Implement the sequencer**

<!-- file: components/gb_core/include/gb/sequencer.h -->
```cpp
#pragma once
#include <cstdint>

#include "gb/model.h"

namespace gb {

enum class EventType : uint8_t { DrumHit, NoteOn, NoteOff, AllNotesOff };

struct Event {
    uint32_t frame;  // Offset inside the rendered block.
    EventType type;
    uint8_t track;
    uint8_t data;  // Drum lane or MIDI pitch.
    uint8_t velocity;
};

// Turns a project into sample-accurate events. Allocates nothing; call from the audio task.
class Sequencer {
public:
    Sequencer(const Project& project, uint32_t sampleRate);

    // Starts at once when stopped; when playing, switches on the next bar and leaves song mode.
    void playScene(uint8_t scene);
    // Starts the song at `entry` immediately.
    void playSong(uint8_t entry);
    void stop();
    bool playing() const { return playing_; }

    // Returns the number of events written to `out`. Events beyond `capacity` are dropped.
    uint32_t render(uint32_t frames, Event* out, uint32_t capacity);

private:
    uint64_t frameOfTick(uint64_t tick) const;
    uint32_t sceneTicks(uint8_t scene) const;
    void begin(uint8_t scene);
    bool advanceSong();
    void processTick(uint32_t frame);
    void emit(uint32_t frame, EventType type, uint8_t track, uint8_t data, uint8_t velocity);
    void emitDrums(uint32_t frame, int8_t index);
    void emitNotes(uint32_t frame, uint8_t track, int8_t index);

    const Project& project_;
    uint32_t rate_;
    uint16_t bpm_;
    uint64_t now_ = 0;
    uint64_t originFrame_ = 0;
    uint64_t originTick_ = 0;
    uint64_t nextTick_ = 0;
    uint32_t sceneTick_ = 0;
    uint8_t scene_ = 0;
    int16_t queued_ = -1;
    bool playing_ = false;
    bool songMode_ = false;
    bool releasePending_ = false;
    uint8_t songIndex_ = 0;
    uint32_t songStopTick_ = 0;
    Event* out_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t count_ = 0;
};

}  // namespace gb
```

<!-- file: components/gb_core/src/sequencer.cpp -->
```cpp
#include "gb/sequencer.h"

namespace gb {

Sequencer::Sequencer(const Project& project, uint32_t sampleRate)
    : project_(project), rate_(sampleRate), bpm_(project.bpm) {}

uint64_t Sequencer::frameOfTick(uint64_t tick) const {
    const uint64_t numerator = (tick - originTick_) * 60 * rate_;
    const uint64_t denominator = uint64_t{bpm_} * kTicksPerQuarter;
    return originFrame_ + (numerator + denominator - 1) / denominator;
}

uint32_t Sequencer::sceneTicks(uint8_t scene) const {
    const Scene& data = project_.scenes[scene];
    uint32_t longest = 0;
    if (data.pattern[0] != kEmptyPattern) {
        const DrumPattern& drums = project_.drums[data.pattern[0]];
        longest = patternTicks(drums.length, drums.grid);
    }
    for (int track = 1; track < kTracks; ++track) {
        if (data.pattern[track] == kEmptyPattern) continue;
        const InstrumentPattern& pattern = project_.instruments[track - 1][data.pattern[track]];
        const uint32_t ticks = patternTicks(pattern.length, pattern.grid);
        if (ticks > longest) longest = ticks;
    }
    return longest == 0 ? kTicksPerBar : longest;
}

void Sequencer::begin(uint8_t scene) {
    scene_ = scene;
    sceneTick_ = 0;
    queued_ = -1;
}

void Sequencer::playScene(uint8_t scene) {
    songMode_ = false;
    if (playing_) {
        queued_ = scene;
        return;
    }
    begin(scene);
    originFrame_ = now_;
    originTick_ = 0;
    nextTick_ = 0;
    bpm_ = project_.bpm;
    playing_ = true;
}

void Sequencer::playSong(uint8_t entry) {
    if (entry >= project_.songLength) return;
    if (playing_) releasePending_ = true;
    songMode_ = true;
    songIndex_ = entry;
    begin(project_.song[entry].scene);
    songStopTick_ = sceneTicks(scene_) * project_.song[entry].repeats;
    originFrame_ = now_;
    originTick_ = 0;
    nextTick_ = 0;
    bpm_ = project_.bpm;
    playing_ = true;
}

void Sequencer::stop() {
    if (!playing_) return;
    playing_ = false;
    releasePending_ = true;
    songMode_ = false;
    queued_ = -1;
}

void Sequencer::emit(uint32_t frame, EventType type, uint8_t track, uint8_t data, uint8_t velocity) {
    if (count_ < capacity_) out_[count_++] = Event{frame, type, track, data, velocity};
}

// Returns false when the song has ended.
bool Sequencer::advanceSong() {
    if (sceneTick_ < songStopTick_) return true;
    if (songIndex_ + 1 >= project_.songLength) return false;
    ++songIndex_;
    begin(project_.song[songIndex_].scene);
    songStopTick_ = sceneTicks(scene_) * project_.song[songIndex_].repeats;
    return true;
}

void Sequencer::emitDrums(uint32_t frame, int8_t index) {
    const DrumPattern& pattern = project_.drums[index];
    const uint16_t stepTicks = gridTicks(pattern.grid);
    const uint32_t position = sceneTick_ % patternTicks(pattern.length, pattern.grid);
    const uint16_t step = static_cast<uint16_t>(position / stepTicks);
    const uint16_t delay = project_.swingEnabled[0] ? swingOffset(step, pattern.grid, project_.swing) : 0;
    if (position != uint32_t{step} * stepTicks + delay) return;
    for (uint8_t lane = 0; lane < kDrumLanes; ++lane) {
        const uint8_t velocity = pattern.velocity[lane][step];
        if (velocity > 0) emit(frame, EventType::DrumHit, 0, lane, velocity);
    }
}

void Sequencer::emitNotes(uint32_t frame, uint8_t track, int8_t index) {
    const InstrumentPattern& pattern = project_.instruments[track - 1][index];
    const uint32_t total = patternTicks(pattern.length, pattern.grid);
    const uint32_t position = sceneTick_ % total;
    const uint16_t stepTicks = gridTicks(pattern.grid);
    const bool swung = project_.swingEnabled[track];

    uint32_t onTicks[kMaxNotes];
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        const Note& note = pattern.notes[i];
        const bool aligned = note.startTick % stepTicks == 0;
        onTicks[i] = note.startTick +
                     (swung && aligned ? swingOffset(note.startTick / stepTicks, pattern.grid, project_.swing) : 0);
    }
    // Note-offs go first so a note that ends where another starts can retrigger cleanly.
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        const Note& note = pattern.notes[i];
        if (note.startTick >= total) continue;
        const uint32_t length = note.lengthTicks < total ? note.lengthTicks : total - 1;
        if (position == (onTicks[i] + length) % total) emit(frame, EventType::NoteOff, track, note.pitch, 0);
    }
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        const Note& note = pattern.notes[i];
        if (note.startTick < total && position == onTicks[i] % total)
            emit(frame, EventType::NoteOn, track, note.pitch, note.velocity);
    }
}

void Sequencer::processTick(uint32_t frame) {
    if (songMode_) {
        const uint8_t before = songIndex_;
        if (!advanceSong()) {
            emit(frame, EventType::AllNotesOff, 0, 0, 0);
            playing_ = false;
            songMode_ = false;
            return;
        }
        if (songIndex_ != before) emit(frame, EventType::AllNotesOff, 0, 0, 0);
    } else if (queued_ >= 0 && sceneTick_ % kTicksPerBar == 0) {
        begin(static_cast<uint8_t>(queued_));
        emit(frame, EventType::AllNotesOff, 0, 0, 0);
    }

    const Scene& scene = project_.scenes[scene_];
    if (scene.pattern[0] != kEmptyPattern) emitDrums(frame, scene.pattern[0]);
    for (uint8_t track = 1; track < kTracks; ++track)
        if (scene.pattern[track] != kEmptyPattern) emitNotes(frame, track, scene.pattern[track]);
    ++sceneTick_;
}

uint32_t Sequencer::render(uint32_t frames, Event* out, uint32_t capacity) {
    out_ = out;
    capacity_ = capacity;
    count_ = 0;
    if (releasePending_) {
        emit(0, EventType::AllNotesOff, 0, 0, 0);
        releasePending_ = false;
    }
    const uint64_t end = now_ + frames;
    if (playing_ && project_.bpm != bpm_) {
        originFrame_ = now_;
        originTick_ = nextTick_;
        bpm_ = project_.bpm;
    }
    while (playing_) {
        const uint64_t frame = frameOfTick(nextTick_);
        if (frame >= end) break;
        processTick(static_cast<uint32_t>(frame - now_));
        ++nextTick_;
    }
    now_ = end;
    return count_;
}

}  // namespace gb
```

- [ ] **Step 4: Verify tests pass**

Run: `make test`
Expected: `21 tests, 0 failures`

- [ ] **Step 5: Commit**

```bash
git add components tests
git commit -m "feat: add sample-accurate sequencer with grids, swing, scenes and song"
```
