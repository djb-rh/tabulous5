#pragma once

#include "phrase_game.h"

// The Tab5's power switches, through M5Unified: what the charger may ask an
// adapter for, whether the USB port puts out 5 V, and the real off.
namespace tabulous {
namespace power {

// Applies the power settings. Also turns the M5-Bus 5 V rail off: nothing we
// plug into it draws power, and its converter otherwise runs all the time.
void apply(const Settings &s);

// The USB 5 V output feeds the USB-A jack, which a gamepad needs, but on the
// Tab5 the same rail also drives the USB-C port's VBUS: a wall brick meeting
// it trips its protection and latches off (measured). So the rail is on only
// while a game is running AND the console is on battery, and off in the menu,
// which is where a charger should be plugged in. Call on every change of
// what is running; it reads the battery monitor once.
void setPlaying(bool playing);

// The USB-C data lines. The chip's USB device pulls D+ up the moment it
// boots; a wall brick that reads D+/D- as a Quick Charge request answers
// by shutting itself off (measured: the brick survives with the chip off).
// So once no computer has been heard from for a while, the USB pad is
// switched off, and stays off until the next reset. Call every loop.
void tick(uint32_t now_ms);
bool usbDataOn();

// Tells the power-management MCU to cut the rails: the same thing as a
// double-press of the power button. Holding the button is NOT this; it drops
// the chip into the download bootloader, screen dark, everything else on.
void off();

}  // namespace power
}  // namespace tabulous
