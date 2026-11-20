/* Host tests for the Tab5 gamepad's decoding (boards/m5stack_tab5/board/tab5pad.h):
 *     tests/run.sh */
#include <stdio.h>
#include "tab5pad.h"      /* from the patched launcher: boards/m5stack_tab5/board (tests/run.sh passes the path) */

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)
enum { UP = 1u << 16, DOWN = 1u << 17, LEFT = 1u << 18, RIGHT = 1u << 19, A = 1u << 0, B = 1u << 1, X = 1u << 2, Y = 1u << 3,
       L1 = 1u << 4, R1 = 1u << 5, SELECT = 1u << 8, START = 1u << 9 };

static int levels[64];
static int level_of(int gpio) { return levels[gpio]; }

int main(void)
{
    /* rev B: nothing held reads FF FF; each bit of the README's table, alone */
    CHECK(tab5pad_revb_buttons(0xFF, 0xFF) == 0);
    static const struct { int port, bit; unsigned want; const char *name; } t[] = {
        {1, 7, UP, "UP"}, {0, 1, DOWN, "DOWN"}, {1, 6, LEFT, "LEFT"}, {0, 0, RIGHT, "RIGHT"}, {0, 2, SELECT, "SELECT"}, {1, 5, L1, "L"},
        {1, 0, X, "X"}, {0, 6, B, "B"}, {0, 7, Y, "Y"}, {1, 1, A, "A"}, {0, 5, START, "START"}, {1, 2, R1, "R"} };
    unsigned all = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        unsigned char p0 = 0xFF, p1 = 0xFF;
        if (t[i].port == 0) p0 &= (unsigned char)~(1u << t[i].bit); else p1 &= (unsigned char)~(1u << t[i].bit);
        unsigned got = tab5pad_revb_buttons(p0, p1);
        if (got != t[i].want) { printf("FAIL rev B %s: P%d%d gave %05x, want %05x\n", t[i].name, t[i].port, t[i].bit, got, t[i].want); fails++; }
        all |= got;
    }
    CHECK(all == (UP | DOWN | LEFT | RIGHT | A | B | X | Y | L1 | R1 | SELECT | START));
    /* the four pins the board leaves unconnected (P03 P04 P13 P14) press nothing */
    CHECK(tab5pad_revb_buttons(0xFF & ~0x18, 0xFF & ~0x18) == 0);
    CHECK(tab5pad_revb_buttons(0x00, 0x00) == all);
    CHECK(tab5pad_revb_buttons(0xFF & ~0x41, 0xFF & ~0x02) == (RIGHT | B | A));

    /* rev A: the wiring file */
    int8_t g[TAB5PAD_BUTTONS];
    CHECK(tab5pad_parse("", g) == 0);
    CHECK(tab5pad_parse("# only a comment\n\n", g) == 0);
    const char *full = "# mine\nUP=G16\nDOWN=G17\nLEFT=G45\nRIGHT=G52\nA=G7\nB=G6\nX=G3\nY=G4\nL=G2\nR=G48\nSELECT=G47\nSTART=G51\n";
    CHECK(tab5pad_parse(full, g) == 12);
    CHECK(g[0] == 16 && g[1] == 17 && g[4] == 7 && g[11] == 51);
    CHECK(tab5pad_parse("up = g16   # the top one\r\n start=51\nl1=2\nR1=G48", g) == 4);
    CHECK(g[0] == 16 && g[11] == 51 && g[8] == 2 && g[9] == 48 && g[1] == TAB5PAD_NONE);
    CHECK(tab5pad_parse("UP=G16\nFIRE=G17\n", g) == -2);          /* no such button */
    CHECK(tab5pad_parse("UP=G16\nDOWN=G43\n", g) == -2);          /* the SD card's clock: not one of the twelve */
    CHECK(tab5pad_parse("UP=G16\nDOWN=G16\n", g) == -2);          /* one line, two buttons */
    CHECK(tab5pad_parse("UP\n", g) == -1);
    CHECK(tab5pad_parse("UP=\n", g) == -1);
    CHECK(tab5pad_parse("UP=G16x\n", g) == -1);
    CHECK(tab5pad_parse("UP=G1600\n", g) == -1);
    CHECK(tab5pad_parse("A=G7\nA=G6\n", g) == 1 && g[4] == 6);    /* said twice: the later one stands */

    CHECK(tab5pad_parse(full, g) == 12);
    for (int i = 0; i < 64; i++) levels[i] = 1;
    CHECK(tab5pad_reva_buttons(g, level_of) == 0);
    levels[16] = 0; levels[7] = 0; levels[51] = 0;
    CHECK(tab5pad_reva_buttons(g, level_of) == (UP | A | START));
    CHECK(tab5pad_parse("UP=G16\n", g) == 1);
    levels[7] = 0; levels[16] = 1;
    CHECK(tab5pad_reva_buttons(g, level_of) == 0);                /* a line with no button in the file presses nothing */

    printf(fails ? "%d FAILED\n" : "all tab5pad tests pass\n", fails);
    return fails != 0;
}
