#include "gb/arpeggiator.h"
#include "test.h"
using namespace gb;
TEST(arpOrdersChordAndSchedulesGate) {
    Synth synth(32000);
    SynthSound sound;
    sound.attack = sound.decay = sound.release = 0;
    sound.sustain = 127;
    synth.setSound(0, sound);
    Arpeggiator arp(32000);
    arp.configure(0, {ArpMode::Up, Grid::Sixteenth, 1, 50}, 120, synth);
    arp.on(0, 'z', NoteSource::Live, 67, 100, 0);
    arp.on(0, 'x', NoteSource::Live, 60, 100, 0);
    arp.on(0, 'c', NoteSource::Live, 64, 100, 0);
    arp.render(0, synth);
    synth.render();
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 60);
    arp.render(2000, synth);
    synth.render();
    CHECK_EQ(synth.activeVoices(), 0);
    arp.render(4000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 64);
    arp.render(8000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 67);
    arp.render(12000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 60);
    arp.clear(255, NoteSource::All, synth);
    synth.render();
    CHECK_EQ(arp.active(), false);
    CHECK_EQ(synth.activeVoices(), 0);
}
TEST(arpDownAndUpDownDoNotRepeatEndpointsAndClipReleaseKeepsLive) {
    Synth synth(32000);
    Arpeggiator arp(32000);
    arp.configure(0, {ArpMode::UpDown, Grid::Sixteenth, 1, 100}, 120, synth);
    for (int i = 0; i < 3; ++i)
        arp.on(0, i, NoteSource::Clip, 60 + i * 4, 100, 0);
    const int expected[] = {60, 64, 68, 64, 60};
    for (int i = 0; i < 5; ++i) {
        arp.render(i * 4000, synth);
        CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), expected[i]);
    }
    arp.on(0, 'z', NoteSource::Live, 72, 100, 20000);
    arp.clear(0, NoteSource::Clip, synth);
    arp.render(20000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 72);
    arp.configure(0, {ArpMode::Down, Grid::Sixteenth, 1, 50}, 120, synth);
    arp.on(0, 0, NoteSource::Clip, 60, 100, 24000);
    arp.on(0, 1, NoteSource::Clip, 67, 100, 24000);
    arp.render(24000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 67);
}
TEST(arpOctavesStaySortedAcrossWideChordsAndMidiLimit) {
    Synth synth(32000);
    Arpeggiator arp(32000);
    arp.configure(0, {ArpMode::Up, Grid::Sixteenth, 2, 50}, 120, synth);
    arp.on(0, 0, NoteSource::Clip, 60, 100, 0);
    arp.on(0, 1, NoteSource::Clip, 79, 100, 0);
    const int expected[] = {60, 72, 79, 91};
    for (int i = 0; i < 4; ++i) {
        arp.render(i * 4000, synth);
        CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), expected[i]);
    }
    arp.configure(0, {ArpMode::Up, Grid::ThirtySecond, 3, 50}, 240, synth);
    arp.on(0, 0, NoteSource::Clip, 127, 100, 16000);
    arp.render(16000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 127);
    arp.render(17000, synth);
    CHECK_EQ(synth.voicePitch(0, 0, NoteSource::Arp), 127);
}
