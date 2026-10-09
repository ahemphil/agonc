/* rt_ref.c - the C of rt.s's multiply and divide helpers: what each one
 * computes, by the same steps as its assembly, written for reading and
 * for checking. It is not part of any library; tests/rt/test_rt.py
 * builds it on the PC, checks it against the PC's own arithmetic, and
 * uses its answers to check the helpers themselves on the emulator.
 *
 * Every value is an unsigned long holding a 24-bit int (masked with
 * M24) or a 32-bit long (M32), so the code means the same on the PC and
 * on the Agon. A helper's signed forms work on two's-complement bit
 * patterns: a 24-bit negative x is held as 2^24 + x.
 *
 * The helpers, with their asm in lib/rt/rt.s:
 *   __imul       ref_imul        24-bit *, from six MLT byte products
 *   __idivu      ref_idivu       24-bit unsigned / and %, together
 *   __idivs      ref_idivs       24-bit signed /, truncating
 *   __irems      ref_irems       24-bit signed %, the dividend's sign
 *   __lmul       ref_lmul        32-bit *, from ten MLT byte products
 *   __ldivmodu   ref_ldivmodu    32-bit unsigned / and %, together
 * (__iremu is __idivu's remainder; __ldivu, __lremu, __ldivs and
 * __lrems take theirs from __ldivmodu, as ref_ldivs and ref_lrems do.)
 */

#define M24 0xFFFFFFUL
#define M32 0xFFFFFFFFUL

/* byte i of v */
static unsigned long byte_of(unsigned long v, int i)
{
    return v >> (8 * i) & 0xFF;
}

/* ---- 24-bit multiply --------------------------------------------------------------- */

/* a*b mod 2^24 depends only on the byte products a_i*b_j with i+j <= 2:
 * P00 = a0*b0, X = a0*b1 + a1*b0 (its low 16 bits matter), and S =
 * a0*b2 + a1*b1 + a2*b0 (its low 8): the result is P00 + 256*X + 65536*S.
 * The assembly sums S in A, adds it to X's high byte, shifts X a byte up
 * and adds P00, each sum cut to the bits that reach the result. */
unsigned long ref_imul(unsigned long a, unsigned long b)
{
    unsigned long p00;
    unsigned long x;
    unsigned long s;

    p00 = byte_of(a, 0) * byte_of(b, 0);
    x = byte_of(a, 0) * byte_of(b, 1) + byte_of(a, 1) * byte_of(b, 0);
    s = (byte_of(a, 0) * byte_of(b, 2) + byte_of(a, 1) * byte_of(b, 1) + byte_of(a, 2) * byte_of(b, 0)) & 0xFF;
    return (((x + 256 * s) & 0xFFFF) * 256 + p00) & M24;
}

/* ---- 24-bit divide ------------------------------------------------------------------ */

/* The rounds of restoring division for the low `bytes` bytes of n, from
 * remainder r (below d): each brings the next bit of n into r, and takes
 * the divisor d off when it fits, which is the quotient's bit; q gathers
 * the bits (those of the bytes above are 0). For any r below a 32-bit d,
 * doubling r could reach 2^32, so the 33rd bit is kept, as __ldivmodu's
 * slow loop keeps it in the carry: with it set, r is above d whatever its
 * low 32 bits say. (Skipping the leading bytes, as both callers do, keeps
 * r below 2^31 before each doubling, so it is never set; the tests cannot
 * reach it.) */
static unsigned long rounds(unsigned long n, int bytes, unsigned long d, unsigned long *r)
{
    unsigned long q;
    int i;
    int top;

    q = 0;
    for (i = 8 * bytes - 1; i >= 0; i--) {
        top = (int)(*r >> 31 & 1);
        *r = (2 * *r + (n >> i & 1)) & M32;
        q = 2 * q;
        if (top || *r >= d) {
            *r = (*r - d) & M32;
            q = q + 1;
        }
    }
    return q;
}

/* The number of bytes v needs: 0 for 0, else 1 to 4. */
static int bytes_of(unsigned long v)
{
    int n;

    n = 0;
    while (v != 0) {
        n++;
        v = v >> 8;
    }
    return n;
}

/* __idivu: n / d with *r = n % d, unsigned 24-bit. Division by zero
 * keeps its old answer: quotient 0xFFFFFF, remainder the dividend. The
 * assembly picks one of three loops by the divisor:
 *   narrow (1..127): the remainder fits in A; one group of 8 rounds per
 *     significant byte of the dividend, but a top byte below the divisor
 *     is the remainder outright, saving its group;
 *   wide (128..0x7FFFFF): the remainder in HL; the dividend's top S bytes
 *     are the remainder outright, S being the divisor's byte length less
 *     one, since they are below 256^S <= the divisor; a dividend below
 *     the divisor gives quotient 0 at once;
 *   huge (0x800000 and up): the quotient is 0 or 1, one compare. */
