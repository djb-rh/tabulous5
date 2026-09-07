#include "content.h"

#include <LittleFS.h>

#include <algorithm>

namespace tabulous {
namespace content {
namespace {

constexpr const char *kPacksDir = "/packs";

// Enough to play a round if the filesystem was never flashed. Deliberately
// tiny — the real content lives in data/packs and is uploaded with
// `pio run -e tab5 -t uploadfs`.
constexpr const char *kBuiltinPack =
    "# name: Starter\n"
    "# color: #4C9F70\n"
    "# icon: house\n"
    "Alarm clock | easy\nShopping cart | easy\nRubber band | easy\n"
    "Coffee mug | easy\nDoorbell | easy\nUmbrella | easy\nBicycle | easy\n"
    "Traffic jam\nRush hour\nPiggy bank\nCeiling fan\nWelcome mat\n"
    "Bubble wrap\nVending machine\nRevolving door\nSnooze button\n";

std::string stemOf(const std::string &filename) {
  size_t start = filename.find_last_of('/');
  start = (start == std::string::npos) ? 0 : start + 1;
  size_t end = filename.find_last_of('.');
  if (end == std::string::npos || end < start) end = filename.size();
  return filename.substr(start, end - start);
}

bool endsWithTxt(const std::string &name) {
  if (name.size() < 4) return false;
  std::string tail = name.substr(name.size() - 4);
  for (char &c : tail) c = (char)tolower((unsigned char)c);
  return tail == ".txt";
}

}  // namespace

const char *packsDir() { return kPacksDir; }

LoadReport loadAll(std::vector<Pack> *out) {
  LoadReport report;
  out->clear();

  // `true` formats on a failed mount — a brand-new device has no filesystem
  // yet and should end up playable, not bricked at a mount error.
  if (!LittleFS.begin(true)) return report;
  report.mounted = true;
  report.bytes_total = LittleFS.totalBytes();
  report.bytes_used = LittleFS.usedBytes();

  File dir = LittleFS.open(kPacksDir);
  if (dir && dir.isDirectory()) {
    // Collect names first, then read. Holding a directory handle open while
    // opening files inside it is asking for trouble on LittleFS.
    std::vector<std::string> names;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (!f.isDirectory()) {
        const std::string name = f.name();
        if (endsWithTxt(name)) names.push_back(name);
      }
      f.close();
    }
    dir.close();
    std::sort(names.begin(), names.end());

    for (const std::string &name : names) {
      // openNextFile() may hand back a bare name or a full path depending on
      // core version, so normalise before reopening.
      const std::string path =
          name.front() == '/' ? name : std::string(kPacksDir) + "/" + name;
      File f = LittleFS.open(path.c_str(), "r");
      if (!f) {
        report.files_skipped++;
        continue;
      }
      std::string source;
      source.reserve(f.size());
      while (f.available()) source.push_back((char)f.read());
      f.close();

      Pack pack;
      pack.parse(source, stemOf(path));
      if (pack.empty()) {
        report.files_skipped++;
        continue;
      }
      report.phrases_total += pack.size();
      out->push_back(std::move(pack));
    }
  }

  if (out->empty()) {
    Pack pack;
    pack.parse(kBuiltinPack, "Starter");
    report.phrases_total = pack.size();
    out->push_back(std::move(pack));
    report.used_builtin = true;
  }

  report.packs_loaded = out->size();
  return report;
}

}  // namespace content
}  // namespace tabulous
