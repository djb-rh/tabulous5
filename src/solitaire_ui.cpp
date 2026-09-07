#include "solitaire_ui.h"

#include <M5Unified.h>

#include <cmath>
#include <cstdio>

#include "app.h"
#include "audio.h"
#include "highscores.h"
#include "scoreboard.h"
#include "settings_store.h"
#include "textentry.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace solitaire_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Action : uint8_t {
  None, Pile, Stock, Undo, Auto, NewGame, Settings, Scores, Menu, About,
  ConfirmYes, ConfirmNo, SettingDec, SettingInc, SettingsBack,
};

solitaire::Game *g_game = nullptr;
bool g_dirty = true;
settings_store::SolitaireSettings g_opt;
bool g_settings_open = false;
bool g_confirm_new = false;
int g_sel_pile = -1, g_sel_index = -1;
uint32_t g_started_ms = 0, g_elapsed_s = 0;
bool g_was_won = false;
uint32_t g_pending_time = 0;

const char *const kScoreKeys[2] = {"hs_sol_1", "hs_sol_3"};
const char *const kScoreLabels[2] = {"Draw 1", "Draw 3"};

// Seven columns across, and a card tall enough to read a rank at arm's length.
constexpr int kCardW = 140;
constexpr int kCardH = 196;
constexpr int kGapX = 14;
constexpr int kBoardW = 7 * kCardW + 6 * kGapX;
constexpr int kBoardX = (kW - kBoardW) / 2;
constexpr int kTopY = 12;
constexpr int kTableauY = kTopY + kCardH + 20;
constexpr int kFooterH = 74;
// Preferred overlaps; long columns compress below these to stay on screen.
constexpr int kFaceUpStep = 34;
constexpr int kFaceDownStep = 16;

int slotX(int slot) { return kBoardX + slot * (kCardW + kGapX); }

// Columns can reach nineteen cards. Rather than clip, the step shrinks to fit
// the space available — the same thing a physical deal does when you squash
// the pile up.
int stepFor(const std::vector<solitaire::Card> &p, bool face_down) {
  const int preferred = face_down ? kFaceDownStep : kFaceUpStep;
  if (p.size() <= 1) return preferred;
  const int avail = kH - kFooterH - kTableauY - kCardH - 6;
  const int needed = (int)p.size() - 1;
  const int fit = avail / needed;
  return fit < preferred ? (fit < 6 ? 6 : fit) : preferred;
}

const char *rankName(uint8_t r) {
  static const char *names[] = {"",  "A", "2", "3", "4",  "5", "6",
                                "7", "8", "9", "10", "J", "Q", "K"};
  return (r >= 1 && r <= 13) ? names[r] : "";
}

// Suits as shapes rather than glyphs: the built-in fonts have no card suits,
// and these stay legible at 20 px where a character would not.
//
// Heart and spade are filled SCANLINE BY SCANLINE as the union of their
// parts, rather than by overpainting a triangle with circles. Overpainting
// leaves a notch wherever the straight edge crosses back inside the curve —
// the divots on the shoulders. Taking the union per row cannot produce a
// concave seam, whatever the proportions.
struct Span {
  int a = 0, b = 0;
  bool ok = false;
};

Span circleSpan(int ccx, int ccy, int cr, int y) {
  Span s;
  const int dy = y - ccy;
  if (dy < -cr || dy > cr) return s;
  const int dx = (int)lrintf(sqrtf((float)(cr * cr - dy * dy)));
  s.a = ccx - dx;
  s.b = ccx + dx;
  s.ok = true;
  return s;
}

// Isoceles triangle: an edge at y = by spanning bx0..bx1, apex at (ax, ay).
Span triSpan(int bx0, int bx1, int by, int ax, int ay, int y) {
  Span s;
  const int lo = by < ay ? by : ay;
  const int hi = by < ay ? ay : by;
  if (y < lo || y > hi || by == ay) return s;
  const float f = (float)(y - by) / (float)(ay - by);
  s.a = (int)lrintf(bx0 + (ax - bx0) * f);
  s.b = (int)lrintf(bx1 + (ax - bx1) * f);
  if (s.a > s.b) {
    const int tmp = s.a;
    s.a = s.b;
    s.b = tmp;
  }
  s.ok = true;
  return s;
}

