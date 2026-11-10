#pragma once
#include <cstdint>

#include "gb/model.h"
namespace gb {
enum class NoteSource : uint8_t { Clip, Live, Arp, All };
// Hardware measurements include all eight drum lanes; leave room for future effects.
constexpr uint8_t kSynthVoices = 5;

// Fixed pool shared by all four tracks. Every method belongs to the audio task.
class Synth {
public:
    explicit Synth(uint32_t sampleRate);
    void setSound(uint8_t track, const SynthSound& sound);
    void noteOn(uint8_t track, uint8_t pitch, uint8_t velocity, uint16_t token, NoteSource source);
    void noteOff(uint8_t track, uint16_t token, NoteSource source);
    void allNotesOff(uint8_t track = 255, NoteSource source = NoteSource::All);
    void setVoiceLimit(uint8_t limit);
    float render();
    void setTrackGain(uint8_t track,float gain) { mixGain_[track]=gain; }
    uint8_t activeVoices() const;
    int voicePitch(uint8_t track,uint16_t token,NoteSource source) const;
    bool hasVoice(uint8_t track, uint16_t token, NoteSource source) const;

private:
    enum class Stage : uint8_t { Off, Attack, Decay, Sustain, Release };
    struct Parameters {
        SynthSound sound;
        float attack, decay, sustain, release;
        float a1, a2, a3, gain, inverseGlide;
        uint32_t glide;
    };
    struct Voice {
        Stage stage = Stage::Off;
        NoteSource source = NoteSource::Clip;
        uint8_t track = 0, pitch = 0;
        uint16_t token = 0;
        uint32_t age = 0;
        float phase = 0, frequency = 0, target = 0, glideStep = 0;
        float delta = 0, inverseDelta = 1;
        uint32_t glideLeft = 0;
        float envelope = 0, velocity = 0, releaseStep = 1;
        float ic1 = 0, ic2 = 0, last = 0, tail = 0;
    };
    float oscillator(Voice& voice, Waveform wave) const;
    void updateOscillator(Voice& voice) const;
    float rate_, inverseRate_;
    float frequencies_[128];
    float phaseSteps_[128], inverseSteps_[128];
    uint8_t lastPitch_[kInstrumentTracks] = {255, 255, 255, 255};
    uint32_t age_ = 0;
    uint8_t voiceLimit_ = kSynthVoices;
    float retiredTail_ = 0;
    float mixGain_[kInstrumentTracks]={1,1,1,1};
    Parameters parameters_[kInstrumentTracks];
    Voice voices_[kSynthVoices];
};
}  // namespace gb
