#pragma once
namespace gb {
// One shared table (internal RAM on ESP32). Linear interpolation replaces per-sample libm sine calls.
extern const float kSineTable[513];
inline float sineLookup(float phase) {
    const float position = phase * 512.0f;
    const int index = static_cast<int>(position);
    return kSineTable[index] + (kSineTable[index + 1] - kSineTable[index]) * (position - index);
}
}  // namespace gb
