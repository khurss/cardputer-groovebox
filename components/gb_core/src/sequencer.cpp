#include "gb/audio_memory.h"
#include "gb/sequencer.h"

#include <algorithm>
#include <cstring>

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
    for (int track = 1; track <= kInstrumentTracks; ++track) {
        if (data.pattern[track] == kEmptyPattern)
            continue;
        const InstrumentPattern& pattern = project_.instruments[track - 1][data.pattern[track]];
        const uint32_t ticks = patternTicks(pattern.length, pattern.grid);
        if (ticks > longest)
            longest = ticks;
    }
    if (data.pattern[kSamplerTrack] >= 0) {
        const auto& p = project_.samplePatterns[data.pattern[kSamplerTrack]].steps;
        longest = std::max(longest, patternTicks(p.length, p.grid));
    }
    return longest == 0 ? kTicksPerBar : longest;
}

void Sequencer::begin(uint8_t scene) {
    scene_ = scene;
    sceneTick_ = 0;
    queued_ = -1;
    for (uint8_t t = 1; t <= kInstrumentTracks; ++t)
        resetDeferrals(t);
}

void Sequencer::playScene(uint8_t scene) {
    if (scene >= kScenes)
        return;
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
    if (entry >= project_.songLength)
        return;
    if (playing_)
        releasePending_ = true;
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
    if (!playing_)
        return;
    playing_ = false;
    releasePending_ = true;
    songMode_ = false;
    queued_ = -1;
}

uint32_t Sequencer::localTick(uint8_t track) const {
    const uint32_t local = sceneTick_ % sceneTicks(scene_);
    if (track == 0 || track > kInstrumentTracks)
        return local;
    const int index = project_.scenes[scene_].pattern[track];
    if (index < 0)
        return local;
    const auto& p = project_.instruments[track - 1][index];
    return local % patternTicks(p.length, p.grid);
}

void Sequencer::resetDeferrals(uint8_t track) {
    if (track > 0 && track <= kInstrumentTracks)
        for (auto& tick : deferred_[track - 1])
            tick = 0;
}

void Sequencer::deferNote(uint8_t track, uint8_t slot) {
    if (track == 0 || track > kInstrumentTracks || slot >= kMaxNotes)
        return;
    const int index = project_.scenes[scene_].pattern[track];
    if (index < 0)
        return;
    const auto& p = project_.instruments[track - 1][index];
    const auto& n = p.notes[slot];
    const uint32_t total = patternTicks(p.length, p.grid);
    const uint32_t on = n.startTick + (project_.swingEnabled[track] && n.startTick % gridTicks(p.grid) == 0
                                           ? swingOffset(n.startTick / gridTicks(p.grid), p.grid, project_.swing)
                                           : 0);
    uint32_t distance = (on + total - localTick(track)) % total;
    if (distance == 0)
        distance = total;
    deferred_[track - 1][slot] = sceneTick_ + distance;
}

void Sequencer::emit(uint32_t frame, EventType type, uint8_t track, uint8_t data, uint8_t velocity, uint16_t token) {
    if (overflowed_)
        return;
    const bool release = type == EventType::NoteOff || type == EventType::AllNotesOff;
    // Keep the last slot for releases or a panic. On panic, no later starts in this block survive.
    if (capacity_ == 0 || count_ == capacity_ || (!release && count_ + 1 >= capacity_)) {
        ++overflows_;
        overflowed_ = true;
        if (capacity_ == 0) {
            releasePending_ = true;
            return;
        }
        if (count_ == capacity_)
            --count_;
        out_[count_++] = Event{frame, EventType::AllNotesOff, kAllTracks, 0, 0};
        return;
    }
    out_[count_++] = Event{frame, type, track, data, velocity, token};
}

// Returns false when the song has ended.
bool Sequencer::advanceSong() {
    if (songIndex_ >= project_.songLength)
        return false;
    if (sceneTick_ < sceneTicks(scene_) * project_.song[songIndex_].repeats)
        return true;
    if (songIndex_ + 1 >= project_.songLength)
        return false;
    ++songIndex_;
    begin(project_.song[songIndex_].scene);
    songStopTick_ = sceneTicks(scene_) * project_.song[songIndex_].repeats;
    return true;
}

GB_AUDIO_CODE void Sequencer::emitDrums(uint32_t frame, int8_t index, uint32_t local) {
    const DrumPattern& pattern = project_.drums[index];
    const uint16_t stepTicks = gridTicks(pattern.grid);
    const uint32_t position = local % patternTicks(pattern.length, pattern.grid);
    const uint16_t step = static_cast<uint16_t>(position / stepTicks);
    const uint16_t delay = project_.swingEnabled[0] ? swingOffset(step, pattern.grid, project_.swing) : 0;
    if (position != uint32_t{step} * stepTicks + delay)
        return;
    for (uint8_t lane = 0; lane < kDrumLanes; ++lane) {
        const uint8_t velocity = pattern.velocity[lane][step];
        if (velocity > 0)
            emit(frame, EventType::DrumHit, 0, lane, velocity);
    }
}

