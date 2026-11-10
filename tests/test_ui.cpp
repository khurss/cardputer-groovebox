#include <cstring>
#include <string>
#include <vector>

#include "gb/engine.h"
#include "gb/ui.h"
#include "test.h"
using namespace gb;
namespace {
struct Session {
    static Project& audio() {
        static Project p;
        p = Project{};
        initProject(p);
        return p;
    }
    static Project& model() {
        static Project p;
        p = Project{};
        initProject(p);
        return p;
    }
    Project& p;
    Engine e;
    std::vector<Command> sent;
    AppUi ui;
    static bool send(void* context, const Command& c) {
        auto& s = *static_cast<Session*>(context);
        s.sent.push_back(c);
        s.e.handle(c);
        return true;
    }
    Session() : p(model()), e(audio(), 32000), ui(p, send, this) {}
    void sync() {
        RecordUpdate u;
        while (e.popRecordUpdate(u))
            ui.receive(u);
        ui.setPlayback(e.playing());
    }
    void run(int frames) {
        int16_t block[512];
        while (frames > 0) {
            auto n = frames > 256 ? 256 : frames;
            e.render(block, n);
            frames -= n;
            sync();
        }
    }
    void key(char code, bool fn = false, bool shift = false) {
        if (fn)
            ui.onKey(key::Fn, true);
        if (shift)
            ui.onKey(key::Shift, true);
        ui.onKey(code, true);
        ui.onKey(code, false);
        if (shift)
            ui.onKey(key::Shift, false);
        if (fn)
            ui.onKey(key::Fn, false);
        sync();
    }
};
class BoundsCanvas : public Canvas {
public:
    bool valid = true;
    bool hidden = false;
    void fillRect(int x, int y, int w, int h, uint16_t) override {
        valid = valid && x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 240 && y + h <= 135;
    }
    void text(int x, int y, const char* value, uint16_t) override {
        valid = valid && x >= 0 && y >= 0 && y < 135;
        hidden = hidden || strstr(value, "HIDDEN");
    }
};
}  // namespace
namespace {
class MenuCanvas : public Canvas {
public:
    std::string visible;
    bool valid = true;
    void fillRect(int x, int y, int w, int h, uint16_t) override {
        if (x == 0 && y == 0 && w == 240 && h == 135) {
            visible.clear();
            valid = true;
        }
        valid = valid && x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 240 && y + h <= 135;
    }
    void text(int x, int y, const char* value, uint16_t) override {
        valid = valid && x >= 0 && y >= 0 && x + std::strlen(value) * 6 <= 240 && y + 8 <= 135;
        visible += value;
        visible += '\n';
    }
    bool has(const char* value) const { return visible.find(value) != std::string::npos; }
};
}
TEST(fnNeverCoversTheWorkspaceAndNavigatesEveryScreen) {
    Session s;
    s.ui.takeDirty();
    CHECK_EQ(s.ui.takeDirty(), false);
    s.ui.onKey(key::Fn, true);
    CHECK_EQ(s.ui.takeDirty(), true);
    for (int i = 0; i < 6; ++i) {
        s.ui.onKey('1' + i, true);
        s.ui.onKey('1' + i, false);
        MenuCanvas c;
        s.ui.draw(c, 0, false, true);
        CHECK_EQ(c.valid, true);
        CHECK_EQ(c.has("FN HELP"), false);
        CHECK_EQ(s.ui.screen(), i);
    }
    s.ui.takeDirty();
    s.ui.onKey(key::Fn, false);
    CHECK_EQ(s.ui.takeDirty(), true);
    MenuCanvas c;
    s.ui.draw(c, 0, false, true);
    CHECK_EQ(c.has("FN HELP"), false);
}
TEST(globalVolumeWorksOnEveryScreenWithoutEditingPatternsOrOctave) {
    Session s;
    const auto bpm = s.p.bpm;
    for (int i = 0; i < 6; ++i) {
        s.key('1' + i, true);
        s.key('-');
        CHECK_EQ(s.p.masterVolume, 95);
        s.key('=', false, true); // Physical Shift+'=' also has a five percent step.
        CHECK_EQ(s.p.masterVolume, 100);
        s.key('-');
        s.key('+');
        CHECK_EQ(s.p.masterVolume, 100);
    }
    CHECK_EQ(s.p.bpm, bpm);
    CHECK_EQ(s.p.songLength, 0);
    CHECK_EQ(s.p.scenes[0].pattern[0], 0);
    CHECK_EQ(s.p.mixer[0].volume, 100);
    s.key('2', true);
    s.key('z');
    CHECK_EQ(s.sent.back().b, 60);
    s.key('=', true);
    s.key('z');
    CHECK_EQ(s.sent.back().b, 72);
    s.key('1', true);
    s.key('-', true);
    CHECK_EQ(s.p.bpm, bpm - 1);
    CHECK_EQ(s.p.masterVolume, 100);
    s.key('5', true);
    s.key('/', true);
    s.key('-', true);
    CHECK_EQ(s.p.mixer[0].volume, 95);
    s.key('-');
    CHECK_EQ(s.p.masterVolume, 95);
    CHECK_EQ(s.p.mixer[0].volume, 95);
}
TEST(volumeIsClampedFineAdjustableAndToastExpiresWhenStopped) {
    Session s;
    s.ui.setTime(100);
    s.ui.onKey(key::Ctrl, true);
    s.key('-');
    s.ui.onKey(key::Ctrl, false);
    CHECK_EQ(s.p.masterVolume, 99);
    for (int i = 0; i < 25; ++i) s.key('-');
    CHECK_EQ(s.p.masterVolume, 0);
    MenuCanvas c;
    s.ui.draw(c, 0, false, true);
    CHECK_EQ(c.has("MASTER 0%"), true);
    s.ui.takeDirty();
    s.ui.setTime(999);
    CHECK_EQ(s.ui.takeDirty(), false);
    s.ui.setTime(1000);
    CHECK_EQ(s.ui.takeDirty(), true);
    MenuCanvas expired;
    s.ui.draw(expired, 0, false, true);
    CHECK_EQ(expired.has("MASTER"), false);
    for (int i = 0; i < 25; ++i) s.key('=');
    CHECK_EQ(s.p.masterVolume, 100);
    // The output guard used for device tests remains independent of master keys.
    Command limit;
    limit.type = CommandType::SetOutputLimit;
    limit.value = 5;
    s.e.handle(limit);
    s.key('2', true);
    s.ui.onKey('z', true);
    int16_t block[512];
    s.e.render(block, 256);
    for (auto sample : block) CHECK_EQ(sample >= -1557 && sample <= 1557, true);
}
TEST(fnSynthSettingsShowEditedValuesWhileLiveNotesCanBeReleased) {
    Session s;
    s.key('2', true);
    s.ui.onKey('z', true);
    s.run(256);
    CHECK_EQ(s.e.activeVoices(), 1);
    s.ui.onKey(key::Fn, true);
    s.ui.onKey('z', false);
    s.run(32000);
    CHECK_EQ(s.e.activeVoices(), 0);
    s.ui.onKey('s', true);
    s.ui.onKey('s', false);
    s.ui.onKey(';', true);
    s.ui.onKey(';', false);
    MenuCanvas c;
    s.ui.draw(c, 0, false, true);
    CHECK_EQ(c.valid, true);
    CHECK_EQ(c.has("SQUARE"), true);
    s.ui.onKey('a', true);
    s.ui.onKey('a', false);
    MenuCanvas arp;
    s.ui.draw(arp, 0, false, true);
    CHECK_EQ(arp.has("ARP UP"), true);
    s.ui.onKey(key::Fn, false);
}
TEST(appOwnsTransportAcrossScreens) {
    Session s;
    s.key(' ');
    s.run(256);
    CHECK_EQ(s.e.playing(), true);
    s.key(key::Tab);
    CHECK_EQ(s.ui.screen(), 1);
    s.key(' ');
    s.run(256);
    CHECK_EQ(s.e.playing(), false);
    CHECK_EQ(s.ui.takeSaveRequest(), true);
    s.key('1', true);
    s.key(' ');
    s.run(256);
    CHECK_EQ(s.e.playing(), true);
}
TEST(pianoKeysAndReleaseSurviveOctaveAndModifierChanges) {
    Session s;
    s.key(key::Tab);
    s.ui.onKey('z', true);
    s.ui.onKey('c', true);
    s.ui.onKey('q', true);
    s.run(256);
    CHECK_EQ(s.e.activeVoices(), 3);
    s.key('=', true);
    s.ui.onKey(key::Fn, true);
    s.ui.onKey('z', false);
    s.ui.onKey('c', false);
    s.ui.onKey('q', false);
    s.ui.onKey(key::Fn, false);
    s.run(32000);
    CHECK_EQ(s.e.activeVoices(), 0);
    CHECK_EQ(s.sent[s.sent.size() - 1].type, CommandType::LiveNoteOff);
    CHECK_EQ(s.sent[s.sent.size() - 1].b, 72);
    s.ui.onKey('z', true);
    s.ui.onKey('z', true);
    s.run(256);
    CHECK_EQ(s.e.activeVoices(), 1);
}
TEST(recordedChangesReachUIBeforeSaveAndUndoRestoresPass) {
    Session s;
    s.key(key::Tab);
    s.key(key::Alt);
    CHECK_EQ(s.ui.busy(), true);
    s.key(' ');
    s.run(256);
    s.ui.onKey('z', true);
    s.ui.onKey('c', true);
    s.run(8000);
    s.ui.onKey('z', false);
    s.key(' ');
    CHECK_EQ(s.ui.takeSaveRequest(), false);
    s.run(256);
    s.sync();
    CHECK_EQ(s.p.instruments[0][0].noteCount, 2);
    CHECK_EQ(s.ui.busy(), false);
    CHECK_EQ(s.ui.takeSaveRequest(), true);
    CHECK_EQ(s.e.playing(), false);
    s.key('z', true);
    CHECK_EQ(s.p.instruments[0][0].noteCount, 0);
}
TEST(pianoRollEditsPasteAndUndoStayMirrored) {
    Session s;
    s.key(key::Tab);
    s.key(key::Enter);
    auto& p = s.p.instruments[0][0];
    CHECK_EQ(p.noteCount, 1);
    CHECK_EQ(p.notes[0].pitch, 60);
    s.key(';', true);
    s.key('/', true, true);
    s.key(';', true, true);
    CHECK_EQ(p.notes[0].pitch, 61);
    CHECK_EQ(p.notes[0].lengthTicks, 48);
    CHECK_EQ(p.notes[0].velocity, 108);
    s.key('c', true);
    s.key(']');
    s.key('v', true);
    CHECK_EQ(s.p.instruments[0][1].notes[0].pitch, 61);
    s.key('z', true);
    CHECK_EQ(s.p.instruments[0][1].noteCount, 0);
    s.key('[');
    s.key(key::Enter);
    s.key(key::Backspace);
    CHECK_EQ(p.noteCount, 0);
    s.key('z', true);
    CHECK_EQ(p.noteCount, 1);
}
TEST(recordingEndsBeforeScreenAndTargetChanges) {
    Session s;
    s.key(key::Tab);
    s.key(key::Alt);
    s.key(' ');
    s.run(256);
    s.ui.onKey('z', true);
    s.run(8000);
    s.key(key::Tab);
    s.run(256);
    CHECK_EQ(s.ui.screen(), 2);
    CHECK_EQ(s.ui.busy(), false);
    CHECK_EQ(s.p.instruments[0][0].noteCount, 1);
    s.ui.onKey('z', false);
    s.run(32000);
    CHECK_EQ(s.e.activeVoices() <= 1, true);
}
TEST(pianoRollFitsScreenAndShowsHiddenNotes) {
    Session s;
    s.key(key::Tab);
    auto& p = s.p.instruments[0][0];
    p.noteCount = 3;
    p.notes[0] = {370, 60, 60, 100};
    p.notes[1] = {800, 24, 61, 100};
    p.notes[2] = {0, 1536, 65, 100};
    BoundsCanvas c;
    s.ui.draw(c, 383, true, true);
    CHECK_EQ(c.valid, true);
    CHECK_EQ(c.hidden, true);
    s.key('s', true);
    BoundsCanvas params;
    s.ui.draw(params, 0, false, false);
    CHECK_EQ(params.valid, true);
}
TEST(stepEnteredInstrumentIsAssignedAndAudible) {
    Session s;
    s.key(key::Tab);
    s.key(key::Enter);
    s.key(' ');
    s.run(2048);
    CHECK_EQ(s.p.scenes[0].pattern[1], 0);
    CHECK_EQ(s.e.activeVoices(), 1);
}

