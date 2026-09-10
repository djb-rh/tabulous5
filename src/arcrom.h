// The .arc container: one arcade machine's ROMs in a single file.
//
// An arcade board's ROMs are a handful of separate chips. tools/mkarcade.py
// packs them, on a machine that already has the romset, into one file laid out
// exactly as the emulator wants -- so the console's list stays one row per game
// and the firmware carries no zip reader.
//
// Arduino-free, and host-tested, for the same reason as nesrom and gbrom: it
// reads bytes nobody here wrote.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tabulous {
namespace arcrom {

constexpr size_t kV1HeaderBytes = 16;
constexpr size_t kHeaderBytes = 32;
constexpr size_t kCpuBytes = 16 * 1024;      // 0x0000-0x3FFF
constexpr size_t kCpuHighBank = 4 * 1024;    // one chip at 0x8000 and up
constexpr size_t kMaxCpuHighBanks = 4;       // 0x8000-0xBFFF at most
constexpr size_t kGfxBytes = 8 * 1024;       // tiles and sprites
constexpr size_t kPaletteBytes = 32;         // the 16 hardware colours
constexpr size_t kColourBytes = 256;         // palette lookup
constexpr size_t kSoundBytes = 256;          // one wavetable PROM each
constexpr size_t kBasePayloadBytes =
    kCpuBytes + kGfxBytes + kPaletteBytes + kColourBytes + 2 * kSoundBytes;

// The largest a file can be, which is what the loader reserves.
constexpr size_t kMaxFileBytes = kHeaderBytes + kBasePayloadBytes +
                                 2 * kMaxCpuHighBanks * kCpuHighBank + kCpuBytes;

// Kept so files made before the format grew still load.
constexpr size_t kV1FileBytes = kV1HeaderBytes + kBasePayloadBytes;

// Which board. Only one so far; the byte exists so a second does not need a
// new container.
enum class System : uint8_t { PacMan = 0 };

// A few boards put a PAL between the program and the interrupt vector latch,
// so the byte the program writes is not the byte the CPU is given. MAME models
// it as a short lookup, and so do we.
constexpr size_t kMaxVectorFixups = 4;

// Where each region starts, as an offset into the file.
struct Info {
  System system = System::PacMan;
  uint8_t version = 0;
  size_t cpu = 0, cpu_high = 0, gfx = 0, palette = 0, colour = 0;
  size_t sound1 = 0, sound2 = 0;
  size_t cpu_high_bytes = 0;  // ROM at 0x8000 upwards; usually none

  // Nearly every game on this board ran with the monitor stood on its side,
  // and the picture has to be turned upright to be read. A few -- Ponpoko and
  // its bootlegs -- ran with the monitor the usual way round, and turning
  // those would be the bug. False means leave the raster alone.
  bool upright_monitor = true;

  // Ms. Pac-Man was sold as a kit that plugged into a Pac-Man board: an extra
  // ROM board that watches the address bus and swaps the whole program between
  // the original Pac-Man code and its own, encrypted, copy. When this is set
  // the file carries that second copy after the first, same shape and size,
  // and the emulation switches between them.
  // Nearly every game on this board was played on a four-way stick, which
  // makes two directions at once physically impossible. Three were not.
  bool eight_way = false;

  bool daughtercard = false;
  size_t alt_cpu = 0, alt_cpu_high = 0;
  uint8_t vector_fixups = 0;
  uint8_t vector_from[kMaxVectorFixups] = {0, 0, 0, 0};
  uint8_t vector_to[kMaxVectorFixups] = {0, 0, 0, 0};
  size_t file_bytes = 0;
};

// False with a short static reason for anything malformed.
bool parse(const uint8_t *data, size_t len, Info *out, const char **why = nullptr);

// How long the file claims to be, from its header alone, so the loader can
// read a header first and then the rest. Zero if the header is not one of ours.
size_t fileBytes(const uint8_t *header, size_t len);

}  // namespace arcrom
}  // namespace tabulous
