// High-score table ranking. Times are lower-is-better.

#include <unity.h>

#include <cstdio>

#include "highscores.h"

using namespace tabulous::highscores;

namespace {
Table withTimes(const uint32_t *secs, int n) {
  Table t;
  char name[8];
  for (int i = 0; i < n; i++) {
    snprintf(name, sizeof(name), "P%d", i);
    t.insert(name, secs[i], true);
  }
  return t;
}
}  // namespace

void test_empty_table_accepts_anything() {
  Table t;
  TEST_ASSERT_TRUE(t.qualifies(9999, true));
  TEST_ASSERT_EQUAL_INT(0, t.insert("ADA", 120, true));
  TEST_ASSERT_EQUAL_UINT8(1, t.count);
  TEST_ASSERT_EQUAL_STRING("ADA", t.entries[0].name);
}

void test_faster_times_sort_first() {
  const uint32_t secs[] = {300, 100, 200};
  Table t = withTimes(secs, 3);
  TEST_ASSERT_EQUAL_UINT32(100, t.entries[0].value);
  TEST_ASSERT_EQUAL_UINT32(200, t.entries[1].value);
  TEST_ASSERT_EQUAL_UINT32(300, t.entries[2].value);
}

void test_table_holds_only_five_and_drops_the_worst() {
  const uint32_t secs[] = {50, 60, 70, 80, 90};
  Table t = withTimes(secs, 5);
  TEST_ASSERT_EQUAL_UINT8(kMax, t.count);

  // A better time displaces the slowest, and the table stays at five.
  TEST_ASSERT_EQUAL_INT(0, t.insert("FAST", 10, true));
  TEST_ASSERT_EQUAL_UINT8(kMax, t.count);
  TEST_ASSERT_EQUAL_UINT32(10, t.entries[0].value);
  TEST_ASSERT_EQUAL_UINT32(80, t.entries[4].value);

  // A slower time than every entry does not qualify at all.
  TEST_ASSERT_FALSE(t.qualifies(999, true));
  TEST_ASSERT_EQUAL_INT(-1, t.insert("SLOW", 999, true));
  TEST_ASSERT_EQUAL_UINT32(80, t.entries[4].value);
}

void test_a_tie_does_not_displace_the_existing_holder() {
  const uint32_t secs[] = {50, 60, 70, 80, 90};
  Table t = withTimes(secs, 5);
  // Matching 50 should not push the original 50 down a place.
  TEST_ASSERT_EQUAL_INT(-1, t.insert("TIE", 90, true));
  TEST_ASSERT_EQUAL_STRING("P0", t.entries[0].name);
}

void test_higher_is_better_ranks_the_other_way() {
  Table t;
  t.insert("A", 10, false);
  t.insert("B", 30, false);
  t.insert("C", 20, false);
  TEST_ASSERT_EQUAL_UINT32(30, t.entries[0].value);
  TEST_ASSERT_EQUAL_UINT32(20, t.entries[1].value);
  TEST_ASSERT_EQUAL_UINT32(10, t.entries[2].value);
  TEST_ASSERT_TRUE(t.qualifies(25, false));
  // With only three of five slots used there is room, so even a poor score
  // qualifies. Being full is what makes a score fail to place.
  TEST_ASSERT_TRUE(t.qualifies(5, false));
  t.insert("D", 5, false);
  t.insert("E", 6, false);
  TEST_ASSERT_EQUAL_UINT8(kMax, t.count);
  TEST_ASSERT_FALSE(t.qualifies(1, false));
  TEST_ASSERT_TRUE(t.qualifies(100, false));
}

void test_long_names_are_truncated_not_overflowed() {
  Table t;
  t.insert("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 42, true);
  TEST_ASSERT_EQUAL_size_t(kNameLen, strlen(t.entries[0].name));
  TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOP", t.entries[0].name);
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_table_accepts_anything);
  RUN_TEST(test_faster_times_sort_first);
  RUN_TEST(test_table_holds_only_five_and_drops_the_worst);
  RUN_TEST(test_a_tie_does_not_displace_the_existing_holder);
  RUN_TEST(test_higher_is_better_ranks_the_other_way);
  RUN_TEST(test_long_names_are_truncated_not_overflowed);
  return UNITY_END();
}
