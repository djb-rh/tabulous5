// Auto-rotate using the BMI270.
//
// The device gets handed back and forth constantly, and whoever receives it
// may well take it the other way up. This keeps it landscape but flips 180°
// so the phrase is never upside-down for the person reading it.
//
// It deliberately does NOT rotate into portrait: the round screen is designed
// for a wide phrase, and a device in motion between two people passes through
// every orientation on the way.
#pragma once

#include <cstdint>

namespace tabulous {
namespace orientation {

void begin();

// Samples the accelerometer and applies a flip once the new orientation has
// been held steadily. Returns true if the display rotation changed, in which
// case the caller must repaint.
bool update(uint32_t now_ms);

void setEnabled(bool enabled);
bool enabled();

// "This is the right way up." Flips 180 deg immediately and calibrates from
// the current accelerometer reading, so auto-rotate agrees from then on.
// Persisted across reboots. This is the only way the mapping gets set — it
// cannot be known up front, and guessing it lands upside-down half the time.
void flipNow();
bool calibrated();

// Forget the stored calibration and return to the default rotation. The way
// out if calibration ends up somewhere unusable.
void resetCalibration();

// How long a new orientation must be held before the screen commits to it.
void setStableMs(uint32_t ms);
uint32_t stableMs();

uint8_t rotation();

// Raw in-plane gravity reading, for calibration logging.
float lastReading();

}  // namespace orientation
}  // namespace tabulous
