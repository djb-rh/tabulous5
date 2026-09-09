#include "snes_ui.h"

#if !defined(HAVE_SNES)

// The core was not fetched, so there is no SNES. Everything else builds and
// behaves exactly as it would have.
namespace tabulous {
namespace snes_ui {
bool available() { return false; }
void reserveWorkRamEarly() {}
void begin() {}
void rescan() {}
void invalidate() {}
void tick(uint32_t) {}
void handleTap(int, int, uint32_t) {}
bool playing() { return false; }
}  // namespace snes_ui
}  // namespace tabulous

#else

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include <cstdio>
#include <cstring>

// The core is C, and old enough that its headers assume they are the only
// thing in the translation unit.
extern "C" {
#include "snes9x.h"
#include "memmap.h"
#include "apu.h"
#include "cpuexec.h"
#include "gfx.h"
#include "ppu.h"
#include "soundux.h"
}
#undef MIN
#undef MAX

// Things the core declares and expects its frontend to define. The timing
// three are the "overclock" knob: left off, the core uses the real machine's
// cycle counts, which is what we want.
extern "C" {
bool overclock_cycles = false;
int one_c = 6, slow_one_c = 8, two_c = 12;

// Input devices this console does not have. The core asks about them whether
// or not anything is plugged in.
bool JustifierOffscreen(void) { return true; }
void JustifierButtons(uint32_t *) {}
bool S9xReadMousePosition(int32_t, int32_t *, int32_t *, uint32_t *) { return false; }
bool S9xReadSuperScopePosition(int32_t *, int32_t *, uint32_t *) { return false; }
// The pad is written straight into IPPU.Joypads once a frame, so nothing is
// read back through here.
uint32_t S9xReadJoypad(int32_t) { return 0; }
}

#include "app.h"
#include "audio.h"
#include "emu_video.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "padmap.h"
#include "rom_browser.h"
#include "settings_store.h"
#include "theme.h"
#include "uikit.h"
#include "usbpad.h"

