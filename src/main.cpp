// Tabulous5 — a party-game console for the M5Stack Tab5.
//
// Pick a category, describe the phrase without saying it, tap to pass. A hidden
// timer beeps faster and faster until it buzzes; whoever is holding the device
// when it does gives a point away. First team to the target score wins.
//
// This file is only wiring: hardware bring-up, then a loop that feeds touches
// and time into the UI. The rules live in game.cpp (unit-tested on the host),
// the drawing in ui.cpp.

#include <M5Unified.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <esp_heap_caps.h>

#include <vector>

#include "audio.h"
#include "battery.h"
#include "content.h"
#include "contentserver.h"
#include "app.h"
#include "nes_ui.h"
#include "phrase_game.h"
#include "orientation.h"
#include "sdcard.h"
#include "pack.h"
#include "settings_store.h"
#include "theme.h"
#include "theme.h"
#include "uikit.h"
#include "phrase_ui.h"

using namespace tabulous;

namespace {

std::vector<Pack> g_packs;

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.output_power = true;
  M5.begin(cfg);

  Serial.begin(115200);
  // Serial here is the ESP32-P4's native USB-serial-JTAG. If nothing is
  // attached reading it, its TX buffer fills and every write then BLOCKS
  // until it times out — about two seconds. That stalls the loop, so touches
  // are not read, and it presents as an intermittent dead touchscreen with no
  // apparent cause. Worse, attaching a serial monitor drains the buffer and
  // makes the symptom vanish, so it hides from exactly the tool used to look
  // for it. Zero means: write if the host is listening, otherwise drop it.
  Serial.setTxTimeoutMs(0);

  // Theme before anything draws, so the first frame is already correct.
  theme::setLight(settings_store::loadLightTheme());
  M5.Display.setBrightness(200);
  orientation::begin();  // sets the resting landscape rotation (1280x720)
  M5.Display.fillScreen(theme::kBg);

  Settings settings;
  settings_store::load(&settings);
  audio::begin(settings.volume);
  audio::setEnabled(settings.sound_enabled);
  orientation::setEnabled(settings.auto_rotate);
  orientation::setStableMs(settings.flip_delay_ms);

