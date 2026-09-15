#include "battery.h"

#include <M5Unified.h>

namespace tabulous {
namespace battery {
namespace {

// Slow enough that the bus is essentially idle; a battery percentage does not
// need to be fresher than this.
constexpr uint32_t kIntervalMs = 30000;
// The charge flag should follow the cable within a few seconds, so it has
// its own faster clock. It comes from the battery monitor's current, not the
// charger's status pin: on the Tab5 that pin reads high whether the charger
// is pushing half an amp into the cell or is switched off (measured).
constexpr uint32_t kChargeIntervalMs = 5000;
constexpr int32_t kChargingMa = 50;   // into the battery; idle on USB reads about 0
bool chargingNow() { return M5.Power.getBatteryCurrent() > kChargingMa; }

// A read should be a couple of I2C transactions. Anything near this means the
// bus is congested or the part is not answering, and it is not worth risking
// the touch controller for a percentage.
constexpr uint32_t kTooSlowUs = 40000;

bool g_have = false;
int g_attempts = 0;
bool g_disabled = false;
int g_level = -1;
bool g_charging = false;
uint32_t g_last_read = 0;
uint32_t g_last_charge_read = 0;
uint32_t g_last_us = 0;

}  // namespace

void update(uint32_t now_ms, bool allowed) {
  if (g_disabled || !allowed) return;
  // Retry briskly at first so a battery inserted while the menu is up shows
  // up soon, but back off rather than polling forever on a device that simply
  // has no battery fitted.
  const uint32_t wait = g_have ? kIntervalMs : (g_attempts < 8 ? 1500 : kIntervalMs);
  if (now_ms - g_last_read < wait) {
    if (g_have && now_ms - g_last_charge_read >= kChargeIntervalMs) {
      g_last_charge_read = now_ms;
      g_charging = chargingNow();
    }
    return;
  }
  g_last_charge_read = now_ms;
  g_last_read = now_ms;
  g_attempts++;

  const uint32_t t0 = micros();
  const int32_t lv = M5.Power.getBatteryLevel();
  const int16_t mv = M5.Power.getBatteryVoltage();
  const bool chg = chargingNow();
  g_last_us = micros() - t0;

  if (g_last_us > kTooSlowUs) {
    // Stop for good rather than repeat something that stalls the loop.
    g_disabled = true;
    return;
  }

  // Voltage is the test for "is a battery actually fitted": with none in the
  // slot the level reads a perfectly plausible 0%, which would show as a flat
  // battery rather than as no battery.
  if (mv > 1000 && lv >= 0 && lv <= 100) {
    g_level = (int)lv;
    g_have = true;
  } else {
    g_have = false;
  }
  g_charging = chg;
}

bool available() { return g_have; }
int level() { return g_level; }
bool charging() { return g_charging; }
bool disabled() { return g_disabled; }
uint32_t lastReadUs() { return g_last_us; }

}  // namespace battery
}  // namespace tabulous
