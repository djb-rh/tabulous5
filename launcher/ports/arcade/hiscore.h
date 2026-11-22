// High scores that outlive the machine, as MAME's hiscore plugin keeps them.
//
// Each game keeps its score table in RAM; hiscore_table.h (tools/mkhiscore.py, from MAME's hiscore.dat) says where,
// and what the first and last byte of each region hold once the game has set its table up. When the game is seen
// to have done that, the scores saved last time are written over it. After that the regions are compared every few
// seconds and saved when they change, and once more when the game is left.
//
// The game is known by the CRC-32 of its ROMs as loaded, not by its file name, which is the user's to change; a file
// whose ROMs are not in the table (a home-made game, a hack) is tried by its name against the table's titles.
// Scores live in /sd/tabulous/hiscore/<set>.hi, the regions' bytes one after another.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "papp/papp.h"
#include "hiscore_table.h"

namespace hiscore {

static const HiscoreGame *g_game;
static uint8_t g_saved[512];          // what is on the card (or was injected)
static uint8_t g_now[512];
static int g_total;
static bool g_have_saved, g_active;
static int g_stable, g_tick;
static volatile bool g_save_due;      // the frame loop asks; the main thread writes
static uint8_t g_to_write[512];

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void path(char *out, size_t n) { snprintf(out, n, "/sd/tabulous/hiscore/%s.hi", g_game->set); }

// Which game: by its ROMs, else by its file's name ("/sd/roms/arcade/Pac-Man (Midway).arc").
static void identify(const uint8_t *payload, size_t len, const char *file)
{
    g_game = nullptr; g_active = false; g_stable = 0; g_tick = 0; g_total = 0; g_have_saved = false; g_save_due = false;
    const uint32_t crc = crc32(payload, len);
    for (const HiscoreGame &g : kHiscoreGames) if (g.cpu_crc == crc) { g_game = &g; break; }
    if (!g_game) {
        const char *base = strrchr(file, '/');
        base = base ? base + 1 : file;
        const size_t bl = strlen(base);
        for (const HiscoreGame &g : kHiscoreGames) {
            const size_t tl = strlen(g.title);
            if (bl == tl + 4 && !strncmp(base, g.title, tl) && !strcmp(base + tl, ".arc")) { g_game = &g; break; }
        }
    }
    if (!g_game) { papp_log("arcade: no high-score table for this game (ROM CRC %08x)\n", (unsigned)crc); return; }
    for (int i = 0; i < g_game->count; i++) g_total += g_game->r[i].len;
    if (g_total > (int)sizeof g_saved) { g_game = nullptr; return; }
    char p[96]; path(p, sizeof p);
    if (FILE *f = fopen(p, "rb")) {
        g_have_saved = fread(g_saved, 1, (size_t)g_total, f) == (size_t)g_total;
        fclose(f);
    }
    papp_log("arcade: high scores for %s (%s), %d bytes in %d regions, %s\n", g_game->set, g_game->title, g_total,
             g_game->count, g_have_saved ? "saved ones found" : "none saved yet");
}

template <typename Rd> static void readAll(Rd rd, uint8_t *out)
{
    int o = 0;
    for (int i = 0; i < g_game->count; i++)
        for (int k = 0; k < g_game->r[i].len; k++) out[o++] = rd((uint16_t)(g_game->r[i].addr + k));
}

// Once a frame, from the frame loop, with the machine stopped between frames.
template <typename Rd, typename Wr> static void frame(Rd rd, Wr wr)
{
    if (!g_game) return;
    if (!g_active) {
        bool ready = true;
        for (int i = 0; i < g_game->count && ready; i++) {
            const HiscoreRegion &r = g_game->r[i];
            ready = rd(r.addr) == r.first && rd((uint16_t)(r.addr + r.len - 1)) == r.last;
        }
        g_stable = ready ? g_stable + 1 : 0;
        if (g_stable < 30) return;                 // half a second of it, so a self-test pattern is not mistaken for it
        if (g_have_saved) {
            int o = 0;
            for (int i = 0; i < g_game->count; i++)
                for (int k = 0; k < g_game->r[i].len; k++) wr((uint16_t)(g_game->r[i].addr + k), g_saved[o++]);
            papp_log("arcade: saved high scores put back\n");
        } else {
            readAll(rd, g_saved);
        }
        g_active = true;
        return;
    }
    if (++g_tick < 300) return;                    // every five seconds
    g_tick = 0;
    readAll(rd, g_now);
    if (memcmp(g_now, g_saved, (size_t)g_total) && !g_save_due) {
        memcpy(g_saved, g_now, (size_t)g_total);
        memcpy(g_to_write, g_now, (size_t)g_total);
        g_save_due = true;
    }
}

// When the game is left: one last look, from the frame loop as it ends.
template <typename Rd> static void leaving(Rd rd)
{
    if (!g_game || !g_active) return;
    readAll(rd, g_now);
    if (memcmp(g_now, g_saved, (size_t)g_total)) {
        memcpy(g_saved, g_now, (size_t)g_total);
        memcpy(g_to_write, g_now, (size_t)g_total);
        g_save_due = true;
    }
}

// From the main thread: the card is written there.
static void writeIfDue()
{
    if (!g_save_due || !g_game) return;
    char p[96]; path(p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f && papp_fs()) { papp_fs()->mkdir("/sd/tabulous"); papp_fs()->mkdir("/sd/tabulous/hiscore"); f = fopen(p, "wb"); }
    if (f) {
        fwrite(g_to_write, 1, (size_t)g_total, f);
        fclose(f);
        papp_log("arcade: high scores saved to %s\n", p);
    }
    g_save_due = false;
}

}  // namespace hiscore
