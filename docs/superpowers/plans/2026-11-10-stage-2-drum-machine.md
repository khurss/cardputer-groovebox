# Groovebox: этап 2, драм-машина. План реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** рабочая драм-машина на Cardputer ADV: синтез 8 ударных, экран с сеткой, запуск, смена сетки и свинга, сохранение на SD.

**Architecture:** логика остаётся в `gb_core` без зависимостей от железа и тестируется на ПК: команды, сохранение, синтез, движок, экран. Прошивка `firmware/` только связывает ядро с кодеком, клавиатурой, дисплеем и картой. Аудиозадача владеет своей копией проекта, интерфейс меняет свою и шлёт те же команды через очередь без блокировок.

**Tech Stack:** ESP-IDF v5.5.1 в Docker (образ `cbc2e2ec2afe`), M5GFX 0.3.0, C++17, g++ и make на ПК, Python 3 (только стандартная библиотека).

**Spec:** `docs/superpowers/specs/2026-11-09-groovebox-design.md`. **Заметки ревью:** `docs/superpowers/plans/stage-2-notes.md`.

## Global Constraints

- Код, звуки и название GLIDE не используются.
- Launcher (0x10000) и GLIDE (0x170000) не перезаписываются. `idf.py flash` запрещён.
- Карта microSD никогда не форматируется программой.
- Проверки на устройстве идут без участия человека: установка по кабелю, измерение микрофоном, результат по USB.
- Звук 32 кГц, 16 бит, блок 256 кадров. Аудиозадача не выделяет память.
- Время 96 тиков на четверть. Сетки 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T. Длина паттерна 4, 8 или 16. Темп 40..240. Свинг 50..75.
- Выводы: I2C SDA 8, SCL 9, ES8311 0x18, TCA8418 0x34; I2S BCK 41, WS 43, DOUT 42, DIN 46; SD SCK 40, MOSI 14, MISO 39, CS 12.
- Слот приложения: 0x260000, размер 0x180000, метка `groovebox`.

---

### Task 1: Звуки, проверка проекта, смена сетки

**Files:**
- Modify: `components/gb_core/include/gb/model.h`
- Create: `components/gb_core/include/gb/validate.h`, `components/gb_core/src/validate.cpp`
- Test: `tests/test_validate.cpp`

**Interfaces:**
- Produces: `gb::DrumSound { uint8_t tune, decay, tone, level; }`, `Project::sounds[8]`, `bool gb::isValidProject(const Project&)`, `void gb::changeDrumGrid(DrumPattern&, Grid)`.

- [ ] **Step 1: Написать падающий тест**

<!-- file: tests/test_validate.cpp -->
```cpp
#include "gb/validate.h"
#include "test.h"

using namespace gb;

namespace {
Project& fresh() {
    static Project project;
    project = Project{};
    return project;
}
}  // namespace

TEST(defaultProjectIsValid) { CHECK_EQ(isValidProject(fresh()), true); }

TEST(soundsHaveNeutralDefaults) {
    CHECK_EQ(fresh().sounds[0].tune, 64);
    CHECK_EQ(fresh().sounds[7].level, 100);
}

TEST(rejectsTempoOutOfRange) {
    Project& p = fresh();
    p.bpm = 0;
    CHECK_EQ(isValidProject(p), false);
    p.bpm = 241;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsSwingOutOfRange) {
    Project& p = fresh();
    p.swing = 49;
    CHECK_EQ(isValidProject(p), false);
    p.swing = 76;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsBadPatternLength) {
    Project& p = fresh();
    p.drums[3].length = 0;
    CHECK_EQ(isValidProject(p), false);
    p.drums[3].length = 17;
    CHECK_EQ(isValidProject(p), false);
    p.drums[3].length = 8;
    CHECK_EQ(isValidProject(p), true);
    p.instruments[2][5].length = 5;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsTooManyNotes) {
    Project& p = fresh();
    p.instruments[0][0].noteCount = kMaxNotes + 1;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsZeroLengthNote) {
    Project& p = fresh();
    p.instruments[0][0].noteCount = 1;
    p.instruments[0][0].notes[0] = {0, 0, 60, 90};
    CHECK_EQ(isValidProject(p), false);
    p.instruments[0][0].notes[0].lengthTicks = 24;
    CHECK_EQ(isValidProject(p), true);
}

TEST(rejectsBadScenePattern) {
    Project& p = fresh();
    p.scenes[1].pattern[0] = kPatternsPerTrack;
    CHECK_EQ(isValidProject(p), false);
    p.scenes[1].pattern[0] = kEmptyPattern;
    CHECK_EQ(isValidProject(p), true);
}

TEST(rejectsBadSong) {
    Project& p = fresh();
    p.songLength = kSongEntries + 1;
    CHECK_EQ(isValidProject(p), false);
    p.songLength = 1;
    p.song[0] = {kScenes, 1};
    CHECK_EQ(isValidProject(p), false);
    p.song[0] = {0, 0};
    CHECK_EQ(isValidProject(p), false);
    p.song[0] = {0, 1};
    CHECK_EQ(isValidProject(p), true);
}

TEST(coarserGridKeepsStepsThatLandOnIt) {
    DrumPattern pattern;
    pattern.velocity[1][4] = 100;  // Tick 96.
    pattern.velocity[1][1] = 100;  // Tick 24 has no place on 1/8.
    changeDrumGrid(pattern, Grid::Eighth);
    CHECK_EQ(pattern.grid == Grid::Eighth, true);
    CHECK_EQ(pattern.velocity[1][2], 100);
    CHECK_EQ(pattern.velocity[1][1], 0);
    CHECK_EQ(pattern.velocity[1][4], 0);
}

TEST(finerGridMovesStepsToSameTime) {
    DrumPattern pattern;
    pattern.grid = Grid::Eighth;
    pattern.velocity[0][1] = 77;  // Tick 48.
    changeDrumGrid(pattern, Grid::Sixteenth);
    CHECK_EQ(pattern.velocity[0][2], 77);
    CHECK_EQ(pattern.velocity[0][1], 0);
}

TEST(stepsPastTheGridAreDropped) {
    DrumPattern pattern;
    pattern.grid = Grid::Quarter;
    pattern.velocity[0][15] = 50;  // Tick 1440 needs step 120 on 1/32.
    changeDrumGrid(pattern, Grid::ThirtySecond);
    int count = 0;
    for (int step = 0; step < kMaxSteps; ++step) count += pattern.velocity[0][step] > 0;
    CHECK_EQ(count, 0);
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `make test`
Expected: ошибка компиляции, `gb/validate.h: No such file or directory`.

- [ ] **Step 3: Добавить звуки в модель**

<!-- insert-before: components/gb_core/include/gb/model.h | struct Project { -->
```cpp
struct DrumSound {
    uint8_t tune = 64;
    uint8_t decay = 64;
    uint8_t tone = 64;
    uint8_t level = 100;
};

```

<!-- insert-before: components/gb_core/include/gb/model.h |     DrumPattern drums[kPatternsPerTrack]; -->
```cpp
    DrumSound sounds[kDrumLanes];
```

- [ ] **Step 4: Реализовать проверку и смену сетки**

<!-- file: components/gb_core/include/gb/validate.h -->
```cpp
#pragma once

#include "gb/model.h"

namespace gb {

// True when every field is in range, so the audio task can use the project safely.
bool isValidProject(const Project& project);

// Sets a new grid and keeps each step at its time, dropping steps that have no place on it.
void changeDrumGrid(DrumPattern& pattern, Grid grid);

}  // namespace gb
```

<!-- file: components/gb_core/src/validate.cpp -->
```cpp
#include "gb/validate.h"

namespace gb {

namespace {

bool validLength(uint8_t length) { return length == 4 || length == 8 || length == 16; }

bool validGrid(Grid grid) { return static_cast<uint8_t>(grid) <= static_cast<uint8_t>(Grid::SixteenthTriplet); }

bool validDrums(const DrumPattern& pattern) { return validLength(pattern.length) && validGrid(pattern.grid); }

bool validInstrument(const InstrumentPattern& pattern) {
    if (!validLength(pattern.length) || !validGrid(pattern.grid) || pattern.noteCount > kMaxNotes) return false;
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        const Note& note = pattern.notes[i];
        if (note.pitch > 127 || note.velocity > 127 || note.lengthTicks == 0) return false;
    }
    return true;
}

}  // namespace

bool isValidProject(const Project& project) {
    if (project.bpm < 40 || project.bpm > 240 || project.swing < 50 || project.swing > 75) return false;
    for (const DrumPattern& pattern : project.drums)
        if (!validDrums(pattern)) return false;
    for (const auto& track : project.instruments)
        for (const InstrumentPattern& pattern : track)
            if (!validInstrument(pattern)) return false;
    for (const Scene& scene : project.scenes)
        for (int8_t index : scene.pattern)
            if (index != kEmptyPattern && (index < 0 || index >= kPatternsPerTrack)) return false;
    if (project.songLength > kSongEntries) return false;
    for (uint8_t i = 0; i < project.songLength; ++i)
        if (project.song[i].scene >= kScenes || project.song[i].repeats == 0) return false;
    return true;
}

void changeDrumGrid(DrumPattern& pattern, Grid grid) {
    const uint16_t oldTicks = gridTicks(pattern.grid);
    const uint16_t newTicks = gridTicks(grid);
    uint8_t moved[kDrumLanes][kMaxSteps] = {};
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        for (int step = 0; step < kMaxSteps; ++step) {
            if (pattern.velocity[lane][step] == 0) continue;
            const uint32_t tick = uint32_t{oldTicks} * step;
            const uint32_t target = tick / newTicks;
            if (tick % newTicks == 0 && target < kMaxSteps) moved[lane][target] = pattern.velocity[lane][step];
        }
    }
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kMaxSteps; ++step) pattern.velocity[lane][step] = moved[lane][step];
    pattern.grid = grid;
}

}  // namespace gb
```

- [ ] **Step 5: Убедиться, что тесты проходят**

Run: `make test`
Expected: `37 tests, 0 failures`

- [ ] **Step 6: Коммит**

```bash
git add components tests
git commit -m "feat: add drum sounds, project validation and grid remapping"
```

---

### Task 2: Команды и очередь

**Files:**
- Create: `components/gb_core/include/gb/spsc.h`, `components/gb_core/include/gb/commands.h`, `components/gb_core/src/commands.cpp`
- Test: `tests/test_commands.cpp`

**Interfaces:**
- Consumes: `changeDrumGrid` (Задача 1).
- Produces: `gb::SpscQueue<T, N>` с `push`/`pop`; `gb::Command`, `gb::CommandType`, `gb::SoundParam`, `void gb::applyEdit(Project&, const Command&)`, `void gb::initProject(Project&)`.

- [ ] **Step 1: Написать падающий тест**

<!-- file: tests/test_commands.cpp -->
```cpp
#include "gb/commands.h"
#include "gb/spsc.h"
#include "test.h"

using namespace gb;

namespace {
Project& fresh() {
    static Project project;
    project = Project{};
    initProject(project);
    return project;
}
Command make(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command command;
    command.type = type;
    command.a = a;
    command.b = b;
    command.c = c;
    command.value = value;
    return command;
}
}  // namespace

TEST(initProjectAssignsFirstPatternToFirstScene) { CHECK_EQ(fresh().scenes[0].pattern[0], 0); }

TEST(setStepWritesVelocity) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetStep, 3, 2, 5, 90));
    CHECK_EQ(p.drums[3].velocity[2][5], 90);
}

TEST(setLengthChangesPattern) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetLength, 1, 0, 0, 8));
    CHECK_EQ(p.drums[1].length, 8);
}

