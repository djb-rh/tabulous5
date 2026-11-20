// The rest of what Tabulous5's games use of the console: the speaker (M5.Speaker, as audio.cpp drives it), the
// accelerometer (M5.Imu), and the flash filesystem (LittleFS), each on the P4 Launcher's SDK.
#include <M5Unified.h>
#include <LittleFS.h>
#include <cmath>
#include <string>
#include <vector>
#include "papp/papp.h"

// --- M5.Speaker: a small mixer on one launcher stream ------------------------------------------------------
// Tones and samples each take a voice and play over one another, as M5Unified's virtual channels do. A worker
// thread keeps about 45 ms queued while anything sounds and queues nothing when all is quiet, so a tap's click
// starts within a few milliseconds. Where there are no threads (the Mac stand-in) the app's loop pumps it.
namespace {
constexpr int kRate = 22050, kVoices = 6, kBlock = 256;
constexpr uint32_t kTarget = kRate * 45 / 1000;
struct Voice {
  bool on = false, tone = false;
  const int16_t *pcm = nullptr;
  uint64_t pos = 0, step = 0, end = 0;          // 32.32, in the sample's own frames
  uint32_t phase = 0, phase_step = 0;           // tone: 0..2^32 a cycle
  uint32_t done = 0, total = 0;                 // tone: frames at the mixer's rate
  uint32_t born = 0;
};
Voice g_voice[kVoices];
papp_audio_stream_t *g_out;
papp_mutex_t *g_mx;
papp_thread_t *g_thread;
volatile bool g_run;
bool g_threaded;
int g_volume = 255;
uint32_t g_serial;
int64_t g_pumped_us;
int16_t g_sine[256];

void lock() { if (g_mx) papp_mutex_lock(g_mx); }
void unlock() { if (g_mx) papp_mutex_unlock(g_mx); }

bool anyOn() { for (const Voice &v : g_voice) if (v.on) return true; return false; }

void mix(int16_t *out, int n)
{
  static int32_t acc[kBlock];
  for (int i = 0; i < n; i++) acc[i] = 0;
  lock();
  for (Voice &v : g_voice) {
    if (!v.on) continue;
    if (v.tone) {
      const uint32_t attack = kRate * 2 / 1000, release = kRate * 5 / 1000;
      for (int i = 0; i < n && v.done < v.total; i++, v.done++) {
        int32_t s = g_sine[v.phase >> 24];
        v.phase += v.phase_step;
        const uint32_t left = v.total - v.done;
        if (v.done < attack) s = s * (int32_t)v.done / (int32_t)attack;
        else if (left < release) s = s * (int32_t)left / (int32_t)release;
        acc[i] += s;
      }
      if (v.done >= v.total) v.on = false;
    } else {
      for (int i = 0; i < n && v.pos < v.end; i++, v.pos += v.step) acc[i] += v.pcm[v.pos >> 32];
      if (v.pos >= v.end) v.on = false;
    }
  }
  unlock();
  for (int i = 0; i < n; i++) {
    int32_t s = acc[i] * g_volume / 255;
    out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
  }
}

int worker(void *)
{
  static int16_t buf[kBlock];
  while (g_run) {
    if (!anyOn()) { papp_delay_ms(4); continue; }
    papp_audio_stats_t st = {};
    papp_audio_stats(g_out, &st);
    if (st.queued_frames >= kTarget) { papp_delay_ms(3); continue; }
    mix(buf, kBlock);
    papp_audio_write(g_out, buf, kBlock, 30);
  }
  return 0;
}

bool ensure()
{
  if (g_out) return true;
  for (int i = 0; i < 256; i++) g_sine[i] = (int16_t)(9000.0 * sin(i * 6.283185307179586 / 256.0));
  papp_audio_cfg_t c = {};
  c.rate = kRate; c.channels = 1; c.format = PAPP_AUDIO_S16; c.latency_ms = 120;
  g_out = papp_audio_open(&c);
  if (!g_out) return false;
  g_mx = papp_mutex_create(false);
  g_run = true;
  g_thread = papp_thread_create(worker, nullptr, nullptr);
  g_threaded = g_thread != nullptr;
  g_pumped_us = papp_time_us();
  return true;
}

Voice *take()
{
  Voice *oldest = &g_voice[0];
  for (Voice &v : g_voice) { if (!v.on) return &v; if (v.born < oldest->born) oldest = &v; }
  return oldest;
}
}  // namespace

// The app's loop, where there is no worker: mix what the time since the last call amounts to.
void m5compat_sound_pump()
{
  if (!g_out || g_threaded) return;
  static int16_t buf[kBlock];
  const int64_t now = papp_time_us();
  int64_t frames = (now - g_pumped_us) * kRate / 1000000;
  if (frames > kRate / 2) frames = kRate / 2;
  if (frames < 32) return;
  g_pumped_us = now;
  while (frames > 0) { const int n = frames > kBlock ? kBlock : (int)frames; mix(buf, n); papp_audio_write(g_out, buf, (size_t)n, 0); frames -= n; }
}

