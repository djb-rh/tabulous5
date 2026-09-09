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

// The chrome for the sizes that draw no on-screen pad, where the picture takes
// the whole height and a USB gamepad is the only way to play: the way out, and
// which pad is going to do the playing. Without the second part an unplugged
// gamepad looks exactly like a game that has frozen -- a picture that moves
// and nothing that answers. Call it every frame; it only paints on a change.
void drawGamepadChrome(bool full);

// What the buttons are called. A cabinet has a coin slot, not a select
// button, and no A or B. Passing nothing puts a name back to its default.
void setLabels(const char *select_label = nullptr, const char *start_label = nullptr,
               const char *a_label = nullptr, const char *b_label = nullptr);

// Turns the screen for play and places the picture and controls on it. Upright
// the game sits at the top with everything else underneath, which is how a
// cabinet stands and how this sits in a controller mount; sideways it is
// centred with the pad either side, held in two hands. False if the picture
// would not fit.
bool beginPlay(bool portrait, int src_w, int src_h, float scale);

// Puts the screen back the way the rest of the console expects it.
void endPlay();

// Every current contact, OR-ed into one bitfield. `menu_held` reports whether
// any of them is on the MENU button.
uint8_t pollPad(bool *menu_held);

}  // namespace joypad_ui
}  // namespace tabulous
