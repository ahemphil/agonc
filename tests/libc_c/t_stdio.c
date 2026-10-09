/* t_stdio.c - stdio.c on the emulator, porting the cases of the assembly
 * stdio.s's five tests (tests/libc: file, misc, big, seek,
 * printf), 45-row sprintf table included.
 */

#include <stdio.h>
#include <string.h>
#include "check.h"

#define BIG 5000

char line[128];
char big[BIG + 1];
char back[BIG + 100];

/* The whole of a file, through a fresh stream; returns its length. */
int slurp(char *path, char *into, int max)
{
    FILE *f;
    int n;

    f = fopen(path, "r");
    if (f == NULL)
        return -1;
    n = fread(into, 1, max, f);
    fclose(f);
    return n;
}

void test_file(void)
{
    FILE *f;
    int c;
    int n;

    f = fopen("/stest.txt", "w");
    check(f != NULL, 1);
    check(fputs("line one\nline two\n", f), 0);
    check(fclose(f), 0);
    f = fopen("/stest.txt", "rb");
    n = 0;
    while ((c = fgetc(f)) != EOF) {
        line[n] = c;
        n++;
    }
    check(n, 20);                           /* a text stream: CR LF in the file */
    check_mem(line, "line one\r\nline two\r\n", 20);
    check(feof(f) != 0, 1);
    check(ferror(f), 0);
    fclose(f);
    f = fopen("/stest.txt", "r");
    check(fgets(line, 128, f) == line, 1);
    check_str(line, "line one\n");          /* the newline is kept */
    check(fgets(line, 6, f) == line, 1);
    check_str(line, "line ");               /* n - 1 characters */
    check(fgets(line, 1, f) == line, 1);    /* n == 1: nothing read, an empty string */
    check_str(line, "");
    check(fgets(line, 128, f) == line, 1);
    check_str(line, "two\n");
    check(fgets(line, 128, f) == NULL, 1);  /* end of file */
    fclose(f);
    f = fopen("/stest.txt", "a");
    fputs("three\n", f);
    fclose(f);
    n = slurp("/stest.txt", back, 100);
    check(n, 24);
    check_mem(back, "line one\nline two\nthree\n", 24);
    check(fopen("/nope.txt", "r") == NULL, 1);
    check(fopen("/stest.txt", "x") == NULL, 1);
}

void test_misc(void)
{
    FILE *w;
    FILE *r;
    char rec[12];

    w = fopen("/rec.bin", "wb");
    check(fwrite("abcdefghijkl", 3, 4, w), 4);
    fclose(w);
    r = fopen("/rec.bin", "rb");
    check(fread(rec, 3, 4, r), 4);
    check_mem(rec, "abcdefghijkl", 12);
    check(fread(rec, 3, 1, r), 0);          /* exactly at the end */
    check(feof(r) != 0, 1);
    fclose(r);
    r = fopen("/rec.bin", "rb");
    check(fread(rec, 5, 3, r), 2);          /* 12 bytes: two whole items */
    fclose(r);
    /* fflush reaches the file itself: a second stream sees the bytes */
    w = fopen("/flush.txt", "w");
    fputs("flushed", w);
    check(fflush(w), 0);
    check(slurp("/flush.txt", back, 100), 7);
    fclose(w);
    check(remove("/flush.txt"), 0);
    check(fopen("/flush.txt", "r") == NULL, 1);
    check(rename("/rec.bin", "/moved.bin"), 0);
    check(fopen("/rec.bin", "r") == NULL, 1);
    check(slurp("/moved.bin", back, 100), 12);
    check_mem(back, "abcdefghijkl", 12);
}

void test_big(void)
{
    FILE *f;
    int i;

    for (i = 0; i < BIG; i++)
        big[i] = 'a' + i % 26;
    big[BIG] = 0;
    f = fopen("/big1.txt", "w");
    check(fputs(big, f), 0);                /* flushes mid-string */
    fclose(f);
    check(slurp("/big1.txt", back, BIG + 100), BIG);
    check_mem(back, big, BIG);
    f = fopen("/big2.txt", "w");
    for (i = 0; i < BIG; i++)
        fputc(big[i], f);
    fclose(f);
    check(slurp("/big2.txt", back, BIG + 100), BIG);
    check_mem(back, big, BIG);
    f = fopen("/big3.txt", "w");
    check(fwrite(big, 1, BIG, f), BIG);
    check(fwrite(big, 7, 3, f), 3);
    fclose(f);
    check(slurp("/big3.txt", back, BIG + 100), BIG + 21);
    check_mem(back + BIG, big, 21);
}

