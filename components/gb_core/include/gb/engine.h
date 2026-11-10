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
