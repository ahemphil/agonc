/* t_softfp_host.c - src/cc1/softfp.c against the PC's own IEEE 754
 * arithmetic, bit for bit (test_softfp.py's H test). Every operation gets
 * random operands, weighted toward the hard cases (subnormals, overflow,
 * cancellation, ties), and a table of special values. A NaN result matches
 * any NaN (the PC's default NaN differs by processor). Built by the host
 * compiler with contraction off. Prints "op cases mismatches" per
 * operation, and the first mismatches; exits 0 if there are none.
 *
 *     t_softfp_host [cases per operation [guard]]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "softfp.h"

static unsigned long seed = 2463534242UL;
static int shown;
static int guard;               /* "guard": one expected result made wrong, which must be reported */

static unsigned long rnd(void)
{
    seed ^= (seed << 13) & 0xFFFFFFFFUL;
    seed ^= seed >> 17;
    seed ^= (seed << 5) & 0xFFFFFFFFUL;
    return seed & 0xFFFFFFFFUL;
}

/* ---- the host's own values, bit patterns read little-endian ---------------------- */

static void from_double(struct sf64 *s, double d)
{
    unsigned char b[8];

    memcpy(b, &d, 8);
    s->lo = b[0] | (unsigned long)b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
    s->hi = b[4] | (unsigned long)b[5] << 8 | (unsigned long)b[6] << 16 | (unsigned long)b[7] << 24;
}

static double to_double(const struct sf64 *s)
{
    unsigned char b[8];
    double d;
    int i;

    for (i = 0; i < 4; i++) {
        b[i] = (unsigned char)(s->lo >> (8 * i));
        b[4 + i] = (unsigned char)(s->hi >> (8 * i));
    }
    memcpy(&d, b, 8);
    return d;
}

static unsigned long from_float(float f)
{
    unsigned char b[4];

    memcpy(b, &f, 4);
    return b[0] | (unsigned long)b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
}

static float to_float(unsigned long v)
{
    unsigned char b[4];
    float f;
    int i;

    for (i = 0; i < 4; i++)
        b[i] = (unsigned char)(v >> (8 * i));
    memcpy(&f, b, 4);
    return f;
}

static int nan64(const struct sf64 *s)
{
    return (s->hi >> 20 & 0x7FF) == 0x7FF && ((s->hi & 0xFFFFF) || s->lo);
}

static int nan32(unsigned long v)
{
    return (v >> 23 & 0xFF) == 0xFF && (v & 0x7FFFFF);
}

/* ---- operands ---------------------------------------------------------------------- */

static unsigned long special64[][2] = {
    { 0, 0 }, { 0x80000000UL, 0 }, { 0x7FF00000UL, 0 }, { 0xFFF00000UL, 0 }, { 0x7FF80000UL, 0 },
    { 0x7FF00000UL, 1 }, { 0, 1 }, { 0x80000000UL, 1 }, { 0x000FFFFFUL, 0xFFFFFFFFUL },
    { 0x00100000UL, 0 }, { 0x7FEFFFFFUL, 0xFFFFFFFFUL }, { 0x3FF00000UL, 0 }, { 0xBFF00000UL, 0 },
    { 0x3FE00000UL, 0 }, { 0x40000000UL, 0 }, { 0x3FF00000UL, 1 }, { 0x3FEFFFFFUL, 0xFFFFFFFFUL },
    { 0x41DFFFFFUL, 0xFFC00000UL }, { 0x41E00000UL, 0 }, { 0xC1E00000UL, 0 }, { 0x41EFFFFFUL, 0xFFE00000UL },
    { 0x43300000UL, 0 }, { 0x00000000UL, 0x80000000UL }, { 0x36A00000UL, 0 }, { 0x47EFFFFFUL, 0xE0000000UL },
};

#define NSPECIAL64 (sizeof special64 / sizeof special64[0])

/* A random binary64: 1 in 16 a special value, the rest with exponents
 * spread over the whole range, crowded near its ends and near 1, and
 * fractions random, all ones, all zeros or one bit (for ties). near, if
 * not negative, is an exponent to stay close to (cancellation). */
static void operand64(struct sf64 *s, int near)
{
    int k;
    int e;
    int pattern;
    unsigned long hi;
    unsigned long lo;

    k = (int)(rnd() % 16);
    if (k == 0) {
        k = (int)(rnd() % NSPECIAL64);
        s->hi = special64[k][0];
        s->lo = special64[k][1];
        return;
    }
    if (near >= 0 && k < 8)
        e = near + (int)(rnd() % 7) - 3;
    else if (k < 5)
        e = (int)(rnd() % 2048);
    else if (k < 8)
        e = (int)(rnd() % 64);
    else if (k < 10)
        e = 2047 - (int)(rnd() % 64);
    else
        e = 1023 + (int)(rnd() % 121) - 60;
    if (e < 0)
        e = 0;
    if (e > 2046)
        e = 2046;
    hi = rnd() & 0xFFFFF;
    lo = rnd();
    pattern = (int)(rnd() % 6);
    if (pattern == 1) {
        hi = hi | 0xFFFFF;
        lo = lo | 0xFFFFFF00UL;
    } else if (pattern == 2) {
        lo = lo & 0xFF;
    } else if (pattern == 3) {
        hi = 0;
        lo = 1UL << (rnd() % 32);
    } else if (pattern == 4) {
        lo = 0;
    }
    s->hi = (rnd() & 1) << 31 | (unsigned long)e << 20 | hi;
    s->lo = lo;
}

