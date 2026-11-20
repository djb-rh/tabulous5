/* Host tests for the clip-on Tab5 Keyboard's decoding (boards/m5stack_tab5/board/tab5kbd.h, launcher patch 5):
 *     tests/run.sh */
#include <stdio.h>
#include "tab5kbd.h"      /* from the patched launcher: boards/m5stack_tab5/board (tests/run.sh passes the path) */

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)
#define EV(down, row, col) ((uint8_t)(((down) ? 0x80 : 0) | ((row) << 4) | (col)))

static tab5kbd_t k;
static uint32_t t;

/* Press and release a key; the reports that came out, in order. */
static int tap(int row, int col, tab5kbd_report_t out[4])
{
    tab5kbd_event(&k, EV(1, row, col), t += 30);
    tab5kbd_event(&k, EV(0, row, col), t += 30);
    int n = 0;
    while (n < 4 && tab5kbd_take(&k, &out[n])) n++;
    return n;
}

int main(void)
{
    tab5kbd_report_t r[4];
    tab5kbd_reset(&k);
    /* a plain letter: down with no Shift, then up (empty report) */
    CHECK(tap(2, 1, r) == 2 && r[0].keys[0] == 0x14 && r[0].mods == 0 && r[1].keys[0] == 0);
    /* a press and release between two reads both come out: nothing is lost to the poll */
    CHECK(tap(4, 13, r) == 2 && r[0].keys[0] == 0x2C);
    /* ! is printed on the second row: Shift+1 */
    CHECK(tap(1, 1, r) == 2 && r[0].keys[0] == 0x1E && r[0].mods == TAB5KBD_MOD_LSHIFT);
    /* Aa held is Shift */
    tab5kbd_event(&k, EV(1, 3, 1), t += 30);
    while (tab5kbd_take(&k, &r[0])) {}
    CHECK(tap(2, 2, r) >= 1 && r[0].keys[0] == 0x1A && (r[0].mods & TAB5KBD_MOD_LSHIFT));
    tab5kbd_event(&k, EV(0, 3, 1), t += 1000);            /* held long: Shift, not a tap */
    while (tab5kbd_take(&k, &r[0])) {}
    CHECK(!k.caps);
    /* Sym: 1 is F1, the arrows page */
    tab5kbd_event(&k, EV(1, 3, 0), t += 30);
    while (tab5kbd_take(&k, &r[0])) {}
    CHECK(tap(0, 1, r) >= 1 && r[0].keys[0] == 0x3A);
    CHECK(tap(3, 11, r) >= 1 && r[0].keys[0] == 0x4B);
    /* Sym+Alt is GUI */
    tab5kbd_event(&k, EV(1, 4, 1), t += 30);
    CHECK(tab5kbd_take(&k, &r[0]) && (r[0].mods & TAB5KBD_MOD_LGUI) && !(r[0].mods & TAB5KBD_MOD_LALT));
    tab5kbd_event(&k, EV(0, 4, 1), t += 30);
    tab5kbd_event(&k, EV(0, 3, 0), t += 30);
    while (tab5kbd_take(&k, &r[0])) {}
    /* two quick taps of Aa: Caps Lock, which shifts letters and only letters */
    tab5kbd_event(&k, EV(1, 3, 1), t += 1000); tab5kbd_event(&k, EV(0, 3, 1), t += 50);
    tab5kbd_event(&k, EV(1, 3, 1), t += 100);  tab5kbd_event(&k, EV(0, 3, 1), t += 50);
    while (tab5kbd_take(&k, &r[0])) {}
    CHECK(k.caps);
    CHECK(tap(2, 1, r) >= 1 && (r[0].mods & TAB5KBD_MOD_LSHIFT));
    CHECK(tap(0, 1, r) >= 1 && !(r[0].mods & TAB5KBD_MOD_LSHIFT));
    /* Caps Lock with Aa held: lower case */
    tab5kbd_event(&k, EV(1, 3, 1), t += 1000);
    while (tab5kbd_take(&k, &r[0])) {}
    CHECK(tap(2, 1, r) >= 1 && !(r[0].mods & TAB5KBD_MOD_LSHIFT));
    tab5kbd_event(&k, EV(0, 3, 1), t += 1000);
    while (tab5kbd_take(&k, &r[0])) {}
    /* two keys held: both in the report; unplugged: everything comes up */
    tab5kbd_event(&k, EV(1, 2, 1), t += 30); tab5kbd_event(&k, EV(1, 2, 2), t += 30);
    tab5kbd_report_t last = {0, {0}};
    while (tab5kbd_take(&k, &r[0])) last = r[0];
    CHECK(last.keys[0] && last.keys[1]);
    tab5kbd_release_all(&k);
    CHECK(tab5kbd_take(&k, &r[0]) && r[0].keys[0] == 0 && r[0].mods == 0);
    /* nonsense rows and columns are ignored */
    tab5kbd_event(&k, EV(1, 6, 3), t += 30); tab5kbd_event(&k, EV(1, 2, 15), t += 30);
    CHECK(!tab5kbd_take(&k, &r[0]));
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("tab5kbd: all passed\n");
    return 0;
}
