#include "phrase_game.h"

namespace tabulous {
namespace {
const PackStats kNoStats{};
}  // namespace

void Game::begin(const std::vector<Pack> *packs, const Settings &settings) {
  packs_ = packs;
  settings_ = settings;

  const size_t n = packs_ ? packs_->size() : 0;
  bags_.assign(n, ShuffleBag{});
  eligible_.assign(n, {});
  stats_.assign(n, PackStats{});
  bag_seeded_.assign(n, false);

  resetMatch();
}

void Game::resetMatch() {
  score_[0] = score_[1] = 0;
  holder_ = Team::A;
  scoring_ = Team::B;
  current_phrase_.clear();
  timer_.stop();
  screen_ = Screen::Home;
}

const Pack *Game::currentPack() const {
  if (!packs_ || pack_index_ >= packs_->size()) return nullptr;
  return &(*packs_)[pack_index_];
}

const PackStats &Game::stats(size_t pack_index) const {
  if (pack_index >= stats_.size()) return kNoStats;
  return stats_[pack_index];
}

void Game::ensureBag(size_t pack_index, uint32_t seed) {
  if (pack_index >= bags_.size()) return;

  // Rebuild the eligibility list whenever it's empty or the difficulty filter
  // has changed the set of playable phrases (kids mode toggled mid-match).
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
    // Every phrase filtered out — kids mode on a pack with no easy phrases.
    current_phrase_.clear();
    return;
  }
  const size_t slot = bags_[pack_index_].next();
  current_phrase_ = pack->phrases()[eligible[slot]].text;
}

void Game::startRound(size_t pack_index, Team starting_team, uint32_t now_ms,
                      uint32_t seed) {
  if (!packs_ || pack_index >= packs_->size()) return;

  pack_index_ = pack_index;
  ensureBag(pack_index, seed);

  holder_ = starting_team;
  skips_left_[0] = skips_left_[1] = settings_.skips_per_round;
  if (pack_index_ < stats_.size()) stats_[pack_index_].rounds++;

  drawNextPhrase();
  timer_.start(settings_.timer, now_ms, seed ^ now_ms);
  screen_ = Screen::Round;
}

void Game::gotIt(uint32_t now_ms) {
  (void)now_ms;
  if (screen_ != Screen::Round) return;
  if (pack_index_ < stats_.size()) stats_[pack_index_].phrases_guessed++;

  if (settings_.scoring == ScoringMode::PointPerPhrase) {
    // The holder is the one giving clues to their own team, so the guess is
    // theirs. Award before the pass.
    score_[teamIndex(holder_)]++;
    if (matchOver()) {
      // Reaching the target mid-round ends it there; carrying on would mean
      // playing out a round whose result cannot matter.
      timer_.stop();
      scoring_ = holder_;
      screen_ = Screen::GameOver;
      return;
    }
  }

  // The pass is the whole game: the device changes hands, so the team that
  // just guessed is no longer the one at risk when it buzzes.
  holder_ = other(holder_);
  drawNextPhrase();
}

bool Game::skip(uint32_t now_ms) {
  (void)now_ms;
  if (screen_ != Screen::Round) return false;
  uint8_t &budget = skips_left_[teamIndex(holder_)];
  if (budget == 0) return false;
  budget--;
  if (pack_index_ < stats_.size()) stats_[pack_index_].phrases_skipped++;
  // A skip draws a new phrase but does NOT pass the device — otherwise you
  // could skip your way out of holding it when the buzzer is close.
  drawNextPhrase();
  return true;
}

bool Game::tick(uint32_t now_ms, uint16_t *beep_hz) {
  if (screen_ != Screen::Round) return false;
  return timer_.poll(now_ms, beep_hz);
}

void Game::buzz(uint32_t now_ms) {
  (void)now_ms;
  if (screen_ != Screen::Round) return;
  timer_.stop();
  // Point to whoever is NOT holding the device.
  scoring_ = other(holder_);
  score_[teamIndex(scoring_)]++;
  screen_ = Screen::Buzzer;
}

void Game::flipAward() {
  if (screen_ != Screen::Buzzer) return;
  // Undo the award and give it to the other team instead.
  if (score_[teamIndex(scoring_)] > 0) score_[teamIndex(scoring_)]--;
  scoring_ = other(scoring_);
  holder_ = other(holder_);
  score_[teamIndex(scoring_)]++;
}

void Game::resolveBonus(bool guessed) {
  if (screen_ != Screen::Buzzer) return;
  if (guessed) score_[teamIndex(scoring_)]++;
  screen_ = matchOver() ? Screen::GameOver : Screen::Home;
}

bool Game::matchOver() const {
  return score_[0] >= settings_.target_score || score_[1] >= settings_.target_score;
}

Team Game::winner() const {
  return score_[0] >= score_[1] ? Team::A : Team::B;
}

}  // namespace tabulous
