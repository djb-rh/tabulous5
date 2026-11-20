#include <M5Unified.h>
#include <Preferences.h>
#include <cstdarg>
#include "papp/papp.h"

m5::M5Unified M5;
SerialShim Serial;

// --- LovyanGFX's platform, which we did not compile from its own sources ---------------------------------
namespace lgfx { inline namespace v1 {
unsigned long millis(void) { return (unsigned long)(papp_time_us() / 1000); }
unsigned long micros(void) { return (unsigned long)papp_time_us(); }
void delay(unsigned long ms) { papp_delay_ms((uint32_t)ms); }
void delayMicroseconds(unsigned int us) { if (us >= 1000) papp_delay_ms(us / 1000); }
void gpio_hi(uint32_t) {}
void gpio_lo(uint32_t) {}
bool gpio_in(uint32_t) { return false; }
void pinMode(int_fast16_t, pin_mode_t) {}
void lgfxPinMode(int_fast16_t, pin_mode_t) {}
} }

uint32_t millis() { return (uint32_t)(papp_time_us() / 1000); }
uint32_t micros() { return (uint32_t)papp_time_us(); }
void delay(uint32_t ms) { papp_delay_ms(ms); }
uint32_t esp_random()
{
  static uint32_t s;                                   // xorshift, seeded from the clock: good enough to deal cards
  if (!s) s = (uint32_t)papp_time_us() | 1u;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s ^ (uint32_t)papp_time_us();
}
void SerialShim::printf(const char *fmt, ...) { char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); papp_log("%s", b); }
void SerialShim::println(const char *s) { papp_log("%s\n", s); }
void SerialShim::print(const char *s) { papp_log("%s", s); }

void m5compat_sound_pump();   // m5sound.cpp
void m5compat_sound_end();

namespace m5compat {
namespace {
papp_surface_t *g_surf;
void *g_px;
int64_t g_shown_us;
uint32_t g_shown_sum, g_presents;
void bind()
{
  int stride = 0;
  void *px = papp_disp_lock(g_surf, &stride);
  if (px && px != g_px) { g_px = px; M5.Display.setBuffer(px, stride ? stride : kW, kH); M5.Display.setRotation(M5.Display.getRotation()); }
}
}  // namespace

bool begin()
{
  papp_surface_cfg_t cfg = {};
  cfg.width = kW; cfg.height = kH; cfg.format = PAPP_PIXFMT_RGB565; cfg.scale = PAPP_SCALE_FIT_ASPECT;
  cfg.buffers = 1;                                     // what is drawn stays: the screens repaint only what changed
  g_surf = papp_disp_surface_create(&cfg);
  if (!g_surf) return false;
  M5.Display.setColorDepth(lgfx::rgb565_nonswapped);   // the surface's own pixels, not the byte-swapped ones an SPI panel wants
  bind();
  M5.Display.fillScreen(0);
  return g_px != nullptr;
}

int64_t g_prof[4];
bool update()
{
  const int64_t p0 = papp_time_us();
  papp_touch_point_t pt[2];
  const int n = papp_disp_touch(g_surf, pt, 2);
  const int64_t p1 = papp_time_us();
  M5.Touch.count = (uint8_t)(n > 0 ? n : 0);
  M5.Touch.detail.pressed = n > 0;
  if (n > 0) {
    const bool over = M5.Display.turnedOver();         // the picture is the other way up: so is where the finger is
    M5.Touch.detail.x = (int16_t)(over ? kW - 1 - pt[0].x : pt[0].x);
    M5.Touch.detail.y = (int16_t)(over ? kH - 1 - pt[0].y : pt[0].y);
  }
  // Showing the surface costs 50 ms on the Tab5 (the launcher turns all 1280x720 of it for the panel), during
  // which the finger is not read. So a frame goes out only when the picture has changed: a sample of it, one
  // pixel in 48, is summed each pass, which takes a fraction of a millisecond.
  const int64_t now = papp_time_us();
  uint32_t sum = 0;
  if (g_px) {
    const uint16_t *px = (const uint16_t *)g_px;
    for (int y = 1; y < kH; y += 6) { const uint16_t *row = px + (size_t)y * kW; for (int x = (y % 8); x < kW; x += 8) sum = sum * 31 + row[x]; }
  }
  const int64_t p2 = papp_time_us();
  g_prof[0] += p1 - p0; g_prof[1] += p2 - p1;
  if (sum != g_shown_sum || now - g_shown_us >= 1000000) { papp_disp_present(g_surf); g_shown_us = papp_time_us(); g_shown_sum = sum; g_presents++; bind(); }
  else papp_delay_ms(4);
  g_prof[2] += papp_time_us() - p2;
  m5compat_sound_pump();
  return !papp_exit_requested();
}

uint32_t presents() { return g_presents; }
void profile(int64_t out[3]) { for (int i = 0; i < 3; i++) { out[i] = g_prof[i]; g_prof[i] = 0; } }
void repaint() { g_shown_sum = ~g_shown_sum; }
void end() { m5compat_sound_end(); if (g_surf) papp_disp_surface_destroy(g_surf); g_surf = nullptr; }
}  // namespace m5compat