TEST(setGridRemapsSteps) {
    Project& p = fresh();
    p.drums[0].velocity[0][4] = 100;
    applyEdit(p, make(CommandType::SetGrid, 0, 0, 0, static_cast<uint16_t>(Grid::Eighth)));
    CHECK_EQ(p.drums[0].velocity[0][2], 100);
    CHECK_EQ(p.drums[0].grid == Grid::Eighth, true);
}

TEST(setTempoAndSwing) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetBpm, 0, 0, 0, 133));
    applyEdit(p, make(CommandType::SetSwing, 0, 0, 0, 62));
    CHECK_EQ(p.bpm, 133);
    CHECK_EQ(p.swing, 62);
}

TEST(setSoundChangesOneParameter) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetSound, 2, static_cast<uint8_t>(SoundParam::Decay), 0, 12));
    CHECK_EQ(p.sounds[2].decay, 12);
    CHECK_EQ(p.sounds[2].tune, 64);
}

TEST(setScenePatternSelectsPattern) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetScenePattern, 0, 0, 7));
    CHECK_EQ(p.scenes[0].pattern[0], 7);
}

TEST(setDrumPatternReplacesWholePattern) {
    Project& p = fresh();
    Command command = make(CommandType::SetDrumPattern, 4);
    command.pattern.length = 4;
    command.pattern.velocity[0][0] = 11;
    applyEdit(p, command);
    CHECK_EQ(p.drums[4].length, 4);
    CHECK_EQ(p.drums[4].velocity[0][0], 11);
}

TEST(transportCommandsDoNotEditTheProject) {
    Project& p = fresh();
    const uint16_t before = p.bpm;
    applyEdit(p, make(CommandType::Play, 0, 0, 0, 200));
    CHECK_EQ(p.bpm, before);
}

TEST(queueKeepsOrderAndReportsFull) {
    SpscQueue<int, 4> queue;
    CHECK_EQ(queue.push(1), true);
    CHECK_EQ(queue.push(2), true);
    CHECK_EQ(queue.push(3), true);
    CHECK_EQ(queue.push(4), false);  // One slot stays empty to tell full from empty.
    int value = 0;
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(value, 1);
    CHECK_EQ(queue.push(4), true);
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(value, 2);
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(value, 4);
    CHECK_EQ(queue.pop(value), false);
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `make test`
Expected: ошибка компиляции, `gb/commands.h: No such file or directory`.

- [ ] **Step 3: Реализовать команды и очередь**

<!-- file: components/gb_core/include/gb/spsc.h -->
```cpp
#pragma once

#include <atomic>
#include <cstddef>

namespace gb {

// Single-producer single-consumer queue, safe between two cores without locks.
template <typename T, size_t N>
class SpscQueue {
public:
    bool push(const T& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % N;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        items_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        value = items_[tail];
        tail_.store((tail + 1) % N, std::memory_order_release);
        return true;
    }

private:
    T items_[N];
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

}  // namespace gb
```

<!-- file: components/gb_core/include/gb/commands.h -->
```cpp
#pragma once

#include <cstdint>

#include "gb/model.h"

namespace gb {

enum class CommandType : uint8_t {
    SetStep,          // a pattern, b lane, c step, value velocity
    SetLength,        // a pattern, value length
    SetGrid,          // a pattern, value Grid
    SetBpm,           // value bpm
    SetSwing,         // value swing
    SetSound,         // a lane, b SoundParam, value amount
    SetScenePattern,  // a scene, b track, c pattern
    SetDrumPattern,   // a pattern, pattern contents
    Play,
    Stop,
    Preview,  // a lane, b velocity
};

enum class SoundParam : uint8_t { Tune, Decay, Tone, Level };

struct Command {
    CommandType type = CommandType::Stop;
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t c = 0;
    uint16_t value = 0;
    DrumPattern pattern;
};

// Gives a blank project its playable starting point.
void initProject(Project& project);

// Applies a command that edits the project; transport commands leave it unchanged.
void applyEdit(Project& project, const Command& command);

}  // namespace gb
```

<!-- file: components/gb_core/src/commands.cpp -->
```cpp
#include "gb/commands.h"

#include "gb/validate.h"

namespace gb {

void initProject(Project& project) { project.scenes[0].pattern[0] = 0; }

void applyEdit(Project& project, const Command& command) {
    switch (command.type) {
        case CommandType::SetStep:
            project.drums[command.a].velocity[command.b][command.c] = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetLength:
            project.drums[command.a].length = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetGrid:
            changeDrumGrid(project.drums[command.a], static_cast<Grid>(command.value));
            break;
        case CommandType::SetBpm:
            project.bpm = command.value;
            break;
        case CommandType::SetSwing:
            project.swing = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetSound: {
            DrumSound& sound = project.sounds[command.a];
            const auto amount = static_cast<uint8_t>(command.value);
            switch (static_cast<SoundParam>(command.b)) {
                case SoundParam::Tune: sound.tune = amount; break;
                case SoundParam::Decay: sound.decay = amount; break;
                case SoundParam::Tone: sound.tone = amount; break;
                case SoundParam::Level: sound.level = amount; break;
            }
            break;
        }
        case CommandType::SetScenePattern:
            project.scenes[command.a].pattern[command.b] = static_cast<int8_t>(command.c);
            break;
        case CommandType::SetDrumPattern:
            project.drums[command.a] = command.pattern;
            break;
        case CommandType::Play:
        case CommandType::Stop:
        case CommandType::Preview:
            break;
    }
}

}  // namespace gb
```

- [ ] **Step 4: Убедиться, что тесты проходят**

Run: `make test`
Expected: `47 tests, 0 failures`

- [ ] **Step 5: Коммит**

```bash
git add components tests
git commit -m "feat: add edit commands and lock-free queue"
```

---

### Task 3: Сохранение проекта

**Files:**
- Create: `components/gb_core/include/gb/project_io.h`, `components/gb_core/src/project_io.cpp`
- Test: `tests/test_project_io.cpp`

**Interfaces:**
- Consumes: `isValidProject` (Задача 1).
- Produces: `gb::kProjectBufferSize`, `size_t gb::serializeProject(const Project&, uint8_t*, size_t)`, `gb::LoadResult`, `LoadResult gb::deserializeProject(const uint8_t*, size_t, Project&)`, `bool gb::saveProjectFile(const char* path, const Project&, uint8_t* buffer, size_t capacity)`, `LoadResult gb::loadProjectFile(const char* path, Project&, uint8_t* buffer, size_t capacity)`.

- [ ] **Step 1: Написать падающий тест**

<!-- file: tests/test_project_io.cpp -->
```cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "gb/commands.h"
#include "gb/project_io.h"
#include "test.h"

using namespace gb;

namespace {

uint8_t buffer[kProjectBufferSize];

Project& sample() {
    static Project project;
    project = Project{};
    initProject(project);
    project.bpm = 133;
    project.swing = 62;
    project.drums[2].grid = Grid::Eighth;
    project.drums[2].length = 8;
    project.drums[2].velocity[1][3] = 99;
    project.sounds[5].decay = 17;
    project.instruments[1][4].noteCount = 1;
    project.instruments[1][4].notes[0] = {96, 48, 60, 90};
    project.scenes[3].pattern[2] = 5;
    project.song[0] = {3, 2};
    project.songLength = 1;
    return project;
}

Project& loaded() {
    static Project project;
    project = Project{};
    return project;
}

std::string tempDir() {
    char name[] = "/tmp/gbtestXXXXXX";
    return mkdtemp(name);
}

void corruptFile(const std::string& path) {
    FILE* file = fopen(path.c_str(), "r+b");
    fseek(file, 100, SEEK_SET);
    fputc(0x5A, file);
    fclose(file);
}

}  // namespace

TEST(projectSurvivesRoundTrip) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    CHECK_EQ(size > 0, true);
    Project& out = loaded();
    CHECK_EQ(deserializeProject(buffer, size, out) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
    CHECK_EQ(out.swing, 62);
    CHECK_EQ(out.drums[2].grid == Grid::Eighth, true);
    CHECK_EQ(out.drums[2].length, 8);
    CHECK_EQ(out.drums[2].velocity[1][3], 99);
    CHECK_EQ(out.sounds[5].decay, 17);
    CHECK_EQ(out.instruments[1][4].notes[0].startTick, 96);
    CHECK_EQ(out.instruments[1][4].notes[0].lengthTicks, 48);
    CHECK_EQ(out.scenes[3].pattern[2], 5);
    CHECK_EQ(out.song[0].repeats, 2);
    CHECK_EQ(out.songLength, 1);
}

TEST(serializeRefusesSmallBuffer) { CHECK_EQ(serializeProject(sample(), buffer, 10), 0u); }

TEST(detectsBadMagic) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[0] = 'X';
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::BadMagic, true);
}

TEST(detectsUnknownVersion) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[4] = 99;
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::BadVersion, true);
}

TEST(detectsTruncation) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    CHECK_EQ(deserializeProject(buffer, size - 1, loaded()) == LoadResult::BadSize, true);
    CHECK_EQ(deserializeProject(buffer, 3, loaded()) == LoadResult::BadSize, true);
}

TEST(detectsCorruption) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[size / 2] ^= 0x40;
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::BadChecksum, true);
}

TEST(refusesProjectThatFailsValidation) {
    Project bad = sample();
    bad.bpm = 0;
    const size_t size = serializeProject(bad, buffer, sizeof(buffer));
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::Invalid, true);
}

TEST(failedLoadLeavesOutputUntouched) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[size / 2] ^= 0x40;
    Project& out = loaded();
    out.bpm = 77;
    deserializeProject(buffer, size, out);
    CHECK_EQ(out.bpm, 77);
}

TEST(fileRoundTrip) {
    const std::string path = tempDir() + "/p.gbp";
    CHECK_EQ(saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer)), true);
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(saveKeepsPreviousVersionAsBackup) {
    const std::string path = tempDir() + "/p.gbp";
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    Project& changed = sample();
    changed.bpm = 90;
    saveProjectFile(path.c_str(), changed, buffer, sizeof(buffer));
    corruptFile(path);
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(loadsBackupWhenInterruptedBeforeFinalRename) {
    const std::string path = tempDir() + "/p.gbp";
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    remove(path.c_str());
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(missingFileIsReported) {
    const std::string path = tempDir() + "/none.gbp";
    CHECK_EQ(loadProjectFile(path.c_str(), loaded(), buffer, sizeof(buffer)) == LoadResult::NotFound, true);
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `make test`
Expected: ошибка компиляции, `gb/project_io.h: No such file or directory`.

- [ ] **Step 3: Реализовать сохранение**

<!-- file: components/gb_core/include/gb/project_io.h -->
```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "gb/model.h"

namespace gb {

constexpr size_t kProjectBufferSize = 32 * 1024;

enum class LoadResult : uint8_t { Ok, NotFound, BadMagic, BadVersion, BadSize, BadChecksum, Invalid };

// Writes the versioned file image. Returns its size, or 0 if the buffer is too small.
size_t serializeProject(const Project& project, uint8_t* buffer, size_t capacity);

// Fills `out` only when the whole image is good.
LoadResult deserializeProject(const uint8_t* data, size_t size, Project& out);

// Writes through a temporary file and keeps the previous save as `<path>.bak`.
bool saveProjectFile(const char* path, const Project& project, uint8_t* buffer, size_t capacity);

// Reads `path`, falling back to `<path>.bak` if it is missing or damaged.
LoadResult loadProjectFile(const char* path, Project& out, uint8_t* buffer, size_t capacity);

}  // namespace gb
```

<!-- file: components/gb_core/src/project_io.cpp -->
```cpp
#include "gb/project_io.h"

#include <cstdio>
#include <cstring>

#include "gb/validate.h"

namespace gb {

namespace {

constexpr char kMagic[4] = {'G', 'B', 'P', 'J'};
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 14;  // Magic, version, payload size, checksum.
constexpr size_t kMaxPathLength = 96;

uint32_t checksum(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void putU16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

void putU32(uint8_t* out, uint32_t value) {
    putU16(out, static_cast<uint16_t>(value));
    putU16(out + 2, static_cast<uint16_t>(value >> 16));
}

uint16_t getU16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t getU32(const uint8_t* in) { return getU16(in) | (uint32_t{getU16(in + 2)} << 16); }

// One routine both reads and writes, so the two directions cannot disagree on the layout.
class Codec {
public:
    Codec(uint8_t* data, size_t capacity, bool writing) : data_(data), capacity_(capacity), writing_(writing) {}

    void u8(uint8_t& value) {
        if (!room(1)) return;
        if (writing_) data_[used_] = value;
        else value = data_[used_];
        used_ += 1;
    }

    void i8(int8_t& value) {
        auto raw = static_cast<uint8_t>(value);
        u8(raw);
        value = static_cast<int8_t>(raw);
    }

    void u16(uint16_t& value) {
        if (!room(2)) return;
        if (writing_) putU16(data_ + used_, value);
        else value = getU16(data_ + used_);
        used_ += 2;
    }

    void grid(Grid& value) {
        auto raw = static_cast<uint8_t>(value);
        u8(raw);
        value = static_cast<Grid>(raw);
    }

    void boolean(bool& value) {
        uint8_t raw = value ? 1 : 0;
        u8(raw);
        value = raw != 0;
    }

    bool ok() const { return ok_; }
    size_t used() const { return used_; }

private:
    bool room(size_t count) {
        if (used_ + count > capacity_) ok_ = false;
        return ok_;
    }

    uint8_t* data_;
    size_t capacity_;
    size_t used_ = 0;
    bool writing_;
    bool ok_ = true;
};

void codeProject(Codec& codec, Project& project) {
    codec.u16(project.bpm);
    codec.u8(project.swing);
    for (bool& enabled : project.swingEnabled) codec.boolean(enabled);
    for (DrumSound& sound : project.sounds) {
        codec.u8(sound.tune);
        codec.u8(sound.decay);
        codec.u8(sound.tone);
        codec.u8(sound.level);
    }
    for (DrumPattern& pattern : project.drums) {
        codec.u8(pattern.length);
        codec.grid(pattern.grid);
        for (auto& lane : pattern.velocity)
            for (uint8_t& velocity : lane) codec.u8(velocity);
    }
    for (auto& track : project.instruments) {
        for (InstrumentPattern& pattern : track) {
            codec.u8(pattern.length);
            codec.grid(pattern.grid);
            codec.u8(pattern.noteCount);
            for (Note& note : pattern.notes) {
                codec.u16(note.startTick);
                codec.u16(note.lengthTicks);
                codec.u8(note.pitch);
                codec.u8(note.velocity);
            }
        }
    }
    for (Scene& scene : project.scenes)
        for (int8_t& index : scene.pattern) codec.i8(index);
    for (SongEntry& entry : project.song) {
        codec.u8(entry.scene);
        codec.u8(entry.repeats);
    }
    codec.u8(project.songLength);
}

// Keeps a large Project off the caller's stack.
Project& scratchProject() {
    static Project project;
    return project;
}

bool appendSuffix(char* out, const char* path, const char* suffix) {
    return static_cast<size_t>(snprintf(out, kMaxPathLength, "%s%s", path, suffix)) < kMaxPathLength;
}

LoadResult readImage(const char* path, Project& out, uint8_t* buffer, size_t capacity) {
    FILE* file = fopen(path, "rb");
    if (!file) return LoadResult::NotFound;
    const size_t size = fread(buffer, 1, capacity, file);
    fclose(file);
    return deserializeProject(buffer, size, out);
}

}  // namespace

size_t serializeProject(const Project& project, uint8_t* buffer, size_t capacity) {
    if (capacity < kHeaderSize) return 0;
    Project& copy = scratchProject();
    copy = project;
    Codec codec(buffer + kHeaderSize, capacity - kHeaderSize, true);
    codeProject(codec, copy);
    if (!codec.ok()) return 0;
    memcpy(buffer, kMagic, sizeof(kMagic));
    putU16(buffer + 4, kVersion);
    putU32(buffer + 6, static_cast<uint32_t>(codec.used()));
    putU32(buffer + 10, checksum(buffer + kHeaderSize, codec.used()));
    return kHeaderSize + codec.used();
}

LoadResult deserializeProject(const uint8_t* data, size_t size, Project& out) {
    if (size < kHeaderSize) return LoadResult::BadSize;
    if (memcmp(data, kMagic, sizeof(kMagic)) != 0) return LoadResult::BadMagic;
    if (getU16(data + 4) != kVersion) return LoadResult::BadVersion;
    const uint32_t payload = getU32(data + 6);
    if (payload != size - kHeaderSize) return LoadResult::BadSize;
    if (getU32(data + 10) != checksum(data + kHeaderSize, payload)) return LoadResult::BadChecksum;
    Project& decoded = scratchProject();
    decoded = Project{};
    Codec codec(const_cast<uint8_t*>(data + kHeaderSize), payload, false);
    codeProject(codec, decoded);
    if (!codec.ok() || codec.used() != payload) return LoadResult::BadSize;
    if (!isValidProject(decoded)) return LoadResult::Invalid;
    out = decoded;
    return LoadResult::Ok;
}

bool saveProjectFile(const char* path, const Project& project, uint8_t* buffer, size_t capacity) {
    char temporary[kMaxPathLength], backup[kMaxPathLength];
    if (!appendSuffix(temporary, path, ".tmp") || !appendSuffix(backup, path, ".bak")) return false;
    const size_t size = serializeProject(project, buffer, capacity);
    if (size == 0) return false;
    FILE* file = fopen(temporary, "wb");
    if (!file) return false;
    const bool written = fwrite(buffer, 1, size, file) == size;
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        remove(temporary);
        return false;
    }
    // FAT cannot rename over an existing file. Between these steps the backup stays loadable.
    FILE* existing = fopen(path, "rb");
    if (existing) {
        fclose(existing);
        remove(backup);
        if (rename(path, backup) != 0) return false;
    }
    return rename(temporary, path) == 0;
}

LoadResult loadProjectFile(const char* path, Project& out, uint8_t* buffer, size_t capacity) {
    const LoadResult primary = readImage(path, out, buffer, capacity);
    if (primary == LoadResult::Ok) return primary;
    char backup[kMaxPathLength];
    if (!appendSuffix(backup, path, ".bak")) return primary;
    const LoadResult fallback = readImage(backup, out, buffer, capacity);
    return fallback == LoadResult::Ok ? fallback : primary;
}

}  // namespace gb
```

- [ ] **Step 4: Убедиться, что тесты проходят**

Run: `make test`
Expected: `59 tests, 0 failures`

- [ ] **Step 5: Коммит**

```bash
git add components tests
git commit -m "feat: add versioned project files with backup"
```

---

### Task 4: Синтез ударных

**Files:**
- Create: `components/gb_core/include/gb/drums.h`, `components/gb_core/src/drums.cpp`, `tools/render_drums.cpp`
- Modify: `Makefile`
- Test: `tests/test_drums.cpp`

**Interfaces:**
- Consumes: `DrumSound` (Задача 1).
- Produces: `gb::DrumRack(uint32_t sampleRate)`, `void trigger(uint8_t lane, uint8_t velocity, const DrumSound&)`, `float render()`.

- [ ] **Step 1: Написать падающий тест**

<!-- file: tests/test_drums.cpp -->
```cpp
#include <cmath>
#include <vector>

#include "gb/drums.h"
#include "test.h"

using namespace gb;

namespace {

constexpr uint32_t kRate = 32000;
enum Lane { Kick, Snare, Clap, ClosedHat, OpenHat, Tom, Rim, Perc };

std::vector<float> play(uint8_t lane, const DrumSound& sound = DrumSound{}, uint32_t frames = kRate) {
    DrumRack rack(kRate);
    rack.trigger(lane, 127, sound);
    std::vector<float> output(frames);
    for (float& sample : output) sample = rack.render();
    return output;
}

float peak(const std::vector<float>& samples) {
    float highest = 0.0f;
    for (float sample : samples) highest = std::max(highest, std::fabs(sample));
    return highest;
}

int zeroCrossings(const std::vector<float>& samples, size_t count) {
    int crossings = 0;
    for (size_t i = 1; i < count && i < samples.size(); ++i)
        crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f);
    return crossings;
}

// Last frame still above a small level.
size_t ringLength(const std::vector<float>& samples) {
    size_t last = 0;
    for (size_t i = 0; i < samples.size(); ++i)
        if (std::fabs(samples[i]) > 0.01f) last = i;
    return last;
}

}  // namespace

