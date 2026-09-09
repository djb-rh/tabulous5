/* The one place the chips headers emit their implementation.
 *
 * They are C, and they use designated initialisers in an order C++ does not
 * accept, so this stays a C translation unit rather than being patched. The
 * rest of the project includes the same headers as declarations only. */
/* The memory helper exports plain names like mem_init, and lwIP exports a
 * mem_init of its own — two definitions of the same symbol, and the link
 * fails. Nothing outside this file calls them, so they are given a prefix
 * here rather than upstream being patched. */
#define mem_init chips_mem_init
#define mem_layer_rd chips_mem_layer_rd
#define mem_layer_wr chips_mem_layer_wr
#define mem_map_ram chips_mem_map_ram
#define mem_map_rom chips_mem_map_rom
#define mem_map_rw chips_mem_map_rw
#define mem_readptr chips_mem_readptr
#define mem_snapshot_onload chips_mem_snapshot_onload
#define mem_snapshot_onsave chips_mem_snapshot_onsave
#define mem_unmap_all chips_mem_unmap_all
#define mem_unmap_layer chips_mem_unmap_layer
#define mem_write_range chips_mem_write_range

#define CHIPS_IMPL
#include "chips_common.h"
#include "z80.h"
#include "clk.h"
#include "mem.h"
#include "namco.h"

/* ------------------------------------------------------------------------
 * The board, driven by an instruction-stepped CPU.
 *
 * This is the one place with both the chips implementation (so the board's
 * static video and sound code is reachable) and a second Z80. The bus decode
 * below is namco.h's, rewritten against plain read/write callbacks instead of
 * a pin mask.
 * ------------------------------------------------------------------------ */
#include "namco_fast.h"

static uint8_t _fast_rd(void* ud, uint16_t addr) {
    namco_t* sys = (namco_t*)ud;
    addr &= NAMCO_ADDR_MASK;
    if (addr < NAMCO_IOMAP_BASE) {
        return mem_rd(&sys->mem, addr);
    }
    switch (addr) {
        case NAMCO_ADDR_IN0: return ~sys->in0;
        case NAMCO_ADDR_IN1: return ~sys->in1;
        case NAMCO_ADDR_DSW1: return sys->dsw1;
        default: return 0xFF;
    }
}

static void _fast_wr(void* ud, uint16_t addr, uint8_t data) {
    namco_t* sys = (namco_t*)ud;
    addr &= NAMCO_ADDR_MASK;
    if (addr < NAMCO_IOMAP_BASE) {
        mem_wr(&sys->mem, addr, data);
        return;
    }
    if (addr == NAMCO_ADDR_INT_ENABLE) {
        sys->int_enable = data & 1;
    } else if (addr == NAMCO_ADDR_SOUND_ENABLE) {
        sys->sound_enable = data & 1;
    } else if (addr == NAMCO_ADDR_FLIP_SCREEN) {
        sys->flip_screen = data & 1;
    } else if ((addr >= NAMCO_ADDR_SOUND_BASE) && (addr < (NAMCO_ADDR_SOUND_BASE + 0x20))) {
        _namco_sound_wr(sys, addr, data);
    } else if ((addr >= NAMCO_ADDR_SPRITES_COORD) && (addr < (NAMCO_ADDR_SPRITES_COORD + 0x10))) {
        sys->sprite_coords[addr & 0xF] = data;
    }
}

static uint8_t _fast_in(sz80* cpu, uint8_t port) {
    (void)cpu; (void)port;
    return 0xFF;
}

/* The interrupt vector is latched by an OUT to port 0, and handed to the CPU
 * when the frame interrupt fires. */
static void _fast_out(sz80* cpu, uint8_t port, uint8_t data) {
    namco_t* sys = (namco_t*)cpu->userdata;
    if (port == 0) {
        sys->int_vector = data;
    }
}

void namco_fast_init(namco_t* sys, sz80* cpu) {
    sz80_init(cpu);
    cpu->userdata = sys;
    cpu->read_byte = _fast_rd;
    cpu->write_byte = _fast_wr;
    cpu->port_in = _fast_in;
    cpu->port_out = _fast_out;
}

void namco_fast_exec(namco_t* sys, sz80* cpu, uint32_t micro_seconds) {
    const uint32_t num_ticks = clk_us_to_ticks(NAMCO_CPU_CLOCK, micro_seconds);
    uint32_t done = 0;
    bool irq = false;
    while (done < num_ticks) {
        if (irq) {
            irq = false;
            sz80_gen_int(cpu, sys->int_vector);
        }
        const unsigned long before = cpu->cyc;
        sz80_step(cpu);
        uint32_t used = (uint32_t)(cpu->cyc - before);
        if (used == 0) {
            used = 4;  /* a halted CPU still burns time */
        }
        /* The board's own clock: the frame interrupt and the sound chip both
         * run off it, so they advance by exactly the cycles just spent. */
        for (uint32_t i = 0; i < used; i++) {
            sys->vsync_count--;
            if (sys->vsync_count < 0) {
                sys->vsync_count += NAMCO_VSYNC_PERIOD;
                if (sys->int_enable) {
                    irq = true;
                }
            }
            _namco_sound_tick(sys);
        }
        done += used;
    }
    _namco_decode_video(sys);
}
