// FiveHead rules: scoring, turn alternation, the recap, match end.

#include <unity.h>

#include <vector>

#include "fivehead.h"

using namespace tabulous;
using namespace tabulous::fivehead;

namespace {

std::vector<Pack> makePacks() {
  std::vector<Pack> packs(1);
  packs[0].parse("# name: Alpha\nA1\nA2\nA3\nA4\nA5\nA6\n");
  return packs;
}

Game makeGame(const std::vector<Pack> *packs, Settings s = Settings{}) {
  Game g;
  g.begin(packs, s);
  return g;
}

// The clock only starts after the Ready countdown, so tests must pass through
// it exactly as play does.
void enterRound(Game *g, uint32_t at) {
  g->startRound(0, at, 42);
  g->beginPlay(at + Settings{}.ready_ms);
}

}  // namespace

void test_ready_precedes_play() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  g.startRound(0, 1000, 42);

  TEST_ASSERT_EQUAL(Screen::Ready, g.screen());
  TEST_ASSERT_FALSE(g.readyElapsed(1500));
  TEST_ASSERT_TRUE(g.readyElapsed(1000 + Settings{}.ready_ms));

  // Scoring must not be possible before the clock starts.
  g.correct(1500);
  TEST_ASSERT_EQUAL_INT(0, g.roundScore());

  g.beginPlay(4000);
  TEST_ASSERT_EQUAL(Screen::Round, g.screen());
}

void test_correct_and_pass_are_both_recorded() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  enterRound(&g, 0);

  const std::string first = g.currentPhrase();
  g.correct(100);
  const std::string second = g.currentPhrase();
  g.pass(200);

  TEST_ASSERT_EQUAL_INT(1, g.roundScore());
  TEST_ASSERT_EQUAL_size_t(2, g.outcomes().size());
  // The recap is meant to show the ones that got away, not just the wins.
  TEST_ASSERT_TRUE(g.outcomes()[0].correct);
  TEST_ASSERT_FALSE(g.outcomes()[1].correct);
  TEST_ASSERT_EQUAL_STRING(first.c_str(), g.outcomes()[0].phrase.c_str());
  TEST_ASSERT_EQUAL_STRING(second.c_str(), g.outcomes()[1].phrase.c_str());
  TEST_ASSERT_NOT_EQUAL(0, first.compare(g.currentPhrase()));
}

void test_clock_runs_and_expires() {
  auto packs = makePacks();
  Settings s;
  s.round_ms = 60000;
  Game g = makeGame(&packs, s);
  enterRound(&g, 0);
  const uint32_t start = s.ready_ms;

  TEST_ASSERT_EQUAL_UINT32(60000, g.remainingMs(start));
  TEST_ASSERT_EQUAL_UINT32(30000, g.remainingMs(start + 30000));
  TEST_ASSERT_FALSE(g.expired(start + 59999));
  TEST_ASSERT_TRUE(g.expired(start + 60000));
  TEST_ASSERT_EQUAL_UINT32(0, g.remainingMs(start + 90000));
}

void test_end_round_banks_the_score_to_the_team_whose_turn_it_is() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  enterRound(&g, 0);

  g.correct(100);
  g.correct(200);
  g.pass(300);
  g.endRound(70000);

  TEST_ASSERT_EQUAL(Screen::Results, g.screen());
  TEST_ASSERT_EQUAL_UINT8(2, g.score(Team::A));
  TEST_ASSERT_EQUAL_UINT8(0, g.score(Team::B));
  TEST_ASSERT_EQUAL_UINT8(1, g.turnsTaken(Team::A));

  // Scoring after the round has ended must not leak into the next one.
  g.correct(80000);
  TEST_ASSERT_EQUAL_UINT8(2, g.score(Team::A));
}

void test_turns_alternate_and_match_ends_evenly() {
  auto packs = makePacks();
  Settings s;
  s.rounds_each = 2;
  Game g = makeGame(&packs, s);

  for (int turn = 0; turn < 4; turn++) {
    TEST_ASSERT_FALSE(g.matchOver());
    enterRound(&g, 100000u * (turn + 1));
    g.correct(100000u * (turn + 1) + s.ready_ms + 10);
    g.endRound(100000u * (turn + 1) + 70000);
    g.nextTurn();
    g.goTo(Screen::Home);
  }

  // Both teams must have had the same number of turns before it can end —
  // otherwise one side wins by having had an extra go.
  TEST_ASSERT_TRUE(g.matchOver());
  TEST_ASSERT_EQUAL_UINT8(2, g.turnsTaken(Team::A));
  TEST_ASSERT_EQUAL_UINT8(2, g.turnsTaken(Team::B));
}

void test_match_not_over_when_only_one_team_has_finished() {
  auto packs = makePacks();
  Settings s;
  s.rounds_each = 1;
  Game g = makeGame(&packs, s);

  enterRound(&g, 1000);
  g.endRound(70000);
  g.nextTurn();
  TEST_ASSERT_FALSE(g.matchOver());
}

void test_no_repeats_within_a_round() {
  auto packs = makePacks();
  Game g = makeGame(&packs);
  enterRound(&g, 0);

  std::vector<std::string> seen{g.currentPhrase()};
  for (int i = 0; i < 5; i++) {
    g.correct(100 + i);
    seen.push_back(g.currentPhrase());
  }
  for (size_t i = 0; i < seen.size(); i++) {
    for (size_t j = i + 1; j < seen.size(); j++) {
      TEST_ASSERT_NOT_EQUAL(0, seen[i].compare(seen[j]));
    }
  }
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ready_precedes_play);
  RUN_TEST(test_correct_and_pass_are_both_recorded);
  RUN_TEST(test_clock_runs_and_expires);
  RUN_TEST(test_end_round_banks_the_score_to_the_team_whose_turn_it_is);
  RUN_TEST(test_turns_alternate_and_match_ends_evenly);
  RUN_TEST(test_match_not_over_when_only_one_team_has_finished);
  RUN_TEST(test_no_repeats_within_a_round);
  return UNITY_END();
}
