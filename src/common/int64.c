/* int64.c - 64-bit arithmetic in integer C (int64.h).
 *
 * Products and quotients are taken 16 bits at a time, so every
 * intermediate result fits the 32 bits an unsigned long has everywhere.
 *
 * The hot functions (add, subtract, compare, the shifts, multiply and the
 * long division) have eZ80 assembly beside their C, chosen by I64_ASM: 1
 * where agonc compiles this file (the library's ll.c, and cc1, cpp, cc2
 * and ld themselves from stage 2 on), 0 elsewhere (the PC, AgDev's stage
 * 1). The assembly computes exactly the C's results (a struct i64 is its
 * 8 bytes low first, so the value is a plain little-endian number);
 * tests/libc_c/t_i64eq.c compares the two builds.
 *
 * Reading the assembly: each function's frame follows abi.md section 5,
 * so the parameters are (ix+6), (ix+9), (ix+12), (ix+15) in declaration
 * order, and the first local is (ix-3). Assembly cannot name a C local, so
 * a function that needs a work buffer declares a pointer p first and sets
 * p = w; the assembly then loads w's address from (ix-3) into IY and works
 * on w through (iy+k). Labels are @local (abi.md section 10). A function's
 * assembly is split over several #asm blocks only because cc1 accepts at
 * most 598 bytes per block; the blocks run straight on, one into the next.
 *
 * Sections: helpers that need no assembly; add and subtract; multiply;
 * the small-divisor helpers; division; the bitwise operations and negate;
 * the shifts; compare; decimal text.
 */

/* I64_ASM: the eZ80 code (1) or the C (0); define it to choose. */
#ifndef I64_ASM
#ifdef __AGONC__
#define I64_ASM 1
#else
#define I64_ASM 0
#endif
#endif

#include <limits.h>
#include "int64.h"

/* Keeps a value to 32 bits where long is wider (a 64-bit PC); nothing to
 * do, and so no code, where long is 32 bits. */
#if ULONG_MAX == 0xFFFFFFFFUL
#define M32(x) (x)
#else
#define M32(x) ((x) & 0xFFFFFFFFUL)
#endif

void i64_set(struct i64 *r, unsigned long hi, unsigned long lo)
{
    r->hi = M32(hi);
    r->lo = M32(lo);
}

void i64_from_long(struct i64 *r, unsigned long v, int is_signed)
{
    r->lo = M32(v);
    r->hi = is_signed && (r->lo & 0x80000000UL) ? 0xFFFFFFFFUL : 0;
}

int i64_is_zero(const struct i64 *a)
{
    return a->hi == 0 && a->lo == 0;
}

int i64_is_neg(const struct i64 *a)
{
    return (a->hi & 0x80000000UL) != 0;
}

/* ---- add and subtract ---- */

/* Ripple-carry addition a byte at a time: one add, then seven adc, each
 * taking the carry out of the byte below. IY walks a, HL walks b, DE walks
 * r (r may be a or b: each byte is read before it is written). */
#if I64_ASM
void i64_add(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
#asm
        ld      iy, (ix+9)
        ld      hl, (ix+12)
        ld      de, (ix+6)
        ld      a, (iy+0)
        add     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+1)
        adc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+2)
        adc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+3)
        adc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+4)
#endasm
#asm
        adc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+5)
        adc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+6)
        adc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+7)
        adc     a, (hl)
        ld      (de), a
#endasm
}
#else
void i64_add(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    unsigned long lo;

    lo = M32(a->lo + b->lo);
    /* the low sum wrapped (a carry out) exactly when it is below an addend */
    r->hi = M32(a->hi + b->hi + (lo < a->lo));
    r->lo = lo;
}
#endif

/* As i64_add, with sub and seven sbc: a - b, the borrow rippling up. */
#if I64_ASM
void i64_sub(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
#asm
        ld      iy, (ix+9)
        ld      hl, (ix+12)
        ld      de, (ix+6)
        ld      a, (iy+0)
        sub     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+1)
        sbc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+2)
        sbc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+3)
        sbc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+4)
