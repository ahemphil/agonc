/* t_drv.c - a program built by the driver from two units, a user library
 * and the C library, with options that must each reach the right pass:
 *
 *     agonc -I inc -DDEFINED=42 -DFLAG -DGONE -UGONE [-L dir] -lextra
 *           t_drv.c t_util.c
 *
 * Exits 0, or the number of the first failing check. */

#include <stdio.h>
#include <string.h>
#include <agon/mos.h>
#include <drv.h>
#include "check.h"

/* t_util.c has a static function of the same name: each unit gets its own. */
static int which(void)
{
    return 1;
}

int main(void)
{
    char buf[40];
    int h[8];
    int i;
    int opened;

    check(FROM_HEADER, 7);              /* 1: -I reached cpp */
    check(DEFINED, 42);                 /* 2: -D with a value */
    check(FLAG, 1);                     /* 3: -D without one */
#ifdef GONE
    check(0, 1);                        /* -U after -D, in order */
#endif
    check(__AGONC__, 1);                /* 4 */
    check(which(), 1);                  /* 5 */
    check(util_which(), 2);             /* 6: the other unit's static */
    check(util_twice(21), 42);          /* 7: the second unit */
    check(extra_add(40, 2), 42);        /* 8: the -l library */
    sprintf(buf, "%d-%s", util_twice(5), "x");
    check_str(buf, "10-x");             /* 9: the C library */
    check(strlen(buf), 4);              /* 10 */

    /* 11: all 8 MOS handles are free - nothing the build ran left one open */
    opened = 0;
    for (i = 0; i < 8; i++) {
        h[i] = mos_fopen("/autoexec.txt", FA_READ);
        if (h[i] != 0)
            opened++;
    }
    check(opened, 8);
    for (i = 0; i < 8; i++)
        if (h[i] != 0)
            mos_fclose(h[i]);
    return finish();
}
