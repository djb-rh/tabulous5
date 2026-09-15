#include "uikit.h"

#include <cmath>

#include <vector>

namespace tabulous {
namespace uikit {
namespace {

struct Target {
  Rect rect;
  int action = 0;
  int param = 0;
};

std::vector<Target> g_targets;

uint32_t g_max_repaint_us = 0;
int g_max_repaint_screen = -1;
uint32_t g_repaints = 0;

}  // namespace

namespace {
LovyanGFX *g_surface = nullptr;
}  // namespace

LovyanGFX &gfx() {
  return g_surface ? *g_surface : *(LovyanGFX *)&M5.Display;
}

void setSurface(LovyanGFX *surface) { g_surface = surface; }

void present() {}  // nothing to flush: we draw straight at the panel

namespace {
bool g_touch_override = false;
TouchState g_touch_synth;
}  // namespace

TouchState touch() {
  if (g_touch_override) return g_touch_synth;
  const auto t = M5.Touch.getDetail();
  TouchState s;
  s.down = t.isPressed() || M5.Touch.getCount() > 0;
  s.x = t.x;
  s.y = t.y;
  return s;
}

bool touchIsSynthetic() { return g_touch_override; }

void overrideTouch(bool active, bool down, int x, int y) {
  g_touch_override = active;
  g_touch_synth.down = down;
  g_touch_synth.x = x;
  g_touch_synth.y = y;
}

void clearTargets() { g_targets.clear(); }

void addTarget(const Rect &r, int action, int param) {
  if (r.w <= 0 || r.h <= 0) return;  // clipped away entirely
  g_targets.push_back({r, action, param});
}

void removeTargetsIn(const Rect &r) {
  for (size_t i = 0; i < g_targets.size();) {
    if (g_targets[i].rect.overlaps(r)) g_targets.erase(g_targets.begin() + i);
    else i++;
  }
}

bool findTarget(int x, int y, int *action, int *param) {
  for (auto it = g_targets.rbegin(); it != g_targets.rend(); ++it) {
    if (!it->rect.hit(x, y)) continue;
    if (action) *action = it->action;
    if (param) *param = it->param;
    return true;
  }
  return false;
}

void fillRoundRectFast(int x, int y, int w, int h, int r, uint16_t colour) {
  auto &g = gfx();
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r <= 1) {
    g.fillRect(x, y, w, h, colour);
    return;
  }
  // Middle band full width, then the insets above and below it, then the
  // corners. Every large area goes through the fast rectangle path.
  g.fillRect(x, y + r, w, h - 2 * r, colour);
  g.fillRect(x + r, y, w - 2 * r, r, colour);
  g.fillRect(x + r, y + h - r, w - 2 * r, r, colour);
  g.fillCircle(x + r, y + r, r, colour);
  g.fillCircle(x + w - r - 1, y + r, r, colour);
  g.fillCircle(x + r, y + h - r - 1, r, colour);
  g.fillCircle(x + w - r - 1, y + h - r - 1, r, colour);
}

namespace {

uint32_t lerp888(uint32_t a, uint32_t b, int num, int den) {
  if (den <= 0) return a;
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const int ca = (int)((a >> shift) & 0xFF);
    const int cb = (int)((b >> shift) & 0xFF);
    out |= (uint32_t)(ca + (cb - ca) * num / den) << shift;
  }
  return out;
}

// Row inset for a rounded rectangle: 0 through the straight middle, growing to
// r at the extreme rows of each cap. Only the few shelf rows need it.
int cornerInset(int row, int h, int r) {
  int d = 0;
  if (row < r) d = r - row;
  else if (row >= h - r) d = r - (h - 1 - row);
  else return 0;
  const int inside = r * r - d * d;
  if (inside <= 0) return r;
  return r - (int)lroundf(sqrtf((float)inside));
}

uint16_t to565(uint32_t rgb888) {
  return gfx().color565((rgb888 >> 16) & 0xFF, (rgb888 >> 8) & 0xFF,
                        rgb888 & 0xFF);
}

// Cap of a rounded rect: the straight span plus its two corner circles.
void fillCap(int x, int y, int w, int h, int r, uint16_t colour, bool top) {
  auto &g = gfx();
  g.fillRect(x + r, top ? y : y + h - r, w - 2 * r, r, colour);
  const int cy = top ? y + r : y + h - r - 1;
  g.fillCircle(x + r, cy, r, colour);
  g.fillCircle(x + w - r - 1, cy, r, colour);
}

}  // namespace

