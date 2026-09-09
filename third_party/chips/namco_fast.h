/* The Pac-Man board with an instruction-stepped CPU.
 *
 * chips' own namco_exec() steps its Z80 one clock at a time, which is exact
 * and costs about 30 ms for each 16.7 ms of machine time on this hardware --
 * half speed. This runs the same board, with the same video decode, sound
 * chip and bus map, but executes whole instructions and then advances the
 * hardware by however many cycles they took. */
#ifndef TABULOUS_NAMCO_FAST_H_
#define TABULOUS_NAMCO_FAST_H_

#include "../z80/z80_sz.h"
#include "namco.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call after namco_init(). Attaches the CPU to the board's bus. */
void namco_fast_init(namco_t* sys, sz80* cpu);

/* Runs the board for a stretch of machine time. */
void namco_fast_exec(namco_t* sys, sz80* cpu, uint32_t micro_seconds);

#ifdef __cplusplus
}
#endif

#endif
