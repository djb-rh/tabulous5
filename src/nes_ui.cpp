#include "nes_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "agnes.h"
#include "app.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "nesrom.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace nes_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::gfx;

constexpr const char *kRomDir = "/roms";

struct Entry {
  char name[40];
  char path[64];
  size_t bytes;
  nesrom::Info info;
  bool readable;       // header parsed
  const char *problem; // why it cannot be played, or ""
};

enum class Mode : uint8_t { Picking, Playing };

std::vector<Entry> g_roms;
Mode g_mode = Mode::Picking;
bool g_dirty = true;

agnes_t *g_agnes = nullptr;
uint8_t *g_rom_data = nullptr;  // agnes keeps pointers into this; it must live
int g_playing = -1;
const char *g_error = "";

// The picture, padded to a stride that avoids the cache cliff. Held for the
// life of the screen: allocating 500 KB per frame would be absurd.
uint16_t *g_frame = nullptr;

uint8_t g_pad = 0, g_pad_drawn = 0xFF;
bool g_menu_down = false;
uint32_t g_frames = 0, g_fps_at = 0, g_fps = 0;
// Phase timings, accumulated over a second so one print covers many frames.
uint32_t g_us_emu = 0, g_us_conv = 0, g_us_push = 0;

enum class Action : uint8_t { None, Pick, Back };

void addAction(const Rect &r, Action a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

// ------------------------------------------------------------------ scanning

void scan() {
  g_roms.clear();
  File dir = LittleFS.open(kRomDir);
  if (!dir || !dir.isDirectory()) return;

  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char *n = f.name();
    const size_t len = strlen(n);
    if (len < 5 || strcasecmp(n + len - 4, ".nes") != 0) continue;

    Entry e{};
    snprintf(e.path, sizeof(e.path), "%s/%s", kRomDir, n);
    // Trim the extension for display; the list is about games, not files.
    snprintf(e.name, sizeof(e.name), "%.*s", (int)(len - 4), n);
    for (char *p = e.name; *p; p++) {
      if (*p == '_') *p = ' ';
    }
    e.bytes = f.size();

    uint8_t head[16];
    File h = LittleFS.open(e.path, "r");
    const size_t got = h ? h.read(head, sizeof(head)) : 0;
    if (h) h.close();

    const char *why = "";
    e.readable = got == sizeof(head) &&
                 nesrom::parseHeader(head, e.bytes, &e.info, &why);
    if (!e.readable) {
      e.problem = why[0] ? why : "unreadable";
    } else if (!e.info.supported) {
      e.problem = "mapper not supported";
    } else {
      e.problem = "";
    }
    g_roms.push_back(e);
  }
}

// ------------------------------------------------------------------- picker

void drawPicker() {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::drawLabel("NES", kMargin, 46, kText, &fonts::FreeSansBold24pt7b,
                   middle_left);

  const Rect back{kW - kMargin - 190, 18, 190, 62};
  uikit::drawButton(back, "MENU", kSurfaceLift, kText,
                    &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Back);

  if (g_error[0]) {
    uikit::drawLabel(g_error, kW / 2, 300, kDanger, &fonts::FreeSansBold18pt7b,
                     middle_center);
  }

  if (g_roms.empty()) {
    uikit::drawLabel("No ROMs found in /roms", kW / 2, 300, kMuted,
                     &fonts::FreeSansBold18pt7b, middle_center);
    uikit::drawLabel("Put .nes files in data/roms and run: pio run -t uploadfs",
                     kW / 2, 350, kMuted, &fonts::FreeSans12pt7b, middle_center);
    return;
  }

  const int row_h = 96, gap = 10, top = 110;
  const int max_rows = (kH - top - 30) / (row_h + gap);
  for (int i = 0; i < (int)g_roms.size() && i < max_rows; i++) {
    const Entry &e = g_roms[i];
    const Rect row{kMargin, top + i * (row_h + gap), kW - 2 * kMargin, row_h};
    const bool ok = e.problem[0] == '\0';

    // A ROM that cannot run is still listed, dimmed, with the reason on it.
    // Hiding it would leave the owner hunting for a file that is right there.
    uikit::fillRoundRectShaded(row.x, row.y, row.w, row.h, 16,
                               ok ? 0x2C6E9B : 0x3A3F47,
                               shade(ok ? 0x2C6E9B : 0x3A3F47, 80),
                               shade(ok ? 0x2C6E9B : 0x3A3F47, 58), 5);
    const uint16_t ink = ok ? inkFor(0x2C6E9B) : kMuted;
    uikit::drawLabel(e.name, row.x + 28, row.y + 24, ink,
                     &fonts::FreeSansBold18pt7b);

    char sub[80];
    if (ok) {
      snprintf(sub, sizeof(sub), "%s  -  %u KB", nesrom::mapperName(e.info.mapper),
               (unsigned)(e.bytes / 1024));
    } else {
      snprintf(sub, sizeof(sub), "%s  -  %s", e.problem,
               e.readable ? nesrom::mapperName(e.info.mapper) : "bad header");
    }
    uikit::drawLabel(sub, row.x + 28, row.y + 62, ink, &fonts::FreeSans12pt7b);
    if (ok) addAction(row, Action::Pick, i);
  }
}

