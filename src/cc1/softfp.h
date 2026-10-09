/* softfp.h - IEEE 754 binary32 and binary64 arithmetic in integer C.
 *
 * One implementation serves cc1, which folds floating constants without
 * using floating point itself, and the C library (whose build compiles
 * this file from src/cc1 as one of its units, lib/libc/fp.c), whose
 * helpers compiled code calls for every float and double operation.
 * Correctly rounded to nearest, ties to even (the only mode); subnormals,
 * infinities, NaNs and signed zeros as IEEE 754 has them. A NaN result is
 * the first NaN operand, made quiet, or for an invalid operation (0/0,
 * inf - inf, sqrt(-1)) the default quiet NaN 0x7FF8000000000000.
 *
 * Every value is held in unsigned longs of 32 bits (0 .. 0xFFFFFFFF),
 * whatever the width of long, so every build computes the same bits.
 *
 * The users: in cc1, lex.c turns a floating constant's text into bits
 * (sf64_from_decimal, sf32_from_decimal) and expr.c folds constant
 * expressions with the arithmetic, comparisons and conversions. In the
 * library, fp.c includes softfp.c with every name below renamed to a
 * reserved one (sf64_add becomes __sf64_add, and so on, so that a program
 * may define its own sf64_add), wraps them as the helpers cc2's code calls
 * (abi.md 6), and uses the decimal conversions for printf, scanf and
 * strtod. The same source on both sides is what makes a constant folded
 * at compile time equal, bit for bit, the same expression computed at run
 * time.
 *
 * Calling convention: binary32 values travel by value as unsigned long;
 * binary64 values by pointer to struct sf64, the result through r. r may
 * be the same object as an operand.
 */

#ifndef SOFTFP_H
#define SOFTFP_H

/* A binary64's bits: hi is bits 32..63 (sign, exponent, the fraction's top
 * 20 bits), lo bits 0..31. The target's double in memory is lo's four
 * bytes, then hi's, both little-endian: exactly this struct there. */
struct sf64 {
    unsigned long lo;
    unsigned long hi;
};

/* A binary32's bits are an unsigned long: bit 31 the sign, bits 23..30 the
 * exponent, bits 0..22 the fraction. */

/* ---- arithmetic ---- */

/* r = a + b, a - b, a * b, a / b, sqrt(a), -a, each correctly rounded.
 * Negation only flips the sign bit, NaNs included, as IEEE 754 has it. */
void sf64_add(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void sf64_sub(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void sf64_mul(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void sf64_div(struct sf64 *r, const struct sf64 *a, const struct sf64 *b);
void sf64_sqrt(struct sf64 *r, const struct sf64 *a);
void sf64_neg(struct sf64 *r, const struct sf64 *a);

/* -1 if a < b, 0 if equal (+0 and -0 are), 1 if a > b, 2 if unordered (a
 * NaN either side). */
int sf64_cmp(const struct sf64 *a, const struct sf64 *b);

/* The same for binary32, by value: each computed exactly in binary64 and
 * rounded once to binary32, which gives the correctly rounded result (see
 * softfp.c's header). */
unsigned long sf32_add(unsigned long a, unsigned long b);
unsigned long sf32_sub(unsigned long a, unsigned long b);
unsigned long sf32_mul(unsigned long a, unsigned long b);
unsigned long sf32_div(unsigned long a, unsigned long b);
unsigned long sf32_sqrt(unsigned long a);
unsigned long sf32_neg(unsigned long a);
int sf32_cmp(unsigned long a, unsigned long b);

/* ---- conversions ---- */

/* Conversions. A 32-bit integer v (two's complement if is_signed) to a
 * floating value is exact for double, rounded for float. A floating value
 * to an integer truncates toward zero; beyond the integer's range (C89
 * leaves it undefined) it saturates, and a NaN gives 0. */
void sf64_from_f32(struct sf64 *r, unsigned long a);
unsigned long sf32_from_f64(const struct sf64 *a);
void sf64_from_long(struct sf64 *r, unsigned long v, int is_signed);
unsigned long sf64_to_long(const struct sf64 *a, int is_signed);
unsigned long sf32_from_long(unsigned long v, int is_signed);
unsigned long sf32_to_long(unsigned long a, int is_signed);

/* The same for 64-bit integers (long long), given and returned as their
 * two 32-bit halves: correctly rounded to either type (float straight
 * from the integer, not through double), truncated back, saturating. */
void sf64_from_long64(struct sf64 *r, unsigned long hi, unsigned long lo, int is_signed);
void sf64_to_long64(const struct sf64 *a, int is_signed, unsigned long *hi, unsigned long *lo);
unsigned long sf32_from_long64(unsigned long hi, unsigned long lo, int is_signed);
void sf32_to_long64(unsigned long a, int is_signed, unsigned long *hi, unsigned long *lo);

/* ---- decimal ---- */

/* Decimal to binary, correctly rounded: the sign, the digits ('0' .. '9',
 * n of them, the decimal point taken out) times 10^exp10. binary32 is
 * rounded from the decimal itself, not through binary64. */
void sf64_from_decimal(struct sf64 *r, int sign, const char *digits, int n, long exp10);
unsigned long sf32_from_decimal(int sign, const char *digits, int n, long exp10);

/* A hexadecimal floating constant (C99), correctly rounded: the 64-bit
 * integer hi:lo of its leading digits times 2^exp2, sticky nonzero if
 * digits beyond them that were not 0 were dropped. */
void sf64_from_hex(struct sf64 *r, unsigned long hi, unsigned long lo, int sticky, long exp2);
unsigned long sf32_from_hex(unsigned long hi, unsigned long lo, int sticky, long exp2);

/* x y + z rounded once (C99's fma), built with SOFTFP_FMA (fp.c). */
void sf64_fma(struct sf64 *r, const struct sf64 *x, const struct sf64 *y, const struct sf64 *z);

/* A finite |a| as decimal digits for printf, correctly rounded (to
 * nearest, ties to even): mode 'e', prec + 1 significant digits; mode 'f',
 * down to 10^-prec. Writes the digits d1 d2 ... to buf (which needs 800
 * bytes; the caller supplies any zeros after them), sets |a| ~ d1.d2d3... *
 * 10^*dexp, and returns their count: 0 if the value is 0 or rounds to it.
 * Not reentrant (it shares softfp.c's big integers). */
int sf64_to_decimal(const struct sf64 *a, int mode, int prec, char *buf, int *dexp);

#endif
