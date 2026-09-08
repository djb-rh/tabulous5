#include "nes_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

#include "app.h"
#include "../third_party/anemoia/core/cartridge.h"
#include "../third_party/anemoia/core/cpu6502.h"
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

Cpu6502 *g_cpu = nullptr;
Cartridge *g_cart = nullptr;
int g_playing = -1;

// The PPU hands back finished scanlines in chunks rather than whole frames, so
// the callback has to know how far down the picture it has got. Reset each
// frame; the core always emits them in order.
volatile int g_chunk_row = 0;
const char *g_error = "";

// One NES frame of RGB565, in INTERNAL RAM. The transpose below reads it with
// a stride, which is cheap here and would not be in PSRAM.
uint16_t *g_nes = nullptr;

// The panel's own framebuffer, written directly. See blit() for why.
uint8_t *g_fb = nullptr;
size_t g_fb_stride = 0;
uint8_t g_fb_rot = 1;

uint8_t g_pad = 0, g_pad_drawn = 0xFF;
bool g_menu_down = false;
uint32_t g_frames = 0, g_fps_at = 0, g_fps = 0;
// Phase timings, accumulated over a second so one print covers many frames.
uint32_t g_us_emu = 0, g_us_conv = 0, g_us_push = 0, g_us_sync = 0;

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
  delete g_cpu;
  g_cpu = nullptr;
  delete g_cart;   // opened the ROM file itself, and closes it
  g_cart = nullptr;
  g_playing = -1;
}

// Called by the PPU every SCANLINES_PER_BUFFER finished lines, with RGB565
// pixels — no palette conversion needed. Just accumulated; the scaling and the
// transpose happen once per frame in blit().
IRAM_ATTR void drawChunk(uint8_t *buffer, uint32_t size) {
  if (!g_nes) return;
  const int rows = (int)(size / sizeof(uint16_t)) / joypad::kNesW;
  if (g_chunk_row + rows > joypad::kNesH) return;
  memcpy(g_nes + (size_t)g_chunk_row * joypad::kNesW, buffer, size);
  g_chunk_row += rows;
}

