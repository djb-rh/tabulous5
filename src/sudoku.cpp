#include "sudoku.h"

#include <algorithm>
#include <cstring>

namespace tabulous {
namespace sudoku {
namespace {

bool legal(const uint8_t c[kCells], int pos, uint8_t v) {
  const int x = pos % kN, y = pos / kN;
  for (int i = 0; i < kN; i++) {
    if (c[y * kN + i] == v && i != x) return false;
    if (c[i * kN + x] == v && i != y) return false;
  }
  const int bx = (x / 3) * 3, by = (y / 3) * 3;
  for (int dy = 0; dy < 3; dy++) {
    for (int dx = 0; dx < 3; dx++) {
      const int p = (by + dy) * kN + bx + dx;
      if (c[p] == v && p != pos) return false;
    }
  }
  return true;
}

// Backtracking count with an early exit. Stopping at `limit` matters: proving
// "more than one solution" only needs two, and the generator asks this once
// per candidate removal.
int solveCount(uint8_t c[kCells], int limit) {
  int pos = -1;
  for (int i = 0; i < kCells; i++) {
    if (c[i] == 0) { pos = i; break; }
  }
  if (pos < 0) return 1;  // complete

  int found = 0;
  for (uint8_t v = 1; v <= 9; v++) {
    if (!legal(c, pos, v)) continue;
    c[pos] = v;
    found += solveCount(c, limit - found);
    c[pos] = 0;
    if (found >= limit) break;
  }
  return found;
}

}  // namespace

const char *difficultyName(Difficulty d) {
  switch (d) {
    case Difficulty::Easy: return "Easy";
    case Difficulty::Medium: return "Medium";
    case Difficulty::Hard: return "Hard";
    default: return "Expert";
  }
}

int targetGivens(Difficulty d) {
  switch (d) {
    case Difficulty::Easy: return 44;
    case Difficulty::Medium: return 36;
    case Difficulty::Hard: return 30;
    default: return 26;
  }
}

int countSolutions(const uint8_t cells[kCells], int limit) {
  uint8_t work[kCells];
  memcpy(work, cells, sizeof(work));
  return solveCount(work, limit);
}

uint32_t Puzzle::rand32() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

bool Puzzle::fill(int pos) {
  if (pos >= kCells) return true;

  // Randomised digit order is what makes each generated grid different; a
  // fixed order would produce the same solved grid every time.
  uint8_t order[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 8; i > 0; i--) {
    const int j = (int)(rand32() % (uint32_t)(i + 1));
    std::swap(order[i], order[j]);
  }

  for (uint8_t k = 0; k < 9; k++) {
    const uint8_t v = order[k];
    if (!legal(solution_, pos, v)) continue;
    solution_[pos] = v;
    if (fill(pos + 1)) return true;
    solution_[pos] = 0;
  }
  return false;
}

void Puzzle::dig(int target) {
  // Remove clues in a random order, keeping a removal only if the puzzle still
  // has exactly one solution. Symmetric digging would look tidier but costs
  // clue count for the same difficulty, so this digs freely.
  int order[kCells];
  for (int i = 0; i < kCells; i++) order[i] = i;
  for (int i = kCells - 1; i > 0; i--) {
    const int j = (int)(rand32() % (uint32_t)(i + 1));
    std::swap(order[i], order[j]);
  }

  int remaining = kCells;
  for (int k = 0; k < kCells && remaining > target; k++) {
    const int pos = order[k];
    const uint8_t saved = cells_[pos];
    if (saved == 0) continue;

    cells_[pos] = 0;
    if (countSolutions(cells_, 2) != 1) {
      cells_[pos] = saved;  // ambiguous without it — put it back
      continue;
    }
    remaining--;
  }
  givens_ = remaining;
}

void Puzzle::generate(Difficulty difficulty, uint32_t seed) {
  difficulty_ = difficulty;
  rng_ = seed ? seed : 1;  // xorshift is stuck at zero

  memset(solution_, 0, sizeof(solution_));
  fill(0);

  memcpy(cells_, solution_, sizeof(cells_));
  dig(targetGivens(difficulty));

  for (int i = 0; i < kCells; i++) {
    given_[i] = cells_[i] != 0;
    notes_[i] = 0;
  }
}

void Puzzle::set(int x, int y, uint8_t value) {
  const int i = idx(x, y);
  if (given_[i] || value > 9) return;
  cells_[i] = value;
  notes_[i] = 0;  // an entry supersedes the pencil marks under it
}

void Puzzle::clear(int x, int y) {
  const int i = idx(x, y);
  if (given_[i]) return;
  cells_[i] = 0;
}

bool Puzzle::note(int x, int y, uint8_t n) const {
  if (n < 1 || n > 9) return false;
  return (notes_[idx(x, y)] & (1u << (n - 1))) != 0;
}

void Puzzle::toggleNote(int x, int y, uint8_t n) {
  const int i = idx(x, y);
  if (given_[i] || n < 1 || n > 9) return;
  if (cells_[i] != 0) return;  // no marks on a square that already has a value
  notes_[i] ^= (uint16_t)(1u << (n - 1));
}

bool Puzzle::conflicts(int x, int y) const {
  const int i = idx(x, y);
  if (cells_[i] == 0) return false;
  return !legal(cells_, i, cells_[i]);
}

bool Puzzle::solved() const {
  for (int i = 0; i < kCells; i++) {
    if (cells_[i] == 0) return false;
    if (!legal(cells_, i, cells_[i])) return false;
  }
  return true;
}

int Puzzle::remaining(uint8_t n) const {
  int placed = 0;
  for (int i = 0; i < kCells; i++) {
    if (cells_[i] == n) placed++;
  }
  return 9 - placed;
}

}  // namespace sudoku
}  // namespace tabulous
