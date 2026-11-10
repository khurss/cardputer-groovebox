#pragma once

#include "gb/model.h"

namespace gb {

// True when every field is in range, so the audio task can use the project safely.
bool isValidProject(const Project& project);

// Sets a new grid and keeps each step at its time, dropping steps that have no place on it.
void changeDrumGrid(DrumPattern& pattern, Grid grid);

}  // namespace gb