static unsigned long special32[] = {
    0, 0x80000000UL, 0x7F800000UL, 0xFF800000UL, 0x7FC00000UL, 0x7F800001UL, 1, 0x80000001UL, 0x007FFFFFUL,
    0x00800000UL, 0x7F7FFFFFUL, 0x3F800000UL, 0xBF800000UL, 0x3F000000UL, 0x4F000000UL, 0xCF000000UL,
    0x4F800000UL, 0x3F800001UL,
};

#define NSPECIAL32 (sizeof special32 / sizeof special32[0])

static unsigned long operand32(int near)
{
    int k;
    int e;
    unsigned long frac;

    k = (int)(rnd() % 16);
    if (k == 0)
        return special32[rnd() % NSPECIAL32];
    if (near >= 0 && k < 8)
        e = near + (int)(rnd() % 7) - 3;
    else if (k < 5)
        e = (int)(rnd() % 256);
    else if (k < 8)
        e = (int)(rnd() % 32);
    else if (k < 10)
        e = 255 - (int)(rnd() % 32);
    else
        e = 127 + (int)(rnd() % 61) - 30;
    if (e < 0)
        e = 0;
    if (e > 254)
        e = 254;
    frac = rnd() & 0x7FFFFF;
    k = (int)(rnd() % 5);
    if (k == 1)
        frac = frac | 0x7FFF00;
    else if (k == 2)
        frac = frac & 0xF;
    else if (k == 3)
        frac = 1UL << (rnd() % 23);
    return (rnd() & 1) << 31 | (unsigned long)e << 23 | frac;
}

/* ---- checking -------------------------------------------------------------------- */

static int report64(const char *op, const struct sf64 *a, const struct sf64 *b, const struct sf64 *got,
                    const struct sf64 *want)
{
    if ((nan64(got) && nan64(want)) || (got->hi == want->hi && got->lo == want->lo))
        return 0;
    if (shown < 20) {
        printf("  %s %08lx%08lx %08lx%08lx: %08lx%08lx, expected %08lx%08lx\n", op, a->hi, a->lo,
               b ? b->hi : 0, b ? b->lo : 0, got->hi, got->lo, want->hi, want->lo);
        shown++;
    }
    return 1;
}

static int report32(const char *op, unsigned long a, unsigned long b, unsigned long got, unsigned long want)
{
    if ((nan32(got) && nan32(want)) || got == want)
        return 0;
    if (shown < 20) {
        printf("  %s %08lx %08lx: %08lx, expected %08lx\n", op, a, b, got, want);
        shown++;
    }
    return 1;
}

static int host_cmp(double x, double y)
{
    if (x != x || y != y)
        return 2;
    return x < y ? -1 : x > y ? 1 : 0;
}

static int host_cmpf(float x, float y)
{
    if (x != x || y != y)
        return 2;
    return x < y ? -1 : x > y ? 1 : 0;
}

