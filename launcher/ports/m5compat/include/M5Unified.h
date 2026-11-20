// What Tabulous5's screens use of M5Unified, on the P4 Launcher's SDK.
//
// The screens draw through LovyanGFX (uikit::gfx() is M5.Display seen as one) and read the finger from
// M5.Touch. Here M5.Display is a LovyanGFX sprite whose pixels are the launcher's surface, and M5.Touch is the
// surface's touch. Build with -D__linux__ -DLGFX_LINUX_FB so LovyanGFX takes its plain platform; its clock and
// the Arduino odds and ends are in m5compat.cpp.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "lgfx/v1/platforms/common.hpp"
#include "lgfx/v1/LGFXBase.hpp"
#include "lgfx/v1/LGFX_Sprite.hpp"
#include "lgfx/v1/lgfx_fonts.hpp"

namespace m5 {
struct TouchDetail {
  int16_t x = 0, y = 0;
  bool pressed = false;
  bool isPressed() const { return pressed; }
};
struct Touch_Class {
  TouchDetail detail;
  uint8_t count = 0;
  const TouchDetail &getDetail(int = 0) const { return detail; }
  uint8_t getCount() const { return count; }
};
// The speaker as audio.cpp uses it: tones and raw 16-bit samples, several at once, mixed into one stream.
struct Speaker_Class {
  bool begin();
  void setAllChannelVolume(uint8_t) {}
  void setVolume(uint8_t v);
  void stop();
  bool tone(float hz, uint32_t ms, int channel = -1, bool stop_current = true);
  bool playRaw(const int16_t *pcm, size_t count, uint32_t rate, bool stereo = false, uint32_t repeat = 1,
               int channel = -1, bool stop_current = false);
};
// The accelerometer, in g. A launcher that offers it does so as three board analog inputs (see m5compat.cpp);
// one that does not leaves isEnabled() false and the games fall back to their buttons.
struct IMU_Class {
  bool isEnabled();
  bool getAccel(float *x, float *y, float *z);
};
struct M5Unified {
  lgfx::LGFX_Sprite Display;
  Touch_Class Touch;
  Speaker_Class Speaker;
  IMU_Class Imu;
};
}  // namespace m5
extern m5::M5Unified M5;
using M5Canvas = lgfx::LGFX_Sprite;

#ifndef constrain
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
uint32_t esp_random();
struct SerialShim {
  void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void println(const char *s = "");
  void print(const char *s);
};
extern SerialShim Serial;

namespace m5compat {
constexpr int kW = 1280, kH = 720;
// Makes the surface and points M5.Display at it. False: no display.
bool begin();
// Once a pass of the app's loop: reads the finger into M5.Touch, shows the picture if it has changed, and says
// whether the launcher wants the app to leave.
bool update();
void end();
uint32_t presents();   // frames shown so far
void profile(int64_t out[3]);   // microseconds since the last call spent reading the finger, summing the picture, and showing or waiting
void repaint();        // show the picture on the next pass even if it has not changed (after a launcher overlay)
// Board analog inputs a Tab5 launcher with the accelerometer patch answers on: 0..65535 with 32768 = 0 g and
// 8192 counts to the g. -1 from a launcher without it.
constexpr int kAccelX = 16, kAccelY = 17, kAccelZ = 18;
}  // namespace m5compat
