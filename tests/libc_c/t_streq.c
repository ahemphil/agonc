/* t_streq.c - string.c's assembly (STR_ASM 1: strlen, strcmp, strcpy,
 * strcat, strchr, memcpy, memmove, memset, memchr) does exactly what its
 * C does. The library's build and the C one, renamed c_ (streq_c.c), run
 * on the same random and edge cases: strings of 0 to 600 bytes, of
 * letters or of any byte but 0, and copies at random offsets between
 * guard bytes, with both overlaps for memmove and counts of 0. Each check
 * is one function over all the cases: the number that differ, which must
 * be 0. Adapted from the acc comparison's tstr.c (2026-10-06).
 */

#include <string.h>
#include "check.h"

unsigned c_strlen(const char *s);
int c_strcmp(const char *a, const char *b);
char *c_strcpy(char *d, const char *s);
char *c_strcat(char *d, const char *s);
char *c_strchr(const char *s, int c);
void *c_memcpy(void *d, const void *s, unsigned n);
void *c_memmove(void *d, const void *s, unsigned n);
void *c_memset(void *d, int c, unsigned n);
void *c_memchr(const void *s, int c, unsigned n);
int c_memcmp(const void *a, const void *b, unsigned n);

#define CASES 1000

/* the differences found, per function */
int d_strlen, d_strcmp, d_strcpy, d_strcat, d_strchr, d_memchr, d_memcpy, d_memset, d_memmove;

unsigned long seed = 12345UL;

unsigned rnd(unsigned n)
{
    seed = (seed * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (unsigned)((seed >> 8) % n);
}

char a[700];
char b[700];
char c1[700];
char c2[700];

/* n random bytes and a NUL: letters, or (hi) any byte but 0 */
void fill(char *p, unsigned n, int hi)
{
    unsigned i;

    for (i = 0; i < n; i++)
        p[i] = (char)(hi ? 1 + rnd(255) : 'a' + rnd(26));
    p[n] = 0;
}

/* 1 if the two buffers differ anywhere */
int differ(void)
{
    return c_memcmp(c1, c2, sizeof c1) != 0;
}

void one_case(int k)
{
    unsigned n;
    unsigned m;
    unsigned off;
    unsigned len;
    int cv;
    char *r1;
    char *r2;

    n = k < 40 ? (unsigned)k : rnd(600);
    fill(a, n, k & 1);
    d_strlen += strlen(a) != c_strlen(a);
    /* strcmp: equal, one byte changed, a prefix, both ways round */
    c_memcpy(b, a, n + 1);
    d_strcmp += strcmp(a, b) != c_strcmp(a, b);
    if (n > 0) {
        m = rnd(n);
        b[m] = (char)(1 + rnd(255));
        d_strcmp += strcmp(a, b) != c_strcmp(a, b);
        d_strcmp += strcmp(b, a) != c_strcmp(b, a);
        b[m] = 0;
        d_strcmp += strcmp(a, b) != c_strcmp(a, b);
        d_strcmp += strcmp(b, a) != c_strcmp(b, a);
    }
    /* strcpy and strcat, between guard bytes */
    c_memset(c1, 0x5A, sizeof c1);
    c_memset(c2, 0x5A, sizeof c2);
    r1 = strcpy(c1 + 3, a);
    r2 = c_strcpy(c2 + 3, a);
    d_strcpy += r1 - c1 != r2 - c2 || differ();
    if (n < 300) {
        fill(b, rnd(300), 0);
        r1 = strcat(c1 + 3, b);
        r2 = c_strcat(c2 + 3, b);
        d_strcat += r1 - c1 != r2 - c2 || differ();
    }
    /* strchr: a byte present, the same as a negative int, 0, one absent */
    cv = n > 0 ? (unsigned char)a[rnd(n)] : 'q';
    d_strchr += strchr(a, cv) != c_strchr(a, cv);
    d_strchr += strchr(a, cv - 256) != c_strchr(a, cv - 256);
    d_strchr += strchr(a, 0) != c_strchr(a, 0);
    d_strchr += strchr(a, 0x7F) != c_strchr(a, 0x7F);
    /* memchr: within m bytes, with bits above the byte, and a count of 0 */
    m = rnd(n + 1);
    d_memchr += memchr(a, cv, m) != c_memchr(a, cv, m);
    d_memchr += memchr(a, cv + 256, m) != c_memchr(a, cv + 256, m);
    d_memchr += memchr(a, cv, 0) != c_memchr(a, cv, 0);
    /* memcpy and memset at an offset, between guard bytes */
    len = k < 10 ? (unsigned)k : rnd(300);
    off = rnd(300);
    c_memset(c1, 0x33, sizeof c1);
    c_memset(c2, 0x33, sizeof c2);
    r1 = memcpy(c1 + off, a, len < n ? len : n);
    r2 = c_memcpy(c2 + off, a, len < n ? len : n);
    d_memcpy += r1 - c1 != r2 - c2 || differ();
    cv = (int)rnd(600) - 100;
    r1 = memset(c1 + off, cv, len);
    r2 = c_memset(c2 + off, cv, len);
    d_memset += r1 - c1 != r2 - c2 || differ();
    /* memmove within one buffer, either overlap or none */
    fill(c1, 690, 1);
    c_memcpy(c2, c1, sizeof c1);
    m = rnd(350);
    off = rnd(350);
    r1 = memmove(c1 + m, c1 + off, len);
    r2 = c_memmove(c2 + m, c2 + off, len);
    d_memmove += r1 - c1 != r2 - c2 || differ();
}

int main(void)
{
    int k;

    for (k = 0; k < CASES; k++)
        one_case(k);
    check(d_strlen, 0);
    check(d_strcmp, 0);
    check(d_strcpy, 0);
    check(d_strcat, 0);
    check(d_strchr, 0);
    check(d_memchr, 0);
    check(d_memcpy, 0);
    check(d_memset, 0);
    check(d_memmove, 0);
    return finish();
}
