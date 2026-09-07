// Minimal RIFF/WAVE header parsing.
//
// Separate from audio.cpp, and free of Arduino headers, because this is the
// one part of the sound path that reads bytes it did not produce: a truncated
// or hand-edited .wav dropped into /sfx must be rejected, not fed to the
// speaker as a pointer and a length. That makes it worth unit-testing on the
// host, which is also why it lives here rather than inline.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tabulous {

struct WavInfo {
  uint32_t sample_rate = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  size_t data_offset = 0;  // byte offset of the sample data within the file
  size_t data_bytes = 0;   // length of that data, clamped to the file
};

// Parses `len` bytes of a WAV file. Accepts uncompressed PCM only, 8 or 16 bit,
// mono or stereo — which is everything M5Unified's speaker can play.
//
// Returns false, leaving *out untouched, for anything malformed. `why` (when
// given) receives a short static string naming the reason, so a bad file can
// be reported by name at boot instead of silently falling back to a beep.
bool parseWav(const uint8_t *data, size_t len, WavInfo *out,
              const char **why = nullptr);

}  // namespace tabulous