#endasm
#asm
        sbc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+5)
        sbc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+6)
        sbc     a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        ld      a, (iy+7)
        sbc     a, (hl)
        ld      (de), a
#endasm
}
#else
void i64_sub(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    unsigned long lo;

    lo = M32(a->lo - b->lo);
    /* the low half borrows from the high half when it would go negative */
    r->hi = M32(a->hi - b->hi - (a->lo < b->lo));
    r->lo = lo;
}
#endif

/* ---- multiply ---- */

/* the 16-bit pieces of a, least significant first */
static void pieces(unsigned long *p, const struct i64 *a)
{
    p[0] = a->lo & 0xFFFF;
    p[1] = a->lo >> 16 & 0xFFFF;
    p[2] = a->hi & 0xFFFF;
    p[3] = a->hi >> 16 & 0xFFFF;
}

/* the inverse of pieces: each p[k] must be 0 .. 0xFFFF */
static void join(struct i64 *r, const unsigned long *p)
{
    r->lo = p[0] | p[1] << 16;
    r->hi = p[2] | p[3] << 16;
}

/* Column by column: each product's low 16 bits into its own column, its
 * high 16 bits into the next.
 *
 * The assembly works in bytes, because the eZ80's MLT multiplies D by E
 * into DE (8 x 8 to 16 bits). It copies a to w[0..7] and b to w[8..15] and
 * clears the product, w[16..31]. For each nonzero byte of a, at index c
 * (the byte is kept in w[32] so D can be reloaded from it), @step adds it
 * times each byte j of b into product bytes c+j and c+j+1; on a carry
 * out, @carry increments the bytes above until one does not wrap to 0.
 * All 64 byte products are added, so the buffer ends up holding the full
 * 128-bit product, and no partial sum ever exceeds it: a carry never runs
 * past w[31] into w[32]. The low 8 bytes are the result. */
#if I64_ASM
void i64_mul(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    unsigned char *p;
    unsigned char w[33];                /* a 0-7, b 8-15, the product 16-31, a byte 32 */

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)
        lea     de, iy+0
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      hl, (ix+12)
        lea     de, iy+8
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      hl, 0
        ld      (iy+16), hl
        ld      (iy+19), hl
        ld      (iy+22), hl
        ld      (iy+25), hl
        ld      (iy+28), hl
        ld      (iy+31), l
        ld      c, 0
@outer:
#endasm
#asm
        lea     hl, iy+0
        ld      de, 0
        ld      e, c
        add     hl, de
        ld      a, (hl)
        or      a
        jp      z, @nexti
        ld      (iy+32), a
        ld      de, 16
        add     hl, de
        ld      e, (iy+8)
        call    @step
        ld      e, (iy+9)
        call    @step
        ld      e, (iy+10)
        call    @step
        ld      e, (iy+11)
        call    @step
        ld      e, (iy+12)
        call    @step
        ld      e, (iy+13)
        call    @step
        ld      e, (iy+14)
#endasm
#asm
        call    @step
        ld      e, (iy+15)
        call    @step
@nexti:
        inc     c
        ld      a, c
        cp      a, 8
        jp      nz, @outer
        ld      de, (ix+6)
        lea     hl, iy+16
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        jr      @done
@step:
        ld      d, (iy+32)
        mlt     de
        ld      a, (hl)
        add     a, e
        ld      (hl), a
        inc     hl
        ld      a, (hl)
        adc     a, d
        ld      (hl), a
#endasm
#asm
        ret     nc
        push    hl
@carry:
        inc     hl
        inc     (hl)
        jr      z, @carry
        pop     hl
        ret
