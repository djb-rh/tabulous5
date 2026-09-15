// One mark per game, drawn with primitives.
//
// The menu identifies games by colour, which stops working the moment two of
// them are close — and with five games on one wheel, two of them always will
// be. A mark is the identifier that keeps working: it reads before the name
// does, and it cannot collide.
//
// Deliberately not PNGs in LittleFS. Assets can go missing, cost a decode on
// every repaint, and would need the binary upload path the content editor
// doesn't have. Circles and rectangles cost neither.
#pragma once

#include <cstdint>

namespace tabulous {
namespace glyphs {

enum class Glyph : uint8_t {
  None,
  Bubble,  // PhraseCraze — say it out loud
  Head,    // FiveHead — a card held to the forehead
  Mine,    // Minesweeper
  Grid,    // Sudoku
  Cards,   // Solitaire
  Pac,     // Arcade -- Ms. Pac-Man, bow and all
  GameBoy, // the 1989 handheld
  Nes,     // the front-loader
  Snes,    // the one with the slot on top
  TicTacToe,  // Joshua -- a won board, X down the diagonal
  Doom,       // the one-eyed floating one
};

// Draws into a square of `size` at (x, y). `ink` is the mark, `ground` is the
// surface behind it — knockouts are drawn in `ground` rather than left as
// holes, so a mark can sit on any colour without a mask.
//
// Every coordinate is a fraction of 72, the size the menu uses; other sizes
// scale from it.
void draw(Glyph g, int x, int y, int size, uint16_t ink, uint16_t ground);

}  // namespace glyphs
}  // namespace tabulous
