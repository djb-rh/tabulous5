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
#include "fourway.h"
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

// Hundreds of games shipped on this one board, and MAME's driver lists sixty
// or so that run on it unmodified -- but most of those are bootleg reskins of
// Puck Man with the ghosts redrawn, and a list of them all is a wall of
// near-identical rows. These are the distinct games in it: one per title,
// best-dumped set of each. Starred the first time the Arcade list is opened,
// and never again, so unstarring one sticks.
constexpr const char *kDefaultFavourites =
    "Pac-Man (Midway).arc\n"
    "Ms. Pac-Man.arc\n"
    "Ms. Pac-Man (speedup hack).arc\n"
    "Puck Man (Japan, set 1).arc\n"
    "Crush Roller (set 2).arc\n"
    "Ponpoko.arc\n"
    "Eyes (US, set 1).arc\n"
    "Piranha.arc\n"
    "Mr. TNT.arc\n"
    "Naughty Mouse (set 1).arc\n"
    "Jump Shot.arc\n"
    "Pac-Man Plus.arc\n"
    "Ms. Pac-Man Plus.arc\n"
    "Lizard Wizard.arc\n"
    "The Glob (Pac-Man hardware, Magic Electronics).arc\n"
    "Shoot the Bull.arc\n"
    "Eggor.arc\n"
    "Gorkans.arc\n";

// The board draws a 288x224 raster and the cabinet's monitor stood on its
// side, so the picture is really 224 across and 288 down. It is turned upright
// while the palette is applied, which costs nothing extra — something has to
// walk every pixel either way.
//
// Which way the SCREEN goes is a separate choice. Held sideways the picture is
// a tall rectangle with the pad either side of it; turned upright it is far
// bigger, with the controls underneath, which is how a cabinet stands and how
// this sits in a controller mount.
//
// Ponpoko and its bootlegs are the exception: they ran on the same board with
// the monitor the usual way round, so for those the raster is the picture and
// turning it would be the bug. The .arc file says which.
constexpr int kRasterW = NAMCO_DISPLAY_WIDTH;   // 288
constexpr int kRasterH = NAMCO_DISPLAY_HEIGHT;  // 224

enum class Mode : uint8_t { Picking, Dips, Playing };

// The board's DIP switches. These are real switches on a real board: the
// machine reads them when a game starts, so changing one takes effect at the
// next credit rather than immediately.
struct Dip {
  const char *name;
  uint8_t mask;
  uint8_t shift;
  uint8_t count;
  const char *values[4];
};
constexpr Dip kDips[] = {
    {"Coins", 0x03, 0, 4, {"Free play", "1 coin, 1 game", "1 coin, 2 games", "2 coins, 1 game"}},
    {"Lives", 0x0C, 2, 4, {"1", "2", "3", "5"}},
    {"Bonus life", 0x30, 4, 4, {"10,000", "15,000", "20,000", "None"}},
    {"Difficulty", 0x40, 6, 2, {"Hard", "Normal", "", ""}},
    {"Ghost names", 0x80, 7, 2, {"Alternate", "Normal", "", ""}},
};
constexpr int kDipCount = (int)(sizeof(kDips) / sizeof(kDips[0]));
Mode g_mode = Mode::Picking;
bool g_dirty = true;

settings_store::ArcadeSettings g_settings;
padmap::Map g_padmap;
// The plate under a four-way stick, which every one of these cabinets but
// three had. See fourway.h: without it, turning a corner means letting go of
// the old direction before pressing the new one, exactly, every time.
fourway::Gate g_gate(joypad::kUp, joypad::kDown, joypad::kLeft, joypad::kRight);
bool g_eight_way = false;
// Whether this cabinet's control panel leaves the gamepad's face buttons with
// nothing to do. When it does, they become a second stick -- see padmap.h.
bool g_buttons_as_stick = false;
int g_quit_hold = 0;
float g_scale = 2.0f;
bool g_portrait = true;
bool g_stand_up = true;  // the board's monitor was on its side
int g_shown_w = kRasterH;
int g_shown_h = kRasterW;

