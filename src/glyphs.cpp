#include "glyphs.h"

#include "uikit.h"

namespace tabulous {
namespace glyphs {
namespace {

// All geometry below is written against a 72 px box, which is what the menu
// draws at; f() maps it to whatever size the caller asked for.
constexpr int kRef = 72;

struct Box {
  int x, y, size;
  int f(int v) const { return v * size / kRef; }
  int px(int v) const { return x + f(v); }
  int py(int v) const { return y + f(v); }
};

void bubble(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  uikit::fillRoundRectFast(b.px(4), b.py(12), b.f(64), b.f(40), b.f(11), ink);
  // The tail. Drawn as a triangle rather than a rounded shape so it keeps a
  // point at 72 px — a rounded tail just reads as a bump.
  g.fillTriangle(b.px(20), b.py(48), b.px(20), b.py(67), b.px(38), b.py(50), ink);
  const int r = b.f(5);
  for (int i = 0; i < 3; i++) {
    g.fillCircle(b.px(24 + i * 12), b.py(32), r, ground);
  }
}

void head(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  g.fillCircle(b.px(36), b.py(44), b.f(24), ink);
  // The card sits ON the forehead, overlapping the skull, which is the whole
  // joke of the game and the only thing that separates this from a plain face.
  uikit::fillRoundRectFast(b.px(15), b.py(6), b.f(42), b.f(24), b.f(5), ground);
  g.drawRoundRect(b.px(15), b.py(6), b.f(42), b.f(24), b.f(5), ink);
  g.fillCircle(b.px(28), b.py(46), b.f(4), ground);
  g.fillCircle(b.px(44), b.py(46), b.f(4), ground);
}

void mine(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  const int t = b.f(6) < 2 ? 2 : b.f(6);
  // Four spikes as thick rectangles; the diagonals as short line runs, which
  // is cheaper than a rotated rect and indistinguishable at this size.
  g.fillRect(b.px(33), b.py(6), t, b.f(60), ink);
  g.fillRect(b.px(6), b.py(33), b.f(60), t, ink);
  for (int i = -t / 2; i <= t / 2; i++) {
    g.drawLine(b.px(16) + i, b.py(16), b.px(56) + i, b.py(56), ink);
    g.drawLine(b.px(56) + i, b.py(16), b.px(16) + i, b.py(56), ink);
  }
  g.fillCircle(b.px(36), b.py(36), b.f(18), ink);
  g.fillCircle(b.px(29), b.py(29), b.f(4), ground);  // the glint
}

void grid(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  const int t = b.f(3) < 1 ? 1 : b.f(3);
  for (int i = 0; i < b.f(4); i++) {
    g.drawRoundRect(b.px(6) + i, b.py(6) + i, b.f(60) - 2 * i, b.f(60) - 2 * i,
                    b.f(5), ink);
  }
  g.fillRect(b.px(26), b.py(6), t, b.f(60), ink);
  g.fillRect(b.px(46), b.py(6), t, b.f(60), ink);
  g.fillRect(b.px(6), b.py(26), b.f(60), t, ink);
  g.fillRect(b.px(6), b.py(46), b.f(60), t, ink);
  // Three filled cells, off the diagonal, so it reads as a part-solved puzzle
  // rather than a window frame.
  g.fillRect(b.px(9), b.py(29), b.f(15), b.f(15), ink);
  g.fillRect(b.px(49), b.py(9), b.f(15), b.f(15), ink);
  g.fillRect(b.px(29), b.py(49), b.f(15), b.f(15), ink);
  (void)ground;
}

void cards(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  // The back card is fanned. A rotated rectangle is two triangles, which is
  // cheaper than any transform and exact at this size.
  const float a = -0.26f;  // ~15 degrees
  const float cs = 0.966f, sn = -0.259f;
  const int cx = b.px(26), cy = b.py(38);
  const int hw = b.f(15), hh = b.f(22);
  int qx[4], qy[4];
  const int sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; i++) {
    const float dx = (float)(sx[i] * hw), dy = (float)(sy[i] * hh);
    qx[i] = cx + (int)(dx * cs - dy * sn);
    qy[i] = cy + (int)(dx * sn + dy * cs);
  }
  g.fillTriangle(qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], ink);
  g.fillTriangle(qx[0], qy[0], qx[2], qy[2], qx[3], qy[3], ink);
  (void)a;

