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

struct DrumSound {
    uint8_t tune = 64;
    uint8_t decay = 64;
    uint8_t tone = 64;
    uint8_t level = 100;
};

struct Project {
    uint16_t bpm = 120;
    uint8_t swing = 50;
    bool swingEnabled[kTracks] = {true, true, true, true, true};
    DrumSound sounds[kDrumLanes];
    DrumPattern drums[kPatternsPerTrack];
    InstrumentPattern instruments[kInstrumentTracks][kPatternsPerTrack];
    Scene scenes[kScenes];
    SongEntry song[kSongEntries];
    uint8_t songLength = 0;
};

}  // namespace gb
