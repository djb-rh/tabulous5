# Changes to quakegeneric for Tabulous5

The source in `source/` is [quakegeneric](https://github.com/erysdren/quakegeneric)
(id Software's Quake engine behind a small platform seam, GPL-2.0-or-later;
see `LICENSE`), as carried in Espressif's
[esp32-quake](https://github.com/espressif/esp32-quake) for the ESP32-P4 and
then in GOB52's [M5StackTab5-quake](https://github.com/GOB52/M5StackTab5-quake),
which is where this copy came from (commit 5514a39, 2026-11-14). Everything
Espressif changed for the P4 is kept. What Tabulous5 changed on top:

* `qg_extram.h`, `qg_extram.c` (new) — the engine's big tables in the external
  RAM. Espressif marks seventeen statics `EXT_RAM_BSS_ATTR`, half a megabyte
  between them, and builds with `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`.
  The Arduino core has that option off, so the attribute leaves them in
  internal memory and the firmware does not link. The header makes each name a
  dereferenced pointer (`#define cl_entities (*cl_entities_qgp)`) before any
  declaration is read, so `extern entity_t cl_entities[MAX_EDICTS];` itself
  declares a pointer to that array and every use still means the array:
  indexing, `sizeof`, all of it. `QG_ExtRamInit()` fills the pointers in from
  the heap. Four more tables that Espressif left internal go the same way
  (`cl_static_entities`, `cl_temp_entities`, `cl_efrags`, `cl_lightstyle`,
  50 KB). The header also turns on `-O3` for the engine.
* `quakedef.h` — includes `qg_extram.h` first.
* Each file that defines one of those tables (`cl_main.c`, `cl_tent.c`,
  `d_modech.c`, `draw.c`, `keys.c`, `model.c`, `pr_exec.c`, `r_draw.c`,
  `r_sky.c`, `snd_mix.c`, `sv_main.c`) — a `QG_ExtRam_<file>()` at the end that
  allocates them. Nothing else in those files changed.
* `quakegeneric.h` — 320x200, the size the game was drawn for (Espressif: 512x300).
* `vid_null.c` — the depth buffer and the two frame pages come from
  `QG_FastAlloc()`, which the platform answers with internal memory when it
  has a block that size.
* `sys_null.c` — `Sys_Error` and `Sys_Printf` go out through `QG_Error()` and
  `QG_Print()`, so the platform has the reason the engine stopped and the
  console's text reaches the serial port.
* `net.h` — `<sys/socket.h>` for `<socket.h>`.
* `snd_tabulous.c` (new) — the sound device: a ring of 16-bit mono samples at
  11025 Hz that the engine mixes into, and `QG_AudioPump()`, which the
  platform's audio task calls to hand the speaker the next block.
* `qg_defaults.c` (new) — this console's first-run settings: the brightness
  (`gamma 0.7`), applied once and remembered in an archived variable,
  `tab5_defaults`, so the Options slider is the player's from then on. Also
  `QG_InMenu()`, by which the platform writes `config.cfg` when the menu closes.
* Not built: `net_bsd.c`, `net_udp.c`, `net_dgrm.c` (there is no network play;
  `net_none.c` and the loopback driver are used), and the other platforms'
  `quakegeneric_*.c`.

The platform side is `src/quake_ui.cpp`.
