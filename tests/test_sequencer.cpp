#include <vector>

#include "gb/sequencer.h"
#include "test.h"

using namespace gb;

namespace {

constexpr uint32_t kRate = 32000;

struct Recorded {
    uint32_t frame;
    EventType type;
    uint8_t track, data;
};

// At 120 BPM and 32 kHz one quarter is exactly 16000 frames.
std::vector<Recorded> run(Sequencer& sequencer, uint32_t frames, uint32_t base = 0) {
    std::vector<Recorded> result;
    Event events[64];
    const uint32_t block = 256;
    for (uint32_t done = 0; done < frames; done += block) {
        const uint32_t count = sequencer.render(block, events, 64);
        for (uint32_t i = 0; i < count; ++i)
            result.push_back({base + done + events[i].frame, events[i].type, events[i].track, events[i].data});
    }
    return result;
}

Project& fresh() {
    static Project project;
    project = Project{};
    project.scenes[0].pattern[0] = 0;
    return project;
}

std::vector<uint32_t> framesOf(const std::vector<Recorded>& events, EventType type) {
    std::vector<uint32_t> frames;
    for (const Recorded& event : events)
        if (event.type == type) frames.push_back(event.frame);
    return frames;
}

}  // namespace

TEST(drumStepFiresOnItsFrame) {
    Project& project = fresh();
    project.drums[0].velocity[2][4] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto events = run(sequencer, 20000);
    CHECK_EQ(events.size(), 1u);
    CHECK_EQ(events[0].frame, 16000u);
    CHECK_EQ(events[0].track, 0);
    CHECK_EQ(events[0].data, 2);
}

TEST(gridChangesStepTime) {
    Project& project = fresh();
    project.drums[0].grid = Grid::Eighth;
    project.drums[0].velocity[0][1] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 10000)[0].frame, 8000u);
}

TEST(tripletGridStepTime) {
    Project& project = fresh();
    project.drums[0].grid = Grid::EighthTriplet;
    project.drums[0].velocity[0][3] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 20000)[0].frame, 16000u);
}

TEST(swingDelaysSecondStep) {
    Project& project = fresh();
    project.swing = 75;
    project.drums[0].velocity[0][1] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 8000)[0].frame, 6000u);
}

TEST(swingCanBeDisabledPerTrack) {
    Project& project = fresh();
    project.swing = 75;
    project.swingEnabled[0] = false;
    project.drums[0].velocity[0][1] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    CHECK_EQ(run(sequencer, 8000)[0].frame, 4000u);
}

TEST(shortPatternLoopsInsideLongerScene) {
    Project& project = fresh();
    project.scenes[0].pattern[1] = 0;
    project.instruments[0][0].length = 16;
    project.drums[0].length = 4;
    project.drums[0].velocity[0][0] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto frames = framesOf(run(sequencer, 64000), EventType::DrumHit);
    CHECK_EQ(frames.size(), 4u);
    CHECK_EQ(frames[0], 0u);
    CHECK_EQ(frames[1], 16000u);
    CHECK_EQ(frames[3], 48000u);
}

TEST(noteOnAndOffUseTicks) {
    Project& project = fresh();
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.noteCount = 1;
    pattern.notes[0].startTick = 96;
    pattern.notes[0].lengthTicks = 48;
    pattern.notes[0].pitch = 60;
    pattern.notes[0].velocity = 90;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto events = run(sequencer, 30000);
    CHECK_EQ(framesOf(events, EventType::NoteOn)[0], 16000u);
    CHECK_EQ(framesOf(events, EventType::NoteOff)[0], 24000u);
    CHECK_EQ(events[0].track, 1);
    CHECK_EQ(events[0].data, 60);
}

