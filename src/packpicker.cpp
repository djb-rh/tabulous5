#include "packpicker.h"

#include <M5Unified.h>

#include <cstdio>

#include "audio.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace packpicker {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::drawButton;
using uikit::drawLabel;
using uikit::gfx;

enum class Act : uint8_t { None, Toggle, All, Done };

const std::vector<Pack> *g_packs = nullptr;
std::string g_title;
uint32_t g_mask = 0xFFFFFFFF;
bool g_active = false;
bool g_dirty = true;

void addAct(const Rect &r, Act a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

int countEnabled() {
  if (!g_packs) return 0;
  int n = 0;
  for (size_t i = 0; i < g_packs->size(); i++) {
    if (g_mask & (1u << i)) n++;
  }
  return n;
}

void drawAll() {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::clearTargets();

  drawLabel(g_title.c_str(), kMargin, 30, kText, &fonts::FreeSansBold18pt7b);

  const int enabled_count = countEnabled();
  char sub[96];
  snprintf(sub, sizeof(sub), "%d of %u packs on", enabled_count,
           (unsigned)(g_packs ? g_packs->size() : 0));
  drawLabel(sub, kMargin, 76, enabled_count == 0 ? kDanger : kMuted,
            &fonts::FreeSans9pt7b);
  if (enabled_count == 0) {
    // Rather than block DONE, say plainly what will happen — a dead end with a
    // disabled button is worse than a rule you can read.
    drawLabel("none selected — all packs will be used", kW - kMargin, 76,
              kDanger, &fonts::FreeSans9pt7b, top_right);
  }

  const int cols = 2;
  const int col_w = (kW - 2 * kMargin - 24) / cols;
  const int row_h = 92;
  const int top = 110;

  for (size_t i = 0; i < (g_packs ? g_packs->size() : 0) && i < 8; i++) {
    const Pack &pack = (*g_packs)[i];
    const int c = (int)i % cols, r = (int)i / cols;
    const Rect box{kMargin + c * (col_w + 24), top + r * row_h, col_w,
                   row_h - 12};
    const bool on = (g_mask & (1u << i)) != 0;

    g.fillRoundRect(box.x, box.y, box.w, box.h, 14, on ? kSurfaceLift : kBg);
    if (!on) g.drawRoundRect(box.x, box.y, box.w, box.h, 14, kSurface);
    // A stripe in the pack's own colour, dimmed when off.
    g.fillRoundRect(box.x + 12, box.y + 12, 12, box.h - 24, 6,
                    rgb(on ? pack.meta().color : shade(pack.meta().color, 35)));

    drawLabel(pack.meta().name.c_str(), box.x + 40, box.y + box.h / 2 - 10,
              on ? kText : kMuted, &fonts::FreeSans12pt7b, middle_left);
    char count[40];
    snprintf(count, sizeof(count), "%u phrases", (unsigned)pack.size());
    drawLabel(count, box.x + 40, box.y + box.h - 22, kMuted,
              &fonts::FreeSans9pt7b, bottom_left);

    // The state has to be readable at a glance, not inferred from shading.
    drawLabel(on ? "ON" : "OFF", box.x + box.w - 24, box.y + box.h / 2,
              on ? kGood : kMuted, &fonts::FreeSansBold12pt7b, middle_right);

    addAct(box, Act::Toggle, (int)i);
  }

  const Rect all{kMargin, kH - 84, 280, 64};
  drawButton(all, enabled_count == (int)(g_packs ? g_packs->size() : 0)
                      ? "TURN ALL OFF"
                      : "TURN ALL ON",
             kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAct(all, Act::All);

  const Rect done{kW - kMargin - 280, kH - 84, 280, 64};
  drawButton(done, "DONE", kGood, kOnFill, &fonts::FreeSansBold12pt7b);
  addAct(done, Act::Done);
}

}  // namespace

void open(const char *title, const std::vector<Pack> *packs, uint32_t mask) {
  g_title = title ? title : "WORD PACKS";
  g_packs = packs;
  g_mask = mask;
  g_active = true;
  g_dirty = true;
}

bool active() { return g_active; }
uint32_t mask() { return g_mask; }

void tick(uint32_t now_ms) {
  (void)now_ms;
  if (!g_active || !g_dirty) return;
  const uint32_t t0 = micros();
  drawAll();
  uikit::present();
  uikit::noteRepaint(micros() - t0, 96);
  g_dirty = false;
}

Result handleTap(int x, int y) {
  if (!g_active) return Result::None;
  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return Result::None;

  switch ((Act)action) {
    case Act::Toggle:
      g_mask ^= (1u << param);
      audio::select();
      break;
    case Act::All: {
      const size_t n = g_packs ? g_packs->size() : 0;
      const bool all_on = countEnabled() == (int)n;
      g_mask = 0;
      if (!all_on) {
        for (size_t i = 0; i < n && i < 32; i++) g_mask |= (1u << i);
      }
      audio::select();
      break;
    }
    case Act::Done:
      g_active = false;
      audio::correct();
      return Result::Closed;
    case Act::None:
      break;
  }
  g_dirty = true;
  return Result::None;
}

}  // namespace packpicker
}  // namespace tabulous
