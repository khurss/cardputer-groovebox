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
bool initRgb();
bool setRgb(uint32_t rgb);
// Moves one audio block out and one in. Both buffers hold kBlockFrames stereo frames.
bool exchangeAudio(const int16_t* output, int16_t* input);
AudioStats audioStats();
struct AudioOutputConfig {
    uint8_t volume = 0, control = 0, drc = 0, eq = 0;
};
// Read-only ES8311 diagnostics; call from the UI/I2C thread.
bool readAudioOutputConfig(AudioOutputConfig& config);

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
