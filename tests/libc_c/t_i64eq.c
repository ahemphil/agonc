/* t_i64eq.c - int64.c's assembly (I64_ASM 1: add, subtract, compare, the
 * shifts, multiply, the long division) gives exactly the C's results.
 * Both builds are linked in under the names a_ and c_ (i64eq_a.c,
 * i64eq_c.c) and run on the same operands: special values (0, 1, -1, the
 * extremes, powers of two, divisors big and small) against each other,
 * then pseudo-random ones. Each check is one group of operations: the
 * number of results that differ, which must be 0.
 */

#include "check.h"
#include "../../src/common/int64.h"

void a_i64_add(struct i64 *r, const struct i64 *a, const struct i64 *b);
void a_i64_sub(struct i64 *r, const struct i64 *a, const struct i64 *b);
void a_i64_mul(struct i64 *r, const struct i64 *a, const struct i64 *b);
void a_i64_divu(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b);
void a_i64_divs(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b);
void a_i64_shl(struct i64 *r, const struct i64 *a, int n);
void a_i64_shr(struct i64 *r, const struct i64 *a, int n, int is_signed);
int a_i64_cmp(const struct i64 *a, const struct i64 *b, int is_signed);
void c_i64_add(struct i64 *r, const struct i64 *a, const struct i64 *b);
void c_i64_sub(struct i64 *r, const struct i64 *a, const struct i64 *b);
void c_i64_mul(struct i64 *r, const struct i64 *a, const struct i64 *b);
void c_i64_divu(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b);
void c_i64_divs(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b);
void c_i64_shl(struct i64 *r, const struct i64 *a, int n);
void c_i64_shr(struct i64 *r, const struct i64 *a, int n, int is_signed);
int c_i64_cmp(const struct i64 *a, const struct i64 *b, int is_signed);

#define NSPECIAL 12
unsigned long special[NSPECIAL][2] = {      /* hi, lo */
    { 0, 0 }, { 0, 1 }, { 0xFFFFFFFFUL, 0xFFFFFFFFUL }, { 0x7FFFFFFFUL, 0xFFFFFFFFUL },
    { 0x80000000UL, 0 }, { 0, 0xFFFF }, { 0, 0x10000UL }, { 1, 0 },
    { 0, 0xFFFFFFFFUL }, { 0x12345678UL, 0x9ABCDEF0UL }, { 0, 1000003UL }, { 0xFFFFFFFFUL, 0xFFFF0000UL }
};

unsigned long seed = 54321;

unsigned long next(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return seed ^ seed >> 15;
}

int same(const struct i64 *x, const struct i64 *y)
{
    return x->hi == y->hi && x->lo == y->lo;
}

int bad_arith;
int bad_div;
int bad_shift;

/* every operation on one pair */
void both(const struct i64 *a, const struct i64 *b)
{
    struct i64 ra;
    struct i64 rc;
    struct i64 qa;
    struct i64 qc;
    int n;

    a_i64_add(&ra, a, b);
    c_i64_add(&rc, a, b);
    bad_arith = bad_arith + !same(&ra, &rc);
    a_i64_sub(&ra, a, b);
    c_i64_sub(&rc, a, b);
    bad_arith = bad_arith + !same(&ra, &rc);
    a_i64_mul(&ra, a, b);
    c_i64_mul(&rc, a, b);
    bad_arith = bad_arith + !same(&ra, &rc);
    bad_arith = bad_arith + (a_i64_cmp(a, b, 0) != c_i64_cmp(a, b, 0)) + (a_i64_cmp(a, b, 1) != c_i64_cmp(a, b, 1));
    if (b->hi != 0 || b->lo != 0) {
        a_i64_divu(&qa, &ra, a, b);
        c_i64_divu(&qc, &rc, a, b);
        bad_div = bad_div + !same(&qa, &qc) + !same(&ra, &rc);
        a_i64_divs(&qa, &ra, a, b);
        c_i64_divs(&qc, &rc, a, b);
        bad_div = bad_div + !same(&qa, &qc) + !same(&ra, &rc);
    }
    n = (int)(b->lo & 63);
    a_i64_shl(&ra, a, n);
    c_i64_shl(&rc, a, n);
    bad_shift = bad_shift + !same(&ra, &rc);
    a_i64_shr(&ra, a, n, 0);
    c_i64_shr(&rc, a, n, 0);
    bad_shift = bad_shift + !same(&ra, &rc);
    a_i64_shr(&ra, a, n, 1);
    c_i64_shr(&rc, a, n, 1);
    bad_shift = bad_shift + !same(&ra, &rc);
}

int main(void)
{
    struct i64 a;
    struct i64 b;
    int i;
    int j;

    for (i = 0; i < NSPECIAL; i++) {
        a.hi = special[i][0];
        a.lo = special[i][1];
        for (j = 0; j < NSPECIAL; j++) {
            b.hi = special[j][0];
            b.lo = special[j][1];
            both(&a, &b);
        }
    }
    check(bad_arith, 0);
    check(bad_div, 0);
    check(bad_shift, 0);
    bad_arith = 0;
    bad_div = 0;
    bad_shift = 0;
    for (i = 0; i < 300; i++) {
        a.hi = next();
        a.lo = next();
        b.hi = i % 3 == 0 ? 0 : next() >> (i % 32);     /* divisors of many sizes */
        b.lo = next();
        if (i % 5 == 0)
            b.lo = b.lo & 0xFFFF;                       /* (the short road when hi is 0) */
        both(&a, &b);
    }
    check(bad_arith, 0);
    check(bad_div, 0);
    check(bad_shift, 0);                /* (the random operands) */
    return finish();
}
