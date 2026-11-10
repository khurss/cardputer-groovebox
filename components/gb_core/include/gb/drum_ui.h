#pragma once

#include <cstdint>

#include "gb/ui_types.h"
#include "gb/model.h"

namespace gb {

// The drum screen: turns keys into commands and draws the grid.
class DrumUi {
public:
    DrumUi(Project& project, CommandSink sink, void* context);

    void onKey(char code, bool pressed);
    void clearPattern();
    void setScene(uint8_t scene);
    void activate();
    void shortcut(char code) { dirty_ = true; if (code != 'g') gridRun_ = false; onFnPress(code); }
    void setTiming(bool grid, uint16_t value);
    void navigate(char arrow);
    void changePattern(int direction) { selectPattern(pattern_ + direction); }
    void draw(Canvas& canvas, uint32_t sceneTick, bool playing, bool storageAvailable) const;

    bool takeSaveRequest();
    bool takeDirty();
    int selectedLane() const { return lane_; }
    int currentPattern() const { return pattern_; }
    uint32_t editCount() const { return editCount_; }
    bool canUndo() const { return undoAvailable_; }
    bool canPaste() const { return clipboardFull_; }

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
    uint8_t editScene_ = 0;
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
