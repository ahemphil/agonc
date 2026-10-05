/* t_echk.c - E test (test_libc_c.py), run after t_atx, t_abrt, t_sig and
 * t_exd and t_tmpx: the files they left, and all 8 MOS handles free (a program that
 * ended without closing a file would have lost one for the session).
 * Returns 0, or the number of the first failing check; with the argument
 * "bad" the first check expects the wrong text (the guard). */

#include <stdio.h>
#include <string.h>
#include <agon/mos.h>

int count;
int first;

void check(int ok)
{
    count++;
    if (!ok && first == 0)
        first = count;
}

/* Whether the file holds exactly want. */
int holds(char *path, char *want)
{
    FILE *f;
    char buf[16];
    int n;

    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return n == (int)strlen(want) && memcmp(buf, want, n) == 0;
}

int main(int argc, char **argv)
{
    char name[16];
    int h[8];
    int i;
    int ok;

    check(holds("/out/e_atx.txt", argc > 1 && strcmp(argv[1], "bad") == 0 ? "12" : "21"));
    check(holds("/out/e_ab.txt", ""));
    check(holds("/out/e_rs.txt", "kept"));
    check(holds("/out/e_ex.txt", "deep"));
    check(mos_fsize("/tmp/t00001.tmp") < 0);     /* t_tmpx's tmpfile, removed at exit */
    ok = 1;
    for (i = 0; i < 8; i++) {
        strcpy(name, "/out/e_h0.txt");
        name[8] = '0' + i;
        h[i] = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
        if (h[i] == 0)
            ok = 0;
    }
    for (i = 0; i < 8; i++)
        if (h[i] != 0)
            mos_fclose(h[i]);
    check(ok);
    return first;
}
