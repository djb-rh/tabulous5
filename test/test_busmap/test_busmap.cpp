#include <unity.h>

#include "busmap.h"
#include "joypad.h"

using namespace tabulous;

void setUp() {}
void tearDown() {}

void test_straight_wiring_is_the_boards_routing() {
  const busmap::Wiring w = busmap::straight();
  TEST_ASSERT_TRUE(busmap::valid(w));
  TEST_ASSERT_EQUAL(12, busmap::assigned(w));
  // UP is header pin 2 (G16), A is pin 23 (G47), X is pin 26 (G51).
  TEST_ASSERT_EQUAL(2, busmap::kHeaderPin[w.line[busmap::kUp]]);
  TEST_ASSERT_EQUAL(16, busmap::kGpio[w.line[busmap::kUp]]);
  TEST_ASSERT_EQUAL(23, busmap::kHeaderPin[w.line[busmap::kA]]);
  TEST_ASSERT_EQUAL(47, busmap::kGpio[w.line[busmap::kA]]);
  TEST_ASSERT_EQUAL(26, busmap::kHeaderPin[w.line[busmap::kX]]);
  TEST_ASSERT_EQUAL(51, busmap::kGpio[w.line[busmap::kX]]);
}

void test_lines_become_buttons_through_the_wiring() {
  busmap::Wiring w = busmap::none();
  w.line[busmap::kUp] = 7;
  w.line[busmap::kA] = 0;
  TEST_ASSERT_EQUAL_HEX16(1u << busmap::kUp, busmap::toButtons(1u << 7, w));
  TEST_ASSERT_EQUAL_HEX16((1u << busmap::kUp) | (1u << busmap::kA), busmap::toButtons((1u << 7) | 1u, w));
  TEST_ASSERT_EQUAL_HEX16(0, busmap::toButtons(1u << 3, w));   // a line nothing is on
  TEST_ASSERT_EQUAL_HEX16(0, busmap::toButtons(0xFFF, busmap::none()));
}

void test_nes_byte() {
  const auto bit = [](busmap::Button b) { return (uint16_t)(1u << b); };
  TEST_ASSERT_EQUAL_HEX8(joypad::kUp | joypad::kA, busmap::toNes(bit(busmap::kUp) | bit(busmap::kA)));
  TEST_ASSERT_EQUAL_HEX8(joypad::kA, busmap::toNes(bit(busmap::kX)));   // X doubles A
  TEST_ASSERT_EQUAL_HEX8(joypad::kB, busmap::toNes(bit(busmap::kY)));   // Y doubles B
  TEST_ASSERT_EQUAL_HEX8(joypad::kSelect | joypad::kStart, busmap::toNes(bit(busmap::kSelect) | bit(busmap::kStart)));
  TEST_ASSERT_EQUAL_HEX8(0, busmap::toNes(bit(busmap::kL) | bit(busmap::kR)));
}

void test_diamond_as_stick() {
  const auto bit = [](busmap::Button b) { return (uint16_t)(1u << b); };
  TEST_ASSERT_EQUAL_HEX8(joypad::kUp, busmap::toStick(bit(busmap::kX)));
  TEST_ASSERT_EQUAL_HEX8(joypad::kDown, busmap::toStick(bit(busmap::kB)));
  TEST_ASSERT_EQUAL_HEX8(joypad::kLeft, busmap::toStick(bit(busmap::kY)));
  TEST_ASSERT_EQUAL_HEX8(joypad::kRight, busmap::toStick(bit(busmap::kA)));
  TEST_ASSERT_EQUAL_HEX8(0, busmap::toStick(bit(busmap::kUp)));
}

void test_teaching_takes_only_new_unassigned_lines() {
  busmap::Wiring w = busmap::none();
  TEST_ASSERT_EQUAL(-1, busmap::newLine(0, 0, w));
  TEST_ASSERT_EQUAL(5, busmap::newLine(1u << 5, 0, w));
  TEST_ASSERT_EQUAL(-1, busmap::newLine(1u << 5, 1u << 5, w));   // was already down: a short, not a press
  w.line[busmap::kUp] = 5;
  TEST_ASSERT_EQUAL(-1, busmap::newLine(1u << 5, 0, w));         // already taught as UP
  TEST_ASSERT_EQUAL(9, busmap::newLine((1u << 5) | (1u << 9), 0, w));
}

void test_validity() {
  busmap::Wiring w = busmap::none();
  TEST_ASSERT_TRUE(busmap::valid(w));
  TEST_ASSERT_EQUAL(0, busmap::assigned(w));
  w.line[busmap::kUp] = 3;
  w.line[busmap::kDown] = 3;       // two buttons on one line
  TEST_ASSERT_FALSE(busmap::valid(w));
  w.line[busmap::kDown] = 12;      // no such line
  TEST_ASSERT_FALSE(busmap::valid(w));
  w.line[busmap::kDown] = busmap::kNone;   // skipped: fine
  TEST_ASSERT_TRUE(busmap::valid(w));
  TEST_ASSERT_EQUAL(1, busmap::assigned(w));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_straight_wiring_is_the_boards_routing);
  RUN_TEST(test_lines_become_buttons_through_the_wiring);
  RUN_TEST(test_nes_byte);
  RUN_TEST(test_diamond_as_stick);
  RUN_TEST(test_teaching_takes_only_new_unassigned_lines);
  RUN_TEST(test_validity);
  return UNITY_END();
}
