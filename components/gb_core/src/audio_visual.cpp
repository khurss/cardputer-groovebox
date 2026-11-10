#include "gb/audio_visual.h"
#include "gb/ui_style.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace gb {
namespace {
constexpr float pi = 3.14159265358979323846f;
void line(Canvas& c, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = std::abs(x1-x0), sx = x0<x1?1:-1, dy = -std::abs(y1-y0), sy = y0<y1?1:-1, error = dx+dy;
    for (;;) {
        if (x0>=0 && x0<240 && y0>=18 && y0<121) c.fillRect(x0,y0,1,1,color);
        if (x0==x1 && y0==y1) break;
        int e = 2*error;
        if (e>=dy) { error+=dy; x0+=sx; }
        if (e<=dx) { error+=dx; y0+=sy; }
    }
}
}
void AudioTap::capture(const int16_t* stereo, unsigned frames) {
    if (filled_ == kFftSize && requested_.exchange(false, std::memory_order_acquire)) filled_ = 0;
    if (filled_ == kFftSize) return;
    const unsigned count = std::min(frames, kFftSize-filled_);
    for (unsigned i=0;i<count;++i) pending_.mono[filled_++] = (int(stereo[2*i])+stereo[2*i+1])/2;
    if (filled_ == kFftSize) ready_.push(pending_);
}
AudioVisual::AudioVisual() {
    for (unsigned i=0;i<kFftSize;++i) window_[i] = .5f-.5f*std::cos(2*pi*i/(kFftSize-1));
}
const char* AudioVisual::modeName(unsigned n) { constexpr const char* names[]={"SPECTRUM","WAVE","WATERFALL","ORBIT"}; return names[n%4]; }
const char* AudioVisual::paletteName(unsigned n) { constexpr const char* names[]={"NEON","SUNSET","OCEAN","RAINBOW"}; return names[n%4]; }
const char* AudioVisual::smoothingName(unsigned n) { constexpr const char* names[]={"FAST","MEDIUM","SLOW"}; return names[n%3]; }
void AudioVisual::adjust(unsigned p,int d) {
    uint8_t* values[]={&settings_.mode,&settings_.palette,&settings_.gain,&settings_.smoothing,&settings_.motion,&settings_.led};
    if (p>=6 || !d) return;
    const int low[]={0,0,0,0,1,0}, high[]={3,3,5,2,3,50};
    int n = *values[p] + d*(p==5?5:1);
    if (p<2) n=(n+4)%4; else n=std::clamp(n,low[p],high[p]);
    if (*values[p]!=n) { *values[p]=n; ++revisions_; }
}
void AudioVisual::process(const AudioFrame& frame) {
    float mean=0;
    peak_=0;
    for (auto x:frame.mono) { mean+=x; peak_=std::max(peak_,unsigned(std::abs(int(x)))); }
    mean/=kFftSize;
    for (unsigned i=0;i<kFftSize;++i) { real_[i]=(frame.mono[i]-mean)/32768.f*window_[i]; imag_[i]=0; }
    // Iterative radix-2 FFT, fixed storage; all work stays on the UI core.
    for (unsigned i=1,j=0;i<kFftSize;++i) {
        unsigned bit=kFftSize>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) { std::swap(real_[i],real_[j]); std::swap(imag_[i],imag_[j]); }
    }
    for (unsigned length=2;length<=kFftSize;length*=2) {
        float angle=-2*pi/length, wrStep=std::cos(angle), wiStep=std::sin(angle);
        for (unsigned i=0;i<kFftSize;i+=length) {
            float wr=1,wi=0;
            for (unsigned j=0;j<length/2;++j) {
                unsigned a=i+j,b=a+length/2;
                float r=real_[b]*wr-imag_[b]*wi, im=real_[b]*wi+imag_[b]*wr;
                real_[b]=real_[a]-r; imag_[b]=imag_[a]-im;
                real_[a]+=r; imag_[a]+=im;
                float next=wr*wrStep-wi*wiStep; wi=wr*wiStep+wi*wrStep; wr=next;
            }
        }
    }
    float strongest=0; dominantBin_=0;
    for (unsigned i=1;i<=kFftSize/2;++i) {
        real_[i]=std::sqrt(real_[i]*real_[i]+imag_[i]*imag_[i])*4.f/kFftSize;
        if (real_[i]>strongest) { strongest=real_[i]; dominantBin_=i; }
    }
    constexpr unsigned edges[]={1,2,3,4,5,7,10,14,20,28,40,56,80,112,156,216,257};
    const float gain=float(1u<<settings_.gain);
    for (unsigned b=0;b<kSpectrumBands;++b) {
        float magnitude=0;
        for (unsigned i=edges[b];i<edges[b+1];++i) magnitude=std::max(magnitude,real_[i]);
        const float db=20*std::log10(std::max(1e-8f,magnitude*gain));
        unsigned target=unsigned(std::clamp((db+72)*255/72,0.f,255.f));
        // Silence releases visibly, never creates animation from a transport clock.
        if (peak_<=2) target=0;
        unsigned old=bands_[b], divisor=1u<<settings_.smoothing;
        bands_[b] = target>=old ? target : old-std::max(1u,(old-target)/divisor);
    }
    // Auto range: catch a louder transient immediately, recover zoom gradually.
    // A fixed noise floor avoids magnifying silence; keep 15% vertical headroom.
    float wavePeak=0;
    for (auto x:frame.mono) wavePeak=std::max(wavePeak,std::abs(x-mean));
    constexpr float release[]={.70f,.85f,.93f};
    waveScale_=std::max({32.f,wavePeak,waveScale_*release[settings_.smoothing]});
    // Rising zero-crossing trigger reduces horizontal jitter.
    unsigned trigger=0;
    for (unsigned i=1;i<kFftSize/2;++i)
        if (frame.mono[i-1]<=mean && frame.mono[i]>mean) { trigger=i; break; }
    for (unsigned i=0;i<120;++i)
        wave_[i]=peak_<=2 ? 0 : int16_t((frame.mono[trigger+i*2]-mean)/waveScale_*216);
    // Hold each hue for 250 ms: rapid note-to-note changes can visually blend.
    // Multiple strong spectral peaks add a slow palette sweep during dense mixes.
    if (peak_<=2 || !dominantBin_) ledHold_=0;
    else if (!ledHold_) {
        unsigned peaks=0;
        for (unsigned i=2;i<kFftSize/2;++i)
            if (real_[i]>.25f*strongest && real_[i]>real_[i-1] && real_[i]>=real_[i+1]) ++peaks;
        if (peaks>=2) ledPhase_=(ledPhase_+13)%256;
        float semitones=12*std::log2(dominantBin_*62.5f/440.f)+69;
        unsigned note=unsigned(std::max(0,int(std::lround(semitones))))%12;
        ledPosition_=(note*255/11+(peaks>=2?ledPhase_:0))%256;
        ledHold_=4;
    } else --ledHold_;
    for (unsigned n=0;n<settings_.motion;++n) {
        std::memcpy(history_[historyAt_],bands_,sizeof(bands_)); historyAt_=(historyAt_+1)%48;
    }
    ++updates_;
}
uint32_t AudioVisual::rgb(unsigned palette,unsigned position,unsigned intensity) {
    // Interpolate RGB anchors instead of assuming RGB565 survives the RGB332 framebuffer.
    constexpr uint32_t colors[4][4]={{0x00ffd0,0x3070ff,0xff30e0,0xffd040},
        {0xff3040,0xffa020,0xff40b0,0x8040ff},{0x20ff90,0x00e0ff,0x3060ff,0xc040ff},
        {0xff3030,0xffff00,0x00ffd0,0x9030ff}};
    unsigned p=std::min(position,255u)*3, at=p/256, fraction=p%256;
    uint32_t a=colors[palette%4][at],b=colors[palette%4][std::min(at+1,3u)],out=0;
    for (unsigned shift: {0u,8u,16u}) {
        unsigned v=(((a>>shift)&255)*(256-fraction)+((b>>shift)&255)*fraction)/256;
        out|=(v*std::min(intensity,255u)/255)<<shift;
    }
    return out;
}
uint16_t AudioVisual::ink(unsigned position,unsigned intensity) const {
    uint32_t c=rgb(settings_.palette,position,intensity);
    return uint16_t(((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&0x001f));
}
uint32_t AudioVisual::ledRgb() const {
    if (!settings_.led || peak_<=2 || !dominantBin_) return 0;
    // Remove the white component from screen palettes, then normalize brightness.
    // At least one physical LED channel stays off, even at maximum signal level.
    uint32_t color=rgb(settings_.palette,ledPosition_);
    unsigned channels[]={unsigned(color>>16),unsigned((color>>8)&255),unsigned(color&255)};
    unsigned low=std::min({channels[0],channels[1],channels[2]});
    unsigned span=std::max({channels[0],channels[1],channels[2]})-low;
    unsigned level=std::min(255u,peak_*(1u<<settings_.gain)/64);
    unsigned brightness=level*settings_.led/100;
    uint32_t result=0;
    for (auto channel:channels) result=(result<<8)|((channel-low)*brightness/std::max(1u,span));
    return result;
}
void AudioVisual::spectrum(Canvas& c,int top,int height) const {
    for (int y=top;y<top+height;y+=12) c.fillRect(4,y,232,1,color::Panel);
    for (unsigned b=0;b<kSpectrumBands;++b) {
        int h=bands_[b]*height/255;
        for (int y=0;y<h;y+=4) c.fillRect(5+b*14,top+height-std::min(y+3,h),11,std::min(3,h-y),ink(b*255/15,150+105*y/std::max(1,height)));
        if (h) c.fillRect(5+b*14,top+height-h,11,1,color::Text);
    }
}
void AudioVisual::draw(Canvas& c,bool playing) const {
    c.fillRect(0,0,240,135,color::Background);
    style::header(c,modeName(settings_.mode),playing?"PLAY":"LIVE");
    char text[40];
    if (settings_.mode==1) std::snprintf(text,sizeof(text),"%s  AUTO  %s",paletteName(settings_.palette),smoothingName(settings_.smoothing));
    else std::snprintf(text,sizeof(text),"%s  x%u  %s",paletteName(settings_.palette),1u<<settings_.gain,smoothingName(settings_.smoothing));
    c.text(5,19,text,color::Muted);
    if (settings_.mode==0) {
        spectrum(c,34,72); c.text(5,111,"63Hz",color::Muted); c.text(102,111,"1k",color::Muted); c.text(200,111,"16kHz",color::Muted);
    } else if (settings_.mode==1) {
        for (int y=34;y<=112;y+=26) c.fillRect(4,y,232,1,color::Panel);
        for (unsigned i=1;i<120;++i) line(c,2*(i-1),73-wave_[i-1]*38/255,2*i,73-wave_[i]*38/255,ink(i*255/119));
    } else if (settings_.mode==2) {
        for (unsigned row=0;row<42;++row) for (unsigned b=0;b<kSpectrumBands;++b) {
            unsigned v=history_[(historyAt_+48-1-row)%48][b];
            c.fillRect(5+b*14,32+row*2,13,2,v?ink(b*255/15,v):color::Background);
        }
        c.text(5,115,"63Hz",color::Muted); c.text(102,115,"1k",color::Muted); c.text(200,115,"16kHz",color::Muted);
    } else {
        for (unsigned i=0;i<48;++i) {
            unsigned b=i%16; float angle=2*pi*i/48 + (peak_>2 ? updates_*settings_.motion*.015f : 0);
            float s=std::sin(angle),co=std::cos(angle); int inner=18,outer=inner+bands_[b]*27/255;
            line(c,120+int(co*inner*2),75+int(s*inner),120+int(co*outer*2),75+int(s*outer),ink(i*255/47));
        }
        c.text(105,72,peak_>2?"AUDIO":"QUIET",color::Text);
    }
    style::footer(c,settings_.mode==1 ? "L/R view  AUTO scale  P color  Opt" : "L/R view U/D gain  P color  Opt setup");
}
void AudioVisual::drawMixer(Canvas& c,const Project& p,uint8_t strip,bool playing) const {
    c.fillRect(0,0,240,135,color::Background); style::header(c,"MIXER / FFT",playing?"PLAY":"LIVE");
    spectrum(c,20,40);
    c.text(5,63,"63Hz",color::Muted); c.text(102,63,"1k",color::Muted); c.text(200,63,"16kHz",color::Muted);
    constexpr const char* names[]={"MAIN","DRUM","T1","T2","T3","T4","SMP"};
    for (unsigned i=0;i<=kTracks;++i) {
        int x=i*34,level=i?p.mixer[i-1].volume:p.masterVolume;
        c.fillRect(x+1,76,32,44,i==strip?color::LaneSelected:color::Panel);
        c.text(x+3,79,names[i],i==strip?color::Text:ink(i*255/6));
        c.fillRect(x+4,92,24,3,color::StepOff);
        if (level) c.fillRect(x+4,92,std::max(1,level*24/100),3,ink(i*255/6));
        char n[8]; std::snprintf(n,sizeof(n),"%u",unsigned(level)); c.text(x+4,99,n,color::Text);
        if (i) { c.text(x+4,110,"M",p.mixer[i-1].mute?color::Warning:color::StepOff); c.text(x+21,110,"S",p.mixer[i-1].solo?color::StepAccent:color::StepOff); }
    }
    style::footer(c,"Arrows mix  M mute S solo  Opt setup");
}
}
