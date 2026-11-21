// One of Tabulous5's touch games as a P4 Launcher app, chosen at build time (-DTABGAME_MINES, _SUDOKU,
// _SOLITAIRE, _PHRASE, _FIVEHEAD or _JOSHUA). The game's own files are the console's, unchanged: the rules, the
// screens, the shared UI kit, the theme, the name entry, the score tables, the sound and the settings. This
// file is what the console's shell (src/app.cpp, src/main.cpp) did for them: start the screen, pass on taps and
// ticks, show the About page, and ask before leaving.
#include <M5Unified.h>
#include <cstdio>
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

// The two games that are passed from hand to hand turn the picture over to face whoever holds the console, as on
// the console: its own code (src/orientation.cpp), reading the accelerometer where the launcher offers one. The
// rest stay the way up the launcher shows everything.
static bool g_orientation;
static void startOrientation()
{
  tabulous::Settings s;                                // the flip settings live with PhraseCraze's, as on the console
  tabulous::settings_store::load(&s);
  tabulous::orientation::begin();
  tabulous::orientation::setEnabled(s.auto_rotate);
  tabulous::orientation::setStableMs(s.flip_delay_ms);
  g_orientation = true;
  float x = 0, y = 0, z = 0;
  if (M5.Imu.isEnabled() && M5.Imu.getAccel(&x, &y, &z))
    papp_log("accelerometer: %d %d %d thousandths of a g; flip %s, %s\n", (int)(x * 1000), (int)(y * 1000), (int)(z * 1000), s.auto_rotate ? "on" : "off", tabulous::orientation::calibrated() ? "calibrated" : "not calibrated yet (FLIP while holding it teaches it)");
  else
    papp_log("accelerometer: not offered by this launcher; the picture turns over only by its FLIP button\n");
}

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
  startOrientation();
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
  papp_log(ABOUT_NAME ": %u packs, %u phrases\n", (unsigned)report.packs_loaded, (unsigned)report.phrases_total);
  tabulous::fivehead::Settings s;
  tabulous::settings_store::loadFive(&s);
  g_state.begin(&g_packs, s);
  game::begin(&g_state, &g_packs, report);
  startOrientation();
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

// Arguments for checking things with nobody at the console. `taps=2000:640:497,3500:190:250` taps the screen by
// itself: milliseconds after the game starts, then where, as many as wanted. The finger is back in charge once
// the last one is done. `flipped` starts with the picture turned over.
static struct { uint32_t at; int x, y; } g_taps[32];
static int g_tap_n, g_tap_next = -1;
static uint32_t g_taps_t0;
static void loadTaps(const char *spec)
{
  while (*spec && g_tap_n < 32) {
    unsigned at = 0; int x = 0, y = 0, used = 0;
    if (sscanf(spec, "%u:%d:%d%n", &at, &x, &y, &used) < 3) break;
    g_taps[g_tap_n].at = at; g_taps[g_tap_n].x = x; g_taps[g_tap_n].y = y; g_tap_n++;
    spec += used;
    if (*spec == ',') spec++;
  }
  g_tap_next = g_tap_n ? 0 : -1;
  g_taps_t0 = millis();
}
static void playTaps(uint32_t now)
{
  if (g_tap_next < 0) return;
  const uint32_t t = now - g_taps_t0;
  if (g_tap_next >= g_tap_n) { uikit::overrideTouch(false, false, 0, 0); g_tap_next = -1; return; }
  const bool down = t >= g_taps[g_tap_next].at && t < g_taps[g_tap_next].at + 150;
  uikit::overrideTouch(true, down, g_taps[g_tap_next].x, g_taps[g_tap_next].y);
  if (t >= g_taps[g_tap_next].at + 300) { papp_log(ABOUT_NAME ": tapped %d,%d by itself\n", g_taps[g_tap_next].x, g_taps[g_tap_next].y); g_tap_next++; }
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

// `prefscheck` writes a setting, forgets it, and reads it back from the card: whether settings and score tables
// will keep, on a card that may never have held our folder.
#include <Preferences.h>
static void prefsCheck()
{
  const uint32_t want = 0x5AFE0000u | (millis() & 0xFFFF);
  { Preferences p; p.begin("selftest"); p.putUInt("probe", want); p.end(); }
  { Preferences other; other.begin("selftest2"); other.end(); }            // loading another namespace drops the first from memory
  Preferences p; p.begin("selftest");
  const uint32_t got = p.getUInt("probe", 0);
  p.end();
  papp_log(ABOUT_NAME ": settings check: wrote %08x, read back %08x: %s\n", (unsigned)want, (unsigned)got, got == want ? "kept" : "NOT KEPT");
}

int main(int argc, char **argv)
{
  if (!m5compat::begin()) { papp_log(ABOUT_NAME ": no display\n"); return 1; }
  setLight(settings_store::loadLightTheme());          // the palette is set at run time; nothing draws right before this
  audio::begin(255);                                   // loudness is the launcher's to set (VOLUME on the About page)
  audio::loadSamples();
  papp_log(ABOUT_NAME ": %d of 7 sounds loaded\n", audio::sampleCount());
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "soundcheck")) soundCheck();
    if (!strcmp(argv[i], "prefscheck")) prefsCheck();
  }
  startGame();
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "flipped")) M5.Display.setRotation(1);   // for a look at the picture the other way up
    if (!strncmp(argv[i], "taps=", 5)) loadTaps(argv[i] + 5);
  }
  game::invalidate();
  uint32_t ticks = 0;
  int64_t worst = 0, stat_at = papp_time_us();
  while (m5compat::update() && !g_quit) {
    const uint32_t now = millis();
    const int64_t t0 = papp_time_us();
    int x, y;
    playTaps(now);
    if (pressEdge(&x, &y)) game::handleTap(x, y, now);   // the press edge, as the console's loop does it
    audio::update(now);
    if (g_orientation && orientation::update(now)) game::invalidate();   // turned over: everything is drawn again
    game::tick(now);
    {   // how many things on the screen can be tapped (and, in PhraseCraze, which screen it is), in the log when
        // it changes, with how long the last state lasted and the longest gap between passes of this loop in it
      static int was_screen = -1; static size_t was_targets = (size_t)-1;
      static uint32_t since, passes, longest, last_pass;
#if defined(TABGAME_PHRASE)
      const int scr = (int)g_state.screen();
#else
      const int scr = 0;
#endif
      const size_t tg = uikit::targetCount();
      if (now - last_pass > longest) longest = now - last_pass;
      last_pass = now; passes++;
      if (scr != was_screen || tg != was_targets) {
        papp_log(ABOUT_NAME ": screen %d, %u places to tap (the last state lasted %u ms: %u passes of the loop, the longest %u ms)\n", scr, (unsigned)tg, (unsigned)(now - since), (unsigned)passes, (unsigned)longest);
        was_screen = scr; was_targets = tg; since = now; passes = 0; longest = 0;
      }
    }
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
