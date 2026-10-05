/* softfp.c - IEEE 754 arithmetic in integer C; see softfp.h.
 *
 * The structure is Berkeley SoftFloat's (John Hauser), rewritten for
 * 32-bit limbs: a 64-bit integer is a struct u64 of two unsigned longs. A
 * binary64 result is assembled by round_pack from a sign, an exponent and
 * a 63-bit significand whose leading 1 is at bit 62, with 10 bits below
 * the last one kept (bit 0 sticky: the OR of everything shifted out), and
 * rounded to nearest, ties to even. pack adds the significand's leading 1
 * into the exponent field, so the exponent given is one less than the
 * biased exponent, and a carry out of rounding moves up by itself.
 *
 * binary32 arithmetic is done exactly in binary64 and rounded once: for
 * + - * / and sqrt that is correctly rounded, since 53 >= 2 * 24 + 2
 * (Figueroa, "When is double rounding innocuous?", 1995).
 *
 * The three inner loops that take nearly all the time (the 64-bit
 * multiply, the division's 63 steps and the square root's 55) have eZ80
 * assembly beside their C, chosen by FP_ASM: 1 where agonc compiles this
 * file (the library's fp.c, and cc1 itself from stage 2 on), 0 elsewhere
 * (the PC, AgDev's stage 1). The assembly computes exactly the C's
 * integers, so the results are the same bits either way, and a constant
 * folded by any cc1 equals the same expression at run time
 * (tests/libc_c/t_fpeq.c compares the two builds). Measured at the Agon's
 * speed (2026-10-03), the C took about 5 ms a multiply, 17 ms a divide
 * and 27 ms a square root. The 64-bit helpers (add, subtract, compare,
 * test for zero, shifts, leading zeros) have assembly too, and add,
 * subtract and multiply have whole fast paths, add_fast and mul_fast,
 * which do the common case entirely in assembly and return 0 to leave
 * everything else to the C.
 *
 * ---- IEEE 754 in brief ----
 *
 * A binary64 is a sign bit, an 11-bit biased exponent field E and a 52-bit
 * fraction field F. For 0 < E < 0x7FF the value is (-1)^sign * 1.F *
 * 2^(E - 1023), a normal number: its leading 1, the implicit (or hidden)
 * bit, is not stored, and the significand 1.F has 53 bits. E = 0 holds
 * zero (F = 0, with either sign) and the subnormals 0.F * 2^-1022, which
 * fill the gap below the smallest normal evenly (gradual underflow). E =
 * 0x7FF holds the infinities (F = 0) and the NaNs (F != 0; F's top bit
 * set makes a quiet NaN, which propagates through arithmetic without
 * complaint). binary32 is the same with an 8-bit exponent (bias 127) and
 * a 23-bit fraction. Within one sign the bit patterns, read as integers,
 * order like the values, which sf64_cmp relies on.
 *
 * ---- Rounding ----
 *
 * IEEE 754 defines + - * / and sqrt as the exact result, rounded. Round to
 * nearest, ties to even, takes the representable value nearest the exact
 * one, and of two equally near the one whose last bit is 0 (so ties go
 * up and down equally often, where always rounding halves up would bias
 * results upward). To decide
 * it without the exact value's endless bits, a few bits are kept below
 * the result's last bit (its ulp, unit in the last place): the first is
 * the guard bit, worth half an ulp; under it, every bit shifted away is
 * ORed into the lowest, the sticky bit (SoftFloat calls this "jamming"),
 * so that it is 1 if anything nonzero was lost. Together they say whether
 * the exact value is below, exactly at or above the halfway point, which
 * is all rounding needs. round_pack keeps 10 such bits for binary64,
 * round_pack32 7 for binary32, the fast paths 3 (guard, round, sticky).
 *
 * Why the fast paths can give the C's exact bits: a correctly rounded
 * result is unique, so two correct implementations agree however they get
 * there. add_fast and mul_fast take only operands that are both normal and
 * give up (return 0, r untouched) on any result that is zero, subnormal or
 * overflows, where the special rules live. Within that, each rounds the
 * exact result correctly, so its bits are the C's (t_fpeq checks it).
 *
 * ---- Inline assembly ----
 *
 * An #asm block is copied into the function's code (abi.md 10). It finds
 * the parameters at (ix+6), (ix+9), ... and the locals at (ix-3), (ix-6),
 * ... in declaration order (abi.md 5). Structs come by pointer: a struct
 * u64 keeps hi first, so its bytes from low to high are at offsets 4..7
 * then 0..3, while a struct sf64's 8 bytes are the double's own, low
 * first. A kernel that needs scratch memory declares a
 * byte array w and a pointer local p = w, and loads p into IY; a result
 * int is stored into a local (r or ok) that the C then returns. cc1 takes
 * at most 598 bytes per #asm block, so a kernel is several blocks in a
 * row; its @labels are local to the function, so jumps cross from block
 * to block.
 *
 * ---- Map ----
 *
 * 64-bit integers (struct u64 and its helpers, mul64_jam); binary64
 * pieces (pack, round_pack); + and - (add_mags, sub_mags, add_fast);
 * * / and sqrt (mul_fast, div63, sqrt55); comparison; conversions to and
 * from integers; binary32 (through binary64); decimal conversion (big
 * integers, for lex.c's constants and printf and scanf).
 */

/* FP_ASM: the eZ80 kernels (1) or their C (0); define it to choose. */
#ifndef FP_ASM
#ifdef __AGONC__
#define FP_ASM 1
#else
#define FP_ASM 0
#endif
#endif

#include <limits.h>
#include "softfp.h"

/* Keeps a value to 32 bits where long is wider (a 64-bit PC); nothing to
 * do, and so no code, where long is 32 bits. */
#if ULONG_MAX == 0xFFFFFFFFUL
#define M32(x) (x)
#else
#define M32(x) ((x) & 0xFFFFFFFFUL)
#endif

/* A 64-bit unsigned integer as two 32-bit limbs (each kept to 32 bits by
 * M32), hi first; the order is the opposite of struct sf64's, and the
 * assembly depends on it. */
struct u64 {
    unsigned long hi;
    unsigned long lo;
};

/* The default quiet NaN's high word (sign 0, exponent all ones, the quiet
 * bit 51); its low word is 0. */
#define DEFAULT_NAN_HI 0x7FF80000UL

/* ---- 64-bit integers ------------------------------------------------------------ */

/* With FP_ASM each has eZ80 assembly beside its C: a struct u64 keeps hi
 * first, so a value's bytes run 4..7 then 0..3, low first. The shifts
 * work on a copy, low byte first, a byte at a time and then up to seven
 * bits. In the assembly the parameters are in order from (ix+6): r, when
 * there is one, at (ix+6), a at (ix+9), and b or n at (ix+12); the
 * operations read each byte before writing it (or work on a copy), so r
 * may be a or b. */

static void u64_set(struct u64 *r, unsigned long hi, unsigned long lo)
{
    r->hi = hi;
    r->lo = lo;
}

/* r = a + b, modulo 2^64. The assembly: IY = a, HL = b, DE = r; the lo
 * words' bytes (offsets 4..7) then the hi words' (0..3), the carry passed
 * on by ADC. The C finds the carry out of the low word by the wrapped-sum
 * test: an unsigned sum wrapped exactly when it is less than an addend. */
#if FP_ASM
static void u64_add(struct u64 *r, const struct u64 *a, const struct u64 *b)
{
#asm
        ld      iy, (ix+9)
        ld      hl, (ix+12)
        ld      de, (ix+6)
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        inc     de
        inc     de
        inc     de
        inc     de
        ld      a, (iy+4)
        add     a, (hl)
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
#endasm
#asm
        inc     de
        ld      a, (iy+7)
        adc     a, (hl)
        ld      (de), a
        ld      hl, (ix+12)
        ld      de, (ix+6)
        ld      a, (iy+0)
        adc     a, (hl)
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
#endasm
}
#else
static void u64_add(struct u64 *r, const struct u64 *a, const struct u64 *b)
{
    unsigned long lo;

    lo = M32(a->lo + b->lo);
    r->hi = M32(a->hi + b->hi + (lo < a->lo));
    r->lo = lo;
}
#endif

/* r = a - b, modulo 2^64: as u64_add, with SBC and a borrow out of the
 * low word when a->lo < b->lo. */
#if FP_ASM
static void u64_sub(struct u64 *r, const struct u64 *a, const struct u64 *b)
{
#asm
        ld      iy, (ix+9)
        ld      hl, (ix+12)
        ld      de, (ix+6)
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        inc     de
        inc     de
        inc     de
        inc     de
        ld      a, (iy+4)
        sub     a, (hl)
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
#endasm
#asm
        inc     de
        ld      a, (iy+7)
        sbc     a, (hl)
        ld      (de), a
        ld      hl, (ix+12)
        ld      de, (ix+6)
        ld      a, (iy+0)
        sbc     a, (hl)
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
#endasm
}
#else
static void u64_sub(struct u64 *r, const struct u64 *a, const struct u64 *b)
{
    unsigned long lo;

    lo = M32(a->lo - b->lo);
    r->hi = M32(a->hi - b->hi - (a->lo < b->lo));
    r->lo = lo;
}
#endif

/* 1 if a < b, unsigned. The assembly subtracts b from a for the flags
 * only, low byte first; the final borrow (carry) is the answer, which
 * goes into the local r at (ix-3). */
#if FP_ASM
static int u64_lt(const struct u64 *a, const struct u64 *b)
{
    int r;

#asm
        ld      iy, (ix+6)
        ld      hl, (ix+9)
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        ld      a, (iy+4)
        sub     a, (hl)
        inc     hl
        ld      a, (iy+5)
        sbc     a, (hl)
        inc     hl
        ld      a, (iy+6)
        sbc     a, (hl)
        inc     hl
        ld      a, (iy+7)
        sbc     a, (hl)
        ld      hl, (ix+9)
        ld      a, (iy+0)
        sbc     a, (hl)
        inc     hl
        ld      a, (iy+1)
        sbc     a, (hl)
        inc     hl
#endasm
#asm
        ld      a, (iy+2)
        sbc     a, (hl)
        inc     hl
        ld      a, (iy+3)
        sbc     a, (hl)
        ld      hl, 0
        jr      nc, @ge
        inc     hl
@ge:
        ld      (ix-3), hl
#endasm
    return r;
}
#else
static int u64_lt(const struct u64 *a, const struct u64 *b)
{
    return a->hi < b->hi || (a->hi == b->hi && a->lo < b->lo);
}
#endif

/* 1 if a is 0; the assembly ORs its eight bytes together. */
#if FP_ASM
static int u64_zero(const struct u64 *a)
{
    int r;

#asm
        ld      iy, (ix+6)
        ld      a, (iy+0)
        or      a, (iy+1)
        or      a, (iy+2)
        or      a, (iy+3)
        or      a, (iy+4)
        or      a, (iy+5)
        or      a, (iy+6)
        or      a, (iy+7)
        ld      hl, 0
        jr      nz, @nz
        inc     hl
@nz:
        ld      (ix-3), hl
#endasm
    return r;
}
#else
static int u64_zero(const struct u64 *a)
{
    return a->hi == 0 && a->lo == 0;
}
#endif

/* r = a << n, 0 <= n <= 63. The assembly copies a into w low byte first,
 * moves it up a whole byte n / 8 times (three-byte loads, the top first
 * so nothing is overwritten before it is read), shifts it left a bit
 * n % 8 times (SLA then RL, the carry linking the bytes) and copies it
 * back to r; w[8] is cleared but not used. The C takes n == 0 apart
 * because a->lo >> (32 - n) would shift by 32, which C leaves undefined
 * for a 32-bit long. */
#if FP_ASM
static void u64_shl(struct u64 *r, const struct u64 *a, int n)
{
    unsigned char *p;
    unsigned char w[9];         /* the value, low byte first */

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)
        ld      bc, 4
        add     hl, bc
        lea     de, iy+0
        ldi
        ldi
        ldi
        ldi
        ld      hl, (ix+9)
        ldi
        ldi
        ldi
        ldi
        ld      (iy+8), 0
        ld      a, (ix+12)
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
#endasm
#asm
        ld      a, (iy+0)
        ld      (iy+1), a
        ld      (iy+0), 0
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
        ld      hl, (ix+6)
        ld      bc, 4
        add     hl, bc
        ex      de, hl
        lea     hl, iy+0
        ldi
        ldi
#endasm
#asm
        ldi
        ldi
        ld      de, (ix+6)
        lea     hl, iy+4
        ldi
        ldi
        ldi
        ldi