// --- Preferences: /sd/tabulous/<namespace>.prefs, records of [key len][key][value len u32][value] ----------
namespace {
struct Rec { char key[32]; uint32_t len; uint8_t *data; };
Rec g_recs[256]; int g_nrec; char g_loaded[24];
void pathFor(const char *ns, char *out, size_t n) { snprintf(out, n, "/sd/tabulous/%s.prefs", ns); }
void loadNs(const char *ns)
{
  if (!strcmp(ns, g_loaded)) return;
  for (int i = 0; i < g_nrec; i++) free(g_recs[i].data);
  g_nrec = 0; strncpy(g_loaded, ns, sizeof g_loaded - 1);
  char p[64]; pathFor(ns, p, sizeof p);
  FILE *f = fopen(p, "rb"); if (!f) return;
  for (;;) {
    uint8_t kl; if (fread(&kl, 1, 1, f) != 1 || kl >= 32 || g_nrec >= 256) break;
    Rec &r = g_recs[g_nrec]; memset(&r, 0, sizeof r);
    if (fread(r.key, 1, kl, f) != kl || fread(&r.len, 4, 1, f) != 1 || r.len > 65536) break;
    r.data = (uint8_t *)malloc(r.len ? r.len : 1);
    if (!r.data || fread(r.data, 1, r.len, f) != r.len) { free(r.data); break; }
    g_nrec++;
  }
  fclose(f);
}
void saveNs()
{
  char p[64]; pathFor(g_loaded, p, sizeof p);
  FILE *f = fopen(p, "wb"); if (!f) return;
  for (int i = 0; i < g_nrec; i++) { uint8_t kl = (uint8_t)strlen(g_recs[i].key); fwrite(&kl, 1, 1, f); fwrite(g_recs[i].key, 1, kl, f); fwrite(&g_recs[i].len, 4, 1, f); fwrite(g_recs[i].data, 1, g_recs[i].len, f); }
  fclose(f);
}
Rec *find(const char *key) { for (int i = 0; i < g_nrec; i++) if (!strcmp(g_recs[i].key, key)) return &g_recs[i]; return nullptr; }
}  // namespace
bool Preferences::begin(const char *name, bool) { strncpy(ns_, name, sizeof ns_ - 1); loadNs(ns_); return true; }
void Preferences::end() {}
size_t Preferences::getBytesLength(const char *key) { loadNs(ns_); Rec *r = find(key); return r ? r->len : 0; }
size_t Preferences::getBytes(const char *key, void *buf, size_t max) { loadNs(ns_); Rec *r = find(key); if (!r || r->len > max) return 0; memcpy(buf, r->data, r->len); return r->len; }
size_t Preferences::putBytes(const char *key, const void *buf, size_t len)
{
  loadNs(ns_); Rec *r = find(key);
  if (!r) { if (g_nrec >= 256) return 0; r = &g_recs[g_nrec++]; memset(r, 0, sizeof *r); strncpy(r->key, key, sizeof r->key - 1); }
  free(r->data); r->data = (uint8_t *)malloc(len ? len : 1); if (!r->data) { r->len = 0; return 0; }
  memcpy(r->data, buf, len); r->len = (uint32_t)len; saveNs(); return len;
}
uint8_t Preferences::getUChar(const char *key, uint8_t def) { uint8_t v; return getBytes(key, &v, 1) == 1 ? v : def; }
size_t Preferences::putUChar(const char *key, uint8_t v) { return putBytes(key, &v, 1); }
int8_t Preferences::getChar(const char *key, int8_t def) { int8_t v; return getBytes(key, &v, 1) == 1 ? v : def; }
size_t Preferences::putChar(const char *key, int8_t v) { return putBytes(key, &v, 1); }
bool Preferences::getBool(const char *key, bool def) { uint8_t v; return getBytes(key, &v, 1) == 1 ? v != 0 : def; }
size_t Preferences::putBool(const char *key, bool v) { const uint8_t b = v ? 1 : 0; return putBytes(key, &b, 1); }
uint16_t Preferences::getUShort(const char *key, uint16_t def) { uint16_t v; return getBytes(key, &v, 2) == 2 ? v : def; }
size_t Preferences::putUShort(const char *key, uint16_t v) { return putBytes(key, &v, 2); }
String Preferences::getString(const char *key, const char *def)
{
  loadNs(ns_); Rec *r = find(key); if (!r) return String(def);
  std::string v((const char *)r->data, r->len); while (!v.empty() && v.back() == '\0') v.pop_back();
  return String(v.c_str());
}
size_t Preferences::putString(const char *key, const char *v) { return putBytes(key, v, strlen(v)); }
uint32_t Preferences::getUInt(const char *key, uint32_t def) { uint32_t v; return getBytes(key, &v, 4) == 4 ? v : def; }
size_t Preferences::putUInt(const char *key, uint32_t v) { return putBytes(key, &v, 4); }
bool Preferences::isKey(const char *key) { loadNs(ns_); return find(key) != nullptr; }
bool Preferences::remove(const char *key) { loadNs(ns_); Rec *r = find(key); if (!r) return false; free(r->data); *r = g_recs[--g_nrec]; saveNs(); return true; }