TEST(everyLaneMakesSound) {
    for (uint8_t lane = 0; lane < 8; ++lane) CHECK_EQ(peak(play(lane)) > 0.05f, true);
}

TEST(silentWithoutTrigger) {
    DrumRack rack(kRate);
    CHECK_EQ(rack.render() == 0.0f, true);
}

TEST(everyLaneFadesOut) {
    for (uint8_t lane = 0; lane < 8; ++lane) {
        const auto samples = play(lane, DrumSound{}, kRate * 3);
        CHECK_EQ(std::fabs(samples.back()) < 0.01f, true);
    }
}

TEST(hatIsBrighterThanKick) {
    CHECK_EQ(zeroCrossings(play(ClosedHat), 1000) > zeroCrossings(play(Kick), 1000) * 5, true);
}

TEST(openHatRingsLongerThanClosed) { CHECK_EQ(ringLength(play(OpenHat)) > ringLength(play(ClosedHat)) * 2, true); }

TEST(tuneRaisesPitch) {
    DrumSound low, high;
    low.tune = 20;
    high.tune = 110;
    CHECK_EQ(zeroCrossings(play(Tom, high), 4000) > zeroCrossings(play(Tom, low), 4000), true);
}

TEST(decayLengthensSound) {
    DrumSound shortSound, longSound;
    shortSound.decay = 20;
    longSound.decay = 110;
    CHECK_EQ(ringLength(play(Snare, longSound)) > ringLength(play(Snare, shortSound)), true);
}

TEST(toneChangesNoiseBrightness) {
    DrumSound dark, bright;
    dark.tone = 10;
    bright.tone = 120;
    CHECK_EQ(zeroCrossings(play(Snare, bright), 2000) > zeroCrossings(play(Snare, dark), 2000), true);
}

TEST(levelZeroIsSilent) {
    DrumSound sound;
    sound.level = 0;
    CHECK_EQ(peak(play(Kick, sound)) == 0.0f, true);
}

TEST(lowerVelocityIsQuieter) {
    DrumRack soft(kRate), loud(kRate);
    soft.trigger(Kick, 40, DrumSound{});
    loud.trigger(Kick, 127, DrumSound{});
    float softPeak = 0.0f, loudPeak = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        softPeak = std::max(softPeak, std::fabs(soft.render()));
        loudPeak = std::max(loudPeak, std::fabs(loud.render()));
    }
    CHECK_EQ(loudPeak > softPeak, true);
}

TEST(retriggerRestartsTheVoice) {
    DrumRack rack(kRate);
    rack.trigger(Snare, 127, DrumSound{});
    for (int i = 0; i < 30000; ++i) rack.render();
    rack.trigger(Snare, 127, DrumSound{});
    float highest = 0.0f;
    for (int i = 0; i < 1000; ++i) highest = std::max(highest, std::fabs(rack.render()));
    CHECK_EQ(highest > 0.05f, true);
}

