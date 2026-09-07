// Tests for the game rules: turn passing, scoring, the bonus round, skips.
//
// These are the rules that are humiliating to get wrong in front of six people
// and impossible to debug mid-round, so they're pinned down here rather than
// discovered at a party.

#include <unity.h>

#include <set>
#include <string>
#include <vector>

#include "phrase_game.h"

using namespace tabulous;

namespace {

std::vector<Pack> makePacks() {
  std::vector<Pack> packs(2);
  packs[0].parse(
      "# name: Alpha\n"
      "A1 | easy\nA2 | medium\nA3 | hard\nA4 | easy\nA5 | medium\nA6 | hard\n");
  packs[1].parse("# name: Beta\nB1\nB2\nB3\nB4\n");
  return packs;
}

Game makeGame(const std::vector<Pack> *packs, Settings s = Settings{}) {
  Game g;
  g.begin(packs, s);
  return g;
}

}  // namespace

void test_start_round_sets_up_state() {
  auto packs = makePacks();
  Game g = makeGame(&packs);

  g.startRound(0, Team::A, 1000, 42);

  TEST_ASSERT_EQUAL(Screen::Round, g.screen());
  TEST_ASSERT_EQUAL(Team::A, g.holder());
  TEST_ASSERT_FALSE(g.currentPhrase().empty());
  TEST_ASSERT_EQUAL_UINT8(Settings{}.skips_per_round, g.skipsLeft());
  TEST_ASSERT_EQUAL_size_t(0, g.packIndex());
  TEST_ASSERT_EQUAL_STRING("Alpha", g.currentPack()->meta().name.c_str());
}

void test_got_it_passes_the_device_and_changes_phrase() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  g.startRound(0, Team::A, 1000, 42);

  const std::string first = g.currentPhrase();
  g.gotIt(1500);

  // The pass IS the game — whoever just guessed hands it over.
  TEST_ASSERT_EQUAL(Team::B, g.holder());
  TEST_ASSERT_NOT_EQUAL(0, first.compare(g.currentPhrase()));

  g.gotIt(2000);
  TEST_ASSERT_EQUAL(Team::A, g.holder());
}

void test_skip_costs_a_skip_but_does_not_pass() {
  auto packs = makePacks();
  Settings s;
  s.skips_per_round = 2;
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 1000, 42);

  const std::string first = g.currentPhrase();
  TEST_ASSERT_TRUE(g.skip(1100));

  // Critical: skipping must NOT hand the device over, or you could skip your
  // way out of holding it as the buzzer closes in.
  TEST_ASSERT_EQUAL(Team::A, g.holder());
  TEST_ASSERT_EQUAL_UINT8(1, g.skipsLeft());
  TEST_ASSERT_NOT_EQUAL(0, first.compare(g.currentPhrase()));

  TEST_ASSERT_TRUE(g.skip(1200));
  TEST_ASSERT_EQUAL_UINT8(0, g.skipsLeft());
  TEST_ASSERT_FALSE(g.canSkip());

  const std::string held = g.currentPhrase();
  TEST_ASSERT_FALSE(g.skip(1300));      // budget spent
  TEST_ASSERT_EQUAL_STRING(held.c_str(), g.currentPhrase().c_str());
}

void test_skips_are_per_team_not_a_shared_pool() {
  auto packs = makePacks();
  Settings s;
  s.skips_per_round = 2;
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 1000, 42);

  // A burns both of its skips.
  TEST_ASSERT_TRUE(g.skip(1100));
  TEST_ASSERT_TRUE(g.skip(1200));
  TEST_ASSERT_FALSE(g.canSkip());
  TEST_ASSERT_EQUAL_UINT8(0, g.skipsLeft(Team::A));

  // Passing to B must hand over a full budget, not an exhausted shared pool.
  g.gotIt(1300);
  TEST_ASSERT_EQUAL(Team::B, g.holder());
  TEST_ASSERT_EQUAL_UINT8(2, g.skipsLeft(Team::B));
  TEST_ASSERT_TRUE(g.canSkip());
  TEST_ASSERT_TRUE(g.skip(1400));
  TEST_ASSERT_EQUAL_UINT8(1, g.skipsLeft(Team::B));

  // And A's exhausted budget is still exhausted when it comes back round.
  g.gotIt(1500);
  TEST_ASSERT_EQUAL(Team::A, g.holder());
  TEST_ASSERT_FALSE(g.canSkip());
}

