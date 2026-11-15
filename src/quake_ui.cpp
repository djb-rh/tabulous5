#include "quake_ui.h"

#if !defined(HAVE_QUAKE)

// Built without the engine. Everything else builds and behaves exactly as it
// would have. See platformio.ini.
namespace tabulous {
namespace quake_ui {
bool available() { return false; }
void begin() {}
void invalidate() {}
void tick(uint32_t) {}
void handleTap(int, int, uint32_t) {}
bool playing() { return false; }
bool resumePending() { return false; }
void injectPad(uint8_t, uint32_t) {}
void console(const char *) {}
}  // namespace quake_ui
}  // namespace tabulous

#else

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <dirent.h>
#include <esp_attr.h>
#include <sys/stat.h>

#include <csetjmp>
#include <cctype>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../third_party/quakegeneric/source/qg_extram.h"
#include "../third_party/quakegeneric/source/quakegeneric.h"
void Cbuf_AddText(char *text);   // the engine's command buffer
void Host_WriteConfiguration(void);   // config.cfg: bindings and options
void QG_DefaultsInit(void);      // qg_defaults.c: this console's own first-run settings
int QG_DefaultsApply(void);
int QG_InMenu(void);
void QG_AudioPump(void);         // snd_tabulous.c: hands the speaker what it has room for
unsigned QG_AudioBlocks(void);
int QG_AudioPeak(void);
}

#include "app.h"
#include "audio.h"
#include "buspad.h"
#include "emu_video.h"
#include "joypad.h"
#include "joypad_ui.h"
#include "padmap.h"
#include "sdcard.h"
#include "settings_store.h"
#include "theme.h"
#include "uikit.h"
#include "usbpad.h"

