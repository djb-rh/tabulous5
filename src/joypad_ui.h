// The on-screen controller harness.
//
// Exists so the pad can be held and judged before an emulator or any hardware
// buttons exist: it draws the layout and lights each control while it is held.
//
// Unlike every other screen here it does NOT go through the shell's tap
// dispatch. Taps are press EDGES, and a controller needs continuous state and
// several contacts at once — holding left while tapping A is the normal case,
// not an edge case. So this polls the touch controller's full point list every
// tick instead.
#pragma once

#include <cstdint>

namespace tabulous {
namespace joypad_ui {

void begin();
void invalidate();
void tick(uint32_t now_ms);

// The live NES button bitfield, ready to hand to a core unchanged.
uint8_t state();

// Shared with the emulator screen, which draws the same pad around a picture
// instead of around a readout. `prev` is the state these controls were last
// drawn in; pass 0xFF to force every one of them to paint.
void drawControls(uint8_t state, uint8_t prev, bool full);

// Every current contact, OR-ed into one bitfield. `menu_held` reports whether
// any of them is on the MENU button.
uint8_t pollPad(bool *menu_held);

}  // namespace joypad_ui
}  // namespace tabulous
