#include "theme.h"

// Included here and nowhere else: each header holds a static bitmap array, so
// including them from theme.h would put a copy in every translation unit that
// draws anything.
#include "font_anton32.h"
#include "font_anton44.h"
#include "font_anton64.h"
#include "font_anton96.h"

namespace tabulous {
namespace theme {

uint16_t kBg, kSurface, kSurfaceLift, kText, kMuted, kAccent, kDanger, kGood;
uint16_t kInk = rgb(0x0B0D10);
uint16_t kOnFill = rgb(0xFFFFFF);

namespace {
bool g_light = false;
}  // namespace

// Light mode is not the dark palette inverted. Outdoors the problem is
// contrast, so the light theme uses a near-white ground with genuinely dark
// text, and darkens the accent colours that only read well against black.
void setLight(bool light) {
  g_light = light;
  if (light) {
    kBg = rgb(0xF3F4F6);
    kSurface = rgb(0xE3E6EB);
    kSurfaceLift = rgb(0xCFD5DD);
    kText = rgb(0x14181D);
    kMuted = rgb(0x55606E);
    kAccent = rgb(0xA96A00);
    kDanger = rgb(0xB3242A);
    kGood = rgb(0x18734A);
  } else {
    kBg = rgb(0x0E1116);
    kSurface = rgb(0x1A1F27);
    kSurfaceLift = rgb(0x252C37);
    kText = rgb(0xF5F7FA);
    kMuted = rgb(0x8892A0);
    kAccent = rgb(0xFFC53D);
    kDanger = rgb(0xE5484D);
    kGood = rgb(0x30A46C);
  }
}

bool isLight() { return g_light; }

// Largest first. Anything longer than a few words drops a step or two; the
// longest phrases in the shipped packs land on the bottom rung.
//
// Anton is condensed, so each step fits roughly a third more characters per
// line than the old face did at the same cap height — which means fewer
// phrases fall to a smaller step at all.
const FontStep kPhraseLadder[] = {
    {&fontdata::Anton96, 1},
    {&fontdata::Anton64, 1},
    {&fontdata::Anton44, 1},
    {&fontdata::Anton32, 1},
};
const size_t kPhraseLadderLen = sizeof(kPhraseLadder) / sizeof(kPhraseLadder[0]);

namespace {

std::vector<std::string> splitWords(const std::string &text) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') i++;
    const size_t start = i;
    while (i < text.size() && text[i] != ' ') i++;
    if (i > start) words.push_back(text.substr(start, i - start));
  }
  return words;
}

// Greedy wrap at the current font/size. Returns false if any single word is
// too wide to fit on a line of its own, or if it needs more than max_lines.
bool wrapAtCurrentFont(LovyanGFX &gfx, const std::string &text, int max_w,
                       int max_lines, std::vector<std::string> *out) {
  out->clear();
  const std::vector<std::string> words = splitWords(text);
  if (words.empty()) return true;

  std::string line;
  for (const std::string &word : words) {
    if (gfx.textWidth(word.c_str()) > max_w) return false;  // unbreakable
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (gfx.textWidth(candidate.c_str()) <= max_w) {
      line = candidate;
      continue;
    }
    out->push_back(line);
    line = word;
    if ((int)out->size() >= max_lines) return false;
  }
  if (!line.empty()) out->push_back(line);
  return (int)out->size() <= max_lines;
}

}  // namespace

void drawFitted(LovyanGFX &gfx, const std::string &text, int cx, int cy,
                int max_w, int max_h, const FontStep *ladder, size_t ladder_len,
                uint16_t color) {
  if (text.empty() || ladder_len == 0) return;

  std::vector<std::string> lines;
  const FontStep *chosen = &ladder[ladder_len - 1];
  int line_h = 0;

  for (size_t i = 0; i < ladder_len; i++) {
    gfx.setFont(ladder[i].font);
    gfx.setTextSize(ladder[i].size);
    const int h = gfx.fontHeight();
    // Allow up to three lines, but only as many as actually fit vertically.
    const int max_lines = h > 0 ? (max_h / h) : 1;
    if (max_lines < 1) continue;
    if (wrapAtCurrentFont(gfx, text, max_w, max_lines > 3 ? 3 : max_lines,
                          &lines)) {
      chosen = &ladder[i];
      line_h = h;
      break;
    }
  }

  // Nothing in the ladder fit — fall back to the smallest step and let it wrap
  // as far as it can rather than drawing nothing at all.
  if (lines.empty()) {
    gfx.setFont(chosen->font);
    gfx.setTextSize(chosen->size);
    line_h = gfx.fontHeight();
    wrapAtCurrentFont(gfx, text, max_w, 3, &lines);
    if (lines.empty()) lines.push_back(text);
  } else {
    gfx.setFont(chosen->font);
    gfx.setTextSize(chosen->size);
  }

  gfx.setTextDatum(middle_center);
  gfx.setTextColor(color);
  const int total_h = line_h * (int)lines.size();
  int y = cy - total_h / 2 + line_h / 2;
  for (const std::string &line : lines) {
    gfx.drawString(line.c_str(), cx, y);
    y += line_h;
  }
  gfx.setTextSize(1);
}

}  // namespace theme
}  // namespace tabulous
