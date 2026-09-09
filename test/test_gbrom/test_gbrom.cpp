#include <unity.h>

#include <vector>

#include "gbrom.h"

using namespace tabulous::gbrom;

void setUp() {}
void tearDown() {}

// A cartridge image with a valid header: type, ROM-size code, RAM-size code.
static std::vector<uint8_t> rom(uint8_t type, uint8_t rom_code, uint8_t ram_code,
                                const char *title = "TETRIS") {
  std::vector<uint8_t> r(32u * 1024u << rom_code, 0);
  for (int i = 0; title[i] && i < 16; i++) r[0x0134 + i] = (uint8_t)title[i];
  r[0x0147] = type;
  r[0x0148] = rom_code;
  r[0x0149] = ram_code;
  uint8_t sum = 0;
  for (int i = 0x0134; i <= 0x014C; i++) sum = (uint8_t)(sum - r[i] - 1);
  r[0x014D] = sum;
  return r;
}

void test_plain_rom() {
  auto r = rom(0x00, 0, 0);
  Info info;
  const char *why = "x";
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("", why);
  TEST_ASSERT_TRUE(info.supported);
  TEST_ASSERT_EQUAL_UINT8(0, info.mbc);
  TEST_ASSERT_EQUAL_UINT32(32u * 1024u, info.rom_bytes);
  TEST_ASSERT_EQUAL_UINT32(0, info.ram_bytes);
  TEST_ASSERT_FALSE(info.battery);
  TEST_ASSERT_EQUAL_STRING("TETRIS", info.title);
  TEST_ASSERT_EQUAL_STRING("ROM only", typeName(0x00));
}

void test_mbc3_with_battery_backed_ram() {
  auto r = rom(0x13, 5, 3, "ZELDA");  // MBC3+RAM+BATTERY, 1 MB, 32 KB RAM
  Info info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info));
  TEST_ASSERT_EQUAL_UINT8(3, info.mbc);
  TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, info.rom_bytes);
  TEST_ASSERT_EQUAL_UINT32(32u * 1024u, info.ram_bytes);
  TEST_ASSERT_TRUE(info.battery);
  TEST_ASSERT_EQUAL_STRING("MBC3", typeName(0x13));
}

void test_mbc2_has_ram_the_header_does_not_mention() {
  auto r = rom(0x06, 1, 0);  // MBC2+BATTERY
  Info info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info));
  TEST_ASSERT_EQUAL_UINT32(512, info.ram_bytes);
  TEST_ASSERT_TRUE(info.battery);
}

void test_unsupported_controllers_parse_but_are_flagged() {
  auto r = rom(0xFE, 0, 0);  // HuC1
  Info info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info));
  TEST_ASSERT_FALSE(info.supported);
  TEST_ASSERT_EQUAL_STRING("unknown", typeName(0xFE));
  TEST_ASSERT_FALSE(typeSupported(0x20));  // MBC6
  TEST_ASSERT_FALSE(typeSupported(0x22));  // MBC7
}

void test_malformed_files_are_refused_with_a_reason() {
  Info info;
  const char *why = "";
  std::vector<uint8_t> tiny(0x80, 0);
  TEST_ASSERT_FALSE(parseHeader(tiny.data(), tiny.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("too short", why);

  auto r = rom(0x00, 0, 0);
  r[0x014D] ^= 0xFF;
  TEST_ASSERT_FALSE(parseHeader(r.data(), r.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("bad header checksum", why);

  auto truncated = rom(0x00, 2, 0);  // header says 128 KB
  truncated.resize(40 * 1024);
  TEST_ASSERT_FALSE(parseHeader(truncated.data(), truncated.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("file is shorter than its header says", why);

  auto bad_size = rom(0x00, 0, 0);
  bad_size[0x0149] = 9;
  bad_size[0x014D] = 0;
  uint8_t sum = 0;
  for (int i = 0x0134; i <= 0x014C; i++) sum = (uint8_t)(sum - bad_size[i] - 1);
  bad_size[0x014D] = sum;
  TEST_ASSERT_FALSE(parseHeader(bad_size.data(), bad_size.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("bad RAM size", why);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_plain_rom);
  RUN_TEST(test_mbc3_with_battery_backed_ram);
  RUN_TEST(test_mbc2_has_ram_the_header_does_not_mention);
  RUN_TEST(test_unsupported_controllers_parse_but_are_flagged);
  RUN_TEST(test_malformed_files_are_refused_with_a_reason);
  return UNITY_END();
}