TEST(lanesPlayTogether) {
    DrumRack rack(kRate);
    rack.trigger(Kick, 127, DrumSound{});
    rack.trigger(ClosedHat, 127, DrumSound{});
    float highest = 0.0f;
    for (int i = 0; i < 2000; ++i) highest = std::max(highest, std::fabs(rack.render()));
    CHECK_EQ(highest > peak(play(ClosedHat, DrumSound{}, 2000)), true);
}

TEST(outOfRangeLaneIsIgnored) {
    DrumRack rack(kRate);
    rack.trigger(200, 127, DrumSound{});
    CHECK_EQ(rack.render() == 0.0f, true);
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `make test`
Expected: ошибка компиляции, `gb/drums.h: No such file or directory`.

- [ ] **Step 3: Реализовать синтез**

<!-- file: components/gb_core/include/gb/drums.h -->
```cpp
#pragma once

#include <cstdint>

#include "gb/model.h"

namespace gb {

// Eight synthesized drum voices. Each lane is monophonic; a new hit restarts it.
class DrumRack {
public:
    explicit DrumRack(uint32_t sampleRate);

    void trigger(uint8_t lane, uint8_t velocity, const DrumSound& sound);
    float render();

private:
    struct Voice {
        bool active = false;
        float phase = 0.0f;
        float baseStep = 0.0f;
        float pitchDepth = 0.0f;
        float pitchEnvelope = 0.0f;
        float pitchCoefficient = 0.0f;
        float amplitude = 0.0f;
        float amplitudeCoefficient = 0.0f;
        float noiseMix = 0.0f;
        float highpass = 0.0f;
        float lowState = 0.0f;
        float gain = 0.0f;
        uint32_t burstsLeft = 0;
        uint32_t burstTimer = 0;
    };

    float nextNoise();

    uint32_t rate_;
    uint32_t noiseState_ = 0x12345678u;
    Voice voices_[kDrumLanes];
};

}  // namespace gb
```

<!-- file: components/gb_core/src/drums.cpp -->
```cpp
#include "gb/drums.h"

#include <cmath>

namespace gb {

namespace {

struct Preset {
    float frequency;
    float pitchDepth;
    float pitchTime;
    float decay;
    float noise;
    float highpass;
    uint32_t bursts;
};

constexpr Preset kPresets[kDrumLanes] = {
    {48.0f, 3.2f, 0.035f, 0.32f, 0.00f, 0.00f, 0},  // Kick.
    {185.0f, 0.6f, 0.020f, 0.16f, 0.70f, 0.25f, 0},  // Snare.
    {900.0f, 0.0f, 0.010f, 0.18f, 1.00f, 0.35f, 3},  // Clap.
    {0.0f, 0.0f, 0.010f, 0.05f, 1.00f, 0.85f, 0},  // Closed hat.
    {0.0f, 0.0f, 0.010f, 0.32f, 1.00f, 0.85f, 0},  // Open hat.
    {110.0f, 0.9f, 0.060f, 0.28f, 0.05f, 0.00f, 0},  // Tom.
    {1700.0f, 0.0f, 0.010f, 0.03f, 0.25f, 0.30f, 0},  // Rim.
    {560.0f, 0.0f, 0.010f, 0.14f, 0.00f, 0.00f, 0},  // Perc.
};

constexpr float kTwoPi = 6.2831853f;
constexpr float kBurstSeconds = 0.012f;
constexpr float kSilence = 0.0005f;

float exponential(uint8_t value, float low, float high) { return low * std::pow(high / low, value / 127.0f); }

float decayCoefficient(float seconds, uint32_t rate) { return std::exp(-1.0f / (seconds * static_cast<float>(rate))); }

float atMost(float value, float limit) { return value < limit ? value : limit; }

}  // namespace

DrumRack::DrumRack(uint32_t sampleRate) : rate_(sampleRate) {}

float DrumRack::nextNoise() {
    noiseState_ ^= noiseState_ << 13;
    noiseState_ ^= noiseState_ >> 17;
    noiseState_ ^= noiseState_ << 5;
    return static_cast<float>(static_cast<int32_t>(noiseState_)) / 2147483648.0f;
}

void DrumRack::trigger(uint8_t lane, uint8_t velocity, const DrumSound& sound) {
    if (lane >= kDrumLanes) return;
    const Preset& preset = kPresets[lane];
    const float brightness = 0.5f + sound.tone / 127.0f;
    Voice& voice = voices_[lane];
    voice.phase = 0.0f;
    voice.baseStep = preset.frequency * exponential(sound.tune, 0.5f, 2.0f) / static_cast<float>(rate_);
    voice.pitchDepth = preset.pitchDepth;
    voice.pitchEnvelope = 1.0f;
    voice.pitchCoefficient = decayCoefficient(preset.pitchTime, rate_);
    voice.amplitude = 1.0f;
    voice.amplitudeCoefficient = decayCoefficient(preset.decay * exponential(sound.decay, 0.25f, 4.0f), rate_);
    voice.noiseMix = atMost(preset.noise * brightness, 1.0f);
    voice.highpass = atMost(preset.highpass * brightness, 0.95f);
    voice.lowState = 0.0f;
    voice.gain = (velocity / 127.0f) * (sound.level / 127.0f);
    voice.burstsLeft = preset.bursts;
    voice.burstTimer = static_cast<uint32_t>(kBurstSeconds * static_cast<float>(rate_));
    voice.active = true;
}

float DrumRack::render() {
    float mix = 0.0f;
    for (Voice& voice : voices_) {
        if (!voice.active) continue;
        const float noise = nextNoise();
        const float tonal = std::sin(kTwoPi * voice.phase);
        voice.phase += voice.baseStep * (1.0f + voice.pitchDepth * voice.pitchEnvelope);
        if (voice.phase >= 1.0f) voice.phase -= std::floor(voice.phase);
        voice.pitchEnvelope *= voice.pitchCoefficient;

        const float raw = tonal * (1.0f - voice.noiseMix) + noise * voice.noiseMix;
        voice.lowState += voice.highpass * (raw - voice.lowState);
        const float filtered = voice.highpass > 0.0f ? raw - voice.lowState : raw;
        mix += filtered * voice.amplitude * voice.gain;

        voice.amplitude *= voice.amplitudeCoefficient;
        if (voice.burstsLeft > 0 && --voice.burstTimer == 0) {
            voice.amplitude = 1.0f;
            --voice.burstsLeft;
            voice.burstTimer = static_cast<uint32_t>(kBurstSeconds * static_cast<float>(rate_));
        }
        if (voice.amplitude < kSilence && voice.burstsLeft == 0) voice.active = false;
    }
    return mix;
}

}  // namespace gb
```

Note on `highpass`: the output is `raw - lowState`, so a larger coefficient removes more low frequencies and sounds brighter.

- [ ] **Step 4: Убедиться, что тесты проходят**

Run: `make test`
Expected: `72 tests, 0 failures`

- [ ] **Step 5: Добавить рендер в WAV для прослушивания на компьютере**

<!-- file: tools/render_drums.cpp -->
```cpp
// Writes each drum sound one after another to a WAV file, for listening on a computer.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gb/drums.h"

namespace {

void putU32(FILE* file, uint32_t value) { fwrite(&value, 4, 1, file); }
void putU16(FILE* file, uint16_t value) { fwrite(&value, 2, 1, file); }

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: render_drums <output.wav>\n");
        return 2;
    }
    const uint32_t rate = 32000;
    std::vector<int16_t> samples;
    gb::DrumRack rack(rate);
    for (uint8_t lane = 0; lane < gb::kDrumLanes; ++lane) {
        rack.trigger(lane, 127, gb::DrumSound{});
        for (uint32_t i = 0; i < rate / 2; ++i) {
            float sample = rack.render();
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            samples.push_back(static_cast<int16_t>(sample * 32767.0f));
        }
    }
    FILE* file = fopen(argv[1], "wb");
    if (!file) {
        perror(argv[1]);
        return 1;
    }
    const auto bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    fwrite("RIFF", 1, 4, file);
    putU32(file, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, file);
    putU32(file, 16);
    putU16(file, 1);
    putU16(file, 1);
    putU32(file, rate);
    putU32(file, rate * 2);
    putU16(file, 2);
    putU16(file, 16);
    fwrite("data", 1, 4, file);
    putU32(file, bytes);
    fwrite(samples.data(), 1, bytes, file);
    fclose(file);
    return 0;
}
```

Заменить `Makefile` целиком:

<!-- file: Makefile -->
```make
CXX := g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Icomponents/gb_core/include -Itests
CORE := $(wildcard components/gb_core/src/*.cpp)
SOURCES := $(CORE) $(wildcard tests/*.cpp)
HEADERS := $(wildcard components/gb_core/include/gb/*.h) tests/test.h

.PHONY: test render clean
test: build/tests
	./build/tests

render: build/render_drums
	./build/render_drums build/drums.wav

build/tests: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

build/render_drums: tools/render_drums.cpp $(CORE) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) tools/render_drums.cpp $(CORE) -o $@

clean:
	rm -rf build
```

Run: `make render && ls -l build/drums.wav`
Expected: файл около 256 КБ.

- [ ] **Step 6: Коммит**

```bash
git add Makefile components tests tools
git commit -m "feat: add eight synthesized drum voices"
```

---

### Task 5: Движок

**Files:**
- Modify: `components/gb_core/include/gb/sequencer.h`
- Create: `components/gb_core/include/gb/engine.h`, `components/gb_core/src/engine.cpp`
- Test: `tests/test_engine.cpp`

**Interfaces:**
- Consumes: `Sequencer` (`render`, `playScene`, `stop`, `playing`), `DrumRack`, `applyEdit`, `Command`.
- Produces: `gb::Engine(Project&, uint32_t rate)`, `void handle(const Command&)`, `void render(int16_t* stereo, uint32_t frames)`, `uint32_t sceneTick() const`, `bool playing() const`; `Sequencer::sceneTick()`.

- [ ] **Step 1: Написать падающий тест**

<!-- file: tests/test_engine.cpp -->
```cpp
#include <cstdlib>
#include <vector>

#include "gb/commands.h"
#include "gb/engine.h"
#include "test.h"

using namespace gb;

namespace {

constexpr uint32_t kRate = 32000;
constexpr uint32_t kBlock = 256;

Project& fresh() {
    static Project project;
    project = Project{};
    initProject(project);
    return project;
}

Command make(CommandType type, uint8_t a = 0, uint8_t b = 0) {
    Command command;
    command.type = type;
    command.a = a;
    command.b = b;
    return command;
}

// Mono copy of the left channel; also checks that both channels match.
std::vector<int16_t> run(Engine& engine, uint32_t frames, int* channelMismatches = nullptr) {
    std::vector<int16_t> left;
    int16_t block[kBlock * 2];
    for (uint32_t done = 0; done < frames; done += kBlock) {
        engine.render(block, kBlock);
        for (uint32_t i = 0; i < kBlock; ++i) {
            left.push_back(block[i * 2]);
            if (channelMismatches && block[i * 2] != block[i * 2 + 1]) ++*channelMismatches;
        }
    }
    return left;
}

int peak(const std::vector<int16_t>& samples, size_t from = 0, size_t to = SIZE_MAX) {
    int highest = 0;
    for (size_t i = from; i < samples.size() && i < to; ++i) highest = std::max(highest, std::abs(samples[i]));
    return highest;
}

size_t firstSound(const std::vector<int16_t>& samples) {
    for (size_t i = 0; i < samples.size(); ++i)
        if (samples[i] != 0) return i;
    return SIZE_MAX;
}

}  // namespace

TEST(silentWhenStopped) {
    Engine engine(fresh(), kRate);
    CHECK_EQ(peak(run(engine, 4096)), 0);
}

TEST(stepPlaysAtItsSampleInsideBlock) {
    Project& project = fresh();
    project.drums[0].grid = Grid::ThirtySecond;
    project.drums[0].velocity[3][1] = 127;  // Tick 12 is frame 2000 at 120 BPM.
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    const auto samples = run(engine, 4096);
    CHECK_EQ(firstSound(samples), 2000u);
}

TEST(previewPlaysWhileStopped) {
    Engine engine(fresh(), kRate);
    engine.handle(make(CommandType::Preview, 1, 127));
    CHECK_EQ(peak(run(engine, 2048)) > 1000, true);
}

TEST(stopSilencesTheSequence) {
    Project& project = fresh();
    project.drums[0].length = 4;
    project.drums[0].velocity[0][0] = 127;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    run(engine, 512);
    CHECK_EQ(engine.playing(), true);
    engine.handle(make(CommandType::Stop));
    run(engine, 128000);  // Let the kick fade out.
    CHECK_EQ(engine.playing(), false);
    CHECK_EQ(peak(run(engine, 4096)), 0);
}

TEST(editsApplyToThePlayingProject) {
    Project& project = fresh();
    project.drums[0].length = 4;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    run(engine, 512);
    Command command;
    command.type = CommandType::SetStep;
    command.b = 1;
    command.c = 1;
    command.value = 127;
    engine.handle(command);
    CHECK_EQ(project.drums[0].velocity[1][1], 127);
    CHECK_EQ(peak(run(engine, 16000)) > 1000, true);
}

TEST(outputNeverClipsWithAllLanes) {
    Project& project = fresh();
    for (int lane = 0; lane < kDrumLanes; ++lane) project.drums[0].velocity[lane][0] = 127;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    const auto samples = run(engine, 8192);
    CHECK_EQ(peak(samples) > 3000, true);
    CHECK_EQ(peak(samples) <= 32767, true);
}

TEST(channelsMatch) {
    Project& project = fresh();
    project.drums[0].velocity[1][0] = 127;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    int mismatches = 0;
    run(engine, 4096, &mismatches);
    CHECK_EQ(mismatches, 0);
}

TEST(sceneTickAdvancesWhilePlaying) {
    Engine engine(fresh(), kRate);
    engine.handle(make(CommandType::Play));
    run(engine, 16128);  // 63 blocks is about 96 ticks at 120 BPM.
    CHECK_EQ(engine.sceneTick() >= 96u, true);
    CHECK_EQ(engine.sceneTick() < 100u, true);
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `make test`
Expected: ошибка компиляции, `gb/engine.h: No such file or directory`.

- [ ] **Step 3: Реализовать движок**

Добавить в `Sequencer` доступ к позиции:

<!-- insert-before: components/gb_core/include/gb/sequencer.h |     // Returns the number of events written to `out`. -->
```cpp
    uint32_t sceneTick() const { return sceneTick_; }

```

<!-- file: components/gb_core/include/gb/engine.h -->
```cpp
#pragma once

#include <atomic>
#include <cstdint>

#include "gb/commands.h"
#include "gb/drums.h"
#include "gb/sequencer.h"

namespace gb {

// Turns the project into audio. Every member except sceneTick() belongs to the audio task.
class Engine {
public:
    Engine(Project& project, uint32_t sampleRate);

    void handle(const Command& command);
    void render(int16_t* stereo, uint32_t frames);

    bool playing() const { return sequencer_.playing(); }
    uint32_t sceneTick() const { return sceneTick_.load(std::memory_order_relaxed); }

private:
    static constexpr uint32_t kMaxEvents = 64;

    int16_t limit(float sample);

    Project& project_;
    Sequencer sequencer_;
    DrumRack rack_;
    float limiterGain_ = 1.0f;
    float releaseCoefficient_;
    std::atomic<uint32_t> sceneTick_{0};
    Event events_[kMaxEvents];
};

}  // namespace gb
```

<!-- file: components/gb_core/src/engine.cpp -->
```cpp
#include "gb/engine.h"

#include <cmath>

namespace gb {

namespace {

constexpr float kMasterGain = 0.7f;
constexpr float kCeiling = 0.95f;
constexpr float kReleaseSeconds = 0.08f;

}  // namespace

Engine::Engine(Project& project, uint32_t sampleRate)
    : project_(project),
      sequencer_(project, sampleRate),
      rack_(sampleRate),
      releaseCoefficient_(1.0f - std::exp(-1.0f / (kReleaseSeconds * static_cast<float>(sampleRate)))) {}

void Engine::handle(const Command& command) {
    switch (command.type) {
        case CommandType::Play:
            sequencer_.playScene(0);
            break;
        case CommandType::Stop:
            sequencer_.stop();
            break;
        case CommandType::Preview:
            rack_.trigger(command.a, command.b, project_.sounds[command.a]);
            break;
        default:
            applyEdit(project_, command);
            break;
    }
}

int16_t Engine::limit(float sample) {
    const float level = std::fabs(sample);
    if (level * limiterGain_ > kCeiling) limiterGain_ = kCeiling / level;
    else limiterGain_ += (1.0f - limiterGain_) * releaseCoefficient_;
    return static_cast<int16_t>(sample * limiterGain_ * 32767.0f);
}

void Engine::render(int16_t* stereo, uint32_t frames) {
    const uint32_t count = sequencer_.render(frames, events_, kMaxEvents);
    uint32_t next = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        while (next < count && events_[next].frame == frame) {
            const Event& event = events_[next++];
            if (event.type == EventType::DrumHit)
                rack_.trigger(event.data, event.velocity, project_.sounds[event.data]);
        }
        const int16_t output = limit(rack_.render() * kMasterGain);
        stereo[frame * 2] = output;
        stereo[frame * 2 + 1] = output;
    }
    sceneTick_.store(sequencer_.sceneTick(), std::memory_order_relaxed);
}

}  // namespace gb
```

- [ ] **Step 4: Убедиться, что тесты проходят**

Run: `make test`
Expected: `80 tests, 0 failures`

- [ ] **Step 5: Коммит**

```bash
git add components tests
git commit -m "feat: add audio engine with sample-accurate drum triggers"
```

---

### Task 6: Экран ударных

**Files:**
- Create: `components/gb_core/include/gb/drum_ui.h`, `components/gb_core/src/drum_ui.cpp`
- Test: `tests/test_drum_ui.cpp`

**Interfaces:**
- Consumes: `Command`, `applyEdit`, модель.
- Produces: `gb::Canvas` (абстрактный), коды клавиш `gb::key::Fn, Shift, Tab`, `gb::DrumUi(Project&, CommandSink, void* context)` с `onKey(char, bool)`, `clearPattern()`, `draw(Canvas&, uint32_t sceneTick, bool playing, bool storageAvailable)`, `takeSaveRequest()`, `takeDirty()`, `selectedLane()`, `currentPattern()`.

- [ ] **Step 1: Написать падающий тест**

<!-- file: tests/test_drum_ui.cpp -->
```cpp
#include <string>
#include <vector>

