/* superzazu's Z80 under a prefix.
 *
 * It and the chips project both export z80_init, and both are needed in the
 * same translation unit: chips supplies the Pac-Man board, this supplies a
 * faster CPU. Renaming happens here rather than upstream so both stay
 * pristine. */
#ifndef TABULOUS_Z80_SZ_H_
#define TABULOUS_Z80_SZ_H_

#define z80 sz80
#define z80_init sz80_init
#define z80_step sz80_step
#define z80_gen_nmi sz80_gen_nmi
#define z80_gen_int sz80_gen_int
#define z80_debug_output sz80_debug_output

#include "z80.h"

#endif
