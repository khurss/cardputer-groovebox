// Deterministic listening examples: four waveforms, chord, resonant sweep and glide.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "gb/synth.h"
namespace {
void u16(FILE* f, uint16_t v) {
    const uint8_t b[] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    fwrite(b, 1, 2, f);
}
void u32(FILE* f, uint32_t v) {
    u16(f, v);
    u16(f, v >> 16);
}
}  // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    gb::Synth synth(32000);
    gb::SynthSound p;
    std::vector<int16_t> samples;
    auto render = [&](int n) {
        for (int i = 0; i < n; ++i) {
            const float v = std::max(-1.0f, std::min(1.0f, synth.render() * 2));
            samples.push_back(static_cast<int16_t>(v * 32767));
        }
    };
    for (int wave = 0; wave < 4; ++wave) {
        p.wave = static_cast<gb::Waveform>(wave);
        synth.setSound(0, p);
        synth.noteOn(0, 60, 120, 0, gb::NoteSource::Live);
        render(16000);
        synth.allNotesOff();
        render(8000);
    }
    p.wave = gb::Waveform::Saw;
    p.resonance = 100;
    synth.setSound(0, p);
    for (int i = 0; i < 3; ++i)
        synth.noteOn(0, 60 + i * 4, 100, i, gb::NoteSource::Live);
    for (int i = 0; i < 32; ++i) {
        p.cutoff = i * 4;
        synth.setSound(0, p);
        render(1000);
    }
    synth.allNotesOff();
    render(8000);
    p.glide = 70;
    synth.setSound(0, p);
    synth.noteOn(0, 48, 110, 10, gb::NoteSource::Live);
    render(16000);
    synth.noteOn(0, 72, 110, 10, gb::NoteSource::Live);
    render(16000);
    synth.allNotesOff();
    render(16000);
    FILE* f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    const uint32_t bytes = samples.size() * 2;
    fwrite("RIFF", 1, 4, f);
    u32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(f, 16);
    u16(f, 1);
    u16(f, 1);
    u32(f, 32000);
    u32(f, 64000);
    u16(f, 2);
    u16(f, 16);
    fwrite("data", 1, 4, f);
    u32(f, bytes);
    for (int16_t v : samples)
        u16(f, static_cast<uint16_t>(v));
    return fclose(f) == 0 ? 0 : 1;
}
