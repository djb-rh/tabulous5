// Sound.
//
// The accelerating beep stays synthesised: its timing IS the game's tension,
// and tone() places it exactly where the round timer says. Everything else
// prefers a sample from /sfx when one is present and falls back to the
// synthesised version when it isn't, so the device is fully playable on a
// virgin filesystem and a sound can be replaced by dropping in a file.
#pragma once

#include <cstdint>

namespace tabulous {
namespace audio {

void begin(uint8_t volume);

// Loads /sfx/*.wav into PSRAM. Safe to call before or after begin(); safe to
// skip entirely, in which case every effect uses its synthesised form. Call
// again to pick up files added over WiFi.
void loadSamples();

// How many effects are playing from a file rather than a tone. Reported on the
// boot line so a filesystem that was never flashed is visible rather than just
// quietly sounding cheaper.
int sampleCount();

// Advances scheduled multi-note effects. Call every loop; without it the
// buzzer and fanfare play only their first note.
void update(uint32_t now_ms);
void setVolume(uint8_t volume);   // 0-255
void setEnabled(bool enabled);
bool enabled();

// The round timer's tick. `hz` comes from the timer's beep curve.
void beep(uint16_t hz);

// Time's up. Deliberately longer and lower than any beep so it can't be
// mistaken for one at the moment it matters most.
void buzzer();

void correct();   // a phrase was guessed, a card found its foundation
void skip();      // a phrase was skipped, a move was undone, a flag was placed
void reject();    // an illegal move or a wrong entry — blunt, not punishing
void boom();      // a mine. Deliberately not buzzer(): one is noise, one tonal
void select();    // UI tap. Fires constantly, so it is the quietest of these
void fanfare();   // match won

}  // namespace audio
}  // namespace tabulous
