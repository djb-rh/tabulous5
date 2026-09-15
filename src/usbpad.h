// A USB gamepad on the Tab5's USB-A port.
//
// The P4's USB host stack is in the core, but no HID class driver is, so this
// is a small one of its own: enough to enumerate a HID device, read its report
// descriptor, and turn its interrupt reports into "which buttons are down".
// Generic pads (the SNES-shaped GP100, DragonRise clones, most no-name ones)
// all describe themselves the same way — X/Y axes or a hat for the D-pad and a
// row of numbered buttons — so a descriptor parser that understands just those
// covers nearly everything without a table of vendor IDs.
//
// Which numbered button is "A" differs per pad, which is what the mapping in
// padmap.h is for. This layer reports raw button numbers.
#pragma once

#include <cstdint>

namespace tabulous {
namespace usbpad {

constexpr int kMaxButtons = 32;

struct State {
  bool connected = false;
  uint16_t vid = 0, pid = 0;
  char name[48] = "";
  int buttons = 0;         // how many the descriptor declares
  uint32_t down = 0;       // bit n = button n+1 held
  int8_t x = 0, y = 0;     // -1, 0, +1 from the axes or hat
  uint32_t reports = 0;    // total received, so a dead pad can be told apart
};

// Starts the host stack. Safe to call more than once. Returns false if the
// host stack would not install. Not called at boot: the stack and its tasks
// take internal RAM the Wi-Fi transport also needs, so it is started by the
// first game that powers the port.
bool begin();
// Power the host stack's root port up or down; a game start powers it up,
// starting the stack first if it has not been.
void portPower(bool on);
// A snapshot of the pad right now. Cheap; call it every frame.
State state();
// How many devices the host library currently sees (enumerated or not); -1
// if the stack is not running. For diagnosing a port that stays silent.
int deviceCount();
// The raw bytes of the most recent report, for the Gamepad Test readout.
int lastReport(uint8_t *out, int max);

}  // namespace usbpad
}  // namespace tabulous
