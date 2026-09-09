/* The Pac-Man board with an instruction-stepped CPU.
 *
 * chips' own namco_exec() steps its Z80 one clock at a time, which is exact
 * and costs about 30 ms for each 16.7 ms of machine time on this hardware --
 * half speed. This runs the same board, with the same video decode, sound
 * chip and bus map, but executes whole instructions and then advances the
 * hardware by however many cycles they took.
 *
 * It also covers two things the plain board does not have, because several of
 * the games that ran on it do:
 *
 *   - the 48K memory map, where a second set of program ROMs answers at
 *     0x8000. Ms. Pac-Man and Ponpoko both need it.
 *   - a PAL between the program and the interrupt vector latch, which rewrites
 *     a couple of the values written to it. Piranha and Naughty Mouse need it.
 *   - the Ms. Pac-Man kit, an add-on ROM board that sits between the Z80 and
 *     the program ROMs and swaps the whole program for its own encrypted copy.
 *     It watches the address bus: touching any of eight small windows in the
 *     code switches banks, and the byte the CPU gets back is the new bank's.
 *     That is how Ms. Pac-Man hides from a board that only knows Pac-Man.
 */
#ifndef TABULOUS_NAMCO_FAST_H_
#define TABULOUS_NAMCO_FAST_H_

#include "../z80/z80_sz.h"
#include "namco.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAMCO_FAST_MAX_VECTOR_FIXUPS (4)

/* What this particular board has that the plain one does not. All zero is the
 * plain Pac-Man board. */
typedef struct {
    /* Program ROM at 0x8000 upwards, as whole 4K chips, or NULL for none. */
    const uint8_t* rom_high;
    uint32_t rom_high_bytes;
    /* Vector bytes the PAL rewrites, written value then delivered value. */
    uint8_t vector_count;
    uint8_t vector_from[NAMCO_FAST_MAX_VECTOR_FIXUPS];
    uint8_t vector_to[NAMCO_FAST_MAX_VECTOR_FIXUPS];
    /* The Ms. Pac-Man kit: a second, encrypted copy of the whole program that
     * the add-on board swaps in and out by watching the address bus. Both
     * copies are 16K at 0x0000 and 16K at 0x8000; rom_alt_low being non-NULL
     * is what turns the board on. */
    const uint8_t* rom_alt_low;
    const uint8_t* rom_alt_high;
} namco_fast_desc_t;

/* Call after namco_init(). Attaches the CPU to the board's bus. desc may be
 * NULL for the plain board. Whatever it points at must outlive the machine. */
void namco_fast_init(namco_t* sys, sz80* cpu, const namco_fast_desc_t* desc);

/* Runs the board for a stretch of machine time. */
void namco_fast_exec(namco_t* sys, sz80* cpu, uint32_t micro_seconds);

#ifdef __cplusplus
}
#endif

#endif
