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
