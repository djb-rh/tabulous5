// One of Tabulous5's touch games as a P4 Launcher app, chosen at build time (-DTABGAME_MINES, _SUDOKU,
// _SOLITAIRE, _PHRASE, _FIVEHEAD or _JOSHUA). The game's own files are the console's, unchanged: the rules, the
// screens, the shared UI kit, the theme, the name entry, the score tables, the sound and the settings. This
// file is what the console's shell (src/app.cpp, src/main.cpp) did for them: start the screen, pass on taps and
// ticks, show the About page, and ask before leaving.
#include <M5Unified.h>
#include <cstring>
#include <vector>
#include "papp/papp.h"

#include "app.h"
#include "audio.h"
#include "content.h"
#include "glyphs.h"
#include "hwkeyboard.h"
#include "orientation.h"
#include "settings_store.h"
#include "theme.h"
#include "uikit.h"
#include "about_gen.h"        // this game's row of the console's menu table: name, strapline, colour, About text

#if defined(TABGAME_MINES)
#include "minesweeper_ui.h"
#define GAME_ID "tabulous.minesweeper"
namespace game = tabulous::mines_ui;
static tabulous::mines::Board g_state;
static void startGame() { game::begin(&g_state); }
#elif defined(TABGAME_SUDOKU)
#include "sudoku_ui.h"
#define GAME_ID "tabulous.sudoku"
namespace game = tabulous::sudoku_ui;
static tabulous::sudoku::Puzzle g_state;
static void startGame() { game::begin(&g_state); }
#elif defined(TABGAME_SOLITAIRE)
#include "solitaire_ui.h"
#define GAME_ID "tabulous.solitaire"
namespace game = tabulous::solitaire_ui;
static tabulous::solitaire::Game g_state;
static void startGame() { game::begin(&g_state); }
#elif defined(TABGAME_PHRASE)
#include "phrase_ui.h"
#define GAME_ID "tabulous.phrasecraze"
namespace game = tabulous::ui;
static tabulous::Game g_state;
static std::vector<tabulous::Pack> g_packs;
static void startGame()
{
  const tabulous::content::LoadReport report = tabulous::content::loadAll(&g_packs);
  papp_log(ABOUT_NAME ": %u packs, %u phrases%s\n", (unsigned)report.packs_loaded, (unsigned)report.phrases_total, report.used_builtin ? " (built-in only: no packs on the card)" : "");
  tabulous::Settings s;
  tabulous::settings_store::load(&s);
  g_state.begin(&g_packs, s);
  game::begin(&g_state, &g_packs, report);
}
#elif defined(TABGAME_FIVEHEAD)
#include "fivehead_ui.h"
#define GAME_ID "tabulous.fivehead"
namespace game = tabulous::fivehead_ui;
static tabulous::fivehead::Game g_state;
static std::vector<tabulous::Pack> g_packs;
static void startGame()
{
  const tabulous::content::LoadReport report = tabulous::content::loadAll(&g_packs);
  papp_log(ABOUT_NAME ": %u packs, %u phrases; tilt %s\n", (unsigned)report.packs_loaded, (unsigned)report.phrases_total, M5.Imu.isEnabled() ? "on" : "not offered by this launcher (use the buttons)");
  tabulous::fivehead::Settings s;
  tabulous::settings_store::loadFive(&s);
  g_state.begin(&g_packs, s);
  game::begin(&g_state, &g_packs, report);
}
#else
#include "joshua_ui.h"
#define GAME_ID "tabulous.joshua"
namespace game = tabulous::joshua_ui;
static void startGame() { game::begin(); }
#endif

// In C++ a const object at file scope is private to the file and its name is mangled, and the packer looks the
// manifest up by name: without this line the app is packed with no manifest at all, and nothing says so.
extern "C" { extern const papp_manifest_t __papp_manifest; }
PAPP_MANIFEST(.id = GAME_ID, .name = ABOUT_NAME, .version = PAPP_APP_VERSION, .main_stack = 65536);

static bool g_quit, g_want_exit, g_want_about;

