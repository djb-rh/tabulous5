#pragma once

#include "phrase_game.h"

// The Tab5's power switches, through M5Unified: what the charger may ask an
// adapter for, whether the USB port puts out 5 V, and the real off.
namespace tabulous {
namespace power {

// Applies the two power settings. Also turns the M5-Bus 5 V rail off: nothing
// we plug into it draws power, and its converter otherwise runs all the time.
void apply(const Settings &s);

// Tells the power-management MCU to cut the rails: the same thing as a
// double-press of the power button. Holding the button is NOT this; it drops
// the chip into the download bootloader, screen dark, everything else on.
void off();

}  // namespace power
}  // namespace tabulous
