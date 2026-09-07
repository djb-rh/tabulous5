#include "round_timer.h"

#include <cmath>

namespace tabulous {
namespace {

float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

}  // namespace

uint32_t beepIntervalMs(const TimerConfig &cfg, float progress) {
  const float p = clamp01(progress);
  const float start = (float)cfg.start_interval_ms;
  const float end = (float)cfg.end_interval_ms;
  if (start <= 0.f || end <= 0.f) return cfg.end_interval_ms;
  // start * (end/start)^p — equivalently, linear interpolation in log space.
  return (uint32_t)lrintf(start * powf(end / start, p));
}

uint16_t beepFrequency(const TimerConfig &cfg, float progress) {
  return clamp01(progress) >= cfg.urgent_from ? cfg.urgent_hz : cfg.beep_hz;
}

uint32_t pickRoundDuration(const TimerConfig &cfg, uint32_t random_value) {
  if (cfg.max_ms <= cfg.min_ms) return cfg.min_ms;
  const uint32_t span = cfg.max_ms - cfg.min_ms;
  return cfg.min_ms + (random_value % (span + 1));
}

void RoundTimer::start(const TimerConfig &cfg, uint32_t now_ms,
                       uint32_t random_value) {
  cfg_ = cfg;
  running_ = true;
  started_ms_ = now_ms;
  duration_ms_ = pickRoundDuration(cfg, random_value);
  // First beep comes one full interval in, not immediately — an instant beep
  // on tap reads as a button click, not as the timer starting.
  next_beep_ms_ = now_ms + beepIntervalMs(cfg_, 0.f);
}

float RoundTimer::progress(uint32_t now_ms) const {
  if (!running_ || duration_ms_ == 0) return 0.f;
  return clamp01((float)(now_ms - started_ms_) / (float)duration_ms_);
}

bool RoundTimer::poll(uint32_t now_ms, uint16_t *frequency) {
  if (!running_) return false;
  // Unsigned subtraction handles the millis() rollover at ~49.7 days. It will
  // never happen in a party game, but the cost of getting it right is nothing.
  if ((int32_t)(now_ms - next_beep_ms_) < 0) return false;

  const float p = progress(now_ms);
  if (frequency) *frequency = beepFrequency(cfg_, p);

  const uint32_t interval = beepIntervalMs(cfg_, p);
  next_beep_ms_ += interval;
  // If the loop stalled long enough to miss several beeps, don't fire a burst
  // to catch up — resync to now.
  if ((int32_t)(now_ms - next_beep_ms_) > 0) next_beep_ms_ = now_ms + interval;

  return true;
}

}  // namespace tabulous
