#include "gb/drums.h"

#include <cmath>

namespace gb {

namespace {

struct Preset {
    float frequency;
    float pitchDepth;
    float pitchTime;
    float decay;
    float noise;
    float highpass;
    uint32_t bursts;
};

constexpr Preset kPresets[kDrumLanes] = {
    {48.0f, 3.2f, 0.035f, 0.32f, 0.00f, 0.00f, 0},  // Kick.
    {185.0f, 0.6f, 0.020f, 0.16f, 0.70f, 0.25f, 0},  // Snare.
    {900.0f, 0.0f, 0.010f, 0.18f, 1.00f, 0.35f, 3},  // Clap.
    {0.0f, 0.0f, 0.010f, 0.05f, 1.00f, 0.85f, 0},  // Closed hat.
    {0.0f, 0.0f, 0.010f, 0.32f, 1.00f, 0.85f, 0},  // Open hat.
    {110.0f, 0.9f, 0.060f, 0.28f, 0.05f, 0.00f, 0},  // Tom.
    {1700.0f, 0.0f, 0.010f, 0.03f, 0.25f, 0.30f, 0},  // Rim.
    {560.0f, 0.0f, 0.010f, 0.14f, 0.00f, 0.00f, 0},  // Perc.
};

constexpr float kTwoPi = 6.2831853f;
constexpr float kBurstSeconds = 0.012f;
constexpr float kBurstDecaySeconds = 0.004f;
constexpr float kSilence = 0.0005f;

float exponential(uint8_t value, float low, float high) { return low * std::pow(high / low, value / 127.0f); }

float decayCoefficient(float seconds, uint32_t rate) { return std::exp(-1.0f / (seconds * static_cast<float>(rate))); }

float atMost(float value, float limit) { return value < limit ? value : limit; }

float clampUnit(float value) { return value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value); }

}  // namespace

DrumRack::DrumRack(uint32_t sampleRate) : rate_(sampleRate) {}

float DrumRack::nextNoise() {
    noiseState_ ^= noiseState_ << 13;
    noiseState_ ^= noiseState_ >> 17;
    noiseState_ ^= noiseState_ << 5;
    return static_cast<float>(static_cast<int32_t>(noiseState_)) / 2147483648.0f;
}

void DrumRack::trigger(uint8_t lane, uint8_t velocity, const DrumSound& sound) {
    if (lane >= kDrumLanes) return;
    const Preset& preset = kPresets[lane];
    const float brightness = 0.5f + sound.tone / 127.0f;
    Voice& voice = voices_[lane];
    voice.phase = 0.0f;
    voice.baseStep = preset.frequency * exponential(sound.tune, 0.5f, 2.0f) / static_cast<float>(rate_);
    voice.pitchDepth = preset.pitchDepth;
    voice.pitchEnvelope = 1.0f;
    voice.pitchCoefficient = decayCoefficient(preset.pitchTime, rate_);
    voice.amplitude = 1.0f;
    voice.amplitudeCoefficient = decayCoefficient(preset.decay * exponential(sound.decay, 0.25f, 4.0f), rate_);
    voice.noiseMix = atMost(preset.noise * brightness, 1.0f);
    voice.highpass = atMost(preset.highpass * brightness * (preset.frequency == 0.0f ? exponential(sound.tune, 0.6f, 1.4f) : 1.0f), 0.95f);
    voice.drive = exponential(sound.tone, 0.7f, 3.0f);
    voice.burstCoefficient = decayCoefficient(kBurstDecaySeconds, rate_);
    voice.lowState = 0.0f;
    voice.gain = (velocity / 127.0f) * (sound.level / 127.0f);
    voice.burstsLeft = preset.bursts;
    voice.burstTimer = static_cast<uint32_t>(kBurstSeconds * static_cast<float>(rate_));
    voice.active = true;
}

float DrumRack::render() {
    float mix = 0.0f;
    for (Voice& voice : voices_) {
        if (!voice.active) continue;
        const float noise = nextNoise();
        const float tonal = voice.baseStep > 0.0f && voice.noiseMix < 1.0f
                                ? clampUnit(std::sin(kTwoPi * voice.phase) * voice.drive)
                                : 0.0f;
        voice.phase += voice.baseStep * (1.0f + voice.pitchDepth * voice.pitchEnvelope);
        if (voice.phase >= 1.0f) voice.phase -= std::floor(voice.phase);
        voice.pitchEnvelope *= voice.pitchCoefficient;
        if (voice.pitchEnvelope < 1e-6f) voice.pitchEnvelope = 0.0f;

        // Only the noise is filtered, so a drum's body keeps its low end.
        voice.lowState += voice.highpass * (noise - voice.lowState);
        const float filteredNoise = voice.highpass > 0.0f ? noise - voice.lowState : noise;
        mix += (tonal * (1.0f - voice.noiseMix) + filteredNoise * voice.noiseMix) * voice.amplitude * voice.gain;

        voice.amplitude *= voice.burstsLeft > 0 ? voice.burstCoefficient : voice.amplitudeCoefficient;
        if (voice.burstsLeft > 0 && --voice.burstTimer == 0) {
            voice.amplitude = 1.0f;
            --voice.burstsLeft;
            voice.burstTimer = static_cast<uint32_t>(kBurstSeconds * static_cast<float>(rate_));
        }
        if (voice.amplitude < kSilence && voice.burstsLeft == 0) voice.active = false;
    }
    return mix;
}

}  // namespace gb
