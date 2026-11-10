#include "gb/commands.h"

#include "gb/validate.h"

namespace gb {

void initProject(Project& project) { project.scenes[0].pattern[0] = 0; }

void applyEdit(Project& project, const Command& command) {
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
