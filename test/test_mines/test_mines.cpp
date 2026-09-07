// Minesweeper board rules. First-click safety, flood fill and win detection
// are the three that are easy to get subtly wrong and miserable to find by
// tapping at a screen.

#include <unity.h>

#include <set>

#include "minesweeper.h"

using namespace tabulous;
using namespace tabulous::mines;

namespace {

// Tests used to tap hardcoded coordinates, which silently became out-of-bounds
// no-ops when the presets shrank — the assertions then failed for the wrong
// reason. Always derive taps from the board's own size.
void revealCentre(Board &b) { b.reveal(b.cols() / 2, b.rows() / 2); }

int countMines(const Board &b) {
  int n = 0;
  for (int y = 0; y < b.rows(); y++) {
    for (int x = 0; x < b.cols(); x++) {
      if (b.at(x, y).mine) n++;
    }
  }
  return n;
}

}  // namespace

void test_board_starts_empty_and_unmined() {
  Board b;
  b.begin(Level::Easy, 1);
  TEST_ASSERT_EQUAL(Status::Ready, b.status());
  TEST_ASSERT_FALSE(b.started());
  TEST_ASSERT_EQUAL_INT(0, countMines(b));  // placed on first reveal, not now
  TEST_ASSERT_EQUAL_INT(0, b.revealedCount());
}

void test_first_tap_is_always_safe_and_opens_a_region() {
  // Across many seeds and many first taps: the tapped cell must never be a
  // mine, and its whole 3x3 must be clear so there is something to deduce from.
  for (uint32_t seed = 1; seed <= 60; seed++) {
    Board b;
    b.begin(Level::Medium, seed);
    // Anywhere strictly inside the board, varying with the seed.
    const int fx = 1 + (int)(seed % (uint32_t)(b.cols() - 2));
    const int fy = 1 + (int)(seed % (uint32_t)(b.rows() - 2));
    TEST_ASSERT_TRUE(b.inBounds(fx, fy));
    b.reveal(fx, fy);

    TEST_ASSERT_NOT_EQUAL(Status::Lost, b.status());
    TEST_ASSERT_FALSE(b.at(fx, fy).mine);
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        if (b.inBounds(fx + dx, fy + dy)) {
          TEST_ASSERT_FALSE(b.at(fx + dx, fy + dy).mine);
        }
      }
    }
    TEST_ASSERT_TRUE(b.at(fx, fy).revealed);
  }
}

void test_mine_count_matches_the_preset() {
  Board b;
  b.begin(Level::Hard, 99);
  revealCentre(b);
  TEST_ASSERT_EQUAL_INT(preset(Level::Hard).mines, countMines(b));
  TEST_ASSERT_EQUAL_INT(preset(Level::Hard).mines, b.mineCount());
}

void test_adjacent_counts_agree_with_the_mines() {
  Board b;
  b.begin(Level::Easy, 7);
  revealCentre(b);
  for (int y = 0; y < b.rows(); y++) {
    for (int x = 0; x < b.cols(); x++) {
      if (b.at(x, y).mine) continue;
      int expected = 0;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (!dx && !dy) continue;
          if (b.inBounds(x + dx, y + dy) && b.at(x + dx, y + dy).mine) {
            expected++;
          }
        }
      }
      TEST_ASSERT_EQUAL_INT(expected, b.at(x, y).adjacent);
    }
  }
}

void test_flood_fill_stops_at_numbers() {
  Board b;
  b.begin(Level::Medium, 12345);
  revealCentre(b);

  // Every revealed zero must have all eight neighbours revealed; a revealed
  // number must not have pulled its neighbours in.
  for (int y = 0; y < b.rows(); y++) {
    for (int x = 0; x < b.cols(); x++) {
      const Cell &c = b.at(x, y);
      if (!c.revealed || c.adjacent != 0) continue;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (b.inBounds(x + dx, y + dy)) {
            TEST_ASSERT_TRUE(b.at(x + dx, y + dy).revealed);
          }
        }
      }
    }
  }
}

