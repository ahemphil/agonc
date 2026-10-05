/* int32.h - 32-bit arithmetic for the compiler's own use, without long.
 *
 * The compiler folds and prints the target's long constants (32 bits),
 * but its own source must build identically under AgDev, a PC compiler and
 * itself, so it does not rely on long. A 32-bit value is therefore two
 * 16-bit halves, each 0..65535, so every intermediate result
 * stays below 2^24: exact on the Agon's 24-bit int and a PC's 32-bit int
 * alike (the same reason as wrap24).
 *
 * Everything is modulo 2^32, two's complement, as the target computes it.
 */

#ifndef INT32_H
#define INT32_H

struct i32 {
    int hi;             /* bits 16..31 */
    int lo;             /* bits 0..15 */
};

void i32_set(struct i32 *r, int hi, int lo);
void i32_from_int(struct i32 *r, int v);        /* sign-extends a 24-bit int */
void i32_from_uint(struct i32 *r, int v);       /* zero-extends a 24-bit unsigned int */
int i32_low24(struct i32 *a);                   /* bits 0..23 as a canonical (sign-extended) int */
int i32_high8(struct i32 *a);                   /* bits 24..31, 0..255 */
void i32_join(struct i32 *r, int high8, int low24);  /* the inverse of those two */
int i32_is_neg(struct i32 *a);
int i32_is_zero(struct i32 *a);
int i32_fits_int(struct i32 *a);                /* -8388608 .. 8388607, read as signed */
int i32_fits_uint(struct i32 *a);               /* 0 .. 16777215, read as unsigned */

void i32_add(struct i32 *r, struct i32 *a, struct i32 *b);
void i32_sub(struct i32 *r, struct i32 *a, struct i32 *b);
void i32_mul(struct i32 *r, struct i32 *a, struct i32 *b);
void i32_divu(struct i32 *q, struct i32 *rem, struct i32 *a, struct i32 *b);   /* b != 0 */
void i32_divs(struct i32 *q, struct i32 *rem, struct i32 *a, struct i32 *b);   /* truncating */
void i32_and(struct i32 *r, struct i32 *a, struct i32 *b);
void i32_or(struct i32 *r, struct i32 *a, struct i32 *b);
void i32_xor(struct i32 *r, struct i32 *a, struct i32 *b);
void i32_neg(struct i32 *r, struct i32 *a);
void i32_cpl(struct i32 *r, struct i32 *a);
void i32_shl(struct i32 *r, struct i32 *a, int n);          /* 0 <= n < 32 */
void i32_shr(struct i32 *r, struct i32 *a, int n, int is_signed);
int i32_cmp(struct i32 *a, struct i32 *b, int is_signed);   /* -1, 0, 1 */

/* Decimal text, as the IR writes it: read with an optional '-', modulo
 * 2^32; returns 0 if s is not a number. i32_str writes it back, signed or
 * unsigned. */
int i32_parse(struct i32 *r, char *s);
void i32_str(char *buf, struct i32 *a, int is_signed);

#endif
