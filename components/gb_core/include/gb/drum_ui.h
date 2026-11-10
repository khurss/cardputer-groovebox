#pragma once

#include <cstdint>

#include "gb/commands.h"
#include "gb/model.h"

namespace gb {

namespace key {
constexpr char Fn = 1;
constexpr char Shift = 2;
constexpr char Ctrl = 3;
constexpr char Opt = 4;
constexpr char Alt = 5;
constexpr char Backspace = 8;
constexpr char Tab = 9;
constexpr char Enter = 10;
}  // namespace key

namespace color {
constexpr uint16_t Background = 0x0000;
constexpr uint16_t StepOff = 0x2124;
constexpr uint16_t StepOutside = 0x0841;
constexpr uint16_t StepOn = 0x07F0;
constexpr uint16_t StepAccent = 0xFFE0;
constexpr uint16_t Playhead = 0xF800;
constexpr uint16_t LaneSelected = 0x051F;
constexpr uint16_t Text = 0xFFFF;
constexpr uint16_t Warning = 0xFD20;
}  // namespace color

constexpr uint8_t kNormalVelocity = 100;
constexpr uint8_t kAccentVelocity = 127;
constexpr uint8_t kSoundStep = 8;

class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void text(int x, int y, const char* value, uint16_t color) = 0;
};

// The sink must accept every command: the interface applies edits to its own copy first,
// so a dropped command would leave the interface and audio copies of the project different.
using CommandSink = bool (*)(void* context, const Command& command);

// The drum screen: turns keys into commands and draws the grid.
class DrumUi {
public:
    DrumUi(Project& project, CommandSink sink, void* context);

    void onKey(char code, bool pressed);
    void clearPattern();
    void draw(Canvas& canvas, uint32_t sceneTick, bool playing, bool storageAvailable) const;

    bool takeSaveRequest();
    bool takeDirty();
    int selectedLane() const { return lane_; }
    int currentPattern() const { return pattern_; }
    uint32_t editCount() const { return editCount_; }

    void setWarning(const char* text) {
        warning_ = text;
        dirty_ = true;
    }

private:
    void onPress(char code);
    void onFnPress(char code);
    bool toggleStep(char code);
    bool selectLane(char code);
    bool adjustSound(char code);
    void send(const Command& command);
    void apply(const Command& command);
    void rememberForUndo();
    void undo();
    void selectPattern(int pattern);
    void cycleLength();
    void cycleGrid();
    void cycleSwing();
    void changeBpm(int delta);
    void toggleTransport();
    void applyPatternContents(const DrumPattern& contents);
    DrumPattern& currentDrums() { return project_.drums[pattern_]; }

    Project& project_;
    CommandSink sink_;
    void* context_;
    int lane_ = 0;
    int pattern_ = 0;
    bool fn_ = false;
    bool shift_ = false;
    bool running_ = false;
    bool saveRequested_ = false;
    bool dirty_ = true;
    const char* warning_ = nullptr;
    bool undoAvailable_ = false;
    bool clipboardFull_ = false;
    int undoPattern_ = 0;
    DrumPattern undoContents_;
    DrumPattern clipboard_;
    uint32_t editCount_ = 0;
    bool gridRun_ = false;
    DrumPattern gridRunOriginal_;
};

}  // namespace gb
