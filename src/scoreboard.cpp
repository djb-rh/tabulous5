#include "scoreboard.h"

#include <M5Unified.h>

#include <cstdio>

#include "audio.h"
#include "highscores.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace scoreboard {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Act : uint8_t { None, Tab, Close };

const char *g_title = "";
const char *const *g_keys = nullptr;
const char *const *g_labels = nullptr;
int g_count = 0;
int g_tab = 0;
int g_highlight = -1;
bool g_lower_better = true;
bool g_active = false;
bool g_dirty = true;

// Times are stored in seconds; nobody wants to read 214.
void formatValue(uint32_t v, bool as_time, char *out, size_t len) {
  if (as_time) {
    snprintf(out, len, "%u:%02u", (unsigned)(v / 60), (unsigned)(v % 60));
  } else {
    snprintf(out, len, "%u", (unsigned)v);
  }
}

void drawAll() {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::clearTargets();

  drawLabel(g_title, kMargin, 28, kText, &fonts::FreeSansBold18pt7b);

  const int tab_w = (kW - 2 * kMargin - (g_count - 1) * 12) / g_count;
  for (int i = 0; i < g_count; i++) {
    const Rect r{kMargin + i * (tab_w + 12), 88, tab_w, 68};
    const bool on = (i == g_tab);
    drawButton(r, g_labels[i], on ? kAccent : kSurfaceLift,
               on ? kInk : kMuted, &fonts::FreeSansBold12pt7b);
    uikit::addTarget(r, (int)Act::Tab, i);
  }

  const highscores::Table table = highscores::load(g_keys[g_tab]);
  const int top = 180;
  const int row_h = 78;

  if (table.count == 0) {
    drawLabel("No times yet — go and set one.", kW / 2, top + 120, kMuted,
              &fonts::FreeSans12pt7b, middle_center);
  }

  for (int i = 0; i < table.count; i++) {
    const int y = top + i * row_h;
    const bool hot = (i == g_highlight);
    uikit::fillRoundRectFast(kMargin, y, kW - 2 * kMargin, row_h - 10, 14,
                             hot ? rgb(0x1E4E33) : kSurface);

    char pos[8];
    snprintf(pos, sizeof(pos), "%d", i + 1);
    drawLabel(pos, kMargin + 34, y + (row_h - 10) / 2, hot ? kGood : kMuted,
              &fonts::FreeSansBold24pt7b, middle_center);
    drawLabel(table.entries[i].name, kMargin + 90, y + (row_h - 10) / 2, kText,
              &fonts::FreeSansBold18pt7b, middle_left);

    char value[24];
    formatValue(table.entries[i].value, g_lower_better, value, sizeof(value));
    drawLabel(value, kW - kMargin - 34, y + (row_h - 10) / 2,
              hot ? kGood : kAccent, &fonts::FreeSansBold24pt7b, middle_right);
  }

  const Rect close{kW / 2 - 200, kH - 92, 400, 74};
  drawButton(close, "CLOSE", kGood, kOnFill, &fonts::FreeSansBold18pt7b);
  uikit::addTarget(close, (int)Act::Close);
}

}  // namespace

void open(const char *title, const char *const *keys,
          const char *const *labels, int count, int initial_tab,
          bool lower_is_better, int highlight_row) {
  g_title = title ? title : "HIGH SCORES";
  g_keys = keys;
  g_labels = labels;
  g_count = count;
  g_tab = (initial_tab >= 0 && initial_tab < count) ? initial_tab : 0;
  g_lower_better = lower_is_better;
  g_highlight = highlight_row;
  g_active = true;
  g_dirty = true;
}

bool active() { return g_active; }

void tick(uint32_t now_ms) {
  (void)now_ms;
  if (!g_active || !g_dirty) return;
  const uint32_t t0 = micros();
  drawAll();
  uikit::present();
  uikit::noteRepaint(micros() - t0, 95);
  g_dirty = false;
}

Result handleTap(int x, int y) {
  if (!g_active) return Result::None;
  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return Result::None;

  switch ((Act)action) {
    case Act::Tab:
      if (param != g_tab) {
        g_tab = param;
        g_highlight = -1;  // the highlight only means anything on its own tab
        audio::select();
        g_dirty = true;
      }
      break;
    case Act::Close:
      g_active = false;
      audio::select();
      return Result::Closed;
    case Act::None:
      break;
  }
  return Result::None;
}

}  // namespace scoreboard
}  // namespace tabulous
