/* fp.c - the helpers compiled code calls for float and double (abi.md 6).
 *
 * Every operation is softfp.c's, the same code cc1 folds constants with,
 * so a constant expression and the same expression at run time give the
 * same bits. A float is its binary32 bits in an unsigned long (E:UHL); a
 * double is the address of its 8 bytes, which are a struct sf64. cc2
 * pushes each operand as soon as it is made, so the parameters are in the
 * reverse of the operands' order: __fsub(b, a) is a - b. A double result
 * goes to an object the caller supplies (r), whose address is returned.
 *
 * Where it sits: the eZ80 has no floating-point hardware, so cc2 turns
 * every float and double operation into a call to one of the __f... and
 * __d... functions here (abi.md section 6 lists them). They are ordinary
 * C functions, called by the C convention (abi.md section 4), so unlike
 * rt.s's helpers they need no assembly. This file is part of the library
 * libm.s, and its helpers are linked only into programs that use them.
 *
 * Sections: the float helpers; the double helpers; conversions between
 * long long and float or double (ll.c holds the rest of long long); and
 * the decimal conversions behind printf's and scanf's e, f, g and strtod.
 *
 * Comparisons follow IEEE 754: sf32_cmp and sf64_cmp return 2 for an
 * unordered pair (a NaN on either side), so every comparison but != is
 * false for a NaN. An increment or decrement adds the constant 1.0 or
 * -1.0 (0x3F800000 and 0xBF800000 as float bits).
 */

/* softfp's functions under reserved names: a program may use the plain
 * ones (cc1 itself links its own softfp.c against this library). The
 * defines rename them before softfp.c is included below, so its code is
 * compiled here as a part of this unit, under the reserved names. */
#define sf64_add __sf64_add
#define sf64_sub __sf64_sub
#define sf64_mul __sf64_mul
#define sf64_div __sf64_div
#define sf64_sqrt __sf64_sqrt
#define sf64_neg __sf64_neg
#define sf64_cmp __sf64_cmp
#define sf32_add __sf32_add
#define sf32_sub __sf32_sub
#define sf32_mul __sf32_mul
#define sf32_div __sf32_div
#define sf32_sqrt __sf32_sqrt
#define sf32_neg __sf32_neg
#define sf32_cmp __sf32_cmp
#define sf64_from_f32 __sf64_from_f32
#define sf32_from_f64 __sf32_from_f64
#define sf64_from_long __sf64_from_long
#define sf64_to_long __sf64_to_long
#define sf32_from_long __sf32_from_long
#define sf32_to_long __sf32_to_long
#define sf64_from_long64 __sf64_from_long64
#define sf64_to_long64 __sf64_to_long64
#define sf32_from_long64 __sf32_from_long64
#define sf32_to_long64 __sf32_to_long64
#define sf64_from_decimal __sf64_from_decimal
#define sf32_from_decimal __sf32_from_decimal
#define sf64_from_hex __sf64_from_hex
#define sf64_fma __sf64_fma
#define SOFTFP_FMA 1
#define sf32_from_hex __sf32_from_hex
#define sf64_to_decimal __sf64_to_decimal

#include "../../src/cc1/softfp.c"
#include <stdio.h>
#include <errno.h>

/* the operator codes ASG passes (cc2's B_ADD ... B_DIVS); 3, division,
 * is the else of each test */
#define OP_ADD 0
#define OP_SUB 1
#define OP_MUL 2

/* ---- float -------------------------------------------------------------------- */

/* A float travels as its bits in an unsigned long, in E:UHL when it is
 * returned; softfp's sf32 functions take and give the same bits. */

unsigned long __fadd(unsigned long b, unsigned long a)
{
    return sf32_add(a, b);
}

unsigned long __fsub(unsigned long b, unsigned long a)
{
    return sf32_sub(a, b);
}

unsigned long __fmul(unsigned long b, unsigned long a)
{
    return sf32_mul(a, b);
}

unsigned long __fdiv(unsigned long b, unsigned long a)
{
    return sf32_div(a, b);
}