void test_seek(void)
{
    FILE *f;

    /* positions while writing, and an overwrite in place */
    f = fopen("/seek.txt", "w");
    check(ftell(f), 0);
    fputs("0123456789", f);
    check(ftell(f), 10);                    /* staged, not yet flushed */
    check(fseek(f, 2, SEEK_SET), 0);
    fputs("AB", f);
    check(ftell(f), 4);
    check(fseek(f, 0, SEEK_END), 0);
    check(ftell(f), 10);
    fputs("!", f);
    fclose(f);
    check(slurp("/seek.txt", back, 100), 11);
    check_mem(back, "01AB456789!", 11);
    /* reading: within the buffer and outside it */
    f = fopen("/big1.txt", "r");
    check(fseek(f, 100, SEEK_SET), 0);
    check(fgetc(f), 'a' + 100 % 26);
    check(ftell(f), 101);
    check(fseek(f, -50, SEEK_CUR), 0);      /* inside the buffered window */
    check(fgetc(f), 'a' + 51 % 26);
    check(fseek(f, 4500, SEEK_SET), 0);     /* past the first 4 KB */
    check(fgetc(f), 'a' + 4500 % 26);
    check(fseek(f, 10, SEEK_SET), 0);       /* back before the window */
    check(fgetc(f), 'a' + 10 % 26);
    check(fseek(f, -1, SEEK_END), 0);
    check(fgetc(f), 'a' + 4999 % 26);
    check(fgetc(f), EOF);
    check(feof(f) != 0, 1);
    check(fseek(f, 0, SEEK_CUR), 0);        /* clears end of file */
    check(feof(f), 0);
    rewind(f);
    check(ftell(f), 0);
    check(fgetc(f), 'a');
    check(fread(back, 1, 4199, f), 4199);   /* from 1, across a refill */
    check(ftell(f), 4200);
    check(fseek(f, -10, SEEK_CUR), 0);
    check(fgetc(f), 'a' + 4190 % 26);
    check(fseek(f, 6000, SEEK_SET), 0);     /* past the end is allowed */
    check(fgetc(f), EOF);
    check(fseek(f, -1, SEEK_SET), -1);      /* before the start */
    check(fseek(f, 0, 7), -1);              /* an unknown whence */
    fclose(f);
    check(fseek(stdout, 0, SEEK_SET), -1);  /* the console */
    check(ftell(stdout), -1);
    /* append stays at the end whatever fseek says */
    f = fopen("/seek.txt", "a");
    check(ftell(f), 11);
    check(fseek(f, 0, SEEK_SET), 0);
    fputs("?", f);
    fclose(f);
    check(slurp("/seek.txt", back, 100), 12);
    check_mem(back, "01AB456789!?", 12);
    /* writing past the end extends the file */
    f = fopen("/ext.txt", "w");
    fputs("ab", f);
    check(fseek(f, 5, SEEK_SET), 0);
    fputs("z", f);
    fclose(f);
    check(slurp("/ext.txt", back, 100), 6);
    check(back[5], 'z');
}

/* ---- printf ---------------------------------------------------------------------- */

char out[64];

void row(char *want, int n)
{
    check_str(out, want);
    check(n, strlen(want));
}

