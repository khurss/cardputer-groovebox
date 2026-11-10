#pragma once

#include <cstdint>

#include "gb/model.h"

namespace gb {

// Eight synthesized drum voices. Each lane is monophonic; a new hit restarts it.
class DrumRack {
public:
    explicit DrumRack(uint32_t sampleRate);

    void trigger(uint8_t lane, uint8_t velocity, const DrumSound& sound);
    float render();

private:
    struct Voice {
        bool active = false;
        float phase = 0.0f;
        float baseStep = 0.0f;
        float pitchDepth = 0.0f;
        float pitchEnvelope = 0.0f;
        float pitchCoefficient = 0.0f;
        float amplitude = 0.0f;
        float amplitudeCoefficient = 0.0f;
        float noiseMix = 0.0f;
        float highpass = 0.0f;
        float lowState = 0.0f;
        float gain = 0.0f;
        uint32_t burstsLeft = 0;
        uint32_t burstTimer = 0;
    };

    float nextNoise();

    uint32_t rate_;
    uint32_t noiseState_ = 0x12345678u;
    Voice voices_[kDrumLanes];
};

}  // namespace gb
