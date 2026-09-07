#include "minesweeper_ui.h"

#include <M5Unified.h>

#include <cstdio>

#include "app.h"
#include "audio.h"
#include "highscores.h"
#include "scoreboard.h"
#include "textentry.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace mines_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Action : uint8_t {
  None, Mode, NewGame, Level, Menu, About, ConfirmYes, ConfirmNo, Scores,
};

// Both of these throw away a board in progress, so both ask first.
enum class Pending : uint8_t { None, NewGame, Level };
Pending g_pending = Pending::None;

// NVS keys are capped at 15 characters.
const char *const kScoreKeys[3] = {"hs_mine_0", "hs_mine_1", "hs_mine_2"};
const char *const kScoreLabels[3] = {"Easy", "Medium", "Hard"};
uint32_t g_pending_time = 0;  // seconds awaiting a name

mines::Board *g_board = nullptr;
bool g_dirty = true;
bool g_flag_mode = false;   // tap places flags instead of digging
mines::Level g_level = mines::Level::Medium;
uint32_t g_started_ms = 0;
uint32_t g_elapsed_s = 0;

// Only the grid is redrawn when a tap changes cells; the header holds still
// except for the counters, which get their own patch.
bool g_redraw_grid_only = false;
int g_drawn_status = -1;

// Grid touches are handled here rather than through the shared tap dispatch,
// because a tap and a hold have to mean different things and the dispatch
// fires on the press edge — before the hold is known.
// Deliberately short, because the two outcomes are not equally bad: releasing
// a fraction too early doesn't merely fail to flag, it DIGS - which can end
// the game outright. So the threshold is set low enough that an intended hold
// almost always clears it, and well above a deliberate tap (typically under
// 150 ms) so digging still feels immediate.
constexpr uint32_t kHoldToFlagMs = 220;
bool g_touch_down = false;
uint32_t g_down_ms = 0;
int g_down_cx = -1, g_down_cy = -1;
bool g_hold_fired = false;

// The tap that launched this game from the menu is usually still in progress
// when the first tick runs. Because the grid polls raw touch state rather than
// press edges, that lingering contact was read as a dig on whatever square
// happened to be under it. Ignore touch until it has been released once.
bool g_swallow_touch = true;

constexpr int kHeaderH = 96;
constexpr int kFooterH = 84;

// Classic number colours — people read these by hue as much as by shape.
uint16_t numberColor(uint8_t n) {
  // Two sets: the dark-theme hues are bright enough to read on a dark cell and
  // would wash out on a light one, so light mode uses saturated dark versions
  // of the same classic colours.
  if (theme::isLight()) {
    switch (n) {
      case 1: return rgb(0x0B3FA8);
      case 2: return rgb(0x18653A);
      case 3: return rgb(0xB3242A);
      case 4: return rgb(0x5B2A9E);
      case 5: return rgb(0x8A5300);
      case 6: return rgb(0x0D6E6E);
      case 7: return rgb(0x1A1A1A);
      default: return rgb(0x5A5A5A);
    }
  }
  switch (n) {
    case 1: return rgb(0x4C8DFF);
    case 2: return rgb(0x3FBF6F);
    case 3: return rgb(0xFF6B6B);
    case 4: return rgb(0xB78BFF);
    case 5: return rgb(0xE0A030);
    case 6: return rgb(0x3FC7C7);
    case 7: return rgb(0xE8E8E8);
    default: return rgb(0xA0A0A0);
  }
}

struct Metrics {
  int cell, x0, y0, w, h;
};

Metrics metrics() {
  const int avail_w = kW - 2 * 16;
  const int avail_h = kH - kHeaderH - kFooterH;
  const int cell = std::min(avail_w / g_board->cols(), avail_h / g_board->rows());
  const int w = cell * g_board->cols();
  const int h = cell * g_board->rows();
  return {cell, (kW - w) / 2, kHeaderH + (avail_h - h) / 2, w, h};
}

void drawCell(const Metrics &m, int x, int y) {
  auto &g = gfx();
  const mines::Cell &c = g_board->at(x, y);
  const int px = m.x0 + x * m.cell;
  const int py = m.y0 + y * m.cell;
  const int s = m.cell - 2;

  if (!c.revealed) {
    g.fillRoundRect(px + 1, py + 1, s, s, 5, kSurfaceLift);
    if (c.flagged) {
      // A wedge rather than a glyph: legible at 40 px and needs no font.
      const int cx = px + m.cell / 2, cy = py + m.cell / 2;
      const int r = m.cell / 4;
      g.fillTriangle(cx - r, cy - r, cx + r, cy, cx - r, cy + r, kDanger);
      g.drawFastVLine(cx - r, cy - r, 2 * r, kText);
    }
    return;
  }

  if (c.mine) {
    g.fillRoundRect(px + 1, py + 1, s, s, 5, kDanger);
    g.fillCircle(px + m.cell / 2, py + m.cell / 2, m.cell / 5, kInk);
    return;
  }

  g.fillRoundRect(px + 1, py + 1, s, s, 5, kSurface);
  if (c.adjacent == 0) return;
  char label[2] = {(char)('0' + c.adjacent), '\0'};
  drawLabel(label, px + m.cell / 2, py + m.cell / 2, numberColor(c.adjacent),
            m.cell >= 56 ? &fonts::FreeSansBold18pt7b
                         : &fonts::FreeSansBold12pt7b,
            middle_center);
}