// Where a straight leg from the apex touches a lobe without crossing it.
//
// This is the whole trick. A leg drawn from the circle's WIDEST point to the
// apex is a chord: it immediately cuts back inside the arc, so the outline
// kinks at the shoulder. The tangent point sits lower and slightly inboard of
// the widest point, and a leg through it meets the arc with the same slope —
// which is the only way the join reads as one continuous edge.
//
// `outward` picks the outer of the two tangents (the side away from centre).
bool tangentPoint(float ax, float ay, float ccx, float ccy, float radius,
                  bool outward, float *tx, float *ty) {
  const float dx = ax - ccx, dy = ay - ccy;
  const float d2 = dx * dx + dy * dy;
  if (d2 <= radius * radius) return false;
  const float len = sqrtf(d2 - radius * radius);
  const float k = radius * radius / d2;
  const float m = radius * len / d2;

  const float x1 = ccx + k * dx - m * dy, y1 = ccy + k * dy + m * dx;
  const float x2 = ccx + k * dx + m * dy, y2 = ccy + k * dy - m * dx;

  const bool first_is_outer =
      outward ? (x1 > x2) : (x1 < x2);
  *tx = first_is_outer ? x1 : x2;
  *ty = first_is_outer ? y1 : y2;
  return true;
}

// Draws one row. `solid` fills straight across between separated lobes, which
// is what makes the body of the shape continuous; without it the heart splits
// into two circles below the cleft. Left false above the cleft so the notch
// at the top of a heart survives.
void fillRow(Span *spans, int count, int y, bool solid, uint16_t colour) {
  auto &g = gfx();
  if (solid) {
    int a = 0, b = 0;
    bool any = false;
    for (int i = 0; i < count; i++) {
      if (!spans[i].ok) continue;
      if (!any) { a = spans[i].a; b = spans[i].b; any = true; continue; }
      if (spans[i].a < a) a = spans[i].a;
      if (spans[i].b > b) b = spans[i].b;
    }
    if (any) g.drawFastHLine(a, y, b - a + 1, colour);
    return;
  }
  for (int i = 0; i < count; i++) {
    if (!spans[i].ok) continue;
    for (int j = i + 1; j < count; j++) {
      if (!spans[j].ok) continue;
      if (spans[j].a <= spans[i].b + 1 && spans[i].a <= spans[j].b + 1) {
        spans[i].a = spans[i].a < spans[j].a ? spans[i].a : spans[j].a;
        spans[i].b = spans[i].b > spans[j].b ? spans[i].b : spans[j].b;
        spans[j].ok = false;
        j = i;
      }
    }
    g.drawFastHLine(spans[i].a, y, spans[i].b - spans[i].a + 1, colour);
  }
}

void drawStem(int cx, int y_top, int y_bottom, int half_top, int half_bottom,
              uint16_t colour) {
  auto &g = gfx();
  g.fillTriangle(cx - half_top, y_top, cx + half_top, y_top, cx + half_bottom,
                 y_bottom, colour);
  g.fillTriangle(cx - half_top, y_top, cx + half_bottom, y_bottom,
                 cx - half_bottom, y_bottom, colour);
}