@done:
#endasm
}
#else
void i64_mul(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    unsigned long pa[4];
    unsigned long pb[4];
    unsigned long pr[4];
    unsigned long acc;
    unsigned long up;
    unsigned long carry;
    unsigned long p;
    int k;
    int i;

    pieces(pa, a);
    pieces(pb, b);
    carry = 0;
    /* only columns 0..3 (bits 0..63) are formed: the rest are discarded
     * modulo 2^64. acc and up each collect at most four 16-bit halves,
     * so neither comes near 2^32. */
    for (k = 0; k < 4; k++) {
        acc = carry;
        up = 0;
        for (i = 0; i <= k; i++) {
            p = pa[i] * pb[k - i];
            acc = acc + (p & 0xFFFF);
            up = up + (p >> 16);
        }
        pr[k] = acc & 0xFFFF;
        carry = (acc >> 16) + up;
    }
    join(r, pr);
}
#endif

/* ---- the small-divisor helpers ---- */

/* Short division in base 65536, the top piece first, as done on paper:
 * the remainder so far is below d, so rem << 16 | p[k] stays under 2^32.
 * d must not be 0. */
unsigned int i64_divsmall(struct i64 *q, const struct i64 *a, unsigned int d)
{
    unsigned long p[4];
    unsigned long cur;
    unsigned long rem;
    int k;

    pieces(p, a);
    rem = 0;
    for (k = 3; k >= 0; k--) {
        cur = rem << 16 | p[k];
        p[k] = cur / d;
        rem = cur % d;
    }
    join(q, p);
    return (unsigned int)rem;
}

/* Each piece times m plus the carry from the piece below: at most
 * 65535 * 65535 + 65535, under 2^32. A carry left over after the top
 * piece is the overflow. */
int i64_muladd(struct i64 *r, unsigned int m, unsigned int c)
{
    unsigned long p[4];
    unsigned long cur;
    unsigned long carry;
    int k;

    pieces(p, r);
    carry = c;
    for (k = 0; k < 4; k++) {
        cur = p[k] * m + carry;
        p[k] = cur & 0xFFFF;
        carry = cur >> 16;
    }
    join(r, p);
    return carry != 0;
}

/* ---- division ---- */

/* Bit by bit, from the top; a 16-bit divisor takes the short road.
 *
 * This is restoring division (long division in base 2): each round shifts
 * the next dividend bit into the remainder r, and if r >= b, subtracts b
 * and sets that quotient bit. The remainder before a shift is never more
 * than the dividend bits taken so far, so it never loses a bit.
 *
 * The assembly keeps the dividend n in w[0] and r in w[1] and shifts all
 * 16 bytes left together, so n's top bit moves into r's bottom. The bit
 * shifted out of n's bottom is free, so the quotient bit goes there
 * (set 0, (iy+0)), and after 64 rounds w[0] holds the quotient. Each round
 * computes t = r - b into w[3]; no borrow means r >= b, and t replaces r. */
#if I64_ASM
void i64_divu(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b)
{
    struct i64 *p;
    struct i64 w[4];                    /* n (then q), r, b, t */

    if (b->hi == 0 && b->lo <= 0xFFFF) {
        i64_set(rem, 0, i64_divsmall(q, a, (unsigned int)b->lo));
        return;
    }
    p = w;
    w[0] = *a;
    w[1].hi = 0;
    w[1].lo = 0;
    w[2] = *b;
#asm
        ld      iy, (ix-3)
        ld      b, 64
@loop:
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+4)
        rl      (iy+5)
        rl      (iy+6)
        rl      (iy+7)
        rl      (iy+8)
        rl      (iy+9)
        rl      (iy+10)
        rl      (iy+11)
        rl      (iy+12)
        rl      (iy+13)
        rl      (iy+14)
        rl      (iy+15)
        ld      a, (iy+8)
        sub     a, (iy+16)
        ld      (iy+24), a
        ld      a, (iy+9)
