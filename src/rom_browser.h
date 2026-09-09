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
  // The two picture sizes offered, as a label and a magnification.
  const char *scale_label[2] = {"", ""};
  uint8_t scale_value[2] = {0, 0};
  // Fills in status, problem, mapper and bytes for one title, opening the
  // file if it must. Called when a title is picked, not when it is listed:
  // on a card holding thousands, an open is expensive.
  void (*probe)(fs::FS &fs, rom_index::Item *item) = nullptr;
  const char *empty_hint = "";
};

enum class Result : uint8_t { None, Back, Launch, ScaleChanged };

// `scale` is the caller's saved preference; it must be one of the two in the
// config, and is returned by scale() from then on.
void begin(const Config &config, uint8_t scale);

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
// Shown under the title until the next pick; for "that ROM would not load".
void setError(const char *message);

}  // namespace rom_browser
}  // namespace tabulous
