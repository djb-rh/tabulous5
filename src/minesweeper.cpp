#include "minesweeper.h"

#include <algorithm>

namespace tabulous {
namespace mines {

uint32_t Board::rand32() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

void Board::begin(Level level, uint32_t seed) {
  const Preset p = preset(level);
  level_ = level;
  cols_ = p.cols;
  rows_ = p.rows;
  mines_ = p.mines;
  rng_ = seed ? seed : 1;  // xorshift is stuck at zero

  cells_.assign((size_t)cols_ * rows_, Cell{});
  flags_ = 0;
  revealed_ = 0;
  started_ = false;
  status_ = Status::Ready;
}

void Board::placeMines(int safe_x, int safe_y) {
  // The whole 3x3 around the first tap is kept clear, not just the tapped
  // cell: opening onto a zero gives a region to reason from, where a lone safe
  // cell surrounded by mines leaves nothing but guesses.
  std::vector<size_t> candidates;
  candidates.reserve(cells_.size());
  for (int y = 0; y < rows_; y++) {
    for (int x = 0; x < cols_; x++) {
      if (std::abs(x - safe_x) <= 1 && std::abs(y - safe_y) <= 1) continue;
      candidates.push_back(index(x, y));
    }
  }

  // Never ask for more mines than there are cells to hold them.
  const int wanted = std::min<int>(mines_, (int)candidates.size());
  mines_ = wanted;

  // Partial Fisher-Yates: shuffle only as far as we need to draw.
  for (int i = 0; i < wanted; i++) {
    const size_t j = i + (rand32() % (candidates.size() - i));
    std::swap(candidates[i], candidates[j]);
    cells_[candidates[i]].mine = true;
  }

  for (int y = 0; y < rows_; y++) {
    for (int x = 0; x < cols_; x++) {
      if (cells_[index(x, y)].mine) continue;
      uint8_t n = 0;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (!dx && !dy) continue;
          if (inBounds(x + dx, y + dy) && cells_[index(x + dx, y + dy)].mine) {
            n++;
          }
        }
      }
      cells_[index(x, y)].adjacent = n;
    }
  }

  started_ = true;
}

void Board::floodFrom(int x, int y) {
  // Iterative rather than recursive: a zero region on the Hard board can span
  // hundreds of cells, and this runs on a 512 KB-stack MCU task.
  std::vector<std::pair<int, int>> stack;
  stack.push_back({x, y});

  while (!stack.empty()) {
    const auto [cx, cy] = stack.back();
    stack.pop_back();
    if (!inBounds(cx, cy)) continue;

    Cell &c = cells_[index(cx, cy)];
    if (c.revealed || c.flagged) continue;

    c.revealed = true;
    revealed_++;
    if (c.adjacent != 0) continue;  // numbers stop the flood

    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dy) continue;
        stack.push_back({cx + dx, cy + dy});
      }
    }
  }
}

int Board::adjacentFlags(int x, int y) const {
  int n = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      if (inBounds(x + dx, y + dy) && at(x + dx, y + dy).flagged) n++;
    }
  }
  return n;
}

void Board::revealAllMines() {
  for (Cell &c : cells_) {
    if (c.mine) c.revealed = true;
  }
}

void Board::checkWin() {
  // Won when every cell that isn't a mine has been uncovered. Flags are
  // irrelevant — a board can be won without placing any.
  if (revealed_ == (int)cells_.size() - mines_) {
    status_ = Status::Won;
    for (Cell &c : cells_) {
      if (c.mine) c.flagged = true;
    }
    flags_ = mines_;
  }
}

void Board::reveal(int x, int y) {
  if (!inBounds(x, y)) return;
  if (status_ == Status::Won || status_ == Status::Lost) return;

  if (!started_) {
    placeMines(x, y);
    status_ = Status::Playing;
  }

  Cell &c = cells_[index(x, y)];

  if (c.revealed) {
    // Chording: a satisfied number uncovers its neighbours.
    if (c.adjacent == 0 || adjacentFlags(x, y) != c.adjacent) return;
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dy) continue;
        const int nx = x + dx, ny = y + dy;
        if (!inBounds(nx, ny)) continue;
        const Cell &n = at(nx, ny);
        if (n.revealed || n.flagged) continue;
        if (n.mine) {
          // Chording on wrongly-placed flags loses, exactly as clicking the
          // cell would have.
          cells_[index(nx, ny)].revealed = true;
          revealAllMines();
          status_ = Status::Lost;
          return;
        }
        floodFrom(nx, ny);
      }
    }
    checkWin();
    return;
  }

  if (c.flagged) return;  // a flag protects the cell from a stray tap

  if (c.mine) {
    c.revealed = true;
    revealAllMines();
    status_ = Status::Lost;
    return;
  }

  floodFrom(x, y);
  checkWin();
}

void Board::toggleFlag(int x, int y) {
  if (!inBounds(x, y)) return;
  if (status_ == Status::Won || status_ == Status::Lost) return;
  Cell &c = cells_[index(x, y)];
  if (c.revealed) return;
  c.flagged = !c.flagged;
  flags_ += c.flagged ? 1 : -1;
}

}  // namespace mines
}  // namespace tabulous
