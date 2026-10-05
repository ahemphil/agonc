/* t_abrt.c - E test (test_libc_c.py): abort ends with status 134 and
 * closes e_ab.txt without writing its buffer, so the file is empty and
 * its MOS handle free (t_echk). */

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    FILE *f;

    f = fopen("/out/e_ab.txt", "w");
    fputs("lost", f);
    abort();
    return 1;
}
