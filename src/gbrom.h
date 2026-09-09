// Game Boy cartridge headers.
//
// Separate and Arduino-free for the same reason as nesrom.h: this reads bytes
// nobody here wrote, so a truncated or unsupported cartridge is refused with a
// reason at pick time rather than handed to the core to fall over on. Same
// argument, same host tests.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tabulous {
namespace gbrom {

// The memory bank controllers Peanut-GB implements, by the cartridge-type
// byte at 0x0147.
bool typeSupported(uint8_t cart_type);
// "MBC1", "MBC5", "ROM only"... for the browser to show.
const char *typeName(uint8_t cart_type);

struct Info {
  uint8_t cart_type = 0;
  uint8_t mbc = 0;          // 0 for a plain ROM, else 1, 2, 3 or 5
  uint32_t rom_bytes = 0;   // as the header declares it
  uint32_t ram_bytes = 0;   // cartridge RAM, 0 if none
  bool battery = false;     // ...and whether it is backed up, i.e. saves
  bool supported = false;
  char title[17] = {0};
};

// Parses the 0x150-byte header. `len` is the whole file's length, used to
// check the file is as long as it claims. False with a short static reason.
bool parseHeader(const uint8_t *data, size_t len, Info *out,
                 const char **why = nullptr);

}  // namespace gbrom
}  // namespace tabulous
