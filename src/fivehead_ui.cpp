#include "fivehead_ui.h"

#include <M5Unified.h>

#include <cstdio>

#include "app.h"
#include "audio.h"
#include "settings_store.h"
#include "packpicker.h"
#include "textentry.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace fivehead_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Action : uint8_t {
  None, Play, Menu, About, PickCategory, Correct, Pass, Next, Again, Settings,
  SettingDec, SettingInc, EditTeam, ChoosePacks,
};

fivehead::Game *g_game = nullptr;
std::vector<Pack> *g_packs = nullptr;
content::LoadReport g_report;
bool g_dirty = true;

// Tracks matchOver() so the fanfare fires once, on the edge.
bool g_match_was_over = false;

// Result flash. Heads Up shows you what the phrase was for a beat before the
// next one, and that beat is most of the fun of the recap.
uint32_t g_flash_until = 0;
bool g_flash_correct = false;
std::string g_flash_phrase;

// Tilt gesture state. The device is vertical against a forehead, so gravity is
// mostly in-plane and `az` (the screen normal) is near zero; tipping the top
// forward or back swings az decisively one way or the other.


// Starts DISARMED. The device is mid-air on its way to a forehead when a round
// begins, and that motion easily exceeds the trigger — which fired a gesture
// on the first frame, flashed a verdict over the opening phrase, and silently
// consumed it. A gesture is only allowed after the device has been seen at
// rest in the neutral zone, which on a forehead is immediate.
bool g_armed = false;

// A gesture must be HELD, not merely sampled once. A single noisy reading
// crossing the threshold was enough to score, which is almost certainly why
// the device appeared to pass by itself while sitting still.
constexpr uint32_t kTiltHoldMs = 150;
uint32_t g_tilt_since = 0;
bool g_tilt_down = false;

// The IMU shares the internal I2C bus with the touch controller, so it is
// sampled at a modest rate rather than every loop iteration.
constexpr uint32_t kTiltSampleMs = 40;
uint32_t g_tilt_sampled = 0;

// Which team the shared keyboard is renaming.
int g_editing_team = 0;

// Seconds left on the get-ready countdown, for the Ready screen.
uint32_t g_ready_left_ms = 0;

// A once-a-second full-screen clear to change two digits is visible as
// flicker, because we draw straight at the panel's framebuffer. The clock and
// the get-ready count therefore repaint only their own patch.
bool g_redraw_clock_only = false;

// Which screen was last drawn in full. A patch-only redraw is only ever valid
// on a screen that has already been painted, so a screen change must always
// force the full draw — otherwise the first thing on the new screen is a clock
// patch sitting on top of the old one.
int g_drawn_screen = -1;

void addAction(const Rect &r, Action a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

const char *teamName(fivehead::Team t) {
  return g_game->settings().team_names[fivehead::teamIndex(t)].c_str();
}

uint32_t teamHex(fivehead::Team t) { return kTeamHex[fivehead::teamIndex(t)]; }

// -------------------------------------------------------------- screens

// Defined below, next to the constants describing the patches they repaint.
void drawClockPatch(uint32_t now_ms);
void drawReadyPatch();
void adjustSetting(int index, int delta);

void drawHome() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("FIVEHEAD", kW / 2, 58, kAccent, &fonts::FreeSansBold24pt7b,
            middle_center, 2);
  drawLabel("hold it on your forehead — tilt down to score, up to pass",
            kW / 2, 118, kMuted, &fonts::FreeSans12pt7b, middle_center);

  const auto &s = g_game->settings();
  const int card_w = 520, card_h = 190, gap = 144;
  for (int i = 0; i < 2; i++) {
    const auto t = i == 0 ? fivehead::Team::A : fivehead::Team::B;
    const int x = kMargin + i * (card_w + gap);
    const bool up = (t == g_game->turn());
    g.fillRoundRect(x, 168, card_w, card_h, 20, kSurface);
    g.fillRoundRect(x, 168, card_w, 12, 6, rgb(teamHex(t)));
    if (up) g.drawRoundRect(x, 168, card_w, card_h, 20, kAccent);

    drawLabel(teamName(t), x + card_w / 2, 214, kMuted, &fonts::FreeSans12pt7b,
              middle_center);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_game->score(t));
    drawLabel(buf, x + card_w / 2, 292, kText, &fonts::FreeSansBold24pt7b,
              middle_center, 2);
    char turns[40];
    snprintf(turns, sizeof(turns), "%u of %u turns  -  tap to rename",
             (unsigned)g_game->turnsTaken(t), (unsigned)s.rounds_each);
    drawLabel(turns, x + card_w / 2, 168 + card_h - 22, up ? kAccent : kMuted,
              &fonts::FreeSans9pt7b, middle_center);
    addAction(Rect{x, 168, card_w, card_h}, Action::EditTeam, i);
  }

  if (g_game->matchOver()) {
    char line[128];
    snprintf(line, sizeof(line), "%s wins", teamName(g_game->winner()));
    drawLabel(line, kW / 2, 400, kAccent, &fonts::FreeSansBold18pt7b,
              middle_center);
    const Rect again{kW / 2 - 200, 440, 400, 96};
    drawButton(again, "PLAY AGAIN", kGood, kOnFill);
    addAction(again, Action::Again);
  } else {
    char line[128];
    snprintf(line, sizeof(line), "%s is up", teamName(g_game->turn()));
    drawLabel(line, kW / 2, 400, kText, &fonts::FreeSansBold18pt7b,
              middle_center);
    const Rect play{kW / 2 - 200, 440, 400, 96};
    drawButton(play, "PLAY", kGood, kOnFill);
    addAction(play, Action::Play);
  }