  uikit::fillRoundRectFast(b.px(30), b.py(14), b.f(30), b.f(44), b.f(5), ground);
  g.drawRoundRect(b.px(30), b.py(14), b.f(30), b.f(44), b.f(5), ink);
  // A diamond pip: four triangles would be wasteful, two suffice.
  g.fillTriangle(b.px(45), b.py(24), b.px(54), b.py(36), b.px(36), b.py(36), ink);
  g.fillTriangle(b.px(45), b.py(48), b.px(54), b.py(36), b.px(36), b.py(36), ink);
}

// The arcade board this console emulates only ever had one face on it worth
// drawing. A wedge for the mouth and a bow on the head is the whole character,
// and both survive being one colour on a coloured row.
void pac(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  g.fillCircle(b.px(36), b.py(44), b.f(22), ink);
  // The mouth, cut back out. Facing right, because that is the direction she
  // is drawn in every piece of artwork the machine ever had.
  g.fillTriangle(b.px(36), b.py(44), b.px(70), b.py(28), b.px(70), b.py(60), ground);
  // The bow: two wings meeting at a knot, sat on the top left of her head.
  g.fillTriangle(b.px(22), b.py(17), b.px(8), b.py(8), b.px(9), b.py(24), ink);
  g.fillTriangle(b.px(22), b.py(17), b.px(37), b.py(7), b.px(37), b.py(23), ink);
  g.fillCircle(b.px(22), b.py(16), b.f(5), ink);
  g.fillCircle(b.px(22), b.py(16), b.f(2), ground);
}

// A tall slab, a screen in the top half, and the controls below it. At this
// size the shape alone is the whole recognition; the detail only has to not
// contradict it.
void gameboy(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  uikit::fillRoundRectFast(b.px(16), b.py(2), b.f(40), b.f(68), b.f(7), ink);
  uikit::fillRoundRectFast(b.px(22), b.py(9), b.f(28), b.f(22), b.f(3), ground);
  // The cross and the two buttons, set at the angle they actually sit at.
  const int t = b.f(4) < 1 ? 1 : b.f(4);
  g.fillRect(b.px(24), b.py(44) - t / 2, b.f(14), t, ground);
  g.fillRect(b.px(31) - t / 2, b.py(37), t, b.f(14), ground);
  g.fillCircle(b.px(45), b.py(46), b.f(4), ground);
  g.fillCircle(b.px(52), b.py(41), b.f(4), ground);
  // Start and Select, the pair of stripes low on the face.
  g.fillRect(b.px(27), b.py(59), b.f(8), b.f(3), ground);
  g.fillRect(b.px(38), b.py(59), b.f(8), b.f(3), ground);
}

// The front-loader: a wide low box with the flap across the middle of it.
void nes(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  uikit::fillRoundRectFast(b.px(3), b.py(18), b.f(66), b.f(38), b.f(4), ink);
  // The two stripes above the flap, which is the one marking anyone can place.
  g.fillRect(b.px(11), b.py(23), b.f(50), b.f(3), ground);
  g.fillRect(b.px(11), b.py(28), b.f(50), b.f(2), ground);
  // The flap itself, with its lip.
  uikit::fillRoundRectFast(b.px(11), b.py(34), b.f(50), b.f(16), b.f(2), ground);
  g.fillRect(b.px(28), b.py(45), b.f(16), b.f(3), ink);
}

// The later one: same low box, but with the cartridge slot raised on top and
// the two big buttons on the front. That is the whole difference, and it is
// enough to tell them apart in a list.
void snes(const Box &b, uint16_t ink, uint16_t ground) {
  auto &g = uikit::gfx();
  uikit::fillRoundRectFast(b.px(3), b.py(26), b.f(66), b.f(30), b.f(7), ink);
  uikit::fillRoundRectFast(b.px(20), b.py(13), b.f(32), b.f(16), b.f(4), ink);
  g.fillRect(b.px(26), b.py(17), b.f(20), b.f(4), ground);  // the slot
  // Eject and reset, side by side where the thumb finds them.
  uikit::fillRoundRectFast(b.px(11), b.py(38), b.f(11), b.f(9), b.f(2), ground);
  uikit::fillRoundRectFast(b.px(26), b.py(38), b.f(11), b.f(9), b.f(2), ground);
  g.fillRect(b.px(46), b.py(41), b.f(14), b.f(3), ground);
}

}  // namespace

void draw(Glyph glyph, int x, int y, int size, uint16_t ink, uint16_t ground) {
  const Box b{x, y, size};
  switch (glyph) {
    case Glyph::Bubble: bubble(b, ink, ground); break;
    case Glyph::Head: head(b, ink, ground); break;
    case Glyph::Mine: mine(b, ink, ground); break;
    case Glyph::Grid: grid(b, ink, ground); break;
    case Glyph::Cards: cards(b, ink, ground); break;
    case Glyph::Pac: pac(b, ink, ground); break;
    case Glyph::GameBoy: gameboy(b, ink, ground); break;
    case Glyph::Nes: nes(b, ink, ground); break;
    case Glyph::Snes: snes(b, ink, ground); break;
    case Glyph::None: break;
  }
}

}  // namespace glyphs
}  // namespace tabulous
