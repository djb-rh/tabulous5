#include "phrase_ui.h"

#include "confetti.h"

#include "uikit.h"

#include <M5Unified.h>

#include <cstdio>

#include "audio.h"
#include "app.h"
#include "orientation.h"
#include "settings_store.h"
#include "packpicker.h"
#include "textentry.h"
#include "theme.h"

namespace tabulous {
namespace ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;
using uikit::present;

Game *g_game = nullptr;
std::vector<Pack> *g_packs = nullptr;
content::LoadReport g_report;

// Set when the screen needs repainting; consumed by tick().
bool g_dirty = true;

// Whichever team didn't start the last round starts the next one.
Team g_next_starter = Team::A;

// Buzzer screen flash animation. Seven half-cycles over kSlamMs reads as an
// alarm; longer becomes a strobe you have to look away from.
constexpr uint32_t kSlamMs = 560;
uint32_t g_buzz_started = 0;

// Separate from confetti::active(): this stays set for one frame after the
// burst ends so the last pieces get erased.
bool g_confetti_on = false;

// Which team the shared keyboard is currently renaming.
int g_editing_team = 0;

// Slowest tap-handler action seen, so a costly one (an NVS write, say) can be
// attributed rather than guessed at.
uint32_t g_worst_action_us = 0;
int g_worst_action = -1;

// ------------------------------------------------------------------ widgets

// Hit targets for the screen currently on display, rebuilt on every repaint so
// they can never drift out of sync with what's drawn.
enum class Action : uint8_t {
  None,
  Play,
  Settings,
  Words,
  Back,
  Flip,
  Menu,
  About,
  PickCategory,
  GotIt,
  Skip,
  BonusYes,
  BonusNo,
  FlipAward,
  PlayAgain,
  SettingDec,
  SettingInc,
  EditTeam,
  ChoosePacks,
};

// Screens here use their own Action enum; uikit stores actions as ints.
void addAction(const Rect &r, Action a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

const char *teamName(Team t) {
  return g_game->settings().team_names[teamIndex(t)].c_str();
}

// ------------------------------------------------------------------ screens

void drawHome() {
  auto &g = gfx();
  g.fillScreen(kBg);

  drawLabel("PHRASECRAZE", kW / 2, 72, kAccent, &fonts::FreeSansBold24pt7b,
            middle_center, 2);
  drawLabel("first to 7 wins", kW / 2, 132, kMuted, &fonts::FreeSans12pt7b,
            middle_center);

  // Two score cards.
  const int card_w = 520, card_h = 210, gap = 144;
  const int y = 180;
  for (int i = 0; i < 2; i++) {
    const Team t = i == 0 ? Team::A : Team::B;
    const int x = kMargin + i * (card_w + gap);
    const bool starts = (t == g_next_starter);
    g.fillRoundRect(x, y, card_w, card_h, 20, kSurface);
    if (starts) g.drawRoundRect(x, y, card_w, card_h, 20, kAccent);
    // A band in the team's colour — the same colour the round screen will use.
    g.fillRoundRect(x, y, card_w, 12, 6, rgb(kTeamHex[i]));

    drawLabel(teamName(t), x + card_w / 2, y + 50, kMuted,
              &fonts::FreeSans12pt7b, middle_center);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_game->score(t));
    drawLabel(buf, x + card_w / 2, y + 130, kText, &fonts::FreeSansBold24pt7b,
              middle_center, 2);
    drawLabel(starts ? "starts  -  tap to rename" : "tap to rename",
              x + card_w / 2, y + card_h - 24, starts ? kAccent : kMuted,
              &fonts::FreeSans9pt7b, middle_center);
    addAction(Rect{x, y, card_w, card_h}, Action::EditTeam, i);
  }

  const Rect play{kW / 2 - 240, 440, 480, 120};
  drawButton(play, "PLAY", kGood, kOnFill, &fonts::FreeSansBold24pt7b);
  addAction(play, Action::Play);

  const Rect settings{kMargin, 610, 300, 74};
  drawButton(settings, "SETTINGS", kSurfaceLift, kText);
  addAction(settings, Action::Settings);

  const Rect words{kW - kMargin - 300, 610, 300, 74};
  drawButton(words, "WORDS", kSurfaceLift, kText);
  addAction(words, Action::Words);

  // Leaving the game is offered only here, between rounds — never on the round
  // screen, where a stray tap would throw away a turn in progress.
// A consistent "?" in the same corner on every game, so it is findable
// without hunting. Offered only where MENU is — never mid-round.
  const Rect help{kW - kMargin - 64, 22, 64, 64};
  uikit::fillRoundRectFast(help.x, help.y, help.w, help.h, 32, kSurfaceLift);
  drawLabel("?", help.x + 32, help.y + 32, kText, &fonts::FreeSansBold18pt7b,
            middle_center);
  addAction(help, Action::About);

  const Rect menu{kW / 2 - 150, 610, 300, 74};
  drawButton(menu, "MENU", kSurfaceLift, kMuted, &fonts::FreeSansBold12pt7b);
  addAction(menu, Action::Menu);

}

void drawCategorySelect() {
  auto &g = gfx();
  g.fillScreen(kBg);

  drawLabel("PICK A CATEGORY", kMargin, 46, kText, &fonts::FreeSansBold18pt7b);
  char sub[96];
  snprintf(sub, sizeof(sub), "%s starts this round", teamName(g_next_starter));
  drawLabel(sub, kMargin, 96, kMuted, &fonts::FreeSans12pt7b);

  // Four across, as many rows as needed; 8 shipped packs make a tidy 4x2.
  const int cols = 4;
  const int tile_w = (kW - 2 * kMargin - (cols - 1) * 20) / cols;
  const int tile_h = 200;
  const int top = 150;

  int slot = 0;
  for (size_t i = 0; i < g_packs->size() && i < 8; i++) {
    if (!packpicker::enabled(g_game->settings().enabled_packs, i,
                             g_packs->size())) {
      continue;
    }
    const Pack &pack = (*g_packs)[i];
    const int col = slot % cols, row = slot / cols;
    slot++;
    const Rect r{kMargin + col * (tile_w + 20), top + row * (tile_h + 20),
                 tile_w, tile_h};
    const uint32_t hex = pack.meta().color;
    g.fillRoundRect(r.x, r.y, r.w, r.h, 20, rgb(hex));

    drawFitted(g, pack.meta().name, r.x + r.w / 2, r.y + r.h / 2 - 14,
               r.w - 24, 96, kPhraseLadder + 2, kPhraseLadderLen - 2,
               inkFor(hex));
    char count[32];
    snprintf(count, sizeof(count), "%u phrases", (unsigned)pack.size());
    drawLabel(count, r.x + r.w / 2, r.y + r.h - 26, inkFor(hex),
              &fonts::FreeSans9pt7b, middle_center);
    addAction(r, Action::PickCategory, (int)i);
  }

  const Rect back{kMargin, kH - 84, 220, 64};
  drawButton(back, "BACK", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Back);
}

void drawRound() {
  auto &g = gfx();
  const Pack *pack = g_game->currentPack();
  const uint32_t cat_hex = pack ? pack->meta().color : 0x4C9F70;

  // The background is the HOLDING TEAM's colour, not the category's. Mid-round
  // the category is already known; who is about to be caught holding it is the
  // thing that has to be unmistakable.
  const uint32_t hex = kTeamHex[teamIndex(g_game->holder())];
  const uint16_t ink = inkFor(hex);
  g.fillScreen(rgb(hex));

  // Category keeps its identity as a chip in its own colour, top-right.
  if (pack) {
    const int chip_h = 46;
    const int chip_w = 300;
    const Rect chip{kW - kMargin - chip_w, 22, chip_w, chip_h};
    g.fillRoundRect(chip.x, chip.y, chip.w, chip.h, 12, rgb(cat_hex));
    drawLabel(pack->meta().name.c_str(), chip.x + chip.w / 2,
              chip.y + chip.h / 2, inkFor(cat_hex), &fonts::FreeSans9pt7b,
              middle_center);
  }

  char holder[96];
  snprintf(holder, sizeof(holder), "%s", teamName(g_game->holder()));
  drawLabel(holder, kMargin, 30, ink, &fonts::FreeSansBold18pt7b);
  drawLabel("holding", kMargin, 62, ink, &fonts::FreeSans9pt7b);

  // No countdown anywhere on this screen. The hidden timer is the game.
  drawFitted(g, g_game->currentPhrase(), kW / 2, kH / 2 - 10, kW - 160, 300,
             kPhraseLadder, kPhraseLadderLen, ink);

  drawLabel("tap anywhere for GOT IT", kW / 2, kH - 130, ink,
            &fonts::FreeSans12pt7b, middle_center);

  // The whole screen is GOT IT; the skip chip sits on top and wins the hit
  // test because targets are checked in reverse order.
  addAction(Rect{0, 0, kW, kH}, Action::GotIt);

  if (g_game->settings().skips_per_round > 0) {
    // Each team has its own budget, so this shows the holder's remaining.
    char label[40];
    snprintf(label, sizeof(label), "SKIP  %u", (unsigned)g_game->skipsLeft());
    const Rect skip{kMargin, kH - 100, 240, 72};
    const bool can = g_game->canSkip();
    g.fillRoundRect(skip.x, skip.y, skip.w, skip.h, 16,
                    rgb(shade(hex, can ? 70 : 85)));
    drawLabel(label, skip.x + skip.w / 2, skip.y + skip.h / 2,
              can ? ink : rgb(shade(hex, 55)), &fonts::FreeSansBold12pt7b,
              middle_center);
    if (can) addAction(skip, Action::Skip);
  }
}

void drawBuzzer(uint32_t now_ms) {
  auto &g = gfx();
  const uint32_t elapsed = now_ms - g_buzz_started;

  // The slam: four hard frames of nothing but the red field and the word,
  // before the score screen resolves underneath it. Drawing only those two
  // things is both the point — an alarm should be the only thing on screen —
  // and what makes it cheap enough to animate, since the full screen below
  // costs a rounded card, three buttons and their hit targets every frame.
  //
  // No targets are registered during the slam, so a tap that lands in it
  // cannot accidentally answer the bonus question that appears next.
  if (elapsed < kSlamMs) {
    const bool on = (elapsed / (kSlamMs / 7)) % 2 == 0;
    g.fillScreen(on ? kDanger : kBg);
    // kOnFill, not kText: kText is near-black in light mode and vanishes
    // against the red.
    drawLabel("TIME!", kW / 2, kH / 2, on ? kOnFill : kDanger,
              &fonts::FreeSansBold24pt7b, middle_center, 3);
    return;
  }

  g.fillScreen(kBg);
  drawLabel("TIME!", kW / 2, 86, kDanger, &fonts::FreeSansBold24pt7b,
            middle_center, 2);

  char line[128];
  snprintf(line, sizeof(line), "%s scores", teamName(g_game->scoringTeam()));
  drawLabel(line, kW / 2, 168, kText, &fonts::FreeSansBold18pt7b,
            middle_center);

  snprintf(line, sizeof(line), "%s %u   —   %s %u",
           teamName(Team::A), (unsigned)g_game->score(Team::A),
           teamName(Team::B), (unsigned)g_game->score(Team::B));
  drawLabel(line, kW / 2, 224, kMuted, &fonts::FreeSans12pt7b, middle_center);

  // The phrase they were stuck on, now revealed for the bonus guess.
  g.fillRoundRect(kMargin, 268, kW - 2 * kMargin, 172, 20, kSurface);
  drawFitted(g, g_game->currentPhrase(), kW / 2, 354, kW - 2 * kMargin - 60,
             140, kPhraseLadder + 1, kPhraseLadderLen - 1, kText);

  snprintf(line, sizeof(line), "Bonus point — did %s get it?",
           teamName(g_game->scoringTeam()));
  drawLabel(line, kW / 2, 476, kMuted, &fonts::FreeSans12pt7b, middle_center);

  const Rect yes{kW / 2 - 330, 512, 300, 96};
  const Rect no{kW / 2 + 30, 512, 300, 96};
  drawButton(yes, "YES  +1", kGood, kOnFill);
  drawButton(no, "NO", kSurfaceLift, kText);
  addAction(yes, Action::BonusYes);
  addAction(no, Action::BonusNo);

  // Escape hatch for when passing went astray and the device's idea of who
  // was holding it is wrong.
  const Rect flip{kW / 2 - 200, 630, 400, 56};
  drawButton(flip, "wrong team?", kBg, kMuted, &fonts::FreeSans12pt7b);
  addAction(flip, Action::FlipAward);
}

// The parts of the win screen the confetti falls across, and therefore the
// parts that have to go back down after each erase. Registers no targets —
// it is a partial redraw, and the full repaint owns the target list.
void drawGameOverText() {
  auto &g = gfx();
  const Team w = g_game->winner();
  drawLabel("WINNER", kW / 2, 120, kMuted, &fonts::FreeSans12pt7b,
            middle_center);
  drawFitted(g, g_game->settings().team_names[teamIndex(w)], kW / 2, 250,
             kW - 200, 180, kPhraseLadder, kPhraseLadderLen, kAccent);

  char line[128];
  snprintf(line, sizeof(line), "%s %u   —   %s %u", teamName(Team::A),
           (unsigned)g_game->score(Team::A), teamName(Team::B),
           (unsigned)g_game->score(Team::B));
  drawLabel(line, kW / 2, 400, kText, &fonts::FreeSansBold18pt7b,
            middle_center);
}

void drawGameOver() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawGameOverText();

