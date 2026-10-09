/* rtref_gen.c - built and run on the PC by test_rt.py: checks
 * lib/rt/rt_ref.c (the C of rt.s's multiply and divide helpers) against
 * the PC's own arithmetic, then writes the cases and the reference's
 * answers as a C header for t_rtref.c, which runs the helpers on them on
 * the emulator.
 *
 *     rtref_gen OUT.h [WRONG_ROW]
 *
 * The operands: edge values chosen to reach every path of each helper
 * (divisors 1..127, 128..255, two and three bytes, 0x800000 and up;
 * dividends of each byte length, and below the divisor), every pair of
 * them, then random ones of random byte lengths. WRONG_ROW, for the
 * guard, makes that row's product wrong in the header. Exit status 0, or
 * 1 with the first disagreement printed.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../../lib/rt/rt_ref.c"

static const unsigned long edge24[] = {
    0, 1, 2, 3, 7, 8, 9, 10, 13, 26, 100, 126, 127, 128, 129, 200, 254, 255, 256, 257, 1000,
    0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x10001, 0x123456, 0x7FFFFE, 0x7FFFFF, 0x800000, 0x800001,
    0xFFFFFE, 0xFFFFFF
};

static const unsigned long edge32[] = {
    0, 1, 2, 3, 10, 127, 128, 255, 256, 1000, 0xFFFF, 0x10000, 0x7FFFFF, 0x800000, 0x800001,
    0xFFFFFF, 0x1000000, 0x1000001, 0x12345678UL, 0x7FFFFFFFUL, 0x80000000UL, 0x80000001UL,
    0xFFFFFFFEUL, 0xFFFFFFFFUL, 1000000000UL, 3000000000UL, 0xFF00FF00UL, 0x00FF00FFUL
};

#define NE24 (sizeof edge24 / sizeof edge24[0])
#define NE32 (sizeof edge32 / sizeof edge32[0])
#define RANDOM 1000

static unsigned long seed = 12345UL;

/* the classic linear congruential generator, its middle bits */
static unsigned long rnd(void)
{
    seed = (seed * 1103515245UL + 12345UL) & M32;
    return seed >> 8 & 0xFFFF;
}

/* a random value of 1 to `max` bytes, a random length first */
static unsigned long rnd_bytes(int max)
{
    unsigned long v;
    int n;
    int i;

    n = 1 + (int)(rnd() % max);
    v = 0;
    for (i = 0; i < n; i++)
        v = v << 8 | (rnd() & 0xFF);
    return v;
}

static long long sx24(unsigned long v)
{
    return v & 0x800000UL ? (long long)v - 0x1000000LL : (long long)v;
}

static long long sx32(unsigned long v)
{
    return v & 0x80000000UL ? (long long)v - 0x100000000LL : (long long)v;
}

static int bad;

static void disagree(const char *what, unsigned long n, unsigned long d, unsigned long got, unsigned long want)
{
    if (!bad)
        printf("rt_ref.c disagrees with the PC: %s of %lx and %lx gives %lx, not %lx\n", what, n, d, got, want);
    bad = 1;
}

/* one 24-bit row: the reference's answers, checked against the PC's */
static void row24(FILE *f, unsigned long n, unsigned long d, int wrong)
{
    unsigned long p;
    unsigned long q;
    unsigned long r;
    unsigned long qs;
    unsigned long rs;

    p = ref_imul(n, d);
    q = ref_idivu(n, d, &r);
    qs = ref_idivs(n, d);
    rs = ref_irems(n, d);
    if (p != (n * d & M24))
        disagree("*", n, d, p, n * d & M24);
    if (d != 0) {
        if (q != n / d || r != n % d)
            disagree("unsigned / %", n, d, q, n / d);
        if (qs != ((unsigned long)(sx24(n) / sx24(d)) & M24) || rs != ((unsigned long)(sx24(n) % sx24(d)) & M24))
            disagree("signed / %", n, d, qs, (unsigned long)(sx24(n) / sx24(d)) & M24);
    }
    fprintf(f, "{0x%lx,0x%lx,0x%lx,0x%lx,0x%lx,0x%lx,0x%lx},\n", n, d, wrong ? p ^ 1 : p, q, r, qs, rs);
}

static void row32(FILE *f, unsigned long n, unsigned long d, int wrong)
{
    unsigned long p;
    unsigned long q;
    unsigned long r;
    unsigned long qs;
    unsigned long rs;

    if (d == 0)
        return;                 /* no particular answer (rt.s), so no row */
    p = ref_lmul(n, d);
    q = ref_ldivmodu(n, d, &r);
    qs = ref_ldivs(n, d);
    rs = ref_lrems(n, d);
    if (p != (n * d & M32))
        disagree("long *", n, d, p, n * d & M32);
    if (q != n / d || r != n % d)
        disagree("unsigned long / %", n, d, q, n / d);
    if (qs != ((unsigned long)(sx32(n) / sx32(d)) & M32) || rs != ((unsigned long)(sx32(n) % sx32(d)) & M32))
        disagree("long / %", n, d, qs, (unsigned long)(sx32(n) / sx32(d)) & M32);
    fprintf(f, "{0x%lxUL,0x%lxUL,0x%lxUL,0x%lxUL,0x%lxUL,0x%lxUL,0x%lxUL},\n", n, d, wrong ? p ^ 1 : p, q, r, qs,
            rs);
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned i;
    unsigned j;
    int k;
    int wrong;

    if (argc < 2)
        return 2;
    wrong = argc > 2 ? atoi(argv[2]) : 0;
    f = fopen(argv[1], "w");
    if (f == NULL)
        return 2;
    fprintf(f, "/* written by tests/rt/rtref_gen.c: n, d, then the reference's n*d,\n"
               " * n/d and n%%d unsigned, n/d and n%%d signed (two's complement bits) */\n");
    k = 0;
    fprintf(f, "const unsigned long rows24[][7] = {\n");
    for (i = 0; i < NE24; i++)
        for (j = 0; j < NE24; j++) {
            k++;
            row24(f, edge24[i], edge24[j], k == wrong);
        }
    for (i = 0; i < RANDOM; i++) {
        k++;
        row24(f, rnd_bytes(3), rnd_bytes(3), k == wrong);
    }
    fprintf(f, "};\n#define N24 %d\n", k);
    k = 0;
    fprintf(f, "const unsigned long rows32[][7] = {\n");
    for (i = 0; i < NE32; i++)
        for (j = 0; j < NE32; j++)
            if (edge32[j] != 0) {
                k++;
                row32(f, edge32[i], edge32[j], 0);
            }
    for (i = 0; i < RANDOM; i++) {
        unsigned long d = rnd_bytes(4);
        if (d != 0) {
            k++;
            row32(f, rnd_bytes(4), d, 0);
        }
    }
    fprintf(f, "};\n#define N32 %d\n", k);
    fclose(f);
    return bad;
}
