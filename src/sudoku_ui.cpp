#include "sudoku_ui.h"

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
namespace sudoku_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Action : uint8_t {
  None, Cell, Digit, Erase, Notes, NewGame, Level, Menu, About, Scores,
};

sudoku::Puzzle *g_puzzle = nullptr;
bool g_dirty = true;
bool g_notes_mode = false;
sudoku::Difficulty g_level = sudoku::Difficulty::Medium;
int g_sel_x = 4, g_sel_y = 4;
uint32_t g_started_ms = 0;
uint32_t g_elapsed_s = 0;
bool g_was_solved = false;
uint32_t g_gen_ms = 0;

// NVS keys are capped at 15 characters.
const char *const kScoreKeys[4] = {"hs_sud_0", "hs_sud_1", "hs_sud_2",
                                   "hs_sud_3"};
const char *const kScoreLabels[4] = {"Easy", "Medium", "Hard", "Expert"};
uint32_t g_pending_time = 0;

// The grid fills the height; the pad takes the rest of the width. At ~294 PPI
// a 76 px cell is about 6.5 mm — smaller than the Minesweeper minimum, but a
// mis-tap here only moves a selection, which costs one more tap to undo.
constexpr int kCell = 76;
constexpr int kGridX = 16;
constexpr int kGridY = (kH - kCell * sudoku::kN) / 2;
constexpr int kGridSize = kCell * sudoku::kN;
constexpr int kPadX = kGridX + kGridSize + 24;
constexpr int kPadW = kW - kPadX - kMargin;

uint16_t kEntryColor = rgb(0x4C8DFF);

bool sameBox(int x1, int y1, int x2, int y2) {
  return x1 / 3 == x2 / 3 && y1 / 3 == y2 / 3;
}

void drawCell(int x, int y) {
  auto &g = gfx();
  const int px = kGridX + x * kCell;
  const int py = kGridY + y * kCell;
  const uint8_t v = g_puzzle->value(x, y);
  const bool selected = (x == g_sel_x && y == g_sel_y);
  const uint8_t sel_v = g_puzzle->value(g_sel_x, g_sel_y);

  uint16_t bg = kBg;
  if (selected) {
    bg = kSurfaceLift;
  } else if (x == g_sel_x || y == g_sel_y || sameBox(x, y, g_sel_x, g_sel_y)) {
    // Peer highlighting: the squares that constrain the selection. This is
    // most of what makes Sudoku playable on a screen rather than paper.
    bg = kSurface;
  } else if (v != 0 && v == sel_v) {
    bg = kSurface;  // every square holding the same digit
  }

  g.fillRect(px + 1, py + 1, kCell - 2, kCell - 2, bg);

  if (v != 0) {
    const uint16_t colour = g_puzzle->conflicts(x, y) ? kDanger
                            : g_puzzle->isGiven(x, y) ? kText
                                                      : kEntryColor;
    char label[2] = {(char)('0' + v), '\0'};
    drawLabel(label, px + kCell / 2, py + kCell / 2, colour,
              &fonts::FreeSansBold24pt7b, middle_center);
    return;
  }

  if (!g_puzzle->hasNotes(x, y)) return;
  for (uint8_t n = 1; n <= 9; n++) {
    if (!g_puzzle->note(x, y, n)) continue;
    const int nx = px + 14 + ((n - 1) % 3) * 24;
    const int ny = py + 16 + ((n - 1) / 3) * 22;
    char label[2] = {(char)('0' + n), '\0'};
    drawLabel(label, nx, ny, kMuted, &fonts::FreeSans9pt7b, middle_center);
  }
}

void drawGrid() {
  auto &g = gfx();
  g.fillRect(kGridX - 4, kGridY - 4, kGridSize + 8, kGridSize + 8, kBg);
  for (int y = 0; y < sudoku::kN; y++) {
    for (int x = 0; x < sudoku::kN; x++) drawCell(x, y);
  }
  // Thin lines everywhere, thick on the box boundaries.
  for (int i = 0; i <= sudoku::kN; i++) {
    const int t = (i % 3 == 0) ? 3 : 1;
    const uint16_t c = (i % 3 == 0) ? kText : kSurfaceLift;
    g.fillRect(kGridX + i * kCell - t / 2, kGridY - 1, t, kGridSize + 2, c);
    g.fillRect(kGridX - 1, kGridY + i * kCell - t / 2, kGridSize + 2, t, c);
  }
  // The selection gets a ring so it survives the peer highlighting.
  g.drawRect(kGridX + g_sel_x * kCell, kGridY + g_sel_y * kCell, kCell, kCell,
             kAccent);
  g.drawRect(kGridX + g_sel_x * kCell + 1, kGridY + g_sel_y * kCell + 1,
             kCell - 2, kCell - 2, kAccent);
}

