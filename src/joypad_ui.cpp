#include "joypad_ui.h"

#include <M5Unified.h>

#include <cstdio>

#include "app.h"
#include "joypad.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace joypad_ui {
namespace {

using namespace theme;
using uikit::gfx;

uint8_t g_state = 0;
uint8_t g_drawn = 0xFF;  // forces the first pass to paint every control
bool g_full = true;

// A press edge for MENU only. Everything else is level-triggered — that is the
// whole point — but leaving the screen must not repeat while a thumb rests on
// the button.
bool g_menu_down = false;

constexpr uint32_t kPollMs = 16;  // ~60 Hz, matching an NES frame
uint32_t g_last_poll = 0;

uint16_t litFill(bool on) { return on ? kAccent : kSurfaceLift; }
uint16_t litInk(bool on) { return on ? kInk : kText; }

void drawDpad(uint8_t state, uint8_t prev, bool full) {
  auto &g = gfx();
  const joypad::Rect area = joypad::dpadArea();

  if (full) {
    // The plate behind the cross, so the pad reads as one object rather than
    // four disconnected pads.
    uikit::fillRoundRectFast(area.x - 8, area.y - 8, area.w + 16, area.h + 16,
                             28, kSurface);
  }

  struct Cell { int col, row; uint8_t bit; };
  const Cell cells[] = {
      {1, 0, joypad::kUp},   {1, 2, joypad::kDown},
      {0, 1, joypad::kLeft}, {2, 1, joypad::kRight},
  };
  for (const Cell &c : cells) {
    const bool on = (state & c.bit) != 0;
    if (!full && ((prev & c.bit) != 0) == on) continue;
    const joypad::Rect r = joypad::dpadCell(c.col, c.row);
    uikit::fillRoundRectFast(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 14,
                             litFill(on));
    // An arrow, not a letter: at a glance the shape says which way it goes.
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = 26;
    const uint16_t ink = litInk(on);
    if (c.bit == joypad::kUp) {
      g.fillTriangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, ink);
    } else if (c.bit == joypad::kDown) {
      g.fillTriangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, ink);
    } else if (c.bit == joypad::kLeft) {
      g.fillTriangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, ink);
    } else {
      g.fillTriangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, ink);
    }
  }

  // The hub. Drawn last so the arms tuck under it, and only on a full pass
  // since it never changes.
  if (full) {
    const joypad::Rect c = joypad::dpadCell(1, 1);
    uikit::fillRoundRectFast(c.x + 3, c.y + 3, c.w - 6, c.h - 6, 14, kSurfaceLift);
  }
}

void drawFace(uint8_t state, uint8_t prev, bool full) {
  auto &g = gfx();
  struct Round { joypad::Circle c; uint8_t bit; const char *label; };
  const Round rounds[] = {
      {joypad::buttonA(), joypad::kA, "A"},
      {joypad::buttonB(), joypad::kB, "B"},
  };
  for (const Round &r : rounds) {
    const bool on = (state & r.bit) != 0;
    if (!full && ((prev & r.bit) != 0) == on) continue;
    g.fillCircle(r.c.cx, r.c.cy, r.c.r, litFill(on));
    uikit::drawLabel(r.label, r.c.cx, r.c.cy, litInk(on),
                     &fonts::FreeSansBold24pt7b, middle_center);
  }

  struct Pill { joypad::Rect r; uint8_t bit; const char *label; };
  const Pill pills[] = {
      {joypad::buttonSelect(), joypad::kSelect, "SELECT"},
      {joypad::buttonStart(), joypad::kStart, "START"},
  };
  for (const Pill &p : pills) {
    const bool on = (state & p.bit) != 0;
    if (!full && ((prev & p.bit) != 0) == on) continue;
    uikit::drawButton(uikit::Rect{p.r.x, p.r.y, p.r.w, p.r.h}, p.label,
                      litFill(on), litInk(on), &fonts::FreeSansBold12pt7b);
  }
}

