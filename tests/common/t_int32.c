/* t_int32.c - exercises src/common/int32.c: prints one line per operation,
 * "op a b result" in hex (8 digits each), for test_int32.py to check. The
 * operands come from a generator built on int32 itself, so the program
 * prints the same lines whatever the width of int. */

#include <stdio.h>
#include "int32.h"

static struct i32 seed;

static void next_rand(struct i32 *r)
{
    struct i32 mul;
    struct i32 inc;

    i32_set(&mul, 0x41C6, 0x4E6D);      /* 1103515245 */
    i32_set(&inc, 0, 12345);
    i32_mul(&seed, &seed, &mul);
    i32_add(&seed, &seed, &inc);
    *r = seed;
}

static void hex(struct i32 *a)
{
    printf("%04x%04x", a->hi, a->lo);
}

static void line3(char *op, struct i32 *a, struct i32 *b, struct i32 *r)
{
    printf("%s ", op);
    hex(a);
    printf(" ");
    hex(b);
    printf(" ");
    hex(r);
    printf("\n");
}

static void pick(struct i32 *r, int k)
{
    next_rand(r);
    switch (k % 8) {
    case 0: i32_set(r, 0, 0); break;
    case 1: i32_set(r, 0xFFFF, 0xFFFF); break;
    case 2: i32_set(r, 0x8000, 0); break;
    case 3: i32_set(r, 0x7FFF, 0xFFFF); break;
    case 4: r->hi = r->hi & 0xFF; break;
    case 5: r->hi = 0; r->lo = r->lo & 0xFF; break;
    default: break;
    }
}

int main(void)
{
    struct i32 a;
    struct i32 b;
    struct i32 r;
    struct i32 q;
    struct i32 m;
    struct i32 p;
    char buf[16];
    int i;
    int n;

    i32_set(&seed, 0x1989, 0x0907);
    for (i = 0; i < 200; i++) {
        pick(&a, i);
        pick(&b, i / 8 + 3);
        i32_add(&r, &a, &b); line3("add", &a, &b, &r);
        i32_sub(&r, &a, &b); line3("sub", &a, &b, &r);
        i32_mul(&r, &a, &b); line3("mul", &a, &b, &r);
        i32_and(&r, &a, &b); line3("and", &a, &b, &r);
        i32_or(&r, &a, &b); line3("or", &a, &b, &r);
        i32_xor(&r, &a, &b); line3("xor", &a, &b, &r);
        i32_neg(&r, &a); line3("neg", &a, &a, &r);
        i32_cpl(&r, &a); line3("cpl", &a, &a, &r);
        if (!i32_is_zero(&b)) {
            i32_divu(&q, &m, &a, &b); line3("divu", &a, &b, &q); line3("remu", &a, &b, &m);
            if (!(a.hi == 0x8000 && a.lo == 0 && b.hi == 0xFFFF && b.lo == 0xFFFF)) {
                i32_divs(&q, &m, &a, &b); line3("divs", &a, &b, &q); line3("rems", &a, &b, &m);
            }
        }
        n = b.lo & 31;
        i32_set(&p, 0, n);
        i32_shl(&r, &a, n); line3("shl", &a, &p, &r);
        i32_shr(&r, &a, n, 0); line3("shru", &a, &p, &r);
        i32_shr(&r, &a, n, 1); line3("shrs", &a, &p, &r);
        i32_set(&r, 0, i32_cmp(&a, &b, 0) + 1); line3("cmpu", &a, &b, &r);
        i32_set(&r, 0, i32_cmp(&a, &b, 1) + 1); line3("cmps", &a, &b, &r);
        i32_str(buf, &a, 1);
        i32_set(&r, 0, 0);
        if (!i32_parse(&r, buf))
            i32_set(&r, 0xDEAD, 0xBEEF);
        line3("strs", &a, &a, &r);
        i32_str(buf, &a, 0);
        i32_set(&r, 0, 0);
        if (!i32_parse(&r, buf))
            i32_set(&r, 0xDEAD, 0xBEEF);
        line3("stru", &a, &a, &r);
        printf("text %s\n", buf);
        i32_from_int(&r, i32_low24(&a)); line3("sext24", &a, &a, &r);
        i32_set(&r, 0, i32_high8(&a)); line3("high8", &a, &a, &r);
        i32_join(&r, i32_high8(&a), i32_low24(&a)); line3("join", &a, &a, &r);
        i32_set(&r, i32_fits_uint(&a), i32_fits_int(&a)); line3("fits", &a, &a, &r);
    }
    return 0;
}
