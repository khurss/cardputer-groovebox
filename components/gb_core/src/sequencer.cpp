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

void Sequencer::emitDrums(uint32_t frame, int8_t index, uint32_t local) {
    const DrumPattern& pattern = project_.drums[index];
    const uint16_t stepTicks = gridTicks(pattern.grid);
    const uint32_t position = local % patternTicks(pattern.length, pattern.grid);
    const uint16_t step = static_cast<uint16_t>(position / stepTicks);
    const uint16_t delay = project_.swingEnabled[0] ? swingOffset(step, pattern.grid, project_.swing) : 0;
    if (position != uint32_t{step} * stepTicks + delay) return;
    for (uint8_t lane = 0; lane < kDrumLanes; ++lane) {
        const uint8_t velocity = pattern.velocity[lane][step];
        if (velocity > 0) emit(frame, EventType::DrumHit, 0, lane, velocity);
    }
}

void Sequencer::emitNotes(uint32_t frame, uint8_t track, int8_t index, uint32_t local, uint32_t sceneLength) {
    const InstrumentPattern& pattern = project_.instruments[track - 1][index];
    const uint32_t total = patternTicks(pattern.length, pattern.grid);
    const uint32_t position = local % total;
    const uint16_t stepTicks = gridTicks(pattern.grid);
    const bool swung = project_.swingEnabled[track];

    // A pattern that does not divide the scene jumps back at the scene wrap, so its held notes need a release.
    if (local == 0 && sceneTick_ > 0 && sceneLength % total != 0) emit(frame, EventType::AllNotesOff, track, 0, 0);

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
        // A note that ends on a swung step is released after that step's delay.
        const uint32_t end = note.startTick + length;
        uint32_t off = onTicks[i] + length;
        if (swung && end % stepTicks == 0) off = end + swingOffset(end / stepTicks, pattern.grid, project_.swing);
        if (position == off % total) emit(frame, EventType::NoteOff, track, note.pitch, 0);
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
            emit(frame, EventType::AllNotesOff, kAllTracks, 0, 0);
            playing_ = false;
            songMode_ = false;
            return;
        }
        if (songIndex_ != before) emit(frame, EventType::AllNotesOff, kAllTracks, 0, 0);
    } else if (queued_ >= 0 && sceneTick_ % kTicksPerBar == 0) {
        begin(static_cast<uint8_t>(queued_));
        emit(frame, EventType::AllNotesOff, kAllTracks, 0, 0);
    }

    // The scene length is the loop unit: patterns restart at its start.
    const uint32_t sceneLength = sceneTicks(scene_);
    const uint32_t local = sceneTick_ % sceneLength;
    const Scene& scene = project_.scenes[scene_];
    if (scene.pattern[0] != kEmptyPattern) emitDrums(frame, scene.pattern[0], local);
    for (uint8_t track = 1; track < kTracks; ++track)
        if (scene.pattern[track] != kEmptyPattern) emitNotes(frame, track, scene.pattern[track], local, sceneLength);
    ++sceneTick_;
}

uint32_t Sequencer::render(uint32_t frames, Event* out, uint32_t capacity) {
    out_ = out;
    capacity_ = capacity;
    count_ = 0;
    if (releasePending_) {
        emit(0, EventType::AllNotesOff, kAllTracks, 0, 0);
        releasePending_ = false;
    }
    const uint64_t end = now_ + frames;
    if (playing_ && project_.bpm != bpm_) {
        originFrame_ = frameOfTick(nextTick_);
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