void drawHelpButton(bool add_target);

void drawStatusPatch() {
  auto &g = gfx();
  g.fillRect(kPadX, 18, kPadW, 84, kBg);
  drawLabel(sudoku::difficultyName(g_level), kPadX, 20, kMuted,
            &fonts::FreeSans9pt7b);

  char buf[32];
  if (g_puzzle->solved()) {
    drawLabel("SOLVED", kPadX, 44, kGood, &fonts::FreeSansBold24pt7b);
  } else {
    snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(g_elapsed_s / 60),
             (unsigned)(g_elapsed_s % 60));
    drawLabel(buf, kPadX, 44, kText, &fonts::FreeSansBold24pt7b);
  }

  snprintf(buf, sizeof(buf), "%d clues", g_puzzle->givenCount());
  drawLabel(buf, kPadX + kPadW - 78, 30, kMuted, &fonts::FreeSans9pt7b,
            top_right);
  drawHelpButton(false);

}

// A consistent "?" in the same corner on every game, so it is findable
// without hunting.
//
// drawStatusPatch() repaints this corner once a second for the clock, so it
// has to redraw the button or it vanishes. But that path does not clear the
// target list, so it must NOT register one — hence the flag. Only the full
// repaint registers the target.
void drawHelpButton(bool add_target) {
  const Rect help{kPadX + kPadW - 64, 18, 64, 64};
  uikit::fillRoundRectFast(help.x, help.y, help.w, help.h, 32, kSurfaceLift);
  drawLabel("?", help.x + 32, help.y + 32, kText, &fonts::FreeSansBold18pt7b,
            middle_center);
  if (add_target) uikit::addTarget(help, (int)Action::About);
}