void drawGrid() {
  const Metrics m = metrics();
  auto &g = gfx();
  g.fillRect(0, kHeaderH, kW, kH - kHeaderH - kFooterH, kBg);
  for (int y = 0; y < g_board->rows(); y++) {
    for (int x = 0; x < g_board->cols(); x++) drawCell(m, x, y);
  }
}

// The grid registers no hit targets at all: see the touch polling in tick().
void addGridTargets() {}

void drawHelpButton(bool add_target);
void offerHighScore();

void drawHeader() {
  auto &g = gfx();
  g.fillRect(0, 0, kW, kHeaderH, kBg);

  char buf[32];
  snprintf(buf, sizeof(buf), "%d", g_board->minesRemaining());
  drawLabel("MINES", kMargin, 20, kMuted, &fonts::FreeSans9pt7b);
  drawLabel(buf, kMargin, 44, kText, &fonts::FreeSansBold18pt7b);

  snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(g_elapsed_s / 60),
           (unsigned)(g_elapsed_s % 60));
  // Shifted left of the ? button that sits in the corner.
  drawLabel("TIME", kW - kMargin - 84, 20, kMuted, &fonts::FreeSans9pt7b,
            top_right);
  drawLabel(buf, kW - kMargin - 84, 44, kText, &fonts::FreeSansBold18pt7b,
            top_right);

  const char *msg = "";
  uint16_t colour = kMuted;
  switch (g_board->status()) {
    case mines::Status::Won: msg = "CLEARED"; colour = kGood; break;
    case mines::Status::Lost: msg = "BOOM"; colour = kDanger; break;
    default: msg = ""; break;
  }
  if (msg[0]) {
    drawLabel(msg, kW / 2 - 190, 40, colour, &fonts::FreeSansBold18pt7b,
              middle_right);
  }

  // The difficulty chip lives here rather than the footer, which has no room
  // left once SCORES is added. It is a control, so it is styled as one.
  const Rect lvl{kW / 2 - 130, 14, 260, 62};
  uikit::fillRoundRectFast(lvl.x, lvl.y, lvl.w, lvl.h, 16, kSurfaceLift);
  drawLabel(mines::preset(g_level).name, lvl.x + lvl.w / 2, lvl.y + lvl.h / 2,
            kText, &fonts::FreeSansBold12pt7b, middle_center);
  drawHelpButton(false);
}

// The header is repainted every second by the clock, which does not clear
// the target list — so its two controls register their targets from the
// full repaint instead.
void addHeaderTargets() {
  uikit::addTarget(Rect{kW / 2 - 130, 14, 260, 62}, (int)Action::Level);
  uikit::addTarget(Rect{kW - kMargin - 64, 16, 64, 64}, (int)Action::About);
}

// A consistent "?" in the same corner on every game, so it is findable
// without hunting. Offered only where MENU is — never mid-round.
// The ? sits inside the header, and drawHeader() repaints the whole header
// once a second for the clock — which erased the button a second after it was
// drawn. So the header redraws it too, WITHOUT registering a target (that
// path does not clear the target list, and a duplicate every second would
// leak). Only the full repaint registers the target.
void drawHelpButton(bool add_target) {
  const Rect help{kW - kMargin - 64, 16, 64, 64};
  uikit::fillRoundRectFast(help.x, help.y, help.w, help.h, 32, kSurfaceLift);
  drawLabel("?", help.x + 32, help.y + 32, kText, &fonts::FreeSansBold18pt7b,
            middle_center);
  if (add_target) uikit::addTarget(help, (int)Action::About);
}