void test_printf(void)
{
    FILE *f;
    int n;

    memset(out, 'X', 64);               /* a missing NUL cannot pass by luck */
    row("hello", sprintf(out, "hello"));
    row("42", sprintf(out, "%d", 42));
    row("-42", sprintf(out, "%d", -42));
    row("0", sprintf(out, "%d", 0));
    row("8388607", sprintf(out, "%i", 8388607));
    row("-8388608", sprintf(out, "%d", 0x800000));
    row("16777215", sprintf(out, "%u", 0xFFFFFF));
    row("abcdef", sprintf(out, "%x", 0xABCDEF));
    row("ABCDEF", sprintf(out, "%X", 0xABCDEF));
    row("0", sprintf(out, "%x", 0));
    row("10", sprintf(out, "%o", 8));
    row("77777777", sprintf(out, "%o", 0xFFFFFF));
    row("A", sprintf(out, "%c", 'A'));
    row("str", sprintf(out, "%s", "str"));
    row("%", sprintf(out, "%%"));
    row("   42", sprintf(out, "%5d", 42));
    row("42   |", sprintf(out, "%-5d|", 42));
    row("00042", sprintf(out, "%05d", 42));
    row("-0042", sprintf(out, "%05d", -42));
    row("007", sprintf(out, "%.3d", 7));
    row(" -007", sprintf(out, "%5.3d", -7));
    row("", sprintf(out, "%.0d", 0));
    row("42   |", sprintf(out, "%-05d|", 42));
    row("    3", sprintf(out, "%05.1d", 3));
    row("   1", sprintf(out, "%*d", 4, 1));
    row("1   |", sprintf(out, "%*d|", -4, 1));
    row("ab", sprintf(out, "%.*s", 2, "abcdef"));
    row("5", sprintf(out, "%.*d", -1, 5));
    row("      hi|", sprintf(out, "%8s|", "hi"));
    row("hi  |", sprintf(out, "%-4s|", "hi"));
    row("he", sprintf(out, "%.2s", "hello"));
    row("(null)", sprintf(out, "%s", NULL));
    row("040010", sprintf(out, "%p", (char *)0x040010));
    row("5", sprintf(out, "%hd", 5));
    row("  x|", sprintf(out, "%3c|", 'x'));
    row("a1bZcffd", sprintf(out, "a%db%sc%xd", 1, "Z", 255));
    row("00001A2B", sprintf(out, "%08X", 0x1A2B));
    row("12345", sprintf(out, "%3d", 12345));
    row("q  |", sprintf(out, "%-3c|", 'q'));
    row("00042", sprintf(out, "%.5u", 42));
    row("     |", sprintf(out, "%5.0d|", 0));
    row("ffffff", sprintf(out, "%x", 0xFFFFFF));
    row("aqb", sprintf(out, "a%qb"));
    row("end", sprintf(out, "end%"));
    row("%%%", sprintf(out, "%%%%%%"));
    /* the '+' and ' ' flags (added in M6: cc2 prints branch offsets with %+d) */
    row("+17", sprintf(out, "%+d", 17));
    row("-5", sprintf(out, "%+d", -5));
    row("+0", sprintf(out, "%+d", 0));
    row(" 5", sprintf(out, "% d", 5));
    row("-5", sprintf(out, "% d", -5));
    row("+5", sprintf(out, "%+ d", 5));
    row("+5", sprintf(out, "% +d", 5));
    row("  +17", sprintf(out, "%+5d", 17));
    row("+0017", sprintf(out, "%+05d", 17));
    row("+17  |", sprintf(out, "%-+5d|", 17));
    row("5 ff", sprintf(out, "%+u % x", 5, 255));
    row("jr z,$+17 jp $-300", sprintf(out, "jr z,$%+d jp $%+d", 17, -300));
    /* fprintf to a file, including one conversion bigger than the buffer */
    f = fopen("/pf.txt", "w");
    check(fprintf(f, "%s=%d\n%s=%d\n", "x", 5, "y", -3), 9);
    fclose(f);
    check(slurp("/pf.txt", back, 100), 9);
    check_mem(back, "x=5\ny=-3\n", 9);
    f = fopen("/pfbig.txt", "w");
    check(fprintf(f, "<%s>%d", big, 77), BIG + 4);
    fclose(f);
    check(slurp("/pfbig.txt", back, BIG + 100), BIG + 4);
    check(back[0], '<');
    check_mem(back + 1, big, BIG);
    check_mem(back + BIG + 1, ">77", 3);
    /* printf's count (the console's text is not checked) */
    n = printf("n=%d\n", 123);
    check(n, 6);
}

/* M8: long conversions, 'h', and positions beyond 24 bits */
void test_long(void)
{
    FILE *f;
    long pos;

    memset(out, 'X', 64);
    row("2147483647", sprintf(out, "%ld", 2147483647L));
    row("-2147483648", sprintf(out, "%ld", -2147483647L - 1));
    row("4294967295", sprintf(out, "%lu", 4294967295UL));
    row("ffffffff", sprintf(out, "%lx", 0xFFFFFFFFUL));
    row("DEADBEEF", sprintf(out, "%lX", 0xDEADBEEFUL));
    row("37777777777", sprintf(out, "%lo", 0xFFFFFFFFUL));
    row("16777216", sprintf(out, "%li", 16777216L));
    row("0", sprintf(out, "%ld", 0L));
    row("", sprintf(out, "%.0ld", 0L));
    row("  -00012345678|", sprintf(out, "%14.11ld|", -12345678L));
    row("+100000000", sprintf(out, "%+ld", 100000000L));
    row("1 2 3 x", sprintf(out, "%ld %d %lu %s", 1L, 2, 3UL, "x"));
    row("-1", sprintf(out, "%hd", 65535));
    row("65535", sprintf(out, "%hu", -1));
    row("ffff", sprintf(out, "%hx", 0x12FFFF));
    row("-32768", sprintf(out, "%hd", 32768));
    f = fopen("/big1.txt", "r");
    check(fseek(f, 20000000L, SEEK_SET), 0);
    pos = ftell(f);
    check(pos == 20000000L, 1);
    check(fgetc(f), EOF);
    check(fseek(f, -19999990L, SEEK_CUR), 0);
    check(ftell(f) == 10L, 1);
    check(fgetc(f), 'a' + 10);
    check(fseek(f, 0x1000000L, SEEK_END), 0);
    check(ftell(f) == 5000L + 0x1000000L, 1);
    fclose(f);
}

