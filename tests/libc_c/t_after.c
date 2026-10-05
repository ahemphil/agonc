/* t_after.c - runs after t_exit in the same autoexec: exit status 99. */

#include <agon/mos.h>

int main(void)
{
    agon_emu_exit(99);
    return 0;
}
