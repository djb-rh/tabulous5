// Choosing a cartridge, for any emulated system.
//
// A rail of groups down the left (Favourites, #, A-Z) and pages of titles on
// the right, over a library gathered from the SD card and the built-in
// filesystem together. Nothing here knows what a NES or a Game Boy is: a
// system supplies its directory, its extension, where its favourites live and
// how to tell whether a file is playable.
#pragma once

#include <FS.h>

#include <cstdint>

#include "rom_index.h"

namespace tabulous {
namespace rom_browser {

struct Config {
  const char *title = "";            // shown top left
  const char *dir = "";              // "/nes" — the same on both volumes
  const char *extension = "";        // ".nes"
  const char *favourites_file = "";  // on the built-in filesystem

  // Starred the first time a system is opened, before anyone has starred
  // anything: one file name per line. A library of hundreds of near-identical
  // bootlegs is unusable without a shortlist, and this is that shortlist.
  // Ignored once the favourites file exists, so it never overrides a choice.
  const char *default_favourites = nullptr;
  // The two picture sizes offered, as a label and a magnification.
  const char *scale_label[2] = {"", ""};
  uint8_t scale_value[2] = {0, 0};
  // Fills in status, problem, mapper and bytes for one title, opening the
  // file if it must. Called when a title is picked, not when it is listed:
  // on a card holding thousands, an open is expensive.
  void (*probe)(fs::FS &fs, rom_index::Item *item) = nullptr;
  const char *empty_hint = "";
  // Whether this system can be played with its picture turned. Only an
  // arcade cabinet has an opinion: its monitor stood on its side, and the
  // choice is whether to honour that or fill more of a screen that does not.
  bool orientable = false;
  // One more button in the header, for something only this system has. Null
  // for a system with nothing to put there.
  const char *extra_label = nullptr;
};

enum class Result : uint8_t {
  None, Back, Launch, ScaleChanged, OrientationChanged, Extra
};

// `scale` is the caller's saved preference; it must be one of the two in the
// config, and is returned by scale() from then on.
void begin(const Config &config, uint8_t scale, bool portrait = false);

// Walk the filesystems again next time the screen is entered.
void rescan();

// Scans if it has to, saying so on screen — a full card takes a moment.
void ensureScanned();

// True when something changed and the screen wants painting again.
bool dirty();
void invalidate();

// Paints and registers this screen's hit targets.
void draw();

// Feeds a tap in. On Launch, `*index` is the title picked.
Result handleTap(int x, int y, int *index);

int size();
const rom_index::Item &item(int i);
fs::FS &fsFor(const rom_index::Item &it);
// Runs the config's probe once for this title.
void probeItem(int i);

uint8_t scale();
// Whether this system should be played with the screen turned upright.
bool portrait();
// Shown under the title until the next pick; for "that ROM would not load".
void setError(const char *message);

// Fills the screen with one line, immediately. Opening a cartridge reads it
// from the card and hands it to a core, which takes long enough on a big one
// to look like nothing happened -- and the screen still holds the list, so
// there is nothing to say the tap landed. Call this first.
void showBusy(const char *message);

// Renames the extra button, for one that shows what it is set to.
void setExtraLabel(const char *label);

}  // namespace rom_browser
}  // namespace tabulous
