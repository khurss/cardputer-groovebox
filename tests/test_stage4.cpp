#include <algorithm>
#include <cstdlib>
#include <string>

#include "gb/engine.h"
#include "gb/project_io.h"
#include "gb/ui.h"
#include "gb/validate.h"
#include "test.h"
using namespace gb;
namespace {
Project& project() {
    static Project p;
    p = Project{};
    initProject(p);
    return p;
}
Command cmd(CommandType t, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command out;
    out.type = t;
    out.a = a;
    out.b = b;
    out.c = c;
    out.value = value;
    return out;
}
int render(Engine& e, int frames) {
    int peak = 0;
    int16_t data[512];
    while (frames > 0) {
        const int n = std::min(frames, 256);
        e.render(data, n);
        for (int i = 0; i < n * 2; ++i) peak = std::max(peak, std::abs(int(data[i])));
        frames -= n;
    }
    return peak;
}
struct Keys {
    Engine e;
    Project& ui;
    AppUi app;
    static Project& mirror() {
        static Project p;
        p = Project{};
        initProject(p);
        return p;
    }
    static bool sink(void* context, const Command& c) {
        static_cast<Keys*>(context)->e.handle(c);
        return true;
    }
    Keys() : e(project(), 32000), ui(mirror()), app(ui, sink, this) {}
    void sync() {
        RecordUpdate u;
        while (e.popRecordUpdate(u)) app.receive(u);
        app.setTransport(e.transport());
        app.setPosition(e.sceneTick());
        app.setPlayback(e.playing());
    }
    void key(char code, bool fn = false, bool shift = false) {
        if (fn) app.onKey(key::Fn, true);
        if (shift) app.onKey(key::Shift, true);
        app.onKey(code, true);
        app.onKey(code, false);
        if (shift) app.onKey(key::Shift, false);
        if (fn) app.onKey(key::Fn, false);
        sync();
    }
};
}  // namespace
TEST(newFormatKeepsV2SynthAndAddsDefaultMixerAndArp) {
    auto& p = project();
    static uint8_t data[kProjectBufferSize];
    CHECK_EQ(loadProjectFile("tests/fixtures/stage3-v2.gbp", p, data, sizeof(data)), LoadResult::Ok);
    CHECK_EQ(p.synths[0].wave, Waveform::Square);
    CHECK_EQ(p.recordQuantize[1], false);
    CHECK_EQ(p.masterVolume, 100);
    CHECK_EQ(p.arps[3].mode, ArpMode::Off);
    p.masterVolume = 27;
    p.mixer[2] = {42, true, true};
    p.arps[1] = {ArpMode::UpDown, Grid::EighthTriplet, 3, 75};
    const auto n = serializeProject(p, data, sizeof(data));
    p = Project{};
    CHECK_EQ(deserializeProject(data, n, p), LoadResult::Ok);
    CHECK_EQ(data[4], 5);
    CHECK_EQ(p.masterVolume, 27);
    CHECK_EQ(p.mixer[2].volume, 42);
    CHECK_EQ(p.mixer[2].solo, true);
    CHECK_EQ(p.arps[1].gate, 75);
    CHECK_EQ(p.arps[1].octaves, 3);
}
TEST(songEditsHaveBoundedIndicesAndShiftEntries) {
    auto& p = project();
    applyEdit(p, cmd(CommandType::InsertSongEntry, 0, 3, 0, 2));
    applyEdit(p, cmd(CommandType::InsertSongEntry, 0, 1, 0, 4));
    CHECK_EQ(p.songLength, 2);
    CHECK_EQ(p.song[1].scene, 3);
    applyEdit(p, cmd(CommandType::DeleteSongEntry, 0));
    CHECK_EQ(p.song[0].scene, 3);
    for (int i = 0; i < 70; ++i) applyEdit(p, cmd(CommandType::InsertSongEntry, 0, 2, 0, 1));
    CHECK_EQ(p.songLength, 64);
    applyEdit(p, cmd(CommandType::SetSongEntry, 63, 15, 0, 255));
    CHECK_EQ(p.song[63].repeats, 255);
    CHECK_EQ(isValidProject(p), true);
    p.masterVolume = 101;
    CHECK_EQ(isValidProject(p), false);
    p.masterVolume = 100;
    p.arps[0].octaves = 0;
    CHECK_EQ(isValidProject(p), false);
}
TEST(masterCeilingAndStripLevelsActuallyAffectAudio) {
    auto& p = project();
    Engine e(p, 32000);
    e.handle(cmd(CommandType::SetOutputLimit, 0, 0, 0, 5));
    for (int lane = 0; lane < 8; ++lane) e.handle(cmd(CommandType::Preview, lane, 127));
    CHECK_EQ(render(e, 8192) <= 1557, true);
    e.handle(cmd(CommandType::SetTrackVolume, 0, 0, 0, 0));
    CHECK_EQ(render(e, 1024), 0);
    e.handle(cmd(CommandType::SetTrackVolume, 0, 0, 0, 100));
    e.handle(cmd(CommandType::LiveNoteOn, 0, 60, 'z', 127));
    CHECK_EQ(render(e, 1024) > 0, true);
    e.handle(cmd(CommandType::SetTrackMute, 1, 0, 0, 1));
    CHECK_EQ(render(e, 1024) > 0, true);  // drums remain
    e.handle(cmd(CommandType::SetMasterVolume, 0, 0, 0, 0));
    CHECK_EQ(render(e, 1024), 0);
}
TEST(synthStripMuteAndSoloKeepOtherTracksIndependent) {
    auto& p = project();
    Engine e(p, 32000);
    e.handle(cmd(CommandType::SetTrackMute, 1, 0, 0, 1));
    e.handle(cmd(CommandType::LiveNoteOn, 0, 60, 'z', 100));
    CHECK_EQ(render(e, 1024), 0);
    e.handle(cmd(CommandType::LiveNoteOn, 1, 64, 'x', 100));
    CHECK_EQ(render(e, 1024) > 0, true);
    e.handle(cmd(CommandType::SetTrackSolo, 1, 0, 0, 1));
    CHECK_EQ(render(e, 1024), 0);
    e.handle(cmd(CommandType::SetTrackMute, 1, 0, 0, 0));
    CHECK_EQ(render(e, 1024) > 0, true);
}
TEST(clearShortcutPreservesGridAndIsUndoableOnBothScreens) {
    Keys s;
    s.key('q');
    s.key('g', true);
    const auto grid = s.ui.drums[0].grid;
    s.key(key::Backspace, true);
    CHECK_EQ(s.ui.drums[0].velocity[0][0], 0);
    CHECK_EQ(s.ui.drums[0].grid, grid);
    s.key('z', true);
    CHECK_EQ(s.ui.drums[0].velocity[0][0], 100);
    s.key('2', true);
    s.key(key::Enter);
    s.key(key::Backspace, true);
    CHECK_EQ(s.ui.instruments[0][0].noteCount, 0);
    s.key('z', true);
    CHECK_EQ(s.ui.instruments[0][0].noteCount, 1);
}
TEST(liveNotesFocusImmediatelyAndClearDuringRecordingDrainsResults) {
    Keys s;
    s.key('2', true);
    s.key('=', true);
    s.key('=', true);
    s.app.onKey('z', true);
    CHECK_EQ(s.app.instrument().focusPitch(), 84);
    s.app.onKey('z', false);
    s.key(key::Alt);
    s.key(' ');
    render(s.e, 256);
    s.sync();
    s.app.onKey('z', true);
    render(s.e, 8000);
    s.sync();
    s.key(key::Backspace, true);
    s.sync();
    CHECK_EQ(s.app.busy(), false);
    CHECK_EQ(s.ui.instruments[0][0].noteCount, 0);
    s.app.onKey('z', false);
    s.key('z', true);
    CHECK_EQ(s.ui.instruments[0][0].noteCount, 1);
    CHECK_EQ(s.app.instrument().focusPitch(), 84);
}
TEST(scenesAndSongUIAssembleTrackWithoutChangingSceneZero) {
    Keys s;
    s.key('3', true);
    s.key(']');
    s.key('=', true);
    CHECK_EQ(s.ui.scenes[1].pattern[0], 0);
    CHECK_EQ(s.ui.scenes[0].pattern[0], 0);
    s.key('=', true);
    CHECK_EQ(s.ui.scenes[1].pattern[0], 1);
    CHECK_EQ(s.ui.scenes[0].pattern[0], 0);
    s.key('1', true);
    s.key(']');
    CHECK_EQ(s.ui.scenes[1].pattern[0], 2);
    CHECK_EQ(s.ui.scenes[0].pattern[0], 0);
    s.key('4', true);
    s.key(key::Enter, true);
    CHECK_EQ(s.ui.songLength, 1);
    CHECK_EQ(s.ui.song[0].scene, 1);
    s.key('/', true);
    s.key('=', true);
    CHECK_EQ(s.ui.song[0].repeats, 2);
    s.key(key::Backspace);
    CHECK_EQ(s.ui.songLength, 0);
    s.key('z', true);
    CHECK_EQ(s.ui.songLength, 1);
    s.key('5', true);
    s.key('-');
    CHECK_EQ(s.ui.masterVolume, 95);
    s.key('/', true);
    s.key('m');
    CHECK_EQ(s.ui.mixer[0].mute, true);
}
TEST(sceneLaunchUsesTransportBarRatherThanSongEntryOrigin) {
    auto& p = project();
    p.drums[0].length = 4;
    p.scenes[1].pattern[0] = 0;
    p.songLength = 2;
    p.song[0] = {0, 1};
    p.song[1] = {1, 20};
    Sequencer seq(p, 32000);
    seq.playSong(0);
    Event events[32];
    for (int i = 0; i < 80; ++i) seq.render(256, events, 32);  // tick 123, second entry began at 96
    seq.playScene(2);
    for (int i = 0; i < 171; ++i) seq.render(256, events, 32);
    CHECK_EQ(seq.sceneIndex(), 2);  // global tick 384, not entry-relative tick 480
}
TEST(songLengthEditRecalculatesEndAndNaturalStopCanRestart) {
    auto& p = project();
    p.songLength = 1;
    p.song[0] = {0, 1};
    p.drums[0].length = 16;
    Sequencer seq(p, 32000);
    seq.playSong(0);
    Event events[32];
    seq.render(256, events, 32);
    p.drums[0].length = 4;
    for (int i = 0; i < 64; ++i) seq.render(256, events, 32);
    CHECK_EQ(seq.playing(), false);
    Keys s;
    s.key('4', true);
    s.key(key::Enter, true);
    s.key(' ');
    render(s.e, 65000);
    s.sync();
    CHECK_EQ(s.e.playing(), false);
    s.key(' ');
    render(s.e, 256);
    s.sync();
    CHECK_EQ(s.e.playing(), true);
}

