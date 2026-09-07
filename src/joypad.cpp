#include "joypad.h"

namespace tabulous {
namespace joypad {

Rect dpadArea() { return Rect{kDpadX, kDpadY, kDpadSize, kDpadSize}; }

Rect dpadCell(int col, int row) {
  return Rect{kDpadX + col * kDpadCell, kDpadY + row * kDpadCell, kDpadCell,
              kDpadCell};
}

// A and B sit on a diagonal, B low-left and A high-right, which is how they
// are arranged on the original pad — the thumb rocks between them rather than
// travelling sideways.
Circle buttonA() { return Circle{1176, 300, 76}; }
Circle buttonB() { return Circle{1000, 380, 76}; }

Rect buttonSelect() { return Rect{930, 556, 150, 66}; }
Rect buttonStart() { return Rect{1104, 556, 150, 66}; }

// Above the D-pad, clear of every play control, so leaving cannot be hit by a
// thumb that slides off the pad.
Rect menuButton() { return Rect{24, 24, 170, 64}; }

uint8_t hitTest(int x, int y) {
  if (buttonA().contains(x, y)) return kA;
  if (buttonB().contains(x, y)) return kB;
  if (buttonSelect().contains(x, y)) return kSelect;
  if (buttonStart().contains(x, y)) return kStart;

  if (dpadArea().contains(x, y)) {
    const int col = (x - kDpadX) / kDpadCell;
    const int row = (y - kDpadY) / kDpadCell;
    uint8_t out = 0;
    if (row == 0) out |= kUp;
    if (row == 2) out |= kDown;
    if (col == 0) out |= kLeft;
    if (col == 2) out |= kRight;
    return out;  // centre cell falls through as 0, which is the neutral zone
  }
  return 0;
}

const char *name(uint8_t bit) {
  switch (bit) {
    case kA: return "A";
    case kB: return "B";
    case kSelect: return "SELECT";
    case kStart: return "START";
    case kUp: return "UP";
    case kDown: return "DOWN";
    case kLeft: return "LEFT";
    case kRight: return "RIGHT";
    default: return "";
  }
}

}  // namespace joypad
}  // namespace tabulous
