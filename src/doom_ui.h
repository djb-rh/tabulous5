// The Doom screen: pick a WAD, then play it.
//
// The engine is doomgeneric (third_party/doomgeneric), the id source behind a
// platform seam of five functions. This file is those five functions, the
// screen around them, and the pad-to-keyboard translation. Same shape as
// gb_ui: the browser, the video path and the on-screen pad are shared.
//
// One thing is unlike the emulators: the engine cannot be started twice. It
// is a program, with globals set up once, so leaving a game keeps the engine
// alive and re-entering resumes it. Loading a different WAD, or recovering
// from an engine error, restarts the console.
#pragma once

#include <cstdint>

namespace tabulous {
namespace doom_ui {

constexpr const char *kFavouritesFile = "/doom_favs.txt";

void begin();
void rescan();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);
bool playing();
// Holds NES-style pad bits for `frames` ticks, for driving the game over
// serial; the same shape as nes_ui::injectPad.
void injectPad(uint8_t buttons, uint32_t frames);

}  // namespace doom_ui
}  // namespace tabulous
