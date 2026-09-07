// Palette, layout constants and text helpers.
//
// The look is dark chrome with one vivid category color per screen, so the
// round screen reads as "you are in the Movies round" from across a room
// without anyone having to read a label.
#pragma once

#include <M5Unified.h>

#include <string>
#include <vector>

namespace tabulous {
namespace theme {

// 0xRRGGBB -> RGB565. Written out rather than using lgfx's helper so it is
// unambiguously constexpr and usable in constant initialisers.
constexpr uint16_t rgb(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xFF) >> 3) << 11 |
                    ((((hex >> 8) & 0xFF) >> 2) << 5) | (((hex)&0xFF) >> 3));
}

// The palette is runtime-switchable, not constant: the Tab5 is used outdoors
// and a dark UI washes out badly in sunlight. Every screen reads these, so a
// single switch re-themes the whole console.
extern uint16_t kBg;
extern uint16_t kSurface;
extern uint16_t kSurfaceLift;
extern uint16_t kText;
extern uint16_t kMuted;
extern uint16_t kAccent;
extern uint16_t kDanger;
extern uint16_t kGood;
extern uint16_t kInk;  // text on a bright category colour; dark in both themes
// Text ON a filled kGood/kDanger button. Stays light in BOTH themes: those
// fills are dark either way, so using kText (which flips to near-black in
// light mode) left dark-on-dark labels that could not be read.
extern uint16_t kOnFill;

void setLight(bool light);
bool isLight();

// Team identity colours, used as the whole round-screen background so whose
// turn it is reads across a room without anyone parsing text.
//
// Blue and orange rather than red and green: it is the pair that stays
// distinguishable for the common forms of colour blindness. Red would also
// read as an error state, which it is not.
constexpr uint32_t kTeamHex[2] = {0x1D4ED8, 0xC2410C};

// Landscape. The panel is natively 720x1280 portrait; rotation 1 turns it.
constexpr int kW = 1280;
constexpr int kH = 720;
constexpr int kMargin = 48;

// Pick black or white text for legibility on an arbitrary category color.
//
// Returns kOnFill, never kText: kText is near-BLACK in light mode, so the old
// version put black text on dark blue and dark teal the moment the theme was
// switched. The colour underneath is a fixed category colour that does not
// follow the theme, so neither should the ink on it.
//
// The choice is WCAG's own crossover rather than a luma threshold. White beats
// black exactly when relative luminance < 0.179, and squaring is a close
// enough stand-in for the sRGB gamma curve to land every category colour on
// the same side as the exact formula does.
inline uint16_t inkFor(uint32_t hex) {
  const uint32_t r = (hex >> 16) & 0xFF, g = (hex >> 8) & 0xFF, b = hex & 0xFF;
  const uint32_t lum = 2126u * r * r + 7152u * g * g + 722u * b * b;
  return lum < 116394750u ? kOnFill : kInk;  // 0.179 * 10000 * 255 * 255
}

// Slightly darkened version of a category color, for panels sitting on it.
inline uint32_t shade(uint32_t hex, uint8_t percent) {
  const uint32_t r = ((hex >> 16) & 0xFF) * percent / 100;
  const uint32_t g = ((hex >> 8) & 0xFF) * percent / 100;
  const uint32_t b = (hex & 0xFF) * percent / 100;
  return (r << 16) | (g << 8) | b;
}

// ------------------------------------------------------------------- text

// Font ladder, largest first. `size` is an integer multiplier, which is all
// M5GFX's bitmap faces support — the ladder now runs on Anton rendered at four
// real pixel sizes, so every step is size 1 and nothing is ever scaled up.
// Scaling a 24 pt face to 3x was what thickened the strokes and rounded the
// corners on the phrase screen.
struct FontStep {
  const lgfx::IFont *font;
  uint8_t size;
};

// Draw `text` centered in the box, choosing the largest font step from the
// ladder at which it fits. Used for the phrase on the round screen, where the
// text length varies wildly and must never overflow.
void drawFitted(LovyanGFX &gfx, const std::string &text, int cx, int cy,
                int max_w, int max_h, const FontStep *ladder, size_t ladder_len,
                uint16_t color);

// The standard ladder for phrases: huge down to merely large.
extern const FontStep kPhraseLadder[];
extern const size_t kPhraseLadderLen;

}  // namespace theme
}  // namespace tabulous
