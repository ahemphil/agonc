/* t_intd.c - C test (test_libc_c.py): Ctrl-C while writing a file, with
 * SIGINT's default action: the stream is flushed and closed, and the
 * status is 130. The harness presses Ctrl-C once /out/m_d exists; t_ichk
 * then finds intd.txt holding whole lines only. */

#include <stdio.h>

int main(void)
{
    FILE *f;

    f = fopen("/out/intd.txt", "w");
    fclose(fopen("/out/m_d", "w"));
    for (;;)
        fputs("line\n", f);
    return 1;
}
