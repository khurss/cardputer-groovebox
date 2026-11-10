// Audio owns its model; UI and files run on core 0. Queues transfer edits in both directions.
#include <M5GFX.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_cpu.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "gb/engine.h"
#include "gb/demo_projects.h"
#include "gb/project_io.h"
#include "gb/spsc.h"
#include "gb/ui.h"
#include "hardware.h"
#include "sampler_storage.h"
namespace {
constexpr char kDirectory[] = "/sd/groovebox";
constexpr char kProjects[] = "/sd/groovebox/projects";
constexpr char kDefaultPath[] = "/sd/groovebox/projects/default.gbp";
constexpr char kTestPath[] = "/sd/groovebox/projects/stage3-test.gbp";
constexpr char kTestMarker[] = "/sd/groovebox/projects/test-mode";
constexpr char kUploadPath[] = "/sd/groovebox/projects/stage3-upload.tmp";
constexpr uint32_t kFrameMs = 50;
constexpr int64_t kIdleSaveMs = 3000;
gb::Project audioProject, uiProject;
gb::SpscQueue<gb::Command, 32> commands;
gb::AudioTap audioTap;
uint32_t fftMaxUs = 0, rgbOutput = 0, rgbFailures = 0;
bool rgbReady = false;
nvs_handle_t visualNvs = 0;
uint8_t fileBuffer[gb::kProjectBufferSize];
int16_t outputBlock[hw::kBlockFrames * 2], inputBlock[hw::kBlockFrames * 2];
std::atomic<int> micPeak{0};
std::atomic<uint32_t> micPower{0}, outputPower{0}, outputPeak{0};
std::atomic<uint32_t> uiFrames{0}, uiFrameMaxUs{0};
std::atomic<uint32_t> loadPermille{0}, exchangeFailures{0}, peakVoices{0};
gb::Engine* engine = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
SemaphoreHandle_t commandMutex = nullptr;
SamplerStorage samplerStorage;
bool storageAvailable = false, saveAllowed = false, loadFailed = false, loadedBackup = false, testMode = false;
char selectedProjectPath[96] = {};
const char* projectPath = kDefaultPath;
const char* libraryDirectory = kProjects;
char currentProjectName[gb::kProjectName] = "default";
FILE* upload = nullptr;
bool uploadIsWav = false;
char wavUploadPath[112] = {}, wavFinalPath[112] = {};
size_t uploadExpected = 0, uploadWritten = 0;
void loadVisualSettings(gb::AppUi& ui) {
    // Never erase the shared NVS partition, including on initialization errors.
    if (testMode) return;
    if (nvs_flash_init() != ESP_OK || nvs_open("gb-visual", NVS_READWRITE, &visualNvs) != ESP_OK) {
        visualNvs = 0; printf("VIS settings persistence unavailable\n"); return;
    }
    uint8_t bytes[7]; size_t size=sizeof(bytes);
    if (nvs_get_blob(visualNvs,"settings",bytes,&size)==ESP_OK && size==sizeof(bytes) && bytes[0]==1) {
        gb::VisualSettings v{bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6]};
        ui.visual().setSettings(v);
    }
}

