#include <cstring>

#include "gb/commands.h"
#include "gb/spsc.h"
#include "test.h"

using namespace gb;

namespace {
bool sameProject(const Project& a, const Project& b) { return std::memcmp(&a, &b, sizeof(Project)) == 0; }

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

TEST(invalidStepIsIgnored) {
    Project& p = fresh();
    const Project before = p;
    const Command commands[] = {
        make(CommandType::SetStep, 16, 0, 0, 90),
        make(CommandType::SetStep, 0, 8, 0, 90),
        make(CommandType::SetStep, 0, 0, 16, 90),
        make(CommandType::SetStep, 0, 0, 0, 128),
    };
    for (const Command& command : commands) {
        CHECK_EQ(isValidCommand(command), false);
        applyEdit(p, command);
    }
    CHECK_EQ(sameProject(p, before), true);
    CHECK_EQ(p.bpm, 120);
    CHECK_EQ(p.drums[0].velocity[0][0], 0);
}

TEST(invalidTempoSwingLengthAndGridAreIgnored) {
    Project& p = fresh();
    const Project before = p;
    const Command commands[] = {
        make(CommandType::SetBpm, 0, 0, 0, 300),
        make(CommandType::SetBpm, 0, 0, 0, 0),
        make(CommandType::SetSwing, 0, 0, 0, 49),
        make(CommandType::SetLength, 0, 0, 0, 5),
        make(CommandType::SetGrid, 0, 0, 0, 9),
    };
    for (const Command& command : commands) {
        CHECK_EQ(isValidCommand(command), false);
        applyEdit(p, command);
    }
    CHECK_EQ(sameProject(p, before), true);
    CHECK_EQ(p.bpm, 120);
    CHECK_EQ(p.swing, 50);
    CHECK_EQ(p.drums[0].length, 16);
    CHECK_EQ(p.drums[0].grid == Grid::Sixteenth, true);
}

TEST(invalidSoundAndScenePatternAreIgnored) {
    Project& p = fresh();
    const Project before = p;
    const Command commands[] = {
        make(CommandType::SetSound, 8, static_cast<uint8_t>(SoundParam::Decay), 0, 12),
        make(CommandType::SetSound, 2, 4, 0, 12),
        make(CommandType::SetScenePattern, 0, 0, 16),
    };
    for (const Command& command : commands) {
        CHECK_EQ(isValidCommand(command), false);
        applyEdit(p, command);
    }
    CHECK_EQ(sameProject(p, before), true);
    CHECK_EQ(p.sounds[2].decay, 64);
    CHECK_EQ(p.scenes[0].pattern[0], 0);
}

TEST(invalidDrumPatternIsIgnored) {
    Project& p = fresh();
    const Project before = p;
    Command command = make(CommandType::SetDrumPattern, 0);
    command.pattern.length = 3;
    CHECK_EQ(isValidCommand(command), false);
    applyEdit(p, command);
    CHECK_EQ(sameProject(p, before), true);
    CHECK_EQ(p.drums[0].length, 16);
}

TEST(validCommandsPass) {
    CHECK_EQ(isValidCommand(make(CommandType::SetStep, 3, 2, 5, 90)), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetLength, 1, 0, 0, 8)), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetGrid, 0, 0, 0, static_cast<uint16_t>(Grid::Eighth))), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetBpm, 0, 0, 0, 133)), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetSwing, 0, 0, 0, 62)), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetSound, 2, static_cast<uint8_t>(SoundParam::Decay), 0, 12)), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetScenePattern, 0, 0, 7)), true);
    CHECK_EQ(isValidCommand(make(CommandType::SetDrumPattern, 4)), true);
    CHECK_EQ(isValidCommand(make(CommandType::Play)), true);
    CHECK_EQ(isValidCommand(make(CommandType::Stop)), true);
    CHECK_EQ(isValidCommand(make(CommandType::Preview, 7, 127)), true);
    CHECK_EQ(isValidCommand(make(CommandType::Preview, 8)), false);
}