void drawSuit(int cx, int cy, int r, uint8_t suit, uint16_t colour) {
  auto &g = gfx();
  switch (suit) {
    case 1:  // diamond — two triangles meet on a shared edge, so no seam
      g.fillTriangle(cx, cy - r, cx - r, cy, cx + r, cy, colour);
      g.fillTriangle(cx, cy + r, cx - r, cy, cx + r, cy, colour);
      break;

    case 2: {  // heart
      const int lobe = r / 2;
      const int ly = cy - r / 4;
      const float apex_y = (float)(cy + r);
      float tx = 0, ty = 0;
      const bool has_t = tangentPoint((float)cx, apex_y, (float)(cx + lobe),
                                      (float)ly, (float)lobe, true, &tx, &ty);
      const int t_y = has_t ? (int)lrintf(ty) : ly;
      const int t_x = has_t ? (int)lrintf(tx) : cx + r;

      for (int y = ly - lobe; y <= cy + r; y++) {
        Span s[3] = {circleSpan(cx - lobe, ly, lobe, y),
                     circleSpan(cx + lobe, ly, lobe, y),
                     triSpan(cx - (t_x - cx), t_x, t_y, cx, cy + r, y)};
        // Solid from the lobe centres down; above that the two arcs stay
        // separate so the cleft is preserved.
        fillRow(s, 3, y, y >= ly, colour);
      }
      break;
    }

    case 3: {  // spade
      const int lobe = r / 2;
      const int base = cy + r / 6;
      const float apex_y = (float)(cy - r);
      float tx = 0, ty = 0;
      const bool has_t = tangentPoint((float)cx, apex_y, (float)(cx + lobe),
                                      (float)base, (float)lobe, true, &tx, &ty);
      const int t_y = has_t ? (int)lrintf(ty) : base;
      const int t_x = has_t ? (int)lrintf(tx) : cx + r;

      for (int y = cy - r; y <= base + lobe; y++) {
        Span s[3] = {triSpan(cx - (t_x - cx), t_x, t_y, cx, cy - r, y),
                     circleSpan(cx - lobe, base, lobe, y),
                     circleSpan(cx + lobe, base, lobe, y)};
        // Solid down to the lobe centres; below them the stem fills the gap.
        fillRow(s, 3, y, y <= base, colour);
      }
      drawStem(cx, base, cy + r, r / 8 + 1, r / 2, colour);
      break;
    }

    default: {  // club — three overlapping circles, so no straight/curve seam
      // Lobe radius and side offset are both r/2, which makes the club exactly
      // as wide as the heart and spade (offset + radius = r) while keeping the
      // side lobes tangent at the centre. Widening by moving the lobes apart
      // instead would open a gap up the middle, since the top lobe does not
      // reach down to their centre line.
      const int lobe = r / 2;
      g.fillCircle(cx, cy - r / 2, lobe, colour);
      g.fillCircle(cx - lobe, cy + r / 5, lobe, colour);
      g.fillCircle(cx + lobe, cy + r / 5, lobe, colour);
      drawStem(cx, cy + r / 5, cy + r, r / 8 + 1, r / 2, colour);
      break;
    }
  }
}

void drawCard(int x, int y, const solitaire::Card &c, bool selected,
              bool full) {
  auto &g = gfx();
  if (!c.face_up) {
    uikit::fillRoundRectFast(x, y, kCardW, kCardH, 12, rgb(0x27406B));
    g.drawRoundRect(x, y, kCardW, kCardH, 12, rgb(0x4C6DA8));
    return;
  }

  uikit::fillRoundRectFast(x, y, kCardW, kCardH, 12, rgb(0xF7F7F2));
  g.drawRoundRect(x, y, kCardW, kCardH, 12, selected ? kAccent : rgb(0xB8B8B0));
  if (selected) {
    g.drawRoundRect(x + 1, y + 1, kCardW - 2, kCardH - 2, 11, kAccent);
    g.drawRoundRect(x + 2, y + 2, kCardW - 4, kCardH - 4, 10, kAccent);
  }

  const uint16_t ink = c.red() ? rgb(0xC0272D) : rgb(0x1A1A1A);
  drawLabel(rankName(c.rank), x + 12, y + 10, ink, &fonts::FreeSansBold18pt7b);
  drawSuit(x + kCardW - 26, y + 30, 13, c.suit, ink);
  // The middle pip only shows on a card whose whole face is visible.
  if (full) drawSuit(x + kCardW / 2, y + kCardH / 2 + 14, 30, c.suit, ink);
}

void drawEmptySlot(int x, int y, const char *hint) {
  auto &g = gfx();
  g.drawRoundRect(x, y, kCardW, kCardH, 12, rgb(0x2C5540));
  if (hint) {
    drawLabel(hint, x + kCardW / 2, y + kCardH / 2, rgb(0x2C5540),
              &fonts::FreeSansBold18pt7b, middle_center);
  }
}

