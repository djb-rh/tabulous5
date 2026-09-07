#include "app.h"

#include "glyphs.h"

#include <M5Unified.h>

#include <cstring>

#include "audio.h"
#include "battery.h"
#include "contentserver.h"
#include "fivehead.h"
#include "fivehead_ui.h"
#include "minesweeper.h"
#include "minesweeper_ui.h"
#include "sudoku.h"
#include "solitaire.h"
#include "solitaire_ui.h"
#include "sudoku_ui.h"
#include "phrase_game.h"
#include "phrase_ui.h"
#include "settings_store.h"
#include "secrets.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace app {
namespace {

using namespace theme;
using uikit::Rect;

enum class Action : uint8_t {
  None, Launch, About, AboutBack, ScrollUp, ScrollDown, ExitYes, ExitNo,
  OpenEditor, CloseEditor, SwitchWifi, ToggleTheme,
  OpenSettings, CloseSettings, VolumeDown, VolumeUp, ToggleGame,
  MoveGameUp, MoveGameDown,
};

std::vector<Pack> *g_packs = nullptr;
content::LoadReport g_report;

GameId g_current = GameId::Menu;
bool g_confirming_exit = false;
bool g_editor_open = false;
bool g_settings_open = false;

// The launcher's arrangement, and the console-wide volume that the settings
// screen shares with PhraseCraze rather than keeping a second copy of.
settings_store::MenuPrefs g_menu;
Settings g_console;
int g_about = -1;  // entry index whose About screen is showing, or -1
bool g_about_over_game = false;  // opened from inside a game, not the menu
uint32_t g_revision_at_open = 0;
bool g_dirty = true;

Game g_phrase;                 // PhraseCraze rules
fivehead::Game g_five;         // FiveHead rules
mines::Board g_mines;          // Minesweeper board
sudoku::Puzzle g_sudoku;       // Sudoku puzzle
solitaire::Game g_solitaire;   // Klondike

void addAction(const Rect &r, Action a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

// ---------------------------------------------------------------- the menu

struct Entry {
  const char *name;
  const char *blurb;
  uint32_t color;
  glyphs::Glyph mark;
  GameId game;
  const char *about;  // newline-separated; shown on the About screen
};

// One action for every game, with the entry index as the parameter, so adding
// a game is a row in this table and nothing else.
const Entry kEntries[] = {
    {"PhraseCraze", "Describe it. Don't get caught holding it.", 0xE4572E,
     glyphs::Glyph::Bubble, GameId::PhraseCraze,
     "Two teams. Pick a category, then describe the phrase on screen\n"
     "without saying any word in it. When your team guesses, tap\n"
     "anywhere to pass the device to the other team.\n"
     "\n"
     "A hidden timer beeps faster and faster. Whoever is holding it when\n"
     "the buzzer sounds gives the other team a point - and that team then\n"
     "gets one guess at the phrase for a bonus point. First to 7 wins.\n"
     "\n"
     "SKIP changes the phrase. Each team has its own skips.\n"
     "\n"
     "In SETTINGS: scoring (classic, or a point per phrase), round length,\n"
     "difficulty including a kids mode, skips, points to win, and which\n"
     "word packs to use. Tap a team's card to rename it."},

    {"FiveHead", "On your forehead. Tilt to score.", 0x1D4ED8,
     glyphs::Glyph::Head, GameId::FiveHead,
     "One player holds the device against their forehead, screen facing\n"
     "everyone else. They describe or act out the phrase; the holder\n"
     "guesses it.\n"
     "\n"
     "Tilt the device DOWN to score it, UP to pass. Both are recorded and\n"
     "you see the whole list at the end of the round. A visible clock runs\n"
     "the turn, then it passes to the other team.\n"
     "\n"
     "The GOT IT and PASS buttons do the same thing if tilting isn't\n"
     "convenient.\n"
     "\n"
     "In SETTINGS: round length, turns per team, difficulty, which word\n"
     "packs to use, and tilt direction and sensitivity."},

    {"Minesweeper", "Dig, flag, don't guess.", 0x4C9F70,
     glyphs::Glyph::Mine, GameId::Minesweeper,
     "Tap a square to dig. A number says how many mines touch that\n"
     "square. Clear every square that isn't a mine to win.\n"
     "\n"
     "HOLD a square to flag it as a mine. The TAP = DIG / TAP = FLAG\n"
     "button swaps what a plain tap does, if you'd rather not hold.\n"
     "\n"
     "Tap a number that already has all its mines flagged and it opens\n"
     "everything else around it.\n"
     "\n"
     "The first tap of a game is always safe, and so is everything\n"
     "immediately around it - you always get a region to reason from.\n"
     "\n"
     "The level button cycles Easy, Medium and Hard. Harder means more\n"
     "mines, not a bigger board: squares stay big enough to tap."},

    {"Solitaire", "Klondike. Tap a card, tap where it goes.", 0x0F8B8D,
     glyphs::Glyph::Cards, GameId::Solitaire,
     "Klondike. Build the four foundations up from ace to king in suit,\n"
     "and you have won.\n"
     "\n"
     "TAP a card to pick it up, then tap where it should go. Tap it again\n"
     "to put it back down. A run of face-up cards moves together if it is\n"
     "already in order - descending, alternating colours.\n"
     "\n"
     "Columns build DOWN in alternating colours; only a king starts an\n"
     "empty column. Turning over a face-down card happens automatically.\n"
     "\n"
     "Tap the stock (top left) to deal. When it runs out, tap again to\n"
     "turn the waste back over.\n"
     "\n"
     "AUTO sends every card it legally can to the foundations - use it to\n"
     "finish a won game without tapping fifty times. UNDO steps back\n"
     "through your last moves.\n"
     "\n"
     "DRAW 1 / DRAW 3 switches how many cards the stock turns at once.\n"
     "Draw 3 is the harder classic, and each is scored separately."},

    {"Sudoku", "Nine by nine. No guessing needed.", 0x9B5DE5,
     glyphs::Glyph::Grid, GameId::Sudoku,
     "Fill the grid so that every row, every column and every 3x3 box\n"
     "contains 1 to 9 exactly once.\n"
     "\n"
     "Tap a square, then tap a number. Tapping the same number again\n"
     "clears it. NOTES writes small pencil marks instead of an answer.\n"
     "\n"
     "The selected square's row, column and box are shaded, along with\n"
     "every square holding the same digit. A number that repeats in a\n"
     "row, column or box turns red.\n"
     "\n"
     "Every puzzle is generated with exactly one solution, so it can\n"
     "always be finished by logic - you never have to guess.\n"
     "\n"
     "The level button cycles Easy, Medium, Hard and Expert, which start\n"
     "you with fewer and fewer clues."},
};

constexpr int kEntryCount = (int)(sizeof(kEntries) / sizeof(kEntries[0]));

// Big blobs, so the blurb stays readable however many games there are. The
// list scrolls rather than shrinking the tiles to fit.
// Sized so four rows clear the footer buttons at kH - 62. Four rows of 132
// ran 56 px underneath them, which is where the list looked broken.
constexpr int kBlobH = 104;
constexpr int kBlobGap = 10;
int g_list_top = 126;  // set from the wordmark's measured height
constexpr int kVisible = 4;
constexpr int kScrollW = 62;
constexpr int kListW = kW - 2 * kMargin - kScrollW - 12;

int g_scroll = 0;  // index of the first visible entry

// The launcher shows games in the stored order, skipping hidden ones, so
// everything below counts VISIBLE entries rather than table rows.
bool hidden(int entry) { return (g_menu.hidden & (1u << entry)) != 0; }

int visibleCount() {
  int n = 0;
  for (int i = 0; i < g_menu.count; i++) {
    if (!hidden(g_menu.order[i])) n++;
  }
  return n;
}

// The entry index shown in visible slot `slot`, or -1.
int visibleEntry(int slot) {
  for (int i = 0; i < g_menu.count; i++) {
    const int entry = g_menu.order[i];
    if (hidden(entry)) continue;
    if (slot-- == 0) return entry;
  }
  return -1;
}

int maxScroll() {
  const int n = visibleCount();
  return n > kVisible ? n - kVisible : 0;
}

// TAB-U-LOUS-5, each part in its own colour, sharing one baseline so the
// oversized serif 5 sits on the line rather than floating.
// Returns the y of the wordmark's bottom, so the list below can follow it
// rather than assuming a height. The serif 5 is drawn larger than the rest and
// shares their baseline, which means the baseline has to clear ITS height, not
// the height of the letters.
int drawWordmark(int cx, int top_y) {
  auto &g = uikit::gfx();
  struct Seg {
    const char *text;
    uint16_t colour;
    const lgfx::IFont *font;
    uint8_t size;
  };
  const Seg segs[] = {
      {"TAB", rgb(0xFFC53D), &fonts::FreeSansBold24pt7b, 2},
      {"U", rgb(0x4C8DFF), &fonts::FreeSansBold24pt7b, 2},
      {"LOUS", kText, &fonts::FreeSansBold24pt7b, 2},
      {"5", rgb(0xE4572E), &fonts::FreeSerifBoldItalic24pt7b, 3},
  };

  int total = 0, tallest = 0;
  for (const Seg &s : segs) {
    g.setFont(s.font);
    g.setTextSize(s.size);
    total += g.textWidth(s.text);
    const int h = g.fontHeight();
    if (h > tallest) tallest = h;
  }

  const int baseline_y = top_y + tallest;
  int x = cx - total / 2;
  g.setTextDatum(bottom_left);
  for (const Seg &s : segs) {
    g.setFont(s.font);
    g.setTextSize(s.size);
    g.setTextColor(s.colour);
    g.drawString(s.text, x, baseline_y);
    x += g.textWidth(s.text);
  }
  g.setTextSize(1);
  return baseline_y;
}

Rect aboutRect(const Rect &blob) {
  return Rect{blob.x + blob.w - 96, blob.y + blob.h / 2 - 34, 68, 68};
}

// Only drawn when a plausible reading exists: an empty or guessed battery
// indicator is worse than none at all.
constexpr int kBattW = 92, kBattH = 40;
constexpr int kBattX = kW - kMargin - kBattW, kBattY = 26;

void drawBattery() {
  auto &g = uikit::gfx();
  // Clear the whole pill area including the nub and the percentage to its
  // left, so this can be redrawn on its own without a full menu repaint.
  g.fillRect(kBattX - 150, kBattY - 6, 150 + kBattW + 12, kBattH + 12, kBg);
  if (!battery::available()) return;

  const int w = kBattW, h = kBattH;
  const int x = kBattX, y = kBattY;
  const int pct = battery::level();

  // Colour by how much trouble you are in, not by brand convention.
  const uint16_t fill = pct <= 15 ? kDanger : (pct <= 35 ? kAccent : kGood);

  g.drawRoundRect(x, y, w, h, 8, kMuted);
  g.fillRect(x + w + 2, y + 12, 5, h - 24, kMuted);  // the little nub
  const int inner = ((w - 8) * pct) / 100;
  if (inner > 0) g.fillRoundRect(x + 4, y + 4, inner, h - 8, 5, fill);

  char label[16];
  snprintf(label, sizeof(label), battery::charging() ? "%d%% +" : "%d%%", pct);
  uikit::drawLabel(label, x - 14, y + h / 2, kMuted, &fonts::FreeSans12pt7b,
                   middle_right);
}

void drawMenu() {
  auto &g = uikit::gfx();
  g.fillScreen(kBg);

  g_list_top = drawWordmark(kW / 2, 12) + 18;
  drawBattery();

  for (int slot = 0; slot < kVisible; slot++) {
    const int i = visibleEntry(g_scroll + slot);
    if (i < 0) break;
    const Entry &e = kEntries[i];
    const Rect blob{kMargin, g_list_top + slot * (kBlobH + kBlobGap), kListW,
                    kBlobH};

    // Gradient plus a darker shelf along the bottom edge, so the blob reads as
    // something you press. The panel is 16-bit, so this resolves to a handful
    // of bands however finely it is interpolated — see fillRoundRectShaded.
    // Radius 16, not 22: the shelf IS the bottom cap, so the radius sets how
    // thick it is, and at 22 it rose far enough to cut through the blurb and
    // the foot of the mark.
    uikit::fillRoundRectShaded(blob.x, blob.y, blob.w, blob.h, 16, e.color,
                               shade(e.color, 80), shade(e.color, 58), 5);
    const uint16_t ink = inkFor(e.color);

    // The mark, then the text pushed right to clear it. Text starts at the
    // same x whether or not a game has a mark, so the column stays straight.
    // Everything sits above the shelf line at blob.h - 16.
    glyphs::draw(e.mark, blob.x + 26, blob.y + 12, 72, ink, rgb(e.color));

    uikit::drawLabel(e.name, blob.x + 122, blob.y + 10, ink,
                     &fonts::FreeSansBold24pt7b);
    uikit::drawLabel(e.blurb, blob.x + 122, blob.y + 58, ink,
                     &fonts::FreeSans12pt7b);

    // A question mark rather than the word "About": it has to sit inside the
    // blob without competing with the game's name.
    const Rect q = aboutRect(blob);
    uikit::fillRoundRectFast(q.x, q.y, q.w, q.h, 34, rgb(shade(e.color, 62)));
    uikit::drawLabel("?", q.x + q.w / 2, q.y + q.h / 2, ink,
                     &fonts::FreeSansBold24pt7b, middle_center);

    // ORDER MATTERS: findTarget() searches in reverse, so the LAST target
    // registered wins an overlap. The ? sits inside the blob, so the blob must
    // be registered first and the ? second.
    uikit::addTarget(blob, (int)Action::Launch, i);
    uikit::addTarget(q, (int)Action::About, i);
  }

  if (visibleCount() > kVisible) {
    const int x = kMargin + kListW + 12;
    const int h = (kVisible * (kBlobH + kBlobGap) - kBlobGap - 12) / 2;
    const Rect up{x, g_list_top, kScrollW, h};
    const Rect down{x, g_list_top + h + 12, kScrollW, h};
    const bool can_up = g_scroll > 0;
    const bool can_down = g_scroll < maxScroll();
    uikit::drawArrowButton(up, true, can_up ? kSurfaceLift : kSurface,
                           can_up ? kText : kMuted);
    uikit::drawArrowButton(down, false, can_down ? kSurfaceLift : kSurface,
                           can_down ? kText : kMuted);
    uikit::addTarget(up, (int)Action::ScrollUp);
    uikit::addTarget(down, (int)Action::ScrollDown);
  }


  // One door rather than a row of them: theme, volume, the Wi-Fi editor and
  // the arrangement of this very list all live behind it now.
  const Rect settings{kW / 2 - 150, kH - 64, 300, 52};
  uikit::drawButton(settings, "SETTINGS", kSurfaceLift, kText,
                    &fonts::FreeSansBold12pt7b);
  addAction(settings, Action::OpenSettings);
}

// How to play, the rules that aren't obvious, and where the variants live.
void drawAbout() {
  auto &g = uikit::gfx();
  const Entry &e = kEntries[g_about];
  g.fillScreen(kBg);

  uikit::fillRoundRectShaded(kMargin, 20, kW - 2 * kMargin, 76, 18, e.color,
                             shade(e.color, 80), shade(e.color, 58), 4);
  // The same mark as the menu row this screen was opened from, so the About
  // page is visibly about that game and not a generic panel.
  glyphs::draw(e.mark, kMargin + 18, 32, 52, inkFor(e.color), rgb(e.color));
  uikit::drawLabel(e.name, kMargin + 84, 58, inkFor(e.color),
                   &fonts::FreeSansBold24pt7b, middle_left);
  uikit::drawLabel(e.blurb, kW - kMargin - 30, 58, inkFor(e.color),
                   &fonts::FreeSans12pt7b, middle_right);

  // Manual line breaks: the copy is written to fit, so it needs laying out
  // rather than wrapping.
  int y = 124;
  const char *p = e.about;
  char line[128];
  while (*p) {
    const char *nl = strchr(p, '\n');
    const size_t len = nl ? (size_t)(nl - p) : strlen(p);
    const size_t n = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
    memcpy(line, p, n);
    line[n] = '\0';
    if (n) {
      uikit::drawLabel(line, kMargin, y, kText, &fonts::FreeSans12pt7b);
    }
    y += 32;
    if (!nl) break;
    p = nl + 1;
  }

  // The theme toggle lives here because About is the one screen reachable
  // from inside every game — the game footers have no room, and duplicating a
  // control into five of them would be five places to keep in step.
  const char *theme_label = isLight() ? "LIGHT MODE" : "DARK MODE";

  if (g_about_over_game) {
    // Opened mid-game: the only sensible way out is back to where you were.
    const Rect theme_btn{kMargin, kH - 92, 280, 74};
    const Rect resume{kW / 2 - 180, kH - 92, 400, 74};
    uikit::drawButton(theme_btn, theme_label, kSurfaceLift, kMuted,
                      &fonts::FreeSansBold12pt7b);
    uikit::drawButton(resume, "BACK TO GAME", kGood, kOnFill,
                      &fonts::FreeSansBold18pt7b);
    addAction(theme_btn, Action::ToggleTheme);
    addAction(resume, Action::AboutBack);
    return;
  }

  const Rect back{kMargin, kH - 92, 280, 74};
  const Rect theme_btn{kW / 2 - 140, kH - 92, 280, 74};
  const Rect play{kW - kMargin - 320, kH - 92, 320, 74};
  uikit::drawButton(back, "BACK", kSurfaceLift, kText,
                    &fonts::FreeSansBold12pt7b);
  uikit::drawButton(theme_btn, theme_label, kSurfaceLift, kMuted,
                    &fonts::FreeSansBold12pt7b);
  uikit::drawButton(play, "PLAY", kGood, kOnFill, &fonts::FreeSansBold18pt7b);
  addAction(back, Action::AboutBack);
  addAction(theme_btn, Action::ToggleTheme);
  uikit::addTarget(play, (int)Action::Launch, g_about);
}

// Full-screen modal rather than a corner dialog: leaving a game mid-evening is
// worth a deliberate look, and a small dialog invites the same stray tap that
// made the confirmation necessary.
void drawExitConfirm() {
  auto &g = uikit::gfx();
  g.fillScreen(kBg);

  uikit::drawLabel("Leave this game?", kW / 2, 210, kText,
                   &fonts::FreeSansBold24pt7b, middle_center);
  uikit::drawLabel("Scores for the current match will be lost.", kW / 2, 290,
                   kMuted, &fonts::FreeSans12pt7b, middle_center);

  const Rect no{kW / 2 - 340, 380, 320, 110};
  const Rect yes{kW / 2 + 20, 380, 320, 110};
  uikit::drawButton(no, "KEEP PLAYING", kGood, kOnFill,
                    &fonts::FreeSansBold12pt7b);
  uikit::drawButton(yes, "QUIT TO MENU", kDanger, kOnFill,
                    &fonts::FreeSansBold12pt7b);
  addAction(no, Action::ExitNo);
  addAction(yes, Action::ExitYes);
}

// Status rather than a spinner: the interesting information is the URL, and
// the failure most likely to happen (not joining the network) needs saying.
void drawEditor() {
  auto &g = uikit::gfx();
  g.fillScreen(kBg);
  uikit::drawLabel("CONTENT EDITOR", kMargin, 34, kText,
                   &fonts::FreeSansBold18pt7b);

  const auto st = contentserver::state();
  if (st == contentserver::State::Connecting) {
    uikit::drawLabel("Joining the network...", kW / 2, 250, kMuted,
                     &fonts::FreeSansBold18pt7b, middle_center);
  } else if (st == contentserver::State::Running) {
    if (contentserver::usingHotspot()) {
      uikit::drawLabel("Join this Wi-Fi network from a phone", kW / 2, 150,
                       kMuted, &fonts::FreeSans12pt7b, middle_center);
      drawFitted(g, contentserver::hotspotSsid(), kW / 2, 232, kW - 200, 110,
                 kPhraseLadder, kPhraseLadderLen, kAccent);
      uikit::drawLabel("No password. The editor should open by itself;",
                       kW / 2, 316, kMuted, &fonts::FreeSans12pt7b,
                       middle_center);
      char line[96];
      snprintf(line, sizeof(line), "if it doesn't, open  http://%s/",
               contentserver::ip().c_str());
      uikit::drawLabel(line, kW / 2, 352, kMuted, &fonts::FreeSans12pt7b,
                       middle_center);
    } else {
      uikit::drawLabel("Open this on a phone or laptop", kW / 2, 150, kMuted,
                       &fonts::FreeSans12pt7b, middle_center);
      drawFitted(g, contentserver::url(), kW / 2, 240, kW - 200, 110,
                 kPhraseLadder, kPhraseLadderLen, kAccent);
      uikit::drawLabel("or  http://tabulous5.local/", kW / 2, 320, kMuted,
                       &fonts::FreeSans12pt7b, middle_center);
    }
    uikit::drawLabel("Games reload their word packs when you finish.", kW / 2,
                     400, kMuted, &fonts::FreeSans9pt7b, middle_center);

    const Rect swap{kMargin, kH - 110, 380, 88};
    uikit::drawButton(swap,
                      contentserver::usingHotspot() ? "USE MY NETWORK"
                                                    : "USE OWN HOTSPOT",
                      kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
    addAction(swap, Action::SwitchWifi);
  } else {
    uikit::drawLabel("Could not start", kW / 2, 230, kDanger,
                     &fonts::FreeSansBold18pt7b, middle_center);
    uikit::drawLabel(contentserver::lastError(), kW / 2, 285, kMuted,
                     &fonts::FreeSans12pt7b, middle_center);
    uikit::drawLabel("Check the Wi-Fi details in include/secrets.h", kW / 2,
                     330, kMuted, &fonts::FreeSans9pt7b, middle_center);
  }

  const Rect done{kW - kMargin - 380, kH - 110, 380, 88};
  uikit::drawButton(done, "FINISH", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  addAction(done, Action::CloseEditor);
}

// Console settings: everything that is not about one game's rules.
//
// The launcher arrangement lives here rather than on the menu itself because
// reordering by dragging rows around the screen you are also trying to launch
// from is how you launch a game by accident.
void drawSettings() {
  auto &g = uikit::gfx();
  g.fillScreen(kBg);

  uikit::drawLabel("SETTINGS", kMargin, 44, kText, &fonts::FreeSansBold24pt7b,
                   middle_left);
  const Rect done{kW - kMargin - 200, 20, 200, 64};
  uikit::drawButton(done, "DONE", kGood, kOnFill, &fonts::FreeSansBold18pt7b);
  addAction(done, Action::CloseSettings);

  // ---- left column: the console-wide preferences
  const int lx = kMargin, lw = 520;
  int y = 116;

  uikit::drawLabel("DISPLAY", lx, y, kMuted, &fonts::FreeSans12pt7b);
  y += 34;
  const Rect theme_btn{lx, y, lw, 84};
  uikit::drawButton(theme_btn, isLight() ? "LIGHT MODE" : "DARK MODE",
                    kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
  addAction(theme_btn, Action::ToggleTheme);
  y += 100;
  uikit::drawLabel("Light mode is for playing outdoors.", lx, y, kMuted,
                   &fonts::FreeSans9pt7b);

  y += 44;
  uikit::drawLabel("VOLUME", lx, y, kMuted, &fonts::FreeSans12pt7b);
  y += 34;
  const Rect vdown{lx, y, 96, 84};
  const Rect vup{lx + lw - 96, y, 96, 84};
  uikit::drawButton(vdown, "-", kSurfaceLift, kText, &fonts::FreeSansBold24pt7b);
  uikit::drawButton(vup, "+", kSurfaceLift, kText, &fonts::FreeSansBold24pt7b);
  addAction(vdown, Action::VolumeDown);
  addAction(vup, Action::VolumeUp);

  char vol[24];
  if (!g_console.sound_enabled || g_console.volume == 0) {
    snprintf(vol, sizeof(vol), "muted");
  } else {
    snprintf(vol, sizeof(vol), "%u%%",
             (unsigned)(g_console.volume * 100 / 255));
  }
  uikit::drawLabel(vol, lx + lw / 2, y + 42, kText, &fonts::FreeSansBold18pt7b,
                   middle_center);

  y += 116;
  const Rect edit{lx, y, lw, 84};
  uikit::drawButton(edit, "EDIT CONTENT OVER WI-FI", kSurfaceLift, kText,
                    &fonts::FreeSansBold12pt7b);
  addAction(edit, Action::OpenEditor);

  // ---- right column: which games appear, and in what order
  const int rx = 640, rw = kW - kMargin - 640;
  uikit::drawLabel("GAMES", rx, 116, kMuted, &fonts::FreeSans12pt7b);
  uikit::drawLabel("tap a name to show or hide it", rx + 110, 118, kMuted,
                   &fonts::FreeSans9pt7b);

  const int row_h = 84, row_gap = 8;
  int ry = 150;
  for (int i = 0; i < g_menu.count; i++) {
    const int entry = g_menu.order[i];
    const Entry &e = kEntries[entry];
    const bool off = hidden(entry);

    // A hidden game keeps its colour but drops to a flat, dim fill, so the
    // list still reads as the same five games rather than as two lists.
    const Rect row{rx, ry, rw - 180, row_h};
    if (off) {
      uikit::fillRoundRectFast(row.x, row.y, row.w, row.h, 14,
                               rgb(shade(e.color, 34)));
    } else {
      uikit::fillRoundRectShaded(row.x, row.y, row.w, row.h, 14, e.color,
                                 shade(e.color, 80), shade(e.color, 58), 4);
    }
    const uint16_t ink = off ? kMuted : inkFor(e.color);
    glyphs::draw(e.mark, row.x + 14, row.y + 14, 56, ink,
                 rgb(off ? shade(e.color, 34) : e.color));
    uikit::drawLabel(e.name, row.x + 84, row.y + row_h / 2, ink,
                     &fonts::FreeSansBold18pt7b, middle_left);
    uikit::drawLabel(off ? "hidden" : "shown", row.x + row.w - 20,
                     row.y + row_h / 2, ink, &fonts::FreeSans9pt7b,
                     middle_right);
    addAction(row, Action::ToggleGame, i);

    const Rect up{rx + rw - 168, ry, 78, row_h};
    const Rect down{rx + rw - 82, ry, 78, row_h};
    const bool can_up = i > 0;
    const bool can_down = i < g_menu.count - 1;
    uikit::drawArrowButton(up, true, can_up ? kSurfaceLift : kSurface,
                           can_up ? kText : kMuted);
    uikit::drawArrowButton(down, false, can_down ? kSurfaceLift : kSurface,
                           can_down ? kText : kMuted);
    if (can_up) addAction(up, Action::MoveGameUp, i);
    if (can_down) addAction(down, Action::MoveGameDown, i);

    ry += row_h + row_gap;
  }
}

void repaint() {
  uikit::clearTargets();
  if (g_confirming_exit) {
    drawExitConfirm();
  } else if (g_editor_open) {
    drawEditor();
  } else if (g_settings_open) {
    drawSettings();
  } else if (g_about >= 0) {
    drawAbout();
  } else {
    drawMenu();
  }
  uikit::present();
}

void launch(GameId id) {
  g_current = id;
  if (id == GameId::PhraseCraze) {
    Settings s;
    settings_store::load(&s);
    g_phrase.begin(g_packs, s);
    ui::begin(&g_phrase, g_packs, g_report);
    ui::invalidate();
  } else if (id == GameId::Solitaire) {
    solitaire_ui::begin(&g_solitaire);
    solitaire_ui::invalidate();
  } else if (id == GameId::Sudoku) {
    sudoku_ui::begin(&g_sudoku);
    sudoku_ui::invalidate();
  } else if (id == GameId::Minesweeper) {
    mines_ui::begin(&g_mines);
    mines_ui::invalidate();
  } else if (id == GameId::FiveHead) {
    fivehead::Settings s;
    settings_store::loadFive(&s);
    g_five.begin(g_packs, s);
    fivehead_ui::begin(&g_five, g_packs, g_report);
    fivehead_ui::invalidate();
  }
}

}  // namespace

void begin(std::vector<Pack> *packs, const content::LoadReport &report) {
  settings_store::loadMenu(&g_menu, (uint8_t)kEntryCount);
  settings_store::load(&g_console);
  g_packs = packs;
  g_report = report;
  g_current = GameId::Menu;
  g_dirty = true;
}

GameId current() { return g_current; }

bool wantsOrientation() {
  switch (g_current) {
    // Passed hand to hand, so which way up matters.
    case GameId::PhraseCraze:
    case GameId::FiveHead:
      return true;
    // Grid puzzles: held or on a table in one orientation for a whole game,
    // and the games where the touch controller has wedged. Not worth any bus
    // traffic at all.
    case GameId::Minesweeper:
    case GameId::Sudoku:
    case GameId::Solitaire:
      return false;
    default:
      return true;  // the shell, where taps are rare
  }
}

void invalidate() {
  g_dirty = true;
  if (g_current == GameId::PhraseCraze) ui::invalidate();
  if (g_current == GameId::FiveHead) fivehead_ui::invalidate();
  if (g_current == GameId::Minesweeper) mines_ui::invalidate();
  if (g_current == GameId::Sudoku) sudoku_ui::invalidate();
  if (g_current == GameId::Solitaire) solitaire_ui::invalidate();
}

void requestExit() {
  g_confirming_exit = true;
  g_dirty = true;
}

int entryFor(GameId id) {
  for (int i = 0; i < kEntryCount; i++) {
    if (kEntries[i].game == id) return i;
  }
  return -1;
}

void showAbout() {
  const int i = entryFor(g_current);
  if (i < 0) return;
  g_about = i;
  g_about_over_game = true;
  g_dirty = true;
}

void tick(uint32_t now_ms) {
  // Menu only: never inside a game, where an I2C stall would be felt as a
  // dropped tap. Also skipped while the editor or a dialog is up.
  const bool on_menu = (g_current == GameId::Menu) && !g_editor_open &&
                       !g_confirming_exit && !g_settings_open && g_about < 0;
  battery::update(now_ms, on_menu);
  if (on_menu) {
    // Repaint ONLY the pill when the reading changes. Marking the whole menu
    // dirty would re-clear the screen and redraw four blobs to change two
    // digits, which reads as the screen flashing.
    static int shown = -999;
    static bool shown_chg = false;
    const int now_level = battery::available() ? battery::level() : -1;
    if (now_level != shown || battery::charging() != shown_chg) {
      shown = now_level;
      shown_chg = battery::charging();
      if (!g_dirty && g_about < 0) {
        drawBattery();
        uikit::present();
      }
    }
  }

  if (g_editor_open) {
    const auto before = contentserver::state();
    contentserver::loop();
    if (contentserver::state() != before) g_dirty = true;
  }

  if (g_current == GameId::Menu || g_confirming_exit || g_settings_open ||
      g_about >= 0) {
    if (g_dirty) {
      g_dirty = false;
      const uint32_t t0 = micros();
      repaint();
      uikit::noteRepaint(micros() - t0, g_confirming_exit ? 98 : 99);
    }
    return;
  }

  if (g_current == GameId::PhraseCraze) ui::tick(now_ms);
  else if (g_current == GameId::FiveHead) fivehead_ui::tick(now_ms);
  else if (g_current == GameId::Minesweeper) mines_ui::tick(now_ms);
  else if (g_current == GameId::Sudoku) sudoku_ui::tick(now_ms);
  else if (g_current == GameId::Solitaire) solitaire_ui::tick(now_ms);
}

void handleTap(int x, int y, uint32_t now_ms) {
  // While the menu or the confirmation is up, the shell owns the screen and
  // the running game must not also act on the tap.
  if (g_current == GameId::Menu || g_confirming_exit || g_settings_open ||
      g_about >= 0) {
    int action = 0, param = 0;
    if (!uikit::findTarget(x, y, &action, &param)) return;
    switch ((Action)action) {
      case Action::Launch:
        audio::select();
        g_about = -1;
        g_about_over_game = false;
        launch(kEntries[param].game);
        break;
      case Action::About:
        audio::select();
        g_about = param;
        g_about_over_game = false;
        break;
      case Action::AboutBack:
        audio::select();
        g_about = -1;
        if (g_about_over_game) {
          g_about_over_game = false;
          invalidate();  // make the game rebuild its own screen and targets
        }
        break;
      case Action::ScrollUp:
        if (g_scroll > 0) { g_scroll--; audio::select(); }
        break;
      case Action::ScrollDown:
        if (g_scroll < maxScroll()) { g_scroll++; audio::select(); }
        break;
      case Action::ExitNo:
        audio::select();
        g_confirming_exit = false;
        invalidate();
        break;
      case Action::ExitYes:
        audio::select();
        g_confirming_exit = false;
        g_current = GameId::Menu;
        break;
      case Action::OpenSettings:
        audio::select();
        g_settings_open = true;
        break;

      case Action::CloseSettings:
        audio::select();
        g_settings_open = false;
        // Written on leaving rather than on every tap: these are flash writes,
        // and reordering a five-game list takes a lot of taps.
        settings_store::saveMenu(g_menu);
        settings_store::save(g_console);
        // A hidden or reordered game can put the scroll position past the end
        // of a now-shorter list.
        if (g_scroll > maxScroll()) g_scroll = maxScroll();
        break;

      case Action::VolumeDown:
      case Action::VolumeUp: {
        const int step = 24;
        int v = (int)g_console.volume +
                ((Action)action == Action::VolumeUp ? step : -step);
        if (v <= 0) {
          g_console.sound_enabled = false;
          g_console.volume = 0;
        } else {
          g_console.sound_enabled = true;
          g_console.volume = (uint8_t)(v > 255 ? 255 : v);
        }
        audio::setEnabled(g_console.sound_enabled);
        audio::setVolume(g_console.volume);
        // Preview with correct(), not select(): select() is mixed deliberately
        // quiet because it fires on every tap, so it misrepresents the level.
        audio::correct();
        break;
      }

      case Action::ToggleGame: {
        audio::select();
        const int entry = g_menu.order[param];
        const uint16_t bit = (uint16_t)(1u << entry);
        // Refuse the last one: an empty launcher has no way back to here.
        if (!(g_menu.hidden & bit) && visibleCount() <= 1) {
          audio::reject();
          break;
        }
        g_menu.hidden ^= bit;
        break;
      }

      case Action::MoveGameUp:
      case Action::MoveGameDown: {
        audio::select();
        const int from = param;
        const int to = from + ((Action)action == Action::MoveGameUp ? -1 : 1);
        if (to >= 0 && to < g_menu.count) {
          const uint8_t tmp = g_menu.order[from];
          g_menu.order[from] = g_menu.order[to];
          g_menu.order[to] = tmp;
        }
        break;
      }

      case Action::ToggleTheme:
        audio::select();
        setLight(!isLight());
        settings_store::saveLightTheme(isLight());
        // The running game drew itself in the old palette, so it has to
        // repaint too — not just this screen.
        invalidate();
        break;
      case Action::OpenEditor:
        audio::select();
        g_settings_open = false;  // the editor replaces it; DONE comes back
        g_editor_open = true;
        g_revision_at_open = contentserver::revision();
        // Auto: try the house network, fall back to the device's own
        // hotspot if it can't be reached.
        contentserver::start(contentserver::Mode::Auto, WIFI_SSID,
                             WIFI_PASSWORD);
        break;
      case Action::SwitchWifi:
        audio::select();
        contentserver::switchMode(contentserver::usingHotspot()
                                      ? contentserver::Mode::JoinNetwork
                                      : contentserver::Mode::Hotspot);
        break;
      case Action::CloseEditor: {
        audio::select();
        const bool changed = contentserver::revision() != g_revision_at_open;
        contentserver::stop();
        g_editor_open = false;
        g_settings_open = true;  // back where it was opened from
        // Reload only if something was actually written — rereading 900
        // phrases for nothing would just be a pause on the way back.
        if (changed && g_packs) g_report = content::loadAll(g_packs);
        break;
      }
      case Action::None:
        break;
    }
    g_dirty = true;
    return;
  }

  if (g_current == GameId::PhraseCraze) ui::handleTap(x, y, now_ms);
  else if (g_current == GameId::FiveHead) fivehead_ui::handleTap(x, y, now_ms);
  else if (g_current == GameId::Minesweeper)
    mines_ui::handleTap(x, y, now_ms);
  else if (g_current == GameId::Sudoku)
    sudoku_ui::handleTap(x, y, now_ms);
  else if (g_current == GameId::Solitaire)
    solitaire_ui::handleTap(x, y, now_ms);
}

}  // namespace app
}  // namespace tabulous
