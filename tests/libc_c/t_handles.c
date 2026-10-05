/* t_handles.c - run after many pass invocations in one MOS session: all 8
 * of MOS's file handles must still be free (neither MOS nor AgDev's exit
 * closes a program's files, so a pass that leaks one loses it for good).
 * Exits 0, or the number of the first open that failed. */

#include <agon/mos.h>

int main(void)
{
    int i;

    for (i = 1; i <= 8; i++)
        if (mos_fopen("existing.txt", FA_READ) == 0)
            agon_emu_exit(i);
    mos_fclose(0);
    agon_emu_exit(0);
    return 0;
}
