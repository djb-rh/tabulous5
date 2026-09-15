// What the console adds to doomgeneric's platform seam. See CHANGES.md.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The game asked to quit (from its own menu). The engine is left whole.
void DG_OnQuit(void);
// I_Error: the message, then unwind to the console. Never returns.
void DG_OnError(const char *message) __attribute__((noreturn));
// Where default.cfg and the savegame folder live, with a trailing slash.
const char *DG_ConfigDir(void);

// Engine internals the console wants, kept in C so it needs none of the
// engine's headers: see dg_tabulous.c.
void dg_alloc_tables(void);    // RAM copies of the flash tables; before D_DoomMain
int dg_ready_weapon(void);     // 0..8, the weapon in hand
void dg_set_autorun(void);     // the joyb_speed=29 trick: always run
void dg_save_defaults(void);   // write default.cfg now
int dg_palette_changed(void);  // 1 once after a palette change, then 0
const unsigned char *dg_palette(void);  // 256 x {b,g,r,a}

#ifdef __cplusplus
}
#endif
