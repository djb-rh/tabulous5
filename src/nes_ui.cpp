#include "nes_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "app.h"
#include "audio.h"
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

// ---- audio -------------------------------------------------------------
//
// The APU hands over UNSIGNED 16-bit samples, the same value in both stereo
// channels, at 44.1 kHz in 128-frame chunks. M5.Speaker wants SIGNED, and it
// does not copy what it is given — it plays straight from the pointer — so the
// samples are converted into one of several buffers that stay alive until the
// speaker is finished with them.
//
// Only one channel is kept: both carry the same value, so playing mono halves
// the work and sounds identical.
constexpr int kAudioBufs = 4;
// One chunk is ~23 ms of sound. The speaker holds two per channel, so at most
// ~46 ms is ever queued — enough to ride out a slow frame, short enough that
// input does not feel detached from what is heard.
constexpr int kAudioBufSamples = 1024;
// A channel of its own, so game sound effects and the emulator cannot cut each
// other off.
constexpr int kAudioChannel = 1;
int16_t *g_audio[kAudioBufs] = {nullptr};
int g_audio_which = 0;
uint32_t g_audio_dropped = 0;

// The APU has NO internal clock. It generates a sample every so many calls to
// clock(), and upstream's only pacing was i2s_write() blocking on a full DMA
// buffer — measured here, free-running it produces 82 kHz, near double real
// time. So the speaker has to be the metronome, exactly as I2S was: chunks are
// submitted only while the speaker has a free slot, which meters consumption
// at exactly 44.1 kHz, and the ring filling up throttles the APU to match.
constexpr uint32_t kApuRate = 44100;
volatile uint32_t g_audio_made = 0;

// DC blocker and low-pass state. See onAudio.
int32_t g_dc_x = 0, g_dc_y = 0, g_lp = 0;

// The APU is not clocked by the CPU — upstream runs it in its own task, paced
// entirely by i2s_write() blocking when the DMA is full. Replacing that with a
// callback removed the brake, so the pacing has to come from somewhere else:
// this ring. The producer blocks when it is full, which throttles the APU to
// whatever rate we actually consume, and keeps sound in step with a game
// running below full speed rather than racing ahead of it.
constexpr int kRing = 8192;  // ~186 ms at 44.1 kHz
int16_t *g_ring = nullptr;
volatile uint32_t g_ring_w = 0, g_ring_r = 0;
TaskHandle_t g_apu_task = nullptr;
volatile bool g_apu_run = false;

uint8_t g_pad = 0, g_pad_drawn = 0xFF;
bool g_menu_down = false;
uint32_t g_frames = 0, g_fps_at = 0, g_fps = 0;

// Emulation is paced to real time, and the picture is drawn every other frame.
//
// The blit costs ~9.6 ms and is at the PSRAM write ceiling, so drawing every
// frame caps the whole machine at ~40 fps — a third slow, which is exactly what
// it looks and sounds like. Emulating all 60 frames and showing 30 of them
// costs 10 ms + half of 9.6, which fits inside a 16.7 ms frame. Game speed and
// pitch come right; motion is half as smooth. Upstream offers the same trade
// for slow displays.
constexpr uint32_t kFrameUs = 1000000 / 60;
constexpr int kDrawEvery = 2;
uint32_t g_next_frame_us = 0;
uint32_t g_frame_seq = 0;
uint32_t g_drawn_frames = 0, g_shown = 0;
// Phase timings, accumulated over a second so one print covers many frames.
uint32_t g_us_emu = 0, g_us_conv = 0, g_us_push = 0, g_us_sync = 0;

enum class Action : uint8_t { None, Pick, Back, PageUp, PageDown };

