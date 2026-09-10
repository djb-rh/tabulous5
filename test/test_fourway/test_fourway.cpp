#include <unity.h>

#include "fourway.h"

using tabulous::fourway::Gate;

// The four bits, in joypad.h's order, plus a button that must be left alone.
constexpr uint8_t kUp = 0x10, kDown = 0x20, kLeft = 0x40, kRight = 0x80;
constexpr uint8_t kA = 0x01;

static Gate makeGate() { return Gate(kUp, kDown, kLeft, kRight); }

void setUp() {}
void tearDown() {}

void test_one_direction_passes_straight_through() {
  Gate g = makeGate();
  TEST_ASSERT_EQUAL_HEX8(kLeft, g.filter(kLeft));
  TEST_ASSERT_EQUAL_HEX8(kLeft, g.filter(kLeft));
  TEST_ASSERT_EQUAL_HEX8(0, g.filter(0));
}

void test_buttons_are_none_of_its_business() {
  Gate g = makeGate();
  TEST_ASSERT_EQUAL_HEX8(kA | kUp, g.filter(kA | kUp));
  TEST_ASSERT_EQUAL_HEX8(kA, g.filter(kA));
}

void test_adding_a_direction_is_a_turn() {
  Gate g = makeGate();
  // Running left, then up is pressed without letting go of left. The turn
  // has to happen on that frame; this is the whole point.
  TEST_ASSERT_EQUAL_HEX8(kLeft, g.filter(kLeft));
  TEST_ASSERT_EQUAL_HEX8(kUp, g.filter(kLeft | kUp));
  // And it stays turned while both are still held.
  TEST_ASSERT_EQUAL_HEX8(kUp, g.filter(kLeft | kUp));
  // Letting go of up leaves left, which is still held.
  TEST_ASSERT_EQUAL_HEX8(kLeft, g.filter(kLeft));
}

void test_letting_go_of_the_turn_does_not_resurrect_it() {
  Gate g = makeGate();
  g.filter(kRight);
  TEST_ASSERT_EQUAL_HEX8(kDown, g.filter(kRight | kDown));
  TEST_ASSERT_EQUAL_HEX8(0, g.filter(0));
  TEST_ASSERT_EQUAL_HEX8(kRight, g.filter(kRight));
}

void test_a_diagonal_from_nothing_picks_the_horizontal() {
  Gate g = makeGate();
  // Both in the same frame: nothing distinguishes them, so it has to be a
  // rule rather than a guess, and the same rule every time.
  TEST_ASSERT_EQUAL_HEX8(kRight, g.filter(kRight | kUp));
  TEST_ASSERT_EQUAL_HEX8(kRight, g.filter(kRight | kUp));
}

void test_opposites_cancel() {
  Gate g = makeGate();
  TEST_ASSERT_EQUAL_HEX8(0, g.filter(kLeft | kRight));
  TEST_ASSERT_EQUAL_HEX8(0, g.filter(kUp | kDown));
  // A stick cannot do it; a pad can, and it means nothing either way.
  TEST_ASSERT_EQUAL_HEX8(kA, g.filter(kA | kUp | kDown));
}

void test_a_diagonal_held_while_the_other_axis_is_swapped() {
  Gate g = makeGate();
  g.filter(kLeft);
  TEST_ASSERT_EQUAL_HEX8(kUp, g.filter(kLeft | kUp));
  // Thumb rolls from up to down, left still held. Down is the change.
  TEST_ASSERT_EQUAL_HEX8(kDown, g.filter(kLeft | kDown));
  TEST_ASSERT_EQUAL_HEX8(kDown, g.filter(kLeft | kDown));
}

void test_reset_forgets_the_held_direction() {
  Gate g = makeGate();
  g.filter(kLeft);
  g.reset();
  // Without the reset this would be a turn to up; after it, the pair is a
  // fresh diagonal and takes the horizontal.
  TEST_ASSERT_EQUAL_HEX8(kLeft, g.filter(kLeft | kUp));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_direction_passes_straight_through);
  RUN_TEST(test_buttons_are_none_of_its_business);
  RUN_TEST(test_adding_a_direction_is_a_turn);
  RUN_TEST(test_letting_go_of_the_turn_does_not_resurrect_it);
  RUN_TEST(test_a_diagonal_from_nothing_picks_the_horizontal);
  RUN_TEST(test_opposites_cancel);
  RUN_TEST(test_a_diagonal_held_while_the_other_axis_is_swapped);
  RUN_TEST(test_reset_forgets_the_held_direction);
  return UNITY_END();
}