#endasm
}
#else
static void u64_shl(struct u64 *r, const struct u64 *a, int n)
{
    unsigned long hi;
    unsigned long lo;

    if (n == 0) {
        hi = a->hi;
        lo = a->lo;
    } else if (n >= 32) {
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

/* r = a >> n, 0 <= n <= 63: u64_shl's mirror image (SRL then RR, from
 * the top byte down). */
#if FP_ASM
static void u64_shr(struct u64 *r, const struct u64 *a, int n)
{
    unsigned char *p;
    unsigned char w[9];         /* the value, low byte first */

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)
        ld      bc, 4
        add     hl, bc
        lea     de, iy+0
        ldi
        ldi
        ldi
        ldi
        ld      hl, (ix+9)
        ldi
        ldi
        ldi
        ldi
        ld      (iy+8), 0
        ld      a, (ix+12)
        ld      c, a
        srl     a
        srl     a
        srl     a
        or      a
        jr      z, @bits
        ld      b, a
@bytes:
        ld      hl, (iy+1)
        ld      (iy+0), hl
        ld      hl, (iy+4)
        ld      (iy+3), hl
#endasm
#asm
        ld      a, (iy+7)
        ld      (iy+6), a
        ld      (iy+7), 0
        djnz    @bytes
@bits:
        ld      a, c
        and     a, 7
        jr      z, @out
        ld      b, a
@bit:
        srl     (iy+7)
        rr      (iy+6)
        rr      (iy+5)
        rr      (iy+4)
        rr      (iy+3)
        rr      (iy+2)
        rr      (iy+1)
        rr      (iy+0)
        djnz    @bit
@out:
        ld      hl, (ix+6)
        ld      bc, 4
        add     hl, bc
        ex      de, hl
        lea     hl, iy+0
        ldi
        ldi
#endasm
#asm
        ldi
        ldi
        ld      de, (ix+6)
        lea     hl, iy+4
        ldi
        ldi
        ldi
        ldi
#endasm
}
#else
static void u64_shr(struct u64 *r, const struct u64 *a, int n)
{
    unsigned long hi;
    unsigned long lo;

    if (n == 0) {
        hi = a->hi;
        lo = a->lo;
    } else if (n >= 32) {
        lo = a->hi >> (n - 32);
        hi = 0;
    } else {
        lo = a->lo >> n | M32(a->hi << (32 - n));
        hi = a->hi >> n;
    }
    r->hi = hi;
    r->lo = lo;
}
#endif

/* Shifts right by n >= 0, ORing every bit shifted out into bit 0.
 *
 * That bit 0 is the sticky bit (see the top): the result is exact when it
 * is 0, and when it is 1 the value lost lay strictly between 0 and one
 * unit of the new bit 0, which is all rounding needs to know. n of 64 or
 * more leaves 0 or 1. The assembly clamps n to 64 (a full 24-bit
 * compare), ORs each whole byte shifted out into w[8], sets w[8] when a
 * single-bit shift carries out a 1, and at the end sets bit 0 if w[8] is
 * not 0. The C takes n == 32 apart to avoid a shift by 32. */
#if FP_ASM
static void u64_shr_jam(struct u64 *r, const struct u64 *a, int n)
{
    unsigned char *p;
    unsigned char w[9];         /* the value low byte first, the lost bits */

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)
        ld      bc, 4
        add     hl, bc
        lea     de, iy+0
        ldi
        ldi
        ldi
        ldi
        ld      hl, (ix+9)
        ldi
        ldi
        ldi
        ldi
        ld      (iy+8), 0
        ld      hl, (ix+12)
        ld      de, 64
        or      a
        sbc     hl, de
        ld      a, 64
        jr      nc, @big
        ld      a, (ix+12)
@big:
        ld      c, a
        srl     a
        srl     a
        srl     a
        or      a
#endasm
#asm
        jr      z, @bits
        ld      b, a
@bytes:
        ld      a, (iy+0)
        or      a, (iy+8)
        ld      (iy+8), a
        ld      hl, (iy+1)
        ld      (iy+0), hl
        ld      hl, (iy+4)
        ld      (iy+3), hl
        ld      a, (iy+7)
        ld      (iy+6), a
        ld      (iy+7), 0
        djnz    @bytes
@bits:
        ld      a, c
        and     a, 7
        jr      z, @out
        ld      b, a
@bit:
        srl     (iy+7)
        rr      (iy+6)
        rr      (iy+5)
        rr      (iy+4)
        rr      (iy+3)
#endasm
#asm
        rr      (iy+2)
        rr      (iy+1)
        rr      (iy+0)
        jr      nc, @kept
        ld      (iy+8), 1
@kept:
        djnz    @bit
@out:
        ld      a, (iy+8)
        or      a
        jr      z, @exact
        set     0, (iy+0)
@exact:
        ld      hl, (ix+6)
        ld      bc, 4
        add     hl, bc
        ex      de, hl
        lea     hl, iy+0
        ldi
        ldi
        ldi
        ldi
        ld      de, (ix+6)
        lea     hl, iy+4
        ldi
        ldi
        ldi
        ldi
#endasm
}
#else
static void u64_shr_jam(struct u64 *r, const struct u64 *a, int n)
{
    unsigned long hi;
    unsigned long lo;
    unsigned long lost;

    if (n == 0) {
        hi = a->hi;
        lo = a->lo;
        lost = 0;
    } else if (n < 32) {
        lost = M32(a->lo << (32 - n));
        lo = a->lo >> n | M32(a->hi << (32 - n));
        hi = a->hi >> n;
    } else if (n < 64) {
        lost = a->lo | (n == 32 ? 0 : M32(a->hi << (64 - n)));
        lo = n == 32 ? a->hi : a->hi >> (n - 32);
        hi = 0;
    } else {
        lost = a->hi | a->lo;
        lo = 0;
        hi = 0;
    }
    r->hi = hi;
    r->lo = lo | (lost != 0);
}
#endif

/* The number of leading zero bits of a 32-bit value; 32 for 0. */
static int clz32(unsigned long a)
{
    int n;

    if (a == 0)
        return 32;
    n = 0;
    while (!(a & 0x80000000UL)) {
        a = M32(a << 1);
        n++;
    }
    return n;
}

/* The number of leading zero bits of a; 64 for 0. The assembly counts 8
 * for each zero byte from the top (hi's bytes 3..0, then lo's 7..4),
 * then doubles the first nonzero byte until its bit 7 is set, counting
 * one each time; register C holds the count. */
#if FP_ASM
static int u64_clz(const struct u64 *a)
{
    int r;

#asm
        ld      iy, (ix+6)
        ld      c, 0
        ld      a, (iy+3)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
        ld      c, a
        ld      a, (iy+2)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
        ld      c, a
        ld      a, (iy+1)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
        ld      c, a
        ld      a, (iy+0)
        or      a
        jr      nz, @found
        ld      a, c
#endasm
#asm
        add     a, 8
        ld      c, a
        ld      a, (iy+7)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
        ld      c, a
        ld      a, (iy+6)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
        ld      c, a
        ld      a, (iy+5)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
        ld      c, a
        ld      a, (iy+4)
        or      a
        jr      nz, @found
        ld      a, c
        add     a, 8
#endasm
#asm
        ld      c, a
        jr      @done
@found:
        bit     7, a
        jr      nz, @done
        add     a, a
        inc     c
        jr      @found
@done:
        ld      hl, 0
        ld      l, c
        ld      (ix-3), hl
#endasm
    return r;
}
#else
static int u64_clz(const struct u64 *a)
{
    return a->hi != 0 ? clz32(a->hi) : 32 + clz32(a->lo);
}
#endif

#if !FP_ASM
/* a * b, both below 2^32, as 64 bits.
 *
 * Schoolbook multiplication on 16-bit halves: a * b = a1 * b1 * 2^32 +
 * (a0 * b1 + a1 * b0) * 2^16 + a0 * b0, each partial product below 2^32.
 * The middle sum can wrap: its carry is worth 2^48, 0x10000 in the high
 * word; the low word's carry comes from the wrapped-sum test. Only the C
 * build needs it: mul64_jam's assembly multiplies by bytes. */
static void mul32(struct u64 *r, unsigned long a, unsigned long b)
{
    unsigned long a0;
    unsigned long a1;
    unsigned long b0;
    unsigned long b1;
    unsigned long mid;
    unsigned long carry;
    unsigned long lo;

    a0 = a & 0xFFFF;
    a1 = a >> 16;
    b0 = b & 0xFFFF;
    b1 = b >> 16;
    mid = M32(a0 * b1 + a1 * b0);
    carry = mid < a0 * b1 ? 0x10000UL : 0;
    lo = M32(a0 * b0 + M32(mid << 16));
    r->hi = M32(a1 * b1 + (mid >> 16) + carry + (lo < M32(mid << 16)));
    r->lo = lo;
}
#endif

/* The high 64 bits of a * b, with bit 0 set if the low 64 are not all 0.
 *
 * That is, the 128-bit product with its low half jammed into a sticky bit:
 * the multiply sf64_mul needs, whose operands carry their significands at
 * the top. */
#if FP_ASM
/* By bytes: the operands into w as little-endian byte strings (a struct
 * u64 keeps hi first, so a value's bytes run 4..7 then 0..3), sixty-four
 * 8 x 8 products from the eZ80's MLT added into the 16-byte product with
 * their carries, then the top 8 bytes back as hi and lo with the jam.
 *
 * MLT DE multiplies D by E into DE, 8 x 8 to 16 bits, the eZ80's only
 * multiply. The outer loop takes a's byte i (C counts 0..7; a zero byte
 * is skipped, adding nothing); @step adds a[i] * b[j] into product bytes
 * i + j and i + j + 1 and carries on up with INC until a byte does not
 * wrap to 0. This is long multiplication in base 256. */
static void mul64_jam(struct u64 *r, const struct u64 *a, const struct u64 *b)
{
    unsigned char *p;
    unsigned char w[33];                /* a 0-7, b 8-15, product 16-31, a byte 32 */

    p = w;
#asm
        ld      iy, (ix-3)
        ld      hl, (ix+9)              ; a: bytes 4..7 then 0..3, low first
        lea     de, iy+0
        call    @in8
        ld      hl, (ix+12)             ; b
        lea     de, iy+8
        call    @in8
        ld      hl, 0
        ld      (iy+16), hl
        ld      (iy+19), hl
        ld      (iy+22), hl
        ld      (iy+25), hl
        ld      (iy+28), hl
        ld      (iy+31), l
        ld      c, 0                    ; i: a's byte
@outer:
        lea     hl, iy+0
        ld      de, 0
        ld      e, c
#endasm
#asm
        add     hl, de                  ; &a[i]
        ld      a, (hl)
        or      a
        jp      z, @nexti
        ld      (iy+32), a
        ld      de, 16
        add     hl, de                  ; &product[i]; then &product[i+j]
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
        cp      8
        jp      nz, @outer
        ld      de, (ix+6)              ; r: hi = product bytes 12..15, lo = 8..11
        lea     hl, iy+28
        ldi
        ldi
        ldi
        ldi
        lea     hl, iy+24
        ldi
        ldi
        ldi
        ldi
        ld      a, (iy+16)              ; the jam: any of the low 8 bytes
        or      a, (iy+17)
        or      a, (iy+18)
        or      a, (iy+19)
#endasm
#asm
        or      a, (iy+20)
        or      a, (iy+21)
        or      a, (iy+22)
        or      a, (iy+23)
        jr      z, @done
        ld      hl, (ix+6)
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        set     0, (hl)                 ; lo's low byte
        jr      @done
; product[i+j], product[i+j+1] += a[i] * e, the carry on up; hl to i+j+1
@step:
        ld      d, (iy+32)
        mlt     de
        ld      a, (hl)
        add     a, e
        ld      (hl), a
        inc     hl
        ld      a, (hl)
#endasm
#asm
        adc     a, d
        ld      (hl), a
        ret     nc
        push    hl
@carry:
        inc     hl
        inc     (hl)
        jr      z, @carry
        pop     hl
        ret
; 8 bytes from hl (a struct u64) to de, low byte first
@in8:
        push    hl
        ld      bc, 4
        add     hl, bc
        ldi
        ldi
        ldi
        ldi
        pop     hl
        ldi
        ldi
        ldi
        ldi
        ret
@done:
#endasm
}
#else
static void mul64_jam(struct u64 *r, const struct u64 *a, const struct u64 *b)
{
    struct u64 ll;
    struct u64 lh;
    struct u64 hl;
    struct u64 hh;
    unsigned long w1;
    unsigned long w2;
    unsigned long w3;
    unsigned long c2;
    unsigned long c3;

    mul32(&ll, a->lo, b->lo);
    mul32(&lh, a->lo, b->hi);
    mul32(&hl, a->hi, b->lo);
    mul32(&hh, a->hi, b->hi);
    /* the product's words 0..3: w0 = ll.lo, then the sums with carries
     * (c2 into word 2, c3 into word 3, each counted by the wrapped-sum
     * test); w0 and w1 only feed the sticky bit */
    w1 = M32(ll.hi + lh.lo);
    c2 = w1 < ll.hi;
    w1 = M32(w1 + hl.lo);
    c2 = c2 + (w1 < hl.lo);
    w2 = M32(lh.hi + hl.hi);
    c3 = w2 < lh.hi;
    w2 = M32(w2 + hh.lo);
    c3 = c3 + (w2 < hh.lo);
    w2 = M32(w2 + c2);
    c3 = c3 + (w2 < c2);
    w3 = M32(hh.hi + c3);
    r->hi = w3;
    r->lo = w2 | (ll.lo != 0 || w1 != 0);
}
#endif

/* ---- binary64: pieces ----------------------------------------------------------- */

/* The sign bit and the biased exponent field of a struct sf64. */
#define SIGN64(x) ((int)((x)->hi >> 31))
#define EXP64(x) ((int)((x)->hi >> 20 & 0x7FF))

/* The 52-bit fraction field, without the implicit 1. */
static void frac64(struct u64 *r, const struct sf64 *x)
{
    r->hi = x->hi & 0xFFFFFUL;
    r->lo = x->lo;
}

