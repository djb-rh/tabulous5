// Host-side tests for the WAV header parser.
// Run with: .venv/bin/pio test -e native
//
// These matter more than their size suggests: /sfx holds files the parser did
// not write, and a bad length here becomes an out-of-bounds read on the device.

#include <unity.h>

#include <cstring>
#include <vector>

#include "wav.h"

using namespace tabulous;

namespace {

void put32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(x & 0xff);
  v.push_back((x >> 8) & 0xff);
  v.push_back((x >> 16) & 0xff);
  v.push_back((x >> 24) & 0xff);
}

void put16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(x & 0xff);
  v.push_back((x >> 8) & 0xff);
}

void putTag(std::vector<uint8_t> &v, const char *tag) {
  for (int i = 0; i < 4; i++) v.push_back((uint8_t)tag[i]);
}

// A well-formed file, with knobs for the things the parser is supposed to
// reject so each test can bend exactly one of them.
std::vector<uint8_t> makeWav(uint16_t format = 1, uint16_t channels = 1,
                             uint32_t rate = 22050, uint16_t bits = 16,
                             size_t data_bytes = 8,
                             bool extra_chunk = false) {
  std::vector<uint8_t> fmt;
  put16(fmt, format);
  put16(fmt, channels);
  put32(fmt, rate);
  put32(fmt, rate * channels * (bits / 8));  // byte rate
  put16(fmt, (uint16_t)(channels * (bits / 8)));
  put16(fmt, bits);

  std::vector<uint8_t> body;
  putTag(body, "WAVE");
  if (extra_chunk) {
    // An odd-sized chunk before fmt, to prove chunk walking and the pad byte.
    putTag(body, "LIST");
    put32(body, 5);
    for (int i = 0; i < 5; i++) body.push_back('x');
    body.push_back(0);  // pad to even
  }
  putTag(body, "fmt ");
  put32(body, (uint32_t)fmt.size());
  body.insert(body.end(), fmt.begin(), fmt.end());
  putTag(body, "data");
  put32(body, (uint32_t)data_bytes);
  for (size_t i = 0; i < data_bytes; i++) body.push_back((uint8_t)i);

  std::vector<uint8_t> out;
  putTag(out, "RIFF");
  put32(out, (uint32_t)body.size());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

}  // namespace

void test_parses_a_good_file() {
  const auto w = makeWav();
  WavInfo info;
  const char *why = nullptr;
  TEST_ASSERT_TRUE(parseWav(w.data(), w.size(), &info, &why));
  TEST_ASSERT_EQUAL_UINT32(22050, info.sample_rate);
  TEST_ASSERT_EQUAL_UINT16(1, info.channels);
  TEST_ASSERT_EQUAL_UINT16(16, info.bits);
  TEST_ASSERT_EQUAL_size_t(8, info.data_bytes);
  // The data must actually be where we were told it is.
  TEST_ASSERT_EQUAL_UINT8(0, w[info.data_offset]);
  TEST_ASSERT_EQUAL_UINT8(7, w[info.data_offset + 7]);
}

void test_skips_unknown_chunks_and_pad_bytes() {
  const auto w = makeWav(1, 1, 22050, 16, 8, /*extra_chunk=*/true);
  WavInfo info;
  TEST_ASSERT_TRUE(parseWav(w.data(), w.size(), &info));
  TEST_ASSERT_EQUAL_UINT32(22050, info.sample_rate);
  TEST_ASSERT_EQUAL_size_t(8, info.data_bytes);
  TEST_ASSERT_EQUAL_UINT8(0, w[info.data_offset]);
}

void test_rejects_non_riff() {
  auto w = makeWav();
  w[0] = 'X';
  WavInfo info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseWav(w.data(), w.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("not RIFF", why);
}

void test_rejects_compressed() {
  const auto w = makeWav(/*format=*/85);  // MP3 in a WAV wrapper
  WavInfo info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseWav(w.data(), w.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("not uncompressed PCM", why);
}

void test_rejects_24_bit() {
  const auto w = makeWav(1, 1, 22050, 24, 9);
  WavInfo info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseWav(w.data(), w.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("not 8 or 16 bit", why);
}

void test_rejects_empty_data() {
  const auto w = makeWav(1, 1, 22050, 16, 0);
  WavInfo info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseWav(w.data(), w.size(), &info, &why));
  TEST_ASSERT_EQUAL_STRING("empty data chunk", why);
}

// The one that actually protects the device: a file whose data chunk claims
// more bytes than the file holds must be clamped, never trusted.
void test_clamps_a_truncated_data_chunk() {
  auto w = makeWav(1, 1, 22050, 16, 64);
  w.resize(w.size() - 40);  // chop the tail off; the header still says 64
  WavInfo info;
  TEST_ASSERT_TRUE(parseWav(w.data(), w.size(), &info));
  TEST_ASSERT_EQUAL_size_t(24, info.data_bytes);
  TEST_ASSERT_TRUE(info.data_offset + info.data_bytes <= w.size());
}

// Same idea one step earlier: a stereo 16-bit file cut mid-frame must round
// down, so data_bytes is always a whole number of frames.
void test_rounds_down_to_whole_frames() {
  auto w = makeWav(1, 2, 22050, 16, 10);  // frame is 4 bytes; 10 is not a multiple
  WavInfo info;
  TEST_ASSERT_TRUE(parseWav(w.data(), w.size(), &info));
  TEST_ASSERT_EQUAL_size_t(8, info.data_bytes);
}

void test_rejects_a_header_only_file() {
  const auto w = makeWav();
  WavInfo info;
  const char *why = nullptr;
  TEST_ASSERT_FALSE(parseWav(w.data(), 12, &info, &why));
  TEST_ASSERT_EQUAL_STRING("no fmt chunk", why);
  TEST_ASSERT_FALSE(parseWav(w.data(), 4, &info, &why));
  TEST_ASSERT_EQUAL_STRING("too short", why);
  TEST_ASSERT_FALSE(parseWav(nullptr, 100, &info, &why));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_good_file);
  RUN_TEST(test_skips_unknown_chunks_and_pad_bytes);
  RUN_TEST(test_rejects_non_riff);
  RUN_TEST(test_rejects_compressed);
  RUN_TEST(test_rejects_24_bit);
  RUN_TEST(test_rejects_empty_data);
  RUN_TEST(test_clamps_a_truncated_data_chunk);
  RUN_TEST(test_rounds_down_to_whole_frames);
  RUN_TEST(test_rejects_a_header_only_file);
  return UNITY_END();
}
