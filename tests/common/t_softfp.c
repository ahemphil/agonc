/* t_softfp.c - src/cc1/softfp.c built by the host compiler and by
 * agonc gives the same bits (test_softfp.py's A test): a fixed stream of
 * operands through every operation, one checksum line per operation. No
 * floating point of its own, so any compiler builds it. (Whether those
 * bits are right is the H test's business: t_softfp_host.c against the
 * PC's arithmetic.)
 *
 *     t_softfp [cases per operation]
 */

#include <stdio.h>
#include <stdlib.h>
#include "softfp.h"

static unsigned long seed = 88172645UL;

static unsigned long rnd(void)
{
    seed ^= (seed << 13) & 0xFFFFFFFFUL;
    seed ^= seed >> 17;
    seed ^= (seed << 5) & 0xFFFFFFFFUL;
    return seed & 0xFFFFFFFFUL;
}

static unsigned long hash;

static void mix(unsigned long v)
{
    hash = (hash * 31 + (v & 0xFFFFFFFFUL)) & 0xFFFFFFFFUL;
}

/* The operands: exponents over the whole range and crowded at its ends,
 * fractions random or patterned; near, if not negative, an exponent to
 * stay close to. */
static void operand64(struct sf64 *s, int near)
{
    int k;
    int e;
    unsigned long hi;
    unsigned long lo;

    k = (int)(rnd() % 16);
    if (near >= 0 && k < 8)
        e = near + (int)(rnd() % 7) - 3;
    else if (k < 4)
        e = (int)(rnd() % 2048);
    else if (k < 7)
        e = (int)(rnd() % 64);
    else if (k < 9)
        e = 2047 - (int)(rnd() % 64);
    else
        e = 1023 + (int)(rnd() % 121) - 60;
    if (e < 0)
        e = 0;
    if (e > 2047)
        e = 2047;
    hi = rnd() & 0xFFFFFUL;
    lo = rnd();
    k = (int)(rnd() % 5);
    if (k == 1) {
        hi = hi | 0xFFFFFUL;
        lo = lo | 0xFFFFFF00UL;
    } else if (k == 2) {
        lo = lo & 0xFF;
    } else if (k == 3) {
        hi = 0;
        lo = 1UL << (int)(rnd() % 32);
    }
    s->hi = (rnd() & 1) << 31 | (unsigned long)e << 20 | hi;
    s->lo = lo;
}

static unsigned long operand32(int near)
{
    int k;
    int e;
    unsigned long frac;

    k = (int)(rnd() % 16);
    if (near >= 0 && k < 8)
        e = near + (int)(rnd() % 7) - 3;
    else if (k < 4)
        e = (int)(rnd() % 256);
    else if (k < 7)
        e = (int)(rnd() % 32);
    else if (k < 9)
        e = 255 - (int)(rnd() % 32);
    else
        e = 127 + (int)(rnd() % 61) - 30;
    if (e < 0)
        e = 0;
    if (e > 255)
        e = 255;
    frac = rnd() & 0x7FFFFFUL;
    if (rnd() % 4 == 0)
        frac = 1UL << (int)(rnd() % 23);
    return (rnd() & 1) << 31 | (unsigned long)e << 23 | frac;
}

int main(int argc, char **argv)
{
    static char *names[] = { "dadd", "dsub", "dmul", "ddiv", "dsqrt", "dcmp", "fadd", "fsub", "fmul", "fdiv",
                             "fsqrt", "fcmp", "conv" };
    long n;
    long i;
    int op;
    struct sf64 a;
    struct sf64 b;
    struct sf64 r;
    unsigned long fa;
    unsigned long fb;

    n = argc > 1 ? atol(argv[1]) : 600;
    for (op = 0; op < 13; op++) {
        hash = 0;
        for (i = 0; i < n; i++) {
            if (op < 6) {
                operand64(&a, -1);
                operand64(&b, op < 2 ? (int)(a.hi >> 20 & 0x7FF) : -1);
                if (op == 0)
                    sf64_add(&r, &a, &b);
                else if (op == 1)
                    sf64_sub(&r, &a, &b);
                else if (op == 2)
                    sf64_mul(&r, &a, &b);
                else if (op == 3)
                    sf64_div(&r, &a, &b);
                else if (op == 4)
                    sf64_sqrt(&r, &a);
                else
                    r.hi = r.lo = (unsigned long)(sf64_cmp(&a, &b) + 1);
                mix(r.hi);
                mix(r.lo);
            } else if (op < 12) {
                fa = operand32(-1);
                fb = operand32(op < 8 ? (int)(fa >> 23 & 0xFF) : -1);
                if (op == 6)
                    mix(sf32_add(fa, fb));
                else if (op == 7)
                    mix(sf32_sub(fa, fb));
                else if (op == 8)
                    mix(sf32_mul(fa, fb));
                else if (op == 9)
                    mix(sf32_div(fa, fb));
                else if (op == 10)
                    mix(sf32_sqrt(fa));
                else
                    mix((unsigned long)(sf32_cmp(fa, fb) + 1));
            } else {
                operand64(&a, -1);
                mix(sf32_from_f64(&a));
                mix(sf64_to_long(&a, 1));
                mix(sf64_to_long(&a, 0));
                fa = operand32(-1);
                sf64_from_f32(&r, fa);
                mix(r.hi);
                mix(r.lo);
                mix(sf32_to_long(fa, 1));
                fb = rnd() >> (int)(rnd() % 32);
                sf64_from_long(&r, fb, 1);
                mix(r.hi);
                mix(r.lo);
                sf64_from_long(&r, fb, 0);
                mix(r.hi);
                mix(r.lo);
                mix(sf32_from_long(fb, 1));
                mix(sf32_from_long(fb, 0));
            }
        }
        printf("%s %ld %08lx\n", names[op], n, hash);
    }
    return 0;
}