unsigned long ref_idivu(unsigned long n, unsigned long d, unsigned long *r)
{
    int bn;
    int skip;

    n = n & M24;
    d = d & M24;
    if (d == 0) {
        *r = n;
        return M24;
    }
    if (d >= 0x800000UL) {
        *r = n >= d ? n - d : n;
        return n >= d;
    }
    bn = bytes_of(n);
    if (d < 128) {
        skip = bn > 0 && byte_of(n, bn - 1) < d;        /* the top byte: a remainder */
    } else {
        if (n < d) {
            *r = n;
            return 0;
        }
        skip = bytes_of(d) - 1;
    }
    *r = n >> (8 * (bn - skip));        /* the skipped top bytes */
    return rounds(n, bn - skip, d, r) & M24;
}

/* 24-bit two's complement: whether v is negative, and -v */
static int neg24(unsigned long v)
{
    return (v & 0x800000UL) != 0;
}

static unsigned long minus24(unsigned long v)
{
    return (0 - v) & M24;
}

/* __idivs: the magnitudes divided by __idivu, the quotient negated when
 * exactly one operand is negative (truncation toward zero, as C's /).
 * -8388608 is its own magnitude, 0x800000, and -8388608 / -1 wraps to
 * -8388608. */
unsigned long ref_idivs(unsigned long n, unsigned long d)
{
    unsigned long q;
    unsigned long r;

    n = n & M24;
    d = d & M24;
    q = ref_idivu(neg24(n) ? minus24(n) : n, neg24(d) ? minus24(d) : d, &r);
    return neg24(n) != neg24(d) ? minus24(q) : q;
}

/* __irems: the remainder of the magnitudes, with the dividend's sign (as
 * C's %); the divisor's sign does not matter. */
unsigned long ref_irems(unsigned long n, unsigned long d)
{
    unsigned long r;

    n = n & M24;
    d = d & M24;
    ref_idivu(neg24(n) ? minus24(n) : n, neg24(d) ? minus24(d) : d, &r);
    return neg24(n) ? minus24(r) : r;
}

/* ---- 32-bit multiply ---------------------------------------------------------------- */

/* Byte k of the product gathers every a_i*b_j with i + j = k, and only
 * the ten pairs with i + j <= 3 reach the low 32 bits. One column at a
 * time: an accumulator takes the column's carry and its 16-bit products;
 * its low byte is the result's byte k, and the rest, a byte down, is the
 * next column's carry. Column 3 needs only the low bytes. */
unsigned long ref_lmul(unsigned long a, unsigned long b)
{
    unsigned long acc;
    unsigned long result;
    int k;
    int i;

    result = 0;
    acc = 0;
    for (k = 0; k < 4; k++) {
        for (i = 0; i <= k; i++)
            acc = acc + byte_of(a, i) * byte_of(b, k - i);
        result = result | (acc & 0xFF) << (8 * k);
        acc = acc >> 8;
    }
    return result & M32;
}

/* ---- 32-bit divide ------------------------------------------------------------------ */

/* __ldivmodu: n / d with *r = n % d, unsigned 32-bit. With bn the
 * dividend's significant bytes and bd the divisor's, the quotient has at
 * most k = bn - bd + 1 bytes: the dividend's top bd - 1 bytes go straight
 * into the remainder, and only its low k bytes take rounds; none when k
 * <= 0, the quotient 0 and the remainder the dividend. (The assembly runs
 * a fast loop for a divisor up to 2^23, with the remainder in 24 bits, and
 * a slow one above, with it in 32 bits and a 33rd in the carry; both are
 * these rounds.) Division by zero gives no particular answer: this gives
 * the C one, and the tests never divide by zero. */
unsigned long ref_ldivmodu(unsigned long n, unsigned long d, unsigned long *r)
{
    int k;

    n = n & M32;
    d = d & M32;
    if (d == 0) {
        *r = n;
        return M32;
    }
    k = bytes_of(n) - bytes_of(d) + 1;
    if (k <= 0) {
        *r = n;
        return 0;
    }
    *r = k == 4 ? 0 : n >> (8 * k);
    return rounds(n, k, d, r) & M32;
}

/* 32-bit two's complement */
static int neg32(unsigned long v)
{
    return (v & 0x80000000UL) != 0;
}

static unsigned long minus32(unsigned long v)
{
    return (0 - v) & M32;
}

/* __ldivs and __lrems: as __idivs and __irems, at 32 bits */
unsigned long ref_ldivs(unsigned long n, unsigned long d)
{
    unsigned long q;
    unsigned long r;

    q = ref_ldivmodu(neg32(n) ? minus32(n) : n, neg32(d) ? minus32(d) : d, &r);
    return neg32(n) != neg32(d) ? minus32(q) : q;
}

unsigned long ref_lrems(unsigned long n, unsigned long d)
{
    unsigned long r;

    ref_ldivmodu(neg32(n) ? minus32(n) : n, neg32(d) ? minus32(d) : d, &r);
    return neg32(n) ? minus32(r) : r;
}
