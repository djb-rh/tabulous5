#include "gbrom.h"

#include <cstring>

namespace tabulous {
namespace gbrom {
namespace {

// Which MBC each cartridge-type byte means, or -1 for one the core cannot
// run. Taken from Peanut-GB's own table so the browser and the core agree;
// MBC6, MBC7, HuC1/3, the Pocket Camera and Bandai TAMA5 are the gaps.
constexpr int8_t kMbc[32] = {
    0, 1, 1, 1, -1, 2, 2, -1, 0, 0, -1, 0, 0, 0, -1, 3,
    3, 3, 3, 3, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5, 5, -1};

// Cartridge RAM by the byte at 0x0149. Entry 1 is 2 KB, which no licensed
// cartridge used.
constexpr uint32_t kRamBytes[6] = {0, 2 * 1024, 8 * 1024, 32 * 1024,
                                   128 * 1024, 64 * 1024};

bool fail(const char **why, const char *reason) {
  if (why) *why = reason;
  return false;
}

}  // namespace

bool typeSupported(uint8_t cart_type) {
  return cart_type < 32 && kMbc[cart_type] >= 0;
}

const char *typeName(uint8_t cart_type) {
  if (cart_type >= 32) return "unknown";
  switch (kMbc[cart_type]) {
    case 0: return "ROM only";
    case 1: return "MBC1";
    case 2: return "MBC2";
    case 3: return "MBC3";
    case 5: return "MBC5";
    default: return "unknown";
  }
}

bool parseHeader(const uint8_t *data, size_t len, Info *out, const char **why) {
  if (why) *why = "";
  if (!data || !out) return fail(why, "no data");
  if (len < 0x150) return fail(why, "too short");

  Info info;
  info.cart_type = data[0x0147];
  info.supported = typeSupported(info.cart_type);
  info.mbc = info.supported ? (uint8_t)kMbc[info.cart_type] : 0;

  const uint8_t rom_code = data[0x0148];
  if (rom_code > 8) return fail(why, "bad ROM size");
  info.rom_bytes = 32u * 1024u << rom_code;

  const uint8_t ram_code = data[0x0149];
  if (ram_code >= 6) return fail(why, "bad RAM size");
  info.ram_bytes = kRamBytes[ram_code];
  // MBC2 keeps 512 half-bytes on the chip itself and says nothing about it in
  // the header, so the header's zero is not the whole story.
  if (info.mbc == 2) info.ram_bytes = 512;

  switch (info.cart_type) {
    case 0x03: case 0x06: case 0x09: case 0x0D: case 0x0F: case 0x10:
    case 0x13: case 0x1B: case 0x1E:
      info.battery = true;
      break;
    default:
      info.battery = false;
  }

  // The header's own checksum. A file that fails this is not a cartridge —
  // the boot ROM on real hardware refuses to run it, and so does the core.
  uint8_t sum = 0;
  for (int i = 0x0134; i <= 0x014C; i++) sum = (uint8_t)(sum - data[i] - 1);
  if (sum != data[0x014D]) return fail(why, "bad header checksum");

  if (len < info.rom_bytes) return fail(why, "file is shorter than its header says");

  // The title is 16 bytes, padded with zeros, and later cartridges stole the
  // last few for other purposes — so stop at anything unprintable.
  int n = 0;
  for (; n < 16; n++) {
    const uint8_t c = data[0x0134 + n];
    if (c < 0x20 || c > 0x7E) break;
    info.title[n] = (char)c;
  }
  while (n > 0 && info.title[n - 1] == ' ') n--;
  info.title[n] = '\0';

  *out = info;
  return true;
}

}  // namespace gbrom
}  // namespace tabulous
