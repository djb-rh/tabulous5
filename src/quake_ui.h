// Quake, from your own pak files on the card.
//
// The engine is quakegeneric (third_party/quakegeneric), id's 1996 source
// behind a seven-function platform seam; this file is the seam, and the
// screen that starts it. See README.md, "Quake".
#pragma once

#include <cstdint>

namespace tabulous {
namespace quake_ui {

// False when this firmware was built without the engine (see platformio.ini:
// the big engines each get a build of their own, for the internal memory).
bool available();
void begin();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);
bool playing();
// True once after a restart the Quake screen asked for (a change of game):
// the launcher opens Quake straight away and it carries on from there.
bool resumePending();
// NES-style buttons for a number of frames, from the serial port: the way a
// host drives a game it cannot touch.
void injectPad(uint8_t buttons, uint32_t frames);
// A line for the game's own console, from the serial port ("timedemo demo1",
// "map e1m1"). Ignored unless the engine is running.
void console(const char *line);

}  // namespace quake_ui
}  // namespace tabulous
