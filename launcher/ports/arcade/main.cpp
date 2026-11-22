// Tabulous5's arcade machine (the 1980 Namco Pac-Man board) as a P4 Launcher app.
//
// The machine itself is the console's, untouched: third_party/chips (namco.h and our namco_fast.h),
// third_party/z80, and src/arcrom for the .arc container. What is here is only what the console's
// src/pacman_ui.cpp did with M5Unified and FreeRTOS, done with the launcher's SDK instead: pick a file,
// build the board, and each frame read the pad, run the machine, stand the picture up, hand over sound.
#include "papp/papp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chips_common.h"
#include "z80.h"
#include "clk.h"
#include "mem.h"
#include "namco.h"
#include "namco_fast.h"

#include "arcrom.h"
#include "fourway.h"
#include "hiscore.h"

using namespace tabulous;

// In C++ a const object at file scope is private to the file and its name is mangled, and the packer looks the
// manifest up by name: without this line the app is packed with no manifest at all, and nothing says so.
extern "C" { extern const papp_manifest_t __papp_manifest; }
PAPP_MANIFEST(.id = "tabulous.arcade", .name = "Arcade", .version = PAPP_APP_VERSION, .main_stack = 32768);

static const int kRasterW = NAMCO_DISPLAY_WIDTH;    // 288
static const int kRasterH = NAMCO_DISPLAY_HEIGHT;   // 224
static const uint32_t kFrameUs = 16667;
static const int kAudioRate = 44100;

// The console's pad bits, which fourway.h and the cabinet mapping below are written in.
enum : uint8_t { kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kSelect = 16, kStart = 32, kA = 64, kB = 128 };

static namco_t *g_sys;
static uint8_t *g_fb;
static sz80 g_cpu;
static uint16_t g_palette[32];
static uint8_t *g_alt_rom;
static bool g_stand_up = true, g_eight_way, g_start_fires, g_far_b_is_up;
// Whether this cabinet's control panel leaves the pad's face buttons with nothing to do. When it does they are
// a second stick (X up, B down, Y left, A right), as on the console, and the menu and volume move to L and R.
static bool g_buttons_as_stick;
static fourway::Gate g_gate(kUp, kDown, kLeft, kRight);
static fourway::Gate g_gate2(kUp, kDown, kLeft, kRight);   // the far side's stick, on a cocktail table

// The cabinet's settings, kept where the console keeps them (its settings file, keys arc_dsw1 and arc_cocktail):
// the board's eight DIP switches as the hardware reads them, and the strap that says which cabinet it is in.
static uint8_t g_dsw1 = 0xD9;                  // as a Pac-Man left the factory: 1 coin 1 game, 3 lives, bonus at 15,000
static bool g_cocktail;
static const uint8_t kIn1Cocktail = 0x80;
struct Dip { const char *name; uint8_t mask, shift, count; const char *values[4]; };
static const Dip kDips[] = {
    {"Coins", 0x03, 0, 4, {"Free play", "1 coin, 1 game", "1 coin, 2 games", "2 coins, 1 game"}},
    {"Lives", 0x0C, 2, 4, {"1", "2", "3", "5"}},
    {"Bonus life", 0x30, 4, 4, {"10,000", "15,000", "20,000", "None"}},
    {"Difficulty", 0x40, 6, 2, {"Hard", "Normal", "", ""}},
    {"Ghost names", 0x80, 7, 2, {"Alternate", "Normal", "", ""}},
};
static const int kDipCount = 5;

