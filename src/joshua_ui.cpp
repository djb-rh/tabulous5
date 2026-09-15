#include "joshua_ui.h"

#include <M5Unified.h>
#include <esp_random.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "audio.h"
#include "font_sharetech54.h"
#include "theme.h"
#include "tictactoe.h"
#include "uikit.h"

namespace tabulous {
namespace joshua_ui {
namespace {

using joshua::Mark;
using theme::kH;
using theme::kMargin;
using theme::kW;
using theme::rgb;
using uikit::Rect;
using uikit::gfx;

enum class Action : uint8_t { None, Entry, Option, Cell, Menu, About };
enum class Mode : uint8_t { Zero = 0, One = 1, Two = 2 };
enum class State : uint8_t {
  Attract, AttractChosen, Players, PlayersChosen, Game, BoardHold, Crash,
  Blank, Message,
};

// The film's palette. Fixed rather than themed: this is a terminal in a dark
// room, and a light-mode WOPR is nobody's WOPR.
constexpr uint16_t kBlack = rgb(0x000000);
constexpr uint16_t kMain = rgb(0xD8D4FF);   // lavender-white terminal text
constexpr uint16_t kEntry = rgb(0x7CFFB2);  // phosphor green, the "typed" font
constexpr uint16_t kBoard = rgb(0xE6E8FF);  // border, grid and marks
constexpr uint16_t kDim = rgb(0x4A4870);    // the console's own two controls

// The terminal font is Share Tech Mono, the thin rounded monospace the
// ESPHome build uses for WOPR's own lines, converted at 54 px so it stands a
// little taller than the typed answers, as in the film. The "typed" font is
// the 5x7 GLCD glyph set blown up, which is the closest thing to the film's
// dot matrix that costs nothing.
const lgfx::IFont *const kFontMain = &fontdata::ShareTechMono54;
const lgfx::IFont *const kFontEntry = &fonts::Font0;
constexpr uint8_t kEntrySize = 6;

const char *const kAttractPrompt = "WOULD YOU LIKE TO PLAY A GAME?";
const char *const kAttractAnswer = "TIC TAC TOE";
const char *const kPlayersPrompt = "HOW MANY PLAYERS?";
const char *const kOptions[3] = {"ZERO", "ONE", "TWO"};
const char *const kNotToPlay = "THE ONLY WAY TO WIN\nIS NOT TO PLAY";
const char *const kStalemate = "STALEMATE.\nTHE ONLY WAY TO WIN\nIS NOT TO PLAY";
char g_winner_text[16];

// Timing, in milliseconds. Same values as the ESPHome build.
constexpr uint32_t kTypeMsMain = 45;
constexpr uint32_t kTypeMsEntry = 70;
constexpr uint32_t kSelfPlayStartMs = 1500;
constexpr uint32_t kSelfPlayMinMs = 9;
constexpr uint32_t kSelfPlayRampMs = 15000;
constexpr uint32_t kSelfPlayHoldMs = 1500;
constexpr uint32_t kCrashMs = 2500;
constexpr uint32_t kCrashFrameMs = 40;
constexpr uint32_t kBlankMs = 700;
constexpr uint32_t kComputerThinkMs = 600;
constexpr uint32_t kBoardHoldMs = 1500;
constexpr uint32_t kWinnerHoldMs = 4000;
constexpr uint32_t kMessageHoldMs = 5000;
constexpr uint32_t kChoiceHoldMs = 500;

// Board proportions, measured from the film frame and expressed as fractions
// of the cell pitch P.
constexpr float kGridLine = 0.078f;
constexpr float kXSize = 0.77f;
constexpr float kOSize = 0.83f;
constexpr float kStroke = 0.14f;
constexpr float kBorderPadX = 0.68f;
constexpr float kBorderPadY = 0.17f;
constexpr float kBorderThick = 0.052f;
constexpr float kPitchOfHeight = 0.26f;

inline bool reached(uint32_t now, uint32_t when) {
  return (int32_t)(now - when) >= 0;
}
uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }

// ------------------------------------------------------------------ state

State g_state = State::Attract;
Mode g_mode = Mode::Two;
joshua::TicTacToe g_game;
uint32_t g_deadline = 0;
uint32_t g_self_start = 0;
uint32_t g_next_move = 0;
bool g_computer_pending = false;
uint32_t g_pending_hold = 0;
int g_chosen_option = -1;
bool g_dirty = true;
bool g_painted = false;  // something was drawn this tick; present() once

// Text-screen metrics, measured from the fonts at begin().
int g_cw_main = 29, g_lh_main = 47;
int g_cw_entry = 36, g_lh_entry = 48;
int g_margin_x = kW / 20;
int g_prompt_y = kH * 3 / 10;

// The typewriter. The prompt and the answer are typed one after the other,
// each with its own font and origin; `g_typing` says which is in progress.
enum class Typing : uint8_t { None, Prompt, Entry };
Typing g_typing = Typing::None;
const char *g_prompt = "";
int g_prompt_pos = 0;
int g_entry_pos = -1;  // -1: the answer row is not on screen
uint32_t g_type_next = 0;
uint8_t g_options_shown = 0;  // bit per option
bool g_cursor_shown = false;
int g_cursor_x = 0, g_cursor_y = 0;

// Board metrics, computed once.
struct Board {
  int P, gx, gy, bx, by, bw, bh, bt, line, xs, os, stroke;
} g_b;
Mark g_drawn[9];

// Crash text.
int g_crash_cols = 36, g_crash_rows = 16;

// ------------------------------------------------------------- text screen

int promptLines() {
  int n = 1;
  for (const char *p = g_prompt; *p; p++) n += (*p == '\n');
  return n;
}
int entryY() { return g_prompt_y + promptLines() * g_lh_main + g_lh_main / 2; }
int entryPad() { return g_lh_entry / 3; }

void setEntryFont() {
  auto &g = gfx();
  g.setFont(kFontEntry);
  g.setTextSize(kEntrySize);
  g.setTextDatum(top_left);
  g.setTextColor(kEntry);
}
void setMainFont() {
  auto &g = gfx();
  g.setFont(kFontMain);
  g.setTextSize(1);
  g.setTextDatum(top_left);
  g.setTextColor(kMain);
}

// Draws characters [from, to) of `text` in the current font, from (x0, y0),
// honouring newlines. Every glyph lands on a fixed grid, so a character can
// be drawn on its own later without disturbing its neighbours.
void drawChars(const char *text, int from, int to, int x0, int y0, int cw,
               int lh) {
  int col = 0, row = 0;
  for (int i = 0; i < to; i++) {
    const char c = text[i];
    if (c == '\n') {
      row++;
      col = 0;
      continue;
    }
    if (i >= from) {
      const char s[2] = {c, 0};
      gfx().drawString(s, x0 + col * cw, y0 + row * lh);
    }
    col++;
  }
  g_painted = true;
}

void drawPrompt(int from) {
  setMainFont();
  drawChars(g_prompt, from, g_prompt_pos, g_margin_x, g_prompt_y, g_cw_main,
            g_lh_main);
  gfx().setTextSize(1);
}

void drawEntry(int from) {
  if (g_entry_pos < 0) return;
  setEntryFont();
  drawChars(kAttractAnswer, from, g_entry_pos, g_margin_x, entryY(),
            g_cw_entry, g_lh_entry);
  gfx().setTextSize(1);
}

int optionX(int i) {
  int x = g_margin_x;
  for (int k = 0; k < i; k++) {
    x += (int)strlen(kOptions[k]) * g_cw_entry + 3 * g_cw_entry;
  }
  return x;
}
int optionW(int i) { return (int)strlen(kOptions[i]) * g_cw_entry; }
Rect optionRect(int i) {
  const int pad = entryPad();
  return Rect{optionX(i) - pad, entryY() - pad, optionW(i) + 2 * pad,
              g_lh_entry + 2 * pad};
}

void drawOptions() {
  setEntryFont();
  for (int i = 0; i < 3; i++) {
    if (!(g_options_shown & (1 << i))) continue;
    gfx().drawString(kOptions[i], optionX(i), entryY());
  }
  gfx().setTextSize(1);
  g_painted = true;
}

void drawCursor() {
  const int ch = 7 * kEntrySize;  // the cap height of the 5x7 glyphs
  gfx().fillRect(g_cursor_x, g_cursor_y, g_cw_entry, ch,
                 g_cursor_shown ? kEntry : kBlack);
  g_painted = true;
}

void setCursorVisible(bool v) {
  if (v == g_cursor_shown) return;
  g_cursor_shown = v;
  drawCursor();
}

// Parks the cursor one quarter-space after the end of a run of entry-font
// text that starts at x and is `chars` long.
void placeCursorAfter(int x, int chars) {
  if (g_cursor_shown) {
    g_cursor_shown = false;
    drawCursor();
  }
  g_cursor_x = x + chars * g_cw_entry + g_cw_entry / 4;
  g_cursor_y = entryY();
}

// MENU and ? are only offered on the terminal screens, tucked into the
// corners in the terminal's own dim ink so they stay out of the film.
void drawShellControls() {
  const Rect menu{kMargin, kH - 84, 150, 60};
  const Rect help{kW - kMargin - 64, kH - 84, 64, 64};
  uikit::drawLabel("MENU", menu.x + menu.w / 2, menu.y + menu.h / 2, kDim,
                   &fonts::FreeMonoBold18pt7b, middle_center);
  uikit::drawLabel("?", help.x + 32, help.y + 32, kDim,
                   &fonts::FreeMonoBold18pt7b, middle_center);
  uikit::addTarget(menu, (int)Action::Menu);
  uikit::addTarget(help, (int)Action::About);
}

void beginTyping(Typing what, uint32_t now) {
  g_typing = what;
  g_type_next = now;
}

// Types whatever is due. Returns true if the run just finished.
bool stepTyping(uint32_t now) {
  if (g_typing == Typing::None) return false;
  const char *text = g_typing == Typing::Prompt ? g_prompt : kAttractAnswer;
  int *pos = g_typing == Typing::Prompt ? &g_prompt_pos : &g_entry_pos;
  const uint32_t per = g_typing == Typing::Prompt ? kTypeMsMain : kTypeMsEntry;
  const int len = (int)strlen(text);
  const int from = *pos;
  while (*pos < len && reached(now, g_type_next)) {
    (*pos)++;
    g_type_next += per;
  }
  if (*pos > from) {
    if (g_typing == Typing::Prompt) drawPrompt(from);
    else drawEntry(from);
  }
  if (*pos >= len) {
    g_typing = Typing::None;
    return true;
  }
  return false;
}

void finishTyping() {
  if (g_typing == Typing::None) return;
  const char *text = g_typing == Typing::Prompt ? g_prompt : kAttractAnswer;
  int *pos = g_typing == Typing::Prompt ? &g_prompt_pos : &g_entry_pos;
  const int from = *pos;
  *pos = (int)strlen(text);
  if (g_typing == Typing::Prompt) drawPrompt(from);
  else drawEntry(from);
  g_typing = Typing::None;
}

// Everything a terminal screen currently shows, from scratch. Used on entry
// to the screen and whenever the shell hands the screen back.
void repaintText() {
  gfx().fillScreen(kBlack);
  drawPrompt(0);
  drawEntry(0);
  drawOptions();
  if (g_cursor_shown) drawCursor();
  if (g_state == State::Attract) {
    const int pad = entryPad();
    uikit::addTarget(Rect{g_margin_x - pad, entryY() - pad,
                          kW - 2 * g_margin_x + 2 * pad, g_lh_entry + 2 * pad},
                     (int)Action::Entry);
  }
  if (g_state == State::Players) {
    for (int i = 0; i < 3; i++) {
      uikit::addTarget(optionRect(i), (int)Action::Option, i);
    }
  }
  drawShellControls();
  g_painted = true;
}

void resetText(const char *prompt) {
  g_prompt = prompt;
  g_prompt_pos = 0;
  g_entry_pos = -1;
  g_options_shown = 0;
  g_cursor_shown = false;
  g_chosen_option = -1;
  g_typing = Typing::None;
}

// ------------------------------------------------------------------ board

void computeBoard() {
  Board &b = g_b;
  b.P = (int)(kH * kPitchOfHeight);
  const int grid = 3 * b.P;
  b.line = (int)(b.P * kGridLine + 0.5f);
  b.bt = (int)(b.P * kBorderThick + 0.5f);
  if (b.bt < 3) b.bt = 3;
  const int padx = (int)(b.P * kBorderPadX);
  const int pady = (int)(b.P * kBorderPadY);
  b.bw = grid + 2 * padx + 2 * b.bt;
  b.bh = grid + 2 * pady + 2 * b.bt;
  b.bx = (kW - b.bw) / 2;
  b.by = (kH - b.bh) / 2;
  b.gx = b.bx + b.bt + padx;
  b.gy = b.by + b.bt + pady;
  b.xs = (int)(b.P * kXSize);
  b.os = (int)(b.P * kOSize);
  b.stroke = (int)(b.P * kStroke + 0.5f);
}

void drawBoardStatic() {
  auto &g = gfx();
  const Board &b = g_b;
  g.fillScreen(kBlack);
  // The border: four bars, square corners, as in the film.
  g.fillRect(b.bx, b.by, b.bw, b.bt, kBoard);
  g.fillRect(b.bx, b.by + b.bh - b.bt, b.bw, b.bt, kBoard);
  g.fillRect(b.bx, b.by, b.bt, b.bh, kBoard);
  g.fillRect(b.bx + b.bw - b.bt, b.by, b.bt, b.bh, kBoard);
  const int grid = 3 * b.P;
  for (int i = 1; i <= 2; i++) {
    g.fillRect(b.gx, b.gy + i * b.P - b.line / 2, grid, b.line, kBoard);
    g.fillRect(b.gx + i * b.P - b.line / 2, b.gy, b.line, grid, kBoard);
  }
  g_painted = true;
}

Rect cellRect(int i) {
  return Rect{g_b.gx + (i % 3) * g_b.P, g_b.gy + (i / 3) * g_b.P, g_b.P,
              g_b.P};
}

// A flat-ended stroke from (x0,y0) to (x1,y1) of width w, as two triangles.
// drawWideLine would round the ends; the film's X has square ones.
void strokeSquare(int x0, int y0, int x1, int y1, int w, uint16_t c) {
  const float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
  const int ax = (int)lroundf(x0 + nx), ay = (int)lroundf(y0 + ny);
  const int bx = (int)lroundf(x1 + nx), by = (int)lroundf(y1 + ny);
  const int cx = (int)lroundf(x1 - nx), cy = (int)lroundf(y1 - ny);
  const int ddx = (int)lroundf(x0 - nx), ddy = (int)lroundf(y0 - ny);
  gfx().fillTriangle(ax, ay, bx, by, cx, cy, c);
  gfx().fillTriangle(ax, ay, cx, cy, ddx, ddy, c);
}

void drawMark(int i, Mark m) {
  auto &g = gfx();
  const Board &b = g_b;
  const Rect r = cellRect(i);
  const int cx = r.x + b.P / 2, cy = r.y + b.P / 2;
  // Clear the cell interior, inside the grid lines.
  const int inset = b.line / 2 + 1;
  g.fillRect(r.x + inset, r.y + inset, b.P - 2 * inset, b.P - 2 * inset,
             kBlack);
  if (m == Mark::X) {
    // A flat cap on a 45-degree stroke sticks out stroke/(2*sqrt2) past the
    // endpoint, so inset by that much to keep the glyph in an xs-by-xs box.
    const int in = (int)(b.stroke * 0.3536f + 0.5f);
    const int x0 = cx - b.xs / 2 + in, y0 = cy - b.xs / 2 + in;
    const int x1 = cx + b.xs / 2 - in, y1 = cy + b.xs / 2 - in;
    strokeSquare(x0, y0, x1, y1, b.stroke, kBoard);
    strokeSquare(x1, y0, x0, y1, b.stroke, kBoard);
  } else if (m == Mark::O) {
    g.fillArc(cx, cy, b.os / 2 - b.stroke, b.os / 2, 0, 360, kBoard);
  }
  g_painted = true;
}

// Draws only the cells whose mark changed since the last sync. The zero-player
// mode calls this dozens of times a second at full speed, so it has to be
// cheap when nothing moved.
void syncBoard() {
  for (int i = 0; i < 9; i++) {
    const Mark m = g_game.at(i);
    if (m == g_drawn[i]) continue;
    g_drawn[i] = m;
    drawMark(i, m);
  }
}

void repaintBoard() {
  drawBoardStatic();
  for (int i = 0; i < 9; i++) g_drawn[i] = Mark::None;
  syncBoard();
  for (int i = 0; i < 9; i++) {
    uikit::addTarget(cellRect(i), (int)Action::Cell, i);
  }
}

// ------------------------------------------------------------------ crash

void crashFrame() {
  static const char kChars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*+-=/<>?@[]^_{}|~   ";
  const int nchars = (int)sizeof(kChars) - 1;
  auto &g = gfx();
  uint16_t fg = kEntry, bg = kBlack;
  const uint32_t r = rnd(12);
  if (r == 6 || r == 7) {
    fg = kMain;
  } else if (r == 8 || r == 9) {
    fg = kBlack;
    bg = kEntry;
  } else if (r == 10) {
    fg = kBlack;
    bg = kMain;
  } else if (r == 11) {
    fg = kBlack;  // a dropped frame
  }
  g.fillScreen(bg);
  g_painted = true;
  if (fg == bg) return;
  setEntryFont();
  g.setTextColor(fg);
  const int ox = -(int)rnd(24), oy = -(int)rnd(12);
  char row[80];
  const int cols = g_crash_cols < 79 ? g_crash_cols : 79;
  for (int y = 0; y < g_crash_rows; y++) {
    for (int c = 0; c < cols; c++) row[c] = kChars[rnd(nchars)];
    row[cols] = 0;
    g.drawString(row, ox, oy + y * g_lh_entry);
  }
  g.setTextSize(1);
}

// ---------------------------------------------------------------- screens

void showAttract(uint32_t now) {
  g_state = State::Attract;
  g_game.reset();
  g_computer_pending = false;
  resetText(kAttractPrompt);
  beginTyping(Typing::Prompt, now);
  g_dirty = true;
}

void showPlayers(uint32_t now) {
  g_state = State::Players;
  resetText(kPlayersPrompt);
  beginTyping(Typing::Prompt, now);
  g_dirty = true;
}

void showMessage(const char *text, uint32_t hold_ms, uint32_t now) {
  g_state = State::Message;
  resetText(text);
  g_pending_hold = hold_ms;
  g_deadline = 0;
  beginTyping(Typing::Prompt, now);
  g_dirty = true;
}

void startGame(Mode mode, uint32_t now) {
  g_mode = mode;
  g_state = State::Game;
  g_game.reset();
  g_computer_pending = false;
  g_self_start = now;
  g_next_move = now + kSelfPlayStartMs;
  g_dirty = true;
}

void showCrash(uint32_t now) {
  g_state = State::Crash;
  g_deadline = now + kCrashMs;
  g_next_move = now;
  audio::boom();
  uikit::clearTargets();
}

uint32_t selfPlayInterval(uint32_t elapsed) {
  if (elapsed >= kSelfPlayRampMs) return kSelfPlayMinMs;
  const float f = (float)elapsed / (float)kSelfPlayRampMs;
  const float ratio = (float)kSelfPlayMinMs / (float)kSelfPlayStartMs;
  float ms = (float)kSelfPlayStartMs * powf(ratio, f);
  if (ms < (float)kSelfPlayMinMs) ms = (float)kSelfPlayMinMs;
  return (uint32_t)ms;
}

void onGameOver(uint32_t now) {
  g_state = State::BoardHold;
  g_computer_pending = false;
  g_deadline = now + kBoardHoldMs;
}

void repaintAll() {
  uikit::clearTargets();
  switch (g_state) {
    case State::Attract:
    case State::AttractChosen:
    case State::Players:
    case State::PlayersChosen:
    case State::Message:
      repaintText();
      break;
    case State::Game:
    case State::BoardHold:
      repaintBoard();
      break;
    case State::Crash:
      break;  // paints itself every frame
    case State::Blank:
      gfx().fillScreen(kBlack);
      g_painted = true;
      break;
  }
}

}  // namespace

void begin() {
  auto &g = gfx();
  setMainFont();
  g_cw_main = g.textWidth("M");
  g_lh_main = g.fontHeight();
  setEntryFont();
  g_cw_entry = g.textWidth("M");
  g_lh_entry = g.fontHeight();
  g.setTextSize(1);
  g_margin_x = kW / 20;
  g_prompt_y = kH * 3 / 10;
  g_crash_cols = kW / g_cw_entry + 1;
  g_crash_rows = kH / g_lh_entry + 1;
  computeBoard();
  showAttract(millis());
}

void invalidate() { g_dirty = true; }

void tick(uint32_t now) {
  g_painted = false;
  if (g_dirty) {
    g_dirty = false;
    repaintAll();
  }

  switch (g_state) {
    case State::Attract:
      if (g_typing == Typing::Prompt) {
        if (stepTyping(now)) {
          // Prompt finished: "type" the answer under it after a beat.
          g_entry_pos = 0;
          beginTyping(Typing::Entry, now + 400);
        }
      } else if (g_typing == Typing::Entry) {
        if (stepTyping(now)) placeCursorAfter(g_margin_x, (int)strlen(kAttractAnswer));
      } else if (g_entry_pos >= 0) {
        setCursorVisible(((now / 500) & 1) == 0);
      }
      break;

    case State::AttractChosen:
      if (reached(now, g_deadline)) showPlayers(now);
      break;

    case State::Players:
      if (g_typing == Typing::Prompt) {
        if (stepTyping(now)) {
          g_options_shown = 0x7;
          drawOptions();
          for (int i = 0; i < 3; i++) {
            uikit::addTarget(optionRect(i), (int)Action::Option, i);
          }
          placeCursorAfter(optionX(2), optionW(2) / g_cw_entry);
        }
      } else if (g_chosen_option < 0) {
        setCursorVisible(((now / 500) & 1) == 0);
      }
      break;

    case State::PlayersChosen:
      if (reached(now, g_deadline)) startGame((Mode)g_chosen_option, now);
      break;

    case State::Game:
      if (g_mode == Mode::Zero) {
        const uint32_t elapsed = now - g_self_start;
        if (elapsed >= kSelfPlayRampMs + kSelfPlayHoldMs) {
          showCrash(now);
          break;
        }
        int guard = 0;
        while (reached(now, g_next_move) && guard++ < 4) {
          if (g_game.is_over()) g_game.reset();
          else g_game.play(g_game.best_move(rnd));
          syncBoard();
          g_next_move += selfPlayInterval(elapsed);
          if ((int32_t)(now - g_next_move) > 250) g_next_move = now;
        }
      } else if (g_mode == Mode::One && g_computer_pending &&
                 reached(now, g_next_move)) {
        g_computer_pending = false;
        const int m = g_game.best_move(rnd);
        if (m >= 0) g_game.play(m);
        syncBoard();
        if (g_game.is_over()) onGameOver(now);
      }
      break;

    case State::BoardHold:
      if (reached(now, g_deadline)) {
        const Mark w = g_game.winner();
        if (w != Mark::None) {
          snprintf(g_winner_text, sizeof(g_winner_text), "WINNER: %c",
                   joshua::mark_char(w));
          audio::correct();
          showMessage(g_winner_text, kWinnerHoldMs, now);
        } else {
          showMessage(kStalemate, kMessageHoldMs, now);
        }
      }
      break;

    case State::Crash:
      if (reached(now, g_deadline)) {
        g_state = State::Blank;
        g_deadline = now + kBlankMs;
        g_dirty = true;
        break;
      }
      if (reached(now, g_next_move)) {
        crashFrame();
        g_next_move = now + kCrashFrameMs;
      }
      break;

    case State::Blank:
      if (reached(now, g_deadline)) showMessage(kNotToPlay, kMessageHoldMs, now);
      break;

    case State::Message:
      if (g_typing == Typing::Prompt) stepTyping(now);
      if (g_typing == Typing::None) {
        if (g_deadline == 0) g_deadline = now + g_pending_hold;
        if (reached(now, g_deadline)) showAttract(now);
      }
      break;
  }

  if (g_painted) uikit::present();
}

void handleTap(int x, int y, uint32_t now) {
  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return;
  g_painted = false;
  switch ((Action)action) {
    case Action::Entry:
      if (g_state != State::Attract || g_entry_pos < 0) break;
      audio::select();
      // Finish any in-progress typing instantly, then pause with a solid
      // cursor before moving on.
      if (g_typing == Typing::Entry) finishTyping();
      placeCursorAfter(g_margin_x, (int)strlen(kAttractAnswer));
      setCursorVisible(true);
      g_state = State::AttractChosen;
      g_deadline = now + kChoiceHoldMs;
      break;

    case Action::Option:
      if (g_state != State::Players || !(g_options_shown & (1 << param))) break;
      audio::select();
      g_chosen_option = param;
      for (int i = 0; i < 3; i++) {
        if (i == param) continue;
        const Rect r = optionRect(i);
        gfx().fillRect(r.x, r.y, r.w, r.h, kBlack);
        g_options_shown &= ~(1 << i);
      }
      placeCursorAfter(optionX(param), optionW(param) / g_cw_entry);
      setCursorVisible(true);
      g_state = State::PlayersChosen;
      g_deadline = now + kChoiceHoldMs;
      break;

    case Action::Cell:
      if (g_state != State::Game || g_mode == Mode::Zero) break;
      if (g_mode == Mode::One &&
          (g_game.turn() != Mark::X || g_computer_pending)) break;
      if (!g_game.play(param)) break;
      audio::select();
      syncBoard();
      if (g_game.is_over()) {
        onGameOver(now);
        break;
      }
      if (g_mode == Mode::One) {
        g_computer_pending = true;
        g_next_move = now + kComputerThinkMs + rnd(400);
      }
      break;

    case Action::Menu:
      audio::select();
      app::requestExit();
      break;

    case Action::About:
      audio::select();
      app::showAbout();
      break;

    case Action::None:
      break;
  }
  if (g_painted) uikit::present();
}

}  // namespace joshua_ui
}  // namespace tabulous
