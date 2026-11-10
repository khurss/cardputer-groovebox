#include "gb/arpeggiator.h"

#include <algorithm>
namespace gb {
int Arpeggiator::slot(uint16_t token, NoteSource source) const {
    if (source == NoteSource::Clip && token < kMaxNotes) return token;
    if (source == NoteSource::Live && token < 128) return kMaxNotes + token;
    return -1;
}
void Arpeggiator::period(Track& t) {
    t.period = (uint64_t{rate_} * 60 * gridTicks(t.settings.rate) * 65536) / (bpm_ * kTicksPerQuarter);
}
void Arpeggiator::tempo(uint16_t bpm) {
    if (bpm < 40 || bpm > 240 || bpm == bpm_) return;
    bpm_ = bpm;
    for (auto& t : tracks_) period(t);
}
void Arpeggiator::configure(uint8_t track, const ArpSettings& settings, uint16_t bpm, Synth& synth) {
    if (track >= kInstrumentTracks) return;
    clear(track, NoteSource::All, synth);
    tempo(bpm);
    tracks_[track].settings = settings;
    period(tracks_[track]);
}
void Arpeggiator::on(uint8_t track, uint16_t token, NoteSource source, uint8_t pitch, uint8_t velocity,
                     uint64_t frame) {
    const int i = slot(token, source);
    if (track >= kInstrumentTracks || i < 0 || !velocity) return;
    auto& t = tracks_[track];
    if (!t.count) {
        t.next = frame << 16;
        t.step = 0;
    }
    if (!t.keys[i].velocity) ++t.count;
    t.keys[i] = {pitch, velocity};
    activeMask_ |= 1u << track;
}
void Arpeggiator::off(uint8_t track, uint16_t token, NoteSource source, Synth& synth) {
    const int i = slot(token, source);
    if (track >= kInstrumentTracks || i < 0) return;
    auto& t = tracks_[track];
    if (!t.keys[i].velocity) return;
    if (t.keys[i].velocity) {
        t.keys[i].velocity = 0;
        --t.count;
    }
    if (!t.count) {
        synth.noteOff(track, 0, NoteSource::Arp);
        t.sounding = false;
        activeMask_ &= ~(1u << track);
    }
}
void Arpeggiator::clear(uint8_t track, NoteSource source, Synth& synth) {
    for (uint8_t t = 0; t < kInstrumentTracks; ++t)
        if (track == 255 || track == t) {
            if (source == NoteSource::All || source == NoteSource::Arp) {
                for (auto& k : tracks_[t].keys) k.velocity = 0;
                tracks_[t].count = 0;
                tracks_[t].sounding = false;
                activeMask_ &= ~(1u << t);
                synth.noteOff(t, 0, NoteSource::Arp);
            } else
                for (int i = 0; i < (source == NoteSource::Clip ? kMaxNotes : 128); ++i) off(t, i, source, synth);
        }
}
void Arpeggiator::render(uint64_t frame, Synth& synth) {
    const uint64_t now = frame << 16;
    for (uint8_t i = 0; i < kInstrumentTracks; ++i) {
        if (!(activeMask_ & (1u << i))) continue;
        auto& t = tracks_[i];
        if (t.sounding && now >= t.off) {
            synth.noteOff(i, 0, NoteSource::Arp);
            t.sounding = false;
        }
        if (now < t.next) continue;
        uint8_t pitches[128] = {};
        int count = 0;
        for (const auto& k : t.keys)
            if (k.velocity)
                for (int octave = 0; octave < t.settings.octaves; ++octave) {
                    const int pitch = k.pitch + 12 * octave;
                    if (pitch <= 127) pitches[pitch] = std::max(pitches[pitch], k.velocity);
                }
        for (int p = 0; p < 128; ++p)
            if (pitches[p]) ++count;
        if (!count) continue;
        const uint32_t total = count;
        uint32_t index = t.step % total;
        if (t.settings.mode == ArpMode::Down)
            index = total - 1 - index;
        else if (t.settings.mode == ArpMode::UpDown && total > 1) {
            index = t.step % (2 * (total - 1));
            if (index >= total) index = 2 * (total - 1) - index;
        }
        int wanted = index;
        for (int p = 0; p < 128; ++p)
            if (pitches[p] && wanted-- == 0) {
                synth.noteOff(i, 0, NoteSource::Arp);
                synth.noteOn(i, p, pitches[p], 0, NoteSource::Arp);
                t.sounding = true;
                break;
            }
        ++t.step;
        t.off = now + std::max<uint64_t>(65536, t.period * t.settings.gate / 100);
        t.next += t.period;
        // Parameter/tempo edits may move a deadline into the past; never emit a burst to catch up.
        if (t.next <= now) t.next = now + t.period;
    }
}
}  // namespace gb
