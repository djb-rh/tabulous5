#include "confetti.h"

#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace confetti {

using namespace theme;

namespace {

constexpr int kCount = 40;
constexpr uint32_t kLifeMs = 2600;
constexpr int kSub = 16;  // fixed-point subpixels, so slow drift doesn't round to zero

// The game colours, so the celebration belongs to this device rather than
// being generic party colours.
constexpr uint32_t kColours[] = {0xE4572E, 0x1D4ED8, 0x4C9F70,
                                 0x9B5DE5, 0x0F8B8D, 0xFFC53D};
constexpr int kColourCount = sizeof(kColours) / sizeof(kColours[0]);

struct Piece {
  int32_t x, y;    // subpixels
  int32_t vx, vy;  // subpixels per frame
  int16_t px, py;  // where it was actually drawn last, in pixels
  uint8_t w, h;
  uint16_t colour;
  bool drawn;
};

Piece g_piece[kCount];
uint32_t g_started = 0;
bool g_active = false;
int g_floor = kH;
uint32_t g_rng = 1;

uint32_t rand32() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

int range(int lo, int hi) { return lo + (int)(rand32() % (uint32_t)(hi - lo + 1)); }

}  // namespace

void start(uint32_t now_ms, int floor_y) {
  g_started = now_ms;
  g_active = true;
  g_floor = floor_y;
  g_rng = now_ms | 1u;

  for (int i = 0; i < kCount; i++) {
    Piece &p = g_piece[i];
    p.x = range(kMargin, kW - kMargin) * kSub;
    // Staggered above the top edge so they arrive as a shower, not a curtain.
    p.y = range(-420, -20) * kSub;
    p.vx = range(-14, 14);
    p.vy = range(24, 62);
    p.w = (uint8_t)range(8, 16);
    p.h = (uint8_t)range(5, 10);
    p.colour = rgb(kColours[rand32() % kColourCount]);
    p.drawn = false;
  }
}

bool active(uint32_t now_ms) {
  if (!g_active) return false;
  if (now_ms - g_started > kLifeMs) g_active = false;
  return g_active;
}

void stop() { g_active = false; }

void erase() {
  auto &g = uikit::gfx();
  g.startWrite();
  for (int i = 0; i < kCount; i++) {
    Piece &p = g_piece[i];
    if (!p.drawn) continue;
    g.fillRect(p.px, p.py, p.w, p.h, kBg);
    p.drawn = false;
  }
  g.endWrite();
}

void step(uint32_t) {
  for (int i = 0; i < kCount; i++) {
    Piece &p = g_piece[i];
    p.vy += 3;                        // gravity
    p.vx = p.vx * 63 / 64;            // a little drag, so they fall straighter
    p.x += p.vx;
    p.y += p.vy;
  }
}

void draw() {
  auto &g = uikit::gfx();
  g.startWrite();
  for (int i = 0; i < kCount; i++) {
    Piece &p = g_piece[i];
    const int x = p.x / kSub;
    const int y = p.y / kSub;
    // Off the bottom or off the sides: leave it undrawn rather than clamping,
    // so pieces exit instead of piling up along an edge.
    if (y + p.h < 0 || y > g_floor || x + p.w < 0 || x > kW) continue;
    p.px = (int16_t)x;
    p.py = (int16_t)y;
    p.drawn = true;
    g.fillRect(x, y, p.w, p.h, p.colour);
  }
  g.endWrite();
}

}  // namespace confetti
}  // namespace tabulous
