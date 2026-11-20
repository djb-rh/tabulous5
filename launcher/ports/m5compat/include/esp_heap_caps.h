// The console keeps its big buffers in PSRAM by asking for it; a launcher app's heap is PSRAM already.
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
static inline void *heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t m, unsigned) { return calloc(n, m); }
static inline void heap_caps_free(void *p) { free(p); }
