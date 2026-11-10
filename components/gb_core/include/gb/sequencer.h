#pragma once
#include <cstdint>

#include "gb/model.h"

namespace gb {

enum class EventType : uint8_t { DrumHit, NoteOn, NoteOff, AllNotesOff };

// AllNotesOff addresses one track, or every track when its track is kAllTracks.
constexpr uint8_t kAllTracks = 255;

struct Event {
    uint32_t frame;  // Offset inside the rendered block.
    EventType type;
    uint8_t track;
    uint8_t data;  // Drum lane or MIDI pitch.
    uint8_t velocity;
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

    uint32_t sceneTick() const { return sceneTick_; }

    // Returns the number of events written to `out`. Events beyond `capacity` are dropped.
    uint32_t render(uint32_t frames, Event* out, uint32_t capacity);

private:
    uint64_t frameOfTick(uint64_t tick) const;
    uint32_t sceneTicks(uint8_t scene) const;
    void begin(uint8_t scene);
    bool advanceSong();
    void processTick(uint32_t frame);
    void emit(uint32_t frame, EventType type, uint8_t track, uint8_t data, uint8_t velocity);
    void emitDrums(uint32_t frame, int8_t index, uint32_t local);
    void emitNotes(uint32_t frame, uint8_t track, int8_t index, uint32_t local, uint32_t sceneLength);

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
};

}  // namespace gb
