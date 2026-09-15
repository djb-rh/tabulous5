// Force-included into every C file of the engine (see platformio.ini).
//
// The engine narrates to stdout, which on this board is the UART header and
// not the USB port anyone is watching. These send it through the console's
// own serial path instead - non-blocking, so a missing host never stalls a
// frame. Real files are left alone: the config and stats writers use
// fprintf on streams they opened themselves.
#pragma once
#ifndef __cplusplus
// Thirty years of C: the engine is not going to be cleaned to this
// compiler's taste, and its noise would bury a real warning from anywhere.
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wpointer-sign"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wchar-subscripts"
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wpointer-arith"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <stdarg.h>
#include <stdio.h>

void dg_printf(const char *fmt, ...);
void dg_fprintf(void *stream, const char *fmt, ...);
void dg_vfprintf(void *stream, const char *fmt, va_list ap);
void dg_puts(const char *s);
int dg_putchar(int c);

#define printf(...) dg_printf(__VA_ARGS__)
#define fprintf(stream, ...) dg_fprintf((void *)(stream), __VA_ARGS__)
#define vfprintf(stream, fmt, ap) dg_vfprintf((void *)(stream), fmt, ap)
#define puts(s) dg_puts(s)
#define putchar(c) dg_putchar(c)
#endif
