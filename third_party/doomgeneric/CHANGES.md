# Local changes to doomgeneric

Upstream: https://github.com/ozkl/doomgeneric (GPL-2.0-or-later; the id
Software Doom source under the same terms). Only the engine sources and the
headers are vendored; the platform files for SDL, X11 and the rest are not.

The platform layer (`DG_*`) lives in the console, in `src/doom_ui.cpp`.
Everything here is upstream's, apart from:

- `dg_tabulous.h`, `dg_tabulous.c`, `dg_print.h`: added. The first two are the
  console's few hooks into the engine; the third is force-included into every
  engine file to route its printing through the console's serial path.
- `i_system.c`: under `DG_TABULOUS`, `I_Quit` hands control back through
  `DG_OnQuit` instead of running the exit functions (the engine is kept whole
  so the game can be resumed), `I_Error` unwinds through `DG_OnError` instead
  of `exit()`, and the zenity error box is compiled out.
- `m_config.c`: under `DG_TABULOUS`, the default configuration directory
  comes from `DG_ConfigDir()` (a folder on the card) rather than `.`.
- `i_sound.c`: the SDL mixer include is skipped under `DG_TABULOUS`.
- `r_plane.c`, `r_bsp.c`/`r_bsp.h`, `r_things.c`/`r_things.h`, `d_loop.c`: the
  visplanes and openings, the drawsegs, the vissprites and the tic buffer are
  allocated on first use instead of being static arrays. Together they were
  180 KB of .bss, which the chip's internal RAM cannot hold beside the rest
  of the firmware; the heap here is PSRAM for anything that size.
- `info.c`/`info.h`, `sounds.c`/`sounds.h`: the state, thing, sound and music
  tables are `const` templates in flash (`*_rom`), copied into RAM by
  `dg_alloc_tables()` before `D_DoomMain`. As initialised data they were 46 KB
  of internal RAM. The sound links are re-pointed into the copy.
- `r_main.c`/`r_main.h`/`r_state.h`: `viewangletox`, `xtoviewangle`,
  `scalelight` and `zlight` are allocated at the top of `R_Init` (21 KB).
- `r_draw.c`: `MAXWIDTH`/`MAXHEIGHT` are the screen's own size, not 1120x832.
- `statdump.c`: `MAX_CAPTURES` 32 -> 2.
- `doomgeneric.c`: calls `dg_alloc_tables()` under `DG_TABULOUS`.
- `r_plane.c`/`r_plane.h`: the ten per-column and per-row arrays join the
  heap allocation in `R_AllocPlanes`; `p_maputl.c`/`p_local.h`: `intercepts`
  allocated in `P_PathTraverse`; `r_things.c`/`r_things.h`: `negonearray`
  and `screenheightarray` allocated in `R_InitSprites`; `m_config.c`: the two
  variable lists are flash templates copied to the heap by `EnsureDefaults`.

Build-time configuration, from `platformio.ini`: `CMAP256` (the engine hands
over 8-bit indexed frames and its palette), `DOOMGENERIC_RESX=320`,
`DOOMGENERIC_RESY=200` (no software scaling; the hardware scaler does it).