void drawFooter() {
  const int y = kH - kFooterH + 10;
  // Says what a tap will actually do, rather than naming a mode you have to
  // remember the meaning of.
  // Laid out left to right from one origin so the buttons cannot overlap:
  // each starts where the previous one ended, plus a gap.
  constexpr int kGap = 16;
  int x = kMargin;

  // The button states what BOTH gestures do, which removes the need for a
  // separate hint label crammed in beside it.
  const Rect mode{x, y, 400, 64};
  drawButton(mode, g_flag_mode ? "TAP=FLAG  HOLD=DIG" : "TAP=DIG  HOLD=FLAG",
             g_flag_mode ? kAccent : kGood, g_flag_mode ? kInk : kText,
             &fonts::FreeSansBold12pt7b);
  uikit::addTarget(mode, (int)Action::Mode);
  x += mode.w + kGap;

  const Rect scores{x, y, 230, 64};
  drawButton(scores, "SCORES", kSurfaceLift, kText,
             &fonts::FreeSansBold12pt7b);
  uikit::addTarget(scores, (int)Action::Scores);
  x += scores.w + kGap;

  const Rect fresh{x, y, 240, 64};
  drawButton(fresh, "NEW GAME", kSurfaceLift, kText,
             &fonts::FreeSansBold12pt7b);
  uikit::addTarget(fresh, (int)Action::NewGame);
  x += fresh.w + kGap;

  const Rect menu{x, y, 250, 64};
  drawButton(menu, "MENU", kSurfaceLift, kMuted, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(menu, (int)Action::Menu);
}

// Only worth asking if there is something to lose: a fresh, untouched board
// is replaced without ceremony.
bool gameInProgress() {
  return g_board->status() == mines::Status::Playing &&
         g_board->revealedCount() > 0;
}

void drawConfirm() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel(g_pending == Pending::Level ? "Change difficulty?" : "New game?",
            kW / 2, 230, kText, &fonts::FreeSansBold24pt7b, middle_center);
  drawLabel("This board will be lost.", kW / 2, 300, kMuted,
            &fonts::FreeSans12pt7b, middle_center);

  const Rect no{kW / 2 - 340, 380, 320, 110};
  const Rect yes{kW / 2 + 20, 380, 320, 110};
  drawButton(no, "KEEP PLAYING", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  drawButton(yes, g_pending == Pending::Level ? "CHANGE" : "NEW GAME", kDanger,
             kOnFill, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(no, (int)Action::ConfirmNo);
  uikit::addTarget(yes, (int)Action::ConfirmYes);
}

void repaintAll() {
  uikit::clearTargets();
  if (g_pending != Pending::None) {
    drawConfirm();
    g_drawn_status = (int)g_board->status();
    uikit::present();
    return;
  }
  gfx().fillScreen(kBg);
  drawHeader();
  drawGrid();
  addGridTargets();
  drawHelpButton(false);
  addHeaderTargets();
  drawFooter();
  g_drawn_status = (int)g_board->status();
  uikit::present();
}

void newGame() {
  g_board->begin(g_level, (uint32_t)esp_random());
  g_touch_down = false;
  g_hold_fired = false;
  // NEW GAME and the level button are taps too: don't let the tap that
  // started the board also dig its first square.
  g_swallow_touch = true;
  g_started_ms = 0;
  g_elapsed_s = 0;
  g_flag_mode = false;
}

// Only ask for a name if the time actually places. Being prompted and then
// not appearing on the board would be worse than not being asked.
void offerHighScore() {
  g_pending_time = g_elapsed_s;
  const highscores::Table table = highscores::load(kScoreKeys[(int)g_level]);
  if (!table.qualifies(g_pending_time, true)) return;
  textentry::open("YOUR NAME", "", highscores::kNameLen);
}

}  // namespace

void begin(mines::Board *board) {
  g_board = board;
  g_level = mines::Level::Medium;
  newGame();
  g_dirty = true;
  g_redraw_grid_only = false;
}

void invalidate() {
  g_dirty = true;
  // invalidate() is how the shell hands the screen back after the exit
  // confirmation, the About screen or an orientation flip — and in every one
  // of those cases the tap that dismissed the overlay is still in contact.
  // Since the grid polls touch state rather than edges, that lingering
  // contact would be read as a dig.
  g_swallow_touch = true;
}

// Tap digs, hold flags — the standard phone Minesweeper interaction, which
// needs no mode and no explanation. The DIG/FLAG button swaps the two for
// anyone who prefers an explicit mode.
void pollGridTouch(uint32_t now_ms) {
  const auto t = M5.Touch.getDetail();
  const bool down = t.isPressed() || M5.Touch.getCount() > 0;
  const Metrics m = metrics();

  if (g_pending != Pending::None) return;

  if (g_swallow_touch) {
    if (!down) g_swallow_touch = false;  // released — start listening
    return;
  }

  if (down && !g_touch_down) {
    const int cx = (t.x - m.x0) / m.cell;
    const int cy = (t.y - m.y0) / m.cell;
    if (t.x < m.x0 || t.y < m.y0 || !g_board->inBounds(cx, cy)) return;
    g_touch_down = true;
    g_hold_fired = false;
    g_down_ms = now_ms;
    g_down_cx = cx;
    g_down_cy = cy;
    return;
  }

  if (!g_touch_down) return;

  if (down) {
    if (!g_hold_fired && now_ms - g_down_ms >= kHoldToFlagMs) {
      g_hold_fired = true;
      const auto before = g_board->status();
      if (g_flag_mode) g_board->reveal(g_down_cx, g_down_cy);
      else g_board->toggleFlag(g_down_cx, g_down_cy);
      audio::skip();  // distinct from a dig, so the hold is audibly different
      g_redraw_grid_only = (before == g_board->status()) && !g_flag_mode;
      g_dirty = true;
    }
    return;
  }

  // Released.
  g_touch_down = false;
  if (g_hold_fired) return;  // the hold already acted

  const auto before = g_board->status();
  if (g_flag_mode) {
    g_board->toggleFlag(g_down_cx, g_down_cy);
    audio::select();
  } else {
    g_board->reveal(g_down_cx, g_down_cy);
    if (g_board->status() == mines::Status::Lost) audio::boom();
    else if (g_board->status() == mines::Status::Won) audio::fanfare();
    else audio::select();
  }
  g_redraw_grid_only = (before == g_board->status()) && !g_flag_mode;
  g_dirty = true;
  if (before != mines::Status::Won &&
      g_board->status() == mines::Status::Won) {
    offerHighScore();
  }
}

void tick(uint32_t now_ms) {
  if (!g_board) return;

  // Modals own the screen while they are up.
  if (textentry::active()) {
    textentry::tick(now_ms);
    return;
  }
  if (scoreboard::active()) {
    scoreboard::tick(now_ms);
    return;
  }

  pollGridTouch(now_ms);

  if (g_board->status() == mines::Status::Playing &&
      g_pending == Pending::None) {
    if (g_started_ms == 0) g_started_ms = now_ms;
    const uint32_t secs = (now_ms - g_started_ms) / 1000;
    if (secs != g_elapsed_s) {
      g_elapsed_s = secs;
      // Only the header changes on a tick; redrawing 480 cells once a second
      // would block the loop and eat taps.
      drawHeader();
      uikit::present();
    }
  }

  // A status change alters the header too, so it can't take the grid-only path.
  if (g_dirty && g_redraw_grid_only &&
      g_drawn_status == (int)g_board->status()) {
    g_dirty = false;
    g_redraw_grid_only = false;
    const uint32_t t0 = micros();
    drawGrid();
    uikit::present();
    uikit::noteRepaint(micros() - t0, 80);
    return;
  }

  if (g_dirty) {
    g_dirty = false;
    g_redraw_grid_only = false;
    const uint32_t t0 = micros();
    repaintAll();
    uikit::noteRepaint(micros() - t0, 81);
  }
}

void handleTap(int x, int y, uint32_t now_ms) {
  (void)now_ms;
  if (!g_board) return;

  if (textentry::active()) {
    if (textentry::handleTap(x, y) == textentry::Result::Closed) {
      if (textentry::accepted()) {
        highscores::Table table = highscores::load(kScoreKeys[(int)g_level]);
        const int row =
            table.insert(textentry::text().c_str(), g_pending_time, true);
        highscores::save(kScoreKeys[(int)g_level], table);
        scoreboard::open("MINESWEEPER - BEST TIMES", kScoreKeys, kScoreLabels,
                         3, (int)g_level, true, row);
      }
      g_swallow_touch = true;
      g_dirty = true;
    }
    return;
  }
  if (scoreboard::active()) {
    if (scoreboard::handleTap(x, y) == scoreboard::Result::Closed) {
      g_swallow_touch = true;
      g_dirty = true;
    }
    return;
  }

  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return;

  switch ((Action)action) {
    case Action::Mode:
      g_flag_mode = !g_flag_mode;
      audio::select();
      break;
    case Action::Level:
      audio::select();
      if (gameInProgress()) {
        g_pending = Pending::Level;
      } else {
        g_level = (mines::Level)(((int)g_level + 1) % 3);
        newGame();
      }
      break;
    case Action::NewGame:
      audio::select();
      if (gameInProgress()) g_pending = Pending::NewGame;
      else newGame();
      break;
    case Action::ConfirmYes:
      audio::select();
      if (g_pending == Pending::Level) {
        g_level = (mines::Level)(((int)g_level + 1) % 3);
      }
      g_pending = Pending::None;
      newGame();
      break;
    case Action::ConfirmNo:
      audio::select();
      g_pending = Pending::None;
      // The tap that dismissed this is still in contact with the grid.
      g_swallow_touch = true;
      break;
    case Action::Menu:
      audio::select();
      app::requestExit();
      break;
    case Action::About:
      audio::select();
      app::showAbout();
      break;
    case Action::Scores:
      audio::select();
      scoreboard::open("MINESWEEPER — BEST TIMES", kScoreKeys, kScoreLabels, 3,
                       (int)g_level, true);
      break;
    case Action::None:
      break;
  }
  g_dirty = true;
}

}  // namespace mines_ui
}  // namespace tabulous
