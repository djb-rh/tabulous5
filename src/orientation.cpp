#include "orientation.h"

#include <M5Unified.h>
#include <Preferences.h>

#include <cmath>

namespace tabulous {
namespace orientation {
namespace {

// The two landscape orientations, 180° apart.
constexpr uint8_t kLandscapeA = 1;
constexpr uint8_t kLandscapeB = 3;

// Below this the device is lying flat and the in-plane reading is noise.
constexpr float kDecisiveG = 0.35f;

// How long a new orientation must be held before committing. User-settable:
// too short and it spins while the device is mid-air between two people, too
// long and it feels sluggish.
uint32_t g_stable_ms = 250;
// The ST7123 touch controller (0x55) sits on this same internal I2C bus, and
// M5GFX's touch driver and M5Unified's IMU driver do not lock against each
// other. A collision loses that transaction — which presents as a tap simply
// not registering. So sample sparingly, and never while a finger is down.
constexpr uint32_t kSampleMs = 400;

// Bumped when the stored calibration format or defaults change, so a flash
// that alters the meaning of the stored values starts clean instead of
// inheriting a calibration that no longer means what it did.
constexpr uint8_t kCalibrationVersion = 3;  // 3: the default way up flipped

// Never flip more often than this, whatever the speed setting says. Without
// it, "instant" lets noise either side of the threshold flip the screen
// repeatedly, which repaints continuously and makes the UI feel dead.
constexpr uint32_t kMinFlipIntervalMs = 500;
uint32_t g_last_flip_ms = 0;

// Flipping needs a clearly-committed tilt; holding position needs less. The
// gap between the two is what stops it dithering at the boundary.
constexpr float kFlipG = 0.50f;

bool g_enabled = true;
// The default way up is B: the way the buttons and the card slot end up on
// the right, which is how it is held.
uint8_t g_rotation = kLandscapeB;
float g_last = 0.0f;

uint32_t g_last_sample = 0;
uint8_t g_candidate = kLandscapeB;
uint32_t g_candidate_since = 0;

// Latest raw sample, so flipNow() can calibrate from whatever is happening at
// the moment the user says "this is the right way up".
float g_ax = 0, g_ay = 0;

// Calibration.
//
// Which accelerometer axis lies along the display's vertical in landscape, and
// which sign of it means g_ref_rotation, is NOT knowable up front: M5Unified
// doesn't promise the IMU is mounted in the display's frame. Guessing it is a
// coin flip, and an earlier attempt to infer it automatically only made a
// wrong guess sticky. So it is set by the user tapping FLIP, and persisted.
bool g_calibrated = false;
bool g_axis_is_y = false;
float g_ref_sign = 1.0f;
uint8_t g_ref_rotation = kLandscapeB;

// The orientation to sit in when lying flat, where there is no in-plane
// gravity to go on. Also user-set via FLIP, and persisted.
uint8_t g_resting = kLandscapeB;

Preferences g_prefs;

uint8_t opposite(uint8_t rotation) {
  return rotation == kLandscapeA ? kLandscapeB : kLandscapeA;
}

void save() {
  if (!g_prefs.begin("phrasecraze", false)) return;
  g_prefs.putUChar("o_ver", kCalibrationVersion);
  g_prefs.putBool("o_cal", g_calibrated);
  g_prefs.putBool("o_axisy", g_axis_is_y);
  g_prefs.putChar("o_sign", g_ref_sign > 0 ? 1 : -1);
  g_prefs.putUChar("o_refrot", g_ref_rotation);
  g_prefs.putUChar("o_rest", g_resting);
  g_prefs.end();
}

void load() {
  if (!g_prefs.begin("phrasecraze", true)) return;
  if (g_prefs.getUChar("o_ver", 0) != kCalibrationVersion) {
    g_prefs.end();
    return;  // stale or absent — keep the defaults, uncalibrated
  }
  g_calibrated = g_prefs.getBool("o_cal", false);
  g_axis_is_y = g_prefs.getBool("o_axisy", false);
  g_ref_sign = g_prefs.getChar("o_sign", 1) >= 0 ? 1.0f : -1.0f;
  g_ref_rotation = g_prefs.getUChar("o_refrot", kLandscapeB);
  g_resting = g_prefs.getUChar("o_rest", kLandscapeB);
  g_prefs.end();
}

}  // namespace

void begin() {
  load();
  g_rotation = g_resting;
  M5.Display.setRotation(g_rotation);
  g_candidate = g_rotation;
  g_candidate_since = 0;
}

void setEnabled(bool enabled) {
  g_enabled = enabled;
  if (!enabled) g_candidate_since = 0;
}

bool enabled() { return g_enabled; }

uint8_t rotation() { return g_rotation; }

float lastReading() { return g_last; }

bool calibrated() { return g_calibrated; }

void setStableMs(uint32_t ms) { g_stable_ms = ms; }
uint32_t stableMs() { return g_stable_ms; }

void resetCalibration() {
  g_calibrated = false;
  g_resting = kLandscapeB;
  g_ref_rotation = kLandscapeB;
  g_ref_sign = 1.0f;
  g_axis_is_y = false;
  g_rotation = kLandscapeB;
  M5.Display.setRotation(g_rotation);
  save();
}

void flipNow() {
  g_rotation = opposite(g_rotation);
  M5.Display.setRotation(g_rotation);
  g_candidate = g_rotation;
  g_candidate_since = 0;

  const float mag_x = fabsf(g_ax), mag_y = fabsf(g_ay);
  if (mag_x >= kDecisiveG || mag_y >= kDecisiveG) {
    // Being held: learn which way up "this reading" means, so auto-rotate
    // agrees with the user from now on.
    g_axis_is_y = mag_y > mag_x;
    const float v = g_axis_is_y ? g_ay : g_ax;
    g_ref_sign = v > 0 ? 1.0f : -1.0f;
    g_ref_rotation = g_rotation;
    g_calibrated = true;
  } else {
    // Lying flat: there is nothing to calibrate against, but the user has told
    // us which way up it should sit at rest, which is the other half of it.
    g_resting = g_rotation;
  }
  save();
}

bool update(uint32_t now_ms) {
  if (!M5.Imu.isEnabled()) return false;
  if (now_ms - g_last_sample < kSampleMs) return false;
  // Yield the bus to the touch controller whenever a touch is in progress.
  // Orientation can wait 400 ms; a dropped tap cannot be recovered.
  if (M5.Touch.getCount() > 0) return false;
  g_last_sample = now_ms;

  float az = 0;
  if (!M5.Imu.getAccel(&g_ax, &g_ay, &az)) return false;

  // Sampling continues even when auto-rotate is off, so that FLIP can still
  // calibrate from a live reading.
  if (!g_enabled || !g_calibrated) return false;

  const float v = g_axis_is_y ? g_ay : g_ax;
  g_last = v;

  if (fabsf(v) < kDecisiveG) {
    // Flat, or tipped towards portrait. Hold station rather than guessing.
    g_candidate_since = 0;
    return false;
  }

  // Hysteresis: only a decisive tilt may change the orientation.
  if (fabsf(v) < kFlipG) {
    g_candidate_since = 0;
    return false;
  }

  const uint8_t desired =
      (v * g_ref_sign > 0) ? g_ref_rotation : opposite(g_ref_rotation);

  if (desired == g_rotation) {
    g_candidate = g_rotation;
    g_candidate_since = 0;
    return false;
  }
  if (desired != g_candidate) {
    g_candidate = desired;
    g_candidate_since = now_ms;
    return false;
  }
  if (g_candidate_since == 0) g_candidate_since = now_ms;
  if (now_ms - g_candidate_since < g_stable_ms) return false;

  if (now_ms - g_last_flip_ms < kMinFlipIntervalMs) return false;

  g_rotation = desired;
  g_candidate_since = 0;
  g_last_flip_ms = now_ms;
  M5.Display.setRotation(g_rotation);
  return true;
}

}  // namespace orientation
}  // namespace tabulous