void m5compat_sound_end()
{
  if (!g_out) return;
  g_run = false;
  if (g_thread) papp_thread_join(g_thread, 500, nullptr);
  papp_audio_close(g_out);
  g_out = nullptr; g_thread = nullptr;
}

namespace m5 {
bool Speaker_Class::begin() { return ensure(); }
void Speaker_Class::setVolume(uint8_t v) { g_volume = v; }
void Speaker_Class::stop() { lock(); for (Voice &v : g_voice) v.on = false; unlock(); }
bool Speaker_Class::tone(float hz, uint32_t ms, int, bool)
{
  if (!ensure() || hz <= 0 || !ms) return false;
  lock();
  Voice *v = take();
  *v = Voice();
  v->tone = true;
  v->phase_step = (uint32_t)((double)hz * 4294967296.0 / kRate);
  v->total = (uint32_t)((uint64_t)ms * kRate / 1000);
  v->born = ++g_serial;
  v->on = true;
  unlock();
  return true;
}
bool Speaker_Class::playRaw(const int16_t *pcm, size_t count, uint32_t rate, bool stereo, uint32_t, int, bool)
{
  if (!ensure() || !pcm || !count || !rate || stereo) return false;
  lock();
  Voice *v = take();
  *v = Voice();
  v->pcm = pcm;
  v->end = (uint64_t)count << 32;
  v->step = ((uint64_t)rate << 32) / kRate;
  v->born = ++g_serial;
  v->on = true;
  unlock();
  return true;
}

// --- M5.Imu -----------------------------------------------------------------------------------------------
bool IMU_Class::isEnabled() { return papp_input_analog(m5compat::kAccelX) >= 0; }
bool IMU_Class::getAccel(float *x, float *y, float *z)
{
  const int ax = papp_input_analog(m5compat::kAccelX), ay = papp_input_analog(m5compat::kAccelY), az = papp_input_analog(m5compat::kAccelZ);
  if (ax < 0 || ay < 0 || az < 0) return false;
  *x = (ax - 32768) / 8192.0f; *y = (ay - 32768) / 8192.0f; *z = (az - 32768) / 8192.0f;
  return true;
}
}  // namespace m5

// --- LittleFS: /sd/tabulous ---------------------------------------------------------------------------------
LittleFSClass LittleFS;
struct File::State {
  FILE *f = nullptr;
  size_t size = 0;
  bool dir = false;
  std::vector<std::string> names;
  size_t next = 0;
  std::string path, name;
};
namespace {
const char *const kRoot = "/sd/tabulous";
std::string baseName(const std::string &p) { const size_t s = p.find_last_of('/'); return s == std::string::npos ? p : p.substr(s + 1); }
}  // namespace

File LittleFSClass::open(const char *path, const char *)
{
  File out;
  const std::string full = std::string(kRoot) + (path && path[0] == '/' ? "" : "/") + (path ? path : "");
  if (FILE *ix = fopen((full + "/index.txt").c_str(), "rb")) {
    auto *st = new File::State();
    st->dir = true; st->path = full; st->name = baseName(full);
    char line[160];
    while (fgets(line, sizeof line, ix)) {
      std::string n(line);
      while (!n.empty() && (n.back() == '\n' || n.back() == '\r' || n.back() == ' ')) n.pop_back();
      if (!n.empty() && n[0] != '#') st->names.push_back(n);
    }
    fclose(ix);
    out.st_ = st;
    return out;
  }
  FILE *f = fopen(full.c_str(), "rb");
  if (!f) return out;
  auto *st = new File::State();
  st->f = f; st->path = full; st->name = baseName(full);
  fseek(f, 0, SEEK_END); st->size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  out.st_ = st;
  return out;
}
size_t File::size() const { return st_ ? st_->size : 0; }
size_t File::read(uint8_t *buf, size_t len) { return st_ && st_->f ? fread(buf, 1, len, st_->f) : 0; }
int File::read() { if (!st_ || !st_->f) return -1; const int c = fgetc(st_->f); return c == EOF ? -1 : c; }
int File::available() { if (!st_ || !st_->f) return 0; const long at = ftell(st_->f); return at < 0 ? 0 : (int)(st_->size - (size_t)at); }
void File::close() { if (!st_) return; if (st_->f) fclose(st_->f); delete st_; st_ = nullptr; }
bool File::isDirectory() const { return st_ && st_->dir; }
const char *File::name() const { return st_ ? st_->name.c_str() : ""; }
File File::openNextFile()
{
  File none;
  while (st_ && st_->dir && st_->next < st_->names.size()) {
    const std::string rel = st_->path.substr(strlen(kRoot)) + "/" + st_->names[st_->next++];
    File f = LittleFS.open(rel.c_str());
    if (f) return f;                                   // a name in the index with no file behind it is skipped
  }
  return none;
}
