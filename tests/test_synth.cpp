#include <cmath>

#include "gb/oscillator.h"
#include "gb/synth.h"
#include "test.h"
using namespace gb;
TEST(synthPitchEnvelopeAndSources) {
    Synth s(32000);
    SynthSound p;
    p.wave = Waveform::Sine;
    p.attack = 0;
    p.decay = 0;
    p.sustain = 127;
    p.cutoff = 127;
    p.release = 0;
    s.setSound(0, p);
    s.noteOn(0, 69, 127, 1, NoteSource::Live);
    s.noteOn(0, 69, 90, 1, NoteSource::Clip);
    int crossings = 0;
    float prev = 0, peak = 0;
    for (int i = 0; i < 32000; ++i) {
        float v = s.render();
        if (i > 1000 && v > 0 && prev <= 0)
            ++crossings;
        prev = v;
        peak = std::fmax(peak, std::fabs(v));
    }
    CHECK_EQ(crossings >= 425 && crossings <= 428, true);
    CHECK_EQ(peak > 0.05f, true);
    s.allNotesOff(255, NoteSource::Clip);
    for (int i = 0; i < 10; ++i) s.render();
    CHECK_EQ(s.hasVoice(0, 1, NoteSource::Live), true);
    CHECK_EQ(s.hasVoice(0, 1, NoteSource::Clip), false);
    s.noteOff(0, 999, NoteSource::Live);
    CHECK_EQ(s.activeVoices(), 1);
    s.noteOff(0, 1, NoteSource::Live);
    s.render();
    CHECK_EQ(s.activeVoices(), 0);
    CHECK_EQ(s.render() == 0, true);
}
TEST(synthStealsQuietestAndIgnoresItsLateRelease) {
    Synth s(32000);
    SynthSound p;
    p.attack = p.decay = p.release = 0;
    p.sustain = 127;
    for (int t = 0; t < 4; ++t) s.setSound(t, p);
    for (int i = 0; i < kSynthVoices; ++i) s.noteOn(i % 4, 60 + i, i == 0 ? 1 : 100, i, NoteSource::Clip);
    for (int i = 0; i < 100; ++i) s.render();
    s.noteOn(2, 72, 100, 90, NoteSource::Live);
    CHECK_EQ(s.activeVoices(), kSynthVoices);
    CHECK_EQ(s.hasVoice(0, 0, NoteSource::Clip), false);
    s.noteOff(0, 0, NoteSource::Clip);
    CHECK_EQ(s.hasVoice(2, 90, NoteSource::Live), true);
    s.allNotesOff();
    s.render();
    CHECK_EQ(s.activeVoices(), 0);
}
TEST(synthAllWaveformsAndFilterExtremesAreFinite) {
    Synth s(32000);
    for (int wave = 0; wave < 4; ++wave)
        for (int cutoff : {0, 127})
            for (int resonance : {0, 127}) {
                SynthSound p;
                p.wave = static_cast<Waveform>(wave);
                p.cutoff = cutoff;
                p.resonance = resonance;
                p.attack = p.decay = 0;
                p.sustain = 127;
                p.release = 0;
                p.glide = 127;
                s.setSound(0, p);
                s.noteOn(0, 0, 127, 0, NoteSource::Live);
                s.noteOn(0, 127, 127, 1, NoteSource::Live);
                bool finite = true;
                float peak = 0;
                for (int i = 0; i < 32000; ++i) {
                    const float v = s.render();
                    finite = finite && std::isfinite(v);
                    peak = std::fmax(peak, std::fabs(v));
                }
                CHECK_EQ(finite, true);
                CHECK_EQ(peak < 20, true);
                s.allNotesOff();
                s.render();
            }
}

TEST(sharedSineTableApproximatesCycleAndWrap) {
    float largest = 0;
    for (int i = 0; i < 32000; ++i) {
        const float phase = i / 32000.0f;
        largest = std::fmax(largest, std::fabs(gb::sineLookup(phase) - std::sin(6.28318530718f * phase)));
    }
    CHECK_EQ(largest < 0.00003f, true);
}

TEST(maximumReleaseFinishesWithinFourSeconds) {
    Synth synth(32000);
    SynthSound sound;
    sound.attack = 0;
    sound.decay = 0;
    sound.sustain = 127;
    sound.release = 127;
    synth.setSound(0, sound);
    synth.noteOn(0, 60, 127, 1, NoteSource::Live);
    for (int i = 0; i < 32000; ++i) synth.render();
    synth.allNotesOff();
    for (int i = 0; i < 129000; ++i) synth.render();
    CHECK_EQ(synth.activeVoices(), 0);
}

TEST(glideStartsAtPreviousPitchAndReachesTarget) {
    Synth synth(32000); SynthSound sound;
    sound.wave=Waveform::Sine; sound.attack=sound.decay=0; sound.sustain=127;
    sound.cutoff=127; sound.glide=127;
    synth.setSound(0,sound);
    synth.noteOn(0,69,100,1,NoteSource::Live);
    for(int i=0;i<32000;++i) synth.render();
    synth.noteOn(0,81,100,1,NoteSource::Live);
    int early=0,late=0; float previous=0;
    for(int i=0;i<36800;++i) {
        const float sample=synth.render();
        if(previous<=0 && sample>0) { early+=i<1600; late+=i>=35200; }
        previous=sample;
    }
    CHECK_EQ(early>=21 && early<=24,true);
    CHECK_EQ(late>=43 && late<=45,true);
}
TEST(legatoRetargetKeepsOneVoiceAndDoesNotRestartSlowAttack) {
    Synth s(32000); SynthSound p; p.wave=Waveform::Sine; p.attack=70; p.sustain=127; p.cutoff=127; p.glide=45;
    s.setSound(0,p); s.noteOn(0,60,100,120,NoteSource::Live);
    for(int i=0;i<32000;++i) s.render();
    float energy=0; for(int i=0;i<512;++i) {float v=s.render();energy+=v*v;}
    s.noteOn(0,72,100,120,NoteSource::Live);
    float after=0; for(int i=0;i<512;++i) {float v=s.render();after+=v*v;}
    CHECK_EQ(s.activeVoices(),1); CHECK_EQ(after>energy*0.4f,true);
    CHECK_EQ(s.voicePitch(0,120,NoteSource::Live),72);
    s.noteOff(0,120,NoteSource::Live); for(int i=0;i<32000;++i)s.render(); CHECK_EQ(s.activeVoices(),0);
}
