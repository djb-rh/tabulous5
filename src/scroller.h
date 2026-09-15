// One vertical scrolling region: a viewport onto content taller than itself,
// and a bar beside it.
//
// Content moves with the finger, keeps going when the finger leaves it fast,
// and can be hauled by the bar's thumb. Before this, every list on the
// console scrolled by whole rows from a pair of arrow buttons, which is how
// a list feels when it is drawn by a program rather than held by a hand.
//
// A press inside the viewport is not a tap until it is released without
// having moved: only then can the difference be known. So the shell must hand
// presses inside a scroller's region to it (owns()) and dispatch the tap it
// gives back (takeTap()), rather than acting on the press edge as every other
// control does. A synthetic tap - one injected over serial, with no finger on
// the panel - can still be dispatched at once; see the callers.
#pragma once

#include <cstdint>
#include <functional>

#include "uikit.h"

namespace tabulous {
namespace scroller {

class Scroller {
 public:
  // Sets or resets the geometry. The offset is kept, clamped, so a repaint
  // of the screen around the list does not throw the position away; any
  // press in progress is dropped, and a finger still down is ignored until
  // it lifts. A track of zero width means no bar.
  void layout(const uikit::Rect &viewport, const uikit::Rect &track,
              int content_h);

  int offset() const { return offset_; }
  int maxOffset() const;
  bool scrollable() const { return maxOffset() > 0; }
  const uikit::Rect &viewport() const { return viewport_; }

  // Clamped. Stops any fling in progress.
  void setOffset(int px);
  // Moves by dy, clamped. Returns true if it moved at all.
  bool scrollBy(int dy);

  // Whether a press at (x, y) is this scroller's to handle.
  bool owns(int x, int y) const;

  // Poll once per loop with the raw touch state. Returns true if the offset
  // changed, in which case the caller redraws its content and the bar.
  bool update(uint32_t now, bool down, int x, int y);

  // A press inside the viewport that was released without moving. Delivered
  // once, with the point of the press.
  bool takeTap(int *x, int *y);

  // The caller has taken over the current press - a reorder handle, say - so
  // it is not a scroll, whatever the finger does next.
  void yieldPress();

  bool dragging() const { return mode_ == Mode::Content || mode_ == Mode::Thumb; }
  bool moving() const { return dragging() || mode_ == Mode::Fling; }

  // The track and its thumb, sized to how much of the content is showing.
  // Nothing is drawn when the content fits.
  void drawBar(uint16_t track_colour, uint16_t thumb_colour) const;

  // A finger already on the panel when the list appears is not a press on
  // the list: it is the tap that brought the screen here, still in contact.
  // The list waits for it to lift. layout() calls this; a screen change that
  // keeps the geometry can call it directly.
  void ignoreUntilRelease() { wait_release_ = true; }

 private:
  enum class Mode : uint8_t { Idle, Pending, Content, Thumb, Yielded, Fling };

  void thumbGeometry(int *y, int *h) const;
  void clamp();

  uikit::Rect viewport_{};
  uikit::Rect track_{};
  int content_h_ = 0;
  int offset_ = 0;

  Mode mode_ = Mode::Idle;
  int press_x_ = 0, press_y_ = 0;  // where the finger went down
  int start_offset_ = 0;           // the offset when it did
  int grab_dy_ = 0;                // thumb drags: finger y minus thumb top
  int last_y_ = 0;
  uint32_t last_ms_ = 0;
  float velocity_ = 0;             // px per ms, positive = content moving up
  bool tap_ready_ = false;
  bool wait_release_ = false;
};

// Paints a list's rows at their scrolled positions WITHOUT clearing the
// viewport first. Each row is drawn straight over whatever was there and only
// the strips between rows, the margins beside them and the corners a rounded
// row leaves bare are filled - so every pixel is painted once per frame, and
// none is blanked and then painted, which is what flickered when the whole
// region was cleared ahead of the rows.
//
// draw_row(i, y) draws row i with its top at y; a row that should show as an
// empty slot fills its own rectangle with the background. `only`, if given,
// limits the painting to that part of the viewport, so a caller can paint
// around something it has already drawn on top.
void drawRows(const Scroller &s, int row_x, int row_w, int pitch, int row_h,
              int corner_r, int count, uint16_t bg,
              const std::function<void(int, int)> &draw_row,
              const uikit::Rect *only = nullptr);

}  // namespace scroller
}  // namespace tabulous