  const Rect again{kW / 2 - 240, 500, 480, 120};
  drawButton(again, "PLAY AGAIN", kGood, kOnFill, &fonts::FreeSansBold24pt7b);
  addAction(again, Action::PlayAgain);
}

// Settings rows are described declaratively so drawing and hit-testing can't
// disagree about how many there are or what order they're in.
struct SettingRow {
  const char *label;
  char value[48];
};

int settingCount() { return 10; }

// Flip-speed choices, in ms of steady holding before the screen commits.
constexpr uint16_t kFlipDelays[] = {0, 250, 500, 1000, 2000};
constexpr const char *kFlipDelayNames[] = {"instant", "0.25 s", "0.5 s",
                                           "1 s", "2 s"};
constexpr int kFlipDelayCount = 5;

int flipDelayIndex(uint16_t ms) {
  for (int i = 0; i < kFlipDelayCount; i++) {
    if (kFlipDelays[i] == ms) return i;
  }
  return 1;
}

void settingValue(int index, char *out, size_t len) {
  const Settings &s = g_game->settings();
  switch (index) {
    case 0: snprintf(out, len, "%u", (unsigned)s.target_score); break;
    case 1: snprintf(out, len, "%u s", (unsigned)(s.timer.min_ms / 1000)); break;
    case 2: snprintf(out, len, "%u s", (unsigned)(s.timer.max_ms / 1000)); break;
    case 3:
      snprintf(out, len, "%s",
               s.max_difficulty == Difficulty::Easy
                   ? "kids"
                   : (s.max_difficulty == Difficulty::Medium ? "normal" : "all"));
      break;
    case 4: snprintf(out, len, "%u", (unsigned)s.skips_per_round); break;
    case 5:
      snprintf(out, len, "%s %u", s.sound_enabled ? "on" : "off",
               (unsigned)(s.volume * 100 / 255));
      break;
    case 6: snprintf(out, len, "%s", s.auto_rotate ? "on" : "off"); break;
    case 7:
      snprintf(out, len, "%s",
               s.scoring == ScoringMode::Classic ? "classic" : "per phrase");
      break;
    case 8:
      snprintf(out, len, "%s",
               orientation::calibrated() ? "calibrated" : "not set");
      break;
    case 9:
      snprintf(out, len, "%s", kFlipDelayNames[flipDelayIndex(s.flip_delay_ms)]);
      break;
    default: snprintf(out, len, "-"); break;
  }
}