TEST(allArrangementPagesAndHeldNotesStayInsideScreen) {
    class Screen : public Canvas {
    public:
        bool valid = true, held = false;
        void fillRect(int x, int y, int w, int h, uint16_t color) override {
            valid = valid && x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 240 && y + h <= 135;
            held = held || color == color::Warning;
        }
        void text(int x, int y, const char*, uint16_t) override {
            valid = valid && x >= 0 && x < 240 && y >= 0 && y < 135;
        }
    };
    Keys s;
    for (int page = 2; page <= 4; ++page) {
        s.key('1' + page, true);
        for (int i = 0; i < 70; ++i) s.key(page == 2 ? ']' : '.', page != 2);
        Screen canvas;
        s.app.draw(canvas, 0, false, true);
        CHECK_EQ(canvas.valid, true);
    }
    s.key('2', true);
    s.key('=', true);
    s.app.onKey('z', true);
    Screen canvas;
    s.app.draw(canvas, 0, false, true);
    CHECK_EQ(canvas.held, true);
    CHECK_EQ(canvas.valid, true);
}

TEST(pianoRollFitsWidelySeparatedNotesWithoutScrolling) {
    class Notes : public Canvas {
    public:
        int visible = 0;
        void fillRect(int, int, int, int, uint16_t paint) override { visible += paint == color::StepOn; }
        void text(int, int, const char*, uint16_t) override {}
    };
    Keys s;
    s.key('2', true);
    auto& p = s.ui.instruments[0][0];
    p.noteCount = 2;
    p.notes[0] = {0, 24, 24, 100};
    p.notes[1] = {48, 24, 108, 100};
    Notes fit;
    s.app.draw(fit, 0, false, true);
    CHECK_EQ(fit.visible, 2);
    s.key('f', true);
    Notes precise;
    s.app.draw(precise, 0, false, true);
    CHECK_EQ(precise.visible, 0);
}

