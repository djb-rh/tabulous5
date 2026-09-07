// Sudoku generator and board rules.
//
// The generator carries the real risk: a puzzle with no solution, or with more
// than one, cannot be solved by deduction and is indistinguishable from a hard
// one until someone has wasted twenty minutes on it.

#include <unity.h>

#include <cstring>
#include <set>

#include "sudoku.h"

using namespace tabulous;
using namespace tabulous::sudoku;

namespace {

bool rowsColsBoxesAreComplete(const Puzzle &p, bool use_solution) {
  for (int i = 0; i < kN; i++) {
    std::set<int> row, col;
    for (int j = 0; j < kN; j++) {
      row.insert(use_solution ? p.solution(j, i) : p.value(j, i));
      col.insert(use_solution ? p.solution(i, j) : p.value(i, j));
    }
    if (row.size() != 9 || col.size() != 9) return false;
    if (row.count(0) || col.count(0)) return false;
  }
  for (int by = 0; by < 3; by++) {
    for (int bx = 0; bx < 3; bx++) {
      std::set<int> box;
      for (int dy = 0; dy < 3; dy++) {
        for (int dx = 0; dx < 3; dx++) {
          box.insert(use_solution ? p.solution(bx * 3 + dx, by * 3 + dy)
                                  : p.value(bx * 3 + dx, by * 3 + dy));
        }
      }
      if (box.size() != 9 || box.count(0)) return false;
    }
  }
  return true;
}

void snapshot(const Puzzle &p, uint8_t out[kCells]) {
  for (int y = 0; y < kN; y++) {
    for (int x = 0; x < kN; x++) out[Puzzle::idx(x, y)] = p.value(x, y);
  }
}

}  // namespace

void test_generated_solution_is_a_complete_valid_grid() {
  for (uint32_t seed = 1; seed <= 8; seed++) {
    Puzzle p;
    p.generate(Difficulty::Medium, seed);
    TEST_ASSERT_TRUE(rowsColsBoxesAreComplete(p, true));
  }
}

void test_every_puzzle_has_exactly_one_solution() {
  // The property the generator exists to guarantee.
  for (uint32_t seed = 1; seed <= 6; seed++) {
    Puzzle p;
    p.generate(Difficulty::Hard, seed);
    uint8_t board[kCells];
    snapshot(p, board);
    TEST_ASSERT_EQUAL_INT(1, countSolutions(board, 5));
  }
}

void test_givens_are_consistent_with_the_solution() {
  Puzzle p;
  p.generate(Difficulty::Medium, 42);
  for (int y = 0; y < kN; y++) {
    for (int x = 0; x < kN; x++) {
      if (!p.isGiven(x, y)) continue;
      TEST_ASSERT_EQUAL_UINT8(p.solution(x, y), p.value(x, y));
    }
  }
}

void test_harder_settings_give_fewer_clues() {
  Puzzle easy, expert;
  easy.generate(Difficulty::Easy, 11);
  expert.generate(Difficulty::Expert, 11);
  TEST_ASSERT_GREATER_THAN_INT(expert.givenCount(), easy.givenCount());
  // Uniqueness can block digging, so the target is a floor, not a promise.
  TEST_ASSERT_GREATER_OR_EQUAL_INT(targetGivens(Difficulty::Easy),
                                   easy.givenCount());
  TEST_ASSERT_LESS_THAN_INT(kCells, easy.givenCount());
}

void test_givens_cannot_be_changed_by_the_player() {
  Puzzle p;
  p.generate(Difficulty::Easy, 3);
  int gx = -1, gy = -1;
  for (int y = 0; y < kN && gx < 0; y++) {
    for (int x = 0; x < kN; x++) {
      if (p.isGiven(x, y)) { gx = x; gy = y; break; }
    }
  }
  TEST_ASSERT_TRUE(gx >= 0);

  const uint8_t original = p.value(gx, gy);
  p.set(gx, gy, (uint8_t)(original == 9 ? 1 : original + 1));
  TEST_ASSERT_EQUAL_UINT8(original, p.value(gx, gy));
  p.clear(gx, gy);
  TEST_ASSERT_EQUAL_UINT8(original, p.value(gx, gy));
}

