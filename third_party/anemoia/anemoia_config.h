#ifndef ANEMOIA_CONFIG_H
#define ANEMOIA_CONFIG_H

// Replaces upstream's config.h, which selects between reference boards and
// defines TFT, SD and GPIO-controller pins — none of which apply here. This
// device drives a MIPI-DSI panel through M5GFX and reads an on-screen pad.
//
// Only what the core actually reads is kept.

#ifndef VIDEO_STANDARD
#define VIDEO_STANDARD 1  // 0 = PAL, 1 = NTSC

// Upstream marks the per-cycle functions IRAM_ATTR. That is ~99 KB of code in
// internal instruction RAM, and linking it stopped this firmware booting at
// all — so it is switchable here rather than baked in. Empty = run from flash.
#ifndef ANEMOIA_IRAM
#define ANEMOIA_IRAM
#endif

#endif

// COMPOSITE_VIDEO is deliberately left undefined: there is no composite output
// on this hardware, and defining it changes how the PPU emits pixels.

#endif
