// Which gamepad button is which NES button.
//
// USB pads number their buttons, and no two makers agree on which number is
// "A". The default here is the GP100 (an SNES-shaped pad: X=1 A=2 B=3 Y=4
// L=5 R=6 Select=9 Start=10), and a different pad is taught in the Gamepad
// Test screen, which stores the result. Arduino-free so it tests on the host.
#pragma once

#include <cstdint>

namespace tabulous {
namespace padmap {

// Button numbers are 1-based, as pads label them; 0 means unassigned. Two
// slots each for A and B: on an SNES layout Y and X make natural extra B and
// A buttons, and a NES game cannot tell the difference.
struct Map {
  uint8_t a = 2, a2 = 1;
  uint8_t b = 3, b2 = 4;
  uint8_t select = 9;
  uint8_t start = 10;
};

// `down` has bit n set for button n+1; x/y are -1/0/+1. Returns the NES
// controller byte in joypad.h's bit order.
uint8_t toNes(uint32_t down, int8_t x, int8_t y, const Map &map);

// The lowest-numbered button that is down and was not down before, or 0.
// The mapping screen asks for one button at a time and takes the first new
// press.
uint8_t newPress(uint32_t down, uint32_t before);

}  // namespace padmap
}  // namespace tabulous
