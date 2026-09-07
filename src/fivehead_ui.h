// FiveHead's screens, and the tilt gestures that drive it.
#pragma once

#include <vector>

#include "content.h"
#include "fivehead.h"
#include "pack.h"

namespace tabulous {
namespace fivehead_ui {

void begin(fivehead::Game *game, std::vector<Pack> *packs,
           const content::LoadReport &report);

void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

}  // namespace fivehead_ui
}  // namespace tabulous
