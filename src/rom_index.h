// The ROM library index: every cartridge the console can see for one system,
// grouped for a person to browse rather than listed the way the filesystem
// found them. One index per system — the extension it accepts is the only
// thing that makes it a NES index or a Game Boy one.
//
// Arduino-free so the grouping, ordering and favourites bookkeeping run under
// the host tests. The filesystem walk that feeds it lives in rom_browser.cpp.
//
// Thousands of entries live here (a full No-Intro set is ~5,800), so the
// strings and the table itself go into PSRAM on the device: internal RAM is
// what the emulator itself runs in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tabulous {
namespace rom_index {

// Group 0 is Favourites, 1 is "#" (titles starting with a digit or symbol),
// 2..27 are A..Z. Twenty-eight in all, which happens to fill a 2 x 14 rail.
constexpr int kGroups = 28;
constexpr int kFavourites = 0;
constexpr int kOther = 1;

uint8_t groupOf(const char *name);
const char *groupLabel(int group);

enum Source : uint8_t { kFlash = 0, kCard = 1 };

struct Item {
  const char *name;   // for display: file name without ".nes", '_' as ' '
  const char *path;   // as the source filesystem wants it, e.g. "/roms/S/x.nes"
  const char *file;   // basename, the favourites key
  Source source;
  uint8_t group;
  bool fav;
  // Filled in lazily by whoever opens the file; -1 until then.
  int8_t status;        // -1 unknown, 0 playable, 1 not playable
  uint8_t mapper;
  uint32_t bytes;
  const char *problem;  // static string when status == 1
};

class Index {
 public:
  // `extension` is matched case-insensitively and includes the dot (".gb").
  explicit Index(const char *extension);
  ~Index();
  Index(const Index &) = delete;
  Index &operator=(const Index &) = delete;

  void clear();
  // `file` is the basename including its extension; `dir` is the directory it
  // sits in, joined with '/' to make the path. Anything with the wrong
  // extension is ignored. Strings are copied.
  void add(const char *dir, const char *file, Source source);
  // Drops every entry with this file name. Only meaningful before finish().
  void removeFile(const char *file);

  // Sorts into groups. Call once after the last add(); adds after that are
  // ignored until the next clear().
  void finish();

  int size() const { return count_; }
  const Item &at(int i) const { return items_[i]; }
  Item &mutableAt(int i) { return items_[i]; }

  // Contiguous ranges into the sorted table for groups 1..27. Favourites is
  // assembled from flags instead: favouritesAt(n) gives the n-th one.
  int groupBegin(int group) const;
  int groupCount(int group) const;
  int favouriteCount() const { return (int)favs_.size(); }
  int favouriteAt(int n) const { return favs_[n]; }

  // Favourites are kept as one basename per line, in a file the owner can read
  // and edit. applyFavourites() sets the flags from such text; favouritesText()
  // produces it. Names in the text that match no item are kept so a favourite
  // on a card that is not inserted right now survives the round trip.
  void applyFavourites(const char *text);
  std::string favouritesText() const;
  void setFavourite(int i, bool fav);

 private:
  char extension_[8] = {0};
  Item *items_ = nullptr;
  int count_ = 0, capacity_ = 0;
  bool finished_ = false;
  int group_begin_[kGroups + 1] = {0};
  std::vector<int> favs_;
  std::vector<std::string> orphan_favs_;  // favourites naming no present item

  // String arena: many small strings, freed all at once.
  std::vector<char *> blocks_;
  size_t block_used_ = 0;
  const char *intern(const char *s, size_t len);
  void rebuildFavourites();
};

}  // namespace rom_index
}  // namespace tabulous