const char *settingLabel(int index) {
  switch (index) {
    case 0: return "Points to win";
    case 1: return "Shortest round";
    case 2: return "Longest round";
    case 3: return "Difficulty";
    case 4: return "Skips per round";
    case 5: return "Sound / volume";
    case 6: return "Auto-rotate";
    case 7: return "Scoring";
    case 8: return "Flip screen now";
    case 9: return "Flip speed";
    default: return "";
  }
}

void adjustSetting(int index, int delta) {
  Settings &s = g_game->mutableSettings();
  switch (index) {
    case 0:
      s.target_score = (uint8_t)constrain((int)s.target_score + delta, 3, 15);
      break;
    case 1:
      s.timer.min_ms = (uint32_t)constrain(
          (int)(s.timer.min_ms / 1000) + delta * 5, 15, 120) * 1000;
      if (s.timer.min_ms > s.timer.max_ms) s.timer.max_ms = s.timer.min_ms;
      break;
    case 2:
      s.timer.max_ms = (uint32_t)constrain(
          (int)(s.timer.max_ms / 1000) + delta * 5, 15, 180) * 1000;
      if (s.timer.max_ms < s.timer.min_ms) s.timer.min_ms = s.timer.max_ms;
      break;
    case 3: {
      int d = (int)s.max_difficulty + delta;
      s.max_difficulty = (Difficulty)constrain(d, 0, 2);
      break;
    }
    case 4:
      s.skips_per_round = (uint8_t)constrain((int)s.skips_per_round + delta, 0, 9);
      break;
    case 5: {
      const int step = 24;
      int v = (int)s.volume + delta * step;
      if (v <= 0) {
        s.sound_enabled = false;
        s.volume = 0;
      } else {
        s.sound_enabled = true;
        s.volume = (uint8_t)constrain(v, step, 255);
      }
      audio::setEnabled(s.sound_enabled);
      audio::setVolume(s.volume);
      // Give immediate feedback — otherwise a volume change is silent until
      // the next game event, which reads as the control being broken. Not
      // select(): that one is mixed deliberately quiet because it fires on
      // every tap, which makes it a misleading preview of the level.
      audio::correct();
      break;
    }
    case 6:
      s.auto_rotate = !s.auto_rotate;
      orientation::setEnabled(s.auto_rotate);
      break;
    case 7:
      s.scoring = s.scoring == ScoringMode::Classic ? ScoringMode::PointPerPhrase
                                                    : ScoringMode::Classic;
      break;
    case 8:
      // Either stepper flips: the row is an action, not a value.
      orientation::flipNow();
      break;
    case 9: {
      int i = flipDelayIndex(s.flip_delay_ms) + delta;
      if (i < 0) i = 0;
      if (i >= kFlipDelayCount) i = kFlipDelayCount - 1;
      s.flip_delay_ms = kFlipDelays[i];
      orientation::setStableMs(s.flip_delay_ms);
      break;
    }
    default: break;
  }
}

