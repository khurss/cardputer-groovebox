#include "gb/ui.h"
#include "gb/ui_style.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace gb {
namespace {
const char* const lessons[][7] = {
    {"WELCOME / 1", "Make a beat, play a melody,", "then arrange a whole track.", "", "OPT opens the menu.", "Fn is only a modifier now.", "No popup while you play."},
    {"MOVE AROUND / 2", "OPT > SCREENS: choose a workspace.", "Fn + arrows: move selection.", "ENTER: choose / edit / confirm.", "TAB: next menu section.", "BACKSPACE: back. OPT: close.", "+ / -: master volume."},
    {"DRUMS / 3", "Fn+1: drums. 1..8 choose a sound.", "Q W E R T Y U I = steps 1..8", "A S D F G H J K = steps 9..16", "Tap steps to add/remove hits.", "SHIFT + step: accented hit.", "SPACE plays / stops the scene."},
    {"SYNTH / 4", "Fn+2: synth. OPT > ACTIONS.", "Choose Performance for strings.", "Lines follow pitch; dots show fingers.", "Each keyboard row is one string.", "SETTINGS: scale, root and sound.", "Piano roll remains available."},
    {"RECORD NOTES / 5", "ALT arms notes. SPACE starts.", "Play keys; notes appear live.", "ALT ends the recording pass.", "Piano roll: Fn+arrows to move.", "ENTER adds/selects; BS deletes.", "ACTIONS > Undo reverts a pass."},
    {"SAMPLES / 6", "Fn+6: sampler. Put WAV on SD in", "/groovebox/samples/", "ACTIONS: Browse WAV or Record mic.", "Wave view: A=start, D=end.", "ENTER saves a slice to a pad.", "Pad sequencer places sample hits."},
    {"BUILD A SONG / 7", "Fn+3: scenes combine patterns.", "Choose track and assigned pattern.", "Fn+4: song. Insert scenes via menu.", "Set repeats, ENTER plays from row.", "Fn+5: mixer levels / mute / solo.", "SPACE stops. + / - sets volume."},
    {"KEEP YOUR WORK / 8", "OPT > SCREENS > Projects.", "New: blank project with a name.", "Save: write current project.", "Save copy: keep another version.", "Open: saves current, then reloads.", "Auto-save: stopped, 3 seconds idle."}
};
}
ProjectAction AppUi::takeProjectRequest(char* name, size_t size) {
    const auto action = projectRequest_;
    projectRequest_ = ProjectAction::None;
    snprintf(name, size, "%s", projectName_);
    return action;
}
void AppUi::projectResult(const char* message) {
    snprintf(library_.message, sizeof(library_.message), "%.39s", message);
    dirty_ = true;
}
void AppUi::helpKey(char code) {
    if (code == key::Opt || code == key::Backspace || code == 27) { helpOpen_ = false; dirty_ = true; return; }
    if (code == key::Enter || code == key::Tab || (fn_ && (code == '/' || code == '.')))
        helpPage_ = (helpPage_ + 1) % 9;
    if (fn_ && (code == ',' || code == ';')) helpPage_ = (helpPage_ + 8) % 9;
}
void AppUi::drawHelp(Canvas& c) const {
    if (helpPage_ == 8) { drawFnMenu(c); return; }
    c.fillRect(0, 0, 240, 135, color::Background);
    style::header(c,lessons[helpPage_][0],nullptr);
    for (int i = 1; i < 7; ++i) c.text(5, 19 + i * 13, lessons[helpPage_][i], color::Text);
    style::footer(c,"Enter next  Fn LEFT prev  Esc close");
}
void AppUi::projectsKey(char code) {
    if (code == key::Esc && (nameAction_ != ProjectAction::None || projectConfirm_)) {
        nameAction_ = ProjectAction::None; projectConfirm_ = false; return;
    }
    if (code == key::Opt || code == key::Esc) {
        projectsOpen_ = false; nameAction_ = ProjectAction::None; projectConfirm_ = false; return;
    }
    if (nameAction_ != ProjectAction::None) {
        const size_t n = strlen(projectName_);
        if (code == key::Backspace) { if (n) projectName_[n - 1] = 0; else nameAction_ = ProjectAction::None; }
        else if (code == key::Enter && ProjectLibrary::validName(projectName_)) {
            projectRequest_ = nameAction_; nameAction_ = ProjectAction::None;
        } else if (!fn_ && n + 1 < sizeof(projectName_) && ((code >= 'a' && code <= 'z') || (code >= '0' && code <= '9') || code == '-' || code == '_')) {
            projectName_[n] = code; projectName_[n + 1] = 0;
        }
        return;
    }
    if (code == key::Backspace) { if (projectConfirm_) projectConfirm_ = false; else projectsOpen_ = false; return; }
    if (projectConfirm_) {
        if (code == key::Enter) { projectRequest_ = ProjectAction::Load; projectConfirm_ = false; }
        return;
    }
    if (fn_ && code == ';' && projectRow_) --projectRow_;
    if (fn_ && code == '.' && projectRow_ < 3 + library_.count) ++projectRow_;
    if (code != key::Enter) return;
    if (!library_.available) { projectResult("SD card unavailable"); return; }
    if (busy() || observedPlaying_ || transportPending_) { projectResult("Stop playback / recording first"); return; }
    if (projectRow_ == 0) { projectRequest_ = ProjectAction::Save; return; }
    if (projectRow_ < 3) {
        nameAction_ = projectRow_ == 1 ? ProjectAction::New : ProjectAction::Copy;
        projectName_[0] = 0; return;
    }
    if (projectRow_ == 3) { projectRequest_ = ProjectAction::Demos; return; }
    const unsigned index = projectRow_ - 4;
    if (index < library_.count) { strcpy(projectName_, library_.names[index]); projectConfirm_ = true; }
}
void AppUi::drawProjects(Canvas& c) const {
    c.fillRect(0, 0, 240, 135, color::Background);
    style::header(c,"PROJECTS",nullptr);
    char line[48]; snprintf(line, sizeof(line), "Current: %.23s", library_.current);
    c.text(4, 21, line, color::StepAccent);
    if (nameAction_ != ProjectAction::None) {
        c.text(4, 43, nameAction_ == ProjectAction::New ? "NEW PROJECT" : "SAVE COPY", color::Text);
        snprintf(line, sizeof(line), "%s_", projectName_); c.text(4, 62, line, color::StepAccent);
        c.text(4, 85, "Name: a-z 0-9 - _ (23 characters)", color::Text);
        c.text(4, 102, "Existing names are never overwritten", color::Text);
    } else if (projectConfirm_) {
        snprintf(line, sizeof(line), "Open %.23s?", projectName_); c.text(4, 48, line, color::StepAccent);
        c.text(4, 69, "Current project will be saved first.", color::Text);
        c.text(4, 88, "Device restarts to load audio.", color::Text);
        c.text(4, 103, "ENTER open / BACK cancel", color::Text);
    } else {
        const unsigned first = projectRow_ < 5 ? 0 : projectRow_ - 4;
        for (unsigned row = first; row < std::min<unsigned>(4 + library_.count, first + 5); ++row) {
            const int y = 36 + (row - first) * 13;
            if (row == projectRow_) c.fillRect(2, y, 236, 12, color::LaneSelected);
            const char* label = row == 0 ? "Save current" : row == 1 ? "New project..." : row == 2 ? "Save copy as..." : row == 3 ? "Add demo songs" : library_.names[row - 4];
            snprintf(line, sizeof(line), "%c %s", row == projectRow_ ? '>' : ' ', label); c.text(4, y + 2, line, color::Text);
        }
        c.text(4, 106, library_.message[0] ? library_.message : library_.more ? "First 32 files shown" : "Select a file to open it", color::StepAccent);
    }
    style::footer(c,"Fn arrows choose  Enter  Esc back");
}
}
