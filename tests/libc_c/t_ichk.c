/* t_ichk.c - C test (test_libc_c.py), run after the programs interrupted
 * by Ctrl-C: intd.txt, whose program was stopped mid-write, holds what it
 * had written, flushed and closed by SIGINT's default action: "line\n"
 * repeated, perhaps ending inside a line (the check comes when a full
 * buffer is written, which can be in the middle of an fputs); and all 8
 * MOS handles are free. Exits the emulator with 0 or the first failing
 * check's number. */

#include <stdio.h>
#include <string.h>
#include "check.h"

int main(void)
{
    FILE *f;
    char name[16];
    int c;
    long size;
    int h[8];
    int i;
    int ok;

    f = fopen("/out/intd.txt", "r");
    check(f != NULL, 1);
    size = 0;
    ok = 1;
    if (f != NULL) {
        while ((c = fgetc(f)) != EOF) {
            if (c != "line\n"[(int)(size % 5)])
                ok = 0;
            size++;
        }
        fclose(f);
    }
    check(size > 0, 1);
    check(ok, 1);
    ok = 1;
    for (i = 0; i < 8; i++) {
        strcpy(name, "/out/i_h0.txt");
        name[8] = '0' + i;
        h[i] = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
        if (h[i] == 0)
            ok = 0;
    }
    for (i = 0; i < 8; i++)
        if (h[i] != 0)
            mos_fclose(h[i]);
    check(ok, 1);
    return finish();
}
