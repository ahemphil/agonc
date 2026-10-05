/* int32.c - 32-bit arithmetic on two 16-bit halves (int32.h).
 *
 * cc1 folds the target's long constants with these functions and writes
 * them into the IR; cc2 reads them back and splits each into the 24-bit
 * and 8-bit parts a 32-bit value has in registers. Each half is kept
 * in 0..65535 at all times (the masks with 65535 restore that after every
 * step), so a value has exactly one representation and comparisons can
 * look at the halves directly. No intermediate result reaches 2^24, so
 * every line computes the same bits with a 24-bit or a 32-bit int.
 */

#include "int24.h"
#include "int32.h"

void i32_set(struct i32 *r, int hi, int lo)
{
    r->hi = hi & 65535;
    r->lo = lo & 65535;
}

/* Bits 16..23 of v, then bit 23 copied into bits 24..31. The & 255 makes
 * the result independent of how wide int is and of how >> treats a
 * negative v. */
void i32_from_int(struct i32 *r, int v)
{
    r->lo = v & 65535;
    r->hi = (v >> 16) & 255;
    if (v & 0x800000)
        r->hi = r->hi | 0xFF00;
}

void i32_from_uint(struct i32 *r, int v)
{
    r->lo = v & 65535;
    r->hi = (v >> 16) & 255;
}

int i32_low24(struct i32 *a)
{
    int v;

    v = ((a->hi & 255) << 16) | a->lo;
    return wrap24(v);
}

int i32_high8(struct i32 *a)
{
    return (a->hi >> 8) & 255;
}

/* r from bits 24..31 (high8) and bits 0..23 (low24). A function of its
 * own: AgDev 3.1.0's LTO build of cc1 got "hi8 << 8" wrong written inline
 * (the shift count came from a stale register, making it a shift by 0). */
void i32_join(struct i32 *r, int high8, int low24)
{
    r->lo = low24 & 65535;
    r->hi = ((low24 >> 16) & 255) | ((high8 & 255) * 256);
}

int i32_is_neg(struct i32 *a)
{
    return (a->hi & 0x8000) != 0;
}

int i32_is_zero(struct i32 *a)
{
    return a->hi == 0 && a->lo == 0;
}

int i32_fits_int(struct i32 *a)
{
    int top;

    top = a->hi >> 7;                   /* bits 23..31 must all match */
    return top == 0 || top == 511;
}

int i32_fits_uint(struct i32 *a)
{
    return (a->hi >> 8) == 0;
}

void i32_add(struct i32 *r, struct i32 *a, struct i32 *b)
{
    int lo;
    int hi;

    lo = a->lo + b->lo;                 /* up to 17 bits: bit 16 is the carry */
    hi = a->hi + b->hi + (lo >> 16);
    r->lo = lo & 65535;
    r->hi = hi & 65535;
}

/* Two's complement negation: the complement plus one. */
void i32_neg(struct i32 *r, struct i32 *a)
{
    struct i32 one;
    struct i32 c;

    i32_cpl(&c, a);
    i32_set(&one, 0, 1);
    i32_add(r, &c, &one);
}

void i32_cpl(struct i32 *r, struct i32 *a)
{
    r->lo = ~a->lo & 65535;
    r->hi = ~a->hi & 65535;
}

void i32_sub(struct i32 *r, struct i32 *a, struct i32 *b)
{
    struct i32 nb;

    i32_neg(&nb, b);
    i32_add(r, a, &nb);
}

/* Product modulo 2^32 from 8-bit pieces, so no partial product exceeds
 * 2^16 and no sum 2^24. Schoolbook long multiplication in base 256: byte i
 * of a times byte j of b belongs in column i + j; columns 4 and up are
 * beyond bit 31 and are never formed. A column collects at most four
 * products, then one pass from column 0 upwards moves each column's
 * excess over 8 bits into the next as a carry. */
void i32_mul(struct i32 *r, struct i32 *a, struct i32 *b)
{
    int x[4];
    int y[4];
    int acc[4];
    int i;
    int j;
    int carry;

    x[0] = a->lo & 255; x[1] = a->lo >> 8; x[2] = a->hi & 255; x[3] = a->hi >> 8;
    y[0] = b->lo & 255; y[1] = b->lo >> 8; y[2] = b->hi & 255; y[3] = b->hi >> 8;
    for (i = 0; i < 4; i++)
        acc[i] = 0;
    for (i = 0; i < 4; i++)
        for (j = 0; i + j < 4; j++)
            acc[i + j] = acc[i + j] + x[i] * y[j];
    carry = 0;
    for (i = 0; i < 4; i++) {
        acc[i] = acc[i] + carry;
        carry = acc[i] >> 8;
        acc[i] = acc[i] & 255;
    }
    r->lo = acc[0] | (acc[1] << 8);
    r->hi = acc[2] | (acc[3] << 8);
}

void i32_and(struct i32 *r, struct i32 *a, struct i32 *b)
{
    r->lo = a->lo & b->lo;
    r->hi = a->hi & b->hi;
}

void i32_or(struct i32 *r, struct i32 *a, struct i32 *b)
{
    r->lo = a->lo | b->lo;
    r->hi = a->hi | b->hi;
}

void i32_xor(struct i32 *r, struct i32 *a, struct i32 *b)
{
    r->lo = a->lo ^ b->lo;
    r->hi = a->hi ^ b->hi;
}