#include "gb/commands.h"
#include "gb/drum_ui.h"
#include "test.h"

using namespace gb;

namespace {

struct Sent {
    std::vector<Command> commands;
};

bool sink(void* context, const Command& command) {
    static_cast<Sent*>(context)->commands.push_back(command);
    return true;
}

class FakeCanvas : public Canvas {
public:
    void fillRect(int x, int y, int w, int h, uint16_t color) override { rects.push_back({x, y, w, h, color}); }
    void text(int, int, const char* value, uint16_t) override { texts += value; texts += '\n'; }

    struct Rect {
        int x, y, w, h;
        uint16_t color;
    };
    std::vector<Rect> rects;
    std::string texts;

    int count(uint16_t color) const {
        int matches = 0;
        for (const Rect& rect : rects) matches += rect.color == color;
        return matches;
    }
};

Project& fresh() {
    static Project project;
    project = Project{};
    initProject(project);
    return project;
}

struct Fixture {
    Project& project = fresh();
    Sent sent;
    DrumUi ui{project, sink, &sent};

    void tap(char code) {
        ui.onKey(code, true);
        ui.onKey(code, false);
    }
    void withFn(char code) {
        ui.onKey(key::Fn, true);
        tap(code);
        ui.onKey(key::Fn, false);
    }
    void withShift(char code) {
        ui.onKey(key::Shift, true);
        tap(code);
        ui.onKey(key::Shift, false);
    }
    const Command& last() const { return sent.commands.back(); }
};

}  // namespace

TEST(stepKeyTogglesStepOnSelectedLane) {
    Fixture f;
    f.tap('3');
    f.tap('e');
    CHECK_EQ(f.project.drums[0].velocity[2][2], kNormalVelocity);
    CHECK_EQ(f.last().type == CommandType::SetStep, true);
    CHECK_EQ(f.last().b, 2);
    CHECK_EQ(f.last().c, 2);
    CHECK_EQ(f.last().value, kNormalVelocity);
    f.tap('e');
    CHECK_EQ(f.project.drums[0].velocity[2][2], 0);
}

TEST(secondRowMapsToLaterSteps) {
    Fixture f;
    f.tap('a');
    f.tap('k');
    CHECK_EQ(f.project.drums[0].velocity[0][8], kNormalVelocity);
    CHECK_EQ(f.project.drums[0].velocity[0][15], kNormalVelocity);
}

TEST(shiftSetsAccentAndBack) {
    Fixture f;
    f.withShift('q');
    CHECK_EQ(f.project.drums[0].velocity[0][0], kAccentVelocity);
    f.withShift('q');
    CHECK_EQ(f.project.drums[0].velocity[0][0], kNormalVelocity);
}

TEST(stepsPastPatternLengthAreIgnored) {
    Fixture f;
    f.withFn('l');  // 16 wraps to 4.
    CHECK_EQ(f.project.drums[0].length, 4);
    f.tap('t');
    CHECK_EQ(f.project.drums[0].velocity[0][4], 0);
}

TEST(laneKeySelectsAndPreviews) {
    Fixture f;
    f.tap('5');
    CHECK_EQ(f.ui.selectedLane(), 4);
    CHECK_EQ(f.last().type == CommandType::Preview, true);
    CHECK_EQ(f.last().a, 4);
}

TEST(lengthCycles) {
    Fixture f;
    f.withFn('l');
    f.withFn('l');
    CHECK_EQ(f.project.drums[0].length, 8);
    CHECK_EQ(f.last().type == CommandType::SetLength, true);
    f.withFn('l');
    CHECK_EQ(f.project.drums[0].length, 16);
}

TEST(gridCyclesAndRemaps) {
    Fixture f;
    f.tap('t');  // Step 4 is tick 96.
    f.withFn('g');
    CHECK_EQ(f.project.drums[0].grid == Grid::Eighth, true);
    CHECK_EQ(f.project.drums[0].velocity[0][2], kNormalVelocity);
    CHECK_EQ(f.last().type == CommandType::SetGrid, true);
}

TEST(swingCycles) {
    Fixture f;
    f.withFn('w');
    CHECK_EQ(f.project.swing, 54);
    for (int i = 0; i < 5; ++i) f.withFn('w');
    CHECK_EQ(f.project.swing, 75);
    f.withFn('w');
    CHECK_EQ(f.project.swing, 50);
    CHECK_EQ(f.last().type == CommandType::SetSwing, true);
}

TEST(tempoStepsAndClamps) {
    Fixture f;
    f.tap('=');
    CHECK_EQ(f.project.bpm, 121);
    f.tap('-');
    f.tap('-');
    CHECK_EQ(f.project.bpm, 119);
    f.project.bpm = 240;
    f.tap('=');
    CHECK_EQ(f.project.bpm, 240);
    f.project.bpm = 40;
    f.tap('-');
    CHECK_EQ(f.project.bpm, 40);
}

