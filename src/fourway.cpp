#include "fourway.h"

namespace tabulous {
namespace fourway {

uint8_t Gate::filter(uint8_t pad) {
  const uint8_t vertical = (uint8_t)(up_ | down_);
  const uint8_t horizontal = (uint8_t)(left_ | right_);
  const uint8_t all = (uint8_t)(vertical | horizontal);

  uint8_t now = (uint8_t)(pad & all);

  // Opposite directions are one stick pulled two ways: the plate makes it
  // impossible and a pad makes it meaningless. Neither wins.
  if ((now & vertical) == vertical) now = (uint8_t)(now & ~vertical);
  if ((now & horizontal) == horizontal) now = (uint8_t)(now & ~horizontal);

  if (now != previous_) {
    decided_ = now;
    // A diagonal, and something changed to get here: keep only what changed.
    // Holding left and adding up leaves up alone, which is the turn asked for.
    if ((decided_ & vertical) && (decided_ & horizontal)) {
      decided_ = (uint8_t)(decided_ & ~(decided_ & previous_));
    }
    // Still a diagonal, so both arrived in the same frame and there is nothing
    // to tell them apart. Take the horizontal, arbitrarily but consistently.
    if ((decided_ & vertical) && (decided_ & horizontal)) {
      decided_ = (uint8_t)(decided_ & ~vertical);
    }
    previous_ = now;
  } else {
    // Nothing changed. Hold the decision, minus anything let go of.
    decided_ = (uint8_t)(decided_ & now);
  }

  return (uint8_t)((pad & ~all) | decided_);
}

}  // namespace fourway
}  // namespace tabulous
