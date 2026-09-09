#include "rom_index.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

namespace tabulous {
namespace rom_index {
namespace {

constexpr size_t kBlock = 64 * 1024;
constexpr int kGrow = 512;

void *bigAlloc(size_t n) {
#ifdef ARDUINO
  return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return malloc(n);
#endif
}
void *bigRealloc(void *p, size_t n) {
#ifdef ARDUINO
  return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return realloc(p, n);
#endif
}
void bigFree(void *p) {
#ifdef ARDUINO
  heap_caps_free(p);
#else
  free(p);
#endif
}

bool lessItem(const Item &a, const Item &b) {
  if (a.group != b.group) return a.group < b.group;
  const int c = strcasecmp(a.name, b.name);
  if (c != 0) return c < 0;
  // Same title on flash and card: flash first, so the copy that is always
  // there is the one a fixed row position lands on.
  return a.source < b.source;
}

}  // namespace

uint8_t groupOf(const char *name) {
  // The first letter or digit, skipping punctuation: "'89 Dennou Kyuusei
  // Uranai" files under "#" with "1942", the way a record shop does it, and
  // "Solomon's Key" under S.
  for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
    if (isalpha(*p)) return (uint8_t)(2 + (toupper(*p) - 'A'));
    if (isdigit(*p)) return kOther;
  }
  return kOther;
}

const char *groupLabel(int group) {
  static const char *const labels[kGroups] = {
      "*", "#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
      "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
  return (group >= 0 && group < kGroups) ? labels[group] : "?";
}

Index::Index(const char *extension) {
  snprintf(extension_, sizeof(extension_), "%s", extension ? extension : "");
}
Index::~Index() { clear(); }

void Index::clear() {
  // Keep the favourites list across the rescan: whatever is starred now goes
  // back to being a name, matched again by the next finish().
  for (int i : favs_) {
    const char *f = items_[i].file;
    bool dup = false;
    for (const std::string &s : orphan_favs_) dup = dup || s == f;
    if (!dup) orphan_favs_.push_back(f);
  }
  for (char *b : blocks_) bigFree(b);
  blocks_.clear();
  block_used_ = 0;
  bigFree(items_);
  items_ = nullptr;
  count_ = capacity_ = 0;
  finished_ = false;
  favs_.clear();
  for (int &g : group_begin_) g = 0;
  // orphan_favs_ survive a rescan on purpose: they are the owner's list, not
  // this card's.
}

const char *Index::intern(const char *s, size_t len) {
  if (blocks_.empty() || block_used_ + len + 1 > kBlock) {
    char *b = (char *)bigAlloc(kBlock);
    if (!b) return "";
    blocks_.push_back(b);
    block_used_ = 0;
  }
  char *dst = blocks_.back() + block_used_;
  memcpy(dst, s, len);
  dst[len] = '\0';
  block_used_ += len + 1;
  return dst;
}

void Index::add(const char *dir, const char *file, Source source) {
  if (finished_) return;
  const size_t flen = strlen(file);
  const size_t elen = strlen(extension_);
  if (flen <= elen || strcasecmp(file + flen - elen, extension_) != 0) return;

  if (count_ == capacity_) {
    const int cap = capacity_ + kGrow;
    Item *grown = (Item *)bigRealloc(items_, sizeof(Item) * cap);
    if (!grown) return;
    items_ = grown;
    capacity_ = cap;
  }

  Item &it = items_[count_];
  it.file = intern(file, flen);

  // Display name: drop the extension, and turn the underscores homebrew
  // authors use instead of spaces back into spaces.
  char name[160];
  size_t n = flen - elen;
  if (n >= sizeof(name)) n = sizeof(name) - 1;
  memcpy(name, file, n);
  name[n] = '\0';
  for (char *p = name; *p; p++) {
    if (*p == '_') *p = ' ';
  }
  it.name = intern(name, n);

  char path[256];
  const size_t plen = (size_t)snprintf(path, sizeof(path), "%s/%s", dir, file);
  it.path = intern(path, plen < sizeof(path) ? plen : sizeof(path) - 1);

  it.source = source;
  it.group = groupOf(it.name);
  it.fav = false;
  it.status = -1;
  it.mapper = 0;
  it.bytes = 0;
  it.problem = "";
  count_++;
}

void Index::finish() {
  if (finished_) return;
  finished_ = true;
  std::sort(items_, items_ + count_, lessItem);
  // Group starts, as a running count: group_begin_[g+1] is one past the end.
  int counts[kGroups] = {0};
  for (int i = 0; i < count_; i++) counts[items_[i].group]++;
  group_begin_[0] = 0;
  for (int g = 0; g < kGroups; g++) group_begin_[g + 1] = group_begin_[g] + counts[g];
  // Re-apply whatever favourites were known before this (re)scan.
  std::vector<std::string> names = orphan_favs_;
  orphan_favs_.clear();
  for (const std::string &s : names) {
    bool found = false;
    for (int i = 0; i < count_; i++) {
      if (strcmp(items_[i].file, s.c_str()) == 0) {
        items_[i].fav = true;
        found = true;
      }
    }
    if (!found) orphan_favs_.push_back(s);
  }
  rebuildFavourites();
}

int Index::groupBegin(int group) const {
  if (group < 0 || group >= kGroups) return 0;
  return group_begin_[group];
}

int Index::groupCount(int group) const {
  if (group < 0 || group >= kGroups) return 0;
  return group_begin_[group + 1] - group_begin_[group];
}

void Index::rebuildFavourites() {
  favs_.clear();
  for (int i = 0; i < count_; i++) {
    if (items_[i].fav) favs_.push_back(i);
  }
}

void Index::applyFavourites(const char *text) {
  for (int i = 0; i < count_; i++) items_[i].fav = false;
  orphan_favs_.clear();
  std::vector<std::string> names;
  const char *p = text ? text : "";
  while (*p) {
    const char *e = strchr(p, '\n');
    const size_t len = e ? (size_t)(e - p) : strlen(p);
    size_t n = len;
    while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ')) n--;
    if (n > 0) names.emplace_back(p, n);
    if (!e) break;
    p = e + 1;
  }
  // Route through finish()'s matching so orphans are kept.
  orphan_favs_ = names;
  if (finished_) {
    std::vector<std::string> pending = orphan_favs_;
    orphan_favs_.clear();
    for (const std::string &s : pending) {
      bool found = false;
      for (int i = 0; i < count_; i++) {
        if (strcmp(items_[i].file, s.c_str()) == 0) {
          items_[i].fav = true;
          found = true;
        }
      }
      if (!found) orphan_favs_.push_back(s);
    }
    rebuildFavourites();
  }
}

std::string Index::favouritesText() const {
  std::string out;
  for (int i : favs_) {
    out += items_[i].file;
    out += '\n';
  }
  for (const std::string &s : orphan_favs_) {
    out += s;
    out += '\n';
  }
  return out;
}

void Index::setFavourite(int i, bool fav) {
  if (i < 0 || i >= count_) return;
  // The same title on both flash and card is one favourite, not two.
  for (int j = 0; j < count_; j++) {
    if (strcmp(items_[j].file, items_[i].file) == 0) items_[j].fav = fav;
  }
  rebuildFavourites();
}

}  // namespace rom_index
}  // namespace tabulous
