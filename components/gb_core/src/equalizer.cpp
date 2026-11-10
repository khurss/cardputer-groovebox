#include "gb/equalizer.h"
#include "gb/audio_memory.h"
#include <algorithm>
#include <cmath>
namespace gb {
Equalizer::Equalizer(uint32_t sampleRate):rate_(sampleRate) { configure(settings_,true); }
void Equalizer::configure(const EqualizerSettings& settings,bool immediate) {
    if(!settings.valid())return;
    for(int i=0;i<kEqBands;++i) {
        auto& b=bands_[i];
        if(!immediate && settings.gain[i]==settings_.gain[i])continue;
        // RBJ peaking EQ, Q=1; https://www.w3.org/TR/audio-eq-cookbook/
        const float a=std::pow(10.f,settings.gain[i]/40.f);
        const float w=6.28318530718f*std::min(float(kEqFrequencies[i]),rate_*.45f)/rate_;
        const float alpha=std::sin(w)*.5f,c=-2*std::cos(w),a0=1+alpha/a;
        const float coefficients[]={(1+alpha*a)/a0,c/a0,(1-alpha*a)/a0,c/a0,(1-alpha/a)/a0};
        const bool snap=immediate || wet_==0;
        b.ramp=snap?0:512; // 16 ms at the device's 32 kHz sample rate.
        b.active=settings.gain[i]!=0;
        for(int n=0;n<5;++n) {
            b.target[n]=coefficients[n];
            if(snap)b.coefficients[n]=b.target[n];
            b.step[n]=(b.target[n]-b.coefficients[n])/512;
        }
        if(snap)b.z1=b.z2=0;
    }
    settings_=settings;
    wetTarget_=settings.enabled?1.f:0.f;
    if(immediate)wet_=wetTarget_;
}
GB_AUDIO_CODE float Equalizer::process(float input) {
    if(wet_==0 && wetTarget_==0)return input;
    float output=input;
    for(auto& b:bands_) {
        if(b.ramp) {
            for(int n=0;n<5;++n)b.coefficients[n]+=b.step[n];
            if(!--b.ramp) {
                for(int n=0;n<5;++n)b.coefficients[n]=b.target[n];
                if(!b.active)b.z1=b.z2=0;
            }
        }
        if(!b.active && !b.ramp)continue;
        const auto* c=b.coefficients;
        const float next=c[0]*output+b.z1;
        b.z1=c[1]*output-c[3]*next+b.z2;
        b.z2=c[2]*output-c[4]*next;
        output=next;
    }
    if(wet_!=wetTarget_) {
        wet_+=(wetTarget_-wet_)*.01f;
        if(std::abs(wet_-wetTarget_)<.00001f) {
            wet_=wetTarget_;
            if(wet_==0)for(auto& b:bands_) {
                b.z1=b.z2=0;b.ramp=0;
                for(int n=0;n<5;++n)b.coefficients[n]=b.target[n];
            }
        }
    }
    return wet_==1?output:input+(output-input)*wet_;
}
}
