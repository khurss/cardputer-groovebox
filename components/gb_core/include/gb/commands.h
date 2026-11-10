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
    AddNote,
    SetNote,
    DeleteNote,
    ClearNotes,  // a track (0..3), b pattern, c note index
    SetInstrumentLength,
    SetInstrumentGrid,
    SetSynthParam,      // a track, b SynthParam, value
    SetRecordQuantize,  // a track, value bool
    BeginInstrumentEdit,
    StageInstrumentNote,
    CommitInstrumentEdit,
    CancelInstrumentEdit,
    LiveNoteOn,
    LiveNoteOff,  // a track (0..3), b pitch, c physical key token, value velocity
    ReleaseLive,
    SetRecording,  // a track, b pattern, value armed
    AcknowledgeRecording,
    PlayScene,
    PlaySong,  // a index
    SetScene,  // a scene, scenePatterns payload
    InsertSongEntry,
    SetSongEntry,
    DeleteSongEntry,  // a entry, b scene, value repeats
    SetMasterVolume,
    SetTrackVolume,
    SetTrackMute,
    SetTrackSolo,    // a track, value percent/bool
    SetArpParam,     // a instrument, b ArpParam, value
    SetOutputLimit,  // transient diagnostic ceiling, percent; never serialized
    SetSampleClip,   // a slot, sampleClip payload (storage confirms decoded audio before committing)
    SetSamplePattern, // a pattern, samplePattern payload
    PreviewSample,   // a slot, value velocity
    SetSampleData,   // a slot, transient memory-mapped PCM, never serialized
    SilenceSamples,
    SetKeyboard, // a track, b layout/root/scale, value
    SetEqualizer, // equalizer payload; atomic edit of gains and bypass state


};

enum class SoundParam : uint8_t { Tune, Decay, Tone, Level };

struct Command {
    CommandType type = CommandType::Stop;
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t c = 0;
    uint16_t value = 0;
    union {
        DrumPattern pattern{};
        Note note;
        int8_t scenePatterns[kTracks];
        SampleClip sampleClip;
        SamplePattern samplePattern;
        EqualizerSettings equalizer;
        struct { const int16_t* data; uint32_t frames; } sampleData;
    };
};
static_assert(sizeof(Command) <= 152, "Commands must not carry an instrument pattern");

// Gives a blank project its playable starting point.
void initProject(Project& project);

// True when every index and value in the command is in range. Invalid commands must be ignored.
bool isValidCommand(const Command& command);

// Applies a command that edits the project; transport commands leave it unchanged.
// Invalid commands are ignored.
void applyEdit(Project& project, const Command& command);

}  // namespace gb
