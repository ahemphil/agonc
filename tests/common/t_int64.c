/* t_int64.c - exercises src/common/int64.c: prints one line per operation,
 * "op a b result..." in hex (16 digits each), for test_int64.py to check.
 * The operands come from a generator built on int64 itself, so the
 * program prints the same lines whatever the widths of int and long.
 *
 *     t_int64 [rounds]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "int64.h"

static struct i64 seed;

/* Knuth's MMIX generator, modulo 2^64 */
static void next_rand(struct i64 *r)
{
    struct i64 mul;
    struct i64 inc;

    i64_set(&mul, 0x5851F42DUL, 0x4C957F2DUL);
    i64_set(&inc, 0x14057B7EUL, 0xF767814FUL);
    i64_mul(&seed, &seed, &mul);
    i64_add(&seed, &seed, &inc);
    *r = seed;
}

/* an operand: every size, both signs, and the extremes */
static void operand(struct i64 *r)
{
    struct i64 k;
    int kind;

    next_rand(&k);
    kind = (int)(k.hi >> 28);
    next_rand(r);
    if (kind < 4)
        return;                                     /* all 64 bits */
    if (kind < 7)
        i64_shr(r, r, (int)(k.lo & 63), kind == 6);   /* some bits, either sign */
    else if (kind < 9)
        i64_set(r, 0, r->lo);                       /* 32 bits */
    else if (kind < 11)
        i64_set(r, 0, r->lo & 0xFFFF);              /* 16 bits */
    else if (kind < 12)
        i64_from_long(r, r->lo & 0xFF, 0);          /* tiny */
    else if (kind < 13)
        i64_from_long(r, (r->lo & 0xFF) ^ 0xFFFFFFFFUL, 1);    /* tiny, negative */
    else if (kind < 14)
        i64_set(r, 0x80000000UL, k.lo & 1);         /* the most negative, and one above */
    else if (kind < 15)
        i64_set(r, 0x7FFFFFFFUL, 0xFFFFFFFFUL - (k.lo & 1));   /* the most positive, and one below */
    else
        i64_set(r, 0xFFFFFFFFUL, 0xFFFFFFFFUL);     /* -1 */
}

static void hex(const struct i64 *a)
{
    printf(" %08lx%08lx", a->hi, a->lo);
}

static void line(const char *op, const struct i64 *a, const struct i64 *b, const struct i64 *r, const struct i64 *s)
{
    printf("%s", op);
    hex(a);
    if (b != NULL)
        hex(b);
    hex(r);
    if (s != NULL)
        hex(s);
    printf("\n");
}

int main(int argc, char **argv)
{
    struct i64 a;
    struct i64 b;
    struct i64 r;
    struct i64 s;
    struct i64 n;
    char buf[32];
    long rounds;
    long i;
    unsigned int d;
    int c;

    rounds = argc > 1 ? atol(argv[1]) : 200;
    i64_set(&seed, 0, 1);
    for (i = 0; i < rounds; i++) {
        operand(&a);
        operand(&b);
        i64_add(&r, &a, &b);
        line("add", &a, &b, &r, NULL);
        i64_sub(&r, &a, &b);
        line("sub", &a, &b, &r, NULL);
        i64_mul(&r, &a, &b);
        line("mul", &a, &b, &r, NULL);
        i64_and(&r, &a, &b);
        line("and", &a, &b, &r, NULL);
        i64_or(&r, &a, &b);
        line("or", &a, &b, &r, NULL);
        i64_xor(&r, &a, &b);
        line("xor", &a, &b, &r, NULL);
        i64_neg(&r, &a);
        line("neg", &a, NULL, &r, NULL);
        i64_cpl(&r, &a);
        line("cpl", &a, NULL, &r, NULL);
        if (!i64_is_zero(&b)) {
            i64_divu(&r, &s, &a, &b);
            line("divu", &a, &b, &r, &s);
            i64_divs(&r, &s, &a, &b);
            line("divs", &a, &b, &r, &s);
        }
        i64_set(&n, 0, b.lo & 63);
        i64_shl(&r, &a, (int)n.lo);
        line("shl", &a, &n, &r, NULL);
        i64_shr(&r, &a, (int)n.lo, 0);
        line("shru", &a, &n, &r, NULL);
        i64_shr(&r, &a, (int)n.lo, 1);
        line("shrs", &a, &n, &r, NULL);
        c = i64_cmp(&a, &b, 0);
        i64_from_long(&r, (unsigned long)(long)c, 1);
        line("cmpu", &a, &b, &r, NULL);
        c = i64_cmp(&a, &b, 1);
        i64_from_long(&r, (unsigned long)(long)c, 1);
        line("cmps", &a, &b, &r, NULL);
        d = (unsigned int)(b.lo & 0xFFFF);
        if (d != 0) {
            i64_set(&n, 0, d);
            i64_set(&s, 0, i64_divsmall(&r, &a, d));
            line("divsmall", &a, &n, &r, &s);
        }
        r = a;
        c = i64_muladd(&r, (unsigned int)(b.lo & 0xFFFF), (unsigned int)(b.lo >> 16));
        i64_set(&s, 0, (unsigned long)c);
        line("muladd", &a, &b, &r, &s);
        i64_str(buf, &a, 1);
        if (!i64_parse(&r, buf))
            printf("parse failed %s\n", buf);
        printf("strs %s", buf);
        hex(&r);
        printf("\n");
        i64_str(buf, &a, 0);
        if (!i64_parse(&r, buf))
            printf("parse failed %s\n", buf);
        printf("stru %s", buf);
        hex(&r);
        printf("\n");
    }
    return 0;
}
