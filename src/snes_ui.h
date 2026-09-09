// The SNES screen: pick a cartridge, then play it.
//
// Only built when the core has been fetched into third_party/snes9x — see
// that directory's README. Without it these are no-ops and the launcher has
// no SNES entry.
#pragma once

#include <cstdint>

namespace tabulous {
namespace snes_ui {

// Favourite cartridges, one file name per line, on the built-in filesystem.
constexpr const char *kFavouritesFile = "/snes_favs.txt";

// False when the core was not present at build time.
bool available();

// Claims the core's 128 KB of work RAM while the heap is still whole, at the
// very start of boot. By the time a cartridge is picked there is no internal
// block that big left, and the core silently falls back to PSRAM -- which
// costs about a third of the frame. Does nothing without the core.
void reserveWorkRamEarly();

void begin();
void rescan();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);
bool playing();

}  // namespace snes_ui
}  // namespace tabulous
