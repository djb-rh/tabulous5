// The built-in gamepad's pins. See busmap.h for what it is.
//
// Every line is an input with the P4's own pull-up, so a console with no pad
// on its header reads twelve idle lines and nothing changes for it. The pad
// only counts once its wiring has been taught and saved.
#pragma once

#include <cstdint>

#include "busmap.h"

namespace tabulous {
namespace buspad {

// Sets the pins up and loads the saved wiring. Call once at boot.
void begin();
// A wiring has been taught: the pad is there and is player one.
bool wired();
// Bit n set while line n is held to ground. Reads the pins; cheap.
uint16_t lines();
// One bit per busmap::Button, through the wiring. Nothing until wired().
uint16_t buttons();
uint8_t nes();
uint8_t stick();

busmap::Wiring wiring();
// Saves it. A wiring with nothing in it un-wires the pad.
void setWiring(const busmap::Wiring &w);

}  // namespace buspad
}  // namespace tabulous
