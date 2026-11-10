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
    if (!isValidCommand(command)) return;
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
    playing_.store(sequencer_.playing(), std::memory_order_relaxed);
}

}  // namespace gb
