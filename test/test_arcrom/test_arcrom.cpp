#include <unity.h>

#include <cstring>
#include <vector>

#include "arcrom.h"

using namespace tabulous::arcrom;

void setUp() {}
void tearDown() {}

// A version 1 file: no upper program ROM, no vector fixups, 16-byte header.
static std::vector<uint8_t> v1(uint8_t system = 0, uint8_t version = 1,
                               uint32_t payload = kBasePayloadBytes) {
  std::vector<uint8_t> f(kV1FileBytes, 0);
  memcpy(f.data(), "TAB5ARC1", 8);
  f[8] = system;
  f[9] = version;
  memcpy(f.data() + 12, &payload, sizeof(payload));
  return f;
}

static std::vector<uint8_t> v2(uint8_t high_banks, uint8_t fixups,
                               bool daughtercard = false) {
  const uint32_t program = kCpuBytes + high_banks * kCpuHighBank;
  const uint32_t payload = kBasePayloadBytes + high_banks * kCpuHighBank +
                           (daughtercard ? program : 0);
  std::vector<uint8_t> f(kHeaderBytes + payload, 0);
  memcpy(f.data(), "TAB5ARC1", 8);
  f[9] = 2;
  f[10] = high_banks;
  f[11] = fixups;
  f[25] = daughtercard ? 1 : 0;
  memcpy(f.data() + 12, &payload, sizeof(payload));
  for (uint8_t i = 0; i < fixups; i++) {
    f[16 + 2 * i] = 0xF0 + i;
    f[17 + 2 * i] = 0x10 + i;
  }
  return f;
}

