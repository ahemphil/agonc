/* t_stream.c - stdio's streams as M12 made them (step 3): text and binary
 * streams (CR LF), the update modes, ungetc, setvbuf and setbuf, clearerr,
 * the getc and putc macros, printf's %n, and FOPEN_MAX files at once. */

#include <stdio.h>
#include <string.h>
#include "check.h"

char buf[5000];
char mine[64];

/* The bytes of a file, read as binary; returns how many. */
int raw(char *path, char *into, int max)
{
    FILE *f;
    int n;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    n = fread(into, 1, max, f);
    fclose(f);
    return n;
}

void put_raw(char *path, char *bytes, int n)
{
    FILE *f;

    f = fopen(path, "wb");
    fwrite(bytes, 1, n, f);
    fclose(f);
}

void test_text(void)
{
    FILE *f;
    int i;
    int n;
    long at;

    /* written: '\n' becomes CR LF, whichever call writes it */
    f = fopen("/t1.txt", "w");
    putc('a', f);
    putc('\n', f);
    fputs("b\n", f);
    fwrite("c\nd", 1, 3, f);
    fprintf(f, "%d\n", 5);
    fclose(f);
    check(raw("/t1.txt", buf, 100), 13);
    check_mem(buf, "a\r\nb\r\nc\r\nd5\r\n", 13);
    /* read: CR LF is '\n', a lone CR or LF is itself */
    put_raw("/t2.txt", "x\r\ny\rz\nw\r", 9);
    f = fopen("/t2.txt", "r");
    check(getc(f), 'x');
    check(getc(f), '\n');
    check(getc(f), 'y');
    check(getc(f), '\r');
    check(getc(f), 'z');
    check(getc(f), '\n');
    check(getc(f), 'w');
    check(getc(f), '\r');               /* a CR at the very end */
    check(getc(f), EOF);
    fclose(f);
    f = fopen("/t2.txt", "rb");         /* binary: every byte as it is */
    n = fread(buf, 1, 100, f);
    check(n, 9);
    check_mem(buf, "x\r\ny\rz\nw\r", 9);
    fclose(f);
    /* a CR LF split across a buffer's end (BUFSIZ 4096) */
    for (i = 0; i < 4095; i++)
        buf[i] = 'q';
    buf[4095] = '\r';
    buf[4096] = '\n';
    buf[4097] = 'e';
    put_raw("/t3.txt", buf, 4098);
    f = fopen("/t3.txt", "r");
    n = fread(buf, 1, 5000, f);
    check(n, 4097);
    check(buf[4095], '\n');
    check(buf[4096], 'e');
    fclose(f);
    /* ftell's values bring fseek back, on a text stream */
    f = fopen("/t1.txt", "r");
    getc(f);
    getc(f);                            /* the '\n' of a CR LF */
    at = ftell(f);
    check(at, 3);
    check(getc(f), 'b');
    check(fseek(f, at, SEEK_SET), 0);
    check(getc(f), 'b');
    fclose(f);
    f = fopen("/t4.txt", "w");
    fputs("1\n2\n", f);
    check(ftell(f), 6);                 /* staged, counting CR LF */
    fclose(f);
}

void test_update(void)
{
    FILE *f;
    int n;

    /* w+: write, then read it back */
    f = fopen("/u1.bin", "w+b");
    check(f != NULL, 1);
    fputs("hello world", f);
    check(fseek(f, 0, SEEK_SET), 0);
    n = fread(buf, 1, 100, f);
    check(n, 11);
    check_mem(buf, "hello world", 11);
    /* read then write, with a seek between */
    check(fseek(f, 6, SEEK_SET), 0);
    check(getc(f), 'w');
    check(fseek(f, 0, SEEK_CUR), 0);
    fputs("OR", f);
    fclose(f);
    check(raw("/u1.bin", buf, 100), 11);
    check_mem(buf, "hello wORld", 11);
    /* r+: overwrite in place, the rest kept */
    f = fopen("/u1.bin", "rb+");
    check(f != NULL, 1);
    fputs("J", f);
    check(fseek(f, 0, SEEK_END), 0);
    check(ftell(f), 11);
    fclose(f);
    check(raw("/u1.bin", buf, 100), 11);
    check_mem(buf, "Jello wORld", 11);
    /* a+: read anywhere, write only at the end */
    f = fopen("/u1.bin", "a+b");
    check(f != NULL, 1);
    check(fseek(f, 0, SEEK_SET), 0);
    check(getc(f), 'J');
    check(fseek(f, 0, SEEK_CUR), 0);
    fputs("!", f);
    fclose(f);
    check(raw("/u1.bin", buf, 100), 12);
    check_mem(buf, "Jello wORld!", 12);
    /* modes: "r+b" too; nonsense refused */
    f = fopen("/u1.bin", "r+b");
    check(f != NULL, 1);
    fclose(f);
    check(fopen("/u1.bin", "rw") == NULL, 1);
    check(fopen("/u1.bin", "") == NULL, 1);
    /* writing a read-only stream is an error */
    f = fopen("/u1.bin", "r");
    check(putc('x', f), EOF);
    check(ferror(f) != 0, 1);
    clearerr(f);
    check(ferror(f), 0);
    check(getc(f), 'J');
    fclose(f);
}