/* Exponent all ones and fraction not 0. */
static int is_nan64(const struct sf64 *x)
{
    return EXP64(x) == 0x7FF && ((x->hi & 0xFFFFFUL) != 0 || x->lo != 0);
}

/* (sign << 63) + (exp << 52) + sig: the significand's leading 1, if at bit
 * 52, adds one to the exponent.
 *
 * Adding rather than ORing is the point: the callers pass one less than
 * the biased exponent and let the leading 1 make it up; a significand
 * that rounding carried up to 2^53 adds one more; a subnormal (exp 0,
 * leading 1 below bit 52) gets exponent field 0, and one that rounding
 * carried up to 2^52 becomes the smallest normal number. No case needs
 * code of its own. */
static void pack(struct sf64 *r, int sign, int exp, const struct u64 *sig)
{
    r->hi = M32(((unsigned long)sign << 31) + ((unsigned long)exp << 20) + sig->hi);
    r->lo = sig->lo;
}

/* A value with an empty fraction: a signed zero (exp 0) or infinity (exp
 * 0x7FF). */
static void pack_special(struct sf64 *r, int sign, int exp)
{
    r->hi = ((unsigned long)sign << 31) | (unsigned long)exp << 20;
    r->lo = 0;
}

static void default_nan(struct sf64 *r)
{
    r->hi = DEFAULT_NAN_HI;
    r->lo = 0;
}

/* The first NaN of a and b (b may be NULL), made quiet.
 *
 * Quiet: the fraction's top bit (bit 51) set; the payload is kept. */
static void propagate_nan(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    const struct sf64 *n;

    n = is_nan64(a) || b == 0 ? a : b;
    r->hi = n->hi | 0x80000UL;
    r->lo = n->lo;
}

/* A subnormal's significand (not 0) moved up to bit 52; its exponent.
 *
 * The exponent is the biased one it would have as a normal number, 1 -
 * shift, so 0 or below: a subnormal's value is its fraction times
 * 2^(1 - 1075), the scale of biased exponent 1. The caller then treats
 * it as a normal operand with its leading 1 at bit 52. */
static int norm_subnormal(struct u64 *sig)
{
    int shift;

    shift = u64_clz(sig) - 11;
    u64_shl(sig, sig, shift);
    return 1 - shift;
}

/* See the top: sig's leading 1 at bit 62 (or below, if exp < 0 makes the
 * result subnormal), exp one less than the biased exponent.
 *
 * SoftFloat's roundPackToF64. Bits 10..62 become the 53-bit significand;
 * bits 0..9 are the round bits, bit 9 the guard (half an ulp), bit 0 the
 * sticky. Rounding adds half an ulp (0x200) and drops the round bits,
 * which rounds every half up; an exact half (round bits 0x200) is then
 * made even by clearing the last bit: if the kept part was even, the
 * added half carried into its last bit and clearing undoes it; if odd,
 * the carry already made it even. sig is modified.
 *
 * A result below the normal range (exp < 0) is shifted right to the
 * subnormal scale, its lost bits jammed, and then rounded: one rounding
 * of the exact value, as IEEE 754 asks. At exp 0x7FD the result is at
 * the top of the range: beyond it, or there when rounding carries into
 * bit 63 (making the biased exponent 0x7FF), it overflows to infinity. A
 * result that rounds to 0 packs as a signed zero. */
static void round_pack(struct sf64 *r, int sign, int exp, struct u64 *sig)
{
    struct u64 inc;
    struct u64 t;
    unsigned long round_bits;

    u64_set(&inc, 0, 0x200);
    if (exp < 0) {
        u64_shr_jam(sig, sig, -exp);
        exp = 0;
    } else if (exp >= 0x7FD) {
        u64_add(&t, sig, &inc);
        if (exp > 0x7FD || (t.hi & 0x80000000UL)) {
            pack_special(r, sign, 0x7FF);           /* overflow: infinity */
            return;
        }
    }
    round_bits = sig->lo & 0x3FF;
    u64_add(sig, sig, &inc);
    u64_shr(&t, sig, 10);
    if (round_bits == 0x200)
        t.lo = t.lo & ~(unsigned long)1;            /* a tie: to even */
    if (u64_zero(&t))
        exp = 0;
    pack(r, sign, exp, &t);
}

/* sig's leading 1 anywhere: normalised to bit 62, or packed exactly when
 * it needs no rounding.
 *
 * For results that may have fewer significant bits than round_pack
 * assumes (a difference after cancellation, an integer). exp is the
 * exponent for a leading 1 at bit 62, as for round_pack; shift moves the
 * leading 1 there. When shift >= 10 nothing lies below the 53 bits kept,
 * so in the normal range the value is packed as it is, its leading 1 at
 * bit 52. */
static void norm_round_pack(struct sf64 *r, int sign, int exp, struct u64 *sig)
{
    int shift;

    shift = u64_clz(sig) - 1;
    exp = exp - shift;
    if (shift >= 10 && exp >= 0 && exp < 0x7FD) {
        u64_shl(sig, sig, shift - 10);
        pack(r, sign, u64_zero(sig) ? 0 : exp, sig);
    } else {
        u64_shl(sig, sig, shift);
        round_pack(r, sign, exp, sig);
    }
}

/* ---- binary64: + - ---------------------------------------------------------------- */

/* |a| + |b| with the given sign: sf64_add with signs alike, sf64_sub with
 * signs unlike. SoftFloat's addMagsF64.
 *
 * Equal exponents need no alignment: the two significands' sum is exact
 * in 54 bits (two implicit 1s make 2^53), so it is moved up to bit 62
 * and the exponent kept, since a sum in [2, 4) has the biased exponent
 * one more than the operands'. Two subnormals add exactly, a carry into
 * bit 52 making the smallest normal through pack.
 *
 * Otherwise the fractions go up 9 bits (implicit 1 at bit 61, leaving
 * room for the sum's carry), the smaller operand is aligned to the
 * larger's exponent with its lost bits jammed, and a sum below 2^62 is
 * moved up a bit with the exponent less one. A subnormal has no implicit
 * 1 and the same scale as exponent 1, not 0; doubling its fraction makes
 * up for the difference of one in diff. Exponent 0x7FF marks an infinity
 * or a NaN. */
static void add_mags(struct sf64 *r, const struct sf64 *a, const struct sf64 *b, int sign)
{
    int exp_a;
    int exp_b;
    int exp_z;
    int diff;
    struct u64 sig_a;
    struct u64 sig_b;
    struct u64 sig_z;
    struct u64 k;

    exp_a = EXP64(a);
    exp_b = EXP64(b);
    frac64(&sig_a, a);
    frac64(&sig_b, b);
    diff = exp_a - exp_b;
    if (diff == 0) {
        if (exp_a == 0) {                           /* two subnormals: exact */
            u64_add(&sig_z, &sig_a, &sig_b);
            pack(r, sign, 0, &sig_z);
            return;
        }
        if (exp_a == 0x7FF) {
            if (!u64_zero(&sig_a) || !u64_zero(&sig_b))
                propagate_nan(r, a, b);
            else
                *r = *a;
            return;
        }
        exp_z = exp_a;
        u64_set(&k, 0x200000UL, 0);                 /* two implicit 1s: 2^53 */
        u64_add(&sig_z, &sig_a, &sig_b);
        u64_add(&sig_z, &sig_z, &k);
        u64_shl(&sig_z, &sig_z, 9);
    } else {
        u64_shl(&sig_a, &sig_a, 9);
        u64_shl(&sig_b, &sig_b, 9);
        u64_set(&k, 0x20000000UL, 0);               /* the implicit 1 at bit 61 */
        if (diff < 0) {
            if (exp_b == 0x7FF) {
                if (!u64_zero(&sig_b))
                    propagate_nan(r, a, b);
                else
                    pack_special(r, sign, 0x7FF);
                return;
            }
            exp_z = exp_b;
            if (exp_a)
                u64_add(&sig_a, &sig_a, &k);
            else
                u64_shl(&sig_a, &sig_a, 1);
            u64_shr_jam(&sig_a, &sig_a, -diff);
        } else {
            if (exp_a == 0x7FF) {
                if (!u64_zero(&sig_a))
                    propagate_nan(r, a, b);
                else
                    *r = *a;
                return;
            }
            exp_z = exp_a;
            if (exp_b)
                u64_add(&sig_b, &sig_b, &k);
            else
                u64_shl(&sig_b, &sig_b, 1);
            u64_shr_jam(&sig_b, &sig_b, diff);
        }
        u64_add(&sig_z, &sig_a, &sig_b);
        u64_add(&sig_z, &sig_z, &k);
        if (!(sig_z.hi & 0x40000000UL)) {
            exp_z--;
            u64_shl(&sig_z, &sig_z, 1);
        }
    }
    round_pack(r, sign, exp_z, &sig_z);
}

/* |a| - |b| with the given sign, which flips when |b| is the larger:
 * sf64_add with signs unlike, sf64_sub with signs alike. SoftFloat's
 * subMagsF64.
 *
 * Equal exponents: the implicit 1s cancel, nothing is shifted out to
 * align, and the difference is exact however many leading bits cancel,
 * so it is normalised to bit 52 and packed without rounding; the shift
 * stops at the subnormal range (gradual underflow). An exact zero is +0,
 * IEEE 754's x - x when rounding to nearest. exp_a less one is the
 * exponent for pack, except for subnormals, whose scale is exponent 1.
 *
 * Otherwise the fractions go up 10 bits (implicit 1 at bit 62), the
 * smaller is aligned with a sticky bit, and the difference, which may
 * have lost leading bits, goes to norm_round_pack. */
static void sub_mags(struct sf64 *r, const struct sf64 *a, const struct sf64 *b, int sign)
{
    int exp_a;
    int exp_b;
    int exp_z;
    int diff;
    int shift;
    struct u64 sig_a;
    struct u64 sig_b;
    struct u64 sig_z;
    struct u64 k;

    exp_a = EXP64(a);
    exp_b = EXP64(b);
    frac64(&sig_a, a);
    frac64(&sig_b, b);
    diff = exp_a - exp_b;
    if (diff == 0) {
        if (exp_a == 0x7FF) {
            if (!u64_zero(&sig_a) || !u64_zero(&sig_b))
                propagate_nan(r, a, b);
            else
                default_nan(r);                     /* inf - inf */
            return;
        }
        if (sig_a.hi == sig_b.hi && sig_a.lo == sig_b.lo) {
            pack_special(r, 0, 0);                  /* x - x is +0 */
            return;
        }
        if (exp_a)
            exp_a--;
        if (u64_lt(&sig_a, &sig_b)) {
            sign = !sign;
            u64_sub(&sig_z, &sig_b, &sig_a);
        } else {
            u64_sub(&sig_z, &sig_a, &sig_b);
        }
        shift = u64_clz(&sig_z) - 11;
        exp_z = exp_a - shift;
        if (exp_z < 0) {
            shift = exp_a;
            exp_z = 0;
        }
        u64_shl(&sig_z, &sig_z, shift);
        pack(r, sign, exp_z, &sig_z);
        return;
    }
    u64_shl(&sig_a, &sig_a, 10);
    u64_shl(&sig_b, &sig_b, 10);
    u64_set(&k, 0x40000000UL, 0);                   /* the implicit 1 at bit 62 */
    if (diff < 0) {
        sign = !sign;
        if (exp_b == 0x7FF) {
            if (!u64_zero(&sig_b))
                propagate_nan(r, a, b);
            else
                pack_special(r, sign, 0x7FF);
            return;
        }
        if (exp_a)
            u64_add(&sig_a, &sig_a, &k);
        else
            u64_shl(&sig_a, &sig_a, 1);
        u64_shr_jam(&sig_a, &sig_a, -diff);
        sig_b.hi = sig_b.hi | 0x40000000UL;
        exp_z = exp_b;
        u64_sub(&sig_z, &sig_b, &sig_a);
    } else {
        if (exp_a == 0x7FF) {
            if (!u64_zero(&sig_a))
                propagate_nan(r, a, b);
            else
                *r = *a;
            return;
        }
        if (exp_b)
            u64_add(&sig_b, &sig_b, &k);
        else
            u64_shl(&sig_b, &sig_b, 1);
        u64_shr_jam(&sig_b, &sig_b, diff);
        sig_a.hi = sig_a.hi | 0x40000000UL;
        exp_z = exp_a;
        u64_sub(&sig_z, &sig_a, &sig_b);
    }
    norm_round_pack(r, sign, exp_z - 1, &sig_z);
}

