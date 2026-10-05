/* t_stdmore.c - the rest of <stdio.h> (M12 step 4): freopen (stderr and
 * stdin redirected to files, which also checks perror's text and gets),
 * tmpnam and tmpfile, the vprintf family, fgetpos and fsetpos, strerror,
 * and errno on failures. */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "check.h"

char buf[128];

int raw(char *path, char *into, int max)
{
    FILE *f;
    int n;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    n = fread(into, 1, max, f);
    fclose(f);
    into[n > 0 ? n : 0] = 0;
    return n;
}

int to_buf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int to_file(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

void test_reopen(void)
{
    FILE *f;

    /* stderr to a file: perror's text */
    check(freopen("/perr.txt", "w", stderr) == stderr, 1);
    errno = ENOENT;
    perror("x");
    errno = EEXIST;
    perror("");                         /* just the message */
    errno = 0;
    perror(NULL);
    fclose(stderr);
    check(raw("/perr.txt", buf, 120), 53);
    check_str(buf, "x: no such file or directory\r\nfile exists\r\nno error\r\n");
    /* stdin from a file: gets */
    f = fopen("/in.txt", "w");
    fputs("first line\nsecond\nlast", f);
    fclose(f);
    check(freopen("/in.txt", "r", stdin) == stdin, 1);
    check(gets(buf) == buf, 1);
    check_str(buf, "first line");       /* the newline dropped */
    check(gets(buf) == buf, 1);
    check_str(buf, "second");
    check(gets(buf) == buf, 1);
    check_str(buf, "last");             /* no newline at the end: still a line */
    check(gets(buf) == NULL, 1);
    check(getchar(), EOF);
    fclose(stdin);
    check(freopen("/nope/none.txt", "r", stdin) == NULL, 1);
}

void test_tmp(void)
{
    FILE *f;
    char a[L_tmpnam];
    char b[L_tmpnam];
    char next[L_tmpnam];
    int i;

    mos_mkdir("/tmp");                  /* (it may be there already) */
    check(tmpnam(a) == a, 1);
    check(tmpnam(b) == b, 1);
    check(strcmp(a, b) != 0, 1);
    check((int)strlen(a), L_tmpnam - 1);
    check(strncmp(a, "/tmp/t", 6), 0);
    check(mos_fsize(a) < 0, 1);         /* a name, not a file */
    check(tmpnam(NULL) != NULL, 1);
    /* tmpfile takes the next name: the one after tmpnam's last */
    strcpy(next, tmpnam(NULL));
    i = 10;
    while (next[i] == '9') {
        next[i] = '0';
        i--;
    }
    next[i]++;
    f = tmpfile();
    check(f != NULL, 1);
    check(mos_fsize(next) >= 0, 1);     /* it exists while open */
    fputs("temporary\n", f);            /* binary: no CR */
    rewind(f);
    check(fgets(buf, 100, f) == buf, 1);
    check_str(buf, "temporary\n");
    check(fclose(f), 0);
    check(mos_fsize(next) < 0, 1);      /* and is gone when closed */
}

void test_misc(void)
{
    FILE *f;
    fpos_t pos;
    int i;
    FILE *many[9];

    /* the vprintf family */
    check(to_buf("%d-%s", 42, "x"), 4);
    check_str(buf, "42-x");
    f = fopen("/vf.txt", "wb");
    check(to_file(f, "<%05d>", 7), 7);
    fclose(f);
    raw("/vf.txt", buf, 100);
    check_str(buf, "<00007>");
    /* fgetpos and fsetpos */
    f = fopen("/vf.txt", "rb");
    getc(f);
    getc(f);
    check(fgetpos(f, &pos), 0);
    check(getc(f), '0');
    check(getc(f), '0');
    check(fsetpos(f, &pos), 0);
    check(getc(f), '0');
    check(ftell(f), 3);
    fclose(f);
    errno = 0;
    check(fgetpos(stdout, &pos) != 0, 1);   /* the console */
    check(errno, EBADF);
    errno = 0;
    check(ftell(stdout), -1L);
    check(errno, EBADF);
    /* errno on failures */
    errno = 0;
    check(fopen("/none.txt", "r") == NULL, 1);
    check(errno, ENOENT);
    errno = 0;
    check(fopen("/vf.txt", "q") == NULL, 1);
    check(errno, EINVAL);
    for (i = 0; i < 9; i++)
        many[i] = fopen("/vf.txt", "rb");
    errno = 0;
    check(fopen("/vf.txt", "rb") == NULL, 1);
    check(errno, EMFILE);
    for (i = 0; i < 9; i++)
        if (many[i] != NULL)
            fclose(many[i]);
    errno = 0;
    check(remove("/none.txt") != 0, 1);
    check(errno, ENOENT);
    f = fopen("/r1.txt", "w");
    fclose(f);
    f = fopen("/r2.txt", "w");
    fputs("two", f);
    fclose(f);
    errno = 0;
    check(rename("/r1.txt", "/r2.txt") != 0, 1);    /* the target exists */
    check(errno, EEXIST);
    check(raw("/r2.txt", buf, 100), 3);             /* and is untouched */
    check(rename("/r1.txt", "/r3.txt"), 0);
    check(remove("/r3.txt"), 0);
    /* strerror */
    check_str(strerror(ENOENT), "no such file or directory");
    check_str(strerror(0), "no error");
    check_str(strerror(12345), "unknown error");
}

int main(void)
{
    test_reopen();
    test_tmp();
    test_misc();
    return finish();
}
