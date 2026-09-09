// The Game Boy screen: pick a cartridge, then play it.
//
// Same shape as nes_ui — the browser, the video path and the on-screen pad are
// shared — so what is left here is the core, its sound, and battery saves.
#pragma once

#include <cstdint>

namespace tabulous {
namespace gb_ui {

// Favourite cartridges, one file name per line, on the built-in filesystem so
// they survive a card swap. The web file manager edits this file too.
constexpr const char *kFavouritesFile = "/gb_favs.txt";

void begin();
// Walk the filesystems again on the next begin(): the list changed underneath.
void rescan();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

// True once a cartridge is running, so the shell knows taps are being polled
// rather than dispatched.
bool playing();

}  // namespace gb_ui
}  // namespace tabulous