/* sf64_add's and sf64_sub's fast path (negate: 1 to subtract): two
 * normal operands whose sum is normal and not zero, done whole in
 * assembly as the IEEE adder (three guard bits, the smaller shifted with
 * its lost bits jammed into the lowest, add or subtract, normalise,
 * round to nearest even, pack); 1 if it did it, 0 (r untouched) for the
 * C. The IEEE result is the C's too (t_fpeq).
 *
 * Three bits below the result (guard, round, sticky) are the classic
 * minimum for a correctly rounded adder. When the exponents differ by 2
 * or more, the difference of the significands loses at most one leading
 * bit, so normalising shifts left at most once and the guard bits still
 * hold what rounding needs; when they differ by 0 or 1, nothing is
 * shifted past the guard bits, the result is exact, and normalising may
 * shift left any number of times.
 *
 * negate becomes 0x80 or 0 so that the assembly can XOR it into b's sign
 * byte. ok is at (ix-3), p at (ix-6). The workspace w, by byte offset
 * (each value low byte first):
 *   0..7    a's eight bytes, as in memory
 *   8..15   b's
 *   16..23  X, the larger magnitude's significand: implicit 1 at bit 55,
 *           three guard bits below the 53; it becomes the result's
 *   24..31  Y, the smaller's, the same way; aligned to X
 *   32..39  scratch for the swap
 *   40..42  X's biased exponent (24 bits), adjusted to the result's
 *   43..45  Y's biased exponent
 *   46      X's sign (0x80 or 0), the result's sign
 *   47      Y's sign, b's flipped for a subtraction
 *   48      the bits shifted out of Y, ORed together
 *
 * The steps:
 *   1. Copy a and b; take each exponent from bits 4..7 of byte 6 and 0..6
 *      of byte 7 (a 16-bit shift right by 4). Exponent 0 (zero,
 *      subnormal) or 0x7FF (infinity, NaN) goes to the C.
 *   2. Unpack each significand: the 52 fraction bits, the implicit 1
 *      (0x10 in byte 6), shifted left 3.
 *   3. Order by magnitude, exponent first, then significand from the top
 *      byte; if a's is the smaller, swap significands, exponents and
 *      signs, so that X >= Y.
 *   4. Align: shift Y right by the exponent difference, at most 58 (which
 *      empties it), by whole bytes then bits, collecting the lost bits in
 *      w[48]; jam them into bit 0.
 *   5. Signs alike: X + Y; a carry into bit 56 is shifted back down, the
 *      bit lost ORed into bit 0, exponent + 1. Signs unlike: X - Y, which
 *      cannot borrow; an exact 0 goes to the C (its sign has rules of its
 *      own); normalise left until bit 55 is set, exponent - 1 a step.
 *   6. Round to nearest even: with the guard bit (bit 2) set, round up
 *      (add 8, one ulp at bit 3) unless bits 1 and 0 and the last bit (3)
 *      are all clear, an exact tie with an even result; AND 0x0B tests
 *      those three. A carry into bit 56 is shifted back, exponent + 1.
 *   7. An exponent below 1 (a subnormal result) or 0x7FF or more
 *      (overflow) goes to the C. Otherwise drop the guard bits and pack
 *      into r: bytes 0..5 of the fraction; byte 6, the exponent's low 4
 *      bits over the fraction's top 4; byte 7, the sign over the
 *      exponent's top 7. */
#if FP_ASM
static int add_fast(struct sf64 *r, const struct sf64 *a, const struct sf64 *b, int negate)
{
    int ok;
    unsigned char *p;
    unsigned char w[50];

    p = w;
    negate = negate ? 0x80 : 0;
#asm
        ld      iy, (ix-6)
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
        ld      a, (iy+7)
        and     a, 0x7F
        ld      h, a
        ld      a, (iy+6)
        ld      l, a
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
#endasm
#asm
        rr      l
        srl     h
        rr      l
        ld      a, h
        or      a, l
        jp      z, @slow
        ld      de, 0x7FF
        or      a
        sbc     hl, de
        jp      z, @slow
        add     hl, de
        ld      (iy+40), hl
        ld      hl, 0
        ld      a, (iy+15)
        and     a, 0x7F
        ld      h, a
        ld      a, (iy+14)
        ld      l, a
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
#endasm
#asm
        ld      a, h
        or      a, l
        jp      z, @slow
        ld      de, 0x7FF
        or      a
        sbc     hl, de
        jp      z, @slow
        add     hl, de
        ld      (iy+43), hl
        ld      a, (iy+7)
        and     a, 0x80
        ld      (iy+46), a
        ld      a, (iy+15)
        and     a, 0x80
        xor     a, (ix+15)
        ld      (iy+47), a
        lea     hl, iy+0
        lea     de, iy+16
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      a, (iy+6)
#endasm
#asm
        and     a, 0x0F
        or      a, 0x10
        ld      (iy+22), a
        ld      (iy+23), 0
        sla     (iy+16)
        rl      (iy+17)
        rl      (iy+18)
        rl      (iy+19)
        rl      (iy+20)
        rl      (iy+21)
        rl      (iy+22)
        rl      (iy+23)
        sla     (iy+16)
        rl      (iy+17)
        rl      (iy+18)
        rl      (iy+19)
        rl      (iy+20)
        rl      (iy+21)
        rl      (iy+22)
        rl      (iy+23)
        sla     (iy+16)
        rl      (iy+17)
        rl      (iy+18)
#endasm
#asm
        rl      (iy+19)
        rl      (iy+20)
        rl      (iy+21)
        rl      (iy+22)
        rl      (iy+23)
        lea     hl, iy+8
        lea     de, iy+24
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      a, (iy+14)
        and     a, 0x0F
        or      a, 0x10
        ld      (iy+30), a
        ld      (iy+31), 0
        sla     (iy+24)
        rl      (iy+25)
        rl      (iy+26)
        rl      (iy+27)
        rl      (iy+28)
        rl      (iy+29)
        rl      (iy+30)
#endasm
#asm
        rl      (iy+31)
        sla     (iy+24)
        rl      (iy+25)
        rl      (iy+26)
        rl      (iy+27)
        rl      (iy+28)
        rl      (iy+29)
        rl      (iy+30)
        rl      (iy+31)
        sla     (iy+24)
        rl      (iy+25)
        rl      (iy+26)
        rl      (iy+27)
        rl      (iy+28)
        rl      (iy+29)
        rl      (iy+30)
        rl      (iy+31)
        ld      hl, (iy+40)
        ld      de, (iy+43)
        or      a
        sbc     hl, de
        jp      c, @swap
        jp      nz, @ordered
#endasm
#asm
        ld      a, (iy+23)
        cp      a, (iy+31)
        jp      c, @swap
        jp      nz, @ordered
        ld      a, (iy+22)
        cp      a, (iy+30)
        jp      c, @swap
        jp      nz, @ordered
        ld      a, (iy+21)
        cp      a, (iy+29)
        jp      c, @swap
        jp      nz, @ordered
        ld      a, (iy+20)
        cp      a, (iy+28)
        jp      c, @swap
        jp      nz, @ordered
        ld      a, (iy+19)
        cp      a, (iy+27)
        jp      c, @swap
        jp      nz, @ordered
#endasm
#asm
        ld      a, (iy+18)
        cp      a, (iy+26)
        jp      c, @swap
        jp      nz, @ordered
        ld      a, (iy+17)
        cp      a, (iy+25)
        jp      c, @swap
        jp      nz, @ordered
        ld      a, (iy+16)
        cp      a, (iy+24)
        jp      c, @swap
        jp      nz, @ordered
        jp      @ordered
@swap:
        lea     hl, iy+16
        lea     de, iy+32
        ld      bc, 8
        ldir
        lea     hl, iy+24
        lea     de, iy+16
        ld      bc, 8
        ldir
        lea     hl, iy+32
#endasm
#asm
        lea     de, iy+24
        ld      bc, 8
        ldir
        ld      hl, (iy+40)
        ld      de, (iy+43)
        ld      (iy+40), de
        ld      (iy+43), hl
        ld      a, (iy+46)
        ld      c, (iy+47)
        ld      (iy+46), c
        ld      (iy+47), a
@ordered:
        ld      hl, (iy+40)
        ld      de, (iy+43)
        or      a
        sbc     hl, de
        ld      de, 58
        or      a
        sbc     hl, de
        jp      c, @near
        ld      hl, 0
@near:
        add     hl, de
        ld      a, l
#endasm
#asm
        ld      (iy+48), 0
        ld      c, a
        srl     a
        srl     a
        srl     a
        or      a
        jp      z, @bits
        ld      b, a
@bytes:
        ld      a, (iy+24)
        or      a, (iy+48)
        ld      (iy+48), a
        ld      hl, (iy+25)
        ld      (iy+24), hl
        ld      hl, (iy+28)
        ld      (iy+27), hl
        ld      a, (iy+31)
        ld      (iy+30), a
        ld      (iy+31), 0
        djnz    @bytes
@bits:
        ld      a, c
        and     a, 7
        jp      z, @jam
#endasm
#asm
        ld      b, a
@bit:
        srl     (iy+31)
        rr      (iy+30)
        rr      (iy+29)
        rr      (iy+28)
        rr      (iy+27)
        rr      (iy+26)
        rr      (iy+25)
        rr      (iy+24)
        jp      nc, @kept
        ld      (iy+48), 1
@kept:
        djnz    @bit
@jam:
        ld      a, (iy+48)
        or      a
        jp      z, @op
        set     0, (iy+24)
@op:
        ld      a, (iy+46)
        cp      a, (iy+47)
        jp      nz, @sub
        ld      a, (iy+16)
        add     a, (iy+24)
#endasm
#asm
        ld      (iy+16), a
        ld      a, (iy+17)
        adc     a, (iy+25)
        ld      (iy+17), a
        ld      a, (iy+18)
        adc     a, (iy+26)
        ld      (iy+18), a
        ld      a, (iy+19)
        adc     a, (iy+27)
        ld      (iy+19), a
        ld      a, (iy+20)
        adc     a, (iy+28)
        ld      (iy+20), a
        ld      a, (iy+21)
        adc     a, (iy+29)
        ld      (iy+21), a
        ld      a, (iy+22)
        adc     a, (iy+30)
        ld      (iy+22), a
        ld      a, (iy+23)
#endasm
#asm
        adc     a, (iy+31)
        ld      (iy+23), a
        bit     0, (iy+23)
        jp      z, @round
        ld      a, (iy+16)
        and     a, 1
        ld      c, a
        srl     (iy+23)
        rr      (iy+22)
        rr      (iy+21)
        rr      (iy+20)
        rr      (iy+19)
        rr      (iy+18)
        rr      (iy+17)
        rr      (iy+16)
        ld      a, (iy+16)
        or      a, c
        ld      (iy+16), a
        ld      hl, (iy+40)
        inc     hl
        ld      (iy+40), hl
        jp      @round
@sub:
#endasm
#asm
        ld      a, (iy+16)
        sub     a, (iy+24)
        ld      (iy+16), a
        ld      a, (iy+17)
        sbc     a, (iy+25)
        ld      (iy+17), a
        ld      a, (iy+18)
        sbc     a, (iy+26)
        ld      (iy+18), a
        ld      a, (iy+19)
        sbc     a, (iy+27)
        ld      (iy+19), a
        ld      a, (iy+20)
        sbc     a, (iy+28)
        ld      (iy+20), a
        ld      a, (iy+21)
        sbc     a, (iy+29)
        ld      (iy+21), a
        ld      a, (iy+22)
        sbc     a, (iy+30)
#endasm
#asm
        ld      (iy+22), a
        ld      a, (iy+23)
        sbc     a, (iy+31)
        ld      (iy+23), a
        ld      a, (iy+16)
        or      a, (iy+17)
        or      a, (iy+18)
        or      a, (iy+19)
        or      a, (iy+20)
        or      a, (iy+21)
        or      a, (iy+22)
        or      a, (iy+23)
        jp      z, @slow
@norm:
        bit     7, (iy+22)
        jp      nz, @round
        sla     (iy+16)
        rl      (iy+17)
        rl      (iy+18)
        rl      (iy+19)
        rl      (iy+20)
        rl      (iy+21)
#endasm
#asm
        rl      (iy+22)
        rl      (iy+23)
        ld      hl, (iy+40)
        dec     hl
        ld      (iy+40), hl
        jp      @norm
@round:
        ld      a, (iy+16)
        bit     2, a
        jp      z, @rounded
        and     a, 0x0B
        jp      z, @rounded
        ld      a, (iy+16)
        add     a, 8
        ld      (iy+16), a
        ld      a, (iy+17)
        adc     a, 0
        ld      (iy+17), a
        ld      a, (iy+18)
        adc     a, 0
        ld      (iy+18), a
        ld      a, (iy+19)
        adc     a, 0
#endasm
#asm
        ld      (iy+19), a
        ld      a, (iy+20)
        adc     a, 0
        ld      (iy+20), a
        ld      a, (iy+21)
        adc     a, 0
        ld      (iy+21), a
        ld      a, (iy+22)
        adc     a, 0
        ld      (iy+22), a
        ld      a, (iy+23)
        adc     a, 0
        ld      (iy+23), a
        bit     0, (iy+23)
        jp      z, @rounded
        srl     (iy+23)
        rr      (iy+22)
        rr      (iy+21)
        rr      (iy+20)
        rr      (iy+19)
        rr      (iy+18)
        rr      (iy+17)
#endasm
#asm
        rr      (iy+16)
        ld      hl, (iy+40)
        inc     hl
        ld      (iy+40), hl
@rounded:
        ld      hl, (iy+40)
        ld      de, 1
        or      a
        sbc     hl, de
        jp      m, @slow
        ld      hl, (iy+40)
        ld      de, 0x7FF
        or      a
        sbc     hl, de
        jp      p, @slow
        srl     (iy+23)
        rr      (iy+22)
        rr      (iy+21)
        rr      (iy+20)
        rr      (iy+19)
        rr      (iy+18)
        rr      (iy+17)
        rr      (iy+16)
#endasm
#asm
        srl     (iy+23)
        rr      (iy+22)
        rr      (iy+21)
        rr      (iy+20)
        rr      (iy+19)
        rr      (iy+18)
        rr      (iy+17)
        rr      (iy+16)
        srl     (iy+23)
        rr      (iy+22)
        rr      (iy+21)
        rr      (iy+20)
        rr      (iy+19)
        rr      (iy+18)
        rr      (iy+17)
        rr      (iy+16)
        ld      de, (ix+6)
        lea     hl, iy+16
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      hl, (iy+40)
        ld      a, l
#endasm
#asm
        add     a, a
        add     a, a
        add     a, a
        add     a, a
        ld      c, a
        ld      a, (iy+22)
        and     a, 0x0F
        or      a, c
        ld      (de), a
        inc     de
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        ld      a, l
        or      a, (iy+46)
        ld      (de), a
        ld      hl, 1
        ld      (ix-3), hl
        jp      @done
@slow:
        ld      hl, 0
#endasm
#asm
        ld      (ix-3), hl
@done:
#endasm
    return ok;
}
#else
static int add_fast(struct sf64 *r, const struct sf64 *a, const struct sf64 *b, int negate)
{
    return 0;                           /* the C does it all */
}
#endif

