// PhraseCraze — Phase 0 hardware spike.
//
// This is NOT the game. It is a throwaway smoke test that proves the five
// things the game depends on actually work on this Tab5, under this toolchain
// (PlatformIO + pioarduino + Arduino ESP32-P4 core + M5Unified):
//
//   1. Display   — you can read this screen at all
//   2. Speaker   — ES8388 / 1 W speaker makes a tone
//   3. LittleFS  — mounts, and survives a reboot (boot counter increments)
//   4. WiFi      — associates via the ESP32-C6 co-processor over SDIO
//   5. Touch     — taps land where you put them, not mirrored or swapped
//
// Results render on-screen so you can read them off the Tab5 itself; the same
// lines go to Serial at 115200 if you'd rather paste logs.

#include <M5Unified.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "secrets.h"

// The P4 has no radio. WiFi comes from the onboard ESP32-C6 over SDIO; these
// are the Tab5's SDIO2 pins, cross-checked against the esp32_hosted block in
// the ESPHome Tab5 config that already drives this hardware.
static constexpr int SDIO2_CLK = 12;
static constexpr int SDIO2_CMD = 13;
static constexpr int SDIO2_D0  = 11;
static constexpr int SDIO2_D1  = 10;
static constexpr int SDIO2_D2  = 9;
static constexpr int SDIO2_D3  = 8;
static constexpr int SDIO2_RST = 15;

static constexpr uint32_t WIFI_TIMEOUT_MS = 20000;

// ---------------------------------------------------------------- checklist

enum class CheckState : uint8_t { Pending, Running, Pass, Fail };

struct Check {
  const char *label;
  CheckState state = CheckState::Pending;
  String detail = "";
};

static Check checks[] = {
    {"1  Display"},
    {"2  Speaker"},
    {"3  LittleFS"},
    {"4  WiFi (C6 over SDIO)"},
    {"5  Touch"},
};
static constexpr size_t CHECK_COUNT = sizeof(checks) / sizeof(checks[0]);

static constexpr int ROW_H = 62;
static constexpr int ROW_TOP = 150;
static constexpr int MARGIN = 60;

static uint16_t stateColor(CheckState s) {
  switch (s) {
    case CheckState::Pass: return TFT_GREEN;
    case CheckState::Fail: return TFT_RED;
    case CheckState::Running: return TFT_YELLOW;
    default: return TFT_DARKGREY;
  }
}

static const char *stateText(CheckState s) {
  switch (s) {
    case CheckState::Pass: return "PASS";
    case CheckState::Fail: return "FAIL";
    case CheckState::Running: return "...";
    default: return "-";
  }
}

static void drawRow(size_t i) {
  auto &d = M5.Display;
  const int y = ROW_TOP + int(i) * ROW_H;
  d.fillRect(0, y, d.width(), ROW_H, TFT_BLACK);

  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextDatum(middle_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.drawString(checks[i].label, MARGIN, y + ROW_H / 2);

  d.setTextColor(stateColor(checks[i].state), TFT_BLACK);
  d.setTextDatum(middle_right);
  d.drawString(stateText(checks[i].state), d.width() - MARGIN, y + ROW_H / 2);

  if (checks[i].detail.length()) {
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setTextDatum(middle_right);
    d.drawString(checks[i].detail, d.width() - MARGIN - 110, y + ROW_H / 2);
  }
}

static void setCheck(size_t i, CheckState s, const String &detail = "") {
  checks[i].state = s;
  if (detail.length()) checks[i].detail = detail;
  Serial.printf("[%s] %s %s\n", stateText(s), checks[i].label,
                checks[i].detail.c_str());
  drawRow(i);
}

// ------------------------------------------------------------------ buttons

struct Button {
  const char *label;
  int x, y, w, h;
  uint16_t color;
  bool hit(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

static Button buttons[] = {
    {"TOUCH TEST", 0, 0, 0, 0, TFT_BLUE},
    {"BEEP", 0, 0, 0, 0, TFT_DARKGREEN},
    {"REBOOT", 0, 0, 0, 0, TFT_MAROON},
};
static constexpr size_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);

static void layoutButtons() {
  auto &d = M5.Display;
  const int h = 86;
  const int gap = 24;
  const int w = (d.width() - 2 * MARGIN - gap * int(BUTTON_COUNT - 1)) / int(BUTTON_COUNT);
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
    buttons[i].x = MARGIN + int(i) * (w + gap);
    buttons[i].y = d.height() - h - 40;
    buttons[i].w = w;
    buttons[i].h = h;
  }
}

static void drawButtons() {
  auto &d = M5.Display;
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextDatum(middle_center);
  for (const auto &b : buttons) {
    d.fillRoundRect(b.x, b.y, b.w, b.h, 14, b.color);
    d.setTextColor(TFT_WHITE, b.color);
    d.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
  }
}

// ------------------------------------------------------------------ screens

static void drawHeader(const char *title, const char *subtitle) {
  auto &d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::FreeSansBold24pt7b);
  d.setTextDatum(top_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.drawString(title, MARGIN, 40);
  d.setFont(&fonts::FreeSans9pt7b);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.drawString(subtitle, MARGIN, 100);
}

static void drawChecklist() {
  drawHeader("PhraseCraze — hardware spike",
             "Phase 0 smoke test. Read the results, then try the touch test.");
  for (size_t i = 0; i < CHECK_COUNT; i++) drawRow(i);
  layoutButtons();
  drawButtons();
}

// Full-screen touch canvas. Corner labels let you confirm the axes aren't
// swapped or mirrored: tap the corner marked TL and the dot must land on TL.
static bool touchTestMode = false;

static void drawTouchTest() {
  auto &d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);

  d.setTextDatum(top_left);
  d.drawString("TL", 16, 16);
  d.setTextDatum(top_right);
  d.drawString("TR", d.width() - 16, 16);
  d.setTextDatum(bottom_left);
  d.drawString("BL", 16, d.height() - 16);
  d.setTextDatum(bottom_right);
  d.drawString("BR", d.width() - 16, d.height() - 16);

  d.setTextDatum(middle_center);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.drawString("Tap the corners. Dots must land where you tap.",
               d.width() / 2, d.height() / 2 - 30);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.drawString("Tap here (center) to go back", d.width() / 2, d.height() / 2 + 30);
}

