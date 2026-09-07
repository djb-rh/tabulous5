// Word pack parsing and phrase selection.
//
// Deliberately free of Arduino/ESP headers so it builds and unit-tests on the
// host (`pio test -e native`). Anything that touches the filesystem lives in
// content.h instead.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tabulous {

enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

struct Phrase {
  std::string text;
  Difficulty difficulty = Difficulty::Medium;
};

struct PackMeta {
  std::string name;      // display name, falls back to the filename stem
  std::string icon;      // icon key, e.g. "film"
  uint32_t color = 0x4C9F70;  // 0xRRGGBB, category theme color
};

// A parsed category file.
//
// Format (see README): `# key: value` header lines, then one phrase per line
// with an optional ` | difficulty` suffix. Blank lines ignored. Parsing is
// deliberately lenient — a pack hand-edited on a phone at a party should still
// load. Strictness lives in tools/packs.py, which runs before flashing.
class Pack {
 public:
  // `fallback_name` is used when the file has no `# name:` header.
  void parse(const std::string &source, const std::string &fallback_name = "");

  const PackMeta &meta() const { return meta_; }
  const std::vector<Phrase> &phrases() const { return phrases_; }
  bool empty() const { return phrases_.empty(); }
  size_t size() const { return phrases_.size(); }

  // Indices of phrases at or below `max`. Drives kids mode: filtering to Easy
  // yields only the easy phrases, Hard yields everything.
  std::vector<size_t> indicesUpTo(Difficulty max) const;

 private:
  PackMeta meta_;
  std::vector<Phrase> phrases_;
};

// Draw-without-replacement selector.
//
// Using rand() % n directly means the same phrase can come up twice in three
// turns, which players read as the game being broken. A bag guarantees every
// phrase appears once before any repeats.
class ShuffleBag {
 public:
  // `count` is how many items to draw from; `seed` makes tests deterministic.
  void reset(size_t count, uint32_t seed);

  // Returns the next index in [0, count). Reshuffles automatically when the
  // bag empties. Returns 0 if the bag is empty (count == 0).
  size_t next();

  size_t remaining() const { return order_.size() - cursor_; }
  size_t count() const { return order_.size(); }

  // True if the most recent next() began a fresh cycle — i.e. the pack has
  // been exhausted at least once.
  bool justWrapped() const { return just_wrapped_; }

 private:
  std::vector<size_t> order_;
  size_t cursor_ = 0;
  uint32_t rng_ = 1;
  bool just_wrapped_ = false;
  bool has_last_ = false;
  size_t last_drawn_ = 0;

  uint32_t rand32();
  void shuffle();
};

// Exposed for testing.
Difficulty parseDifficulty(const std::string &text, bool *ok = nullptr);
uint32_t parseHexColor(const std::string &text, bool *ok = nullptr);

}  // namespace tabulous
