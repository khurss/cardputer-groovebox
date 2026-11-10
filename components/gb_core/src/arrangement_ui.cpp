#include "gb/arrangement_ui.h"

#include <algorithm>
#include <cstdio>
namespace gb {
namespace {
Command command(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command cmd;
    cmd.type = type;
    cmd.a = a;
    cmd.b = b;
    cmd.c = c;
    cmd.value = value;
    return cmd;
}
int clamp(int v, int low, int high) { return std::max(low, std::min(v, high)); }
}  // namespace
void ArrangementUi::apply(Command c, Command inverse) {
    if (!sink_(context_, c))
        return;
    applyEdit(p_, c);
    undo_ = inverse;
    undoAvailable_ = true;
    ++edits_;
    dirty_ = true;
}
void ArrangementUi::undo() {
    if (undoAvailable_ && sink_(context_, undo_)) {
        applyEdit(p_, undo_);
        undoAvailable_ = false;
        ++edits_;
    }
}
void ArrangementUi::adjustMaster(int delta) {
    const int old = p_.masterVolume, next = clamp(old + delta, 0, 100);
    if (next != old)
        apply(command(CommandType::SetMasterVolume, 0, 0, 0, next),
              command(CommandType::SetMasterVolume, 0, 0, 0, old));
}
void ArrangementUi::onKey(uint8_t screen, char code, bool fn, bool shift) {
    dirty_ = true;
    if (fn && code == 'z') {
        undo();
        return;
    }
    if (screen == 2)
        sceneKey(code, fn);
    else if (screen == 3)
        songKey(code, fn);
    else if (screen == 4)
        mixerKey(code, fn, shift);
}
void ArrangementUi::sceneKey(char code, bool fn) {
    if ((fn && code == ';') || code == '[')
        scene_ = clamp(scene_ - 1, 0, kScenes - 1);
    else if ((fn && code == '.') || code == ']')
        scene_ = clamp(scene_ + 1, 0, kScenes - 1);
    else if (fn && code == ',')
        column_ = clamp(column_ - 1, 0, kTracks - 1);
    else if (fn && code == '/')
        column_ = clamp(column_ + 1, 0, kTracks - 1);
    else if (code >= '1' && code <= '6' && !fn)
        column_ = code - '1';
    else if (fn && code == 'c') {
        clipboard_ = p_.scenes[scene_];
        clipboardFull_ = true;
    } else if (fn && code == 'v' && clipboardFull_) {
        auto c = command(CommandType::SetScene, scene_), old = c;
        for (int t = 0; t < kTracks; ++t) {
            c.scenePatterns[t] = clipboard_.pattern[t];
            old.scenePatterns[t] = p_.scenes[scene_].pattern[t];
        }
        apply(c, old);
    } else if (code == '-' || code == '=' || code == key::Backspace) {
        const int previous = p_.scenes[scene_].pattern[column_];
        const int next = code == key::Backspace ? -1 : clamp(previous + (code == '=' ? 1 : -1), -1, 15);
        if (next != previous)
            apply(command(CommandType::SetScenePattern, scene_, column_, static_cast<uint8_t>(next)),
                  command(CommandType::SetScenePattern, scene_, column_, static_cast<uint8_t>(previous)));
    }
}
void ArrangementUi::songKey(char code, bool fn) {
    if (fn && code == ';')
        entry_ = clamp(entry_ - 1, 0, std::min<int>(p_.songLength, kSongEntries - 1));
    else if (fn && code == '.')
        entry_ = clamp(entry_ + 1, 0, std::min<int>(p_.songLength, kSongEntries - 1));
    else if (fn && (code == ',' || code == '/'))
        songField_ = code == '/' ? 1 : 0;
    else if (fn && code == key::Enter && p_.songLength < kSongEntries) {
        const uint8_t at = std::min(entry_, p_.songLength);
        apply(command(CommandType::InsertSongEntry, at, scene_, 0, 1), command(CommandType::DeleteSongEntry, at));
        entry_ = at;
    } else if (code == key::Backspace && entry_ < p_.songLength) {
        const auto old = p_.song[entry_];
        apply(command(CommandType::DeleteSongEntry, entry_),
              command(CommandType::InsertSongEntry, entry_, old.scene, 0, old.repeats));
        entry_ = std::min<uint8_t>(entry_, p_.songLength ? p_.songLength - 1 : 0);
    } else if ((code == '-' || code == '=') && entry_ < p_.songLength) {
        const auto old = p_.song[entry_];
        auto next = old;
        const int delta = code == '=' ? 1 : -1;
        if (songField_ == 0)
            next.scene = clamp(next.scene + delta, 0, kScenes - 1);
        else
            next.repeats = clamp(next.repeats + delta, 1, 255);
        apply(command(CommandType::SetSongEntry, entry_, next.scene, 0, next.repeats),
              command(CommandType::SetSongEntry, entry_, old.scene, 0, old.repeats));
    }
}
void ArrangementUi::mixerKey(char code, bool fn, bool shift) {
    if (fn && code == ',') {
        mix_ = clamp(mix_ - 1, 0, kTracks);
        return;
    }
    if (fn && code == '/') {
        mix_ = clamp(mix_ + 1, 0, kTracks);
        return;
    }
    if (code == '=' || code == '-' || (fn && (code == ';' || code == '.'))) {
        const int delta = (code == '=' || code == ';' ? 1 : -1) * (shift ? 1 : 5);
        const auto type = mix_ == 0 ? CommandType::SetMasterVolume : CommandType::SetTrackVolume;
        const uint8_t track = mix_ ? mix_ - 1 : 0;
        const int old = mix_ ? p_.mixer[track].volume : p_.masterVolume;
        apply(command(type, track, 0, 0, clamp(old + delta, 0, 100)), command(type, track, 0, 0, old));
    } else if (mix_ && (code == 'm' || code == 's')) {
        const uint8_t track = mix_ - 1;
        const bool old = code == 'm' ? p_.mixer[track].mute : p_.mixer[track].solo;
        const auto type = code == 'm' ? CommandType::SetTrackMute : CommandType::SetTrackSolo;
        apply(command(type, track, 0, 0, !old), command(type, track, 0, 0, old));
    }
}
void ArrangementUi::draw(Canvas& c, uint8_t screen, bool playing) const {
    c.fillRect(0, 0, 240, 135, color::Background);
    char line[64];
    if (screen == 2) {
        snprintf(line, sizeof(line), "SCENES %02u  %s", scene_ + 1, playing ? "PLAY" : "STOP");
        c.text(0, 2, line, color::Text);
        c.text(36, 16, "DRUM  T1   T2   T3   T4  SMP", color::Text);
        const int first = (scene_ / 7) * 7;
        for (int r = 0; r < 7 && first + r < kScenes; ++r) {
            const int index = first + r, y = 28 + r * 12;
            snprintf(line, sizeof(line), "%02d%s", index + 1,
                     playing && int(status_ & 15) == index   ? "*"
                     : int((status_ >> 4) & 31) == index + 1 ? "+"
                                                             : " ");
            c.text(0, y, line, color::Text);
            for (int t = 0; t < kTracks; ++t) {
                const int x = 36 + t * 34;
                if (index == scene_ && t == column_)
                    c.fillRect(x, y, 33, 11, color::LaneSelected);
                const int pat = p_.scenes[index].pattern[t];
                if (pat < 0)
                    snprintf(line, sizeof(line), "--");
                else
                    snprintf(line, sizeof(line), "P%02d", pat + 1);
                c.text(x + 2, y + 1, line, color::Text);
            }
        }
        c.text(0, 124, "[]scene Fn+/- pat Enter:launch", color::Text);
    } else if (screen == 3) {
        snprintf(line, sizeof(line), "SONG %u/64 %s", p_.songLength, playing ? "PLAY" : "STOP");
        c.text(0, 2, line, color::Text);
        if (!p_.songLength)
            c.text(0, 22, "Fn+Enter: add selected scene", color::Warning);
        const int first = (entry_ / 7) * 7;
        for (int r = 0; r < 7 && first + r < kSongEntries; ++r) {
            const int i = first + r, y = 28 + r * 12;
            if (i > p_.songLength)
                break;
            if (i == entry_)
                c.fillRect(0, y, 240, 11, color::LaneSelected);
            if (i == p_.songLength)
                snprintf(line, sizeof(line), "%02d + ADD", i + 1);
            else
                snprintf(line, sizeof(line), "%02d%s Scene %02u   x%u %s", i + 1,
                         playing && (status_ & (1u << 9)) && int((status_ >> 10) & 63) == i ? "*" : " ",
                         p_.song[i].scene + 1, p_.song[i].repeats,
                         i == entry_ ? (songField_ ? "REPEAT" : "SCENE") : "");
            c.text(0, y + 1, line, color::Text);
        }
        c.text(0, 124, "Fn+Enter:add Back:del Fn+/-:edit", color::Text);
    } else {
        c.text(0, 2, "MIXER  +/- MAIN  M mute S solo", color::Text);
        constexpr const char* names[] = {"MAIN", "DRUM", "T1", "T2", "T3", "T4", "SMP"};
        for (int i = 0; i <= kTracks; ++i) {
            const int x = i * 34;
            const int volume = i ? p_.mixer[i - 1].volume : p_.masterVolume;
            if (i == mix_)
                c.fillRect(x, 16, 33, 105, color::StepOutside);
            c.text(x + 2, 20, names[i], i == mix_ ? color::StepAccent : color::Text);
            c.fillRect(x + 5, 40, 24, 60, color::StepOff);
            if (volume)
                c.fillRect(x + 5, 100 - volume * 60 / 100, 24, std::max(1, volume * 60 / 100), color::StepOn);
            snprintf(line, sizeof(line), "%u%%", volume);
            c.text(x + 3, 104, line, color::Text);
            if (i && (p_.mixer[i - 1].mute || p_.mixer[i - 1].solo))
                c.text(x + 26, 32, p_.mixer[i - 1].mute ? "M" : "S", color::Warning);
        }
        c.text(0, 124, "Fn+arrows select/edit Shift:1%", color::Text);
    }
}
}  // namespace gb
