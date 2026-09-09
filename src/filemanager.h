// A file manager for the SD card and the built-in filesystem, in the browser.
//
// The content editor next door edits text; this moves files about: upload
// ROMs to the card from a laptop, make folders, rename, delete, and hide the
// titles you never want to see in the list without deleting them. It hangs
// its routes off the content server, so it is reachable whenever that is.
//
// Hidden files are recorded as one name per line in a `.hidden` file in the
// same directory, which the NES browser reads at scan time. A plain text
// file, so it can be edited by hand, and it travels with the card.
#pragma once

#include <cstdint>

namespace tabulous {
namespace filemanager {

// Registers with the content server. Call once at boot, before the server
// is ever started.
void begin();

// Bumped whenever a file is added, removed, renamed or hidden, so the ROM
// list knows to look again.
uint32_t revision();

}  // namespace filemanager
}  // namespace tabulous
