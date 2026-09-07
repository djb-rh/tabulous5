// Host tests for the on-screen controller's hit geometry.
// Run with: .venv/bin/pio test -e native
//
// Diagonals are the reason this is tested rather than eyeballed: four separate
// arrow rectangles cannot produce up+right, and a game that needs it would
// just feel broken with no obvious cause.

#include <unity.h>

#include "joypad.h"

using namespace tabulous::joypad;

namespace {

// Centre of a D-pad cell, so a test says which direction it means rather than
// carrying magic coordinates that silently drift if the layout moves.
void cellCentre(int col, int row, int *x, int *y) {
  const Rect r = dpadCell(col, row);
  *x = r.x + r.w / 2;
  *y = r.y + r.h / 2;
}

uint8_t atCell(int col, int row) {
  int x, y;
  cellCentre(col, row, &x, &y);
  return hitTest(x, y);
}

}  // namespace

void test_cardinal_directions() {
  TEST_ASSERT_EQUAL_UINT8(kUp, atCell(1, 0));
  TEST_ASSERT_EQUAL_UINT8(kDown, atCell(1, 2));
  TEST_ASSERT_EQUAL_UINT8(kLeft, atCell(0, 1));
  TEST_ASSERT_EQUAL_UINT8(kRight, atCell(2, 1));
}

void test_corners_give_diagonals() {
  TEST_ASSERT_EQUAL_UINT8(kUp | kLeft, atCell(0, 0));
  TEST_ASSERT_EQUAL_UINT8(kUp | kRight, atCell(2, 0));
  TEST_ASSERT_EQUAL_UINT8(kDown | kLeft, atCell(0, 2));
  TEST_ASSERT_EQUAL_UINT8(kDown | kRight, atCell(2, 2));
}

void test_centre_is_neutral() {
  TEST_ASSERT_EQUAL_UINT8(0, atCell(1, 1));
}

void test_opposite_directions_are_never_both_set() {
  // Up+Down or Left+Right is impossible on a physical pad, and some games
  // behave badly if a core is handed it.
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      const uint8_t s = atCell(col, row);
      TEST_ASSERT_NOT_EQUAL(kUp | kDown, s & (kUp | kDown));
      TEST_ASSERT_NOT_EQUAL(kLeft | kRight, s & (kLeft | kRight));
    }
  }
}

void test_face_buttons() {
  const Circle a = buttonA(), b = buttonB();
  TEST_ASSERT_EQUAL_UINT8(kA, hitTest(a.cx, a.cy));
  TEST_ASSERT_EQUAL_UINT8(kB, hitTest(b.cx, b.cy));

  const Rect sel = buttonSelect(), start = buttonStart();
  TEST_ASSERT_EQUAL_UINT8(kSelect, hitTest(sel.x + sel.w / 2, sel.y + sel.h / 2));
  TEST_ASSERT_EQUAL_UINT8(kStart, hitTest(start.x + start.w / 2, start.y + start.h / 2));
}

void test_face_buttons_are_round_not_square() {
  // The corner of A's bounding box must miss, or the gap between A and B
  // becomes a place where both fire.
  const Circle a = buttonA();
  TEST_ASSERT_EQUAL_UINT8(0, hitTest(a.cx - a.r + 2, a.cy - a.r + 2));
}

void test_controls_do_not_overlap_the_picture() {
  // Anything drawn over the video would be covered by the next frame, and the
  // touch would look dead for no visible reason.
  const int vx0 = kVideoX, vx1 = kVideoX + kVideoW;
  const int vy0 = kVideoY, vy1 = kVideoY + kVideoH;

  const Rect dp = dpadArea();
  TEST_ASSERT_TRUE(dp.x + dp.w <= vx0);

  const Circle a = buttonA(), b = buttonB();
  TEST_ASSERT_TRUE(a.cx - a.r >= vx1);
  TEST_ASSERT_TRUE(b.cx - b.r >= vx1);
  TEST_ASSERT_TRUE(buttonSelect().x >= vx1);
  TEST_ASSERT_TRUE(buttonStart().x >= vx1);

  const Rect m = menuButton();
  TEST_ASSERT_TRUE(m.x + m.w <= vx0 || m.y + m.h <= vy0);
  TEST_ASSERT_TRUE(vy0 >= 0 && vy1 <= kPanelH);
}

void test_everything_is_on_screen() {
  const Rect dp = dpadArea();
  TEST_ASSERT_TRUE(dp.x >= 0 && dp.y >= 0);
  TEST_ASSERT_TRUE(dp.x + dp.w <= kPanelW && dp.y + dp.h <= kPanelH);

  const Circle a = buttonA();
  TEST_ASSERT_TRUE(a.cx + a.r <= kPanelW && a.cy + a.r <= kPanelH);
  TEST_ASSERT_TRUE(buttonStart().x + buttonStart().w <= kPanelW);
  TEST_ASSERT_TRUE(buttonStart().y + buttonStart().h <= kPanelH);
}

void test_touch_targets_meet_the_minimum() {
  // 80 px is this project's established floor for a finger; anything smaller
  // was found unusable when Minesweeper shipped 41 px cells.
  TEST_ASSERT_TRUE(kDpadCell >= 80);
  TEST_ASSERT_TRUE(buttonA().r * 2 >= 80);
  TEST_ASSERT_TRUE(buttonB().r * 2 >= 80);
  TEST_ASSERT_TRUE(buttonSelect().h >= 60);  // pressed rarely, so a little smaller
  TEST_ASSERT_TRUE(menuButton().h >= 60);
}

void test_video_stride_avoids_the_cache_cliff() {
  // A row stride that is a multiple of 512 bytes collides in cache and cost
  // 190 ms a frame instead of 20 in the display benchmark.
  TEST_ASSERT_NOT_EQUAL(0, (kVideoStride * 2) % 512);
  TEST_ASSERT_TRUE(kVideoStride >= kVideoW);
}

void test_misses_return_nothing() {
  TEST_ASSERT_EQUAL_UINT8(0, hitTest(kVideoX + 10, kVideoY + 10));
  TEST_ASSERT_EQUAL_UINT8(0, hitTest(0, kPanelH - 1));
  TEST_ASSERT_EQUAL_UINT8(0, hitTest(kPanelW - 1, 0));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("UP", name(kUp));
  TEST_ASSERT_EQUAL_STRING("SELECT", name(kSelect));
  TEST_ASSERT_EQUAL_STRING("", name(kUp | kRight));  // not a single button
  TEST_ASSERT_EQUAL_STRING("", name(0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_cardinal_directions);
  RUN_TEST(test_corners_give_diagonals);
  RUN_TEST(test_centre_is_neutral);
  RUN_TEST(test_opposite_directions_are_never_both_set);
  RUN_TEST(test_face_buttons);
  RUN_TEST(test_face_buttons_are_round_not_square);
  RUN_TEST(test_controls_do_not_overlap_the_picture);
  RUN_TEST(test_everything_is_on_screen);
  RUN_TEST(test_touch_targets_meet_the_minimum);
  RUN_TEST(test_video_stride_avoids_the_cache_cliff);
  RUN_TEST(test_misses_return_nothing);
  RUN_TEST(test_names);
  return UNITY_END();
}