namespace tabulous {
namespace quake_ui {
namespace {

using namespace theme;
using uikit::Rect;

// Where the game lives on the card: /quake/id1/pak0.pak, and pak1.pak beside
// it for the registered episodes. The engine is told the folder above id1.
// Any other folder of /quake with a pak0.pak in it is a game of its own that
// runs on top of id1: the two mission packs, or anything else made for the
// original engine.
constexpr const char *kBaseDir = "/quake";
constexpr int kW = 1280, kH = 720;

enum class Mode : uint8_t { Picking, Playing };
Mode g_mode = Mode::Picking;
bool g_dirty = true;

// The engine's life: never started; started and whole; or ended, by its own
// QUIT or by a Sys_Error, after which only a restart of the console gets it
// back. It keeps its world in statics and cannot be started twice.
enum class Engine : uint8_t { Cold, Running, Dead };
Engine g_engine = Engine::Cold;
char g_error[160] = "";
char g_status[96] = "";
bool g_have_pak = false, g_have_pak1 = false;
int g_size = 3;   // the picture: 2 = 640x480 with the touch pad, 3 = 960x720

// The games found on the card. The first is always id1, Quake itself.
struct Game {
  char dir[24];
  char title[32];
};
constexpr int kMaxGames = 4;   // what one row of buttons holds
Game g_games[kMaxGames];
int g_game_count = 0;
int g_game = 0;            // the one chosen on the screen
int g_engine_game = -1;    // the one the engine was started with
char g_wanted[24] = "";    // the folder remembered from last time

// The engine takes its game on its command line and never changes it, and it
// cannot be started twice; so a change of game is a restart of the console.
// This is the note left for the far side of that restart, in memory that a
// software reset leaves alone: come straight back, to this game, at this size.
constexpr uint32_t kResumeMagic = 0x51554B45;   // "QUKE"
struct Resume {
  uint32_t magic;
  uint8_t size;
  char dir[24];
};
RTC_NOINIT_ATTR Resume g_resume;
int g_autostart = 0;       // a size to start at on the next tick, or 0

char g_basedir[64] = "";
char g_a0[] = "quake", g_a1[] = "-basedir";
char g_game_flag[12] = "", g_game_dir[24] = "";
char *g_argv[] = {g_a0, g_a1, g_basedir, nullptr, nullptr, nullptr};
int g_argc = 3;
constexpr int kAudioChannel = 1;

// Sys_Error and the game's own QUIT unwind to here. Armed only around calls
// into the engine.
jmp_buf g_jmp;
bool g_jmp_armed = false;

// The engine runs in its own task, for its stack: the renderer keeps its
// edge and surface lists there, well over a hundred kilobytes of them. The
// loop hands the task one command and waits for it, so nothing runs
// concurrently and the engine sees the world as it would in the loop.
TaskHandle_t g_task = nullptr;
SemaphoreHandle_t g_go = nullptr, g_done = nullptr;
enum class Cmd : uint8_t { Create, Tick };
Cmd g_cmd = Cmd::Tick;
bool g_cmd_failed = false;
double g_tick_seconds = 0;
// 200 KB is what Espressif gave it, and the shareware demos leave 20 of
// that; the registered maps are bigger, and this memory is not scarce.
constexpr uint32_t kTaskStack = 320 * 1024;

void quakeTask(void *) {
  for (;;) {
    xSemaphoreTake(g_go, portMAX_DELAY);
    g_cmd_failed = false;
    g_jmp_armed = true;
    if (setjmp(g_jmp) == 0) {
      if (g_cmd == Cmd::Create) QG_Create(g_argc, g_argv);
      else QG_Tick(g_tick_seconds);
    } else {
      g_cmd_failed = true;
    }
    g_jmp_armed = false;
    xSemaphoreGive(g_done);
  }
}

bool runEngine(Cmd cmd) {
  if (!g_task) {
    g_go = xSemaphoreCreateBinary();
    g_done = xSemaphoreCreateBinary();
    if (xTaskCreatePinnedToCoreWithCaps(quakeTask, "quake", kTaskStack, nullptr, 1, &g_task,
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

padmap::Map g_padmap;
int g_quit_hold = 0;
bool g_menu_down = false;

// The engine's keyboard, fed from the pads. One bit per key ever pressed, so
// a change of pad state becomes press and release events.
enum : uint32_t {
  kKUp = 1u << 0, kKDown = 1u << 1, kKLeft = 1u << 2, kKRight = 1u << 3,
  kKFire = 1u << 4, kKJump = 1u << 5, kKEsc = 1u << 6, kKStrafeL = 1u << 7,
  kKStrafeR = 1u << 8, kKWeapon = 1u << 9, kKRun = 1u << 10,
};
struct KeyBit {
  uint32_t bit;
  int key;
};
// The keys id's default.cfg binds: arrows, CTRL to fire, ENTER to jump (and
// to choose, in the menus), comma and full stop to step sideways, slash for
// the next weapon, SHIFT to run.
const KeyBit kKeys[] = {
    {kKUp, K_UPARROW},   {kKDown, K_DOWNARROW}, {kKLeft, K_LEFTARROW}, {kKRight, K_RIGHTARROW},
    {kKFire, K_CTRL},    {kKJump, K_ENTER},     {kKEsc, K_ESCAPE},     {kKStrafeL, ','},
    {kKStrafeR, '.'},    {kKWeapon, '/'},       {kKRun, K_SHIFT},
};
uint32_t g_keys = 0;

constexpr int kQueueSize = 64;
uint16_t g_queue[kQueueSize];
int g_q_head = 0, g_q_tail = 0;

void pushKey(bool pressed, int key) {
  const int next = (g_q_head + 1) % kQueueSize;
  if (next == g_q_tail) return;  // full; the engine is not reading
  g_queue[g_q_head] = (uint16_t)((pressed ? 0x100 : 0) | (key & 0xFF));
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

uint16_t g_lut[256];
uint8_t g_pad = 0, g_pad_drawn = 0xFF;
uint32_t g_frames = 0, g_fps_at = 0, g_convert_us = 0;
uint8_t g_inject_pad = 0;
uint32_t g_inject_frames = 0;
uint32_t g_last_us = 0;
uint32_t g_ticks = 0;   // frames the engine has run since it started
bool g_was_in_menu = false;

void paintFrame(const uint8_t *src) {
  uint16_t *fb = emu_video::frame();
  if (!fb) return;
  const uint32_t t0 = micros();
  for (int i = 0; i < QUAKEGENERIC_RES_X * QUAKEGENERIC_RES_Y; i++) fb[i] = g_lut[src[i]];
  g_convert_us = micros() - t0;
  emu_video::present();
  g_frames++;
}

// The painter: core 0, one frame at a time. g_paint_done is free whenever no
// frame is in its hands, which is what the engine waits on before giving it
// the next, and what leaving the game waits on before the picture's buffers
// are let go.
TaskHandle_t g_painter = nullptr;
SemaphoreHandle_t g_paint_go = nullptr, g_paint_done = nullptr;
const uint8_t *volatile g_paint_src = nullptr;

void painterTask(void *) {
  for (;;) {
    xSemaphoreTake(g_paint_go, portMAX_DELAY);
    if (g_paint_src) paintFrame(g_paint_src);
    xSemaphoreGive(g_paint_done);
  }
}

// Sound, on core 0 with the speaker's own task: a few times a block, it hands
// the speaker whatever it has room for.
TaskHandle_t g_sound = nullptr;
void soundTask(void *) {
  for (;;) {
    if (g_engine == Engine::Running) QG_AudioPump();
    delay(5);
  }
}

void startPainter() {
  if (!g_sound) xTaskCreatePinnedToCore(soundTask, "quake_snd", 3072, nullptr, 3, &g_sound, 0);
  if (g_painter) return;
  g_paint_go = xSemaphoreCreateBinary();
  g_paint_done = xSemaphoreCreateBinary();
  if (!g_paint_go || !g_paint_done) return;
  xSemaphoreGive(g_paint_done);
  if (xTaskCreatePinnedToCore(painterTask, "quake_paint", 4096, nullptr, 2, &g_painter, 0) != pdPASS) {
    g_painter = nullptr;
  }
}

// Blocks until the painter has nothing in hand.
void painterIdle() {
  if (!g_painter) return;
  xSemaphoreTake(g_paint_done, portMAX_DELAY);
  xSemaphoreGive(g_paint_done);
}

// Restarts the console and comes straight back into the chosen game.
void restartInto(const char *why) {
  auto &g = uikit::gfx();
  g.fillScreen(kBg);
  uikit::drawLabel(why, kW / 2, kH / 2, kText, &fonts::FreeSansBold18pt7b, middle_center);
  g_resume.magic = kResumeMagic;
  g_resume.size = (uint8_t)g_size;
  snprintf(g_resume.dir, sizeof(g_resume.dir), "%s", g_games[g_game].dir);
  delay(1200);
  esp_restart();
}

bool resumeValid() {
  return g_resume.magic == kResumeMagic && esp_reset_reason() == ESP_RST_SW &&
         (g_resume.size == 2 || g_resume.size == 3) && memchr(g_resume.dir, 0, sizeof(g_resume.dir));
}

bool hasPak(const char *dir, const char *pak) {
  char path[112];
  struct stat st;
  snprintf(path, sizeof(path), "%s%s/%s/%s", sdcard::mountPoint(), kBaseDir, dir, pak);
  return stat(path, &st) == 0 && st.st_size > 100000;
}

// The names the two mission packs go by; anything else is known by its folder.
void titleFor(const char *dir, char *out, size_t size) {
  if (!strcmp(dir, "id1")) snprintf(out, size, "QUAKE");
  else if (!strcmp(dir, "hipnotic")) snprintf(out, size, "SCOURGE OF ARMAGON");
  else if (!strcmp(dir, "rogue")) snprintf(out, size, "DISSOLUTION OF ETERNITY");
  else {
    snprintf(out, size, "%s", dir);
    for (char *c = out; *c; c++) *c = (char)toupper((unsigned char)*c);
  }
}

// Where a folder goes in the row: Quake, the mission packs in the order they
// came out, then the rest by name.
int gameRank(const char *dir) {
  if (!strcmp(dir, "id1")) return 0;
  if (!strcmp(dir, "hipnotic")) return 1;
  if (!strcmp(dir, "rogue")) return 2;
  return 3;
}

void setStatus() {
  if (!sdcard::mounted()) {
    snprintf(g_status, sizeof(g_status), "No card. Quake's pak files go in /quake/id1 on one.");
  } else if (!g_have_pak) {
    snprintf(g_status, sizeof(g_status), "Put pak0.pak in /quake/id1 on the card.");
  } else if (g_game > 0) {
    snprintf(g_status, sizeof(g_status), "Runs from /quake/%s on the card, on top of Quake itself.",
             g_games[g_game].dir);
  } else if (!g_have_pak1) {
    snprintf(g_status, sizeof(g_status), "Shareware episode found (pak0.pak).");
  } else {
    snprintf(g_status, sizeof(g_status), "All four episodes found (pak0.pak, pak1.pak).");
  }
}

void lookForPaks() {
  g_have_pak = g_have_pak1 = false;
  g_game_count = 1;
  g_game = 0;
  snprintf(g_games[0].dir, sizeof(g_games[0].dir), "id1");
  titleFor("id1", g_games[0].title, sizeof(g_games[0].title));
  if (!sdcard::mounted()) {
    setStatus();
    return;
  }
  g_have_pak = hasPak("id1", "pak0.pak");
  g_have_pak1 = hasPak("id1", "pak1.pak");
  // The other games: the engine refuses anything but id1 without the
  // registered pak, so they are only looked for when it is there.
  if (g_have_pak && g_have_pak1) {
    char path[96];
    snprintf(path, sizeof(path), "%s%s", sdcard::mountPoint(), kBaseDir);
    if (DIR *d = opendir(path)) {
      while (struct dirent *e = readdir(d)) {
        if (g_game_count >= kMaxGames) break;
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(g_games[0].dir)) continue;
        char dir[24];
        snprintf(dir, sizeof(dir), "%s", e->d_name);
        for (char *c = dir; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (!strcmp(dir, "id1") || !hasPak(e->d_name, "pak0.pak")) continue;
        // Kept in order as they are found.
        int at = g_game_count;
        while (at > 1 && (gameRank(dir) < gameRank(g_games[at - 1].dir) ||
                          (gameRank(dir) == gameRank(g_games[at - 1].dir) &&
                           strcmp(dir, g_games[at - 1].dir) < 0))) {
          g_games[at] = g_games[at - 1];
          at--;
        }
        snprintf(g_games[at].dir, sizeof(g_games[at].dir), "%s", dir);
        titleFor(dir, g_games[at].title, sizeof(g_games[at].title));
        g_game_count++;
      }
      closedir(d);
    }
  }
  // The one chosen last time, if it is still on the card.
  for (int i = 0; i < g_game_count; i++) {
    if (!strcmp(g_games[i].dir, g_wanted)) g_game = i;
  }
  setStatus();
}

// Quake is a dark game, and a pale surround swamps it; so whatever the
// console's theme, the screen round the picture is the dark one while the
// game is up, and the theme comes back on the way out.
bool g_theme_was_light = false, g_theme_held = false;
void darkSurround(bool on) {
  if (on && !g_theme_held) {
    g_theme_was_light = theme::isLight();
    g_theme_held = true;
    theme::setLight(false);
  } else if (!on && g_theme_held) {
    g_theme_held = false;
    theme::setLight(g_theme_was_light);
  }
}

bool startVideo() {
  joypad_ui::setLabels("WEAPON", "MENU", "JUMP", "FIRE");
  // The picture's 16-bit copies go in the external RAM: internal memory is
  // worth far more to the engine's depth and frame buffers.
  emu_video::framesInExternalRam(true);
  // 320x200 on a 4:3 screen: three across and three and a half down for the
  // full height (960x700); with the pad on the screen, 500x375, which is what
  // fits between the D-pad and the buttons. The scaler's factors are
  // sixteenths, and these land on whole pixels.
  const float sx = g_size >= 3 ? 3.0f : 1.5625f, sy = g_size >= 3 ? 3.5f : 1.875f;
  if (!joypad_ui::beginPlay(false, QUAKEGENERIC_RES_X, QUAKEGENERIC_RES_Y, sx, sy)) {
    snprintf(g_error, sizeof(g_error), "no room for the picture");
    return false;
  }
  darkSurround(true);
  g_pad = 0;
  g_pad_drawn = 0xFF;
  g_quit_hold = 0;
  g_frames = 0;
  g_fps_at = millis();
  g_last_us = micros();
  return true;
}

// The picture given back, when the engine did not come up behind it.
void stopVideo() {
  joypad_ui::endPlay();
  emu_video::framesInExternalRam(false);
  darkSurround(false);
}

void leavePlay() {
  setKeys(0);  // every key up, or the game keeps walking while it waits
  painterIdle();   // before the picture's buffers go
  // The engine writes its options out when it quits, and this one is never
  // quit, only left; so they are written on the way out of the picture.
  if (g_engine == Engine::Running) Host_WriteConfiguration();
  joypad_ui::endPlay();
  emu_video::framesInExternalRam(false);
  darkSurround(false);
  M5.Speaker.stop();
  g_mode = Mode::Picking;
  g_dirty = true;
}

void reportInternal(const char *where) {
  Serial.printf("quake: internal %s: %uKB (largest %uKB), psram %uKB\n", where,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

bool start(int size) {
  g_size = size;
  g_error[0] = '\0';
  if (g_engine == Engine::Dead) restartInto("Restarting to start Quake again...");
  if (g_engine == Engine::Running && strcmp(g_game_dir, g_games[g_game].dir) != 0) {
    Host_WriteConfiguration();   // the game being left keeps its options
    restartInto("Restarting to change game...");
  }
  if (!startVideo()) return false;
  if (g_engine == Engine::Running) return true;   // it was waiting where it was left

  startPainter();
  reportInternal("before the engine");
  if (!QG_ExtRamInit()) {
    snprintf(g_error, sizeof(g_error), "out of memory for the engine's tables");
    stopVideo();
    return false;
  }
  snprintf(g_basedir, sizeof(g_basedir), "%s%s", sdcard::mountPoint(), kBaseDir);
  // The game: nothing for Quake itself; the mission packs have switches of
  // their own, which also turn on their status bars; anything else is -game.
  snprintf(g_game_dir, sizeof(g_game_dir), "%s", g_games[g_game].dir);
  g_engine_game = g_game;
  g_argc = 3;
  if (!strcmp(g_game_dir, "hipnotic") || !strcmp(g_game_dir, "rogue")) {
    snprintf(g_game_flag, sizeof(g_game_flag), "-%s", g_game_dir);
    g_argv[g_argc++] = g_game_flag;
  } else if (g_game > 0) {
    snprintf(g_game_flag, sizeof(g_game_flag), "-game");
    g_argv[g_argc++] = g_game_flag;
    g_argv[g_argc++] = g_game_dir;
  }
  g_argv[g_argc] = nullptr;
  const uint32_t t0 = millis();
  Serial.printf("quake: starting %s in %s\n", g_game_dir, g_basedir);
  if (!runEngine(Cmd::Create)) {
    g_engine = Engine::Dead;
    Serial.printf("quake: engine error at start: %s\n", g_error);
    stopVideo();
    return false;
  }
  g_engine = Engine::Running;
  // Registered now, before the first frame reads config.cfg; applied a few
  // frames in, once it has (runFrame).
  QG_DefaultsInit();
  g_ticks = 0;
  Serial.printf("quake: up in %lu ms, stack left %u\n", (unsigned long)(millis() - t0),
                (unsigned)uxTaskGetStackHighWaterMark(g_task));
  reportInternal("with the engine up");
  g_last_us = micros();
  return true;
}

void drawPlayChrome(bool full) {
  if (full) {
    uikit::gfx().fillScreen(kBg);
    g_pad_drawn = 0xFF;
  }
  if (g_size >= 3) {
    joypad_ui::drawGamepadChrome(full);
    g_pad_drawn = g_pad;
    return;
  }
  joypad_ui::drawControls(g_pad, g_pad_drawn, full);
  g_pad_drawn = g_pad;
}

// The pads, as the engine's keyboard. Directions walk and turn; B fires; A
// jumps, and chooses in the menus; START is the menu; SELECT and X step
// through the weapons; Y runs; L and R step sideways.
void readPad() {
  bool menu_held = false;
  g_pad = joypad_ui::pollPad(&menu_held);
  if (g_size >= 3) g_pad = 0;  // no on-screen controls to hit
  if (g_inject_frames) {
    g_pad |= g_inject_pad;
    g_inject_frames--;
  }
  uint32_t keys = 0;
  const usbpad::State u = usbpad::state();
  if (u.connected) {
    // A and B only: X and Y have jobs of their own here, so they are taken
    // off the eight-button byte, where they stand in for A and B.
    const bool x = (u.down >> 0) & 1u, a = (u.down >> 1) & 1u;   // the GP100's numbering
    const bool b = (u.down >> 2) & 1u, y = (u.down >> 3) & 1u;
    uint8_t nes = padmap::toNes(u.down, u.x, u.y, g_padmap);
    if (x && !a) nes &= (uint8_t)~joypad::kA;
    if (y && !b) nes &= (uint8_t)~joypad::kB;
    g_pad |= nes;
    if (x) keys |= kKWeapon;
    if (y) keys |= kKRun;
    if (u.down & (1u << 4)) keys |= kKStrafeL;
    if (u.down & (1u << 5)) keys |= kKStrafeR;
  }
  {
    const uint16_t b = buspad::buttons();
    const auto on = [b](busmap::Button n) { return (b >> n) & 1u; };
    if (on(busmap::kUp)) g_pad |= joypad::kUp;
    if (on(busmap::kDown)) g_pad |= joypad::kDown;
    if (on(busmap::kLeft)) g_pad |= joypad::kLeft;
    if (on(busmap::kRight)) g_pad |= joypad::kRight;
    if (on(busmap::kA)) g_pad |= joypad::kA;
    if (on(busmap::kB)) g_pad |= joypad::kB;
    if (on(busmap::kSelect)) g_pad |= joypad::kSelect;
    if (on(busmap::kStart)) g_pad |= joypad::kStart;
    if (on(busmap::kX)) keys |= kKWeapon;
    if (on(busmap::kY)) keys |= kKRun;
    if (on(busmap::kL)) keys |= kKStrafeL;
    if (on(busmap::kR)) keys |= kKStrafeR;
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
  if (g_pad & joypad::kA) keys |= kKJump;
  if (g_pad & joypad::kStart) keys |= kKEsc;
  if (g_pad & joypad::kSelect) keys |= kKWeapon;
  setKeys(keys);
}

void runFrame(uint32_t now_ms) {
  readPad();
  if (g_mode != Mode::Playing) return;

  const uint32_t now_us = micros();
  g_tick_seconds = (double)(now_us - g_last_us) / 1000000.0;
  g_last_us = now_us;
  if (!runEngine(Cmd::Tick)) {
    g_engine = Engine::Dead;
    Serial.printf("quake: engine stopped: %s\n", g_error[0] ? g_error : "quit");
    leavePlay();
    return;
  }
  // The first frame runs quake.rc, and with it config.cfg; after that the
  // engine knows whether this card has had its first-run settings.
  if (++g_ticks == 3 && QG_DefaultsApply()) Host_WriteConfiguration();
  // The engine writes its options only when it quits, and this one is far
  // more often switched off than quit. So they are written whenever its menu
  // closes, which is where they are changed.
  const bool in_menu = QG_InMenu() != 0;
  if (g_was_in_menu && !in_menu && g_ticks > 3) Host_WriteConfiguration();
  g_was_in_menu = in_menu;
  drawPlayChrome(false);

  if (now_ms - g_fps_at >= 2000) {
    static unsigned blocks_before = 0;
    const unsigned blocks = QG_AudioBlocks();
    Serial.printf("quake %lu fps  convert=%luus push=%luus  sound %u blocks/s peak %d  stack left %u  internal %uKB\n",
                  (unsigned long)(g_frames * 1000 / (now_ms - g_fps_at)),
                  (unsigned long)g_convert_us, (unsigned long)emu_video::lastUs(),
                  (unsigned)((blocks - blocks_before) * 1000 / (now_ms - g_fps_at)), QG_AudioPeak(),
                  (unsigned)uxTaskGetStackHighWaterMark(g_task),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    blocks_before = blocks;
    g_frames = 0;
    g_fps_at = now_ms;
  }
}

// ---- the screen before the game ---------------------------------------------

enum : int { kActTouch = 1, kActFull = 2, kActBack = 3, kActGame = 4 };

void drawPicking() {
  auto &g = uikit::gfx();
  g.fillScreen(kBg);
  uikit::clearTargets();
  uikit::drawLabel("QUAKE", 48, 22, kText, &fonts::FreeSansBold24pt7b);
  const Rect back{kW - 48 - 190, 18, 190, 62};
  uikit::drawButton(back, "MENU", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  uikit::addTarget(back, kActBack);

  uikit::drawLabel(g_status, 48, 110, g_have_pak ? kMuted : kDanger, &fonts::FreeSans12pt7b);
  if (g_error[0]) uikit::drawLabel(g_error, 48, 150, kDanger, &fonts::FreeSans12pt7b);

  // The games, when there is more than Quake itself: one row, the chosen one
  // filled in.
  const bool row = g_game_count > 1;
  if (row) {
    const int gap = 14, w = (kW - 96 - gap * (g_game_count - 1)) / g_game_count;
    for (int i = 0; i < g_game_count; i++) {
      const Rect r{48 + i * (w + gap), 150, w, 64};
      const bool on = i == g_game;
      uikit::drawButton(r, g_games[i].title, on ? kAccent : kSurfaceLift, on ? kOnFill : kText,
                        g_game_count > 3 ? &fonts::FreeSansBold9pt7b : &fonts::FreeSansBold12pt7b);
      uikit::addTarget(r, kActGame, i);
    }
  }
  const bool other = g_engine == Engine::Running && strcmp(g_game_dir, g_games[g_game].dir) != 0;

  if (g_have_pak) {
    const int top = row ? 250 : 230;
    const Rect touch{48, top, 560, 150}, full{kW - 48 - 560, top, 560, 150};
    uikit::drawButton(touch, "PLAY  -  TOUCH PAD", kSurfaceLift, kText, &fonts::FreeSansBold18pt7b);
    uikit::drawButton(full, "PLAY  -  GAMEPAD", kGood, kOnFill, &fonts::FreeSansBold18pt7b);
    uikit::addTarget(touch, kActTouch);
    uikit::addTarget(full, kActFull);
    uikit::drawLabel("500 x 375 with the pad on the screen", touch.x + touch.w / 2, touch.y + touch.h + 26,
                     kMuted, &fonts::FreeSans9pt7b, middle_center);
    uikit::drawLabel("960 x 700, for the built-in pad or a USB one", full.x + full.w / 2, full.y + full.h + 26,
                     kMuted, &fonts::FreeSans9pt7b, middle_center);
    const char *const lines[] = {
        "D-pad walks and turns.  B fires.  A jumps, and chooses in the menus.",
        "L and R step sideways.  X or SELECT changes weapon.  Y runs.  START is the game's menu.",
        "Hold SELECT and START together to come back here; the game waits where it was.",
    };
    for (int i = 0; i < 3; i++) {
      uikit::drawLabel(lines[i], 48, top + 240 + i * 34, kMuted, &fonts::FreeSans12pt7b);
    }
    if (other) {
      char note[96];
      char waiting[32];
      titleFor(g_game_dir, waiting, sizeof(waiting));
      snprintf(note, sizeof(note), "%s is waiting. Playing this one restarts the console first.", waiting);
      uikit::drawLabel(note, 48, top + 240 + 3 * 34 + 8, kText, &fonts::FreeSans12pt7b);
    }
  }
  uikit::present();
}

// PLAY, from a tap or on the way back from a restart.
void play(int size) {
  uikit::drawLabel("Loading...", kW / 2, 684, kText, &fonts::FreeSansBold18pt7b, middle_center);
  uikit::present();
  if (start(size)) {
    g_mode = Mode::Playing;
    g_menu_down = true;  // the tap that launched it must not also exit
  } else {
    audio::reject();
  }
  g_dirty = true;
}

}  // namespace

bool available() { return true; }

bool resumePending() { return resumeValid(); }

void begin() {
  g_mode = Mode::Picking;
  settings_store::loadPadMap(&g_padmap);
  g_quit_hold = 0;
  g_menu_down = false;
  settings_store::loadQuakeGame(g_wanted, sizeof(g_wanted));
  // Back from a restart that was asked for here: the same game, straight in.
  g_autostart = 0;
  if (resumeValid()) {
    snprintf(g_wanted, sizeof(g_wanted), "%s", g_resume.dir);
    g_autostart = g_resume.size;
  }
  g_resume.magic = 0;
  lookForPaks();
  if (g_autostart && (!g_have_pak || strcmp(g_games[g_game].dir, g_wanted) != 0)) g_autostart = 0;
  g_dirty = true;
}

void invalidate() { g_dirty = true; }

bool playing() { return g_mode == Mode::Playing; }

void injectPad(uint8_t buttons, uint32_t frames) {
  g_inject_pad = buttons;
  g_inject_frames = frames;
}

void console(const char *line) {
  // The engine's task is idle whenever the loop is running, so its command
  // buffer can be appended to from here.
  if (g_engine != Engine::Running || !line || !line[0]) return;
  static char text[160];
  snprintf(text, sizeof(text), "%s\n", line);
  Cbuf_AddText(text);
}

void tick(uint32_t now_ms) {
  if (g_mode == Mode::Playing) {
    if (g_dirty) {
      g_dirty = false;
      drawPlayChrome(true);
    }
    runFrame(now_ms);
    return;
  }
  if (g_dirty) {
    g_dirty = false;
    drawPicking();
  }
  if (g_autostart) {
    const int size = g_autostart;
    g_autostart = 0;
    play(size);
  }
}

void handleTap(int x, int y, uint32_t) {
  if (g_mode == Mode::Playing) return;
  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return;
  if (action == kActBack) {
    app::requestExit();
    return;
  }
  if (action == kActGame) {
    if (param < 0 || param >= g_game_count || param == g_game) return;
    audio::select();
    g_game = param;
    snprintf(g_wanted, sizeof(g_wanted), "%s", g_games[g_game].dir);
    settings_store::saveQuakeGame(g_wanted);
    setStatus();
    g_dirty = true;
    return;
  }
  if (action != kActTouch && action != kActFull) return;
  audio::select();
  play(action == kActFull ? 3 : 2);
}

}  // namespace quake_ui
}  // namespace tabulous

// ---- the platform, as quakegeneric asks for it --------------------------------

using namespace tabulous;
using namespace tabulous::quake_ui;

extern "C" {

void *QG_ExtAlloc(size_t bytes) {
  return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// Zeroed memory for what the renderer touches on every pixel: internal RAM
// if there is a block that big, the external RAM if not.
void *QG_FastAlloc(size_t bytes, const char *what) {
  void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const bool fast = p != nullptr;
  if (!p) p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf("quake: %s (%u KB) in %s\n", what, (unsigned)(bytes / 1024),
                fast ? "internal RAM" : "external RAM");
  return p;
}

void QG_Init(void) {}

void QG_Print(const char *text) { Serial.print(text); }

void QG_Error(const char *text) {
  snprintf(g_error, sizeof(g_error), "%s", text);
  Serial.printf("quake: Sys_Error: %s\n", text);
}

// Both ways out of the engine end here: a Sys_Error, and QUIT from its own
// menu. Neither may return into it.
void QG_Quit(void) {
  if (g_jmp_armed) longjmp(g_jmp, 1);
  for (;;) delay(1000);
}

void QG_SetPalette(unsigned char palette[768]) {
  for (int i = 0; i < 256; i++) {
    g_lut[i] = uikit::gfx().color565(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
  }
}

// A finished frame. The engine draws in 8-bit colour; turning that into the
// panel's sixteen and handing it to the scaler takes about four milliseconds,
// a tenth of a frame, and none of it needs the engine. So it happens on the
// other core: the engine flips to its second page and starts the next frame
// while this one is on its way to the glass.
void QG_DrawFrame(void *pixels) {
  if (!pixels) return;
  if (!g_painter) {   // no second task: do it here
    paintFrame((const uint8_t *)pixels);
    return;
  }
  xSemaphoreTake(g_paint_done, portMAX_DELAY);   // the frame before this one is out
  g_paint_src = (const uint8_t *)pixels;
  xSemaphoreGive(g_paint_go);
}

int QG_GetKey(int *down, int *key) {
  if (g_q_tail == g_q_head) return 0;
  const uint16_t e = g_queue[g_q_tail];
  g_q_tail = (g_q_tail + 1) % kQueueSize;
  *down = (e & 0x100) ? 1 : 0;
  *key = e & 0xFF;
  return 1;
}

void QG_GetMouseMove(int *x, int *y) {
  *x = 0;
  *y = 0;
}

void QG_GetJoyAxes(float *axes) {
  for (int i = 0; i < QUAKEGENERIC_JOY_MAX_AXES; i++) axes[i] = 0.0f;
}

// The sound device is third_party/quakegeneric/source/snd_tabulous.c; these
// are the speaker it plays through. A block is queued only while the game is
// on the screen, so leaving it stops the sound where it was.
int QG_AudioWants(void) {
  return g_mode == Mode::Playing && audio::enabled() && M5.Speaker.isPlaying(kAudioChannel) < 2;
}

void QG_AudioPush(const short *pcm, int count, int rate) {
  M5.Speaker.playRaw(pcm, (size_t)count, (uint32_t)rate, false, 1, kAudioChannel);
}

}  // extern "C"

#endif  // HAVE_QUAKE