// Per-pile dirty tracking.
//
// Every tap used to clear the whole screen and redraw all 52 cards: ~115 ms,
// and the clear is visible as a flash. But a move only changes two piles, and
// a selection only changes one. So each pile is redrawn on its own, and the
// screen is cleared only when the whole board really has changed.
//
// Hit targets are ALWAYS re-registered for every pile, because a pile that
// changed height moved its neighbours' cards; only the drawing is selective.
bool g_pile_dirty[solitaire::kPileCount] = {};
bool g_full_dirty = true;

void markDirty(int pile) {
  if (pile >= 0 && pile < solitaire::kPileCount) g_pile_dirty[pile] = true;
  g_dirty = true;
}

void markAllDirty() {
  g_full_dirty = true;
  g_dirty = true;
}

// A tableau column clears all the way to the bottom of the play area: a column
// that just got shorter has to erase where it used to reach.
Rect pileRect(int pile) {
  if (solitaire::isTableau(pile)) {
    const int col = pile - solitaire::kTableau0;
    return Rect{slotX(col), kTableauY, kCardW, kH - kFooterH - kTableauY};
  }
  int slot = 0;
  if (pile == solitaire::kWaste) slot = 1;
  else if (solitaire::isFoundation(pile)) slot = 3 + (pile - solitaire::kFoundation0);
  // The waste fans up to three cards rightwards, so it is wider than a card.
  const int w = (pile == solitaire::kWaste) ? kCardW + 48 : kCardW;
  return Rect{slotX(slot), kTopY, w, kCardH};
}

void clearPile(int pile) {
  const Rect r = pileRect(pile);
  gfx().fillRect(r.x, r.y, r.w, r.h, rgb(0x14532D));
}

void drawStockSlot(bool draw) {
  const auto &stock = g_game->pile(solitaire::kStock);
  const auto &waste = g_game->pile(solitaire::kWaste);
  const int sx = slotX(0);
  if (draw) {
    clearPile(solitaire::kStock);
    if (stock.empty()) {
      drawEmptySlot(sx, kTopY, waste.empty() ? nullptr : "\x7f");
    } else {
      drawCard(sx, kTopY, {0, 0, false}, false, true);
    }
  }
  uikit::addTarget(Rect{sx, kTopY, kCardW, kCardH}, (int)Action::Stock);
}

void drawWasteSlot(bool draw) {
  const auto &waste = g_game->pile(solitaire::kWaste);
  const int wx = slotX(1);
  if (draw) clearPile(solitaire::kWaste);

  if (waste.empty()) {
    if (draw) drawEmptySlot(wx, kTopY, nullptr);
    return;
  }
  const int show = (int)std::min<size_t>(waste.size(), 3);
  for (int i = show - 1; i >= 0; i--) {
    const size_t idx = waste.size() - 1 - (size_t)i;
    const int x = wx + (show - 1 - i) * 22;
    const bool top = (idx == waste.size() - 1);
    if (draw) {
      drawCard(x, kTopY, waste[idx], top && g_sel_pile == solitaire::kWaste,
               true);
    }
    if (top) {
      uikit::addTarget(Rect{x, kTopY, kCardW, kCardH}, (int)Action::Pile,
                       solitaire::kWaste);
    }
  }
}

void drawFoundationSlot(int i, bool draw) {
  const int fx = slotX(3 + i);
  const auto &f = g_game->pile(solitaire::kFoundation0 + i);
  if (draw) {
    clearPile(solitaire::kFoundation0 + i);
    // No suit hint: any empty foundation takes any ace.
    if (f.empty()) drawEmptySlot(fx, kTopY, nullptr);
    else drawCard(fx, kTopY, f.back(), false, true);
  }
  uikit::addTarget(Rect{fx, kTopY, kCardW, kCardH}, (int)Action::Pile,
                   solitaire::kFoundation0 + i);
}

