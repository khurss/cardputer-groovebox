#include "gb/commands.h"
#include "gb/spsc.h"
#include "test.h"

using namespace gb;

namespace {
Project& fresh() {
    static Project project;
    project = Project{};
    initProject(project);
    return project;
}
Command make(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command command;
    command.type = type;
    command.a = a;
    command.b = b;
    command.c = c;
    command.value = value;
    return command;
}
}  // namespace

TEST(initProjectAssignsFirstPatternToFirstScene) { CHECK_EQ(fresh().scenes[0].pattern[0], 0); }

TEST(setStepWritesVelocity) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetStep, 3, 2, 5, 90));
    CHECK_EQ(p.drums[3].velocity[2][5], 90);
}

TEST(setLengthChangesPattern) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetLength, 1, 0, 0, 8));
    CHECK_EQ(p.drums[1].length, 8);
}

TEST(setGridRemapsSteps) {
    Project& p = fresh();
    p.drums[0].velocity[0][4] = 100;
    applyEdit(p, make(CommandType::SetGrid, 0, 0, 0, static_cast<uint16_t>(Grid::Eighth)));
    CHECK_EQ(p.drums[0].velocity[0][2], 100);
    CHECK_EQ(p.drums[0].grid == Grid::Eighth, true);
}

TEST(setTempoAndSwing) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetBpm, 0, 0, 0, 133));
    applyEdit(p, make(CommandType::SetSwing, 0, 0, 0, 62));
    CHECK_EQ(p.bpm, 133);
    CHECK_EQ(p.swing, 62);
}

TEST(setSoundChangesOneParameter) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetSound, 2, static_cast<uint8_t>(SoundParam::Decay), 0, 12));
    CHECK_EQ(p.sounds[2].decay, 12);
    CHECK_EQ(p.sounds[2].tune, 64);
}

TEST(setScenePatternSelectsPattern) {
    Project& p = fresh();
    applyEdit(p, make(CommandType::SetScenePattern, 0, 0, 7));
    CHECK_EQ(p.scenes[0].pattern[0], 7);
}

TEST(setDrumPatternReplacesWholePattern) {
    Project& p = fresh();
    Command command = make(CommandType::SetDrumPattern, 4);
    command.pattern.length = 4;
    command.pattern.velocity[0][0] = 11;
    applyEdit(p, command);
    CHECK_EQ(p.drums[4].length, 4);
    CHECK_EQ(p.drums[4].velocity[0][0], 11);
}

TEST(transportCommandsDoNotEditTheProject) {
    Project& p = fresh();
    const uint16_t before = p.bpm;
    applyEdit(p, make(CommandType::Play, 0, 0, 0, 200));
    CHECK_EQ(p.bpm, before);
}

TEST(queueKeepsOrderAndReportsFull) {
    SpscQueue<int, 4> queue;
    CHECK_EQ(queue.push(1), true);
    CHECK_EQ(queue.push(2), true);
    CHECK_EQ(queue.push(3), true);
    CHECK_EQ(queue.push(4), false);  // One slot stays empty to tell full from empty.
    int value = 0;
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(value, 1);
    CHECK_EQ(queue.push(4), true);
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(value, 2);
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(queue.pop(value), true);
    CHECK_EQ(value, 4);
    CHECK_EQ(queue.pop(value), false);
}
