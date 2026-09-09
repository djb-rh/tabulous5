#include "nes_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <dirent.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "app.h"
#include "audio.h"
#include "emu_video.h"
#include "filemanager.h"
#include "../third_party/anemoia/core/cartridge.h"
#include "../third_party/anemoia/core/cpu6502.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "rom_index.h"
#include "nesrom.h"
#include "padmap.h"
#include "sdcard.h"
#include "settings_store.h"
#include "theme.h"
#include "uikit.h"
#include "usbpad.h"

namespace tabulous {
namespace nes_ui {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::gfx;

constexpr const char *kRomDir = "/roms";

// Everything playable, from flash and card together. Scanned once per boot:
// a card holding a full set takes a second or two to walk, and it does not
// change while the console is running.
rom_index::Index g_lib(".nes");
bool g_scanned = false;
uint32_t g_scanned_rev = 0;  // filemanager::revision() at the last scan

enum class Mode : uint8_t { Picking, Playing };

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

// How much the picture is magnified, which decides whether there is room for
// the on-screen pad beside it. The panel work itself is emu_video's.
int g_scale = joypad::kScale;

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

// Injected controller input. See injectPad().
uint8_t g_inject_pad = 0;
uint32_t g_inject_frames = 0;

// Audio capture. See startAudioCapture().
int16_t *g_cap = nullptr;
uint32_t g_cap_len = 0, g_cap_fill = 0;
bool g_cap_done = false;

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

// Emulation is paced to real time.
//
// With the CPU blit (~9.6 ms, at the PSRAM write ceiling) the picture had to
// be drawn every other frame to fit: 10 ms of emulation plus half of 9.6 inside
// a 16.7 ms frame. The PPA does the same blit in ~1.5 ms, so every frame is
// drawn now. g_draw_every stays as the knob for the CPU fallback.
constexpr uint32_t kFrameUs = 1000000 / 60;
int g_draw_every = 1;
uint32_t g_next_frame_us = 0;
uint32_t g_frame_seq = 0;
uint32_t g_drawn_frames = 0, g_shown = 0;
// Phase timings, accumulated over a second so one print covers many frames.
uint32_t g_us_emu = 0, g_us_conv = 0, g_us_push = 0, g_us_sync = 0;

enum class Action : uint8_t {
  None, Pick, Back, PageUp, PageDown, Group, ToggleFav, Scale
};

settings_store::NesSettings g_settings;
padmap::Map g_padmap;
// Frames Select and Start have been held together: the gamepad's way out.
int g_quit_hold = 0;

// The browser: a rail of groups down the left (Favourites, #, A-Z), and the
// chosen group's titles in pages on the right. A page is fourteen rows, which
// is as many as read comfortably at this size; a group like S holds several
// hundred, so paging is by whole screens and the position is spelled out.
constexpr int kRows = 14;
constexpr int kRowH = 40, kRowGap = 2;
constexpr int kListTop = 100;
constexpr int kRailX = kMargin, kRailCellW = 60, kRailCols = 2;
constexpr int kListX = kMargin + kRailCols * (kRailCellW + 4) + 16;
int g_group = 2;  // 'A'
int g_page = 0;

void addAction(const Rect &r, Action a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

// ------------------------------------------------------------------ scanning

void loadFavourites() {
  File f = LittleFS.open(kFavouritesFile, "r");
  if (!f) return;
  String text = f.readString();
  f.close();
  g_lib.applyFavourites(text.c_str());
}

void saveFavourites() {
  const std::string text = g_lib.favouritesText();
  File f = LittleFS.open(kFavouritesFile, "w");
  if (!f) return;
  f.write((const uint8_t *)text.data(), text.size());
  f.close();
}

// The card is walked with readdir rather than the Arduino File API: that API
// opens every entry it lists, and one open in a FAT directory of thousands of
// files is a linear search through the whole directory. Names are all the
// index needs. One level of subdirectories is included, so a card laid out as
// /roms/A, /roms/B ... (which keeps those opens fast) works the same as a
// flat /roms.
// The names in a directory's .hidden file, as written by the web file
// manager: those titles stay on the card but out of the list.
std::string readHidden(fs::FS &fs, const char *dir) {
  char path[300];
  snprintf(path, sizeof(path), "%s/.hidden", dir);
  File f = fs.open(path, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return std::string("\n") + s.c_str() + "\n";
}

bool isHidden(const std::string &hidden, const char *name) {
  if (hidden.empty()) return false;
  return hidden.find("\n" + std::string(name) + "\n") != std::string::npos ||
         hidden.find("\n" + std::string(name) + "\r\n") != std::string::npos;
}

void scanCardDir(const char *vfs_dir, const char *lib_dir, bool recurse) {
  DIR *d = opendir(vfs_dir);
  if (!d) return;
  const std::string hidden = readHidden(sdcard::fs(), lib_dir);
  for (struct dirent *e = readdir(d); e; e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    if (isHidden(hidden, e->d_name)) continue;
    if (e->d_type == DT_DIR) {
      if (!recurse) continue;
      char sub_vfs[300], sub_lib[160];
      snprintf(sub_vfs, sizeof(sub_vfs), "%s/%s", vfs_dir, e->d_name);
      snprintf(sub_lib, sizeof(sub_lib), "%s/%s", lib_dir, e->d_name);
      scanCardDir(sub_vfs, sub_lib, false);
      continue;
    }
    g_lib.add(lib_dir, e->d_name, rom_index::kCard);
  }
  closedir(d);
}

void scan() {
  const uint32_t t0 = millis();
  g_lib.clear();

  File dir = LittleFS.open(kRomDir);
  if (dir && dir.isDirectory()) {
    const std::string hidden = readHidden(LittleFS, kRomDir);
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory() || isHidden(hidden, f.name())) continue;
      g_lib.add(kRomDir, f.name(), rom_index::kFlash);
    }
  }
  const int flash_n = g_lib.size();

  if (sdcard::begin()) {
    char vfs[64];
    snprintf(vfs, sizeof(vfs), "%s%s", sdcard::mountPoint(), kRomDir);
    scanCardDir(vfs, kRomDir, true);
  }
  g_lib.finish();
  loadFavourites();
  g_scanned = true;
  g_scanned_rev = filemanager::revision();
  Serial.printf("nes: %d ROMs (%d built in, %d on card) in %lu ms\n",
                g_lib.size(), flash_n, g_lib.size() - flash_n,
                (unsigned long)(millis() - t0));
}

fs::FS &fsFor(const rom_index::Item &it) {
  return it.source == rom_index::kCard ? sdcard::fs() : (fs::FS &)LittleFS;
}

// Reads the header of one title. Deferred to the moment it is needed rather
// than done for all of them at scan time: on the card it is an open, and see
// scanCardDir() for what an open can cost there.
void probe(int i) {
  rom_index::Item &it = g_lib.mutableAt(i);
  if (it.status >= 0) return;
  uint8_t head[16];
  File h = fsFor(it).open(it.path, "r");
  const size_t got = h ? h.read(head, sizeof(head)) : 0;
  it.bytes = h ? (uint32_t)h.size() : 0;
  if (h) h.close();

  nesrom::Info info;
  const char *why = "";
  const bool readable = got == sizeof(head) &&
                        nesrom::parseHeader(head, it.bytes, &info, &why);
  it.mapper = info.mapper;
  if (!readable) {
    it.status = 1;
    it.problem = why[0] ? why : "unreadable";
  } else if (!info.supported) {
    it.status = 1;
    it.problem = "mapper not supported";
  } else {
    it.status = 0;
    it.problem = "";
  }
}

// ------------------------------------------------------------------- picker

// A five-point star, as a fan of triangles from the centre. Fonts here are
// ASCII, so the glyph is drawn rather than typed.
void drawStar(int cx, int cy, int r, uint16_t colour, bool filled) {
  auto &g = gfx();
  int px[10], py[10];
  for (int k = 0; k < 10; k++) {
    const float a = -1.5707963f + k * 0.62831853f;  // start at the top
    const float rr = (k & 1) ? r * 0.42f : (float)r;
    px[k] = cx + (int)lroundf(cosf(a) * rr);
    py[k] = cy + (int)lroundf(sinf(a) * rr);
  }
  if (filled) {
    for (int k = 0; k < 10; k++) {
      g.fillTriangle(cx, cy, px[k], py[k], px[(k + 1) % 10], py[(k + 1) % 10], colour);
    }
  } else {
    for (int k = 0; k < 10; k++) {
      g.drawLine(px[k], py[k], px[(k + 1) % 10], py[(k + 1) % 10], colour);
    }
  }
}

int groupTotal(int group) {
  return group == rom_index::kFavourites ? g_lib.favouriteCount()
                                         : g_lib.groupCount(group);
}

int groupItem(int group, int n) {
  return group == rom_index::kFavourites ? g_lib.favouriteAt(n)
                                         : g_lib.groupBegin(group) + n;
}

void drawRail() {
  const int rows = rom_index::kGroups / kRailCols;
  for (int gi = 0; gi < rom_index::kGroups; gi++) {
    const int col = gi / rows, row = gi % rows;
    const Rect cell{kRailX + col * (kRailCellW + 4), kListTop + row * (kRowH + kRowGap),
                    kRailCellW, kRowH};
    const bool current = gi == g_group;
    const bool any = groupTotal(gi) > 0;
    uikit::fillRoundRectFast(cell.x, cell.y, cell.w, cell.h, 8,
                             current ? kAccent : any ? kSurfaceLift : kSurface);
    const uint16_t ink = current ? inkFor(theme::isLight() ? 0xA96A00u : 0xFFC53Du)
                                 : any ? kText : kMuted;
    if (gi == rom_index::kFavourites) {
      drawStar(cell.x + cell.w / 2, cell.y + cell.h / 2, 13, ink, true);
    } else {
      uikit::drawLabel(rom_index::groupLabel(gi), cell.x + cell.w / 2,
                       cell.y + cell.h / 2 + 1, ink, &fonts::FreeSansBold12pt7b,
                       middle_center);
    }
    addAction(cell, Action::Group, gi);
  }
}

void drawPicker() {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::drawLabel("NES", kMargin, 46, kText, &fonts::FreeSansBold24pt7b,
                   middle_left);

  const Rect back{kW - kMargin - 190, 18, 190, 62};
  uikit::drawButton(back, "MENU", kSurfaceLift, kText,
                    &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Back);

  // Picture size, as a toggle in the header: it is the one thing about play
  // that is decided before a game starts.
  const Rect size_btn{back.x - 12 - 100 - 12 - 100 - 12 - 200, 18, 200, 62};
  uikit::drawButton(size_btn, g_settings.scale == 3 ? "3x  GAMEPAD" : "2x  TOUCH",
                    kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(size_btn, Action::Scale);

  if (g_lib.size() == 0) {
    uikit::drawLabel("No ROMs found", kW / 2, 300, kMuted,
                     &fonts::FreeSansBold18pt7b, middle_center);
    uikit::drawLabel(sdcard::mounted()
                         ? "Put .nes files in /roms on the card, or in data/roms and run: pio run -t uploadfs"
                         : "Insert a FAT32 card with .nes files in /roms, or put them in data/roms and run: pio run -t uploadfs",
                     kW / 2, 350, kMuted, &fonts::FreeSans12pt7b, middle_center);
    return;
  }

  drawRail();

  const int total = groupTotal(g_group);
  const int pages = total > 0 ? (total + kRows - 1) / kRows : 1;
  if (g_page >= pages) g_page = pages - 1;
  if (g_page < 0) g_page = 0;

  // Where we are, spelled out next to the title: group, how many, which page.
  char where[96];
  if (g_group == rom_index::kFavourites) {
    snprintf(where, sizeof(where), "Favourites  %d", total);
  } else {
    snprintf(where, sizeof(where), "%s  %d of %d", rom_index::groupLabel(g_group),
             total, g_lib.size());
  }
  uikit::drawLabel(where, kMargin + 120, 46, kMuted, &fonts::FreeSansBold18pt7b,
                   middle_left);
  if (g_error[0]) {
    uikit::drawLabel(g_error, kMargin + 120, 78, kDanger, &fonts::FreeSans12pt7b,
                     middle_left);
  }

  const int list_w = kW - kMargin - kListX;
  if (total == 0) {
    uikit::drawLabel(g_group == rom_index::kFavourites
                         ? "Tap the star on a game to keep it here"
                         : "Nothing filed here",
                     kListX + list_w / 2, kListTop + 200, kMuted,
                     &fonts::FreeSansBold18pt7b, middle_center);
    return;
  }

  for (int slot = 0; slot < kRows; slot++) {
    const int n = g_page * kRows + slot;
    if (n >= total) break;
    const int i = groupItem(g_group, n);
    const rom_index::Item &e = g_lib.at(i);
    const Rect row{kListX, kListTop + slot * (kRowH + kRowGap), list_w, kRowH};
    // A title already found unplayable stays listed, dimmed, with the reason:
    // hiding it would leave the owner hunting for a file that is right there.
    const bool bad = e.status == 1;
    uikit::fillRoundRectFast(row.x, row.y, row.w, row.h, 8, bad ? kSurface : kSurfaceLift);
    const uint16_t ink = bad ? kMuted : kText;
    uikit::drawLabel(e.name, row.x + 16, row.y + kRowH / 2 + 1, ink,
                     &fonts::FreeSansBold12pt7b, middle_left);
    if (bad) {
      uikit::drawLabel(e.problem, row.x + row.w - 200, row.y + kRowH / 2 + 1, kDanger,
                       &fonts::FreeSans9pt7b, middle_right);
    } else if (e.source == rom_index::kFlash) {
      uikit::drawLabel("built in", row.x + row.w - 200, row.y + kRowH / 2 + 1, kMuted,
                       &fonts::FreeSans9pt7b, middle_right);
    }
    const Rect star{row.x + row.w - 64, row.y, 64, kRowH};
    drawStar(star.x + star.w / 2, star.y + star.h / 2, 13, e.fav ? kAccent : kMuted, e.fav);
    const Rect title{row.x, row.y, row.w - 64, row.h};
    if (!bad) addAction(title, Action::Pick, i);
    addAction(star, Action::ToggleFav, i);
  }

  if (pages > 1) {
    const Rect up{back.x - 12 - 100 - 12 - 100, 18, 100, 62};
    const Rect down{back.x - 12 - 100, 18, 100, 62};
    const bool can_up = g_page > 0;
    const bool can_down = g_page + 1 < pages;
    uikit::drawArrowButton(up, true, can_up ? kSurfaceLift : kSurface,
                           can_up ? kText : kMuted);
    uikit::drawArrowButton(down, false, can_down ? kSurfaceLift : kSurface,
                           can_down ? kText : kMuted);
    if (can_up) addAction(up, Action::PageUp);
    if (can_down) addAction(down, Action::PageDown);
    char pos[48];
    snprintf(pos, sizeof(pos), "%d / %d", g_page + 1, pages);
    uikit::drawLabel(pos, up.x + 106, 92, kMuted, &fonts::FreeSansBold12pt7b,
                     middle_center);
  }
}

// --------------------------------------------------------------------- play

void releaseCore() {
  emu_video::waitIdle();
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
    // Division, not >> 12: an arithmetic shift floors negative values toward
    // -infinity, and at y = -372 that rounding exactly cancelled the decay —
    // the filter stuck there and every stretch of silence carried a -372 DC
    // offset. Division truncates toward zero, so it settles at exactly 0.
    const int32_t x = (int32_t)samples[i * 2];
    int32_t y = x - g_dc_x + (g_dc_y * 4085) / 4096;
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
    g_lp += ((y - g_lp) * 3539) / 4096;  // a = 0.864, ~14 kHz; same rounding point
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
// transpose happen once per frame, in emu_video.
IRAM_ATTR void drawChunk(uint8_t *buffer, uint32_t size) {
  uint16_t *fb = emu_video::frame();
  if (!fb) return;
  const int rows = (int)(size / sizeof(uint16_t)) / joypad::kNesW;
  if (g_chunk_row + rows > joypad::kNesH) return;
  memcpy(fb + (size_t)g_chunk_row * joypad::kNesW, buffer, size);
  g_chunk_row += rows;
}

bool load(int index) {
  releaseCore();
  probe(index);
  const rom_index::Item &e = g_lib.at(index);
  if (e.status != 0) {
    g_error = e.problem;
    return false;
  }

  // The cartridge copies the whole file into PSRAM and runs from there, so a
  // title on the card costs one open, not one per bank switch.
  const uint32_t t0 = millis();
  g_cart = new Cartridge(fsFor(e), e.path, ROMBackend::LRU);
  Serial.printf("nes: opened %s (%lu KB, %s) in %lu ms\n", e.file,
                (unsigned long)(e.bytes / 1024),
                e.source == rom_index::kCard ? "card" : "flash",
                (unsigned long)(millis() - t0));
  if (!g_cart || !g_cart->isValid()) {
    g_error = "core rejected this ROM";
    releaseCore();
    return false;
  }

  g_scale = g_settings.scale;
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

  if (!emu_video::begin() ||
      !emu_video::configure(joypad::kNesW, joypad::kNesH, g_scale)) {
    g_error = "no room for the picture";
    releaseCore();
    return false;
  }
  if (!emu_video::hardware()) g_draw_every = 2;

  g_next_frame_us = micros();
  g_frame_seq = 0;
  g_drawn_frames = 0;
  return true;
}

void drawPlayChrome(bool full) {
  if (full) {
    gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_scale == 3) {
    // No on-screen pad at 3x: the picture takes the full height and the
    // margins hold only the way out. Input is the gamepad's job.
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

void runFrame(uint32_t now_ms) {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_scale == 3) g_pad = 0;  // no on-screen controls to hit

  // A USB pad, if there is one, on top of the touch controls.
  {
    const usbpad::State u = usbpad::state();
    if (u.connected) g_pad |= padmap::toNes(u.down, u.x, u.y, g_padmap);
  }
  // Select+Start held for about two thirds of a second leaves the game: a
  // pad has no MENU button, and no game uses that chord for long.
  if ((g_pad & (joypad::kSelect | joypad::kStart)) == (joypad::kSelect | joypad::kStart)) {
    if (++g_quit_hold >= 40) menu_held = true;
  } else {
    g_quit_hold = 0;
  }

  if (menu_held && !g_menu_down) {
    g_menu_down = true;
    releaseCore();
    g_mode = Mode::Picking;
    g_dirty = true;
    return;
  }
  if (!menu_held) g_menu_down = false;

  if (g_inject_frames > 0) {
    g_pad |= g_inject_pad;
    g_inject_frames--;
  }
  // joypad's bit order is the NES shift register's, which is what the core
  // expects, so the pad byte goes across untranslated.
  g_cpu->bus.setController(g_pad);

  const uint32_t t_emu = micros();
  g_chunk_row = 0;
  g_cpu->clockFrame();   // drawChunk fires ~30 times during this
  g_us_emu += micros() - t_emu;

  // Draw every g_draw_every-th frame; emulate all of them.
  if ((g_frame_seq % g_draw_every) == 0) {
    emu_video::present();
    g_us_push += emu_video::lastUs();
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
    if (g_cap && !g_cap_done) {
      const uint32_t room = g_cap_len - g_cap_fill;
      const uint32_t take = room < (uint32_t)kAudioBufSamples ? room : kAudioBufSamples;
      memcpy(g_cap + g_cap_fill, dst, take * sizeof(int16_t));
      g_cap_fill += take;
      if (g_cap_fill >= g_cap_len) g_cap_done = true;
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
  g_mode = Mode::Picking;
  g_error = "";
  settings_store::loadNes(&g_settings);
  settings_store::loadPadMap(&g_padmap);
  g_quit_hold = 0;
  if (g_scanned && filemanager::revision() != g_scanned_rev) g_scanned = false;
  if (!g_scanned) {
    // A card of thousands takes a moment to walk; say so rather than sit on
    // the previous screen.
    auto &g = gfx();
    g.fillScreen(kBg);
    uikit::drawLabel("Reading the game list...", kW / 2, kH / 2, kMuted,
                     &fonts::FreeSansBold18pt7b, middle_center);
    uikit::present();
    scan();
  }
  g_dirty = true;
}

// Forces the next entry to walk the filesystems again — after the web file
// manager has added or removed something, say.
void rescan() { g_scanned = false; }

void invalidate() {
  g_dirty = true;
  g_pad_drawn = 0xFF;
}

bool playing() { return g_mode == Mode::Playing; }

void injectPad(uint8_t buttons, uint32_t frames) {
  g_inject_pad = buttons;
  g_inject_frames = frames;
}

void startAudioCapture(uint32_t seconds) {
  endAudioCapture();
  g_cap_len = seconds * kApuRate;
  g_cap = (int16_t *)heap_caps_malloc(g_cap_len * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  g_cap_fill = 0;
  g_cap_done = false;
  if (!g_cap) g_cap_len = 0;
}

bool audioCaptureReady(const int16_t **samples, uint32_t *count) {
  if (!g_cap || !g_cap_done) return false;
  *samples = g_cap;
  *count = g_cap_fill;
  return true;
}

void endAudioCapture() {
  if (g_cap) heap_caps_free(g_cap);
  g_cap = nullptr;
  g_cap_len = g_cap_fill = 0;
  g_cap_done = false;
}

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
    // Frames alternate cheap and expensive: ~10.9 ms when the picture is
    // skipped, ~20.6 ms when it is drawn. The average, 15.5 ms, fits inside
    // 16.7 — but only if the cheap frame is allowed to make up the deficit the
    // expensive one ran up. An earlier version discarded the deficit after
    // every late frame, so the cheap frame got a fresh full budget and slept
    // ~5 ms rather than catching up. That sleep was the whole missing 5.3 ms a
    // frame, and it was self-inflicted.
    //
    // So carry the debt, and cap it at three frames so a genuine stall does
    // not turn into a sprint.
    g_next_frame_us += kFrameUs;
    const int32_t slack = (int32_t)(g_next_frame_us - micros());
    if (slack > 1000) {
      delay((uint32_t)slack / 1000);  // yields, so the APU task keeps running
    } else if (slack < -(int32_t)(3 * kFrameUs)) {
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
      g_error = "";
      if (load(param)) {
        g_mode = Mode::Playing;
        g_menu_down = true;  // the tap that launched it must not also exit
      } else {
        audio::reject();
      }
      g_dirty = true;
      break;
    case Action::Group:
      audio::select();
      if (param != g_group) g_page = 0;
      g_group = param;
      g_dirty = true;
      break;
    case Action::Scale:
      audio::select();
      g_settings.scale = g_settings.scale == 3 ? 2 : 3;
      settings_store::saveNes(g_settings);
      g_dirty = true;
      break;
    case Action::ToggleFav: {
      audio::select();
      g_lib.setFavourite(param, !g_lib.at(param).fav);
      saveFavourites();
      g_dirty = true;
      break;
    }
    case Action::PageUp:
      audio::select();
      g_page--;
      g_dirty = true;
      break;
    case Action::PageDown:
      audio::select();
      g_page++;
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
