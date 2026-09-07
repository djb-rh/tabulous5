// Loading word packs off LittleFS.
//
// Split from pack.h so the parser itself stays free of Arduino headers and can
// be unit-tested on the host; this file is the thin filesystem shim over it.
#pragma once

#include <string>
#include <vector>

#include "pack.h"

namespace tabulous {
namespace content {

struct LoadReport {
  bool mounted = false;
  size_t packs_loaded = 0;
  size_t phrases_total = 0;
  size_t files_skipped = 0;   // present but empty or unparseable
  size_t bytes_total = 0;
  size_t bytes_used = 0;
  bool used_builtin = false;  // no packs on flash, fell back to the built-in
};

// Mounts LittleFS (formatting if it has never been used) and loads every
// /packs/*.txt into `out`. If none are found, loads a single small built-in
// pack so a device with an unflashed filesystem is still playable rather than
// showing an empty category list.
LoadReport loadAll(std::vector<Pack> *out);

const char *packsDir();

}  // namespace content
}  // namespace tabulous