// The settings file is the one our touch games' Preferences keeps (ports/m5compat): records of
// [key length][key][value length, 4 bytes][value]. Read whole, one value changed, written whole.
static const char *const kPrefs = "/sd/tabulous/phrasecraze.prefs";
static uint8_t *g_prefs_buf; static size_t g_prefs_len;
static void prefsLoad()
{
    FILE *f = fopen(kPrefs, "rb");
    if (!f) return;
    g_prefs_buf = (uint8_t *)malloc(16384);
    g_prefs_len = g_prefs_buf ? fread(g_prefs_buf, 1, 16384, f) : 0;
    fclose(f);
}
static uint8_t *prefsFind(const char *key, uint32_t *len)
{
    const size_t kl = strlen(key);
    for (size_t i = 0; i + 5 <= g_prefs_len;) {
        const size_t n = g_prefs_buf[i];
        if (i + 1 + n + 4 > g_prefs_len) break;
        uint32_t vl; memcpy(&vl, g_prefs_buf + i + 1 + n, 4);
        if (i + 5 + n + vl > g_prefs_len) break;
        if (n == kl && !memcmp(g_prefs_buf + i + 1, key, kl)) { *len = vl; return g_prefs_buf + i + 5 + n; }
        i += 5 + n + vl;
    }
    return nullptr;
}
static uint8_t prefsByte(const char *key, uint8_t def) { uint32_t l; const uint8_t *v = g_prefs_buf ? prefsFind(key, &l) : nullptr; return v && l == 1 ? *v : def; }
static void prefsPutByte(const char *key, uint8_t value)
{
    if (!g_prefs_buf) { g_prefs_buf = (uint8_t *)malloc(16384); g_prefs_len = 0; if (!g_prefs_buf) return; }
    uint32_t l;
    uint8_t *v = prefsFind(key, &l);
    if (v && l == 1) *v = value;
    else if (!v && g_prefs_len + strlen(key) + 6 <= 16384) {
        uint8_t *o = g_prefs_buf + g_prefs_len; const uint32_t one = 1;
        *o++ = (uint8_t)strlen(key); memcpy(o, key, strlen(key)); o += strlen(key); memcpy(o, &one, 4); o += 4; *o++ = value;
        g_prefs_len = (size_t)(o - g_prefs_buf);
    } else return;
    FILE *f = fopen(kPrefs, "wb");
    if (!f) { papp_fs() ? (void)papp_fs()->mkdir("/sd/tabulous") : (void)0; f = fopen(kPrefs, "wb"); }
    if (f) { fwrite(g_prefs_buf, 1, g_prefs_len, f); fclose(f); }
}