TEST(parameterUndoDoesNotUndoPreviousNoteEdits) {
    Session s;
    s.key(key::Tab);
    s.key(key::Enter);
    s.key('q', true);
    CHECK_EQ(s.p.recordQuantize[0], false);
    s.key('z', true);
    CHECK_EQ(s.p.recordQuantize[0], true);
    CHECK_EQ(s.p.instruments[0][0].noteCount, 1);
    s.key('s', true);
    s.key(';', true);
    CHECK_EQ(s.p.synths[0].wave, Waveform::Square);
    s.key('z', true);
    CHECK_EQ(s.p.synths[0].wave, Waveform::Saw);
    CHECK_EQ(s.p.instruments[0][0].noteCount, 1);
}

TEST(liveKeyboardReachesBothMidiLimits) {
    Session s;
    s.key(key::Tab);
    for(int i=0;i<12;++i) s.key('-', true);
    s.key('z');
    CHECK_EQ(s.sent.back().type,CommandType::LiveNoteOff);
    CHECK_EQ(s.sent.back().b,0);
    for(int i=0;i<16;++i) s.key('=', true);
    s.key('t');
    CHECK_EQ(s.sent.back().b,127);
    const auto count=s.sent.size();
    s.key('i');
    CHECK_EQ(s.sent.size(),count);
}
TEST(pastingShorterPatternKeepsCursorOnScreen) {
    Session s;
    s.key(key::Tab);
    s.key('l',true); // four steps
    s.key('c',true);
    s.key('l',true);
    s.key('l',true); // sixteen steps
    for(int i=0;i<20;++i) s.key('/',true);
    s.key('v',true);
    BoundsCanvas c;
    s.ui.draw(c,0,false,true);
    CHECK_EQ(c.valid,true);
    s.key(key::Enter);
    const auto& p=s.p.instruments[0][0];
    CHECK_EQ(p.noteCount,1);
    CHECK_EQ(p.notes[0].startTick<patternTicks(p.length,p.grid),true);
}