  Serial.printf("cpu=%uMHz psram=%uKB internal=%uKB\n",
                (unsigned)getCpuFrequencyMhz(),
                (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  Serial.printf("imu=%d speaker=%d rot=%u calibrated=%d\n",
                (int)M5.Imu.isEnabled(), (int)M5.Speaker.isEnabled(),
                (unsigned)orientation::rotation(),
                (int)orientation::calibrated());

  const content::LoadReport report = content::loadAll(&g_packs);
  Serial.printf("packs=%u phrases=%u builtin=%d skipped=%u fs=%uKB/%uKB\n",
                (unsigned)report.packs_loaded, (unsigned)report.phrases_total,
                (int)report.used_builtin, (unsigned)report.files_skipped,
                (unsigned)(report.bytes_used / 1024),
                (unsigned)(report.bytes_total / 1024));

  // After content::loadAll, which is what mounts LittleFS. Missing files are
  // not an error — each effect falls back to its synthesised form — so this
  // only reports, and a 0 here means the filesystem image was never flashed.
  audio::loadSamples();
  Serial.printf("sfx=%d/7\n", audio::sampleCount());

  // The card is optional: without one the NES has its built-in ROMs and
  // nothing else changes.
  if (sdcard::begin()) {
    Serial.printf("sd=%lluMB %s\n", (unsigned long long)sdcard::cardMB(),
                  sdcard::busWidth());
  } else {
    Serial.println("sd=none");
  }

  // Registering roots here, not inside the server, is what keeps the editor
  // generic: a future game adds its own line and gets an editor for free.
  contentserver::addRoot("/packs", "Word packs", ".txt");

  app::begin(&g_packs, report);

  Serial.printf("PSRAM free %u KB\n", (unsigned)(ESP.getFreePsram() / 1024));
}


// Dump the panel to the host as raw RGB565, so the UI can be looked at instead
// of guessed at. Triggered by sending 's' over serial; see tools/screenshot.py.
//
// This exists because every visual bug in this project so far has been
// diagnosed by describing it over a chat window. The panel's framebuffer is
// readable, so it never needed to be.
// One-shot SD probe: which pins the board reports, whether a card is present,
// and — the part most likely to bite on a big modern card — whether the
// filesystem actually mounts. Cards over 32 GB ship exFAT, and Arduino's
// FATFS is usually built without exFAT support.
void probeSd() {
  Serial.setTxTimeoutMs(200);
  if (!sdcard::begin()) {
    Serial.println("SD: no card mounted (no card, or not FAT32?)");
    Serial.setTxTimeoutMs(0);
    return;
  }
  Serial.printf("SD: mounted, %s, size=%lluMB\n", sdcard::busWidth(),
                (unsigned long long)sdcard::cardMB());
  File root = sdcard::fs().open("/");
  int n = 0;
  for (File f = root.openNextFile(); f && n < 8; f = root.openNextFile(), n++) {
    Serial.printf("  %-40s %s\n", f.name(), f.isDirectory() ? "<dir>" : "");
  }
  Serial.printf("SD: %d entries listed at the root\n", n);
  Serial.setTxTimeoutMs(0);
}

void dumpCanvas(M5Canvas *canvas);

// Dump what is ACTUALLY on the panel, by reading its framebuffer.
//
// The canvas capture re-renders through uikit::gfx(), so it cannot see
// anything written straight to the framebuffer — which is how the emulator
// draws. This reads the real thing, un-rotating as it goes, and is the only
// way to check that mapping is right rather than merely plausible.
void dumpPanel() {
  Serial.setTxTimeoutMs(200);
  auto *panel = (lgfx::Panel_DSI *)M5.Display.getPanel();
  const uint8_t *fb = (const uint8_t *)panel->config_detail().buffer;
  const size_t stride = ((size_t)panel->config().panel_width * 2 + 3) & ~(size_t)3;
  const uint8_t rot = (uint8_t)M5.Display.getRotation();
  if (!fb) {
    Serial.println("SHOTFAIL no framebuffer");
    Serial.setTxTimeoutMs(0);
    return;
  }

  Serial.printf("SHOT %d %d\n", theme::kW, theme::kH);
  static uint16_t row[theme::kW];
  for (int y = 0; y < theme::kH; y++) {
    for (int x = 0; x < theme::kW; x++) {
      // Same mapping as Panel_FrameBufferBase::drawPixelPreclipped.
      size_t prow, pcol;
      if (rot == 1) {
        prow = (size_t)x;
        pcol = (size_t)(theme::kH - 1 - y);
      } else {
        prow = (size_t)(theme::kW - 1 - x);
        pcol = (size_t)y;
      }
      row[x] = *(const uint16_t *)(fb + prow * stride + pcol * 2);
    }
    Serial.write((const uint8_t *)row, sizeof(row));
  }
  Serial.flush();
  Serial.println("ENDSHOT");
  Serial.setTxTimeoutMs(0);
}

// Renders every ladder step so a new font can be checked without playing a
// round to reach the phrase screen. The bitmap repacking in
// tools/fontconvert.c is the part most likely to be subtly wrong — FreeType
// pads rows to byte boundaries and the GFX format does not — and the failure
// mode is a progressive shear down each glyph, which is obvious on sight and
// invisible in a build log.
void dumpFontSpecimen() {
  Serial.setTxTimeoutMs(200);
  M5Canvas canvas(&M5.Display);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(theme::kW, theme::kH)) {
    Serial.println("SHOTFAIL no memory");
    Serial.setTxTimeoutMs(0);
    return;
  }
  canvas.fillScreen(theme::kBg);
  canvas.setTextColor(theme::kText);
  int y = 10;
  for (size_t i = 0; i < theme::kPhraseLadderLen; i++) {
    canvas.setFont(theme::kPhraseLadder[i].font);
    canvas.setTextSize(theme::kPhraseLadder[i].size);
    canvas.setTextDatum(top_left);
    canvas.drawString("Handgloves 0123", 16, y);
    y += canvas.fontHeight() + 6;
  }
  canvas.setFont(&fonts::FreeSans12pt7b);
  canvas.drawString("gjpqy ,.!?-'&  THE QUICK BROWN FOX", 16, y + 4);
  dumpCanvas(&canvas);
  canvas.deleteSprite();
  Serial.setTxTimeoutMs(0);
}

void dumpCanvas(M5Canvas *canvas) {
  const uint16_t *pixels = (const uint16_t *)canvas->getBuffer();
  if (!pixels) {
    Serial.println("SHOTFAIL no buffer");
    return;
  }
  Serial.printf("SHOT %d %d\n", theme::kW, theme::kH);
  for (int y = 0; y < theme::kH; y++) {
    Serial.write((const uint8_t *)(pixels + (size_t)y * theme::kW),
                 theme::kW * 2);
  }
  Serial.flush();
  Serial.println("ENDSHOT");
}

void dumpScreen() {
  // The panel cannot be read back: M5GFX's readRect and readRectRGB both
  // return a constant on this DSI panel whatever is on screen. So the screen
  // is re-rendered into an off-screen canvas, which IS readable, and that is
  // what gets sent. Every screen draws through uikit::gfx(), so pointing that
  // at the canvas captures whichever one is up without any screen knowing.
  Serial.setTxTimeoutMs(200);

  M5Canvas canvas(&M5.Display);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(theme::kW, theme::kH)) {
    Serial.println("SHOTFAIL no memory");
    Serial.setTxTimeoutMs(0);
    return;
  }

