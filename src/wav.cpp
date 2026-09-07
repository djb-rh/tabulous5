#include "wav.h"

#include <cstring>

namespace tabulous {
namespace {

uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

bool fail(const char **why, const char *reason) {
  if (why) *why = reason;
  return false;
}

constexpr uint16_t kPcm = 1;

}  // namespace

bool parseWav(const uint8_t *data, size_t len, WavInfo *out, const char **why) {
  if (why) *why = "";
  if (!data || !out) return fail(why, "no data");
  if (len < 12) return fail(why, "too short");
  if (memcmp(data, "RIFF", 4) != 0) return fail(why, "not RIFF");
  if (memcmp(data + 8, "WAVE", 4) != 0) return fail(why, "not WAVE");

  WavInfo info;
  bool have_fmt = false;
  bool have_data = false;

  // Walk the chunk list rather than assuming fmt is first and data second.
  // Anything a converter emits in between (LIST, fact, cue) is skipped.
  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t *hdr = data + pos;
    const uint32_t size = rd32(hdr + 4);
    const size_t body = pos + 8;
    // Chunk sizes come from the file, so treat one that runs past the end as a
    // truncated file rather than trusting it into an out-of-bounds read.
    const size_t avail = len - body;
    const size_t take = size > avail ? avail : size;

    if (memcmp(hdr, "fmt ", 4) == 0) {
      if (take < 16) return fail(why, "short fmt chunk");
      const uint16_t format = rd16(data + body);
      if (format != kPcm) return fail(why, "not uncompressed PCM");
      info.channels = rd16(data + body + 2);
      info.sample_rate = rd32(data + body + 4);
      info.bits = rd16(data + body + 14);
      have_fmt = true;
    } else if (memcmp(hdr, "data", 4) == 0) {
      info.data_offset = body;
      info.data_bytes = take;
      have_data = true;
    }

    // Chunks are word-aligned: an odd size is followed by a pad byte.
    pos = body + size + (size & 1);
    if (size > avail) break;  // truncated; nothing valid can follow
  }

  if (!have_fmt) return fail(why, "no fmt chunk");
  if (!have_data) return fail(why, "no data chunk");
  if (info.channels != 1 && info.channels != 2) return fail(why, "not mono or stereo");
  if (info.bits != 8 && info.bits != 16) return fail(why, "not 8 or 16 bit");
  if (info.sample_rate < 4000 || info.sample_rate > 96000) return fail(why, "odd sample rate");
  if (info.data_bytes == 0) return fail(why, "empty data chunk");

  // Round down to a whole frame so the caller can divide without a remainder.
  const size_t frame = (size_t)info.channels * (info.bits / 8);
  info.data_bytes -= info.data_bytes % frame;
  if (info.data_bytes == 0) return fail(why, "less than one frame");

  *out = info;
  return true;
}

}  // namespace tabulous
