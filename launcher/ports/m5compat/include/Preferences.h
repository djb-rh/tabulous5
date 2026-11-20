// Arduino's Preferences, as far as Tabulous5 uses it, kept in one small file a namespace on the card
// (/sd/tabulous/<namespace>.prefs). Also Arduino's String, as far as Preferences hands it out.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
class String {
 public:
  String(const char *s = "") : s_(s ? s : "") {}
  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
 private:
  std::string s_;
};
class Preferences {
 public:
  bool begin(const char *name, bool read_only = false);
  void end();
  size_t getBytes(const char *key, void *buf, size_t max);
  size_t putBytes(const char *key, const void *buf, size_t len);
  size_t getBytesLength(const char *key);
  uint8_t getUChar(const char *key, uint8_t def = 0);
  size_t putUChar(const char *key, uint8_t v);
  int8_t getChar(const char *key, int8_t def = 0);
  size_t putChar(const char *key, int8_t v);
  bool getBool(const char *key, bool def = false);
  size_t putBool(const char *key, bool v);
  uint16_t getUShort(const char *key, uint16_t def = 0);
  size_t putUShort(const char *key, uint16_t v);
  uint32_t getUInt(const char *key, uint32_t def = 0);
  size_t putUInt(const char *key, uint32_t v);
  String getString(const char *key, const char *def = "");
  String getString(const char *key, const String &def) { return getString(key, def.c_str()); }
  size_t putString(const char *key, const char *v);
  size_t putString(const char *key, const String &v) { return putString(key, v.c_str()); }
  bool isKey(const char *key);
  bool remove(const char *key);
 private:
  char ns_[24] = {0};
};
