/* t_atx.c - E test (test_libc_c.py): main's return is exit(7), which runs
 * the atexit functions in reverse order and then flushes and closes the
 * streams: e_atx.txt gets "21" although the program never closes it. */

#include <stdio.h>
#include <stdlib.h>

FILE *f;

void one(void)
{
    fputc('1', f);
    printf("one\n");
}

void two(void)
{
    fputc('2', f);
    printf("two\n");
}

int main(void)
{
    f = fopen("/out/e_atx.txt", "w");
    atexit(one);
    atexit(two);
    printf("main\n");
    return 7;
}
