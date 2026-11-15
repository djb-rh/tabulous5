// A picture of the console's own gamepad with each button's name on it.
//
// The pad is a copy of a SNES one and its caps carry no legends, while every
// About page talks about A, B, X, Y, L and R by name. This is the key: the
// pad as it sits under the screen, drawn to scale from the board's own
// numbers, so a button is found by where it is.
#pragma once

namespace tabulous {
namespace pad_picture {

constexpr int kWidth = 484, kHeight = 218;

// Its top-left corner. Draws in the current theme's colours.
void draw(int x, int y);

}  // namespace pad_picture
}  // namespace tabulous
