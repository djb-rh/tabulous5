// Persisting Settings to NVS.
//
// A device that forgets your team names, scoring mode and volume every time it
// reboots is a device people stop trusting, so everything the settings screen
// can change is stored.
#pragma once

#include "fivehead.h"
#include "padmap.h"
#include "phrase_game.h"

namespace tabulous {
namespace settings_store {

// Fills `out` from NVS, leaving any field absent from storage at its default.
void load(Settings *out);

// Written on leaving the settings screen and on confirming a team name, rather
// than on every stepper tap — NVS writes are flash writes.
void save(const Settings &settings);

// FiveHead keeps its own settings under its own keys, so the two games can be
// tuned independently.
// Solitaire's options. Kept here rather than in the rules because most of
// them are presentation or convenience, not rules.
struct SolitaireSettings {
  bool draw_three = false;
  int max_passes = 0;        // 0 = unlimited
  bool tap_to_foundation = true;
  bool show_timer = true;
};

// Console-wide display preference, not per game.
bool loadLightTheme();
void saveLightTheme(bool light);

// How the launcher list is arranged: which games are shown and in what order.
// Console-wide, so it lives here rather than in any one game's settings.
constexpr int kMaxMenuEntries = 16;

struct MenuPrefs {
  uint8_t order[kMaxMenuEntries] = {0};
  uint8_t count = 0;
  uint16_t hidden = 0;  // bit per ENTRY index, not per display position
};

// Reconciles whatever is stored against the games that actually exist now:
// unknown or duplicated indices are dropped and anything missing is appended
// in its natural order. That way adding a game later just makes it show up at
// the bottom rather than orphaning a saved arrangement.
void loadMenu(MenuPrefs *out, uint8_t entry_count);
void saveMenu(const MenuPrefs &prefs);

void loadSolitaire(SolitaireSettings *out);
void saveSolitaire(const SolitaireSettings &settings);

void loadFive(fivehead::Settings *out);
void saveFive(const fivehead::Settings &settings);

// The NES picture: 2x with the on-screen pad either side, or 3x, the full
// height, for a USB gamepad.
struct NesSettings {
  uint8_t scale = 2;  // 2 or 3
};
void loadNes(NesSettings *out);
void saveNes(const NesSettings &settings);

// The Game Boy picture: 3x with the on-screen pad either side, or 5x, which is
// exactly the panel's height, for a USB gamepad.
struct GbSettings {
  uint8_t scale = 3;  // 3 or 5
};
void loadGb(GbSettings *out);
void saveGb(const GbSettings &settings);

// The SNES picture: 2x with the on-screen pad either side, or 3x for a USB
// gamepad.
struct SnesSettings {
  uint8_t scale = 2;  // 2 or 3
};
void loadSnes(SnesSettings *out);
void saveSnes(const SnesSettings &settings);

// Which USB gamepad button is which; taught in the Gamepad Test screen.
void loadPadMap(padmap::Map *out);
void savePadMap(const padmap::Map &map);

}  // namespace settings_store
}  // namespace tabulous