void drawSettings() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("SETTINGS", kMargin, 34, kText, &fonts::FreeSansBold18pt7b);

  // Two columns: ten rows will not fit down one side of a 720 px screen at a
  // touch-friendly row height.
  const int cols = 2;
  const int col_w = (kW - 2 * kMargin - 24) / cols;
  const int rows_per_col = (settingCount() + cols - 1) / cols;
  const int top = 104;
  const int row_h = 96;

  for (int i = 0; i < settingCount(); i++) {
    const int col = i / rows_per_col;
    const int row = i % rows_per_col;
    const int x = kMargin + col * (col_w + 24);
    const int y = top + row * row_h;
    const int inner_h = row_h - 14;

    g.fillRoundRect(x, y, col_w, inner_h, 14, kSurface);
    drawLabel(settingLabel(i), x + 22, y + inner_h / 2, kText,
              &fonts::FreeSans12pt7b, middle_left);

    char value[48];
    settingValue(i, value, sizeof(value));
    drawLabel(value, x + col_w - 190, y + inner_h / 2, kAccent,
              &fonts::FreeSans9pt7b, middle_right);

    const int bh = inner_h - 20;
    const int by = y + 10;
    if (i == 8) {
      // An action, not a value — one wide button rather than a pair of steppers.
      const Rect act{x + col_w - 178, by, 166, bh};
      drawButton(act, "FLIP", kAccent, kInk, &fonts::FreeSansBold12pt7b);
      addAction(act, Action::SettingInc, i);
    } else {
      const Rect dec{x + col_w - 178, by, 78, bh};
      const Rect inc{x + col_w - 90, by, 78, bh};
      drawButton(dec, "-", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
      drawButton(inc, "+", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
      addAction(dec, Action::SettingDec, i);
      addAction(inc, Action::SettingInc, i);
    }
  }

  const Rect back{kMargin, kH - 74, 220, 58};
  drawButton(back, "BACK", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Back);
}

void drawWords() {
  auto &g = gfx();
  g.fillScreen(kBg);
  drawLabel("WORD PACKS", kMargin, 44, kText, &fonts::FreeSansBold18pt7b);

  char line[160];
  snprintf(line, sizeof(line), "%u packs, %u phrases   -   %u KB used of %u KB",
           (unsigned)g_report.packs_loaded, (unsigned)g_report.phrases_total,
           (unsigned)(g_report.bytes_used / 1024),
           (unsigned)(g_report.bytes_total / 1024));
  drawLabel(line, kMargin, 92, kMuted, &fonts::FreeSans9pt7b);

  if (g_report.used_builtin) {
    drawLabel("No packs on flash — using the built-in starter pack. "
              "Run: pio run -e tab5 -t uploadfs",
              kMargin, 120, kDanger, &fonts::FreeSans9pt7b);
  }

  const int cols = 2;
  const int col_w = (kW - 2 * kMargin - 20) / cols;
  const int top = 156;
  const int row_h = 74;
  for (size_t i = 0; i < g_packs->size() && i < 10; i++) {
    const Pack &pack = (*g_packs)[i];
    const int col = (int)i % cols, row = (int)i / cols;
    const int x = kMargin + col * (col_w + 20);
    const int y = top + row * row_h;
    g.fillRoundRect(x, y, col_w, row_h - 12, 12, kSurface);
    g.fillRoundRect(x + 14, y + 14, 12, row_h - 40, 6, rgb(pack.meta().color));
    drawLabel(pack.meta().name.c_str(), x + 42, y + (row_h - 12) / 2, kText,
              &fonts::FreeSans12pt7b, middle_left);
    char count[32];
    snprintf(count, sizeof(count), "%u", (unsigned)pack.size());
    drawLabel(count, x + col_w - 20, y + (row_h - 12) / 2, kMuted,
              &fonts::FreeSans12pt7b, middle_right);
  }

  drawLabel("Edit packs over WiFi — coming in the next phase.", kMargin,
            kH - 120, kMuted, &fonts::FreeSans9pt7b);

  const Rect choose{kW - kMargin - 320, kH - 78, 320, 58};
  drawButton(choose, "CHOOSE PACKS", kAccent, kInk,
             &fonts::FreeSansBold12pt7b);
  addAction(choose, Action::ChoosePacks);

  const Rect back{kMargin, kH - 78, 220, 60};
  drawButton(back, "BACK", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Back);
}

void repaint(uint32_t now_ms) {
  uikit::clearTargets();
  switch (g_game->screen()) {
    case Screen::Home: drawHome(); break;
    case Screen::CategorySelect: drawCategorySelect(); break;
    case Screen::Round: drawRound(); break;
    case Screen::Buzzer: drawBuzzer(now_ms); break;
    case Screen::GameOver: drawGameOver(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::Words: drawWords(); break;
  }
  present();
}

}  // namespace

// ---------------------------------------------------------------- interface

void begin(Game *game, std::vector<Pack> *packs,
           const content::LoadReport &report) {
  g_game = game;
  g_packs = packs;
  g_report = report;

  g_dirty = true;

}

void invalidate() {
  g_dirty = true;
  // Any screen change abandons the burst; without this it would keep erasing
  // rectangles over whatever replaced the win screen.
  g_confetti_on = false;
  confetti::stop();
}

uint32_t worstActionUs() { return g_worst_action_us; }
int worstAction() { return g_worst_action; }

namespace {
// The text entry closed - by a tap on DONE or CANCEL, or by the keyboard.
void onEntryClosed() {
    if (textentry::accepted()) {
      g_game->mutableSettings().team_names[g_editing_team] =
          textentry::text();
      settings_store::save(g_game->settings());
    }
    g_dirty = true;  // rebuild this screen's own hit targets
}
}  // namespace

void tick(uint32_t now_ms) {
  if (!g_game) return;

  if (textentry::active()) {
    if (textentry::tick(now_ms) == textentry::Result::Closed) onEntryClosed();
    return;
  }
  if (packpicker::active()) {
    packpicker::tick(now_ms);
    return;
  }

  if (g_game->screen() == Screen::Round) {
    uint16_t hz = 0;
    if (g_game->tick(now_ms, &hz)) audio::beep(hz);

    if (g_game->expired(now_ms)) {
      g_game->buzz(now_ms);
      g_buzz_started = now_ms;
      audio::buzzer();
      g_dirty = true;
    }
  } else if (g_game->screen() == Screen::Buzzer) {
    // Keep repainting through the slam, plus one frame to settle on the full
    // screen underneath it.
    if (now_ms - g_buzz_started < kSlamMs + 80) g_dirty = true;
  } else if (g_game->screen() == Screen::GameOver && g_confetti_on) {
    // Deliberately NOT via g_dirty: a full repaint costs ~45 ms and would
    // block the loop for the whole celebration. Erase, put the text back,
    // advance, draw — the hit targets from the last full repaint survive
    // untouched, so PLAY AGAIN stays live throughout.
    //
    // Runs one frame PAST the end of the burst: erase and repaint the text
    // unconditionally, and only step and draw while it is still alive. Gating
    // the whole branch on active() would stop it with the final frame's
    // pieces still painted on the screen.
    const bool alive = confetti::active(now_ms);
    confetti::erase();
    drawGameOverText();
    if (alive) {
      confetti::step(now_ms);
      confetti::draw();
    } else {
      g_confetti_on = false;
    }
  }

  if (g_dirty) {
    g_dirty = false;
    // Every repaint blocks the loop and therefore eats taps, so all of them
    // are measured, not just the ones over some threshold.
    const uint32_t t0 = micros();
    repaint(now_ms);
    uikit::noteRepaint(micros() - t0, (int)g_game->screen());
  }
}

void handleTap(int x, int y, uint32_t now_ms) {
  if (!g_game) return;

  if (packpicker::active()) {
    if (packpicker::handleTap(x, y) == packpicker::Result::Closed) {
      g_game->mutableSettings().enabled_packs = packpicker::mask();
      settings_store::save(g_game->settings());
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

  {
    const uint32_t action_t0 = micros();
    const Action acted = (Action)action;
    const struct { int param; } it_storage{param};
    const auto *it = &it_storage;

    switch (acted) {
      case Action::Play:
        audio::select();
        g_game->goTo(Screen::CategorySelect);
        break;
      case Action::Settings:
        audio::select();
        g_game->goTo(Screen::Settings);
        break;
      case Action::Words:
        audio::select();
        g_game->goTo(Screen::Words);
        break;
      case Action::Back:
        audio::select();
        if (g_game->screen() == Screen::Settings) {
          settings_store::save(g_game->settings());
        }
        g_game->goTo(Screen::Home);
        break;
      case Action::Flip:
        audio::select();
        orientation::flipNow();
        break;
      case Action::Menu:
        audio::select();
        app::requestExit();
        break;
      case Action::About:
        audio::select();
        app::showAbout();
        break;
      case Action::PickCategory:
        audio::select();
        g_game->startRound((size_t)it->param, g_next_starter, now_ms,
                           (uint32_t)esp_random());
        // Alternate who starts, so neither team is permanently first.
        g_next_starter = other(g_next_starter);
        break;
      case Action::GotIt:
        audio::correct();
        g_game->gotIt(now_ms);
        break;
      case Action::Skip:
        if (g_game->skip(now_ms)) audio::skip();
        break;
      case Action::BonusYes:
      case Action::BonusNo:
        g_game->resolveBonus(acted == Action::BonusYes);
        if (g_game->screen() == Screen::GameOver) {
          audio::fanfare();
          // Floor is the top of PLAY AGAIN: a piece that fell across the
          // button would erase a bite out of it on the next frame.
          confetti::start(now_ms, 492);
          g_confetti_on = true;
        }
        else audio::select();
        break;
      case Action::FlipAward:
        audio::select();
        g_game->flipAward();
        break;
      case Action::PlayAgain:
        audio::select();
        g_game->resetMatch();
        break;
      case Action::ChoosePacks:
        audio::select();
        packpicker::open("PHRASECRAZE PACKS", g_packs,
                         g_game->settings().enabled_packs);
        break;
      case Action::EditTeam:
        audio::select();
        g_editing_team = it->param;
        textentry::open(it->param == 0 ? "TEAM 1 NAME" : "TEAM 2 NAME",
                        g_game->settings().team_names[it->param],
                        kMaxTeamNameLen);
        break;

      case Action::SettingDec:
        audio::select();
        adjustSetting(it->param, -1);
        break;
      case Action::SettingInc:
        audio::select();
        adjustSetting(it->param, +1);
        break;

      case Action::None:
        break;
    }
    const uint32_t action_us = micros() - action_t0;
    if (action_us > g_worst_action_us) {
      g_worst_action_us = action_us;
      g_worst_action = (int)acted;
    }

    g_dirty = true;
  }
}

}  // namespace ui
}  // namespace tabulous
