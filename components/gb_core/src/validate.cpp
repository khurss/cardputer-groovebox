#include "gb/validate.h"
#include <cstring>

namespace gb {

namespace {

bool validLength(uint8_t length) { return length == 4 || length == 8 || length == 16; }

bool validGrid(Grid grid) { return static_cast<uint8_t>(grid) <= static_cast<uint8_t>(Grid::SixteenthTriplet); }

}  // namespace

bool isValidNote(const Note& note) {
    return note.startTick < kMaxPatternTicks && note.lengthTicks > 0 && note.lengthTicks <= kMaxPatternTicks &&
           note.pitch <= 127 && note.velocity > 0 && note.velocity <= 127;
}

bool isValidDrums(const DrumPattern& pattern) {
    if (!validLength(pattern.length) || !validGrid(pattern.grid))
        return false;
    for (const auto& lane : pattern.velocity)
        for (uint8_t velocity : lane)
            if (velocity > 127)
                return false;
    return true;
}

bool isValidInstrument(const InstrumentPattern& pattern) {
    if (!validLength(pattern.length) || !validGrid(pattern.grid) || pattern.noteCount > kMaxNotes)
        return false;
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        if (!isValidNote(pattern.notes[i]))
            return false;
    }
    return true;
}

bool isValidProject(const Project& project) {
    if(!project.equalizer.valid())return false;
    for (int t = 0; t < kInstrumentTracks; ++t)
        if (project.keyboardLayout[t] > 1 || project.keyboardRoot[t] > 11 || project.keyboardScale[t] > 3) return false;
    for (const auto& clip : project.samples) if (!isValidSampleClip(clip)) return false;
    for (const auto& pattern : project.samplePatterns) if (!isValidSamplePattern(pattern)) return false;
    if (project.masterVolume > 100)
        return false;
    for (const auto& m : project.mixer)
        if (m.volume > 100)
            return false;
    for (const auto& a : project.arps)
        if (static_cast<uint8_t>(a.mode) > 3 || !validGrid(a.rate) || a.octaves < 1 || a.octaves > 3 || a.gate < 10 ||
            a.gate > 100)
            return false;
    if (project.bpm < 40 || project.bpm > 240 || project.swing < 50 || project.swing > 75)
        return false;
    for (const DrumPattern& pattern : project.drums)
        if (!isValidDrums(pattern))
            return false;
    for (const auto& track : project.instruments)
        for (const InstrumentPattern& pattern : track)
            if (!isValidInstrument(pattern))
                return false;
    for (const DrumSound& s : project.sounds)
        if (s.tune > 127 || s.decay > 127 || s.tone > 127 || s.level > 127)
            return false;
    for (const SynthSound& s : project.synths)
        if (static_cast<uint8_t>(s.wave) > 3 || s.attack > 127 || s.decay > 127 || s.sustain > 127 || s.release > 127 ||
            s.cutoff > 127 || s.resonance > 127 || s.glide > 127 || s.level > 127)
            return false;
    for (const Scene& scene : project.scenes)
        for (int8_t index : scene.pattern)
            if (index != kEmptyPattern && (index < 0 || index >= kPatternsPerTrack))
                return false;
    if (project.songLength > kSongEntries)
        return false;
    for (uint8_t i = 0; i < project.songLength; ++i)
        if (project.song[i].scene >= kScenes || project.song[i].repeats == 0)
            return false;
    return true;
}

bool isValidSampleClip(const SampleClip& clip) {
    if (!clip.source[0]) return clip.start == 0 && clip.end == 0;
    if (!std::memchr(clip.source, 0, sizeof(clip.source)) || clip.end <= clip.start) return false;
    if (!std::strcmp(clip.source, ".") || !std::strcmp(clip.source, "..")) return false;
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(clip.source); *c; ++c)
        if (*c < 32 || *c == '/' || *c == '\\' || *c == ':') return false;
    return true;
}
bool isValidSamplePattern(const SamplePattern& p) {
    if (!isValidDrums(p.steps)) return false;
    for (auto s : p.slot) if (s >= kSampleSlots) return false;
    return true;
}

void changeDrumGrid(DrumPattern& pattern, Grid grid) {
    const uint16_t oldTicks = gridTicks(pattern.grid);
    const uint16_t newTicks = gridTicks(grid);
    uint8_t moved[kDrumLanes][kMaxSteps] = {};
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        for (int step = 0; step < kMaxSteps; ++step) {
            if (pattern.velocity[lane][step] == 0)
                continue;
            const uint32_t tick = uint32_t{oldTicks} * step;
            const uint32_t target = tick / newTicks;
            if (tick % newTicks == 0 && target < kMaxSteps)
                moved[lane][target] = pattern.velocity[lane][step];
        }
    }
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kMaxSteps; ++step)
            pattern.velocity[lane][step] = moved[lane][step];
    pattern.grid = grid;
}

}  // namespace gb
