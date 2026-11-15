/*
Tabulous5: the engine's big tables, in the external RAM.

Espressif's port marks seventeen of the engine's statics EXT_RAM_BSS_ATTR,
half a megabyte between them, and builds with the option that puts such
things in the PSRAM. The Arduino core this console is built on has that
option off, so the same attribute leaves them in internal memory, which is
512 KB in all: the firmware would not link.

So each of them becomes a pointer, set once from the heap, and its name
becomes that pointer dereferenced. Because the macro is in force before any
declaration is read,

    extern entity_t cl_entities[MAX_EDICTS];

declares a pointer to an array of that many entities, and every use of
cl_entities - indexing it, taking its size, passing it - means the array it
points at. No other line of the engine changes. QG_ExtRamInit() fills the
pointers in, and must run before QG_Create().
*/
#ifndef QG_EXTRAM_H
#define QG_EXTRAM_H

#include <stddef.h>

/* Tabulous5: the whole engine at -O3. The project builds at -O2; the
   renderer's inner loops are worth the extra. */
#pragma GCC optimize("O3")

#define bottommask (*bottommask_qgp)
#define bottomsky (*bottomsky_qgp)
#define cl_entities (*cl_entities_qgp)
/* Tabulous5 adds these four, which Espressif left in internal memory: fifty
   kilobytes that are read a few times a frame, and that the depth and frame
   buffers have a far better claim on. */
#define cl_static_entities (*cl_static_entities_qgp)
#define cl_temp_entities (*cl_temp_entities_qgp)
#define cl_efrags (*cl_efrags_qgp)
#define cl_lightstyle (*cl_lightstyle_qgp)
#define d_scantable (*d_scantable_qgp)
#define intsintable (*intsintable_qgp)
#define key_lines (*key_lines_qgp)
#define localstack (*localstack_qgp)
#define menu_cachepics (*menu_cachepics_qgp)
#define mod_known (*mod_known_qgp)
#define newsky (*newsky_qgp)
#define paintbuffer (*paintbuffer_qgp)
#define sintable (*sintable_qgp)
#define snd_scaletable (*snd_scaletable_qgp)
#define sv (*sv_qgp)
#define zspantable (*zspantable_qgp)

#if defined(__cplusplus)
extern "C" {
#endif
/* Provided by the platform: zeroed memory in the external RAM, or NULL. */
void *QG_ExtAlloc(size_t bytes);
/* Allocates every table above. Returns 0 if any could not be had. */
int QG_ExtRamInit(void);
#if defined(__cplusplus)
}
#endif

#endif
