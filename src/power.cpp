#include "power.h"

#include <M5Unified.h>
#include "hal/usb_serial_jtag_ll.h"
#include "usbpad.h"

namespace tabulous {
namespace power {

namespace {
uint8_t g_pad_power = 1;     // the setting; the policy below decides the rest
bool g_usb_on = false;
bool g_data_auto = true;
bool g_data_on = true;       // the pad is on out of reset
bool g_host_seen = false;    // decided once: a host heard early keeps the lines on for good
constexpr uint32_t kHostGraceMs = 12000;   // how long after boot a host gets to show up
}  // namespace

void apply(const Settings &s) {
  // M5-Bus 5 V: off. ext_PA is the bus on the Tab5.
  M5.Power.setExtOutput(false, m5::ext_PA);
  g_pad_power = s.pad_power;
  g_data_auto = s.usb_data_auto;
  // Not setUsbOutput(): that has no Tab5 case and does nothing here.
  M5.Power.setExtOutput(false, m5::ext_USB);
  g_usb_on = false;
  // 1000 = charge with the Quick Charge handshake enabled, 500 = without.
  M5.Power.setChargeCurrent(s.fast_charge ? 1000 : 500);
}

void setPlaying(bool playing) {
  // The rail feeds the pad, and the USB-C VBUS with it. Discharging reads
  // clearly negative on battery; with a charger attached it sits near zero
  // or positive, and then the rail stays off in mode 1 so the charger lives.
  const bool on_battery = M5.Power.getBatteryCurrent() < -20;
  const bool want = playing && (g_pad_power == 2 || (g_pad_power == 1 && on_battery));
  if (want != g_usb_on) {
    g_usb_on = want;
    M5.Power.setExtOutput(want, m5::ext_USB);
    if (want) delay(50);
  }
  usbpad::portPower(playing);
  Serial.printf("power: playing=%d usbc_5v=%d\n", (int)playing, (int)g_usb_on);
}

void forceUsbRail(bool on) {   // serial diagnostics only
  g_usb_on = on;
  M5.Power.setExtOutput(on, m5::ext_USB);
}

void tick(uint32_t now_ms) {
  if (!g_data_auto || !g_data_on || g_host_seen) return;
  if (Serial.isPlugged()) { g_host_seen = true; return; }   // a computer: keep the lines up, even through a suspend
  if (now_ms < kHostGraceMs) return;
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
