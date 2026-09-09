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

}  // namespace

bool parse(const uint8_t *data, size_t len, Info *out, const char **why) {
  if (why) *why = "";
  if (!data || !out) return fail(why, "no data");
  if (len < kFileBytes) return fail(why, "too short");
  if (memcmp(data, kMagic, sizeof(kMagic)) != 0) return fail(why, "not an .arc file");

  const uint8_t system = data[8];
  const uint8_t version = data[9];
  if (system != 0) return fail(why, "unknown arcade board");
  if (version != 1) return fail(why, "made by a different version of mkarcade");

  uint32_t payload = 0;
  memcpy(&payload, data + 12, sizeof(payload));
  if (payload != kPayloadBytes) return fail(why, "wrong amount of ROM in it");

  Info info;
  info.system = System::PacMan;
  info.version = version;
  info.cpu = kHeaderBytes;
  info.gfx = info.cpu + kCpuBytes;
  info.palette = info.gfx + kGfxBytes;
  info.colour = info.palette + kPaletteBytes;
  info.sound1 = info.colour + kColourBytes;
  info.sound2 = info.sound1 + kSoundBytes;
  *out = info;
  return true;
}

}  // namespace arcrom
}  // namespace tabulous
