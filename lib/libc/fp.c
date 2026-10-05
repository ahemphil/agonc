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

/* A floating constant as strtod and scanf read it (C89 4.10.1.4): a sign,
 * digits with at most one '.', then e or E, a sign and digits, at most
 * width characters from get, the character after them given back. The
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
                 long *exp10, int *neg)
{
    int n;
    int c;
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
    c = get(src);
    if (c == '+' || c == '-') {
        *neg = c == '-';
        n++;
        c = get(src);
    }
    while (n < width) {
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
     * does *good move past the 'e' */
    if (any && n < width && (c == 'e' || c == 'E')) {
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

/* scanf's e f g E G (stdio.c reaches this through a weak reference): 1 if
 * a whole constant was read, stored to dest (a double for size 'l' or 'L',
 * else a float, rounded from the decimal itself) unless dest is null. */
int __fp_scan(int (*get)(void *), void (*unget)(void *, int), void *src, int width, int size, void *dest)
{
    int n;
    int good;
    int nd;
    int neg;
    long exp10;

    n = parse(get, unget, src, width, &good, &nd, &exp10, &neg);
    /* characters read past the end of a complete constant ("1e") are a
     * matching failure, as C has it: the input item is "1e", which is no
     * number, and a stream cannot take back more than one character */
    if (good == 0 || good != n)
        return 0;
    if (dest != NULL) {
        if (size == 'l' || size == 'L')
            sf64_from_decimal((struct sf64 *)dest, neg, fp_buf, nd, exp10);
        else
            *(unsigned long *)dest = sf32_from_decimal(neg, fp_buf, nd, exp10);
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

/* strtod (C89 4.10.1.4): the value to *r; out of range, HUGE_VAL (an
 * infinity) or 0 with errno ERANGE. stdlib.c's strtod calls this, with r
 * its double's address. Leading white space is skipped here, as parse
 * does not; width 32767 is "no limit". A result that rounds to infinity
 * or to zero from nonzero digits (nd > 0) is the range error; a result
 * that underflows only to a subnormal is not reported. */
void __fp_strtod(const char *nptr, char **endptr, struct sf64 *r)
{
    const char *s;
    const char *p;
    int good;
    int nd;
    int neg;
    long exp10;

    s = nptr;
    while (*s == ' ' || (*s >= 9 && *s <= 13))
        s++;
    p = s;
    parse(str_get, str_unget, &p, 32767, &good, &nd, &exp10, &neg);
    r->lo = 0;
    r->hi = 0;
    if (good == 0) {
        if (endptr != NULL)
            *endptr = (char *)nptr;     /* no conversion */
    } else {
        /* the exponent is part of the constant only if good reached it */
        if (endptr != NULL)
            *endptr = (char *)s + good;
        sf64_from_decimal(r, neg, fp_buf, nd, exp10);
        if (nd > 0 && ((r->hi & 0x7FFFFFFFUL) == 0x7FF00000UL || ((r->hi & 0x7FFFFFFFUL) == 0 && r->lo == 0)))
            errno = ERANGE;
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

/* printf's e f g E F G (C89 4.9.6.1; stdio.c reaches this through a weak
 * reference): the double at d, each character through out(k, c). plus
 * is the sign a positive value takes (0, '+' or ' '), alt the '#' flag,
 * prec -1 for the default. An infinity prints as inf and a NaN as nan
 * (INF and NAN for the capital conversions).
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
    upper = conv == 'E' || conv == 'F' || conv == 'G';
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
