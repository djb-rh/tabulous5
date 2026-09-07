// A confetti burst for win screens.
//
// Drawn by erasing each piece's previous rectangle rather than repainting the
// screen. A full repaint on this panel costs ~45 ms, so a 2.5 s animation of
// them would block the loop for the entire celebration and eat the taps that
// follow it — the exact failure this project has chased down twice already.
// Forty pieces erased and redrawn individually cost about 3 ms a frame.
//
// The caller drives it in three steps so it can put static content back
// between the erase and the draw:
//
//   confetti::erase();      // paint the ground back over the old positions
//   drawTheStaticText();    // whatever the pieces were covering
//   confetti::step(now);    // advance the physics
//   confetti::draw();       // pieces land on top
//
// None of these touch hit targets, so they must not be called from a full
// repaint path — same rule as any other partial redraw here.
#pragma once

#include <cstdint>

namespace tabulous {
namespace confetti {

// Bursts from the top of the screen. Pieces are kept above `floor_y` so they
// never fall across a button and punch holes in it while erasing.
void start(uint32_t now_ms, int floor_y);

bool active(uint32_t now_ms);
void stop();

void erase();
void step(uint32_t now_ms);
void draw();

}  // namespace confetti
}  // namespace tabulous
