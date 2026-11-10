#include "gb/recorder.h"

#include <algorithm>
namespace gb {
void Recorder::flush() {
    uint8_t sent = 0;
    while (sent < pendingCount_ && updates_.push(pending_[sent])) ++sent;
    for (uint8_t i = sent; i < pendingCount_; ++i) pending_[i - sent] = pending_[i];
    pendingCount_ -= sent;
}
void Recorder::post(RecordUpdateType type, uint8_t slot, Note note) {
    // Draining excludes another pass until the consumer acknowledges Ended.
    pending_[pendingCount_++] = {type, track_, pattern_, slot, note};
    flush();
}
void Recorder::arm(uint8_t track, uint8_t pattern) {
    if (armed_ || draining_ || track >= kInstrumentTracks || pattern >= kPatternsPerTrack) return;
    track_ = track;
    pattern_ = pattern;
    reserved_ = 0;
    fullPosted_ = false;
    armed_ = true;
    post(RecordUpdateType::Armed);
    if (sequencer_.playing()) play();
}
void Recorder::play() {
    if (!armed_ || recording_) return;
    recording_ = true;
    post(RecordUpdateType::Recording);
}
void Recorder::noteOn(uint8_t track, uint8_t key, uint8_t pitch, uint8_t velocity) {
    if (!recording_ || track != track_ || key >= 128 || held_[key].active) return;
    const auto& p = project_.instruments[track_][pattern_];
    if (p.noteCount + reserved_ >= kMaxNotes) {
        if (!fullPosted_) {
            fullPosted_ = true;
            post(RecordUpdateType::Full);
        }
        return;
    }
    held_[key] = {true, pitch, velocity, sequencer_.transportTick(), sequencer_.localTick(track_ + 1)};
    ++reserved_;
}
void Recorder::noteOff(uint8_t track, uint8_t key) {
    if (track != track_ || key >= 128 || !held_[key].active) return;
    const auto h = held_[key];
    held_[key].active = false;
    --reserved_;
    auto& p = project_.instruments[track_][pattern_];
    const uint32_t total = patternTicks(p.length, p.grid);
    const uint32_t elapsed = std::min<uint32_t>(sequencer_.transportTick() - h.tick, total);
    uint32_t start = h.local, end = start + elapsed;
    if (project_.recordQuantize[track_]) {
        const uint32_t step = gridTicks(p.grid);
        start = ((start + step / 2) / step) * step;
        end = ((end + step / 2) / step) * step;
    }
    const uint32_t length = end > start ? std::min(end - start, total) : 1;
    const Note n{static_cast<uint16_t>(start % total), static_cast<uint16_t>(length), h.pitch, h.velocity};
    const uint8_t slot = p.noteCount;
    p.notes[p.noteCount++] = n;
    if (sequencer_.activePattern(track_ + 1) == pattern_) sequencer_.deferNote(track_ + 1, slot);
    post(RecordUpdateType::Added, slot, n);
}
void Recorder::finish() {
    if (!armed_) return;
    for (uint8_t key = 0; key < 128; ++key) noteOff(track_, key);
    recording_ = false;
    armed_ = false;
    draining_ = true;
    post(RecordUpdateType::Ended);
}
}  // namespace gb
