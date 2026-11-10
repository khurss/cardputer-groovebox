#pragma once
#include <cstdint>

#include "gb/model.h"

namespace gb {

enum class EventType : uint8_t { DrumHit, SampleHit, NoteOn, NoteOff, AllNotesOff };

// AllNotesOff addresses one track, or every track when its track is kAllTracks.
constexpr uint8_t kAllTracks = 255;

struct Event {
    uint32_t frame;  // Offset inside the rendered block.
    EventType type;
    uint8_t track;
    uint8_t data;  // Drum lane or MIDI pitch.
    uint8_t velocity;
    uint16_t token = 0;  // Stable clip slot; live voices use a separate source namespace.
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

    uint32_t transportTick() const { return static_cast<uint32_t>(nextTick_); }
    int8_t activePattern(uint8_t track) const { return project_.scenes[scene_].pattern[track]; }
    uint8_t sceneIndex() const { return scene_; }
    bool songMode() const { return songMode_; }
    uint32_t status() const {
        return scene_ | (uint32_t(queued_ + 1) << 4) | (uint32_t(songMode_) << 9) | (uint32_t(songIndex_) << 10);
    }
    uint32_t sceneTick() const { return sceneTick_; }
    uint32_t localTick(uint8_t track) const;
    uint32_t overflows() const { return overflows_; }
    void deferNote(uint8_t track, uint8_t slot);
    void resetDeferrals(uint8_t track);

    // Overflow ends the block with AllNotesOff and suppresses subsequent starts.
    uint32_t render(uint32_t frames, Event* out, uint32_t capacity);

private:
    uint64_t frameOfTick(uint64_t tick) const;
    uint32_t sceneTicks(uint8_t scene) const;
    void begin(uint8_t scene);
    bool advanceSong();
    void processTick(uint32_t frame);
    void emit(uint32_t frame, EventType type, uint8_t track, uint8_t data, uint8_t velocity, uint16_t token = 0);
    void emitDrums(uint32_t frame, int8_t index, uint32_t local);
    void emitNotes(uint32_t frame, uint8_t track, int8_t index, uint32_t local, uint32_t sceneLength, bool attacks);
    struct Schedule {
        InstrumentPattern pattern;
        uint16_t on[kMaxNotes] = {}, off[kMaxNotes] = {};
        uint8_t swing = 0;
        bool enabled = false, valid = false;
    };
    void updateSchedule(uint8_t track, const InstrumentPattern& pattern);

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
    uint32_t overflows_ = 0;
    bool overflowed_ = false;
    uint32_t deferred_[kInstrumentTracks][kMaxNotes] = {};
    Schedule schedules_[kInstrumentTracks];
};

}  // namespace gb