namespace tabulous {
namespace snes_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::gfx;

constexpr const char *kRomDir = "/snes";

// What the core may render into, rather than what is shown: the hi-res modes
// double the width, and a game may open the picture up to 239 lines. Sizing
// for the worst case is what keeps a mode change from writing past the end.
constexpr int kMaxW = 512, kMaxH = SNES_HEIGHT_EXTENDED;
constexpr int kShownW = SNES_WIDTH, kShownH = SNES_HEIGHT;
constexpr uint32_t kMaxRomBytes = 4u * 1024u * 1024u;

enum class Mode : uint8_t { Picking, Playing };
Mode g_mode = Mode::Picking;
bool g_dirty = true;

settings_store::SnesSettings g_settings;
padmap::Map g_padmap;
int g_quit_hold = 0;
int g_scale = 2;
bool g_running = false;

uint16_t *g_screen = nullptr, *g_subscreen = nullptr;
uint8_t *g_zbuf = nullptr, *g_subzbuf = nullptr;

// Battery-backed cartridge RAM, saved beside the ROM as <name>.srm — the
// name every desktop emulator uses, so a save can be carried off the card.
fs::FS *g_save_fs = nullptr;
char g_save_path[200] = "";
uint32_t g_sram_len = 0;
uint32_t g_saved_at = 0;
bool g_sram_dirty = false;

// The SNES runs at 60.098 Hz and its APU at 32 kHz. Playing back at exactly
// the rate this loop produces keeps the queue from drifting, as on the Game
// Boy.
constexpr uint32_t kFrameUs = 16639;
constexpr int kAudioFrames = 532;
constexpr uint32_t kAudioRate = kAudioFrames * 60;
constexpr int kAudioChannel = 1;
constexpr int kAudioBufs = 4;
int16_t *g_audio[kAudioBufs] = {nullptr};
int16_t *g_mix = nullptr;  // stereo, straight from the core
int g_audio_which = 0;

uint8_t g_pad = 0, g_pad_drawn = 0xFF;
bool g_menu_down = false;
uint32_t g_next_frame_us = 0;
uint32_t g_frames = 0, g_fps_at = 0, g_fps = 0;
uint32_t g_us_emu = 0, g_us_skip = 0, g_us_copy = 0, g_us_push = 0;
uint32_t g_us_mix = 0, g_us_frame = 0;
uint32_t g_drawn = 0;

// The core asks for its 128 KB of work RAM in internal memory and falls back
// to PSRAM, and that fallback costs about a third of the frame. Internal
// memory has the room, but by the time a cartridge is picked the largest
// single block is a few kilobytes short of 128 KB -- so the block is claimed
// on the way into this screen, while the heap is still whole, and handed
// straight back the instant before the core asks for it.
uint8_t *g_wram_reserve = nullptr;

void reserveWorkRam() {
  if (g_wram_reserve) return;
  g_wram_reserve = (uint8_t *)heap_caps_malloc(0x20000,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void releaseWorkRamReserve() {
  if (!g_wram_reserve) return;
  heap_caps_free(g_wram_reserve);
  g_wram_reserve = nullptr;
}

void *psram(size_t bytes) {
  return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ---- battery saves --------------------------------------------------------

void loadSram() {
  if (!g_save_fs || !g_sram_len || !g_save_path[0]) return;
  File f = g_save_fs->open(g_save_path, "r");
  if (!f) return;
  const size_t got = f.read(::Memory.SRAM, g_sram_len);
  f.close();
  Serial.printf("snes: loaded %u bytes of save\n", (unsigned)got);
}

void writeSram() {
  if (!g_save_fs || !g_sram_len || !g_save_path[0] || !g_sram_dirty) return;
  File f = g_save_fs->open(g_save_path, "w");
  if (!f) {
    Serial.println("snes: could not write the save file");
    return;
  }
  const size_t put = f.write(::Memory.SRAM, g_sram_len);
  f.close();
  g_sram_dirty = put != g_sram_len;
  g_saved_at = millis();
}

// The core gives no signal that cartridge RAM changed, so the save is
// compared against what was last written rather than guessed at.
uint32_t sramFingerprint() {
  if (!::Memory.SRAM || !g_sram_len) return 0;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < g_sram_len; i += 64) {
    h = (h ^ ::Memory.SRAM[i]) * 16777619u;
  }
  return h;
}
uint32_t g_sram_seen = 0;

// ---- lifecycle ------------------------------------------------------------

void releaseCore() {
  if (!g_running) return;
  writeSram();
  emu_video::waitIdle();
  M5.Speaker.stop();
  S9xDeinitGFX();
  S9xDeinitAPU();
  S9xDeinitMemory();
  g_running = false;
  g_save_fs = nullptr;
  g_save_path[0] = '\0';
  g_sram_len = 0;
  g_sram_dirty = false;
}

// Whether the core will take this cartridge. The header sits at a different
// place in LoROM and HiROM images and the only way to tell them apart is to
// score both, which the core does at load time — so this is deliberately
// shallow: the size is checked, and the rest is left to the core.
void probeRom(fs::FS &fs, rom_index::Item *it) {
  File h = fs.open(it->path, "r");
  it->bytes = h ? (uint32_t)h.size() : 0;
  if (h) h.close();

  if (it->bytes == 0) {
    it->status = 1;
    it->problem = "unreadable";
  } else if (it->bytes > kMaxRomBytes + 512) {
    it->status = 1;
    it->problem = "larger than 4 MB";
  } else {
    it->status = 0;
    it->problem = "";
  }
}

// The core wants its 128 KB of work RAM in internal memory and falls back to
// PSRAM, which costs about a third of the frame. Internal memory has the room
// but only just, so nothing else may take an internal block before the core
// has had its turn: the audio buffers below are claimed afterwards.
bool allocateVideoBuffers() {
  if (!g_screen) g_screen = (uint16_t *)psram((size_t)kMaxW * kMaxH * 2);
  if (!g_subscreen) g_subscreen = (uint16_t *)psram((size_t)kMaxW * kMaxH * 2);
  if (!g_zbuf) g_zbuf = (uint8_t *)psram((size_t)kMaxW * kMaxH);
  if (!g_subzbuf) g_subzbuf = (uint8_t *)psram((size_t)kMaxW * kMaxH);
  return g_screen && g_subscreen && g_zbuf && g_subzbuf;
}

bool allocateAudioBuffers() {
  if (!g_mix) {
    g_mix = (int16_t *)heap_caps_malloc(kAudioFrames * 2 * sizeof(int16_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  for (int i = 0; i < kAudioBufs; i++) {
    if (!g_audio[i]) {
      g_audio[i] = (int16_t *)heap_caps_malloc(kAudioFrames * sizeof(int16_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!g_audio[i]) return false;
  }
  return g_mix != nullptr;
}

bool load(int index) {
  releaseCore();
  rom_browser::probeItem(index);
  const rom_index::Item &e = rom_browser::item(index);
  if (e.status != 0) {
    rom_browser::setError(e.problem);
    return false;
  }
  if (!allocateVideoBuffers()) {
    rom_browser::setError("out of memory for the core");
    return false;
  }

  releaseWorkRamReserve();
  memset(&::Settings, 0, sizeof(::Settings));
  ::Settings.CyclesPercentage = 100;
  ::Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
  ::Settings.HBlankStart = (256 * ::Settings.H_Max) / SNES_HCOUNTER_MAX;
  ::Settings.FrameTimeNTSC = 16667;
  ::Settings.FrameTimePAL = 20000;
  ::Settings.ControllerOption = SNES_JOYPAD;
  ::Settings.SoundPlaybackRate = kAudioRate;
  ::Settings.DisableSoundEcho = false;
  ::Settings.InterpolatedSound = true;

  if (!S9xInitMemory() || !S9xInitAPU() || !S9xInitSound(0, 0)) {
    rom_browser::setError("core would not start");
    S9xDeinitMemory();
    return false;
  }
  g_running = true;
  Serial.printf("snes: work RAM in %s\n",
                esp_ptr_internal(::Memory.RAM) ? "internal SRAM" : "PSRAM");
  if (!allocateAudioBuffers()) {
    rom_browser::setError("out of memory for sound");
    releaseCore();
    return false;
  }

  // The core reads these when it works out its own offsets, so they are set
  // before S9xInitGFX rather than after.
  ::GFX.Pitch = kMaxW * 2;
  ::GFX.ZPitch = kMaxW;
  ::GFX.Screen = (uint8_t *)g_screen;
  ::GFX.SubScreen = (uint8_t *)g_subscreen;
  ::GFX.ZBuffer = g_zbuf;
  ::GFX.SubZBuffer = g_subzbuf;
  if (!S9xInitGFX()) {
    rom_browser::setError("core would not start");
    releaseCore();
    return false;
  }
  S9xSetPlaybackRate(kAudioRate);

  const uint32_t t0 = millis();
  fs::FS &fs = rom_browser::fsFor(e);
  File f = fs.open(e.path, "r");
  if (!f) {
    rom_browser::setError("could not open the file");
    releaseCore();
    return false;
  }
  size_t size = f.size();
  if (size > kMaxRomBytes + 512) size = kMaxRomBytes + 512;
  if (!::Memory.ROM) ::Memory.ROM = (uint8_t *)psram(kMaxRomBytes + 512);
  if (!::Memory.ROM) {
    f.close();
    rom_browser::setError("out of memory for the cartridge");
    releaseCore();
    return false;
  }
  size_t got = 0;
  while (got < size) {
    const size_t n = f.read(::Memory.ROM + got, size - got);
    if (!n) break;
    got += n;
  }
  f.close();
  if (got != size) {
    rom_browser::setError("could not read the cartridge");
    releaseCore();
    return false;
  }
  // A copier header is 512 bytes on the front of an otherwise round size.
  // The core spots one but does not remove it, so it goes here.
  if ((got & 0x7FF) == 512) {
    memmove(::Memory.ROM, ::Memory.ROM + 512, got - 512);
    got -= 512;
  }
  ::Memory.ROM_Size = got;

  if (!LoadROM(nullptr)) {
    rom_browser::setError("core did not recognise this cartridge");
    releaseCore();
    return false;
  }
  Serial.printf("snes: %s %s %s, %lu KB, read in %lu ms\n", ::Memory.ROMName,
                ::Memory.HiROM ? "HiROM" : "LoROM", ::Settings.PAL ? "PAL" : "NTSC",
                (unsigned long)(got / 1024), (unsigned long)(millis() - t0));
  if (::Settings.SuperFX) {
    // fxstub.c stands in for the GSU interpreter, so these load and run but
    // show nothing. Better to say so than to leave a black screen.
    rom_browser::setError("Super FX cartridges are not supported");
    releaseCore();
    return false;
  }

  g_sram_len = ::Memory.SRAMMask ? ::Memory.SRAMMask + 1 : 0;
  if (g_sram_len > 128 * 1024) g_sram_len = 128 * 1024;
  if (g_sram_len && ::Memory.SRAM) {
    g_save_fs = &fs;
    snprintf(g_save_path, sizeof(g_save_path), "%s.srm", e.path);
    loadSram();
  }
  g_sram_dirty = false;
  g_sram_seen = sramFingerprint();
  g_saved_at = millis();

  // No S9xReset() here: LoadROM has already put the machine in its reset
  // state, and resetting again on top of that hangs the first frame.

  g_scale = rom_browser::scale();
  if (!emu_video::begin() || !emu_video::configure(kShownW, kShownH, g_scale)) {
    rom_browser::setError("no room for the picture");
    releaseCore();
    return false;
  }

  g_pad = 0;
  g_pad_drawn = 0xFF;
  g_quit_hold = 0;
  g_frames = 0;
  g_fps_at = millis();
  g_next_frame_us = micros();
  return true;
}

// ---- play -----------------------------------------------------------------

void drawPlayChrome(bool full) {
  if (full) {
    gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_scale >= 3) {
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

uint32_t toSnes(uint8_t pad, const usbpad::State &u) {
  uint32_t out = 0;
  if (pad & joypad::kUp) out |= SNES_UP_MASK;
  if (pad & joypad::kDown) out |= SNES_DOWN_MASK;
  if (pad & joypad::kLeft) out |= SNES_LEFT_MASK;
  if (pad & joypad::kRight) out |= SNES_RIGHT_MASK;
  if (pad & joypad::kA) out |= SNES_A_MASK;
  if (pad & joypad::kB) out |= SNES_B_MASK;
  if (pad & joypad::kSelect) out |= SNES_SELECT_MASK;
  if (pad & joypad::kStart) out |= SNES_START_MASK;
  // The on-screen pad has no X, Y or shoulders; a real one does, and this is
  // the machine that needs them.
  if (u.connected) {
    if ((u.down >> 0) & 1u) out |= SNES_X_MASK;   // GP100 X
    if ((u.down >> 3) & 1u) out |= SNES_Y_MASK;   // GP100 Y
    if ((u.down >> 4) & 1u) out |= SNES_TL_MASK;  // L
    if ((u.down >> 5) & 1u) out |= SNES_TR_MASK;  // R
  }
  return out;
}

// The core renders into a buffer sized for the worst case; this lifts the
// part that is actually being shown into the frame the panel will get.
void copyOut() {
  uint16_t *dst = emu_video::frame();
  if (!dst) return;
  const int src_w = ::IPPU.RenderedScreenWidth > 0 ? ::IPPU.RenderedScreenWidth : kShownW;
  const int src_h = ::IPPU.RenderedScreenHeight > 0 ? ::IPPU.RenderedScreenHeight : kShownH;
  const int rows = src_h < kShownH ? src_h : kShownH;
  for (int y = 0; y < rows; y++) {
    const uint16_t *src = g_screen + (size_t)y * kMaxW;
    uint16_t *out = dst + (size_t)y * kShownW;
    if (src_w >= 2 * kShownW) {
      // A hi-res mode: show every other column rather than half the picture.
      for (int x = 0; x < kShownW; x++) out[x] = src[x * 2];
    } else {
      memcpy(out, src, (size_t)kShownW * 2);
    }
  }
  if (rows < kShownH) {
    memset(dst + (size_t)rows * kShownW, 0, (size_t)(kShownH - rows) * kShownW * 2);
  }
}

void runFrame(uint32_t now_ms) {
  const uint32_t t_frame = micros();
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_scale >= 3) g_pad = 0;
  const usbpad::State u = usbpad::state();
  if (u.connected) g_pad |= padmap::toNes(u.down, u.x, u.y, g_padmap);
  if ((g_pad & (joypad::kSelect | joypad::kStart)) == (joypad::kSelect | joypad::kStart)) {
    if (++g_quit_hold >= 40) menu_held = true;
  } else {
    g_quit_hold = 0;
  }
  if (menu_held && !g_menu_down) {
    g_menu_down = true;
    releaseCore();
    g_mode = Mode::Picking;
    reserveWorkRam();  // ready for the next cartridge
    rom_browser::invalidate();
    return;
  }
  if (!menu_held) g_menu_down = false;

  ::IPPU.Joypads[0] = toSnes(g_pad, u);

  // Every frame is emulated; only every other one is drawn. The picture is
  // where the time goes -- the machine's own logic and its sound run at full
  // speed either way, which is what keeps a game playing and sounding right.
  static bool render = true;
  const uint32_t t_emu = micros();
  ::IPPU.RenderThisFrame = render;
  S9xMainLoop();
  const uint32_t took = micros() - t_emu;
  if (render) {
    g_us_emu += took;
    g_drawn++;
  } else {
    g_us_skip += took;
  }

  if (render) {
    const uint32_t t_copy = micros();
    copyOut();
    g_us_copy += micros() - t_copy;
    emu_video::present();
    g_us_push += emu_video::lastUs();
  }
  render = !render;

  const uint32_t t_mix = micros();
  S9xMixSamples(g_mix, kAudioFrames * 2);
  g_us_mix += micros() - t_mix;
  if (audio::enabled() && M5.Speaker.isPlaying(kAudioChannel) < 2) {
    int16_t *out = g_audio[g_audio_which];
    for (int i = 0; i < kAudioFrames; i++) {
      out[i] = (int16_t)((g_mix[2 * i] + g_mix[2 * i + 1]) / 2);
    }
    M5.Speaker.playRaw(out, (size_t)kAudioFrames, kAudioRate, false, 1, kAudioChannel);
    g_audio_which = (g_audio_which + 1) % kAudioBufs;
  }

  drawPlayChrome(false);

  // Cartridge RAM is checked once a second rather than on every write, and
  // written ten seconds after it last changed.
  if (g_sram_len && now_ms - g_fps_at >= 1000) {
    const uint32_t now = sramFingerprint();
    if (now != g_sram_seen) {
      g_sram_seen = now;
      g_sram_dirty = true;
      g_saved_at = now_ms;
    } else if (g_sram_dirty && now_ms - g_saved_at > 10000) {
      writeSram();
    }
  }

  g_us_frame += micros() - t_frame;
  g_frames++;
  if (now_ms - g_fps_at >= 1000) {
    g_fps = g_frames * 1000 / (now_ms - g_fps_at);
    const uint32_t n = g_fps ? g_fps : 1;
    const uint32_t d = g_drawn ? g_drawn : 1;
    const uint32_t sk = (n > g_drawn) ? n - g_drawn : 1;
    Serial.printf("snes %lu emu/s %lu drawn/s  drawn=%luus skipped=%luus copy=%luus push=%luus mix=%luus frame=%luus\n",
                  (unsigned long)g_fps, (unsigned long)g_drawn,
                  (unsigned long)(g_us_emu / d), (unsigned long)(g_us_skip / sk),
                  (unsigned long)(g_us_copy / d), (unsigned long)(g_us_push / d),
                  (unsigned long)(g_us_mix / n), (unsigned long)(g_us_frame / n));
    g_frames = 0;
    g_drawn = 0;
    g_fps_at = now_ms;
    g_us_emu = g_us_skip = g_us_copy = g_us_push = g_us_mix = g_us_frame = 0;
  }
}

}  // namespace

bool available() { return true; }

void reserveWorkRamEarly() {
  reserveWorkRam();
  Serial.printf("snes: work RAM reserved=%d\n", g_wram_reserve != nullptr);
}

void begin() {
  releaseCore();
  g_mode = Mode::Picking;
  settings_store::loadSnes(&g_settings);
  settings_store::loadPadMap(&g_padmap);
  g_quit_hold = 0;

  rom_browser::Config cfg;
  cfg.title = "SNES";
  cfg.dir = kRomDir;
  cfg.extension = ".sfc";
  cfg.favourites_file = kFavouritesFile;
  cfg.scale_label[0] = "2x  TOUCH";
  cfg.scale_label[1] = "3x  GAMEPAD";
  cfg.scale_value[0] = 2;
  cfg.scale_value[1] = 3;
  cfg.probe = probeRom;
  cfg.empty_hint = "Put .sfc files in /snes on the card";
  rom_browser::begin(cfg, g_settings.scale);
  rom_browser::ensureScanned();
  reserveWorkRam();  // no-op when the boot-time reservation still stands
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
  if (g_mode == Mode::Playing && g_running) {
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
  uikit::noteRepaint(micros() - t0, 73);
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
      settings_store::saveSnes(g_settings);
      break;
    case rom_browser::Result::Back:
      app::requestExit();
      break;
    case rom_browser::Result::None:
      break;
  }
}

}  // namespace snes_ui
}  // namespace tabulous

#endif  // HAVE_SNES