TEST(spaceStartsAndStopsAndRequestsSave) {
    Fixture f;
    f.tap(' ');
    CHECK_EQ(f.last().type == CommandType::Play, true);
    CHECK_EQ(f.ui.takeSaveRequest(), false);
    f.tap(' ');
    CHECK_EQ(f.last().type == CommandType::Stop, true);
    CHECK_EQ(f.ui.takeSaveRequest(), true);
    CHECK_EQ(f.ui.takeSaveRequest(), false);
}

TEST(patternKeysSwitchAndAssignScene) {
    Fixture f;
    f.tap(']');
    CHECK_EQ(f.ui.currentPattern(), 1);
    CHECK_EQ(f.project.scenes[0].pattern[0], 1);
    CHECK_EQ(f.last().type == CommandType::SetScenePattern, true);
    CHECK_EQ(f.last().c, 1);
    f.tap('[');
    f.tap('[');
    CHECK_EQ(f.ui.currentPattern(), 0);
}

TEST(undoRestoresLastEdit) {
    Fixture f;
    f.tap('q');
    f.tap('w');
    f.withFn('z');
    CHECK_EQ(f.project.drums[0].velocity[0][0], kNormalVelocity);
    CHECK_EQ(f.project.drums[0].velocity[0][1], 0);
    CHECK_EQ(f.last().type == CommandType::SetDrumPattern, true);
    const size_t before = f.sent.commands.size();
    f.withFn('z');  // Only one level of undo.
    CHECK_EQ(f.sent.commands.size(), before);
}

TEST(undoRestoresStepsLostToGridChange) {
    Fixture f;
    f.tap('w');  // Step 1 has no place on 1/8.
    f.withFn('g');
    CHECK_EQ(f.project.drums[0].velocity[0][1], 0);
    f.withFn('z');
    CHECK_EQ(f.project.drums[0].velocity[0][1], kNormalVelocity);
    CHECK_EQ(f.project.drums[0].grid == Grid::Sixteenth, true);
}

TEST(copyAndPastePattern) {
    Fixture f;
    f.tap('q');
    f.withFn('c');
    f.tap(']');
    f.withFn('v');
    CHECK_EQ(f.project.drums[1].velocity[0][0], kNormalVelocity);
    CHECK_EQ(f.last().type == CommandType::SetDrumPattern, true);
    CHECK_EQ(f.last().a, 1);
}

TEST(soundKeysAdjustSelectedLane) {
    Fixture f;
    f.tap('2');
    f.tap('x');
    CHECK_EQ(f.project.sounds[1].tune, 64 + kSoundStep);
    f.tap('z');
    f.tap('z');
    CHECK_EQ(f.project.sounds[1].tune, 64 - kSoundStep);
    f.tap('c');
    CHECK_EQ(f.project.sounds[1].decay, 64 - kSoundStep);
    f.tap('v');
    CHECK_EQ(f.project.sounds[1].decay, 64);
    f.tap('n');
    CHECK_EQ(f.project.sounds[1].tone, 64 + kSoundStep);
    f.tap(',');
    CHECK_EQ(f.project.sounds[1].level, 100 + kSoundStep);
    CHECK_EQ(f.last().type == CommandType::Preview, true);
}

TEST(soundValuesStayInRange) {
    Fixture f;
    f.project.sounds[0].tune = 3;
    f.tap('z');
    CHECK_EQ(f.project.sounds[0].tune, 0);
    f.project.sounds[0].tune = 125;
    f.tap('x');
    CHECK_EQ(f.project.sounds[0].tune, 127);
}

TEST(clearPatternEmptiesCurrentPattern) {
    Fixture f;
    f.tap('q');
    f.ui.clearPattern();
    CHECK_EQ(f.project.drums[0].velocity[0][0], 0);
    CHECK_EQ(f.last().type == CommandType::SetDrumPattern, true);
}

TEST(drawShowsActiveSteps) {
    Fixture f;
    f.tap('q');
    f.withShift('w');
    FakeCanvas canvas;
    f.ui.draw(canvas, 0, false, true);
    CHECK_EQ(canvas.count(color::StepOn), 1);
    CHECK_EQ(canvas.count(color::StepAccent), 1);
}

TEST(drawMarksPlayheadOnlyWhilePlaying) {
    Fixture f;
    FakeCanvas stopped, playing;
    f.ui.draw(stopped, 0, false, true);
    f.ui.draw(playing, 48, true, true);
    CHECK_EQ(stopped.count(color::Playhead), 0);
    CHECK_EQ(playing.count(color::Playhead), 1);
}

TEST(drawShowsStatus) {
    Fixture f;
    FakeCanvas canvas;
    f.ui.draw(canvas, 0, false, false);
    CHECK_EQ(canvas.texts.find("120") != std::string::npos, true);
    CHECK_EQ(canvas.texts.find("1/16") != std::string::npos, true);
    CHECK_EQ(canvas.texts.find("NO SD") != std::string::npos, true);
    CHECK_EQ(canvas.texts.find("KICK") != std::string::npos, true);
}