void test_regions_follow_each_other() {
  auto f = v1();
  Info info;
  const char *why = "x";
  TEST_ASSERT_TRUE(parse(f.data(), f.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("", why);
  TEST_ASSERT_EQUAL_size_t(16, info.cpu);
  TEST_ASSERT_EQUAL_size_t(0, info.cpu_high_bytes);
  TEST_ASSERT_EQUAL_size_t(16 + 16384, info.gfx);
  TEST_ASSERT_EQUAL_size_t(16 + 16384 + 8192, info.palette);
  TEST_ASSERT_EQUAL_size_t(info.palette + 32, info.colour);
  TEST_ASSERT_EQUAL_size_t(info.colour + 256, info.sound1);
  TEST_ASSERT_EQUAL_size_t(info.sound1 + 256, info.sound2);
  TEST_ASSERT_EQUAL_size_t(kV1FileBytes, info.sound2 + 256);
}

void test_upper_program_rom_pushes_the_later_regions_along() {
  auto f = v2(2, 0);
  Info info;
  const char *why = "x";
  TEST_ASSERT_TRUE(parse(f.data(), f.size(), &info, &why));
  TEST_ASSERT_EQUAL_UINT8(2, info.version);
  TEST_ASSERT_EQUAL_size_t(32, info.cpu);
  TEST_ASSERT_EQUAL_size_t(32 + 16384, info.cpu_high);
  TEST_ASSERT_EQUAL_size_t(8192, info.cpu_high_bytes);
  TEST_ASSERT_EQUAL_size_t(info.cpu_high + 8192, info.gfx);
  TEST_ASSERT_EQUAL_size_t(f.size(), info.sound2 + 256);
  TEST_ASSERT_EQUAL_size_t(f.size(), info.file_bytes);
}

void test_the_monitor_is_sideways_unless_the_file_says_otherwise() {
  auto sideways = v2(0, 0);
  Info info;
  TEST_ASSERT_TRUE(parse(sideways.data(), sideways.size(), &info));
  TEST_ASSERT_TRUE(info.upright_monitor);

  auto flat = v2(0, 0);
  flat[24] = 1;
  TEST_ASSERT_TRUE(parse(flat.data(), flat.size(), &info));
  TEST_ASSERT_FALSE(info.upright_monitor);

  // Files made before the byte meant anything are sideways, as they were.
  auto old = v1();
  TEST_ASSERT_TRUE(parse(old.data(), old.size(), &info));
  TEST_ASSERT_TRUE(info.upright_monitor);
}

void test_the_kit_puts_a_second_program_before_the_tiles() {
  auto f = v2(4, 0, true);
  Info info;
  const char *why = "x";
  TEST_ASSERT_TRUE(parse(f.data(), f.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("", why);
  TEST_ASSERT_TRUE(info.daughtercard);
  TEST_ASSERT_EQUAL_size_t(32, info.cpu);
  TEST_ASSERT_EQUAL_size_t(32 + 16384, info.cpu_high);
  TEST_ASSERT_EQUAL_size_t(16384, info.cpu_high_bytes);
  // The second copy is the same shape as the first, and the tiles follow it.
  TEST_ASSERT_EQUAL_size_t(32 + 32768, info.alt_cpu);
  TEST_ASSERT_EQUAL_size_t(32 + 49152, info.alt_cpu_high);
  TEST_ASSERT_EQUAL_size_t(32 + 65536, info.gfx);
  TEST_ASSERT_EQUAL_size_t(f.size(), info.sound2 + 256);

  // Without the kit the tiles follow the first copy, as before.
  auto plain = v2(4, 0);
  TEST_ASSERT_TRUE(parse(plain.data(), plain.size(), &info));
  TEST_ASSERT_FALSE(info.daughtercard);
  TEST_ASSERT_EQUAL_size_t(32 + 32768, info.gfx);

  auto unknown = v2(0, 0);
  unknown[25] = 7;
  TEST_ASSERT_FALSE(parse(unknown.data(), unknown.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("unknown add-on board", why);
}

void test_vector_fixups_are_read_back() {
  auto f = v2(0, 3);
  Info info;
  TEST_ASSERT_TRUE(parse(f.data(), f.size(), &info));
  TEST_ASSERT_EQUAL_UINT8(3, info.vector_fixups);
  TEST_ASSERT_EQUAL_HEX8(0xF0, info.vector_from[0]);
  TEST_ASSERT_EQUAL_HEX8(0x10, info.vector_to[0]);
  TEST_ASSERT_EQUAL_HEX8(0xF2, info.vector_from[2]);
  TEST_ASSERT_EQUAL_HEX8(0x12, info.vector_to[2]);
}

void test_the_header_alone_says_how_long_the_file_is() {
  auto one = v1();
  TEST_ASSERT_EQUAL_size_t(kV1FileBytes, fileBytes(one.data(), kV1HeaderBytes));
  auto two = v2(4, 0);
  TEST_ASSERT_EQUAL_size_t(two.size(), fileBytes(two.data(), kHeaderBytes));
  std::vector<uint8_t> junk(kHeaderBytes, 0);
  TEST_ASSERT_EQUAL_size_t(0, fileBytes(junk.data(), junk.size()));
}

void test_malformed_files_are_refused_with_a_reason() {
  Info info;
  const char *why = "";

  std::vector<uint8_t> tiny(8, 0);
  TEST_ASSERT_FALSE(parse(tiny.data(), tiny.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("too short", why);

  auto bad_magic = v1();
  bad_magic[0] = 'X';
  TEST_ASSERT_FALSE(parse(bad_magic.data(), bad_magic.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("not an .arc file", why);

  auto bad_system = v1(9);
  TEST_ASSERT_FALSE(parse(bad_system.data(), bad_system.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("unknown arcade board", why);

  auto bad_version = v1(0, 7);
  TEST_ASSERT_FALSE(parse(bad_version.data(), bad_version.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("made by a different version of mkarcade", why);

  auto bad_len = v1(0, 1, 1234);
  TEST_ASSERT_FALSE(parse(bad_len.data(), bad_len.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("wrong amount of ROM in it", why);

  auto too_much_rom = v2(0, 0);
  too_much_rom[10] = 9;
  TEST_ASSERT_FALSE(parse(too_much_rom.data(), too_much_rom.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("too much program ROM", why);

  auto too_many_fixups = v2(0, 0);
  too_many_fixups[11] = 5;
  TEST_ASSERT_FALSE(parse(too_many_fixups.data(), too_many_fixups.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("too many vector fixups", why);

  auto truncated = v2(2, 0);
  truncated.resize(truncated.size() - 1);
  TEST_ASSERT_FALSE(parse(truncated.data(), truncated.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("too short", why);

  TEST_ASSERT_FALSE(parse(nullptr, kV1FileBytes, &info, &why));
  TEST_ASSERT_EQUAL_STRING("no data", why);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_regions_follow_each_other);
  RUN_TEST(test_upper_program_rom_pushes_the_later_regions_along);
  RUN_TEST(test_the_monitor_is_sideways_unless_the_file_says_otherwise);
  RUN_TEST(test_the_kit_puts_a_second_program_before_the_tiles);
  RUN_TEST(test_vector_fixups_are_read_back);
  RUN_TEST(test_the_header_alone_says_how_long_the_file_is);
  RUN_TEST(test_malformed_files_are_refused_with_a_reason);
  return UNITY_END();
}
