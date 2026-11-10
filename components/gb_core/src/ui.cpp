#include "gb/ui.h"
#include "gb/ui_style.h"

#include <algorithm>
#include <cstdio>
namespace gb {
AppUi::AppUi(Project& p, CommandSink sink, void* context)
    : project_(p),
      sink_(sink),
      context_(context),
      drums_(p, sink, context),
      instrument_(p, sink, context),
      arrangement_(p, sink, context),
      sampler_(p, sink, context) {}
void AppUi::setPlayback(bool playing) {
    if (observedPlaying_ && !playing) saveRequested_ = true;
    observedPlaying_ = playing;
    if (transportPending_ && playing == desiredPlaying_) transportPending_ = false;
    if (!transportPending_) desiredPlaying_ = playing;
}
void AppUi::selectScreen(uint8_t screen) {
    if (screen == screen_) return;
    if (!instrument_.prepareExit()) {
        pendingScreen_ = screen;
        return;
    }
    screen_ = screen;
    menuRow_[1] = menuRow_[2] = 0;
    menuEditing_ = menuConfirm_ = false;
    if (screen_ == 5) sampler_.activate(arrangement_.scene());
    dirty_ = true;
    if (screen_ < 2) {
        drums_.setScene(arrangement_.scene());
        instrument_.setScene(arrangement_.scene());
    }
}
void AppUi::onKey(char code, bool pressed) {
    if (code == '`') code = key::Esc;
    if (code == '+') code = '=';
    const auto k = static_cast<uint8_t>(code);
    if (k >= 128) return;
    const bool repeat = down_[k];
    down_[k] = pressed;
    if (pressed && repeat) return;
    if (code == key::Fn) {
        fn_ = pressed;
        if (pressed) { fnUsed_ = false; fnPressedMs_ = nowMs_; }

        dirty_ = true;
        drums_.onKey(code, pressed);
        return;
    }
    if (fn_) fnUsed_ = true;
    if (code == key::Shift) {
        shift_ = pressed;
        dirty_ = true;
        drums_.onKey(code, pressed);
        return;
    }
    if (!pressed) {
        instrument_.onKey(code, false, fn_, shift_);
        return;
    }
    dirty_ = true;
    if (projectsOpen_) { projectsKey(code); return; }
    if (helpOpen_) { helpKey(code); return; }
    if (!fn_ && (code == '-' || code == '=')) {
        // Shift is needed for the printed '+' on the keyboard: keep its step
        // identical to '-'. Ctrl provides fine adjustment instead.
        const int step = down_[static_cast<uint8_t>(key::Ctrl)] ? 1 : 5;
        arrangement_.adjustMaster(code == '=' ? step : -step);
        volumeVisible_ = true;
        volumeUntilMs_ = nowMs_ + 900;
        return;
    }
    if (code == key::Esc && !menuOpen_) {
        if (screen_ == 1 && instrument_.back()) return;
        if (screen_ == 5 && sampler_.sourceFrames() && (sampler_.browserMode() || sampler_.sequencerMode()) && !sampler_.busy()) { sampler_.setMode(false); return; }
        toggleMenu(); return;
    }
    if (code == key::Opt) { toggleMenu(); return; }
    if (menuOpen_ && code != ' ') { menuKey(code); return; }
    if (code == key::Tab) {
        selectScreen((screen_ + 1) % 6);
        return;
    }
    if (fn_ && (code >= '1' && code <= '6')) {
        selectScreen(code - '1');
        return;
    }
    if (fn_ && code == key::Backspace && screen_ < 2) {
        clearPattern();
        return;
    }
    if (screen_ == 2 && code == key::Enter && !fn_) {
        launch(false);
        return;
    }
    if (screen_ == 3 && code == key::Enter && !fn_) {
        launch(true);
        return;
    }
    if (code == ' ') { toggleTransport(); return; }
    if (code == key::Alt) {
        if (screen_ == 5) { sampler_.onKey(code, fn_, shift_); return; }
        if (screen_ < 2) {
            if(observedPlaying_ && !instrument_.busy()) {
                instrument_.setScene(status_&15);
                drums_.setScene(status_&15);
            }
            instrument_.toggleRecording();
        }
        return;
    }
    if (screen_ == 0)
        drums_.onKey(code, true);
    else if (screen_ == 1)
        instrument_.onKey(code, true, fn_, shift_);
    else if (screen_ == 5)
        sampler_.onKey(code, fn_, shift_);
    else
        arrangement_.onKey(screen_, code, fn_, shift_);
}
void AppUi::toggleTransport() {
    if (sampler_.storageBusy()) return;
    if (!desiredPlaying_) launch(screen_ == 3);
    else {
        desiredPlaying_ = false; transportPending_ = true;
        Command c; c.type = CommandType::Stop; sink_(context_, c); saveRequested_ = true;
    }
}
void AppUi::launch(bool song) {
    if (sampler_.storageBusy()) return;
    if (song && arrangement_.songEntry() >= project_.songLength) return;
    if (instrument_.busy() && (desiredPlaying_ || screen_ >= 2)) {
        instrument_.prepareExit();
        pendingLaunch_ = true;
        pendingSong_ = song;
        return;
    }
    Command c;
    c.type = song ? CommandType::PlaySong : CommandType::PlayScene;
    c.a = song ? arrangement_.songEntry() : arrangement_.scene();
    sink_(context_, c);
    desiredPlaying_ = true;
    transportPending_ = true;
}
void AppUi::setPosition(uint32_t tick) {
    const auto& scene = project_.scenes[status_ & 15];
    uint32_t longest = 0;
    if (scene.pattern[0] >= 0) {
        const auto& p = project_.drums[scene.pattern[0]];
        longest = patternTicks(p.length, p.grid);
    }
    for (int t = 0; t < kInstrumentTracks; ++t)
        if (scene.pattern[t + 1] >= 0) {
            const auto& p = project_.instruments[t][scene.pattern[t + 1]];
            longest = std::max(longest, patternTicks(p.length, p.grid));
        }
    if (scene.pattern[kSamplerTrack] >= 0) {
        const auto& p = project_.samplePatterns[scene.pattern[kSamplerTrack]].steps;
        longest = std::max(longest, patternTicks(p.length, p.grid));
    }
    instrument_.setTick(tick % (longest ? longest : kTicksPerBar));
}
void AppUi::receive(const RecordUpdate& update) {
    instrument_.receive(update);
    if (!instrument_.busy() && pendingClear_) {
        pendingClear_ = false;
        instrument_.clearPattern();
    }
    if (!instrument_.busy() && pendingLaunch_) {
        pendingLaunch_ = false;
        launch(pendingSong_);
    }
    if (!instrument_.busy() && pendingScreen_ >= 0) {
        const auto next = static_cast<uint8_t>(pendingScreen_);
        pendingScreen_ = -1;
        selectScreen(next);
    }
}
void AppUi::clearPattern() {
    if (screen_ == 5) { sampler_.clearPattern(); return; }
    if (screen_ == 0)
        drums_.clearPattern();
    else if (screen_ == 1) {
        if (instrument_.busy()) {
            instrument_.prepareExit();
            pendingClear_ = true;
        } else
            instrument_.clearPattern();
    }
}
void AppUi::setWarning(const char* warning) {
    warning_ = warning;
    drums_.setWarning(warning);
    dirty_ = true;
}
void AppUi::setTime(uint32_t milliseconds) {
    nowMs_ = milliseconds;
    instrument_.setVisualTime(milliseconds, observedPlaying_);
    if (volumeVisible_ && static_cast<int32_t>(nowMs_ - volumeUntilMs_) >= 0) {
        volumeVisible_ = false;
        dirty_ = true;
    }
}
bool AppUi::takeSaveRequest() {
    if (!saveRequested_ || busy() || observedPlaying_ || transportPending_) return false;
    saveRequested_ = false;
    return true;
}
bool AppUi::takeDirty() {
    const bool d = drums_.takeDirty(), i = instrument_.takeDirty(), a = arrangement_.takeDirty(), own = dirty_;
    const bool s = sampler_.takeDirty();
    dirty_ = false;
    return d || i || a || s || own;
}
void AppUi::draw(Canvas& c, uint32_t tick, bool playing, bool storage) const {
    uint32_t longest = 0;
    const auto& scene = project_.scenes[status_ & 15];
    if (scene.pattern[0] >= 0) {
        const auto& p = project_.drums[scene.pattern[0]];
        longest = patternTicks(p.length, p.grid);
    }
    for (int t = 0; t < kInstrumentTracks; ++t)
        if (scene.pattern[t + 1] >= 0) {
            const auto& p = project_.instruments[t][scene.pattern[t + 1]];
            longest = std::max(longest, patternTicks(p.length, p.grid));
        }
    if (scene.pattern[kSamplerTrack] >= 0) {
        const auto& p = project_.samplePatterns[scene.pattern[kSamplerTrack]].steps;
        longest = std::max(longest, patternTicks(p.length, p.grid));
    }
    const uint32_t local = tick % (longest ? longest : kTicksPerBar);
    if (screen_ == 0)
        drums_.draw(c, local, playing, storage);
    else if (screen_ == 1)
        instrument_.draw(c, local, playing, storage, warning_);
    else if (screen_ == 5)
        sampler_.draw(c, local, playing);
    else {
        arrangement_.draw(c, screen_, playing);
        const char* notice = warning_ ? warning_ : (storage ? nullptr : "NO SD");
        if (notice) style::footer(c,notice);
    }
    if (sampler_.storageBusy() && screen_ != 5) {
        c.fillRect(0, 113, 240, 10, color::Background);
        c.text(0, 114, "Preparing samples...", color::Warning);
    }
    if (helpOpen_) drawHelp(c);
    else if (projectsOpen_) drawProjects(c);
    else if (menuOpen_)
        drawMenu(c);

    else if (volumeVisible_) {
        c.fillRect(65, 48, 110, 38, color::LaneSelected);
        c.fillRect(66, 49, 108, 36, color::StepOutside);
        char level[24];
        snprintf(level, sizeof(level), "MASTER %u%%", project_.masterVolume);
        c.text(76, 55, level, color::Text);
        c.fillRect(70, 71, 100, 6, color::StepOff);
        if (project_.masterVolume)
            c.fillRect(70, 71, project_.masterVolume, 6, color::StepOn);
    }
}
void AppUi::drawFnMenu(Canvas& c) const {
    constexpr const char* names[] = {"DRUMS", "SYNTH", "SCENES", "SONG", "MIXER", "SAMPLER"};
    c.fillRect(0, 0, 240, 135, color::StepOutside);
    c.fillRect(0, 0, 240, 14, color::LaneSelected);
    char line[64];
    snprintf(line, sizeof(line), "FN HELP %s %u%% SYN%u OPT:MENU", names[screen_], project_.masterVolume, voiceBudget_);
    c.text(3, 3, line, color::Text);
    c.text(3, 19, "1:DRUM 2:SYN 3:SCN 4:SONG 5:MIX 6:SMP", color::StepAccent);
    c.fillRect(3, 29, 234, 1, color::LaneSelected);
    // All actions above the bottom hint use the Fn key already being held.
    const auto row = [&](int index, const char* value) { c.text(3, 34 + index * 10, value, color::Text); };
    if (screen_ == 5) {
        snprintf(line, sizeof(line), "PAD %u -> S%02u  P%02u", sampler_.pad() + 1, sampler_.slot() + 1, sampler_.pattern() + 1);
        row(0, line); sampler_.drawFnMenu(c); return;
    }
    if (screen_ == 0) {
        row(0, "+/- tempo  L length  G grid  W swing");
        row(1, "BS clear all steps  Z undo");
        row(2, "C copy pattern  V paste pattern");
        const auto& p = project_.drums[drums_.currentPattern()];
        constexpr const char* grids[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"};
        snprintf(line, sizeof(line), "P%02u  %u BPM  %s  x%u  SW%u", drums_.currentPattern() + 1,
                 project_.bpm, grids[static_cast<uint8_t>(p.grid)], p.length, project_.swing);
        row(4, line);
        row(6, "Release FN: 1..8 sound, [] pattern");
    } else if (screen_ == 1) {
        row(0, "+/- octave  [/] track  Enter deselect");
        row(1, "L length G grid W swing Q quantize");
        row(2, "A arp  Shift+A rate  O oct  T gate");
        row(3, "F fit  BS clear  Z undo  C/V copy");
        row(4, "Arrows edit; Shift: length/velocity");
        row(5, "S synth settings / piano roll");
        instrument_.drawFnSettings(c);
        return;
    } else if (screen_ == 2) {
        row(0, "Arrows: scene / track  [] scene");
        row(1, "+/- assign pattern  BS empty slot");
        row(2, "C copy scene  V paste scene  Z undo");
        const int pattern = project_.scenes[arrangement_.scene()].pattern[arrangement_.column()];
        snprintf(line, sizeof(line), "SCENE %02u  TRACK %u  %s", arrangement_.scene() + 1,
                 arrangement_.column() + 1, pattern < 0 ? "EMPTY" : "PATTERN");
        row(4, line);
        if (pattern >= 0) {
            snprintf(line, sizeof(line), "PATTERN %02d", pattern + 1);
            row(5, line);
        }
        row(6, "No Fn: Enter launch scene");
    } else if (screen_ == 3) {
        row(0, "Arrows: row / scene-repeat field");
        row(1, "+/- edit scene or repeats");
        row(2, "Enter insert scene  BS delete row");
        row(3, "Z undo");
        snprintf(line, sizeof(line), "ROW %02u/%u  INSERT SCENE %02u", arrangement_.songEntry() + 1,
                 project_.songLength, arrangement_.scene() + 1);
        row(4, line);
        if (arrangement_.songEntry() < project_.songLength) {
            const auto& entry = project_.song[arrangement_.songEntry()];
            snprintf(line, sizeof(line), "%s: SCENE %02u x%u", arrangement_.editingRepeats() ? "REPEATS" : "SCENE",
                     entry.scene + 1, entry.repeats);
            row(5, line);
        }
        row(6, "No Fn: Enter play song");
    } else {
        row(0, "Left/right select; up/down level");
        row(1, "+/- selected strip  Shift: 1%");
        row(2, "M mute  S solo (tracks)  Z undo");
        constexpr const char* strips[] = {"MAIN", "DRUM", "T1", "T2", "T3", "T4", "SMP"};
        const auto strip = arrangement_.mixerStrip();
        snprintf(line, sizeof(line), "SELECTED: %s  %s %s", strips[strip],
                 strip && project_.mixer[strip - 1].mute ? "MUTE" : "",
                 strip && project_.mixer[strip - 1].solo ? "SOLO" : "");
        row(3, line);
        row(4, "MAIN DRUM T1  T2  T3  T4  SMP");
        snprintf(line, sizeof(line), "%3u%% %3u%% %3u%% %3u%% %3u%% %3u%% %3u%%", project_.masterVolume,
                 project_.mixer[0].volume, project_.mixer[1].volume, project_.mixer[2].volume,
                 project_.mixer[3].volume, project_.mixer[4].volume, project_.mixer[5].volume);
        row(5, line);
    }
    row(8, "OPT close   ENTER quick start");
    if (warning_) c.text(3, 104, warning_, color::Warning);
}
}  // namespace gb
