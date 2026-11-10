#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "gb/model.h"
namespace gb {
struct WavePeak { int16_t low = 0, high = 0; };
constexpr unsigned kWaveColumns = 240;
struct WavInfo {
    uint32_t rate = 0, frames = 0, outputFrames = 0, dataOffset = 0, dataBytes = 0;
    uint16_t channels = 0, bits = 0, format = 0, align = 0;
};
// Strict RIFF bounds; PCM 8/16/24/32 and IEEE float32, mono/stereo.
bool readWavInfo(FILE* file, WavInfo& info);
bool writeWavHeader(FILE* file, uint32_t frames);
class WavReader {
public:
    ~WavReader() { close(); }
    bool open(const char* path);
    void close();
    bool seek(uint32_t outputFrame);
    size_t read(int16_t* out, size_t count);
    const WavInfo& info() const { return info_; }
    bool failed() const { return failed_; }
private:
    float frame(uint32_t index);
    FILE* file_ = nullptr;
    WavInfo info_;
    uint32_t position_ = 0, bufferedAt_ = UINT32_MAX, bufferedFrames_ = 0;
    bool failed_ = false;
    uint8_t buffer_[2048];
};
bool buildWaveform(WavReader& reader, WavePeak* peaks, uint32_t start, uint32_t end);
}
