// Connects the music core to the board: audio on core 1, screen, keys and files on core 0.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <M5GFX.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gb/commands.h"
#include "gb/drum_ui.h"
#include "gb/engine.h"
#include "gb/project_io.h"
#include "gb/spsc.h"
#include "hardware.h"

namespace {

constexpr char kDirectory[] = "/sd/groovebox";
constexpr char kProjects[] = "/sd/groovebox/projects";
constexpr char kProjectPath[] = "/sd/groovebox/projects/default.gbp";
constexpr uint32_t kFrameMs = 50;

// The audio task owns audioProject; the interface edits uiProject and sends the same commands.
gb::Project audioProject;
gb::Project uiProject;
gb::SpscQueue<gb::Command, 32> commands;
uint8_t fileBuffer[gb::kProjectBufferSize];
int16_t outputBlock[hw::kBlockFrames * 2];
int16_t inputBlock[hw::kBlockFrames * 2];

std::atomic<int> micPeak{0};
std::atomic<uint32_t> loadPermille{0};
std::atomic<uint32_t> exchangeFailures{0};
gb::Engine* engine = nullptr;
bool storageAvailable = false;
bool saveAllowed = false;
bool loadFailed = false;

bool sendCommand(void*, const gb::Command& command) {
    while (!commands.push(command)) vTaskDelay(pdMS_TO_TICKS(1));
    return true;
}

void audioTask(void*) {
    const uint32_t available = 240000000u / hw::kSampleRate * hw::kBlockFrames;
    while (true) {
        const uint32_t start = esp_cpu_get_cycle_count();
        gb::Command command;
        while (commands.pop(command)) engine->handle(command);
        engine->render(outputBlock, hw::kBlockFrames);
        const uint32_t cycles = esp_cpu_get_cycle_count() - start;
        const uint32_t load = static_cast<uint32_t>(uint64_t{cycles} * 1000 / available);
        if (load > loadPermille.load(std::memory_order_relaxed)) loadPermille.store(load, std::memory_order_relaxed);

        if (!hw::exchangeAudio(outputBlock, inputBlock)) exchangeFailures.fetch_add(1, std::memory_order_relaxed);
        int peak = micPeak.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < hw::kBlockFrames; ++i) {
            const int level = inputBlock[i * 2] < 0 ? -inputBlock[i * 2] : inputBlock[i * 2];
            if (level > peak) peak = level;
        }
        micPeak.store(peak, std::memory_order_relaxed);
    }
}

class SpriteCanvas : public gb::Canvas {
public:
    explicit SpriteCanvas(M5Canvas& sprite) : sprite_(sprite) {}
    void fillRect(int x, int y, int w, int h, uint16_t color) override { sprite_.fillRect(x, y, w, h, color); }
    void text(int x, int y, const char* value, uint16_t color) override {
        sprite_.setTextColor(color);
        sprite_.drawString(value, x, y);
    }

private:
    M5Canvas& sprite_;
};

void pressKeys(gb::DrumUi& ui, const char* mods, char code) {
    const bool fn = strchr(mods, 'f') != nullptr;
    const bool shift = strchr(mods, 's') != nullptr;
    if (fn) ui.onKey(gb::key::Fn, true);
    if (shift) ui.onKey(gb::key::Shift, true);
    ui.onKey(code, true);
    ui.onKey(code, false);
    if (shift) ui.onKey(gb::key::Shift, false);
    if (fn) ui.onKey(gb::key::Fn, false);
}

void printState(const gb::DrumUi& ui) {
    const gb::DrumPattern& pattern = uiProject.drums[ui.currentPattern()];
    printf("STATE bpm=%u swing=%u pattern=%d length=%u grid=%u playing=%d storage=%d\n", uiProject.bpm, uiProject.swing,
           ui.currentPattern(), pattern.length, static_cast<unsigned>(pattern.grid), engine->playing() ? 1 : 0,
           storageAvailable ? 1 : 0);
    for (int lane = 0; lane < gb::kDrumLanes; ++lane) {
        char steps[gb::kMaxSteps + 1] = {};
        for (int step = 0; step < gb::kMaxSteps; ++step) steps[step] = pattern.velocity[lane][step] > 0 ? 'x' : '.';
        printf("LANE %d %s\n", lane, steps);
    }
    printf("STATE-END\n");
}