void test_unget(void)
{
    FILE *f;

    put_raw("/g1.txt", "ab", 2);
    f = fopen("/g1.txt", "r");
    check(getc(f), 'a');
    check(ungetc('z', f), 'z');         /* not the character read: still fine */
    check(ftell(f), 0);
    check(ungetc('y', f), EOF);         /* one is guaranteed; a second fails */
    check(getc(f), 'z');
    check(getc(f), 'b');
    check(getc(f), EOF);
    check(feof(f) != 0, 1);
    check(ungetc('q', f), 'q');         /* clears end of file */
    check(feof(f), 0);
    check(getc(f), 'q');
    check(ungetc(EOF, f), EOF);
    check(ungetc('r', f), 'r');
    check(fseek(f, 0, SEEK_SET), 0);    /* a seek forgets it */
    check(getc(f), 'a');
    fclose(f);
}

void test_buffering(void)
{
    FILE *f;
    FILE *g;

    /* unbuffered: every byte goes straight to the file */
    f = fopen("/b1.txt", "wb");
    check(setvbuf(f, NULL, _IONBF, 0), 0);
    putc('1', f);
    check(raw("/b1.txt", buf, 100), 1);
    fclose(f);
    /* line-buffered: written at each newline */
    f = fopen("/b2.txt", "wb");
    check(setvbuf(f, NULL, _IOLBF, 64), 0);
    fputs("ab", f);
    check(raw("/b2.txt", buf, 100), 0);
    fputs("c\nd", f);
    check(raw("/b2.txt", buf, 100), 4);
    fclose(f);
    /* the program's own buffer, of its own size */
    f = fopen("/b3.txt", "wb");
    check(setvbuf(f, mine, _IOFBF, 8), 0);
    fputs("1234567", f);
    check(raw("/b3.txt", buf, 100), 0);
    fputs("89", f);
    check(raw("/b3.txt", buf, 100), 8);
    check(mine[0], '9');                /* the buffer is the program's, reused after writing */
    fclose(f);
    check(raw("/b3.txt", buf, 100), 9);
    f = fopen("/b4.txt", "wb");
    setbuf(f, NULL);                    /* unbuffered */
    putc('x', f);
    check(raw("/b4.txt", buf, 100), 1);
    fclose(f);
    f = fopen("/b4.txt", "rb");
    check(setvbuf(f, NULL, 9, 0) != 0, 1);  /* an unknown mode */
    fclose(f);
    /* fflush(NULL) writes every stream */
    f = fopen("/b5.txt", "wb");
    g = fopen("/b6.txt", "wb");
    fputs("five", f);
    fputs("six", g);
    check(fflush(NULL), 0);
    check(raw("/b5.txt", buf, 100), 4);
    check(raw("/b6.txt", buf, 100), 3);
    fclose(f);
    fclose(g);
}

void test_misc(void)
{
    FILE *f[9];
    char name[12];
    int i;
    int n;
    int k;
    long lk;
    short sk;

    /* %n */
    n = sprintf(buf, "abc%ndef%ln%hn", &k, &lk, &sk);
    check(n, 6);
    check(k, 3);
    check((int)lk, 6);
    check(sk, 6);
    /* getchar and putchar are macros, and functions too */
#ifndef getchar
    check(0, 1);
#endif
    check((putchar)(0) == 0, 1);
    /* FOPEN_MAX files at once */
    check(FOPEN_MAX, 8);
    n = 0;
    for (i = 0; i < 9; i++) {
        strcpy(name, "/m0.txt");
        name[2] = '0' + i;
        f[i] = fopen(name, "w");
        if (f[i] != NULL)
            n++;
    }
    check(n, 8);
    check(f[8] == NULL, 1);
    for (i = 0; i < 8; i++)
        check(fclose(f[i]), 0);
    check(fclose(f[0]), EOF);           /* already closed */
}

int main(void)
{
    test_text();
    test_update();
    test_unget();
    test_buffering();
    test_misc();
    return finish();
}