bool load(int index) {
  releaseCore();
  const Entry &e = g_roms[index];

  // The cartridge opens the file itself and streams from it, so there is no
  // whole-ROM buffer to place any more.
  g_cart = new Cartridge(e.path, ROMBackend::LRU);
  if (!g_cart || !g_cart->isValid()) {
    g_error = "core rejected this ROM";
    releaseCore();
    return false;
  }

  g_cpu = new Cpu6502();
  if (!g_cpu) { g_error = "out of memory for core"; releaseCore(); return false; }
  g_cpu->bus.insertCartridge(g_cart);
  g_cpu->bus.ppu.setDrawCallback(drawChunk);
  g_cpu->reset();

  if (!g_nes) {
    g_nes = (uint16_t *)heap_caps_malloc(
        (size_t)joypad::kNesW * joypad::kNesH * 2,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!g_nes) { g_error = "out of memory for framebuffer"; releaseCore(); return false; }
  memset(g_nes, 0, (size_t)joypad::kNesW * joypad::kNesH * 2);

  // Resolve the panel's framebuffer once. config_detail() is public, so this
  // needs no games with protected members.
  auto *panel = (lgfx::Panel_DSI *)M5.Display.getPanel();
  g_fb = (uint8_t *)panel->config_detail().buffer;
  g_fb_stride = ((size_t)panel->config().panel_width * 2 + 3) & ~(size_t)3;
  g_fb_rot = (uint8_t)M5.Display.getRotation();
  if (!g_fb || (g_fb_rot != 1 && g_fb_rot != 3)) {
    g_error = "unexpected panel orientation";
    releaseCore();
    return false;
  }

  g_playing = index;
  g_error = "";
  g_frames = 0;
  g_fps = 0;
  g_fps_at = millis();
  return true;
}

// Writes the picture straight into the panel's framebuffer, transposed.
//
// The panel is natively 720x1280 PORTRAIT and the UI runs 1280x720 landscape,
// so LovyanGFX's rotation maps a landscape scanline onto a COLUMN of the
// framebuffer — every pixel of a horizontal run lands 1440 bytes from the last.
// That is what made pushImage cost 29 ms: it is not the copy, it is the stride.
//
// The 2x scale has to touch every destination pixel anyway, so the transpose
// rides along for free: iterating DOWN a landscape column walks the framebuffer
// sequentially. The strided access moves to the source, which is 120 KB in
// internal RAM where stride costs almost nothing.
//
// Mapping is taken from Panel_FrameBufferBase::drawPixelPreclipped rather than
// guessed. For rotation 1: panel row = logical x, panel column = 719 - y.
// For rotation 3 it is the other diagonal.
IRAM_ATTR void blit() {
  if (!g_fb || !g_nes) return;
  const uint32_t t1 = micros();

  const int vx = joypad::kVideoX, vy = joypad::kVideoY;
  const int logical_h = joypad::kPanelH, logical_w = joypad::kPanelW;

  for (int sx = 0; sx < joypad::kNesW; sx++) {
    const uint16_t *src = g_nes + sx;  // walk this source column
    for (int k = 0; k < 2; k++) {      // each source column is two output ones
      const int lx = vx + sx * 2 + k;
      uint16_t *dst;
      int step;
      if (g_fb_rot == 1) {
        // row = lx, column = 719 - y; ascending address = descending y.
        dst = (uint16_t *)(g_fb + (size_t)lx * g_fb_stride) +
              (logical_h - 1 - (vy + joypad::kVideoH - 1));
        step = 1;
      } else {
        // rotation 3: row = 1279 - lx, column = y; ascending address = ascending y.
        dst = (uint16_t *)(g_fb + (size_t)(logical_w - 1 - lx) * g_fb_stride) + vy;
        step = 1;
      }
      // The two output pixels are adjacent and the run always starts 4-byte
      // aligned, so each pair goes out as one 32-bit store. Measured: no
      // faster than two 16-bit stores — this is bound by PSRAM write
      // bandwidth (492 KB a frame at ~57 MB/s), not by instruction count.
      uint32_t *pair = (uint32_t *)dst;
      if (g_fb_rot == 1) {
        for (int sy = joypad::kNesH - 1; sy >= 0; sy--) {
          const uint32_t v = src[(size_t)sy * joypad::kNesW];
          *pair++ = v | (v << 16);
        }
      } else {
        for (int sy = 0; sy < joypad::kNesH; sy++) {
          const uint32_t v = src[(size_t)sy * joypad::kNesW];
          *pair++ = v | (v << 16);
        }
      }
      (void)step;
    }
  }

  // The framebuffer is cached and the DSI scans it by DMA, so the writes have
  // to be pushed out or the panel shows stale pixels. One flush over the whole
  // touched span rather than 512 small ones.
  const size_t first_row = (g_fb_rot == 1) ? vx : (logical_w - (vx + joypad::kVideoW));
  const uint32_t t_sync = micros();
  esp_cache_msync(g_fb + first_row * g_fb_stride,
                  (size_t)joypad::kVideoW * g_fb_stride,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  g_us_sync += micros() - t_sync;

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

  // joypad's bit order is the NES shift register's, which is what the core
  // expects, so the pad byte goes across untranslated.
  g_cpu->bus.setController(g_pad);

  const uint32_t t_emu = micros();
  g_chunk_row = 0;
  g_cpu->clockFrame();   // drawChunk fires ~30 times during this
  g_us_emu += micros() - t_emu;

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
    Serial.printf("nes %lu fps  emulate=%luus push=%luus (of which sync=%luus)\n",
                  (unsigned long)g_fps, (unsigned long)(g_us_emu / n),
                  (unsigned long)(g_us_push / n),
                  (unsigned long)(g_us_sync / n));
    g_us_emu = g_us_conv = g_us_push = g_us_sync = 0;
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
  if (g_mode == Mode::Playing && g_cpu) {
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
