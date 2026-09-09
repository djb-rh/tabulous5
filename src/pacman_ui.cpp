#include "pacman_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include <cstdio>
#include <cstring>

// Declarations only: the implementation is compiled as C in
// third_party/chips/chips_impl.c, because these headers use designated
// initialisers in an order C++ rejects. Include order is the one the machine's
// own header asks for.
extern "C" {
#include "../third_party/chips/chips_common.h"
#include "../third_party/chips/z80.h"
#include "../third_party/chips/clk.h"
#include "../third_party/chips/mem.h"
#include "../third_party/chips/namco.h"
#include "../third_party/chips/namco_fast.h"
}

#include "app.h"
#include "arcrom.h"
#include "audio.h"
#include "emu_video.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "padmap.h"
#include "rom_browser.h"
#include "settings_store.h"
#include "snes_ui.h"
#include "theme.h"
#include "uikit.h"
#include "usbpad.h"

namespace tabulous {
namespace pacman_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::gfx;

constexpr const char *kRomDir = "/arcade";

// The board draws a 288x224 raster and the cabinet's monitor stood on its
// side, so the picture is really 224 across and 288 down. It is turned upright
// while the palette is applied, which costs nothing extra — something has to
// walk every pixel either way.
//
// Which way the SCREEN goes is a separate choice. Held sideways the picture is
// a tall rectangle with the pad either side of it; turned upright it is far
// bigger, with the controls underneath, which is how a cabinet stands and how
// this sits in a controller mount.
constexpr int kRasterW = NAMCO_DISPLAY_WIDTH;   // 288
constexpr int kRasterH = NAMCO_DISPLAY_HEIGHT;  // 224
constexpr int kShownW = kRasterH;               // 224
constexpr int kShownH = kRasterW;               // 288

enum class Mode : uint8_t { Picking, Playing };
Mode g_mode = Mode::Picking;
bool g_dirty = true;

settings_store::ArcadeSettings g_settings;
padmap::Map g_padmap;
int g_quit_hold = 0;
float g_scale = 2.0f;
bool g_portrait = true;

namco_t *g_sys = nullptr;
// The CPU lives beside the board rather than inside it: chips' own Z80 is
// stepped one clock at a time and costs twice the frame budget, so the board
// is driven by an instruction-stepped one instead. See namco_fast.h.
sz80 g_cpu;
uint16_t g_palette[32];

// The machine runs at 60 Hz and its sound chip is clocked from the same run,
// so a frame's worth of samples arrives inside namco_exec().
constexpr uint32_t kFrameUs = 16667;
constexpr int kAudioRate = 44100;
// Room for two frames' worth: the machine is driven by however long the last
// frame actually took, and on this hardware that is nearer a thirtieth of a
// second than a sixtieth.
constexpr int kAudioFrames = kAudioRate / 25;
constexpr int kAudioChannel = 1;
constexpr int kAudioBufs = 4;
int16_t *g_audio[kAudioBufs] = {nullptr};
int g_audio_which = 0;
volatile int g_audio_have = 0;  // samples waiting in the current buffer

uint8_t g_pad = 0, g_pad_drawn = 0xFF;
bool g_menu_down = false;
uint32_t g_next_frame_us = 0;
uint32_t g_frames = 0, g_fps_at = 0, g_fps = 0;
uint32_t g_us_emu = 0, g_us_convert = 0, g_us_push = 0;

// ---- what the machine asks of us -----------------------------------------

void onAudio(const float *samples, int num_samples, void *) {
  int16_t *out = g_audio[g_audio_which];
  if (!out) return;
  const int room = kAudioFrames - g_audio_have;
  const int n = num_samples < room ? num_samples : room;
  for (int i = 0; i < n; i++) {
    float v = samples[i];
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    out[g_audio_have + i] = (int16_t)(v * 32000.0f);
  }
  g_audio_have += n;
}

// ---- play ----------------------------------------------------------------

void releaseCore() {
  joypad_ui::endPlay();
  M5.Speaker.stop();
  if (g_sys) {
    heap_caps_free(g_sys);
    g_sys = nullptr;
  }
}

void probeRom(fs::FS &fs, rom_index::Item *it) {
  uint8_t head[arcrom::kHeaderBytes];
  File h = fs.open(it->path, "r");
  const size_t got = h ? h.read(head, sizeof(head)) : 0;
  it->bytes = h ? (uint32_t)h.size() : 0;
  if (h) h.close();

  // parse() wants the whole file's length to check it, but only reads the
  // header, so a short read of the head is enough to judge it by.
  arcrom::Info info;
  const char *why = "";
  if (got != sizeof(head) ||
      !arcrom::parse(head, it->bytes < arcrom::kFileBytes ? it->bytes : arcrom::kFileBytes,
                     &info, &why)) {
    it->status = 1;
    it->problem = why[0] ? why : "unreadable";
  } else {
    it->status = 0;
    it->problem = "";
  }
}

bool load(int index) {
  releaseCore();
  rom_browser::probeItem(index);
  const rom_index::Item &e = rom_browser::item(index);
  if (e.status != 0) {
    rom_browser::setError(e.problem);
    return false;
  }

  uint8_t *rom = (uint8_t *)heap_caps_malloc(arcrom::kFileBytes, MALLOC_CAP_SPIRAM);
  if (!rom) {
    rom_browser::setError("out of memory");
    return false;
  }
  File f = rom_browser::fsFor(e).open(e.path, "r");
  size_t got = 0;
  while (f && got < arcrom::kFileBytes) {
    const size_t n = f.read(rom + got, arcrom::kFileBytes - got);
    if (!n) break;
    got += n;
  }
  if (f) f.close();

  arcrom::Info info;
  const char *why = "";
  if (got != arcrom::kFileBytes || !arcrom::parse(rom, got, &info, &why)) {
    heap_caps_free(rom);
    rom_browser::setError(why[0] ? why : "could not read it");
    return false;
  }

  // The machine is mostly its own framebuffer, which its renderer fills a
  // byte at a time, and its Z80 is emulated tick by tick — so where this sits
  // decides the frame rate, not the clock speed of the board being emulated.
  // Internal memory is worth asking the SNES for its reservation back.
  snes_ui::yieldWorkRamReserve();
  g_sys = (namco_t *)heap_caps_calloc(1, sizeof(namco_t),
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!g_sys) g_sys = (namco_t *)heap_caps_calloc(1, sizeof(namco_t), MALLOC_CAP_SPIRAM);
  Serial.printf("arcade: machine is %u bytes, in %s (largest internal block %u)\n",
                (unsigned)sizeof(namco_t),
                g_sys && esp_ptr_internal(g_sys) ? "internal SRAM" : "PSRAM",
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  if (!g_sys) {
    heap_caps_free(rom);
    rom_browser::setError("out of memory for the machine");
    return false;
  }
  for (int i = 0; i < kAudioBufs; i++) {
    if (!g_audio[i]) {
      g_audio[i] = (int16_t *)heap_caps_malloc(kAudioFrames * sizeof(int16_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
  }

  namco_desc_t desc = {};
  desc.audio.callback.func = onAudio;
  desc.audio.sample_rate = kAudioRate;
  desc.audio.num_samples = 128;
  desc.roms.common.cpu_0000_0FFF = {rom + info.cpu + 0x0000, 0x1000};
  desc.roms.common.cpu_1000_1FFF = {rom + info.cpu + 0x1000, 0x1000};
  desc.roms.common.cpu_2000_2FFF = {rom + info.cpu + 0x2000, 0x1000};
  desc.roms.common.cpu_3000_3FFF = {rom + info.cpu + 0x3000, 0x1000};
  desc.roms.common.prom_0000_001F = {rom + info.palette, arcrom::kPaletteBytes};
  desc.roms.common.sound_0000_00FF = {rom + info.sound1, arcrom::kSoundBytes};
  desc.roms.common.sound_0100_01FF = {rom + info.sound2, arcrom::kSoundBytes};
  desc.roms.pacman.gfx_0000_0FFF = {rom + info.gfx + 0x0000, 0x1000};
  desc.roms.pacman.gfx_1000_1FFF = {rom + info.gfx + 0x1000, 0x1000};
  desc.roms.pacman.prom_0020_011F = {rom + info.colour, arcrom::kColourBytes};
  namco_init(g_sys, &desc);
  namco_fast_init(g_sys, &g_cpu);
  // namco_init copies every ROM into the machine, so the file goes now.
  heap_caps_free(rom);

  // The hardware colours never change once the palette PROM is decoded.
  for (int i = 0; i < 32; i++) {
    const uint32_t c = g_sys->hw_colors[i];
    g_palette[i] = rgb(((c & 0xFF) << 16) | (((c >> 8) & 0xFF) << 8) | ((c >> 16) & 0xFF));
  }

  // Sideways, 2.5x fills the panel's height exactly and 2x leaves room for the
  // pad either side. Turned upright there is far more height to play with, and
  // 3x is as wide as 720 pixels will take.
  g_portrait = g_settings.portrait;
  const bool big = g_settings.scale == 5;
  g_scale = g_portrait ? (big ? 3.0f : 2.5f) : (big ? 2.5f : 2.0f);
  if (!joypad_ui::beginPlay(g_portrait, kShownW, kShownH, g_scale)) {
    rom_browser::setError("no room for the picture");
    releaseCore();
    return false;
  }
  Serial.printf("arcade: %s running\n", e.name);

  g_pad = 0;
  g_pad_drawn = 0xFF;
  g_quit_hold = 0;
  g_audio_which = 0;
  g_audio_have = 0;
  g_frames = 0;
  g_fps_at = millis();
  g_next_frame_us = micros();
  return true;
}

// Palette indices to colour, standing the raster up on the way: a source row
// becomes a destination column. The source is read in order, which is what
// matters — it lives in PSRAM, and walking it down a column instead would cost
// far more than the strided writes do.
void convert() {
  uint16_t *dst = emu_video::frame();
  if (!dst) return;
  for (int row = 0; row < kRasterH; row++) {
    const uint8_t *src = g_sys->fb + (size_t)row * NAMCO_FRAMEBUFFER_WIDTH;
    uint16_t *out = dst + (kShownW - 1 - row);
    for (int col = 0; col < kRasterW; col++) {
      *out = g_palette[src[col] & 31];
      out += kShownW;
    }
  }
}

void drawPlayChrome(bool full) {
  if (full) {
    gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_settings.scale == 5) {
    if (full) {
      const joypad::Rect m = joypad::menuButton();
      uikit::drawButton(Rect{m.x, m.y, m.w, m.h}, "MENU", kSurfaceLift, kText,
                        &fonts::FreeSansBold12pt7b);
    }
    g_pad_drawn = g_pad;
    return;
  }
  joypad_ui::drawControls(g_pad, g_pad_drawn, full);
  g_pad_drawn = g_pad;
}

// A cabinet has a four-way stick, a coin slot and a start button. SELECT is
// the coin, because inserting one is what you do first.
uint32_t toCabinet(uint8_t pad) {
  uint32_t m = 0;
  if (pad & joypad::kUp) m |= NAMCO_INPUT_P1_UP;
  if (pad & joypad::kDown) m |= NAMCO_INPUT_P1_DOWN;
  if (pad & joypad::kLeft) m |= NAMCO_INPUT_P1_LEFT;
  if (pad & joypad::kRight) m |= NAMCO_INPUT_P1_RIGHT;
  if (pad & joypad::kSelect) m |= NAMCO_INPUT_P1_COIN;
  if (pad & joypad::kStart) m |= NAMCO_INPUT_P1_START;
  return m;
}

void runFrame(uint32_t now_ms) {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_settings.scale == 5) g_pad = 0;
  {
    const usbpad::State u = usbpad::state();
    if (u.connected) g_pad |= padmap::toNes(u.down, u.x, u.y, g_padmap);
  }
  if ((g_pad & (joypad::kSelect | joypad::kStart)) == (joypad::kSelect | joypad::kStart)) {
    if (++g_quit_hold >= 40) menu_held = true;
  } else {
    g_quit_hold = 0;
  }
  if (menu_held && !g_menu_down) {
    g_menu_down = true;
    releaseCore();
    g_mode = Mode::Picking;
    rom_browser::invalidate();
    return;
  }
  if (!menu_held) g_menu_down = false;

  const uint32_t wanted = toCabinet(g_pad);
  namco_input_set(g_sys, wanted);
  namco_input_clear(g_sys, ~wanted & 0x3FFF);

  // One frame of machine time per pass.
  g_audio_have = 0;
  const uint32_t t_emu = micros();
  namco_fast_exec(g_sys, &g_cpu, kFrameUs);
  g_us_emu += micros() - t_emu;

  const uint32_t t_conv = micros();
  convert();
  g_us_convert += micros() - t_conv;
  emu_video::present();
  g_us_push += emu_video::lastUs();

  if (audio::enabled() && g_audio_have > 0 && g_audio[g_audio_which] &&
      M5.Speaker.isPlaying(kAudioChannel) < 2) {
    M5.Speaker.playRaw(g_audio[g_audio_which], (size_t)g_audio_have, kAudioRate, false, 1,
                       kAudioChannel);
    g_audio_which = (g_audio_which + 1) % kAudioBufs;
  }

  drawPlayChrome(false);

  g_frames++;
  if (now_ms - g_fps_at >= 1000) {
    g_fps = g_frames * 1000 / (now_ms - g_fps_at);
    const uint32_t n = g_fps ? g_fps : 1;
    Serial.printf("arcade %lu fps  emulate=%luus convert=%luus push=%luus\n",
                  (unsigned long)g_fps, (unsigned long)(g_us_emu / n),
                  (unsigned long)(g_us_convert / n), (unsigned long)(g_us_push / n));
    g_frames = 0;
    g_fps_at = now_ms;
    g_us_emu = g_us_convert = g_us_push = 0;
  }
}

}  // namespace

void begin() {
  releaseCore();
  g_mode = Mode::Picking;
  settings_store::loadArcade(&g_settings);
  settings_store::loadPadMap(&g_padmap);
  g_quit_hold = 0;

  rom_browser::Config cfg;
  cfg.title = "ARCADE";
  cfg.dir = kRomDir;
  cfg.extension = ".arc";
  cfg.favourites_file = kFavouritesFile;
  cfg.scale_label[0] = "2x  TOUCH";
  cfg.scale_label[1] = "FULL  GAMEPAD";
  cfg.scale_value[0] = 2;
  cfg.scale_value[1] = 5;  // the larger size, whichever way up it is
  cfg.orientable = true;
  cfg.probe = probeRom;
  cfg.empty_hint = "Make .arc files with tools/mkarcade.py and put them in /arcade on the card";
  rom_browser::begin(cfg, g_settings.scale, g_settings.portrait);
  rom_browser::ensureScanned();
  g_dirty = true;
}

void rescan() { rom_browser::rescan(); }

void invalidate() {
  g_dirty = true;
  g_pad_drawn = 0xFF;
  rom_browser::invalidate();
}

bool playing() { return g_mode == Mode::Playing; }

void tick(uint32_t now_ms) {
  if (g_mode == Mode::Playing && g_sys) {
    if (g_dirty) {
      g_dirty = false;
      drawPlayChrome(true);
      g_next_frame_us = micros();
    }
    runFrame(now_ms);
    g_next_frame_us += kFrameUs;
    const int32_t slack = (int32_t)(g_next_frame_us - micros());
    if (slack > 1000) {
      delay((uint32_t)slack / 1000);
    } else if (slack < -(int32_t)(3 * kFrameUs)) {
      g_next_frame_us = micros();
    }
    return;
  }
  if (!rom_browser::dirty()) return;
  const uint32_t t0 = micros();
  uikit::clearTargets();
  rom_browser::draw();
  uikit::present();
  uikit::noteRepaint(micros() - t0, 74);
}

void handleTap(int x, int y, uint32_t) {
  if (g_mode == Mode::Playing) return;
  int index = 0;
  switch (rom_browser::handleTap(x, y, &index)) {
    case rom_browser::Result::Launch:
      if (load(index)) {
        g_mode = Mode::Playing;
        g_menu_down = true;
        g_dirty = true;
      } else {
        audio::reject();
      }
      break;
    case rom_browser::Result::ScaleChanged:
      g_settings.scale = rom_browser::scale();
      settings_store::saveArcade(g_settings);
      break;
    case rom_browser::Result::OrientationChanged:
      g_settings.portrait = rom_browser::portrait();
      settings_store::saveArcade(g_settings);
      break;
    case rom_browser::Result::Back:
      app::requestExit();
      break;
    case rom_browser::Result::None:
      break;
  }
}

}  // namespace pacman_ui
}  // namespace tabulous
