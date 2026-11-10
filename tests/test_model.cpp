#include "gb/model.h"
#include "test.h"

using namespace gb;

TEST(gridSizesAreIntegerTicks) {
    CHECK_EQ(gridTicks(Grid::Quarter), 96);
    CHECK_EQ(gridTicks(Grid::Eighth), 48);
    CHECK_EQ(gridTicks(Grid::Sixteenth), 24);
    CHECK_EQ(gridTicks(Grid::ThirtySecond), 12);
    CHECK_EQ(gridTicks(Grid::EighthTriplet), 32);
    CHECK_EQ(gridTicks(Grid::SixteenthTriplet), 16);
}

TEST(patternLengthDependsOnGrid) {
    CHECK_EQ(patternTicks(16, Grid::Sixteenth), 384);
    CHECK_EQ(patternTicks(4, Grid::Quarter), 384);
    CHECK_EQ(patternTicks(8, Grid::ThirtySecond), 96);
    CHECK_EQ(patternTicks(16, Grid::Quarter), 1536);
}

TEST(swingDelaysOnlyEverySecondStep) {
    CHECK_EQ(swingOffset(0, Grid::Sixteenth, 75), 0);
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 75), 12);
    CHECK_EQ(swingOffset(2, Grid::Sixteenth, 75), 0);
    CHECK_EQ(swingOffset(3, Grid::Eighth, 66), 15);
    CHECK_EQ(swingOffset(1, Grid::ThirtySecond, 75), 6);
}

TEST(straightSwingHasNoOffset) {
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 50), 0);
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 10), 0);
}

TEST(swingIsClampedToSeventyFive) {
    CHECK_EQ(swingOffset(1, Grid::Sixteenth, 99), 12);
}

TEST(quarterAndTripletGridsIgnoreSwing) {
    CHECK_EQ(swingOffset(1, Grid::Quarter, 75), 0);
    CHECK_EQ(swingOffset(1, Grid::EighthTriplet, 75), 0);
    CHECK_EQ(swingOffset(1, Grid::SixteenthTriplet, 75), 0);
}

TEST(newProjectIsEmptyAndStraight) {
    static Project project;
    CHECK_EQ(project.bpm, 120);
    CHECK_EQ(project.swing, 50);
    CHECK_EQ(project.scenes[0].pattern[0], kEmptyPattern);
    CHECK_EQ(project.drums[0].length, 16);
    CHECK_EQ(project.swingEnabled[4], true);
}
