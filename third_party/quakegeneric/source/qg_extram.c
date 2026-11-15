/* Tabulous5: see qg_extram.h. */
#include "qg_extram.h"

int QG_ExtRam_cl_main(void);
int QG_ExtRam_cl_tent(void);
int QG_ExtRam_d_modech(void);
int QG_ExtRam_draw(void);
int QG_ExtRam_keys(void);
int QG_ExtRam_model(void);
int QG_ExtRam_pr_exec(void);
int QG_ExtRam_r_draw(void);
int QG_ExtRam_r_sky(void);
int QG_ExtRam_snd_mix(void);
int QG_ExtRam_sv_main(void);

int QG_ExtRamInit(void)
{
	return QG_ExtRam_cl_main()
	    && QG_ExtRam_cl_tent()
	    && QG_ExtRam_d_modech()
	    && QG_ExtRam_draw()
	    && QG_ExtRam_keys()
	    && QG_ExtRam_model()
	    && QG_ExtRam_pr_exec()
	    && QG_ExtRam_r_draw()
	    && QG_ExtRam_r_sky()
	    && QG_ExtRam_snd_mix()
	    && QG_ExtRam_sv_main();
}
