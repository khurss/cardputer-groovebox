#pragma once
#include "gb/synth.h"
namespace gb {
// A fixed held-key table per track. Arp output has its own voice namespace.
class Arpeggiator {
public:
    explicit Arpeggiator(uint32_t rate) : rate_(rate) {}
    void configure(uint8_t track, const ArpSettings& settings, uint16_t bpm, Synth& synth);
    void tempo(uint16_t bpm);
    void on(uint8_t track, uint16_t token, NoteSource source, uint8_t pitch, uint8_t velocity, uint64_t frame);
    void off(uint8_t track, uint16_t token, NoteSource source, Synth& synth);
    void clear(uint8_t track, NoteSource source, Synth& synth);
    void render(uint64_t frame, Synth& synth);
    bool active() const { return activeMask_ != 0; }

private:
    struct Key {
        uint8_t pitch = 0, velocity = 0;
    };
    struct Track {
        ArpSettings settings;
        Key keys[kMaxNotes + 128];
        uint64_t next = 0, off = 0, period = 1;
        uint32_t step = 0;
        uint16_t count = 0;
        bool sounding = false;
    };
    int slot(uint16_t token, NoteSource source) const;
    void period(Track& track);
    uint32_t rate_;
    uint16_t bpm_ = 120;
    uint8_t activeMask_ = 0;
    Track tracks_[kInstrumentTracks];
};
}  // namespace gb
