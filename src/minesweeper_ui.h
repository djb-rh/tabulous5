// Minesweeper's screen: the grid, the header counters and the footer controls.
#pragma once

#include "minesweeper.h"

namespace tabulous {
namespace mines_ui {

void begin(mines::Board *board);
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

}  // namespace mines_ui
}  // namespace tabulous