TEST(fnIsOnlyAModifierAndOptOpensTheMenu) {
    Session s;
    s.ui.setTime(100);s.ui.onKey(key::Fn,true);s.ui.setTime(200);s.ui.onKey(key::Fn,false);
    CHECK_EQ(s.ui.menuOpen(),false);
    s.ui.setTime(1000);CHECK_EQ(s.ui.menuOpen(),false);
    s.key(key::Opt);CHECK_EQ(s.ui.menuOpen(),true);
    s.key(key::Opt);CHECK_EQ(s.ui.menuOpen(),false);
    s.ui.onKey(key::Fn,true);s.ui.setTime(1500);s.ui.onKey(key::Fn,false);
    CHECK_EQ(s.ui.menuOpen(),false);
    s.key('l',true);CHECK_EQ(s.ui.menuOpen(),false);CHECK_EQ(s.p.drums[0].length,4);
}
TEST(menuNavigationAcceptsPhysicalFnArrowsWithoutClosingOrPlayingNotes) {
    Session s;s.key(key::Opt);
    s.key('.',true);CHECK_EQ(s.ui.menuOpen(),true);CHECK_EQ(s.ui.menuRow(),1);
    s.key(key::Enter,true);CHECK_EQ(s.ui.screen(),1);CHECK_EQ(s.ui.menuOpen(),false);
    CHECK_EQ(s.e.activeVoices(),0);
    s.key(key::Opt);s.key(key::Tab);CHECK_EQ(s.ui.menuPage(),1);
    s.key(key::Tab);CHECK_EQ(s.ui.menuPage(),2);
    s.key('.',true);s.key(key::Enter);CHECK_EQ(s.ui.menuEditing(),true);
    s.key('/',true);CHECK_EQ(s.p.bpm,121);
    s.key('/',true,true);CHECK_EQ(s.p.bpm,131);
    s.key(key::Backspace);CHECK_EQ(s.ui.menuEditing(),false);CHECK_EQ(s.ui.menuOpen(),true);
    s.key(key::Backspace);CHECK_EQ(s.ui.menuOpen(),false);
    CHECK_EQ(s.p.instruments[0][0].noteCount,0);
}
TEST(menuAlwaysReleasesHeldNotesAndSuppressesNewPianoKeys) {
    Session s;s.key('2',true);s.ui.onKey('z',true);s.ui.onKey('c',true);s.run(256);
    CHECK_EQ(s.e.activeVoices(),2);s.key(key::Opt);s.key('q');s.key('m');
    s.run(256);CHECK_EQ(s.e.activeVoices(),2);
    s.ui.onKey('z',false);s.ui.onKey('c',false);s.run(32000);
    CHECK_EQ(s.e.activeVoices(),0);CHECK_EQ(s.ui.menuOpen(),true);
}
TEST(menuGridChangesRetainARealUndoSnapshot) {
    Session s;s.key('w');CHECK_EQ(s.p.drums[0].velocity[0][1],100);
    s.key(key::Opt);s.key(key::Tab);s.key(key::Tab);
    for(int i=0;i<4;++i)s.key('.');
    s.key(key::Enter);s.key(',',true); // 1/16 -> 1/8: the off-grid step is removed.
    CHECK_EQ(s.p.drums[0].grid,Grid::Eighth);CHECK_EQ(s.p.drums[0].velocity[0][1],0);
    s.key(key::Backspace);s.key(key::Backspace);s.key('z',true);
    CHECK_EQ(s.p.drums[0].grid,Grid::Sixteenth);CHECK_EQ(s.p.drums[0].velocity[0][1],100);
}
TEST(menuSettingsOnSamplerWorkFromFileBrowserWithoutTriggeringAudio) {
    Session s;s.key('6',true);SampleJob job;CHECK_EQ(s.ui.sampler().takeJob(job),true);
    SampleResult result;result.job=job;result.ok=true;s.ui.sampler().receive(result);
    std::strcpy(s.p.samples[2].source,"break.wav");s.p.samples[2].end=32000;
    s.key(key::Opt);s.key(key::Tab);s.key(key::Tab);
    for(int i=0;i<3;++i)s.key('.');
    s.key(key::Enter);s.key('/',true);s.key(key::Enter);CHECK_EQ(s.ui.sampler().pad(),1);
    s.key('.');s.key(key::Enter);s.key('/',true);CHECK_EQ(s.ui.sampler().slot(),2);
    MenuCanvas canvas;s.ui.draw(canvas,0,false,true);
    CHECK_EQ(canvas.valid,true);CHECK_EQ(canvas.has("break.wav"),true);CHECK_EQ(s.e.sampleVoices(),0u);
    CHECK_EQ(s.ui.sampler().takeJob(job),false);
}
TEST(menuDeletionHasCancelAndNeverFiresFromTheFirstEnter) {
    Session s;s.key('6',true);SampleJob job;s.ui.sampler().takeJob(job);
    SampleResult result;result.job=job;result.ok=true;s.ui.sampler().receive(result);
    std::strcpy(s.p.samples[0].source,"keep.wav");s.p.samples[0].end=32000;
    s.key(key::Opt);s.key(key::Tab);
    for(int i=0;i<12;++i)s.key('.');
    s.key(key::Enter);CHECK_EQ(s.ui.menuConfirming(),true);CHECK_EQ(s.ui.sampler().takeJob(job),false);
    MenuCanvas confirm;s.ui.draw(confirm,0,false,true);CHECK_EQ(confirm.valid,true);CHECK_EQ(confirm.has("keep.wav"),true);
    s.key(key::Backspace);CHECK_EQ(s.ui.menuConfirming(),false);CHECK_EQ(s.p.samples[0].end,32000u);
    s.key(key::Enter);s.key(key::Enter);CHECK_EQ(s.ui.sampler().takeJob(job),true);
    CHECK_EQ(job.type,SampleJobType::Remove);CHECK_EQ(s.p.samples[0].end,32000u);
}
TEST(menuSettingsAndAllContextListsFitDisplayAndResetOnScreenChanges) {
    Session s;
    for(int screen=0;screen<6;++screen) {
        s.key('1'+screen,true);s.key(key::Opt);
        for(int page=0;page<3;++page) {
            // Visit every row, including the end of the long synthesizer settings list.
            for(int i=0;i<28;++i) {
                MenuCanvas canvas;s.ui.draw(canvas,0,false,true);CHECK_EQ(canvas.valid,true);
                s.key('.');
            }
            s.key(key::Tab);
        }
        s.key(key::Opt);
    }
    CHECK_EQ(s.ui.menuOpen(),false);
}
TEST(menuMixerLevelAndGlobalVolumeRemainIndependent) {
    Session s;s.key('5',true);s.key(key::Opt);s.key(key::Tab);s.key(key::Tab);
    for(int i=0;i<3;++i)s.key('.');
    s.key(key::Enter);s.key('/',true);s.key(key::Enter);CHECK_EQ(s.ui.arrangement().mixerStrip(),1);
    s.key('.');s.key(key::Enter);s.key(',',true);CHECK_EQ(s.p.mixer[0].volume,95);CHECK_EQ(s.p.masterVolume,100);
    s.key('-');CHECK_EQ(s.p.masterVolume,95);CHECK_EQ(s.p.mixer[0].volume,95);
}
TEST(menuClipboardReportsCompletionAndDisablesEmptyPaste) {
    Session s;s.key('q');s.key(key::Opt);s.key(key::Tab);
    for(int i=0;i<3;++i)s.key('.');
    s.key(key::Enter);MenuCanvas empty;s.ui.draw(empty,0,false,true);
    CHECK_EQ(empty.has("Copy a pattern or scene first"),true);
    s.key(';');s.key(key::Enter);MenuCanvas copied;s.ui.draw(copied,0,false,true);
    CHECK_EQ(copied.has("Copied to clipboard"),true);
    s.key('.');s.key('.');s.key(key::Enter);CHECK_EQ(s.p.drums[0].velocity[0][0],0);
    s.key(';');s.key(key::Enter);CHECK_EQ(s.p.drums[0].velocity[0][0],100);
    MenuCanvas pasted;s.ui.draw(pasted,0,false,true);CHECK_EQ(pasted.has("Pasted"),true);
}
TEST(samplerMenuCanUndoClearWithoutLeavingWaveOrFileView) {
    Session s;s.key('6',true);SampleJob job;s.ui.sampler().takeJob(job);
    SampleResult result;result.job=job;result.ok=true;s.ui.sampler().receive(result);
    s.p.samplePatterns[0].steps.velocity[0][0]=100;
    s.key(key::Opt);s.key(key::Tab);
    for(int i=0;i<10;++i)s.key('.');
    s.key(key::Enter);CHECK_EQ(s.p.samplePatterns[0].steps.velocity[0][0],0);
    s.key('.');s.key(key::Enter);CHECK_EQ(s.p.samplePatterns[0].steps.velocity[0][0],100);
    CHECK_EQ(s.ui.sampler().browserMode(),true);
}
TEST(stringsReturnToHeldNoteAndReleaseCleanlyAcrossMenu) {
    Session s; s.key('2',true); s.ui.instrument().showPerformance();
    CHECK_EQ(s.p.keyboardLayout[0],1); CHECK_EQ(s.p.keyboardScale[0],2);
    s.ui.onKey('z',true); s.run(256);
    s.ui.onKey('x',true); s.run(256);
    CHECK_EQ(s.e.activeVoices(),1); CHECK_EQ(s.ui.instrument().focusPitch(),62);
    s.ui.onKey('x',false);s.run(256);CHECK_EQ(s.ui.instrument().focusPitch(),60);
    s.ui.onKey('a',true);s.run(256);CHECK_EQ(s.e.activeVoices(),2);
    s.key(key::Opt);s.ui.onKey('z',false);s.ui.onKey('a',false);s.run(32000);CHECK_EQ(s.e.activeVoices(),0);
}
TEST(quickStartAndProjectBrowserFitAndConsumeKeys) {
    Session s; s.key(key::Opt);
    for(int i=0;i<7;++i)s.key('.',true);
    s.key(key::Enter);CHECK_EQ(s.ui.helpOpen(),true);
    for(int i=0;i<9;++i){MenuCanvas c;s.ui.draw(c,0,false,true);CHECK_EQ(c.valid,true);s.key(key::Enter);}
    s.key(key::Opt);s.key(key::Opt);
    for(int i=0;i<6;++i)s.key('.',true);
    s.key(key::Enter);CHECK_EQ(s.ui.projectsOpen(),true);
    char name[kProjectName];CHECK_EQ(s.ui.takeProjectRequest(name,sizeof(name)),ProjectAction::Refresh);
    s.ui.library().available=true;
    s.key('.',true);s.key(key::Enter);s.key('b');s.key('e');s.key('a');s.key('t');
    MenuCanvas c;s.ui.draw(c,0,false,true);CHECK_EQ(c.valid,true);
    s.key(key::Enter);CHECK_EQ(s.ui.takeProjectRequest(name,sizeof(name)),ProjectAction::New);CHECK_EQ(std::strcmp(name,"beat"),0);
    CHECK_EQ(s.p.drums[0].velocity[0][0],0);
}
TEST(presetUndoRestoresEntireSoundAndPerformanceFitsDisplay) {
    Session s;s.key('2',true);const auto before=s.p.synths[0];s.ui.instrument().loadPreset(4);
    CHECK_EQ(s.p.synths[0].resonance,95);s.key('z',true);CHECK_EQ(s.p.synths[0].resonance,before.resonance);CHECK_EQ(s.p.synths[0].cutoff,before.cutoff);
    s.ui.instrument().showPerformance();MenuCanvas c;s.ui.draw(c,0,false,true);CHECK_EQ(c.valid,true);
}
TEST(undoKeyboardLayoutReleasesTokensFromTheOldLayout) {
    Session s;s.key('2',true);s.ui.instrument().setKeyboard(0,1);
    s.ui.onKey('x',true);s.run(256);CHECK_EQ(s.e.activeVoices(),1);
    s.key('z',true);s.ui.onKey('x',false);s.run(32000);
    CHECK_EQ(s.p.keyboardLayout[0],0);CHECK_EQ(s.e.activeVoices(),0);
}
