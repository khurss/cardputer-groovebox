// Render interface examples; PCM comes from the demo engine at 5% volume.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "gb/ui.h"
#include "gb/demo_projects.h"
#include "gb/engine.h"
struct Dump:gb::Canvas{void fillRect(int x,int y,int w,int h,uint16_t c)override{printf("R %d %d %d %d %u\n",x,y,w,h,c);}void text(int x,int y,const char* s,uint16_t c)override{printf("T %d %d %u %s\n",x,y,c,s);}};
bool sink(void*,const gb::Command&){return true;}
static gb::Project p;
int main(){gb::makeDemoProject(0,p);gb::AppUi ui(p,sink,nullptr);Dump c;bool renderPlaying=false;
 auto key=[&](char k,bool fn=false){if(fn)ui.onKey(gb::key::Fn,true);ui.onKey(k,true);ui.onKey(k,false);if(fn)ui.onKey(gb::key::Fn,false);};
 auto draw=[&](int n){printf("FRAME %d\n",n);ui.draw(c,168,renderPlaying,true);};
 key('2',true);ui.instrument().showPerformance();p.synths[0].glide=65;ui.onKey('z',true);ui.onKey('a',true);ui.onKey('q',true);
 const char* seq="xcvbnm";char last=0;for(unsigned n=1;n<=120;++n){if(n%15==0){if(last)ui.onKey(last,false);last=seq[(n/15)%6];ui.onKey(last,true);}ui.setTime(n*50);}draw(0);
 key('1',true);draw(1);
 key('2',true);ui.instrument().showPianoRoll();key(']',true);key(']',true);key(']',true);draw(2);
 ui.instrument().showParameters();draw(3);
 key('5',true);p.mixer[2].mute=true;p.mixer[4].solo=true;draw(4);
 ui.setTransport(8u|(1u<<9)|(12u<<10)|(1u<<16));ui.setPlayback(true);renderPlaying=true;
 key('4',true);draw(5);
 key('3',true);draw(6);
 ui.setTransport(0);ui.setPlayback(false);renderPlaying=false;
 key('6',true);gb::SampleJob job;ui.sampler().takeJob(job);gb::SampleResult r;r.job=job;r.ok=true;r.frames=32000*4;r.files=4;r.visibleFiles=4;
 strcpy(r.names[0],"drum-break.wav");strcpy(r.names[1],"night-drive.wav");strcpy(r.names[2],"vocal-chop.wav");strcpy(r.names[3],"tape-noise.wav");ui.sampler().receive(r);draw(7);
 ui.sampler().onKey(gb::key::Enter,false,false);ui.sampler().takeJob(job);r={};r.job=job;r.ok=true;r.frames=32000*4;
 for(int x=0;x<gb::kWaveColumns;++x){int a=int((.2+.8*exp(-(x%30)/8.f))*22000);r.peaks[x]={int16_t(-a*(.5+.5*fabs(sin(x*.7)))),int16_t(a*(.5+.5*fabs(cos(x*.8))))};}ui.sampler().receive(r);ui.sampler().setMode(false);draw(8);
 strcpy(p.samples[0].source,"drum-break.wav");p.samples[0].end=32000;p.samplePatterns[0].steps=p.drums[2];ui.sampler().setMode(true);draw(9);
 key(gb::key::Opt);draw(10);key('1',true);key(gb::key::Opt);for(int i=0;i<6;++i)key('.');key(gb::key::Enter);
 ui.library().available=true;ui.library().count=3;strcpy(ui.library().names[0],"neon-drive");strcpy(ui.library().names[1],"sunset-arcade");strcpy(ui.library().names[2],"midnight-run");draw(11);
 key(gb::key::Opt);key(gb::key::Opt);for(int i=0;i<7;++i)key('.');key(gb::key::Enter);draw(12);
 key(gb::key::Opt);
 static gb::Project audioProject;gb::makeDemoProject(0,audioProject);
 audioProject.masterVolume=5;
 gb::Engine engine(audioProject,32000);
 gb::Command play;play.type=gb::CommandType::PlaySong;play.a=12;engine.handle(play);
 int16_t pcm[1024];gb::AudioFrame frame,best;
 auto advance=[&](){unsigned loudest=0;for(int n=0;n<96;++n){engine.render(pcm,512);unsigned peak=0;for(unsigned i=0;i<gb::kFftSize;++i){frame.mono[i]=pcm[i*2];peak=std::max(peak,unsigned(std::abs(int(frame.mono[i]))));}ui.audioFrame(frame);if(peak>loudest){loudest=peak;best=frame;}}if(ui.visual().settings().mode==1)ui.audioFrame(best);};
 renderPlaying=true;ui.setPlayback(true);advance();key('7',true);draw(13);
 key('8',true);
 for(int mode=0;mode<4;++mode){auto settings=ui.visual().settings();settings.palette=mode;settings.gain=3;ui.visual().setSettings(settings);advance();draw(14+mode);key('/');}
 key(gb::key::Opt);key(gb::key::Tab);key(gb::key::Tab);draw(18);
 key(gb::key::Opt);key('4',true);
 ui.setTransport(8u|(1u<<9)|(12u<<10)|(1u<<16));ui.setPlayback(true);
 key(';');draw(19);
}