// Pages rather than single steps: this list is about to hold thousands of
// entries, and stepping one row at a time through that is not navigation.
constexpr int kPickRows = 5;
int g_pick_scroll = 0;

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

  // Alphabetical, case-insensitively: the filesystem hands them back in
  // whatever order it likes, which is no order at all to a person hunting for
  // a title.
  std::sort(g_roms.begin(), g_roms.end(), [](const Entry &a, const Entry &b) {
    return strcasecmp(a.name, b.name) < 0;
  });
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
  const int total = (int)g_roms.size();
  const int max_scroll = total > kPickRows ? total - kPickRows : 0;
  if (g_pick_scroll > max_scroll) g_pick_scroll = max_scroll;
  if (g_pick_scroll < 0) g_pick_scroll = 0;

  for (int slot = 0; slot < kPickRows; slot++) {
    const int i = g_pick_scroll + slot;
    if (i >= total) break;
    const Entry &e = g_roms[i];
    const Rect row{kMargin, top + slot * (row_h + gap), kW - 2 * kMargin, row_h};
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

  if (max_scroll > 0) {
    const int by = top + kPickRows * (row_h + gap) + 4;
    const Rect up{kMargin, by, 120, 58};
    const Rect down{kMargin + 132, by, 120, 58};
    const bool can_up = g_pick_scroll > 0;
    const bool can_down = g_pick_scroll < max_scroll;
    uikit::drawArrowButton(up, true, can_up ? kSurfaceLift : kSurface,
                           can_up ? kText : kMuted);
    uikit::drawArrowButton(down, false, can_down ? kSurfaceLift : kSurface,
                           can_down ? kText : kMuted);
    if (can_up) addAction(up, Action::PageUp);
    if (can_down) addAction(down, Action::PageDown);

    char pos[48];
    snprintf(pos, sizeof(pos), "%d-%d of %d", g_pick_scroll + 1,
             g_pick_scroll + kPickRows < total ? g_pick_scroll + kPickRows : total,
             total);
    uikit::drawLabel(pos, kMargin + 280, by + 29, kMuted,
                     &fonts::FreeSansBold12pt7b, middle_left);
  }
}

// --------------------------------------------------------------------- play

void releaseCore() {
  // Stop the APU task BEFORE the APU it points at is destroyed.
  g_apu_run = false;
  for (int i = 0; i < 200 && g_apu_task; i++) delay(1);
  Apu2A03::setAudioCallback(nullptr);
  M5.Speaker.stop();
  delete g_cpu;
  g_cpu = nullptr;
  delete g_cart;   // opened the ROM file itself, and closes it
  g_cart = nullptr;
  g_playing = -1;
}

// Called by the APU whenever its buffer fills. Runs inside clockFrame(), so it
// must be cheap: convert and stash, never touch the speaker from here.
void onAudio(const uint16_t *samples, uint32_t bytes) {
  if (!g_ring) return;
  const uint32_t frames = bytes / sizeof(uint16_t) / 2;  // stereo pairs
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t next = (g_ring_w + 1) % kRing;
    int spins = 0;
    while (next == g_ring_r) {  // full — wait, which is what paces the APU
      if (!g_apu_run) return;
      vTaskDelay(1);
      if (++spins > 200) { g_audio_dropped++; return; }  // consumer gone; give up
    }
    // The APU's output is UNIPOLAR: generateSample() masks to 0xFF and shifts
    // left 8, so it runs 0..0xFF00 with silence at ZERO, not offset-binary
    // centred on 0x8000. Subtracting 32768 therefore pinned silence at full
    // negative DC, and every chunk boundary became a step between that and
    // zero — audible as a pop about 43 times a second.
    //
    // A one-pole DC blocker removes the offset instead of assuming one:
    //   y = x - x_prev + R*y_prev,  R = 4085/4096 (~34 Hz corner at 44.1 kHz)
    // Silence then really is silence, and chunks join without a step.
    // Only one channel is kept — both carry the same value.
    const int32_t x = (int32_t)samples[i * 2];
    int32_t y = x - g_dc_x + ((g_dc_y * 4085) >> 12);
    g_dc_x = x;
    g_dc_y = y;
    // Then a gentle low-pass, which the real console has and we did not.
    //
    // Anemoia sums the five channels linearly, so the output carries only
    // about 187 distinct levels — roughly 7.5 bits. That noise floor sits at a
    // fixed absolute level, so it is masked while a sound is loud and becomes
    // audible as an effect fades: hiss on the tails, which is exactly what was
    // reported. A NES runs its mix through an RC stage before the speaker;
    // this is a first-order equivalent at about 10 kHz, which cuts the
    // high-frequency part of that noise without dulling the square waves.
    g_lp += ((y - g_lp) * 2867) >> 12;  // a = 0.70
    int32_t out = g_lp;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    g_ring[g_ring_w] = (int16_t)out;
    g_ring_w = next;
    g_audio_made++;
  }
}