void drawColumn(int col, bool draw) {
  const int pile = solitaire::kTableau0 + col;
  const auto &p = g_game->pile(pile);
  const int x = slotX(col);
  if (draw) clearPile(pile);

  if (p.empty()) {
    if (draw) drawEmptySlot(x, kTableauY, nullptr);
    uikit::addTarget(Rect{x, kTableauY, kCardW, kCardH}, (int)Action::Pile,
                     pile);
    return;
  }

  int y = kTableauY;
  for (size_t i = 0; i < p.size(); i++) {
    const bool last = (i == p.size() - 1);
    const bool selected =
        (g_sel_pile == pile && g_sel_index >= 0 && (int)i >= g_sel_index);
    if (draw) drawCard(x, y, p[i], selected, last);
    // Registered in order, so the topmost card wins the hit test: uikit
    // searches targets in reverse.
    uikit::addTarget(Rect{x, y, kCardW, last ? kCardH : stepFor(p, false)},
                     (int)Action::Pile, pile * 100 + (int)i);
    if (!last) y += stepFor(p, !p[i + 1].face_up && !p[i].face_up);
  }
}

// The footer is seven equal slots: a status readout then six buttons. The
// clock patch below reuses this, so a once-a-second repaint cannot paint over
// the neighbouring button — which is exactly what made UNDO look cut off.
constexpr int kFootGap = 12;
constexpr int kFootW = (kBoardW - 6 * kFootGap) / 7;
Rect footSlot(int i) {
  return Rect{kBoardX + i * (kFootW + kFootGap), kH - kFooterH + 6, kFootW, 62};
}

void drawStatusPatch() {
  auto &g = gfx();
  const Rect s = footSlot(0);
  g.fillRect(s.x, kH - kFooterH, s.w, kFooterH, rgb(0x14532D));
  char buf[32];
  if (g_opt.show_timer) {
    snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(g_elapsed_s / 60),
             (unsigned)(g_elapsed_s % 60));
    drawLabel(buf, s.x, s.y + 24, kText, &fonts::FreeSansBold18pt7b,
              middle_left);
  }
  snprintf(buf, sizeof(buf), "%d moves", g_game->moves());
  drawLabel(buf, s.x, s.y + 52, kMuted, &fonts::FreeSans9pt7b, middle_left);
}

