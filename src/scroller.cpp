#include "scroller.h"

#include <cmath>
#include <cstdlib>

namespace tabulous {
namespace scroller {
namespace {

using uikit::Rect;

// A finger that moves less than this before lifting was tapping, not
// scrolling. Wider than the panel's jitter, narrower than the smallest
// deliberate flick anyone makes.
constexpr int kDragThreshold = 12;
// The bar's drawn width; the hit area is the whole track rect, which is wider.
constexpr int kBarW = 18;
constexpr int kMinThumbH = 48;
// Slower than this at release and the content simply stops under the finger.
constexpr float kFlingMinV = 0.06f;  // px/ms
constexpr float kFlingStopV = 0.02f;
// How quickly a fling dies: the time constant of its decay.
constexpr float kFlingTauMs = 320.0f;

}  // namespace

void Scroller::layout(const Rect &viewport, const Rect &track, int content_h) {
  viewport_ = viewport;
  track_ = track;
  content_h_ = content_h;
  clamp();
}

int Scroller::maxOffset() const {
  const int m = content_h_ - viewport_.h;
  return m > 0 ? m : 0;
}

void Scroller::clamp() {
  if (offset_ > maxOffset()) offset_ = maxOffset();
  if (offset_ < 0) offset_ = 0;
}

void Scroller::setOffset(int px) {
  offset_ = px;
  clamp();
  if (mode_ == Mode::Fling) mode_ = Mode::Idle;
}

bool Scroller::scrollBy(int dy) {
  const int before = offset_;
  offset_ += dy;
  clamp();
  return offset_ != before;
}

bool Scroller::owns(int x, int y) const {
  if (viewport_.hit(x, y)) return true;
  return track_.w > 0 && scrollable() && track_.hit(x, y);
}

void Scroller::yieldPress() {
  if (mode_ == Mode::Pending || mode_ == Mode::Content || mode_ == Mode::Thumb) {
    mode_ = Mode::Yielded;
  }
}

void Scroller::thumbGeometry(int *y, int *h) const {
  int th = content_h_ > 0 ? (int)((int64_t)track_.h * viewport_.h / content_h_) : track_.h;
  if (th < kMinThumbH) th = kMinThumbH;
  if (th > track_.h) th = track_.h;
  const int room = track_.h - th;
  const int max = maxOffset();
  *h = th;
  *y = track_.y + (max > 0 && room > 0 ? (int)((int64_t)room * offset_ / max) : 0);
}

bool Scroller::update(uint32_t now, bool down, int x, int y) {
  const int before = offset_;

  if (!down) {
    switch (mode_) {
      case Mode::Pending:
        tap_ready_ = true;
        mode_ = Mode::Idle;
        break;
      case Mode::Content:
        // Let go while moving: keep going, slowing down.
        if (fabsf(velocity_) >= kFlingMinV && scrollable()) {
          mode_ = Mode::Fling;
          last_ms_ = now;
        } else {
          mode_ = Mode::Idle;
        }
        break;
      case Mode::Thumb:
      case Mode::Yielded:
        mode_ = Mode::Idle;
        break;
      case Mode::Fling: {
        const uint32_t dt = now - last_ms_;
        last_ms_ = now;
        if (dt == 0) break;
        // Integrate, then decay. Stopping at either end is a wall, not a
        // bounce: the content is a list, not a rubber sheet.
        offset_ += (int)lroundf(velocity_ * (float)dt);
        velocity_ *= expf(-(float)dt / kFlingTauMs);
        const bool hit_end = offset_ <= 0 || offset_ >= maxOffset();
        clamp();
        if (hit_end || fabsf(velocity_) < kFlingStopV) {
          mode_ = Mode::Idle;
          velocity_ = 0;
        }
        break;
      }
      case Mode::Idle:
        break;
    }
    return offset_ != before;
  }

  // Finger down.
  switch (mode_) {
    case Mode::Idle:
    case Mode::Fling:
      velocity_ = 0;
      if (track_.w > 0 && scrollable() && track_.hit(x, y)) {
        // On the thumb: haul it from where it was grabbed. Off the thumb:
        // the thumb jumps under the finger and is hauled from its middle.
        int ty, th;
        thumbGeometry(&ty, &th);
        grab_dy_ = (y >= ty && y < ty + th) ? y - ty : th / 2;
        mode_ = Mode::Thumb;
        // Fall into the thumb-drag maths below by pretending the finger
        // just arrived here.
        press_x_ = x;
        press_y_ = y;
        break;
      }
      if (viewport_.hit(x, y)) {
        mode_ = Mode::Pending;
        press_x_ = x;
        press_y_ = y;
        start_offset_ = offset_;
        last_y_ = y;
        last_ms_ = now;
      }
      break;
    case Mode::Pending:
      if (abs(y - press_y_) >= kDragThreshold && scrollable()) {
        mode_ = Mode::Content;
        // Start the follow from HERE rather than from the press point, so
        // the content does not jump by the threshold the moment it commits.
        press_y_ = y;
        start_offset_ = offset_;
        last_y_ = y;
        last_ms_ = now;
      }
      break;
    case Mode::Content: {
      offset_ = start_offset_ - (y - press_y_);
      clamp();
      const uint32_t dt = now - last_ms_;
      if (dt > 0) {
        const float inst = -(float)(y - last_y_) / (float)dt;
        velocity_ = velocity_ * 0.6f + inst * 0.4f;
        last_y_ = y;
        last_ms_ = now;
      }
      break;
    }
    case Mode::Yielded:
      break;
    case Mode::Thumb:
      break;
  }

  if (mode_ == Mode::Thumb) {
    int ty, th;
    thumbGeometry(&ty, &th);
    const int room = track_.h - th;
    if (room > 0) {
      const int top = y - grab_dy_ - track_.y;
      offset_ = (int)((int64_t)maxOffset() * top / room);
      clamp();
    }
  }
  return offset_ != before;
}

bool Scroller::takeTap(int *x, int *y) {
  if (!tap_ready_) return false;
  tap_ready_ = false;
  if (x) *x = press_x_;
  if (y) *y = press_y_;
  return true;
}

void Scroller::drawBar(uint16_t track_colour, uint16_t thumb_colour) const {
  if (track_.w <= 0 || !scrollable()) return;
  const int x = track_.x + (track_.w - kBarW) / 2;
  uikit::fillRoundRectFast(x, track_.y, kBarW, track_.h, kBarW / 2, track_colour);
  int ty, th;
  thumbGeometry(&ty, &th);
  uikit::fillRoundRectFast(x, ty, kBarW, th, kBarW / 2, thumb_colour);
}

}  // namespace scroller
}  // namespace tabulous