void test_skip_budgets_reset_each_round() {
  auto packs = makePacks();
  Settings s;
  s.skips_per_round = 1;
  Game g = makeGame(&packs, s);

  g.startRound(0, Team::A, 1000, 42);
  TEST_ASSERT_TRUE(g.skip(1100));
  TEST_ASSERT_FALSE(g.canSkip());

  g.buzz(60000);
  g.resolveBonus(false);
  g.startRound(0, Team::B, 70000, 43);
  TEST_ASSERT_EQUAL_UINT8(1, g.skipsLeft(Team::A));
  TEST_ASSERT_EQUAL_UINT8(1, g.skipsLeft(Team::B));
}

void test_buzz_awards_to_the_team_not_holding() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  g.startRound(0, Team::A, 1000, 42);

  // A holds it, so B scores.
  g.buzz(60000);
  TEST_ASSERT_EQUAL(Screen::Buzzer, g.screen());
  TEST_ASSERT_EQUAL(Team::B, g.scoringTeam());
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::B));
}

void test_buzz_after_a_pass_awards_the_other_way() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  g.startRound(0, Team::A, 1000, 42);

  g.gotIt(1500);  // now B holds it
  g.buzz(60000);

  TEST_ASSERT_EQUAL(Team::A, g.scoringTeam());
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));
}

void test_bonus_round_adds_a_point_only_when_guessed() {
  auto packs = makePacks();
  Game g = makeGame(&packs);

  g.startRound(0, Team::A, 1000, 42);
  g.buzz(60000);              // B scores 1
  g.resolveBonus(true);       // B guesses the bonus phrase
  TEST_ASSERT_EQUAL_UINT8(2, g.score(Team::B));
  TEST_ASSERT_EQUAL(Screen::Home, g.screen());

  g.startRound(0, Team::A, 70000, 43);
  g.buzz(130000);             // B scores again
  g.resolveBonus(false);      // and misses the bonus
  TEST_ASSERT_EQUAL_UINT8(3, g.score(Team::B));
}

void test_flip_award_moves_the_point_to_the_other_team() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  g.startRound(0, Team::A, 1000, 42);
  g.buzz(60000);

  TEST_ASSERT_EQUAL(Team::B, g.scoringTeam());
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::B));

  g.flipAward();  // "wrong team?" — passing went astray at the table

  TEST_ASSERT_EQUAL(Team::A, g.scoringTeam());
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));

  // The bonus must follow the corrected team, not the original one.
  g.resolveBonus(true);
  TEST_ASSERT_EQUAL_UINT8(2, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));
}

void test_match_ends_at_target_score() {
  auto packs = makePacks();
  Settings s;
  s.target_score = 3;
  Game g = makeGame(&packs, s);

  // A holds each round, so B scores each time.
  for (int i = 0; i < 2; i++) {
    g.startRound(0, Team::A, 1000 + i * 1000, 42 + i);
    g.buzz(60000 + i * 1000);
    g.resolveBonus(false);
    TEST_ASSERT_FALSE(g.matchOver());
    TEST_ASSERT_EQUAL(Screen::Home, g.screen());
  }

  g.startRound(0, Team::A, 9000, 99);
  g.buzz(69000);
  g.resolveBonus(false);

  TEST_ASSERT_TRUE(g.matchOver());
  TEST_ASSERT_EQUAL(Screen::GameOver, g.screen());
  TEST_ASSERT_EQUAL(Team::B, g.winner());
  TEST_ASSERT_EQUAL_UINT8(3, g.score(Team::B));
}

