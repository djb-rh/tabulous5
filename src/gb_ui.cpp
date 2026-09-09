#include "gb_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------- the core
//
// Peanut-GB is a single header that emits its implementation where it is
// included, and it calls audio_read/audio_write by name when sound is on. So
// the APU and those two functions have to exist before it is included, and all
// of it has to sit outside our namespaces — a header cannot be included into
// one safely.
#define MINIGB_APU_AUDIO_FORMAT_S16SYS 1
extern "C" {
#include "../third_party/peanut_gb/minigb_apu.h"
}

static struct minigb_apu_ctx g_apu;

static uint8_t audio_read(uint16_t addr) { return minigb_apu_audio_read(&g_apu, addr); }
static void audio_write(uint16_t addr, uint8_t val) {
  minigb_apu_audio_write(&g_apu, addr, val);
}

#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#include "../third_party/peanut_gb/peanut_gb.h"

#include "app.h"
#include "audio.h"
#include "emu_video.h"
#include "gbrom.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "padmap.h"
#include "rom_browser.h"
#include "settings_store.h"
#include "theme.h"
#include "uikit.h"
#include "usbpad.h"

namespace tabulous {
namespace gb_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::gfx;

constexpr const char *kRomDir = "/gb";

enum class Mode : uint8_t { Picking, Playing };
Mode g_mode = Mode::Picking;
bool g_dirty = true;

settings_store::GbSettings g_settings;
padmap::Map g_padmap;
int g_quit_hold = 0;
int g_scale = 3;

struct gb_s *g_gb = nullptr;
uint8_t *g_rom = nullptr;
size_t g_rom_len = 0;
uint8_t *g_ram = nullptr;
size_t g_ram_len = 0;
char g_save_path[200] = "";
bool g_ram_dirty = false;
uint32_t g_saved_at = 0;

// The four shades, lightest first. Which machine you remember decides which
// of these looks right: the original's LCD was green, the Pocket's was grey,
// and the Light was backlit and much more vivid. The core only ever produces
// an index from 0 to 3, so this is the whole of the difference.
struct Shades {
  const char *name;
  uint16_t colour[4];
};
constexpr Shades kPalettes[] = {
    {"GREEN", {rgb(0x9BBC0F), rgb(0x8BAC0F), rgb(0x306230), rgb(0x0F380F)}},
    {"POCKET", {rgb(0xE0DBCD), rgb(0xA89F94), rgb(0x706B66), rgb(0x2B2B26)}},
    {"LIGHT", {rgb(0x00B581), rgb(0x009A71), rgb(0x00694A), rgb(0x004F3B)}},
    {"GREY", {rgb(0xFFFFFF), rgb(0xA9A9A9), rgb(0x545454), rgb(0x000000)}},
};
constexpr int kPaletteCount = (int)(sizeof(kPalettes) / sizeof(kPalettes[0]));
const uint16_t *g_palette = kPalettes[0].colour;

// The Game Boy's frame is 59.7275 Hz, not 60 — a fifth of a second an hour,
// which matters here because the APU's clock IS this loop: it hands over
// AUDIO_SAMPLES per frame however fast the frames come. Playing them back at
// their true rate (548 x 59.7275) is what keeps the queue from drifting.
constexpr uint32_t kFrameUs = 16743;
constexpr uint32_t kAudioRate = (uint32_t)(AUDIO_SAMPLES * 59.7275f);
constexpr int kAudioChannel = 1;
constexpr int kAudioBufs = 4;
int16_t *g_audio[kAudioBufs] = {nullptr};
int16_t g_mix[AUDIO_SAMPLES_TOTAL];
int g_audio_which = 0;

uint8_t g_pad = 0, g_pad_drawn = 0xFF;
bool g_menu_down = false;
uint32_t g_next_frame_us = 0;
uint32_t g_frames = 0, g_fps_at = 0, g_fps = 0;
uint32_t g_us_emu = 0, g_us_push = 0;

// ---- what the core asks of us --------------------------------------------

uint8_t romRead(struct gb_s *, const uint_fast32_t addr) {
  return addr < g_rom_len ? g_rom[addr] : 0xFF;
}

uint8_t ramRead(struct gb_s *, const uint_fast32_t addr) {
  return addr < g_ram_len ? g_ram[addr] : 0xFF;
}

void ramWrite(struct gb_s *, const uint_fast32_t addr, const uint8_t val) {
  if (addr >= g_ram_len) return;
  g_ram[addr] = val;
  g_ram_dirty = true;
}

// The core reports its own faults rather than trapping. None of them are worth
// stopping the console for; a game that does this is already misbehaving.
void gbError(struct gb_s *, const enum gb_error_e err, const uint16_t addr) {
  static uint32_t seen = 0;
  if (seen++ < 8) Serial.printf("gb: core error %d at %04X\n", (int)err, addr);
}

// One finished scanline, as indices into the palette. The core hands over the
// palette bits in the high nibble too, hence the mask.
void lcdLine(struct gb_s *, const uint8_t *pixels, const uint_fast8_t line) {
  uint16_t *fb = emu_video::frame();
  if (!fb || line >= LCD_HEIGHT) return;
  uint16_t *dst = fb + (size_t)line * LCD_WIDTH;
  for (int x = 0; x < LCD_WIDTH; x++) dst[x] = g_palette[pixels[x] & LCD_COLOUR];
}

// ---- battery saves --------------------------------------------------------
//
// Cartridge RAM lives beside the ROM as <name>.gb.sav, which is the format
// every desktop emulator uses, so a save can be carried off the card.

fs::FS *g_save_fs = nullptr;

void loadSave() {
  if (!g_save_fs || !g_ram || !g_save_path[0]) return;
  File f = g_save_fs->open(g_save_path, "r");
  if (!f) return;
  const size_t got = f.read(g_ram, g_ram_len);
  f.close();
  Serial.printf("gb: loaded %u bytes of save\n", (unsigned)got);
}

void writeSave() {
  if (!g_save_fs || !g_ram || !g_save_path[0] || !g_ram_dirty) return;
  File f = g_save_fs->open(g_save_path, "w");
  if (!f) {
    Serial.println("gb: could not write the save file");
    return;
  }
  const size_t put = f.write(g_ram, g_ram_len);
  f.close();
  g_ram_dirty = put != g_ram_len;
  g_saved_at = millis();
}

// ---- play -----------------------------------------------------------------

void releaseCore() {
  writeSave();
  emu_video::waitIdle();
  M5.Speaker.stop();
  if (g_gb) {
    heap_caps_free(g_gb);
    g_gb = nullptr;
  }
  if (g_rom) {
    heap_caps_free(g_rom);
    g_rom = nullptr;
  }
  if (g_ram) {
    heap_caps_free(g_ram);
    g_ram = nullptr;
  }
  g_rom_len = g_ram_len = 0;
  g_save_fs = nullptr;
  g_save_path[0] = '\0';
  g_ram_dirty = false;
}

// The one system-specific part of choosing a cartridge.
void probeRom(fs::FS &fs, rom_index::Item *it) {
  uint8_t head[0x150];
  File h = fs.open(it->path, "r");
  const size_t got = h ? h.read(head, sizeof(head)) : 0;
  it->bytes = h ? (uint32_t)h.size() : 0;
  if (h) h.close();

  gbrom::Info info;
  const char *why = "";
  const bool readable =
      got == sizeof(head) && gbrom::parseHeader(head, it->bytes, &info, &why);
  it->mapper = info.cart_type;
  if (!readable) {
    it->status = 1;
    it->problem = why[0] ? why : "unreadable";
  } else if (!info.supported) {
    it->status = 1;
    it->problem = "cartridge type not supported";
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

  // The whole cartridge goes into PSRAM and is read from there: on a card in
  // a folder of thousands, an open costs a tenth of a second and a bank
  // switch must cost nothing.
  const uint32_t t0 = millis();
  fs::FS &fs = rom_browser::fsFor(e);
  File f = fs.open(e.path, "r");
  if (!f) {
    rom_browser::setError("could not open the file");
    return false;
  }
  g_rom_len = f.size();
  g_rom = (uint8_t *)heap_caps_malloc(g_rom_len, MALLOC_CAP_SPIRAM);
  size_t got = 0;
  while (g_rom && got < g_rom_len) {
    const size_t n = f.read(g_rom + got, g_rom_len - got);
    if (!n) break;
    got += n;
  }
  f.close();
  if (!g_rom || got != g_rom_len) {
    rom_browser::setError("could not read the cartridge");
    releaseCore();
    return false;
  }
  Serial.printf("gb: opened %s (%lu KB, %s) in %lu ms\n", e.file,
                (unsigned long)(g_rom_len / 1024),
                e.source == rom_index::kCard ? "card" : "flash",
                (unsigned long)(millis() - t0));

  g_gb = (struct gb_s *)heap_caps_malloc(sizeof(struct gb_s),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!g_gb) {
    rom_browser::setError("out of memory for the core");
    releaseCore();
    return false;
  }
  const enum gb_init_error_e err =
      gb_init(g_gb, romRead, ramRead, ramWrite, gbError, nullptr);
  if (err != GB_INIT_NO_ERROR) {
    rom_browser::setError(err == GB_INIT_CARTRIDGE_UNSUPPORTED
                              ? "cartridge type not supported"
                              : "core rejected this cartridge");
    releaseCore();
    return false;
  }

  size_t save_size = 0;
  gb_get_save_size_s(g_gb, &save_size);
  if (save_size) {
    g_ram_len = save_size;
    g_ram = (uint8_t *)heap_caps_malloc(g_ram_len, MALLOC_CAP_SPIRAM);
    if (!g_ram) {
      rom_browser::setError("out of memory for cartridge RAM");
      releaseCore();
      return false;
    }
    memset(g_ram, 0, g_ram_len);
    g_save_fs = &fs;
    snprintf(g_save_path, sizeof(g_save_path), "%s.sav", e.path);
    loadSave();
  }
  g_ram_dirty = false;
  g_saved_at = millis();

  gb_init_lcd(g_gb, lcdLine);
  minigb_apu_audio_init(&g_apu);

  for (int i = 0; i < kAudioBufs; i++) {
    if (!g_audio[i]) {
      g_audio[i] = (int16_t *)heap_caps_malloc(AUDIO_SAMPLES * sizeof(int16_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
  }
  g_audio_which = 0;

  g_scale = rom_browser::scale();
  if (!emu_video::begin() || !emu_video::configure(LCD_WIDTH, LCD_HEIGHT, g_scale)) {
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

void drawPlayChrome(bool full) {
  if (full) {
    gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_scale >= 5) {
    // No on-screen pad at this size: the picture takes the full height
    // and the margins hold the way out and which gamepad is playing.
    joypad_ui::drawGamepadChrome(full);
    g_pad_drawn = g_pad;
    return;
  }
  joypad_ui::drawControls(g_pad, g_pad_drawn, full);
  g_pad_drawn = g_pad;
}

// Our bit order is the NES shift register's; the Game Boy's joypad register is
// the other way round in its direction bits, and active LOW.
uint8_t toGameBoy(uint8_t pad) {
  uint8_t out = 0xFF;
  if (pad & joypad::kA) out &= (uint8_t)~JOYPAD_A;
  if (pad & joypad::kB) out &= (uint8_t)~JOYPAD_B;
  if (pad & joypad::kSelect) out &= (uint8_t)~JOYPAD_SELECT;
  if (pad & joypad::kStart) out &= (uint8_t)~JOYPAD_START;
  if (pad & joypad::kUp) out &= (uint8_t)~JOYPAD_UP;
  if (pad & joypad::kDown) out &= (uint8_t)~JOYPAD_DOWN;
  if (pad & joypad::kLeft) out &= (uint8_t)~JOYPAD_LEFT;
  if (pad & joypad::kRight) out &= (uint8_t)~JOYPAD_RIGHT;
  return out;
}

void runFrame(uint32_t now_ms) {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_scale >= 5) g_pad = 0;  // no on-screen controls to hit
  {
    const usbpad::State u = usbpad::state();
    if (u.connected) g_pad |= padmap::toNes(u.down, u.x, u.y, g_padmap);
  }
  // Select+Start held for about two thirds of a second leaves the game: a pad
  // has no MENU button, and no game uses that chord for long.
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

  g_gb->direct.joypad = toGameBoy(g_pad);

  const uint32_t t_emu = micros();
  gb_run_frame(g_gb);  // lcdLine fires 144 times during this
  g_us_emu += micros() - t_emu;

  emu_video::present();
  g_us_push += emu_video::lastUs();

  // The APU is clocked by this call, so it happens every frame whether or not
  // there is anywhere to put the result.
  minigb_apu_audio_callback(&g_apu, g_mix);
  if (audio::enabled() && g_audio[g_audio_which] &&
      M5.Speaker.isPlaying(kAudioChannel) < 2) {
    // The panel's speaker is mono, so the two channels are folded together
    // rather than thrown away.
    int16_t *out = g_audio[g_audio_which];
    for (int i = 0; i < (int)AUDIO_SAMPLES; i++) {
      out[i] = (int16_t)((g_mix[2 * i] + g_mix[2 * i + 1]) / 2);
    }
    M5.Speaker.playRaw(out, (size_t)AUDIO_SAMPLES, kAudioRate, false, 1, kAudioChannel);
    g_audio_which = (g_audio_which + 1) % kAudioBufs;
  }

  drawPlayChrome(false);

  // A save written only on the way out is a save lost to a flat battery. Ten
  // seconds after the game last touched cartridge RAM, it goes to the card.
  if (g_ram_dirty && now_ms - g_saved_at > 10000) writeSave();

  g_frames++;
  if (now_ms - g_fps_at >= 1000) {
    g_fps = g_frames * 1000 / (now_ms - g_fps_at);
    const uint32_t n = g_fps ? g_fps : 1;
    Serial.printf("gb %lu fps  emulate=%luus push=%luus\n", (unsigned long)g_fps,
                  (unsigned long)(g_us_emu / n), (unsigned long)(g_us_push / n));
    g_frames = 0;
    g_fps_at = now_ms;
    g_us_emu = g_us_push = 0;
  }
}

}  // namespace

void begin() {
  releaseCore();
  g_mode = Mode::Picking;
  settings_store::loadGb(&g_settings);
  settings_store::loadPadMap(&g_padmap);
  g_quit_hold = 0;

  rom_browser::Config cfg;
  cfg.title = "GAME BOY";
  cfg.dir = kRomDir;
  cfg.extension = ".gb";
  cfg.favourites_file = kFavouritesFile;
  cfg.scale_label[0] = "3x  TOUCH";
  cfg.scale_label[1] = "5x  GAMEPAD";
  cfg.scale_value[0] = 3;
  cfg.scale_value[1] = 5;
  cfg.probe = probeRom;
  if (g_settings.palette >= kPaletteCount) g_settings.palette = 0;
  g_palette = kPalettes[g_settings.palette].colour;
  cfg.extra_label = kPalettes[g_settings.palette].name;
  cfg.empty_hint = "Put .gb files in /gb on the card, or in data/gb and run: pio run -t uploadfs";
  rom_browser::begin(cfg, g_settings.scale);
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
  if (g_mode == Mode::Playing && g_gb) {
    if (g_dirty) {
      g_dirty = false;
      drawPlayChrome(true);
      g_next_frame_us = micros();
    }
    runFrame(now_ms);

    // Pace to the Game Boy's own frame rate, carrying any debt so a frame that
    // ran long is made up by the next cheap one, and capping it at three so a
    // real stall does not turn into a sprint.
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
  uikit::noteRepaint(micros() - t0, 72);
}

void handleTap(int x, int y, uint32_t) {
  if (g_mode == Mode::Playing) return;  // play polls touch itself
  int index = 0;
  switch (rom_browser::handleTap(x, y, &index)) {
    case rom_browser::Result::Launch:
      if (load(index)) {
        g_mode = Mode::Playing;
        g_menu_down = true;  // the tap that launched it must not also exit
        g_dirty = true;
      } else {
        audio::reject();
      }
      break;
    case rom_browser::Result::ScaleChanged:
      g_settings.scale = rom_browser::scale();
      settings_store::saveGb(g_settings);
      break;
    case rom_browser::Result::Extra:
      // The button says which set is in use, and steps to the next one.
      g_settings.palette = (uint8_t)((g_settings.palette + 1) % kPaletteCount);
      g_palette = kPalettes[g_settings.palette].colour;
      rom_browser::setExtraLabel(kPalettes[g_settings.palette].name);
      settings_store::saveGb(g_settings);
      break;
    case rom_browser::Result::Back:
      app::requestExit();
      break;
    case rom_browser::Result::None:
      break;
  }
}

}  // namespace gb_ui
}  // namespace tabulous
