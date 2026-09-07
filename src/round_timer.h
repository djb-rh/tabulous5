// The hidden round timer and its beep schedule.
//
// The whole game hangs off this: players never see a countdown, they only hear
// the beeps speeding up, so the acceleration curve *is* the tension. Pure
// functions of elapsed time, no Arduino headers, so the curve is unit-testable
// on the host.
#pragma once

#include <cstdint>

namespace tabulous {

struct TimerConfig {
  uint32_t min_ms = 45000;   // shortest possible round
  uint32_t max_ms = 75000;   // longest possible round
  uint32_t start_interval_ms = 1200;  // gap between beeps at the start
  uint32_t end_interval_ms = 120;     // gap between beeps just before the buzz
  uint16_t beep_hz = 1400;
  uint16_t urgent_hz = 1900;  // pitch for the final stretch
  float urgent_from = 0.85f;  // progress at which the pitch jumps
};

// Gap to the next beep at a given progress through the round (0.0 → 1.0).
//
// Geometric rather than linear: a linear ramp spends most of the round sounding
// slow and then collapses at the very end, whereas a constant *ratio* per unit
// time is what reads to the ear as steady acceleration.
uint32_t beepIntervalMs(const TimerConfig &cfg, float progress);

// Beep pitch at a given progress. Jumps once, near the end, so players get an
// audible "you're nearly out" distinct from the tempo.
uint16_t beepFrequency(const TimerConfig &cfg, float progress);

// A round's total duration, drawn uniformly from [min_ms, max_ms].
uint32_t pickRoundDuration(const TimerConfig &cfg, uint32_t random_value);

// Tracks one round. Call start(), then poll() every loop with millis().
class RoundTimer {
 public:
  void start(const TimerConfig &cfg, uint32_t now_ms, uint32_t random_value);
  void stop() { running_ = false; }

  // Returns true exactly once per beep. Sets *frequency to the pitch to play.
  bool poll(uint32_t now_ms, uint16_t *frequency);

  bool running() const { return running_; }
  bool expired(uint32_t now_ms) const {
    return running_ && (now_ms - started_ms_) >= duration_ms_;
  }
  float progress(uint32_t now_ms) const;

  // Exposed for the "did the buzzer land where the timer said" check; never
  // shown to players mid-round.
  uint32_t durationMs() const { return duration_ms_; }
  uint32_t elapsedMs(uint32_t now_ms) const { return now_ms - started_ms_; }

 private:
  TimerConfig cfg_;
  bool running_ = false;
  uint32_t started_ms_ = 0;
  uint32_t duration_ms_ = 0;
  uint32_t next_beep_ms_ = 0;
};

}  // namespace tabulous
