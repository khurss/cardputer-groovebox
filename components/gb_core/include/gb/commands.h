#pragma once

#include <cstdint>

#include "gb/model.h"

namespace gb {

enum class CommandType : uint8_t {
    SetStep,          // a pattern, b lane, c step, value velocity
    SetLength,        // a pattern, value length
    SetGrid,          // a pattern, value Grid
    SetBpm,           // value bpm
    SetSwing,         // value swing
    SetSound,         // a lane, b SoundParam, value amount
    SetScenePattern,  // a scene, b track, c pattern
    SetDrumPattern,   // a pattern, pattern contents
    Play,
    Stop,
    Preview,  // a lane, b velocity
};

enum class SoundParam : uint8_t { Tune, Decay, Tone, Level };

struct Command {
    CommandType type = CommandType::Stop;
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t c = 0;
    uint16_t value = 0;
    DrumPattern pattern;
};

// Gives a blank project its playable starting point.
void initProject(Project& project);

// Applies a command that edits the project; transport commands leave it unchanged.
void applyEdit(Project& project, const Command& command);

}  // namespace gb
