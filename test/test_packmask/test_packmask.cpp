// Which packs a game draws from. The invariant that matters: a selection can
// never leave a game with nothing to play.

#include <unity.h>

#include "packpicker.h"

using namespace tabulous;

void test_normal_selection_passes_through() {
  const uint32_t mask = 0b00001010;  // packs 1 and 3
  TEST_ASSERT_FALSE(packpicker::enabled(mask, 0, 8));
  TEST_ASSERT_TRUE(packpicker::enabled(mask, 1, 8));
  TEST_ASSERT_FALSE(packpicker::enabled(mask, 2, 8));
  TEST_ASSERT_TRUE(packpicker::enabled(mask, 3, 8));
}

void test_empty_selection_means_all() {
  // Turning every pack off would otherwise leave the category screen blank
  // with no way to start a round or get back out.
  TEST_ASSERT_EQUAL_UINT32(0xFFu, packpicker::sanitise(0, 8));
  for (size_t i = 0; i < 8; i++) {
    TEST_ASSERT_TRUE(packpicker::enabled(0, i, 8));
  }
}

void test_bits_for_packs_that_do_not_exist_are_ignored() {
  // A stored mask from a time when more packs were installed must not enable
  // indices past the end of the current list.
  const uint32_t stale = 0xFFFFFFFF;
  TEST_ASSERT_EQUAL_UINT32(0b111u, packpicker::sanitise(stale, 3));
  TEST_ASSERT_TRUE(packpicker::enabled(stale, 2, 3));

  // And a mask selecting ONLY packs that no longer exist is empty in effect,
  // so it must fall back to all rather than to nothing.
  const uint32_t only_gone = 0b11110000;
  TEST_ASSERT_EQUAL_UINT32(0b1111u, packpicker::sanitise(only_gone, 4));
}

void test_default_mask_enables_everything() {
  const uint32_t all_on = 0xFFFFFFFF;
  for (size_t i = 0; i < 8; i++) {
    TEST_ASSERT_TRUE(packpicker::enabled(all_on, i, 8));
  }
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_normal_selection_passes_through);
  RUN_TEST(test_empty_selection_means_all);
  RUN_TEST(test_bits_for_packs_that_do_not_exist_are_ignored);
  RUN_TEST(test_default_mask_enables_everything);
  return UNITY_END();
}
