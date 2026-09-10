// A four-way joystick gate, in software.
//
// Pac-Man's cabinet had a four-way stick: a plate under the handle that makes
// two directions at once physically impossible. The program was written on top
// of that guarantee and does nothing sensible when it is broken -- and every
// modern control breaks it. A gamepad's cross, a thumbstick and four buttons
// on a touch screen all let two adjacent directions be held together, and the
// board reads whichever bit its code happens to test first. Turning a corner
// becomes a matter of releasing the old direction before pressing the new one,
// precisely, every time, which is not how anyone plays.
//
// This is the plate. It takes the raw four bits and hands back at most one
// horizontal and one vertical, favouring the direction that JUST changed --
// so holding left and adding up is a turn upward, immediately, which is what
// the player meant. The rule is MAME's, which has had it right for decades.
//
// Arduino-free so it can be tested on the host, where the interesting cases
// are sequences rather than single frames.
#pragma once

#include <cstdint>

namespace tabulous {
namespace fourway {

// Holds the state one stick needs: what it read last, and what it decided.
class Gate {
 public:
  // `mask` names the four bits, in joypad.h's order, so this does not have to
  // know which console it is filtering for.
  Gate(uint8_t up, uint8_t down, uint8_t left, uint8_t right)
      : up_(up), down_(down), left_(left), right_(right) {}

  // One frame. Bits outside the four are passed through untouched: the buttons
  // and the coin slot are none of this function's business.
  uint8_t filter(uint8_t pad);

  // Forget the held direction, for when a game starts or the screen changes.
  void reset() { previous_ = 0; decided_ = 0; }

 private:
  uint8_t up_, down_, left_, right_;
  uint8_t previous_ = 0;  // the four bits, as they were last frame
  uint8_t decided_ = 0;   // the one or two we handed on
};

}  // namespace fourway
}  // namespace tabulous
