#include "gb/drum_ui.h"

#include <cstdio>
#include <cstring>

#include "gb/validate.h"

namespace gb {

namespace {

constexpr char kStepKeys[] = "qwertyuiasdfghjk";
constexpr int kCellWidth = 15;
constexpr int kCellHeight = 12;
constexpr int kGridTop = 12;
constexpr int kFooterTop = kGridTop + kCellHeight * kDrumLanes + 2;
constexpr uint8_t kSwingValues[] = {50, 54, 58, 62, 66, 70, 75};
constexpr Grid kGridCycle[] = {Grid::Sixteenth, Grid::Eighth, Grid::Quarter,
                               Grid::ThirtySecond, Grid::SixteenthTriplet, Grid::EighthTriplet};
constexpr const char* kLaneNames[kDrumLanes] = {"KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "TOM", "RIM", "PERC"};

const char* gridName(Grid grid) {
    switch (grid) {
        case Grid::Quarter: return "1/4";
        case Grid::Eighth: return "1/8";
        case Grid::Sixteenth: return "1/16";
        case Grid::ThirtySecond: return "1/32";
        case Grid::EighthTriplet: return "1/8T";
        case Grid::SixteenthTriplet: return "1/16T";
    }
    return "?";
}

int stepForKey(char code) {
    const char* found = code != '\0' ? strchr(kStepKeys, code) : nullptr;
    return found ? static_cast<int>(found - kStepKeys) : -1;
}

uint8_t stepped(uint8_t value, int delta) {
    const int result = value + delta;
    return static_cast<uint8_t>(result < 0 ? 0 : (result > 127 ? 127 : result));
}

Command command(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command out;
    out.type = type;
    out.a = a;
    out.b = b;
    out.c = c;
    out.value = value;
    return out;
}

}  // namespace

DrumUi::DrumUi(Project& project, CommandSink sink, void* context)
    : project_(project), sink_(sink), context_(context) {
    const int8_t playing = project.scenes[0].pattern[0];
    pattern_ = playing != kEmptyPattern ? playing : 0;
}

void DrumUi::send(const Command& out) {
    sink_(context_, out);
    dirty_ = true;
}

void DrumUi::apply(const Command& out) {
    applyEdit(project_, out);
    ++editCount_;
    send(out);
}

void DrumUi::onKey(char code, bool pressed) {
    if (code == key::Fn) fn_ = pressed;
    else if (code == key::Shift) shift_ = pressed;
    else if (pressed) {
        if (!(fn_ && code == 'g')) gridRun_ = false;
        onPress(code);
    }
}

void DrumUi::onPress(char code) {
    if (fn_) {
        onFnPress(code);
        return;
    }
    if (code == ' ') toggleTransport();
    else if (code == '[') selectPattern(pattern_ - 1);
    else if (code == ']') selectPattern(pattern_ + 1);
    else if (code == '-') changeBpm(-1);
    else if (code == '=') changeBpm(1);
    else if (!selectLane(code) && !toggleStep(code)) adjustSound(code);
}

void DrumUi::onFnPress(char code) {
    switch (code) {
        case 'l': cycleLength(); break;
        case 'g': cycleGrid(); break;
        case 'w': cycleSwing(); break;
        case 'z': undo(); break;
        case 'c':
            clipboard_ = currentDrums();
            clipboardFull_ = true;
            break;
        case 'v':
            if (clipboardFull_) {
                rememberForUndo();
                applyPatternContents(clipboard_);
            }
            break;
        default: break;
    }
}

bool DrumUi::selectLane(char code) {
    if (code < '1' || code > '8') return false;
    lane_ = code - '1';
    send(command(CommandType::Preview, static_cast<uint8_t>(lane_), kNormalVelocity));
    return true;
}

bool DrumUi::toggleStep(char code) {
    const int step = stepForKey(code);
    if (step < 0) return false;
    const DrumPattern& pattern = currentDrums();
    if (step >= pattern.length) return true;
    const uint8_t current = pattern.velocity[lane_][step];
    uint8_t next;
    if (shift_) next = current == kAccentVelocity ? kNormalVelocity : kAccentVelocity;
    else next = current == 0 ? kNormalVelocity : 0;
    rememberForUndo();
    apply(command(CommandType::SetStep, static_cast<uint8_t>(pattern_), static_cast<uint8_t>(lane_),
                  static_cast<uint8_t>(step), next));
    return true;
}

bool DrumUi::adjustSound(char code) {
    struct Binding {
        char lower, raise;
        SoundParam param;
    };
    static constexpr Binding kBindings[] = {
        {'z', 'x', SoundParam::Tune},
        {'c', 'v', SoundParam::Decay},
        {'b', 'n', SoundParam::Tone},
        {'m', ',', SoundParam::Level},
    };
    for (const Binding& binding : kBindings) {
        if (code != binding.lower && code != binding.raise) continue;
        undoAvailable_ = false;
        const int delta = code == binding.raise ? kSoundStep : -static_cast<int>(kSoundStep);
        const DrumSound& sound = project_.sounds[lane_];
        uint8_t value = sound.tune;
        if (binding.param == SoundParam::Decay) value = sound.decay;
        else if (binding.param == SoundParam::Tone) value = sound.tone;
        else if (binding.param == SoundParam::Level) value = sound.level;
        apply(command(CommandType::SetSound, static_cast<uint8_t>(lane_), static_cast<uint8_t>(binding.param), 0,
                      stepped(value, delta)));
        send(command(CommandType::Preview, static_cast<uint8_t>(lane_), kNormalVelocity));
        return true;
    }
    return false;
}

void DrumUi::rememberForUndo() {
    undoContents_ = currentDrums();
    undoPattern_ = pattern_;
    undoAvailable_ = true;
}

void DrumUi::undo() {
    if (!undoAvailable_) return;
    undoAvailable_ = false;
    Command out = command(CommandType::SetDrumPattern, static_cast<uint8_t>(undoPattern_));
    out.pattern = undoContents_;
    apply(out);
}

void DrumUi::applyPatternContents(const DrumPattern& contents) {
    Command out = command(CommandType::SetDrumPattern, static_cast<uint8_t>(pattern_));
    out.pattern = contents;
    apply(out);
}

void DrumUi::clearPattern() {
    rememberForUndo();
    applyPatternContents(DrumPattern{});
}

void DrumUi::selectPattern(int pattern) {
    if (pattern < 0 || pattern >= kPatternsPerTrack) return;
    undoAvailable_ = false;
    pattern_ = pattern;
    apply(command(CommandType::SetScenePattern, 0, 0, static_cast<uint8_t>(pattern_)));
}

void DrumUi::cycleLength() {
    rememberForUndo();
    const uint8_t current = currentDrums().length;
    const uint16_t next = current == 4 ? 8 : (current == 8 ? 16 : 4);
    apply(command(CommandType::SetLength, static_cast<uint8_t>(pattern_), 0, 0, next));
}

void DrumUi::cycleGrid() {
    if (!gridRun_) {
        rememberForUndo();
        gridRunOriginal_ = currentDrums();
        gridRun_ = true;
    }
    const size_t count = sizeof(kGridCycle) / sizeof(kGridCycle[0]);
    size_t index = 0;
    while (index < count && kGridCycle[index] != currentDrums().grid) ++index;
    const Grid next = kGridCycle[(index + 1) % count];
    applyPatternContents(gridRunOriginal_);
    apply(command(CommandType::SetGrid, static_cast<uint8_t>(pattern_), 0, 0, static_cast<uint16_t>(next)));
}

void DrumUi::cycleSwing() {
    undoAvailable_ = false;
    const size_t count = sizeof(kSwingValues);
    size_t index = 0;
    while (index < count && kSwingValues[index] != project_.swing) ++index;
    apply(command(CommandType::SetSwing, 0, 0, 0, kSwingValues[(index + 1) % count]));
}

void DrumUi::changeBpm(int delta) {
    const int next = project_.bpm + delta;
    if (next < 40 || next > 240) return;
    undoAvailable_ = false;
    apply(command(CommandType::SetBpm, 0, 0, 0, static_cast<uint16_t>(next)));
}

void DrumUi::toggleTransport() {
    running_ = !running_;
    send(command(running_ ? CommandType::Play : CommandType::Stop));
    if (!running_) saveRequested_ = true;
}

bool DrumUi::takeSaveRequest() {
    const bool requested = saveRequested_;
    saveRequested_ = false;
    return requested;
}

bool DrumUi::takeDirty() {
    const bool dirty = dirty_;
    dirty_ = false;
    return dirty;
}

void DrumUi::draw(Canvas& canvas, uint32_t sceneTick, bool playing, bool storageAvailable) const {
    const DrumPattern& pattern = project_.drums[pattern_];
    canvas.fillRect(0, 0, 240, 135, color::Background);

    char line[64];
    snprintf(line, sizeof(line), "P%02d %3uBPM %s x%u SW%u %s", pattern_ + 1, project_.bpm, gridName(pattern.grid),
             pattern.length, project_.swing, playing ? "PLAY" : "STOP");
    canvas.text(0, 2, line, color::Text);
    const char* warning = warning_ ? warning_ : (storageAvailable ? nullptr : "NO SD");
    if (warning) canvas.text(192, 2, warning, color::Warning);

    const uint32_t stepTicks = gridTicks(pattern.grid);
    const int playStep = playing ? static_cast<int>((sceneTick % patternTicks(pattern.length, pattern.grid)) / stepTicks) : -1;

    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = kGridTop + lane * kCellHeight;
        if (lane == lane_) canvas.fillRect(0, y, 240, kCellHeight, color::LaneSelected);
        for (int step = 0; step < kMaxSteps; ++step) {
            const uint8_t velocity = pattern.velocity[lane][step];
            uint16_t fill = color::StepOff;
            if (step >= pattern.length) fill = color::StepOutside;
            else if (velocity >= kAccentVelocity) fill = color::StepAccent;
            else if (velocity > 0) fill = color::StepOn;
            canvas.fillRect(step * kCellWidth + 1, y + 1, kCellWidth - 2, kCellHeight - 2, fill);
        }
    }
    if (playStep >= 0)
        canvas.fillRect(playStep * kCellWidth, kGridTop, 1, kCellHeight * kDrumLanes, color::Playhead);

    const DrumSound& sound = project_.sounds[lane_];
    snprintf(line, sizeof(line), "%s", kLaneNames[lane_]);
    canvas.text(0, kFooterTop, line, color::Text);
    snprintf(line, sizeof(line), "TUNE %u DEC %u TONE %u LVL %u", sound.tune, sound.decay, sound.tone, sound.level);
    canvas.text(0, kFooterTop + 10, line, color::Text);
}

}  // namespace gb
