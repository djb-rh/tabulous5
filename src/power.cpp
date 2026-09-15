#include "power.h"

#include <M5Unified.h>

namespace tabulous {
namespace power {

void apply(const Settings &s) {
  // M5-Bus 5 V: off. ext_PA is the bus on the Tab5.
  M5.Power.setExtOutput(false, m5::ext_PA);
  // The USB port's 5 V out, for a gamepad on the USB-A jack.
  M5.Power.setUsbOutput(s.usb_power);
  // 1000 = charge with the Quick Charge handshake enabled, 500 = without.
  M5.Power.setChargeCurrent(s.fast_charge ? 1000 : 500);
}

void off() {
  M5.Display.sleep();
  M5.Power.powerOff();
}

}  // namespace power
}  // namespace tabulous
