#include "gb/audio_visual.h"
#include "gb/ui.h"
#include "test.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include <set>
using namespace gb;
namespace {
AudioFrame tone(unsigned bin, int level=1500) {
    AudioFrame f;
    for(unsigned i=0;i<kFftSize;++i) f.mono[i]=int16_t(level*std::sin(6.28318530718*bin*i/kFftSize));
    return f;
}
struct CanvasCheck : Canvas {
    bool valid=true; unsigned pixels=0;
    void fillRect(int x,int y,int w,int h,uint16_t) override {
        valid &= x>=0 && y>=0 && w>0 && h>0 && x+w<=240 && y+h<=135; pixels+=w*h;
    }
    void text(int x,int y,const char* t,uint16_t) override { valid &= x>=0 && y>=0 && x+int(strlen(t))*6<=240 && y+8<=135; }
};
struct WaveCanvas : Canvas {
    int top=135,bottom=0;
    bool automatic=false;
    void fillRect(int,int y,int w,int h,uint16_t) override {
        if(w==1 && h==1) { top=std::min(top,y);bottom=std::max(bottom,y); }
    }
    void text(int,int,const char* t,uint16_t) override { if(strstr(t,"AUTO"))automatic=true; }
};
WaveCanvas waveView(const AudioVisual& v) { WaveCanvas c;v.draw(c,true);return c; }
unsigned hue(uint32_t rgb) {
    unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255,high=std::max({r,g,b});
    return high ? ((r*15/high)<<8)|((g*15/high)<<4)|(b*15/high) : 0;
}
struct UiSession {
    Project p;
    std::vector<Command> commands;
    AppUi ui;
    static bool send(void* context,const Command& c) { static_cast<UiSession*>(context)->commands.push_back(c);return true; }
    static Project make() { Project p;initProject(p);return p; }
    UiSession():p(make()),ui(p,send,this) {}
    void key(char k,bool fn=false,bool shift=false) {
        if(fn) ui.onKey(key::Fn,true);
        if(shift) ui.onKey(key::Shift,true);
        ui.onKey(k,true);ui.onKey(k,false);
        if(shift) ui.onKey(key::Shift,false);
        if(fn) ui.onKey(key::Fn,false);
    }
};
}
TEST(fftFindsKnownFrequenciesAndRejectsDc) {
    AudioVisual v;
    for(unsigned bin: {2u,16u,64u,192u}) {
        v.process(tone(bin)); CHECK_EQ(v.dominantBin(),bin); CHECK_EQ(v.peak()<=1500,true);
    }
    AudioFrame dc;for(auto& x:dc.mono)x=1500;
    for(int i=0;i<40;++i)v.process(dc);
    for(unsigned i=0;i<kSpectrumBands;++i)CHECK_EQ(v.band(i),0);
    v.process(AudioFrame{}); CHECK_EQ(v.ledRgb(),0u);
}
TEST(fftSensitivityOnlyAffectsDisplayAndSilenceDecays) {
    AudioVisual v; auto f=tone(16,500); v.process(f);
    const auto peak=v.peak(), band=v.band(7); CHECK_EQ(band>0,true);
    v.adjust(2,1);v.process(f);CHECK_EQ(v.band(7)>band,true);CHECK_EQ(v.peak(),peak);
    for(int i=0;i<40;++i)v.process(AudioFrame{});
    for(unsigned i=0;i<kSpectrumBands;++i)CHECK_EQ(v.band(i),0);
    v.process(tone(7));auto low=v.ledRgb();for(int i=0;i<5;++i)v.process(tone(11));CHECK_EQ(low!=v.ledRgb(),true);
    for(int i=0;i<10;++i)v.adjust(5,-1);
    CHECK_EQ(v.ledRgb(),0u);
}
TEST(waveAutoRangeKeepsQuietAndLoudSignalsVisibleWithoutClipping) {
    for(int gain=0;gain<6;++gain) {
        AudioVisual v;VisualSettings s;s.mode=1;s.gain=gain;v.setSettings(s);
        v.process(tone(8,300));auto quiet=waveView(v);
        CHECK_EQ(quiet.automatic,true);CHECK_EQ(quiet.top>=40 && quiet.bottom<=106,true);
        CHECK_EQ(quiet.bottom-quiet.top>=60,true);
        v.process(tone(8,32000));auto loud=waveView(v);
        CHECK_EQ(loud.top,quiet.top);CHECK_EQ(loud.bottom,quiet.bottom);
        v.process(tone(8,300));auto recovering=waveView(v);
        CHECK_EQ(recovering.bottom-recovering.top<quiet.bottom-quiet.top,true);
        for(int i=0;i<60;++i)v.process(tone(8,300));
        CHECK_EQ(waveView(v).top,quiet.top);CHECK_EQ(waveView(v).bottom,quiet.bottom);
        AudioFrame extreme;extreme.mono[3]=-32768;for(unsigned i=4;i<kFftSize;++i)extreme.mono[i]=32767;
        v.process(extreme);auto transient=waveView(v);CHECK_EQ(transient.top>=40 && transient.bottom<=106,true);
        for(int i=0;i<60;++i)v.process(tone(8,3));
        CHECK_EQ(waveView(v).bottom-waveView(v).top<8,true);
        v.process(AudioFrame{});CHECK_EQ(waveView(v).top,73);CHECK_EQ(waveView(v).bottom,73);
    }
}
TEST(ledStaysSaturatedAndHoldsDistinctHuesInDenseAudio) {
    for(unsigned palette=0;palette<4;++palette) {
        AudioVisual v;VisualSettings s;s.palette=palette;s.led=50;v.setSettings(s);
        auto mix=tone(7,500);auto upper=tone(31,500);
        for(unsigned i=0;i<kFftSize;++i)mix.mono[i]+=upper.mono[i];
        std::set<unsigned> hues;unsigned held=0;
        for(int n=0;n<80;++n) {
            v.process(mix);auto color=v.ledRgb();
            unsigned r=color>>16,g=(color>>8)&255,b=color&255;
            CHECK_EQ(std::min({r,g,b}),0u);CHECK_EQ(std::max({r,g,b})<=127,true);
            if(n%5==0) { held=hue(color);hues.insert(held); }
            else CHECK_EQ(hue(color),held);
        }
        CHECK_EQ(hues.size()>=6,true);
        v.process(AudioFrame{});CHECK_EQ(v.ledRgb(),0u);
    }
    AudioVisual v;v.process(tone(7));auto first=hue(v.ledRgb());
    for(int i=0;i<4;++i) { v.process(tone(11));CHECK_EQ(hue(v.ledRgb()),first); }
    v.process(tone(11));CHECK_EQ(hue(v.ledRgb())!=first,true);
}
TEST(audioTapCapturesContiguousStereoAndNeverBlocksOnFullQueue) {
    AudioTap tap;AudioFrame f;int16_t stereo[512];
    for(int i=0;i<512;++i)stereo[i]=100;
    tap.capture(stereo,256);CHECK_EQ(tap.pop(f),false);
    tap.request();tap.capture(stereo,256);CHECK_EQ(tap.pop(f),false);
    for(auto& x:stereo)x=-200;
    tap.capture(stereo,256);CHECK_EQ(tap.pop(f),true);
    CHECK_EQ(f.mono[0],100);CHECK_EQ(f.mono[255],100);CHECK_EQ(f.mono[256],-200);CHECK_EQ(f.mono[511],-200);
    for(int n=0;n<50;++n){tap.request();tap.capture(stereo,256);tap.capture(stereo,256);}
    CHECK_EQ(tap.pop(f),true);CHECK_EQ(tap.pop(f),false);
    tap.request();tap.capture(stereo,256);tap.capture(stereo,256);CHECK_EQ(tap.pop(f),true);
}
TEST(allVisualModesPalettesAndMixerFitTheDisplay) {
    AudioVisual v;Project p;initProject(p);
    AudioFrame extreme;for(unsigned i=0;i<kFftSize;++i)extreme.mono[i]=i%2?-32768:32767;
    for(int mode=0;mode<4;++mode) {
        for(int palette=0;palette<4;++palette) {
            v.process(extreme);CanvasCheck c;v.draw(c,true);CHECK_EQ(c.valid,true);
            CanvasCheck m;v.drawMixer(m,p,6,true);CHECK_EQ(m.valid,true);
            v.adjust(1,1);
        }
        v.adjust(0,1);
    }
    VisualSettings invalid;invalid.gain=255;v.setSettings(invalid);CHECK_EQ(v.settings().valid(),true);
    for(unsigned p=0;p<6;++p)for(int n=0;n<300;++n){v.adjust(p,1);CHECK_EQ(v.settings().valid(),true);}
}
TEST(defaultPerformanceDoesNotModifyExistingProjectOrStealMusicalArrowKeys) {
    UiSession s;auto before=s.p;
    s.key('2',true);CHECK_EQ(s.ui.instrument().performanceVisible(),true);
    CHECK_EQ(std::memcmp(&before,&s.p,sizeof(Project)),0);
    s.key(','); bool note=false;
    for(auto c:s.commands)if(c.type==CommandType::LiveNoteOn)note=true;
    CHECK_EQ(note,true);
    s.key(key::Esc);CHECK_EQ(s.ui.instrument().performanceVisible(),false);
    int pitch=s.ui.instrument().focusPitch();s.commands.clear();s.key(';');
    CHECK_EQ(s.ui.instrument().focusPitch()>pitch,true);
    for(auto c:s.commands)CHECK_EQ(c.type==CommandType::LiveNoteOn,false);
    s.key('s',true);auto sound=s.p.synths[0].wave;s.key(';');CHECK_EQ(s.p.synths[0].wave!=sound,true);
}
TEST(bareArrowsNavigateWorkspacesAndFftSharesMixer) {
    UiSession s;s.key('.');CHECK_EQ(s.ui.drums().selectedLane(),1);s.key('/');CHECK_EQ(s.ui.drums().currentPattern(),1);
    s.key('3',true);s.key('.');s.key('/');CHECK_EQ(s.ui.arrangement().scene(),1);CHECK_EQ(s.ui.arrangement().column(),1);
    s.key('4',true);s.key(key::Enter,true);s.key('/');CHECK_EQ(s.ui.arrangement().editingRepeats(),true);
    s.key('7',true);s.key('/');s.key('.');CHECK_EQ(s.p.mixer[0].volume,95);
    s.key('m');CHECK_EQ(s.p.mixer[0].mute,true);
    s.key('5',true);CHECK_EQ(s.ui.arrangement().mixerStrip(),1);
    s.key('6',true);SampleJob job;s.ui.sampler().takeJob(job);SampleResult result;result.job=job;result.ok=true;s.ui.sampler().receive(result);s.ui.sampler().setMode(true);s.key('.');CHECK_EQ(s.ui.sampler().pad(),1);
    s.key('8',true);s.key('/');CHECK_EQ(s.ui.visual().settings().mode,1);
    s.key('p');CHECK_EQ(s.ui.visual().settings().palette,1);
    s.key(';');CHECK_EQ(s.ui.visual().settings().gain,4);
    s.key(key::Tab);CHECK_EQ(s.ui.screen(),0);
}
TEST(visualMenusAndHelpStayInBoundsAndNeverChangeAudioForDisplaySettings) {
    UiSession s;
    for(char screen: {'7','8'}) {
        s.key(screen,true);s.key(key::Opt);
        for(int page=0;page<3;++page) {
            for(int row=0;row<20;++row) {
                CanvasCheck c;s.ui.draw(c,0,false,true);CHECK_EQ(c.valid,true);s.key('.');
            }
            s.key(key::Tab);
        }
        s.key(key::Opt);
    }
    s.key('8',true);s.key(key::Opt);s.key(key::Tab);s.key(key::Tab);
    for(int i=0;i<4;++i)s.key('.'); // palette after the three global settings
    auto before=s.p;s.commands.clear();s.key(key::Enter);s.key('/');s.key(key::Esc);
    CHECK_EQ(s.commands.empty(),true);CHECK_EQ(std::memcmp(&before,&s.p,sizeof(Project)),0);
}
TEST(projectAndHelpNavigationWorkWithoutFnAndNameEntryKeepsPunctuation) {
    UiSession s;s.key(key::Opt);for(int i=0;i<6;++i)s.key('.');s.key(key::Enter);
    CHECK_EQ(s.ui.projectsOpen(),true);s.ui.library().available=true;s.key('.');s.key(key::Enter);
    s.key('a');s.key('-');s.key('b');s.key(key::Enter);
    char name[kProjectName];CHECK_EQ(s.ui.takeProjectRequest(name,sizeof(name)),ProjectAction::New);CHECK_EQ(strcmp(name,"a-b"),0);
    s.key(key::Esc);s.key(key::Opt);for(int i=0;i<7;++i)s.key('.');s.key(key::Enter);
    CHECK_EQ(s.ui.helpOpen(),true);s.key('/');CanvasCheck c;s.ui.draw(c,0,false,true);CHECK_EQ(c.valid,true);
}
