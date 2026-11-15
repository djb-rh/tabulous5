/*
Tabulous5: first-run settings.

id's defaults were chosen for a CRT in a dim room. On this panel the game
is too dark to read at them, so the first time a card is played here the
brightness is set to where it is comfortable. "The first time" is kept in
an archived variable of its own, which the engine writes to config.cfg
with the rest: once it is there, the Options menu's slider is the player's,
and this never touches it again.

The platform calls QG_DefaultsInit before the first frame (which is when
config.cfg is read) and QG_DefaultsApply a few frames later.

Also here, for want of a better home: QG_InMenu, by which the platform
knows when to write the options out.
*/
#include "quakedef.h"

#define QG_GAMMA 0.7f   /* the slider runs from 1.0, darkest, to 0.5 */

static cvar_t qg_defaults = {"tab5_defaults", "0", true};

void QG_DefaultsInit(void)
{
	Cvar_RegisterVariable(&qg_defaults);
}

/* Returns 1 if anything was changed, which wants writing out. */
int QG_DefaultsApply(void)
{
	if (qg_defaults.value >= 1)
		return 0;
	Cvar_SetValue("gamma", QG_GAMMA);
	Cvar_Set("tab5_defaults", "1");
	return 1;
}

/* Is the game's own menu up? The platform writes config.cfg when it closes:
   the engine only does so when it quits, and a console is switched off far
   more often than its game is quit. */
int QG_InMenu(void)
{
	return key_dest == key_menu;
}