#endasm
#asm
        sbc     a, (iy+17)
        ld      (iy+25), a
        ld      a, (iy+10)
        sbc     a, (iy+18)
        ld      (iy+26), a
        ld      a, (iy+11)
        sbc     a, (iy+19)
        ld      (iy+27), a
        ld      a, (iy+12)
        sbc     a, (iy+20)
        ld      (iy+28), a
        ld      a, (iy+13)
        sbc     a, (iy+21)
        ld      (iy+29), a
        ld      a, (iy+14)
        sbc     a, (iy+22)
        ld      (iy+30), a
        ld      a, (iy+15)
        sbc     a, (iy+23)
        ld      (iy+31), a
#endasm
#asm
        jr      c, @less
        ld      hl, (iy+24)
        ld      (iy+8), hl
        ld      hl, (iy+27)
        ld      (iy+11), hl
        ld      a, (iy+30)
        ld      (iy+14), a
        ld      a, (iy+31)
        ld      (iy+15), a
        set     0, (iy+0)
@less:
        dec     b
        jp      nz, @loop
#endasm
    *q = w[0];
    *rem = w[1];
}
#else
void i64_divu(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b)
{
    struct i64 n;
    struct i64 r;
    struct i64 qq;
    int i;

    if (b->hi == 0 && b->lo <= 0xFFFF) {
        i64_set(rem, 0, i64_divsmall(q, a, (unsigned int)b->lo));
        return;
    }
    n = *a;
    i64_set(&r, 0, 0);
    i64_set(&qq, 0, 0);
    for (i = 63; i >= 0; i--) {
        r.hi = M32(r.hi << 1) | r.lo >> 31;
        r.lo = M32(r.lo << 1) | ((i >= 32 ? n.hi >> (i - 32) : n.lo >> i) & 1);
        if (i64_cmp(&r, b, 0) >= 0) {
            i64_sub(&r, &r, b);
            if (i >= 32)
                qq.hi = qq.hi | 1UL << (i - 32);
            else
                qq.lo = qq.lo | 1UL << i;
        }
    }
    *q = qq;
    *rem = r;
}
#endif

/* Truncating: the remainder takes the dividend's sign. The magnitudes are
 * divided and the signs fixed afterwards, so a == q * b + rem. */
void i64_divs(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b)
{
    struct i64 ua;
    struct i64 ub;
    int na;
    int nb;

    na = i64_is_neg(a);
    nb = i64_is_neg(b);
    ua = *a;
    ub = *b;
    if (na)
        i64_neg(&ua, a);
    if (nb)
        i64_neg(&ub, b);
    i64_divu(q, rem, &ua, &ub);
    if (na != nb)
        i64_neg(q, q);
    if (na)
        i64_neg(rem, rem);
}

/* ---- the bitwise operations and negate ---- */

void i64_and(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    r->hi = a->hi & b->hi;
    r->lo = a->lo & b->lo;
}

void i64_or(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    r->hi = a->hi | b->hi;
    r->lo = a->lo | b->lo;
}

void i64_xor(struct i64 *r, const struct i64 *a, const struct i64 *b)
{
    r->hi = a->hi ^ b->hi;
    r->lo = a->lo ^ b->lo;
}

void i64_cpl(struct i64 *r, const struct i64 *a)
{
    r->hi = M32(~a->hi);
    r->lo = M32(~a->lo);
}

/* 0 - a, which is two's complement negation */
void i64_neg(struct i64 *r, const struct i64 *a)
{
    struct i64 z;

    i64_set(&z, 0, 0);
    i64_sub(r, &z, a);
}

/* ---- the shifts ---- */

/* The assembly copies a into w, moves whole bytes up n / 8 times (a zero
 * entering at the bottom), then shifts the 8 bytes left one bit at a time
 * n % 8 times (sla, then rl through the bytes above), and copies w to r.
 * n is taken modulo 64. */
