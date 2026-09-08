#include "padmap.h"

#include "joypad.h"

namespace tabulous {
namespace padmap {
namespace {

bool held(uint32_t down, uint8_t button) {
  return button > 0 && button <= 32 && (down >> (button - 1)) & 1u;
}

}  // namespace

uint8_t toNes(uint32_t down, int8_t x, int8_t y, const Map &map) {
  uint8_t out = 0;
  if (held(down, map.a) || held(down, map.a2)) out |= joypad::kA;
  if (held(down, map.b) || held(down, map.b2)) out |= joypad::kB;
  if (held(down, map.select)) out |= joypad::kSelect;
  if (held(down, map.start)) out |= joypad::kStart;
  if (y < 0) out |= joypad::kUp;
  if (y > 0) out |= joypad::kDown;
  if (x < 0) out |= joypad::kLeft;
  if (x > 0) out |= joypad::kRight;
  return out;
}

uint8_t newPress(uint32_t down, uint32_t before) {
  const uint32_t fresh = down & ~before;
  if (!fresh) return 0;
  for (uint8_t n = 0; n < 32; n++) {
    if ((fresh >> n) & 1u) return (uint8_t)(n + 1);
  }
  return 0;
}

}  // namespace padmap
}  // namespace tabulous