// Free-running, exactly as upstream does it, on the core the main loop is not
// using. Its speed is set by onAudio blocking, not by this loop.
void apuTask(void *) {
  while (g_apu_run) {
    g_cpu->apu.clock();
  }
  g_apu_task = nullptr;
  vTaskDelete(nullptr);
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
  for (int i = 0; i < kAudioBufs; i++) {
    if (!g_audio[i]) {
      g_audio[i] = (int16_t *)heap_caps_malloc(kAudioBufSamples * sizeof(int16_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
  }
  if (!g_ring) {
    g_ring = (int16_t *)heap_caps_malloc(kRing * sizeof(int16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!g_ring) { g_error = "out of memory for audio"; releaseCore(); return false; }
  g_audio_which = 0;
  g_ring_w = g_ring_r = 0;
  g_audio_dropped = 0;
  g_audio_made = 0;
  g_dc_x = g_dc_y = g_lp = 0;
  Apu2A03::setAudioCallback(onAudio);
  g_cpu->apu.setVolume(80);

  // Move the speaker's own task to core 0 as well. Measured: the loop takes
  // 15.7 ms but frames arrive 21 ms apart with only 0.4 ms spent outside
  // tick() — so 5.3 ms a frame was core 1 being preempted, and the speaker
  // task feeding I2S is what grew when NES audio arrived. Emulation gets
  // core 1 to itself; the two audio tasks share core 0, where the APU is
  // elastic and simply yields.
  {
    auto sc = M5.Speaker.config();
    if (sc.task_pinned_core != 0) {
      sc.task_pinned_core = 0;
      M5.Speaker.config(sc);
      M5.Speaker.begin();
    }
  }

  // Core 0: the Arduino loop this emulator runs in lives on core 1, so the APU
  // gets a core to itself rather than competing with the CPU and PPU.
  g_apu_run = true;
  xTaskCreatePinnedToCore(apuTask, "nes_apu", 4096, nullptr, 1, &g_apu_task, 0);
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
  g_next_frame_us = micros();
  g_frame_seq = 0;
  g_drawn_frames = 0;
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
  //
  // Splitting this loop across both cores was tried and gained 6% (9.6 -> 9.1
  // ms) for a task and a semaphore: the writes are bound by PSRAM bandwidth,
  // and two cores cannot push memory faster than one. Not worth the machinery.
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

  // Draw every kDrawEvery-th frame; emulate all of them.
  if ((g_frame_seq % kDrawEvery) == 0) {
    blit();
    g_drawn_frames++;
  }
  g_frame_seq++;

  // Feed the speaker while it has room. Its own consumption is the clock: two
  // queued chunks per channel is the ceiling, so this hands over sound at
  // exactly the rate it is played, and the ring backs up to throttle the APU.
  while (audio::enabled() && M5.Speaker.isPlaying(kAudioChannel) < 2) {
    uint32_t avail = (g_ring_w - g_ring_r + kRing) % kRing;
    if ((int)avail < kAudioBufSamples) break;
    int16_t *dst = g_audio[g_audio_which];
    if (!dst) break;
    for (int i = 0; i < kAudioBufSamples; i++) {
      dst[i] = g_ring[g_ring_r];
      g_ring_r = (g_ring_r + 1) % kRing;
    }
    M5.Speaker.playRaw(dst, (size_t)kAudioBufSamples, kApuRate, false, 1,
                       kAudioChannel, false);
    g_audio_which = (g_audio_which + 1) % kAudioBufs;
  }

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
    // made/sec should sit at ~44100: below means the speaker is starving, far
    // above means the APU is outrunning its pacing. Two frame rates now:
    // emulated frames a second (60 = correct game speed) and drawn frames a
    // second, which is deliberately half of it.
    Serial.printf("nes %lu emu/s %lu drawn/s  emulate=%luus push=%luus  audio made=%lu/s dropped=%lu\n",
                  (unsigned long)g_fps, (unsigned long)g_drawn_frames,
                  (unsigned long)(g_us_emu / n), (unsigned long)(g_us_push / n),
                  (unsigned long)g_audio_made, (unsigned long)g_audio_dropped);
    g_drawn_frames = 0;
    g_audio_made = 0;
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
      g_next_frame_us = micros();
    }
    runFrame(now_ms);

    // Sleep the slack rather than returning and letting the main loop spin.
    // Every spin costs an M5.update(), which polls touch over I2C — returning
    // early burned several ms a frame on the very wait it was performing. A
    // pad only needs sampling once a frame, so one loop iteration per frame is
    // exactly right.
    // Pace to 60 fps, but never accumulate debt. The previous version added a
    // frame period unconditionally, so when frames genuinely took longer than
    // 16.7 ms the target fell steadily behind real time until it was a whole
    // 66 ms adrift — then resynced and slept an entire frame. It oscillated
    // between racing and stalling, and that stall was most of the mystery
    // 6 ms a frame. If there is slack, sleep it; if we are late, start the
    // next frame now and forget the debt.
    const int32_t slack = (int32_t)(g_next_frame_us + kFrameUs - micros());
    if (slack > 1000) {
      g_next_frame_us += kFrameUs;
      delay((uint32_t)slack / 1000);  // yields, so the APU task keeps running
    } else {
      g_next_frame_us = micros();
    }
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
    case Action::PageUp:
      audio::select();
      g_pick_scroll -= kPickRows;
      g_dirty = true;
      break;

    case Action::PageDown:
      audio::select();
      g_pick_scroll += kPickRows;
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
