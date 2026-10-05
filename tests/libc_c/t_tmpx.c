/* t_tmpx.c - E test (test_libc_c.py): a tmpfile left open is removed at
 * exit; t_echk finds /tmp/t00001.tmp (this program's first name) gone. */

#include <stdio.h>
#include <agon/mos.h>

int main(void)
{
    FILE *f;

    mos_mkdir("/tmp");
    f = tmpfile();
    fputs("scratch", f);
    return f != NULL && mos_fsize("/tmp/t00001.tmp") >= 0 ? 0 : 1;
}
