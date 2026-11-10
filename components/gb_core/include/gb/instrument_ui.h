#pragma once
#include "gb/recorder.h"
#include "gb/ui_types.h"
namespace gb {
class InstrumentUi {
public:
    InstrumentUi(Project& project, CommandSink sink, void* context);
    void onKey(char code, bool pressed, bool fn, bool shift);
    void toggleRecording();
    void activate();
    void setScene(uint8_t scene);
    void setVisualTime(uint32_t milliseconds, bool playing);
    bool back();
    void setTick(uint32_t tick) {
        tick_ = tick;
        if (recording_)
            dirty_ = true;
    }
    int focusPitch() const { return cursorPitch_; }
    bool prepareExit();
    void receive(const RecordUpdate& update);
    void clearPattern();
    void draw(Canvas& canvas, uint32_t sceneTick, bool playing, bool storageAvailable, const char* warning) const;
    void drawFnSettings(Canvas& canvas) const;
    bool takeDirty() {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }
    uint32_t editCount() const { return edits_; }
    uint8_t track() const { return track_; }
    uint8_t pattern() const { return pattern_; }
    bool busy() const { return locked_; }
    bool armed() const { return armed_; }
    bool recording() const { return recording_; }
    int octave() const { return octave_; }
    bool fitsNotes() const { return fitNotes_; }
    bool parametersVisible() const { return parameters_; }
    bool canUndo() const { return undoAvailable_; }
    bool canPaste() const { return clipboardFull_; }
    void showPerformance();
    void showPianoRoll() { releaseLive(); performance_ = parameters_ = false; dirty_ = true; }
    bool performanceVisible() const { return performance_; }
    void setKeyboard(uint8_t parameter, uint8_t value);
    void loadPreset(uint8_t index);
    static const char* presetName(uint8_t index);
    uint8_t preset() const { return preset_; }
    void showParameters() { releaseLive(); parameters_ = true; performance_ = false; dirty_ = true; }
    void setTiming(bool grid, uint16_t value);
    void setSynthParameter(uint8_t parameter, uint16_t value);
    void setArpeggiator(uint8_t parameter, uint16_t value);

private:
    struct Held {
        bool active = false;
        uint8_t track = 0, pitch = 0;
        uint16_t start = 0;
    };
    bool send(Command c);
    void apply(Command c);
    void remember();
    void rememberCommand(CommandType type, uint8_t parameter, uint16_t value);
    void replace(uint8_t track, uint8_t pattern, const InstrumentPattern& contents);
    void selectTarget(int track, int pattern);
    void releaseLive();
    void move(char code, bool shift);
    void enter();
    void fnPress(char code, bool shift);
    InstrumentPattern& current() { return project_.instruments[track_][pattern_]; }
    const InstrumentPattern& current() const { return project_.instruments[track_][pattern_]; }
    Project& project_;
    CommandSink sink_;
    void* context_;
    Held held_[128];
    InstrumentPattern clipboard_, undo_;
    uint8_t track_ = 0, pattern_ = 0, undoTrack_ = 0, undoPattern_ = 0;
    int8_t pendingTrack_ = -1, pendingPattern_ = -1;
    int selected_ = -1, cursorPitch_ = 60, octave_ = 4;
    uint16_t cursorTick_ = 0;
    uint8_t parameter_ = 0, editScene_ = 0;
    uint32_t tick_ = 0;
    static constexpr int kTrail = 100;
    int16_t trails_[4][kTrail] = {};
    int32_t smoothPitch_[4] = {-1,-1,-1,-1};
    int32_t visualLow_ = 48*256, visualHigh_ = 84*256;
    uint32_t visualMs_ = 0;
    uint8_t trailCount_ = 0;
    bool visualPlaying_ = false;
    void focusPattern();
    bool dirty_ = true, locked_ = false, armed_ = false, recording_ = false, ending_ = false, full_ = false;
    bool performance_ = false;
    uint8_t preset_ = 0;
    uint32_t heldOrder_ = 0;
    uint32_t keyOrder_[128] = {};
    int stringKey(int row) const;
    void drawPerformance(Canvas& canvas) const;
    bool parameters_ = false, clipboardFull_ = false, undoAvailable_ = false, fitNotes_ = true;
    bool soundUndo_ = false;
    SynthSound undoSound_;
    bool commandUndo_ = false;
    CommandType undoType_ = CommandType::Stop;
    uint8_t undoParameter_ = 0;
    uint16_t undoValue_ = 0;
    uint32_t edits_ = 0;
};
}  // namespace gb
