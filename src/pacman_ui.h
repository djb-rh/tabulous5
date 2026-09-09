// The arcade screen: pick a machine, then play it.
//
// Pac-Man hardware — Pac-Man itself and the Ms. Pac-Man bootleg that runs on an
// unmodified board. ROMs come as .arc files made by tools/mkarcade.py from a
// romset you already have.
#pragma once

#include <cstdint>

namespace tabulous {
namespace pacman_ui {

// Favourites, one file name per line, on the built-in filesystem.
constexpr const char *kFavouritesFile = "/arcade_favs.txt";

void begin();
// Walk the filesystems again on the next begin(): the list changed underneath.
void rescan();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);
bool playing();

}  // namespace pacman_ui
}  // namespace tabulous
