#pragma once
#include "gb/model.h"
namespace gb {
// Seven Q=1 peaking biquads on the summed mono signal, before the output limiter.
class Equalizer {
public:
    explicit Equalizer(uint32_t sampleRate);
    void configure(const EqualizerSettings& settings,bool immediate=false);
    float process(float input);
private:
    struct Band {
        float coefficients[5]={1,0,0,0,0},target[5]={1,0,0,0,0},step[5]={};
        float z1=0,z2=0;
        unsigned ramp=0;
        bool active=false;
    };
    Band bands_[kEqBands];
    EqualizerSettings settings_;
    uint32_t rate_;
    float wet_=1,wetTarget_=1;
};
}
