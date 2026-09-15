// The game rules, as a pure state machine.
//
// No Arduino headers and no drawing: the UI reads this and calls into it, never
// the other way round. That keeps the rules unit-testable on the host, which
// matters because scoring and turn-passing are exactly the things that are
// embarrassing to get wrong at a party and impossible to debug mid-round.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pack.h"
#include "round_timer.h"

namespace tabulous {

enum class Screen : uint8_t {
  Home,
  CategorySelect,
  Round,
  Buzzer,       // time's up, point awarded, bonus phrase offered
  GameOver,
  Settings,
  Words,
  TextEntry,
};

// Team names are capped so they fit the score cards and the round header.
constexpr size_t kMaxTeamNameLen = 20;

enum class Team : uint8_t { A = 0, B = 1 };

inline Team other(Team t) { return t == Team::A ? Team::B : Team::A; }
inline size_t teamIndex(Team t) { return (size_t)t; }

// How points are earned. The toy only implements Classic; PointPerPhrase is a
// house rule for tables that find "a correct guess scores nothing" unintuitive.
enum class ScoringMode : uint8_t {
  Classic = 0,         // points only from the buzzer + bonus (the real rules)
  PointPerPhrase = 1,  // every correct guess also scores for the guessing team
};

struct Settings {
  TimerConfig timer;
  ScoringMode scoring = ScoringMode::Classic;
  // Which packs this game draws from; one bit per pack index.
  uint32_t enabled_packs = 0xFFFFFFFF;
  Difficulty max_difficulty = Difficulty::Hard;  // Easy == "kids mode"
  uint8_t skips_per_round = 2;
  uint8_t target_score = 7;
  uint8_t volume = 96;    // 0-255; 180 was painfully loud on the 1 W speaker
  bool sound_enabled = true;
  bool auto_rotate = true;   // flip 180 deg when the device changes hands
  uint16_t flip_delay_ms = 250;  // how long a new orientation must be held
  // Power. The charger can ask a USB adapter for 9 or 12 V (Quick Charge);
  // some PD bricks then refuse to charge at all, so it is off by default.
  // usb_power is the 5 V the console puts OUT on its USB port for a gamepad.
  bool fast_charge = false;
  bool usb_power = true;
  // The USB-C data lines: off when no computer is talking, so a charger
  // never sees the chip's USB pull-up (a brick read it as a voltage request).
  bool usb_data_auto = true;
  std::string team_names[2] = {"Team A", "Team B"};
};

// Per-category play counts, for the stats screen.
struct PackStats {
  uint32_t rounds = 0;
  uint32_t phrases_guessed = 0;
  uint32_t phrases_skipped = 0;
};

class Game {
 public:
  // `packs` must outlive the Game. Index 0..n-1 are the loaded categories.
  void begin(const std::vector<Pack> *packs, const Settings &settings);

  // ---- navigation
  Screen screen() const { return screen_; }
  void goTo(Screen s) { screen_ = s; }

  // ---- setup
  const Settings &settings() const { return settings_; }
  Settings &mutableSettings() { return settings_; }
  void resetMatch();  // scores to zero, back to Home

  // ---- round lifecycle
  //
  // `starting_team` is whoever pressed Start; they hold the device first.
  // `seed` seeds the shuffle bag on first use of a pack.
  void startRound(size_t pack_index, Team starting_team, uint32_t now_ms,
                  uint32_t seed);

  // Correct guess: advance to the next phrase and pass to the other team.
  void gotIt(uint32_t now_ms);

  // Give up on this phrase. Costs the holding team one of ITS skips; no-op
  // when that team's budget is spent.
  bool skip(uint32_t now_ms);

  // Each team gets its own budget per round — a shared pool let one team burn
  // both and leave their opponents none.
  uint8_t skipsLeft(Team t) const { return skips_left_[teamIndex(t)]; }
  uint8_t skipsLeft() const { return skipsLeft(holder_); }
  bool canSkip() const { return skipsLeft() > 0; }

  // Drive the timer. Returns true when a beep should sound (frequency out).
  bool tick(uint32_t now_ms, uint16_t *beep_hz);

  // True once the hidden timer has run out; the caller then shows the buzzer.
  bool expired(uint32_t now_ms) const { return timer_.expired(now_ms); }

  // Time's up. Awards a point to the team NOT holding the device and moves to
  // the Buzzer screen, where the scoring team gets one shot at the phrase they
  // were stuck on for a bonus point.
  void buzz(uint32_t now_ms);

  // Resolve the bonus guess, then advance to GameOver or back to Home.
  void resolveBonus(bool guessed);

  // "Wrong team?" override on the buzzer screen, for when passing went astray
  // and the device's idea of who was holding it is wrong. Moves the awarded
  // point (and the pending bonus) to the other team.
  void flipAward();

  // ---- state the UI renders
  Team holder() const { return holder_; }        // who is holding it right now
  Team scoringTeam() const { return scoring_; }  // who the buzzer awarded to
  uint8_t score(Team t) const { return score_[teamIndex(t)]; }
  const std::string &currentPhrase() const { return current_phrase_; }
  size_t packIndex() const { return pack_index_; }
  const Pack *currentPack() const;
  bool matchOver() const;
  Team winner() const;

  // Progress through the hidden round, for subtle UI heat — never a countdown.
  float progress(uint32_t now_ms) const { return timer_.progress(now_ms); }

  const PackStats &stats(size_t pack_index) const;

 private:
  const std::vector<Pack> *packs_ = nullptr;
  Settings settings_;
  Screen screen_ = Screen::Home;

  // One bag and one eligibility list per pack, so switching categories
  // mid-match doesn't lose a pack's no-repeat progress.
  std::vector<ShuffleBag> bags_;
  std::vector<std::vector<size_t>> eligible_;
  std::vector<PackStats> stats_;
  std::vector<bool> bag_seeded_;

  RoundTimer timer_;
  size_t pack_index_ = 0;
  std::string current_phrase_;
  Team holder_ = Team::A;
  Team scoring_ = Team::B;
  uint8_t score_[2] = {0, 0};
  uint8_t skips_left_[2] = {0, 0};

  void drawNextPhrase();
  void ensureBag(size_t pack_index, uint32_t seed);
};

}  // namespace tabulous
