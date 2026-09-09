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
