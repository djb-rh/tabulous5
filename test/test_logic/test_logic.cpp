// Host-side unit tests for the parts of PhraseCraze that don't need hardware.
// Run with: .venv/bin/pio test -e native

#include <unity.h>

#include <set>
#include <string>

#include "pack.h"
#include "round_timer.h"

using namespace tabulous;

// ------------------------------------------------------------------ parsing

void test_parse_difficulty() {
  bool ok = false;
  TEST_ASSERT_EQUAL(Difficulty::Easy, parseDifficulty("easy", &ok));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(Difficulty::Hard, parseDifficulty("  HARD  ", &ok));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(Difficulty::Medium, parseDifficulty("", &ok));
  TEST_ASSERT_TRUE(ok);

  // Unknown values fall back to Medium but report failure, so tools/packs.py
  // can flag them while the device still plays the phrase.
  TEST_ASSERT_EQUAL(Difficulty::Medium, parseDifficulty("spicy", &ok));
  TEST_ASSERT_FALSE(ok);
}

void test_parse_hex_color() {
  bool ok = false;
  TEST_ASSERT_EQUAL_UINT32(0xE4572E, parseHexColor("#E4572E", &ok));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(0xabcdef, parseHexColor("abcdef", &ok));
  TEST_ASSERT_TRUE(ok);

  parseHexColor("#FFF", &ok);
  TEST_ASSERT_FALSE(ok);
  parseHexColor("#GGGGGG", &ok);
  TEST_ASSERT_FALSE(ok);
}

void test_pack_parse_basics() {
  const std::string src =
      "# name: Movies & TV\n"
      "# color: #E4572E\n"
      "# icon: film\n"
      "\n"
      "# a bare comment with no colon is ignored\n"
      "Jurassic Park\n"
      "The Great British Bake Off | easy\n"
      "Eternal Sunshine of the Spotless Mind | hard\n";

  Pack pack;
  pack.parse(src, "fallback");

  TEST_ASSERT_EQUAL_STRING("Movies & TV", pack.meta().name.c_str());
  TEST_ASSERT_EQUAL_STRING("film", pack.meta().icon.c_str());
  TEST_ASSERT_EQUAL_UINT32(0xE4572E, pack.meta().color);

  TEST_ASSERT_EQUAL_size_t(3, pack.size());
  TEST_ASSERT_EQUAL_STRING("Jurassic Park", pack.phrases()[0].text.c_str());
  TEST_ASSERT_EQUAL(Difficulty::Medium, pack.phrases()[0].difficulty);
  TEST_ASSERT_EQUAL_STRING("The Great British Bake Off",
                           pack.phrases()[1].text.c_str());
  TEST_ASSERT_EQUAL(Difficulty::Easy, pack.phrases()[1].difficulty);
  TEST_ASSERT_EQUAL(Difficulty::Hard, pack.phrases()[2].difficulty);
}

