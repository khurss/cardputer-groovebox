#pragma once
#include <atomic>
#include <cstdio>
#include "esp_partition.h"
#include "gb/engine.h"
#include "gb/sampler_ui.h"
#include "gb/spsc.h"
#include "gb/wav.h"
class SamplerStorage {
public:
    void start(gb::Project& project, gb::Engine& engine, gb::CommandSink sink, void* context, bool testMode);
    bool submit(const gb::SampleJob& job) { return running_ && jobs_.push(job); }
    bool poll(gb::SampleResult& result) { return results_.pop(result); }
    void capture(const int16_t* stereo); // audio task only; never touches SD
    uint32_t recordedFrames() const { return recordedFrames_.load(); }
    uint16_t takePeak() { return peak_.exchange(0); }
    uint32_t dropped() const { return dropped_.load(); }
    bool recording() const { return recordState_.load() != Idle; }
    bool ready() const { return ready_.load(); }
    uint32_t cacheFrames() const { return cacheFrames_.load(); }
    const char* directory() const { return root_; }
private:
    enum RecordState : uint8_t { Idle, Recording, StopRequested, Stopped, Overrun };
    struct RecordBlock { int16_t pcm[256]; };
    static void task(void* context);
    void run();
    void execute(const gb::SampleJob& job);
    bool cache(const gb::SampleJob* store);
    void scan();
    void waveform();
    void startRecording();
    void stopRecording();
    void drainRecording(bool final);
    void publish();
    void error(const char* message);
    gb::Project* project_ = nullptr;
    gb::Engine* engine_ = nullptr;
    gb::CommandSink sink_ = nullptr;
    void* context_ = nullptr;
    const esp_partition_t* partition_ = nullptr;
    const void* mapped_ = nullptr;
    esp_partition_mmap_handle_t mapHandle_ = 0;
    gb::SpscQueue<gb::SampleJob, 2> jobs_;
    gb::SpscQueue<gb::SampleResult, 2> results_;
    gb::SpscQueue<RecordBlock, 32> recordBlocks_;
    std::atomic<RecordState> recordState_{Idle};
    std::atomic<uint32_t> recordedFrames_{0}, dropped_{0}, cacheFrames_{0};
    std::atomic<uint16_t> peak_{0};
    std::atomic<bool> ready_{false};
    gb::SampleResult result_;
    gb::SampleClip clips_[gb::kSampleSlots];
    gb::WavReader reader_;
    alignas(4) uint8_t io_[4096];
    char root_[48] = {}, recordName_[gb::kSampleNameLength] = {}, recordPath_[112] = {}, recordTemporary_[120] = {};
    FILE* recordFile_ = nullptr;
    uint32_t writtenFrames_ = 0;
    unsigned buffered_ = 0;
    bool writeFailed_ = false;
    bool running_ = false;
};
