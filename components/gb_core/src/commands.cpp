#include "gb/commands.h"

#include "gb/validate.h"

namespace gb {

void initProject(Project& project) { project.scenes[0].pattern[0] = 0; }

bool isValidCommand(const Command& command) {
    switch (command.type) {
        case CommandType::SetSampleClip:
            return command.a < kSampleSlots && isValidSampleClip(command.sampleClip);
        case CommandType::SetSamplePattern:
            return command.a < kPatternsPerTrack && isValidSamplePattern(command.samplePattern);
        case CommandType::PreviewSample:
            return command.a < kSampleSlots && command.value <= 127;
        case CommandType::SetSampleData:
            return command.a < kSampleSlots && ((command.sampleData.frames == 0 && !command.sampleData.data) ||
                (command.sampleData.data && command.sampleData.frames > 0 && command.sampleData.frames <= 2097152));
        case CommandType::SilenceSamples:
            return true;
        case CommandType::PlayScene:
            return command.a < kScenes;
        case CommandType::PlaySong:
            return command.a < kSongEntries;
        case CommandType::SetScene:
            if (command.a >= kScenes)
                return false;
            for (int8_t pattern : command.scenePatterns)
                if (pattern < -1 || pattern >= kPatternsPerTrack)
                    return false;
            return true;
        case CommandType::InsertSongEntry:
        case CommandType::SetSongEntry:
            return command.a < kSongEntries && command.b < kScenes && command.value > 0 && command.value <= 255;
        case CommandType::DeleteSongEntry:
            return command.a < kSongEntries;
        case CommandType::SetMasterVolume:
        case CommandType::SetOutputLimit:
            return command.value <= 100;
        case CommandType::SetTrackVolume:
            return command.a < kTracks && command.value <= 100;
        case CommandType::SetTrackMute:
        case CommandType::SetTrackSolo:
            return command.a < kTracks && command.value <= 1;
        case CommandType::SetArpParam:
            if (command.a >= kInstrumentTracks)
                return false;
            switch (static_cast<ArpParam>(command.b)) {
                case ArpParam::Mode:
                    return command.value <= 3;
                case ArpParam::Rate:
                    return command.value <= 5;
                case ArpParam::Octaves:
                    return command.value >= 1 && command.value <= 3;
                case ArpParam::Gate:
                    return command.value >= 10 && command.value <= 100;
            }
            return false;
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
            return command.a < kScenes && command.b < kTracks && (command.c < kPatternsPerTrack || command.c == 255);
        case CommandType::SetDrumPattern:
            return command.a < kPatternsPerTrack && isValidDrums(command.pattern);
        case CommandType::AddNote:
        case CommandType::SetNote:
        case CommandType::StageInstrumentNote:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack && command.c < kMaxNotes &&
                   isValidNote(command.note);
        case CommandType::DeleteNote:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack && command.c < kMaxNotes;
        case CommandType::ClearNotes:
        case CommandType::CommitInstrumentEdit:
        case CommandType::CancelInstrumentEdit:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack;
        case CommandType::BeginInstrumentEdit:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack &&
                   (command.c == 4 || command.c == 8 || command.c == 16) && command.value <= 5;
        case CommandType::SetInstrumentLength:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack &&
                   (command.value == 4 || command.value == 8 || command.value == 16);
        case CommandType::SetInstrumentGrid:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack && command.value <= 5;
        case CommandType::SetKeyboard:
            return command.a < kInstrumentTracks && command.b < 3 && command.value <= (command.b == 0 ? 1 : command.b == 1 ? 11 : 3);
        case CommandType::SetSynthParam:
            return command.a < kInstrumentTracks && command.b <= static_cast<uint8_t>(SynthParam::Level) &&
                   command.value <= (command.b == 0 ? 3 : 127);
        case CommandType::SetRecordQuantize:
            return command.a < kInstrumentTracks && command.value <= 1;
        case CommandType::LiveNoteOn:
        case CommandType::LiveNoteOff:
            return command.a < kInstrumentTracks && command.b <= 127 && command.c < 128 && command.value <= 127;
        case CommandType::ReleaseLive:
        case CommandType::AcknowledgeRecording:
            return true;
        case CommandType::SetRecording:
            return command.a < kInstrumentTracks && command.b < kPatternsPerTrack && command.value <= 1;
        case CommandType::Play:
        case CommandType::Stop:
            return true;
        case CommandType::Preview:
            return command.a < kDrumLanes && command.b <= 127;
    }
    return false;
}

void applyEdit(Project& project, const Command& command) {
    if (!isValidCommand(command))
        return;
    switch (command.type) {
        case CommandType::SetSampleClip:
            project.samples[command.a] = command.sampleClip;
            break;
        case CommandType::SetSamplePattern:
            project.samplePatterns[command.a] = command.samplePattern;
            break;
        case CommandType::SetScene:
            for (int t = 0; t < kTracks; ++t)
                project.scenes[command.a].pattern[t] = command.scenePatterns[t];
            break;
        case CommandType::InsertSongEntry:
            if (project.songLength >= kSongEntries || command.a > project.songLength)
                break;
            for (int i = project.songLength; i > command.a; --i)
                project.song[i] = project.song[i - 1];
            ++project.songLength;
            project.song[command.a] = {command.b, static_cast<uint8_t>(command.value)};
            break;
        case CommandType::SetSongEntry:
            if (command.a < project.songLength)
                project.song[command.a] = {command.b, static_cast<uint8_t>(command.value)};
            break;
        case CommandType::DeleteSongEntry:
            if (command.a >= project.songLength)
                break;
            for (int i = command.a + 1; i < project.songLength; ++i)
                project.song[i - 1] = project.song[i];
            project.song[--project.songLength] = SongEntry{};
            break;
        case CommandType::SetMasterVolume:
            project.masterVolume = command.value;
            break;
        case CommandType::SetTrackVolume:
            project.mixer[command.a].volume = command.value;
            break;
        case CommandType::SetTrackMute:
            project.mixer[command.a].mute = command.value != 0;
            break;
        case CommandType::SetTrackSolo:
            project.mixer[command.a].solo = command.value != 0;
            break;
        case CommandType::SetArpParam: {
            auto& a = project.arps[command.a];
            switch (static_cast<ArpParam>(command.b)) {
                case ArpParam::Mode:
                    a.mode = static_cast<ArpMode>(command.value);
                    break;
                case ArpParam::Rate:
                    a.rate = static_cast<Grid>(command.value);
                    break;
                case ArpParam::Octaves:
                    a.octaves = command.value;
                    break;
                case ArpParam::Gate:
                    a.gate = command.value;
                    break;
            }
            break;
        }

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
                case SoundParam::Tune:
                    sound.tune = amount;
                    break;
                case SoundParam::Decay:
                    sound.decay = amount;
                    break;
                case SoundParam::Tone:
                    sound.tone = amount;
                    break;
                case SoundParam::Level:
                    sound.level = amount;
                    break;
            }
            break;
        }
        case CommandType::SetScenePattern:
            project.scenes[command.a].pattern[command.b] = static_cast<int8_t>(command.c);
            break;
        case CommandType::SetDrumPattern:
            project.drums[command.a] = command.pattern;
            break;
        case CommandType::AddNote: {
            auto& p = project.instruments[command.a][command.b];
            if (p.noteCount < kMaxNotes)
                p.notes[p.noteCount++] = command.note;
            break;
        }
        case CommandType::SetNote: {
            auto& p = project.instruments[command.a][command.b];
            if (command.c < p.noteCount)
                p.notes[command.c] = command.note;
            break;
        }
        case CommandType::DeleteNote: {
            auto& p = project.instruments[command.a][command.b];
            if (command.c >= p.noteCount)
                break;
            for (uint8_t i = command.c + 1; i < p.noteCount; ++i)
                p.notes[i - 1] = p.notes[i];
            p.notes[--p.noteCount] = Note{};
            break;
        }
        case CommandType::ClearNotes:
            project.instruments[command.a][command.b].noteCount = 0;
            break;
        case CommandType::SetInstrumentLength:
            project.instruments[command.a][command.b].length = static_cast<uint8_t>(command.value);
            break;
        case CommandType::SetInstrumentGrid:
            project.instruments[command.a][command.b].grid = static_cast<Grid>(command.value);
            break;
        case CommandType::SetRecordQuantize:
            project.recordQuantize[command.a] = command.value != 0;
            break;
        case CommandType::SetKeyboard:
            if (command.b == 0) project.keyboardLayout[command.a] = command.value;
            else if (command.b == 1) project.keyboardRoot[command.a] = command.value;
            else project.keyboardScale[command.a] = command.value;
            break;
        case CommandType::SetSynthParam: {
            auto& s = project.synths[command.a];
            const auto v = static_cast<uint8_t>(command.value);
            switch (static_cast<SynthParam>(command.b)) {
                case SynthParam::Wave:
                    s.wave = static_cast<Waveform>(v);
                    break;
                case SynthParam::Attack:
                    s.attack = v;
                    break;
                case SynthParam::Decay:
                    s.decay = v;
                    break;
                case SynthParam::Sustain:
                    s.sustain = v;
                    break;
                case SynthParam::Release:
                    s.release = v;
                    break;
                case SynthParam::Cutoff:
                    s.cutoff = v;
                    break;
                case SynthParam::Resonance:
                    s.resonance = v;
                    break;
                case SynthParam::Glide:
                    s.glide = v;
                    break;
                case SynthParam::Level:
                    s.level = v;
                    break;
            }
            break;
        }
        case CommandType::Play:
        case CommandType::Stop:
        case CommandType::Preview:
        default:
            break;
    }
}

}  // namespace gb