/* The fast path, then by the signs: magnitudes add or subtract. t keeps
 * r from being written while a and b are still being read, since r may
 * be one of them. */
void sf64_add(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    struct sf64 t;

    if (add_fast(r, a, b, 0))
        return;
    if (SIGN64(a) == SIGN64(b))
        add_mags(&t, a, b, SIGN64(a));
    else
        sub_mags(&t, a, b, SIGN64(a));
    *r = t;
}

/* a - b is a + (-b): the same with the roles of like and unlike signs
 * swapped. */
void sf64_sub(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    struct sf64 t;

    if (add_fast(r, a, b, 1))
        return;
    if (SIGN64(a) == SIGN64(b))
        sub_mags(&t, a, b, SIGN64(a));
    else
        add_mags(&t, a, b, SIGN64(a));
    *r = t;
}

void sf64_neg(struct sf64 *r, const struct sf64 *a)
{
    r->hi = a->hi ^ 0x80000000UL;
    r->lo = a->lo;
}

/* ---- binary64: * / sqrt ------------------------------------------------------------ */

/* sf64_mul's fast path: two normal operands whose product is normal,
 * done whole in assembly (unpack, the 106-bit product by MLT byte
 * products, normalise, round to nearest even, pack); 1 if it did it, 0
 * (r untouched) for the C to do. Its result is the IEEE one, which the C
 * also gives, so the bits are the same (t_fpeq).
 *
 * The product of two 53-bit significands is exact in 106 bits, so the
 * rounding sees every bit, the low ones as a sticky bit. ok is at
 * (ix-3), p at (ix-6). The workspace w, by byte offset (each value low
 * byte first):
 *   0..7    a's eight bytes, as in memory
 *   8..15   b's
 *   16..22  A, a's 53-bit significand (implicit 1 at bit 52)
 *   23..29  B, b's
 *   30..43  the product A * B, 14 bytes
 *   44..51  the result's significand: the product's bits 53..105
 *   52      the byte of A being multiplied
 *   53..55  the result's biased exponent (24 bits)
 *   56      the result's sign (0x80 or 0)
 *
 * The steps:
 *   1. Copy and take the exponents as add_fast does; 0 or 0x7FF goes to
 *      the C. The exponent is exp_a + exp_b - 1023, the sign a's XOR b's.
 *   2. Unpack A and B and multiply by bytes as mul64_jam does: 7 x 7 MLT
 *      products, a zero byte of A skipped.
 *   3. Normalise: the product is in [2^104, 2^106); with bit 105 set the
 *      exponent goes up one, else the product moves left a bit, so that
 *      its leading 1 is at bit 105.
 *   4. The significand is bits 53..105 (product bytes 6..13 shifted down
 *      5); bit 52 (bit 4 of byte 6) is the guard, bits 0..51 the sticky.
 *      Round up when the guard is set and either a sticky bit or the last
 *      bit is (round to nearest even); a carry into bit 53 is shifted
 *      back, exponent + 1.
 *   5. An exponent below 1 or 0x7FF or more goes to the C; otherwise pack
 *      as add_fast does. */
#if FP_ASM
static int mul_fast(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    int ok;
    unsigned char *p;
    unsigned char w[60];

    p = w;
#asm
        ld      iy, (ix-6)
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
        ld      a, (iy+7)
        and     a, 0x7F
        ld      h, a
        ld      a, (iy+6)
        ld      l, a
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
#endasm
#asm
        rr      l
        srl     h
        rr      l
        ld      a, h
        or      a, l
        jp      z, @slow
        ld      de, 0x7FF
        or      a
        sbc     hl, de
        jp      z, @slow
        add     hl, de
        ld      (iy+53), hl
        ld      hl, 0
        ld      a, (iy+15)
        and     a, 0x7F
        ld      h, a
        ld      a, (iy+14)
        ld      l, a
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
#endasm
#asm
        ld      a, h
        or      a, l
        jp      z, @slow
        ld      de, 0x7FF
        or      a
        sbc     hl, de
        jp      z, @slow
        add     hl, de
        ld      de, (iy+53)
        add     hl, de
        ld      de, -1023
        add     hl, de
        ld      (iy+53), hl
        ld      a, (iy+7)
        xor     a, (iy+15)
        and     a, 0x80
        ld      (iy+56), a
        lea     hl, iy+0
        lea     de, iy+16
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
#endasm
#asm
        ld      a, (iy+6)
        and     a, 0x0F
        or      a, 0x10
        ld      (iy+22), a
        lea     hl, iy+8
        lea     de, iy+23
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      a, (iy+14)
        and     a, 0x0F
        or      a, 0x10
        ld      (iy+29), a
        ld      hl, 0
        ld      (iy+30), hl
        ld      (iy+33), hl
        ld      (iy+36), hl
        ld      (iy+39), hl
        ld      (iy+42), l
        ld      (iy+43), l
        ld      c, 0
@outer:
#endasm
#asm
        lea     hl, iy+16
        ld      de, 0
        ld      e, c
        add     hl, de
        ld      a, (hl)
        or      a
        jp      z, @nexti
        ld      (iy+52), a
        ld      de, 14
        add     hl, de
        ld      e, (iy+23)
        call    @step
        ld      e, (iy+24)
        call    @step
        ld      e, (iy+25)
        call    @step
        ld      e, (iy+26)
        call    @step
        ld      e, (iy+27)
        call    @step
        ld      e, (iy+28)
        call    @step
        ld      e, (iy+29)
#endasm
#asm
        call    @step
@nexti:
        inc     c
        ld      a, c
        cp      a, 7
        jp      nz, @outer
        bit     1, (iy+43)
        jr      nz, @top
        sla     (iy+30)
        rl      (iy+31)
        rl      (iy+32)
        rl      (iy+33)
        rl      (iy+34)
        rl      (iy+35)
        rl      (iy+36)
        rl      (iy+37)
        rl      (iy+38)
        rl      (iy+39)
        rl      (iy+40)
        rl      (iy+41)
        rl      (iy+42)
        rl      (iy+43)
        jr      @norm
@top:
#endasm
#asm
        ld      hl, (iy+53)
        inc     hl
        ld      (iy+53), hl
@norm:
        lea     hl, iy+36
        lea     de, iy+44
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      b, 5
@q5:
        srl     (iy+51)
        rr      (iy+50)
        rr      (iy+49)
        rr      (iy+48)
        rr      (iy+47)
        rr      (iy+46)
        rr      (iy+45)
        rr      (iy+44)
        djnz    @q5
        bit     4, (iy+36)
        jp      z, @rounded
        ld      a, (iy+36)
#endasm
#asm
        and     a, 0x0F
        or      a, (iy+30)
        or      a, (iy+31)
        or      a, (iy+32)
        or      a, (iy+33)
        or      a, (iy+34)
        or      a, (iy+35)
        jp      nz, @up
        bit     0, (iy+44)
        jp      z, @rounded
@up:
        inc     (iy+44)
        jr      nz, @carried
        inc     (iy+45)
        jr      nz, @carried
        inc     (iy+46)
        jr      nz, @carried
        inc     (iy+47)
        jr      nz, @carried
        inc     (iy+48)
        jr      nz, @carried
        inc     (iy+49)
#endasm
#asm
        jr      nz, @carried
        inc     (iy+50)
        jr      nz, @carried
        inc     (iy+51)
@carried:
        bit     5, (iy+50)
        jr      z, @rounded
        srl     (iy+51)
        rr      (iy+50)
        rr      (iy+49)
        rr      (iy+48)
        rr      (iy+47)
        rr      (iy+46)
        rr      (iy+45)
        rr      (iy+44)
        ld      hl, (iy+53)
        inc     hl
        ld      (iy+53), hl
@rounded:
        ld      hl, (iy+53)
        ld      de, 1
        or      a
        sbc     hl, de
#endasm
#asm
        jp      m, @slow
        ld      hl, (iy+53)
        ld      de, 0x7FF
        or      a
        sbc     hl, de
        jp      p, @slow
        ld      de, (ix+6)
        lea     hl, iy+44
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ld      hl, (iy+53)
        ld      a, l
        add     a, a
        add     a, a
        add     a, a
        add     a, a
        ld      c, a
        ld      a, (iy+50)
        and     a, 0x0F
        or      a, c
        ld      (de), a
        inc     de
        srl     h
#endasm
#asm
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        srl     h
        rr      l
        ld      a, l
        or      a, (iy+56)
        ld      (de), a
        ld      hl, 1
        ld      (ix-3), hl
        jr      @done
@step:
        ld      d, (iy+52)
        mlt     de
        ld      a, (hl)
        add     a, e
        ld      (hl), a
        inc     hl
        ld      a, (hl)
        adc     a, d
        ld      (hl), a
        ret     nc
        push    hl
@carry:
        inc     hl
#endasm
#asm
        inc     (hl)
        jr      z, @carry
        pop     hl
        ret
@slow:
        ld      hl, 0
        ld      (ix-3), hl
@done:
#endasm
    return ok;
}
#else
static int mul_fast(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    return 0;                           /* the C does it all */
}
#endif

/* SoftFloat's mulF64, after the fast path. The special cases first (a
 * NaN, inf * 0 which is invalid, infinity, zero), subnormals normalised,
 * then the significands with their implicit 1 at bits 62 and 63: the
 * high half of the 128-bit product is in [2^61, 2^63), the low half
 * jammed into its bit 0. exp_z = exp_a + exp_b - 1023 is one less than
 * the biased exponent of a product of significands in [2, 4); a high
 * half below 2^62 (a product in [1, 2)) moves up a bit with the exponent
 * less one. */
void sf64_mul(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    int sign;
    int exp_a;
    int exp_b;
    int exp_z;
    struct u64 sig_a;
    struct u64 sig_b;
    struct u64 sig_z;
    struct sf64 t;

    if (mul_fast(r, a, b))
        return;

    sign = SIGN64(a) ^ SIGN64(b);
    exp_a = EXP64(a);
    exp_b = EXP64(b);
    frac64(&sig_a, a);
    frac64(&sig_b, b);
    if (exp_a == 0x7FF || exp_b == 0x7FF) {
        if (is_nan64(a) || is_nan64(b))
            propagate_nan(&t, a, b);
        else if ((exp_a == 0 && u64_zero(&sig_a)) || (exp_b == 0 && u64_zero(&sig_b)))
            default_nan(&t);                        /* inf * 0 */
        else
            pack_special(&t, sign, 0x7FF);
        *r = t;
        return;
    }
    if (exp_a == 0) {
        if (u64_zero(&sig_a)) {
            pack_special(r, sign, 0);
            return;
        }
        exp_a = norm_subnormal(&sig_a);
    }
    if (exp_b == 0) {
        if (u64_zero(&sig_b)) {
            pack_special(r, sign, 0);
            return;
        }
        exp_b = norm_subnormal(&sig_b);
    }
    exp_z = exp_a + exp_b - 0x3FF;
    sig_a.hi = sig_a.hi | 0x100000UL;
    sig_b.hi = sig_b.hi | 0x100000UL;
    u64_shl(&sig_a, &sig_a, 10);
    u64_shl(&sig_b, &sig_b, 11);
    mul64_jam(&sig_z, &sig_a, &sig_b);
    if (!(sig_z.hi & 0x40000000UL)) {
        exp_z--;
        u64_shl(&sig_z, &sig_z, 1);
    }
    round_pack(r, sign, exp_z, &sig_z);
}

/* The division's loop: x in [y, 2y) gives q's 63 bits (shift q, take y
 * from x if it goes, shift x), and leaves twice the remainder in x.
 *
 * Restoring division, binary long division: each step finds one quotient
 * bit by trying the subtraction. Since x stays below 2y, every bit is 0
 * or 1 and the first is 1. The caller only asks whether the remainder
 * is 0 (the sticky bit), which doubling does not change. */
#if FP_ASM
/* In place in w, as struct u64s (each value's bytes low first: 4..7,
 * 0..3); a trial subtraction into t, kept when it does not borrow.
 *
 * w's bytes: x 0..7, y 8..15, t 16..23, q 24..31; B counts the steps. */
