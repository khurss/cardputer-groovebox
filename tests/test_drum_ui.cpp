#include <string>
#include <vector>

#include "gb/commands.h"
#include "gb/drum_ui.h"
#include "test.h"

using namespace gb;

namespace {

struct Sent {
    std::vector<Command> commands;
};

bool sink(void* context, const Command& command) {
    static_cast<Sent*>(context)->commands.push_back(command);
    return true;
}

class FakeCanvas : public Canvas {
public:
    void fillRect(int x, int y, int w, int h, uint16_t color) override { rects.push_back({x, y, w, h, color}); }
    void text(int, int, const char* value, uint16_t) override { texts += value; texts += '\n'; }

    struct Rect {
        int x, y, w, h;
        uint16_t color;
    };
    std::vector<Rect> rects;
    std::string texts;

    int count(uint16_t color) const {
        int matches = 0;
        for (const Rect& rect : rects) matches += rect.color == color;
        return matches;
    }
};

Project& fresh() {
    static Project project;
    project = Project{};
    initProject(project);
    return project;
}

struct Fixture {
    Project& project = fresh();
    Sent sent;
    DrumUi ui{project, sink, &sent};

    void tap(char code) {
        ui.onKey(code, true);
        ui.onKey(code, false);
    }
    void withFn(char code) {
        ui.onKey(key::Fn, true);
        tap(code);
        ui.onKey(key::Fn, false);
    }
    void withShift(char code) {
        ui.onKey(key::Shift, true);
        tap(code);
        ui.onKey(key::Shift, false);
    }
    const Command& last() const { return sent.commands.back(); }
};

}  // namespace

TEST(stepKeyTogglesStepOnSelectedLane) {
    Fixture f;
    f.tap('3');
    f.tap('e');
    CHECK_EQ(f.project.drums[0].velocity[2][2], kNormalVelocity);
    CHECK_EQ(f.last().type == CommandType::SetStep, true);
    CHECK_EQ(f.last().b, 2);
    CHECK_EQ(f.last().c, 2);
    CHECK_EQ(f.last().value, kNormalVelocity);
    f.tap('e');
    CHECK_EQ(f.project.drums[0].velocity[2][2], 0);
}

TEST(secondRowMapsToLaterSteps) {
    Fixture f;
    f.tap('a');
    f.tap('k');
    CHECK_EQ(f.project.drums[0].velocity[0][8], kNormalVelocity);
    CHECK_EQ(f.project.drums[0].velocity[0][15], kNormalVelocity);
}

TEST(shiftSetsAccentAndBack) {
    Fixture f;
    f.withShift('q');
    CHECK_EQ(f.project.drums[0].velocity[0][0], kAccentVelocity);
    f.withShift('q');
    CHECK_EQ(f.project.drums[0].velocity[0][0], kNormalVelocity);
}

TEST(stepsPastPatternLengthAreIgnored) {
    Fixture f;
    f.withFn('l');  // 16 wraps to 4.
    CHECK_EQ(f.project.drums[0].length, 4);
    f.tap('t');
    CHECK_EQ(f.project.drums[0].velocity[0][4], 0);
}

TEST(laneKeySelectsAndPreviews) {
    Fixture f;
    f.tap('5');
    CHECK_EQ(f.ui.selectedLane(), 4);
    CHECK_EQ(f.last().type == CommandType::Preview, true);
    CHECK_EQ(f.last().a, 4);
}

TEST(lengthCycles) {
    Fixture f;
    f.withFn('l');
    f.withFn('l');
    CHECK_EQ(f.project.drums[0].length, 8);
    CHECK_EQ(f.last().type == CommandType::SetLength, true);
    f.withFn('l');
    CHECK_EQ(f.project.drums[0].length, 16);
}

TEST(gridCyclesAndRemaps) {
    Fixture f;
    f.tap('t');  // Step 4 is tick 96.
    f.withFn('g');
    CHECK_EQ(f.project.drums[0].grid == Grid::Eighth, true);
    CHECK_EQ(f.project.drums[0].velocity[0][2], kNormalVelocity);
    CHECK_EQ(f.last().type == CommandType::SetGrid, true);
}

TEST(swingCycles) {
    Fixture f;
    f.withFn('w');
    CHECK_EQ(f.project.swing, 54);
    for (int i = 0; i < 5; ++i) f.withFn('w');
    CHECK_EQ(f.project.swing, 75);
    f.withFn('w');
    CHECK_EQ(f.project.swing, 50);
    CHECK_EQ(f.last().type == CommandType::SetSwing, true);
}

TEST(tempoStepsAndClamps) {
    Fixture f;
    f.tap('=');
    CHECK_EQ(f.project.bpm, 121);
    f.tap('-');
    f.tap('-');
    CHECK_EQ(f.project.bpm, 119);
    f.project.bpm = 240;
    f.tap('=');
    CHECK_EQ(f.project.bpm, 240);
    f.project.bpm = 40;
    f.tap('-');
    CHECK_EQ(f.project.bpm, 40);
}

