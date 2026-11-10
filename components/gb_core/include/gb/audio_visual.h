#pragma once
#include <atomic>
#include "gb/spsc.h"
#include "gb/ui_types.h"
namespace gb {
constexpr unsigned kFftSize = 512, kSpectrumBands = 16;
struct AudioFrame { int16_t mono[kFftSize] = {}; };
// Audio writes only requested, contiguous snapshots. UI owns all FFT work.
// A slow display may lose a visualization frame, never block audio.
class AudioTap {
public:
    void request() { requested_.store(true, std::memory_order_release); }
    void capture(const int16_t* stereo, unsigned frames);
    bool pop(AudioFrame& out) { return ready_.pop(out); }
private:
    std::atomic<bool> requested_{false};
    SpscQueue<AudioFrame, 2> ready_;
    AudioFrame pending_;
    unsigned filled_ = kFftSize;
};
struct VisualSettings {
    uint8_t mode = 0, palette = 0, gain = 3, smoothing = 1, motion = 1, led = 20;
    bool valid() const { return mode < 4 && palette < 4 && gain < 6 && smoothing < 3 && motion >= 1 && motion <= 3 && led <= 50; }
};
class AudioVisual {
public:
    AudioVisual();
    void process(const AudioFrame& frame);
    void adjust(unsigned parameter, int direction);
    void setSettings(const VisualSettings& s) { if (s.valid()) settings_ = s; }
    const VisualSettings& settings() const { return settings_; }
    uint32_t revisions() const { return revisions_; }
    unsigned peak() const { return peak_; }
    unsigned dominantBin() const { return dominantBin_; }
    unsigned updates() const { return updates_; }
    unsigned band(unsigned n) const { return n < kSpectrumBands ? bands_[n] : 0; }
    uint32_t ledRgb() const;
    void draw(Canvas& c, bool playing) const;
    void drawMixer(Canvas& c, const Project& p, uint8_t strip, bool playing) const;
    static const char* modeName(unsigned n);
    static const char* paletteName(unsigned n);
    static const char* smoothingName(unsigned n);
    static uint32_t rgb(unsigned palette, unsigned position, unsigned intensity = 255);
private:
    uint16_t ink(unsigned position, unsigned intensity = 255) const;
    void spectrum(Canvas& c, int top, int height) const;
    VisualSettings settings_;
    float real_[kFftSize] = {}, imag_[kFftSize] = {}, window_[kFftSize] = {};
    uint8_t bands_[kSpectrumBands] = {}, history_[48][kSpectrumBands] = {};
    int16_t wave_[120] = {};
    uint32_t revisions_ = 0, updates_ = 0;
    unsigned peak_ = 0, dominantBin_ = 0, historyAt_ = 0;
};
}