int __feq(unsigned long b, unsigned long a)
{
    return sf32_cmp(a, b) == 0;
}

int __fne(unsigned long b, unsigned long a)
{
    return sf32_cmp(a, b) != 0;
}

int __flt(unsigned long b, unsigned long a)
{
    return sf32_cmp(a, b) == -1;
}

int __fle(unsigned long b, unsigned long a)
{
    int c;

    c = sf32_cmp(a, b);
    return c == -1 || c == 0;
}

int __fgt(unsigned long b, unsigned long a)
{
    return sf32_cmp(a, b) == 1;
}

int __fge(unsigned long b, unsigned long a)
{
    int c;

    c = sf32_cmp(a, b);
    return c == 1 || c == 0;
}

/* *p op= b; the new value */
unsigned long __fasg(int op, unsigned long b, unsigned long *p)
{
    if (op == OP_ADD)
        *p = sf32_add(*p, b);
    else if (op == OP_SUB)
        *p = sf32_sub(*p, b);
    else if (op == OP_MUL)
        *p = sf32_mul(*p, b);
    else
        *p = sf32_div(*p, b);
    return *p;
}

/* *p += d (d is 1 or -1); the new value, or the old one */
unsigned long __fincpre(int d, unsigned long *p)
{
    *p = sf32_add(*p, d > 0 ? 0x3F800000UL : 0xBF800000UL);
    return *p;
}

unsigned long __fincpost(int d, unsigned long *p)
{
    unsigned long old;

    old = *p;
    *p = sf32_add(old, d > 0 ? 0x3F800000UL : 0xBF800000UL);
    return old;
}

/* Conversions with long, each rounded to nearest; to an integer they
 * truncate toward zero, saturating out of range and giving 0 for a NaN
 * (softfp.h), where C leaves the result undefined. int conversions go
 * through these, by way of long (abi.md section 6). */
unsigned long __ltof(long v)
{
    return sf32_from_long((unsigned long)v, 1);
}

unsigned long __ultof(unsigned long v)
{
    return sf32_from_long(v, 0);
}

long __ftol(unsigned long f)
{
    return (long)sf32_to_long(f, 1);
}

unsigned long __ftoul(unsigned long f)
{
    return sf32_to_long(f, 0);
}

/* ---- double ------------------------------------------------------------------- */

/* A double never sits in registers: each argument is the address of its
 * 8 bytes, read as a struct sf64, and a result is written to *r. The
 * target's double layout is exactly struct sf64 (softfp.h), so the casts
 * between them cost nothing. */

struct sf64 *__dadd(struct sf64 *r, const struct sf64 *b, const struct sf64 *a)
{
    sf64_add(r, a, b);
    return r;
}

struct sf64 *__dsub(struct sf64 *r, const struct sf64 *b, const struct sf64 *a)
{
    sf64_sub(r, a, b);
    return r;
}

struct sf64 *__dmul(struct sf64 *r, const struct sf64 *b, const struct sf64 *a)
{
    sf64_mul(r, a, b);
    return r;
}

struct sf64 *__ddiv(struct sf64 *r, const struct sf64 *b, const struct sf64 *a)
{
    sf64_div(r, a, b);
    return r;
}

int __deq(const struct sf64 *b, const struct sf64 *a)
{
    return sf64_cmp(a, b) == 0;
}

int __dne(const struct sf64 *b, const struct sf64 *a)
{
    return sf64_cmp(a, b) != 0;
}

int __dlt(const struct sf64 *b, const struct sf64 *a)
{
    return sf64_cmp(a, b) == -1;
}

int __dle(const struct sf64 *b, const struct sf64 *a)
{
    int c;

    c = sf64_cmp(a, b);
    return c == -1 || c == 0;
}

int __dgt(const struct sf64 *b, const struct sf64 *a)
{
    return sf64_cmp(a, b) == 1;
}

int __dge(const struct sf64 *b, const struct sf64 *a)
{
    int c;

    c = sf64_cmp(a, b);
    return c == 1 || c == 0;
}

