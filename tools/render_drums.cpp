// Writes each drum sound one after another to a WAV file, for listening on a computer.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gb/drums.h"

namespace {

void putU32(FILE* file, uint32_t value) { fwrite(&value, 4, 1, file); }
void putU16(FILE* file, uint16_t value) { fwrite(&value, 2, 1, file); }

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: render_drums <output.wav>\n");
        return 2;
    }
    const uint32_t rate = 32000;
    std::vector<int16_t> samples;
    gb::DrumRack rack(rate);
    for (uint8_t lane = 0; lane < gb::kDrumLanes; ++lane) {
        rack.trigger(lane, 127, gb::DrumSound{});
        for (uint32_t i = 0; i < rate / 2; ++i) {
            float sample = rack.render();
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            samples.push_back(static_cast<int16_t>(sample * 32767.0f));
        }
    }
    FILE* file = fopen(argv[1], "wb");
    if (!file) {
        perror(argv[1]);
        return 1;
    }
    const auto bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    fwrite("RIFF", 1, 4, file);
    putU32(file, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, file);
    putU32(file, 16);
    putU16(file, 1);
    putU16(file, 1);
    putU32(file, rate);
    putU32(file, rate * 2);
    putU16(file, 2);
    putU16(file, 16);
    fwrite("data", 1, 4, file);
    putU32(file, bytes);
    fwrite(samples.data(), 1, bytes, file);
    fclose(file);
    return 0;
}
