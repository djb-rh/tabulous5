// The NES screen: pick a ROM, then run it.
//
// Two modes in one module because they share the pad. The picker uses the
// shell's ordinary tap dispatch; play polls raw touch, for the same reason the
// harness does — a controller needs held state and several contacts at once.
#pragma once

#include <cstdint>

namespace tabulous {
namespace nes_ui {

// Favourite ROMs: one file name per line, on the built-in filesystem so a
// card swap does not lose them. The web file manager edits this file too.
constexpr const char *kFavouritesFile = "/nes_favs.txt";

void begin();
// Walk the filesystems again on the next begin(): the list changed underneath.
void rescan();
void invalidate();
void tick(uint32_t now_ms);
void handleTap(int x, int y, uint32_t now_ms);

// True once a ROM is running, so the shell knows taps are being polled rather
// than dispatched.
bool playing();

// Record the exact samples handed to the speaker, for analysis on the host.
// The only way to tell whether a reported noise is in the samples or in the
// playback path is to listen to the samples somewhere else.
void startAudioCapture(uint32_t seconds);
bool audioCaptureReady(const int16_t **samples, uint32_t *count);
void endAudioCapture();

// Hold a controller byte (NES bit order) for a number of frames, OR-ed with
// whatever the touch pad reports. The pad bypasses tap dispatch, so this is
// the only way to press a button on it without a finger.
void injectPad(uint8_t buttons, uint32_t frames);

}  // namespace nes_ui
}  // namespace tabulous
