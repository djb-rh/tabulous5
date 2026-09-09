// Host tests for iNES header parsing.
// Run with: .venv/bin/pio test -e native

#include <unity.h>

#include <cstring>
#include <vector>

#include "nesrom.h"

using namespace tabulous::nesrom;

namespace {

std::vector<uint8_t> makeRom(uint8_t prg = 2, uint8_t chr = 1,
                             uint8_t mapper = 0, bool trainer = false,
                             int truncate_by = 0) {
  std::vector<uint8_t> v(16, 0);
  memcpy(v.data(), "NES\x1A", 4);
  v[4] = prg;
  v[5] = chr;
  v[6] = (uint8_t)(((mapper & 0x0F) << 4) | (trainer ? 0x04 : 0));
  v[7] = (uint8_t)(mapper & 0xF0);
  const size_t body = (trainer ? 512 : 0) + (size_t)prg * 16384 + (size_t)chr * 8192;
  v.resize(16 + body, 0xAA);
  if (truncate_by > 0 && (int)v.size() > truncate_by) v.resize(v.size() - truncate_by);
  return v;
}

}  // namespace

void test_parses_nrom() {
  const auto r = makeRom();
  Info info;
  const char *why = nullptr;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info, &why));
  TEST_ASSERT_EQUAL_UINT8(0, info.mapper);
  TEST_ASSERT_EQUAL_UINT8(2, info.prg_banks);
  TEST_ASSERT_EQUAL_UINT8(1, info.chr_banks);
  TEST_ASSERT_TRUE(info.supported);
  TEST_ASSERT_EQUAL_size_t(r.size(), info.expected_bytes);
}

void test_mapper_number_spans_two_bytes() {
  // Mapper 4 is in the low nibble; mapper 64 needs the high byte. Reading only
  // byte 6 would call a mapper-64 ROM "mapper 0" and play noise.
  Info info;
  const auto mmc3 = makeRom(2, 1, 4);
  TEST_ASSERT_TRUE(parseHeader(mmc3.data(), mmc3.size(), &info));
  TEST_ASSERT_EQUAL_UINT8(4, info.mapper);
  TEST_ASSERT_TRUE(info.supported);

  const auto high = makeRom(2, 1, 64);
  TEST_ASSERT_TRUE(parseHeader(high.data(), high.size(), &info));
  TEST_ASSERT_EQUAL_UINT8(64, info.mapper);
  TEST_ASSERT_FALSE(info.supported);
}

// The vendored core decides this, not us: Cartridge::createMapper in
// third_party/anemoia/core/cartridge.cpp dispatches 0, 1, 2, 3, 4 and 69, and
// its default arm sets is_valid = false. The list below was written against
// agnes, which handled only 0, 1, 2 and 4 -- if the core is swapped again,
// this test is the thing that has to move first.
void test_supported_set_matches_core() {
  for (uint8_t m : {0, 1, 2, 3, 4, 69}) TEST_ASSERT_TRUE(mapperSupported(m));
  for (uint8_t m : {5, 7, 9, 64, 68, 70, 255}) TEST_ASSERT_FALSE(mapperSupported(m));
}

void test_trainer_shifts_the_expected_length() {
  const auto r = makeRom(1, 1, 0, /*trainer=*/true);
  Info info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info));
  TEST_ASSERT_TRUE(info.has_trainer);
  TEST_ASSERT_EQUAL_size_t(16 + 512 + 16384 + 8192, info.expected_bytes);
}

void test_rejects_truncated_file() {
  const auto r = makeRom(2, 1, 0, false, /*truncate_by=*/4096);
  Info info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseHeader(r.data(), r.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("file is truncated", why);
}

void test_rejects_non_ines() {
  auto r = makeRom();
  r[0] = 'X';
  Info info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseHeader(r.data(), r.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("not an iNES file", why);

  TEST_ASSERT_FALSE(parseHeader(r.data(), 8, &info, &why));
  TEST_ASSERT_EQUAL_STRING("too short", why);
  TEST_ASSERT_FALSE(parseHeader(nullptr, 100, &info, &why));
}

void test_rejects_zero_prg() {
  const auto r = makeRom(0, 1);
  Info info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseHeader(r.data(), r.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("no PRG ROM", why);
}

void test_chr_ram_is_legal() {
  // chr_banks == 0 means the cartridge has CHR RAM, not that it is broken.
  const auto r = makeRom(2, 0);
  Info info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), &info));
  TEST_ASSERT_EQUAL_UINT8(0, info.chr_banks);
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("NROM", mapperName(0));
  TEST_ASSERT_EQUAL_STRING("MMC3", mapperName(4));
  TEST_ASSERT_EQUAL_STRING("FME-7", mapperName(69));
  TEST_ASSERT_EQUAL_STRING("unsupported", mapperName(200));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_nrom);
  RUN_TEST(test_mapper_number_spans_two_bytes);
  RUN_TEST(test_supported_set_matches_core);
  RUN_TEST(test_trainer_shifts_the_expected_length);
  RUN_TEST(test_rejects_truncated_file);
  RUN_TEST(test_rejects_non_ines);
  RUN_TEST(test_rejects_zero_prg);
  RUN_TEST(test_chr_ram_is_legal);
  RUN_TEST(test_names);
  return UNITY_END();
}
