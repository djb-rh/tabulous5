#include "nesrom.h"

#include <cstring>

namespace tabulous {
namespace nesrom {
namespace {

bool fail(const char **why, const char *reason) {
  if (why) *why = reason;
  return false;
}

}  // namespace

bool mapperSupported(uint8_t mapper) {
  return mapper == 0 || mapper == 1 || mapper == 2 || mapper == 3 ||
         mapper == 4 || mapper == 69;
}

const char *mapperName(uint8_t mapper) {
  switch (mapper) {
    case 0: return "NROM";
    case 1: return "MMC1";
    case 2: return "UxROM";
    case 3: return "CNROM";
    case 4: return "MMC3";
    case 7: return "AxROM";
    case 69: return "FME-7";
    default: return "unsupported";
  }
}

bool parseHeader(const uint8_t *data, size_t len, Info *out, const char **why) {
  if (why) *why = "";
  if (!data || !out) return fail(why, "no data");
  if (len < 16) return fail(why, "too short");
  if (memcmp(data, "NES\x1A", 4) != 0) return fail(why, "not an iNES file");

  Info info;
  info.prg_banks = data[4];
  info.chr_banks = data[5];
  info.has_trainer = (data[6] & 0x04) != 0;

  // The mapper number is split across two bytes, low nibble in 6, high in 7.
  // NES 2.0 puts a marker in bits 2-3 of byte 7; the low bits still mean the
  // same thing, so a NES 2.0 file with a supported mapper still plays.
  info.mapper = (uint8_t)((data[6] >> 4) | (data[7] & 0xF0));
  info.supported = mapperSupported(info.mapper);

  if (info.prg_banks == 0) return fail(why, "no PRG ROM");

  info.expected_bytes = 16 + (info.has_trainer ? 512 : 0) +
                        (size_t)info.prg_banks * 16384 +
                        (size_t)info.chr_banks * 8192;
  // A file shorter than its own header claims is truncated, and the core would
  // read past the end of it.
  if (len < info.expected_bytes) return fail(why, "file is truncated");

  *out = info;
  return true;
}

}  // namespace nesrom
}  // namespace tabulous