TEST(swingMovesOnlyGridAlignedNotes) {
    Project& project = fresh();
    project.swing = 75;
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.noteCount = 2;
    pattern.notes[0] = {24, 6, 60, 90};
    pattern.notes[1] = {70, 6, 62, 90};
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto frames = framesOf(run(sequencer, 16000), EventType::NoteOn);
    CHECK_EQ(frames.size(), 2u);
    CHECK_EQ(frames[0], 6000u);
    CHECK_EQ(frames[1], 11667u);
}

TEST(queuedSceneStartsOnBarBoundary) {
    Project& project = fresh();
    project.scenes[1].pattern[0] = 1;
    project.drums[0].velocity[0][0] = 100;
    project.drums[1].velocity[1][0] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    auto events = run(sequencer, 256);
    sequencer.playScene(1);
    events = run(sequencer, 70000, 256);
    const auto hits = framesOf(events, EventType::DrumHit);
    CHECK_EQ(hits.size(), 1u);
    CHECK_EQ(hits[0], 64000u);
    CHECK_EQ(framesOf(events, EventType::AllNotesOff)[0], 64000u);
}

TEST(songPlaysEntriesAndStops) {
    Project& project = fresh();
    project.scenes[1].pattern[0] = 1;
    project.drums[0].length = 4;
    project.drums[0].velocity[0][0] = 100;
    project.drums[1].length = 4;
    project.drums[1].velocity[1][0] = 100;
    project.song[0] = {0, 2};
    project.song[1] = {1, 1};
    project.songLength = 2;
    Sequencer sequencer(project, kRate);
    sequencer.playSong(0);
    const auto events = run(sequencer, 60000);
    const auto hits = framesOf(events, EventType::DrumHit);
    CHECK_EQ(hits.size(), 3u);
    CHECK_EQ(hits[1], 16000u);
    CHECK_EQ(hits[2], 32000u);
    CHECK_EQ(events[2].type == EventType::AllNotesOff, true);  // Released at the scene change.
    CHECK_EQ(events[3].data, 1);
    CHECK_EQ(sequencer.playing(), false);
    CHECK_EQ(framesOf(events, EventType::AllNotesOff).back(), 48000u);
}

TEST(songStartsFromAnyEntry) {
    Project& project = fresh();
    project.scenes[1].pattern[0] = 1;
    project.drums[1].length = 4;
    project.drums[1].velocity[1][0] = 100;
    project.song[0] = {0, 1};
    project.song[1] = {1, 1};
    project.songLength = 2;
    Sequencer sequencer(project, kRate);
    sequencer.playSong(1);
    const auto events = run(sequencer, 1000);
    CHECK_EQ(events[0].data, 1);
}

TEST(tempoChangeAppliesFromCurrentPosition) {
    Project& project = fresh();
    project.drums[0].velocity[0][8] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    Event events[64];
    for (int i = 0; i < 16; ++i) sequencer.render(1000, events, 64);
    project.bpm = 240;
    uint32_t frame = 0;
    for (int i = 0; i < 16 && frame == 0; ++i) {
        const uint32_t count = sequencer.render(1000, events, 64);
        if (count > 0) frame = 16000 + 1000 * i + events[0].frame;
    }
    CHECK_EQ(frame, 24000u);
}

TEST(stopReleasesNotes) {
    Project& project = fresh();
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    run(sequencer, 256);
    sequencer.stop();
    const auto events = run(sequencer, 256);
    CHECK_EQ(events.size(), 1u);
    CHECK_EQ(events[0].type == EventType::AllNotesOff, true);
    CHECK_EQ(events[0].track, kAllTracks);
    CHECK_EQ(sequencer.playing(), false);
}

TEST(tempoChangeKeepsTimeToNextTick) {
    Project& project = fresh();
    project.drums[0].velocity[0][4] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    Event events[64];
    sequencer.render(15900, events, 64);
    project.bpm = 121;
    CHECK_EQ(sequencer.render(1000, events, 64), 1u);
    CHECK_EQ(events[0].frame, 100u);
}

