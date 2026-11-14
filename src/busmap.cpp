#include "busmap.h"

#include "joypad.h"

namespace tabulous {
namespace busmap {

Wiring none() {
  Wiring w;
  for (uint8_t &l : w.line) l = kNone;
  return w;
}

Wiring straight() {
  // From the board's routing (tab5-gamepad, pcb/make_board.py): UP on header
  // pin 2, LEFT 4, L 8, RIGHT 10, SELECT 15, DOWN 16, START 19, B 20, R 21,
  // Y 22, A 23, X 26. The numbers below are positions in kHeaderPin.
  Wiring w = none();
  w.line[kUp] = 0;
  w.line[kLeft] = 1;
  w.line[kL] = 2;
  w.line[kRight] = 3;
  w.line[kSelect] = 4;
  w.line[kDown] = 5;
  w.line[kStart] = 6;
  w.line[kB] = 7;
  w.line[kR] = 8;
  w.line[kY] = 9;
  w.line[kA] = 10;
  w.line[kX] = 11;
  return w;
}

bool valid(const Wiring &w) {
  uint16_t seen = 0;
  for (uint8_t l : w.line) {
    if (l == kNone) continue;
    if (l >= kLines) return false;
    if (seen & (1u << l)) return false;
    seen |= (uint16_t)(1u << l);
  }
  return true;
}

int assigned(const Wiring &w) {
  int n = 0;
  for (uint8_t l : w.line) {
    if (l < kLines) n++;
  }
  return n;
}

uint16_t toButtons(uint16_t lines, const Wiring &w) {
  uint16_t out = 0;
  for (int b = 0; b < kButtonCount; b++) {
    const uint8_t l = w.line[b];
    if (l < kLines && (lines >> l) & 1u) out |= (uint16_t)(1u << b);
  }
  return out;
}

namespace {
bool on(uint16_t buttons, Button b) { return (buttons >> b) & 1u; }
}  // namespace

uint8_t toNes(uint16_t buttons) {
  uint8_t out = 0;
  if (on(buttons, kUp)) out |= joypad::kUp;
  if (on(buttons, kDown)) out |= joypad::kDown;
  if (on(buttons, kLeft)) out |= joypad::kLeft;
  if (on(buttons, kRight)) out |= joypad::kRight;
  if (on(buttons, kA) || on(buttons, kX)) out |= joypad::kA;
  if (on(buttons, kB) || on(buttons, kY)) out |= joypad::kB;
  if (on(buttons, kSelect)) out |= joypad::kSelect;
  if (on(buttons, kStart)) out |= joypad::kStart;
  return out;
}

uint8_t toStick(uint16_t buttons) {
  uint8_t out = 0;
  if (on(buttons, kX)) out |= joypad::kUp;
  if (on(buttons, kB)) out |= joypad::kDown;
  if (on(buttons, kY)) out |= joypad::kLeft;
  if (on(buttons, kA)) out |= joypad::kRight;
  return out;
}

int newLine(uint16_t lines, uint16_t before, const Wiring &so_far) {
  uint16_t fresh = (uint16_t)(lines & ~before);
  for (uint8_t l : so_far.line) {
    if (l < kLines) fresh &= (uint16_t)~(1u << l);
  }
  for (int n = 0; n < kLines; n++) {
    if ((fresh >> n) & 1u) return n;
  }
  return -1;
}

const char *name(Button b) {
  static const char *const names[kButtonCount] = {
      "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "SELECT", "START"};
  return b < kButtonCount ? names[b] : "";
}

}  // namespace busmap
}  // namespace tabulous