void drawFooter() {
  const int y = kH - kFooterH + 6;
  constexpr int kGap = kFootGap;
  int x = kBoardX;
  const int w = kFootW;

  drawStatusPatch();
  x += w + kGap;

  const Rect undo{x, y, w, 62};
  drawButton(undo, "UNDO", g_game->canUndo() ? kSurfaceLift : kSurface,
             g_game->canUndo() ? kText : kMuted, &fonts::FreeSansBold12pt7b);
  if (g_game->canUndo()) uikit::addTarget(undo, (int)Action::Undo);
  x += w + kGap;

  const Rect autob{x, y, w, 62};
  drawButton(autob, "AUTO", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(autob, (int)Action::Auto);
  x += w + kGap;

  const Rect settings{x, y, w, 62};
  drawButton(settings, "OPTIONS", kSurfaceLift, kText,
             &fonts::FreeSansBold12pt7b);
  uikit::addTarget(settings, (int)Action::Settings);
  x += w + kGap;

  const Rect fresh{x, y, w, 62};
  drawButton(fresh, "NEW", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(fresh, (int)Action::NewGame);
  x += w + kGap;

  const Rect scores{x, y, w, 62};
  drawButton(scores, "SCORES", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(scores, (int)Action::Scores);
  x += w + kGap;

  const Rect menu{x, y, w, 62};
  drawButton(menu, "MENU", kSurfaceLift, kMuted, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(menu, (int)Action::Menu);
}

int settingCount() { return 4; }

const char *settingLabel(int i) {
  switch (i) {
    case 0: return "Cards per deal";
    case 1: return "Times through the deck";
    case 2: return "Tap sends a card home";
    case 3: return "Show timer";
    default: return "";
  }
}

void settingValue(int i, char *out, size_t len) {
  switch (i) {
    case 0: snprintf(out, len, "%d", g_opt.draw_three ? 3 : 1); break;
    case 1:
      if (g_opt.max_passes <= 0) snprintf(out, len, "unlimited");
      else snprintf(out, len, "%d", g_opt.max_passes);
      break;
    case 2: snprintf(out, len, "%s", g_opt.tap_to_foundation ? "on" : "off"); break;
    case 3: snprintf(out, len, "%s", g_opt.show_timer ? "on" : "off"); break;
    default: snprintf(out, len, "-"); break;
  }
}

void newDeal();

void adjustSetting(int i, int delta) {
  switch (i) {
    case 0:
      // Changing the deal size mid-game would be meaningless, so it starts a
      // fresh one — and each size keeps its own high-score table.
      g_opt.draw_three = !g_opt.draw_three;
      newDeal();
      break;
    case 1: {
      int v = g_opt.max_passes + delta;
      if (v < 0) v = 3;
      if (v > 3) v = 0;
      g_opt.max_passes = v;
      g_game->setMaxPasses(v);
      break;
    }
    case 2: g_opt.tap_to_foundation = !g_opt.tap_to_foundation; break;
    case 3: g_opt.show_timer = !g_opt.show_timer; break;
    default: break;
  }
}

void drawSettings() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("SOLITAIRE OPTIONS", kMargin, 34, kText,
            &fonts::FreeSansBold18pt7b);

  const int top = 120;
  const int row_h = 104;
  for (int i = 0; i < settingCount(); i++) {
    const int y = top + i * row_h;
    const int inner = row_h - 16;
    uikit::fillRoundRectFast(kMargin, y, kW - 2 * kMargin, inner, 14, kSurface);
    drawLabel(settingLabel(i), kMargin + 28, y + inner / 2, kText,
              &fonts::FreeSans12pt7b, middle_left);

    char value[32];
    settingValue(i, value, sizeof(value));
    drawLabel(value, kW - kMargin - 230, y + inner / 2, kAccent,
              &fonts::FreeSansBold12pt7b, middle_right);

    const Rect dec{kW - kMargin - 210, y + 10, 92, inner - 20};
    const Rect inc{kW - kMargin - 106, y + 10, 92, inner - 20};
    drawButton(dec, "-", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
    drawButton(inc, "+", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
    uikit::addTarget(dec, (int)Action::SettingDec, i);
    uikit::addTarget(inc, (int)Action::SettingInc, i);
  }

  const Rect back{kMargin, kH - 84, 260, 64};
  drawButton(back, "BACK", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(back, (int)Action::SettingsBack);
}

void drawConfirm() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("New deal?", kW / 2, 240, kText, &fonts::FreeSansBold24pt7b,
            middle_center);
  drawLabel("This game will be lost.", kW / 2, 306, kMuted,
            &fonts::FreeSans12pt7b, middle_center);
  const Rect no{kW / 2 - 340, 380, 320, 110};
  const Rect yes{kW / 2 + 20, 380, 320, 110};
  drawButton(no, "KEEP PLAYING", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  drawButton(yes, "NEW DEAL", kDanger, kOnFill, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(no, (int)Action::ConfirmNo);
  uikit::addTarget(yes, (int)Action::ConfirmYes);
}

void repaintAll() {
  uikit::clearTargets();
  if (g_settings_open) {
    drawSettings();
    uikit::present();
    return;
  }
  if (g_confirm_new) {
    drawConfirm();
    uikit::present();
    return;
  }

  auto &g = gfx();
  // Only clear the whole board when the whole board changed. Otherwise each
  // pile clears and redraws its own strip, which is what removes the flash.
  if (g_full_dirty) g.fillScreen(rgb(0x14532D));

  drawStockSlot(g_full_dirty || g_pile_dirty[solitaire::kStock]);
  drawWasteSlot(g_full_dirty || g_pile_dirty[solitaire::kWaste]);
  for (int i = 0; i < 4; i++) {
    drawFoundationSlot(i, g_full_dirty ||
                              g_pile_dirty[solitaire::kFoundation0 + i]);
  }
  for (int col = 0; col < 7; col++) {
    drawColumn(col, g_full_dirty || g_pile_dirty[solitaire::kTableau0 + col]);
  }

  // Cheap, and the move count and UNDO state change on nearly every tap.
  drawFooter();

  const Rect help{kW - 76, kH - kFooterH - 76, 64, 64};
  uikit::fillRoundRectFast(help.x, help.y, help.w, help.h, 32, kSurfaceLift);
  drawLabel("?", help.x + 32, help.y + 32, kText, &fonts::FreeSansBold18pt7b,
            middle_center);
  uikit::addTarget(help, (int)Action::About);

  if (g_game->won()) {
    uikit::fillRoundRectFast(kW / 2 - 260, kH / 2 - 80, 520, 160, 20, kBg);
    drawLabel("YOU WIN", kW / 2, kH / 2 - 20, kGood,
              &fonts::FreeSansBold24pt7b, middle_center);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u:%02u  -  %d moves",
             (unsigned)(g_elapsed_s / 60), (unsigned)(g_elapsed_s % 60),
             g_game->moves());
    drawLabel(buf, kW / 2, kH / 2 + 36, kMuted, &fonts::FreeSans12pt7b,
              middle_center);
  }

  g_full_dirty = false;
  for (bool &d : g_pile_dirty) d = false;
  uikit::present();
}

void newDeal() {
  markAllDirty();
  g_game->deal((uint32_t)esp_random(), g_opt.draw_three);
  g_game->setMaxPasses(g_opt.max_passes);
  g_sel_pile = g_sel_index = -1;
  g_started_ms = 0;
  g_elapsed_s = 0;
  g_was_won = false;
}

void offerHighScore() {
  g_pending_time = g_elapsed_s;
  const int key = g_opt.draw_three ? 1 : 0;
  const highscores::Table t = highscores::load(kScoreKeys[key]);
  if (!t.qualifies(g_pending_time, true)) return;
  textentry::open("YOUR NAME", "", highscores::kNameLen);
}

void checkWin();

void checkWinImpl() {
  // The win banner sits over the board, so it needs a clean one underneath.
  if (g_game->won() && !g_was_won) markAllDirty();
  if (g_game->won() && !g_was_won) {
    g_was_won = true;
    audio::fanfare();
    offerHighScore();
  }
}

void checkWin() { checkWinImpl(); }

}  // namespace

void begin(solitaire::Game *game) {
  g_game = game;
  settings_store::loadSolitaire(&g_opt);
  newDeal();
  g_dirty = true;
}

void invalidate() { markAllDirty(); }

void tick(uint32_t now_ms) {
  if (!g_game) return;

  if (textentry::active()) {
    textentry::tick(now_ms);
    return;
  }
  if (scoreboard::active()) {
    scoreboard::tick(now_ms);
    return;
  }

  if (!g_game->won() && !g_confirm_new) {
    if (g_started_ms == 0) g_started_ms = now_ms;
    const uint32_t secs = (now_ms - g_started_ms) / 1000;
    if (secs != g_elapsed_s) {
      g_elapsed_s = secs;
      // Only the footer clock changes; redrawing 52 cards once a second would
      // block the loop and swallow taps.
      if (!g_dirty) {
        drawStatusPatch();
        uikit::present();
      }
    }
  }

  if (g_dirty) {
    g_dirty = false;
    const uint32_t t0 = micros();
    repaintAll();
    uikit::noteRepaint(micros() - t0, 83);
  }
}

void handleTap(int x, int y, uint32_t now_ms) {
  (void)now_ms;
  if (!g_game) return;

  if (textentry::active()) {
    if (textentry::handleTap(x, y) == textentry::Result::Closed) {
      if (textentry::accepted()) {
        const int key = g_opt.draw_three ? 1 : 0;
        highscores::Table t = highscores::load(kScoreKeys[key]);
        const int row = t.insert(textentry::text().c_str(), g_pending_time, true);
        highscores::save(kScoreKeys[key], t);
        scoreboard::open("SOLITAIRE - BEST TIMES", kScoreKeys, kScoreLabels, 2,
                         key, true, row);
      }
      g_dirty = true;
    }
    return;
  }
  if (scoreboard::active()) {
    if (scoreboard::handleTap(x, y) == scoreboard::Result::Closed) g_dirty = true;
    return;
  }

  // Whatever the tap does, the pile that was selected and the pile that ends
  // up selected both change appearance, so both get redrawn. Individual cases
  // add the piles they actually move cards between.
  const int prev_sel = g_sel_pile;

  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) {
    g_sel_pile = g_sel_index = -1;  // tapping the baize clears a selection
    markDirty(prev_sel);
    return;
  }

  switch ((Action)action) {
    case Action::Stock:
      if (g_game->drawStock()) audio::select();
      g_sel_pile = g_sel_index = -1;
      markDirty(solitaire::kStock);
      markDirty(solitaire::kWaste);
      break;

    case Action::Pile: {
      // Tableau targets pack pile and card index into one parameter.
      const int pile = param >= 100 ? param / 100 : param;
      const int index = param >= 100 ? param % 100 : -1;

      if (g_sel_pile < 0) {
        const auto &p = g_game->pile(pile);
        const int idx = index >= 0 ? index : (int)p.size() - 1;
        // Convenience: tapping an exposed card that has a home sends it there
        // rather than making you tap twice.
        if (g_opt.tap_to_foundation && idx >= 0 &&
            idx == (int)p.size() - 1 && !p.empty() && p.back().face_up) {
          const int f = g_game->foundationFor(p.back());
          if (f >= 0 && g_game->move(pile, idx, f)) {
            audio::correct();
            markDirty(pile);
            markDirty(f);
            checkWin();
            break;
          }
        }
        if (idx >= 0 && g_game->isSelectable(pile, idx)) {
          g_sel_pile = pile;
          g_sel_index = idx;
          audio::select();
        }
        break;
      }

      if (pile == g_sel_pile) {
        g_sel_pile = g_sel_index = -1;  // tap it again to put it down
        audio::select();
        break;
      }

      if (g_game->move(g_sel_pile, g_sel_index, pile)) {
        audio::correct();
        markDirty(g_sel_pile);
        markDirty(pile);
        g_sel_pile = g_sel_index = -1;
        checkWin();
      } else {
        // Not a legal destination: treat the tap as picking up instead, which
        // is what a player almost always means.
        const auto &p = g_game->pile(pile);
        const int idx = index >= 0 ? index : (int)p.size() - 1;
        if (idx >= 0 && g_game->isSelectable(pile, idx)) {
          g_sel_pile = pile;
          g_sel_index = idx;
          audio::select();
        } else {
          audio::reject();
          g_sel_pile = g_sel_index = -1;
        }
      }
      break;
    }

    case Action::Undo:
      if (g_game->undo()) audio::skip();
      g_sel_pile = g_sel_index = -1;
      markAllDirty();  // undo can restore any pile
      break;
    case Action::Auto:
      if (g_game->autoPlay() > 0) {
        audio::correct();
        checkWin();
      }
      g_sel_pile = g_sel_index = -1;
      markAllDirty();  // autoplay walks every pile
      break;
    case Action::Settings:
      audio::select();
      g_settings_open = true;
      break;
    case Action::SettingsBack:
      audio::select();
      g_settings_open = false;
      settings_store::saveSolitaire(g_opt);
      break;
    case Action::SettingDec:
      audio::select();
      adjustSetting(param, -1);
      break;
    case Action::SettingInc:
      audio::select();
      adjustSetting(param, +1);
      break;
    case Action::NewGame:
      audio::select();
      if (g_game->moves() > 0 && !g_game->won()) g_confirm_new = true;
      else newDeal();
      break;
    case Action::ConfirmYes:
      audio::select();
      g_confirm_new = false;
      newDeal();
      break;
    case Action::ConfirmNo:
      audio::select();
      g_confirm_new = false;
      break;
    case Action::Scores:
      audio::select();
      scoreboard::open("SOLITAIRE - BEST TIMES", kScoreKeys, kScoreLabels, 2,
                       g_opt.draw_three ? 1 : 0, true);
      break;
    case Action::About:
      audio::select();
      app::showAbout();
      break;
    case Action::Menu:
      audio::select();
      app::requestExit();
      break;
    case Action::None:
      break;
  }

  markDirty(prev_sel);
  markDirty(g_sel_pile);
  g_dirty = true;
}

}  // namespace solitaire_ui
}  // namespace tabulous