// Test console: lets automated checks drive the same code paths as the keyboard.
void runConsoleLine(gb::DrumUi& ui, char* line) {
    if (line[0] == 'K') {
        char mods[8] = {}, name[8] = {};
        if (sscanf(line, "K %7s %7s", mods, name) == 2) {
            char code = name[0];
            if (strcmp(name, "space") == 0) code = ' ';
            else if (strcmp(name, "tab") == 0) code = gb::key::Tab;
            pressKeys(ui, mods, code);
            printf("OK\n");
        }
    } else if (line[0] == 'S') {
        printState(ui);
    } else if (line[0] == 'L') {
        int ms = 0;
        if (sscanf(line, "L %d", &ms) == 1) {
            if (ms < 0) ms = 0;
            if (ms > 10000) ms = 10000;
            vTaskDelay(pdMS_TO_TICKS(50));
            micPeak.store(0, std::memory_order_relaxed);
            vTaskDelay(pdMS_TO_TICKS(ms));
            printf("LEVEL peak=%d\n", micPeak.load(std::memory_order_relaxed));
        }
    } else if (line[0] == 'C') {
        const hw::AudioStats stats = hw::audioStats();
        printf("CPU load_permille=%u tx_overflows=%u rx_overflows=%u failures=%u\n",
               static_cast<unsigned>(loadPermille.load(std::memory_order_relaxed)), static_cast<unsigned>(stats.txOverflows),
               static_cast<unsigned>(stats.rxOverflows),
               static_cast<unsigned>(exchangeFailures.load(std::memory_order_relaxed)));
    } else if (line[0] == 'X') {
        ui.clearPattern();
        printf("OK\n");
    } else if (line[0] == 'F') {
        printf("FS type=%d\n", storageAvailable ? hw::cardFilesystem() : 0);
    }
}

void pollConsole(gb::DrumUi& ui) {
    static char line[32];
    static size_t used = 0;
    uint8_t byte = 0;
    while (usb_serial_jtag_read_bytes(&byte, 1, 0) > 0) {
        if (byte == '\n' || byte == '\r') {
            line[used] = '\0';
            if (used > 0) runConsoleLine(ui, line);
            used = 0;
        } else if (used + 1 < sizeof(line)) {
            line[used++] = static_cast<char>(byte);
        }
    }
}

void loadProject() {
    gb::initProject(uiProject);
    if (storageAvailable) {
        mkdir(kDirectory, 0777);
        mkdir(kProjects, 0777);
        const gb::LoadResult result = gb::loadProjectFile(kProjectPath, uiProject, fileBuffer, sizeof(fileBuffer));
        printf("LOAD result=%d\n", static_cast<int>(result));
        loadFailed = result != gb::LoadResult::Ok && result != gb::LoadResult::NotFound;
        saveAllowed = storageAvailable && !loadFailed;
    }
    audioProject = uiProject;
}

void uiTask(void*) {
    static M5GFX display;
    static M5Canvas sprite(&display);
    display.init();
    display.setRotation(1);
    display.setBrightness(120);
    sprite.setColorDepth(16);
    sprite.createSprite(240, 135);
    sprite.setTextSize(1);
    printf("DISPLAY board=%d width=%d height=%d\n", static_cast<int>(display.getBoard()),
           static_cast<int>(display.width()), static_cast<int>(display.height()));

    SpriteCanvas canvas(sprite);
    static gb::DrumUi ui(uiProject, sendCommand, nullptr);
    if (loadFailed) ui.setWarning("LOAD ERR");
    if (!hw::initKeyboard()) printf("ERROR keyboard\n");
    printf("READY\n");

    int64_t nextFrame = 0;
    while (true) {
        hw::KeyEvent event;
        while (hw::pollKey(event)) ui.onKey(event.code, event.pressed);
        pollConsole(ui);

        if (ui.takeSaveRequest()) {
            if (saveAllowed) {
                const bool saved = gb::saveProjectFile(kProjectPath, uiProject, fileBuffer, sizeof(fileBuffer));
                printf("SAVE ok=%d\n", saved ? 1 : 0);
            } else if (storageAvailable) {
                printf("SAVE blocked\n");
            }
        }

        const int64_t now = esp_timer_get_time() / 1000;
        const bool playing = engine->playing();
        if (now >= nextFrame && (ui.takeDirty() || playing)) {
            nextFrame = now + kFrameMs;
            ui.draw(canvas, engine->sceneTick(), playing, storageAvailable);
            sprite.pushSprite(0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

extern "C" void app_main() {
    usb_serial_jtag_driver_config_t console = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&console);
    usb_serial_jtag_vfs_use_driver();

    if (!hw::initBus()) printf("ERROR i2c\n");
    if (!hw::initAudio()) printf("ERROR audio\n");
    storageAvailable = hw::mountCard();
    printf("STORAGE available=%d fs=%d\n", storageAvailable ? 1 : 0, storageAvailable ? hw::cardFilesystem() : 0);
    loadProject();

    static gb::Engine audioEngine(audioProject, hw::kSampleRate);
    engine = &audioEngine;
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 20, nullptr, 1);
    xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 5, nullptr, 0);
}
