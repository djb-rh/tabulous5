// An on-screen NES controller.
//
// Scaffolding, deliberately: it exists so the emulator can be driven and felt
// before any hardware gamepad is wired to the 30-pin header. When real buttons
// arrive the video goes to 3x full height and this whole surface is deleted.
//
// The layout is built around one physical fact. The Tab5's panel is ~294 PPI,
// so 1280x720 is only 111 x 62 mm. At 3x the NES picture (768 px) leaves side
// columns 22 mm wide, which gives a D-pad arm of 6.9 mm — below the 80 px this
// project already established as the usable floor, and far below a thumb's
// 10-14 mm contact patch. At 2x the columns are 33 mm and the arms 10.5 mm,
// which is comfortable. The picture is then about the size of an original Game
// Boy's screen. That trade is why this is 2x.
//
// No Arduino headers: the hit geometry is the part most likely to be subtly
// wrong (diagonals especially), so it is unit-tested on the host.
#pragma once

#include <cstdint>

namespace tabulous {
namespace joypad {

// Bit order as the NES shift register reports it, so `state()` can be handed
// to a core unchanged rather than translated at the boundary.
enum : uint8_t {
  kA      = 1u << 0,
  kB      = 1u << 1,
  kSelect = 1u << 2,
  kStart  = 1u << 3,
  kUp     = 1u << 4,
  kDown   = 1u << 5,
  kLeft   = 1u << 6,
  kRight  = 1u << 7,
};

constexpr int kButtonCount = 8;

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

struct Circle {
  int cx = 0, cy = 0, r = 0;
  bool contains(int px, int py) const {
    const int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
  }
};

// --- layout, in panel pixels -------------------------------------------
//
// The picture is padded to 520 rather than its natural 512. Both 512 and 768
// give a row stride that is a multiple of 512 bytes, which collides in the
// cache and measured 190 ms a frame against 20 ms for a width 8 px either
// side of it. The padding is black margin and costs nothing.
constexpr int kPanelW = 1280, kPanelH = 720;
constexpr int kNesW = 256, kNesH = 240;
constexpr int kScale = 2;
constexpr int kVideoW = kNesW * kScale;          // 512 drawn...
constexpr int kVideoStride = kVideoW + 8;        // ...520 pushed
constexpr int kVideoH = kNesH * kScale;
constexpr int kVideoX = (kPanelW - kVideoW) / 2;
constexpr int kVideoY = (kPanelH - kVideoH) / 2;

// The D-pad is a 3x3 grid inside one square: corners give diagonals, which
// several NES games need and which four separate rectangles cannot produce —
// a touch between two of them would land on neither.
constexpr int kDpadSize = 360;
constexpr int kDpadCell = kDpadSize / 3;
constexpr int kDpadX = 12;
constexpr int kDpadY = 220;

Rect dpadArea();
Rect dpadCell(int col, int row);  // 0..2 each; the centre cell is neutral

Circle buttonA();
Circle buttonB();
Rect buttonSelect();
Rect buttonStart();
Rect menuButton();

// Which buttons a single touch point presses. Returns 0 for a miss, and may
// return two bits for a D-pad corner.
uint8_t hitTest(int x, int y);

// Human-readable name for one bit, for the harness readout. Returns "" if
// `bit` is not exactly one button.
const char *name(uint8_t bit);

}  // namespace joypad
}  // namespace tabulous
