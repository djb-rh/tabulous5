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
#include <string.h>

#include "namco_fast.h"

/* Everything the bus callbacks need. The console plays one game at a time, so
 * there is one of these, and the CPU is handed a pointer to it. */
typedef struct {
    namco_t* sys;
    namco_fast_desc_t desc;
    /* The Ms. Pac-Man kit's current bank: 0 is the Pac-Man program the board
     * came with, 1 is the kit's own. */
    const uint8_t* cur_low;
    const uint8_t* cur_high;
    /* One byte per 256-byte page, so the common case is a single load: only
     * the seven pages holding a switching window are ever looked at twice. */
    uint8_t page_switches[256];
} _fast_board_t;

/* Where the kit watches. Touching any of these, reading or writing, switches
 * the program; the last one switches to the kit's code and the rest back to
 * Pac-Man's. Both banks answer at 0x0000 and 0x8000. */
typedef struct { uint16_t first, last; uint8_t bank; } _fast_window_t;
static const _fast_window_t _fast_mspacman_windows[] = {
    {0x0038, 0x003F, 0}, {0x03B0, 0x03B7, 0}, {0x1600, 0x1607, 0},
    {0x2120, 0x2127, 0}, {0x3FF0, 0x3FF7, 0}, {0x8000, 0x8007, 0},
    {0x97F0, 0x97F7, 0}, {0x3FF8, 0x3FFF, 1},
};

static _fast_board_t _fast_board;

/* The plain board decodes 15 address lines, so the top half of the address
 * space mirrors the bottom. A board with ROM at 0x8000 decodes all 16, and
 * answers there instead of mirroring. */
/* Switch banks if this address is one the kit watches. Cheap enough to call on
 * every access: nearly always one indexed byte load that says "no". */
static void _fast_watch(_fast_board_t* board, uint16_t addr) {
    if (!board->page_switches[addr >> 8]) {
        return;
    }
    for (size_t i = 0; i < sizeof(_fast_mspacman_windows) / sizeof(_fast_window_t); i++) {
        const _fast_window_t* w = &_fast_mspacman_windows[i];
        if (addr >= w->first && addr <= w->last) {
            if (w->bank == 0) {
                board->cur_low = board->sys->rom_cpu;
                board->cur_high = board->sys->rom_cpu + 0x4000;
            } else {
                board->cur_low = board->desc.rom_alt_low;
                board->cur_high = board->desc.rom_alt_high;
            }
            return;
        }
    }
}

/* The kit's board decodes all sixteen address lines: the program answers at
 * 0x0000 and 0x8000, and everything else is the usual map mirrored into the
 * gaps. */
static uint8_t _fast_kit_rd(_fast_board_t* board, uint16_t addr) {
    _fast_watch(board, addr);
    if (addr < 0x4000) return board->cur_low[addr];
    if (addr >= 0x8000 && addr < 0xC000) return board->cur_high[addr - 0x8000];
    return 0xFF;  /* caller handles the RAM and IO half */
}

static uint8_t _fast_rd(void* ud, uint16_t addr) {
    _fast_board_t* board = (_fast_board_t*)ud;
    namco_t* sys = board->sys;
    if (board->desc.rom_alt_low != 0) {
        if (addr < 0x4000 || (addr >= 0x8000 && addr < 0xC000)) {
            return _fast_kit_rd(board, addr);
        }
        _fast_watch(board, addr);
        addr &= (uint16_t)~0xA000;  /* RAM and IO mirror into the gaps */
    }
    if (board->desc.rom_high_bytes != 0 && addr >= 0x8000) {
        const uint32_t high = (uint32_t)addr - 0x8000u;
        return high < board->desc.rom_high_bytes ? board->desc.rom_high[high] : 0xFF;
    }
    addr &= NAMCO_ADDR_MASK;
    if (addr < NAMCO_IOMAP_BASE) {
        return mem_rd(&sys->mem, addr);
    }
    /* The board decodes six address lines here, so each port answers over a
     * 64-byte span rather than at one address. Several games read them at the
     * mirrors -- Paint Roller reads its DIP switches high in the range, and
     * with only the base address answering it saw every switch set and sat in
     * its power-on test for ever. */
    switch (addr & ~0x3F) {
        case NAMCO_ADDR_IN0: return ~sys->in0;
        case NAMCO_ADDR_IN1: return ~sys->in1;
        case NAMCO_ADDR_DSW1: return sys->dsw1;
        default: return 0xFF;
    }
}

static void _fast_wr(void* ud, uint16_t addr, uint8_t data) {
    _fast_board_t* board = (_fast_board_t*)ud;
    namco_t* sys = board->sys;
    if (board->desc.rom_alt_low != 0) {
        _fast_watch(board, addr);
        if (addr < 0x4000 || (addr >= 0x8000 && addr < 0xC000)) {
            return;  /* ROM either way round */
        }
        addr &= (uint16_t)~0xA000;
    } else if (board->desc.rom_high_bytes != 0 && addr >= 0x8000) {
        return;  /* ROM up there, and nothing else is decoded */
    }
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
 * when the frame interrupt fires. On a few boards a PAL sits in between and
 * rewrites some of the values on their way through. */
static void _fast_out(sz80* cpu, uint8_t port, uint8_t data) {
    _fast_board_t* board = (_fast_board_t*)cpu->userdata;
    if (port != 0) {
        return;
    }
    for (uint8_t i = 0; i < board->desc.vector_count; i++) {
        if (board->desc.vector_from[i] == data) {
            data = board->desc.vector_to[i];
            break;
        }
    }
    board->sys->int_vector = data;
}

void namco_fast_init(namco_t* sys, sz80* cpu, const namco_fast_desc_t* desc) {
    _fast_board.sys = sys;
    if (desc != 0) {
        _fast_board.desc = *desc;
    } else {
        memset(&_fast_board.desc, 0, sizeof(_fast_board.desc));
    }
    if (_fast_board.desc.rom_high == 0) {
        _fast_board.desc.rom_high_bytes = 0;
    }
    if (_fast_board.desc.vector_count > NAMCO_FAST_MAX_VECTOR_FIXUPS) {
        _fast_board.desc.vector_count = NAMCO_FAST_MAX_VECTOR_FIXUPS;
    }
    memset(_fast_board.page_switches, 0, sizeof(_fast_board.page_switches));
    if (_fast_board.desc.rom_alt_low != 0) {
        for (size_t i = 0; i < sizeof(_fast_mspacman_windows) / sizeof(_fast_window_t); i++) {
            const _fast_window_t* w = &_fast_mspacman_windows[i];
            for (uint32_t p = (uint32_t)w->first >> 8; p <= (uint32_t)w->last >> 8; p++) {
                _fast_board.page_switches[p] = 1;
            }
        }
        /* A board with the kit fitted starts on the kit's code: that is what
         * the reset vector at 0x0000 has to come from. */
        _fast_board.cur_low = _fast_board.desc.rom_alt_low;
        _fast_board.cur_high = _fast_board.desc.rom_alt_high;
    }
    sz80_init(cpu);
    cpu->userdata = &_fast_board;
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