// --------------------------------------------------------------------- play

void releaseCore() {
  if (g_agnes) {
    agnes_destroy(g_agnes);
    g_agnes = nullptr;
  }
  if (g_rom_data) {
    heap_caps_free(g_rom_data);
    g_rom_data = nullptr;
  }
  g_playing = -1;
}

bool load(int index) {
  releaseCore();
  const Entry &e = g_roms[index];

  File f = LittleFS.open(e.path, "r");
  if (!f) { g_error = "cannot open ROM"; return false; }
  // INTERNAL RAM, not PSRAM. agnes keeps pointers into this buffer and reads
  // it on every instruction fetch and every pattern fetch — tens of thousands
  // of random reads per frame. Putting it in PSRAM cost more than everything
  // else in the frame put together. Most cartridges are tens of KB against
  // ~440 KB free, so this fits; oversized ones fall back to PSRAM and simply
  // run slower rather than refusing to load.
  g_rom_data = (uint8_t *)heap_caps_malloc(e.bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!g_rom_data) {
    g_rom_data = (uint8_t *)heap_caps_malloc(e.bytes, MALLOC_CAP_SPIRAM);
  }
  if (!g_rom_data) { f.close(); g_error = "out of memory for ROM"; return false; }
  const size_t got = f.read(g_rom_data, e.bytes);
  f.close();
  if (got != e.bytes) { g_error = "short read"; releaseCore(); return false; }

  // agnes_make mallocs ~83 KB. Deliberately left in INTERNAL RAM: emulation is
  // random access over a 61 KB screen buffer and 2 KB of CPU RAM, and PSRAM
  // would make every one of those accesses slower.
  g_agnes = agnes_make();
  if (!g_agnes) { g_error = "out of memory for core"; releaseCore(); return false; }
  if (!agnes_load_ines_data(g_agnes, g_rom_data, e.bytes)) {
    g_error = "core rejected this ROM";
    releaseCore();
    return false;
  }

  if (!g_frame) {
    g_frame = (uint16_t *)heap_caps_malloc(
        (size_t)joypad::kVideoStride * joypad::kVideoH * 2, MALLOC_CAP_SPIRAM);
  }
  if (!g_frame) { g_error = "out of memory for framebuffer"; releaseCore(); return false; }
  memset(g_frame, 0, (size_t)joypad::kVideoStride * joypad::kVideoH * 2);

  g_playing = index;
  g_error = "";
  g_frames = 0;
  g_fps = 0;
  g_fps_at = millis();
  return true;
}

// 256x240 palette indices out of the core, 2x into the padded buffer.
void blit() {
  auto &g = gfx();
  const uint32_t t0 = micros();
  const int pad = (joypad::kVideoStride - joypad::kVideoW) / 2;
  for (int y = 0; y < joypad::kNesH; y++) {
    uint16_t *r0 = g_frame + (size_t)(y * 2) * joypad::kVideoStride + pad;
    uint16_t *r1 = r0 + joypad::kVideoStride;
    for (int x = 0; x < joypad::kNesW; x++) {
      const agnes_color_t c = agnes_get_screen_pixel(g_agnes, x, y);
      const uint16_t v = g.color565(c.r, c.g, c.b);
      r0[0] = v; r0[1] = v;
      r1[0] = v; r1[1] = v;
      r0 += 2; r1 += 2;
    }
  }
  const uint32_t t1 = micros();
  g.startWrite();
  g.pushImage(joypad::kVideoX - pad, joypad::kVideoY, joypad::kVideoStride,
              joypad::kVideoH, g_frame);
  g.endWrite();
  g.waitDMA();
  g_us_conv += t1 - t0;
  g_us_push += micros() - t1;
}