struct sf64 *__dneg(struct sf64 *r, const struct sf64 *a)
{
    sf64_neg(r, a);
    return r;
}

/* *p op= b; p (the stored object is the expression's value). Relies on
 * softfp's functions accepting a result pointer equal to an operand. */
struct sf64 *__dasg(int op, const struct sf64 *b, struct sf64 *p)
{
    if (op == OP_ADD)
        sf64_add(p, p, b);
    else if (op == OP_SUB)
        sf64_sub(p, p, b);
    else if (op == OP_MUL)
        sf64_mul(p, p, b);
    else
        sf64_div(p, p, b);
    return p;
}

/* 1.0 and -1.0 as binary64 bits, { lo, hi }, for ++ and -- */
static struct sf64 one = { 0, 0x3FF00000UL };
static struct sf64 minus_one = { 0, 0xBFF00000UL };

struct sf64 *__dincpre(int d, struct sf64 *p)
{
    sf64_add(p, p, d > 0 ? &one : &minus_one);
    return p;
}

/* the old value to r */
struct sf64 *__dincpost(struct sf64 *r, int d, struct sf64 *p)
{
    *r = *p;
    sf64_add(p, p, d > 0 ? &one : &minus_one);
    return r;
}

struct sf64 *__ltod(struct sf64 *r, long v)
{
    sf64_from_long(r, (unsigned long)v, 1);
    return r;
}

struct sf64 *__ultod(struct sf64 *r, unsigned long v)
{
    sf64_from_long(r, v, 0);
    return r;
}

struct sf64 *__ftod(struct sf64 *r, unsigned long f)
{
    sf64_from_f32(r, f);
    return r;
}

unsigned long __dtof(const struct sf64 *a)
{
    return sf32_from_f64(a);
}

long __dtol(const struct sf64 *a)
{
    return (long)sf64_to_long(a, 1);
}

unsigned long __dtoul(const struct sf64 *a)
{
    return sf64_to_long(a, 0);
}

/* ---- long long (ll.c's helpers' other half): a struct q is a struct i64 --------- */

/* struct q has the layout of int64.h's struct i64 (lo, then hi), which
 * this file does not include. softfp converts a 64-bit integer given
 * as its two halves, and rounds straight to float rather than through
 * double, which could round twice. */

struct q {
    unsigned long lo;
    unsigned long hi;
};

unsigned long __qtof(const struct q *a)
{
    return sf32_from_long64(a->hi, a->lo, 1);
}

unsigned long __uqtof(const struct q *a)
{
    return sf32_from_long64(a->hi, a->lo, 0);
}

struct sf64 *__qtod(struct sf64 *r, const struct q *a)
{
    sf64_from_long64(r, a->hi, a->lo, 1);
    return r;
}

struct sf64 *__uqtod(struct sf64 *r, const struct q *a)
{
    sf64_from_long64(r, a->hi, a->lo, 0);
    return r;
}

struct q *__ftoq(struct q *r, unsigned long f)
{
    sf32_to_long64(f, 1, &r->hi, &r->lo);
    return r;
}

struct q *__ftouq(struct q *r, unsigned long f)
{
    sf32_to_long64(f, 0, &r->hi, &r->lo);
    return r;
}

struct q *__dtoq(struct q *r, const struct sf64 *a)
{
    sf64_to_long64(a, 1, &r->hi, &r->lo);
    return r;
}

struct q *__dtouq(struct q *r, const struct sf64 *a)
{
    sf64_to_long64(a, 0, &r->hi, &r->lo);
    return r;
}

/* ---- decimal conversions for the printf and scanf families and strtod --------- */

/* Decimal to binary and back are the hard part of floating-point I/O:
 * softfp does both exactly with big integers (sf64_from_decimal,
 * sf64_to_decimal), so a value printed with enough digits reads back as
 * the same bits. The code here only scans and lays out the characters.
 * fp_buf and softfp's big integers are shared, so none of this is
 * reentrant. */