void drawPad() {
  const int bw = (kPadW - 2 * 12) / 3;
  const int bh = 110;
  const int top = 120;

  for (int i = 0; i < 9; i++) {
    const uint8_t n = (uint8_t)(i + 1);
    const Rect r{kPadX + (i % 3) * (bw + 12), top + (i / 3) * (bh + 8), bw, bh};
    // A digit already placed nine times is dimmed rather than removed: the
    // pad must not reshuffle under the player's fingers.
    const bool done = g_puzzle->remaining(n) <= 0;
    char label[2] = {(char)('0' + n), '\0'};
    drawButton(r, label, done ? kBg : kSurfaceLift, done ? kMuted : kText,
               &fonts::FreeSansBold24pt7b);
    uikit::addTarget(r, (int)Action::Digit, n);
  }

  const int row2 = top + 3 * (bh + 8) + 6;
  const Rect erase{kPadX, row2, (kPadW - 12) / 2, 84};
  const Rect notes{kPadX + (kPadW - 12) / 2 + 12, row2, (kPadW - 12) / 2, 84};
  drawButton(erase, "ERASE", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  drawButton(notes, g_notes_mode ? "NOTES: ON" : "NOTES: OFF",
             g_notes_mode ? kAccent : kSurfaceLift,
             g_notes_mode ? kInk : kMuted, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(erase, (int)Action::Erase);
  uikit::addTarget(notes, (int)Action::Notes);

  const int row3 = row2 + 96;
  const int w3 = (kPadW - 3 * 8) / 4;
  const Rect fresh{kPadX, row3, w3, 72};
  const Rect lvl{kPadX + (w3 + 8), row3, w3, 72};
  const Rect scores{kPadX + 2 * (w3 + 8), row3, w3, 72};
  const Rect menu{kPadX + 3 * (w3 + 8), row3, w3, 72};
  drawButton(fresh, "NEW", kSurfaceLift, kText, &fonts::FreeSans9pt7b);
  drawButton(lvl, sudoku::difficultyName(g_level), kSurfaceLift, kText,
             &fonts::FreeSans9pt7b);
  drawButton(scores, "SCORES", kSurfaceLift, kText, &fonts::FreeSans9pt7b);
  drawButton(menu, "MENU", kSurfaceLift, kMuted, &fonts::FreeSans9pt7b);
  uikit::addTarget(fresh, (int)Action::NewGame);
  uikit::addTarget(lvl, (int)Action::Level);
  uikit::addTarget(scores, (int)Action::Scores);
  uikit::addTarget(menu, (int)Action::Menu);
}

void repaintAll() {
  uikit::clearTargets();
  gfx().fillScreen(kBg);
  drawGrid();
  uikit::addTarget(Rect{kGridX, kGridY, kGridSize, kGridSize},
                   (int)Action::Cell);
  drawStatusPatch();
  drawHelpButton(true);
  drawPad();
  uikit::present();
}

void newGame() {
  const uint32_t t0 = millis();
  g_puzzle->generate(g_level, (uint32_t)esp_random());
  g_gen_ms = millis() - t0;
  Serial.printf("[sudoku] generated %s in %u ms (%d clues)\n",
                sudoku::difficultyName(g_level), (unsigned)g_gen_ms,
                g_puzzle->givenCount());
  g_sel_x = g_sel_y = 4;
  g_started_ms = 0;
  g_elapsed_s = 0;
  g_was_solved = false;
  g_notes_mode = false;
}

}  // namespace

void begin(sudoku::Puzzle *puzzle) {
  g_puzzle = puzzle;
  g_level = sudoku::Difficulty::Medium;
  newGame();
  g_dirty = true;
}

void invalidate() { g_dirty = true; }

void tick(uint32_t now_ms) {
  if (!g_puzzle) return;

  // Modals own the screen while they are up.
  if (textentry::active()) {
    textentry::tick(now_ms);
    return;
  }
  if (scoreboard::active()) {
    scoreboard::tick(now_ms);
    return;
  }

  if (!g_puzzle->solved()) {
    if (g_started_ms == 0) g_started_ms = now_ms;
    const uint32_t secs = (now_ms - g_started_ms) / 1000;
    if (secs != g_elapsed_s) {
      g_elapsed_s = secs;
      // Only the clock patch — redrawing 81 cells every second would block the
      // loop and swallow taps.
      drawStatusPatch();
      uikit::present();
    }
  }

  if (g_dirty) {
    g_dirty = false;
    const uint32_t t0 = micros();
    repaintAll();
    uikit::noteRepaint(micros() - t0, 82);
  }
}

void handleTap(int x, int y, uint32_t now_ms) {
  (void)now_ms;
  if (!g_puzzle) return;

  if (textentry::active()) {
    if (textentry::handleTap(x, y) == textentry::Result::Closed) {
      if (textentry::accepted()) {
        highscores::Table table = highscores::load(kScoreKeys[(int)g_level]);
        const int row =
            table.insert(textentry::text().c_str(), g_pending_time, true);
        highscores::save(kScoreKeys[(int)g_level], table);
        scoreboard::open("SUDOKU - BEST TIMES", kScoreKeys, kScoreLabels, 4,
                         (int)g_level, true, row);
      }
      g_dirty = true;
    }
    return;
  }
  if (scoreboard::active()) {
    if (scoreboard::handleTap(x, y) == scoreboard::Result::Closed) g_dirty = true;
    return;
  }

  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return;

  switch ((Action)action) {
    case Action::Cell: {
      const int cx = (x - kGridX) / kCell;
      const int cy = (y - kGridY) / kCell;
      if (cx < 0 || cy < 0 || cx >= sudoku::kN || cy >= sudoku::kN) return;
      g_sel_x = cx;
      g_sel_y = cy;
      audio::select();
      break;
    }
    case Action::Digit: {
      const uint8_t n = (uint8_t)param;
      if (g_notes_mode) {
        g_puzzle->toggleNote(g_sel_x, g_sel_y, n);
        audio::select();
      } else {
        // Tapping the digit already in the square clears it, so correcting a
        // mistake doesn't mean reaching for ERASE.
        if (g_puzzle->value(g_sel_x, g_sel_y) == n) {
          g_puzzle->clear(g_sel_x, g_sel_y);
        } else {
          g_puzzle->set(g_sel_x, g_sel_y, n);
        }
        if (g_puzzle->solved() && !g_was_solved) {
          g_was_solved = true;
          audio::fanfare();
          // Only prompt if the time actually places: being asked for a name
          // and then not appearing would be worse than not being asked.
          g_pending_time = g_elapsed_s;
          const highscores::Table table =
              highscores::load(kScoreKeys[(int)g_level]);
          if (table.qualifies(g_pending_time, true)) {
            textentry::open("YOUR NAME", "", highscores::kNameLen);
          }
        } else if (g_puzzle->conflicts(g_sel_x, g_sel_y)) {
          audio::reject();
        } else {
          audio::correct();
        }
      }
      break;
    }
    case Action::Erase:
      g_puzzle->clear(g_sel_x, g_sel_y);
      audio::select();
      break;
    case Action::Notes:
      g_notes_mode = !g_notes_mode;
      audio::select();
      break;
    case Action::NewGame:
      audio::select();
      newGame();
      break;
    case Action::Level:
      g_level = (sudoku::Difficulty)(((int)g_level + 1) % 4);
      audio::select();
      newGame();
      break;
    case Action::Menu:
      audio::select();
      app::requestExit();
      break;
    case Action::Scores:
      audio::select();
      scoreboard::open("SUDOKU - BEST TIMES", kScoreKeys, kScoreLabels, 4,
                       (int)g_level, true);
      break;
    case Action::About:
      audio::select();
      app::showAbout();
      break;
    case Action::None:
      break;
  }
  g_dirty = true;
}

}  // namespace sudoku_ui
}  // namespace tabulous