// ------------------------------------------------------------------- checks

static void checkSpeaker() {
  setCheck(1, CheckState::Running);
  M5.Speaker.setVolume(180);
  // A short rising arpeggio — distinctive enough that you can't mistake it for
  // an incidental click, and it exercises tone() the way the game timer will.
  const int notes[] = {880, 1175, 1568};
  for (int f : notes) {
    M5.Speaker.tone(f, 120);
    delay(150);
  }
  // tone() can't report whether sound physically came out, so this is a
  // "did the API accept it" pass plus a listen-for-it instruction.
  setCheck(1, M5.Speaker.isEnabled() ? CheckState::Pass : CheckState::Fail,
           M5.Speaker.isEnabled() ? "3 rising beeps — did you hear them?"
                                  : "speaker not enabled");
}

static void checkLittleFS() {
  setCheck(2, CheckState::Running);
  if (!LittleFS.begin(true)) {
    setCheck(2, CheckState::Fail, "mount failed");
    return;
  }

  uint32_t boots = 0;
  if (File f = LittleFS.open("/boot_count", "r")) {
    boots = strtoul(f.readString().c_str(), nullptr, 10);
    f.close();
  }
  boots++;

  File f = LittleFS.open("/boot_count", "w");
  if (!f) {
    setCheck(2, CheckState::Fail, "mounted but write failed");
    return;
  }
  f.print(boots);
  f.close();

  const size_t totalKB = LittleFS.totalBytes() / 1024;
  const size_t usedKB = LittleFS.usedBytes() / 1024;
  String detail = String(totalKB / 1024) + " MB fs, boot #" + String(boots);
  if (boots < 2) detail += " (reboot to verify persistence)";
  else detail += " — persisted";
  setCheck(2, CheckState::Pass, detail);
  Serial.printf("LittleFS: %u KB used of %u KB\n", (unsigned)usedKB,
                (unsigned)totalKB);
}

static void checkWiFi() {
  setCheck(3, CheckState::Running, "associating...");
  WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3,
               SDIO2_RST);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    setCheck(3, CheckState::Pass, WiFi.localIP().toString() + "  " + String(WiFi.RSSI()) + " dBm");
  } else {
    // Overwhelmingly the most likely cause is stale esp_hosted firmware on the
    // C6, not a wrong password — see the project README.
    setCheck(3, CheckState::Fail, "no IP — check C6 esp_hosted firmware");
  }
}

// --------------------------------------------------------------------- main

void setup() {
  auto cfg = M5.config();
  cfg.output_power = true;
  M5.begin(cfg);

  Serial.begin(115200);
  delay(100);

  M5.Display.setRotation(1);  // landscape, 1280x720
  M5.Display.setBrightness(200);

  drawChecklist();

  // If you can read the checklist, the display works. Nothing else to test.
  setCheck(0, CheckState::Pass,
           String(M5.Display.width()) + "x" + String(M5.Display.height()));

  checkSpeaker();
  checkLittleFS();
  checkWiFi();

  setCheck(4, CheckState::Running, "tap TOUCH TEST below");
  drawButtons();

  Serial.printf("PSRAM: %u KB free of %u KB\n",
                (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(ESP.getPsramSize() / 1024));
}

void loop() {
  M5.update();

  auto t = M5.Touch.getDetail();
  if (!t.wasPressed()) {
    delay(5);
    return;
  }

  Serial.printf("touch x=%d y=%d\n", t.x, t.y);

  if (touchTestMode) {
    auto &d = M5.Display;
    // Center third acts as "back" so the rest of the screen stays free to test.
    const bool inCenter = abs(t.x - d.width() / 2) < 260 &&
                          abs(t.y - d.height() / 2) < 70;
    if (inCenter) {
      touchTestMode = false;
      setCheck(4, CheckState::Pass, "dots tracked taps");
      drawChecklist();
      return;
    }
    d.fillCircle(t.x, t.y, 14, TFT_ORANGE);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextDatum(middle_left);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawString(String(t.x) + "," + String(t.y), t.x + 22, t.y);
    M5.Speaker.tone(2000, 30);
    return;
  }

  if (buttons[0].hit(t.x, t.y)) {
    touchTestMode = true;
    drawTouchTest();
  } else if (buttons[1].hit(t.x, t.y)) {
    checkSpeaker();
  } else if (buttons[2].hit(t.x, t.y)) {
    Serial.println("rebooting to verify LittleFS persistence");
    delay(200);
    ESP.restart();
  }
}
