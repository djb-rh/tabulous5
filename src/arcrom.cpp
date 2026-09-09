#include "arcrom.h"

#include <cstring>

namespace tabulous {
namespace arcrom {
namespace {

constexpr char kMagic[8] = {'T', 'A', 'B', '5', 'A', 'R', 'C', '1'};

bool fail(const char **why, const char *reason) {
  if (why) *why = reason;
  return false;
}

uint32_t readU32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

}  // namespace

size_t fileBytes(const uint8_t *header, size_t len) {
  if (!header || len < kV1HeaderBytes) return 0;
  if (memcmp(header, kMagic, sizeof(kMagic)) != 0) return 0;
  const uint8_t version = header[9];
  const uint32_t payload = readU32(header + 12);
  if (version == 1) return kV1HeaderBytes + payload;
  if (version == 2) return kHeaderBytes + payload;
  return 0;
}

bool parse(const uint8_t *data, size_t len, Info *out, const char **why) {
  if (why) *why = "";
  if (!data || !out) return fail(why, "no data");
  if (len < kV1HeaderBytes) return fail(why, "too short");
  if (memcmp(data, kMagic, sizeof(kMagic)) != 0) return fail(why, "not an .arc file");
  if (data[8] != 0) return fail(why, "unknown arcade board");

  Info info;
  info.version = data[9];

  size_t header = 0, high_banks = 0;
  if (info.version == 1) {
    header = kV1HeaderBytes;
  } else if (info.version == 2) {
    header = kHeaderBytes;
    high_banks = data[10];
    info.vector_fixups = data[11];
    if (high_banks > kMaxCpuHighBanks) return fail(why, "too much program ROM");
    if (info.vector_fixups > kMaxVectorFixups) return fail(why, "too many vector fixups");
    for (size_t i = 0; i < info.vector_fixups; i++) {
      info.vector_from[i] = data[16 + 2 * i];
      info.vector_to[i] = data[17 + 2 * i];
    }
    // Zero, which is what every file written before this byte meant anything
    // has here, is the common case: the monitor stood on its side.
    info.upright_monitor = data[24] == 0;
  } else {
    return fail(why, "made by a different version of mkarcade");
  }

  info.cpu_high_bytes = high_banks * kCpuHighBank;
  const size_t payload = kBasePayloadBytes + info.cpu_high_bytes;
  if (readU32(data + 12) != payload) return fail(why, "wrong amount of ROM in it");
  if (len < header + payload) return fail(why, "too short");

  info.system = System::PacMan;
  info.cpu = header;
  info.cpu_high = info.cpu + kCpuBytes;
  info.gfx = info.cpu_high + info.cpu_high_bytes;
  info.palette = info.gfx + kGfxBytes;
  info.colour = info.palette + kPaletteBytes;
  info.sound1 = info.colour + kColourBytes;
  info.sound2 = info.sound1 + kSoundBytes;
  info.file_bytes = header + payload;
  *out = info;
  return true;
}

}  // namespace arcrom
}  // namespace tabulous
