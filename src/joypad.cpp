#include "joypad.h"

namespace tabulous {
namespace joypad {
namespace {

bool g_portrait = false;
int g_w = kPanelW, g_h = kPanelH;
int g_top = 0;  // the first row below the picture

// Worked out once per layout change: the pad on the left of the band under the
// picture, the two face buttons on the right, the small pair between them.
struct Upright {
  int dpad_x, dpad_y, dpad_size, dpad_cell;
  int a_cx, a_cy, b_cx, b_cy, r;
  Rect select, start;
};
Upright g_u;

void layoutUpright() {
  const int band_h = g_h - g_top;
  int size = band_h - 150;                           // room for the small pair
  if (size > (g_w / 2) - 30) size = (g_w / 2) - 30;  // never meet in the middle
  if (size < 180) size = 180;
  size -= size % 3;                                  // three whole cells

  g_u.dpad_size = size;
  g_u.dpad_cell = size / 3;
  g_u.dpad_x = 20;
  g_u.dpad_y = g_top + 20;

  g_u.r = size / 5;
  const int right = g_w - 24 - g_u.r;
  g_u.a_cx = right;
  g_u.a_cy = g_u.dpad_y + g_u.r + 10;
  g_u.b_cx = right - (int)(g_u.r * 2.1f);
  g_u.b_cy = g_u.a_cy + (int)(g_u.r * 1.4f);

  const int bw = 150, bh = 66;
  const int by = g_h - bh - 24;
  g_u.select = Rect{g_w / 2 - bw - 10, by, bw, bh};
  g_u.start = Rect{g_w / 2 + 10, by, bw, bh};
}

}  // namespace

void setLayout(bool portrait, int screen_w, int screen_h, int controls_top) {
  g_portrait = portrait;
  g_w = screen_w > 0 ? screen_w : (portrait ? kPanelH : kPanelW);
  g_h = screen_h > 0 ? screen_h : (portrait ? kPanelW : kPanelH);
  g_top = controls_top;
  if (portrait) layoutUpright();
}

bool portrait() { return g_portrait; }

Rect dpadArea() {
  if (g_portrait) return Rect{g_u.dpad_x, g_u.dpad_y, g_u.dpad_size, g_u.dpad_size};
  return Rect{kDpadX, kDpadY, kDpadSize, kDpadSize};
}

Rect dpadCell(int col, int row) {
  if (g_portrait) {
    return Rect{g_u.dpad_x + col * g_u.dpad_cell, g_u.dpad_y + row * g_u.dpad_cell,
                g_u.dpad_cell, g_u.dpad_cell};
  }
  return Rect{kDpadX + col * kDpadCell, kDpadY + row * kDpadCell, kDpadCell, kDpadCell};
}

Circle buttonA() {
  if (g_portrait) return Circle{g_u.a_cx, g_u.a_cy, g_u.r};
  return Circle{1176, 300, 76};
}

Circle buttonB() {
  if (g_portrait) return Circle{g_u.b_cx, g_u.b_cy, g_u.r};
  return Circle{1000, 380, 76};
}

Rect buttonSelect() {
  if (g_portrait) return g_u.select;
  return Rect{930, 556, 150, 66};
}

Rect buttonStart() {
  if (g_portrait) return g_u.start;
  return Rect{1104, 556, 150, 66};
}

namespace {
int g_menu_w = 170;
}  // namespace

void setMenuWidth(int w) { g_menu_w = w < 96 ? 96 : (w > 170 ? 170 : w); }

Rect menuButton() { return Rect{24, 24, g_menu_w, 64}; }

uint8_t hitTest(int x, int y) {
  if (buttonA().contains(x, y)) return kA;
  if (buttonB().contains(x, y)) return kB;
  if (buttonSelect().contains(x, y)) return kSelect;
  if (buttonStart().contains(x, y)) return kStart;
  const Rect pad = dpadArea();
  if (pad.contains(x, y)) {
    const int cell = pad.w / 3;
    const int col = (x - pad.x) / cell;
    const int row = (y - pad.y) / cell;
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
