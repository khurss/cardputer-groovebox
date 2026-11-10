#pragma once
#include <atomic>
#include <cstdint>

#include "gb/arpeggiator.h"
#include "gb/commands.h"
#include "gb/drums.h"
#include "gb/recorder.h"
#include "gb/synth.h"
#include "gb/sample_player.h"
#include "gb/equalizer.h"
namespace gb {
class Engine {
public:
    Engine(Project& project, uint32_t sampleRate);
    void handle(const Command& command);
    void render(int16_t* stereo, uint32_t frames);
    uint32_t transport() const { return transport_.load(std::memory_order_relaxed); }
    uint8_t outputLimit() const { return outputLimit_; }
    bool playing() const { return playing_.load(std::memory_order_relaxed); }
    uint32_t sceneTick() const { return sceneTick_.load(std::memory_order_relaxed); }
    uint32_t eventOverflows() const { return overflows_.load(std::memory_order_relaxed); }
    uint32_t activeVoices() const { return voices_.load(std::memory_order_relaxed); }
    uint32_t sampleVoices() const { return sampleVoices_.load(); }
    uint32_t sampleEpoch() const { return sampleEpoch_.load(); }
    uint8_t voiceBudget() const { return voiceBudget_.load(); }
    bool popRecordUpdate(RecordUpdate& update) { return recorder_.popUpdate(update); }

private:
    // 4 ticks/block at 240 BPM: <=512 note events, <=8 drum hits, plus releases.
    static constexpr uint32_t kMaxEvents = 576;
    int16_t limit(float sample);
    void renderBlock(int16_t* stereo, uint32_t frames);
    void releaseClip(uint8_t track);
    void updateMixer();
    void startNote(uint8_t track, uint8_t pitch, uint8_t velocity, uint16_t token, NoteSource source, uint64_t frame);
    void endNote(uint8_t track, uint16_t token, NoteSource source);
    void silence(uint8_t track = 255, NoteSource source = NoteSource::All);
    Project& project_;
    Sequencer sequencer_;
    DrumRack rack_;
    Synth synth_;
    Recorder recorder_;
    Arpeggiator arp_;
    SamplePlayer samples_;
    Equalizer equalizer_;
    float sampleGain_ = 1;
    std::atomic<uint32_t> sampleEpoch_{0}, sampleVoices_{0};
    std::atomic<uint8_t> voiceBudget_{kSynthVoices};
    uint64_t frames_ = 0;
    uint16_t transportSerial_ = 0;
    float drumGain_ = 1, masterGain_ = 1, masterTarget_ = 1;
    uint8_t outputLimit_ = 100;
    InstrumentPattern staged_;
    uint8_t stagedTrack_ = 0, stagedPattern_ = 0;
    bool staging_ = false, stagingValid_ = false;
    float limiterGain_ = 1.0f, releaseCoefficient_;
    std::atomic<uint32_t> sceneTick_{0}, overflows_{0}, voices_{0}, transport_{0};
    std::atomic<bool> playing_{false};
    Event events_[kMaxEvents];
};
}  // namespace gb
