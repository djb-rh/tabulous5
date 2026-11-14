// The built-in gamepad: twelve switches on the M5-Bus header.
//
// The gamepad housing puts an SNES pad's worth of buttons on the back of the
// console, each one a plain switch from a header GPIO to ground. Twelve of
// the header's pins are free for that (the rest are power, the internal I2C
// bus, the console UART, the SPI bus and a boot strap), and the pad has
// exactly twelve buttons.
//
// Which switch is on which pin is not fixed here. The pad's board was laid
// out pin for pin (straight() below), but a board wired to the header by
// hand goes wherever the wires went, so the wiring is taught: the Gamepad
// Test screen asks for each button in turn and records the line that moved.
//
// This file is the arithmetic only, with no Arduino in it, so it tests on
// the host. buspad.h reads the pins.
#pragma once

#include <cstdint>

namespace tabulous {
namespace busmap {

enum Button : uint8_t {
  kUp, kDown, kLeft, kRight, kA, kB, kX, kY, kL, kR, kSelect, kStart, kButtonCount
};

// The twelve lines, in header-pin order.
constexpr int kLines = 12;
constexpr uint8_t kGpio[kLines] = {16, 17, 45, 52, 7, 6, 3, 4, 2, 48, 47, 51};
constexpr uint8_t kHeaderPin[kLines] = {2, 4, 8, 10, 15, 16, 19, 20, 21, 22, 23, 26};

constexpr uint8_t kNone = 0xFF;

// line[b] is the line button b is on, or kNone.
struct Wiring {
  uint8_t line[kButtonCount];
};

Wiring none();
// The pad's own board on a straight ribbon: header pin n to socket pin n.
Wiring straight();
// No line is used twice and none is out of range. A button may be left
// without a line (a joint that did not take): the rest of the pad works.
bool valid(const Wiring &w);
// How many buttons have a line.
int assigned(const Wiring &w);

// `lines` has bit n set while line n is held to ground. Returns one bit per
// Button.
uint16_t toButtons(uint16_t lines, const Wiring &w);

// The NES byte in joypad.h's bit order. X and Y double as A and B, as they
// do on a USB pad: a NES game cannot tell and the thumb has four buttons.
uint8_t toNes(uint16_t buttons);
// The face diamond as a compass (X up, B down, Y left, A right), for the
// arcade boards that have a stick and nothing else.
uint8_t toStick(uint16_t buttons);

// The lowest line that is down now, was not before, and is not already in
// `so_far`; -1 if there is none. The teaching flow takes one at a time.
int newLine(uint16_t lines, uint16_t before, const Wiring &so_far);

const char *name(Button b);

}  // namespace busmap
}  // namespace tabulous
