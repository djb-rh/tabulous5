// Solitaire's table: the layout, card drawing and tap-to-move handling.
#pragma once

#include "solitaire.h"

namespace tabulous {
namespace solitaire_ui {

void begin(solitaire::Game *game);
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

}  // namespace solitaire_ui
}  // namespace tabulous
