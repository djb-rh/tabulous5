// The console's view into the engine: a few globals read through functions,
// so the C++ side never includes an engine header.
#include "dg_tabulous.h"

#include <stdlib.h>
#include <string.h>

#include "d_player.h"
#include "info.h"
#include "sounds.h"
#include "doomstat.h"
#include "i_video.h"
#include "m_config.h"
#include "m_controls.h"

void dg_alloc_tables(void) {
  if (states) return;
  states = malloc(sizeof(state_t) * NUMSTATES);
  memcpy(states, states_rom, sizeof(state_t) * NUMSTATES);
  mobjinfo = malloc(sizeof(mobjinfo_t) * NUMMOBJTYPES);
  memcpy(mobjinfo, mobjinfo_rom, sizeof(mobjinfo_t) * NUMMOBJTYPES);
  S_music = malloc(sizeof(musicinfo_t) * NUMMUSIC);
  memcpy(S_music, S_music_rom, sizeof(musicinfo_t) * NUMMUSIC);
  S_sfx = malloc(sizeof(sfxinfo_t) * NUMSFX);
  memcpy(S_sfx, S_sfx_rom, sizeof(sfxinfo_t) * NUMSFX);
  // A sound that links to another does so by pointer into the template;
  // point it into the copy, which is the one that gets written to.
  for (int i = 0; i < NUMSFX; i++) {
    if (S_sfx[i].link) S_sfx[i].link = S_sfx + (S_sfx[i].link - S_sfx_rom);
  }
}

int dg_ready_weapon(void) { return (int)players[consoleplayer].readyweapon; }

void dg_set_autorun(void) { joybspeed = 29; }

void dg_save_defaults(void) { M_SaveDefaults(); }

int dg_palette_changed(void) {
  if (!palette_changed) return 0;
  palette_changed = 0;
  return 1;
}

const unsigned char *dg_palette(void) { return (const unsigned char *)colors; }
