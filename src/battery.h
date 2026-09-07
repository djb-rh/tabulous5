// Battery level, read sparingly.
//
// The Tab5 reads battery through an INA226 on the internal I2C bus - the same
// bus as the touch controller, where contention is the suspected cause of
// phantom-touch lockups. So this is polled ONLY on the menu, never inside a
// game, at a slow interval. It also times its own reads and gives up
// permanently if one is slow, so it can never become the thing that makes
// touch feel bad.
#pragma once

#include <cstdint>

namespace tabulous {
namespace battery {

// `allowed` gates polling: pass true only where a stall would not matter.
void update(uint32_t now_ms, bool allowed);

bool available();   // a plausible reading has been obtained
int level();        // 0-100, or -1
bool charging();
bool disabled();    // gave up because a read was too slow
uint32_t lastReadUs();

}  // namespace battery
}  // namespace tabulous
