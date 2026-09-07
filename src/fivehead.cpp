#include "fivehead.h"

namespace tabulous {
namespace fivehead {

void Game::begin(const std::vector<Pack> *packs, const Settings &settings) {
  packs_ = packs;
  settings_ = settings;

  const size_t n = packs_ ? packs_->size() : 0;
  bags_.assign(n, ShuffleBag{});
  eligible_.assign(n, {});
  bag_seeded_.assign(n, false);

  resetMatch();
}

void Game::resetMatch() {
  score_[0] = score_[1] = 0;
  turns_[0] = turns_[1] = 0;
  turn_ = Team::A;
  round_score_ = 0;
  running_ = false;
  outcomes_.clear();
  current_phrase_.clear();
  screen_ = Screen::Home;
}

const Pack *Game::currentPack() const {
  if (!packs_ || pack_index_ >= packs_->size()) return nullptr;
  return &(*packs_)[pack_index_];
}

void Game::ensureBag(size_t pack_index, uint32_t seed) {
  if (pack_index >= bags_.size()) return;
  const Pack &pack = (*packs_)[pack_index];
  std::vector<size_t> eligible = pack.indicesUpTo(settings_.max_difficulty);
  if (!bag_seeded_[pack_index] || eligible != eligible_[pack_index]) {
    eligible_[pack_index] = std::move(eligible);
    bags_[pack_index].reset(eligible_[pack_index].size(), seed);
    bag_seeded_[pack_index] = true;
  }
}

void Game::drawNextPhrase() {
  const Pack *pack = currentPack();
  if (!pack || pack_index_ >= bags_.size()) {
    current_phrase_.clear();
    return;
  }
  const auto &eligible = eligible_[pack_index_];
  if (eligible.empty()) {
    current_phrase_.clear();
    return;
  }
  current_phrase_ = pack->phrases()[eligible[bags_[pack_index_].next()]].text;
}

void Game::startRound(size_t pack_index, uint32_t now_ms, uint32_t seed) {
  if (!packs_ || pack_index >= packs_->size()) return;

  pack_index_ = pack_index;
  ensureBag(pack_index, seed);

  outcomes_.clear();
  round_score_ = 0;
  running_ = false;
  ready_started_ = now_ms;
  drawNextPhrase();
  screen_ = Screen::Ready;
}

void Game::beginPlay(uint32_t now_ms) {
  if (screen_ != Screen::Ready) return;
  started_ms_ = now_ms;
  running_ = true;
  screen_ = Screen::Round;
}

bool Game::readyElapsed(uint32_t now_ms) const {
  return screen_ == Screen::Ready &&
         (now_ms - ready_started_) >= settings_.ready_ms;
}

uint32_t Game::readyRemainingMs(uint32_t now_ms) const {
  if (screen_ != Screen::Ready) return 0;
  const uint32_t elapsed = now_ms - ready_started_;
  return elapsed >= settings_.ready_ms ? 0 : settings_.ready_ms - elapsed;
}

bool Game::expired(uint32_t now_ms) const {
  return running_ && (now_ms - started_ms_) >= settings_.round_ms;
}

uint32_t Game::remainingMs(uint32_t now_ms) const {
  if (!running_) return settings_.round_ms;
  const uint32_t elapsed = now_ms - started_ms_;
  return elapsed >= settings_.round_ms ? 0 : settings_.round_ms - elapsed;
}

void Game::record(bool correct) {
  if (current_phrase_.empty()) return;
  outcomes_.push_back({current_phrase_, correct});
  if (correct) round_score_++;
  drawNextPhrase();
}

void Game::correct(uint32_t now_ms) {
  if (screen_ != Screen::Round || !running_) return;
  (void)now_ms;
  record(true);
}

void Game::pass(uint32_t now_ms) {
  if (screen_ != Screen::Round || !running_) return;
  (void)now_ms;
  // A pass is recorded too — the recap is meant to show the ones that got
  // away, not just the wins.
  record(false);
}

void Game::endRound(uint32_t now_ms) {
  (void)now_ms;
  if (screen_ != Screen::Round) return;
  running_ = false;
  score_[teamIndex(turn_)] += (uint8_t)round_score_;
  turns_[teamIndex(turn_)]++;
  screen_ = Screen::Results;
}

bool Game::matchOver() const {
  // The match ends when both teams have had their allotted turns, so neither
  // side can win by having had one more go than the other.
  return turns_[0] >= settings_.rounds_each && turns_[1] >= settings_.rounds_each;
}

Team Game::winner() const { return score_[0] >= score_[1] ? Team::A : Team::B; }

}  // namespace fivehead
}  // namespace tabulous
