/* hello.c - the smallest whole program, for comparing the driver's output
 * with the passes run by hand. */

#include <stdio.h>
#include <agon/mos.h>

int main(void)
{
    printf("hello\n");
    agon_emu_exit(0);
    return 0;
}
