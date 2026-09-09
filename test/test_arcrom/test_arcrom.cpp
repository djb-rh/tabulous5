#include <unity.h>

#include <cstring>
#include <vector>

#include "arcrom.h"

using namespace tabulous::arcrom;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> file(uint8_t system = 0, uint8_t version = 1,
                                 uint32_t payload = kPayloadBytes) {
  std::vector<uint8_t> f(kFileBytes, 0);
  memcpy(f.data(), "TAB5ARC1", 8);
  f[8] = system;
  f[9] = version;
  memcpy(f.data() + 12, &payload, sizeof(payload));
  return f;
}

void test_regions_follow_each_other() {
  auto f = file();
  Info info;
  const char *why = "x";
  TEST_ASSERT_TRUE(parse(f.data(), f.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("", why);
  TEST_ASSERT_EQUAL_size_t(16, info.cpu);
  TEST_ASSERT_EQUAL_size_t(16 + 16384, info.gfx);
  TEST_ASSERT_EQUAL_size_t(16 + 16384 + 8192, info.palette);
  TEST_ASSERT_EQUAL_size_t(info.palette + 32, info.colour);
  TEST_ASSERT_EQUAL_size_t(info.colour + 256, info.sound1);
  TEST_ASSERT_EQUAL_size_t(info.sound1 + 256, info.sound2);
  TEST_ASSERT_EQUAL_size_t(kFileBytes, info.sound2 + 256);
}

void test_malformed_files_are_refused_with_a_reason() {
  Info info;
  const char *why = "";

  std::vector<uint8_t> tiny(32, 0);
  TEST_ASSERT_FALSE(parse(tiny.data(), tiny.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("too short", why);

  auto bad_magic = file();
  bad_magic[0] = 'X';
  TEST_ASSERT_FALSE(parse(bad_magic.data(), bad_magic.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("not an .arc file", why);

  auto bad_system = file(9);
  TEST_ASSERT_FALSE(parse(bad_system.data(), bad_system.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("unknown arcade board", why);

  auto bad_version = file(0, 2);
  TEST_ASSERT_FALSE(parse(bad_version.data(), bad_version.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("made by a different version of mkarcade", why);

  auto bad_len = file(0, 1, 1234);
  TEST_ASSERT_FALSE(parse(bad_len.data(), bad_len.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("wrong amount of ROM in it", why);

  TEST_ASSERT_FALSE(parse(nullptr, kFileBytes, &info, &why));
  TEST_ASSERT_EQUAL_STRING("no data", why);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_regions_follow_each_other);
  RUN_TEST(test_malformed_files_are_refused_with_a_reason);
  return UNITY_END();
}
