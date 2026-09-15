// tictactoe.h -- portable tic-tac-toe engine with a perfect-play (minimax) AI.
//
// Copied verbatim from ~/Developer/Joshua (components/joshua/tictactoe.h),
// the ESPHome build of the same game for the CrowPanel. Keep the two in step
// if either changes; only the screens differ between the projects.
//
// Plain C++11, no dependencies on LVGL, Arduino, ESP-IDF or ESPHome, so it
// can be dropped into any ESP32 (or desktop) project as-is.
//
//   joshua::TicTacToe g;
//   g.reset();                      // X always moves first
//   g.play(4);                      // X takes the centre
//   int m = g.best_move();          // ask the AI where O should go
//   g.play(m);
//   if (g.is_over()) { ... g.winner() ... g.is_draw() ... }
//
#pragma once
#include <stdint.h>

namespace joshua {

enum class Mark : uint8_t { None = 0, X = 1, O = 2 };

inline Mark other(Mark m) { return m == Mark::X ? Mark::O : (m == Mark::O ? Mark::X : Mark::None); }
inline char mark_char(Mark m) { return m == Mark::X ? 'X' : (m == Mark::O ? 'O' : ' '); }

class TicTacToe {
 public:
  static constexpr int kCells = 9;
  // Random source used to break ties between equally good moves. Must return
  // a value in [0, n). If none is supplied a small xorshift generator is used.
  using Rng = uint32_t (*)(uint32_t n);

  TicTacToe() { reset(); }

  void reset() {
    for (int i = 0; i < kCells; i++) cells_[i] = Mark::None;
    turn_ = Mark::X;
    moves_ = 0;
  }

  Mark at(int i) const { return (i >= 0 && i < kCells) ? cells_[i] : Mark::None; }
  Mark turn() const { return turn_; }
  int move_count() const { return moves_; }
  bool is_full() const { return moves_ >= kCells; }
  Mark winner() const { return winner_of(cells_); }
  bool is_draw() const { return is_full() && winner() == Mark::None; }
  bool is_over() const { return winner() != Mark::None || is_full(); }

  // Play the current player's mark into cell i (0..8, row-major).
  // Returns false if the cell is occupied, out of range, or the game is over.
  bool play(int i) {
    if (i < 0 || i >= kCells || cells_[i] != Mark::None || is_over()) return false;
    cells_[i] = turn_;
    turn_ = other(turn_);
    moves_++;
    return true;
  }

  // Fills out[3] with the cell indices of the winning line, if any.
  bool winning_line(int out[3]) const {
    for (int l = 0; l < 8; l++) {
      Mark a = cells_[kLines[l][0]];
      if (a != Mark::None && a == cells_[kLines[l][1]] && a == cells_[kLines[l][2]]) {
        out[0] = kLines[l][0];
        out[1] = kLines[l][1];
        out[2] = kLines[l][2];
        return true;
      }
    }
    return false;
  }

  // Best cell for the player to move, by full minimax (perfect play).
  // Ties are broken randomly so games don't all look identical. An empty
  // board is answered with a random cell: with perfect play every opening
  // is a draw, so there is nothing to search for. Returns -1 if no move.
  int best_move(Rng rng = nullptr) const {
    if (is_over()) return -1;
    if (rng == nullptr) rng = default_rng;
    if (moves_ == 0) return (int) rng(kCells);

    Mark board[kCells];
    for (int i = 0; i < kCells; i++) board[i] = cells_[i];

    int best_score = -1000;
    int best[kCells];
    int nbest = 0;
    for (int i = 0; i < kCells; i++) {
      if (board[i] != Mark::None) continue;
      board[i] = turn_;
      int s = -negamax(board, other(turn_), 1);
      board[i] = Mark::None;
      if (s > best_score) {
        best_score = s;
        nbest = 0;
      }
      if (s == best_score) best[nbest++] = i;
    }
    return nbest ? best[rng((uint32_t) nbest)] : -1;
  }

 private:
  static constexpr uint8_t kLines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                                           {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

  static Mark winner_of(const Mark *b) {
    for (int l = 0; l < 8; l++) {
      Mark a = b[kLines[l][0]];
      if (a != Mark::None && a == b[kLines[l][1]] && a == b[kLines[l][2]]) return a;
    }
    return Mark::None;
  }

  // Score of the position for the player about to move ("me"):
  // +10-depth for a forced win, depth-10 for a forced loss, 0 for a draw.
  static int negamax(Mark *b, Mark me, int depth) {
    Mark w = winner_of(b);
    if (w != Mark::None) return (w == me) ? (10 - depth) : (depth - 10);
    int best = -1000;
    bool any = false;
    for (int i = 0; i < kCells; i++) {
      if (b[i] != Mark::None) continue;
      any = true;
      b[i] = me;
      int s = -negamax(b, other(me), depth + 1);
      b[i] = Mark::None;
      if (s > best) best = s;
    }
    return any ? best : 0;  // no empty cells and no winner: draw
  }

  static uint32_t default_rng(uint32_t n) {
    static uint32_t s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return n ? (s % n) : 0;
  }

  Mark cells_[kCells];
  Mark turn_;
  int moves_;
};

}  // namespace joshua
