#include "gb/instrument_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
namespace gb {
namespace {
Command cmd(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command out;
    out.type = type;
    out.a = a;
    out.b = b;
    out.c = c;
    out.value = value;
    return out;
}
int piano(char code) {
    constexpr char keys[] = "zsxdcvgbhnjm,q2w3er5t6y7ui";
    constexpr int semitones[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    const auto* p = code ? strchr(keys, code) : nullptr;
    return p ? semitones[p - keys] : -1;
}
int stringRow(char code) {
    constexpr const char* rows[] = {"zxcvbnm,./", "asdfghjkl;", "qwertyuiop", "1234567890"};
    for (int i = 0; i < 4; ++i) if (code && strchr(rows[i], code)) return i;
    return -1;
}
int stringPitch(char code, int octave, int root, int scale) {
    constexpr const char* rows[] = {"zxcvbnm,./", "asdfghjkl;", "qwertyuiop", "1234567890"};
    const int row = stringRow(code); if (row < 0) return -1;
    int pitch = (octave + 1) * 12 + root + row * 5;
    const int column = strchr(rows[row], code) - rows[row];
    constexpr uint16_t masks[] = {0xfff, 0xab5, 0x5ad, 0x295};
    while (!(masks[scale] & (1 << ((pitch - root + 120) % 12)))) ++pitch;
    for (int n = 0; n < column; ++n) do { ++pitch; } while (!(masks[scale] & (1 << ((pitch - root + 120) % 12))));
    return pitch;
}
const char* gridName(Grid g) {
    constexpr const char* n[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"};
    return n[static_cast<uint8_t>(g)];
}
uint16_t bounded(int value, int lo, int hi) { return static_cast<uint16_t>(std::max(lo, std::min(hi, value))); }
uint8_t paramValue(const SynthSound& s, uint8_t index) {
    const uint8_t v[] = {
        static_cast<uint8_t>(s.wave), s.attack, s.decay, s.sustain, s.release, s.cutoff, s.resonance, s.glide, s.level};
    return v[index];
}
constexpr const char* kParamLabels[] = {"WAVE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
                                       "CUTOFF", "RESONANCE", "GLIDE", "LEVEL"};
constexpr const char* kWaves[] = {"SINE", "TRIANGLE", "SAW", "SQUARE"};
}  // namespace
InstrumentUi::InstrumentUi(Project& project, CommandSink sink, void* context)
    : project_(project), sink_(sink), context_(context) {
    const int p = project.scenes[editScene_].pattern[1];
    if (p >= 0) pattern_ = p;
    focusPattern();
}
void InstrumentUi::focusPattern() {
    if (current().noteCount) {
        cursorPitch_ = current().notes[0].pitch;
        cursorTick_ = current().notes[0].startTick % patternTicks(current().length, current().grid);
    } else
        cursorTick_ = 0;
    dirty_ = true;
}
void InstrumentUi::setScene(uint8_t scene) {
    if (scene >= kScenes || locked_) return;
    editScene_ = scene;
    const int p = project_.scenes[scene].pattern[track_ + 1];
    if (p >= 0) pattern_ = p;
    selected_ = -1;
    focusPattern();
}
bool InstrumentUi::send(Command c) {
    dirty_ = true;
    return sink_(context_, c);
}
void InstrumentUi::apply(Command c) {
    if (send(c)) {
        applyEdit(project_, c);
        ++edits_;
    }
}
void InstrumentUi::remember() {
    undo_ = current();
    undoTrack_ = track_;
    undoPattern_ = pattern_;
    undoAvailable_ = true;
    soundUndo_ = false;
    commandUndo_ = false;
}
void InstrumentUi::rememberCommand(CommandType type, uint8_t parameter, uint16_t value) {
    if (locked_) return;  // The entire ongoing recording pass remains one undo action.
    soundUndo_ = false;
    undoType_ = type;
    undoTrack_ = track_;
    undoParameter_ = parameter;
    undoValue_ = value;
    undoAvailable_ = commandUndo_ = true;
}
void InstrumentUi::replace(uint8_t track, uint8_t pattern, const InstrumentPattern& contents) {
    if (!send(cmd(CommandType::BeginInstrumentEdit, track, pattern, contents.length,
                  static_cast<uint16_t>(contents.grid))))
        return;
    for (uint8_t i = 0; i < contents.noteCount; ++i) {
        auto c = cmd(CommandType::StageInstrumentNote, track, pattern, i);
        c.note = contents.notes[i];
        if (!send(c)) {
            send(cmd(CommandType::CancelInstrumentEdit, track, pattern));
            return;
        }
    }
    if (send(cmd(CommandType::CommitInstrumentEdit, track, pattern))) {
        project_.instruments[track][pattern] = contents;
        ++edits_;
        selected_ = -1;
        if (track == track_ && pattern == pattern_)
            cursorTick_ = std::min<uint16_t>(cursorTick_, patternTicks(contents.length, contents.grid) - 1);
    }
}
void InstrumentUi::releaseLive() {
    for (auto& h : held_) h.active = false;
    send(cmd(CommandType::ReleaseLive));
}
bool InstrumentUi::prepareExit() {
    releaseLive();
    if (locked_) {
        ending_ = true;
        return false;
    }
    return true;
}
void InstrumentUi::selectTarget(int track, int pattern) {
    if (track < 0 || track >= kInstrumentTracks || pattern < 0 || pattern >= kPatternsPerTrack) return;
    if (!prepareExit()) {
        pendingTrack_ = track;
        pendingPattern_ = pattern;
        return;
    }
    track_ = track;
    pattern_ = pattern;
    selected_ = -1;
    cursorTick_ = 0;
    full_ = false;
    undoAvailable_ = false;
    apply(cmd(CommandType::SetScenePattern, editScene_, track_ + 1, pattern_));
    focusPattern();
}
void InstrumentUi::toggleRecording() {
    if (locked_) {
        if (!ending_) {
            releaseLive();
            ending_ = true;
        }
        return;
    }
    // Make the selected clip audible before arming; no scene editor is needed at this stage.
    activate();
    remember();
    full_ = false;
    if (send(cmd(CommandType::SetRecording, track_, pattern_, 0, 1))) {
        locked_ = armed_ = true;
        recording_ = false;
    }
}
void InstrumentUi::activate() {
    if (project_.scenes[editScene_].pattern[track_ + 1] != pattern_)
        apply(cmd(CommandType::SetScenePattern, editScene_, track_ + 1, pattern_));
}
void InstrumentUi::receive(const RecordUpdate& u) {
    if (u.type == RecordUpdateType::Added) {
        auto& p = project_.instruments[u.track][u.pattern];
        if (u.slot == p.noteCount && p.noteCount < kMaxNotes) {
            p.notes[p.noteCount++] = u.note;
            ++edits_;
            if (u.track == track_ && u.pattern == pattern_) {
                cursorPitch_ = u.note.pitch;
                cursorTick_ = u.note.startTick;
                selected_ = u.slot;
            }
        }
    } else if (u.type == RecordUpdateType::Armed) {
        armed_ = true;
    } else if (u.type == RecordUpdateType::Recording) {
        recording_ = true;
    } else if (u.type == RecordUpdateType::Full) {
        full_ = true;
    } else if (u.type == RecordUpdateType::Ended) {
        for(auto& h:held_) h.active=false;
        locked_ = armed_ = recording_ = ending_ = false;
        send(cmd(CommandType::AcknowledgeRecording));
        if (pendingTrack_ >= 0) {
            const int t = pendingTrack_, p = pendingPattern_;
            pendingTrack_ = pendingPattern_ = -1;
            selectTarget(t, p);
        }
    }
    dirty_ = true;
}
void InstrumentUi::clearPattern() {
    if (locked_) return;
    remember();
    apply(cmd(CommandType::ClearNotes, track_, pattern_));
    selected_ = -1;
    full_ = false;
}
void InstrumentUi::setTiming(bool grid, uint16_t value) {
    auto c = cmd(grid ? CommandType::SetInstrumentGrid : CommandType::SetInstrumentLength, track_, pattern_, 0, value);
    if (locked_ || !isValidCommand(c)) return;
    remember(); apply(c); cursorTick_ = 0; selected_ = -1;
}
void InstrumentUi::setSynthParameter(uint8_t parameter, uint16_t value) {
    auto c = cmd(CommandType::SetSynthParam, track_, parameter, 0, value);
    if (locked_ || !isValidCommand(c)) return;
    rememberCommand(c.type, parameter, paramValue(project_.synths[track_], parameter)); apply(c);
}
void InstrumentUi::setArpeggiator(uint8_t parameter, uint16_t value) {
    auto c = cmd(CommandType::SetArpParam, track_, parameter, 0, value);
    if (locked_ || !isValidCommand(c)) return;
    const auto& a = project_.arps[track_];
    const uint16_t old[] = {static_cast<uint16_t>(a.mode), static_cast<uint16_t>(a.rate), a.octaves, a.gate};
    releaseLive(); rememberCommand(c.type, parameter, old[parameter]); apply(c);
}
void InstrumentUi::move(char code, bool shift) {
    const bool horizontal = code == ',' || code == '/';
    const int direction = (code == '/' || code == ';') ? 1 : -1;
    if (parameters_) {
        if (horizontal)
            parameter_ = static_cast<uint8_t>((parameter_ + 9 + direction) % 9);
        else {
            const int hi = parameter_ == 0 ? 3 : 127, step = parameter_ == 0 ? 1 : 8;
            rememberCommand(CommandType::SetSynthParam, parameter_, paramValue(project_.synths[track_], parameter_));
            apply(cmd(CommandType::SetSynthParam, track_, parameter_, 0,
                      bounded(paramValue(project_.synths[track_], parameter_) + direction * step, 0, hi)));
        }
        return;
    }
    if (locked_) return;
    const int total = patternTicks(current().length, current().grid), step = gridTicks(current().grid);
    if (selected_ < 0) {
        if (horizontal)
            cursorTick_ = bounded(cursorTick_ + direction * step, 0, total - 1);
        else
            cursorPitch_ = bounded(cursorPitch_ + direction, 0, 127);
        return;
    }
    Note n = current().notes[selected_];
    if (shift) {
        if (horizontal)
            n.lengthTicks = bounded(n.lengthTicks + direction * step, 1, total);
        else
            n.velocity = static_cast<uint8_t>(bounded(n.velocity + direction * 8, 1, 127));
    } else if (horizontal)
        n.startTick = bounded(n.startTick + direction * step, 0, total - 1);
    else
        n.pitch = static_cast<uint8_t>(bounded(n.pitch + direction, 0, 127));
    remember();
    auto c = cmd(CommandType::SetNote, track_, pattern_, selected_);
    c.note = n;
    apply(c);
    cursorTick_ = n.startTick;
    cursorPitch_ = n.pitch;
}
void InstrumentUi::enter() {
    if (locked_ || parameters_) return;
    const auto& p = current();
    const uint32_t total = patternTicks(p.length, p.grid);
    for (int j = 1; j <= p.noteCount; ++j) {
        const int i = (std::max(-1, selected_) + j) % p.noteCount;
        const auto& n = p.notes[i];
        if (n.startTick < total && n.pitch == cursorPitch_ &&
            (cursorTick_ + total - n.startTick) % total < std::min<uint32_t>(n.lengthTicks, total)) {
            selected_ = i;
            cursorTick_ = n.startTick;
            return;
        }
    }
    if (p.noteCount == kMaxNotes) {
        full_ = true;
        return;
    }
    remember();
    auto c = cmd(CommandType::AddNote, track_, pattern_);
    c.note = {cursorTick_, gridTicks(p.grid), static_cast<uint8_t>(cursorPitch_), 100};
    selected_ = p.noteCount;
    apply(c);
}
void InstrumentUi::fnPress(char code, bool shift) {
    if (code == '-' || code == '=') {
        octave_ = std::max(-1, std::min(8, octave_ + (code == '=' ? 1 : -1)));
        return;
    }
    if (code == 'f') {
        fitNotes_ = !fitNotes_;
        dirty_ = true;
        return;
    }
    if (code == '[' || code == ']') {
        const int t = track_ + (code == ']' ? 1 : -1);
        const int p = t >= 0 && t < kInstrumentTracks ? project_.scenes[editScene_].pattern[t + 1] : 0;
        selectTarget(t, p < 0 ? 0 : p);
        return;
    }
    if (code == ',' || code == '/' || code == ';' || code == '.') {
        move(code, shift);
        return;
    }
    if (locked_) return;
    const auto& arp = project_.arps[track_];
    if (code == 'a' || code == 'o' || code == 't') {
        auto param = ArpParam::Mode;
        uint16_t value = (static_cast<uint8_t>(arp.mode) + 1) % 4;
        if (code == 'a' && shift) {
            param = ArpParam::Rate;
            value = (static_cast<uint8_t>(arp.rate) + 1) % 6;
        }
        if (code == 'o') {
            param = ArpParam::Octaves;
            value = arp.octaves % 3 + 1;
        }
        if (code == 't') {
            param = ArpParam::Gate;
            value = arp.gate >= 100 ? 25 : std::min(100, int(arp.gate) + 25);
        }
        releaseLive();
        const uint16_t old = param == ArpParam::Mode      ? static_cast<uint8_t>(arp.mode)
                             : param == ArpParam::Rate    ? static_cast<uint8_t>(arp.rate)
                             : param == ArpParam::Octaves ? arp.octaves
                                                          : arp.gate;
        rememberCommand(CommandType::SetArpParam, static_cast<uint8_t>(param), old);
        apply(cmd(CommandType::SetArpParam, track_, static_cast<uint8_t>(param), 0, value));
        return;
    }
    switch (code) {
        case key::Enter:
            selected_ = -1;
            break;
        case 'l':
            remember();
            apply(cmd(CommandType::SetInstrumentLength, track_, pattern_, 0,
                      current().length == 4   ? 8
                      : current().length == 8 ? 16
                                              : 4));
            cursorTick_ = 0;
            selected_ = -1;
            break;
        case 'g': {
            constexpr Grid cycle[] = {Grid::Sixteenth,    Grid::Eighth,           Grid::Quarter,
                                      Grid::ThirtySecond, Grid::SixteenthTriplet, Grid::EighthTriplet};
            int i = 0;
            while (i < 6 && cycle[i] != current().grid) ++i;
            remember();
            apply(cmd(CommandType::SetInstrumentGrid, track_, pattern_, 0, static_cast<uint16_t>(cycle[(i + 1) % 6])));
            selected_ = -1;
            cursorTick_ = 0;
            break;
        }
        case 'w': {
            constexpr uint8_t values[] = {50, 54, 58, 62, 66, 70, 75};
            int i = 0;
            while (i < 7 && values[i] != project_.swing) ++i;
            rememberCommand(CommandType::SetSwing, 0, project_.swing);
            apply(cmd(CommandType::SetSwing, 0, 0, 0, values[(i + 1) % 7]));
            break;
        }
        case 'q':
            rememberCommand(CommandType::SetRecordQuantize, 0, project_.recordQuantize[track_]);
            apply(cmd(CommandType::SetRecordQuantize, track_, 0, 0, !project_.recordQuantize[track_]));
            break;
        case 's':
            releaseLive();
            parameters_ = !parameters_;
            performance_ = false;
            break;
        case 'c':
            clipboard_ = current();
            clipboardFull_ = true;
            break;
        case 'v':
            if (clipboardFull_) {
                remember();
                replace(track_, pattern_, clipboard_);
            }
            break;
        case 'z':
            if (undoAvailable_) {
                if (soundUndo_) {
                    releaseLive();
                    for (uint8_t p = 0; p < 9; ++p) apply(cmd(CommandType::SetSynthParam, undoTrack_, p, 0, paramValue(undoSound_, p)));
                    soundUndo_ = false;
                } else if (commandUndo_) {
                    if (undoType_ == CommandType::SetKeyboard) releaseLive();
                    apply(cmd(undoType_, undoTrack_, undoParameter_, 0, undoValue_));
                }
                else
                    replace(undoTrack_, undoPattern_, undo_);
                undoAvailable_ = false;
            }
            break;
        default:
            break;
    }
}
void InstrumentUi::onKey(char code, bool pressed, bool fn, bool shift) {
    const auto keycode = static_cast<uint8_t>(code);
    if (keycode >= 128) return;
    auto& held = held_[keycode];
    if (!pressed) {
        if (held.active) {
            const int row = project_.keyboardLayout[held.track] ? stringRow(code) : -1;
            const bool sounding = row >= 0 && stringKey(row) == keycode;
            held.active = false;
            if (row < 0) send(cmd(CommandType::LiveNoteOff, held.track, held.pitch, keycode));
            else if (sounding) {
                const int previous = stringKey(row);
                if (previous >= 0) {
                    cursorPitch_ = held_[previous].pitch;
                    send(cmd(CommandType::LiveNoteOn, held.track, held_[previous].pitch, 120 + row, 100));
                } else send(cmd(CommandType::LiveNoteOff, held.track, held.pitch, 120 + row));
            }
            dirty_ = true;
        }
        return;
    }
    dirty_ = true;
    if (fn) {
        fnPress(code, shift);
        return;
    }
    if (code == '[' || code == ']') {
        selectTarget(track_, pattern_ + (code == ']' ? 1 : -1));
        return;
    }
    if (code == '-' || code == '=') {
        octave_ = std::max(-1, std::min(8, octave_ + (code == '=' ? 1 : -1)));
        return;
    }
    if (code == key::Enter) {
        enter();
        return;
    }
    if (code == key::Backspace) {
        if (!locked_ && selected_ >= 0) {
            remember();
            apply(cmd(CommandType::DeleteNote, track_, pattern_, selected_));
            selected_ = -1;
        }
        return;
    }
    const bool strings = project_.keyboardLayout[track_] != 0;
    const int offset = strings ? stringRow(code) : piano(code);
    const int pitch = strings ? stringPitch(code, octave_, project_.keyboardRoot[track_], project_.keyboardScale[track_]) : (octave_ + 1) * 12 + offset;
    if (offset < 0 || pitch < 0 || pitch > 127 || parameters_ || held.active) return;
    held = {true, track_, static_cast<uint8_t>(pitch),
            static_cast<uint16_t>(tick_ % patternTicks(current().length, current().grid))};
    cursorPitch_ = pitch;
    cursorTick_ = held.start;
    selected_ = -1;
    keyOrder_[keycode] = ++heldOrder_;
    send(cmd(CommandType::LiveNoteOn, track_, held.pitch, strings ? 120 + stringRow(code) : keycode, shift ? 127 : 100));
}
void InstrumentUi::draw(Canvas& c, uint32_t tick, bool playing, bool storageAvailable, const char* warning) const {
    c.fillRect(0, 0, 240, 135, color::Background);
    const auto& p = current();
    char line[64];
    snprintf(line, sizeof(line), "T%u P%02u %s x%u %s", track_ + 1, pattern_ + 1, gridName(p.grid), p.length,
             recording_ ? "REC"
             : armed_   ? "ARM"
             : playing  ? "PLAY"
                        : "STOP");
    c.text(0, 2, line, color::Text);
    if (performance_) { drawPerformance(c); return; }
    if (parameters_) {
        c.text(3, 25, "SOUND / Fn+arrows select & adjust", color::Text);
        for (uint8_t p = 0; p < 9; ++p) {
            const int x = 3 + (p % 3) * 79, y = 38 + (p / 3) * 23;
            const bool active = p == parameter_;
            c.fillRect(x, y, 76, 21, active ? color::LaneSelected : color::StepOutside);
            c.text(x + 2, y + 2, kParamLabels[p], active ? color::StepAccent : color::Text);
            const auto value = paramValue(project_.synths[track_], p);
            if (p == 0) snprintf(line, sizeof(line), "%s", kWaves[value]);
            else snprintf(line, sizeof(line), "%u", value);
            c.text(x + 2, y + 11, line, color::Text);
            if (p) { c.fillRect(x + 25, y + 13, 48, 4, color::StepOff); if (value) c.fillRect(x + 25, y + 13, std::max(1, value * 48 / 127), 4, color::StepOn); }
        }
        c.text(3, 112, "Fn+S roll   Opt: presets / performance", color::Text);
    } else {
        const int total = patternTicks(p.length, p.grid);
        int low = cursorPitch_, high = cursorPitch_;
        if (fitNotes_) {
            for (uint8_t i = 0; i < p.noteCount; ++i)
                if (p.notes[i].startTick < total) {
                    low = std::min(low, int(p.notes[i].pitch));
                    high = std::max(high, int(p.notes[i].pitch));
                }
            for (const auto& h : held_)
                if (h.active && h.track == track_) {
                    low = std::min(low, int(h.pitch));
                    high = std::max(high, int(h.pitch));
                }
        }
        const int pitchStep = fitNotes_ ? std::max(1, (high - low + 12) / 12) : 1, span = pitchStep * 12;
        const int left = fitNotes_ ? std::max(0, std::min(std::max(0, 128 - span), low - (span - (high - low + 1)) / 2))
                                   : bounded(cursorPitch_ - 5, 0, 116);
        const auto rowFor = [&](int pitch) { return (left + span - 1 - pitch) / pitchStep; };
        constexpr int x0 = 24, width = 216, y0 = 25, row = 7;
        for (int i = 0; i < 12; ++i) {
            const int pitch = left + (11 - i) * pitchStep, y = y0 + i * row;
            const int pc = pitch % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            c.fillRect(x0, y, width, row - 1, black ? color::StepOutside : color::StepOff);
            const int cNote = pitch + (12 - pc) % 12;
            if (cNote < pitch + pitchStep && cNote <= 127) {
                snprintf(line, sizeof(line), "C%d", cNote / 12 - 1);
                c.text(0, y, line, color::Text);
            }
        }
        for (int step = 0; step <= p.length; ++step)
            c.fillRect(x0 + std::min(width - 1, step * width / p.length), y0, 1, 84, color::StepOutside);
        bool hidden = false;
        for (int i = 0; i < p.noteCount; ++i) {
            const auto& n = p.notes[i];
            if (n.startTick >= total) {
                hidden = true;
                continue;
            }
            if (n.pitch < left || n.pitch >= left + span) continue;
            const int y = y0 + rowFor(n.pitch) * row;
            const int len = std::min<int>(n.lengthTicks, total), end = n.startTick + len;
            const auto color = i == selected_ ? color::StepAccent : color::StepOn;
            const int x = x0 + n.startTick * width / total;
            c.fillRect(x, y, std::max(1, (std::min(end, total) * width / total) - (x - x0)), row - 1, color);
            if (end > total) c.fillRect(x0, y, std::max(1, (end - total) * width / total), row - 1, color);
        }
        for (const auto& held : held_)
            if (held.active && held.track == track_ && held.pitch >= left && held.pitch < left + span) {
                const int x = x0 + held.start * width / total;
                const int duration =
                    recording_ ? static_cast<int>((tick % total + total - held.start) % total) + 1 : gridTicks(p.grid);
                const int end = std::min(total, int(held.start) + duration);
                c.fillRect(x, y0 + rowFor(held.pitch) * row, std::max(1, end * width / total - (x - x0)), row - 1,
                           color::Warning);
            }
        if (playing) c.fillRect(x0 + static_cast<int>(tick % total) * width / total, y0, 1, 84, color::Playhead);
        c.fillRect(x0 + cursorTick_ * width / total, y0 + rowFor(cursorPitch_) * row, 2, row - 1, color::Text);
        const Note n = selected_ >= 0 ? p.notes[selected_]
                                      : Note{cursorTick_, gridTicks(p.grid), static_cast<uint8_t>(cursorPitch_), 100};
        snprintf(line, sizeof(line), "N%u t%u L%u V%u %s", n.pitch, n.startTick, n.lengthTicks, n.velocity,
                 hidden ? "HIDDEN" : "");
        c.text(0, 112, line, color::Text);
    }
    snprintf(line, sizeof(line), "%u/64 OCT%d Q:%s SW%u %s", p.noteCount, octave_,
             project_.recordQuantize[track_] ? "GRID" : "OFF", project_.swing, fitNotes_ ? "FIT" : "1x");
    c.text(0, 124, line, color::Text);
    c.text(180, 14, "Fn+BS CLR", color::Text);
    const char* notice = warning ? warning : full_ ? "NOTE FULL" : storageAvailable ? nullptr : "NO SD";
    if (notice)
        c.text(0, 14, notice, color::Warning);
    else if (project_.arps[track_].mode != ArpMode::Off) {
        const auto& a = project_.arps[track_];
        constexpr const char* names[] = {"OFF", "UP", "DOWN", "UPDN"};
        snprintf(line, sizeof(line), "ARP %s %s x%u G%u", names[static_cast<uint8_t>(a.mode)], gridName(a.rate),
                 a.octaves, a.gate);
        c.text(0, 14, line, color::StepAccent);
    }
}
void InstrumentUi::drawFnSettings(Canvas& c) const {
    char line[64];
    snprintf(line, sizeof(line), "T%u P%02u OCT%d x%u %s Q:%s SW%u", track_ + 1, pattern_ + 1, octave_,
             current().length, gridName(current().grid), project_.recordQuantize[track_] ? "ON" : "OFF", project_.swing);
    c.text(3, 94, line, color::StepAccent);
    const auto& a = project_.arps[track_];
    constexpr const char* modes[] = {"OFF", "UP", "DOWN", "UPDN"};
    snprintf(line, sizeof(line), "ARP %s %s OCT%u GATE%u%%", modes[static_cast<uint8_t>(a.mode)], gridName(a.rate),
             a.octaves, a.gate);
    c.text(3, 104, line, color::StepAccent);
    if (locked_)
        snprintf(line, sizeof(line), "%s: edits locked; Alt ends", recording_ ? "RECORDING" : "ARMED");
    else if (parameters_)
        snprintf(line, sizeof(line), "%s: %u  WAVE %s", kParamLabels[parameter_],
                 paramValue(project_.synths[track_], parameter_), kWaves[static_cast<uint8_t>(project_.synths[track_].wave)]);
    else
        snprintf(line, sizeof(line), "PIANO %s  NOTES %u/64", fitNotes_ ? "FIT" : "1x", current().noteCount);
    c.text(3, 114, line, color::StepAccent);
    c.text(3, 124, "OPT close   ENTER quick start", color::Text);
}

int InstrumentUi::stringKey(int row) const {
    int best = -1;
    for (int k = 0; k < 128; ++k)
        if (held_[k].active && held_[k].track == track_ && stringRow(static_cast<char>(k)) == row &&
            (best < 0 || keyOrder_[k] > keyOrder_[best])) best = k;
    return best;
}
void InstrumentUi::setKeyboard(uint8_t parameter, uint8_t value) {
    if (locked_) return;
    releaseLive();
    const uint8_t old[] = {project_.keyboardLayout[track_], project_.keyboardRoot[track_], project_.keyboardScale[track_]};
    if (parameter > 2) return;
    rememberCommand(CommandType::SetKeyboard, parameter, old[parameter]);
    apply(cmd(CommandType::SetKeyboard, track_, parameter, 0, value));
}
void InstrumentUi::showPerformance() {
    if (locked_) return;
    releaseLive();
    if (!project_.keyboardLayout[track_]) {
        setKeyboard(0, 1); setKeyboard(2, 2);
        if (!project_.synths[track_].glide) loadPreset(0);
    }
    parameters_ = false; performance_ = true; dirty_ = true;
}
const char* InstrumentUi::presetName(uint8_t index) {
    constexpr const char* names[] = {"Warm slide", "Round bass", "Soft keys", "Slow pad", "Acid lead", "Pure sine"};
    return names[index % 6];
}
void InstrumentUi::loadPreset(uint8_t index) {
    if (locked_ || index >= 6) return;
    constexpr SynthSound sounds[] = {
        {Waveform::Saw, 6, 32, 92, 38, 68, 18, 45, 76},
        {Waveform::Square, 2, 45, 72, 22, 38, 28, 35, 80},
        {Waveform::Triangle, 3, 60, 35, 45, 94, 4, 20, 90},
        {Waveform::Saw, 65, 52, 100, 65, 52, 12, 60, 72},
        {Waveform::Saw, 1, 34, 88, 25, 65, 95, 40, 60},
        {Waveform::Sine, 5, 30, 100, 38, 127, 0, 50, 95}
    };
    releaseLive();
    undoSound_ = project_.synths[track_]; undoTrack_ = track_;
    for (uint8_t p = 0; p < 9; ++p) apply(cmd(CommandType::SetSynthParam, track_, p, 0, paramValue(sounds[index], p)));
    soundUndo_ = undoAvailable_ = true; commandUndo_ = false; preset_ = index;
}
void InstrumentUi::drawPerformance(Canvas& c) const {
    char line[64];
    constexpr const char* roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    constexpr const char* scales[] = {"CHROM", "MAJOR", "MINOR", "PENTA"};
    constexpr const char* rows[] = {"zxcvbnm,./", "asdfghjkl;", "qwertyuiop", "1234567890"};
    const auto& sound = project_.synths[track_];
    snprintf(line, sizeof(line), "%s %s  %s  GLIDE %u", project_.keyboardLayout[track_] ? "STR" : "KEY",
        roots[project_.keyboardRoot[track_]], scales[project_.keyboardScale[track_]], sound.glide);
    c.text(3, 17, line, color::StepAccent);
    for (int row = 0; row < 4; ++row) {
        const int y = 33 + (3 - row) * 18;
        c.fillRect(4, y + 13, 230, 1, color::StepOff);
        for (int col = 0; col < 10; ++col) {
            const char key = rows[row][col];
            const bool held = held_[static_cast<unsigned>(key)].active;
            const bool sounding = held && (!project_.keyboardLayout[track_] || stringKey(row) == key);
            c.fillRect(4 + col * 23, y, 21, 12, sounding ? color::StepOn : held ? color::LaneSelected : color::StepOutside);
            char label[2] = {key, 0}; c.text(11 + col * 23, y + 2, label, sounding ? color::Background : color::Text);
        }
    }
    snprintf(line, sizeof(line), "NOTE %s%d  OCT%d  %s", roots[cursorPitch_ % 12], cursorPitch_ / 12 - 1, octave_, recording_ ? "RECORDING" : "OPT: sound / view");
    c.text(3, 109, line, color::Text);
    c.text(3, 124, "Alt REC  Space PLAY  Fn+- OCT  Opt MENU", color::StepAccent);
}
}  // namespace gb
