// The console's flash filesystem, as far as its games read from it: here it is the folder /sd/tabulous on the
// card. A launcher app cannot list a folder, so a folder that is to be listed carries an index.txt naming its
// files, one a line (stage_sd.sh writes it).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
class File {
 public:
  explicit operator bool() const { return st_ != nullptr; }
  size_t size() const;
  size_t read(uint8_t *buf, size_t len);
  int read();
  int available();
  void close();
  bool isDirectory() const;
  File openNextFile();
  const char *name() const;
 private:
  friend class LittleFSClass;
  struct State;
  State *st_ = nullptr;
};
class LittleFSClass {
 public:
  bool begin(bool format_on_fail = false) { (void)format_on_fail; return true; }
  File open(const char *path, const char *mode = "r");
  size_t totalBytes() { return 0; }
  size_t usedBytes() { return 0; }
};
extern LittleFSClass LittleFS;