// Where the emulator's picture will go. Until then it shows what the pad is
// reporting, which is the only way to tell a dead control from a dead finger.
void drawReadout(uint8_t state, bool full) {
  auto &g = gfx();
  const int x = joypad::kVideoX, y = joypad::kVideoY;
  const int w = joypad::kVideoW, h = joypad::kVideoH;

  if (full) {
    g.fillRect(x, y, w, h, kInk);
    g.drawRect(x, y, w, h, kMuted);
    uikit::drawLabel("PICTURE GOES HERE", x + w / 2, y + 34, kMuted,
                     &fonts::FreeSans9pt7b, middle_center);
    char note[64];
    snprintf(note, sizeof(note), "%d x %d  (NES %dx%d at %dx)", joypad::kVideoW,
             joypad::kVideoH, joypad::kNesW, joypad::kNesH, joypad::kScale);
    uikit::drawLabel(note, x + w / 2, y + h - 28, kMuted, &fonts::FreeSans9pt7b,
                     middle_center);
  }

  // Eight lamps in the NES bit order, so the readout doubles as a check that
  // the bitfield a core would receive is the one you think it is.
  const uint8_t bits[joypad::kButtonCount] = {
      joypad::kUp, joypad::kDown,   joypad::kLeft,  joypad::kRight,
      joypad::kB,  joypad::kA,      joypad::kSelect, joypad::kStart};
  const int cols = 2, cw = (w - 60) / cols, rh = 44;
  const int top = y + 74;
  for (int i = 0; i < joypad::kButtonCount; i++) {
    const bool on = (state & bits[i]) != 0;
    if (!full && ((g_drawn & bits[i]) != 0) == on) continue;
    const int col = i % cols, row = i / cols;
    const int lx = x + 30 + col * cw, ly = top + row * rh;
    g.fillRect(lx, ly, cw - 10, rh - 8, kInk);
    g.fillCircle(lx + 14, ly + (rh - 8) / 2, 9, on ? kGood : kSurfaceLift);
    uikit::drawLabel(joypad::name(bits[i]), lx + 34, ly + (rh - 8) / 2,
                     on ? kText : kMuted, &fonts::FreeSansBold12pt7b,
                     middle_left);
  }

  // The raw byte, which is what actually gets handed over.
  char hex[32];
  snprintf(hex, sizeof(hex), "0x%02X", state);
  g.fillRect(x + 30, y + h - 92, w - 60, 40, kInk);
  uikit::drawLabel(hex, x + w / 2, y + h - 72, kAccent,
                   &fonts::FreeSansBold18pt7b, middle_center);
}

void draw(bool full) {
  if (full) gfx().fillScreen(kBg);
  drawControls(g_state, g_drawn, full);
  drawReadout(g_state, full);
  g_drawn = g_state;
}

}  // namespace

// Every contact, OR-ed. Holding a direction while pressing A has to work, and
// that is two simultaneous points.
uint8_t pollPad(bool *menu_held) {
  uint8_t out = 0;
  *menu_held = false;
  const int count = M5.Touch.getCount();
  for (int i = 0; i < count; i++) {
    const auto t = M5.Touch.getDetail(i);
    if (!t.isPressed()) continue;
    out |= joypad::hitTest(t.x, t.y);
    const joypad::Rect m = joypad::menuButton();
    if (m.contains(t.x, t.y)) *menu_held = true;
  }
  return out;
}

void drawControls(uint8_t state, uint8_t prev, bool full) {
  if (full) {
    const joypad::Rect m = joypad::menuButton();
    uikit::drawButton(uikit::Rect{m.x, m.y, m.w, m.h}, "MENU", kSurfaceLift,
                      kText, &fonts::FreeSansBold12pt7b);
  }
  drawDpad(state, prev, full);
  drawFace(state, prev, full);
}

namespace {
}  // namespace

void begin() {
  g_state = 0;
  g_drawn = 0xFF;
  g_full = true;
  g_menu_down = false;
  g_last_poll = 0;
}

void invalidate() {
  g_full = true;
  g_drawn = 0xFF;
}

uint8_t state() { return g_state; }

void tick(uint32_t now_ms) {
  if (now_ms - g_last_poll < kPollMs && !g_full) return;
  g_last_poll = now_ms;

  bool menu_held = false;
  const uint8_t fresh = pollPad(&menu_held);

  // MENU on the press edge, so resting a thumb there does not fire repeatedly.
  if (menu_held && !g_menu_down) {
    g_menu_down = true;
    app::requestExit();
    return;
  }
  if (!menu_held) g_menu_down = false;

  g_state = fresh;
  if (!g_full && g_state == g_drawn) return;  // nothing moved, nothing to draw

  const uint32_t t0 = micros();
  draw(g_full);
  uikit::noteRepaint(micros() - t0, 70);
  g_full = false;
}

}  // namespace joypad_ui
}  // namespace tabulous