void drawPlayChrome(bool full) {
  if (full) {
    gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  joypad_ui::drawControls(g_pad, g_pad_drawn, full);
  g_pad_drawn = g_pad;
}

void runFrame(uint32_t now_ms) {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);

  if (menu_held && !g_menu_down) {
    g_menu_down = true;
    releaseCore();
    g_mode = Mode::Picking;
    g_dirty = true;
    return;
  }
  if (!menu_held) g_menu_down = false;

  agnes_input_t in;
  memset(&in, 0, sizeof(in));
  in.a = (g_pad & joypad::kA) != 0;
  in.b = (g_pad & joypad::kB) != 0;
  in.select = (g_pad & joypad::kSelect) != 0;
  in.start = (g_pad & joypad::kStart) != 0;
  in.up = (g_pad & joypad::kUp) != 0;
  in.down = (g_pad & joypad::kDown) != 0;
  in.left = (g_pad & joypad::kLeft) != 0;
  in.right = (g_pad & joypad::kRight) != 0;
  agnes_set_input(g_agnes, &in, nullptr);

  const uint32_t t_emu = micros();
  const bool ok = agnes_next_frame(g_agnes);
  g_us_emu += micros() - t_emu;
  if (!ok) {
    g_error = "core stopped";
    releaseCore();
    g_mode = Mode::Picking;
    g_dirty = true;
    return;
  }

  blit();
  drawPlayChrome(false);

  // Rate reported once a second rather than drawn per frame: the readout would
  // otherwise cost more than the thing it measures.
  g_frames++;
  if (now_ms - g_fps_at >= 1000) {
    g_fps = g_frames * 1000 / (now_ms - g_fps_at);
    g_frames = 0;
    g_fps_at = now_ms;
    // Once a second, never per frame: this is the number that decides whether
    // the display path needs replacing, and it has to be readable without a
    // screenshot (a capture repaints over the on-screen figure).
    const uint32_t n = g_fps ? g_fps : 1;
    Serial.printf("nes %lu fps  emulate=%lums convert=%lums push=%lums\n",
                  (unsigned long)g_fps, (unsigned long)(g_us_emu / 1000 / n),
                  (unsigned long)(g_us_conv / 1000 / n),
                  (unsigned long)(g_us_push / 1000 / n));
    g_us_emu = g_us_conv = g_us_push = 0;
    char line[32];
    snprintf(line, sizeof(line), "%lu fps", (unsigned long)g_fps);
    auto &g = gfx();
    g.fillRect(kMargin + 4, 108, 170, 34, kBg);
    uikit::drawLabel(line, kMargin + 4, 124, kMuted, &fonts::FreeSans12pt7b,
                     middle_left);
  }
}

}  // namespace

void begin() {
  releaseCore();
  scan();
  g_mode = Mode::Picking;
  g_error = "";
  g_dirty = true;
}

void invalidate() {
  g_dirty = true;
  g_pad_drawn = 0xFF;
}

bool playing() { return g_mode == Mode::Playing; }

void tick(uint32_t now_ms) {
  if (g_mode == Mode::Playing && g_agnes) {
    if (g_dirty) {
      g_dirty = false;
      drawPlayChrome(true);
    }
    runFrame(now_ms);
    return;
  }
  if (!g_dirty) return;
  g_dirty = false;
  const uint32_t t0 = micros();
  uikit::clearTargets();
  drawPicker();
  uikit::present();
  uikit::noteRepaint(micros() - t0, 71);
}

void handleTap(int x, int y, uint32_t) {
  if (g_mode == Mode::Playing) return;  // play polls touch itself
  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return;
  switch ((Action)action) {
    case Action::Pick:
      if (load(param)) {
        g_mode = Mode::Playing;
        g_menu_down = true;  // the tap that launched it must not also exit
      }
      g_dirty = true;
      break;
    case Action::Back:
      app::requestExit();
      break;
    case Action::None:
      break;
  }
}

}  // namespace nes_ui
}  // namespace tabulous
