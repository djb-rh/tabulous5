// Putting an emulator's picture on the panel.
//
// Shared by every emulated system: they differ only in how big their screen
// is and how much it is magnified. The caller draws a frame of RGB565 into
// frame() and calls present(); everything about rotation, scaling, cache and
// the hardware scaler lives here.
//
// Two frames are kept, so the transfer of one overlaps the emulation of the
// next.
#pragma once

#include <cstdint>

namespace tabulous {
namespace emu_video {

// Where the picture sits, in the UI's landscape coordinates, so the chrome
// around it knows what space is left.
struct Geometry {
  int x = 0, y = 0, w = 0, h = 0;
};

// Resolves the panel's framebuffer and registers with the hardware scaler.
// Safe to call repeatedly; false means neither is available.
bool begin();

// Moves the picture, for a layout that does not want it centred — turned
// upright, the game goes at the top and the controls underneath.
void placeAt(int x, int y);

// Sets the source size and magnification, and allocates the frames. The
// magnification may be fractional — the hardware scaler takes it either way,
// and 2.5x is what makes a 288-line arcade picture exactly fill the panel.
// Returns false if there is no memory for the frames, or if a fractional
// magnification is asked for on a machine that has no hardware scaler.
bool configure(int src_w, int src_h, float scale);
// The same with different magnifications across and down, for a picture
// whose pixels were never square: Doom's 320x200 was drawn for a 4:3 screen.
bool configure(int src_w, int src_h, float scale_x, float scale_y);

// Frees the frames. Waits for any transfer still in flight first.
void release();

// The frame to draw into. Null before configure().
uint16_t *frame();

// Sends the current frame to the panel and turns the buffers over.
void present();

// Blocks until the panel has the last frame. Call before touching the
// buffers from outside, or freeing anything the scaler is reading.
void waitIdle();

Geometry geometry();

// False once the hardware scaler has been ruled out and the CPU is doing the
// work — the caller may then want to draw fewer frames.
bool hardware();

// Microseconds the last present() took.
uint32_t lastUs();

}  // namespace emu_video
}  // namespace tabulous
