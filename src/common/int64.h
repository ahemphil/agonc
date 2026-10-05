/* int64.h - 64-bit arithmetic in integer C, for long long.
 *
 * cpp's #if (in the default mode), cc1's constant folding and the C
 * library's long long helpers (lib/libc/ll.c) all compute with this one
 * module, so a folded constant and the same operation at run time give the
 * same bits. A value is two unsigned longs of 32 bits each (0 ..
 * 0xFFFFFFFF, whatever the width of long, as softfp.c keeps its limbs).
 *
 * Everything is modulo 2^64, two's complement, as the target computes it.
 */

#ifndef INT64_H
#define INT64_H

/* The target's long long in memory is lo's four bytes, then hi's, both
 * little-endian: exactly this struct there, so the library's helpers take
 * a long long's address as a struct i64 *. */
struct i64 {
    unsigned long lo;   /* bits 0..31 */
    unsigned long hi;   /* bits 32..63 */
};

void i64_set(struct i64 *r, unsigned long hi, unsigned long lo);
void i64_from_long(struct i64 *r, unsigned long v, int is_signed);    /* v's 32 bits, sign- or zero-extended */
int i64_is_zero(const struct i64 *a);
int i64_is_neg(const struct i64 *a);

void i64_add(struct i64 *r, const struct i64 *a, const struct i64 *b);
void i64_sub(struct i64 *r, const struct i64 *a, const struct i64 *b);
void i64_mul(struct i64 *r, const struct i64 *a, const struct i64 *b);
void i64_divu(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b);    /* b != 0 */
void i64_divs(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b);    /* truncating */
void i64_and(struct i64 *r, const struct i64 *a, const struct i64 *b);
void i64_or(struct i64 *r, const struct i64 *a, const struct i64 *b);
void i64_xor(struct i64 *r, const struct i64 *a, const struct i64 *b);
void i64_neg(struct i64 *r, const struct i64 *a);
void i64_cpl(struct i64 *r, const struct i64 *a);
void i64_shl(struct i64 *r, const struct i64 *a, int n);                 /* 0 <= n < 64 */
void i64_shr(struct i64 *r, const struct i64 *a, int n, int is_signed);  /* 0 <= n < 64 */
int i64_cmp(const struct i64 *a, const struct i64 *b, int is_signed);    /* -1, 0, 1 */

/* a / d and a % d for a small divisor (1 .. 65535): the quotient to q,
 * the remainder returned. For printing in any base. */
unsigned int i64_divsmall(struct i64 *q, const struct i64 *a, unsigned int d);

/* r = r * m + c for m, c 0 .. 65535; returns 1 if the result passed 2^64
 * (r is then the value modulo 2^64). For reading digits. */
int i64_muladd(struct i64 *r, unsigned int m, unsigned int c);

/* Decimal text: read with an optional '-', modulo 2^64 (0 if s is not a
 * number); written back signed or unsigned (buf needs 21 bytes). */
int i64_parse(struct i64 *r, const char *s);
void i64_str(char *buf, const struct i64 *a, int is_signed);

#endif