int main(int argc, char **argv)
{
    static const char *ops64[] = { "dadd", "dsub", "dmul", "ddiv", "dsqrt", "dcmp" };
    static const char *ops32[] = { "fadd", "fsub", "fmul", "fdiv", "fsqrt", "fcmp" };
    long n;
    long i;
    int op;
    int bad;
    int total;
    struct sf64 a;
    struct sf64 b;
    struct sf64 got;
    struct sf64 want;
    double x;
    double y;
    unsigned long fa;
    unsigned long fb;
    unsigned long fgot;
    unsigned long fwant;
    float p;
    float q;

    unsigned long uv;

    n = argc > 1 ? atol(argv[1]) : 200000;
    guard = argc > 2;
    total = 0;
    for (op = 0; op < 6; op++) {
        bad = 0;
        for (i = 0; i < n; i++) {
            operand64(&a, -1);
            operand64(&b, op < 2 ? (int)(a.hi >> 20 & 0x7FF) : -1);
            x = to_double(&a);
            y = to_double(&b);
            if (op == 0) {
                sf64_add(&got, &a, &b);
                from_double(&want, x + y);
                if (guard && i == 0)
                    want.lo = want.lo ^ 1;
            } else if (op == 1) {
                sf64_sub(&got, &a, &b);
                from_double(&want, x - y);
            } else if (op == 2) {
                sf64_mul(&got, &a, &b);
                from_double(&want, x * y);
            } else if (op == 3) {
                sf64_div(&got, &a, &b);
                from_double(&want, x / y);
            } else if (op == 4) {
                sf64_sqrt(&got, &a);
                from_double(&want, sqrt(x));
            } else {
                got.hi = (unsigned long)(sf64_cmp(&a, &b) + 1);
                got.lo = 0;
                want.hi = (unsigned long)(host_cmp(x, y) + 1);
                want.lo = 0;
            }
            bad += report64(ops64[op], &a, &b, &got, &want);
        }
        printf("%s %ld %d\n", ops64[op], n, bad);
        total += bad;
    }
    for (op = 0; op < 6; op++) {
        bad = 0;
        for (i = 0; i < n; i++) {
            fa = operand32(-1);
            fb = operand32(op < 2 ? (int)(fa >> 23 & 0xFF) : -1);
            p = to_float(fa);
            q = to_float(fb);
            if (op == 0) {
                fgot = sf32_add(fa, fb);
                fwant = from_float(p + q);
            } else if (op == 1) {
                fgot = sf32_sub(fa, fb);
                fwant = from_float(p - q);
            } else if (op == 2) {
                fgot = sf32_mul(fa, fb);
                fwant = from_float(p * q);
            } else if (op == 3) {
                fgot = sf32_div(fa, fb);
                fwant = from_float(p / q);
            } else if (op == 4) {
                fgot = sf32_sqrt(fa);
                fwant = from_float((float)sqrt((double)p));
            } else {
                fgot = (unsigned long)(sf32_cmp(fa, fb) + 1);
                fwant = (unsigned long)(host_cmpf(p, q) + 1);
            }
            bad += report32(ops32[op], fa, fb, fgot, fwant);
        }
        printf("%s %ld %d\n", ops32[op], n, bad);
        total += bad;
    }
    /* conversions */
    bad = 0;
    for (i = 0; i < n; i++) {
        operand64(&a, -1);
        x = to_double(&a);
        fgot = sf32_from_f64(&a);
        bad += report32("d2f", a.hi, a.lo, fgot, from_float((float)x));
        fa = operand32(-1);
        sf64_from_f32(&got, fa);
        from_double(&want, (double)to_float(fa));
        bad += report64("f2d", &a, 0, &got, &want);
    }
    printf("d2f/f2d %ld %d\n", n, bad);
    total += bad;
    bad = 0;
    for (i = 0; i < n; i++) {
        uv = rnd();
        if (rnd() & 1)
            uv = uv >> (rnd() % 32);
        /* uv's bits as a signed 32-bit value, held exactly in a double */
        y = (double)(uv & 0x7FFFFFFFUL) - (uv & 0x80000000UL ? 2147483648.0 : 0.0);
        sf64_from_long(&got, uv, 1);
        from_double(&want, y);
        bad += report64("l2d", &a, 0, &got, &want);
        sf64_from_long(&got, uv, 0);
        from_double(&want, (double)uv);
        bad += report64("ul2d", &a, 0, &got, &want);
        bad += report32("l2f", uv, 1, sf32_from_long(uv, 1), from_float((float)y));
        bad += report32("ul2f", uv, 0, sf32_from_long(uv, 0), from_float((float)uv));
        /* back: an integer plus a fraction, truncated */
        x = y + (double)(rnd() % 1000) / 1000.0 * (y < 0 ? -1 : 1);
        from_double(&a, x);
        bad += report32("d2l", a.hi, a.lo, sf64_to_long(&a, 1), uv);
        x = (double)uv + (double)(rnd() % 1000) / 1000.0;
        from_double(&a, x);
        bad += report32("d2ul", a.hi, a.lo, sf64_to_long(&a, 0), uv);
        p = (float)x;
        bad += report32("f2ul", from_float(p), 0, sf32_to_long(from_float(p), 0),
                        p >= 4294967296.0f ? 0xFFFFFFFFUL : (unsigned long)p);
    }
    printf("int %ld %d\n", n, bad);
    total += bad;
    /* the saturating edges */
    bad = 0;
    from_double(&a, 3e9);
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 1), 0x7FFFFFFFUL);
    from_double(&a, -3e9);
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 1), 0x80000000UL);
    from_double(&a, -2147483648.0);
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 1), 0x80000000UL);
    from_double(&a, 5e9);
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 0), 0xFFFFFFFFUL);
    from_double(&a, -1.5);
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 0), 0);
    from_double(&a, -0.5);
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 1), 0);
    a.hi = 0x7FF80000UL;
    a.lo = 0;
    bad += report32("sat", a.hi, a.lo, sf64_to_long(&a, 1), 0);
    printf("edges 7 %d\n", bad);
    total += bad;
    printf("total mismatches %d\n", total);
    return total != 0;
}
