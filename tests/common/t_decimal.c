/* t_decimal.c - softfp.c's decimal conversions against gen_decimal.py's
 * exact answers (test_softfp.py's D tests): each D line's decimal as
 * binary64 and binary32, each P line's double formatted %.<p>e or %.<p>f
 * from sf64_to_decimal's digits. No floating point of its own, so the host
 * compiler and agonc build it alike.
 *
 *     t_decimal vectors.txt [lines [longest decimal]]
 *
 * With a longest decimal, D lines with more digits are skipped (the
 * emulator converts a 750-digit decimal in about a second: big integers
 * compiled by agonc).
 *
 * Prints "D <checked> <wrong>" and "P <checked> <wrong>", and the first
 * wrong lines; exits 0 if none were wrong.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "softfp.h"

static char line[2048];
static char text[900];
static char digits[900];
static int shown;

static unsigned long hex(const char *s, int n)
{
    unsigned long v;
    int i;
    int c;

    v = 0;
    for (i = 0; i < n; i++) {
        c = s[i];
        v = v << 4 | (unsigned long)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    }
    return v & 0xFFFFFFFFUL;
}

/* printf's %.<prec>e or %.<prec>f of a, made from sf64_to_decimal's digits */
static void format(const struct sf64 *a, int mode, int prec, char *out)
{
    int n;
    int dexp;
    int i;
    int pos;
    int e;

    if (a->hi >> 31)
        *out++ = '-';
    n = sf64_to_decimal(a, mode, prec, digits, &dexp);
    if (mode == 'e') {
        if (n == 0)
            dexp = 0;
        *out++ = n > 0 ? digits[0] : '0';
        if (prec > 0) {
            *out++ = '.';
            for (i = 1; i <= prec; i++)
                *out++ = i < n ? digits[i] : '0';
        }
        *out++ = 'e';
        *out++ = dexp < 0 ? '-' : '+';
        e = dexp < 0 ? -dexp : dexp;
        if (e >= 100)
            *out++ = (char)('0' + e / 100);
        *out++ = (char)('0' + e / 10 % 10);
        *out++ = (char)('0' + e % 10);
    } else {
        if (n == 0)
            dexp = 0;
        if (dexp < 0) {
            *out++ = '0';
        } else {
            for (pos = dexp; pos >= 0; pos--)
                *out++ = dexp - pos < n ? digits[dexp - pos] : '0';
        }
        if (prec > 0) {
            *out++ = '.';
            for (pos = -1; pos >= -prec; pos--) {
                i = dexp - pos;
                *out++ = n > 0 && i >= 0 && i < n ? digits[i] : '0';
            }
        }
    }
    *out = 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    long limit;
    long longest;
    long nd;
    long np;
    long bad_d;
    long bad_p;
    char *w[6];
    int k;
    char *p;
    struct sf64 got;
    unsigned long want_hi;
    unsigned long want_lo;
    unsigned long want32;
    unsigned long got32;
    int wrong;

    if (argc < 2 || (f = fopen(argv[1], "r")) == NULL) {
        printf("cannot read the vectors\n");
        return 2;
    }
    limit = argc > 2 ? atol(argv[2]) : 1000000L;
    longest = argc > 3 ? atol(argv[3]) : 100000L;
    nd = np = bad_d = bad_p = 0;
    while (nd + np < limit && fgets(line, sizeof line, f) != NULL) {
        k = 0;
        for (p = line; *p && k < 6;) {
            while (*p == ' ' || *p == '\n' || *p == '\r')
                *p++ = 0;
            if (!*p)
                break;
            w[k++] = p;
            while (*p && *p != ' ' && *p != '\n' && *p != '\r')
                p++;
        }
        if (k == 6 && w[0][0] == 'D' && (long)strlen(w[2]) <= longest) {
            nd++;
            want_hi = hex(w[4], 8);
            want_lo = hex(w[4] + 8, 8);
            want32 = hex(w[5], 8);
            sf64_from_decimal(&got, w[1][0] - '0', w[2], (int)strlen(w[2]), atol(w[3]));
            got32 = sf32_from_decimal(w[1][0] - '0', w[2], (int)strlen(w[2]), atol(w[3]));
            wrong = got.hi != want_hi || got.lo != want_lo || got32 != want32;
            if (wrong) {
                bad_d++;
                if (shown++ < 10)
                    printf("  D %c %.40s e%s: %08lx%08lx %08lx, expected %s %s\n", w[1][0], w[2], w[3], got.hi,
                           got.lo, got32, w[4], w[5]);
            }
        } else if (k == 5 && w[0][0] == 'P') {
            np++;
            got.hi = hex(w[1], 8);
            got.lo = hex(w[1] + 8, 8);
            format(&got, w[2][0], atoi(w[3]), text);
            if (strcmp(text, w[4]) != 0) {
                bad_p++;
                if (shown++ < 10)
                    printf("  P %s %%.%s%s: %s, expected %s\n", w[1], w[3], w[2], text, w[4]);
            }
        }
    }
    fclose(f);
    printf("D %ld %ld\n", nd, bad_d);
    printf("P %ld %ld\n", np, bad_p);
    return bad_d + bad_p != 0;
}
