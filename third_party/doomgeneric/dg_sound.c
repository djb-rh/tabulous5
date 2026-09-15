// The console's sound module for the engine: sound effects mixed to 11025 Hz
// mono, sixteen bits, handed to the console one tic at a time. Music is
// stubbed - the tracks are MUS for an OPL chip, and nothing here emulates one.
//
// A Doom sound lump is DMX: a 16-bit format word, a 16-bit sample rate, a
// 32-bit length, then sixteen bytes of padding either side of unsigned 8-bit
// samples. Most are 11025 Hz; a few are 22050 and are stepped through at
// twice the pace.
#include <stdint.h>
#include <string.h>

#include "dg_tabulous.h"
#include "doomtype.h"
#include "i_sound.h"
#include "i_swap.h"
#include "m_misc.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

// Bound into default.cfg by i_sound.c; meaningless here, but must exist.
int use_libsamplerate = 0;
float libsamplerate_scale = 1.0f;

#define DG_RATE 11025
#define DG_CHANNELS 8
#define DG_TIC_SAMPLES (DG_RATE / 35)  // 315: one tic of sound

typedef struct {
  const uint8_t *data;  // the samples, or NULL when idle
  uint32_t len;
  uint32_t pos;         // 16.16 fixed point into data
  uint32_t step;        // 16.16 samples per output sample
  int vol;              // 0..127
} channel_t;

static channel_t channels[DG_CHANNELS];
static boolean use_prefix;
static int16_t mixbuf[DG_TIC_SAMPLES];
static uint32_t starts;

static boolean Init(boolean use_sfx_prefix) {
  use_prefix = use_sfx_prefix;
  memset(channels, 0, sizeof(channels));
  dg_printf("DG_sound: %d channels at %d Hz\n", DG_CHANNELS, DG_RATE);
  return true;
}

static void Shutdown(void) {}

static int GetSfxLumpNum(sfxinfo_t *sfx) {
  char name[16];
  if (sfx->link) sfx = sfx->link;
  if (use_prefix) M_snprintf(name, sizeof(name), "ds%s", sfx->name);
  else M_StringCopy(name, sfx->name, sizeof(name));
  return W_CheckNumForName(name);
}

static int StartSound(sfxinfo_t *sfx, int channel, int vol, int sep) {
  (void)sep;  // the panel's one speaker has no left and right
  if (channel < 0 || channel >= DG_CHANNELS) return -1;
  const int lump = sfx->lumpnum;
  if (lump < 0) return -1;
  const int lumplen = W_LumpLength(lump);
  if (lumplen < 8 + 32) return -1;
  // Static, not cache: a purged sound mid-play would be read after free.
  // The lumps a level uses come to a few hundred kilobytes of a zone that
  // is megabytes.
  const uint8_t *data = W_CacheLumpNum(lump, PU_STATIC);
  const int format = data[0] | (data[1] << 8);
  const int rate = data[2] | (data[3] << 8);
  uint32_t length = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) |
                    ((uint32_t)data[7] << 24);
  if (format != 3 || rate <= 0 || length > (uint32_t)(lumplen - 8) || length <= 32) return -1;
  channel_t *c = &channels[channel];
  c->data = data + 8 + 16;
  c->len = length - 32;
  c->pos = 0;
  c->step = (uint32_t)(((uint64_t)rate << 16) / DG_RATE);
  c->vol = vol;
  starts++;
  return channel;
}

static void StopSound(int channel) {
  if (channel >= 0 && channel < DG_CHANNELS) channels[channel].data = NULL;
}

static boolean SoundIsPlaying(int channel) {
  return channel >= 0 && channel < DG_CHANNELS && channels[channel].data != NULL;
}

static void UpdateSoundParams(int channel, int vol, int sep) {
  (void)sep;
  if (channel >= 0 && channel < DG_CHANNELS) channels[channel].vol = vol;
}

// One tic of sound, every tic. The console says whether it has room; when
// it has not, this tic's sound is simply not made, which keeps the mix from
// running ahead of the speaker.
static void Update(void) {
  if (!dg_audio_wants()) return;
  for (int i = 0; i < DG_TIC_SAMPLES; i++) {
    int32_t acc = 0;
    for (int ch = 0; ch < DG_CHANNELS; ch++) {
      channel_t *c = &channels[ch];
      if (!c->data) continue;
      const uint32_t idx = c->pos >> 16;
      if (idx >= c->len) {
        c->data = NULL;
        continue;
      }
      acc += ((int32_t)c->data[idx] - 128) * c->vol;
      c->pos += c->step;
    }
    // (sample - 128) * 127 reaches about 16000; doubled is one full-scale
    // sound, and two loud ones together clip rather than halve.
    acc *= 2;
    if (acc > 32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    mixbuf[i] = (int16_t)acc;
  }
  dg_audio_push(mixbuf, DG_TIC_SAMPLES, DG_RATE);
}

static void CacheSounds(sfxinfo_t *sounds, int num) {
  (void)sounds;
  (void)num;
}

uint32_t dg_sound_starts(void) {
  const uint32_t n = starts;
  starts = 0;
  return n;
}

static snddevice_t sound_devices[] = {SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
                                      SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS,
                                      SNDDEVICE_AWE32};

sound_module_t DG_sound_module = {
    sound_devices, arrlen(sound_devices), Init,       Shutdown,   GetSfxLumpNum,
    Update,        UpdateSoundParams,     StartSound, StopSound,  SoundIsPlaying,
    CacheSounds,
};

// ---- music: none ---------------------------------------------------------

static boolean MusicInit(void) { return true; }
static void MusicShutdown(void) {}
static void SetMusicVolume(int volume) { (void)volume; }
static void PauseMusic(void) {}
static void ResumeMusic(void) {}
static void *RegisterSong(void *data, int len) {
  (void)data;
  (void)len;
  return NULL;
}
static void UnRegisterSong(void *handle) { (void)handle; }
static void PlaySong(void *handle, boolean looping) {
  (void)handle;
  (void)looping;
}
static void StopSong(void) {}
static boolean MusicIsPlaying(void) { return false; }
static void Poll(void) {}

static snddevice_t music_devices[] = {SNDDEVICE_ADLIB, SNDDEVICE_SB, SNDDEVICE_PAS,
                                      SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                      SNDDEVICE_SOUNDCANVAS, SNDDEVICE_GENMIDI,
                                      SNDDEVICE_AWE32};

music_module_t DG_music_module = {
    music_devices, arrlen(music_devices), MusicInit, MusicShutdown, SetMusicVolume,
    PauseMusic,    ResumeMusic,           RegisterSong, UnRegisterSong, PlaySong,
    StopSong,      MusicIsPlaying,        Poll,
};