#if I64_ASM
void i64_shl(struct i64 *r, const struct i64 *a, int n)
{
    unsigned char *p;
    unsigned char w[9];

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)
        lea     de, iy+0
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      a, (ix+12)
        and     a, 63
        ld      c, a
        srl     a
        srl     a
        srl     a
        or      a
        jr      z, @bits
        ld      b, a
@bytes:
        ld      hl, (iy+4)
        ld      (iy+5), hl
        ld      hl, (iy+1)
        ld      (iy+2), hl
        ld      a, (iy+0)
        ld      (iy+1), a
        ld      (iy+0), 0
#endasm
#asm
        djnz    @bytes
@bits:
        ld      a, c
        and     a, 7
        jr      z, @out
        ld      b, a
@bit:
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+4)
        rl      (iy+5)
        rl      (iy+6)
        rl      (iy+7)
        djnz    @bit
@out:
        ld      de, (ix+6)
        lea     hl, iy+0
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
#endasm
}
#else
void i64_shl(struct i64 *r, const struct i64 *a, int n)
{
    unsigned long hi;
    unsigned long lo;

    /* n == 0 is separate because a->lo >> (32 - n) would then shift by
     * the full width of a 32-bit long, which C leaves undefined */
    if (n == 0) {
        *r = *a;
        return;
    }
    if (n >= 32) {
        hi = M32(a->lo << (n - 32));
        lo = 0;
    } else {
        hi = M32(a->hi << n) | a->lo >> (32 - n);
        lo = M32(a->lo << n);
    }
    r->hi = hi;
    r->lo = lo;
}
#endif

/* The fill byte w[8] is 0xFF for a signed shift of a negative value, else
 * 0. Whole-byte moves shift the bytes down and bring the fill in at the
 * top; then each single-bit round loads the fill into A and rla puts its
 * top bit in the carry, and rr through the bytes from the top down shifts
 * that bit in: an arithmetic shift when the fill is 0xFF, a logical one
 * when it is 0. n is taken modulo 64. */
#if I64_ASM
void i64_shr(struct i64 *r, const struct i64 *a, int n, int is_signed)
{
    unsigned char *p;
    unsigned char w[9];                 /* the value, then the fill byte */

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)
        lea     de, iy+0
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      (iy+8), 0
        ld      a, (ix+15)
        or      a
        jr      z, @logical
        bit     7, (iy+7)
        jr      z, @logical
        ld      (iy+8), 0xFF
@logical:
        ld      a, (ix+12)
        and     a, 63
        ld      c, a
        srl     a
        srl     a
        srl     a
        or      a
        jr      z, @bits
        ld      b, a
#endasm
#asm
@bytes:
        ld      hl, (iy+1)
        ld      (iy+0), hl
        ld      hl, (iy+4)
        ld      (iy+3), hl
        ld      a, (iy+7)
        ld      (iy+6), a
        ld      a, (iy+8)
        ld      (iy+7), a
        djnz    @bytes
@bits:
        ld      a, c
        and     a, 7
        jr      z, @out
        ld      b, a
@bit:
        ld      a, (iy+8)
        rla
        rr      (iy+7)
        rr      (iy+6)
        rr      (iy+5)
        rr      (iy+4)
        rr      (iy+3)
        rr      (iy+2)
        rr      (iy+1)
#endasm
#asm
        rr      (iy+0)
        djnz    @bit
@out:
        ld      de, (ix+6)
        lea     hl, iy+0
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
#endasm
}
#else
void i64_shr(struct i64 *r, const struct i64 *a, int n, int is_signed)
{
    unsigned long fill;
    unsigned long hi;
    unsigned long lo;

    fill = is_signed && i64_is_neg(a) ? 0xFFFFFFFFUL : 0;
    /* n == 0 and n == 32 are separate for the reason given in i64_shl:
     * they would shift a 32-bit value by 32 */
    if (n == 0) {
        *r = *a;
        return;
    }
    if (n >= 32) {
        lo = n == 32 ? a->hi : (a->hi >> (n - 32)) | M32(fill << (64 - n));
        hi = fill;
    } else {
        lo = a->lo >> n | M32(a->hi << (32 - n));
        hi = a->hi >> n | M32(fill << (32 - n));
    }
    r->hi = hi;
    r->lo = lo;
}
#endif

