// Per-game, per-difficulty score tables.
//
// The ranking logic is deliberately free of Arduino headers so it unit-tests
// on the host: off-by-one errors in "does this qualify" and "where does it go"
// are easy to write and annoying to find by playing.
//
// Times are lower-is-better; a future game scoring points would pass false.
#pragma once

#include <cstdint>
#include <cstring>

namespace tabulous {
namespace highscores {

constexpr int kMax = 5;
constexpr size_t kNameLen = 16;

struct Entry {
  char name[kNameLen + 1] = {0};
  uint32_t value = 0;
};

struct Table {
  Entry entries[kMax];
  uint8_t count = 0;

  bool better(uint32_t a, uint32_t b, bool lower_is_better) const {
    return lower_is_better ? a < b : a > b;
  }

  // Where `value` would land, or -1 if it doesn't make the table. Ties do NOT
  // displace an existing entry: matching someone's time shouldn't push them
  // down, and it means a repeated identical result is not endlessly re-entered.
  int rankFor(uint32_t value, bool lower_is_better) const {
    for (int i = 0; i < count; i++) {
      if (better(value, entries[i].value, lower_is_better)) return i;
    }
    return count < kMax ? count : -1;
  }

  bool qualifies(uint32_t value, bool lower_is_better) const {
    return rankFor(value, lower_is_better) >= 0;
  }

  // Returns the position taken, or -1 if it didn't qualify.
  int insert(const char *name, uint32_t value, bool lower_is_better) {
    const int at = rankFor(value, lower_is_better);
    if (at < 0) return -1;

    for (int i = (count < kMax ? count : kMax - 1); i > at; i--) {
      entries[i] = entries[i - 1];
    }
    Entry &e = entries[at];
    memset(e.name, 0, sizeof(e.name));
    if (name) strncpy(e.name, name, kNameLen);
    e.value = value;
    if (count < kMax) count++;
    return at;
  }
};

// NVS-backed. `key` must be short: NVS keys are limited to 15 characters.
Table load(const char *key);
void save(const char *key, const Table &table);
void clear(const char *key);

}  // namespace highscores
}  // namespace tabulous
