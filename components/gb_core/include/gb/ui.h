#pragma once
#include <cstddef>
#include "gb/arrangement_ui.h"
#include "gb/audio_visual.h"
#include "gb/drum_ui.h"
#include "gb/instrument_ui.h"
#include "gb/sampler_ui.h"
#include "gb/menu.h"
#include "gb/project_library.h"
namespace gb {
class AppUi {
public:
    AppUi(Project& project, CommandSink sink, void* context);
    void onKey(char code, bool pressed);
    AudioVisual& visual() { return visual_; }
    const AudioVisual& visual() const { return visual_; }
    void audioFrame(const AudioFrame& frame) { visual_.process(frame); if (screen_ >= 6) dirty_ = true; }
    void setPlayback(bool playing);
    void receive(const RecordUpdate& update);
    void setPosition(uint32_t tick);
    void setTime(uint32_t milliseconds);
    void setVoiceBudget(uint8_t voices) { if (voiceBudget_ != voices) { voiceBudget_ = voices; dirty_ = true; } }
    void setTransport(uint32_t status) {
        if ((status >> 17) != (status_ >> 17))
            transportPending_ = false;
        status_ = status;
        arrangement_.setStatus(status);
    }
    void draw(Canvas& canvas, uint32_t tick, bool playing, bool storageAvailable) const;
    void clearPattern();
    void setWarning(const char* warning);
    bool takeSaveRequest();
    bool takeDirty();
    uint32_t editCount() const { return drums_.editCount() + instrument_.editCount() + arrangement_.edits() + sampler_.edits() + menuEdits_; }
    bool busy() const { return instrument_.busy() || sampler_.busy(); }
    bool menuOpen() const { return menuOpen_; }
    uint8_t menuPage() const { return menuPage_; }
    uint8_t menuRow() const { return menuRow_[menuPage_]; }
    bool menuEditing() const { return menuEditing_; }
    bool menuConfirming() const { return menuConfirm_; }
    ProjectLibraryState& library() { return library_; }
    ProjectAction takeProjectRequest(char* name, size_t size);
    void projectResult(const char* message);
    bool helpOpen() const { return helpOpen_; }
    bool projectsOpen() const { return projectsOpen_; }
    uint8_t screen() const { return screen_; }
    ArrangementUi& arrangement() { return arrangement_; }
    const ArrangementUi& arrangement() const { return arrangement_; }
    SamplerUi& sampler() { return sampler_; }
    const SamplerUi& sampler() const { return sampler_; }
    DrumUi& drums() { return drums_; }
    const DrumUi& drums() const { return drums_; }
    InstrumentUi& instrument() { return instrument_; }
    const InstrumentUi& instrument() const { return instrument_; }

private:
    static constexpr uint8_t kTutorialPages = 9;
    void helpKey(char code);
    void drawHelp(Canvas& canvas) const;
    void projectsKey(char code);
    void drawProjects(Canvas& canvas) const;
    ProjectLibraryState library_;
    ProjectAction projectRequest_ = ProjectAction::None, nameAction_ = ProjectAction::None;
    char projectName_[kProjectName] = {};
    bool projectsOpen_ = false, helpOpen_ = false, projectConfirm_ = false;
    uint8_t projectRow_ = 0, helpPage_ = 0;
    void selectScreen(uint8_t screen);
    void launch(bool song);
    void drawFnMenu(Canvas& canvas) const;
    void toggleTransport();
    void toggleMenu();
    void menuKey(char code);
    void drawMenu(Canvas& canvas) const;
    unsigned menuCount(uint8_t page) const;
    MenuItem menuItem(uint8_t page, unsigned row) const;
    const char* menuDisabled(const MenuItem& item) const;
    void menuValue(const MenuItem& item, char* value, size_t size) const;
    void menuAdjust(const MenuItem& item, int direction);
    void menuRun(const MenuItem& item);
    void shortcut(char code);
    void menuApply(Command command);
    Project& project_;
    CommandSink sink_;
    void* context_;
    DrumUi drums_;
    InstrumentUi instrument_;
    ArrangementUi arrangement_;
    SamplerUi sampler_;
    AudioVisual visual_;
    uint32_t status_ = 0;
    uint32_t fnPressedMs_ = 0, menuEdits_ = 0;
    bool fnUsed_ = false, menuOpen_ = false, menuEditing_ = false, menuConfirm_ = false;
    uint8_t menuPage_ = 0, menuRow_[3] = {};
    uint32_t nowMs_ = 0, volumeUntilMs_ = 0;
    bool volumeVisible_ = false;
    bool pendingClear_ = false, pendingLaunch_ = false, pendingSong_ = false;
    bool down_[128] = {};
    bool fn_ = false, shift_ = false, desiredPlaying_ = false, transportPending_ = false, observedPlaying_ = false;
    bool dirty_ = true, saveRequested_ = false;
    uint8_t screen_ = 0;
    uint8_t voiceBudget_ = 5;
    int8_t pendingScreen_ = -1;
    const char* warning_ = nullptr;
    const char* menuNotice_ = nullptr;
};
}  // namespace gb
