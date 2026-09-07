// Sudoku: generator, solver and board state.
//
// Puzzles are generated on the device rather than shipped, so difficulty is a
// real property of the puzzle and you never run out. That makes the generator
// the part most worth testing: a puzzle with no solution, or with more than
// one, is unsolvable by deduction and indistinguishable from a hard one until
// someone has wasted twenty minutes on it.
//
// No Arduino headers — this unit-tests on the host.
#pragma once

#include <cstdint>
#include <vector>

namespace tabulous {
namespace sudoku {

constexpr int kN = 9;
constexpr int kCells = kN * kN;

enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2, Expert = 3 };

const char *difficultyName(Difficulty d);

// Target number of starting clues. Fewer clues generally means more work, and
// uniqueness is enforced regardless, so a puzzle is always solvable by logic.
int targetGivens(Difficulty d);

// Counts solutions, stopping once `limit` have been found. Exposed because
// "exactly one solution" is the property the generator exists to guarantee,
// and it deserves testing directly.
int countSolutions(const uint8_t cells[kCells], int limit);

class Puzzle {
 public:
  void generate(Difficulty difficulty, uint32_t seed);

  Difficulty difficulty() const { return difficulty_; }
  int givenCount() const { return givens_; }

  // 1..9, or 0 for an empty square.
  uint8_t value(int x, int y) const { return cells_[idx(x, y)]; }
  uint8_t solution(int x, int y) const { return solution_[idx(x, y)]; }
  bool isGiven(int x, int y) const { return given_[idx(x, y)]; }

  // Ignored on a given: the starting clues are not the player's to change.
  void set(int x, int y, uint8_t value);
  void clear(int x, int y);

  // Pencil marks. Setting a value clears that square's marks.
  bool note(int x, int y, uint8_t n) const;
  void toggleNote(int x, int y, uint8_t n);
  bool hasNotes(int x, int y) const { return notes_[idx(x, y)] != 0; }

  // True if this square's entry repeats in its row, column or box. Reported
  // against what is actually on the board, not against the solution, so a
  // legal-but-wrong entry is not given away as a mistake.
  bool conflicts(int x, int y) const;

  // True when every square is filled and nothing conflicts.
  bool solved() const;

  // How many of 1..9 are still unplaced, for greying out a full number.
  int remaining(uint8_t n) const;

  static int idx(int x, int y) { return y * kN + x; }

 private:
  uint8_t cells_[kCells] = {};
  uint8_t solution_[kCells] = {};
  bool given_[kCells] = {};
  uint16_t notes_[kCells] = {};  // bit n-1 set = pencil mark n
  Difficulty difficulty_ = Difficulty::Easy;
  int givens_ = 0;
  uint32_t rng_ = 1;

  uint32_t rand32();
  bool fill(int pos);
  void dig(int target_givens);
};

}  // namespace sudoku
}  // namespace tabulous
