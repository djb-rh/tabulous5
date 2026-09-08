// iNES header parsing.
//
// Separate and Arduino-free because this reads bytes nobody here wrote: a
// truncated or mislabelled .nes dropped into /roms must be refused with a
// reason at pick time, not handed to the core to crash on. Same argument as
// wav.h, and it earns the same host tests.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tabulous {
namespace nesrom {

// What third_party/anemoia implements. A ROM needing anything else is listed but
// not launchable, which is friendlier than hiding it and leaving the owner
// wondering where their file went.
bool mapperSupported(uint8_t mapper);
const char *mapperName(uint8_t mapper);

struct Info {
  uint8_t mapper = 0;
  uint8_t prg_banks = 0;   // 16 KB each
  uint8_t chr_banks = 0;   // 8 KB each; 0 means CHR RAM
  bool has_trainer = false;
  bool supported = false;
  size_t expected_bytes = 0;  // header + trainer + PRG + CHR
};

// Parses the 16-byte iNES header. `len` is the whole file's length, used to
// check the file is actually as long as its header claims.
//
// Returns false with a short static reason in `why` for anything malformed.
bool parseHeader(const uint8_t *data, size_t len, Info *out,
                 const char **why = nullptr);

}  // namespace nesrom
}  // namespace tabulous