// A consistent "?" in the same corner on every game, so it is findable
// without hunting. Offered only where MENU is — never mid-round.
  const Rect help{kW - kMargin - 64, 22, 64, 64};
  uikit::fillRoundRectFast(help.x, help.y, help.w, help.h, 32, kSurfaceLift);
  drawLabel("?", help.x + 32, help.y + 32, kText, &fonts::FreeSansBold18pt7b,
            middle_center);
  addAction(help, Action::About);

  const Rect menu{kMargin, kH - 84, 240, 64};
  drawButton(menu, "MENU", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(menu, Action::Menu);

  const Rect settings{kW - kMargin - 300, kH - 84, 300, 64};
  drawButton(settings, "SETTINGS", kSurfaceLift, kText,
             &fonts::FreeSansBold12pt7b);
  addAction(settings, Action::Settings);
}

void drawCategorySelect() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("PICK A CATEGORY", kMargin, 46, kText, &fonts::FreeSansBold18pt7b);

  const int cols = 4;
  const int tile_w = (kW - 2 * kMargin - (cols - 1) * 20) / cols;
  const int tile_h = 200;
  int slot = 0;
  for (size_t i = 0; i < g_packs->size() && i < 8; i++) {
    if (!packpicker::enabled(g_game->settings().enabled_packs, i,
                             g_packs->size())) {
      continue;
    }
    const Pack &pack = (*g_packs)[i];
    const Rect r{kMargin + (slot % cols) * (tile_w + 20),
                 150 + (slot / cols) * (tile_h + 20), tile_w, tile_h};
    slot++;
    const uint32_t hex = pack.meta().color;
    g.fillRoundRect(r.x, r.y, r.w, r.h, 20, rgb(hex));
    drawFitted(g, pack.meta().name, r.x + r.w / 2, r.y + r.h / 2 - 14,
               r.w - 24, 96, kPhraseLadder + 2, kPhraseLadderLen - 2,
               inkFor(hex));
    addAction(r, Action::PickCategory, (int)i);
  }

  const Rect back{kMargin, kH - 84, 220, 64};
  drawButton(back, "BACK", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Menu);
}

void drawReady(uint32_t now_ms) {
  auto &g = gfx();
  const uint32_t hex = teamHex(g_game->turn());
  g.fillScreen(rgb(hex));
  const uint16_t ink = inkFor(hex);

  drawLabel("ON YOUR FOREHEAD", kW / 2, 180, ink, &fonts::FreeSansBold18pt7b,
            middle_center);
  drawLabel("screen facing everyone else", kW / 2, 240, ink,
            &fonts::FreeSans12pt7b, middle_center);

  drawReadyPatch();
}

constexpr int kClockX = kMargin, kClockY = 18, kClockW = 300, kClockH = 78;
constexpr int kReadyX = kW / 2 - 220, kReadyY = 350, kReadyW = 440,
              kReadyH = 220;

