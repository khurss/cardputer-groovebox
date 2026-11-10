#include "gb/audio_memory.h"
#include "gb/sample_player.h"
#include <algorithm>
namespace gb {
void SamplePlayer::setData(uint8_t slot, const int16_t* data, uint32_t frames) {
    if (slot >= kSampleSlots) return;
    stop();
    data_[slot] = {data, frames};
}
void SamplePlayer::trigger(uint8_t slot, uint8_t velocity) {
    if (slot >= kSampleSlots || !data_[slot].pcm || !data_[slot].frames || !velocity) return;
    Voice* next = &voices_[0];
    for (auto& v : voices_) {
        if (!v.data.pcm) { next = &v; break; }
        if (v.serial < next->serial) next = &v;
    }
    *next = {data_[slot], 0, ++serial_, velocity};
}
void SamplePlayer::stop() { for (auto& v : voices_) v.data.pcm = nullptr; }
unsigned SamplePlayer::activeVoices() const {
    unsigned result = 0;
    for (const auto& v : voices_) result += v.data.pcm != nullptr;
    return result;
}
GB_AUDIO_CODE float SamplePlayer::render() {
    // Four voices at full velocity/fade fit in int32; convert only the final sum.
    int32_t sample = 0;
    for (auto& v : voices_) if (v.data.pcm) {
        const int32_t fade = std::min<uint32_t>(64, std::min(v.position + 1, v.data.frames - v.position));
        sample += int32_t(v.data.pcm[v.position++]) * v.velocity * fade;
        if (v.position >= v.data.frames) v.data.pcm = nullptr;
    }
    return sample * (1.0f / (32768.0f * 127 * 64));
}
}
