#include "pad_picture.h"

#include <M5Unified.h>

#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace pad_picture {

using namespace theme;

namespace {
// 3.2 px to the millimetre. The numbers are the gamepad board's (tab5-gamepad,
// pcb/make_board.py): the tray is 132.5 x 58.7, the D-pad's centre 25.75 in
// and 23.7 down, the face buttons' 106.75 in, select and start 62.25 in and
// 28 down. The face diamond is 26.84 x 21.16 between centres.
constexpr int kBodyW = 424, kBodyH = 188, kBodyX = 30, kBodyY = 2;
constexpr int kDpadX = 82, kDpadY = 76, kDpadSpan = 80, kDpadArm = 30;
constexpr int kFaceX = 342, kFaceY = 76, kFaceDx = 43, kFaceDy = 34, kFaceR = 17;
constexpr int kMidX = 199, kMidY = 88, kMidPitch = 48;

void face(int cx, int cy, const char *name) {
  auto &g = uikit::gfx();
  g.fillSmoothCircle(cx, cy, kFaceR, kBg);
  uikit::drawLabel(name, cx, cy + 1, kText, &fonts::FreeSansBold12pt7b, middle_center);
}
}  // namespace

void draw(int x, int y) {
  auto &g = uikit::gfx();
  const int bx = x + kBodyX, by = y + kBodyY;

  // The triggers stand on the two end walls, their faces out through them.
  g.fillSmoothRoundRect(bx - 12, by + 34, 16, 116, 6, kMuted);
  g.fillSmoothRoundRect(bx + kBodyW - 4, by + 34, 16, 116, 6, kMuted);
  uikit::drawLabel("L", x + 8, by + 92, kText, &fonts::FreeSansBold12pt7b, middle_center);
  uikit::drawLabel("R", x + kWidth - 8, by + 92, kText, &fonts::FreeSansBold12pt7b, middle_center);

  g.fillSmoothRoundRect(bx, by, kBodyW, kBodyH, 16, kSurfaceLift);

  // The D-pad: one cross, an arrowhead on each arm.
  const int dx = bx + kDpadX, dy = by + kDpadY, h = kDpadSpan / 2, a = kDpadArm / 2;
  g.fillSmoothRoundRect(dx - h, dy - a, kDpadSpan, kDpadArm, 6, kBg);
  g.fillSmoothRoundRect(dx - a, dy - h, kDpadArm, kDpadSpan, 6, kBg);
  const int t = h - 9;   // how far out the arrowheads' points are
  g.fillTriangle(dx, dy - t, dx - 7, dy - t + 10, dx + 7, dy - t + 10, kMuted);
  g.fillTriangle(dx, dy + t, dx - 7, dy + t - 10, dx + 7, dy + t - 10, kMuted);
  g.fillTriangle(dx - t, dy, dx - t + 10, dy - 7, dx - t + 10, dy + 7, kMuted);
  g.fillTriangle(dx + t, dy, dx + t - 10, dy - 7, dx + t - 10, dy + 7, kMuted);

  // Select and start: two leaning pills, their names under them.
  for (int s = -1; s <= 1; s += 2) {
    const int cx = bx + kMidX + s * kMidPitch / 2, cy = by + kMidY;
    g.drawWideLine(cx - 9, cy + 7, cx + 9, cy - 7, 6.0f, kBg);
  }
  uikit::drawLabel("SELECT", bx + kMidX - 42, by + kMidY + 34, kText, &fonts::FreeSansBold9pt7b, middle_center);
  uikit::drawLabel("START", bx + kMidX + 42, by + kMidY + 34, kText, &fonts::FreeSansBold9pt7b, middle_center);

  // The four face buttons: X on top, B underneath, Y left, A right.
  const int fx = bx + kFaceX, fy = by + kFaceY;
  face(fx, fy - kFaceDy, "X");
  face(fx, fy + kFaceDy, "B");
  face(fx - kFaceDx, fy, "Y");
  face(fx + kFaceDx, fy, "A");

  uikit::drawLabel("the pad under the screen", x + kWidth / 2, by + kBodyH + 16, kMuted,
                   &fonts::FreeSans9pt7b, middle_center);
}

}  // namespace pad_picture
}  // namespace tabulous
