// Screen rendering and touch handling.
//
// The UI reads game state and calls into it; the rules never call back out
// here. Every screen is a full redraw into an off-screen canvas which is then
// pushed in one go, so changing phrase mid-round doesn't flash.
#pragma once

#include <vector>

#include "content.h"
#include "phrase_game.h"
#include "pack.h"

namespace tabulous {
namespace ui {

void begin(Game *game, std::vector<Pack> *packs,
           const content::LoadReport &report);

// Marks the screen dirty; the next tick() repaints.
void invalidate();

// Drives animation, the round timer and repaints. Call every loop.
void tick(uint32_t now_ms);

// A touch landed. Coordinates are display pixels.
void handleTap(int x, int y, uint32_t now_ms);

// Slowest tap-handler action seen, and which Action enum value it was.
uint32_t worstActionUs();
int worstAction();

}  // namespace ui
}  // namespace tabulous
