/* t_c99lib.c - the library's C99 additions (N4): snprintf and vsnprintf,
 * printf's and scanf's hh, z and t, printf's %a (glibc's layout), the
 * v*scanf family, scanf and strtod's hexadecimal, inf and nan forms,
 * strtof and strtold, isblank, va_copy, and the headers <stdbool.h>,
 * <inttypes.h> and <iso646.h>; and that the C89 functions strict mode
 * names (strtod, atof, the scanf family) still read only C89's forms. Every expected value is worked out by hand from the bits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <iso646.h>
#include <ctype.h>
#include "check.h"

char buf[100];
double dzero;

unsigned long word(unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

void checkd(double d, unsigned long hi, unsigned long lo)
{
    unsigned char *p;

    p = (unsigned char *)&d;
    check(word(p + 4) == hi && word(p) == lo, 1);
}

void checkf(float f, unsigned long bits)
{
    check(word((unsigned char *)&f) == bits, 1);
}

/* a printf into buf, checked against want, its count too */
void row(const char *want, int n)
{
    check_str(buf, (char *)want);
    check(n, (int)strlen(want));
}

int vsn(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

/* the sum of n ints, twice over: once through a copy */
int sum_twice(int n, ...)
{
    va_list ap;
    va_list again;
    int i;
    int s;

    va_start(ap, n);
    va_copy(again, ap);
    s = 0;
    for (i = 0; i < n; i++)
        s += va_arg(ap, int);
    for (i = 0; i < n; i++)
        s += va_arg(again, int);
    va_end(again);
    va_end(ap);
    return s;
}

int vss(const char *s, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

void snprintf_checks(void)
{
    memset(buf, 'z', sizeof buf);
    check(snprintf(buf, 5, "%d", 123456), 6);   /* the whole length */
    check_str(buf, "1234");
    check(buf[5], 'z');                         /* nothing past the limit */
    check(snprintf(NULL, 0, "%s", "hello"), 5);
    check(snprintf(buf, 1, "abc"), 3);
    check(buf[0], 0);
    check(snprintf(buf, sizeof buf, "%s-%d", "ab", 7), 4);
    check_str(buf, "ab-7");
    memset(buf, 'z', sizeof buf);
    check(vsn(buf, 3, "%x", 0xABCD), 4);
    check_str(buf, "ab");
    check(buf[3], 'z');
}

void length_checks(void)
{
    signed char sc;
    unsigned char uc;
    int arr[10];
    int *p = arr;

    row("44", sprintf(buf, "%hhd", 300));
    row("-56", sprintf(buf, "%hhd", 200));
    row("255", sprintf(buf, "%hhu", -1));
    row("34", sprintf(buf, "%hhx", 0x1234));
    row("3", sprintf(buf, "%zu", sizeof(int)));
    row("5", sprintf(buf, "%td", (p + 5) - p));
    row("-7", sprintf(buf, "%zd", -7));
    sprintf(buf, "abc%hhn", &sc);
    check(sc, 3);
    check(sscanf("300", "%hhd", &sc), 1);
    check(sc, 44);
    check(sscanf("-1", "%hhu", &uc), 1);
    check(uc, 255);
    check(sscanf("ff 12", "%hhx %zu", &uc, (size_t *)&arr[0]), 2);
    check(uc, 255);
    check(arr[0], 12);
}

void hex_print_checks(void)
{
    row("0x1p+0", sprintf(buf, "%a", 1.0));
    row("0x1p-1", sprintf(buf, "%a", 0.5));
    row("0x1.8p+3", sprintf(buf, "%a", 12.0));
    row("-0x1.999999999999ap-4", sprintf(buf, "%a", -0.1));
    row("0x0p+0", sprintf(buf, "%a", 0.0));
    row("-0x0p+0", sprintf(buf, "%a", -0.0));
    row("0x0.0000000000001p-1022", sprintf(buf, "%a", 0x1p-1074));
    row("0x1.fffffffffffffp+1023", sprintf(buf, "%a", 0x1.fffffffffffffp1023));
    row("0X1.FFP+7", sprintf(buf, "%A", 255.5));
    row("0x1.8p+0", sprintf(buf, "%.1a", 1.5));
    row("0x2p+0", sprintf(buf, "%.0a", 1.5));            /* a tie, 1 odd: up */
    row("0x1p+0", sprintf(buf, "%.0a", 1.25));
    row("0x1.9ap-4", sprintf(buf, "%.2a", 0.1));
    row("0x2.00p+0", sprintf(buf, "%.2a", 0x1.fffp0));   /* carried to the leading digit */
    row("0x1.000p+0", sprintf(buf, "%.3a", 1.0));
    row("0x1.p+0", sprintf(buf, "%#.0a", 1.0));
    row("  0x1.0p+0", sprintf(buf, "%10.1a", 1.0));
    row("0x1p+0    |", sprintf(buf, "%-10a|", 1.0));
    row("0x00001p+0", sprintf(buf, "%010a", 1.0));
    row("+0x1p+1", sprintf(buf, "%+a", 2.0));
    row(" 0x1p+1", sprintf(buf, "% a", 2.0));
    row("inf", sprintf(buf, "%a", 1.0 / dzero));
    row("-INF", sprintf(buf, "%A", -1.0 / dzero));
}

void scan_checks(void)
{
    double d;
    float f;
    int i;
    char c;

    check(sscanf("0x1.8p1", "%lf", &d), 1);
    checkd(d, 0x40080000UL, 0);                 /* 3 */
    check(sscanf("-inf", "%lf", &d), 1);
    checkd(d, 0xFFF00000UL, 0);
    check(sscanf("NaN", "%f", &f), 1);
    checkf(f, 0x7FC00000UL);
    check(sscanf("0x1p-2", "%a", &f), 1);
    checkf(f, 0x3E800000UL);                    /* 0.25 */
    check(sscanf("infinity x", "%lf %c", &d, &c), 2);
    checkd(d, 0x7FF00000UL, 0);
    check(c, 'x');
    check(sscanf("0xz", "%lf", &d), 0);         /* "0x": more read than "0" */
    check(sscanf("12.5", "%A", &f), 1);
    checkf(f, 0x41480000UL);
    check(vss("7 0x10", "%d %x", &i, (unsigned int *)&buf[0]), 2);
    check(i, 7);
}

void strtod_checks(void)
{
    char *end;
    const char *s;

    s = "0x1.8p1xyz";
    checkd(strtod(s, &end), 0x40080000UL, 0);
    check(end - s, 7);
    s = "0x";
    checkd(strtod(s, &end), 0, 0);
    check(end - s, 1);                          /* the 0 alone */
    s = "0xg";
    strtod(s, &end);
    check(end - s, 1);
    s = "  -Infinity!";
    checkd(strtod(s, &end), 0xFFF00000UL, 0);
    check(end - s, 11);
    s = "infin";
    checkd(strtod(s, &end), 0x7FF00000UL, 0);
    check(end - s, 3);
    s = "nan(abc)z";
    checkd(strtod(s, &end), 0x7FF80000UL, 0);
    check(end - s, 8);
    s = "nan(abc";
    strtod(s, &end);
    check(end - s, 3);
    s = "0x1p";
    checkd(strtod(s, &end), 0x3FF00000UL, 0);
    check(end - s, 3);
    s = "0X.8P+1";
    checkd(strtod(s, &end), 0x3FF00000UL, 0);
    check(end - s, 7);
    checkd(strtod("0x1.fffffffffffff8p0", NULL), 0x40000000UL, 0);     /* a tie, odd below: up to 2 */
    checkd(strtod("0x1.00000000000008p0", NULL), 0x3FF00000UL, 0);     /* a tie, even below: stays */
    checkd(strtod("0x1.000000000000080000000001p0", NULL), 0x3FF00000UL, 1);   /* a dropped digit */
    errno = 0;
    checkd(strtod("0x1p-1074", NULL), 0, 1);
    check(errno, 0);
    checkd(strtod("0x1p-1080", NULL), 0, 0);
    check(errno, ERANGE);
    errno = 0;
    checkd(strtod("0x1p1024", NULL), 0x7FF00000UL, 0);
    check(errno, ERANGE);
    errno = 0;
    checkd(strtod("-nan", NULL), 0xFFF80000UL, 0);
    check(errno, 0);
    checkd(strtod("1.5", NULL), 0x3FF80000UL, 0);       /* decimal, unchanged */
    checkd(strtod("00012", NULL), 0x40280000UL, 0);
}

void strtof_checks(void)
{
    char *end;
    const char *s;

    checkf(strtof("0.1", NULL), 0x3DCCCCCDUL);
    checkf(strtof("0x1.000001p0", NULL), 0x3F800000UL);         /* a tie: to even */
    checkf(strtof("0x1.0000011p0", NULL), 0x3F800001UL);
    checkf(strtof("1.00000005960464477539062500001", NULL), 0x3F800001UL);    /* not through double */
    errno = 0;
    checkf(strtof("1e40", NULL), 0x7F800000UL);
    check(errno, ERANGE);
    errno = 0;
    checkf(strtof("1e-50", NULL), 0);
    check(errno, ERANGE);
    errno = 0;
    checkf(strtof("-inf", NULL), 0xFF800000UL);
    check(errno, 0);
    s = "x";
    checkf(strtof(s, &end), 0);
    check(end == s, 1);
    checkd((double)strtold("2.5", NULL), 0x40040000UL, 0);
}

void header_checks(void)
{
    bool b = 7;
    int8_t i8;
    uint8_t u8;
    int32_t i32 = -5;
    uint64_t u64 = 1ULL << 40;
    imaxdiv_t q;
    char *end;
    int x = 6;

    check(b, 1);
    check(b == true, 1);
    check(sizeof(bool), 1);
    check(false, 0);
    check(__bool_true_false_are_defined, 1);
    row("-5", sprintf(buf, "%" PRId32, i32));
    row("1099511627776", sprintf(buf, "%" PRIu64, u64));
    row("ff", sprintf(buf, "%" PRIx8, 255));
    row("-1", sprintf(buf, "%" PRIdMAX, (intmax_t)-1));
    check(sscanf("200 -3", "%" SCNu8 " %" SCNd8, &u8, &i8), 2);
    check(u8, 200);
    check(i8, -3);
    check(imaxabs(-5) == 5, 1);
    q = imaxdiv(17, 5);
    check(q.quot == 3 && q.rem == 2, 1);
    q = imaxdiv(-17, 5);
    check(q.quot == -3 && q.rem == -2, 1);
    check(strtoimax("-123z", &end, 10) == -123, 1);
    check(*end, 'z');
    check(strtoumax("ff", NULL, 16) == 255, 1);
    check((1 and 0) == 0, 1);
    check((1 or 0) == 1, 1);
    check(not 0, 1);
    check(3 not_eq 4, 1);
    check((6 bitand 3) == 2 and (6 bitor 1) == 7 and (6 xor 3) == 5 and compl 0 == -1, 1);
    x and_eq 3;
    check(x, 2);
    x or_eq 8;
    check(x, 10);
    x xor_eq 2;
    check(x, 8);
    check(isblank(' ') && isblank('\t') && !isblank('\n') && !isblank('x'), 1);
    check(sum_twice(3, 1, 2, 4), 14);
}

/* C89's own functions, which strict mode's headers name: last in the
 * file, since the #undefs reach to its end */
#undef strtod
#undef atof
#undef sscanf

void c89_checks(void)
{
    char *end;
    const char *s;
    double d;
    int n;

    s = "inf";
    checkd(strtod(s, &end), 0, 0);
    check(end == s, 1);                         /* no number at all */
    s = "0x1p3";
    checkd(strtod(s, &end), 0, 0);
    check(end - s, 1);                          /* the 0 */
    s = "-nan";
    strtod(s, &end);
    check(end == s, 1);
    checkd(atof("0x10"), 0, 0);
    checkd(atof("12.5"), 0x40290000UL, 0);
    n = -1;
    check(sscanf("0x1p3", "%lf%n", &d, &n), 1);
    checkd(d, 0, 0);
    check(n, 1);
    check(sscanf("inf", "%lf", &d), 0);
    check(sscanf("1.5e1", "%lf", &d), 1);
    checkd(d, 0x402E0000UL, 0);                 /* 15 */
}

int main(void)
{
    snprintf_checks();
    length_checks();
    hex_print_checks();
    scan_checks();
    strtod_checks();
    strtof_checks();
    header_checks();
    c89_checks();
    return finish();
}
