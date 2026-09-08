// The microSD slot, mounted once and kept mounted.
//
// Mounting is slow and not free of side effects (it reconfigures the SDIO
// pins), so nothing mounts on demand: begin() runs at boot, and anything that
// wants the card asks mounted() and uses fs().
#pragma once

#include <FS.h>

#include <cstdint>

namespace tabulous {
namespace sdcard {

// Tries the slot. Safe to call again; a card inserted after boot is picked up
// by the next call. Returns whether a card is mounted afterwards.
bool begin();
bool mounted();
fs::FS &fs();

// The VFS mount point (e.g. "/sdcard"), for code that goes through POSIX
// opendir/readdir rather than the Arduino File API.
const char *mountPoint();

uint64_t cardMB();
// "4-bit" or "1-bit" once mounted; "" before.
const char *busWidth();

}  // namespace sdcard
}  // namespace tabulous
