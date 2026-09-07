#include "highscores.h"

#include <Preferences.h>

namespace tabulous {
namespace highscores {
namespace {
constexpr const char *kNamespace = "tab5scores";
Preferences g_prefs;
}  // namespace

Table load(const char *key) {
  Table t;
  if (!key || !g_prefs.begin(kNamespace, true)) return t;
  Table stored;
  const size_t n = g_prefs.getBytes(key, &stored, sizeof(stored));
  g_prefs.end();
  // Only trust a blob of exactly the expected size and a sane count: a short
  // or stale read must not be interpreted as a table.
  if (n == sizeof(stored) && stored.count <= kMax) {
    stored.entries[kMax - 1].name[kNameLen] = '\0';
    for (int i = 0; i < kMax; i++) stored.entries[i].name[kNameLen] = '\0';
    return stored;
  }
  return t;
}

void save(const char *key, const Table &table) {
  if (!key || !g_prefs.begin(kNamespace, false)) return;
  g_prefs.putBytes(key, &table, sizeof(table));
  g_prefs.end();
}

void clear(const char *key) {
  if (!key || !g_prefs.begin(kNamespace, false)) return;
  g_prefs.remove(key);
  g_prefs.end();
}

}  // namespace highscores
}  // namespace tabulous