GB_AUDIO_CODE void Sequencer::emitNotes(uint32_t frame, uint8_t track, int8_t index, uint32_t local, uint32_t sceneLength,
                          bool attacks) {
    const InstrumentPattern& pattern = project_.instruments[track - 1][index];
    const uint32_t total = patternTicks(pattern.length, pattern.grid);
    const uint32_t position = local % total;

    // A pattern that does not divide the scene jumps back at the scene wrap, so its held notes need a release.
    if (!attacks && local == 0 && sceneTick_ > 0 && sceneLength % total != 0)
        emit(frame, EventType::AllNotesOff, track, 0, 0);

    if (!attacks)
        updateSchedule(track, pattern);
    const auto& schedule = schedules_[track - 1];
    // Cached positions avoid divisions for every note at every tick. Releases still go first.
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        if (!attacks && position == schedule.off[i])
            emit(frame, EventType::NoteOff, track, pattern.notes[i].pitch, 0, i);
        if (attacks && position == schedule.on[i] && sceneTick_ >= deferred_[track - 1][i])
            emit(frame, EventType::NoteOn, track, pattern.notes[i].pitch, pattern.notes[i].velocity, i);
    }
}

GB_AUDIO_CODE void Sequencer::updateSchedule(uint8_t track, const InstrumentPattern& pattern) {
    auto& c = schedules_[track - 1];
    const bool swung = project_.swingEnabled[track];
    // Compare fields and Note bytes, excluding struct padding. Direct model edits on host
    // and normal audio-task commands both invalidate the same cache.
    if (c.valid && c.swing == project_.swing && c.enabled == swung && c.pattern.length == pattern.length &&
        c.pattern.grid == pattern.grid && c.pattern.noteCount == pattern.noteCount &&
        std::memcmp(c.pattern.notes, pattern.notes, pattern.noteCount * sizeof(Note)) == 0)
        return;
    c.pattern = pattern;
    c.swing = project_.swing;
    c.enabled = swung;
    c.valid = true;
    const uint32_t total = patternTicks(pattern.length, pattern.grid);
    const uint16_t step = gridTicks(pattern.grid);
    for (uint8_t i = 0; i < pattern.noteCount; ++i) {
        const auto& n = pattern.notes[i];
        if (n.startTick >= total) {
            c.on[i] = c.off[i] = 65535;
            continue;
        }
        const uint32_t on =
            n.startTick +
            (swung && n.startTick % step == 0 ? swingOffset(n.startTick / step, pattern.grid, c.swing) : 0);
        const uint32_t length = std::min<uint32_t>(n.lengthTicks, total), end = n.startTick + length;
        uint32_t off = on + length;
        if (swung && end % step == 0)
            off = end + swingOffset(end / step, pattern.grid, c.swing);
        off = std::max(on + 1, std::min(off, on + total));
        c.on[i] = on % total;
        c.off[i] = off % total;
    }
}

GB_AUDIO_CODE void Sequencer::processTick(uint32_t frame) {
    if (songMode_) {
        const uint8_t before = songIndex_;
        if (!advanceSong()) {
            emit(frame, EventType::AllNotesOff, kAllTracks, 0, 0);
            playing_ = false;
            songMode_ = false;
            return;
        }
        if (songIndex_ != before)
            emit(frame, EventType::AllNotesOff, kAllTracks, 0, 0);
    } else if (queued_ >= 0 && nextTick_ % kTicksPerBar == 0) {
        begin(static_cast<uint8_t>(queued_));
        emit(frame, EventType::AllNotesOff, kAllTracks, 0, 0);
    }

    // The scene length is the loop unit: patterns restart at its start.
    const uint32_t sceneLength = sceneTicks(scene_);
    const uint32_t local = sceneTick_ % sceneLength;
    const Scene& scene = project_.scenes[scene_];
    // Two linear passes give global release-before-attack ordering without sorting a dense chord.
    for (uint8_t track = 1; track <= kInstrumentTracks; ++track)
        if (scene.pattern[track] != kEmptyPattern)
            emitNotes(frame, track, scene.pattern[track], local, sceneLength, false);
    if (scene.pattern[0] != kEmptyPattern)
        emitDrums(frame, scene.pattern[0], local);
    for (uint8_t track = 1; track <= kInstrumentTracks; ++track)
        if (scene.pattern[track] != kEmptyPattern)
            emitNotes(frame, track, scene.pattern[track], local, sceneLength, true);
    if (scene.pattern[kSamplerTrack] >= 0) {
        const auto& p = project_.samplePatterns[scene.pattern[kSamplerTrack]];
        const auto& steps = p.steps;
        const uint32_t position = local % patternTicks(steps.length, steps.grid);
        const uint16_t step = position / gridTicks(steps.grid);
        const uint16_t delay = project_.swingEnabled[kSamplerTrack] ? swingOffset(step, steps.grid, project_.swing) : 0;
        if (position == uint32_t(step) * gridTicks(steps.grid) + delay)
            for (uint8_t pad = 0; pad < kSamplePads; ++pad)
                if (steps.velocity[pad][step]) emit(frame, EventType::SampleHit, kSamplerTrack, p.slot[pad], steps.velocity[pad][step]);
    }
    ++sceneTick_;
}

GB_AUDIO_CODE uint32_t Sequencer::render(uint32_t frames, Event* out, uint32_t capacity) {
    out_ = out;
    capacity_ = capacity;
    count_ = 0;
    overflowed_ = false;
    if (releasePending_) {
        emit(0, EventType::AllNotesOff, kAllTracks, 0, 0);
        releasePending_ = capacity_ == 0;
    }
    const uint64_t end = now_ + frames;
    if (playing_ && project_.bpm != bpm_) {
        originFrame_ = frameOfTick(nextTick_);
        originTick_ = nextTick_;
        bpm_ = project_.bpm;
    }
    while (playing_) {
        const uint64_t frame = frameOfTick(nextTick_);
        if (frame >= end)
            break;
        processTick(static_cast<uint32_t>(frame - now_));
        ++nextTick_;
    }
    now_ = end;
    return count_;
}

}  // namespace gb