void test_pack_parse_crlf_and_whitespace() {
  // A pack edited on Windows, or pasted into the web editor, arrives like this.
  const std::string src =
      "# name:  Spaced Out  \r\n"
      "  Padded Phrase  \r\n"
      "Tabbed\t|\teasy\r\n"
      "\r\n"
      "Last One";

  Pack pack;
  pack.parse(src);

  TEST_ASSERT_EQUAL_STRING("Spaced Out", pack.meta().name.c_str());
  TEST_ASSERT_EQUAL_size_t(3, pack.size());
  TEST_ASSERT_EQUAL_STRING("Padded Phrase", pack.phrases()[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING("Tabbed", pack.phrases()[1].text.c_str());
  TEST_ASSERT_EQUAL(Difficulty::Easy, pack.phrases()[1].difficulty);
  // No trailing newline on the last line must not drop it.
  TEST_ASSERT_EQUAL_STRING("Last One", pack.phrases()[2].text.c_str());
}

void test_pack_parse_degenerate() {
  Pack pack;

  pack.parse("", "Fallback Name");
  TEST_ASSERT_TRUE(pack.empty());
  TEST_ASSERT_EQUAL_STRING("Fallback Name", pack.meta().name.c_str());

  // A bar with no phrase in front of it is skipped, not stored as empty.
  pack.parse("| easy\nReal Phrase\n");
  TEST_ASSERT_EQUAL_size_t(1, pack.size());
  TEST_ASSERT_EQUAL_STRING("Real Phrase", pack.phrases()[0].text.c_str());

  // A bad color leaves the default rather than turning the category black.
  pack.parse("# color: nonsense\nX\n");
  TEST_ASSERT_EQUAL_UINT32(PackMeta{}.color, pack.meta().color);
}

void test_pack_difficulty_filter() {
  Pack pack;
  pack.parse("A | easy\nB | medium\nC | hard\nD | easy\n");

  TEST_ASSERT_EQUAL_size_t(2, pack.indicesUpTo(Difficulty::Easy).size());
  TEST_ASSERT_EQUAL_size_t(3, pack.indicesUpTo(Difficulty::Medium).size());
  TEST_ASSERT_EQUAL_size_t(4, pack.indicesUpTo(Difficulty::Hard).size());
}

// --------------------------------------------------------------- shuffle bag

void test_bag_covers_every_index_once_per_cycle() {
  constexpr size_t N = 25;
  ShuffleBag bag;
  bag.reset(N, 12345);

  std::set<size_t> seen;
  for (size_t i = 0; i < N; i++) seen.insert(bag.next());

  // This is the property that makes the game feel fair: a full cycle hands out
  // every phrase exactly once.
  TEST_ASSERT_EQUAL_size_t(N, seen.size());
}

void test_bag_wraps_and_reports_it() {
  constexpr size_t N = 8;
  ShuffleBag bag;
  bag.reset(N, 999);

  for (size_t i = 0; i < N; i++) {
    bag.next();
    TEST_ASSERT_FALSE(bag.justWrapped());
  }
  bag.next();
  TEST_ASSERT_TRUE(bag.justWrapped());
}

void test_bag_avoids_repeat_across_cycle_boundary() {
  // The bag is "correct" either way, but drawing the same phrase as the last
  // of one cycle and the first of the next reads to players as a bug.
  for (uint32_t seed = 1; seed <= 200; seed++) {
    constexpr size_t N = 6;
    ShuffleBag bag;
    bag.reset(N, seed);

    size_t last = 0;
    for (size_t i = 0; i < N; i++) last = bag.next();
    const size_t first_of_next = bag.next();

    TEST_ASSERT_NOT_EQUAL(last, first_of_next);
  }
}

void test_bag_is_deterministic_per_seed() {
  ShuffleBag a, b, c;
  a.reset(20, 4242);
  b.reset(20, 4242);
  c.reset(20, 4243);

  bool differs_from_c = false;
  for (int i = 0; i < 20; i++) {
    const size_t va = a.next();
    TEST_ASSERT_EQUAL_size_t(va, b.next());
    if (va != c.next()) differs_from_c = true;
  }
  TEST_ASSERT_TRUE(differs_from_c);
}

void test_bag_edge_cases() {
  ShuffleBag empty;
  empty.reset(0, 7);
  TEST_ASSERT_EQUAL_size_t(0, empty.next());  // must not crash or divide by 0

  ShuffleBag single;
  single.reset(1, 7);
  TEST_ASSERT_EQUAL_size_t(0, single.next());
  TEST_ASSERT_EQUAL_size_t(0, single.next());  // one item can only repeat
}

// -------------------------------------------------------------- round timer

void test_beep_interval_endpoints_and_monotonicity() {
  TimerConfig cfg;
  TEST_ASSERT_EQUAL_UINT32(cfg.start_interval_ms, beepIntervalMs(cfg, 0.f));
  TEST_ASSERT_EQUAL_UINT32(cfg.end_interval_ms, beepIntervalMs(cfg, 1.f));

  // Must never speed up and then slow down — the acceleration is the tension.
  uint32_t previous = beepIntervalMs(cfg, 0.f);
  for (int i = 1; i <= 100; i++) {
    const uint32_t current = beepIntervalMs(cfg, i / 100.f);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(previous, current);
    previous = current;
  }

  // Out-of-range progress clamps rather than exploding.
  TEST_ASSERT_EQUAL_UINT32(cfg.start_interval_ms, beepIntervalMs(cfg, -5.f));
  TEST_ASSERT_EQUAL_UINT32(cfg.end_interval_ms, beepIntervalMs(cfg, 5.f));
}

void test_beep_frequency_jumps_late() {
  TimerConfig cfg;
  TEST_ASSERT_EQUAL_UINT16(cfg.beep_hz, beepFrequency(cfg, 0.f));
  TEST_ASSERT_EQUAL_UINT16(cfg.beep_hz, beepFrequency(cfg, cfg.urgent_from - 0.01f));
  TEST_ASSERT_EQUAL_UINT16(cfg.urgent_hz, beepFrequency(cfg, cfg.urgent_from));
  TEST_ASSERT_EQUAL_UINT16(cfg.urgent_hz, beepFrequency(cfg, 1.f));
}

void test_round_duration_within_range() {
  TimerConfig cfg;
  for (uint32_t r = 0; r < 5000; r += 37) {
    const uint32_t d = pickRoundDuration(cfg, r);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(cfg.min_ms, d);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(cfg.max_ms, d);
  }

  TimerConfig fixed;
  fixed.min_ms = fixed.max_ms = 60000;
  TEST_ASSERT_EQUAL_UINT32(60000, pickRoundDuration(fixed, 12345));
}

void test_round_timer_beeps_accelerate_and_expire() {
  TimerConfig cfg;
  cfg.min_ms = cfg.max_ms = 60000;

  RoundTimer timer;
  timer.start(cfg, 0, 0);
  TEST_ASSERT_TRUE(timer.running());
  TEST_ASSERT_EQUAL_UINT32(60000, timer.durationMs());

  int beeps = 0;
  uint32_t last_beep_at = 0;
  uint32_t first_gap = 0, last_gap = 0;

  // Step a whole round in 5 ms ticks, the way loop() will.
  for (uint32_t t = 0; t <= 60000; t += 5) {
    uint16_t hz = 0;
    if (timer.poll(t, &hz)) {
      if (beeps > 0) {
        const uint32_t gap = t - last_beep_at;
        if (beeps == 1) first_gap = gap;
        last_gap = gap;
      }
      last_beep_at = t;
      beeps++;
      TEST_ASSERT_TRUE(hz == cfg.beep_hz || hz == cfg.urgent_hz);
    }
  }

  TEST_ASSERT_GREATER_THAN_INT(20, beeps);
  // The end of the round must be meaningfully faster than the start.
  TEST_ASSERT_LESS_THAN_UINT32(first_gap / 2, last_gap);
  TEST_ASSERT_TRUE(timer.expired(60000));
  TEST_ASSERT_FALSE(timer.expired(59000));
}

void test_round_timer_does_not_burst_after_a_stall() {
  // If the render loop blocks (e.g. a big redraw), we must not dump every
  // missed beep at once when it comes back.
  TimerConfig cfg;
  cfg.min_ms = cfg.max_ms = 60000;

  RoundTimer timer;
  timer.start(cfg, 0, 0);

  uint16_t hz = 0;
  TEST_ASSERT_FALSE(timer.poll(100, &hz));

  int beeps_in_one_tick = 0;
  const uint32_t after_stall = 10000;
  while (timer.poll(after_stall, &hz)) {
    beeps_in_one_tick++;
    if (beeps_in_one_tick > 5) break;
  }
  TEST_ASSERT_EQUAL_INT(1, beeps_in_one_tick);
}

void test_round_timer_stopped_does_not_beep() {
  TimerConfig cfg;
  RoundTimer timer;
  uint16_t hz = 0;
  TEST_ASSERT_FALSE(timer.poll(5000, &hz));  // never started

  timer.start(cfg, 0, 0);
  timer.stop();
  TEST_ASSERT_FALSE(timer.poll(5000, &hz));
  TEST_ASSERT_FALSE(timer.expired(999999));
}

// --------------------------------------------------------------------- main

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_parse_difficulty);
  RUN_TEST(test_parse_hex_color);
  RUN_TEST(test_pack_parse_basics);
  RUN_TEST(test_pack_parse_crlf_and_whitespace);
  RUN_TEST(test_pack_parse_degenerate);
  RUN_TEST(test_pack_difficulty_filter);

  RUN_TEST(test_bag_covers_every_index_once_per_cycle);
  RUN_TEST(test_bag_wraps_and_reports_it);
  RUN_TEST(test_bag_avoids_repeat_across_cycle_boundary);
  RUN_TEST(test_bag_is_deterministic_per_seed);
  RUN_TEST(test_bag_edge_cases);

  RUN_TEST(test_beep_interval_endpoints_and_monotonicity);
  RUN_TEST(test_beep_frequency_jumps_late);
  RUN_TEST(test_round_duration_within_range);
  RUN_TEST(test_round_timer_beeps_accelerate_and_expire);
  RUN_TEST(test_round_timer_does_not_burst_after_a_stall);
  RUN_TEST(test_round_timer_stopped_does_not_beep);

  return UNITY_END();
}
