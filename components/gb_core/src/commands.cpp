#include "gb/commands.h"

#include "gb/validate.h"

namespace gb {

void initProject(Project& project) { project.scenes[0].pattern[0] = 0; }

bool isValidCommand(const Command& command) {
    switch (command.type) {
        case CommandType::SetStep:
            return command.a < kPatternsPerTrack && command.b < kDrumLanes && command.c < kMaxSteps &&
                   command.value <= 127;
        case CommandType::SetLength:
            return command.a < kPatternsPerTrack && (command.value == 4 || command.value == 8 || command.value == 16);
        case CommandType::SetGrid:
            return command.a < kPatternsPerTrack && command.value <= static_cast<uint16_t>(Grid::SixteenthTriplet);
        case CommandType::SetBpm:
            return command.value >= 40 && command.value <= 240;
        case CommandType::SetSwing:
            return command.value >= 50 && command.value <= 75;
        case CommandType::SetSound:
            return command.a < kDrumLanes && command.b <= static_cast<uint8_t>(SoundParam::Level) &&
                   command.value <= 127;
        case CommandType::SetScenePattern:
            return command.a < kScenes && command.b < kTracks && command.c < kPatternsPerTrack;
        case CommandType::SetDrumPattern:
            return command.a < kPatternsPerTrack &&
                   (command.pattern.length == 4 || command.pattern.length == 8 || command.pattern.length == 16) &&
                   static_cast<uint8_t>(command.pattern.grid) <= static_cast<uint8_t>(Grid::SixteenthTriplet);
        case CommandType::Play:
        case CommandType::Stop:
            return true;
        case CommandType::Preview:
            return command.a < kDrumLanes && command.b <= 127;
    }
    return false;
}

void applyEdit(Project& project, const Command& command) {
    if (!isValidCommand(command)) return;
    switch (command.type) {
        case CommandType::SetStep:
            project.drums[command.a].velocity[command.b][command.c] = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetLength:
            project.drums[command.a].length = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetGrid:
            changeDrumGrid(project.drums[command.a], static_cast<Grid>(command.value));
            break;
        case CommandType::SetBpm:
            project.bpm = command.value;
            break;
        case CommandType::SetSwing:
            project.swing = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetSound: {
            DrumSound& sound = project.sounds[command.a];
            const auto amount = static_cast<uint8_t>(command.value);
            switch (static_cast<SoundParam>(command.b)) {
                case SoundParam::Tune: sound.tune = amount; break;
                case SoundParam::Decay: sound.decay = amount; break;
                case SoundParam::Tone: sound.tone = amount; break;
                case SoundParam::Level: sound.level = amount; break;
            }
            break;
        }
        case CommandType::SetScenePattern:
            project.scenes[command.a].pattern[command.b] = static_cast<int8_t>(command.c);
            break;
        case CommandType::SetDrumPattern:
            project.drums[command.a] = command.pattern;
            break;
        case CommandType::Play:
        case CommandType::Stop:
        case CommandType::Preview:
            break;
    }
}

}  // namespace gb
