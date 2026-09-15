// Joshua's screens: the WOPR terminal, the film's board, and the crash.
//
// A port of ~/Developer/Joshua (ESPHome + LVGL 9, for the CrowPanel) to the
// console's direct-draw style. The engine, tictactoe.h, is shared verbatim;
// everything visual is redrawn here with primitives and the same timing knobs.
#pragma once

#include <cstdint>

namespace tabulous {
namespace joshua_ui {

void begin();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

}  // namespace joshua_ui
}  // namespace tabulous
