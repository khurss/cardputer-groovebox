#include "gb/audio_memory.h"
#include "gb/synth.h"

#include <algorithm>
#include <cmath>

#include "gb/oscillator.h"
namespace gb {
namespace {
constexpr float kPi = 3.14159265358979323846f;
float seconds(uint8_t value, float maximum) {
    const float v = value / 127.0f;
    return v * v * maximum;
}
float blep(float phase, float delta, float inverseDelta) {
    if (phase < delta) {
        const float x = phase * inverseDelta;
        return x + x - x * x - 1;
    }
    if (phase > 1 - delta) {
        const float x = (phase - 1) * inverseDelta;
        return x * x + x + x + 1;
    }
    return 0;
}
}  // namespace
Synth::Synth(uint32_t sampleRate) : rate_(static_cast<float>(sampleRate)), inverseRate_(1.0f / sampleRate) {
    for (int i = 0; i < 128; ++i) {
        frequencies_[i] = 440.0f * std::pow(2.0f, (i - 69) / 12.0f);
        phaseSteps_[i] = std::min(0.45f, frequencies_[i] * inverseRate_);
        inverseSteps_[i] = 1.0f / phaseSteps_[i];
    }
    for (uint8_t t = 0; t < kInstrumentTracks; ++t)
        setSound(t, SynthSound{});
}
void Synth::setSound(uint8_t track, const SynthSound& sound) {
    if (track >= kInstrumentTracks)
        return;
    auto& p = parameters_[track];
    p.sound = sound;
    p.gain = sound.level * (0.16f / 127.0f);
    p.attack = 1 / std::max(1.0f, seconds(sound.attack, 2) * rate_);
    p.sustain = sound.sustain / 127.0f;
    p.decay = 1 / std::max(1.0f, seconds(sound.decay, 2) * rate_);
    p.release = 1 / std::max(1.0f, seconds(sound.release, 4) * rate_);
    p.glide = static_cast<uint32_t>(seconds(sound.glide, 1) * rate_);
    p.inverseGlide = p.glide ? 1.0f / p.glide : 0;
    const float cutoff = std::min(rate_ * 0.375f, 100.0f * std::pow(120.0f, sound.cutoff / 127.0f));
    const float g = std::tan(kPi * cutoff / rate_);
    const float k = 2.0f - 1.9f * (sound.resonance / 127.0f);
    p.a1 = 1 / (1 + g * (g + k));
    p.a2 = g * p.a1;
    p.a3 = g * p.a2;
    for (auto& v : voices_)
        if (v.track == track && v.stage == Stage::Release)
            v.releaseStep = v.envelope * p.release;
}
GB_AUDIO_CODE void Synth::noteOn(uint8_t track, uint8_t pitch, uint8_t velocity, uint16_t token, NoteSource source) {
    if (track >= kInstrumentTracks || pitch > 127 || velocity == 0 || velocity > 127 || source == NoteSource::All)
        return;
    Voice* chosen = nullptr;
    for (uint8_t i = 0; i < voiceLimit_; ++i) {
        auto& v = voices_[i];
        if (v.stage != Stage::Off && v.track == track && v.token == token && v.source == source) {
            chosen = &v;
            break;
        }
        if (v.stage == Stage::Off && !chosen)
            chosen = &v;
    }
    if (!chosen) {
        chosen = &voices_[0];
        for (uint8_t i = 0; i < voiceLimit_; ++i) {
            auto& v = voices_[i];
            const float level = v.envelope * v.velocity * parameters_[v.track].sound.level;
            const float best = chosen->envelope * chosen->velocity * parameters_[chosen->track].sound.level;
            if (level < best || (level == best && v.age < chosen->age))
                chosen = &v;
        }
    }
    // A string keeps its oscillator, envelope and filter state across fingers.
    if (source == NoteSource::Live && parameters_[track].glide && chosen->stage != Stage::Off &&
        chosen->stage != Stage::Release && chosen->track == track && chosen->token == token && chosen->source == source) {
        auto& v = *chosen;
        v.pitch = pitch; v.target = frequencies_[pitch];
        v.glideLeft = parameters_[track].glide;
        v.glideStep = (v.target - v.frequency) * parameters_[track].inverseGlide;
        lastPitch_[track] = pitch;
        return;
    }
    const float tail = chosen->last;
    *chosen = Voice{};
    auto& v = *chosen;
    v.tail = tail;
    v.track = track; v.pitch=pitch;
    v.token = token;
    v.source = source;
    v.age = ++age_;
    v.stage = Stage::Attack;
    v.velocity = velocity * (1.0f / 127.0f);
    v.target = frequencies_[pitch];
    const auto& p = parameters_[track];
    const uint8_t from = p.glide && lastPitch_[track] != 255 ? lastPitch_[track] : pitch;
    v.frequency = frequencies_[from];
    v.glideLeft = p.glide;
    v.glideStep = (v.target - v.frequency) * p.inverseGlide;
    v.delta = phaseSteps_[from];
    v.inverseDelta = inverseSteps_[from];
    lastPitch_[track] = pitch;
}
GB_AUDIO_CODE void Synth::noteOff(uint8_t track, uint16_t token, NoteSource source) {
    for (auto& v : voices_)
        if (v.track == track && v.token == token && v.source == source && v.stage != Stage::Off &&
            v.stage != Stage::Release) {
            v.stage = Stage::Release;
            v.releaseStep = std::max(0.0000001f, v.envelope * parameters_[track].release);
        }
}
void Synth::allNotesOff(uint8_t track, NoteSource source) {
    for (auto& v : voices_)
        if (v.stage != Stage::Off && (track == 255 || track == v.track) &&
            (source == NoteSource::All || source == v.source))
            noteOff(v.track, v.token, v.source);
}
void Synth::updateOscillator(Voice& v) const {
    v.delta = std::min(0.45f, v.frequency * inverseRate_);
    const float ratio = v.delta * v.inverseDelta;
    // Glide normally changes by much less than 1% per control step. One Newton
    // iteration then bounds reciprocal error to 0.01%, avoiding a software divide.
    if (ratio > 0.99f && ratio < 1.01f)
        v.inverseDelta *= 2.0f - ratio;
    else
        v.inverseDelta = 1.0f / v.delta;
}
float Synth::oscillator(Voice& v, Waveform wave) const {
    const float delta = v.delta;
    float sample = 0;
    switch (wave) {
        case Waveform::Sine: {
            sample = sineLookup(v.phase);
            break;
        }
        case Waveform::Triangle:
            sample = 1 - 4 * std::fabs(v.phase - 0.5f);
            break;
        case Waveform::Saw:
            sample = 2 * v.phase - 1 - blep(v.phase, delta, v.inverseDelta);
            break;
        case Waveform::Square: {
            float other = v.phase + 0.5f;
            if (other >= 1)
                other -= 1;
            sample = (v.phase < 0.5f ? 1.0f : -1.0f) + blep(v.phase, delta, v.inverseDelta) -
                     blep(other, delta, v.inverseDelta);
            break;
        }
    }
    v.phase += delta;
    if (v.phase >= 1)
        v.phase -= 1;
    return sample;
}
void Synth::setVoiceLimit(uint8_t limit) {
    limit = std::max<uint8_t>(1, std::min<uint8_t>(limit, kSynthVoices));
    if (limit == voiceLimit_) return;
    for (uint8_t n = voiceLimit_; n > limit; --n) {
        Voice* chosen = &voices_[0];
        for (uint8_t i = 0; i < n; ++i) {
            auto& v = voices_[i];
            if (v.stage == Stage::Off) { chosen = &v; break; }
            if (v.envelope * v.velocity * parameters_[v.track].gain * mixGain_[v.track] <
                chosen->envelope * chosen->velocity * parameters_[chosen->track].gain * mixGain_[chosen->track])
                chosen = &v;
        }
        if (chosen->stage != Stage::Off) retiredTail_ += chosen->last * mixGain_[chosen->track];
        *chosen = voices_[n - 1];
        voices_[n - 1] = Voice{};
    }
    voiceLimit_ = limit;
}
GB_AUDIO_CODE float Synth::render() {
    float sum = 0;
    if (retiredTail_ != 0) {
        sum = retiredTail_; retiredTail_ *= 0.95f;
        if (std::fabs(retiredTail_) < 1e-7f) retiredTail_ = 0;
    }
    for (auto& v : voices_) {
        if (v.stage == Stage::Off)
            continue;
        const auto& p = parameters_[v.track];
        switch (v.stage) {
            case Stage::Attack:
                v.envelope = std::min(1.0f, v.envelope + p.attack);
                if (v.envelope >= 1)
                    v.stage = Stage::Decay;
                break;
            case Stage::Decay:
                v.envelope = std::max(p.sustain, v.envelope - (1 - p.sustain) * p.decay);
                if (v.envelope <= p.sustain)
                    v.stage = Stage::Sustain;
                break;
            case Stage::Sustain:
                v.envelope = p.sustain;
                break;
            case Stage::Release:
                v.envelope = std::max(0.0f, v.envelope - v.releaseStep);
                if (v.envelope == 0) {
                    v.stage = Stage::Off;
                    v.last = 0;
                    continue;
                }
                break;
            case Stage::Off:
                break;
        }
        if (v.glideLeft) {
            v.frequency += v.glideStep;
            if (--v.glideLeft == 0)
                v.frequency = v.target;
            // 2 kHz glide control keeps expensive reciprocals out of the sample loop.
            if (v.glideLeft % 16 == 0)
                updateOscillator(v);
        }
        const float input = oscillator(v, p.sound.wave);
        const float v3 = input - v.ic2;
        const float v1 = p.a1 * v.ic1 + p.a2 * v3;
        const float v2 = v.ic2 + p.a2 * v.ic1 + p.a3 * v3;
        v.ic1 = 2 * v1 - v.ic1;
        v.ic2 = 2 * v2 - v.ic2;
        v.tail *= 0.95f;
        v.last = v2 * v.envelope * v.velocity * p.gain + v.tail;
        sum += v.last * mixGain_[v.track];
    }
    return sum;
}
uint8_t Synth::activeVoices() const {
    uint8_t n = 0;
    for (const auto& v : voices_)
        if (v.stage != Stage::Off)
            ++n;
    return n;
}
int Synth::voicePitch(uint8_t track,uint16_t token,NoteSource source) const {
    for(const auto& v:voices_) if(v.stage!=Stage::Off && v.track==track && v.token==token && v.source==source) return v.pitch;
    return -1;
}
bool Synth::hasVoice(uint8_t track, uint16_t token, NoteSource source) const {
    for (const auto& v : voices_)
        if (v.stage != Stage::Off && v.track == track && v.token == token && v.source == source)
            return true;
    return false;
}
}  // namespace gb
