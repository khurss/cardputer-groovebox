#pragma once
#include "gb/model.h"
namespace gb {
constexpr unsigned kSampleVoices = 4;
// All pointers refer to immutable memory-mapped PCM. No filesystem or allocation in render().
class SamplePlayer {
public:
    void setData(uint8_t slot, const int16_t* data, uint32_t frames);
    void trigger(uint8_t slot, uint8_t velocity);
    void stop();
    float render();
    unsigned activeVoices() const;
private:
    struct Data { const int16_t* pcm = nullptr; uint32_t frames = 0; } data_[kSampleSlots];
    struct Voice { Data data; uint32_t position = 0, serial = 0; uint8_t velocity = 0; } voices_[kSampleVoices];
    uint32_t serial_ = 0;
};
}
