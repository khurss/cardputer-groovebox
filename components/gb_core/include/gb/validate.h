#pragma once

#include "gb/model.h"

namespace gb {
bool isValidSampleClip(const SampleClip& clip);
bool isValidSamplePattern(const SamplePattern& pattern);

// True when every field is in range, so the audio task can use the project safely.
bool isValidProject(const Project& project);
bool isValidNote(const Note& note);
bool isValidInstrument(const InstrumentPattern& pattern);
bool isValidDrums(const DrumPattern& pattern);

// Sets a new grid and keeps each step at its time, dropping steps that have no place on it.
void changeDrumGrid(DrumPattern& pattern, Grid grid);

}  // namespace gb
