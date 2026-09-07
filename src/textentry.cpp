#include "textentry.h"

#include <M5Unified.h>

#include <cctype>
#include <cstdio>

#include "audio.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace textentry {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Key : uint8_t {
  None, Char, Backspace, Shift, Layer, Done, Cancel, Clear,
};

std::string g_text;
std::string g_title;
std::string g_result;
size_t g_max = 20;
bool g_active = false;
bool g_accepted = false;
bool g_shift = true;   // names usually start with a capital
bool g_symbols = false;

bool g_dirty = true;
// Typing changes only the field, not the 37 keys around it. Repainting the
// whole keyboard per keystroke was slow enough to feel like dropped touches.
bool g_redraw_field = false;
bool g_redraw_keys = false;

struct Spec {
  const char *label;
  Key key;
  int param;
  float width;  // in key units; every row totals 10 so the rows line up
};

const Spec kAlpha[4][12] = {
  {{"Q",Key::Char,'q',1},{"W",Key::Char,'w',1},{"E",Key::Char,'e',1},
   {"R",Key::Char,'r',1},{"T",Key::Char,'t',1},{"Y",Key::Char,'y',1},
   {"U",Key::Char,'u',1},{"I",Key::Char,'i',1},{"O",Key::Char,'o',1},
   {"P",Key::Char,'p',1},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
  {{"A",Key::Char,'a',1},{"S",Key::Char,'s',1},{"D",Key::Char,'d',1},
   {"F",Key::Char,'f',1},{"G",Key::Char,'g',1},{"H",Key::Char,'h',1},
   {"J",Key::Char,'j',1},{"K",Key::Char,'k',1},{"L",Key::Char,'l',1},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
  {{"SHIFT",Key::Shift,0,1.5f},{"Z",Key::Char,'z',1},{"X",Key::Char,'x',1},
   {"C",Key::Char,'c',1},{"V",Key::Char,'v',1},{"B",Key::Char,'b',1},
   {"N",Key::Char,'n',1},{"M",Key::Char,'m',1},{"DEL",Key::Backspace,0,1.5f},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
  {{"123",Key::Layer,0,1.5f},{"SPACE",Key::Char,' ',5.5f},
   {"DONE",Key::Done,0,3},{nullptr,Key::None,0,0},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
};

const Spec kSymbols[4][12] = {
  {{"1",Key::Char,'1',1},{"2",Key::Char,'2',1},{"3",Key::Char,'3',1},
   {"4",Key::Char,'4',1},{"5",Key::Char,'5',1},{"6",Key::Char,'6',1},
   {"7",Key::Char,'7',1},{"8",Key::Char,'8',1},{"9",Key::Char,'9',1},
   {"0",Key::Char,'0',1},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
  {{"-",Key::Char,'-',1},{"_",Key::Char,'_',1},{"'",Key::Char,'\'',1},
   {"&",Key::Char,'&',1},{"+",Key::Char,'+',1},{"/",Key::Char,'/',1},
   {"(",Key::Char,'(',1},{")",Key::Char,')',1},{"#",Key::Char,'#',1},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
  {{".",Key::Char,'.',1.5f},{",",Key::Char,',',1},{"!",Key::Char,'!',1},
   {"?",Key::Char,'?',1},{":",Key::Char,':',1},{"*",Key::Char,'*',1},
   {"@",Key::Char,'@',1},{"$",Key::Char,'$',1},{"DEL",Key::Backspace,0,1.5f},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
  {{"ABC",Key::Layer,0,1.5f},{"SPACE",Key::Char,' ',5.5f},
   {"DONE",Key::Done,0,3},{nullptr,Key::None,0,0},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0},{nullptr,Key::None,0,0},
   {nullptr,Key::None,0,0},{nullptr,Key::None,0,0}},
};

constexpr int kFieldY = 82;
constexpr int kFieldH = 88;
constexpr int kClearSize = 64;

Rect clearRect() {
  return Rect{kW - kMargin - 20 - kClearSize,
              kFieldY + (kFieldH - kClearSize) / 2, kClearSize, kClearSize};
}

void addKey(const Rect &r, Key k, int param = 0) {
  uikit::addTarget(r, (int)k, param);
}

// Cheap enough to run per keystroke: a fixed font chosen by length, rather
// than a ladder-and-wrap search.
void drawField() {
  auto &g = gfx();
  g.fillRoundRect(kMargin, kFieldY, kW - 2 * kMargin, kFieldH, 14, kSurface);

  const std::string shown = g_text + "|";
  g.setFont(g_text.size() <= 14 ? &fonts::FreeSansBold24pt7b
                                : &fonts::FreeSansBold18pt7b);
  g.setTextSize(1);
  g.setTextDatum(middle_center);
  g.setTextColor(kText, kSurface);
  const int usable_left = kMargin + 28;
  const int usable_right = clearRect().x - 20;
  g.drawString(shown.c_str(), (usable_left + usable_right) / 2,
               kFieldY + kFieldH / 2);

  char counter[16];
  snprintf(counter, sizeof(counter), "%u/%u", (unsigned)g_text.size(),
           (unsigned)g_max);
  g.fillRect(kW - kMargin - 200, 26, 200, 30, kBg);
  drawLabel(counter, kW - kMargin, 28, kMuted, &fonts::FreeSans9pt7b,
            top_right);

  const Rect c = clearRect();
  const int cx = c.x + c.w / 2, cy = c.y + c.h / 2, r = c.w / 2;
  const bool on = !g_text.empty();
  g.fillCircle(cx, cy, r, on ? kDanger : kSurfaceLift);
  const int arm = r / 2;
  const uint16_t ink = on ? kText : kMuted;
  for (int o = -2; o <= 2; o++) {
    g.drawLine(cx - arm + o, cy - arm, cx + arm + o, cy + arm, ink);
    g.drawLine(cx + arm + o, cy - arm, cx - arm + o, cy + arm, ink);
  }
}

// Keys never move, so a change of appearance (SHIFT flipping their case, or
// letters greying out at the cap) needs only the key faces redrawn.
void drawKeys(bool add_targets) {
  auto &g = gfx();
  const int kw = 110, gap = 10, row_h = 92, top = 186;
  const auto &rows = g_symbols ? kSymbols : kAlpha;

  for (int r = 0; r < 4; r++) {
    float units = 0;
    int count = 0;
    for (int i = 0; i < 12 && rows[r][i].label; i++) {
      units += rows[r][i].width;
      count++;
    }
    const int row_w = (int)(units * kw) + (count - 1) * gap;
    int x = (kW - row_w) / 2;
    const int y = top + r * (row_h + gap);

    for (int i = 0; i < count; i++) {
      const Spec &s = rows[r][i];
      const int w = (int)(s.width * kw);
      const Rect rect{x, y, w, row_h};

      uint16_t bg = kSurfaceLift, fg = kText;
      if (s.key == Key::Done) bg = kGood;
      else if (s.key == Key::Shift && g_shift) { bg = kAccent; fg = kInk; }
      else if (s.key == Key::Backspace) bg = kSurface;

      char label[8];
      if (s.key == Key::Char && s.param >= 'a' && s.param <= 'z') {
        label[0] = g_shift ? (char)toupper(s.param) : (char)s.param;
        label[1] = '\0';
      } else {
        snprintf(label, sizeof(label), "%s", s.label);
      }

      const bool full = g_text.size() >= g_max;
      if (s.key == Key::Char && full) { bg = kSurface; fg = kMuted; }

      drawButton(rect, label, bg, fg,
                 s.width > 1.2f ? &fonts::FreeSansBold12pt7b
                                : &fonts::FreeSansBold18pt7b);
      if (add_targets && !(s.key == Key::Char && full)) {
        addKey(rect, s.key, s.param);
      }
      x += w + gap;
    }
  }
}

void drawAll() {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::clearTargets();

  // CANCEL discards everything typed, so it sits as far from SPACE as the
  // screen allows — a stray thumb by the spacebar must not throw the name away.
  const Rect cancel{kMargin, 16, 160, 54};
  drawButton(cancel, "CANCEL", kSurface, kMuted, &fonts::FreeSansBold12pt7b);
  addKey(cancel, Key::Cancel);

  drawLabel(g_title.c_str(), kW / 2, 30, kMuted, &fonts::FreeSans12pt7b,
            top_center);

  drawField();
  addKey(clearRect(), Key::Clear);
  drawKeys(true);
}

}  // namespace

void open(const char *title, const std::string &initial, size_t max_len) {
  g_title = title ? title : "";
  g_text = initial;
  g_max = max_len;
  g_shift = initial.empty();
  g_symbols = false;
  g_active = true;
  g_accepted = false;
  g_redraw_field = g_redraw_keys = false;
  g_dirty = true;
}

bool active() { return g_active; }
bool accepted() { return g_accepted; }
const std::string &text() { return g_result; }
void invalidate() { g_dirty = true; }

void tick(uint32_t now_ms) {
  (void)now_ms;
  if (!g_active || !g_dirty) return;

  const uint32_t t0 = micros();
  if (g_redraw_field || g_redraw_keys) {
    if (g_redraw_field) drawField();
    if (g_redraw_keys) drawKeys(false);
  } else {
    drawAll();
  }
  uikit::present();
  uikit::noteRepaint(micros() - t0, 97);

  g_dirty = false;
  g_redraw_field = g_redraw_keys = false;
}

Result handleTap(int x, int y) {
  if (!g_active) return Result::None;

  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return Result::None;

  switch ((Key)action) {
    case Key::Char:
      if (g_text.size() < g_max) {
        const bool shift_was = g_shift;
        char c = (char)param;
        if (g_shift && c >= 'a' && c <= 'z') c = (char)toupper(c);
        g_text.push_back(c);
        // One-shot shift, phone-style; holding the mode would mean tapping
        // SHIFT before every letter.
        if (c != ' ') g_shift = false;
        audio::select();
        g_redraw_field = true;
        g_redraw_keys = (shift_was != g_shift) || g_text.size() >= g_max;
      }
      break;
    case Key::Backspace:
      if (!g_text.empty()) {
        const bool was_full = g_text.size() >= g_max;
        g_text.pop_back();
        audio::select();
        g_redraw_field = true;
        g_redraw_keys = was_full;  // leaving "full" re-enables the keys
      }
      break;
    case Key::Clear:
      if (!g_text.empty()) {
        const bool was_full = g_text.size() >= g_max;
        g_text.clear();
        audio::skip();
        g_redraw_field = true;
        g_redraw_keys = was_full;
      }
      break;
    case Key::Shift:
      g_shift = !g_shift;
      audio::select();
      g_redraw_keys = true;  // labels change case; nothing else moves
      break;
    case Key::Layer:
      g_symbols = !g_symbols;
      audio::select();
      break;  // whole layer changes, targets included — full redraw
    case Key::Done: {
      std::string name = g_text;
      while (!name.empty() && name.front() == ' ') name.erase(name.begin());
      while (!name.empty() && name.back() == ' ') name.pop_back();
      if (name.empty()) {
        // Nothing to accept; treat DONE as a no-op rather than storing blank.
        audio::skip();
        break;
      }
      g_result = name;
      g_accepted = true;
      g_active = false;
      audio::correct();
      return Result::Closed;
    }
    case Key::Cancel:
      g_accepted = false;
      g_active = false;
      audio::select();
      return Result::Closed;
    case Key::None:
      break;
  }

  g_dirty = true;
  return Result::None;
}

}  // namespace textentry
}  // namespace tabulous
