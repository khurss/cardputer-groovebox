#pragma once
#include "gb/ui_types.h"
namespace gb {
class ArrangementUi {
public:
    ArrangementUi(Project& project, CommandSink sink, void* context) : p_(project), sink_(sink), context_(context) {}
    void onKey(uint8_t screen, char code, bool fn, bool shift);
    void adjustMaster(int delta);
    void draw(Canvas& c, uint8_t screen, bool playing) const;
    void setStatus(uint32_t status) {
        if (status_ != status)
            dirty_ = true;
        status_ = status;
    }
    uint8_t scene() const { return scene_; }
    uint8_t songEntry() const { return entry_; }
    uint8_t column() const { return column_; }
    uint8_t mixerStrip() const { return mix_; }
    bool editingRepeats() const { return songField_ != 0; }
    uint32_t edits() const { return edits_; }
    bool canUndo() const { return undoAvailable_; }
    bool canPaste() const { return clipboardFull_; }
    bool takeDirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    void apply(Command command, Command inverse);
    void undo();
    void sceneKey(char code, bool fn);
    void songKey(char code, bool fn);
    void mixerKey(char code, bool fn, bool shift);
    Project& p_;
    CommandSink sink_;
    void* context_;
    uint8_t scene_ = 0, column_ = 0, entry_ = 0, songField_ = 0, mix_ = 0;
    uint32_t status_ = 0, edits_ = 0;
    Command undo_;
    Scene clipboard_;
    bool undoAvailable_ = false, clipboardFull_ = false, dirty_ = true;
};
}  // namespace gb
