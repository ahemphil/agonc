/* t_fpeq.c - softfp.c's assembly kernels (FP_ASM 1: the 64-bit multiply,
 * the division and square root loops) give exactly the C's bits. Both
 * builds are linked in under the names a_ and c_ (fpeq_a.c, fpeq_c.c) and
 * run on the same operands: special values (zeros, infinities, NaNs,
 * subnormals, the extremes) against each other, then pseudo-random ones
 * of every exponent. Each check is one group of operations over all its
 * operands: the number of results that differ, which must be 0. Add,
 * subtract, compare and the conversions use the 64-bit helpers the
 * assembly also replaces (shifts, add, subtract, compare, leading zeros).
 * Add, subtract and multiply also have whole fast paths for normal
 * numbers; the special values include exact ties for their rounding
 * (1.5 * (1 + ulp), 1 + 2^-53, (1 + ulp) + 2^-53), and the random ones
 * reach every exponent distance.
 */

#include "check.h"
#include "../../src/cc1/softfp.h"

void a_sf64_mul(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void c_sf64_mul(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void a_sf64_div(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void c_sf64_div(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void a_sf64_sqrt(struct sf64 *r, const struct sf64 *a);
void c_sf64_sqrt(struct sf64 *r, const struct sf64 *a);
unsigned long a_sf32_mul(unsigned long a, unsigned long b);
unsigned long c_sf32_mul(unsigned long a, unsigned long b);
unsigned long a_sf32_div(unsigned long a, unsigned long b);
unsigned long c_sf32_div(unsigned long a, unsigned long b);
unsigned long a_sf32_sqrt(unsigned long a);
unsigned long c_sf32_sqrt(unsigned long a);
void a_sf64_add(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void c_sf64_add(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void a_sf64_sub(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void c_sf64_sub(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
int a_sf64_cmp(const struct sf64 *a, const struct sf64 *b);
int c_sf64_cmp(const struct sf64 *a, const struct sf64 *b);
unsigned long a_sf32_add(unsigned long a, unsigned long b);
unsigned long c_sf32_add(unsigned long a, unsigned long b);
unsigned long a_sf32_from_f64(const struct sf64 *a);
unsigned long c_sf32_from_f64(const struct sf64 *a);
void a_sf64_from_f32(struct sf64 *r, unsigned long a);
void c_sf64_from_f32(struct sf64 *r, unsigned long a);
unsigned long a_sf64_to_long(const struct sf64 *a, int is_signed);
unsigned long c_sf64_to_long(const struct sf64 *a, int is_signed);
void a_sf64_from_long(struct sf64 *r, unsigned long v, int is_signed);
void c_sf64_from_long(struct sf64 *r, unsigned long v, int is_signed);
void a_sf64_from_long64(struct sf64 *r, unsigned long hi, unsigned long lo, int is_signed);
void c_sf64_from_long64(struct sf64 *r, unsigned long hi, unsigned long lo, int is_signed);
void a_sf64_to_long64(const struct sf64 *a, int is_signed, unsigned long *hi, unsigned long *lo);
void c_sf64_to_long64(const struct sf64 *a, int is_signed, unsigned long *hi, unsigned long *lo);

#define NSPECIAL 17
unsigned long special[NSPECIAL][2] = {
    { 0x00000000UL, 0 }, { 0x80000000UL, 0 },                   /* +0, -0 */
    { 0x7FF00000UL, 0 }, { 0xFFF00000UL, 0 },                   /* +inf, -inf */
    { 0x7FF80000UL, 0 }, { 0x7FF00000UL, 1 },                   /* quiet, signalling NaN */
    { 0x00000000UL, 1 }, { 0x000FFFFFUL, 0xFFFFFFFFUL },        /* subnormals */
    { 0x00100000UL, 0 }, { 0x7FEFFFFFUL, 0xFFFFFFFFUL },        /* smallest normal, largest */
    { 0x3FF00000UL, 0 }, { 0xBFF00000UL, 0 },                   /* 1, -1 */
    { 0x40000000UL, 0 }, { 0x3FF00000UL, 1 },                   /* 2, 1 + ulp */
    { 0x3FF80000UL, 0 }, { 0x3FF00000UL, 3 },                   /* 1.5, 1 + 3 ulp: products that tie */
    { 0x3CA00000UL, 0 }                                         /* 2^-53: sums with 1 that tie */
};

unsigned long seed = 12345;

unsigned long next(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return seed;
}

/* A random double: any sign and exponent, often near 1 so that products
 * and quotients stay normal, sometimes anywhere. */
void random64(struct sf64 *x)
{
    unsigned long e;

    e = next() >> 16 & 0x7FF;
    if (next() & 1)
        e = 0x3FF + (e & 0x3F) - 0x20;
    x->hi = (next() & 0x80000000UL) | e << 20 | (next() & 0xFFFFFUL);
    x->lo = next() ^ next() << 13;
}

int same(const struct sf64 *x, const struct sf64 *y)
{
    return x->hi == y->hi && x->lo == y->lo;
}

int main(void)
{
    struct sf64 a;
    struct sf64 b;
    struct sf64 ra;
    struct sf64 rc;
    int i;
    int j;
    int bad_mul;
    int bad_div;
    int bad_sqrt;
    int bad32;
    int bad_add;
    int bad_cvt;
    unsigned long fa;
    unsigned long fb;
    unsigned long h1;
    unsigned long l1;
    unsigned long h2;
    unsigned long l2;

    bad_mul = 0;
    bad_div = 0;
    bad_sqrt = 0;
    bad_add = 0;
    for (i = 0; i < NSPECIAL; i++) {
        a.hi = special[i][0];
        a.lo = special[i][1];
        a_sf64_sqrt(&ra, &a);
        c_sf64_sqrt(&rc, &a);
        bad_sqrt = bad_sqrt + !same(&ra, &rc);
        for (j = 0; j < NSPECIAL; j++) {
            b.hi = special[j][0];
            b.lo = special[j][1];
            a_sf64_mul(&ra, &a, &b);
            c_sf64_mul(&rc, &a, &b);
            bad_mul = bad_mul + !same(&ra, &rc);
            a_sf64_div(&ra, &a, &b);
            c_sf64_div(&rc, &a, &b);
            bad_div = bad_div + !same(&ra, &rc);
            a_sf64_add(&ra, &a, &b);
            c_sf64_add(&rc, &a, &b);
            bad_add = bad_add + !same(&ra, &rc) + (a_sf64_cmp(&a, &b) != c_sf64_cmp(&a, &b));
            a_sf64_sub(&ra, &a, &b);
            c_sf64_sub(&rc, &a, &b);
            bad_add = bad_add + !same(&ra, &rc);
        }
    }
    check(bad_mul, 0);
    check(bad_div, 0);
    check(bad_sqrt, 0);
    check(bad_add, 0);
    bad_mul = 0;
    bad_div = 0;
    bad_sqrt = 0;
    bad32 = 0;
    bad_add = 0;
    bad_cvt = 0;
    for (i = 0; i < 400; i++) {
        random64(&a);
        random64(&b);
        a_sf64_mul(&ra, &a, &b);
        c_sf64_mul(&rc, &a, &b);
        bad_mul = bad_mul + !same(&ra, &rc);
        a_sf64_div(&ra, &a, &b);
        c_sf64_div(&rc, &a, &b);
        bad_div = bad_div + !same(&ra, &rc);
        a_sf64_add(&ra, &a, &b);
        c_sf64_add(&rc, &a, &b);
        bad_add = bad_add + !same(&ra, &rc);
        b.hi = (a.hi & 0xFFF00000UL) | (b.hi & 0xFFFFFUL);    /* close exponents: cancellation */
        a_sf64_sub(&ra, &a, &b);
        c_sf64_sub(&rc, &a, &b);
        bad_add = bad_add + !same(&ra, &rc) + (a_sf64_cmp(&a, &b) != c_sf64_cmp(&a, &b));
        bad_cvt = bad_cvt + (a_sf32_from_f64(&a) != c_sf32_from_f64(&a))
                  + (a_sf64_to_long(&a, 1) != c_sf64_to_long(&a, 1))
                  + (a_sf64_to_long(&a, 0) != c_sf64_to_long(&a, 0));
        a_sf64_to_long64(&a, 1, &h1, &l1);
        c_sf64_to_long64(&a, 1, &h2, &l2);
        bad_cvt = bad_cvt + (h1 != h2 || l1 != l2);
        a_sf64_from_long(&ra, a.lo, 1);
        c_sf64_from_long(&rc, a.lo, 1);
        bad_cvt = bad_cvt + !same(&ra, &rc);
        a_sf64_from_long64(&ra, b.lo, a.lo, 1);
        c_sf64_from_long64(&rc, b.lo, a.lo, 1);
        bad_cvt = bad_cvt + !same(&ra, &rc);
        a.hi = a.hi & 0x7FFFFFFFUL;     /* the root of a positive number */
        a_sf64_sqrt(&ra, &a);
        c_sf64_sqrt(&rc, &a);
        bad_sqrt = bad_sqrt + !same(&ra, &rc);
        fa = next();
        fb = next();
        a_sf64_from_f32(&ra, fa);
        c_sf64_from_f32(&rc, fa);
        bad_cvt = bad_cvt + !same(&ra, &rc);
        bad32 = bad32 + (a_sf32_add(fa, fb) != c_sf32_add(fa, fb))
                + (a_sf32_mul(fa, fb) != c_sf32_mul(fa, fb))
                + (a_sf32_div(fa, fb) != c_sf32_div(fa, fb))
                + (a_sf32_sqrt(fa & 0x7FFFFFFFUL) != c_sf32_sqrt(fa & 0x7FFFFFFFUL));
    }
    check(bad_mul, 0);
    check(bad_div, 0);
    check(bad_sqrt, 0);
    check(bad32, 0);
    check(bad_add, 0);
    check(bad_cvt, 0);
    return finish();
}
