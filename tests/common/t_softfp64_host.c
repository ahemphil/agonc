/* t_softfp64_host.c - softfp.c's conversions between the floating types and
 * 64-bit integers against the PC's own (C99, for long long; the PC only).
 *
 *     t_softfp64_host [cases [guard]]
 *
 * One line per conversion: its name, the cases, the mismatches.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "softfp.h"

static uint64_t state = 0x9E3779B97F4A7C15ULL;

static uint64_t rnd(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static uint64_t bits_d(double d)
{
    uint64_t u;

    memcpy(&u, &d, 8);
    return u;
}

static uint32_t bits_f(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

static double from_bits(uint64_t u)
{
    double d;

    memcpy(&d, &u, 8);
    return d;
}

/* every size of integer, both signs */
static uint64_t integer(void)
{
    uint64_t v;

    v = rnd();
    switch (rnd() % 4) {
    case 0:
        return v;
    case 1:
        return v >> (rnd() % 64);
    case 2:
        return (uint64_t)-(int64_t)(v >> (rnd() % 64));
    default:
        return (v >> (rnd() % 64)) << (rnd() % 16);
    }
}

static int shown;

static int report(const char *op, uint64_t in, uint64_t got, uint64_t want)
{
    if (got == want)
        return 0;
    if (shown < 10) {
        printf("  %s %016llx: %016llx, the PC %016llx\n", op, (unsigned long long)in, (unsigned long long)got,
               (unsigned long long)want);
        shown++;
    }
    return 1;
}

int main(int argc, char **argv)
{
    long n;
    long i;
    int bad;
    int guard;
    uint64_t v;
    uint64_t want;
    unsigned long hi;
    unsigned long lo;
    struct sf64 d;
    double x;
    float f;

    n = argc > 1 ? atol(argv[1]) : 200000;
    guard = argc > 2;
    bad = 0;
    for (i = 0; i < n; i++) {
        v = integer();
        sf64_from_long64(&d, (unsigned long)(v >> 32), (unsigned long)(v & 0xFFFFFFFFu), 1);
        want = bits_d((double)(int64_t)v);
        if (guard && i == 0)
            want ^= 1;
        bad += report("ll2d", v, (uint64_t)d.hi << 32 | d.lo, want);
        sf64_from_long64(&d, (unsigned long)(v >> 32), (unsigned long)(v & 0xFFFFFFFFu), 0);
        bad += report("ull2d", v, (uint64_t)d.hi << 32 | d.lo, bits_d((double)v));
        bad += report("ll2f", v, sf32_from_long64((unsigned long)(v >> 32), (unsigned long)(v & 0xFFFFFFFFu), 1),
                      bits_f((float)(int64_t)v));
        bad += report("ull2f", v, sf32_from_long64((unsigned long)(v >> 32), (unsigned long)(v & 0xFFFFFFFFu), 0),
                      bits_f((float)v));
        /* back, from values in range with a fraction added */
        x = (double)(int64_t)(v >> 1 >> (rnd() % 63)) + (double)(rnd() % 1000) / 1000.0;
        if (rnd() & 1)
            x = -x;
        d.hi = (unsigned long)(bits_d(x) >> 32);
        d.lo = (unsigned long)(bits_d(x) & 0xFFFFFFFFu);
        sf64_to_long64(&d, 1, &hi, &lo);
        bad += report("d2ll", bits_d(x), (uint64_t)hi << 32 | lo, (uint64_t)(int64_t)x);
        if (x >= 0) {
            sf64_to_long64(&d, 0, &hi, &lo);
            bad += report("d2ull", bits_d(x), (uint64_t)hi << 32 | lo, (uint64_t)x);
        }
        f = (float)x;
        sf32_to_long64(bits_f(f), 1, &hi, &lo);
        bad += report("f2ll", bits_f(f), (uint64_t)hi << 32 | lo, (uint64_t)(int64_t)f);
    }
    /* the saturating edges */
    d.hi = 0x43E00000UL;                /* 2^63 */
    d.lo = 0;
    sf64_to_long64(&d, 1, &hi, &lo);
    bad += report("sat", 0, (uint64_t)hi << 32 | lo, 0x7FFFFFFFFFFFFFFFULL);
    d.hi = 0xC3E00000UL;                /* -2^63 */
    sf64_to_long64(&d, 1, &hi, &lo);
    bad += report("sat", 1, (uint64_t)hi << 32 | lo, 0x8000000000000000ULL);
    d.hi = 0x43F00000UL;                /* 2^64 */
    sf64_to_long64(&d, 0, &hi, &lo);
    bad += report("sat", 2, (uint64_t)hi << 32 | lo, 0xFFFFFFFFFFFFFFFFULL);
    d.hi = 0x7FF80000UL;                /* NaN */
    sf64_to_long64(&d, 1, &hi, &lo);
    bad += report("sat", 3, (uint64_t)hi << 32 | lo, 0);
    x = from_bits(0x43DFFFFFFFFFFFFFULL);       /* the largest double below 2^63 */
    d.hi = (unsigned long)(bits_d(x) >> 32);
    d.lo = (unsigned long)(bits_d(x) & 0xFFFFFFFFu);
    sf64_to_long64(&d, 1, &hi, &lo);
    bad += report("edge", 4, (uint64_t)hi << 32 | lo, (uint64_t)(int64_t)x);
    printf("long64 %ld %d\n", n, bad);
    return bad != 0;
}
