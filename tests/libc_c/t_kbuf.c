/* t_kbuf.c - AgonDev's key-event queue (<agon/keyboard.h>, handler.c) on
 * the emulator: the keys typed ("kq") arrive as events, oldest first, each
 * press with its ASCII code and isdown 1; after kbuf_deinit nothing more
 * is queued.
 */

#include <agon/mos.h>
#include <agon/keyboard.h>
#include "check.h"

int main(void)
{
    struct keyboard_event_t e;
    unsigned long start;
    int downs[2];
    int n;

    kbuf_init(16);
    start = MOS_SYSVAR->time;
    n = 0;
    while (n < 2 && MOS_SYSVAR->time - start < 1000) {     /* at most 10 s */
        if (kbuf_poll_event(&e) && e.isdown)
            downs[n++] = e.ascii;
    }
    check(n, 2);
    check(downs[0], 'k');
    check(downs[1], 'q');
    kbuf_deinit();
    check(kbuf_poll_event(&e), 0);
    return finish();
}