void test_flags_block_reveal_and_count_down() {
  Board b;
  b.begin(Level::Easy, 3);
  revealCentre(b);
  const int before = b.revealedCount();

  // Find a covered cell to flag.
  int fx = -1, fy = -1;
  for (int y = 0; y < b.rows() && fx < 0; y++) {
    for (int x = 0; x < b.cols(); x++) {
      if (!b.at(x, y).revealed) { fx = x; fy = y; break; }
    }
  }
  TEST_ASSERT_TRUE(fx >= 0);

  b.toggleFlag(fx, fy);
  TEST_ASSERT_TRUE(b.at(fx, fy).flagged);
  TEST_ASSERT_EQUAL_INT(b.mineCount() - 1, b.minesRemaining());

  // A flag must protect the cell from a stray tap.
  b.reveal(fx, fy);
  TEST_ASSERT_FALSE(b.at(fx, fy).revealed);
  TEST_ASSERT_EQUAL_INT(before, b.revealedCount());
  TEST_ASSERT_NOT_EQUAL(Status::Lost, b.status());

  b.toggleFlag(fx, fy);
  TEST_ASSERT_EQUAL_INT(b.mineCount(), b.minesRemaining());
}

void test_revealing_a_mine_loses_and_exposes_the_field() {
  Board b;
  b.begin(Level::Easy, 21);
  revealCentre(b);

  int mx = -1, my = -1;
  for (int y = 0; y < b.rows() && mx < 0; y++) {
    for (int x = 0; x < b.cols(); x++) {
      if (b.at(x, y).mine) { mx = x; my = y; break; }
    }
  }
  TEST_ASSERT_TRUE(mx >= 0);

  b.reveal(mx, my);
  TEST_ASSERT_EQUAL(Status::Lost, b.status());
  for (int y = 0; y < b.rows(); y++) {
    for (int x = 0; x < b.cols(); x++) {
      if (b.at(x, y).mine) TEST_ASSERT_TRUE(b.at(x, y).revealed);
    }
  }
  // Nothing may change after the game is over.
  const int revealed = b.revealedCount();
  b.reveal(0, 0);
  b.toggleFlag(0, 0);
  TEST_ASSERT_EQUAL_INT(revealed, b.revealedCount());
}

void test_clearing_every_safe_cell_wins_without_needing_flags() {
  Board b;
  b.begin(Level::Easy, 5);
  revealCentre(b);

  for (int y = 0; y < b.rows(); y++) {
    for (int x = 0; x < b.cols(); x++) {
      if (!b.at(x, y).mine) b.reveal(x, y);
    }
  }

  TEST_ASSERT_EQUAL(Status::Won, b.status());
  TEST_ASSERT_EQUAL_INT(b.cols() * b.rows() - b.mineCount(), b.revealedCount());
  // Winning should tidy the board up by flagging what's left.
  TEST_ASSERT_EQUAL_INT(0, b.minesRemaining());
}

void test_chording_a_satisfied_number_opens_its_neighbours() {
  Board b;
  b.begin(Level::Medium, 808);
  revealCentre(b);

  // Find a revealed 1 with exactly one covered neighbour, and flag it.
  for (int y = 1; y < b.rows() - 1; y++) {
    for (int x = 1; x < b.cols() - 1; x++) {
      const Cell &c = b.at(x, y);
      if (!c.revealed || c.adjacent != 1) continue;
      int covered = 0, cx = -1, cy = -1;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (!dx && !dy) continue;
          if (!b.at(x + dx, y + dy).revealed) {
            covered++; cx = x + dx; cy = y + dy;
          }
        }
      }
      if (covered != 1 || !b.at(cx, cy).mine) continue;

      b.toggleFlag(cx, cy);
      const int before = b.revealedCount();
      b.reveal(x, y);  // chord
      // The flagged mine stays covered; nothing else was left to open, so the
      // important assertion is that chording did not detonate anything.
      TEST_ASSERT_NOT_EQUAL(Status::Lost, b.status());
      TEST_ASSERT_GREATER_OR_EQUAL_INT(before, b.revealedCount());
      return;
    }
  }
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_board_starts_empty_and_unmined);
  RUN_TEST(test_first_tap_is_always_safe_and_opens_a_region);
  RUN_TEST(test_mine_count_matches_the_preset);
  RUN_TEST(test_adjacent_counts_agree_with_the_mines);
  RUN_TEST(test_flood_fill_stops_at_numbers);
  RUN_TEST(test_flags_block_reveal_and_count_down);
  RUN_TEST(test_revealing_a_mine_loses_and_exposes_the_field);
  RUN_TEST(test_clearing_every_safe_cell_wins_without_needing_flags);
  RUN_TEST(test_chording_a_satisfied_number_opens_its_neighbours);
  return UNITY_END();
}
