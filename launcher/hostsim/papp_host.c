/* A stand-in for the launcher, on this Mac: enough of the PAPP SDK to run an app's own code with no board.
 *
 * Time is the app's own: papp_delay_ms() and papp_disp_present() move a pretend clock, nothing sleeps, so a
 * minute of play takes a second. Frames are counted at present().
 *   PAPP_FRAMES=600            stop (papp_exit_requested) after this many presents
 *   PAPP_SHOTS=120,300,600     write shot_<frame>.bmp at these presents, into PAPP_OUT (default .)
 *   PAPP_INPUT=100:SELECT:6,160:START:6,220:LEFT:60    pad buttons: frame, names joined by +, frames held
 *   PAPP_TOUCH=100:640:360:4   a finger: frame, x, y (surface coordinates), frames held
 *   PAPP_PICK=/path/to/file    what papp_ui_pick_file returns
 *   PAPP_SD=/path              what "/sd" means to host_fopen
 *   PAPP_WAV=out.wav           the first audio stream, as a file
 *   PAPP_ACCEL=0:0:0:-1000,60:900:0:0   an accelerometer: frame, then x, y, z in thousandths of a g, from that frame on
 *   PAPP_PRESENT_MS=43         what showing a frame costs (default: 43 for a 1280x720 surface, 3 for a smaller one)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "papp/papp.h"

static int64_t g_now_us = 1000000;
static long g_frame, g_max_frames = 600;
static long g_present_us;
static struct { long at; int mg[3]; } g_acc[32]; static int g_acc_n;
static FILE *g_wav; static uint32_t g_wav_frames, g_wav_rate = 44100, g_wav_ch = 1;
struct papp_surface { int w, h, bufs, which; uint16_t *px[2]; };
struct papp_audio_stream { papp_audio_cfg_t cfg; uint64_t written; };
typedef struct { long at, hold; uint32_t buttons; } in_t;
typedef struct { long at, hold; int x, y; } touch_t;
static in_t g_in[256]; static int g_in_n;
static touch_t g_touch[256]; static int g_touch_n;
static long g_shots[64]; static int g_shot_n;
static const char *g_out = ".";

static const struct { const char *n; uint32_t b; } kBtn[] = {
    {"A", PAPP_BTN_A}, {"B", PAPP_BTN_B}, {"X", PAPP_BTN_X}, {"Y", PAPP_BTN_Y}, {"L1", PAPP_BTN_L1}, {"R1", PAPP_BTN_R1},
    {"SELECT", PAPP_BTN_SELECT}, {"START", PAPP_BTN_START}, {"UP", PAPP_BTN_UP}, {"DOWN", PAPP_BTN_DOWN},
    {"LEFT", PAPP_BTN_LEFT}, {"RIGHT", PAPP_BTN_RIGHT} };

static void host_setup(void)
{
    static int done; if (done) return; done = 1;
    const char *e;
    if ((e = getenv("PAPP_FRAMES"))) g_max_frames = atol(e);
    if ((e = getenv("PAPP_PRESENT_MS"))) g_present_us = atol(e) * 1000;
    if ((e = getenv("PAPP_ACCEL"))) { char *c = strdup(e); for (char *t = strtok(c, ","); t && g_acc_n < 32; t = strtok(NULL, ",")) if (sscanf(t, "%ld:%d:%d:%d", &g_acc[g_acc_n].at, &g_acc[g_acc_n].mg[0], &g_acc[g_acc_n].mg[1], &g_acc[g_acc_n].mg[2]) == 4) g_acc_n++; }
    if ((e = getenv("PAPP_OUT"))) g_out = e;
    if ((e = getenv("PAPP_SHOTS"))) { char *s = strdup(e); for (char *t = strtok(s, ","); t && g_shot_n < 64; t = strtok(NULL, ",")) g_shots[g_shot_n++] = atol(t); }
    if ((e = getenv("PAPP_INPUT"))) {
        char *s = strdup(e), *save;
        for (char *t = strtok_r(s, ",", &save); t && g_in_n < 256; t = strtok_r(NULL, ",", &save)) {
            char names[128]; long at, hold = 4;
            if (sscanf(t, "%ld:%127[^:]:%ld", &at, names, &hold) < 2) continue;
            uint32_t b = 0; char *save2;
            for (char *n = strtok_r(names, "+", &save2); n; n = strtok_r(NULL, "+", &save2))
                for (size_t i = 0; i < sizeof kBtn / sizeof kBtn[0]; i++) if (!strcmp(n, kBtn[i].n)) b |= kBtn[i].b;
            g_in[g_in_n++] = (in_t){at, hold, b};
        }
    }
    if ((e = getenv("PAPP_TOUCH"))) {
        char *s = strdup(e), *save;
        for (char *t = strtok_r(s, ",", &save); t && g_touch_n < 256; t = strtok_r(NULL, ",", &save)) {
            touch_t q = {0, 4, 0, 0};
            if (sscanf(t, "%ld:%d:%d:%ld", &q.at, &q.x, &q.y, &q.hold) >= 3) g_touch[g_touch_n++] = q;
        }
    }
}

FILE *host_fopen(const char *path, const char *mode)
{
    const char *sd = getenv("PAPP_SD");
    if (sd && !strncmp(path, "/sd/", 4)) { char p[1024]; snprintf(p, sizeof p, "%s/%s", sd, path + 4); return fopen(p, mode); }
    return fopen(path, mode);
}

/* system */
void papp_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); fflush(stdout); }
int64_t papp_time_us(void) { g_now_us += 20; return g_now_us; }
void papp_delay_ms(uint32_t ms) { g_now_us += (int64_t)ms * 1000; }
bool papp_exit_requested(void) { host_setup(); return g_frame >= g_max_frames; }
void papp_exit(int code) { exit(code); }
const char *papp_board_id(void) { return "host"; }
uint32_t papp_launcher_abi(void) { return 0x20000; }
bool papp_local_time(papp_datetime_t *out) { (void)out; return false; }
bool papp_utc_time(int64_t *s) { (void)s; return false; }
/* memory */
void *papp_alloc(size_t size, uint32_t caps, size_t align) { (void)caps; void *p = NULL; if (align < 16) align = 16; return posix_memalign(&p, align, size) ? NULL : p; }
void *papp_realloc(void *p, size_t size) { return realloc(p, size); }
void papp_free(void *p) { free(p); }
size_t papp_mem_free_size(uint32_t caps) { (void)caps; return 16u << 20; }
void *papp_code_alloc(size_t size) { (void)size; return NULL; }
void papp_code_free(void *c) { (void)c; }
int papp_code_sync(void *c, size_t n) { (void)c; (void)n; return -1; }
const papp_if_fs_t *papp_fs(void) { return NULL; }
/* display */
bool papp_disp_info(papp_disp_info_t *o) { o->width = 1280; o->height = 720; o->refresh_hz = 60; return true; }
papp_surface_t *papp_disp_surface_create(const papp_surface_cfg_t *cfg)
{
    host_setup();
    papp_surface_t *s = calloc(1, sizeof *s);
    s->w = cfg->width; s->h = cfg->height; s->bufs = cfg->buffers == 2 ? 2 : 1;
    for (int i = 0; i < s->bufs; i++) s->px[i] = calloc((size_t)s->w * s->h, 2);
    printf("host: surface %dx%d, %d buffer(s)\n", s->w, s->h, s->bufs);
    return s;
}
void papp_disp_surface_destroy(papp_surface_t *s) { (void)s; }
void *papp_disp_lock(papp_surface_t *s, int *stride) { if (stride) *stride = s->w; return s->px[s->which]; }
static void shot(const papp_surface_t *s, const uint16_t *px)
{
    char name[1024]; snprintf(name, sizeof name, "%s/shot_%05ld.bmp", g_out, g_frame);
    FILE *f = fopen(name, "wb"); if (!f) return;
    const int row = (s->w * 3 + 3) & ~3; const uint32_t size = 54 + (uint32_t)row * s->h;
    uint8_t h[54] = {'B', 'M'}; memcpy(h + 2, &size, 4); h[10] = 54; h[14] = 40; memcpy(h + 18, &s->w, 4); memcpy(h + 22, &s->h, 4); h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    uint8_t *line = calloc(1, (size_t)row);
    for (int y = s->h - 1; y >= 0; y--) {
        for (int x = 0; x < s->w; x++) { uint16_t c = px[(size_t)y * s->w + x];
            line[x * 3 + 2] = (uint8_t)(((c >> 11) & 31) * 255 / 31); line[x * 3 + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63); line[x * 3] = (uint8_t)((c & 31) * 255 / 31); }
        fwrite(line, 1, (size_t)row, f);
    }
    long lit = 0; for (long i = 0; i < (long)s->w * s->h; i++) lit += px[i] != 0;
    free(line); fclose(f); printf("host: wrote %s (%ld of %ld pixels not black, buffer %p)\n", name, lit, (long)s->w * s->h, (void *)px);
}
static int presented(papp_surface_t *s, const uint16_t *px)
{
    g_frame++;
    for (int i = 0; i < g_shot_n; i++) if (g_shots[i] == g_frame) shot(s, px);
    /* What showing a frame costs on the Tab5: 43 ms for a full 1280x720 surface (the launcher turns all of it for
     * the panel), a few for an emulator's small one. A game whose timing only works with quick frames shows it here. */
    g_now_us += g_present_us ? g_present_us : ((long)s->w * s->h >= 1280L * 720 ? 43000 : 3000);
    return 0;
}
int papp_disp_present(papp_surface_t *s) { const uint16_t *px = s->px[s->which]; if (s->bufs == 2) s->which ^= 1; return presented(s, px); }
int papp_disp_present_ext(papp_surface_t *s, const void *pixels) { return presented(s, pixels); }
int papp_disp_touch(papp_surface_t *s, papp_touch_point_t *pts, int max)
{
    (void)s; host_setup();
    for (int i = 0; i < g_touch_n && max > 0; i++)
        if (g_frame >= g_touch[i].at && g_frame < g_touch[i].at + g_touch[i].hold) { pts[0] = (papp_touch_point_t){(uint16_t)g_touch[i].x, (uint16_t)g_touch[i].y, 10, 0, 0}; return 1; }
    return 0;
}
int papp_disp_blit(papp_surface_t *s, const papp_blit_t *b) { (void)s; (void)b; return -1; }
int papp_disp_fill(papp_surface_t *s, int x, int y, int w, int h, uint16_t c)
{
    uint16_t *px = s->px[s->which];
    for (int j = y < 0 ? 0 : y; j < y + h && j < s->h; j++) for (int i = x < 0 ? 0 : x; i < x + w && i < s->w; i++) px[(size_t)j * s->w + i] = c;
    return 0;
}
/* audio */
uint32_t papp_audio_output_rate(void) { return 48000; }
papp_audio_stream_t *papp_audio_open(const papp_audio_cfg_t *cfg)
{
    papp_audio_stream_t *a = calloc(1, sizeof *a); a->cfg = *cfg;
    const char *w = getenv("PAPP_WAV");
    if (w && !g_wav) { g_wav = fopen(w, "wb"); g_wav_rate = cfg->rate; g_wav_ch = cfg->channels; uint8_t z[44] = {0}; if (g_wav) fwrite(z, 1, 44, g_wav); }
    printf("host: audio stream %u Hz, %u ch\n", (unsigned)cfg->rate, (unsigned)cfg->channels);
    return a;
}
void papp_audio_close(papp_audio_stream_t *s)
{
    (void)s;
    if (!g_wav) return;
    const uint32_t bytes = g_wav_frames * g_wav_ch * 2, riff = 36 + bytes, fmt = 16, rate = g_wav_rate, br = rate * g_wav_ch * 2;
    const uint16_t pcm = 1, ch = (uint16_t)g_wav_ch, ba = (uint16_t)(g_wav_ch * 2), bits = 16;
    fseek(g_wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, g_wav); fwrite(&riff, 4, 1, g_wav); fwrite("WAVEfmt ", 1, 8, g_wav); fwrite(&fmt, 4, 1, g_wav);
    fwrite(&pcm, 2, 1, g_wav); fwrite(&ch, 2, 1, g_wav); fwrite(&rate, 4, 1, g_wav); fwrite(&br, 4, 1, g_wav);
    fwrite(&ba, 2, 1, g_wav); fwrite(&bits, 2, 1, g_wav); fwrite("data", 1, 4, g_wav); fwrite(&bytes, 4, 1, g_wav);
    fclose(g_wav); g_wav = NULL;
}
size_t papp_audio_write(papp_audio_stream_t *s, const void *data, size_t frames, uint32_t timeout_ms)
{
    (void)timeout_ms; s->written += frames;
    if (g_wav && s->cfg.format == PAPP_AUDIO_S16) { fwrite(data, (size_t)s->cfg.channels * 2, frames, g_wav); g_wav_frames += (uint32_t)frames; }
    return frames;
}
void papp_audio_pause(papp_audio_stream_t *s, bool p) { (void)s; (void)p; }
void papp_audio_set_gain(papp_audio_stream_t *s, int g) { (void)s; (void)g; }
void papp_audio_clear(papp_audio_stream_t *s) { (void)s; }
void papp_audio_stats(papp_audio_stream_t *s, papp_audio_stats_t *o) { memset(o, 0, sizeof *o); o->played_frames = s->written; o->capacity_frames = s->cfg.rate / 10; }
/* input */
bool papp_input_gamepad(int player, papp_gamepad_t *out)
{
    host_setup(); memset(out, 0, sizeof *out);
    if (player != 0) return false;
    out->connected = 1; strcpy(out->name, "host pad");
    for (int i = 0; i < g_in_n; i++) if (g_frame >= g_in[i].at && g_frame < g_in[i].at + g_in[i].hold) out->buttons |= g_in[i].buttons;
    return true;
}
bool papp_input_key_down(uint16_t u) { (void)u; return false; }
uint16_t papp_input_modifiers(void) { return 0; }
bool papp_input_poll_event(papp_input_event_t *ev) { (void)ev; return false; }
void papp_input_flush(void) {}
/* The accelerometer, if PAPP_ACCEL scripts one: channels 16..18, 32768 = 0 g, 4096 to the g (launcher patch 4). */
int papp_input_analog(int ch)
{
    host_setup();
    if (ch < 16 || ch > 18 || !g_acc_n) return -1;
    int at = 0;
    for (int i = 0; i < g_acc_n; i++) if (g_acc[i].at <= g_frame) at = i;
    return 32768 + g_acc[at].mg[ch - 16] * 4096 / 1000;
}
/* launcher screens */
int papp_ui_pick_file(const papp_pick_cfg_t *cfg, papp_pick_result_t *out)
{
    const char *p = getenv("PAPP_PICK"); memset(out, 0, sizeof *out);
    printf("host: pick_file \"%s\" in %s (%s)\n", cfg->title, cfg->root, cfg->extensions ? cfg->extensions : "*");
    if (!p) return PAPP_PICK_CANCEL;
    strncpy(out->path, p, sizeof out->path - 1); out->action = PAPP_PICK_RUN; return PAPP_PICK_RUN;
}
void papp_ui_message(const char *title, const char *text) { printf("host: message [%s] %s\n", title, text); }
int papp_ui_menu(const char *title, const char *const *items, int n, int initial) { (void)items; (void)n; printf("host: menu [%s]\n", title); return initial; }
int papp_ui_volume(void) { return 50; }
int papp_ui_wifi(void) { return -1; }
const papp_if_net_t *papp_net(void) { return NULL; }
const papp_if_ble_t *papp_ble(void) { return NULL; }

/* No threads here: an app that asks for one gets NULL and does the work in its own loop. */
papp_thread_t *papp_thread_create(papp_thread_fn_t fn, void *arg, const papp_thread_cfg_t *cfg) { (void)fn; (void)arg; (void)cfg; return NULL; }
bool papp_thread_join(papp_thread_t *t, uint32_t ms, int *code) { (void)t; (void)ms; (void)code; return true; }
papp_mutex_t *papp_mutex_create(bool r) { (void)r; return NULL; }
void papp_mutex_destroy(papp_mutex_t *m) { (void)m; }
void papp_mutex_lock(papp_mutex_t *m) { (void)m; }
void papp_mutex_unlock(papp_mutex_t *m) { (void)m; }

int papp_main(int argc, char **argv);
int main(int argc, char **argv) { host_setup(); int rc = papp_main(argc, argv); printf("host: app returned %d after %ld frames\n", rc, g_frame); return rc == PAPP_EXIT_RELAUNCH ? 0 : rc; }