/* ---- compare ---- */

/* The assembly compares from the top byte down; the first byte that
 * differs decides, by the carry out of cp (a's byte below b's means
 * a < b). For a signed compare the top bytes have their sign bits flipped
 * first (xor 0x80), which turns the signed order into the unsigned one.
 * The answer is stored in the local r at (ix-3). */
#if I64_ASM
int i64_cmp(const struct i64 *a, const struct i64 *b, int is_signed)
{
    int r;

#asm
        ld      iy, (ix+6)
        ld      hl, (ix+9)
        ld      bc, 7
        add     hl, bc
        ld      e, 0
        ld      a, (ix+12)
        or      a
        jr      z, @unsigned
        ld      e, 0x80
@unsigned:
        ld      a, (hl)
        xor     a, e
        ld      d, a
        ld      a, (iy+7)
        xor     a, e
        cp      a, d
        jr      nz, @differ
        dec     hl
        ld      a, (iy+6)
        cp      a, (hl)
        jr      nz, @differ
        dec     hl
        ld      a, (iy+5)
        cp      a, (hl)
#endasm
#asm
        jr      nz, @differ
        dec     hl
        ld      a, (iy+4)
        cp      a, (hl)
        jr      nz, @differ
        dec     hl
        ld      a, (iy+3)
        cp      a, (hl)
        jr      nz, @differ
        dec     hl
        ld      a, (iy+2)
        cp      a, (hl)
        jr      nz, @differ
        dec     hl
        ld      a, (iy+1)
        cp      a, (hl)
        jr      nz, @differ
        dec     hl
        ld      a, (iy+0)
        cp      a, (hl)
        jr      nz, @differ
        ld      hl, 0
        jr      @done
#endasm
#asm
@differ:
        ld      hl, 1
        jr      nc, @done
        ld      hl, -1
@done:
        ld      (ix-3), hl
#endasm
    return r;
}
#else
int i64_cmp(const struct i64 *a, const struct i64 *b, int is_signed)
{
    unsigned long ah;
    unsigned long bh;

    ah = a->hi;
    bh = b->hi;
    if (is_signed) {
        ah = ah ^ 0x80000000UL;         /* the signed order as the unsigned one */
        bh = bh ^ 0x80000000UL;
    }
    if (ah != bh)
        return ah < bh ? -1 : 1;
    if (a->lo != b->lo)
        return a->lo < b->lo ? -1 : 1;
    return 0;
}
#endif

/* ---- decimal text ---- */

int i64_parse(struct i64 *r, const char *s)
{
    int neg;

    i64_set(r, 0, 0);
    neg = *s == '-';
    if (neg)
        s++;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9') {
        i64_muladd(r, 10, (unsigned int)(*s - '0'));
        s++;
    }
    if (*s)
        return 0;
    if (neg)
        i64_neg(r, r);
    return 1;
}

/* Digits come out least significant first, so they are collected and
 * written in reverse. The most negative value negates to itself and
 * prints correctly as its unsigned magnitude. */
void i64_str(char *buf, const struct i64 *a, int is_signed)
{
    char digits[21];
    struct i64 v;
    int n;

    v = *a;
    if (is_signed && i64_is_neg(&v)) {
        *buf = '-';
        buf++;
        i64_neg(&v, &v);
    }
    n = 0;
    do {
        digits[n] = (char)('0' + i64_divsmall(&v, &v, 10));
        n++;
    } while (!i64_is_zero(&v));
    while (n > 0) {
        n--;
        *buf = digits[n];
        buf++;
    }
    *buf = 0;
}
