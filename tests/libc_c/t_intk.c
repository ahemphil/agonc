/* t_intk.c - C test (test_libc_c.py): Ctrl-C while waiting in mos_getkey,
 * a MOS call: it returns the key, and SIGINT's default action ends the
 * program with 130 at the start of the next call. */

#include <stdio.h>
#include <agon/mos.h>

int main(void)
{
    fclose(fopen("/out/m_k", "w"));
    for (;;)
        mos_getkey();
    return 1;
}