TEST(spaceStartsAndStopsAndRequestsSave) {
    Fixture f;
    f.tap(' ');
    CHECK_EQ(f.last().type == CommandType::Play, true);
    CHECK_EQ(f.ui.takeSaveRequest(), false);
    f.tap(' ');
    CHECK_EQ(f.last().type == CommandType::Stop, true);
    CHECK_EQ(f.ui.takeSaveRequest(), true);
    CHECK_EQ(f.ui.takeSaveRequest(), false);
}

TEST(patternKeysSwitchAndAssignScene) {
    Fixture f;
    f.tap(']');
    CHECK_EQ(f.ui.currentPattern(), 1);
    CHECK_EQ(f.project.scenes[0].pattern[0], 1);
    CHECK_EQ(f.last().type == CommandType::SetScenePattern, true);
    CHECK_EQ(f.last().c, 1);
    f.tap('[');
    f.tap('[');
    CHECK_EQ(f.ui.currentPattern(), 0);
}

TEST(undoRestoresLastEdit) {
    Fixture f;
    f.tap('q');
    f.tap('w');
    f.withFn('z');
    CHECK_EQ(f.project.drums[0].velocity[0][0], kNormalVelocity);
    CHECK_EQ(f.project.drums[0].velocity[0][1], 0);
    CHECK_EQ(f.last().type == CommandType::SetDrumPattern, true);
    const size_t before = f.sent.commands.size();
    f.withFn('z');  // Only one level of undo.
    CHECK_EQ(f.sent.commands.size(), before);
}

TEST(undoRestoresStepsLostToGridChange) {
    Fixture f;
    f.tap('w');  // Step 1 has no place on 1/8.
    f.withFn('g');
    CHECK_EQ(f.project.drums[0].velocity[0][1], 0);
    f.withFn('z');
    CHECK_EQ(f.project.drums[0].velocity[0][1], kNormalVelocity);
    CHECK_EQ(f.project.drums[0].grid == Grid::Sixteenth, true);
}

TEST(copyAndPastePattern) {
    Fixture f;
    f.tap('q');
    f.withFn('c');
    f.tap(']');
    f.withFn('v');
    CHECK_EQ(f.project.drums[1].velocity[0][0], kNormalVelocity);
    CHECK_EQ(f.last().type == CommandType::SetDrumPattern, true);
    CHECK_EQ(f.last().a, 1);
}

TEST(soundKeysAdjustSelectedLane) {
    Fixture f;
    f.tap('2');
    f.tap('x');
    CHECK_EQ(f.project.sounds[1].tune, 64 + kSoundStep);
    f.tap('z');
    f.tap('z');
    CHECK_EQ(f.project.sounds[1].tune, 64 - kSoundStep);
    f.tap('c');
    CHECK_EQ(f.project.sounds[1].decay, 64 - kSoundStep);
    f.tap('v');
    CHECK_EQ(f.project.sounds[1].decay, 64);
    f.tap('n');
    CHECK_EQ(f.project.sounds[1].tone, 64 + kSoundStep);
    f.tap(',');
    CHECK_EQ(f.project.sounds[1].level, 100 + kSoundStep);
    CHECK_EQ(f.last().type == CommandType::Preview, true);
}

TEST(soundValuesStayInRange) {
    Fixture f;
    f.project.sounds[0].tune = 3;
    f.tap('z');
    CHECK_EQ(f.project.sounds[0].tune, 0);
    f.project.sounds[0].tune = 125;
    f.tap('x');
    CHECK_EQ(f.project.sounds[0].tune, 127);
}

TEST(clearPatternEmptiesCurrentPattern) {
    Fixture f;
    f.tap('q');
    f.ui.clearPattern();
    CHECK_EQ(f.project.drums[0].velocity[0][0], 0);
    CHECK_EQ(f.last().type == CommandType::SetDrumPattern, true);
}

TEST(drawShowsActiveSteps) {
    Fixture f;
    f.tap('q');
    f.withShift('w');
    FakeCanvas canvas;
    f.ui.draw(canvas, 0, false, true);
    CHECK_EQ(canvas.count(color::StepOn), 1);
    CHECK_EQ(canvas.count(color::StepAccent), 1);
}

TEST(drawMarksPlayheadOnlyWhilePlaying) {
    Fixture f;
    FakeCanvas stopped, playing;
    f.ui.draw(stopped, 0, false, true);
    f.ui.draw(playing, 48, true, true);
    CHECK_EQ(stopped.count(color::Playhead), 0);
    CHECK_EQ(playing.count(color::Playhead), 1);
}

TEST(drawShowsStatus) {
    Fixture f;
    FakeCanvas canvas;
    f.ui.draw(canvas, 0, false, false);
    CHECK_EQ(canvas.texts.find("120") != std::string::npos, true);
    CHECK_EQ(canvas.texts.find("1/16") != std::string::npos, true);
    CHECK_EQ(canvas.texts.find("NO SD") != std::string::npos, true);
    CHECK_EQ(canvas.texts.find("KICK") != std::string::npos, true);
}

TEST(dirtyFlagFollowsEdits) {
    Fixture f;
    CHECK_EQ(f.ui.takeDirty(), true);  // The first frame must be drawn.
    CHECK_EQ(f.ui.takeDirty(), false);
    f.tap('q');
    CHECK_EQ(f.ui.takeDirty(), true);
}