void drawClockPatch(uint32_t now_ms) {
  auto &g = gfx();
  const uint32_t hex = teamHex(g_game->turn());
  const uint16_t ink = inkFor(hex);
  g.fillRect(kClockX, kClockY, kClockW, kClockH, rgb(hex));
  char clock[16];
  snprintf(clock, sizeof(clock), "%u",
           (unsigned)((g_game->remainingMs(now_ms) + 999) / 1000));
  drawLabel(clock, kClockX, kClockY + 8, ink, &fonts::FreeSansBold24pt7b,
            top_left);
}

void drawReadyPatch() {
  auto &g = gfx();
  const uint32_t hex = teamHex(g_game->turn());
  const uint16_t ink = inkFor(hex);
  g.fillRect(kReadyX, kReadyY, kReadyW, kReadyH, rgb(hex));
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)((g_ready_left_ms + 999) / 1000));
  drawFitted(g, buf, kW / 2, kReadyY + kReadyH / 2, kReadyW - 40, kReadyH - 40,
             kPhraseLadder, kPhraseLadderLen, ink);
}

void drawRound(uint32_t now_ms) {
  auto &g = gfx();

  // Result flash: whole screen in the verdict colour with the phrase, so the
  // room sees what it was even though the holder never will.
  if (now_ms < g_flash_until) {
    const uint32_t hex = g_flash_correct ? 0x16A34A : 0xC2410C;
    g.fillScreen(rgb(hex));
    const uint16_t ink = inkFor(hex);
    drawLabel(g_flash_correct ? "GOT IT" : "PASS", kW / 2, 90, ink,
              &fonts::FreeSansBold18pt7b, middle_center);
    drawFitted(g, g_flash_phrase, kW / 2, kH / 2 + 20, kW - 160, 260,
               kPhraseLadder, kPhraseLadderLen, ink);
    return;
  }

  const uint32_t hex = teamHex(g_game->turn());
  g.fillScreen(rgb(hex));
  const uint16_t ink = inkFor(hex);

  // Unlike PhraseCraze, the clock here is the point and is shown large.
  drawClockPatch(now_ms);

  char score[24];
  snprintf(score, sizeof(score), "%d", g_game->roundScore());
  drawLabel(score, kW - kMargin, 26, ink, &fonts::FreeSansBold24pt7b,
            top_right);

  drawFitted(g, g_game->currentPhrase(), kW / 2, kH / 2 - 10, kW - 160, 300,
             kPhraseLadder, kPhraseLadderLen, ink);

  // Tilt is the intended input, but a helper can tap these — useful when the
  // holder's tilt is marginal, and the only way to play if tilt misbehaves.
  const Rect pass{kMargin, kH - 108, 300, 84};
  const Rect ok{kW - kMargin - 300, kH - 108, 300, 84};
  drawButton(pass, "PASS", rgb(shade(hex, 70)), ink,
             &fonts::FreeSansBold12pt7b);
  drawButton(ok, "GOT IT", rgb(shade(hex, 70)), ink,
             &fonts::FreeSansBold12pt7b);
  addAction(pass, Action::Pass);
  addAction(ok, Action::Correct);
}

int settingCount() { return 7; }

const char *settingLabel(int i) {
  switch (i) {
    case 0: return "Round length";
    case 1: return "Turns each";
    case 2: return "Difficulty";
    case 3: return "Tilt direction";
    case 4: return "Tilt sensitivity";
    case 5: return "Verdict flash";
    case 6: return "Word packs";
    default: return "";
  }
}