static void div63(struct u64 *q, struct u64 *x, const struct u64 *y)
{
    struct u64 *p;
    struct u64 w[4];                    /* x, y, t, q */

    p = w;
    w[0] = *x;
    w[1] = *y;
    w[3].hi = 0;
    w[3].lo = 0;
#asm
        ld      iy, (ix-3)
        ld      b, 63
@loop:
        sla     (iy+28)
        rl      (iy+29)
        rl      (iy+30)
        rl      (iy+31)
        rl      (iy+24)
        rl      (iy+25)
        rl      (iy+26)
        rl      (iy+27)
        ld      a, (iy+4)
        sub     a, (iy+12)
        ld      (iy+20), a
        ld      a, (iy+5)
        sbc     a, (iy+13)
        ld      (iy+21), a
        ld      a, (iy+6)
        sbc     a, (iy+14)
        ld      (iy+22), a
        ld      a, (iy+7)
        sbc     a, (iy+15)
#endasm
#asm
        ld      (iy+23), a
        ld      a, (iy+0)
        sbc     a, (iy+8)
        ld      (iy+16), a
        ld      a, (iy+1)
        sbc     a, (iy+9)
        ld      (iy+17), a
        ld      a, (iy+2)
        sbc     a, (iy+10)
        ld      (iy+18), a
        ld      a, (iy+3)
        sbc     a, (iy+11)
        ld      (iy+19), a
        jr      c, @less
        ld      hl, (iy+16)
        ld      (iy+0), hl
        ld      hl, (iy+19)
        ld      (iy+3), hl
        ld      a, (iy+22)
        ld      (iy+6), a
        ld      a, (iy+23)
#endasm
#asm
        ld      (iy+7), a
        set     0, (iy+28)
@less:
        sla     (iy+4)
        rl      (iy+5)
        rl      (iy+6)
        rl      (iy+7)
        rl      (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        dec     b
        jp      nz, @loop
#endasm
    *x = w[0];
    *q = w[3];
}
#else
static void div63(struct u64 *q, struct u64 *x, const struct u64 *y)
{
    int i;

    u64_set(q, 0, 0);
    for (i = 0; i < 63; i++) {
        u64_shl(q, q, 1);
        if (!u64_lt(x, y)) {
            u64_sub(x, x, y);
            q->lo = q->lo | 1;
        }
        u64_shl(x, x, 1);
    }
}
#endif

/* The special cases first: a NaN, inf / inf and 0 / 0 (invalid), inf / x
 * and x / 0 (infinity), x / inf (zero); subnormals normalised. Then the
 * quotient of the significands, made to lie in [1, 2) by doubling x when
 * it is below y, has the biased exponent exp_a - exp_b + 1023, less one
 * if x was doubled; exp_z is one less again, for round_pack. */
void sf64_div(struct sf64 *r, const struct sf64 *a, const struct sf64 *b)
{
    int sign;
    int exp_a;
    int exp_b;
    int exp_z;
    struct u64 x;
    struct u64 y;
    struct u64 q;
    struct sf64 t;

    sign = SIGN64(a) ^ SIGN64(b);
    exp_a = EXP64(a);
    exp_b = EXP64(b);
    frac64(&x, a);
    frac64(&y, b);
    if (exp_a == 0x7FF) {
        if (!u64_zero(&x) || is_nan64(b))
            propagate_nan(&t, a, b);
        else if (exp_b == 0x7FF)
            default_nan(&t);                        /* inf / inf */
        else
            pack_special(&t, sign, 0x7FF);
        *r = t;
        return;
    }
    if (exp_b == 0x7FF) {
        if (!u64_zero(&y))
            propagate_nan(&t, a, b);
        else
            pack_special(&t, sign, 0);              /* x / inf */
        *r = t;
        return;
    }
    if (exp_b == 0) {
        if (u64_zero(&y)) {
            if (exp_a == 0 && u64_zero(&x))
                default_nan(r);                     /* 0 / 0 */
            else
                pack_special(r, sign, 0x7FF);       /* x / 0 */
            return;
        }
        exp_b = norm_subnormal(&y);
    }
    if (exp_a == 0) {
        if (u64_zero(&x)) {
            pack_special(r, sign, 0);
            return;
        }
        exp_a = norm_subnormal(&x);
    }
    exp_z = exp_a - exp_b + 0x3FE;
    x.hi = x.hi | 0x100000UL;
    y.hi = y.hi | 0x100000UL;
    if (u64_lt(&x, &y)) {
        exp_z--;
        u64_shl(&x, &x, 1);
    }
    /* x in [y, 2y): 63 quotient bits, the first a 1, then the remainder's
     * sticky bit */
    div63(&q, &x, &y);
    if (!u64_zero(&x))
        q.lo = q.lo | 1;
    round_pack(r, sign, exp_z, &q);
}

/* The square root's loop: the root of m * 2^56 (m below 2^54), two bits
 * of m a step, into root (55 bits) and the remainder rem.
 *
 * The digit-by-digit method, pencil-and-paper square root in base 4:
 * with root r so far and remainder rem (r^2 + rem is the value's bits
 * used so far), bringing down the next two bits makes rem = 4 rem + the
 * pair; the next root bit is 1 if rem >= 4r + 1, since (2r + 1)^2 = 4r^2
 * + 4r + 1, and then 4r + 1 is taken from rem. At the end root^2 + rem is
 * exactly m * 2^56, so rem != 0 says the root is inexact. */
#if FP_ASM
/* With m shifted up so that its bit 53 is at bit 63, (rem:m) is shifted
 * left two bits a step, which brings m's bits into rem two at a time and
 * zeros after them, as the C's pairs do. In place in w as struct u64s.
 *
 * w's bytes: m 0..7, rem 8..15, root 16..23, trial 24..31, d 32..39. A
 * step: (rem:m) left 2 as one 128-bit value; trial = root << 2 | 1; root
 * left 1; d = rem - trial, and if that does not borrow, rem = d and the
 * root's bit 0 is set. B counts the steps. */
static void sqrt55(struct u64 *root, struct u64 *rem, const struct u64 *m)
{
    struct u64 *p;
    struct u64 w[5];                    /* m, rem, root, trial, d */

    p = w;
    u64_shl(&w[0], m, 10);
    w[1].hi = 0;
    w[1].lo = 0;
    w[2].hi = 0;
    w[2].lo = 0;
#asm
        ld      iy, (ix-3)
        ld      b, 55
@loop:
        sla     (iy+4)
        rl      (iy+5)
        rl      (iy+6)
        rl      (iy+7)
        rl      (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+12)
        rl      (iy+13)
        rl      (iy+14)
        rl      (iy+15)
        rl      (iy+8)
        rl      (iy+9)
        rl      (iy+10)
        rl      (iy+11)
        sla     (iy+4)
        rl      (iy+5)
        rl      (iy+6)
        rl      (iy+7)
        rl      (iy+0)
#endasm
#asm
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+12)
        rl      (iy+13)
        rl      (iy+14)
        rl      (iy+15)
        rl      (iy+8)
        rl      (iy+9)
        rl      (iy+10)
        rl      (iy+11)
        ld      hl, (iy+16)
        ld      (iy+24), hl
        ld      hl, (iy+19)
        ld      (iy+27), hl
        ld      a, (iy+22)
        ld      (iy+30), a
        ld      a, (iy+23)
        ld      (iy+31), a
        sla     (iy+28)
        rl      (iy+29)
        rl      (iy+30)
#endasm
#asm
        rl      (iy+31)
        rl      (iy+24)
        rl      (iy+25)
        rl      (iy+26)
        rl      (iy+27)
        sla     (iy+28)
        rl      (iy+29)
        rl      (iy+30)
        rl      (iy+31)
        rl      (iy+24)
        rl      (iy+25)
        rl      (iy+26)
        rl      (iy+27)
        set     0, (iy+28)
        sla     (iy+20)
        rl      (iy+21)
        rl      (iy+22)
        rl      (iy+23)
        rl      (iy+16)
        rl      (iy+17)
        rl      (iy+18)
        rl      (iy+19)
        ld      a, (iy+12)
#endasm
#asm
        sub     a, (iy+28)
        ld      (iy+36), a
        ld      a, (iy+13)
        sbc     a, (iy+29)
        ld      (iy+37), a
        ld      a, (iy+14)
        sbc     a, (iy+30)
        ld      (iy+38), a
        ld      a, (iy+15)
        sbc     a, (iy+31)
        ld      (iy+39), a
        ld      a, (iy+8)
        sbc     a, (iy+24)
        ld      (iy+32), a
        ld      a, (iy+9)
        sbc     a, (iy+25)
        ld      (iy+33), a
        ld      a, (iy+10)
        sbc     a, (iy+26)
        ld      (iy+34), a
#endasm
#asm
        ld      a, (iy+11)
        sbc     a, (iy+27)
        ld      (iy+35), a
        jr      c, @less
        ld      hl, (iy+32)
        ld      (iy+8), hl
        ld      hl, (iy+35)
        ld      (iy+11), hl
        ld      a, (iy+38)
        ld      (iy+14), a
        ld      a, (iy+39)
        ld      (iy+15), a
        set     0, (iy+20)
@less:
        dec     b
        jp      nz, @loop
#endasm
    *rem = w[1];
    *root = w[2];
}
#else
static void sqrt55(struct u64 *root, struct u64 *rem, const struct u64 *m)
{
    int i;
    int bit;
    struct u64 trial;

    u64_set(root, 0, 0);
    u64_set(rem, 0, 0);
    for (i = 0; i < 55; i++) {
        bit = 53 - 2 * i;                           /* m's next pair: bits bit, bit - 1 */
        u64_shl(rem, rem, 2);
        if (bit >= 1)
            rem->lo = rem->lo | ((bit >= 32 ? m->hi >> (bit - 32) : m->lo >> bit) & 1) << 1
                      | ((bit - 1 >= 32 ? m->hi >> (bit - 33) : m->lo >> (bit - 1)) & 1);
        u64_shl(&trial, root, 2);
        trial.lo = trial.lo | 1;
        u64_shl(root, root, 1);
        if (!u64_lt(rem, &trial)) {
            u64_sub(rem, rem, &trial);
            root->lo = root->lo | 1;
        }
    }
}
#endif

/* The special cases first: a NaN, sqrt(+inf) = +inf, sqrt(-0) = -0, any
 * other negative (invalid). The square root of a binary64 is never
 * exactly halfway between two binary64s, but the remainder still goes in
 * as the sticky bit, so round_pack sees which side of the root it is. */
void sf64_sqrt(struct sf64 *r, const struct sf64 *a)
{
    int exp_a;
    int e;
    struct u64 m;
    struct u64 root;
    struct u64 rem;
    struct sf64 t;

    exp_a = EXP64(a);
    frac64(&m, a);
    if (exp_a == 0x7FF) {
        if (!u64_zero(&m))
            propagate_nan(&t, a, 0);
        else if (SIGN64(a))
            default_nan(&t);                        /* sqrt(-inf) */
        else
            t = *a;
        *r = t;
        return;
    }
    if (SIGN64(a)) {
        if (exp_a == 0 && u64_zero(&m))
            *r = *a;                                /* sqrt(-0) is -0 */
        else
            default_nan(r);
        return;
    }
    if (exp_a == 0) {
        if (u64_zero(&m)) {
            *r = *a;
            return;
        }
        exp_a = norm_subnormal(&m);
    }
    m.hi = m.hi | 0x100000UL;
    /* the value is m * 2^e, m in [2^52, 2^53); make e even */
    e = exp_a - 1075;
    if (e & 1) {
        u64_shl(&m, &m, 1);
        e--;
    }
    /* the root of m * 2^56 (110 bits, m's 54 then 56 zeros), two bits a
     * step: 55 bits of root in [2^54, 2^55) */
    sqrt55(&root, &rem, &m);
    /* root * 2^((e - 56) / 2), its leading 1 at bit 54: to bit 62. e is
     * even, so the division is exact even when e - 56 is negative (where
     * C89 lets an inexact quotient round either way); 54 + (e - 56) / 2 is
     * the root's unbiased exponent, and round_pack takes the biased one
     * less one */
    u64_shl(&root, &root, 8);
    if (!u64_zero(&rem))
        root.lo = root.lo | 1;
    round_pack(r, 0, 54 + (e - 56) / 2 + 1022, &root);
}

/* NaNs are unordered and the two zeros equal; otherwise a different sign
 * decides, and within one sign the bits, as a 64-bit integer, order
 * like the magnitudes (see the top), the order reversed for negatives. */
int sf64_cmp(const struct sf64 *a, const struct sf64 *b)
{
    int sign_a;
    int sign_b;
    int less;

    if (is_nan64(a) || is_nan64(b))
        return 2;
    if ((a->hi & 0x7FFFFFFFUL) == 0 && a->lo == 0 && (b->hi & 0x7FFFFFFFUL) == 0 && b->lo == 0)
        return 0;                                   /* +0 and -0 */
    sign_a = SIGN64(a);
    sign_b = SIGN64(b);
    if (sign_a != sign_b)
        return sign_a ? -1 : 1;
    if (a->hi == b->hi && a->lo == b->lo)
        return 0;
    less = a->hi < b->hi || (a->hi == b->hi && a->lo < b->lo);     /* by magnitude */
    return less != sign_a ? -1 : 1;
}

/* ---- conversions ------------------------------------------------------------------- */

/* Always exact (32 bits fit in 53): the magnitude (0 - v, so INT32_MIN
 * gives 2^31), its leading 1 moved to bit 52, and exponent 0x432 - shift,
 * where 0x432 = 1023 + 52 - 1 (pack adds the leading 1). */