void test_bonus_point_can_win_the_match() {
  auto packs = makePacks();
  Settings s;
  s.target_score = 2;
  Game g = makeGame(&packs, s);

  g.startRound(0, Team::A, 1000, 42);
  g.buzz(60000);               // B -> 1
  TEST_ASSERT_FALSE(g.matchOver());
  g.resolveBonus(true);        // B -> 2, wins on the bonus

  TEST_ASSERT_TRUE(g.matchOver());
  TEST_ASSERT_EQUAL(Screen::GameOver, g.screen());
  TEST_ASSERT_EQUAL(Team::B, g.winner());
}

void test_kids_mode_only_serves_easy_phrases() {
  auto packs = makePacks();
  Settings s;
  s.max_difficulty = Difficulty::Easy;  // pack Alpha has exactly A1 and A4 easy
  Game g = makeGame(&packs, s);

  g.startRound(0, Team::A, 1000, 42);

  std::set<std::string> seen;
  seen.insert(g.currentPhrase());
  for (int i = 0; i < 10; i++) {
    g.gotIt(1000 + i * 100);
    seen.insert(g.currentPhrase());
  }

  TEST_ASSERT_EQUAL_size_t(2, seen.size());
  TEST_ASSERT_TRUE(seen.count("A1") == 1);
  TEST_ASSERT_TRUE(seen.count("A4") == 1);
}

void test_each_pack_keeps_its_own_no_repeat_progress() {
  auto packs = makePacks();
  Game g = makeGame(&packs);

  // Draw two phrases from Beta, switch to Alpha, come back. Beta must resume
  // its bag rather than restarting, or switching category would resurface
  // phrases players just had.
  g.startRound(1, Team::A, 1000, 7);
  std::set<std::string> beta_seen;
  beta_seen.insert(g.currentPhrase());
  g.gotIt(1100);
  beta_seen.insert(g.currentPhrase());

  g.startRound(0, Team::A, 2000, 7);   // Alpha
  TEST_ASSERT_EQUAL_STRING("Alpha", g.currentPack()->meta().name.c_str());

  g.startRound(1, Team::A, 3000, 7);   // back to Beta
  beta_seen.insert(g.currentPhrase());
  g.gotIt(3100);
  beta_seen.insert(g.currentPhrase());

  // Beta has 4 phrases; four draws across the interruption must all differ.
  TEST_ASSERT_EQUAL_size_t(4, beta_seen.size());
}

void test_actions_outside_the_round_are_ignored() {
  auto packs = makePacks();
  Game g = makeGame(&packs);

  // On Home, nothing should mutate state.
  uint16_t hz = 0;
  TEST_ASSERT_FALSE(g.tick(5000, &hz));
  TEST_ASSERT_FALSE(g.skip(5000));
  g.gotIt(5000);
  g.buzz(5000);
  TEST_ASSERT_EQUAL(Screen::Home, g.screen());
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));

  // flipAward outside the buzzer screen is a no-op too.
  g.flipAward();
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::A));
}

void test_start_round_with_bad_pack_index_is_a_no_op() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  g.startRound(99, Team::A, 1000, 42);
  TEST_ASSERT_EQUAL(Screen::Home, g.screen());
}

void test_timer_beeps_during_a_round() {
  auto packs = makePacks();
  Settings s;
  s.timer.min_ms = s.timer.max_ms = 50000;
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 0, 42);

  int beeps = 0;
  for (uint32_t t = 0; t <= 50000; t += 5) {
    uint16_t hz = 0;
    if (g.tick(t, &hz)) beeps++;
  }
  TEST_ASSERT_GREATER_THAN_INT(20, beeps);
  TEST_ASSERT_TRUE(g.expired(50000));
}

