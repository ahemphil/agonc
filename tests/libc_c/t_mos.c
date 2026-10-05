/* t_mos.c - <agon/mos.h> (mos.c) on the emulator, porting the cases of the
 * assembly mos.s's tests (tests/lib/agon). Fixtures on the card:
 * existing.txt ("Hello, Agon!\n", 13 bytes) and exact16.bin (bytes 0-15).
 *
 * Only behaviour real MOS shares is checked; the emulator-only file-system
 * quirks documented in mos.h are avoided (appends use FA_OPEN_APPEND, a
 * seek-then-write opens with plain FA_WRITE).
 */

#include <string.h>
#include "check.h"

char buf[64];

void test_open_close(void)
{
    int h1;
    int h2;
    int i;

    h1 = mos_fopen("existing.txt", FA_READ);
    check(h1, 1);                       /* the first handle on a clean card */
    check(mos_fclose(h1), 1);           /* a single close echoes its handle */
    check(mos_fopen("missing.txt", FA_READ), 0);
    check(mos_fopen("missing2.txt", FA_WRITE), 0);      /* FA_WRITE alone needs the file */
    h1 = mos_fopen("new.txt", FA_WRITE | FA_CREATE_ALWAYS);
    check(h1 != 0, 1);
    mos_fclose(h1);
    h1 = mos_fopen("new.txt", FA_READ);
    check(h1 != 0, 1);
    mos_fclose(h1);
    h1 = mos_fopen("exact16.bin", FA_READ);
    h2 = mos_fopen("new.txt", FA_READ);
    check(h1, 1);
    check(h2, 2);
    check(mos_fclose(0), 0);            /* close everything */
    mos_fclose(h2);                     /* already closed: harmless */
    mos_fclose(99);                     /* never issued: harmless */
    h1 = mos_fopen("existing.txt", FA_READ);
    check(h1, 1);
    mos_fclose(h1);
    /* MOS has 8 handles: the 9th open fails, and closing all recovers them */
    for (i = 0; i < 8; i++)
        check(mos_fopen("existing.txt", FA_READ) != 0, 1);
    check(mos_fopen("existing.txt", FA_READ), 0);
    check(mos_fclose(0), 0);
    h1 = mos_fopen("existing.txt", FA_READ);
    check(h1 != 0, 1);
    mos_fclose(h1);
}

void test_read_write(void)
{
    int h;
    int i;

    h = mos_fopen("existing.txt", FA_READ);
    check(mos_fread(h, buf, 5), 5);
    check_mem(buf, "Hello", 5);
    check(mos_fread(h, buf, 100), 8);   /* more than is left */
    check_mem(buf, ", Agon!\n", 8);
    check(mos_fread(h, buf, 10), 0);    /* at the end */
    check(mos_fread(h, buf, 0), 0);
    mos_fclose(h);
    h = mos_fopen("exact16.bin", FA_READ);
    check(mos_fread(h, buf, 16), 16);
    for (i = 0; i < 16; i++)
        check(buf[i], i);
    mos_fclose(h);
    /* write, read back, truncate, append */
    h = mos_fopen("rw.bin", FA_WRITE | FA_CREATE_ALWAYS);
    check(mos_fwrite(h, "0123456789", 10), 10);
    check(mos_fwrite(h, "x", 0), 0);
    mos_fclose(h);
    check(mos_fsize("rw.bin"), 10);
    h = mos_fopen("rw.bin", FA_WRITE | FA_OPEN_APPEND);
    check(mos_fwrite(h, "AB", 2), 2);
    mos_fclose(h);
    h = mos_fopen("rw.bin", FA_READ);
    check(mos_fread(h, buf, 64), 12);
    check_mem(buf, "0123456789AB", 12);
    mos_fclose(h);
    h = mos_fopen("rw.bin", FA_WRITE | FA_CREATE_ALWAYS);       /* truncates */
    check(mos_fwrite(h, "z", 1), 1);
    mos_fclose(h);
    check(mos_fsize("rw.bin"), 1);
    check(mos_fsize("missing.txt"), -1);
    check(mos_fsize("existing.txt"), 13);
}

void test_seek_eof(void)
{
    int h;

    h = mos_fopen("exact16.bin", FA_READ);
    check(mos_feof(h), 0);
    check(mos_flseek(h, 10), 0);
    check(mos_fread(h, buf, 3), 3);
    check(buf[0] * 100 + buf[1] * 10 + buf[2], 1000 + 110 + 12);
    check(mos_flseek(h, 16), 0);        /* exactly the end */
    check(mos_fread(h, buf, 1), 0);
    check(mos_feof(h), 1);
    check(mos_flseek(h, 1000), 0);      /* far past it */
    check(mos_fread(h, buf, 1), 0);
    check(mos_flseek(h, 0), 0);         /* rewind */
    check(mos_feof(h), 0);
    check(mos_fread(h, buf, 2), 2);
    check(buf[1], 1);
    mos_fclose(h);
    /* seek, then overwrite in place (plain FA_WRITE honours the seek) */
    h = mos_fopen("rw2.bin", FA_WRITE | FA_CREATE_ALWAYS);
    mos_fwrite(h, "abcdefgh", 8);
    mos_fclose(h);
    h = mos_fopen("rw2.bin", FA_WRITE);
    check(mos_flseek(h, 3), 0);
    check(mos_fwrite(h, "XY", 2), 2);
    mos_fclose(h);
    h = mos_fopen("rw2.bin", FA_READ);
    check(mos_fread(h, buf, 20), 8);
    check_mem(buf, "abcXYfgh", 8);
    mos_fclose(h);
}

void test_files(void)
{
    int h;

    h = mos_fopen("gone.txt", FA_WRITE | FA_CREATE_ALWAYS);
    mos_fwrite(h, "bye", 3);
    mos_fclose(h);
    check(mos_del("gone.txt"), 0);
    check(mos_fopen("gone.txt", FA_READ), 0);
    check(mos_del("gone.txt") != 0, 1);         /* no longer there */
    check(mos_ren("rw2.bin", "moved.bin"), 0);
    check(mos_fopen("rw2.bin", FA_READ), 0);
    check(mos_fsize("moved.bin"), 8);
    check(mos_mkdir("sub"), 0);
    h = mos_fopen("sub/inner.txt", FA_WRITE | FA_CREATE_ALWAYS);
    check(h != 0, 1);
    mos_fwrite(h, "in", 2);
    mos_fclose(h);
    check(mos_fsize("sub/inner.txt"), 2);
    check(mos_sysvars() != 0, 1);
}

int main(void)
{
    test_open_close();
    test_read_write();
    test_seek_eof();
    test_files();
    return finish();
}