namco_t *g_sys = nullptr;
// The Ms. Pac-Man kit's own copy of the program: 16K at 0x0000 and 16K at
// 0x8000, held apart from the board's so the add-on can swap between them.
uint8_t *g_alt_rom = nullptr;
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
  // header, so a short read of the head is enough to judge it by -- as long as
  // the length it is told is the real one when the file is short.
  const size_t claimed = arcrom::fileBytes(head, got);
  arcrom::Info info;
  const char *why = "";
  if (got < arcrom::kV1HeaderBytes ||
      !arcrom::parse(head, it->bytes < claimed ? it->bytes : claimed, &info, &why)) {
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

  // Games differ in size -- a board with a second set of program ROMs carries
  // up to 16K more -- so read whatever is there, up to the largest we take.
  uint8_t *rom = (uint8_t *)heap_caps_malloc(arcrom::kMaxFileBytes, MALLOC_CAP_SPIRAM);
  if (!rom) {
    rom_browser::setError("out of memory");
    return false;
  }
  File f = rom_browser::fsFor(e).open(e.path, "r");
  size_t got = 0;
  while (f && got < arcrom::kMaxFileBytes) {
    const size_t n = f.read(rom + got, arcrom::kMaxFileBytes - got);
    if (!n) break;
    got += n;
  }
  if (f) f.close();

  arcrom::Info info;
  const char *why = "";
  if (!arcrom::parse(rom, got, &info, &why)) {
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

  // A 48K board answers at 0x8000 as well. namco_init leaves the upper half of
  // its program ROM alone on this board, which is exactly the room needed, so
  // the extra chips go there and survive the file being freed below.
  namco_fast_desc_t board = {};
  if (info.daughtercard) {
    // Two copies of the program have to be resident at once, and the CPU
    // fetches from whichever is switched in, so this wants internal memory as
    // much as the machine itself does.
    const size_t alt_bytes = arcrom::kCpuBytes + info.cpu_high_bytes;
    if (!g_alt_rom) {
      g_alt_rom = (uint8_t *)heap_caps_malloc(
          arcrom::kCpuBytes + arcrom::kMaxCpuHighBanks * arcrom::kCpuHighBank,
          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (!g_alt_rom) {
        g_alt_rom = (uint8_t *)heap_caps_malloc(
            arcrom::kCpuBytes + arcrom::kMaxCpuHighBanks * arcrom::kCpuHighBank,
            MALLOC_CAP_SPIRAM);
      }
    }
    if (!g_alt_rom) {
      heap_caps_free(rom);
      releaseCore();
      rom_browser::setError("out of memory for the Ms. Pac-Man board");
      return false;
    }
    memcpy(g_alt_rom, rom + info.alt_cpu, alt_bytes);
    board.rom_alt_low = g_alt_rom;
    board.rom_alt_high = g_alt_rom + arcrom::kCpuBytes;
    Serial.printf("arcade: Ms. Pac-Man kit fitted, second program in %s\n",
                  esp_ptr_internal(g_alt_rom) ? "internal SRAM" : "PSRAM");
  }
  if (info.cpu_high_bytes) {
    memcpy(&g_sys->rom_cpu[0x4000], rom + info.cpu_high, info.cpu_high_bytes);
    board.rom_high = &g_sys->rom_cpu[0x4000];
    board.rom_high_bytes = (uint32_t)info.cpu_high_bytes;
  }
  board.vector_count = info.vector_fixups;
  for (size_t i = 0; i < info.vector_fixups; i++) {
    board.vector_from[i] = info.vector_from[i];
    board.vector_to[i] = info.vector_to[i];
  }
  namco_fast_init(g_sys, &g_cpu, &board);
  g_sys->dsw1 = g_settings.dsw1;
  // namco_init copies every ROM into the machine, so the file goes now.
  heap_caps_free(rom);

  // The hardware colours never change once the palette PROM is decoded.
  for (int i = 0; i < 32; i++) {
    const uint32_t c = g_sys->hw_colors[i];
    g_palette[i] = rgb(((c & 0xFF) << 16) | (((c >> 8) & 0xFF) << 8) | ((c >> 16) & 0xFF));
  }

  g_eight_way = info.eight_way;
  g_buttons_as_stick = !info.eight_way && !info.uses_button;
  g_gate.reset();
  g_stand_up = info.upright_monitor;
  g_shown_w = g_stand_up ? kRasterH : kRasterW;
  g_shown_h = g_stand_up ? kRasterW : kRasterH;
  g_portrait = g_settings.portrait;
  const bool big = g_settings.scale == 5;
  if (g_stand_up) {
    // A standing raster is 224 across. Sideways, 2.5x fills the panel's height
    // exactly and 2x leaves room for the pad either side. Turned upright there
    // is far more height to play with, and 3x is as wide as 720 will take.
    g_scale = g_portrait ? (big ? 3.0f : 2.5f) : (big ? 2.5f : 2.0f);
  } else {
    // Ponpoko's picture is 288 across instead, so the same numbers overrun.
    // Sideways with the on-screen pad there are 553 pixels between the pad and
    // the buttons, and 1.75x is the largest that clears both; a gamepad frees
    // the whole width. Upright, 2.5x is exactly the panel's 720.
    g_scale = g_portrait ? (big ? 2.5f : 2.0f) : (big ? 2.5f : 1.75f);
  }
  joypad_ui::setLabels("COIN", "1P START", "2P", nullptr);
  if (!joypad_ui::beginPlay(g_portrait, g_shown_w, g_shown_h, g_scale)) {
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
  if (!g_stand_up) {
    // Ponpoko's monitor stood the usual way round: straight through.
    for (int row = 0; row < kRasterH; row++) {
      const uint8_t *src = g_sys->fb + (size_t)row * NAMCO_FRAMEBUFFER_WIDTH;
      uint16_t *out = dst + (size_t)row * kRasterW;
      for (int col = 0; col < kRasterW; col++) out[col] = g_palette[src[col] & 31];
    }
    return;
  }
  for (int row = 0; row < kRasterH; row++) {
    const uint8_t *src = g_sys->fb + (size_t)row * NAMCO_FRAMEBUFFER_WIDTH;
    uint16_t *out = dst + (kRasterH - 1 - row);
    for (int col = 0; col < kRasterW; col++) {
      *out = g_palette[src[col] & 31];
      out += kRasterH;
    }
  }
}

// The switch panel. One row each, tapped to step through the settings the
// board offers — no more and no less than the real thing has.
void drawDips() {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::drawLabel("DIP SWITCHES", kMargin, 46, kText, &fonts::FreeSansBold24pt7b,
                   middle_left);
  // ASCII only: these fonts have no dash of any width, and anything else
  // comes out as an empty box.
  uikit::drawLabel("as on the board. Read when a game starts.", kMargin, 84, kMuted,
                   &fonts::FreeSans12pt7b, middle_left);

  const Rect done{kW - kMargin - 190, 18, 190, 62};
  uikit::drawButton(done, "DONE", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(done, 100, 0);

  const int row_h = 92, gap = 10, top = 118;
  for (int i = 0; i < kDipCount; i++) {
    const Dip &d = kDips[i];
    const Rect row{kMargin, top + i * (row_h + gap), kW - 2 * kMargin, row_h};
    uikit::fillRoundRectFast(row.x, row.y, row.w, row.h, 12, kSurfaceLift);
    uikit::drawLabel(d.name, row.x + 28, row.y + row_h / 2, kText,
                     &fonts::FreeSansBold18pt7b, middle_left);
    const int v = (g_settings.dsw1 & d.mask) >> d.shift;
    uikit::drawLabel(d.values[v], row.x + row.w - 32, row.y + row_h / 2, kAccent,
                     &fonts::FreeSansBold18pt7b, middle_right);
    uikit::addTarget(row, 200 + i, 0);
  }
}

void drawPlayChrome(bool full) {
  if (full) {
    gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_settings.scale == 5) {
    // No on-screen pad at this size: the picture takes the full height
    // and the margins hold the way out and which gamepad is playing.
    joypad_ui::drawGamepadChrome(full);
    g_pad_drawn = g_pad;
    return;
  }
  joypad_ui::drawControls(g_pad, g_pad_drawn, full);
  g_pad_drawn = g_pad;
}

// A cabinet has a four-way stick, a coin slot and two start buttons. SELECT is
// the coin, because inserting one is what you do first.
//
// The second start button is worth having even on a console nobody plays two
// players on: several of these games read it during play. Ms. Pac-Man Plus
// puts its speed-up there, and invincibility on the first start button, which
// is how that hack was meant to be switched on -- there is no DIP switch for
// either, on any of these boards.
uint32_t toCabinet(uint8_t pad) {
  uint32_t m = 0;
  if (pad & joypad::kUp) m |= NAMCO_INPUT_P1_UP;
  if (pad & joypad::kDown) m |= NAMCO_INPUT_P1_DOWN;
  if (pad & joypad::kLeft) m |= NAMCO_INPUT_P1_LEFT;
  if (pad & joypad::kRight) m |= NAMCO_INPUT_P1_RIGHT;
  if (pad & joypad::kSelect) m |= NAMCO_INPUT_P1_COIN;
  if (pad & joypad::kStart) m |= NAMCO_INPUT_P1_START;
  if (pad & joypad::kA) m |= NAMCO_INPUT_P2_START;
  return m;
}

void runFrame(uint32_t now_ms) {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_settings.scale == 5) g_pad = 0;
  {
    const usbpad::State u = usbpad::state();
    if (u.connected) {
      const uint8_t from_pad = padmap::toNes(u.down, u.x, u.y, g_padmap);
      if (g_buttons_as_stick) {
        // They are the stick now, and only that: A is the cabinet's second
        // start button, and a diamond that also starts a two-player game every
        // time you turn right would be worse than no diamond at all. It is
        // still on the screen at the smaller size.
        g_pad |= (uint8_t)(from_pad & ~(joypad::kA | joypad::kB));
        g_pad |= padmap::toStick(u.down, g_padmap);
      } else {
        g_pad |= from_pad;
      }
    }
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

  // Through the plate first, so the board sees what a four-way stick could
  // actually have sent it.
  if (!g_eight_way) g_pad = g_gate.filter(g_pad);
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
  cfg.default_favourites = kDefaultFavourites;
  cfg.scale_label[0] = "2x  TOUCH";
  cfg.scale_label[1] = "FULL  GAMEPAD";
  cfg.scale_value[0] = 2;
  cfg.scale_value[1] = 5;  // the larger size, whichever way up it is
  cfg.orientable = true;
  cfg.extra_label = "DIPS";
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

namespace {
void onBrowser(rom_browser::Result r, int index);
}  // namespace

void tick(uint32_t now_ms) {
  if (g_mode == Mode::Dips) {
    if (!g_dirty) return;
    g_dirty = false;
    const uint32_t t0 = micros();
    uikit::clearTargets();
    drawDips();
    uikit::present();
    uikit::noteRepaint(micros() - t0, 75);
    return;
  }
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
  {
    // The list scrolls under the finger and delivers taps on release.
    int index = 0;
    onBrowser(rom_browser::tick(now_ms, &index), index);
    if (g_mode != Mode::Picking) return;  // it launched, or left
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
  if (g_mode == Mode::Dips) {
    int action = 0, param = 0;
    if (!uikit::findTarget(x, y, &action, &param)) return;
    if (action == 100) {  // DONE
      audio::select();
      settings_store::saveArcade(g_settings);
      g_mode = Mode::Picking;
      rom_browser::invalidate();
      return;
    }
    const int i = action - 200;
    if (i >= 0 && i < kDipCount) {
      audio::select();
      const Dip &d = kDips[i];
      const int next = (((g_settings.dsw1 & d.mask) >> d.shift) + 1) % d.count;
      g_settings.dsw1 = (uint8_t)((g_settings.dsw1 & ~d.mask) | (next << d.shift));
      g_dirty = true;
    }
    return;
  }
  int index = 0;
  onBrowser(rom_browser::handleTap(x, y, &index), index);
}

namespace {
// What the browser asked for, from a press or from tick().
void onBrowser(rom_browser::Result r, int index) {
  switch (r) {
    case rom_browser::Result::Launch:
      // Say the tap landed before disappearing to read the card: opening a
      // cartridge takes a couple of seconds and the list would otherwise sit
      // there looking as though nothing had happened.
      {
        char busy[96];
        snprintf(busy, sizeof(busy), "Loading %s...", rom_browser::item(index).name);
        rom_browser::showBusy(busy);
      }
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
    case rom_browser::Result::Extra:
      g_mode = Mode::Dips;
      g_dirty = true;
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
}  // namespace

}  // namespace pacman_ui
}  // namespace tabulous
