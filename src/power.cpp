#include "power.h"

#include <M5Unified.h>
#include "hal/usb_serial_jtag_ll.h"

namespace tabulous {
namespace power {

namespace {
bool g_usb_allowed = true;   // the setting; the policy below decides the rest
bool g_usb_on = false;
bool g_data_auto = true;
bool g_data_on = true;       // the pad is on out of reset
uint32_t g_last_host_ms = 0;
constexpr uint32_t kHostGraceMs = 12000;   // after boot, and after a host goes quiet
}  // namespace

void apply(const Settings &s) {
  // M5-Bus 5 V: off. ext_PA is the bus on the Tab5.
  M5.Power.setExtOutput(false, m5::ext_PA);
  g_usb_allowed = s.usb_power;
  g_data_auto = s.usb_data_auto;
  M5.Power.setUsbOutput(false);
  g_usb_on = false;
  // 1000 = charge with the Quick Charge handshake enabled, 500 = without.
  M5.Power.setChargeCurrent(s.fast_charge ? 1000 : 500);
}

void setPlaying(bool playing) {
  // Discharging reads clearly negative on battery; on external power it sits
  // near zero or positive. The margin keeps a flat idle from counting.
  const bool on_battery = M5.Power.getBatteryCurrent() < -20;
  const bool want = g_usb_allowed && playing && on_battery;
  if (want == g_usb_on) return;
  g_usb_on = want;
  M5.Power.setUsbOutput(want);
}

void tick(uint32_t now_ms) {
  if (!g_data_auto || !g_data_on) return;
  if (Serial.isPlugged()) g_last_host_ms = now_ms;
  if (now_ms - g_last_host_ms < kHostGraceMs) return;
  Serial.println("power: no USB host; USB-C data off until reset");
  Serial.flush();
  usb_serial_jtag_ll_phy_enable_pad(false);
  g_data_on = false;
}

bool usbDataOn() { return g_data_on; }

void off() {
  M5.Display.sleep();
  M5.Power.powerOff();
}

}  // namespace power
}  // namespace tabulous
