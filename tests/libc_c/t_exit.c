/* t_exit.c - exit() from deep in the call stack returns to MOS with the
 * status: MOS then runs the next autoexec line (t_after), which exits the
 * emulator with 99. If exit() returned instead, this exits with 1. */

#include <stdlib.h>
#include <agon/mos.h>

void deep(int n)
{
    if (n == 0)
        exit(0);
    deep(n - 1);
}

int main(void)
{
    deep(10);
    agon_emu_exit(1);
    return 0;
}
