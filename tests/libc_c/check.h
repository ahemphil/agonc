/* check.h - the execution tests' harness: check() counts checks and
 * remembers the first failure; finish() exits the emulator with 0 or that
 * check's number (never text: see agon-emulator-output-flakiness). */

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

/* Two strings (or byte runs, n >= 0) must be equal. */
void check_str(char *got, char *want)
{
    int i;

    i = 0;
    while (got[i] == want[i] && want[i])
        i++;
    check(got[i] == want[i], 1);
}

void check_mem(char *got, char *want, int n)
{
    int i;

    i = 0;
    while (i < n && got[i] == want[i])
        i++;
    check(i, n);
}

int finish(void)
{
    agon_emu_exit(fails == 0 ? 0 : first < 250 ? first : 250);
    return 0;
}
