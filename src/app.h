// The Tabulous5 shell: game menu, dispatch to the running game, and the
// confirmed exit back to the menu.
//
// Games don't know about each other or about the menu. They ask to leave via
// requestExit() and the shell handles the confirmation, so the "are you sure"
// looks and behaves the same whichever game asks.
#pragma once

#include <vector>

#include "content.h"
#include "pack.h"

namespace tabulous {
namespace app {

enum class GameId : uint8_t {
  Menu = 0,
  PhraseCraze = 1,
  FiveHead = 2,
  Minesweeper = 3,
  Sudoku = 4,
  Solitaire = 5,
  Joypad = 6,
  Nes = 7,
  GameBoy = 8,
};

void begin(std::vector<Pack> *packs, const content::LoadReport &report);

void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);
void invalidate();

// A game asking to be left. The shell puts up a confirmation first — quitting
// mid-evening by a stray tap would be worse than the extra press.
//
// Games only offer this between rounds; nothing calls it mid-round, where an
// accidental exit would throw away a turn in progress.
void requestExit();

// Shows the running game's own About text over the top of it. Games offer
// this only between rounds, for the same reason they only offer MENU there.
void showAbout();

GameId current();

// Whether the current context has any use for the accelerometer.
//
// The IMU shares the Tab5's internal I2C bus with the touch controller, and a
// phantom-touch lockup has been observed during exactly the games that gain
// nothing from orientation. Not polling it there removes that traffic
// entirely. The cost is that those games do not auto-flip.
bool wantsOrientation();

}  // namespace app
}  // namespace tabulous
