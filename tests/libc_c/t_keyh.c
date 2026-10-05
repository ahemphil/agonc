/* t_keyh.c - <agon/mos.h>'s C handlers (handler.c, kbint.s) on the
 * emulator. A key handler sees the keys typed at the emulator ("kq"),
 * dividing in the handler while the main loop divides too: the main
 * loop's quotients stay right, so the runtime's scratch cells are saved
 * around the handler. An interrupt handler is installed on a vector that
 * never fires (nothing enables its source), its old value checked, and
 * removed again; the slots run out at four.
 */

#include <agon/mos.h>
#include "check.h"

volatile int keys;
volatile int pressed[2];  /* the first two keys pressed (Return may follow) */
volatile long junk;

void on_key(const unsigned char *packet)
{
    long a;

    if (packet[3] == 0)                 /* a release */
        return;
    if (keys < 2)
        pressed[keys] = packet[0];
    keys++;
    for (a = 1000003L; a > 7; a = a / 7)
        junk = junk + a % 13;           /* division here, as in main */
}

void on_int(void)
{
}

int main(void)
{
    unsigned long start;
    long n;
    long bad;
    void *old;
    int i;

    mos_set_key_handler(on_key);
    start = MOS_SYSVAR->time;
    bad = 0;
    n = 0;
    while (keys < 2 && MOS_SYSVAR->time - start < 1000) {   /* at most 10 s */
        n++;
        if ((n * 1000L + 17) / 1000 != n || (n * 7 + 3) % 7 != 3)
            bad++;
    }
    check(keys >= 2, 1);
    check(pressed[0], 'k');
    check(pressed[1], 'q');
    check(bad == 0, 1);
    check(n > 100, 1);                  /* the loop did run alongside */
    mos_set_key_handler(0);

    /* vector 0x38: a port D pin interrupt, never enabled here */
    old = mos_setintvector(0x38, 0);
    mos_setintvector(0x38, old);
    check(mos_set_interrupt_handler(0x38, on_int), 0);
    check(mos_setintvector(0x38, old) != old, 1);   /* the slot's entry was there */
    mos_setintvector(0x38, old);
    /* (the slot still thinks it serves 0x38: removing restores old) */
    check(mos_set_interrupt_handler(0x38, 0), 0);
    check(mos_setintvector(0x38, old) == old, 1);
    for (i = 0; i < 4; i++)
        check(mos_set_interrupt_handler(0x30 + 2 * i, on_int), 0);
    check(mos_set_interrupt_handler(0x3A, on_int), -1);   /* no fifth slot */
    return finish();                    /* __exit puts the four back */
}
