#include "gb/demo_projects.h"
#include "gb/sequencer.h"
#include "gb/validate.h"
#include "gb/project_io.h"
#include "test.h"
#include <cstring>
using namespace gb;
TEST(demoSongsAreCompleteBoundedAndEndWithoutMissingSamples){
    static Project p;
    for(unsigned demo=0;demo<kDemoCount;++demo){
        CHECK_EQ(makeDemoProject(demo,p),true);CHECK_EQ(isValidProject(p),true);CHECK_EQ(p.masterVolume,5);CHECK_EQ(p.songLength,29);
        unsigned bars=0;for(unsigned i=0;i<p.songLength;++i)bars+=p.song[i].repeats;
        const unsigned expectedBars=demo<3?82:demo<5?112:demo<7?144:128;CHECK_EQ(bars,expectedBars);
        Sequencer s(p,32000);s.playSong(0);Event events[128];unsigned hits=0,notes=0;uint64_t frames=0;
        const uint64_t maximum=uint64_t(32000)*240*(bars+1)/p.bpm;
        while(s.playing() && frames<maximum){const auto n=s.render(256,events,128);for(unsigned i=0;i<n;++i){hits+=events[i].type==EventType::DrumHit;notes+=events[i].type==EventType::NoteOn;}frames+=256;}
        CHECK_EQ(s.playing(),false);CHECK_EQ(hits>200,true);CHECK_EQ(notes>500,true);
        CHECK_EQ(frames>uint64_t(32000)*240*(bars-1)/p.bpm,true);
        for(const auto& clip:p.samples)CHECK_EQ(clip.source[0],0);
    }
}
TEST(danceDemosKeepGenreRhythmsAndRoundTripTheirWholeArrangements) {
    static Project p,loaded;static uint8_t data[kProjectBufferSize];
    for(unsigned demo=3;demo<kDemoCount;++demo) {
        CHECK_EQ(makeDemoProject(demo,p),true);
        const auto size=serializeProject(p,data,sizeof(data));CHECK_EQ(size>0,true);
        CHECK_EQ(deserializeProject(data,size,loaded),LoadResult::Ok);
        CHECK_EQ(loaded.bpm,p.bpm);CHECK_EQ(loaded.songLength,p.songLength);
        CHECK_EQ(std::memcmp(loaded.scenes,p.scenes,sizeof(p.scenes)),0);
        CHECK_EQ(std::memcmp(loaded.song,p.song,sizeof(p.song)),0);
        for(unsigned track=0;track<kInstrumentTracks;++track)for(unsigned pattern=0;pattern<kPatternsPerTrack;++pattern) {
            const auto& a=p.instruments[track][pattern];const auto& b=loaded.instruments[track][pattern];
            CHECK_EQ(a.noteCount,b.noteCount);CHECK_EQ(std::memcmp(a.notes,b.notes,a.noteCount*sizeof(Note)),0);
        }
        const auto& drop=p.drums[p.scenes[8].pattern[0]];
        CHECK_EQ(drop.velocity[1][4]>0 && drop.velocity[1][12]>0,true);
        if(demo<5 || demo>=7)for(int beat=0;beat<16;beat+=4)CHECK_EQ(drop.velocity[0][beat]>0,true);
        else {
            CHECK_EQ(p.bpm>=170,true);CHECK_EQ(drop.velocity[0][4],0);
            CHECK_EQ(drop.velocity[0][6]>0 || drop.velocity[0][7]>0,true);
            CHECK_EQ(drop.velocity[1][3]>0 && drop.velocity[1][3]<drop.velocity[1][4],true);
        }
        CHECK_EQ(p.scenes[0].pattern[1],kEmptyPattern); // intro has no bass
        CHECK_EQ(p.scenes[8].pattern[1]>=0,true);        // bass enters the drop
        CHECK_EQ(p.scenes[12].pattern[1],kEmptyPattern);// breakdown clears the low end
    }
}
TEST(hardTechnoDemosHaveDistinctRiffsAndDrivenFourOnTheFloorDrops) {
    static Project a,b;
    CHECK_EQ(makeDemoProject(7,a),true);CHECK_EQ(makeDemoProject(8,b),true);
    CHECK_EQ(a.bpm,154);CHECK_EQ(b.bpm,162);
    for(const auto* p:{&a,&b}) {
        CHECK_EQ(p->sounds[0].tone>=110,true);CHECK_EQ(p->sounds[0].level,127);
        CHECK_EQ(p->synths[0].wave==Waveform::Sine,false);
        const auto& d=p->drums[p->scenes[8].pattern[0]];
        for(int beat=0;beat<16;beat+=4)CHECK_EQ(d.velocity[0][beat]>=120,true);
        for(int step=0;step<16;++step)CHECK_EQ(d.velocity[3][step]>0,true);
        CHECK_EQ(p->instruments[0][4].noteCount,8);
        CHECK_EQ(p->instruments[3][4].noteCount>0,true);
        CHECK_EQ(p->drums[15].velocity[0][0],0);
        CHECK_EQ(p->drums[15].velocity[1][15]>p->drums[15].velocity[1][8],true);
    }
    CHECK_EQ(std::memcmp(a.instruments[0][4].notes,b.instruments[0][4].notes,8*sizeof(Note))!=0,true);
    CHECK_EQ(std::strcmp(demoName(7),"steel-rush"),0);
    CHECK_EQ(std::strcmp(demoName(8),"redline-foundry"),0);
}
TEST(darkSynthDemosKeepSteelRushBeatsAndGiveTheLeadItsOwnMelody) {
    static Project steel,a,b;
    makeDemoProject(7,steel);makeDemoProject(9,a);makeDemoProject(10,b);
    for(const auto* p:{&a,&b}) {
        CHECK_EQ(p->bpm,steel.bpm);CHECK_EQ(p->swing,steel.swing);
        CHECK_EQ(std::memcmp(p->sounds,steel.sounds,sizeof(steel.sounds)),0);
        CHECK_EQ(std::memcmp(p->drums,steel.drums,sizeof(steel.drums)),0);
        CHECK_EQ(std::memcmp(p->song,steel.song,sizeof(steel.song)),0);
        CHECK_EQ(p->mixer[0].volume,steel.mixer[0].volume);
        CHECK_EQ(p->mixer[4].volume,100);
        CHECK_EQ(p->synths[3].glide>0,true);CHECK_EQ(p->synths[3].sustain>=90,true);
        for(int pat=0;pat<16;++pat) {
            const auto& bass=p->instruments[0][pat];const auto& old=steel.instruments[0][pat];
            CHECK_EQ(bass.noteCount,old.noteCount);
            for(unsigned n=0;n<bass.noteCount;++n) {
                CHECK_EQ(bass.notes[n].startTick,old.notes[n].startTick);
                CHECK_EQ(bass.notes[n].lengthTicks,old.notes[n].lengthTicks);
                CHECK_EQ(bass.notes[n].velocity,old.notes[n].velocity);
            }
            const auto& lead=p->instruments[3][pat];CHECK_EQ(lead.noteCount>0,true);
            for(unsigned n=1;n<lead.noteCount;++n)
                CHECK_EQ(lead.notes[n-1].startTick+lead.notes[n-1].lengthTicks<=lead.notes[n].startTick,true);
            if(pat>=4 && pat<8)CHECK_EQ(lead.noteCount,6);
        }
        for(const auto& scene:p->scenes)CHECK_EQ(scene.pattern[4]>=0,true);
    }
    CHECK_EQ(a.synths[3].wave!=b.synths[3].wave,true);
    CHECK_EQ(std::memcmp(a.instruments[3][4].notes,b.instruments[3][4].notes,6*sizeof(Note))!=0,true);
}
