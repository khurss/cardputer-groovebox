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