  uikit::setSurface(&canvas);
  app::invalidate();
  app::tick(millis());
  uikit::setSurface(nullptr);

  dumpCanvas(&canvas);
  canvas.deleteSprite();
  // Put the real screen back: the capture repaint went to the canvas, so the
  // panel still holds whatever was there before, but the hit targets were
  // rebuilt against the canvas and must be rebuilt against the panel.
  app::invalidate();
  Serial.setTxTimeoutMs(0);
}

void loop() {
  const uint32_t loop_start = micros();
  M5.update();

  // Screenshot request. Cheap to poll and inert unless a host asks.
  if (Serial.available()) {
    const int cmd = Serial.read();
    if (cmd == 's') dumpScreen();
    if (cmd == 'f') dumpFontSpecimen();
    if (cmd == 'p') dumpPanel();
    if (cmd == 'd') probeSd();
    if (cmd == 'a') nes_ui::startAudioCapture(4);
    // "j<hex>,<frames>": hold NES buttons, e.g. j08,20 holds START 20 frames.
    if (cmd == 'j') {
      const String arg = Serial.readStringUntil('\n');
      const int comma = arg.indexOf(',');
      if (comma > 0) {
        nes_ui::injectPad((uint8_t)strtol(arg.substring(0, comma).c_str(), nullptr, 16),
                          (uint32_t)arg.substring(comma + 1).toInt());
      }
    }
    // "t<x>,<y>" injects a tap, so a screen several taps deep can be reached
    // and captured without hands on the panel. Same entry point a real touch
    // uses, so it exercises the actual hit targets rather than a shortcut.
    if (cmd == 't') {
      const String arg = Serial.readStringUntil('\n');
      const int comma = arg.indexOf(',');
      if (comma > 0) {
        app::handleTap(arg.substring(0, comma).toInt(),
                       arg.substring(comma + 1).toInt(), millis());
      }
    }
  }

  // A finished audio capture is streamed out here, between frames, rather
  // than from inside the emulator's own loop.
  {
    const int16_t *cap = nullptr;
    uint32_t n = 0;
    if (nes_ui::audioCaptureReady(&cap, &n)) {
      Serial.setTxTimeoutMs(200);
      Serial.printf("AUDIO %lu 44100\n", (unsigned long)n);
      Serial.write((const uint8_t *)cap, n * sizeof(int16_t));
      Serial.flush();
      Serial.println("ENDAUDIO");
      Serial.setTxTimeoutMs(0);
      nes_ui::endAudioCapture();
    }
  }

  const uint32_t now = millis();

  // Detect the press edge directly rather than relying on wasPressed(), so a
  // tap still counts if M5Unified's gesture state machine classifies it as
  // something other than a plain press.
  // Detect the press edge directly rather than relying on wasPressed(), so a
  // tap still counts if M5Unified's gesture state machine classifies it as a
  // flick or drag because the finger moved a pixel or two.
  auto touch = M5.Touch.getDetail();
  const bool raw_down = touch.isPressed() || M5.Touch.getCount() > 0;

  // Stuck-touch watchdog.
  //
  // Touch has been seen to stop responding mid-game while the loop kept
  // running (the clock carried on ticking). Everything downstream keys off
  // press EDGES, so a contact that never reports a release wedges all of it:
  // no edge can fire again. Nobody holds a finger still on a puzzle for four
  // seconds, so past that the contact is treated as stale and ignored, which
  // lets edges resume as soon as the hardware reports anything sane.
  //
  // The log line records whether the raw controller or M5Unified's state
  // machine is the one stuck, which is what decides where a real fix belongs.
  constexpr uint32_t kStuckMs = 4000;
  static uint32_t down_since = 0;
  static bool stale = false;

  // Latched, not printed. Serial is non-blocking (it must be — see the README)
  // so anything printed while no host is attached is DROPPED. A fault that
  // only happens while nobody is watching therefore cannot be diagnosed from
  // log lines; it has to leave state behind that can be read afterwards.
  static uint32_t g_max_down_ms = 0;
  static uint32_t g_stuck_events = 0;
  static int g_stuck_raw = -1, g_stuck_cooked = -1;
  static uint32_t g_taps = 0;
  static int g_last_tap_x = -1, g_last_tap_y = -1;
  if (raw_down) {
    if (down_since == 0) down_since = now;
    const uint32_t held = now - down_since;
    if (held > g_max_down_ms) g_max_down_ms = held;
    if (!stale && held >= kStuckMs) {
      stale = true;
      lgfx::touch_point_t raw[4];
      g_stuck_raw = M5.Display.getTouchRaw(raw, 4);
      g_stuck_cooked = M5.Touch.getCount();
      g_stuck_events++;
    }
  } else {
    down_since = 0;
    stale = false;
  }

  const bool down = raw_down && !stale;
  static bool was_down = false;
  const uint32_t p0 = micros();
  if (down && !was_down) {
    g_taps++;
    g_last_tap_x = touch.x;
    g_last_tap_y = touch.y;
    app::handleTap(touch.x, touch.y, now);
  }
  was_down = down;
  const uint32_t p1 = micros();

  // A flip changes the display transform, so everything has to be redrawn.
  //
  // Skipped entirely in games that don't need it: every IMU read shares the
  // internal I2C bus with the touch controller, and that contention is the
  // leading suspect for the phantom-touch lockups.
  if (app::wantsOrientation() && orientation::update(now)) app::invalidate();
  const uint32_t p2 = micros();

  audio::update(now);
  const uint32_t p3 = micros();

  app::tick(now);
  const uint32_t p4 = micros();

  // Anything that blocks the loop eats taps, whatever the cause — rendering,
  // an NVS write, the audio path restarting, an I2C stall. Rather than guess
  // which, attribute every slow iteration to the phase that caused it.
  // m5 = M5.update() at the top of the loop, which does the touch I2C read.
  const uint32_t total = p4 - loop_start;
  static uint32_t worst = 0, w_m5 = 0, w_tap = 0, w_or = 0, w_au = 0, w_tk = 0;
  if (total > worst) {
    worst = total;
    w_m5 = p0 - loop_start;
    w_tap = p1 - p0;
    w_or = p2 - p1;
    w_au = p3 - p2;
    w_tk = p4 - p3;
  }
  // Not while emulating. Every NES frame exceeds this threshold by design, and
  // a Serial.printf per frame with a host attached blocks long enough to
  // change the very number being measured — the same trap that made touch look
  // broken earlier in this project. nes_ui reports its own timings once a
  // second instead.
  if (total > 25000 && app::current() != app::GameId::Nes) {
    Serial.printf("slow loop %u ms: m5=%u tap=%u orient=%u audio=%u tick=%u\n",
                  (unsigned)(total / 1000), (unsigned)((p0 - loop_start) / 1000),
                  (unsigned)((p1 - p0) / 1000), (unsigned)((p2 - p1) / 1000),
                  (unsigned)((p3 - p2) / 1000), (unsigned)((p4 - p3) / 1000));
  }

  // Worst repaint since boot. Any repaint blocks the loop and therefore eats
  // taps, so this is the single number that predicts "touch feels dead".
  static uint32_t last_stat = 0;
  if (now - last_stat >= 15000) {
    last_stat = now;
    Serial.printf(
        "stats worst_repaint=%u ms screen=%d repaints=%u | worst_loop=%u ms "
        "m5=%u tap=%u orient=%u audio=%u tick=%u\n",
        (unsigned)(uikit::worstRepaintUs() / 1000), uikit::worstRepaintScreen(),
        (unsigned)uikit::repaintCount(), (unsigned)(worst / 1000),
        (unsigned)(w_m5 / 1000), (unsigned)(w_tap / 1000),
        (unsigned)(w_or / 1000), (unsigned)(w_au / 1000),
        (unsigned)(w_tk / 1000));
    // Everything needed to diagnose "the screen stopped taking presses"
    // after the fact, without having been connected when it happened.
    Serial.printf(
        "      taps=%u last=(%d,%d) maxdown=%ums stuck=%u raw=%d cooked=%d "
        "targets=%u\n",
        (unsigned)g_taps, g_last_tap_x, g_last_tap_y,
        (unsigned)g_max_down_ms, (unsigned)g_stuck_events, g_stuck_raw,
        g_stuck_cooked, (unsigned)uikit::targetCount());
    Serial.printf("      batt=%d%% chg=%d read=%uus disabled=%d\n",
                  battery::level(), (int)battery::charging(),
                  (unsigned)battery::lastReadUs(), (int)battery::disabled());
  }

  // The beep schedule is checked inside tick(); a short yield keeps the timer
  // accurate to a few milliseconds without spinning the CPU flat out.
  delay(5);
}
