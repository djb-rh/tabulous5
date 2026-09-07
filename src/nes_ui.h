// The NES screen: pick a ROM, then run it.
//
// Two modes in one module because they share the pad. The picker uses the
// shell's ordinary tap dispatch; play polls raw touch, for the same reason the
// harness does — a controller needs held state and several contacts at once.
#pragma once

#include <cstdint>

namespace tabulous {
namespace nes_ui {

void begin();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

// True once a ROM is running, so the shell knows taps are being polled rather
// than dispatched.
bool playing();

}  // namespace nes_ui
}  // namespace tabulous
