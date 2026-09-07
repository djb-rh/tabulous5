// A shared on-screen keyboard.
//
// Modal rather than a screen: it takes over the display and the hit targets
// while open, so a game doesn't need an entry in its own Screen enum to use
// it. Lifted out of PhraseCraze when FiveHead needed team names too.
//
// Host pattern:
//   if (textentry::active()) {
//     if (textentry::handleTap(x, y) != textentry::Result::None) {
//       if (textentry::accepted()) applyName(textentry::text());
//       invalidate();          // rebuild the host's own targets
//     }
//     return;
//   }
#pragma once

#include <cstddef>
#include <string>

namespace tabulous {
namespace textentry {

enum class Result : uint8_t { None, Closed };

void open(const char *title, const std::string &initial, size_t max_len);
bool active();

// True when the editor closed via DONE rather than CANCEL. Only meaningful
// immediately after handleTap() reports Closed.
bool accepted();

// Trimmed result. Never empty on acceptance — an empty name reads as a
// rendering bug rather than a choice, so DONE with only whitespace is refused.
const std::string &text();

void invalidate();
void tick(uint32_t now_ms);
Result handleTap(int x, int y);

}  // namespace textentry
}  // namespace tabulous
