// Klondike solitaire rules.
//
// No Arduino headers, so all of it unit-tests on the host — which matters
// more here than in any other game so far: move legality, run moves,
// recycling and undo are a lot of small rules that are individually easy to
// get subtly wrong and tedious to find by playing.
#pragma once

#include <cstdint>
#include <vector>

namespace tabulous {
namespace solitaire {

// 0 clubs, 1 diamonds, 2 hearts, 3 spades. Diamonds and hearts are red.
inline bool isRed(uint8_t suit) { return suit == 1 || suit == 2; }

struct Card {
  uint8_t rank = 0;  // 1 = ace .. 13 = king
  uint8_t suit = 0;
  bool face_up = false;

  bool red() const { return isRed(suit); }
  bool operator==(const Card &o) const {
    return rank == o.rank && suit == o.suit && face_up == o.face_up;
  }
};

// Piles are addressed by a single index so the UI can talk about "the pile
// under this finger" without a special case per kind.
constexpr int kStock = 0;
constexpr int kWaste = 1;
constexpr int kFoundation0 = 2;  // .. 5
constexpr int kTableau0 = 6;     // .. 12
constexpr int kPileCount = 13;

inline bool isFoundation(int pile) { return pile >= 2 && pile <= 5; }
inline bool isTableau(int pile) { return pile >= 6 && pile <= 12; }

struct State {
  std::vector<Card> piles[kPileCount];
  int moves = 0;
};

class Game {
 public:
  // `draw_three` is the classic harder deal; one card at a time is the
  // forgiving one. Changing it starts a new game.
  void deal(uint32_t seed, bool draw_three);

  // How many times the waste may be turned back over. 0 means unlimited,
  // which is the forgiving default; 3 and 1 are the usual stricter variants.
  void setMaxPasses(int passes) { max_passes_ = passes; }
  int maxPasses() const { return max_passes_; }
  bool canRecycle() const;

  bool drawThree() const { return draw_three_; }
  const std::vector<Card> &pile(int index) const { return state_.piles[index]; }
  int moves() const { return state_.moves; }
  int passes() const { return passes_; }

  // Stock -> waste, or recycle the waste back into the stock when empty.
  // Returns false only when there is nothing to do at all.
  bool drawStock();

  // Can the run starting at `from_index` of `from` legally land on `to`?
  bool canMove(int from, int from_index, int to) const;
  bool move(int from, int from_index, int to);

  // Foundations are NOT fixed to a suit. Any empty one accepts any ace and
  // takes that suit from then on, which is how Klondike is normally played —
  // pinning suits to slots means an ace can be rejected by three of the four
  // spaces for no reason the player can see.
  bool canPlaceOnFoundation(int pile, const Card &c) const;

  // The first foundation that would accept this card, or -1.
  int foundationFor(const Card &c) const;

  // Plays every card that can go to a foundation, repeatedly. Returns how
  // many moved, so the caller can tell whether anything happened.
  int autoPlay();

  bool undo();
  bool canUndo() const { return !history_.empty(); }

  bool won() const;

  // True if this pile index/card index identifies a face-up card that could be
  // picked up. Used by the UI to decide what a tap selects.
  bool isSelectable(int pile, int index) const;

 private:
  State state_;
  std::vector<State> history_;
  bool draw_three_ = false;
  int passes_ = 0;
  int max_passes_ = 0;

  void push();
  void flipExposed(int tableau_pile);
  bool validRun(int pile, int index) const;
};

}  // namespace solitaire
}  // namespace tabulous
