#pragma once
#include <cstdint>
namespace gb {
enum class MenuOp : uint8_t {
    Projects, Help, KeyHelp, Performance, PianoRoll, Preset, Layout, Root, Scale, Screen, Play, Record, Shortcut, Clear, SoundEditor, Browse, Waveform, Steps,
    SaveSlice, Chop, DeleteSlice, MarkStart, MarkEnd, ResetRange,
    Master, Tempo, Swing, Length, Grid, Pattern, Track, Octave, Quantize, Fit,
    SynthParameter, ArpParameter, Scene, SceneTrack, ScenePattern,
    SongRow, SongScene, SongRepeats, InsertScene, Strip, Level, Mute, Solo, Pad, Slot, Parts
};
struct MenuItem {
    const char* label;
    const char* hint;
    MenuOp op;
    uint8_t argument = 0;
};
}