namespace tabulous {
// --- what the screens ask of the shell. Both are answered from the main loop, not from inside the tap that
// asked, so the game's own handler has returned before another screen is drawn over it. ---
namespace app {
void requestExit() { g_want_exit = true; }
void showAbout() { g_want_about = true; }
}  // namespace app

// --- the clip-on keyboard: there is none here ---
namespace hwkeyboard {
void begin() {}
void tick(uint32_t) {}
bool present() { return false; }
bool take(Key *) { return false; }
void dumpLog() {}
}  // namespace hwkeyboard

// --- turning the picture over when the console is passed across the table. The launcher owns the panel's
// rotation, so here it stays as it is and the setting does nothing. ---
namespace orientation {
void begin() {}
bool update(uint32_t) { return false; }
void setEnabled(bool) {}
bool enabled() { return false; }
void flipNow() {}
bool calibrated() { return false; }
void resetCalibration() {}
void setStableMs(uint32_t) {}
uint32_t stableMs() { return 0; }
uint8_t rotation() { return 0; }
float lastReading() { return 0; }
}  // namespace orientation
}  // namespace tabulous

using namespace tabulous;
using namespace tabulous::theme;

// A press, once: true on the pass the finger comes down.
static bool pressEdge(int *x, int *y)
{
  static bool was_down = true;                         // a finger already down when the app opens is not a tap on it
  const uikit::TouchState t = uikit::touch();
  const bool edge = t.down && !was_down;
  was_down = t.down;
  *x = t.x; *y = t.y;
  return edge;
}

// The console's About page (src/app.cpp, drawAbout), with the launcher's volume where the console had its own.
static void drawAbout()
{
  auto &g = uikit::gfx();
  g.fillScreen(kBg);
  uikit::fillRoundRectShaded(kMargin, 20, kW - 2 * kMargin, 76, 18, ABOUT_COLOR, shade(ABOUT_COLOR, 80), shade(ABOUT_COLOR, 58), 4);
  glyphs::draw(ABOUT_GLYPH, kMargin + 18, 32, 52, inkFor(ABOUT_COLOR), rgb(ABOUT_COLOR));
  uikit::drawLabel(ABOUT_NAME, kMargin + 84, 58, inkFor(ABOUT_COLOR), &fonts::FreeSansBold24pt7b, middle_left);
  uikit::drawLabel(ABOUT_BLURB, kW - kMargin - 30, 58, inkFor(ABOUT_COLOR), &fonts::FreeSans12pt7b, middle_right);

  // Wrapped to the screen: a blank line is a paragraph break, a single newline is only where the sentence was typed.
  g.setFont(&fonts::FreeSans12pt7b);
  g.setTextSize(1);
  int y = 124;
  char line[192];
  int len = 0, fits = 0;
  const char *p = kAboutText;
  const int bottom = kH - 120;
  auto flush = [&](bool blank_after) {
    line[fits > 0 ? fits : len] = '\0';
    if (line[0] && y <= bottom) uikit::drawLabel(line, kMargin, y, kText, &fonts::FreeSans12pt7b);
    y += blank_after ? 46 : 32;
    len = 0; fits = 0;
  };
  while (*p) {
    if (*p == '\n' && p[1] == '\n') { flush(true); while (*p == '\n') p++; continue; }
    if (*p == ' ' || *p == '\n') { p++; continue; }
    const char *w = p;
    while (*p && *p != ' ' && *p != '\n') p++;
    const int wlen = (int)(p - w);
    const int start = len;
    if (len > 0 && len < (int)sizeof(line) - 2) line[len++] = ' ';
    for (int i = 0; i < wlen && len < (int)sizeof(line) - 1; i++) line[len++] = w[i];
    line[len] = '\0';
    if (g.textWidth(line) > kW - 2 * kMargin && start > 0) { len = start; flush(false); p = w; continue; }
    fits = len;
  }
  flush(false);

  uikit::drawButton(uikit::Rect{kMargin, kH - 92, 280, 74}, isLight() ? "LIGHT MODE" : "DARK MODE", kSurfaceLift, kMuted, &fonts::FreeSansBold12pt7b);
  uikit::drawButton(uikit::Rect{kMargin + 300, kH - 92, 220, 74}, "VOLUME", kSurfaceLift, kMuted, &fonts::FreeSansBold12pt7b);
  uikit::drawButton(uikit::Rect{kW - kMargin - 400, kH - 92, 400, 74}, "BACK TO GAME", kGood, kOnFill, &fonts::FreeSansBold18pt7b);
}

