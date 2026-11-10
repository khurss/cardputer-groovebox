#include "gb/validate.h"
#include "test.h"

using namespace gb;

namespace {
Project& fresh() {
    static Project project;
    project = Project{};
    return project;
}
}  // namespace

TEST(defaultProjectIsValid) { CHECK_EQ(isValidProject(fresh()), true); }

TEST(soundsHaveNeutralDefaults) {
    CHECK_EQ(fresh().sounds[0].tune, 64);
    CHECK_EQ(fresh().sounds[7].level, 100);
}

TEST(rejectsTempoOutOfRange) {
    Project& p = fresh();
    p.bpm = 0;
    CHECK_EQ(isValidProject(p), false);
    p.bpm = 241;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsSwingOutOfRange) {
    Project& p = fresh();
    p.swing = 49;
    CHECK_EQ(isValidProject(p), false);
    p.swing = 76;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsBadPatternLength) {
    Project& p = fresh();
    p.drums[3].length = 0;
    CHECK_EQ(isValidProject(p), false);
    p.drums[3].length = 17;
    CHECK_EQ(isValidProject(p), false);
    p.drums[3].length = 8;
    CHECK_EQ(isValidProject(p), true);
    p.instruments[2][5].length = 5;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsTooManyNotes) {
    Project& p = fresh();
    p.instruments[0][0].noteCount = kMaxNotes + 1;
    CHECK_EQ(isValidProject(p), false);
}

TEST(rejectsZeroLengthNote) {
    Project& p = fresh();
    p.instruments[0][0].noteCount = 1;
    p.instruments[0][0].notes[0] = {0, 0, 60, 90};
    CHECK_EQ(isValidProject(p), false);
    p.instruments[0][0].notes[0].lengthTicks = 24;
    CHECK_EQ(isValidProject(p), true);
}

TEST(rejectsBadScenePattern) {
    Project& p = fresh();
    p.scenes[1].pattern[0] = kPatternsPerTrack;
    CHECK_EQ(isValidProject(p), false);
    p.scenes[1].pattern[0] = kEmptyPattern;
    CHECK_EQ(isValidProject(p), true);
}

TEST(rejectsBadSong) {
    Project& p = fresh();
    p.songLength = kSongEntries + 1;
    CHECK_EQ(isValidProject(p), false);
    p.songLength = 1;
    p.song[0] = {kScenes, 1};
    CHECK_EQ(isValidProject(p), false);
    p.song[0] = {0, 0};
    CHECK_EQ(isValidProject(p), false);
    p.song[0] = {0, 1};
    CHECK_EQ(isValidProject(p), true);
}

TEST(coarserGridKeepsStepsThatLandOnIt) {
    DrumPattern pattern;
    pattern.velocity[1][4] = 100;  // Tick 96.
    pattern.velocity[1][1] = 100;  // Tick 24 has no place on 1/8.
    changeDrumGrid(pattern, Grid::Eighth);
    CHECK_EQ(pattern.grid == Grid::Eighth, true);
    CHECK_EQ(pattern.velocity[1][2], 100);
    CHECK_EQ(pattern.velocity[1][1], 0);
    CHECK_EQ(pattern.velocity[1][4], 0);
}

TEST(finerGridMovesStepsToSameTime) {
    DrumPattern pattern;
    pattern.grid = Grid::Eighth;
    pattern.velocity[0][1] = 77;  // Tick 48.
    changeDrumGrid(pattern, Grid::Sixteenth);
    CHECK_EQ(pattern.velocity[0][2], 77);
    CHECK_EQ(pattern.velocity[0][1], 0);
}

TEST(stepsPastTheGridAreDropped) {
    DrumPattern pattern;
    pattern.grid = Grid::Quarter;
    pattern.velocity[0][15] = 50;  // Tick 1440 needs step 120 on 1/32.
    changeDrumGrid(pattern, Grid::ThirtySecond);
    int count = 0;
    for (int step = 0; step < kMaxSteps; ++step) count += pattern.velocity[0][step] > 0;
    CHECK_EQ(count, 0);
}