void sf64_from_long(struct sf64 *r, unsigned long v, int is_signed)
{
    int sign;
    int shift;
    struct u64 sig;

    v = M32(v);
    sign = is_signed && (v & 0x80000000UL);
    if (sign)
        v = M32(0 - v);
    if (v == 0) {
        pack_special(r, 0, 0);
        return;
    }
    shift = clz32(v) + 21;
    u64_set(&sig, 0, v);
    u64_shl(&sig, &sig, shift);
    pack(r, sign, 0x432 - shift, &sig);
}

/* Truncation toward zero, C's rule: with e the unbiased exponent, the
 * significand shifted right 52 - e bits keeps only the integer part. An
 * unsigned conversion of a negative value gives 0. */
unsigned long sf64_to_long(const struct sf64 *a, int is_signed)
{
    int sign;
    int e;
    unsigned long v;
    struct u64 sig;

    if (is_nan64(a))
        return 0;
    sign = SIGN64(a);
    e = EXP64(a) - 1023;
    if (e < 0)
        return 0;                                   /* |a| < 1 */
    if (is_signed) {
        if (e >= 31) {
            if (sign)
                return 0x80000000UL;                /* INT32_MIN itself, or beyond */
            return 0x7FFFFFFFUL;
        }
    } else {
        if (sign)
            return 0;
        if (e >= 32)
            return 0xFFFFFFFFUL;
    }
    frac64(&sig, a);
    sig.hi = sig.hi | 0x100000UL;
    u64_shr(&sig, &sig, 52 - e);                    /* e <= 31: the integer part fits in lo */
    v = sig.lo;
    return sign ? M32(0 - v) : v;
}

/* ---- binary32 -------------------------------------------------------------------- */

/* Exact. The exponent is rebiased (+0x380, 1023 - 127) and the 23-bit
 * fraction goes to the top of the 52-bit one (hi takes its top 20 bits,
 * lo's top 3 bits the rest). A subnormal binary32 is normal in binary64,
 * so it is normalised first; a NaN is made quiet and keeps its payload. */
void sf64_from_f32(struct sf64 *r, unsigned long a)
{
    int sign;
    int exp;
    unsigned long frac;
    int shift;

    sign = (int)(a >> 31 & 1);
    exp = (int)(a >> 23 & 0xFF);
    frac = a & 0x7FFFFFUL;
    if (exp == 0xFF) {
        if (frac) {                                 /* a NaN: quiet, payload kept */
            r->hi = ((unsigned long)sign << 31) | 0x7FF80000UL | frac >> 3;
            r->lo = M32(frac << 29);
        } else {
            pack_special(r, sign, 0x7FF);
        }
        return;
    }
    if (exp == 0) {
        if (frac == 0) {
            pack_special(r, sign, 0);
            return;
        }
        shift = clz32(frac) - 8;                    /* the leading 1 to bit 23 */
        frac = M32(frac << shift) & 0x7FFFFFUL;
        exp = 1 - shift;
    }
    r->hi = ((unsigned long)sign << 31) | (unsigned long)(exp + 0x380) << 20 | frac >> 3;
    r->lo = M32(frac << 29);
}

/* SoftFloat's roundPackToF32: sig's leading 1 at bit 30, 7 bits below the
 * last one kept.
 *
 * round_pack for binary32: exp one less than the biased exponent, 0x40
 * half an ulp, the same round to nearest even, subnormal shift with a
 * sticky bit, and overflow to infinity. */
static unsigned long round_pack32(int sign, int exp, unsigned long sig)
{
    unsigned long round_bits;

    if (exp < 0) {
        if (-exp < 31)
            sig = sig >> -exp | (M32(sig << (32 + exp)) != 0);
        else
            sig = sig != 0;
        exp = 0;
    } else if (exp >= 0xFD) {
        if (exp > 0xFD || (M32(sig + 0x40) & 0x80000000UL))
            return ((unsigned long)sign << 31) | 0x7F800000UL;      /* overflow: infinity */
    }
    round_bits = sig & 0x7F;
    sig = M32(sig + 0x40) >> 7;
    if (round_bits == 0x40)
        sig = sig & ~(unsigned long)1;
    if (sig == 0)
        exp = 0;
    return M32(((unsigned long)sign << 31) + ((unsigned long)exp << 23) + sig);
}

/* Rounded once: the 52-bit fraction shifted down to 30 bits with a sticky
 * bit, the implicit 1 at bit 30, and the exponent rebiased by 0x381
 * (1023 - 127, and one more for round_pack32's convention). A binary64
 * subnormal, far below binary32's range, gets an exponent so negative
 * that round_pack32 makes it a signed zero. A NaN stays a NaN: quiet, its
 * payload's top bits kept. */
unsigned long sf32_from_f64(const struct sf64 *a)
{
    int sign;
    int exp;
    unsigned long frac32;
    struct u64 frac;

    sign = SIGN64(a);
    exp = EXP64(a);
    frac64(&frac, a);
    if (exp == 0x7FF) {
        if (!u64_zero(&frac))                       /* a NaN: quiet, payload kept */
            return ((unsigned long)sign << 31) | 0x7FC00000UL | ((frac.hi << 3 | frac.lo >> 29) & 0x3FFFFFUL);
        return ((unsigned long)sign << 31) | 0x7F800000UL;
    }
    u64_shr_jam(&frac, &frac, 22);                  /* 30 bits, sticky */
    frac32 = frac.lo;
    if (exp == 0 && frac32 == 0)
        return (unsigned long)sign << 31;
    return round_pack32(sign, exp - 0x381, frac32 | 0x40000000UL);
}

/* Each through binary64: exact there, then rounded once (see the top). */
static unsigned long op32(int op, unsigned long a, unsigned long b)
{
    struct sf64 x;
    struct sf64 y;

    sf64_from_f32(&x, a);
    sf64_from_f32(&y, b);
    if (op == '+')
        sf64_add(&x, &x, &y);
    else if (op == '-')
        sf64_sub(&x, &x, &y);
    else if (op == '*')
        sf64_mul(&x, &x, &y);
    else if (op == '/')
        sf64_div(&x, &x, &y);
    else
        sf64_sqrt(&x, &x);
    return sf32_from_f64(&x);
}

unsigned long sf32_add(unsigned long a, unsigned long b)
{
    return op32('+', a, b);
}

unsigned long sf32_sub(unsigned long a, unsigned long b)
{
    return op32('-', a, b);
}

unsigned long sf32_mul(unsigned long a, unsigned long b)
{
    return op32('*', a, b);
}

unsigned long sf32_div(unsigned long a, unsigned long b)
{
    return op32('/', a, b);
}

unsigned long sf32_sqrt(unsigned long a)
{
    return op32('s', a, 0);
}

unsigned long sf32_neg(unsigned long a)
{
    return M32(a ^ 0x80000000UL);
}

/* Through binary64, where both are exact. */
int sf32_cmp(unsigned long a, unsigned long b)
{
    struct sf64 x;
    struct sf64 y;

    sf64_from_f32(&x, a);
    sf64_from_f32(&y, b);
    return sf64_cmp(&x, &y);
}

/* Exact in binary64, so rounded only once, to binary32. */
unsigned long sf32_from_long(unsigned long v, int is_signed)
{
    struct sf64 x;

    sf64_from_long(&x, v, is_signed);
    return sf32_from_f64(&x);
}

unsigned long sf32_to_long(unsigned long a, int is_signed)
{
    struct sf64 x;

    sf64_from_f32(&x, a);
    return sf64_to_long(&x, is_signed);
}

/* A 64-bit integer's magnitude to m; returns its sign. */
static int magnitude64(struct u64 *m, unsigned long hi, unsigned long lo, int is_signed)
{
    struct u64 z;

    u64_set(m, M32(hi), M32(lo));
    if (!is_signed || !(m->hi & 0x80000000UL))
        return 0;
    u64_set(&z, 0, 0);
    u64_sub(m, &z, m);
    return 1;
}

/* Up to 64 significant bits, so rounded (exact when 53 suffice). */
void sf64_from_long64(struct sf64 *r, unsigned long hi, unsigned long lo, int is_signed)
{
    int sign;
    struct u64 m;

    sign = magnitude64(&m, hi, lo, is_signed);
    if (u64_zero(&m)) {
        pack_special(r, 0, 0);
        return;
    }
    if (m.hi & 0x80000000UL) {
        u64_shr_jam(&m, &m, 1);                     /* an unsigned value from 2^63: down to bit 62 */
        norm_round_pack(r, sign, 0x43D, &m);
        return;
    }
    norm_round_pack(r, sign, 0x43C, &m);            /* the exponent for a leading 1 at bit 62 */
}

/* As sf64_to_long, for 64 bits: the significand shifted left or right
 * so that bit 0 is the units. */
void sf64_to_long64(const struct sf64 *a, int is_signed, unsigned long *hi, unsigned long *lo)
{
    int sign;
    int e;
    struct u64 sig;
    struct u64 z;

    *hi = 0;
    *lo = 0;
    if (is_nan64(a))
        return;
    sign = SIGN64(a);
    e = EXP64(a) - 1023;
    if (e < 0)
        return;                                     /* |a| < 1 */
    if (is_signed) {
        if (e >= 63) {
            *hi = sign ? 0x80000000UL : 0x7FFFFFFFUL;       /* INT64_MIN itself, or beyond */
            *lo = sign ? 0 : 0xFFFFFFFFUL;
            return;
        }
    } else {
        if (sign)
            return;
        if (e >= 64) {
            *hi = 0xFFFFFFFFUL;
            *lo = 0xFFFFFFFFUL;
            return;
        }
    }
    frac64(&sig, a);
    sig.hi = sig.hi | 0x100000UL;
    if (e >= 52)
        u64_shl(&sig, &sig, e - 52);
    else
        u64_shr(&sig, &sig, 52 - e);
    if (sign) {
        u64_set(&z, 0, 0);
        u64_sub(&sig, &z, &sig);
    }
    *hi = sig.hi;
    *lo = sig.lo;
}

/* Rounded once, from the integer: its leading 1 to bit 30, what is below
 * bit 0 kept as a sticky bit.
 *
 * Going through binary64 would round twice, to 53 bits and then to 24,
 * which can differ from rounding once (a value just past a binary32
 * halfway point can first round onto it, then tie to even). The exponent
 * 126 + top is the biased exponent (127 + top) less one. */
unsigned long sf32_from_long64(unsigned long hi, unsigned long lo, int is_signed)
{
    int sign;
    int top;
    struct u64 m;

    sign = magnitude64(&m, hi, lo, is_signed);
    if (u64_zero(&m))
        return 0;
    top = 63 - u64_clz(&m);
    if (top > 30)
        u64_shr_jam(&m, &m, top - 30);
    else
        u64_shl(&m, &m, 30 - top);
    return round_pack32(sign, 126 + top, m.lo);
}

void sf32_to_long64(unsigned long a, int is_signed, unsigned long *hi, unsigned long *lo)
{
    struct sf64 x;

    sf64_from_f32(&x, a);
    sf64_to_long64(&x, is_signed, hi, lo);
}

/* ---- decimal conversion ------------------------------------------------------------
 *
 * Exact, with big integers: a decimal value digits * 10^exp10 is the
 * fraction N / M of two big integers, whose quotient, to a few bits more
 * than the precision, and remainder (nonzero or not) round correctly; and
 * a double m * 2^e is the integer m * 2^e or m * 5^-e / 10^-e, whose
 * decimal digits are all exact. A big integer is 16-bit limbs in unsigned
 * longs, so a limb times a limb, plus a carry, fits in 32 bits.
 *
 * Slow but simple: every digit takes part and nothing is approximated,
 * so the result is right by construction. The limbs are little-endian
 * (d[0] the lowest) and every operation trims zero limbs off the top.
 */

#define BIG_LIMBS 300                   /* 4,800 bits: 10^1151 and 5^1074 times 2^60 fit */
#define DIG_MAX 800                     /* more digits than binary64 ever needs to round */

struct big {
    int n;                              /* limbs in use; d[n - 1] != 0 (n = 0 for zero) */
    unsigned long d[BIG_LIMBS];
};

/* N and M, shared by the conversions (and so sf64_to_decimal is not
 * reentrant); exact_digits uses big_n alone. */
static struct big big_n;
static struct big big_m;

/* b = v. */
static void big_small(struct big *b, unsigned long v)
{
    b->n = 0;
    while (v) {
        b->d[b->n] = v & 0xFFFF;
        b->n++;
        v = v >> 16;
    }
}

/* b = b * m + add, m and add below 2^16
 *
 * Each step is below (2^16 - 1)^2 + 2^16 < 2^32. A carry past BIG_LIMBS
 * would be dropped; the limit is set so that no conversion gets there. */
static void big_mul_add(struct big *b, unsigned long m, unsigned long add)
{
    int i;
    unsigned long t;
    unsigned long carry;

    carry = add;
    for (i = 0; i < b->n; i++) {
        t = b->d[i] * m + carry;
        b->d[i] = t & 0xFFFF;
        carry = t >> 16;
    }
    if (carry && b->n < BIG_LIMBS) {
        b->d[b->n] = carry;
        b->n++;
    }
}

