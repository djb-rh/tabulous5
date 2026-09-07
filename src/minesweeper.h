// Minesweeper rules.
//
// No Arduino headers, so the board logic unit-tests on the host — which
// matters here because first-click safety, flood fill and win detection are
// all easy to get subtly wrong and tedious to find by tapping.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tabulous {
namespace mines {

enum class Status : uint8_t { Ready, Playing, Won, Lost };

enum class Level : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

struct Preset {
  uint8_t cols;
  uint8_t rows;
  uint16_t mines;
  const char *name;
};

// Deliberately smaller boards than the classic 9x9 / 16x16 / 30x16.
//
// The Tab5's panel is ~294 PPI, so a cell has to be roughly 80-100 px just to
// be 7-9 mm — a fingertip. The old 30x16 board gave 34 px cells, about 3 mm,
// which is stylus territory. Difficulty therefore scales mine DENSITY more
// than board size: Easy 13%, Medium 18%, Hard 22%.
inline Preset preset(Level l) {
  switch (l) {
    case Level::Easy: return {9, 5, 6, "Easy"};      // ~108 px cells
    case Level::Hard: return {13, 7, 20, "Hard"};    // ~77 px cells
    default: return {11, 6, 12, "Medium"};           // ~90 px cells
  }
}

struct Cell {
  bool mine = false;
  bool revealed = false;
  bool flagged = false;
  uint8_t adjacent = 0;
};

class Board {
 public:
  // Lays out an empty board. Mines are NOT placed yet: they are placed on the
  // first reveal so the first tap can never lose, which is the difference
  // between a game of deduction and a coin flip.
  void begin(Level level, uint32_t seed);

  Level level() const { return level_; }
  int cols() const { return cols_; }
  int rows() const { return rows_; }
  int mineCount() const { return mines_; }
  Status status() const { return status_; }

  const Cell &at(int x, int y) const { return cells_[index(x, y)]; }
  bool inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < cols_ && y < rows_;
  }

  // Mines minus flags placed. May go negative if the player over-flags.
  int minesRemaining() const { return mines_ - flags_; }
  int revealedCount() const { return revealed_; }

  // Uncovers a cell. On a revealed number with exactly the right number of
  // adjacent flags, uncovers its neighbours instead ("chording") — players
  // expect it, and without it the endgame is a lot of tedious tapping.
  void reveal(int x, int y);
  void toggleFlag(int x, int y);

  // True once mines exist, i.e. after the first reveal.
  bool started() const { return started_; }

 private:
  std::vector<Cell> cells_;
  int cols_ = 0, rows_ = 0, mines_ = 0, flags_ = 0, revealed_ = 0;
  Level level_ = Level::Easy;
  Status status_ = Status::Ready;
  bool started_ = false;
  uint32_t rng_ = 1;

  size_t index(int x, int y) const { return (size_t)y * cols_ + x; }
  uint32_t rand32();
  void placeMines(int safe_x, int safe_y);
  void floodFrom(int x, int y);
  void revealAllMines();
  void checkWin();
  int adjacentFlags(int x, int y) const;
};

}  // namespace mines
}  // namespace tabulous