void test_conflicts_are_reported_against_the_board_not_the_solution() {
  Puzzle p;
  p.generate(Difficulty::Easy, 9);

  // Find an empty square and the value of another given in the same row.
  int ex = -1, ey = -1;
  for (int y = 0; y < kN && ex < 0; y++) {
    for (int x = 0; x < kN; x++) {
      if (p.value(x, y) == 0) { ex = x; ey = y; break; }
    }
  }
  TEST_ASSERT_TRUE(ex >= 0);

  uint8_t dup = 0;
  for (int x = 0; x < kN; x++) {
    if (x != ex && p.value(x, ey) != 0) { dup = p.value(x, ey); break; }
  }
  TEST_ASSERT_TRUE(dup != 0);

  p.set(ex, ey, dup);
  TEST_ASSERT_TRUE(p.conflicts(ex, ey));

  // A legal-but-wrong entry must NOT be flagged: reporting it would hand the
  // player the answer.
  uint8_t legal_wrong = 0;
  for (uint8_t v = 1; v <= 9; v++) {
    if (v == p.solution(ex, ey)) continue;
    p.set(ex, ey, v);
    if (!p.conflicts(ex, ey)) { legal_wrong = v; break; }
  }
  if (legal_wrong) {
    p.set(ex, ey, legal_wrong);
    TEST_ASSERT_FALSE(p.conflicts(ex, ey));
    TEST_ASSERT_NOT_EQUAL(p.solution(ex, ey), p.value(ex, ey));
  }
}

void test_filling_in_the_solution_counts_as_solved() {
  Puzzle p;
  p.generate(Difficulty::Medium, 77);
  TEST_ASSERT_FALSE(p.solved());
  for (int y = 0; y < kN; y++) {
    for (int x = 0; x < kN; x++) {
      if (!p.isGiven(x, y)) p.set(x, y, p.solution(x, y));
    }
  }
  TEST_ASSERT_TRUE(p.solved());
  TEST_ASSERT_TRUE(rowsColsBoxesAreComplete(p, false));
}

void test_notes_behave_like_pencil_marks() {
  Puzzle p;
  p.generate(Difficulty::Easy, 5);
  int ex = -1, ey = -1;
  for (int y = 0; y < kN && ex < 0; y++) {
    for (int x = 0; x < kN; x++) {
      if (p.value(x, y) == 0) { ex = x; ey = y; break; }
    }
  }

  p.toggleNote(ex, ey, 4);
  p.toggleNote(ex, ey, 7);
  TEST_ASSERT_TRUE(p.note(ex, ey, 4));
  TEST_ASSERT_TRUE(p.note(ex, ey, 7));
  TEST_ASSERT_FALSE(p.note(ex, ey, 5));

  p.toggleNote(ex, ey, 4);
  TEST_ASSERT_FALSE(p.note(ex, ey, 4));

  // Writing a value supersedes the marks under it.
  p.set(ex, ey, 3);
  TEST_ASSERT_FALSE(p.hasNotes(ex, ey));
  // And marks can't be added on top of a value.
  p.toggleNote(ex, ey, 2);
  TEST_ASSERT_FALSE(p.note(ex, ey, 2));
}

void test_remaining_counts_down_as_a_digit_is_placed() {
  Puzzle p;
  p.generate(Difficulty::Medium, 31);
  const int before = p.remaining(5);
  int ex = -1, ey = -1;
  for (int y = 0; y < kN && ex < 0; y++) {
    for (int x = 0; x < kN; x++) {
      if (p.value(x, y) == 0) { ex = x; ey = y; break; }
    }
  }
  p.set(ex, ey, 5);
  TEST_ASSERT_EQUAL_INT(before - 1, p.remaining(5));
}

void test_different_seeds_give_different_puzzles() {
  Puzzle a, b;
  a.generate(Difficulty::Medium, 1);
  b.generate(Difficulty::Medium, 2);
  bool differs = false;
  for (int y = 0; y < kN && !differs; y++) {
    for (int x = 0; x < kN; x++) {
      if (a.solution(x, y) != b.solution(x, y)) { differs = true; break; }
    }
  }
  TEST_ASSERT_TRUE(differs);
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_generated_solution_is_a_complete_valid_grid);
  RUN_TEST(test_every_puzzle_has_exactly_one_solution);
  RUN_TEST(test_givens_are_consistent_with_the_solution);
  RUN_TEST(test_harder_settings_give_fewer_clues);
  RUN_TEST(test_givens_cannot_be_changed_by_the_player);
  RUN_TEST(test_conflicts_are_reported_against_the_board_not_the_solution);
  RUN_TEST(test_filling_in_the_solution_counts_as_solved);
  RUN_TEST(test_notes_behave_like_pencil_marks);
  RUN_TEST(test_remaining_counts_down_as_a_digit_is_placed);
  RUN_TEST(test_different_seeds_give_different_puzzles);
  return UNITY_END();
}