/* The digits of a conversion: sf64_to_decimal's (which needs DIG_MAX), or
 * a constant's as parse reads them (one more, standing for any beyond). */
static char fp_buf[DIG_MAX + 1];

/* What parse read besides a decimal constant (C99's forms): a
 * hexadecimal one, whose first 16 significant digits make hex_hi:hex_lo
 * (the rest only hex_sticky, if any is not 0), an infinity or a NaN. */
#define K_DEC 0
#define K_HEX 1
#define K_INF 2
#define K_NAN 3
static int fp_kind;
static unsigned long hex_hi;
static unsigned long hex_lo;
static int hex_sticky;

static int hex_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* After a sign: inf or infinity, or nan with an optional (letters,
 * digits and '_'), any case (C99 7.20.1.3), c the first letter, n the
 * characters read so far. As for parse; *good marks "inf", "infinity",
 * "nan" and "nan(...)" as complete. */
static int parse_word(int (*get)(void *), void (*unget)(void *, int), void *src, int width, int *good, int n,
                      int c)
{
    char *word;
    int i;

    word = (c | 32) == 'i' ? "infinity" : "nan";
    fp_kind = word[0] == 'i' ? K_INF : K_NAN;
    for (i = 0; word[i] != 0 && n < width && (c | 32) == word[i]; i++) {
        n++;
        c = get(src);
        if (i == 2 || i == 7)
            *good = n;
    }
    if (fp_kind == K_NAN && *good > 0 && n < width && c == '(') {
        n++;
        c = get(src);
        while (n < width && (hex_value(c) >= 0 || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_')) {
            n++;
            c = get(src);
        }
        if (n < width && c == ')') {
            n++;
            *good = n;
            c = get(src);
        }
    }
    unget(src, c);
    return n;
}

/* A floating constant as strtod and scanf read it (C89 4.10.1.4): a sign,
 * digits with at most one '.', then e or E, a sign and digits, at most
 * width characters from get, the character after them given back; or,
 * if c99, C99's forms too (fp_kind says which): after the sign, 0x and
 * hex digits with at most one '.', then an optional p or P, a sign and
 * decimal digits, the power of two, which *exp10 holds; or parse_word's.
 * C89 has none of them: its strtod reads "inf" as no number at all. The
 * digits go to fp_buf without the leading zeros (past DIG_MAX + 1, only
 * whether any is nonzero is kept), so that the value is fp_buf's integer
 * times 10^*exp10. Returns the characters consumed; *good is the number
 * that ended a complete constant (0 if none did), which is fewer after
 * "1e" or "1e+".
 *
 * get and unget make one scanner serve both callers: scanf's stream and
 * strtod's string. Only the last character read is given back, which is
 * all a stream allows. The sign counts toward width. The digit kept at
 * fp_buf[DIG_MAX] is a sticky digit: any nonzero digit at or past that
 * position leaves it nonzero, which is all correct rounding needs to
 * know of them ("sticky" as in a sticky bit: it records that something
 * nonzero was cut off). Digits past it that come before the point add
 * one each to the exponent instead. The written exponent stops growing
 * once it reaches 100000, far beyond any double's range, so it cannot
 * overflow a long. */
static int parse(int (*get)(void *), void (*unget)(void *, int), void *src, int width, int *good, int *nd,
                 long *exp10, int *neg, int c99)
{
    int n;
    int c;
    int d;
    int any;
    int point;
    int eneg;
    long e;

    n = 0;
    *good = 0;
    *nd = 0;
    *exp10 = 0;
    *neg = 0;
    any = 0;
    point = 0;
    fp_kind = K_DEC;
    c = get(src);
    if (c == '+' || c == '-') {
        *neg = c == '-';
        n++;
        c = get(src);
    }
    if (c99 && n < width && ((c | 32) == 'i' || (c | 32) == 'n'))
        return parse_word(get, unget, src, width, good, n, c);
    if (c99 && n < width && c == '0') {
        any = 1;                        /* "0" is complete, whatever follows */
        n++;
        *good = n;
        c = get(src);
        if (n < width && (c | 32) == 'x') {
            /* each of the first 16 significant digits goes into the
             * integer hex_hi:hex_lo; a digit after the point divides by
             * 16, a dropped one before it multiplies by 16 */
            fp_kind = K_HEX;
            hex_hi = 0;
            hex_lo = 0;
            hex_sticky = 0;
            any = 0;
            n++;
            c = get(src);
            while (n < width) {
                d = hex_value(c);
                if (d >= 0) {
                    any = 1;
                    if (*nd < 16) {
                        if (*nd > 0 || d != 0) {
                            hex_hi = (hex_hi << 4 | hex_lo >> 28) & 0xFFFFFFFFUL;
                            hex_lo = (hex_lo << 4 & 0xFFFFFFFFUL) | d;
                            (*nd)++;
                        }
                        if (point)
                            *exp10 = *exp10 - 4;
                    } else {
                        if (d != 0)
                            hex_sticky = 1;
                        if (!point)
                            *exp10 = *exp10 + 4;
                    }
                } else if (c == '.' && !point) {
                    point = 1;
                } else {
                    break;
                }
                n++;
                c = get(src);
                if (any)
                    *good = n;
            }
            if (!any)
                fp_kind = K_DEC;        /* only the "0" before the x */
        }
    }
    while (fp_kind == K_DEC && n < width) {
        if (c >= '0' && c <= '9') {
            any = 1;
            if (c != '0' || *nd > 0) {
                if (*nd <= DIG_MAX) {
                    fp_buf[*nd] = c;
                    (*nd)++;
                    if (point)
                        (*exp10)--;
                } else {
                    if (c != '0')
                        fp_buf[DIG_MAX] = '1';  /* the sticky digit */
                    if (!point)
                        (*exp10)++;
                }
            } else if (point) {
                (*exp10)--;                     /* a leading zero after the point */
            }
        } else if (c == '.' && !point) {
            point = 1;
        } else {
            break;
        }
        n++;
        c = get(src);
        if (any)
            *good = n;
    }
    /* the exponent part; e stays -1 until a digit comes, and only then
     * does *good move past the 'e' (or p) */
    if (any && n < width && (c | 32) == (fp_kind == K_HEX ? 'p' : 'e')) {
        n++;
        c = get(src);
        eneg = 0;
        if (n < width && (c == '+' || c == '-')) {
            eneg = c == '-';
            n++;
            c = get(src);
        }
        e = -1;
        while (n < width && c >= '0' && c <= '9') {
            if (e < 0)
                e = 0;
            if (e < 100000L)
                e = e * 10 + (c - '0');
            n++;
            c = get(src);
            *good = n;
        }
        if (e >= 0)
            *exp10 = *exp10 + (eneg ? -e : e);
    }
    unget(src, c);
    return n;
}

/* What parse read, as a double or as a float's bits, each rounded once
 * from the constant itself. */
static void double_of(struct sf64 *r, int nd, long exp10, int neg)
{
    r->lo = 0;
    if (fp_kind == K_INF)
        r->hi = 0x7FF00000UL;
    else if (fp_kind == K_NAN)
        r->hi = DEFAULT_NAN_HI;
    else if (fp_kind == K_HEX)
        sf64_from_hex(r, hex_hi, hex_lo, hex_sticky, exp10);
    else
        sf64_from_decimal(r, 0, fp_buf, nd, exp10);
    if (neg)
        r->hi = r->hi | 0x80000000UL;
}

static unsigned long float_of(int nd, long exp10, int neg)
{
    unsigned long f;

    if (fp_kind == K_INF)
        f = 0x7F800000UL;
    else if (fp_kind == K_NAN)
        f = 0x7FC00000UL;
    else if (fp_kind == K_HEX)
        f = sf32_from_hex(hex_hi, hex_lo, hex_sticky, exp10);
    else
        f = sf32_from_decimal(0, fp_buf, nd, exp10);
    return neg ? f | 0x80000000UL : f;
}

/* scanf's e f g a E F G A (stdio.c reaches this through a weak
 * reference): 1 if a whole constant was read, stored to dest (a double
 * for size 'l' or 'L', else a float, rounded from the constant itself)
 * unless dest is null. c99: C99's forms are read too. */
int __fp_scan(int (*get)(void *), void (*unget)(void *, int), void *src, int width, int size, void *dest,
              int c99)
{
    int n;
    int good;
    int nd;
    int neg;
    long exp10;

    n = parse(get, unget, src, width, &good, &nd, &exp10, &neg, c99);
    /* characters read past the end of a complete constant ("1e") are a
     * matching failure, as C has it: the input item is "1e", which is no
     * number, and a stream cannot take back more than one character */
    if (good == 0 || good != n)
        return 0;
    if (dest != NULL) {
        if (size == 'l' || size == 'L')
            double_of((struct sf64 *)dest, nd, exp10, neg);
        else
            *(unsigned long *)dest = float_of(nd, exp10, neg);
    }
    return 1;
}

/* parse's get and unget over a string: src is the address of a char
 * pointer, the cursor. The end of the string reads as EOF without
 * moving, so ungetting EOF moves nothing either. & 255 gives the
 * character as unsigned, as getc would (char is signed). */
static int str_get(void *src)
{
    const char **p;

    p = src;
    if (**p == 0)
        return EOF;
    (*p)++;
    return (*p)[-1] & 255;
}

static void str_unget(void *src, int c)
{
    const char **p;

    p = src;
    if (c != EOF)
        (*p)--;
}

/* strtod (C89 4.10.1.4, and with c99 C99's forms), or with r null C99's
 * strtof, whose float's bits go to *f: the value; out of range, HUGE_VAL
 * (an infinity) or 0 with errno ERANGE. stdlib.c's strtod and strtof call
 * this. Leading white space is skipped here, as parse does not; width
 * 32767 is "no limit". A result that rounds to infinity or to zero from
 * nonzero digits (nd > 0) is the range error; a result that underflows
 * only to a subnormal is not reported. */
void __fp_strtod(const char *nptr, char **endptr, struct sf64 *r, unsigned long *f, int c99)
{
    const char *s;
    const char *p;
    int good;
    int nd;
    int neg;
    long exp10;
    unsigned long hi;
    unsigned long lo;

    s = nptr;
    while (*s == ' ' || (*s >= 9 && *s <= 13))
        s++;
    p = s;
    parse(str_get, str_unget, &p, 32767, &good, &nd, &exp10, &neg, c99);
    hi = 0;
    lo = 0;
    if (good == 0) {
        if (endptr != NULL)
            *endptr = (char *)nptr;     /* no conversion */
    } else {
        /* the exponent is part of the constant only if good reached it;
         * a hexadecimal one's digits stop where good does, so they hold
         * no digit beyond it */
        if (endptr != NULL)
            *endptr = (char *)s + good;
        if (r != NULL) {
            double_of(r, nd, exp10, neg);
            hi = r->hi & 0x7FFFFFFFUL;
            lo = r->lo;
        } else {
            *f = float_of(nd, exp10, neg);
            hi = *f & 0x7FFFFFFFUL;
            hi = hi == 0x7F800000UL ? 0x7FF00000UL : hi;
        }
        if (nd > 0 && (hi == 0x7FF00000UL || (hi == 0 && lo == 0)))
            errno = ERANGE;
        return;
    }
    if (r != NULL) {
        r->lo = 0;
        r->hi = 0;
    } else {
        *f = 0;
    }
}

/* The digit at index i of the digits sf64_to_decimal wrote (d1 is index
 * 0), zeros beyond them and before them. Reading out of range as '0'
 * lets the layout code below print leading and trailing zeros with no
 * special cases. */
static int digit(int i, int nd)
{
    return i >= 0 && i < nd ? fp_buf[i] : '0';
}

static void out_n(void (*out)(void *, int), void *k, int c, int n)
{
    while (n > 0) {
        out(k, c);
        n--;
    }
}

/* %a and %A (C99 7.19.6.1): finite v, its sign already taken off, as
 * 0xh.hhhp+d, the leading digit 1 for a normal number and 0 for a
 * subnormal or zero (whose exponent is then -1022, or 0 for zero). The
 * digits are v's bits, so with no precision the fraction is exact,
 * trailing zeros dropped; a precision rounds it to that many digits, to
 * nearest even, which may carry the leading digit to 2 (as glibc does). */
static void hex_print(void (*out)(void *, int), void *k, const struct sf64 *v, int upper, int sign, int alt,
                      int left, int zero, int width, int prec)
{
    char hd[14];        /* the leading digit, then the 13 fraction digits */
    char edig[6];
    char *set;
    int ex;
    int e;
    int i;
    int nf;
    int up;
    int ne;
    int len;
    int point;

    set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    ex = (int)(v->hi >> 20);
    hd[0] = ex != 0;
    for (i = 1; i <= 5; i++)
        hd[i] = (char)(v->hi >> (20 - 4 * i) & 15);
    for (i = 6; i <= 13; i++)
        hd[i] = (char)(v->lo >> (52 - 4 * i) & 15);
    if (ex == 0)
        ex = (v->hi | v->lo) == 0 ? 0 : -1022;
    else
        ex = ex - 1023;
    if (prec < 0) {
        nf = 13;
        while (nf > 0 && hd[nf] == 0)
            nf--;
    } else {
        nf = prec;
        if (nf < 13) {
            /* the digits after the last kept against half (8, then 0s) */
            up = hd[nf + 1] > 8;
            if (hd[nf + 1] == 8) {
                up = hd[nf] & 1;        /* a tie: to even */
                for (i = nf + 2; i <= 13; i++)
                    if (hd[i] != 0)
                        up = 1;
            }
            for (i = nf; up && i >= 0; i--) {
                hd[i]++;
                up = hd[i] == 16 && i > 0;
                if (up)
                    hd[i] = 0;
            }
        }
    }
    e = ex < 0 ? -ex : ex;
    ne = 0;
    do {
        edig[ne] = (char)('0' + e % 10);
        ne++;
        e = e / 10;
    } while (e != 0);
    point = nf > 0 || alt;
    len = (sign != 0) + 3 + point + nf + 2 + ne;        /* 0x, the leading digit; p and the sign */
    if (!left && !zero)
        out_n(out, k, ' ', width - len);
    if (sign)
        out(k, sign);
    out(k, '0');
    out(k, upper ? 'X' : 'x');
    if (!left && zero)
        out_n(out, k, '0', width - len);
    out(k, set[(int)hd[0]]);
    if (point)
        out(k, '.');
    for (i = 1; i <= nf; i++)
        out(k, i <= 13 ? set[(int)hd[i]] : '0');
    out(k, upper ? 'P' : 'p');
    out(k, ex < 0 ? '-' : '+');
    while (ne > 0) {
        ne--;
        out(k, edig[ne]);
    }
    if (left)
        out_n(out, k, ' ', width - len);
}

/* printf's e f g E F G, and a A through hex_print (C89 4.9.6.1, C99
 * 7.19.6.1; stdio.c reaches this through a weak reference): the double
 * at d, each character through out(k, c). plus is the sign a positive
 * value takes (0, '+' or ' '), alt the '#' flag, prec -1 for the
 * default. An infinity prints as inf and a NaN as nan (INF and NAN for
 * the capital conversions).
 *
 * The steps: the sign comes off and |v| is converted to digits
 * (d1 d2 ... times 10^dexp) by sf64_to_decimal, already rounded to the
 * precision; the field length is worked out so that padding can come
 * first; then the sign, any zero padding, the digits in the f or e
 * layout, and any padding on the right. */
void __fp_print(void (*out)(void *, int), void *k, const void *d, int conv, int plus, int alt, int left,
                int zero, int width, int prec)
{
    struct sf64 v;
    char *s;
    int upper;
    int sign;
    int style;
    int nd;
    int dexp;
    int fdigits;
    int p;
    int x;
    int i;
    int len;
    int intlen;
    int point;
    int ex;
    char edig[6];
    int ne;

    v = *(const struct sf64 *)d;
    upper = conv == 'E' || conv == 'F' || conv == 'G' || conv == 'A';
    if (upper)
        conv = conv - 'A' + 'a';
    sign = v.hi & 0x80000000UL ? '-' : plus;
    v.hi = v.hi & 0x7FFFFFFFUL;
    if ((v.hi & 0x7FF00000UL) == 0x7FF00000UL) {
        s = (v.hi & 0xFFFFFUL) != 0 || v.lo != 0 ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        len = 3 + (sign != 0);
        if (!left)
            out_n(out, k, ' ', width - len);
        if (sign)
            out(k, sign);
        for (i = 0; i < 3; i++)
            out(k, s[i]);
        if (left)
            out_n(out, k, ' ', width - len);
        return;
    }
    if (conv == 'a') {
        hex_print(out, k, &v, upper, sign, alt, left, zero, width, prec);
        return;
    }
    if (prec < 0)
        prec = 6;
    /* %g: P significant digits (P = prec, 0 meaning 1). The digits are
     * made once in e style; X, their decimal exponent after rounding,
     * picks the layout as C says (f when -4 <= X < P), and the same
     * digits serve either layout, f then showing P - 1 - X decimals. */
    if (conv == 'g') {
        p = prec == 0 ? 1 : prec;
        nd = sf64_to_decimal(&v, 'e', p - 1, fp_buf, &dexp);
        x = nd == 0 ? 0 : dexp;
        style = x < p && x >= -4 ? 'f' : 'e';
        fdigits = style == 'f' ? p - 1 - x : p - 1;
        if (!alt) {
            /* no trailing zeros, and no point without digits after it */
            while (nd > 0 && fp_buf[nd - 1] == '0')
                nd--;
            i = style == 'f' ? nd - 1 - x : nd - 1;
            if (i < fdigits)
                fdigits = i < 0 ? 0 : i;
        }
    } else {
        style = conv;
        fdigits = prec;
        nd = sf64_to_decimal(&v, style, prec, fp_buf, &dexp);
    }
    if (nd == 0)
        dexp = 0;
    /* the field's length; for e style the exponent's digits are made now
     * (least significant first, at least two) so that they count */
    point = fdigits > 0 || alt;
    ne = 0;
    if (style == 'f') {
        intlen = dexp >= 0 ? dexp + 1 : 1;
        len = intlen + point + fdigits;
    } else {
        ex = dexp < 0 ? -dexp : dexp;
        do {
            edig[ne] = '0' + ex % 10;
            ne++;
            ex = ex / 10;
        } while (ex != 0 || ne < 2);
        len = 1 + point + fdigits + 2 + ne;
    }
    len = len + (sign != 0);
    /* space padding goes before the sign, zero padding after it */
    if (!left && !zero)
        out_n(out, k, ' ', width - len);
    if (sign)
        out(k, sign);
    if (!left && zero)
        out_n(out, k, '0', width - len);
    /* f: digits 0 .. dexp before the point (or one 0 when |v| < 1), then
     * fdigits after it; e: one digit, the point, fdigits, the exponent */
    if (style == 'f') {
        if (dexp < 0)
            out(k, '0');
        for (i = 0; i <= dexp; i++)
            out(k, digit(i, nd));
        if (point)
            out(k, '.');
        for (i = 1; i <= fdigits; i++)
            out(k, digit(dexp + i, nd));
    } else {
        out(k, digit(0, nd));
        if (point)
            out(k, '.');
        for (i = 1; i <= fdigits; i++)
            out(k, digit(i, nd));
        out(k, upper ? 'E' : 'e');
        out(k, dexp < 0 ? '-' : '+');
        while (ne > 0) {
            ne--;
            out(k, edig[ne]);
        }
    }
    if (left)
        out_n(out, k, ' ', width - len);
}