/* b = b / d (d below 2^16); returns the remainder */
static unsigned long big_div_small(struct big *b, unsigned long d)
{
    int i;
    unsigned long t;
    unsigned long rem;

    rem = 0;
    for (i = b->n - 1; i >= 0; i--) {
        t = rem << 16 | b->d[i];
        b->d[i] = t / d;
        rem = t % d;
    }
    while (b->n > 0 && b->d[b->n - 1] == 0)
        b->n--;
    return rem;
}

/* The bit length: 2^(k - 1) <= b < 2^k, 0 for zero. */
static int big_bits(const struct big *b)
{
    int k;
    unsigned long top;

    if (b->n == 0)
        return 0;
    top = b->d[b->n - 1];
    k = 0;
    while (top) {
        k++;
        top = top >> 1;
    }
    return (b->n - 1) * 16 + k;
}

/* b <<= bits: the bits within a limb first (a new top limb for what
 * comes out), then whole limbs. */
static void big_shl(struct big *b, int bits)
{
    int limbs;
    int i;
    int s;

    if (b->n == 0 || bits <= 0)
        return;
    limbs = bits / 16;
    s = bits % 16;
    if (s) {
        b->d[b->n] = 0;
        for (i = b->n; i > 0; i--)
            b->d[i] = (b->d[i] << s | b->d[i - 1] >> (16 - s)) & 0xFFFF;
        b->d[0] = b->d[0] << s & 0xFFFF;
        b->n++;
    }
    if (limbs) {
        for (i = b->n - 1; i >= 0; i--)
            b->d[i + limbs] = b->d[i];
        for (i = 0; i < limbs; i++)
            b->d[i] = 0;
        b->n = b->n + limbs;
    }
    while (b->n > 0 && b->d[b->n - 1] == 0)
        b->n--;
}

/* b >>= 1. */
static void big_shr1(struct big *b)
{
    int i;

    for (i = 0; i < b->n; i++)
        b->d[i] = (b->d[i] >> 1 | (i + 1 < b->n ? b->d[i + 1] << 15 : 0)) & 0xFFFF;
    while (b->n > 0 && b->d[b->n - 1] == 0)
        b->n--;
}

/* -1, 0 or 1 as a <, = or > b; with no zero limbs at the top, more limbs
 * means larger. */
static int big_cmp(const struct big *a, const struct big *b)
{
    int i;

    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i])
            return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* a -= b, a >= b
 *
 * A borrow shows as bit 16 of the 32-bit difference, which wraps when
 * negative. */
static void big_sub(struct big *a, const struct big *b)
{
    int i;
    unsigned long borrow;
    unsigned long t;

    borrow = 0;
    for (i = 0; i < a->n; i++) {
        t = M32(a->d[i] - (i < b->n ? b->d[i] : 0) - borrow);
        borrow = t >> 16 & 1;
        a->d[i] = t & 0xFFFF;
    }
    while (a->n > 0 && a->d[a->n - 1] == 0)
        a->n--;
}

/* b * 10^k, 10^4 at a time */
static void big_pow10(struct big *b, long k)
{
    while (k >= 4) {
        big_mul_add(b, 10000, 0);
        k = k - 4;
    }
    while (k > 0) {
        big_mul_add(b, 10, 0);
        k--;
    }
}

/* digits * 10^exp10 to nearest with p bits of precision, normal exponents
 * emin .. emax: the biased exponent field (all ones for infinity) and the
 * fraction field.
 *
 * The steps: drop leading and trailing zeros; keep at most DIG_MAX
 * digits, an extra nonzero digit standing for any nonzero ones dropped
 * (a sticky digit: it can only matter by being nonzero); settle values
 * certainly out of range; make the value exactly N / M; scale one of
 * them by 2^s so that the integer quotient q has p + 3 or p + 4 bits (or,
 * for a value near or below the smallest normal, e0 <= emin, s fixed by
 * emin so that q's units sit 4 bits below the smallest subnormal's);
 * divide bit by bit, restoring division on big integers, the remainder's
 * being nonzero the sticky bit; round q's low 3 or 4 bits to nearest
 * even; then infinity, subnormal or normal. Called with p = 53, emin = -1022, emax = 1023 for
 * binary64 and 24, -126, 127 for binary32. */
static int dec_to_bin(const char *digits, int n, long exp10, int p, int emin, int emax, struct u64 *frac)
{
    int i;
    int sticky;
    int e0;
    int e;
    int s;
    int t;
    int extra;
    long top;
    struct u64 q;
    struct u64 half;
    struct u64 rb;
    struct u64 one;
    struct u64 mask;

    u64_set(frac, 0, 0);
    while (n > 0 && *digits == '0') {
        digits++;
        n--;
    }
    while (n > 0 && digits[n - 1] == '0') {
        n--;
        exp10++;
    }
    if (n == 0)
        return 0;
    sticky = 0;
    if (n > DIG_MAX) {
        for (i = DIG_MAX; i < n; i++)
            if (digits[i] != '0')
                sticky = 1;
        exp10 = exp10 + (n - DIG_MAX);
        n = DIG_MAX;
    }
    top = n + exp10;                    /* the value is below 10^top, at least 10^(top - 1) */
    if (top > 330)
        return (1 << (p == 53 ? 11 : 8)) - 1;           /* certainly beyond the range: infinity */
    if (top < -350)
        return 0;                                       /* certainly below it: zero */
    big_small(&big_n, 0);
    for (i = 0; i < n; i++)
        big_mul_add(&big_n, 10, (unsigned long)(digits[i] - '0'));
    if (sticky) {
        big_mul_add(&big_n, 10, 1);     /* a digit more, nonzero, stands for those dropped */
        exp10--;
    }
    big_small(&big_m, 1);
    if (exp10 >= 0)
        big_pow10(&big_n, exp10);
    else
        big_pow10(&big_m, -exp10);
    /* value = N / M, 2^(e0 - 1) <= value < 2^(e0 + 1); q = value * 2^s to
     * p + 3 or p + 4 bits */
    e0 = big_bits(&big_n) - big_bits(&big_m);
    s = p + 3 - (e0 > emin ? e0 : emin);
    if (s >= 0)
        big_shl(&big_n, s);
    else
        big_shl(&big_m, -s);
    u64_set(&q, 0, 0);
    t = big_bits(&big_n) - big_bits(&big_m);
    if (t >= 0) {
        big_shl(&big_m, t);
        for (i = 0; i <= t; i++) {
            u64_shl(&q, &q, 1);
            if (big_cmp(&big_n, &big_m) >= 0) {
                big_sub(&big_n, &big_m);
                q.lo = q.lo | 1;
            }
            if (i < t)
                big_shr1(&big_m);
        }
    }
    sticky = big_n.n != 0;
    /* the exponent: q's length says whether value >= 2^e0 (when e0 >
     * emin, e is e0 or e0 - 1, never below emin) */
    if (e0 > emin) {
        e = u64_clz(&q) == 64 - (p + 4) ? e0 : e0 - 1;
        if (e < emin)
            e = emin;
    } else {
        e = emin;
    }
    extra = 4 - (e0 > emin ? e0 : emin) + e;            /* 3 or 4 bits below the precision */
    u64_set(&one, 0, 1);
    u64_shl(&half, &one, extra - 1);
    u64_shl(&mask, &one, extra);
    u64_sub(&mask, &mask, &one);
    rb.hi = q.hi & mask.hi;
    rb.lo = q.lo & mask.lo;
    u64_shr(frac, &q, extra);
    /* round to nearest even: up if the dropped bits rb are over half, or
     * exactly half with a nonzero remainder below them or an odd result */
    if (u64_lt(&half, &rb) || (rb.hi == half.hi && rb.lo == half.lo && (sticky || (frac->lo & 1))))
        u64_add(frac, frac, &one);
    if (u64_clz(frac) == 64 - (p + 1)) {                /* rounded up to 2^p */
        u64_shr(frac, frac, 1);
        e++;
    }
    if (e > emax) {
        u64_set(frac, 0, 0);
        return (1 << (p == 53 ? 11 : 8)) - 1;
    }
    if (u64_clz(frac) > 64 - p)
        return 0;                                       /* subnormal */
    /* the leading 1 is implicit */
    u64_shl(&mask, &one, p - 1);
    u64_sub(frac, frac, &mask);
    return e - emin + 1;
}

void sf64_from_decimal(struct sf64 *r, int sign, const char *digits, int n, long exp10)
{
    struct u64 frac;
    int ex;

    ex = dec_to_bin(digits, n, exp10, 53, -1022, 1023, &frac);
    r->hi = ((unsigned long)sign << 31) | (unsigned long)ex << 20 | frac.hi;
    r->lo = frac.lo;
}

unsigned long sf32_from_decimal(int sign, const char *digits, int n, long exp10)
{
    struct u64 frac;
    int ex;

    ex = dec_to_bin(digits, n, exp10, 24, -126, 127, &frac);
    return ((unsigned long)sign << 31) | (unsigned long)ex << 23 | frac.lo;
}

/* All the decimal digits of a finite, nonzero |a| into buf (no trailing
 * zeros), their count returned; |a| = d1.d2d3... * 10^*dexp.
 *
 * |a| = m * 2^e2 with m made odd, which keeps -e2, and so the work, as
 * small as possible. For e2 >= 0 the value is the integer m * 2^e2; for
 * e2 < 0 it is m * 5^k / 10^k (k = -e2), so the integer m * 5^k with the
 * decimal point k digits from the right: every binary fraction has a
 * finite decimal expansion. 5^k goes in as 5^6 = 15625 at a time, the
 * largest power of 5 below 2^16. The digits come out four at a time,
 * lowest first, by division by 10^4, and are then reversed. */
static int exact_digits(const struct sf64 *a, char *buf, int *dexp)
{
    int e2;
    int n;
    int i;
    int k;
    unsigned long rem;
    struct u64 m;
    char c;

    frac64(&m, a);
    e2 = EXP64(a);
    if (e2)
        m.hi = m.hi | 0x100000UL;
    else
        e2 = 1;
    e2 = e2 - 1075;                     /* |a| = m * 2^e2 */
    while (!(m.lo & 1)) {
        u64_shr(&m, &m, 1);
        e2++;
    }
    big_n.d[0] = m.lo & 0xFFFF;
    big_n.d[1] = m.lo >> 16;
    big_n.d[2] = m.hi & 0xFFFF;
    big_n.d[3] = m.hi >> 16;
    big_n.n = 4;
    while (big_n.n > 0 && big_n.d[big_n.n - 1] == 0)
        big_n.n--;
    k = 0;                              /* digits after the decimal point */
    if (e2 >= 0) {
        big_shl(&big_n, e2);
    } else {
        k = -e2;                        /* m * 2^e2 = m * 5^-e2 / 10^-e2 */
        for (i = k; i >= 6; i = i - 6)
            big_mul_add(&big_n, 15625, 0);
        for (; i > 0; i--)
            big_mul_add(&big_n, 5, 0);
    }
    n = 0;
    while (big_n.n > 0) {
        rem = big_div_small(&big_n, 10000);
        for (i = 0; i < 4; i++) {
            buf[n] = (char)('0' + rem % 10);
            rem = rem / 10;
            n++;
        }
    }
    while (n > 0 && buf[n - 1] == '0')  /* the leading zeros, written last */
        n--;
    for (i = 0; i < n / 2; i++) {
        c = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = c;
    }
    *dexp = n - 1 - k;
    while (n > 1 && buf[n - 1] == '0')
        n--;
    return n;
}

/* The digits of a finite |a|, correctly rounded (to nearest, ties to even)
 * for printf: mode 'e', prec + 1 significant digits; mode 'f', down to
 * 10^-prec. |a| ~ d1.d2d3... * 10^*dexp; the count of digits is returned
 * (the caller supplies the zeros after them), 0 if the value is 0 or
 * rounds to it. buf needs 800 bytes. */
int sf64_to_decimal(const struct sf64 *a, int mode, int prec, char *buf, int *dexp)
{
    int n;
    int keep;
    int i;
    int up;

    *dexp = 0;
    if ((a->hi & 0x7FFFFFFFUL) == 0 && a->lo == 0)
        return 0;
    n = exact_digits(a, buf, dexp);
    /* the digits to keep: in 'f' digit i is worth 10^(*dexp - i), so
     * those down to 10^-prec number *dexp + 1 + prec; fewer than 0 means
     * the value is below half of 10^-prec */
    keep = mode == 'e' ? prec + 1 : *dexp + 1 + prec;
    if (keep >= n)
        return n;
    if (keep < 0)
        return 0;
    /* round at digit keep: up if what follows is more than half, or half
     * with the last kept digit odd */
    if (buf[keep] > '5')
        up = 1;
    else if (buf[keep] < '5')
        up = 0;
    else if (keep + 1 < n)
        up = 1;                         /* 5 and more nonzero digits (no trailing zeros) */
    else
        up = keep > 0 ? (buf[keep - 1] - '0') & 1 : 0;
    n = keep;
    if (up) {
        i = n - 1;
        while (i >= 0 && buf[i] == '9') {
            buf[i] = '0';
            i--;
        }
        if (i >= 0) {
            buf[i]++;
        } else {                        /* 9...9 became 10...0 */
            buf[0] = '1';
            for (i = 1; i < n; i++)
                buf[i] = '0';
            if (n == 0)
                n = 1;
            (*dexp)++;
        }
    }
    while (n > 0 && buf[n - 1] == '0')
        n--;
    return n;
}