void test_point_per_phrase_scores_the_guessing_team() {
  auto packs = makePacks();
  Settings s;
  s.scoring = ScoringMode::PointPerPhrase;
  s.target_score = 10;  // high enough not to end the match mid-test
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 1000, 42);

  // A is holding, so A is giving clues to A's own team — the point is theirs.
  g.gotIt(1100);
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));
  TEST_ASSERT_EQUAL(Team::B, g.holder());

  g.gotIt(1200);
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(1, g.score(Team::B));
  TEST_ASSERT_EQUAL(Team::A, g.holder());
}

void test_point_per_phrase_still_penalises_on_the_buzzer() {
  auto packs = makePacks();
  Settings s;
  s.scoring = ScoringMode::PointPerPhrase;
  s.target_score = 10;
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 1000, 42);

  g.gotIt(1100);          // A scores, passes to B
  g.buzz(60000);          // B holding, so A scores again
  TEST_ASSERT_EQUAL_UINT8(2, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));
  TEST_ASSERT_EQUAL(Team::A, g.scoringTeam());
}

void test_point_per_phrase_can_win_mid_round() {
  auto packs = makePacks();
  Settings s;
  s.scoring = ScoringMode::PointPerPhrase;
  s.target_score = 2;
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 1000, 42);

  g.gotIt(1100);  // A -> 1, passes to B
  g.gotIt(1200);  // B -> 1, passes to A
  g.gotIt(1300);  // A -> 2, hits the target

  TEST_ASSERT_TRUE(g.matchOver());
  TEST_ASSERT_EQUAL(Screen::GameOver, g.screen());
  TEST_ASSERT_EQUAL(Team::A, g.winner());
  // The round must stop dead rather than play on to a result that can't matter.
  uint16_t hz = 0;
  TEST_ASSERT_FALSE(g.tick(1400, &hz));
}

void test_classic_scoring_awards_nothing_for_a_correct_guess() {
  auto packs = makePacks();
  Settings s;
  s.scoring = ScoringMode::Classic;  // the default, asserted explicitly
  Game g = makeGame(&packs, s);
  g.startRound(0, Team::A, 1000, 42);

  for (int i = 0; i < 5; i++) g.gotIt(1000 + i * 100);
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));
  TEST_ASSERT_EQUAL(Screen::Round, g.screen());
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_start_round_sets_up_state);
  RUN_TEST(test_got_it_passes_the_device_and_changes_phrase);
  RUN_TEST(test_skip_costs_a_skip_but_does_not_pass);
  RUN_TEST(test_skips_are_per_team_not_a_shared_pool);
  RUN_TEST(test_skip_budgets_reset_each_round);
  RUN_TEST(test_buzz_awards_to_the_team_not_holding);
  RUN_TEST(test_buzz_after_a_pass_awards_the_other_way);
  RUN_TEST(test_bonus_round_adds_a_point_only_when_guessed);
  RUN_TEST(test_flip_award_moves_the_point_to_the_other_team);
  RUN_TEST(test_match_ends_at_target_score);
  RUN_TEST(test_bonus_point_can_win_the_match);
  RUN_TEST(test_kids_mode_only_serves_easy_phrases);
  RUN_TEST(test_each_pack_keeps_its_own_no_repeat_progress);
  RUN_TEST(test_actions_outside_the_round_are_ignored);
  RUN_TEST(test_start_round_with_bad_pack_index_is_a_no_op);
  RUN_TEST(test_timer_beeps_during_a_round);
  RUN_TEST(test_classic_scoring_awards_nothing_for_a_correct_guess);
  RUN_TEST(test_point_per_phrase_scores_the_guessing_team);
  RUN_TEST(test_point_per_phrase_still_penalises_on_the_buzzer);
  RUN_TEST(test_point_per_phrase_can_win_mid_round);

  return UNITY_END();
}