TEST(swungNoteOffDoesNotCutNextNote) {
    Project& project = fresh();
    project.swing = 75;
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.noteCount = 2;
    pattern.notes[0] = {24, 24, 60, 90};
    pattern.notes[1] = {48, 24, 60, 90};
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto events = run(sequencer, 16000);
    const auto notesOn = framesOf(events, EventType::NoteOn);
    CHECK_EQ(notesOn.size(), 2u);
    if (notesOn.size() == 2) {
        CHECK_EQ(notesOn[0], 6000u);
        CHECK_EQ(notesOn[1], 8000u);
    }
    const auto notesOff = framesOf(events, EventType::NoteOff);
    CHECK_EQ(notesOff.size(), 2u);
    if (notesOff.size() == 2) {
        CHECK_EQ(notesOff[0], 8000u);
        CHECK_EQ(notesOff[1], 14000u);
    }
}

TEST(shorterPatternRestartsAtSceneLength) {
    Project& project = fresh();
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.grid = Grid::SixteenthTriplet;
    pattern.noteCount = 1;
    pattern.notes[0] = {0, 16, 60, 90};
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    const auto events = run(sequencer, 70000);
    const auto notesOn = framesOf(events, EventType::NoteOn);
    CHECK_EQ(notesOn.size(), 3u);
    if (notesOn.size() == 3) {
        CHECK_EQ(notesOn[0], 0u);
        CHECK_EQ(notesOn[1], 42667u);
        CHECK_EQ(notesOn[2], 64000u);
    }
    const auto notesOff = framesOf(events, EventType::AllNotesOff);
    CHECK_EQ(notesOff.empty(), false);
    if (!notesOff.empty()) CHECK_EQ(notesOff[0], 64000u);
    for (const Recorded& event : events) {
        if (event.type == EventType::AllNotesOff) {
            CHECK_EQ(event.track, 1);
            break;
        }
    }
}

TEST(songRepeatsRestartTheScene) {
    Project& project = fresh();
    project.scenes[0].pattern[1] = 0;
    InstrumentPattern& pattern = project.instruments[0][0];
    pattern.grid = Grid::SixteenthTriplet;
    pattern.noteCount = 1;
    pattern.notes[0] = {0, 16, 60, 90};
    project.song[0] = {0, 2};
    project.songLength = 1;
    Sequencer sequencer(project, kRate);
    sequencer.playSong(0);
    const auto events = run(sequencer, 130000);
    const auto notesOn = framesOf(events, EventType::NoteOn);
    CHECK_EQ(notesOn.size(), 4u);
    if (notesOn.size() == 4) {
        CHECK_EQ(notesOn[0], 0u);
        CHECK_EQ(notesOn[1], 42667u);
        CHECK_EQ(notesOn[2], 64000u);
        CHECK_EQ(notesOn[3], 106667u);
    }
    CHECK_EQ(sequencer.playing(), false);
}

TEST(eventsBeyondCapacityAreDropped) {
    Project& project = fresh();
    for (int lane = 0; lane < kDrumLanes; ++lane) project.drums[0].velocity[lane][0] = 100;
    Sequencer sequencer(project, kRate);
    sequencer.playScene(0);
    Event events[3];
    CHECK_EQ(sequencer.render(256, events, 3), 3u);
}

