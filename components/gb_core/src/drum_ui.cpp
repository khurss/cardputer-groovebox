#include "gb/drum_ui.h"
#include "gb/ui_style.h"

#include <cstdio>
#include <cstring>

#include "gb/validate.h"

namespace gb {

namespace {

constexpr char kStepKeys[] = "qwertyuiasdfghjk";
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
        case '-': changeBpm(-1); break;
        case '=': changeBpm(1); break;
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
                activate();
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
    if (next) activate();
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
    auto cleared=currentDrums();
    for(auto& lane:cleared.velocity) for(auto& velocity:lane) velocity=0;
    applyPatternContents(cleared);
}

void DrumUi::activate() {
    if(project_.scenes[editScene_].pattern[0]!=pattern_) apply(command(CommandType::SetScenePattern,editScene_,0,pattern_));
}
void DrumUi::setScene(uint8_t scene) {
    if(scene>=kScenes) return;
    editScene_=scene;
    if(project_.scenes[scene].pattern[0]>=0) pattern_=project_.scenes[scene].pattern[0];
    dirty_=true;
}
void DrumUi::selectPattern(int pattern) {
    if (pattern < 0 || pattern >= kPatternsPerTrack) return;
    undoAvailable_ = false;
    pattern_ = pattern;
    apply(command(CommandType::SetScenePattern, editScene_, 0, static_cast<uint8_t>(pattern_)));
}

void DrumUi::cycleLength() {
    rememberForUndo();
    const uint8_t current = currentDrums().length;
    const uint16_t next = current == 4 ? 8 : (current == 8 ? 16 : 4);
    apply(command(CommandType::SetLength, static_cast<uint8_t>(pattern_), 0, 0, next));
}
void DrumUi::setTiming(bool grid, uint16_t value) {
    auto c = command(grid ? CommandType::SetGrid : CommandType::SetLength, static_cast<uint8_t>(pattern_), 0, 0, value);
    if (!isValidCommand(c)) return;
    gridRun_ = false;
    rememberForUndo();
    apply(c);
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

void DrumUi::draw(Canvas& c, uint32_t tick, bool playing, bool storage) const {
    const auto& p=project_.drums[pattern_];c.fillRect(0,0,240,135,color::Background);
    char text[64];snprintf(text,sizeof(text),"DRM P%02d %u %s",pattern_+1,project_.bpm,gridName(p.grid));
    style::header(c,text,playing?"PLAY":"STOP");
    constexpr const char* shortNames[]={"KD","SN","CP","CH","OH","TM","RM","PC"};
    for(int beat=0;beat<4;++beat){snprintf(text,sizeof(text),"%d",beat*4+1);c.text(31+beat*52,18,text,color::Muted);}
    const int playStep=playing?(tick%patternTicks(p.length,p.grid))/gridTicks(p.grid):-1;
    for(int lane=0;lane<8;++lane){
        const int y=28+lane*10;
        if(lane==lane_)c.fillRect(0,y,27,9,color::LaneSelected);
        c.text(3,y+1,shortNames[lane],lane==lane_?color::StepAccent:color::Muted);
        for(int step=0;step<16;++step){
            const int x=30+step*13;const auto v=p.velocity[lane][step];
            auto ink=step>=p.length?color::StepOutside:v>=127?color::StepAccent:v?style::tracks[lane%6]:color::StepOff;
            c.fillRect(x,y,11,8,ink);
            if(v && step<p.length)c.fillRect(x,y,2,8,color::Text);
            if(lane==lane_ && step<p.length)c.fillRect(x,y+8,11,1,color::LaneSelected);
        }
    }
    if(playStep>=0){c.fillRect(29+playStep*13,26,1,82,color::Playhead);c.fillRect(30+playStep*13,25,11,2,color::Playhead);}
    const auto& sound=project_.sounds[lane_];
    snprintf(text,sizeof(text),"%s T%u D%u C%u L%u",kLaneNames[lane_],sound.tune,sound.decay,sound.tone,sound.level);
    c.text(3,111,text,color::Text);
    const char* warning=warning_?warning_:storage?nullptr:"NO SD - not saved";
    if(warning)style::footer(c,warning);
    else {snprintf(text,sizeof(text),"%s x%u SW%u  1-8 sound  Opt menu",gridName(p.grid),p.length,project_.swing);style::footer(c,text);}
}

}  // namespace gb
