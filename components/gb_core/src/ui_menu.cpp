#include "gb/ui.h"
#include "gb/ui_style.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace gb {
namespace {
using O = MenuOp;
constexpr const char* screens[] = {"DRUMS", "SYNTH", "SCENES", "SONG", "MIXER", "SAMPLER", "EQ FFT", "VISUALS"};
constexpr const char* grids[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"};
constexpr const char* waves[] = {"SINE", "TRIANGLE", "SAW", "SQUARE"};
constexpr const char* arps[] = {"OFF", "UP", "DOWN", "UP/DOWN"};
constexpr const char* strips[] = {"MAIN", "DRUM", "T1", "T2", "T3", "T4", "SMP"};
constexpr MenuItem navigation[] = {
    {"Drums", "8 sounds / step sequencer", O::Screen, 0},
    {"Synth", "Live keyboard / piano roll", O::Screen, 1},
    {"Scenes", "Combine patterns into scenes", O::Screen, 2},
    {"Song", "Arrange scenes / set repeats", O::Screen, 3},
    {"Mixer", "Levels / mute / solo", O::Screen, 4},
    {"Sampler", "WAV / microphone / cuts / pads", O::Screen, 5},
    {"Projects", "New / open / save / save a copy", O::Projects},
    {"Quick start", "First track in nine short steps", O::Help},
    {"Key reference", "Shortcuts for the current screen", O::KeyHelp},
    {"Equalizer FFT", "Seven frequency bands / output FFT", O::Screen, 6},
    {"Visuals", "Four audio-reactive views", O::Screen, 7}
};
constexpr MenuItem globals[] = {
    {"Master volume", "Overall output; Ctrl for 1% steps", O::Master},
    {"Tempo", "Beats per minute; Shift: 10 BPM", O::Tempo},
    {"Swing", "Global swing, 50% is straight", O::Swing}
};
constexpr MenuItem drumActions[] = {
    {"Play / stop", "Space also controls playback", O::Play},
    {"Undo", "Undo the last pattern edit", O::Shortcut, 'z'},
    {"Copy pattern", "Copy steps to the clipboard", O::Shortcut, 'c'},
    {"Paste pattern", "Paste copied steps here", O::Shortcut, 'v'},
    {"Clear all steps", "Keeps sound/grid; undo available", O::Clear}
};
constexpr MenuItem synthActions[] = {
    {"Play / stop", "Space also controls playback", O::Play},
    {"Record notes", "Arm or finish live note recording", O::Record},
    {"Sound editor", "Open synthesizer parameters", O::SoundEditor},
    {"Undo", "Undo an edit or recording pass", O::Shortcut, 'z'},
    {"Copy pattern", "Copy the current notes", O::Shortcut, 'c'},
    {"Paste pattern", "Paste copied notes here", O::Shortcut, 'v'},
    {"Clear all notes", "Keeps sound/grid; undo available", O::Clear},
    {"Performance", "Note trails / keyboard minimap", O::Performance},
    {"Piano roll", "View and edit recorded notes", O::PianoRoll}
};
constexpr MenuItem sceneActions[] = {
    {"Launch scene", "Queue the selected scene", O::Play},
    {"Copy scene", "Copy all six track assignments", O::Shortcut, 'c'},
    {"Paste scene", "Paste all six track assignments", O::Shortcut, 'v'},
    {"Undo", "Undo the last arrangement edit", O::Shortcut, 'z'},
    {"Empty track slot", "Remove this pattern from scene", O::Shortcut, key::Backspace},
    {"Follow playback", "F: scroll to the playing scene", O::FollowPlayback}
};
constexpr MenuItem songActions[] = {
    {"Play from row", "Start song at the selected row", O::Play},
    {"Insert scene", "Insert selected scene at this row", O::Shortcut, key::Enter},
    {"Delete row", "Remove this song entry", O::Shortcut, key::Backspace},
    {"Undo", "Undo the last arrangement edit", O::Shortcut, 'z'},
    {"Follow playback", "F: scroll to the playing song row", O::FollowPlayback}
};
constexpr MenuItem mixerActions[] = {
    {"Play / stop", "Space also controls playback", O::Play},
    {"Mute channel", "Mute selected channel", O::Mute},
    {"Solo channel", "Solo selected channel", O::Solo},
    {"Undo", "Undo the last mixer edit", O::Shortcut, 'z'}
};
constexpr MenuItem samplerActions[] = {
    {"Browse WAV files", "Choose a source from SD", O::Browse},
    {"Record microphone", "Record / finish microphone WAV", O::Record},
    {"Waveform editor", "Select and trim a sound region", O::Waveform},
    {"Pad sequencer", "Place samples on the step grid", O::Steps},
    {"Play / stop", "Space also controls playback", O::Play},
    {"Mark start here", "Set selection start at cursor", O::MarkStart},
    {"Mark end here", "Set selection end at cursor", O::MarkEnd},
    {"Save selected slice", "Assign the new slice to this pad", O::SaveSlice},
    {"Chop selection", "Save equally sized slices", O::Chop},
    {"Reset selection", "Show and select the full source", O::ResetRange},
    {"Clear all steps", "Keep pad assignments; undoable", O::Clear},
    {"Undo steps", "Undo the last pad pattern edit", O::Shortcut, 'z'},
    {"Delete sample slot", "Frees slot; source WAV is kept", O::DeleteSlice}
};
constexpr MenuItem drumSettings[] = {
    {"Pattern length", "Number of active steps", O::Length}, {"Step grid", "Retimes steps; undo available", O::Grid},
    {"Pattern", "Pattern assigned to this scene", O::Pattern}
};
constexpr MenuItem synthSettings[] = {
    {"Track", "Select synthesizer T1..T4", O::Track}, {"Pattern", "Pattern assigned to this scene", O::Pattern},
    {"Keyboard octave", "Register of the live keyboard", O::Octave},
    {"Pattern length", "Number of active steps", O::Length}, {"Step grid", "Grid for editing and recording", O::Grid},
    {"Record quantize", "Align recorded notes to grid", O::Quantize}, {"Fit notes", "Show the full note range", O::Fit},
    {"Waveform", "Oscillator waveform", O::SynthParameter, 0},
    {"Attack", "Envelope attack", O::SynthParameter, 1}, {"Decay", "Envelope decay", O::SynthParameter, 2},
    {"Sustain", "Envelope sustain", O::SynthParameter, 3}, {"Release", "Envelope release", O::SynthParameter, 4},
    {"Cutoff", "Low-pass filter cutoff", O::SynthParameter, 5}, {"Resonance", "Filter resonance", O::SynthParameter, 6},
    {"Glide", "Slide between note pitches", O::SynthParameter, 7}, {"Synth level", "Synthesizer sound level", O::SynthParameter, 8},
    {"Arpeggiator", "Chord playback order", O::ArpParameter, 0}, {"Arp rate", "Arpeggiator step grid", O::ArpParameter, 1},
    {"Arp octaves", "Arpeggiator pitch range", O::ArpParameter, 2}, {"Arp gate", "Length within each arp step", O::ArpParameter, 3},
    {"Sound preset", "Six starting sounds; Undo restores", O::Preset},
    {"Keyboard layout", "Strings: legato per keyboard row", O::Layout},
    {"Root note", "Root for the string scale", O::Root},
    {"Scale", "String notes stay in this scale", O::Scale}
};
constexpr MenuItem sceneSettings[] = {
    {"Scene", "Select scene 01..16", O::Scene}, {"Track", "Select the track column", O::SceneTrack},
    {"Assigned pattern", "Pattern number or empty slot", O::ScenePattern}
};
constexpr MenuItem songSettings[] = {
    {"Song row", "Select an entry or the ADD row", O::SongRow}, {"Insert scene", "Scene used by Insert scene action", O::InsertScene},
    {"Entry scene", "Change scene in this song entry", O::SongScene}, {"Repeats", "Number of scene repetitions", O::SongRepeats}
};
constexpr MenuItem mixerSettings[] = {
    {"Channel", "Choose MAIN / DRUM / T1..4 / SMP", O::Strip}, {"Channel level", "Selected channel; Shift: 1%", O::Level},
    {"Mute", "Mute selected channel", O::Mute}, {"Solo", "Solo selected channel", O::Solo}
};
constexpr MenuItem samplerSettings[] = {
    {"Pad", "Choose one of eight sample pads", O::Pad}, {"Assigned sample", "Slot S01..S32 for this pad", O::Slot},
    {"Pattern", "Pattern assigned to this scene", O::Pattern}, {"Chop count", "Number of equal slices to save", O::Parts},
    {"Pattern length", "Number of active steps", O::Length}, {"Step grid", "Retimes steps; undo available", O::Grid}
};
constexpr MenuItem visualActions[] = {
    {"Play / stop", "Space also controls playback", O::Play},
    {"Next visual", "Spectrum / wave / waterfall / orbit", O::VisualParameter, 0},
    {"Next palette", "Four multicolor palettes", O::VisualParameter, 1}
};
constexpr MenuItem visualSettings[] = {
    {"Visualization", "Left/right also changes the view", O::VisualParameter, 0},
    {"Palette", "Neon / Sunset / Ocean / Rainbow", O::VisualParameter, 1},
    {"Sensitivity", "FFT / LED gain; Wave scales itself", O::VisualParameter, 2},
    {"Smoothing", "Spectrum / Wave scale release", O::VisualParameter, 3},
    {"Motion speed", "Waterfall scroll / orbit rotation", O::VisualParameter, 4}
};
constexpr MenuItem eqActions[] = {
    {"Play / stop", "Space also controls playback", O::Play},
    {"Bypass EQ", "B: compare with unprocessed mix", O::EqBypass},
    {"Reset all bands", "Shift+0: flat; undo available", O::EqFlat},
    {"Undo", "Undo last EQ or mixer edit", O::Shortcut, 'z'}
};
constexpr MenuItem fftSettings[] = {
    {"Frequency band", "Left/right selects a fixed band", O::EqBand},
    {"Band gain", "Up/down: 1 dB; Shift: 3 dB", O::EqGain},
    {"Bypass EQ", "Compare processed and dry audio", O::EqBypass},
    {"Palette", "Color palette for spectrum and LED", O::VisualParameter, 1},
    {"Sensitivity", "Display gain only; audio unchanged", O::VisualParameter, 2},
    {"Smoothing", "Spectrum release: fast to slow", O::VisualParameter, 3}
};
template<size_t N> constexpr unsigned count(const MenuItem (&)[N]) { return N; }
unsigned contextCount(uint8_t page, uint8_t screen) {
    if (page == 1) {
        const unsigned sizes[] = {count(drumActions), count(synthActions), count(sceneActions), count(songActions), count(mixerActions), count(samplerActions), count(eqActions), count(visualActions)};
        return sizes[screen];
    }
    const unsigned sizes[] = {count(drumSettings), count(synthSettings), count(sceneSettings), count(songSettings), count(mixerSettings), count(samplerSettings), count(fftSettings), count(visualSettings)};
    return sizes[screen];
}
int bounded(int n, int low, int high) { return std::max(low, std::min(n, high)); }
uint8_t synthValue(const SynthSound& s, uint8_t index) {
    const uint8_t values[] = {static_cast<uint8_t>(s.wave), s.attack, s.decay, s.sustain, s.release, s.cutoff, s.resonance, s.glide, s.level};
    return values[index];
}
int cycle(int value, int direction, const uint8_t* values, unsigned n) {
    unsigned at = 0; while (at < n && values[at] != value) ++at;
    return values[(at + n + direction) % n];
}
}
unsigned AppUi::menuCount(uint8_t page) const { return page == 0 ? count(navigation) : contextCount(page, screen_) + (page == 2 ? count(globals) + 1 : 0); }
MenuItem AppUi::menuItem(uint8_t page, unsigned row) const {
    if (page == 0) return navigation[row];
    if (page == 2 && row == menuCount(page)-1) return {"RGB brightness", "0: off; on requires full backlight", O::VisualParameter, 5};
    if (page == 2 && row < count(globals)) return globals[row];
    if (page == 2) row -= count(globals);
    const MenuItem* actions[] = {drumActions, synthActions, sceneActions, songActions, mixerActions, samplerActions, eqActions, visualActions};
    const MenuItem* settings[] = {drumSettings, synthSettings, sceneSettings, songSettings, mixerSettings, samplerSettings, fftSettings, visualSettings};
    return (page == 1 ? actions : settings)[screen_][row];
}
void AppUi::toggleMenu() {
    menuOpen_ = !menuOpen_; menuEditing_ = menuConfirm_ = false; dirty_ = true;
    menuNotice_ = nullptr;
    if (menuOpen_) menuRow_[0] = screen_ < 6 ? screen_ : screen_ + 3;
}
void AppUi::shortcut(char code) {
    if (screen_ == 0) drums_.shortcut(code);
    else if (screen_ == 1) instrument_.onKey(code, true, true, shift_);
    else if (screen_ == 5) {
        if (code == 'z') sampler_.undoPattern();
        else sampler_.onKey(code, true, shift_);
    }
    else arrangement_.onKey(screen_, code, true, shift_);
}
void AppUi::menuApply(Command c) {
    if (sink_(context_, c)) { applyEdit(project_, c); ++menuEdits_; }
    dirty_ = true;
}
const char* AppUi::menuDisabled(const MenuItem& item) const {
    const auto op = item.op;
    if (op == O::VisualParameter || op == O::Help || op == O::KeyHelp || op == O::Projects || op == O::Screen || op == O::Master || op == O::Tempo || op == O::Swing) return nullptr;
    if (sampler_.storageBusy()) return "Preparing samples; please wait";
    if (screen_ == 3 && op == O::Play && arrangement_.songEntry() >= project_.songLength) return "Insert a scene into this row first";
    if (screen_ == 3 && op == O::Shortcut && item.argument == key::Enter && project_.songLength == kSongEntries) return "Song is full (64 entries)";
    if (op == O::Shortcut && (item.argument == 'z' || item.argument == 'v')) {
        const bool undo = item.argument == 'z';
        const bool available = screen_ == 0 ? (undo ? drums_.canUndo() : drums_.canPaste())
            : screen_ == 1 ? (undo ? instrument_.canUndo() : instrument_.canPaste())
            : screen_ == 5 ? (undo ? sampler_.canUndo() : sampler_.canPaste())
            : (undo ? arrangement_.canUndo() : arrangement_.canPaste());
        if (!available) return undo ? "Nothing to undo" : "Copy a pattern or scene first";
    }
    if (screen_ == 1 && instrument_.busy() && op != O::Record && op != O::Play) return "Finish note recording to edit";
    if ((op == O::Mute || op == O::Solo) && arrangement_.mixerStrip() == 0) return "Select a channel other than MAIN";
    if ((op == O::SongScene || op == O::SongRepeats || (screen_ == 3 && op == O::Shortcut && item.argument == key::Backspace)) && arrangement_.songEntry() >= project_.songLength)
        return "Insert a scene into this row first";
    if (screen_ != 5) return nullptr;
    if (sampler_.busy() && !(op == O::Record && sampler_.recording()) && op != O::Play) return "Finish recording or wait for I/O";
    if ((op == O::SaveSlice || op == O::Chop || op == O::DeleteSlice) && observedPlaying_) return "Stop playback before changing samples";
    if ((op == O::SaveSlice || op == O::Chop || op == O::MarkStart || op == O::MarkEnd || op == O::ResetRange) && !sampler_.sourceFrames()) return "Open a WAV or record the microphone";
    if ((op == O::MarkStart || op == O::MarkEnd || op == O::ResetRange) && (sampler_.sequencerMode() || sampler_.browserMode())) return "Open the waveform editor first";
    if (op == O::DeleteSlice && !project_.samples[sampler_.slot()].source[0]) return "This sample slot is already empty";
    return nullptr;
}
void AppUi::menuKey(char code) {
    menuNotice_ = nullptr;
    if (menuConfirm_) {
        if (code == key::Enter) {
            const auto item = menuItem(menuPage_, menuRow_[menuPage_]);
            menuConfirm_ = false;
            if (!menuDisabled(item)) { menuRun(item); menuOpen_ = false; }
        } else if (code == key::Backspace || code == 27) menuConfirm_ = false;
        return;
    }
    if (code == key::Backspace || code == 27) {
        if (menuEditing_) menuEditing_ = false;
        else menuOpen_ = false;
        return;
    }
    if (code >= '1' && code <= '8' && (fn_ || (!menuEditing_ && menuPage_ == 0))) {
        menuOpen_ = menuEditing_ = false;
        selectScreen(code - '1'); menuRow_[1] = menuRow_[2] = 0; return;
    }
    if (code == key::Tab || (!menuEditing_ && (code == ',' || code == '/' || code == '[' || code == ']'))) {
        const int direction = code == ',' || code == '[' ? -1 : 1;
        menuPage_ = (menuPage_ + 3 + direction) % 3; menuEditing_ = false; return;
    }
    const auto item = menuItem(menuPage_, menuRow_[menuPage_]);
    if (menuEditing_) {
        if (code == key::Enter) menuEditing_ = false;
        else if (code == ',' || code == '/' || code == ';' || code == '.') {
            if (!menuDisabled(item)) menuAdjust(item, code == '/' || code == ';' ? 1 : -1);
        }
        return;
    }
    if (code == ';' || code == '.') {
        auto& row = menuRow_[menuPage_];
        row = bounded(int(row) + (code == '.' ? 1 : -1), 0, menuCount(menuPage_) - 1);
    } else if (code == key::Enter && !menuDisabled(item)) {
        if (menuPage_ == 2) menuEditing_ = true;
        else if (item.op == O::DeleteSlice) menuConfirm_ = true;
        else menuRun(item);
    }
}
void AppUi::menuRun(const MenuItem& item) {
    switch (item.op) {
        case O::EqBypass: arrangement_.onKey(6,'b',false,false);break;
        case O::EqFlat: arrangement_.onKey(6,'0',false,true);break;
        case O::FollowPlayback: arrangement_.onKey(screen_,'f',false,false); menuOpen_=false; break;
        case O::VisualParameter: visual_.adjust(item.argument, 1); break;
        case O::Performance:
            instrument_.showPerformance(); menuOpen_ = false; break;
        case O::PianoRoll: instrument_.showPianoRoll(); menuOpen_ = false; break;
        case O::Projects: projectsOpen_ = true; menuOpen_ = false; projectRow_ = 0; projectRequest_ = ProjectAction::Refresh; break;
        case O::Help: case O::KeyHelp: helpOpen_ = true; menuOpen_ = false; helpPage_ = item.op == O::Help ? 0 : kTutorialPages; break;
        case O::Screen:
            selectScreen(item.argument); menuRow_[1] = menuRow_[2] = 0; menuOpen_ = false; break;
        case O::Play:
            if (screen_ == 2) launch(false);
            else if (screen_ == 3) launch(true);
            else toggleTransport();
            menuOpen_ = false; break;
        case O::Record:
            if (screen_ == 5) sampler_.onKey(key::Alt, false, false);
            else {
                if (observedPlaying_ && !instrument_.busy()) { instrument_.setScene(status_ & 15); drums_.setScene(status_ & 15); }
                instrument_.toggleRecording();
            }
            menuOpen_ = false; break;
        case O::Shortcut:
            shortcut(item.argument);
            menuNotice_ = item.argument == 'c' ? "Copied to clipboard" : item.argument == 'v' ? "Pasted" : item.argument == 'z' ? "Undone" : "Arrangement updated";
            break;
        case O::Clear: clearPattern(); menuNotice_ = "Pattern cleared; Undo available"; break;
        case O::SoundEditor: instrument_.showParameters(); menuOpen_ = false; break;
        case O::Browse: sampler_.onKey('b', true, false); menuOpen_ = false; break;
        case O::Waveform: sampler_.setMode(false); menuOpen_ = false; break;
        case O::Steps: sampler_.setMode(true); menuOpen_ = false; break;
        case O::SaveSlice: sampler_.saveSelection(false); menuOpen_ = false; break;
        case O::Chop: sampler_.saveSelection(true); menuOpen_ = false; break;
        case O::DeleteSlice: sampler_.onKey('d', true, false); break;
        case O::MarkStart: sampler_.onKey('a', false, false); menuOpen_ = false; break;
        case O::MarkEnd: sampler_.onKey('d', false, false); menuOpen_ = false; break;
        case O::ResetRange: sampler_.onKey('r', false, false); menuOpen_ = false; break;
        case O::Mute: case O::Solo: arrangement_.onKey(4, item.op == O::Mute ? 'm' : 's', false, false); break;
        default: break;
    }
}
void AppUi::menuValue(const MenuItem& item, char* value, size_t size) const {
    value[0] = 0;
    int n = 0;
    const char* text = nullptr;
    const uint8_t track = instrument_.track();
    const auto& arp = project_.arps[track];
    const auto& s = project_.synths[track];
    const DrumPattern& timing = screen_ == 5 ? project_.samplePatterns[sampler_.pattern()].steps : project_.drums[drums_.currentPattern()];
    switch (item.op) {
        case O::EqBand: text=kEqLabels[arrangement_.eqBand()];break;
        case O::EqGain: snprintf(value,size,"%+d dB",project_.equalizer.gain[arrangement_.eqBand()]);return;
        case O::EqBypass: text=project_.equalizer.enabled?"OFF":"ON";break;
        case O::VisualParameter: {
            const auto& v = visual_.settings();
            if (item.argument == 0) text = AudioVisual::modeName(v.mode);
            else if (item.argument == 1) text = AudioVisual::paletteName(v.palette);
            else if (item.argument == 2) { snprintf(value,size,"x%u",1u<<v.gain); return; }
            else if (item.argument == 3) text = AudioVisual::smoothingName(v.smoothing);
            else if (item.argument == 4) n = v.motion;
            else { snprintf(value,size,"%u%%",v.led); return; }
            break;
        }
        case O::Screen: text = item.argument == screen_ ? "CURRENT" : "OPEN"; break;
        case O::Play: text = (screen_ != 2 && screen_ != 3 && desiredPlaying_) ? "STOP" : "PLAY"; break;
        case O::Record: text = (screen_ == 5 ? sampler_.recording() : instrument_.busy()) ? "STOP REC" : "START REC"; break;
        case O::Preset: text = InstrumentUi::presetName(instrument_.preset()); break;
        case O::Layout: text = project_.keyboardLayout[track] ? "STRINGS" : "PIANO"; break;
        case O::Root: { constexpr const char* roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; text = roots[project_.keyboardRoot[track]]; break; }
        case O::Scale: { constexpr const char* scales[] = {"CHROMATIC", "MAJOR", "MINOR", "PENTA"}; text = scales[project_.keyboardScale[track]]; break; }
        case O::Master: snprintf(value, size, "%u%%", project_.masterVolume); return;
        case O::Tempo: snprintf(value, size, "%u BPM", project_.bpm); return;
        case O::Swing: snprintf(value, size, "%u%%", project_.swing); return;
        case O::Length: n = screen_ == 1 ? project_.instruments[track][instrument_.pattern()].length : timing.length; break;
        case O::Grid: text = grids[static_cast<unsigned>(screen_ == 1 ? project_.instruments[track][instrument_.pattern()].grid : timing.grid)]; break;
        case O::Pattern: n = 1 + (screen_ == 0 ? drums_.currentPattern() : screen_ == 1 ? instrument_.pattern() : sampler_.pattern()); break;
        case O::Track: n = track + 1; break;
        case O::Octave: n = instrument_.octave(); break;
        case O::Quantize: text = project_.recordQuantize[track] ? "ON" : "OFF"; break;
        case O::Fit: text = instrument_.fitsNotes() ? "FIT" : "SEMITONE"; break;
        case O::SynthParameter: if (!item.argument) text = waves[static_cast<unsigned>(s.wave)]; else n = synthValue(s, item.argument); break;
        case O::ArpParameter:
            if (item.argument == 0) text = arps[static_cast<unsigned>(arp.mode)];
            else if (item.argument == 1) text = grids[static_cast<unsigned>(arp.rate)];
            else if (item.argument == 2) n = arp.octaves;
            else { snprintf(value, size, "%u%%", arp.gate); return; }
            break;
        case O::Scene: case O::InsertScene: n = arrangement_.scene() + 1; break;
        case O::SceneTrack: text = strips[arrangement_.column() + 1]; break;
        case O::ScenePattern:
            n = project_.scenes[arrangement_.scene()].pattern[arrangement_.column()];
            if (n < 0) text = "EMPTY"; else { snprintf(value, size, "P%02d", n + 1); return; } break;
        case O::SongRow: n = arrangement_.songEntry() + 1; break;
        case O::SongScene: case O::SongRepeats:
            if (arrangement_.songEntry() >= project_.songLength) text = "ADD ROW";
            else n = item.op == O::SongScene ? project_.song[arrangement_.songEntry()].scene + 1 : project_.song[arrangement_.songEntry()].repeats;
            break;
        case O::Strip: text = strips[arrangement_.mixerStrip()]; break;
        case O::Level: {
            const auto channel = arrangement_.mixerStrip();
            snprintf(value, size, "%u%%", channel ? project_.mixer[channel - 1].volume : project_.masterVolume); return;
        }
        case O::Mute: case O::Solo: {
            const auto channel = arrangement_.mixerStrip();
            text = channel && (item.op == O::Mute ? project_.mixer[channel - 1].mute : project_.mixer[channel - 1].solo) ? "ON" : "OFF"; break;
        }
        case O::Pad: n = sampler_.pad() + 1; break;
        case O::Slot: case O::DeleteSlice: snprintf(value, size, "S%02u", sampler_.slot() + 1); return;
        case O::Parts: case O::Chop: n = sampler_.parts(); break;
        default: return;
    }
    if (text) snprintf(value, size, "%s", text); else snprintf(value, size, "%d", n);
}
void AppUi::menuAdjust(const MenuItem& item, int direction) {
    const bool fine = down_[static_cast<uint8_t>(key::Ctrl)];
    const auto t = instrument_.track();
    const auto& arp = project_.arps[t];
    const uint8_t lengths[] = {4, 8, 16};
    Command c;
    switch (item.op) {
        case O::EqBand: arrangement_.onKey(6,direction>0?'/':',',false,false);return;
        case O::EqGain: arrangement_.onKey(6,direction>0?';':'.',false,shift_);return;
        case O::EqBypass:
            if(project_.equalizer.enabled==(direction>0))arrangement_.onKey(6,'b',false,false);
            return;
        case O::VisualParameter: visual_.adjust(item.argument,direction); return;
        case O::Preset: instrument_.loadPreset((instrument_.preset() + 6 + direction) % 6); return;
        case O::Layout: instrument_.setKeyboard(0, direction > 0); return;
        case O::Root: instrument_.setKeyboard(1, (project_.keyboardRoot[t] + 12 + direction) % 12); return;
        case O::Scale: instrument_.setKeyboard(2, (project_.keyboardScale[t] + 4 + direction) % 4); return;
        case O::Master: arrangement_.adjustMaster(direction * (fine ? 1 : 5)); return;
        case O::Tempo: c.type = CommandType::SetBpm; c.value = bounded(project_.bpm + direction * (shift_ ? 10 : 1), 40, 240); break;
        case O::Swing: c.type = CommandType::SetSwing; c.value = bounded(project_.swing + direction, 50, 75); break;
        case O::Length: case O::Grid: {
            const bool grid = item.op == O::Grid;
            int old;
            if (screen_ == 0) { const auto& p = project_.drums[drums_.currentPattern()]; old = grid ? static_cast<int>(p.grid) : p.length; }
            else if (screen_ == 1) { const auto& p = project_.instruments[t][instrument_.pattern()]; old = grid ? static_cast<int>(p.grid) : p.length; }
            else { const auto& p = project_.samplePatterns[sampler_.pattern()].steps; old = grid ? static_cast<int>(p.grid) : p.length; }
            const auto next = grid ? (old + 6 + direction) % 6 : cycle(old, direction, lengths, 3);
            if (screen_ == 0) drums_.setTiming(grid, next);
            else if (screen_ == 1) instrument_.setTiming(grid, next);
            else sampler_.setTiming(grid, next);
            return;
        }
        case O::Pattern:
            if (screen_ == 0) drums_.changePattern(direction);
            else if (screen_ == 1) instrument_.onKey(direction > 0 ? ']' : '[', true, false, false);
            else sampler_.onKey(direction > 0 ? ']' : '[', true, false);
            return;
        case O::Track: instrument_.onKey(direction > 0 ? ']' : '[', true, true, false); return;
        case O::Octave: instrument_.onKey(direction > 0 ? '=' : '-', true, true, false); return;
        case O::Quantize: if (project_.recordQuantize[t] != (direction > 0)) shortcut('q'); return;
        case O::Fit: if (instrument_.fitsNotes() != (direction > 0)) shortcut('f'); return;
        case O::SynthParameter:
            instrument_.setSynthParameter(item.argument, bounded(synthValue(project_.synths[t], item.argument) + direction * (item.argument == 0 || fine ? 1 : 8), 0, item.argument == 0 ? 3 : 127)); return;
        case O::ArpParameter: {
            const int old[] = {static_cast<int>(arp.mode), static_cast<int>(arp.rate), arp.octaves, arp.gate};
            const int low[] = {0, 0, 1, 10}, high[] = {3, 5, 3, 100};
            const auto p = item.argument;
            instrument_.setArpeggiator(p, bounded(old[p] + direction * (p == 3 ? 5 : 1), low[p], high[p])); return;
        }
        case O::Scene: case O::InsertScene: arrangement_.onKey(2, direction > 0 ? '.' : ';', true, false); return;
        case O::SceneTrack: arrangement_.onKey(2, direction > 0 ? '/' : ',', true, false); return;
        case O::ScenePattern: arrangement_.onKey(2, direction > 0 ? '=' : '-', true, false); return;
        case O::SongRow: arrangement_.onKey(3, direction > 0 ? '.' : ';', true, false); return;
        case O::SongScene: case O::SongRepeats:
            arrangement_.onKey(3, item.op == O::SongScene ? ',' : '/', true, false);
            arrangement_.onKey(3, direction > 0 ? '=' : '-', true, false); return;
        case O::Strip: arrangement_.onKey(4, direction > 0 ? '/' : ',', true, false); return;
        case O::Level: arrangement_.onKey(4, direction > 0 ? ';' : '.', true, shift_ || fine); return;
        case O::Mute: case O::Solo: {
            const auto channel = arrangement_.mixerStrip();
            if (channel && (item.op == O::Mute ? project_.mixer[channel - 1].mute : project_.mixer[channel - 1].solo) != (direction > 0)) menuRun(item);
            return;
        }
        case O::Pad: {
            const int pad = bounded(sampler_.pad() + direction, 0, kSamplePads - 1);
            sampler_.selectPad(pad); return;
        }
        case O::Slot: sampler_.assignSlot(bounded(sampler_.slot() + direction, 0, kSampleSlots - 1)); return;
        case O::Parts: {
            const uint8_t parts[] = {4, 8, 16, 32}; sampler_.setParts(cycle(sampler_.parts(), direction, parts, 4)); return;
        }
        default: return;
    }
    menuApply(c);
}
void AppUi::drawMenu(Canvas& canvas) const {
    canvas.fillRect(0, 0, 240, 135, color::Background);

    char line[64]; snprintf(line, sizeof(line), "MENU %s  %u%%  %s", screens[screen_], project_.masterVolume, observedPlaying_ ? "PLAY" : "STOP");
    style::header(canvas,line,nullptr);
    constexpr const char* tabs[] = {"SCREENS", "ACTIONS", "SETTINGS"};
    for (int i = 0; i < 3; ++i) {
        canvas.fillRect(3 + i * 79, 17, 76, 12, i == menuPage_ ? color::LaneSelected : color::Panel);
        canvas.text(7 + i * 79, 19, tabs[i], i == menuPage_ ? color::StepAccent : color::Text);
    }
    const unsigned selected = menuRow_[menuPage_], total = menuCount(menuPage_);
    const unsigned first = selected < 6 ? 0 : selected - 5;
    for (unsigned row = first; row < std::min(total, first + 6); ++row) {
        const auto item = menuItem(menuPage_, row);
        const int y = 34 + (row - first) * 12;
        const bool active = row == selected;
        const char* disabled = menuDisabled(item);
        if (active) canvas.fillRect(3, y, 232, 12, color::LaneSelected);
        if (active) canvas.text(5, y + 2, menuEditing_ ? "*" : ">", color::StepAccent);
        const char* label = item.label;
        if (item.op == O::Record && (screen_ == 5 ? sampler_.recording() : instrument_.busy())) label = screen_ == 5 ? "Stop microphone" : "Finish recording";
        canvas.text(15, y + 2, label, disabled ? color::Muted : color::Text);
        char value[16]; menuValue(item, value, sizeof(value));
        if (value[0]) {
            const int x = 230 - std::strlen(value) * 6;
            if (active && menuEditing_) canvas.fillRect(x - 2, y + 1, std::strlen(value) * 6 + 4, 10, color::StepAccent);
            canvas.text(x, y + 2, value, active && menuEditing_ ? color::Background : active ? color::StepAccent : color::Text);
        }
    }
    if (total > 6) {
        canvas.fillRect(237, 34, 2, 72, color::StepOff);
        canvas.fillRect(237, 34 + selected * 60 / (total - 1), 2, 12, color::StepAccent);
    }
    const auto item = menuItem(menuPage_, selected);
    const char* disabled = menuDisabled(item);
    snprintf(line, sizeof(line), "%.38s", menuNotice_ ? menuNotice_ : disabled ? disabled : warning_ ? warning_ : item.hint);
    if (!disabled && item.op == O::Slot) {
        const auto& clip = project_.samples[sampler_.slot()];
        if (clip.source[0]) snprintf(line, sizeof(line), "S%02u %.32s", sampler_.slot() + 1, clip.source);
        else snprintf(line, sizeof(line), "S%02u EMPTY - import or record first", sampler_.slot() + 1);
    }
    canvas.text(3, 110, line, (disabled && !menuNotice_) || warning_ ? color::Warning : color::StepAccent);
    style::footer(canvas,menuEditing_ ? "Arrows change  Enter done  Esc back" : "Arrows select Tab section Enter Esc");
    if (menuConfirm_) {
        canvas.fillRect(9, 37, 222, 69, color::Warning);
        canvas.fillRect(10, 38, 220, 67, color::Background);
        snprintf(line, sizeof(line), "Delete sample S%02u?", sampler_.slot() + 1);
        canvas.text(20, 45, line, color::Warning);
        snprintf(line, sizeof(line), "%.32s", project_.samples[sampler_.slot()].source);
        canvas.text(20, 59, line, color::Text);
        canvas.text(20, 75, "Original WAV will be kept", color::Text);
        canvas.text(20, 91, "Enter delete   Back cancel", color::StepAccent);
    }
}
}
