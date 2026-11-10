#include "gb/equalizer.h"
#include "gb/engine.h"
#include "gb/ui.h"
#include "gb/project_io.h"
#include "gb/validate.h"
#include "test.h"
#include <algorithm>
#include <cmath>
#include <cstring>
using namespace gb;
namespace {
constexpr float pi=3.14159265358979323846f;
float response(int band,int db,float frequency) {
    Equalizer eq(32000);EqualizerSettings s;s.gain[band]=db;eq.configure(s,true);
    double in=0,out=0;
    for(int i=0;i<64000;++i) {
        const float x=.01f*std::sin(2*pi*frequency*i/32000),y=eq.process(x);
        if(i>=32000) { in+=x*x;out+=y*y; }
    }
    return 10*std::log10(out/in);
}
struct Ui {
    Project p;AppUi ui;
    static bool sink(void*,const Command&) { return true; }
    Ui():ui(p,sink,nullptr) { initProject(p); }
    void key(char c,bool fn=false,bool shift=false) {
        if(fn)ui.onKey(key::Fn,true);
        if(shift)ui.onKey(key::Shift,true);
        ui.onKey(c,true);ui.onKey(c,false);
        if(fn)ui.onKey(key::Fn,false);
        if(shift)ui.onKey(key::Shift,false);
    }
};
}
TEST(equalizerBandsBoostAndCutTheRequestedFrequencies) {
    for(int band=0;band<kEqBands;++band)for(int db: {-12,12})
        CHECK_EQ(std::abs(response(band,db,kEqFrequencies[band])-db)<.15f,true);
    CHECK_EQ(std::abs(response(0,12,8000))<.1f,true);
    CHECK_EQ(std::abs(response(6,-12,100))<.1f,true);
}
TEST(equalizerFlatBypassAndRapidEditsRemainStable) {
    Equalizer eq(32000);EqualizerSettings s;
    for(int i=0;i<4096;++i) { float x=std::sin(i*.27f);CHECK_EQ(eq.process(x)==x,true); }
    float maximumJump=0,last=0;
    for(int edit=0;edit<100;++edit) {
        for(int b=0;b<kEqBands;++b)s.gain[b]=(edit+b)%2?12:-12;
        s.enabled=edit%5!=0;eq.configure(s);
        for(int i=0;i<768;++i) {
            const float y=eq.process(.01f*std::sin((edit*768+i)*.1f));
            CHECK_EQ(std::isfinite(y) && std::abs(y)<1,true);
            maximumJump=std::max(maximumJump,std::abs(y-last));last=y;
        }
    }
    CHECK_EQ(maximumJump<.05f,true);
    s.enabled=false;eq.configure(s);for(int i=0;i<4096;++i)eq.process(0);
    CHECK_EQ(eq.process(.123f)==.123f,true);
    s=EqualizerSettings{};eq.configure(s);for(int i=0;i<4096;++i)eq.process(0);
    CHECK_EQ(eq.process(.123f)==.123f,true);
}
TEST(equalizerProcessesSamplerAudioBeforeTheMasterCeiling) {
    static int16_t samples[32000];
    const auto render=[&](int gain,bool enabled,int amplitude,int master) {
        Project p;initProject(p);p.masterVolume=master;Engine e(p,32000);
        for(int i=0;i<32000;++i)samples[i]=amplitude*std::sin(2*pi*1000*i/32000);
        Command c;c.type=CommandType::SetOutputLimit;c.value=5;e.handle(c);
        c.type=CommandType::SetEqualizer;c.equalizer=EqualizerSettings{};c.equalizer.gain[3]=gain;c.equalizer.enabled=enabled;e.handle(c);
        c.type=CommandType::SetSampleData;c.a=0;c.sampleData={samples,32000};e.handle(c);
        c.type=CommandType::PreviewSample;c.value=127;e.handle(c);
        int16_t block[512];double power=0;
        for(int n=0;n<40;++n) {
            e.render(block,256);
            for(int i=0;i<512;++i) {
                CHECK_EQ(std::abs(int(block[i]))<=1557,true);
                if(n>=10)power+=double(block[i])*block[i];
            }
        }
        return power;
    };
    const double dry=render(0,true,600,5),boost=render(12,true,600,5),cut=render(-12,true,600,5);
    CHECK_EQ(dry>0 && boost>dry*14 && boost<dry*18,true);
    CHECK_EQ(cut<dry*.08,true);CHECK_EQ(render(12,false,600,5)==dry,true);
    CHECK_EQ(render(12,true,30000,100)>0,true);CHECK_EQ(render(12,true,30000,0),0);
}
TEST(equalizerProjectMigrationRoundTripAndValidation) {
    static Project p,out;static uint8_t bytes[kProjectBufferSize];
    FILE* file=fopen("examples/projects/neon-drive.gbp","rb");CHECK_EQ(file!=nullptr,true);if(!file)return;
    auto size=fread(bytes,1,sizeof(bytes),file);fclose(file);
    out.equalizer.gain[0]=12;out.equalizer.enabled=false;
    CHECK_EQ(deserializeProject(bytes,size,out),LoadResult::Ok);
    CHECK_EQ(out.equalizer.enabled,true);for(auto g:out.equalizer.gain)CHECK_EQ(g,0);
    p=out;for(int i=0;i<kEqBands;++i)p.equalizer.gain[i]=i*4-12;p.equalizer.enabled=false;
    size=serializeProject(p,bytes,sizeof(bytes));CHECK_EQ(bytes[4],6);
    CHECK_EQ(deserializeProject(bytes,size,out),LoadResult::Ok);
    CHECK_EQ(out.equalizer.enabled,false);for(int i=0;i<kEqBands;++i)CHECK_EQ(out.equalizer.gain[i],p.equalizer.gain[i]);
    Command invalid;invalid.type=CommandType::SetEqualizer;invalid.equalizer=p.equalizer;invalid.equalizer.gain[0]=13;
    CHECK_EQ(isValidCommand(invalid),false);applyEdit(p,invalid);CHECK_EQ(p.equalizer.gain[0],-12);
    p.equalizer.gain[0]=-13;CHECK_EQ(isValidProject(p),false);
}
TEST(equalizerKeyboardBypassResetUndoAndMixerStayIndependent) {
    Ui s;s.p.masterVolume=5;s.key('7',true);
    s.key('/');s.key(';');CHECK_EQ(s.p.equalizer.gain[1],1);CHECK_EQ(s.p.masterVolume,5);
    s.key(';',false,true);CHECK_EQ(s.p.equalizer.gain[1],4);
    for(int i=0;i<20;++i)s.key(';');
    CHECK_EQ(s.p.equalizer.gain[1],12);
    s.key('b');CHECK_EQ(s.p.equalizer.enabled,false);s.key('z',true);CHECK_EQ(s.p.equalizer.enabled,true);
    s.key('0');CHECK_EQ(s.p.equalizer.gain[1],0);s.key('z',true);CHECK_EQ(s.p.equalizer.gain[1],12);
    s.key('/');s.key('.');s.key('0',false,true);
    for(auto g:s.p.equalizer.gain)CHECK_EQ(g,0);
    s.key('z',true);CHECK_EQ(s.p.equalizer.gain[1],12);CHECK_EQ(s.p.equalizer.gain[2],-1);
    s.key('5',true);s.key('/');s.key('.');CHECK_EQ(s.p.mixer[0].volume,95);CHECK_EQ(s.p.equalizer.gain[1],12);
    s.key('7',true);s.key('+');CHECK_EQ(s.p.masterVolume,10);CHECK_EQ(s.p.equalizer.gain[2],-1);
}
