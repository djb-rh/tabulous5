#include <unity.h>

#include "joypad.h"
#include "padmap.h"

using namespace tabulous;

void setUp() {}
void tearDown() {}

void test_gp100_default_layout() {
  padmap::Map m;
  TEST_ASSERT_EQUAL_HEX8(joypad::kA, padmap::toNes(1u << 1, 0, 0, m));       // button 2
  TEST_ASSERT_EQUAL_HEX8(joypad::kA, padmap::toNes(1u << 0, 0, 0, m));       // X doubles as A
  TEST_ASSERT_EQUAL_HEX8(joypad::kB, padmap::toNes(1u << 2, 0, 0, m));       // button 3
  TEST_ASSERT_EQUAL_HEX8(joypad::kB, padmap::toNes(1u << 3, 0, 0, m));       // Y doubles as B
  TEST_ASSERT_EQUAL_HEX8(joypad::kSelect, padmap::toNes(1u << 8, 0, 0, m));  // button 9
  TEST_ASSERT_EQUAL_HEX8(joypad::kStart, padmap::toNes(1u << 9, 0, 0, m));   // button 10
  TEST_ASSERT_EQUAL_HEX8(0, padmap::toNes((1u << 4) | (1u << 5), 0, 0, m));  // L and R: nothing
}

void test_axes_become_dpad() {
  padmap::Map m;
  TEST_ASSERT_EQUAL_HEX8(joypad::kUp, padmap::toNes(0, 0, -1, m));
  TEST_ASSERT_EQUAL_HEX8(joypad::kDown | joypad::kRight, padmap::toNes(0, 1, 1, m));
  TEST_ASSERT_EQUAL_HEX8(joypad::kLeft | joypad::kA, padmap::toNes(1u << 1, -1, 0, m));
}

void test_unassigned_and_out_of_range_are_ignored() {
  padmap::Map m;
  m.select = 0;
  m.start = 40;
  TEST_ASSERT_EQUAL_HEX8(0, padmap::toNes(0xFFFFFFFFu & ~0x0Fu, 0, 0, m));
}

void test_new_press_is_the_lowest_fresh_button() {
  TEST_ASSERT_EQUAL(0, padmap::newPress(0, 0));
  TEST_ASSERT_EQUAL(3, padmap::newPress(1u << 2, 0));
  TEST_ASSERT_EQUAL(0, padmap::newPress(1u << 2, 1u << 2));      // still held: not new
  TEST_ASSERT_EQUAL(2, padmap::newPress((1u << 1) | (1u << 9), 1u << 9));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_gp100_default_layout);
  RUN_TEST(test_axes_become_dpad);
  RUN_TEST(test_unassigned_and_out_of_range_are_ignored);
  RUN_TEST(test_new_press_is_the_lowest_fresh_button);
  return UNITY_END();
}
