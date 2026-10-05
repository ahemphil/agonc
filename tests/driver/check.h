/* check.h - the execution tests' harness: check() counts checks and
 * remembers the first failure; finish() exits the emulator with 0 or that
 * check's number (never text: see agon-emulator-output-flakiness). Built
 * with -DCOUNT, finish() exits with the number of checks run instead. */

#include <agon/mos.h>

int fails;
int first;
int count;

void check(int got, int want)
{
    count++;
    if (got != want) {
        fails++;
        if (first == 0)
            first = count;
    }
}

void check_str(char *got, char *want)
{
    int i;

    i = 0;
    while (got[i] == want[i] && want[i])
        i++;
    check(got[i] == want[i], 1);
}

int finish(void)
{
#ifdef COUNT
    agon_emu_exit(count);
#else
    agon_emu_exit(fails == 0 ? 0 : first < 250 ? first : 250);
#endif
    return 0;
}