TEST(recordingSongBoundaryUsesUnwrappedTimeAndEndsPass) {
    auto& p = project();
    p.drums[0].length = 4;
    p.scenes[0].pattern[1] = 0;
    p.scenes[1].pattern[1] = 1;
    p.instruments[0][0].length = p.instruments[0][1].length = 4;
    p.songLength = 2;
    p.song[0] = {0, 1};
    p.song[1] = {1, 4};
    Engine e(p, 32000);
    e.handle(cmd(CommandType::PlaySong));
    render(e, 12000);
    e.handle(cmd(CommandType::SetRecording, 0, 0, 0, 1));
    e.handle(cmd(CommandType::LiveNoteOn, 0, 60, 'z', 100));
    render(e, 6000);
    CHECK_EQ(p.instruments[0][0].noteCount, 1);
    CHECK_EQ(p.instruments[0][1].noteCount, 0);
    CHECK_EQ(p.instruments[0][0].notes[0].lengthTicks >= 24 && p.instruments[0][0].notes[0].lengthTicks <= 26, true);
    RecordUpdate update;
    int ended = 0;
    while (e.popRecordUpdate(update)) ended += update.type == RecordUpdateType::Ended;
    CHECK_EQ(ended, 1);
}
TEST(recordTargetsActualPlayingSceneRatherThanSceneCursor) {
    Keys s;
    auto scene = cmd(CommandType::SetScenePattern, 2, 1, 3);
    s.e.handle(scene);
    applyEdit(s.ui, scene);
    s.e.handle(cmd(CommandType::PlayScene, 2));
    render(s.e, 256);
    s.sync();
    s.key('2', true);
    s.key(key::Alt);
    CHECK_EQ(s.app.instrument().pattern(), 3);
    s.app.onKey('z', true);
    render(s.e, 8192);
    s.app.onKey('z', false);
    s.sync();
    CHECK_EQ(s.ui.instruments[0][3].noteCount, 1);
    CHECK_EQ(s.ui.instruments[0][0].noteCount, 0);
}
