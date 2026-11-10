#include "gb/demo_projects.h"
#include "gb/sequencer.h"
#include "gb/validate.h"
#include "test.h"
using namespace gb;
TEST(demoSongsAreCompleteBoundedAndEndWithoutMissingSamples){
    static Project p;
    for(unsigned demo=0;demo<kDemoCount;++demo){
        CHECK_EQ(makeDemoProject(demo,p),true);CHECK_EQ(isValidProject(p),true);CHECK_EQ(p.masterVolume,5);CHECK_EQ(p.songLength,29);
        unsigned bars=0;for(unsigned i=0;i<p.songLength;++i)bars+=p.song[i].repeats;CHECK_EQ(bars,82u);
        Sequencer s(p,32000);s.playSong(0);Event events[128];unsigned hits=0,notes=0;uint64_t frames=0;
        const uint64_t maximum=uint64_t(32000)*240*83/p.bpm;
        while(s.playing() && frames<maximum){const auto n=s.render(256,events,128);for(unsigned i=0;i<n;++i){hits+=events[i].type==EventType::DrumHit;notes+=events[i].type==EventType::NoteOn;}frames+=256;}
        CHECK_EQ(s.playing(),false);CHECK_EQ(hits>200,true);CHECK_EQ(notes>500,true);
        CHECK_EQ(frames>uint64_t(32000)*240*81/p.bpm,true);
        for(const auto& clip:p.samples)CHECK_EQ(clip.source[0],0);
    }
}