// A frame's samples arrive inside namco_fast_run(), 128 at a time; they wait here until the frame is done.
static int16_t g_pcm[2048];
static int g_pcm_n;
static uint32_t g_pcm_lost;
static void onAudio(const float *samples, int n, void *)
{
    for (int i = 0; i < n; i++) {
        if (g_pcm_n >= (int)(sizeof g_pcm / sizeof g_pcm[0])) { g_pcm_lost += (uint32_t)(n - i); return; }
        float v = samples[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        g_pcm[g_pcm_n++] = (int16_t)(v * 32000.0f);
    }
}

// The P4 has two kinds of internal RAM, and the launcher's heap hands out both as "internal": the main kind, and
// 32 KB of the low-power domain's, which sits behind a slow bus (measured here: fifty times slower to read and
// write). The heap falls back to it when the main kind has no piece big enough. A machine whose ROM or whose
// thread's stack landed there ran at 5 frames a second, so nothing of ours is left there: PSRAM is far quicker.
static bool inSlowRam(const void *p) { return ((uintptr_t)p >> 28) == 5; }
static void *fastAlloc(size_t size)
{
    void *p = papp_alloc(size, PAPP_MEM_FAST, 64);
    if (p && inSlowRam(p)) { papp_free(p); p = nullptr; }
    return p;
}

static bool loadMachine(const char *path, const char **why)
{
    FILE *f = fopen(path, "rb");
    if (!f) { *why = "could not open the file"; return false; }
    uint8_t *rom = (uint8_t *)papp_alloc(arcrom::kMaxFileBytes, PAPP_MEM_ANY, 0);
    if (!rom) { fclose(f); *why = "out of memory for the file"; return false; }
    size_t got = fread(rom, 1, arcrom::kMaxFileBytes, f);
    fclose(f);
    arcrom::Info info;
    const char *bad = "";
    if (!arcrom::parse(rom, got, &info, &bad)) { papp_free(rom); *why = bad[0] ? bad : "not an .arc file"; return false; }

    // Where the machine sits decides the frame rate: its framebuffer and RAM are hammered a byte at a time.
    // Where the machine sits decides the frame rate: its RAM and ROM are read a byte at a time by the Z80. All of
    // it with its picture is 177 KB, and the launcher has no piece of internal RAM that big (128 KB at most on the
    // Tab5), so the picture is kept apart (NAMCO_FB_SEPARATE): the machine is then 62 KB and goes in internal
    // RAM, and the picture, written once a frame from end to end, goes wherever there is room.
    g_sys = (namco_t *)fastAlloc(sizeof(namco_t));
    const bool internal = g_sys != nullptr;
    if (!g_sys) g_sys = (namco_t *)papp_alloc(sizeof(namco_t), PAPP_MEM_ANY, 64);
    g_fb = (uint8_t *)fastAlloc(NAMCO_FRAMEBUFFER_SIZE_BYTES);
    const bool fb_internal = g_fb != nullptr;
    if (!g_fb) g_fb = (uint8_t *)papp_alloc(NAMCO_FRAMEBUFFER_SIZE_BYTES, PAPP_MEM_ANY, 64);
    if (!g_sys || !g_fb) { papp_free(rom); *why = "out of memory for the machine"; return false; }
    memset(g_sys, 0, sizeof(namco_t));
    memset(g_fb, 0, NAMCO_FRAMEBUFFER_SIZE_BYTES);
    papp_log("arcade: machine is %u bytes, in %s; its picture in %s\n", (unsigned)sizeof(namco_t), internal ? "internal SRAM" : "PSRAM", fb_internal ? "internal SRAM" : "PSRAM");

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
    g_sys->fb = g_fb;                             // namco_init cleared the struct

    static namco_fast_desc_t board;
    memset(&board, 0, sizeof board);
    if (info.daughtercard) {
        const size_t room = arcrom::kCpuBytes + arcrom::kMaxCpuHighBanks * arcrom::kCpuHighBank;
        g_alt_rom = (uint8_t *)fastAlloc(room);
        papp_log("arcade: the Ms. Pac-Man kit's program, %u bytes, is in %s\n", (unsigned)room, g_alt_rom ? "internal SRAM" : "PSRAM");
        if (!g_alt_rom) g_alt_rom = (uint8_t *)papp_alloc(room, PAPP_MEM_ANY, 0);
        if (!g_alt_rom) { papp_free(rom); *why = "out of memory for the Ms. Pac-Man board"; return false; }
        // The kit answers over all of 0x8000-0xBFFF and its program is shorter than that: what is left over has to
        // read the same every time, not as whatever the heap last held.
        memset(g_alt_rom, 0, room);
        memcpy(g_alt_rom, rom + info.alt_cpu, arcrom::kCpuBytes + info.cpu_high_bytes);
        board.rom_alt_low = g_alt_rom;
        board.rom_alt_high = g_alt_rom + arcrom::kCpuBytes;
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
    g_sys->dsw1 = g_dsw1;                         // read by the game when it starts
    hiscore::identify(rom + info.cpu, info.file_bytes - info.cpu, path);
    papp_free(rom);                               // namco_init copied every ROM into the machine

    for (int i = 0; i < 32; i++) {
        const uint32_t c = g_sys->hw_colors[i];   // 0xAABBGGRR
        g_palette[i] = PAPP_RGB565(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
    }
    g_stand_up = info.upright_monitor;
    g_eight_way = info.eight_way;
    g_start_fires = info.start_fires;
    g_far_b_is_up = info.far_b_is_up;
    g_buttons_as_stick = !info.eight_way && !info.uses_button && !info.start_fires;
    return true;
}

// Palette indices to colour, standing the raster up on the way: a source row becomes a destination column.
// The destination is written a line at a time from left to right, and the source read down its columns: the
// surface is in PSRAM and bigger than the cache in front of it, and written the other way round (a column at a
// time) it kept the memory bus busy enough to cost the machine on the other core two milliseconds a frame.
static void convert(uint16_t *dst, int stride)
{
    if (!g_stand_up) {
        for (int row = 0; row < kRasterH; row++) {
            const uint8_t *src = g_sys->fb + (size_t)row * NAMCO_FRAMEBUFFER_WIDTH;
            uint16_t *out = dst + (size_t)row * stride;
            for (int col = 0; col < kRasterW; col++) out[col] = g_palette[src[col] & 31];
        }
        return;
    }
    for (int y = 0; y < kRasterW; y++) {                 // the destination is kRasterH wide and kRasterW tall
        const uint8_t *src = g_sys->fb + (size_t)(kRasterH - 1) * NAMCO_FRAMEBUFFER_WIDTH + y;
        uint16_t *out = dst + (size_t)y * stride;
        for (int x = 0; x < kRasterH; x++) { out[x] = g_palette[*src & 31]; src -= NAMCO_FRAMEBUFFER_WIDTH; }
    }
}

// A player's pad as the console's pad bits; `raw` gets the launcher's own bits, for the menu and volume buttons.
static uint8_t readPad(int player, uint32_t *raw)
{
    papp_gamepad_t p;
    uint32_t b = 0;
    if (papp_input_gamepad(player, &p)) {
        b = p.buttons;
        if (p.lx < -16000) b |= PAPP_BTN_LEFT;      // a stick is a stick
        if (p.lx > 16000) b |= PAPP_BTN_RIGHT;
        if (p.ly < -16000) b |= PAPP_BTN_UP;
        if (p.ly > 16000) b |= PAPP_BTN_DOWN;
    }
    *raw = b;
    uint8_t m = 0;
    if (b & PAPP_BTN_UP) m |= kUp;
    if (b & PAPP_BTN_DOWN) m |= kDown;
    if (b & PAPP_BTN_LEFT) m |= kLeft;
    if (b & PAPP_BTN_RIGHT) m |= kRight;
    if (b & PAPP_BTN_SELECT) m |= kSelect;
    if (b & PAPP_BTN_START) m |= kStart;
    if (g_buttons_as_stick) {                       // the face diamond is a second stick, and only that
        if (b & PAPP_BTN_X) m |= kUp;
        if (b & PAPP_BTN_B) m |= kDown;
        if (b & PAPP_BTN_Y) m |= kLeft;
        if (b & PAPP_BTN_A) m |= kRight;
    } else {
        if (b & PAPP_BTN_A) m |= kA;
        if (b & PAPP_BTN_B) m |= kB;
    }
    return m;
}

static uint32_t toCabinet(uint8_t pad)
{
    uint32_t m = 0;
    if (pad & kUp) m |= NAMCO_INPUT_P1_UP;
    if (pad & kDown) m |= NAMCO_INPUT_P1_DOWN;
    if (pad & kLeft) m |= NAMCO_INPUT_P1_LEFT;
    if (pad & kRight) m |= NAMCO_INPUT_P1_RIGHT;
    if (pad & kSelect) m |= NAMCO_INPUT_P1_COIN;
    if (pad & kStart) m |= NAMCO_INPUT_P1_START;
    if (pad & kA) m |= NAMCO_INPUT_P2_START;
    if (g_start_fires && (pad & kB)) m |= NAMCO_INPUT_P1_START;
    return m;
}

// The stick on the far side of a cocktail table. That side has nothing else: the coin door and both start
// buttons are shared. A pad has them anyway, so its SELECT is a coin and its START and A the two-player start.
static uint32_t toFarSide(uint8_t pad)
{
    uint32_t m = 0;
    if (pad & kUp) m |= NAMCO_INPUT_P2_UP;
    if (pad & kDown) m |= NAMCO_INPUT_P2_DOWN;
    if (pad & kLeft) m |= NAMCO_INPUT_P2_LEFT;
    if (pad & kRight) m |= NAMCO_INPUT_P2_RIGHT;
    if (pad & kSelect) m |= NAMCO_INPUT_P1_COIN;
    if (pad & (kStart | kA)) m |= NAMCO_INPUT_P2_START;
    if (g_far_b_is_up && (pad & kB)) m |= NAMCO_INPUT_P2_UP;
    return m;
}

// The painter, as on the console: standing the picture up and handing it to the launcher needs nothing from the
// machine, so it runs on the other core while the machine works on the next frame. Measured on the Tab5 without
// it: 14 ms a frame for the machine and 3.5 ms for the picture, which is more than the 16.7 there are.
static papp_surface_t *g_surf;
static int g_bench;                                // 1: no picture at all, 2: picture made but not shown (timing experiments)
static papp_sem_t *g_paint_go, *g_paint_done, *g_paint_idle;
static volatile bool g_paint_quit;
static volatile uint32_t g_us_draw, g_us_show;
static int painter(void *)
{
    for (;;) {
        papp_sem_take(g_paint_go, PAPP_WAIT_FOREVER);
        if (g_paint_quit) break;
        const int64_t t0 = papp_time_us();
        int stride;
        uint16_t *px = (uint16_t *)papp_disp_lock(g_surf, &stride);
        if (px && g_bench != 1) convert(px, stride);
        const int64_t t1 = papp_time_us();
        papp_sem_give(g_paint_done);                 // the machine may draw its next picture now
        if (px && !g_bench) papp_disp_present(g_surf);
        g_us_draw = g_us_draw + (uint32_t)(t1 - t0); g_us_show = g_us_show + (uint32_t)(papp_time_us() - t1);
        papp_sem_give(g_paint_idle);                 // and the frame is on its way to the glass
    }
    return 0;
}

// Sound, as on the console: the machine makes 44,100 samples for every second of its own time and the speaker
// plays 44,100 a second of real time, and those clocks are never quite the same. Each frame's samples are
// stretched or squeezed by a fraction of a percent towards whatever keeps the launcher's ring near its target,
// so a frame that runs late is covered by what is already queued instead of being heard as a click.
static papp_audio_stream_t *g_audio;
static const int kRingTarget = kAudioRate * 50 / 1000;      // what the launcher's ring is kept near: 50 ms
static float g_fill_avg = (float)kRingTarget;
static bool g_audio_primed;
static void sendAudio()
{
    if (!g_audio || !g_pcm_n) return;
    papp_audio_stats_t st = {};
    papp_audio_stats(g_audio, &st);
    if (!g_audio_primed) {                                   // start, or ran dry: build up to the target with silence
        static int16_t quiet[512];
        for (int left = kRingTarget - (int)st.queued_frames; left > 0; left -= 512) papp_audio_write(g_audio, quiet, (size_t)(left > 512 ? 512 : left), 0);
        g_fill_avg = (float)kRingTarget;
        g_audio_primed = true;
    } else {
        if (st.queued_frames == 0) g_audio_primed = false;
        g_fill_avg += ((float)st.queued_frames - g_fill_avg) / 8.0f;
    }
    float rate = (float)kAudioRate + 1.5f * (g_fill_avg - (float)kRingTarget);    // the rate these samples should be played at
    if (rate < kAudioRate * 0.92f) rate = kAudioRate * 0.92f;
    if (rate > kAudioRate * 1.04f) rate = kAudioRate * 1.04f;
    static int16_t out[2304];
    int n = (int)((float)g_pcm_n * (float)kAudioRate / rate + 0.5f);              // played faster = fewer samples sent
    if (n > (int)(sizeof out / sizeof out[0])) n = sizeof out / sizeof out[0];
    if (n < 2 || g_pcm_n < 2) return;
    const uint32_t step = (uint32_t)(((uint64_t)(g_pcm_n - 1) << 16) / (uint32_t)(n - 1));
    uint32_t pos = 0;
    for (int i = 0; i < n; i++, pos += step) {
        const int k = (int)(pos >> 16), f = (int)(pos & 0xFFFF);
        const int a = g_pcm[k], b = g_pcm[k + 1 < g_pcm_n ? k + 1 : k];
        out[i] = (int16_t)(a + (((b - a) * f) >> 16));
    }
    papp_audio_write(g_audio, out, (size_t)n, 0);
}

// The frame loop runs on a thread of its own, on core 1. The app's main thread is pinned to core 0, which it
// shares with the launcher's USB, Wi-Fi and input tasks: there the machine took 17 ms a frame where it has 16.7.
// The launcher's overlays (menu, volume) are asked for here and put up by the main thread.
enum { kUiNone = 0, kUiMenu, kUiVolume };
enum { kPickResume = 0, kPickAnother = 1, kPickExit = 3 };
static volatile int g_ui_want, g_ui_answer;
static papp_sem_t *g_ui_done, *g_machine_go;
static volatile bool g_stop, g_touched;
static volatile int g_rc;
static bool g_painter_on;

// The board's switches and the cabinet strap, one row each: choosing a row steps it to its next setting.
static void cabinetMenu()
{
    int at = 0;
    for (;;) {
        static char text[kDipCount + 1][48];
        const char *items[kDipCount + 2];
        for (int i = 0; i < kDipCount; i++) {
            snprintf(text[i], sizeof text[i], "%s: %s", kDips[i].name, kDips[i].values[(g_dsw1 & kDips[i].mask) >> kDips[i].shift]);
            items[i] = text[i];
        }
        snprintf(text[kDipCount], sizeof text[kDipCount], "Cabinet: %s", g_cocktail ? "Cocktail table" : "Upright");
        items[kDipCount] = text[kDipCount];
        items[kDipCount + 1] = "Done";
        const int r = papp_ui_menu("Cabinet switches (read when a game starts)", items, kDipCount + 2, at);
        if (r < 0 || r == kDipCount + 1) return;
        at = r;
        if (r == kDipCount) { g_cocktail = !g_cocktail; prefsPutByte("arc_cocktail", g_cocktail ? 1 : 0); continue; }
        const Dip &d = kDips[r];
        const int next = (((g_dsw1 & d.mask) >> d.shift) + 1) % d.count;
        g_dsw1 = (uint8_t)((g_dsw1 & ~d.mask) | (next << d.shift));
        prefsPutByte("arc_dsw1", g_dsw1);
        if (g_sys) g_sys->dsw1 = g_dsw1;
    }
}

static int runUi(int what)
{
    if (what == kUiVolume) return papp_ui_volume();
    for (;;) {
        static const char *const items[] = {"Resume game", "Another game", "Cabinet switches", "Exit"};
        const int r = papp_ui_menu("Arcade", items, 4, 0);
        if (r != 2) return r;
        cabinetMenu();
    }
}

static int askUi(int what)
{
    if (!g_ui_done) return runUi(what);                // no thread of our own: this is the main one
    g_ui_want = what;
    papp_sem_take(g_ui_done, PAPP_WAIT_FOREVER);
    return g_ui_answer;
}

static int machine(void *)
{
    if (g_machine_go) papp_sem_take(g_machine_go, PAPP_WAIT_FOREVER);   // made before the game was loaded; wait for it
    if (g_stop) return 0;
    { int here; if (inSlowRam(&here)) papp_log("arcade: the machine's thread has its stack in slow RAM (%p): expect a crawl\n", (void *)&here); }
    int64_t next = papp_time_us(), stat_at = next;
    uint32_t frames = 0, late = 0, us_emu = 0, us_draw = 0, us_show = 0, us_wait = 0, us_dec = 0, skipped = 0, us_run = 0;
    bool menu_was = false, vol_was = false, painting = false, behind = false;   // painting: a frame is with the painter
    while (!g_stop && !papp_exit_requested()) {
        // Player one's pad, and a second one: on a cocktail table it is the far side's stick, otherwise it is one
        // more way to hold the only stick there is. The menu is X and the volume Y, or L and R (and only those
        // where the face buttons are a stick), or a tap on the screen for the menu.
        uint32_t raw, raw2;
        uint8_t pad = readPad(0, &raw);
        uint8_t pad2 = readPad(1, &raw2);
        if (!g_cocktail) { pad |= pad2; pad2 = 0; }
        raw |= raw2;
        const bool touched = g_touched;                  // read by the main thread: asking the panel takes 8 ms
        const uint32_t menu_mask = PAPP_BTN_L1 | (g_buttons_as_stick ? 0 : PAPP_BTN_X), vol_mask = PAPP_BTN_R1 | (g_buttons_as_stick ? 0 : PAPP_BTN_Y);
        const bool menu = (raw & menu_mask) != 0 || touched, vol = (raw & vol_mask) != 0;
        if ((menu && !menu_was) || (vol && !vol_was)) {
            if (g_audio) papp_audio_pause(g_audio, true);
            const int pickd = askUi(menu && !menu_was ? kUiMenu : kUiVolume);
            if (g_audio) { papp_audio_pause(g_audio, false); g_audio_primed = false; }
            if (menu && !menu_was) {
                if (pickd == kPickAnother) { g_rc = PAPP_EXIT_RELAUNCH; break; }
                if (pickd == kPickExit) break;
            }
            next = papp_time_us();
        }
        menu_was = menu; vol_was = vol;

        if (!g_eight_way) { pad = g_gate.filter(pad); pad2 = g_gate2.filter(pad2); }
        const uint32_t wanted = toCabinet(pad) | toFarSide(pad2);
        namco_input_set(g_sys, wanted);
        namco_input_clear(g_sys, ~wanted & 0x3FFF);
        if (g_cocktail) g_sys->in1 |= kIn1Cocktail;      // the cabinet strap, which the calls above leave alone
        else g_sys->in1 &= (uint8_t)~kIn1Cocktail;

        int64_t t0 = papp_time_us();
        g_pcm_n = 0;
        namco_fast_run(g_sys, &g_cpu, kFrameUs);       // touches no pixel: the frame before may still be with the painter
        hiscore::frame([](uint16_t a) { return mem_rd(&g_sys->mem, a); }, [](uint16_t a, uint8_t v) { mem_wr(&g_sys->mem, a, v); });
        us_run += (uint32_t)(papp_time_us() - t0);
        if (g_painter_on) {
            // The panel shows 57 pictures a second and the machine makes 60: showing one waits for the panel, so
            // waiting for the painter would run the machine, and its sound, at the panel's pace. When the last
            // picture is still on its way this one is simply not drawn, about three times a second. Nor is it
            // drawn when the frame before ran late (see the end of the loop).
            const int64_t tw = papp_time_us();
            const bool draw = !behind;
            behind = false;
            if (draw && papp_sem_take(g_paint_idle, 0)) {
                if (painting) papp_sem_take(g_paint_done, PAPP_WAIT_FOREVER);
                namco_fast_decode(g_sys);
                us_dec += (uint32_t)(papp_time_us() - tw);
                papp_sem_give(g_paint_go); painting = true;
            } else {
                skipped++;
            }
            us_emu += (uint32_t)(papp_time_us() - t0);
        } else {
            namco_fast_decode(g_sys);
            int64_t t1 = papp_time_us();
            int stride;
            uint16_t *px = (uint16_t *)papp_disp_lock(g_surf, &stride);
            if (px) convert(px, stride);
            int64_t t2 = papp_time_us();
            if (px) papp_disp_present(g_surf);
            us_emu += (uint32_t)(t1 - t0); us_draw += (uint32_t)(t2 - t1); us_show += (uint32_t)(papp_time_us() - t2);
        }
        sendAudio();
        frames++;

        // One frame of machine time per sixtieth of a second, whatever the panel's own rate is.
        next += kFrameUs;
        const int64_t now = papp_time_us();
        if (now < next) papp_delay_ms((uint32_t)((next - now) / 1000));
        else {
            // Late. This thread still sleeps a millisecond, every time: it never would otherwise, and whatever
            // else lives on this core at a lower priority (the launcher's USB link among it) would never run. The
            // time is found by not drawing the next picture, which is a fifth of a frame's work.
            late++; behind = true;
            if (now - next > 100000) next = now;
            papp_delay_ms(1);
        }

        if (now - stat_at >= 5000000) {
            papp_audio_stats_t st = {};
            if (g_audio) papp_audio_stats(g_audio, &st);
            papp_log("arcade: %u.%u fps, per frame emu %u us (the machine alone %u, drawing %u), convert %u us, present %u us, pictures not drawn %u, late %u, audio queued %u, underruns %u, lost %u\n",
                     (unsigned)(frames * 10 / 5 / 10), (unsigned)(frames * 10 / 5 % 10), (unsigned)(us_emu / frames), (unsigned)(us_run / frames), (unsigned)(us_dec / frames),
                     (unsigned)((us_draw + g_us_draw) / frames), (unsigned)((us_show + g_us_show) / frames), (unsigned)skipped, (unsigned)late,
                     (unsigned)st.queued_frames, (unsigned)st.underruns, (unsigned)g_pcm_lost);
            frames = late = us_emu = us_draw = us_show = us_wait = us_dec = skipped = us_run = 0; g_us_draw = 0; g_us_show = 0;
            stat_at = now;
        }
    }
    if (g_painter_on && painting) { papp_sem_take(g_paint_done, 500); papp_sem_take(g_paint_idle, 500); }
    hiscore::leaving([](uint16_t a) { return mem_rd(&g_sys->mem, a); });   // written by the main thread, below
    g_stop = true;
    return 0;
}

int main(int argc, char **argv)
{
    char path[256] = "";
    if (argc > 1) {                                // papp_dev.py deploy app.papp -- /sd/roms/arcade/x.arc
        strncpy(path, argv[1], sizeof path - 1);
    } else {
        papp_pick_cfg_t pick = {};
        pick.title = "Arcade";
        pick.root = "/sd/roms/arcade";
        pick.extensions = ".arc";
        papp_pick_result_t r;
        if (papp_ui_pick_file(&pick, &r) == PAPP_PICK_CANCEL) return 0;
        strncpy(path, r.path, sizeof path - 1);
    }
    if (argc > 2) g_bench = atoi(argv[2]);
    prefsLoad();
    g_dsw1 = prefsByte("arc_dsw1", g_dsw1);
    g_cocktail = prefsByte("arc_cocktail", 0) != 0;

    // The two threads first, while the launcher's internal RAM is as empty as it gets: their stacks come out of
    // it, and once the machine has taken its share what is left may only be the slow kind (see inSlowRam).
    g_paint_go = papp_sem_create(0, 1);
    g_paint_done = papp_sem_create(0, 1);
    g_paint_idle = papp_sem_create(1, 1);
    g_ui_done = papp_sem_create(0, 1);
    g_machine_go = papp_sem_create(0, 1);
    papp_thread_cfg_t tcfg = {}; tcfg.name = "painter"; tcfg.stack_size = 8192; tcfg.prio = PAPP_PRIO_NORMAL; tcfg.core = 0;
    papp_thread_t *paint_thread = (g_paint_go && g_paint_done && g_paint_idle) ? papp_thread_create(painter, nullptr, &tcfg) : nullptr;
    if (!paint_thread) papp_log("arcade: no painter thread, drawing in the frame loop\n");
    g_painter_on = paint_thread != nullptr;
    papp_thread_cfg_t mcfg = {}; mcfg.name = "machine"; mcfg.stack_size = 16384; mcfg.prio = PAPP_PRIO_NORMAL; mcfg.core = 1;
    papp_thread_t *machine_thread = (g_ui_done && g_machine_go) ? papp_thread_create(machine, nullptr, &mcfg) : nullptr;
    if (!machine_thread) {
        papp_log("arcade: no thread for the machine, running it on the main one\n");
        g_ui_done = nullptr;
    }
    auto stopThreads = [&]() {
        g_stop = true;
        if (machine_thread) { papp_sem_give(g_machine_go); papp_thread_join(machine_thread, 2000, nullptr); }
        if (paint_thread) { g_paint_quit = true; papp_sem_give(g_paint_go); papp_thread_join(paint_thread, 1000, nullptr); }
    };

    const char *why = "";
    if (!loadMachine(path, &why)) {
        stopThreads();
        papp_log("arcade: %s: %s\n", path, why);
        papp_ui_message("Arcade", why);
        return 2;
    }

    papp_surface_cfg_t cfg = {};
    cfg.width = (uint16_t)(g_stand_up ? kRasterH : kRasterW);
    cfg.height = (uint16_t)(g_stand_up ? kRasterW : kRasterH);
    cfg.format = PAPP_PIXFMT_RGB565;
    cfg.scale = PAPP_SCALE_FIT_ASPECT;
    cfg.buffers = 2;
    g_surf = papp_disp_surface_create(&cfg);
    if (!g_surf) { stopThreads(); papp_log("arcade: no surface\n"); return 3; }

    papp_audio_cfg_t acfg = {};
    acfg.rate = kAudioRate; acfg.channels = 1; acfg.format = PAPP_AUDIO_S16; acfg.latency_ms = 150;
    g_audio = papp_audio_open(&acfg);

    if (machine_thread) {
        papp_sem_give(g_machine_go);
        while (!g_stop) {                              // the main thread: the launcher's overlays, when the frame loop asks
            const int want = g_ui_want;
            if (want) {
                g_ui_answer = runUi(want);
                g_ui_want = kUiNone;
                papp_sem_give(g_ui_done);
            }
            if (papp_exit_requested()) g_stop = true;
            hiscore::writeIfDue();
            papp_touch_point_t tp;
            g_touched = papp_disp_touch(g_surf, &tp, 1) > 0;
            papp_delay_ms(10);
        }
        if (g_ui_want) papp_sem_give(g_ui_done);
    } else {
        if (g_machine_go) papp_sem_give(g_machine_go);
        machine(nullptr);
    }
    stopThreads();
    hiscore::writeIfDue();                          // the scores as the game was left
    if (g_audio) papp_audio_close(g_audio);
    papp_disp_surface_destroy(g_surf);
    return g_rc;
}
