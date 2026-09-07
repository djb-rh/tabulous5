#include "audio.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "wav.h"

namespace tabulous {
namespace audio {
namespace {

bool g_enabled = true;

// ------------------------------------------------------------------ samples
//
// Each effect may have a file at /sfx/<name>.wav. Loaded ones are held in
// PSRAM for the life of the program because M5Unified's speaker does NOT copy
// what it is given — it plays straight from the pointer, so freeing a buffer
// after play() returns would hand the speaker task a dangling one.
//
// 32 MB of PSRAM against ~150 KB of effects, so keeping them all resident is
// not a tradeoff worth thinking about.

enum Effect : uint8_t {
  kSelect, kCorrect, kSkip, kReject, kBoom, kBuzzer, kFanfare, kEffectCount
};

const char *kEffectName[kEffectCount] = {
    "select", "correct", "skip", "reject", "boom", "buzzer", "fanfare"};

struct Sample {
  const int16_t *pcm = nullptr;  // owned; 16-bit mono, host byte order
  size_t count = 0;              // samples, not bytes
  uint32_t rate = 0;
};

Sample g_sample[kEffectCount];
int g_loaded = 0;

bool haveSample(Effect e) { return g_sample[e].pcm != nullptr; }

// Returns true if the sample played, so callers can fall through to the
// synthesised version when it didn't.
bool playSample(Effect e) {
  if (!g_enabled) return false;
  const Sample &s = g_sample[e];
  if (!s.pcm) return false;
  // stop_current_sound = false: effects should overlap rather than cut each
  // other off, which matters most for select() firing under a fanfare.
  return M5.Speaker.playRaw(s.pcm, s.count, s.rate, false, 1, -1, false);
}

// Multi-note effects are scheduled rather than played with delay()s: the round
// screen must keep animating and the beep timer must stay accurate while the
// buzzer is sounding.
struct Step {
  uint16_t hz;
  uint16_t ms;
  uint32_t at;  // millis() at which to start it
};

constexpr size_t kMaxSteps = 6;
Step g_seq[kMaxSteps];
size_t g_len = 0;
size_t g_next = 0;

void play(uint16_t hz, uint32_t ms) {
  if (!g_enabled) return;
  // NB: the 4-argument form. The 6-argument overload takes an explicit
  // waveform, and passing it nullptr plays an empty buffer — silently.
  M5.Speaker.tone((float)hz, ms);
}

void schedule(const uint16_t *hz, const uint16_t *ms, const uint16_t *gap,
              size_t count) {
  if (!g_enabled) return;
  count = count > kMaxSteps ? kMaxSteps : count;
  const uint32_t now = millis();
  uint32_t at = now;
  for (size_t i = 0; i < count; i++) {
    g_seq[i] = {hz[i], ms[i], at};
    at += gap[i];
  }
  g_len = count;
  g_next = 0;
}

}  // namespace

void loadSamples() {
  for (uint8_t i = 0; i < kEffectCount; i++) {
    if (g_sample[i].pcm) continue;  // already resident; files don't change here

    char path[40];
    snprintf(path, sizeof(path), "/sfx/%s.wav", kEffectName[i]);
    File f = LittleFS.open(path, "r");
    if (!f) continue;

    const size_t len = f.size();
    // A sound effect is tens of KB. Anything past a megabyte is a wrong file,
    // and reading it would be a slow way to run out of memory.
    if (len < 44 || len > 1024u * 1024u) {
      f.close();
      continue;
    }

    uint8_t *raw = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!raw) {
      f.close();
      continue;
    }
    const size_t got = f.read(raw, len);
    f.close();

    WavInfo info;
    const char *why = "";
    if (!parseWav(raw, got, &info, &why)) {
      Serial.printf("sfx %s: %s\n", path, why);
      heap_caps_free(raw);
      continue;
    }

    // Convert to 16-bit mono once, here, rather than on every play: the
    // speaker takes a flat int16 buffer and this is the only place that knows
    // what the file actually was.
    const size_t frames = info.data_bytes / (info.channels * (info.bits / 8));
    int16_t *pcm = (int16_t *)heap_caps_malloc(frames * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM);
    if (!pcm) {
      heap_caps_free(raw);
      continue;
    }
    const uint8_t *src = raw + info.data_offset;
    for (size_t n = 0; n < frames; n++) {
      int32_t acc = 0;
      for (uint16_t c = 0; c < info.channels; c++) {
        if (info.bits == 16) {
          const size_t o = (n * info.channels + c) * 2;
          acc += (int16_t)(src[o] | ((uint16_t)src[o + 1] << 8));
        } else {
          // 8-bit WAV data is unsigned, centred on 128.
          acc += ((int32_t)src[n * info.channels + c] - 128) << 8;
        }
      }
      pcm[n] = (int16_t)(acc / info.channels);
    }
    heap_caps_free(raw);

    g_sample[i] = {pcm, frames, info.sample_rate};
    g_loaded++;
  }
}

int sampleCount() { return g_loaded; }

void begin(uint8_t volume) {
  M5.Speaker.begin();
  // tone() picks a channel automatically, so every channel needs to be audible
  // — setting only channel 0 leaves the others silent.
  M5.Speaker.setAllChannelVolume(255);
  setVolume(volume);
}

void setVolume(uint8_t volume) { M5.Speaker.setVolume(volume); }

void setEnabled(bool enabled) {
  g_enabled = enabled;
  if (!enabled) {
    g_len = g_next = 0;
    M5.Speaker.stop();
  }
}

bool enabled() { return g_enabled; }

void update(uint32_t now_ms) {
  while (g_next < g_len && (int32_t)(now_ms - g_seq[g_next].at) >= 0) {
    play(g_seq[g_next].hz, g_seq[g_next].ms);
    g_next++;
  }
  if (g_next >= g_len) g_len = g_next = 0;
}

void beep(uint16_t hz) {
  // Short and dry — at the end of a round the gap is down to ~120 ms, so a
  // long beep would still be sounding when the next one is due.
  play(hz, 55);
}

void buzzer() {
  if (playSample(kBuzzer)) return;
  // Two descending tones then a low rasp, all well below the beep range so it
  // can't be mistaken for another tick at the moment it matters most.
  const uint16_t hz[] = {320, 240, 180};
  const uint16_t ms[] = {260, 420, 320};
  const uint16_t gap[] = {240, 380, 320};
  schedule(hz, ms, gap, 3);
}

void correct() {
  if (playSample(kCorrect)) return;
  const uint16_t hz[] = {1320, 1760};
  const uint16_t ms[] = {70, 110};
  const uint16_t gap[] = {60, 110};
  schedule(hz, ms, gap, 2);
}

void skip() {
  if (playSample(kSkip)) return;
  play(520, 90);
}

void reject() {
  if (playSample(kReject)) return;
  play(180, 130);
}

void boom() {
  if (playSample(kBoom)) return;
  // Without a sample there is no way to synthesise noise through tone(), so
  // this borrows the buzzer's shape an octave down rather than pretending.
  const uint16_t hz[] = {160, 110, 80};
  const uint16_t ms[] = {120, 200, 300};
  const uint16_t gap[] = {110, 180, 300};
  schedule(hz, ms, gap, 3);
}

void select() {
  if (playSample(kSelect)) return;
  play(1000, 35);
}

void fanfare() {
  if (playSample(kFanfare)) return;
  // C - E - G - C. Reads as "you won" without needing to be a composition.
  const uint16_t hz[] = {523, 659, 784, 1047};
  const uint16_t ms[] = {130, 130, 130, 340};
  const uint16_t gap[] = {150, 150, 150, 340};
  schedule(hz, ms, gap, 4);
}

}  // namespace audio
}  // namespace tabulous
