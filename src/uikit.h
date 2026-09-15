// Shared UI plumbing: the drawing surface, hit targets, and the two widgets
// every screen needs.
//
// Extracted from PhraseCraze's screens when FiveHead arrived, so the two games
// and the launcher all hit-test and draw the same way. Actions are opaque ints
// here — each screen module keeps its own enum and casts — so uikit never has
// to know what any particular game's buttons mean.
#pragma once

#include <M5Unified.h>

#include <cstdint>

namespace tabulous {
namespace uikit {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool hit(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  bool overlaps(const Rect &o) const {
    return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
  }
  // This rect cut down to the part inside `b`; empty (w or h <= 0) if none.
  Rect clip(const Rect &b) const {
    const int x0 = x > b.x ? x : b.x, y0 = y > b.y ? y : b.y;
    const int x1 = (x + w < b.x + b.w) ? x + w : b.x + b.w;
    const int y1 = (y + h < b.y + b.h) ? y + h : b.y + b.h;
    return Rect{x0, y0, x1 - x0, y1 - y0};
  }
};

// Drawing goes straight at the panel. An off-screen canvas was tried and cost
// ~750 ms per frame in PSRAM; the MIPI-DSI panel has its own framebuffer, so
// a second buffer buys nothing. See the README.
LovyanGFX &gfx();
void present();

// Redirect every screen's drawing at another surface — an off-screen canvas,
// for capture. Pass nullptr to go back to the panel.
//
// This is worth the indirection because the Tab5's MIPI-DSI panel is
// write-only in practice: M5GFX's readRect and readRectRGB both return a
// constant here whatever is on screen, so the only way to see what was drawn
// is to draw it somewhere readable. Every screen goes through gfx(), so this
// one hook captures all of them.
void setSurface(LovyanGFX *surface);

// The finger, as the shell sees it. Reads the panel unless a synthetic
// gesture is in progress (see overrideTouch), so a drag injected over serial
// travels the same path a real one does - the scroller, the reorder handle,
// the press edge in the main loop.
struct TouchState {
  bool down = false;
  int x = 0, y = 0;
};
TouchState touch();
// A synthetic touch stands in for the panel while `active`; pass false to
// hand the panel back.
void overrideTouch(bool active, bool down, int x, int y);

// Hit targets are rebuilt on every repaint, so they can never drift out of
// sync with what is actually on screen.
void clearTargets();
void addTarget(const Rect &r, int action, int param = 0);
// Drops every target that overlaps `r`. A scrolling list redraws itself
// without repainting the screen around it, so it drops its own targets and
// registers them again where its rows now are, leaving the rest alone.
void removeTargetsIn(const Rect &r);

// Searches in REVERSE registration order, so for overlapping targets the one
// registered LAST wins. Register the big background target first and the small
// control on top of it second. Returns false if nothing was hit.
bool findTarget(int x, int y, int *action, int *param);

// Same result as gfx().fillRoundRect(), but composed from bulk fills and four
// corner circles. Measured on the Tab5: 23 ms -> ~3 ms for a full-width blob.
// Prefer this anywhere a large rounded rectangle is drawn.
void fillRoundRectFast(int x, int y, int w, int h, int r, uint16_t colour);

// The same shape with a vertical gradient from `top` to `bottom` and a darker
// `shelf` band along the bottom edge, which is what makes a blob read as an
// object you press rather than a flat fill.
//
// Colours are RGB888 here, not RGB565, because the interpolation has to happen
// before the panel's 5/6/5 quantisation — lerping two already-quantised
// colours throws away most of the steps there are.
//
// It emits one fill per DISTINCT panel colour rather than one per row. Over a
// 120 px blob a ~20% darkening only passes through about seven values in
// RGB565, so per-row drawing would issue 120 fills to paint seven bands.
//
// `shelf` is a band of `shelf_h` rows along the bottom edge, following the
// curve. Keep it thin — it wants to read as the shape's lit edge, not as a
// stripe. Both caps sample the gradient so the body stays continuous.
void fillRoundRectShaded(int x, int y, int w, int h, int r, uint32_t top,
                         uint32_t bottom, uint32_t shelf, int shelf_h);

void drawButton(const Rect &r, const char *label, uint16_t bg, uint16_t fg,
                const lgfx::IFont *font = &fonts::FreeSansBold18pt7b);

// A scroll button with a real triangle rather than a glyph. "^" and "v" from
// a text font read as a caret and a lower-case letter, not as arrows.
void drawArrowButton(const Rect &r, bool up, uint16_t bg, uint16_t fg);
void drawLabel(const char *text, int x, int y, uint16_t color,
               const lgfx::IFont *font, textdatum_t datum = top_left,
               uint8_t size = 1);

// Every repaint blocks the loop and therefore eats taps, so all of them are
// measured. Reported periodically from main().
void noteRepaint(uint32_t took_us, int screen);
uint32_t worstRepaintUs();
int worstRepaintScreen();
uint32_t repaintCount();
size_t targetCount();

}  // namespace uikit
}  // namespace tabulous