void settingValue(int i, char *out, size_t len) {
  const auto &s = g_game->settings();
  switch (i) {
    case 0: snprintf(out, len, "%u s", (unsigned)(s.round_ms / 1000)); break;
    case 1: snprintf(out, len, "%u", (unsigned)s.rounds_each); break;
    case 2:
      snprintf(out, len, "%s",
               s.max_difficulty == Difficulty::Easy
                   ? "kids"
                   : (s.max_difficulty == Difficulty::Medium ? "normal" : "all"));
      break;
    case 3:
      snprintf(out, len, "%s", s.tilt_swapped ? "swapped" : "normal");
      break;
    case 4:
      snprintf(out, len, "%s",
               s.tilt_sensitivity == fivehead::TiltSensitivity::Light
                   ? "light"
                   : (s.tilt_sensitivity == fivehead::TiltSensitivity::Firm
                          ? "firm"
                          : "normal"));
      break;
    case 5: snprintf(out, len, "%u ms", (unsigned)s.flash_ms); break;
    case 6: snprintf(out, len, "choose"); break;
    default: snprintf(out, len, "-"); break;
  }
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void adjustSetting(int i, int delta) {
  auto &s = g_game->mutableSettings();
  switch (i) {
    case 0:
      s.round_ms = (uint32_t)clampi((int)(s.round_ms / 1000) + delta * 15, 30,
                                    180) * 1000;
      break;
    case 1: s.rounds_each = (uint8_t)clampi(s.rounds_each + delta, 1, 9); break;
    case 2:
      s.max_difficulty =
          (Difficulty)clampi((int)s.max_difficulty + delta, 0, 2);
      break;
    case 3: s.tilt_swapped = !s.tilt_swapped; break;
    case 4:
      s.tilt_sensitivity = (fivehead::TiltSensitivity)clampi(
          (int)s.tilt_sensitivity + delta, 0, 2);
      break;
    case 5:
      s.flash_ms = (uint16_t)clampi(s.flash_ms + delta * 200, 200, 2000);
      break;
    case 6:
      // An action row: either stepper opens the picker.
      packpicker::open("FIVEHEAD PACKS", g_packs, s.enabled_packs);
      break;
    default: break;
  }
}

void drawSettings() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("FIVEHEAD SETTINGS", kMargin, 34, kText,
            &fonts::FreeSansBold18pt7b);

  const int top = 104;
  const int row_h = 88;
  for (int i = 0; i < settingCount(); i++) {
    const int y = top + i * row_h;
    const int inner = row_h - 12;
    g.fillRoundRect(kMargin, y, kW - 2 * kMargin, inner, 14, kSurface);
    drawLabel(settingLabel(i), kMargin + 26, y + inner / 2, kText,
              &fonts::FreeSans12pt7b, middle_left);

    char value[48];
    settingValue(i, value, sizeof(value));
    drawLabel(value, kW - kMargin - 230, y + inner / 2, kAccent,
              &fonts::FreeSansBold12pt7b, middle_right);

    const Rect dec{kW - kMargin - 210, y + 8, 92, inner - 16};
    const Rect inc{kW - kMargin - 106, y + 8, 92, inner - 16};
    drawButton(dec, "-", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
    drawButton(inc, "+", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
    addAction(dec, Action::SettingDec, i);
    addAction(inc, Action::SettingInc, i);
  }

  const Rect back{kMargin, kH - 74, 220, 58};
  drawButton(back, "BACK", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Menu);
}

void drawResults() {
  auto &g = gfx();
  g.fillScreen(kBg);

  char head[128];
  snprintf(head, sizeof(head), "%s scored %d", teamName(g_game->turn()),
           g_game->roundScore());
  drawLabel(head, kMargin, 34, kText, &fonts::FreeSansBold18pt7b);

  // Two columns of outcomes, newest first is not useful here — play order is.
  const auto &outs = g_game->outcomes();
  const int cols = 2;
  const int col_w = (kW - 2 * kMargin - 24) / cols;
  const int row_h = 52;
  const int rows = 8;
  for (size_t i = 0; i < outs.size() && i < (size_t)(cols * rows); i++) {
    const int c = (int)i / rows, r = (int)i % rows;
    const int x = kMargin + c * (col_w + 24);
    const int y = 92 + r * row_h;
    g.fillRoundRect(x, y, col_w, row_h - 8, 10,
                    outs[i].correct ? rgb(0x14532D) : kSurface);
    drawLabel(outs[i].correct ? "+" : "-", x + 20, y + (row_h - 8) / 2,
              outs[i].correct ? kGood : kMuted, &fonts::FreeSansBold12pt7b,
              middle_left);
    drawLabel(outs[i].phrase.c_str(), x + 54, y + (row_h - 8) / 2, kText,
              &fonts::FreeSans12pt7b, middle_left);
  }

  const Rect next{kW / 2 - 200, kH - 96, 400, 76};
  drawButton(next, "NEXT", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  addAction(next, Action::Next);
}

void repaint(uint32_t now_ms) {
  uikit::clearTargets();
  g_drawn_screen = (int)g_game->screen();
  switch (g_game->screen()) {
    case fivehead::Screen::Home: drawHome(); break;
    case fivehead::Screen::CategorySelect: drawCategorySelect(); break;
    case fivehead::Screen::Ready: drawReady(now_ms); break;
    case fivehead::Screen::Round: drawRound(now_ms); break;
    case fivehead::Screen::Results: drawResults(); break;
    case fivehead::Screen::Settings: drawSettings(); break;
  }
  uikit::present();
}

void flash(bool correct, const std::string &phrase, uint32_t now_ms) {
  g_flash_correct = correct;
  g_flash_phrase = phrase;
  g_flash_until = now_ms + g_game->settings().flash_ms;
  g_dirty = true;
}

void scoreIt(bool correct, uint32_t now_ms) {
  if (g_game->screen() != fivehead::Screen::Round) return;
  if (now_ms < g_flash_until) return;  // still showing the previous verdict
  const std::string phrase = g_game->currentPhrase();
  if (correct) {
    g_game->correct(now_ms);
    audio::correct();
  } else {
    g_game->pass(now_ms);
    audio::skip();
  }
  flash(correct, phrase, now_ms);
}

// Tilt is read from the screen-normal axis, which is near zero while the
// device is upright against a forehead and swings hard when tipped.
void pollTilt(uint32_t now_ms) {
  if (g_game->screen() != fivehead::Screen::Round) return;
  if (!M5.Imu.isEnabled()) return;
  if (now_ms < g_flash_until) return;

  if (now_ms - g_tilt_sampled < kTiltSampleMs) return;
  g_tilt_sampled = now_ms;

  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

  const auto prof = fivehead::tiltProfile(g_game->settings().tilt_sensitivity);
  if (fabsf(az) < prof.neutral_g) {
    g_armed = true;  // returned to upright; ready for the next gesture
    g_tilt_since = 0;
    return;
  }
  if (!g_armed || fabsf(az) < prof.trigger_g) {
    g_tilt_since = 0;
    return;
  }

  const bool down = az > 0;
  if (g_tilt_since == 0 || down != g_tilt_down) {
    // Start (or restart) the hold. A gesture that changes direction part-way
    // through is someone repositioning, not a decision.
    g_tilt_since = now_ms;
    g_tilt_down = down;
    return;
  }
  if (now_ms - g_tilt_since < kTiltHoldMs) return;

  g_armed = false;
  g_tilt_since = 0;
  scoreIt(g_game->settings().tilt_swapped ? !down : down, now_ms);
}

}  // namespace

void begin(fivehead::Game *game, std::vector<Pack> *packs,
           const content::LoadReport &report) {
  g_game = game;
  g_packs = packs;
  g_report = report;
  g_flash_until = 0;
  g_armed = false;
  g_dirty = true;
}

void invalidate() { g_dirty = true; }

namespace {
// The text entry closed - by a tap on DONE or CANCEL, or by the keyboard.
void onEntryClosed() {
    if (textentry::accepted()) {
      g_game->mutableSettings().team_names[g_editing_team] =
          textentry::text();
      settings_store::saveFive(g_game->settings());
    }
    g_dirty = true;  // rebuild this screen's own hit targets
}
}  // namespace

void tick(uint32_t now_ms) {
  if (!g_game) return;

  // FiveHead announces its winner as a line inside the home screen rather than
  // on a screen of its own, so nothing ever marked the end of a match — you
  // won in silence. Fires on the transition, not on every repaint of it.
  const bool over = g_game->matchOver();
  if (over && !g_match_was_over) audio::fanfare();
  g_match_was_over = over;

  if (textentry::active()) {
    if (textentry::tick(now_ms) == textentry::Result::Closed) onEntryClosed();
    return;
  }
  if (packpicker::active()) {
    packpicker::tick(now_ms);
    return;
  }

  if (g_game->screen() == fivehead::Screen::Ready) {
    const uint32_t prev = g_ready_left_ms / 1000;
    g_ready_left_ms = g_game->readyRemainingMs(now_ms);
    if (g_ready_left_ms / 1000 != prev) {
      g_redraw_clock_only = true;
      g_dirty = true;
    }
    if (g_game->readyElapsed(now_ms)) {
      g_game->beginPlay(now_ms);
      audio::select();
      // Disarmed until the device settles: see g_armed above.
      g_armed = false;
      g_redraw_clock_only = false;
      g_dirty = true;
    }
  } else if (g_game->screen() == fivehead::Screen::Round) {
    pollTilt(now_ms);

    // The clock is on screen, so it has to be redrawn as it counts down.
    static uint32_t last_second = 0;
    const uint32_t sec = g_game->remainingMs(now_ms) / 1000;
    if (sec != last_second) {
      last_second = sec;
      // Only the digits changed — everything else on screen is unmoved.
      g_redraw_clock_only = true;
      g_dirty = true;
    }
    if (g_flash_until && now_ms >= g_flash_until) {
      g_flash_until = 0;
      g_redraw_clock_only = false;  // coming out of a flash needs the lot
      g_dirty = true;
    }
    if (g_game->expired(now_ms)) {
      g_game->endRound(now_ms);
      audio::buzzer();
      g_dirty = true;
    }
  }

  // A screen change always needs the full draw, whatever the cheap-path flag
  // says. beginPlay() switches Ready -> Round from inside this same tick, so
  // without this the round's first paint would be a clock patch over the
  // get-ready screen.
  if ((int)g_game->screen() != g_drawn_screen) {
    g_redraw_clock_only = false;
    g_dirty = true;
  }

  // Cheap path: repaint just the ticking digits, leaving hit targets and the
  // rest of the screen untouched.
  if (g_dirty && g_redraw_clock_only && now_ms >= g_flash_until) {
    const bool round = g_game->screen() == fivehead::Screen::Round;
    const bool ready = g_game->screen() == fivehead::Screen::Ready;
    if (round || ready) {
      g_dirty = false;
      g_redraw_clock_only = false;
      const uint32_t t0 = micros();
      if (round) drawClockPatch(now_ms);
      else drawReadyPatch();
      uikit::present();
      uikit::noteRepaint(micros() - t0, 60);
      return;
    }
  }

  if (g_dirty) {
    g_dirty = false;
    g_redraw_clock_only = false;
    const uint32_t t0 = micros();
    repaint(now_ms);
    uikit::noteRepaint(micros() - t0, 50 + (int)g_game->screen());
  }
}

void handleTap(int x, int y, uint32_t now_ms) {
  if (!g_game) return;

  if (packpicker::active()) {
    if (packpicker::handleTap(x, y) == packpicker::Result::Closed) {
      g_game->mutableSettings().enabled_packs = packpicker::mask();
      settings_store::saveFive(g_game->settings());
      g_dirty = true;
    }
    return;
  }

  if (textentry::active()) {
    if (textentry::handleTap(x, y) == textentry::Result::Closed) onEntryClosed();
    return;
  }

  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return;

  switch ((Action)action) {
    case Action::Play:
      audio::select();
      g_game->goTo(fivehead::Screen::CategorySelect);
      break;
    case Action::Menu:
      // Only reachable between rounds; the round screen offers no way out.
      audio::select();
      if (g_game->screen() == fivehead::Screen::CategorySelect) {
        g_game->goTo(fivehead::Screen::Home);
      } else if (g_game->screen() == fivehead::Screen::Settings) {
        settings_store::saveFive(g_game->settings());
        g_game->goTo(fivehead::Screen::Home);
      } else {
        app::requestExit();
      }
      break;
    case Action::PickCategory:
      audio::select();
      g_armed = false;
      g_game->startRound((size_t)param, now_ms, (uint32_t)esp_random());
      break;
    case Action::Correct: scoreIt(true, now_ms); break;
    case Action::Pass: scoreIt(false, now_ms); break;
    case Action::Next:
      audio::select();
      g_game->nextTurn();
      g_game->goTo(fivehead::Screen::Home);
      break;
    case Action::About:
      audio::select();
      app::showAbout();
      break;
    case Action::Settings:
      audio::select();
      g_game->goTo(fivehead::Screen::Settings);
      break;
    case Action::EditTeam:
      audio::select();
      g_editing_team = param;
      textentry::open(param == 0 ? "TEAM 1 NAME" : "TEAM 2 NAME",
                      g_game->settings().team_names[param], 20);
      break;
    case Action::SettingDec:
      audio::select();
      adjustSetting(param, -1);
      break;
    case Action::SettingInc:
      audio::select();
      adjustSetting(param, +1);
      break;
    case Action::Again:
      audio::select();
      g_game->resetMatch();
      break;
    case Action::None: break;
  }
  g_redraw_clock_only = false;
  g_dirty = true;
}

}  // namespace fivehead_ui
}  // namespace tabulous