void fillRoundRectShaded(int x, int y, int w, int h, int r, uint32_t top,
                         uint32_t bottom, uint32_t shelf, int shelf_h) {
  auto &g = gfx();
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (w <= 0 || h <= 0) return;
  if (r <= 1) {
    g.fillRect(x, y, w, h, to565(top));
    return;
  }

  g.startWrite();

  // The caps get one flat colour each rather than a colour per row. Over a
  // corner radius the gradient moves well under one RGB565 step, so per-row
  // drawing there buys nothing visible and costs a near-full-width fill for
  // every row of the curve — which measured 10.4 ms a blob against 3 ms flat.
  //
  // Both caps sample the gradient, so the shape is continuous top to bottom.
  // Tying the shelf to the bottom cap instead was tried and looked wrong: the
  // radius then sets the shelf's thickness, which made it a 16 px band with a
  // hard cliff into it rather than an edge.
  fillCap(x, y, w, h, r, to565(lerp888(top, bottom, r / 2, h - 1)), true);
  fillCap(x, y, w, h, r, to565(lerp888(top, bottom, h - 1 - r / 2, h - 1)),
          false);

  // The straight middle, one fill per distinct panel colour. A ~20% darkening
  // over 120 px passes through about seven values in RGB565, so this emits
  // about seven fills however finely the gradient is interpolated.
  int run_start = r;
  uint16_t run_colour = 0;
  bool have_run = false;
  for (int row = r; row <= h - r; row++) {
    uint16_t colour = 0;
    if (row < h - r) colour = to565(lerp888(top, bottom, row, h - 1));
    if (row == h - r || !have_run || colour != run_colour) {
      if (have_run && row > run_start) {
        g.fillRect(x, y + run_start, w, row - run_start, run_colour);
      }
      run_start = row;
      run_colour = colour;
      have_run = true;
    }
  }

  // The shelf last, over the finished shape: a few rows hugging the bottom
  // edge, following the curve. Per-row here because the inset changes on every
  // one of them — but there are only a handful, so it costs nothing.
  if (shelf_h > 0) {
    const uint16_t c = to565(shelf);
    for (int row = h - shelf_h; row < h; row++) {
      if (row < 0) continue;
      const int inset = cornerInset(row, h, r);
      if (w - 2 * inset > 0) {
        g.fillRect(x + inset, y + row, w - 2 * inset, 1, c);
      }
    }
  }
  g.endWrite();
}

void drawButton(const Rect &r, const char *label, uint16_t bg, uint16_t fg,
                const lgfx::IFont *font) {  // default arg is in the header
  auto &g = gfx();
  fillRoundRectFast(r.x, r.y, r.w, r.h, 16, bg);
  g.setFont(font);
  g.setTextSize(1);
  g.setTextDatum(middle_center);
  g.setTextColor(fg);
  g.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

void drawArrowButton(const Rect &r, bool up, uint16_t bg, uint16_t fg) {
  auto &g = gfx();
  fillRoundRectFast(r.x, r.y, r.w, r.h, 16, bg);

  // Wider than tall, so it reads as a direction rather than a spike. Sized
  // against the rail rather than fixed, so it keeps its proportions if the
  // list geometry changes.
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const int hw = r.w / 3;
  const int hh = hw * 2 / 3;
  const int tip = up ? cy - hh : cy + hh;
  const int base = up ? cy + hh : cy - hh;
  g.fillTriangle(cx, tip, cx - hw, base, cx + hw, base, fg);
}

void drawLabel(const char *text, int x, int y, uint16_t color,
               const lgfx::IFont *font, textdatum_t datum, uint8_t size) {
  auto &g = gfx();
  g.setFont(font);
  g.setTextSize(size);
  g.setTextDatum(datum);
  g.setTextColor(color);
  g.drawString(text, x, y);
  g.setTextSize(1);
}

void noteRepaint(uint32_t took_us, int screen) {
  g_repaints++;
  if (took_us > g_max_repaint_us) {
    g_max_repaint_us = took_us;
    g_max_repaint_screen = screen;
  }
}

uint32_t worstRepaintUs() { return g_max_repaint_us; }
int worstRepaintScreen() { return g_max_repaint_screen; }
uint32_t repaintCount() { return g_repaints; }
size_t targetCount() { return g_targets.size(); }

}  // namespace uikit
}  // namespace tabulous
