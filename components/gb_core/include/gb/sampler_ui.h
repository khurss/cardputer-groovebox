#pragma once
#include "gb/ui_types.h"
#include "gb/wav.h"
namespace gb {
enum class SampleJobType : uint8_t { Scan, Waveform, RecordStart, RecordStop, Store, Remove, Prepare };
struct SampleJob {
    SampleJobType type = SampleJobType::Scan;
    char source[kSampleNameLength] = {};
    uint32_t start = 0, end = 0;
    uint16_t page = 0;
    uint8_t count = 1, slots[kSampleSlots] = {};
};
struct SampleResult {
    SampleJob job;
    bool ok = false;
    char message[32] = {};
    uint32_t frames = 0;
    uint16_t files = 0;
    uint8_t visibleFiles = 0;
    char names[16][kSampleNameLength] = {};
    WavePeak peaks[kWaveColumns];
};
class SamplerUi {
public:
    SamplerUi(Project& p, CommandSink sink, void* context) : p_(p), sink_(sink), context_(context) {}
    void activate(uint8_t scene);
    void prepare() { SampleJob j; j.type = SampleJobType::Prepare; request(j); }
    bool storageBusy() const { return pending_ && (job_.type == SampleJobType::Store || job_.type == SampleJobType::Remove || job_.type == SampleJobType::Prepare); }
    void onKey(char code, bool fn, bool shift);
    void draw(Canvas& c, uint32_t tick, bool playing) const;
    void drawFnMenu(Canvas& c) const;
    bool takeJob(SampleJob& job);
    void receive(const SampleResult& result);
    void recordingProgress(uint32_t frames, uint16_t peak);
    bool busy() const { return pending_ || recording_; }
    bool recording() const { return recording_; }
    uint32_t edits() const { return edits_; }
    uint8_t pattern() const { return pattern_; }
    uint8_t pad() const { return pad_; }
    uint8_t slot() const { return p_.samplePatterns[pattern_].slot[pad_]; }
    uint32_t sourceFrames() const { return frames_; }
    uint32_t start() const { return start_; }
    uint32_t end() const { return end_; }
    const char* source() const { return source_; }
    const char* message() const { return message_; }
    bool sequencerMode() const { return sequencer_; }
    bool browserMode() const { return browser_; }
    bool canUndo() const { return undoAvailable_; }
    bool canPaste() const { return clipboardFull_; }
    uint8_t parts() const { return parts_; }
    void setMode(bool sequencer) { if (!busy()) { sequencer_ = sequencer; browser_ = false; dirty_ = true; } }
    void setParts(uint8_t value) { if (!busy() && (value == 4 || value == 8 || value == 16 || value == 32)) { parts_ = value; dirty_ = true; } }
    void saveSelection(bool chops) { if (!busy()) store(chops ? parts_ : 1); }
    void clearPattern();
    void undoPattern() { if (!busy() && undoAvailable_) { setPattern(undo_, false); undoAvailable_ = false; } }
    void setTiming(bool grid, uint16_t value);
    void selectPad(uint8_t pad) { if (!busy() && pad < kSamplePads) { pad_ = pad; dirty_ = true; } }
    void assignSlot(uint8_t slot) { if (!busy() && slot < kSampleSlots) { auto p = p_.samplePatterns[pattern_]; p.slot[pad_] = slot; setPattern(p); } }
    bool takeDirty() { bool d = dirty_; dirty_ = false; return d; }
private:
    void send(Command c);
    void setPattern(const SamplePattern& p, bool remember = true);
    void request(SampleJob job);
    void waveform(uint32_t begin, uint32_t end);
    void store(uint8_t count);
    Project& p_;
    CommandSink sink_;
    void* context_;
    SampleJob job_;
    SamplePattern undo_, clipboard_;
    WavePeak peaks_[kWaveColumns];
    char names_[16][kSampleNameLength] = {}, source_[kSampleNameLength] = {}, message_[32] = "Fn+B files / Alt record";
    uint32_t frames_ = 0, start_ = 0, end_ = 0, cursor_ = 0, viewStart_ = 0, viewEnd_ = 0, edits_ = 0;
    uint16_t page_ = 0, files_ = 0;
    uint8_t file_ = 0, visible_ = 0, pattern_ = 0, pad_ = 0, scene_ = 0, parts_ = 4;
    bool dirty_ = true, pending_ = false, jobReady_ = false, recording_ = false, sequencer_ = false, browser_ = true;
    bool initialized_ = false, undoAvailable_ = false, clipboardFull_ = false;
};
}
