#pragma once
#include "gb/model.h"
namespace gb {
constexpr unsigned kDemoCount=9;
const char* demoName(unsigned index);
// Deterministic original compositions, no WAV dependencies. Master starts at 5%.
bool makeDemoProject(unsigned index, Project& project);
}
