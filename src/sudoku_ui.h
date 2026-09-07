// Sudoku's screen: the grid on the left, the number pad on the right.
#pragma once

#include "sudoku.h"

namespace tabulous {
namespace sudoku_ui {

void begin(sudoku::Puzzle *puzzle);
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

}  // namespace sudoku_ui
}  // namespace tabulous
