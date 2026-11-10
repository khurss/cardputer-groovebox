#pragma once
#include <cstdint>

#include "gb/commands.h"
namespace gb {
namespace key {
constexpr char Fn = 1;
constexpr char Shift = 2;
constexpr char Ctrl = 3;
constexpr char Opt = 4;
constexpr char Alt = 5;
constexpr char Backspace = 8;
constexpr char Tab = 9;
constexpr char Enter = 10;
constexpr char Esc = 27;
}  // namespace key

namespace color {
constexpr uint16_t Background = 0x0000;
constexpr uint16_t Panel = 0x0108;
constexpr uint16_t StepOff = 0x4208;
constexpr uint16_t StepOutside = 0x2108;
constexpr uint16_t StepOn = 0x07ff;
constexpr uint16_t StepAccent = 0xffc0;
constexpr uint16_t Playhead = 0xf81f;
constexpr uint16_t LaneSelected = 0x2252;
constexpr uint16_t Text = 0xffff;
constexpr uint16_t Muted = 0xa536;
constexpr uint16_t Warning = 0xfd20;
}  // namespace color

constexpr uint8_t kNormalVelocity = 100;
constexpr uint8_t kAccentVelocity = 127;
constexpr uint8_t kSoundStep = 8;

class Canvas {
   public:
    virtual ~Canvas() = default;
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void text(int x, int y, const char* value, uint16_t color) = 0;
};

// The sink must accept every command: the interface applies edits to its own copy first,
// so a dropped command would leave the interface and audio copies of the project different.
using CommandSink = bool (*)(void* context, const Command& command);

}  // namespace gb