static void aboutScreen()
{
  drawAbout();
  while (m5compat::update()) {
    int x, y;
    audio::update(millis());
    if (!pressEdge(&x, &y)) continue;
    audio::select();
    if (uikit::Rect{kMargin, kH - 92, 280, 74}.hit(x, y)) {
      const bool light = !isLight();
      setLight(light);
      settings_store::saveLightTheme(light);
      drawAbout();
    } else if (uikit::Rect{kMargin + 300, kH - 92, 220, 74}.hit(x, y)) {
      papp_ui_volume();                                // the launcher's own overlay; our picture comes back on the next show
      m5compat::repaint();
    } else if (y >= kH - 110 || y < 100) {
      break;                                           // BACK TO GAME, or the title bar
    }
  }
}

// Leaving is asked about first, as on the console: a stray tap mid-evening would be worse than one more press.
static bool confirmExit()
{
  static const char *const items[] = {"Keep playing", "Leave " ABOUT_NAME};
  const int r = papp_ui_menu("Leave " ABOUT_NAME "?", items, 2, 0);
  m5compat::repaint();
  return r == 1;
}

// `soundcheck` as the app's argument plays a sample, a tone and a scheduled run of notes before the game starts,
// so the sound path can be checked from the launcher's log with nobody there to tap.
static void soundCheck()
{
  audio::select(); delay(250);
  audio::correct(); delay(500);
  audio::beep(1200); delay(200);
  audio::beep(1500); delay(200);
  audio::fanfare();
  for (int i = 0; i < 250; i++) { audio::update(millis()); delay(10); }
  papp_log(ABOUT_NAME ": sound check played\n");
}

int main(int argc, char **argv)
{
  if (!m5compat::begin()) { papp_log(ABOUT_NAME ": no display\n"); return 1; }
  setLight(settings_store::loadLightTheme());          // the palette is set at run time; nothing draws right before this
  audio::begin(255);                                   // loudness is the launcher's to set (VOLUME on the About page)
  audio::loadSamples();
  papp_log(ABOUT_NAME ": %d of 7 sounds from /sd/tabulous/sfx\n", audio::sampleCount());
  if (argc > 1 && !strcmp(argv[1], "soundcheck")) soundCheck();
  startGame();
  game::invalidate();
  uint32_t ticks = 0;
  int64_t worst = 0, stat_at = papp_time_us();
  while (m5compat::update() && !g_quit) {
    const uint32_t now = millis();
    const int64_t t0 = papp_time_us();
    int x, y;
    if (pressEdge(&x, &y)) game::handleTap(x, y, now);   // the press edge, as the console's loop does it
    audio::update(now);
    game::tick(now);
    if (g_want_about) { g_want_about = false; aboutScreen(); game::invalidate(); continue; }
    if (g_want_exit) { g_want_exit = false; if (confirmExit()) g_quit = true; else game::invalidate(); continue; }
    const int64_t took = papp_time_us() - t0;
    if (took > worst) worst = took;
    ticks++;
    if (t0 - stat_at >= 5000000) {
      int64_t pr[3]; m5compat::profile(pr);
      papp_log(ABOUT_NAME ": %u passes in 5 s, slowest %u ms, %u frames shown so far; ms in touch %u, sum %u, show/wait %u\n", (unsigned)ticks, (unsigned)(worst / 1000), (unsigned)m5compat::presents(), (unsigned)(pr[0] / 1000), (unsigned)(pr[1] / 1000), (unsigned)(pr[2] / 1000));
      ticks = 0; worst = 0; stat_at = t0;
    }
  }
  m5compat::end();
  return 0;
}
