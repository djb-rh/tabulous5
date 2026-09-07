// A shared score-table screen.
//
// Modal, like the keyboard and the pack picker: it takes over the display and
// the hit targets, so a game needs no screen of its own to show its tables.
// Tabs across the top switch difficulty.
#pragma once

#include <cstdint>

namespace tabulous {
namespace scoreboard {

enum class Result : uint8_t { None, Closed };

// `keys` are NVS keys, `labels` the difficulty names; both arrays must have
// `count` entries and outlive the modal. `highlight` marks a just-set entry.
void open(const char *title, const char *const *keys,
          const char *const *labels, int count, int initial_tab,
          bool lower_is_better, int highlight_row = -1);

bool active();
void tick(uint32_t now_ms);
Result handleTap(int x, int y);

}  // namespace scoreboard
}  // namespace tabulous
