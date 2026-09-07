// Choosing which word packs a game draws from.
//
// A shared modal, like textentry: it owns the screen and the hit targets while
// open, so each game gets the picker without adding a screen of its own.
//
// The selection is a bitmask over the loaded pack list, one bit per index.
// Games keep their own mask, so a pack can be in PhraseCraze but not FiveHead.
#pragma once

#include <cstdint>
#include <vector>

#include "pack.h"

namespace tabulous {
namespace packpicker {

enum class Result : uint8_t { None, Closed };

void open(const char *title, const std::vector<Pack> *packs, uint32_t mask);
bool active();

// The chosen mask. Only meaningful after handleTap() reports Closed.
uint32_t mask();

void tick(uint32_t now_ms);
Result handleTap(int x, int y);

// Restricts `mask` to packs that actually exist. An empty result means "all":
// no pack selected would leave a game with no category to start a round with,
// which is a dead end with no way out from inside the game.
//
// Inline and free of Arduino headers so the rule is unit-tested on the host.
inline uint32_t sanitise(uint32_t mask, size_t pack_count) {
  if (pack_count == 0) return mask;
  uint32_t available = 0;
  for (size_t i = 0; i < pack_count && i < 32; i++) available |= (1u << i);
  const uint32_t effective = mask & available;
  return effective ? effective : available;
}

inline bool enabled(uint32_t mask, size_t index, size_t pack_count) {
  return (sanitise(mask, pack_count) & (1u << index)) != 0;
}

}  // namespace packpicker
}  // namespace tabulous