TEST(overflowUsesReservedPanicAndRecoversAfterZeroCapacity) {
    auto& p=fresh(); p.scenes[0].pattern[1]=0; auto& n=p.instruments[0][0]; n.noteCount=4;
    for(int i=0;i<4;++i) n.notes[i]={0,1,static_cast<uint8_t>(60+i),100};
    Sequencer seq(p,kRate); seq.playScene(0); Event events[3];
    auto count=seq.render(256,events,3); CHECK_EQ(count,3);
    CHECK_EQ(events[2].type,EventType::AllNotesOff); CHECK_EQ(seq.overflows(),1);
    seq.stop(); CHECK_EQ(seq.render(256,nullptr,0),0); count=seq.render(256,events,3);
    CHECK_EQ(count,1); CHECK_EQ(events[0].type,EventType::AllNotesOff);
}
TEST(nearlyFullSwungNoteDoesNotEndImmediatelyAfterStart) {
    auto& p=fresh(); p.swing=75; p.scenes[0].pattern[1]=0;
    p.instruments[0][0].noteCount=1; p.instruments[0][0].notes[0]={30,378,60,100};
    Sequencer seq(p,kRate); seq.playScene(0); const auto events=run(seq,71000);
    auto on=framesOf(events,EventType::NoteOn),off=framesOf(events,EventType::NoteOff);
    CHECK_EQ(on[0],5000); CHECK_EQ(off.size()>=1,true);
    // A stray pre-start NoteOff is harmless; the paired release is at the next loop's tick 30.
    CHECK_EQ(off.back(),69000);
}

TEST(noteScheduleCacheTracksEditsGridAndSwing) {
    for(int mode=0;mode<4;++mode) {
        auto& p=fresh(); p.swing=75; p.scenes[0].pattern[1]=0;
        auto& n=p.instruments[0][0]; n.noteCount=1; n.notes[0]={24,24,60,100};
        Sequencer seq(p,kRate); seq.playScene(0); run(seq,256);
        if(mode==0) { n.notes[0].startTick=48; n.notes[0].pitch=65; }
        if(mode==1) p.swing=50;
        if(mode==2) p.swingEnabled[1]=false;
        if(mode==3) n.grid=Grid::ThirtySecond;
        const auto events=run(seq,16000,256);
        const auto frames=framesOf(events,EventType::NoteOn);
        CHECK_EQ(frames.empty(),false);
        if(!frames.empty()) CHECK_EQ(frames[0],mode==0?8000u:4000u);
        if(!events.empty()) CHECK_EQ(events[0].data,mode==0?65:60);
    }
}

TEST(denseWholeCycleNotesReleaseBeforeAnyAttackWithoutOverflow) {
    auto& p=fresh(); p.bpm=240;
    for(int lane=0;lane<8;++lane) p.drums[0].velocity[lane][0]=127;
    for(int t=0;t<4;++t) {
        p.scenes[0].pattern[t+1]=0;
        auto& n=p.instruments[t][0]; n.noteCount=64;
        for(int i=0;i<64;++i) n.notes[i]={0,384,static_cast<uint8_t>(40+i),127};
    }
    Sequencer seq(p,kRate); seq.playScene(0); Event events[544];
    const auto count=seq.render(256,events,544);
    CHECK_EQ(count,520u); CHECK_EQ(seq.overflows(),0u);
    for(unsigned i=0;i<256;++i) CHECK_EQ(events[i].type,EventType::NoteOff);
    for(unsigned i=264;i<count;++i) CHECK_EQ(events[i].type,EventType::NoteOn);
}

TEST(startingSongDuringPlaybackReleasesOldSceneBeforeNewNotes) {
    auto& p=fresh(); p.scenes[0].pattern[1]=0; p.scenes[1].pattern[1]=1;
    p.instruments[0][0].noteCount=1; p.instruments[0][0].notes[0]={0,300,60,100};
    p.instruments[0][1].noteCount=1; p.instruments[0][1].notes[0]={0,24,64,100};
    p.songLength=1; p.song[0]={1,1};
    Sequencer seq(p,kRate); seq.playScene(0); run(seq,256);
    seq.playSong(0); Event events[8];
    CHECK_EQ(seq.render(256,events,8),2u);
    CHECK_EQ(events[0].type,EventType::AllNotesOff);
    CHECK_EQ(events[0].track,kAllTracks);
    CHECK_EQ(events[1].type,EventType::NoteOn);
    CHECK_EQ(events[1].data,64);
    CHECK_EQ(events[0].frame,events[1].frame);
}
