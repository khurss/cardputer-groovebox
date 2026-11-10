#pragma once
#include "gb/model.h"
#include "gb/sequencer.h"
#include "gb/spsc.h"
namespace gb {
enum class RecordUpdateType : uint8_t { Armed, Recording, Added, Full, Ended };
struct RecordUpdate {
    RecordUpdateType type = RecordUpdateType::Ended;
    uint8_t track = 0, pattern = 0, slot = 0;
    Note note{};
};
// Audio-owned recorder; popUpdate is the only method called by the UI task.
class Recorder {
public:
    Recorder(Project& project, Sequencer& sequencer) : project_(project), sequencer_(sequencer) {}
    void arm(uint8_t track, uint8_t pattern);
    void play();
    bool armed() const { return armed_; }
    void finish();
    void noteOn(uint8_t track, uint8_t key, uint8_t pitch, uint8_t velocity);
    void noteOff(uint8_t track, uint8_t key);
    bool locked(uint8_t track, uint8_t pattern) const {
        return (armed_ || draining_) && track == track_ && pattern == pattern_;
    }
    void acknowledge() {
        if (!armed_) draining_ = false;
    }
    void flush();
    bool popUpdate(RecordUpdate& update) { return updates_.pop(update); }

private:
    struct Held {
        bool active = false;
        uint8_t pitch = 0, velocity = 0;
        uint32_t tick = 0, local = 0;
    };
    void post(RecordUpdateType type, uint8_t slot = 0, Note note = {});
    Project& project_;
    Sequencer& sequencer_;
    SpscQueue<RecordUpdate, 128> updates_;
    // A pass can add at most 64 notes plus Armed/Recording/Full/Ended. Never wait on UI.
    RecordUpdate pending_[72];
    uint8_t pendingCount_ = 0;
    Held held_[128];
    uint8_t track_ = 0, pattern_ = 0, reserved_ = 0;
    bool armed_ = false, recording_ = false, draining_ = false, fullPosted_ = false;
};
}  // namespace gb
