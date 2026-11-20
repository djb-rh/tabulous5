// FiveHead — our take on Heads Up!
//
// One player holds the Tab5 against their forehead, screen facing the room.
// Everyone else describes or acts out the phrase; the holder guesses. Tilting
// the device down scores it, tilting up passes. A visible clock runs the
// round — the opposite of PhraseCraze, where the timer is hidden and the whole
// game is not being caught holding it.
//
// Rules only, no Arduino headers, so it unit-tests on the host alongside
// PhraseCraze's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pack.h"

namespace tabulous {
namespace fivehead {

enum class Screen : uint8_t {
  Home,
  CategorySelect,
  Ready,    // "put it on your forehead" countdown before the clock starts
  Round,
  Results,
  Settings,
};

enum class Team : uint8_t { A = 0, B = 1 };
inline Team other(Team t) { return t == Team::A ? Team::B : Team::A; }
inline size_t teamIndex(Team t) { return (size_t)t; }

// How far the device must be tipped before a gesture counts, in g on the
// screen's normal axis, and how near it must come back before the next one
// can. Both are measured from where the device RESTS, not from upright: a
// tablet held against a forehead leans back with the forehead, a tenth or a
// quarter of a g off vertical, and measured from vertical one direction then
// needs twice the swing of the other and the "light" setting, whose neutral
// zone was the narrowest, was the hardest to re-arm.
//
// Near upright a g is 57 degrees, so these are about 14, 20 and 29 degrees of
// tip. Firmer is safer against accidental scoring; lighter is quicker to
// play. Trigger and neutral are kept well apart so the reading can't dither
// across the boundary.
enum class TiltSensitivity : uint8_t { Light = 0, Normal = 1, Firm = 2 };

struct TiltProfile {
  float trigger_g;
  float neutral_g;
};

inline TiltProfile tiltProfile(TiltSensitivity s) {
  switch (s) {
    case TiltSensitivity::Light: return {0.24f, 0.12f};
    case TiltSensitivity::Firm: return {0.48f, 0.24f};
    default: return {0.34f, 0.17f};
  }
}

// The tilt gesture, from readings of the screen-normal axis.
//
// It starts with no idea where rest is, and disarmed: the device is mid-air
// on its way to a forehead when a round begins. Rest is wherever the reading
// first holds still. From then a gesture is a tip of trigger_g or more away
// from rest, held for kHoldMs (a single noisy sample is not a decision), and
// the next one is only allowed once the device has come back to within
// neutral_g of rest. Rest follows a slow change of posture, and is found
// again if the device settles somewhere new for a couple of seconds.
enum class Tilt : int8_t { Up = -1, None = 0, Down = 1 };

class TiltDetector {
 public:
  static constexpr uint32_t kHoldMs = 150;
  // "Still" is the reading staying within this of where it was.
  static constexpr float kStillBandG = 0.08f;
  static constexpr uint32_t kStillMs = 250;       // to find rest the first time
  static constexpr uint32_t kGiveUpMs = 1500;     // never still: take what there is
  static constexpr uint32_t kMovedMs = 2000;      // settled somewhere new: that is rest now
  // Flat on a table reads a whole g. That is not a forehead, and nothing
  // further from upright than this is taken for a resting position.
  static constexpr float kMaxRestG = 0.65f;

  void reset();
  // Feed every sample. `may_fire` false (a verdict is on screen) keeps rest
  // and arming up to date without starting a gesture.
  Tilt update(float az, uint32_t now_ms, const TiltProfile &profile, bool may_fire = true);

  bool hasRest() const { return has_rest_; }
  bool armed() const { return armed_; }
  float rest() const { return rest_; }

 private:
  bool started_ = false, has_rest_ = false, armed_ = false;
  uint32_t started_ms_ = 0;
  float rest_ = 0;
  bool still_valid_ = false;
  float still_ref_ = 0;
  uint32_t still_since_ = 0;
  bool holding_ = false, hold_down_ = false;
  uint32_t hold_since_ = 0;
};

struct Settings {
  uint32_t round_ms = 60000;
  uint32_t ready_ms = 3000;   // grace to get it to your forehead
  uint8_t rounds_each = 3;    // match length, in turns per team
  Difficulty max_difficulty = Difficulty::Hard;
  bool tilt_swapped = false;  // which way is "correct" depends on how it's held
  TiltSensitivity tilt_sensitivity = TiltSensitivity::Normal;
  uint16_t flash_ms = 800;    // how long the verdict stays up between phrases
  // Which packs this game draws from; one bit per pack index.
  uint32_t enabled_packs = 0xFFFFFFFF;
  std::string team_names[2] = {"Team 1", "Team 2"};
};

// What happened to each phrase, for the end-of-round recap. Heads Up's recap
// is half the fun — people want to see the one they nearly had.
struct Outcome {
  std::string phrase;
  bool correct = false;
};

class Game {
 public:
  void begin(const std::vector<Pack> *packs, const Settings &settings);
  void resetMatch();

  Screen screen() const { return screen_; }
  void goTo(Screen s) { screen_ = s; }

  const Settings &settings() const { return settings_; }
  Settings &mutableSettings() { return settings_; }

  // Arms the Ready countdown; the clock itself starts on beginPlay().
  void startRound(size_t pack_index, uint32_t now_ms, uint32_t seed);
  void beginPlay(uint32_t now_ms);

  void correct(uint32_t now_ms);
  void pass(uint32_t now_ms);

  bool readyElapsed(uint32_t now_ms) const;
  uint32_t readyRemainingMs(uint32_t now_ms) const;
  bool expired(uint32_t now_ms) const;
  uint32_t remainingMs(uint32_t now_ms) const;

  // Ends the round, banks the score and moves to the recap.
  void endRound(uint32_t now_ms);

  // Hands play to the other team. Called when leaving the recap.
  void nextTurn() { turn_ = other(turn_); }

  const std::string &currentPhrase() const { return current_phrase_; }
  const std::vector<Outcome> &outcomes() const { return outcomes_; }
  int roundScore() const { return round_score_; }

  Team turn() const { return turn_; }
  uint8_t score(Team t) const { return score_[teamIndex(t)]; }
  uint8_t turnsTaken(Team t) const { return turns_[teamIndex(t)]; }
  bool matchOver() const;
  Team winner() const;

  size_t packIndex() const { return pack_index_; }
  const Pack *currentPack() const;

 private:
  const std::vector<Pack> *packs_ = nullptr;
  Settings settings_;
  Screen screen_ = Screen::Home;

  std::vector<ShuffleBag> bags_;
  std::vector<std::vector<size_t>> eligible_;
  std::vector<bool> bag_seeded_;

  size_t pack_index_ = 0;
  std::string current_phrase_;
  std::vector<Outcome> outcomes_;

  uint32_t ready_started_ = 0;
  uint32_t started_ms_ = 0;
  bool running_ = false;

  int round_score_ = 0;
  Team turn_ = Team::A;
  uint8_t score_[2] = {0, 0};
  uint8_t turns_[2] = {0, 0};

  void ensureBag(size_t pack_index, uint32_t seed);
  void drawNextPhrase();
  void record(bool correct);
};

}  // namespace fivehead
}  // namespace tabulous