/* One bit per round, the top bit of lo moving into hi; n < 32 rounds. */
void i32_shl(struct i32 *r, struct i32 *a, int n)
{
    int hi;
    int lo;

    hi = a->hi;
    lo = a->lo;
    while (n > 0) {
        hi = ((hi << 1) | (lo >> 15)) & 65535;
        lo = (lo << 1) & 65535;
        n--;
    }
    r->hi = hi;
    r->lo = lo;
}

/* One bit per round, the low bit of hi moving into lo. A signed shift of
 * a negative value feeds the sign bit back in at the top (an arithmetic
 * shift); otherwise zeros come in (a logical shift). */
void i32_shr(struct i32 *r, struct i32 *a, int n, int is_signed)
{
    int hi;
    int lo;
    int top;

    hi = a->hi;
    lo = a->lo;
    top = is_signed && (hi & 0x8000) ? 0x8000 : 0;
    while (n > 0) {
        lo = ((lo >> 1) | ((hi & 1) << 15)) & 65535;
        hi = (hi >> 1) | top;
        n--;
    }
    r->hi = hi;
    r->lo = lo;
}

int i32_cmp(struct i32 *a, struct i32 *b, int is_signed)
{
    int ah;
    int bh;

    ah = a->hi;
    bh = b->hi;
    if (is_signed) {                    /* flip the sign bit: signed order becomes unsigned order */
        ah = ah ^ 0x8000;
        bh = bh ^ 0x8000;
    }
    if (ah != bh)
        return ah < bh ? -1 : 1;
    if (a->lo != b->lo)
        return a->lo < b->lo ? -1 : 1;
    return 0;
}

/* Restoring division, one bit at a time: the dividend's bits are shifted,
 * top first, into a running remainder r; whenever r reaches b, b is
 * subtracted and a 1 enters the quotient, else a 0. This is long division
 * in base 2. The shift of r never loses a bit: r is at most the part of
 * the dividend shifted in so far, under 2^31 before the last shift. */
void i32_divu(struct i32 *q, struct i32 *rem, struct i32 *a, struct i32 *b)
{
    struct i32 quo;
    struct i32 r;
    struct i32 t;
    int i;
    int bit;

    i32_set(&quo, 0, 0);
    i32_set(&r, 0, 0);
    for (i = 31; i >= 0; i--) {
        bit = i >= 16 ? (a->hi >> (i - 16)) & 1 : (a->lo >> i) & 1;
        i32_shl(&r, &r, 1);
        r.lo = r.lo | bit;
        i32_shl(&quo, &quo, 1);
        if (i32_cmp(&r, b, 0) >= 0) {
            i32_sub(&t, &r, b);
            r = t;
            quo.lo = quo.lo | 1;
        }
    }
    *q = quo;
    *rem = r;
}

/* Divides the magnitudes, then fixes the signs: the quotient is negative
 * when exactly one operand is, and the remainder takes the dividend's sign,
 * so the quotient truncates towards zero and a == q * b + rem. */
void i32_divs(struct i32 *q, struct i32 *rem, struct i32 *a, struct i32 *b)
{
    struct i32 ua;
    struct i32 ub;
    int na;
    int nb;

    na = i32_is_neg(a);
    nb = i32_is_neg(b);
    if (na)
        i32_neg(&ua, a);
    else
        ua = *a;
    if (nb)
        i32_neg(&ub, b);
    else
        ub = *b;
    i32_divu(q, rem, &ua, &ub);
    if (na != nb)
        i32_neg(q, q);
    if (na)
        i32_neg(rem, rem);
}

/* Multiplies by ten and adds each digit, carrying lo's excess over 16 bits
 * into hi; hi is masked, so the value wraps modulo 2^32. A leading '-'
 * negates at the end. */
int i32_parse(struct i32 *r, char *s)
{
    int neg;
    int lo;
    int hi;
    int any;

    neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    lo = 0;
    hi = 0;
    any = 0;
    while (*s >= '0' && *s <= '9') {
        lo = lo * 10 + (*s - '0');
        hi = (hi * 10 + (lo >> 16)) & 65535;
        lo = lo & 65535;
        any = 1;
        s++;
    }
    if (!any || *s != 0)
        return 0;
    r->hi = hi;
    r->lo = lo;
    if (neg)
        i32_neg(r, r);
    return 1;
}

/* Digits come out least significant first (repeated division by ten), so
 * they are collected in digits[] and copied out in reverse. A negative
 * signed value is negated first; the most negative one, -2147483648,
 * negates to itself and still prints correctly read as unsigned. */
void i32_str(char *buf, struct i32 *a, int is_signed)
{
    struct i32 v;
    struct i32 ten;
    struct i32 q;
    struct i32 d;
    char digits[12];
    int n;
    int i;

    v = *a;
    i = 0;
    if (is_signed && i32_is_neg(&v)) {
        buf[i] = '-';
        i++;
        i32_neg(&v, &v);
    }
    i32_set(&ten, 0, 10);
    n = 0;
    do {
        i32_divu(&q, &d, &v, &ten);
        digits[n] = '0' + d.lo;
        n++;
        v = q;
    } while (!i32_is_zero(&v));
    while (n > 0) {
        n--;
        buf[i] = digits[n];
        i++;
    }
    buf[i] = 0;
}
