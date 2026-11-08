/* math_shim.c - for the PC's build of the maths library (test_math.py):
 * math.c's sqrt and math99.c's fma pass a double's address to softfp.c
 * as a struct sf64 *. That is the same 8 bytes on the Agon, and on a PC
 * whose long is 32 bits, but a struct sf64 of two 64-bit longs (64-bit
 * Linux and macOS) is 16 bytes, so softfp.c would write past the double.
 * math_names.h sends the two calls here instead, which copy the double's
 * halves into a struct sf64 and back. A double is taken to be IEEE 754
 * binary64, little-endian, as on every PC this runs on. */

#include "softfp.h"

static void from_double(struct sf64 *s, const void *d)
{
    const unsigned char *b = (const unsigned char *)d;

    s->lo = b[0] | (unsigned long)b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
    s->hi = b[4] | (unsigned long)b[5] << 8 | (unsigned long)b[6] << 16 | (unsigned long)b[7] << 24;
}

static void to_double(void *d, const struct sf64 *s)
{
    unsigned char *b = (unsigned char *)d;
    int i;

    for (i = 0; i < 4; i++) {
        b[i] = (unsigned char)(s->lo >> 8 * i);
        b[4 + i] = (unsigned char)(s->hi >> 8 * i);
    }
}

/* Declared as math.c and math99.c declare them, but each pointer is to a
 * double. */
void am_sf64_sqrt(struct sf64 *r, const struct sf64 *a)
{
    struct sf64 x;

    from_double(&x, a);
    sf64_sqrt(&x, &x);
    to_double(r, &x);
}

void am_sf64_fma(struct sf64 *r, const struct sf64 *x, const struct sf64 *y, const struct sf64 *z)
{
    struct sf64 a, b, c, s;

    from_double(&a, x);
    from_double(&b, y);
    from_double(&c, z);
    sf64_fma(&s, &a, &b, &c);
    to_double(r, &s);
}