/* M9: the scanf family */
void test_scanf(void)
{
    FILE *f;
    int a;
    int b;
    int c;
    int n;
    short h;
    long l;
    char w[16];
    char *p;
    void *vp;

    check(sscanf("  42 -17 0x1F 017", "%d %d %x %o", &a, &b, &c, &n), 4);
    check(a, 42);
    check(b, -17);
    check(c, 31);
    check(n, 15);
    check(sscanf("0x10 010 10 -0x2", "%i %i %i %i", &a, &b, &c, &n), 4);
    check(a * 10000 + b * 100 + c, 160810);
    check(n, -2);
    check(sscanf("12345", "%3d%d", &a, &b), 2);
    check(a, 123);
    check(b, 45);
    check(sscanf("7 8 9", "%*d %d %n", &a, &n), 1);   /* %n is not counted */
    check(a, 8);
    check(n, 4);
    check(sscanf("-2147483648 -30000", "%ld %hd", &l, &h), 2);
    check(l == -2147483647L - 1, 1);
    check(h, -30000);
    check(sscanf("", "%d", &a), EOF);
    check(sscanf("   ", "%d", &a), EOF);
    check(sscanf("abc", "%d", &a), 0);
    check(sscanf("12 x", "%d %d", &a, &b), 1);
    check(sscanf("a=5,b=6", "a=%d,b=%d", &a, &b), 2);
    check(a * 10 + b, 56);
    check(sscanf("100%", "%d%%", &a), 1);
    check(sscanf("  hello world", "%s %s", w, w + 8), 2);
    check_str(w, "hello");
    check_str(w + 8, "world");
    check(sscanf("abcdef", "%3s", w), 1);
    check_str(w, "abc");
    w[2] = '#';
    check(sscanf("xyz", "%2c", w), 1);
    check(w[0] * 256 + w[1], 'x' * 256 + 'y');
    check(w[2], '#');                                  /* %c adds no NUL */
    check(sscanf(" q", "%c", w), 1);
    check(w[0], ' ');                                  /* %c skips no space */
    check(sscanf("hello,world", "%[^,],%s", w, w + 8), 2);
    check_str(w, "hello");
    check_str(w + 8, "world");
    check(sscanf("]]ab]", "%[]a]", w), 1);             /* a leading ] is in the set */
    check_str(w, "]]a");
    check(sscanf("hello42 x", "%[a-z]%[0-9]", w, w + 8), 2);   /* ranges */
    check_str(w, "hello");
    check_str(w + 8, "42");
    check(sscanf("3Fa0g", "%[0-9A-Fa-f]", w), 1);
    check_str(w, "3Fa0");
    check(sscanf("a-z-b", "%[-a]", w), 1);             /* first: an ordinary '-' */
    check_str(w, "a-");
    check(sscanf("zz-a-y", "%[z-a]", w), 1);           /* backwards: the three characters */
    check_str(w, "zz-a-");
    check(sscanf("mid9", "%[^a-z]", w), 0);            /* negated range */
    check(sscanf("a-x", "%[a-]", w), 1);               /* last: ordinary */
    check_str(w, "a-");
    sprintf(w, "%p", (void *)w);
    check(sscanf(w, "%p", &vp), 1);
    check(vp == (void *)w, 1);
    /* a stream: CR LF text, and the character after a number stays unread */
    f = fopen("/scan.txt", "w");
    fputs("10 20\r\n30\r\n12abc", f);
    fclose(f);
    f = fopen("/scan.txt", "r");
    check(fscanf(f, "%d%d", &a, &b), 2);
    check(fscanf(f, "%d", &c), 1);
    check(a + b + c, 60);
    check(fscanf(f, "%d", &n), 1);
    check(n, 12);
    check(fgetc(f), 'a');
    check(fscanf(f, "%s", w), 1);
    check_str(w, "bc");
    check(fscanf(f, "%d", &a), EOF);
    fclose(f);
    p = "5";
    check(sscanf(p, "%d", &a) + a, 6);
}

int main(void)
{
    test_file();
    test_misc();
    test_big();
    test_seek();
    test_printf();
    test_long();
    test_scanf();
    return finish();
}