TEST(dirtyFlagFollowsEdits) {
    Fixture f;
    CHECK_EQ(f.ui.takeDirty(), true);  // The first frame must be drawn.
    CHECK_EQ(f.ui.takeDirty(), false);
    f.tap('q');
    CHECK_EQ(f.ui.takeDirty(), true);
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `make test`
Expected: ошибка компиляции, `gb/drum_ui.h: No such file or directory`.

- [ ] **Step 3: Реализовать экран**

<!-- file: components/gb_core/include/gb/drum_ui.h -->
```cpp
#pragma once

#include <cstdint>

#include "gb/commands.h"
#include "gb/model.h"

namespace gb {

namespace key {
constexpr char Fn = 1;
constexpr char Shift = 2;
constexpr char Ctrl = 3;
constexpr char Opt = 4;
constexpr char Alt = 5;
constexpr char Backspace = 8;
constexpr char Tab = 9;
constexpr char Enter = 10;
}  // namespace key

namespace color {
constexpr uint16_t Background = 0x0000;
constexpr uint16_t StepOff = 0x2124;
constexpr uint16_t StepOutside = 0x0841;
constexpr uint16_t StepOn = 0x07F0;
constexpr uint16_t StepAccent = 0xFFE0;
constexpr uint16_t Playhead = 0xF800;
constexpr uint16_t LaneSelected = 0x051F;
constexpr uint16_t Text = 0xFFFF;
constexpr uint16_t Warning = 0xFD20;
}  // namespace color

constexpr uint8_t kNormalVelocity = 100;
constexpr uint8_t kAccentVelocity = 127;
constexpr uint8_t kSoundStep = 8;

class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void text(int x, int y, const char* value, uint16_t color) = 0;
};

using CommandSink = bool (*)(void* context, const Command& command);

// The drum screen: turns keys into commands and draws the grid.
class DrumUi {
public:
    DrumUi(Project& project, CommandSink sink, void* context);

    void onKey(char code, bool pressed);
    void clearPattern();
    void draw(Canvas& canvas, uint32_t sceneTick, bool playing, bool storageAvailable) const;

    bool takeSaveRequest();
    bool takeDirty();
    int selectedLane() const { return lane_; }
    int currentPattern() const { return pattern_; }

private:
    void onPress(char code);
    void onFnPress(char code);
    bool toggleStep(char code);
    bool selectLane(char code);
    bool adjustSound(char code);
    void send(const Command& command);
    void apply(const Command& command);
    void rememberForUndo();
    void undo();
    void selectPattern(int pattern);
    void cycleLength();
    void cycleGrid();
    void cycleSwing();
    void changeBpm(int delta);
    void toggleTransport();
    void applyPatternContents(const DrumPattern& contents);
    DrumPattern& currentDrums() { return project_.drums[pattern_]; }

    Project& project_;
    CommandSink sink_;
    void* context_;
    int lane_ = 0;
    int pattern_ = 0;
    bool fn_ = false;
    bool shift_ = false;
    bool running_ = false;
    bool saveRequested_ = false;
    bool dirty_ = true;
    bool undoAvailable_ = false;
    bool clipboardFull_ = false;
    int undoPattern_ = 0;
    DrumPattern undoContents_;
    DrumPattern clipboard_;
};

}  // namespace gb
```

<!-- file: components/gb_core/src/drum_ui.cpp -->
```cpp
#include "gb/drum_ui.h"

#include <cstdio>
#include <cstring>

#include "gb/validate.h"

namespace gb {

namespace {

constexpr char kStepKeys[] = "qwertyuiasdfghjk";
constexpr int kCellWidth = 15;
constexpr int kCellHeight = 12;
constexpr int kGridTop = 12;
constexpr int kFooterTop = kGridTop + kCellHeight * kDrumLanes + 2;
constexpr uint8_t kSwingValues[] = {50, 54, 58, 62, 66, 70, 75};
constexpr Grid kGridCycle[] = {Grid::Sixteenth, Grid::Eighth, Grid::Quarter,
                               Grid::ThirtySecond, Grid::SixteenthTriplet, Grid::EighthTriplet};
constexpr const char* kLaneNames[kDrumLanes] = {"KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "TOM", "RIM", "PERC"};

const char* gridName(Grid grid) {
    switch (grid) {
        case Grid::Quarter: return "1/4";
        case Grid::Eighth: return "1/8";
        case Grid::Sixteenth: return "1/16";
        case Grid::ThirtySecond: return "1/32";
        case Grid::EighthTriplet: return "1/8T";
        case Grid::SixteenthTriplet: return "1/16T";
    }
    return "?";
}

int stepForKey(char code) {
    const char* found = code != '\0' ? strchr(kStepKeys, code) : nullptr;
    return found ? static_cast<int>(found - kStepKeys) : -1;
}

uint8_t stepped(uint8_t value, int delta) {
    const int result = value + delta;
    return static_cast<uint8_t>(result < 0 ? 0 : (result > 127 ? 127 : result));
}

Command command(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command out;
    out.type = type;
    out.a = a;
    out.b = b;
    out.c = c;
    out.value = value;
    return out;
}

}  // namespace

DrumUi::DrumUi(Project& project, CommandSink sink, void* context)
    : project_(project), sink_(sink), context_(context) {}

void DrumUi::send(const Command& out) {
    sink_(context_, out);
    dirty_ = true;
}

void DrumUi::apply(const Command& out) {
    applyEdit(project_, out);
    send(out);
}

void DrumUi::onKey(char code, bool pressed) {
    if (code == key::Fn) fn_ = pressed;
    else if (code == key::Shift) shift_ = pressed;
    else if (pressed) onPress(code);
}

void DrumUi::onPress(char code) {
    if (fn_) {
        onFnPress(code);
        return;
    }
    if (code == ' ') toggleTransport();
    else if (code == '[') selectPattern(pattern_ - 1);
    else if (code == ']') selectPattern(pattern_ + 1);
    else if (code == '-') changeBpm(-1);
    else if (code == '=') changeBpm(1);
    else if (!selectLane(code) && !toggleStep(code)) adjustSound(code);
}

void DrumUi::onFnPress(char code) {
    switch (code) {
        case 'l': cycleLength(); break;
        case 'g': cycleGrid(); break;
        case 'w': cycleSwing(); break;
        case 'z': undo(); break;
        case 'c':
            clipboard_ = currentDrums();
            clipboardFull_ = true;
            break;
        case 'v':
            if (clipboardFull_) {
                rememberForUndo();
                applyPatternContents(clipboard_);
            }
            break;
        default: break;
    }
}

bool DrumUi::selectLane(char code) {
    if (code < '1' || code > '8') return false;
    lane_ = code - '1';
    send(command(CommandType::Preview, static_cast<uint8_t>(lane_), kNormalVelocity));
    return true;
}

bool DrumUi::toggleStep(char code) {
    const int step = stepForKey(code);
    if (step < 0) return false;
    const DrumPattern& pattern = currentDrums();
    if (step >= pattern.length) return true;
    const uint8_t current = pattern.velocity[lane_][step];
    uint8_t next;
    if (shift_) next = current == kAccentVelocity ? kNormalVelocity : kAccentVelocity;
    else next = current == 0 ? kNormalVelocity : 0;
    rememberForUndo();
    apply(command(CommandType::SetStep, static_cast<uint8_t>(pattern_), static_cast<uint8_t>(lane_),
                  static_cast<uint8_t>(step), next));
    return true;
}

bool DrumUi::adjustSound(char code) {
    struct Binding {
        char lower, raise;
        SoundParam param;
    };
    static constexpr Binding kBindings[] = {
        {'z', 'x', SoundParam::Tune},
        {'c', 'v', SoundParam::Decay},
        {'b', 'n', SoundParam::Tone},
        {'m', ',', SoundParam::Level},
    };
    for (const Binding& binding : kBindings) {
        if (code != binding.lower && code != binding.raise) continue;
        const int delta = code == binding.raise ? kSoundStep : -static_cast<int>(kSoundStep);
        const DrumSound& sound = project_.sounds[lane_];
        uint8_t value = sound.tune;
        if (binding.param == SoundParam::Decay) value = sound.decay;
        else if (binding.param == SoundParam::Tone) value = sound.tone;
        else if (binding.param == SoundParam::Level) value = sound.level;
        apply(command(CommandType::SetSound, static_cast<uint8_t>(lane_), static_cast<uint8_t>(binding.param), 0,
                      stepped(value, delta)));
        send(command(CommandType::Preview, static_cast<uint8_t>(lane_), kNormalVelocity));
        return true;
    }
    return false;
}

void DrumUi::rememberForUndo() {
    undoContents_ = currentDrums();
    undoPattern_ = pattern_;
    undoAvailable_ = true;
}

void DrumUi::undo() {
    if (!undoAvailable_) return;
    undoAvailable_ = false;
    Command out = command(CommandType::SetDrumPattern, static_cast<uint8_t>(undoPattern_));
    out.pattern = undoContents_;
    apply(out);
}

void DrumUi::applyPatternContents(const DrumPattern& contents) {
    Command out = command(CommandType::SetDrumPattern, static_cast<uint8_t>(pattern_));
    out.pattern = contents;
    apply(out);
}

void DrumUi::clearPattern() {
    rememberForUndo();
    applyPatternContents(DrumPattern{});
}

void DrumUi::selectPattern(int pattern) {
    if (pattern < 0 || pattern >= kPatternsPerTrack) return;
    pattern_ = pattern;
    apply(command(CommandType::SetScenePattern, 0, 0, static_cast<uint8_t>(pattern_)));
}

void DrumUi::cycleLength() {
    rememberForUndo();
    const uint8_t current = currentDrums().length;
    const uint16_t next = current == 4 ? 8 : (current == 8 ? 16 : 4);
    apply(command(CommandType::SetLength, static_cast<uint8_t>(pattern_), 0, 0, next));
}

void DrumUi::cycleGrid() {
    rememberForUndo();
    const size_t count = sizeof(kGridCycle) / sizeof(kGridCycle[0]);
    size_t index = 0;
    while (index < count && kGridCycle[index] != currentDrums().grid) ++index;
    const Grid next = kGridCycle[(index + 1) % count];
    apply(command(CommandType::SetGrid, static_cast<uint8_t>(pattern_), 0, 0, static_cast<uint16_t>(next)));
}

void DrumUi::cycleSwing() {
    const size_t count = sizeof(kSwingValues);
    size_t index = 0;
    while (index < count && kSwingValues[index] != project_.swing) ++index;
    apply(command(CommandType::SetSwing, 0, 0, 0, kSwingValues[(index + 1) % count]));
}

void DrumUi::changeBpm(int delta) {
    const int next = project_.bpm + delta;
    if (next < 40 || next > 240) return;
    apply(command(CommandType::SetBpm, 0, 0, 0, static_cast<uint16_t>(next)));
}

void DrumUi::toggleTransport() {
    running_ = !running_;
    send(command(running_ ? CommandType::Play : CommandType::Stop));
    if (!running_) saveRequested_ = true;
}

bool DrumUi::takeSaveRequest() {
    const bool requested = saveRequested_;
    saveRequested_ = false;
    return requested;
}

bool DrumUi::takeDirty() {
    const bool dirty = dirty_;
    dirty_ = false;
    return dirty;
}

void DrumUi::draw(Canvas& canvas, uint32_t sceneTick, bool playing, bool storageAvailable) const {
    const DrumPattern& pattern = project_.drums[pattern_];
    canvas.fillRect(0, 0, 240, 135, color::Background);

    char line[64];
    snprintf(line, sizeof(line), "P%02d %3uBPM %s x%u SW%u %s", pattern_ + 1, project_.bpm, gridName(pattern.grid),
             pattern.length, project_.swing, playing ? "PLAY" : "STOP");
    canvas.text(0, 2, line, color::Text);
    if (!storageAvailable) canvas.text(198, 2, "NO SD", color::Warning);

    const uint32_t stepTicks = gridTicks(pattern.grid);
    const int playStep = playing ? static_cast<int>((sceneTick % patternTicks(pattern.length, pattern.grid)) / stepTicks) : -1;

    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = kGridTop + lane * kCellHeight;
        if (lane == lane_) canvas.fillRect(0, y, 240, kCellHeight, color::LaneSelected);
        for (int step = 0; step < kMaxSteps; ++step) {
            const uint8_t velocity = pattern.velocity[lane][step];
            uint16_t fill = color::StepOff;
            if (step >= pattern.length) fill = color::StepOutside;
            else if (velocity >= kAccentVelocity) fill = color::StepAccent;
            else if (velocity > 0) fill = color::StepOn;
            canvas.fillRect(step * kCellWidth + 1, y + 1, kCellWidth - 2, kCellHeight - 2, fill);
        }
    }
    if (playStep >= 0)
        canvas.fillRect(playStep * kCellWidth, kGridTop, 1, kCellHeight * kDrumLanes, color::Playhead);

    const DrumSound& sound = project_.sounds[lane_];
    snprintf(line, sizeof(line), "%s", kLaneNames[lane_]);
    canvas.text(0, kFooterTop, line, color::Text);
    snprintf(line, sizeof(line), "TUNE %u DEC %u TONE %u LVL %u", sound.tune, sound.decay, sound.tone, sound.level);
    canvas.text(0, kFooterTop + 10, line, color::Text);
}

}  // namespace gb
```

- [ ] **Step 4: Убедиться, что тесты проходят**

Run: `make test`
Expected: `101 tests, 0 failures`

- [ ] **Step 5: Коммит**

```bash
git add components tests
git commit -m "feat: add drum screen logic and drawing"
```

---

### Task 7: Прошивка

**Files:**
- Modify: `.gitignore`, `tools/slot.py`, `tools/test_slot.py`
- Create: `components/gb_core/CMakeLists.txt`, `firmware/CMakeLists.txt`, `firmware/sdkconfig.defaults`, `firmware/partitions.csv`, `firmware/main/CMakeLists.txt`, `firmware/main/idf_component.yml`, `firmware/main/hardware.h`, `firmware/main/hardware.cpp`, `firmware/main/main.cpp`

**Interfaces:**
- Consumes: всё из `gb_core`.
- Produces: `firmware/build/groovebox.bin`; `tools/slot.py install-app <bin> | remove-app`; консоль USB с командами `K <мод> <клавиша>`, `S`, `L <мс>`, `C`, `X`, `F`.

- [ ] **Step 1: Дополнить `.gitignore`**

<!-- insert-before: .gitignore | .tmp/ -->
```gitignore
firmware/components/fatfs/
```

- [ ] **Step 2: Написать падающие тесты установщика**

<!-- insert-before: tools/test_slot.py | if __name__ == "__main__": -->
```python
class AppSlotTests(unittest.TestCase):
    def test_add_app_uses_its_slot(self):
        entries = slot.add_app(DEVICE)
        fields = struct.unpack("<HBBII16sI", entries[-1])
        self.assertEqual(fields[1:5], (0, 0x11, 0x260000, 0x180000))
        self.assertEqual(slot.label(entries[-1]), "groovebox")

    def test_add_app_is_idempotent(self):
        self.assertEqual(slot.add_app(slot.add_app(DEVICE)), slot.add_app(DEVICE))

    def test_add_app_rejects_overlap(self):
        taken = DEVICE + [entry(1, 0x82, 0x300000, 0x1000, "other")]
        with self.assertRaises(ValueError):
            slot.add_app(taken)

    def test_remove_app_restores_table(self):
        self.assertEqual(slot.remove_app(slot.add_app(DEVICE)), DEVICE)

    def test_install_app_arguments_are_checked(self):
        with tempfile.NamedTemporaryFile() as image:
            image.write(b"\xe9rest")
            image.flush()
            slot.validate_args(["slot.py", "install-app", image.name])
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "install-app"])
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "install-app", "/nonexistent/path"])
        slot.validate_args(["slot.py", "remove-app"])


```

If `tempfile` is not yet imported in `tools/test_slot.py`, add `import tempfile` with the other imports.

Run: `python3 tools/test_slot.py`
Expected: FAIL, `AttributeError: module 'slot' has no attribute 'add_app'`.

- [ ] **Step 3: Добавить слот приложения в `slot.py`**

Add next to the other constants:

```python
APP_OFFSET = 0x260000
APP_SIZE = 0x180000
APP_LABEL = "groovebox"
```

Add next to `remove_probe`:

```python
def remove_app(entries):
    return [e for e in entries if label(e) != APP_LABEL]


def add_app(entries):
    kept = remove_app(entries)
    subtypes = []
    for e in kept:
        _, type_, subtype, offset, size, _, _ = struct.unpack(FORMAT, e)
        if offset < APP_OFFSET + APP_SIZE and APP_OFFSET < offset + size:
            raise ValueError(f"app slot overlaps partition {label(e)}")
        if type_ == 0 and 0x10 <= subtype <= 0x1F:
            subtypes.append(subtype)
    next_subtype = max(subtypes, default=0x0F) + 1
    return kept + [struct.pack(FORMAT, 0x50AA, 0, next_subtype, APP_OFFSET, APP_SIZE, APP_LABEL.encode(), 0)]
```

In `validate_args`, treat `install-app` exactly like `install` (same path and 0xE9 checks) and accept `remove-app` with no further arguments.

In `main`, add two branches in the existing command dispatch:

```python
        elif argv[1] == "install-app":
            app = Path(argv[2]).read_bytes()
            if len(app) > APP_SIZE:
                sys.exit("app image does not fit its slot")
            table = add_app(remove_probe(entries))
            write(work, APP_OFFSET, app)
            write(work, TABLE_OFFSET, build(table))
            write(work, OTADATA_OFFSET, otadata_image(ota_index(table, APP_LABEL)))
        elif argv[1] == "remove-app":
            write(work, OTADATA_OFFSET, otadata_image(ota_index(entries, "glide")))
            write(work, TABLE_OFFSET, build(remove_app(entries)))
```

Run: `python3 tools/test_slot.py`
Expected: `OK`

- [ ] **Step 4: Создать конфигурацию сборки**

<!-- file: components/gb_core/CMakeLists.txt -->
```cmake
idf_component_register(
    SRCS "src/commands.cpp" "src/drum_ui.cpp" "src/drums.cpp" "src/engine.cpp"
         "src/project_io.cpp" "src/sequencer.cpp" "src/validate.cpp"
    INCLUDE_DIRS "include"
)
```

<!-- file: firmware/CMakeLists.txt -->
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(groovebox)
```

<!-- file: firmware/sdkconfig.defaults -->
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

<!-- file: firmware/partitions.csv -->
```text
nvs,      data, nvs,    0x9000,   0x5000,
otadata,  data, ota,    0xE000,   0x2000,
app0,     app,  ota_0,  0x10000,  0x180000,
```

<!-- file: firmware/main/CMakeLists.txt -->
```cmake
idf_component_register(
    SRCS "main.cpp" "hardware.cpp"
    INCLUDE_DIRS "."
)
```

<!-- file: firmware/main/idf_component.yml -->
```yaml
dependencies:
  idf: ">=5.1"
  m5stack/m5gfx: "0.3.0"
```

- [ ] **Step 5: Написать слой железа**

<!-- file: firmware/main/hardware.h -->
```cpp
#pragma once

#include <cstdint>

// Everything that touches the board. Nothing here knows about music.
namespace hw {

constexpr uint32_t kSampleRate = 32000;
constexpr uint32_t kBlockFrames = 256;

struct AudioStats {
    uint32_t txOverflows;
    uint32_t rxOverflows;
};

bool initBus();
bool initAudio();
bool initKeyboard();
// Moves one audio block out and one in. Both buffers hold kBlockFrames stereo frames.
bool exchangeAudio(const int16_t* output, int16_t* input);
AudioStats audioStats();

struct KeyEvent {
    char code;
    bool pressed;
};
// Returns true and fills `event` when a key changed.
bool pollKey(KeyEvent& event);

bool mountCard();
// FatFs type number of the mounted card, 0 when unknown (2 FAT16, 3 FAT32, 4 exFAT).
int cardFilesystem();

}  // namespace hw
```

<!-- file: firmware/main/hardware.cpp -->
```cpp
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
    {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', gb::key::Backspace},
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
    static const uint8_t init[][2] = {
        {0x00, 0x80}, {0x01, 0xBF}, {0x02, 0x18}, {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00},
        {0x13, 0x10}, {0x14, 0x18}, {0x17, 0xBF}, {0x1C, 0x6A}, {0x32, 0xBF}, {0x37, 0x08},
    };
    bool ok = true;
    for (const auto& item : init) ok = writeRegister(codec, item[0], item[1]) && ok;
    // The capture path arms only on a 0x0D write made while the I2S clock is running.
    for (int i = 0; i < 3; ++i) {
        vTaskDelay(pdMS_TO_TICKS(30));
        ok = writeRegister(codec, 0x0D, 0x01) && ok;
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

bool pollKey(KeyEvent& event) {
    uint8_t count = 0;
    if (!readRegister(keyboard, kRegEventCount, count) || (count & 0x0F) == 0) return false;
    uint8_t raw = 0;
    if (!readRegister(keyboard, kRegEventA, raw) || raw == 0) return false;
    writeRegister(keyboard, kRegIntStat, 0x01);
    const int code = (raw & 0x7F) - 1;
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
```

- [ ] **Step 6: Написать приложение**

<!-- file: firmware/main/main.cpp -->
```cpp
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

bool sendCommand(void*, const gb::Command& command) { return commands.push(command); }

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
    if (!hw::initKeyboard()) printf("ERROR keyboard\n");
    printf("READY\n");

    int64_t nextFrame = 0;
    while (true) {
        hw::KeyEvent event;
        while (hw::pollKey(event)) ui.onKey(event.code, event.pressed);
        pollConsole(ui);

        if (ui.takeSaveRequest() && storageAvailable) {
            const bool saved = gb::saveProjectFile(kProjectPath, uiProject, fileBuffer, sizeof(fileBuffer));
            printf("SAVE ok=%d\n", saved ? 1 : 0);
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
```

- [ ] **Step 7: Собрать**

Run: `tools/idf.sh firmware set-target esp32s3 && tools/idf.sh firmware build`
Expected: `Project build complete`, файл `firmware/build/groovebox.bin` меньше 0x180000 байт.

- [ ] **Step 8: Коммит**

```bash
git add .gitignore components tools firmware
git commit -m "feat: add drum machine firmware"
```

---

### Task 8: Автоматическая проверка на устройстве

**Files:**
- Create: `tools/device_test.py`
- Test: `tools/test_device_test.py`
- Modify: `docs/superpowers/specs/2026-11-09-groovebox-design.md` (раздел «Результаты этапа 2»)

**Interfaces:**
- Consumes: `tools/slot.py install-app`, консоль прошивки из Задачи 7.
- Produces: `python3 tools/device_test.py` с кодом выхода 0 при успехе.

- [ ] **Step 1: Написать падающий тест разбора ответов**

<!-- file: tools/test_device_test.py -->
```python
import unittest

import device_test


class ParseTests(unittest.TestCase):
    def test_parse_fields(self):
        fields = device_test.parse_fields("STATE bpm=120 swing=50 playing=1")
        self.assertEqual(fields, {"bpm": 120, "swing": 50, "playing": 1})

    def test_ignores_words_without_a_value(self):
        self.assertEqual(device_test.parse_fields("LEVEL peak=12 extra"), {"peak": 12})

    def test_louder_passes_at_threshold(self):
        self.assertTrue(device_test.is_louder(4000, 1000))
        self.assertFalse(device_test.is_louder(3999, 1000))

    def test_quiet_reference_of_zero_is_handled(self):
        self.assertTrue(device_test.is_louder(10, 0))


if __name__ == "__main__":
    unittest.main()
```

Run: `python3 tools/test_device_test.py`
Expected: FAIL, `ModuleNotFoundError: No module named 'device_test'`.

- [ ] **Step 2: Реализовать проверку**

<!-- file: tools/device_test.py -->
```python
#!/usr/bin/env python3
"""Drives the drum machine over USB and measures its speaker with its own microphone."""
import os
import select
import sys
import termios
import time

PORT = "/dev/ttyACM0"
LOUDER_RATIO = 4  # 12 dB
MAX_LOAD_PERMILLE = 500


def parse_fields(line):
    fields = {}
    for word in line.split():
        name, separator, value = word.partition("=")
        if separator and value.lstrip("-").isdigit():
            fields[name] = int(value)
    return fields


def is_louder(level, reference):
    return level >= reference * LOUDER_RATIO and level > 0


class Device:
    def __init__(self):
        self.fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
        attributes = termios.tcgetattr(self.fd)
        attributes[0] = attributes[1] = attributes[3] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        self.buffer = b""

    def send(self, line):
        os.write(self.fd, line.encode() + b"\n")

    def read_line(self, timeout):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            if select.select([self.fd], [], [], remaining)[0]:
                self.buffer += os.read(self.fd, 256)
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def wait_for(self, prefix, timeout=5.0):
        deadline = time.monotonic() + timeout
        while True:
            line = self.read_line(max(deadline - time.monotonic(), 0))
            if line is None:
                raise TimeoutError(f"no line starting with {prefix}")
            if line.startswith(prefix):
                return line

    def key(self, name, mods="-"):
        self.send(f"K {mods} {name}")
        self.wait_for("OK")

    def state(self):
        self.send("S")
        fields = parse_fields(self.wait_for("STATE "))
        lanes = {}
        while True:
            line = self.read_line(5.0)
            if line is None:
                raise TimeoutError("state dump ended early")
            if line.startswith("LANE"):
                _, lane, steps = line.split()
                lanes[int(lane)] = steps
            elif line.startswith("STATE-END"):
                return fields, lanes

    def level(self, ms):
        self.send(f"L {ms}")
        return parse_fields(self.wait_for("LEVEL", ms / 1000 + 5))["peak"]

    def cpu(self):
        self.send("C")
        return parse_fields(self.wait_for("CPU"))

    def wait_ready(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            self.send("S")
            line = self.read_line(1.0)
            while line is not None:
                if line.startswith("STATE-END"):
                    return
                line = self.read_line(0.3)
        raise TimeoutError("device did not answer")


class Report:
    def __init__(self):
        self.failures = 0

    def check(self, name, passed, detail=""):
        print(f"{'PASS' if passed else 'FAIL'} {name} {detail}".rstrip())
        self.failures += 0 if passed else 1


def reset_device():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import slot

    slot.esptool("chip_id")


def run():
    report = Report()
    device = Device()
    device.wait_ready()
    device.send("X")
    device.wait_for("OK")

    fields, lanes = device.state()
    report.check("storage available", fields["storage"] == 1)
    report.check("pattern cleared", all(set(s) == {"."} for s in lanes.values()))
    device.send("F")
    print(device.wait_for("FS"))

    quiet = device.level(500)
    print(f"INFO quiet peak {quiet}")

    device.key("2")  # Snare, audible on a small speaker.
    time.sleep(0.6)
    for key in ("q", "t", "a", "g"):
        device.key(key)
    fields, lanes = device.state()
    report.check("steps entered", lanes[1] == "x...x...x...x...", lanes[1])

    device.key("space")
    time.sleep(0.3)
    playing = device.level(2000)
    fields, _ = device.state()
    report.check("playing", fields["playing"] == 1)
    report.check("speaker heard while playing", is_louder(playing, quiet), f"{playing} vs {quiet}")
    cpu = device.cpu()
    report.check("cpu load", cpu["load_permille"] <= MAX_LOAD_PERMILLE, f"{cpu['load_permille'] / 10}%")
    report.check("audio exchange failures", cpu["failures"] == 0, str(cpu["failures"]))

    before = device.cpu()
    time.sleep(3)
    after = device.cpu()
    report.check("no overflows while playing", after["tx_overflows"] == before["tx_overflows"]
                 and after["rx_overflows"] == before["rx_overflows"])

    device.key("g", "f")
    fields, lanes = device.state()
    report.check("grid changed to 1/8", fields["grid"] == 1, str(fields["grid"]))
    report.check("steps remapped", lanes[1] == "x.x.x.x.........", lanes[1])
    device.key("z", "f")
    fields, lanes = device.state()
    report.check("undo restored grid", fields["grid"] == 2 and lanes[1] == "x...x...x...x...")
    device.key("w", "f")
    fields, _ = device.state()
    report.check("swing changed", fields["swing"] == 54, str(fields["swing"]))

    device.key("space")
    device.wait_for("SAVE")
    time.sleep(1.5)
    stopped = device.level(500)
    report.check("silent after stop", is_louder(playing, stopped), f"{playing} vs {stopped}")

    os.close(device.fd)
    reset_device()
    time.sleep(1)
    device = Device()
    device.wait_ready()
    fields, lanes = device.state()
    report.check("pattern survives reboot", lanes[1] == "x...x...x...x...", lanes[1])
    report.check("swing survives reboot", fields["swing"] == 54, str(fields["swing"]))
    os.close(device.fd)

    print(f"{report.failures} failures")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(run())
```

Run: `python3 tools/test_device_test.py`
Expected: `OK`, 4 теста.

- [ ] **Step 3: Установить и проверить на устройстве**

Эти шаги выполняет контроллер. Карта microSD должна быть в устройстве.

Run: `python3 tools/slot.py install-app firmware/build/groovebox.bin && python3 tools/device_test.py`
Expected: все строки `PASS`, `0 failures`, код выхода 0.

- [ ] **Step 4: Записать результаты и закоммитить**

Добавить в конец спецификации раздел «Результаты этапа 2» с фактическими значениями из вывода: тип файловой системы, нагрузка процессора, уровни с микрофона.

```bash
git add tools docs
git commit -m "test: add self-running drum machine check"
```
