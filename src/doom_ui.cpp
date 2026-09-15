#include "doom_ui.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

extern "C" {
#include "../third_party/doomgeneric/dg_tabulous.h"
#include "../third_party/doomgeneric/doomgeneric.h"
#include "../third_party/doomgeneric/doomkeys.h"
}

#include "app.h"
#include "audio.h"
#include "emu_video.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "padmap.h"
#include "rom_browser.h"
#include "sdcard.h"
#include "settings_store.h"
#include "snes_ui.h"
#include "theme.h"
#include "uikit.h"
#include "usbpad.h"

namespace tabulous {
namespace doom_ui {
namespace {

using namespace theme;

constexpr const char *kWadDir = "/doom";

enum class Mode : uint8_t { Picking, Playing };
Mode g_mode = Mode::Picking;
bool g_dirty = true;

// The engine's life: never started; started and whole; or broken by an
// I_Error, after which only a restart of the console gets it back.
enum class Engine : uint8_t { Cold, Running, Dead };
Engine g_engine = Engine::Cold;
char g_wad_vfs[320] = "";      // the IWAD the engine was started on
char g_pwad_vfs[320] = "";     // and the add-on with it, or empty
char g_started_on[660] = "";   // both, for telling a resume from a change
char g_config_dir[320] = "";   // beside the IWAD, with a trailing slash
char g_argv0[] = "doom";
char g_arg_iwad[] = "-iwad";
char g_arg_file[] = "-file";
char *g_argv[] = {g_argv0, g_arg_iwad, g_wad_vfs, g_arg_file, g_pwad_vfs, nullptr};
int g_argc = 3;

// For an add-on (a PWAD), the IWAD in the same folder it was matched with,
// by list index. An add-on plays on that; the engine gets both.
std::map<int, std::string> g_pwad_iwad;

// I_Error unwinds to here. Armed only around calls into the engine.
jmp_buf g_jmp;
bool g_jmp_armed = false;
char g_error[160] = "";
bool g_quit = false;  // the game's own QUIT was chosen

// The engine runs in its own task, for its stack: the renderer's recursion
// down the BSP and the loaders' locals want far more than the loop task's
// 8 KB, and internal RAM has none to spare for a bigger loop stack - the
// Wi-Fi co-processor's transport failed to start when that was tried. This
// task's 64 KB stack lives in PSRAM, which the chip allows.
//
// Nothing runs concurrently: the loop hands the task one command and waits
// for it, so the engine sees the world exactly as it did in the loop.
TaskHandle_t g_task = nullptr;
SemaphoreHandle_t g_go = nullptr, g_done = nullptr;
enum class Cmd : uint8_t { Create, Tick };
Cmd g_cmd = Cmd::Tick;
bool g_cmd_failed = false;
constexpr uint32_t kTaskStack = 64 * 1024;

void doomTask(void *) {
  for (;;) {
    xSemaphoreTake(g_go, portMAX_DELAY);
    g_cmd_failed = false;
    g_jmp_armed = true;
    if (setjmp(g_jmp) == 0) {
      if (g_cmd == Cmd::Create) doomgeneric_Create(g_argc, g_argv);
      else doomgeneric_Tick();
    } else {
      g_cmd_failed = true;
    }
    g_jmp_armed = false;
    xSemaphoreGive(g_done);
  }
}

void reportInternal(const char *where);

// Runs one command in the engine's task and waits for it. False if the
// engine raised an error, which is then in g_error.
bool runEngine(Cmd cmd) {
  if (!g_task) {
    g_go = xSemaphoreCreateBinary();
    g_done = xSemaphoreCreateBinary();
    if (xTaskCreatePinnedToCoreWithCaps(doomTask, "doom", kTaskStack, nullptr, 1, &g_task,
                                        1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
      g_task = nullptr;
      snprintf(g_error, sizeof(g_error), "no memory for the engine's task");
      return false;
    }
  }
  g_cmd = cmd;
  xSemaphoreGive(g_go);
  xSemaphoreTake(g_done, portMAX_DELAY);
  return !g_cmd_failed;
}

settings_store::DoomSettings g_settings;
padmap::Map g_padmap;
int g_quit_hold = 0;
bool g_menu_down = false;

// The engine's keyboard, fed from the pad. One bit per key we ever press,
// so a change of pad state becomes press and release events.
enum : uint32_t {
  kKUp = 1u << 0, kKDown = 1u << 1, kKLeft = 1u << 2, kKRight = 1u << 3,
  kKFire = 1u << 4, kKUse = 1u << 5, kKEnter = 1u << 6, kKEsc = 1u << 7,
  kKStrafeL = 1u << 8, kKStrafeR = 1u << 9,
};
struct KeyBit {
  uint32_t bit;
  unsigned char key;
};
const KeyBit kKeys[] = {
    {kKUp, KEY_UPARROW},       {kKDown, KEY_DOWNARROW}, {kKLeft, KEY_LEFTARROW},
    {kKRight, KEY_RIGHTARROW}, {kKFire, KEY_FIRE},      {kKUse, KEY_USE},
    {kKEnter, KEY_ENTER},      {kKEsc, KEY_ESCAPE},     {kKStrafeL, KEY_STRAFE_L},
    {kKStrafeR, KEY_STRAFE_R},
};
uint32_t g_keys = 0;
bool g_select_was = false;
int g_weapon_cursor = 2;  // the digit last asked for; pistol to start

constexpr int kQueueSize = 64;
uint16_t g_queue[kQueueSize];
int g_q_head = 0, g_q_tail = 0;

void pushKey(bool pressed, unsigned char key) {
  const int next = (g_q_head + 1) % kQueueSize;
  if (next == g_q_tail) return;  // full; the engine is not reading
  g_queue[g_q_head] = (uint16_t)((pressed ? 0x100 : 0) | key);
  g_q_head = next;
}

void setKeys(uint32_t now) {
  const uint32_t changed = now ^ g_keys;
  if (!changed) return;
  for (const KeyBit &k : kKeys) {
    if (changed & k.bit) pushKey((now & k.bit) != 0, k.key);
  }
  g_keys = now;
}

// The palette as the panel wants it, rebuilt when the engine changes it
// (it flashes on damage and pickups, so this happens often).
uint16_t g_lut[256];
bool g_lut_valid = false;
uint8_t g_pad = 0, g_pad_drawn = 0xFF;
uint32_t g_frames = 0, g_fps_at = 0;
uint8_t g_inject_pad = 0;
uint32_t g_inject_frames = 0;

void rebuildLut() {
  const unsigned char *p = dg_palette();  // b, g, r, a per entry
  for (int i = 0; i < 256; i++) {
    g_lut[i] = uikit::gfx().color565(p[i * 4 + 2], p[i * 4 + 1], p[i * 4]);
  }
  g_lut_valid = true;
}

// ---- the WADs -------------------------------------------------------------

// The engine tells the games apart by the file's name, so a WAD under any
// other name is refused here, with the reason, rather than at launch.
const char *const kIwadNames[] = {
    "doom.wad",    "doom1.wad",     "doom2.wad",     "plutonia.wad", "tnt.wad",
    "chex.wad",    "hacx.wad",      "freedm.wad",    "freedoom1.wad", "freedoom2.wad",
};

// Which game an add-on is for, read from its map names: E1M1 and friends
// are Doom, MAP01 and up are Doom II. The IWAD it plays on has to be beside
// it under one of the game's names; the first one found is used.
const char *const kDoom1Iwads[] = {"doom.wad", "doom1.wad", "freedoom1.wad", nullptr};
const char *const kDoom2Iwads[] = {"doom2.wad", "plutonia.wad", "tnt.wad", "freedoom2.wad", nullptr};

bool findIwadFor(fs::FS &fs, const rom_index::Item *it, File &h, std::string *iwad) {
  uint8_t head[12];
  h.seek(0);
  if (h.read(head, 12) != 12) return false;
  const uint32_t numlumps = (uint32_t)head[4] | ((uint32_t)head[5] << 8) | ((uint32_t)head[6] << 16) | ((uint32_t)head[7] << 24);
  const uint32_t dirofs = (uint32_t)head[8] | ((uint32_t)head[9] << 8) | ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
  bool doom1 = false, doom2 = false;
  if (numlumps < 20000 && h.seek(dirofs)) {
    uint8_t ent[16];
    for (uint32_t i = 0; i < numlumps; i++) {
      if (h.read(ent, 16) != 16) break;
      const char *n = (const char *)ent + 8;
      if (n[0] == 'E' && n[2] == 'M' && n[1] >= '1' && n[1] <= '9' && n[3] >= '1' && n[3] <= '9' && n[4] == 0) doom1 = true;
      if (n[0] == 'M' && n[1] == 'A' && n[2] == 'P' && n[3] >= '0' && n[3] <= '9' && n[4] >= '0' && n[4] <= '9') doom2 = true;
    }
  }
  // A folder, from the file's own path.
  std::string dir = it->path;
  const size_t slash = dir.find_last_of('/');
  dir = slash == std::string::npos ? "/" : dir.substr(0, slash + 1);
  const char *const *lists[2] = {doom2 && !doom1 ? kDoom2Iwads : kDoom1Iwads,
                                 doom2 && !doom1 ? kDoom1Iwads : kDoom2Iwads};
  for (const char *const *list : lists) {
    for (int k = 0; list[k]; k++) {
      const std::string candidate = dir + list[k];
      if (fs.exists(candidate.c_str())) {
        *iwad = candidate;
        return true;
      }
    }
  }
  return false;
}

void probeWad(fs::FS &fs, rom_index::Item *it) {
  char magic[4] = {0, 0, 0, 0};
  File h = fs.open(it->path, "r");
  const size_t got = h ? h.read((uint8_t *)magic, 4) : 0;
  it->bytes = h ? (uint32_t)h.size() : 0;
  it->status = 1;
  if (got != 4) {
    if (h) h.close();
    it->problem = "unreadable";
    return;
  }
  if (memcmp(magic, "PWAD", 4) == 0) {
    std::string iwad;
    const bool found = findIwadFor(fs, it, h, &iwad);
    h.close();
    if (!found) {
      it->problem = "an add-on; needs doom.wad or doom2.wad beside it";
      return;
    }
    g_pwad_iwad[(int)(it - &rom_browser::item(0))] = iwad;
    it->status = 0;
    it->problem = "";
    return;
  }
  h.close();
  if (memcmp(magic, "IWAD", 4) != 0) {
    it->problem = "not a WAD";
    return;
  }
  for (const char *name : kIwadNames) {
    if (strcasecmp(it->file, name) == 0) {
      it->status = 0;
      it->problem = "";
      return;
    }
  }
  it->problem = "rename it: doom.wad, doom1.wad, doom2.wad...";
}

// ---- play -----------------------------------------------------------------

// Doom's 320x200 was drawn for a 4:3 screen, so it is stretched taller than
// wide. The scaler takes factors in sixteenths and nothing else - 3.6 became
// 3.5625 and the last rows spilled past the block as a strip of noise - so
// these are exact sixteenths whose products are whole pixels: 940x700 for
// the full height (1.34:1), 460x350 between the pads (1.31:1).
float scaleX() { return g_settings.size == 2 ? 2.9375f : 1.4375f; }
float scaleY() { return g_settings.size == 2 ? 3.5f : 1.75f; }

void restartConsole(const char *why) {
  rom_browser::showBusy(why);
  delay(1500);
  esp_restart();
}

bool startVideo() {
  joypad_ui::setLabels("WEAPON", "MENU", "USE", "FIRE");
  if (!joypad_ui::beginPlay(false, 320, 200, scaleX(), scaleY())) {
    rom_browser::setError("no room for the picture");
    return false;
  }
  g_lut_valid = false;
  g_pad = 0;
  g_pad_drawn = 0xFF;
  g_quit_hold = 0;
  g_quit = false;
  g_frames = 0;
  g_fps_at = millis();
  return true;
}

void leavePlay() {
  setKeys(0);  // every key up, or the game keeps walking while it waits
  joypad_ui::endPlay();
  M5.Speaker.stop();
  g_mode = Mode::Picking;
  rom_browser::invalidate();
  g_dirty = true;
}

void reportInternal(const char *where) {
  Serial.printf("doom: internal %s: %uKB (largest %uKB)\n", where,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
}

bool load(int index) {
  // The SNES core holds a 128 KB block of internal RAM from boot for its
  // work RAM. Doom's start needs that memory more than the SNES needs it
  // fast: the card driver bounces every read into PSRAM through internal
  // buffers, and with the block held those ran out mid-load. Same trade the
  // Wi-Fi editor makes; the SNES falls back to PSRAM until the next boot.
  snes_ui::yieldWorkRamReserve();
  reportInternal("at load");
  rom_browser::probeItem(index);
  const rom_index::Item &e = rom_browser::item(index);
  if (e.status != 0) {
    rom_browser::setError(e.problem);
    return false;
  }
  const char *root = e.source == rom_index::kCard ? sdcard::mountPoint() : "/littlefs";
  char iwad[320], pwad[320] = "";
  const auto add_on = g_pwad_iwad.find(index);
  if (add_on != g_pwad_iwad.end()) {
    snprintf(iwad, sizeof(iwad), "%s%s", root, add_on->second.c_str());
    snprintf(pwad, sizeof(pwad), "%s%s", root, e.path);
  } else {
    snprintf(iwad, sizeof(iwad), "%s%s", root, e.path);
  }
  char started_on[660];
  snprintf(started_on, sizeof(started_on), "%s|%s", iwad, pwad);

  if (g_engine == Engine::Dead) restartConsole("Restarting the console for Doom...");
  if (g_engine == Engine::Running) {
    if (strcmp(started_on, g_started_on) != 0) restartConsole("Restarting the console to change WADs...");
    return startVideo();  // resume where it was
  }

  snprintf(g_wad_vfs, sizeof(g_wad_vfs), "%s", iwad);
  snprintf(g_pwad_vfs, sizeof(g_pwad_vfs), "%s", pwad);
  snprintf(g_started_on, sizeof(g_started_on), "%s", started_on);
  g_argc = pwad[0] ? 5 : 3;
  snprintf(g_config_dir, sizeof(g_config_dir), "%s", iwad);
  char *slash = strrchr(g_config_dir, '/');
  if (slash) slash[1] = '\0';

  Serial.printf("doom: starting on %s%s%s (%lu KB), config in %s\n", g_wad_vfs,
                pwad[0] ? " with " : "", pwad, (unsigned long)(e.bytes / 1024), g_config_dir);
  const uint32_t t0 = millis();
  if (!runEngine(Cmd::Create)) {
    g_engine = g_task ? Engine::Dead : Engine::Cold;
    Serial.printf("doom: engine error at start: %s\n", g_error);
    rom_browser::setError(g_error);
    return false;
  }
  dg_set_autorun();
  g_engine = Engine::Running;
  Serial.printf("doom: up in %lu ms, internal free %u KB, stack left %u\n",
                (unsigned long)(millis() - t0),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)uxTaskGetStackHighWaterMark(g_task));
  return startVideo();
}

void drawPlayChrome(bool full) {
  if (full) {
    uikit::gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_settings.size == 2) {
    joypad_ui::drawGamepadChrome(full);
    g_pad_drawn = g_pad;
    return;
  }
  joypad_ui::drawControls(g_pad, g_pad_drawn, full);
  g_pad_drawn = g_pad;
}

// The pad, as the engine's keyboard. Directions turn and walk; B fires; A
// uses, and is also Enter so the same thumb works the menus; START is the
// menu; SELECT steps through the weapons; a gamepad's L and R strafe.
void readPad() {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_settings.size == 2) g_pad = 0;  // no on-screen controls to hit
  if (g_inject_frames) {
    g_pad |= g_inject_pad;
    g_inject_frames--;
  }
  uint32_t keys = 0;
  const usbpad::State u = usbpad::state();
  if (u.connected) {
    g_pad |= padmap::toNes(u.down, u.x, u.y, g_padmap);
    // The GP100's shoulder buttons are 5 and 6; nothing maps them because
    // no NES game has them, so they are read as they are.
    if (u.down & (1u << 4)) keys |= kKStrafeL;
    if (u.down & (1u << 5)) keys |= kKStrafeR;
  }
  const uint8_t chord = joypad::kSelect | joypad::kStart;
  if ((g_pad & chord) == chord) {
    if (++g_quit_hold >= 25) menu_held = true;
  } else {
    g_quit_hold = 0;
  }
  if (menu_held && !g_menu_down) {
    g_menu_down = true;
    leavePlay();
    return;
  }
  if (!menu_held) g_menu_down = false;

  if (g_pad & joypad::kUp) keys |= kKUp;
  if (g_pad & joypad::kDown) keys |= kKDown;
  if (g_pad & joypad::kLeft) keys |= kKLeft;
  if (g_pad & joypad::kRight) keys |= kKRight;
  if (g_pad & joypad::kB) keys |= kKFire;
  if (g_pad & joypad::kA) keys |= kKUse | kKEnter;
  if (g_pad & joypad::kStart) keys |= kKEsc;
  setKeys(keys);

  // Weapons are the digit keys, so SELECT asks for the next digit after the
  // one in hand. A digit for a weapon not carried does nothing, and the next
  // press moves on, so a few presses always reach the one wanted.
  const bool select = (g_pad & joypad::kSelect) != 0;
  if (select && !g_select_was) {
    static const int kDigitOf[9] = {1, 2, 3, 4, 5, 6, 7, 1, 3};
    const int w = dg_ready_weapon();
    const int held = (w >= 0 && w < 9) ? kDigitOf[w] : g_weapon_cursor;
    g_weapon_cursor = (held >= g_weapon_cursor ? held : g_weapon_cursor) % 7 + 1;
    pushKey(true, (unsigned char)('0' + g_weapon_cursor));
    pushKey(false, (unsigned char)('0' + g_weapon_cursor));
  }
  g_select_was = select;
}

void runFrame(uint32_t now_ms) {
  readPad();
  if (g_mode != Mode::Playing) return;

  if (!runEngine(Cmd::Tick)) {  // the tics that are due, then DG_DrawFrame
    g_engine = Engine::Dead;
    Serial.printf("doom: engine error: %s\n", g_error);
    leavePlay();
    rom_browser::setError(g_error);
    return;
  }
  if (g_quit) {
    // Its own QUIT: the engine is whole, so this is a way out, not an end.
    g_quit = false;
    dg_save_defaults();
    leavePlay();
    return;
  }
  drawPlayChrome(false);

  if (now_ms - g_fps_at >= 2000) {
    Serial.printf("doom %lu fps  push=%luus  sfx=%lu  stack left %u\n",
                  (unsigned long)(g_frames * 1000 / (now_ms - g_fps_at)),
                  (unsigned long)emu_video::lastUs(), (unsigned long)dg_sound_starts(),
                  (unsigned)uxTaskGetStackHighWaterMark(g_task));
    g_frames = 0;
    g_fps_at = now_ms;
  }
}

}  // namespace

void begin() {
  g_mode = Mode::Picking;
  g_pwad_iwad.clear();
  settings_store::loadDoom(&g_settings);
  settings_store::loadPadMap(&g_padmap);
  g_quit_hold = 0;

  rom_browser::Config cfg;
  cfg.title = "DOOM";
  cfg.dir = kWadDir;
  cfg.extension = ".wad";
  cfg.favourites_file = kFavouritesFile;
  cfg.scale_label[0] = "TOUCH PAD";
  cfg.scale_label[1] = "FULL  GAMEPAD";
  cfg.scale_value[0] = 1;
  cfg.scale_value[1] = 2;
  cfg.probe = probeWad;
  cfg.empty_hint = "Put doom.wad, doom1.wad or doom2.wad in /doom on the card";
  cfg.flat = true;  // a handful of files, not a card of thousands
  rom_browser::begin(cfg, g_settings.size);
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

void injectPad(uint8_t buttons, uint32_t frames) {
  g_inject_pad = buttons;
  g_inject_frames = frames;
}

namespace {
void onBrowser(rom_browser::Result r, int index);
}  // namespace

void tick(uint32_t now_ms) {
  if (g_mode == Mode::Playing) {
    if (g_dirty) {
      g_dirty = false;
      drawPlayChrome(true);
    }
    runFrame(now_ms);
    return;
  }
  {
    int index = 0;
    onBrowser(rom_browser::tick(now_ms, &index), index);
    if (g_mode != Mode::Picking) return;
  }
  if (!rom_browser::dirty()) return;
  const uint32_t t0 = micros();
  uikit::clearTargets();
  rom_browser::draw();
  uikit::present();
  uikit::noteRepaint(micros() - t0, 76);
}

void handleTap(int x, int y, uint32_t) {
  if (g_mode == Mode::Playing) return;
  int index = 0;
  onBrowser(rom_browser::handleTap(x, y, &index), index);
}

namespace {
void onBrowser(rom_browser::Result r, int index) {
  switch (r) {
    case rom_browser::Result::Launch: {
      char busy[96];
      snprintf(busy, sizeof(busy), "Loading %s...", rom_browser::item(index).name);
      rom_browser::showBusy(busy);
      if (load(index)) {
        g_mode = Mode::Playing;
        g_menu_down = true;
        g_dirty = true;
      } else {
        audio::reject();
      }
      break;
    }
    case rom_browser::Result::ScaleChanged:
      g_settings.size = rom_browser::scale();
      settings_store::saveDoom(g_settings);
      break;
    case rom_browser::Result::Back:
      app::requestExit();
      break;
    case rom_browser::Result::Extra:
    case rom_browser::Result::OrientationChanged:
    case rom_browser::Result::None:
      break;
  }
}
}  // namespace

}  // namespace doom_ui
}  // namespace tabulous

// ---- the platform seam, in C linkage ----------------------------------------

using namespace tabulous;
using namespace tabulous::doom_ui;

extern "C" {

void DG_Init(void) {}

// ---- sound -------------------------------------------------------------
//
// The engine mixes a tic at a time (dg_sound.c); these are the speaker end.
// Four blocks rotate so the one the speaker is reading is never the one
// being filled; the queue is kept two deep, as the emulators do.
namespace {
constexpr int kDoomAudioChannel = 1;
constexpr int kAudioBlocks = 4;
constexpr int kAudioBlockMax = 320;
int16_t g_audio[kAudioBlocks][kAudioBlockMax];
int g_audio_which = 0;
}  // namespace

int dg_audio_wants(void) {
  return audio::enabled() && M5.Speaker.isPlaying(kDoomAudioChannel) < 2;
}

void dg_audio_push(const int16_t *pcm, int count, int rate) {
  if (count > kAudioBlockMax) count = kAudioBlockMax;
  int16_t *block = g_audio[g_audio_which];
  memcpy(block, pcm, (size_t)count * sizeof(int16_t));
  M5.Speaker.playRaw(block, (size_t)count, (uint32_t)rate, false, 1, kDoomAudioChannel);
  g_audio_which = (g_audio_which + 1) % kAudioBlocks;
}

void DG_DrawFrame(void) {
  if (dg_palette_changed() || !g_lut_valid) rebuildLut();
  uint16_t *fb = emu_video::frame();
  if (!fb || !DG_ScreenBuffer) return;
  const uint8_t *src = (const uint8_t *)DG_ScreenBuffer;
  for (int i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++) fb[i] = g_lut[src[i]];
  emu_video::present();
  g_frames++;
}

void DG_SleepMs(uint32_t ms) { delay(ms); }

uint32_t DG_GetTicksMs(void) { return millis(); }

int DG_GetKey(int *pressed, unsigned char *key) {
  if (g_q_tail == g_q_head) return 0;
  const uint16_t v = g_queue[g_q_tail];
  g_q_tail = (g_q_tail + 1) % kQueueSize;
  *pressed = (v >> 8) & 1;
  *key = (unsigned char)(v & 0xFF);
  return 1;
}

void DG_SetWindowTitle(const char *) {}

void DG_OnQuit(void) { g_quit = true; }

void DG_OnError(const char *message) {
  snprintf(g_error, sizeof(g_error), "%s", message ? message : "engine error");
  if (g_jmp_armed) longjmp(g_jmp, 1);
  // Nowhere to unwind to: this is the console's freeze policy.
  Serial.printf("doom: fatal outside the engine call: %s\n", g_error);
  Serial.flush();
  esp_restart();
}

const char *DG_ConfigDir(void) { return g_config_dir; }

// The engine's stdout, through the console's non-blocking serial path.
void dg_vfprintf(void *stream, const char *fmt, va_list ap) {
  if (stream != (void *)stdout && stream != (void *)stderr) {
    vfprintf((FILE *)stream, fmt, ap);
    return;
  }
  char buf[256];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  Serial.print(buf);
}

void dg_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  dg_vfprintf((void *)stdout, fmt, ap);
  va_end(ap);
}

void dg_fprintf(void *stream, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  dg_vfprintf(stream, fmt, ap);
  va_end(ap);
}

void dg_puts(const char *s) { Serial.println(s); }

int dg_putchar(int c) {
  Serial.write((uint8_t)c);
  return c;
}

}  // extern "C"
