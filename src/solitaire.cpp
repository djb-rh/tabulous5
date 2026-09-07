#include "solitaire.h"

#include <algorithm>

namespace tabulous {
namespace solitaire {
namespace {

constexpr size_t kMaxHistory = 60;  // bounded: each entry is the whole board

uint32_t xorshift(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

}  // namespace

void Game::deal(uint32_t seed, bool draw_three) {
  draw_three_ = draw_three;
  passes_ = 0;
  history_.clear();
  for (auto &p : state_.piles) p.clear();
  state_.moves = 0;

  std::vector<Card> deck;
  deck.reserve(52);
  for (uint8_t s = 0; s < 4; s++) {
    for (uint8_t r = 1; r <= 13; r++) deck.push_back({r, s, false});
  }

  uint32_t rng = seed ? seed : 1;  // xorshift is stuck at zero
  for (size_t i = deck.size(); i > 1; i--) {
    std::swap(deck[i - 1], deck[xorshift(rng) % i]);
  }

  // Columns of 1..7, only the last of each face up.
  size_t at = 0;
  for (int col = 0; col < 7; col++) {
    for (int n = 0; n <= col; n++) {
      Card c = deck[at++];
      c.face_up = (n == col);
      state_.piles[kTableau0 + col].push_back(c);
    }
  }
  while (at < deck.size()) {
    Card c = deck[at++];
    c.face_up = false;
    state_.piles[kStock].push_back(c);
  }
}

void Game::push() {
  history_.push_back(state_);
  if (history_.size() > kMaxHistory) history_.erase(history_.begin());
}

bool Game::undo() {
  if (history_.empty()) return false;
  state_ = history_.back();
  history_.pop_back();
  return true;
}

bool Game::drawStock() {
  auto &stock = state_.piles[kStock];
  auto &waste = state_.piles[kWaste];

  if (stock.empty()) {
    if (waste.empty()) return false;
    if (!canRecycle()) return false;
    push();
    // Recycle: the waste goes back in the order it came out, so the same
    // sequence repeats — that is what makes draw-three a puzzle rather than
    // an unlimited search.
    while (!waste.empty()) {
      Card c = waste.back();
      waste.pop_back();
      c.face_up = false;
      stock.push_back(c);
    }
    passes_++;
    state_.moves++;
    return true;
  }

  push();
  const int n = draw_three_ ? 3 : 1;
  for (int i = 0; i < n && !stock.empty(); i++) {
    Card c = stock.back();
    stock.pop_back();
    c.face_up = true;
    waste.push_back(c);
  }
  state_.moves++;
  return true;
}

bool Game::canPlaceOnFoundation(int pile, const Card &c) const {
  if (!isFoundation(pile)) return false;
  const auto &f = state_.piles[pile];
  if (f.empty()) return c.rank == 1;
  return f.back().suit == c.suit && f.back().rank + 1 == c.rank;
}

int Game::foundationFor(const Card &c) const {
  for (int i = 0; i < 4; i++) {
    if (canPlaceOnFoundation(kFoundation0 + i, c)) return kFoundation0 + i;
  }
  return -1;
}

bool Game::canRecycle() const {
  if (max_passes_ <= 0) return true;
  return passes_ < max_passes_;
}

// A run is movable only if it is already a valid descending, alternating
// sequence — you cannot drag an arbitrary handful of cards.
bool Game::validRun(int pile, int index) const {
  const auto &p = state_.piles[pile];
  if (index < 0 || (size_t)index >= p.size()) return false;
  if (!p[index].face_up) return false;
  for (size_t i = index + 1; i < p.size(); i++) {
    const Card &prev = p[i - 1];
    const Card &cur = p[i];
    if (!cur.face_up) return false;
    if (prev.rank != cur.rank + 1) return false;
    if (prev.red() == cur.red()) return false;
  }
  return true;
}

bool Game::isSelectable(int pile, int index) const {
  if (pile == kStock) return false;
  const auto &p = state_.piles[pile];
  if (index < 0 || (size_t)index >= p.size()) return false;
  if (!p[index].face_up) return false;
  // Waste and foundations only ever move their top card.
  if (pile == kWaste || isFoundation(pile)) return (size_t)index == p.size() - 1;
  return validRun(pile, index);
}

bool Game::canMove(int from, int from_index, int to) const {
  if (from == to || from == kStock || to == kStock || to == kWaste) return false;
  if (!isSelectable(from, from_index)) return false;

  const auto &src = state_.piles[from];
  const Card &moving = src[from_index];
  const size_t run = src.size() - from_index;

  if (isFoundation(to)) {
    // Foundations take one card at a time, in suit order, and any empty one
    // will start with an ace.
    if (run != 1) return false;
    return canPlaceOnFoundation(to, moving);
  }

  if (!isTableau(to)) return false;
  const auto &dst = state_.piles[to];
  if (dst.empty()) return moving.rank == 13;  // only a king starts a column
  const Card &onto = dst.back();
  if (!onto.face_up) return false;
  return onto.rank == moving.rank + 1 && onto.red() != moving.red();
}

// Uncovering a face-down card turns it over. It is part of the move, not a
// separate action — nobody chooses not to flip it.
void Game::flipExposed(int pile) {
  if (!isTableau(pile)) return;
  auto &p = state_.piles[pile];
  if (!p.empty() && !p.back().face_up) p.back().face_up = true;
}

bool Game::move(int from, int from_index, int to) {
  if (!canMove(from, from_index, to)) return false;
  push();

  auto &src = state_.piles[from];
  auto &dst = state_.piles[to];
  dst.insert(dst.end(), src.begin() + from_index, src.end());
  src.erase(src.begin() + from_index, src.end());

  flipExposed(from);
  state_.moves++;
  return true;
}

int Game::autoPlay() {
  int moved = 0;
  bool again = true;
  while (again) {
    again = false;
    // Waste first, then each tableau top: this is the order a player would
    // do it by hand, and it keeps the waste from stalling.
    for (int pile = kWaste; pile <= kTableau0 + 6; pile++) {
      if (pile == kWaste || isTableau(pile)) {
        const auto &p = state_.piles[pile];
        if (p.empty() || !p.back().face_up) continue;
        const int f = foundationFor(p.back());
        if (f < 0) continue;
        if (move(pile, (int)p.size() - 1, f)) {
          moved++;
          again = true;
        }
      }
    }
  }
  return moved;
}

bool Game::won() const {
  size_t total = 0;
  for (int i = 0; i < 4; i++) total += state_.piles[kFoundation0 + i].size();
  return total == 52;
}

}  // namespace solitaire
}  // namespace tabulous
