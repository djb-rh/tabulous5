/* agnes lives in third_party/ so it stays visibly close to upstream — pulling
   it in through one shim keeps PlatformIO's src filter simple.
   See third_party/agnes/iram.patch.README for the single local change.

   AGNES_HOT is deliberately left EMPTY. It was added to place the per-cycle
   functions in internal instruction RAM, on the theory that running them
   through the flash cache was the bottleneck. Measured on hardware: no
   difference whatsoever, 38 ms a frame either way — and -O2 had already
   inlined ppu_tick and cpu_tick into agnes_tick, so the hot path really did
   move and really did not care. It cost ~90 KB of IRAM for nothing, so the
   hook stays for future experiments and stays switched off.

   The cost is the PPU's per-dot work, not fetching the code that does it:
   emulation runs at 11 ms a frame before a game enables rendering and 38 ms
   after. */
#define AGNES_HOT
#include "agnes.c"
