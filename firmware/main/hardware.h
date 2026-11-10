#pragma once

#include <cstdint>

// Everything that touches the board. Nothing here knows about music.
namespace hw {

constexpr uint32_t kSampleRate = 32000;
constexpr uint32_t kBlockFrames = 256;

struct AudioStats {
    uint32_t txOverflows;
    uint32_t rxOverflows;
};

bool initBus();
bool initAudio();
bool initKeyboard();
// Moves one audio block out and one in. Both buffers hold kBlockFrames stereo frames.
bool exchangeAudio(const int16_t* output, int16_t* input);
AudioStats audioStats();

struct KeyEvent {
    char code;
    bool pressed;
};
// Returns true and fills `event` when a key changed.
bool pollKey(KeyEvent& event);

bool mountCard();
// FatFs type number of the mounted card, 0 when unknown (2 FAT16, 3 FAT32, 4 exFAT).
int cardFilesystem();

}  // namespace hw
