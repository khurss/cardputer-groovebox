#include "gb/wav.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <climits>
namespace gb {
namespace {
uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }
void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void put32(uint8_t* p, uint32_t n) { put16(p, n); put16(p + 2, n >> 16); }
}
bool readWavInfo(FILE* f, WavInfo& out) {
    if (!f || fseek(f, 0, SEEK_END) || ftell(f) < 12) return false;
    const uint64_t size = ftell(f);
    rewind(f);
    uint8_t header[16];
    if (fread(header, 1, 12, f) != 12 || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
    const uint64_t end = uint64_t(u32(header + 4)) + 8;
    if (end > size || end < 12 || end > LONG_MAX) return false;
    WavInfo info;
    bool fmt = false, data = false;
    for (uint64_t at = 12; at + 8 <= end;) {
        if (fseek(f, static_cast<long>(at), SEEK_SET) || fread(header, 1, 8, f) != 8) return false;
        const uint32_t length = u32(header + 4);
        if (uint64_t(length) > end - at - 8) return false;
        if (!std::memcmp(header, "fmt ", 4)) {
            if (fmt || length < 16 || fread(header, 1, 16, f) != 16) return false;
            info.format = u16(header); info.channels = u16(header + 2); info.rate = u32(header + 4);
            info.align = u16(header + 12); info.bits = u16(header + 14); fmt = true;
        } else if (!std::memcmp(header, "data", 4)) {
            if (data || !length) return false;
            info.dataOffset = at + 8; info.dataBytes = length; data = true;
        }
        at += 8 + uint64_t(length) + (length & 1);
    }
    if (!fmt || !data || (info.channels != 1 && info.channels != 2) || info.rate < 8000 || info.rate > 192000 ||
        (info.format != 1 && info.format != 3) ||
        (info.format == 3 ? info.bits != 32 : (info.bits != 8 && info.bits != 16 && info.bits != 24 && info.bits != 32)) ||
        info.align != info.channels * (info.bits / 8) || info.dataBytes % info.align) return false;
    info.frames = info.dataBytes / info.align;
    const uint64_t converted = uint64_t(info.frames) * kSampleRate / info.rate;
    if (!converted || converted > UINT32_MAX) return false;
    info.outputFrames = converted;
    out = info;
    return true;
}
bool writeWavHeader(FILE* f, uint32_t frames) {
    if (!f || frames > (UINT32_MAX - 36) / 2) return false;
    uint8_t h[44] = {};
    std::memcpy(h, "RIFF", 4); put32(h + 4, frames * 2 + 36); std::memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, kSampleRate);
    put32(h + 28, kSampleRate * 2); put16(h + 32, 2); put16(h + 34, 16);
    std::memcpy(h + 36, "data", 4); put32(h + 40, frames * 2);
    return fseek(f, 0, SEEK_SET) == 0 && fwrite(h, 1, sizeof(h), f) == sizeof(h);
}
bool WavReader::open(const char* path) {
    close(); failed_ = false;
    file_ = fopen(path, "rb");
    if (!readWavInfo(file_, info_)) { close(); return false; }
    position_ = 0; bufferedAt_ = UINT32_MAX; bufferedFrames_ = 0;
    return true;
}
void WavReader::close() { if (file_) fclose(file_); file_ = nullptr; }
bool WavReader::seek(uint32_t outputFrame) {
    if (!file_ || outputFrame > info_.outputFrames) return false;
    position_ = outputFrame; return true;
}
float WavReader::frame(uint32_t index) {
    index = std::min(index, info_.frames - 1);
    if (bufferedAt_ == UINT32_MAX || index < bufferedAt_ || index - bufferedAt_ >= bufferedFrames_) {
        bufferedAt_ = index;
        bufferedFrames_ = std::min<uint32_t>(sizeof(buffer_) / info_.align, info_.frames - index);
        if (fseek(file_, info_.dataOffset + static_cast<long>(uint64_t(index) * info_.align), SEEK_SET) ||
            fread(buffer_, info_.align, bufferedFrames_, file_) != bufferedFrames_) { failed_ = true; return 0; }
    }
    const auto* p = buffer_ + (index - bufferedAt_) * info_.align;
    float mix = 0;
    for (unsigned c = 0; c < info_.channels; ++c, p += info_.bits / 8) {
        float value = 0;
        if (info_.format == 3) {
            const uint32_t bits = u32(p); std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value)) value = 0;
        } else if (info_.bits == 8) value = (int(p[0]) - 128) / 128.0f;
        else if (info_.bits == 16) value = static_cast<int16_t>(u16(p)) / 32768.0f;
        else if (info_.bits == 24) {
            const uint32_t raw = (uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24);
            value = static_cast<int32_t>(raw) / 2147483648.0f;
        } else value = static_cast<int32_t>(u32(p)) / 2147483648.0f;
        mix += std::max(-1.0f, std::min(1.0f, value));
    }
    return mix / info_.channels;
}
size_t WavReader::read(int16_t* out, size_t count) {
    if (!file_ || failed_) return 0;
    const auto n = std::min<size_t>(count, info_.outputFrames - position_);
    for (size_t i = 0; i < n; ++i, ++position_) {
        const uint64_t at = uint64_t(position_) * info_.rate;
        const uint32_t index = at / kSampleRate;
        const float a = frame(index), fraction = (at % kSampleRate) * (1.0f / kSampleRate);
        const float sample = fraction ? a + (frame(index + 1) - a) * fraction : a;
        if (failed_) return i;
        out[i] = static_cast<int16_t>(std::max(-32768.0f, std::min(32767.0f, sample * 32768)));
    }
    return n;
}
bool buildWaveform(WavReader& reader, WavePeak* peaks, uint32_t start, uint32_t end) {
    if (!peaks || end <= start || end > reader.info().outputFrames || !reader.seek(start)) return false;
    for (unsigned i = 0; i < kWaveColumns; ++i) peaks[i] = {};
    int16_t block[256];
    for (uint32_t at = start; at < end;) {
        const auto count = std::min<uint32_t>(256, end - at);
        if (reader.read(block, count) != count) return false;
        for (unsigned i = 0; i < count; ++i) {
            auto& peak = peaks[uint64_t(at + i - start) * kWaveColumns / (end - start)];
            peak.low = std::min(peak.low, block[i]); peak.high = std::max(peak.high, block[i]);
        }
        at += count;
    }
    return true;
}
}
