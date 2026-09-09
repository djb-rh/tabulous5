// The .arc container: one arcade machine's ROMs in a single file.
//
// An arcade board's ROMs are a handful of separate chips. tools/mkarcade.py
// packs them, on a machine that already has the romset, into one file laid out
// exactly as the emulator wants — so the console's list stays one row per game
// and the firmware carries no zip reader.
//
// Arduino-free, and host-tested, for the same reason as nesrom and gbrom: it
// reads bytes nobody here wrote.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tabulous {
namespace arcrom {

constexpr size_t kHeaderBytes = 16;
constexpr size_t kCpuBytes = 16 * 1024;  // 0x0000-0x3FFF
constexpr size_t kGfxBytes = 8 * 1024;   // tiles and sprites
constexpr size_t kPaletteBytes = 32;     // the 16 hardware colours
constexpr size_t kColourBytes = 256;     // palette lookup
constexpr size_t kSoundBytes = 256;      // one wavetable PROM each
constexpr size_t kPayloadBytes =
    kCpuBytes + kGfxBytes + kPaletteBytes + kColourBytes + 2 * kSoundBytes;
constexpr size_t kFileBytes = kHeaderBytes + kPayloadBytes;

// Which board. Only one so far; the byte exists so a second does not need a
// new container.
enum class System : uint8_t { PacMan = 0 };

// Where each region starts, as an offset into the file.
struct Info {
  System system = System::PacMan;
  uint8_t version = 0;
  size_t cpu = 0, gfx = 0, palette = 0, colour = 0, sound1 = 0, sound2 = 0;
};

// False with a short static reason for anything malformed.
bool parse(const uint8_t *data, size_t len, Info *out, const char **why = nullptr);

}  // namespace arcrom
}  // namespace tabulous