bool sendCommand(void*, const gb::Command& c) {
    xSemaphoreTake(commandMutex, portMAX_DELAY);
    while (!commands.push(c)) vTaskDelay(pdMS_TO_TICKS(1));
    xSemaphoreGive(commandMutex);
    return true;
}
void audioTask(void*) {
    const uint32_t available = 240000000u / hw::kSampleRate * hw::kBlockFrames;
    while (true) {
        const uint32_t start = esp_cpu_get_cycle_count();
        gb::Command c;
        // One full queue per block is bounded even if the producer keeps filling it.
        for (int i = 0; i < 32 && commands.pop(c); ++i) engine->handle(c);
        engine->render(outputBlock, hw::kBlockFrames);
        audioTap.capture(outputBlock, hw::kBlockFrames);
        const uint32_t cycles = esp_cpu_get_cycle_count() - start;
        const uint32_t load = static_cast<uint32_t>(uint64_t{cycles} * 1000 / available);
        if (load > loadPermille.load(std::memory_order_relaxed)) loadPermille.store(load, std::memory_order_relaxed);
        const uint32_t voices = engine->activeVoices();
        if (voices > peakVoices.load(std::memory_order_relaxed)) peakVoices.store(voices, std::memory_order_relaxed);
        if (!hw::exchangeAudio(outputBlock, inputBlock)) exchangeFailures.fetch_add(1, std::memory_order_relaxed);
        samplerStorage.capture(inputBlock);
        int peak = 0;
        uint64_t energy = 0, outEnergy = 0;
        uint32_t outPeak = 0;
        for (uint32_t i = 0; i < hw::kBlockFrames; ++i) {
            const int level = inputBlock[i * 2] < 0 ? -inputBlock[i * 2] : inputBlock[i * 2];
            energy += uint64_t(level) * level;
            const int outLevel = outputBlock[i * 2] < 0 ? -outputBlock[i * 2] : outputBlock[i * 2];
            outEnergy += uint64_t(outLevel) * outLevel;
            outPeak = std::max(outPeak, static_cast<uint32_t>(outLevel));
            if (level > peak) peak = level;
        }
        const uint32_t power = energy / hw::kBlockFrames;
        uint32_t oldPower = micPower.load(std::memory_order_relaxed);
        while (power > oldPower && !micPower.compare_exchange_weak(oldPower, power, std::memory_order_relaxed)) {
        }
        const uint32_t outPower = outEnergy / hw::kBlockFrames;
        uint32_t oldOut = outputPower.load(std::memory_order_relaxed);
        while (outPower > oldOut && !outputPower.compare_exchange_weak(oldOut, outPower, std::memory_order_relaxed)) {
        }
        oldOut = outputPeak.load(std::memory_order_relaxed);
        while (outPeak > oldOut && !outputPeak.compare_exchange_weak(oldOut, outPeak, std::memory_order_relaxed)) {
        }
        int recorded = micPeak.load(std::memory_order_relaxed);
        while (peak > recorded && !micPeak.compare_exchange_weak(recorded, peak, std::memory_order_relaxed)) {
        }
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
char keyCode(const char* name) {
    struct Named {
        const char* name;
        char code;
    };
    constexpr Named named[] = {
        {"space", ' '},        {"tab", gb::key::Tab},   {"fn", gb::key::Fn},       {"shift", gb::key::Shift},
        {"alt", gb::key::Alt}, {"ctrl", gb::key::Ctrl}, {"opt", gb::key::Opt}, {"esc", 27}, {"enter", gb::key::Enter}, {"back", gb::key::Backspace}};
    for (const auto& n : named)
        if (strcmp(name, n.name) == 0) return n.code;
    return strlen(name) == 1 ? name[0] : 0;
}
void modifiers(gb::AppUi& ui, const char* mods, bool pressed) {
    if (strchr(mods, 'f')) ui.onKey(gb::key::Fn, pressed);
    if (strchr(mods, 's')) ui.onKey(gb::key::Shift, pressed);
    if (strchr(mods, 'c')) ui.onKey(gb::key::Ctrl, pressed);
}
void printState(const gb::AppUi& ui) {
    const auto& p = uiProject.drums[ui.drums().currentPattern()];
    printf(
        "STATE bpm=%u swing=%u pattern=%d length=%u grid=%u playing=%d storage=%d screen=%u armed=%d recording=%d "
        "busy=%d test_mode=%d scene=%u queued=%u song_mode=%u song_entry=%u master=%u output_limit=%u focus_pitch=%d sampler_busy=%u sampler_recording=%u menu=%u menu_page=%u menu_row=%u menu_edit=%u menu_confirm=%u help=%u projects=%u\n",
        uiProject.bpm, uiProject.swing, ui.drums().currentPattern(), p.length, static_cast<unsigned>(p.grid),
        engine->playing() ? 1 : 0, storageAvailable ? 1 : 0, ui.screen(), ui.instrument().armed() ? 1 : 0,
        ui.instrument().recording() ? 1 : 0, ui.busy() ? 1 : 0, testMode ? 1 : 0, unsigned(engine->transport() & 15),
        unsigned((engine->transport() >> 4) & 31), unsigned((engine->transport() >> 9) & 1),
        unsigned((engine->transport() >> 10) & 63), uiProject.masterVolume, engine->outputLimit(),
        ui.instrument().focusPitch(), ui.sampler().busy(), ui.sampler().recording(), ui.menuOpen(), ui.menuPage(), ui.menuRow(), ui.menuEditing(), ui.menuConfirming(), ui.helpOpen(), ui.projectsOpen());
    for (int lane = 0; lane < gb::kDrumLanes; ++lane) {
        char steps[gb::kMaxSteps + 1] = {};
        for (int s = 0; s < gb::kMaxSteps; ++s) steps[s] = p.velocity[lane][s] ? 'x' : '.';
        printf("LANE %d %s\n", lane, steps);
    }
    printf("STATE-END\n");
}
void printInstrument(const gb::AppUi& ui) {
    const auto t = ui.instrument().track(), p = ui.instrument().pattern();
    const auto& n = uiProject.instruments[t][p];
    const auto& s = uiProject.synths[t];
    printf(
        "INSTR track=%u pattern=%u length=%u grid=%u count=%u quantize=%u wave=%u attack=%u decay=%u sustain=%u "
        "release=%u cutoff=%u resonance=%u glide=%u level=%u arp=%u arp_rate=%u arp_octaves=%u arp_gate=%u layout=%u root=%u scale=%u\n",
        t, p, n.length, static_cast<unsigned>(n.grid), n.noteCount, uiProject.recordQuantize[t] ? 1 : 0,
        static_cast<unsigned>(s.wave), s.attack, s.decay, s.sustain, s.release, s.cutoff, s.resonance, s.glide, s.level,
        unsigned(uiProject.arps[t].mode), unsigned(uiProject.arps[t].rate), uiProject.arps[t].octaves,
        uiProject.arps[t].gate, uiProject.keyboardLayout[t], uiProject.keyboardRoot[t], uiProject.keyboardScale[t]);
    for (uint8_t i = 0; i < n.noteCount; ++i) {
        const auto& note = n.notes[i];
        printf("NOTE slot=%u start=%u length=%u pitch=%u velocity=%u\n", i, note.startTick, note.lengthTicks,
               note.pitch, note.velocity);
    }
    printf("INSTR-END\n");
}
void printHash(const char* path, const char* label) {
    FILE* f = fopen(path, "rb");
    uint32_t hash = 2166136261u, size = 0;
    if (f) {
        int b;
        while ((b = fgetc(f)) != EOF) {
            hash = (hash ^ static_cast<uint8_t>(b)) * 16777619u;
            ++size;
        }
        fclose(f);
    } else
        hash = 0;
    printf("HASH %s size=%u hash=%u\n", label, static_cast<unsigned>(size), static_cast<unsigned>(hash));
}
void reboot() {
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
void runConsoleLine(gb::AppUi& ui, char* line) {
    if (line[0] == 'K' || line[0] == 'D' || line[0] == 'U') {
        char mods[8] = {}, name[12] = {};
        if (sscanf(line + 1, "%7s %11s", mods, name) == 2 && keyCode(name)) {
            const char code = keyCode(name);
            if (line[0] != 'U') {
                modifiers(ui, mods, true);
                ui.onKey(code, true);
            }
            if (line[0] != 'D') {
                ui.onKey(code, false);
                modifiers(ui, mods, false);
            }
            printf("OK\n");
        } else
            printf("ERR key\n");
    } else if (strcmp(line, "B") == 0) {
        const auto& v=ui.visual(); const auto& settings=v.settings();
        printf("VIS mode=%u palette=%u gain=%u smoothing=%u motion=%u led=%u frames=%u peak=%u bin=%u fft_max_us=%u rgb=%u rgb_ready=%u rgb_failures=%u performance=%u\n",
            settings.mode,settings.palette,settings.gain,settings.smoothing,settings.motion,settings.led,
            v.updates(),v.peak(),v.dominantBin(),unsigned(fftMaxUs),unsigned(rgbOutput),rgbReady,unsigned(rgbFailures),ui.instrument().performanceVisible());
    } else if (line[0] == 'S')
        printState(ui);
    else if (line[0] == 'I')
        printInstrument(ui);
    else if (line[0] == 'A') {
        const auto scenes=ui.arrangement().view(2,engine->playing()),song=ui.arrangement().view(3,engine->playing());
        printf("ARR song_length=%u master=%u output_limit=%u scene_first=%u scene_count=%u scene_live=%d scene_pinned=%u scene_follow=%u song_first=%u song_count=%u song_live=%d song_pinned=%u song_follow=%u edit_scene=%u edit_row=%u\n",
               uiProject.songLength,uiProject.masterVolume,engine->outputLimit(),
               scenes.first,scenes.count,scenes.live,scenes.pinned,scenes.following,
               song.first,song.count,song.live,song.pinned,song.following,ui.arrangement().scene(),ui.arrangement().songEntry());
        for (int i = 0; i < gb::kScenes; ++i) {
            printf("SCENE index=%d", i);
            for (int t = 0; t < gb::kTracks; ++t) printf(" t%d=%d", t, uiProject.scenes[i].pattern[t]);
            printf("\n");
        }
        for (int i = 0; i < uiProject.songLength; ++i)
            printf("SONG index=%d scene=%u repeats=%u\n", i, uiProject.song[i].scene, uiProject.song[i].repeats);
        for (int t = 0; t < gb::kTracks; ++t)
            printf("MIX track=%d volume=%u mute=%u solo=%u\n", t, uiProject.mixer[t].volume,
                   uiProject.mixer[t].mute ? 1 : 0, uiProject.mixer[t].solo ? 1 : 0);
        printf("ARR-END\n");
    } else if (line[0] == 'L') {
        int ms = 0;
        if (sscanf(line, "L %d", &ms) == 1) {
            ms = ms < 0 ? 0 : ms > 10000 ? 10000 : ms;
            vTaskDelay(pdMS_TO_TICKS(50));
            micPeak.store(0, std::memory_order_relaxed);
            micPower.store(0, std::memory_order_relaxed);
            outputPower.store(0);
            outputPeak.store(0);
            vTaskDelay(pdMS_TO_TICKS(ms));
            printf("LEVEL peak=%d power=%u output_peak=%u output_power=%u\n", micPeak.load(std::memory_order_relaxed),
                   unsigned(micPower.load(std::memory_order_relaxed)), unsigned(outputPeak.load()),
                   unsigned(outputPower.load()));
        }
    } else if (line[0] == 'C') {
        if (strcmp(line, "C reset") == 0) {
            loadPermille.store(0);
            peakVoices.store(0);
            uiFrames.store(0); uiFrameMaxUs.store(0);
        }
        const auto stats = hw::audioStats();
        printf(
            "CPU load_permille=%u tx_overflows=%u rx_overflows=%u failures=%u active_voices=%u peak_voices=%u "
            "event_overflows=%u heap_free=%u audio_stack_free=%u ui_stack_free=%u voice_capacity=%u voice_budget=%u ui_frames=%u ui_frame_max_us=%u\n",
            static_cast<unsigned>(loadPermille.load()), static_cast<unsigned>(stats.txOverflows),
            static_cast<unsigned>(stats.rxOverflows), static_cast<unsigned>(exchangeFailures.load()),
            static_cast<unsigned>(engine->activeVoices()), static_cast<unsigned>(peakVoices.load()),
            static_cast<unsigned>(engine->eventOverflows()), static_cast<unsigned>(esp_get_free_heap_size()),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(audioTaskHandle)),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned>(gb::kSynthVoices), unsigned(engine->voiceBudget()), unsigned(uiFrames.load()), unsigned(uiFrameMaxUs.load()));
    } else if (strcmp(line, "P") == 0) {
        printf("PROJECT name=%s count=%u available=%u\nMESSAGE %s\n", ui.library().current, ui.library().count, ui.library().available, ui.library().message);
        for (unsigned i = 0; i < ui.library().count; ++i) printf("FILE %s\n", ui.library().names[i]);
        printf("PROJECT-END\n");
    } else if (line[0] == 'J') {
        const auto& s = ui.sampler();
        printf("SAMPLER ready=%u recording=%u busy=%u frames=%u dropped=%u cache_frames=%u pattern=%u pad=%u slot=%u source_frames=%u start=%u end=%u voices=%u\n",
               samplerStorage.ready(), samplerStorage.recording(), s.busy(), unsigned(samplerStorage.recordedFrames()),
               unsigned(samplerStorage.dropped()), unsigned(samplerStorage.cacheFrames()), s.pattern(), s.pad(), s.slot(),
               unsigned(s.sourceFrames()), unsigned(s.start()), unsigned(s.end()), unsigned(engine->sampleVoices()));
        printf("SOURCE %s\nMESSAGE %s\n", s.source(), s.message());
        for (int i = 0; i < gb::kSampleSlots; ++i) if (uiProject.samples[i].source[0]) {
            const auto& c = uiProject.samples[i];
            printf("SLICE slot=%d start=%u end=%u source=%s\n", i, unsigned(c.start), unsigned(c.end), c.source);
        }
        const auto& pattern = uiProject.samplePatterns[s.pattern()];
        for (int pad = 0; pad < gb::kSamplePads; ++pad) {
            char steps[gb::kMaxSteps + 1] = {};
            for (int step = 0; step < gb::kMaxSteps; ++step) steps[step] = pattern.steps.velocity[pad][step] ? 'x' : '.';
            printf("PAD index=%d slot=%u steps=%s\n", pad, pattern.slot[pad], steps);
        }
        printf("SAMPLER-END\n");
    } else if (line[0] == 'X') {
        ui.clearPattern();
        printf("OK\n");
    } else if (line[0] == 'F')
        printf("FS type=%d\n", storageAvailable ? hw::cardFilesystem() : 0);
    else if (line[0] == 'H') {
        char primary[96], backup[100];
        gb::ProjectLibrary normal(kProjects, fileBuffer, sizeof(fileBuffer));
        char name[gb::kProjectName] = "default";
        normal.selected(name, sizeof(name)); normal.path(name, primary, sizeof(primary));
        snprintf(backup, sizeof(backup), "%s.bak", primary);
        printHash(primary, "primary"); printHash(backup, "backup");
        printf("HASH-END\n");
    } else if (line[0] == 'T') {
        if (!storageAvailable || engine->playing() || ui.busy()) {
            printf("ERR test busy\n");
            return;
        }
        if (strcmp(line, "T begin") == 0) {
            FILE* marker = fopen(kTestMarker, "wb");
            if (!marker) {
                printf("ERR marker\n");
                return;
            }
            if (fclose(marker) != 0) {
                printf("ERR marker\n");
                return;
            }
            remove("/sd/groovebox/test-projects/active.txt");
            remove("/sd/groovebox/test-projects/active.bak");
            remove(kTestPath);
            remove("/sd/groovebox/projects/stage3-test.gbp.bak");
            printf("TEST begin\n");
            reboot();
        } else if (strcmp(line, "T end") == 0) {
            if (remove(kTestMarker) != 0) {
                printf("ERR marker\n");
                return;
            }
            printf("TEST end\n");
            reboot();
        }
    } else if (line[0] == 'V' || line[0] == 'W') {
        // Fixture import is restricted to the isolated test project; arbitrary paths are not accepted.
        if (!testMode || engine->playing() || ui.busy()) {
            printf("ERR upload busy\n");
            return;
        }
        unsigned size = 0;
        if (sscanf(line + 1, " begin %u", &size) == 1 && size <= (line[0] == 'W' ? 8 * 1024 * 1024 : gb::kProjectBufferSize) && size > 0) {
            if (upload) fclose(upload);
            uploadIsWav = line[0] == 'W';
            snprintf(wavUploadPath, sizeof(wavUploadPath), "%s/fixture.wav.part", samplerStorage.directory());
            snprintf(wavFinalPath, sizeof(wavFinalPath), "%s/fixture.wav", samplerStorage.directory());
            upload = fopen(uploadIsWav ? wavUploadPath : kUploadPath, "wb");
            uploadExpected = size;
            uploadWritten = 0;
            printf(upload ? "UPLOAD ready\n" : "ERR upload open\n");
        } else if (strncmp(line + 1, " data ", 6) == 0 && upload && uploadIsWav == (line[0] == 'W')) {
            const char* hex = line + 7;
            const size_t n = strlen(hex);
            uint8_t bytes[64];
            if (n % 2 || n > 128 || uploadWritten + n / 2 > uploadExpected) {
                printf("ERR upload size\n");
                return;
            }
            for (size_t i = 0; i < n / 2; ++i) {
                char pair[] = {hex[2 * i], hex[2 * i + 1], 0};
                char* end = nullptr;
                const unsigned long v = strtoul(pair, &end, 16);
                if (end != pair + 2) {
                    printf("ERR upload hex\n");
                    return;
                }
                bytes[i] = v;
            }
            const size_t written = fwrite(bytes, 1, n / 2, upload);
            uploadWritten += written;
            printf(written == n / 2 ? "UPLOAD data\n" : "ERR upload write\n");
        } else if (strcmp(line + 1, " end") == 0 && upload && uploadIsWav == (line[0] == 'W')) {
            const bool closed = fclose(upload) == 0;
            upload = nullptr;
            bool ok = closed && uploadWritten == uploadExpected;
            if (ok && uploadIsWav) {
                FILE* wav = fopen(wavUploadPath, "rb"); gb::WavInfo info;
                ok = gb::readWavInfo(wav, info); if (wav) fclose(wav);
                if (ok) { remove(wavFinalPath); ok = rename(wavUploadPath, wavFinalPath) == 0; }
            } else if (ok) {
                remove("/sd/groovebox/test-projects/active.txt");
            remove("/sd/groovebox/test-projects/active.bak");
            remove(kTestPath);
                ok = rename(kUploadPath, kTestPath) == 0;
            }
            // The in-memory test project is older than the imported image until reboot.
            if (ok && !uploadIsWav) saveAllowed = false;
            printf(ok ? "UPLOAD done\n" : "ERR upload incomplete\n");
        } else
            printf("ERR upload command\n");
    }
}
void pollConsole(gb::AppUi& ui) {
    static char line[160];
    static size_t used = 0;
    static bool overflow = false;
    uint8_t byte = 0;
    while (usb_serial_jtag_read_bytes(&byte, 1, 0) > 0) {
        if (byte == '\n' || byte == '\r') {
            line[used] = '\0';
            if (overflow)
                printf("ERR line too long\n");
            else if (used)
                runConsoleLine(ui, line);
            used = 0;
            overflow = false;
        } else if (used + 1 < sizeof(line))
            line[used++] = static_cast<char>(byte);
        else
            overflow = true;
    }
}
void loadProject() {
    gb::initProject(uiProject);
    if (storageAvailable) {
        mkdir(kDirectory, 0777);
        mkdir(kProjects, 0777);
        struct stat marker;
        testMode = stat(kTestMarker, &marker) == 0;
        libraryDirectory = testMode ? "/sd/groovebox/test-projects" : kProjects;
        mkdir(libraryDirectory, 0777);
        gb::ProjectLibrary library(libraryDirectory, fileBuffer, sizeof(fileBuffer));
        projectPath = testMode ? kTestPath : kDefaultPath;
        if (testMode) strcpy(currentProjectName, "stage3-test");
        // Named test projects use a separate directory and startup marker.
        if (library.selected(currentProjectName, sizeof(currentProjectName)) &&
            library.path(currentProjectName, selectedProjectPath, sizeof(selectedProjectPath))) projectPath = selectedProjectPath;
        const auto result = gb::loadProjectFile(projectPath, uiProject, fileBuffer, sizeof(fileBuffer));
        printf("LOAD result=%d test_mode=%d\n", static_cast<int>(result), testMode ? 1 : 0);
        loadFailed = result != gb::LoadResult::Ok && result != gb::LoadResult::NotFound &&
                     result != gb::LoadResult::OkFromBackup;
        loadedBackup = result == gb::LoadResult::OkFromBackup;
        saveAllowed = !loadFailed && !loadedBackup;
    }
    if (testMode) uiProject.masterVolume = 5;
    audioProject = uiProject;
}
void uiTask(void*) {
    static M5GFX display;
    static M5Canvas sprite(&display);
    display.init();
    display.setRotation(0);
    display.setBrightness(120);
    // The interface uses flat colors; RGB332 halves the contiguous framebuffer
    // allocation and leaves internal memory available for real-time DSP and SD.
    sprite.setColorDepth(8);
    if (!sprite.createSprite(240, 135)) {
        printf("ERROR display allocation\n");
        vTaskDelete(nullptr);
        return;
    }
    sprite.setTextSize(1);
    // Reserve the large contiguous display buffer before the SD worker stack.
    samplerStorage.start(uiProject, *engine, sendCommand, nullptr, testMode);
    printf("DISPLAY board=%d width=%d height=%d\n", static_cast<int>(display.getBoard()),
           static_cast<int>(display.width()), static_cast<int>(display.height()));
    SpriteCanvas canvas(sprite);
    static gb::AppUi ui(uiProject, sendCommand, nullptr);
    ui.sampler().prepare();
    gb::ProjectLibrary library(libraryDirectory, fileBuffer, sizeof(fileBuffer));
    strcpy(ui.library().current, currentProjectName);
    library.scan(ui.library());
    if (loadFailed)
        ui.setWarning("LOAD ERR");
    else if (loadedBackup)
        ui.setWarning("BACKUP");
    loadVisualSettings(ui);
    display.setBrightness(255);
    rgbReady = hw::initRgb();
    display.setBrightness(ui.visual().settings().led ? 255 : 120);
    if (!rgbReady) printf("ERROR rgb initialization\n");
    if (!hw::initKeyboard()) printf("ERROR keyboard\n");
    printf("READY\n");
    int64_t nextFrame = 0, lastEditMs = 0;
    uint32_t savedEdits = ui.editCount(), seenEdits = savedEdits;
    bool lastPlaying = false, savePending = false;
    int64_t nextAnalysis = 0, visualEditMs = 0, lastAudioMs = 0;
    uint32_t visualSeen = 0, visualSaved = 0;
    uint8_t lastLed = ui.visual().settings().led;
    audioTap.request();
    while (true) {
        static gb::SampleResult sampleResult;
        while (samplerStorage.poll(sampleResult)) ui.sampler().receive(sampleResult);
        gb::SampleJob sampleJob;
        if (ui.sampler().takeJob(sampleJob) && !samplerStorage.submit(sampleJob)) {
            sampleResult.job = sampleJob; sampleResult.ok = false;
            snprintf(sampleResult.message, sizeof(sampleResult.message), "Sample I/O unavailable");
            ui.sampler().receive(sampleResult);
        }
        ui.sampler().recordingProgress(samplerStorage.recordedFrames(), samplerStorage.takePeak());
        gb::RecordUpdate update;
        while (engine->popRecordUpdate(update)) ui.receive(update);
        ui.setTransport(engine->transport());
        ui.setVoiceBudget(engine->voiceBudget());
        ui.setPosition(engine->sceneTick());
        ui.setPlayback(engine->playing());
        ui.setTime(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        hw::KeyEvent event;
        while (hw::pollKey(event)) ui.onKey(event.code, event.pressed);
        pollConsole(ui);
        // Console keyups can finish a pass; receive its edits before considering a save.
        while (engine->popRecordUpdate(update)) ui.receive(update);
        const int64_t now = esp_timer_get_time() / 1000;
        const bool playing = engine->playing();
        if (now >= nextAnalysis) {
            nextAnalysis = now + kFrameMs;
            static gb::AudioFrame visualFrame;
            if (audioTap.pop(visualFrame)) {
                const int64_t start = esp_timer_get_time();
                ui.audioFrame(visualFrame);
                fftMaxUs = std::max(fftMaxUs, uint32_t(esp_timer_get_time()-start));
                lastAudioMs = now;
            }
            audioTap.request();
            const auto led = ui.visual().settings().led;
            if (lastLed != led) {
                // Send black while power is still stable, then restore dim backlight.
                if (!led && rgbReady) { if (hw::setRgb(0)) rgbOutput = 0; else ++rgbFailures; }
                display.setBrightness(led ? 255 : 120); lastLed = led;
            }
            const uint32_t rgb = now-lastAudioMs > 250 ? 0 : ui.visual().ledRgb();
            if (rgbReady && led) {
                if (hw::setRgb(rgb)) rgbOutput = rgb; else ++rgbFailures;
            }
        }
        if (visualSeen != ui.visual().revisions()) { visualSeen = ui.visual().revisions(); visualEditMs = now; }
        if (visualNvs && !testMode && visualSaved != visualSeen && !playing && !engine->activeVoices() && !engine->sampleVoices() && !ui.busy() && now-visualEditMs >= kIdleSaveMs) {
            const auto& v = ui.visual().settings();
            uint8_t bytes[]={1,v.mode,v.palette,v.gain,v.smoothing,v.motion,v.led};
            if (nvs_set_blob(visualNvs,"settings",bytes,sizeof(bytes))==ESP_OK && nvs_commit(visualNvs)==ESP_OK) visualSaved=visualSeen;
            else { visualEditMs=now; printf("VIS settings save failed\n"); }
        }
        ui.setPlayback(playing);
        if (ui.editCount() != seenEdits) {
            seenEdits = ui.editCount();
            lastEditMs = now;
        }
        char requestedName[gb::kProjectName] = {};
        const auto request = ui.takeProjectRequest(requestedName, sizeof(requestedName));
        if (request == gb::ProjectAction::Refresh) library.scan(ui.library());
        else if (request != gb::ProjectAction::None) {
            if (playing || ui.busy() || upload || !storageAvailable) ui.projectResult("Stop transport / recording first");
            else if (request == gb::ProjectAction::Demos) {
                auto* demo = new (std::nothrow) gb::Project;
                unsigned saved = 0;
                if (demo) for (unsigned i=0;i<gb::kDemoCount;++i) {
                    gb::makeDemoProject(i,*demo);
                    if (library.save(gb::demoName(i),*demo,true)) ++saved;
                }
                delete demo;
                char result[48];
                if(saved) snprintf(result,sizeof(result),"%u demos added; existing files kept",saved);
                else snprintf(result,sizeof(result),"No new files; names exist / SD error");
                ui.projectResult(result);
                library.scan(ui.library());
            } else if (request == gb::ProjectAction::Save) {
                const bool ok = saveAllowed && gb::saveProjectFile(projectPath, uiProject, fileBuffer, sizeof(fileBuffer));
                ui.projectResult(ok ? "Project saved" : "Save failed; use Save copy");
                if (ok) { savedEdits = seenEdits; savePending = false; }
                library.scan(ui.library());
            } else {
                // Allocate only for this operation; never put a 36 KB Project on the UI stack.
                auto* candidate = new (std::nothrow) gb::Project;
                bool ok = candidate != nullptr;
                const char* message = "Not enough memory";
                if (ok && request == gb::ProjectAction::Load) {
                    const auto result = library.load(requestedName, *candidate);
                    ok = result == gb::LoadResult::Ok || result == gb::LoadResult::OkFromBackup;
                    message = "Cannot read selected project";
                } else if (ok) {
                    if (request == gb::ProjectAction::New) { gb::initProject(*candidate); candidate->masterVolume = uiProject.masterVolume; }
                    else *candidate = uiProject;
                    ok = library.save(requestedName, *candidate, true);
                    message = "Name exists or write failed";
                }
                delete candidate;
                if (ok && request == gb::ProjectAction::Copy) {
                    if (!saveAllowed && library.select(requestedName)) {
                        printf("PROJECT recovered=%s\n", requestedName); reboot();
                    }
                    ui.projectResult("Copy saved; current unchanged");
                    library.scan(ui.library());
                } else if (ok) {
                    ok = saveAllowed && gb::saveProjectFile(projectPath, uiProject, fileBuffer, sizeof(fileBuffer));
                    message = "Current save failed; kept open";
                    if (ok) {
                        // Tests cannot change the normal boot selection or user files.
                        ok = library.select(requestedName);
                        message = "Cannot save startup selection";
                    }
                    if (ok) { printf("PROJECT open=%s\n", requestedName); reboot(); }
                    else ui.projectResult(message);
                } else ui.projectResult(message);
            }
        }
        savePending = savePending || ui.takeSaveRequest();
        const bool idleDue = !playing && seenEdits != savedEdits && now - lastEditMs >= kIdleSaveMs;
        if ((savePending || idleDue) && !ui.busy() && !playing && !upload) {
            savePending = false;
            if (saveAllowed) {
                const bool saved = gb::saveProjectFile(projectPath, uiProject, fileBuffer, sizeof(fileBuffer));
                printf("SAVE ok=%d\n", saved ? 1 : 0);
                if (saved)
                    savedEdits = seenEdits;
                else
                    lastEditMs = now;
                ui.setWarning(saved ? nullptr : "SAVE ERR");
            } else if (storageAvailable) {
                printf("SAVE blocked\n");
                savedEdits = seenEdits;
            }
        }
        if (now >= nextFrame && (ui.takeDirty() || playing || playing != lastPlaying)) {
            nextFrame = now + kFrameMs;
            lastPlaying = playing;
            const int64_t drawStart = esp_timer_get_time();
            ui.draw(canvas, engine->sceneTick(), playing, storageAvailable);
            sprite.pushSprite(0, 0);
            const auto drawUs = static_cast<uint32_t>(esp_timer_get_time() - drawStart);
            uiFrames.fetch_add(1);
            if (drawUs > uiFrameMaxUs.load()) uiFrameMaxUs.store(drawUs);
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
    commandMutex = xSemaphoreCreateMutex();
    if (testMode) {
        gb::Command c;
        c.type = gb::CommandType::SetOutputLimit;
        c.value = 5;
        engine->handle(c);
    }
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 20, &audioTaskHandle, 1);
    xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 5, nullptr, 0);
}
