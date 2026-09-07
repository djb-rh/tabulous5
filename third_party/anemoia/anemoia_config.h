#ifndef ANEMOIA_CONFIG_H
#define ANEMOIA_CONFIG_H

// Replaces upstream's config.h, which selects between reference boards and
// defines TFT, SD and GPIO-controller pins — none of which apply here. This
// device drives a MIPI-DSI panel through M5GFX and reads an on-screen pad.
//
// Only what the core actually reads is kept.

#ifndef VIDEO_STANDARD
#define VIDEO_STANDARD 1  // 0 = PAL, 1 = NTSC
#endif

// COMPOSITE_VIDEO is deliberately left undefined: there is no composite output
// on this hardware, and defining it changes how the PPU emits pixels.

#endif
