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
        const unsigned expectedBars=demo<3?82:demo<5?112:144;CHECK_EQ(bars,expectedBars);
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
        if(demo<5)for(int beat=0;beat<16;beat+=4)CHECK_EQ(drop.velocity[0][beat]>0,true);
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
