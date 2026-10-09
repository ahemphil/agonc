/* math.c - <math.h> (C89 4.5).
 *
 * The algorithms are those of fdlibm, Sun's freely distributable libm
 * (Copyright (C) 1993 by Sun Microsystems, Inc.; "Permission to use,
 * copy, modify, and distribute this software is freely granted, provided
 * that this notice is preserved."), rewritten for a target whose int is
 * 24 bits: a double's words are 32-bit u32s, and fdlibm's int arithmetic
 * on them is s32. Every result is within about one unit in the last place
 * (the test compares millions against the PC's libm), and, the arithmetic
 * being IEEE 754's, the same bits on the Agon as on the PC.
 *
 * Huge arguments of sin, cos and tan are reduced by pi/2 with the product
 * of their integer significand and 2/pi's bits (Payne and Hanek), in
 * 16-bit limbs. sqrt is softfp.c's, correctly rounded. Errors set errno
 * as C89 says: EDOM for an argument outside the domain (with a NaN), and
 * ERANGE when the result overflows (HUGE_VAL) or underflows to zero.
 *
 * Where it sits: an ordinary library unit in C. On the Agon every + - * /
 * on a double here compiles to a call of fp.c's soft-float helpers. As in
 * fdlibm, tests and exact steps work on a double's bits instead (its two
 * 32-bit words, through union dw): an exponent is a field of the high
 * word, and comparing |x|'s high word with a constant compares
 * magnitudes to about 20 significant bits.
 *
 * The method behind nearly every function, for a learner:
 *   1. Special cases first: NaN, infinities, zeros, tiny and huge
 *      arguments, each decided from the high word's exponent bits.
 *   2. Argument reduction: rewrite the argument into a small interval
 *      where a short polynomial is accurate, by an identity that is
 *      exact or nearly so. exp uses x = k ln2 + r (so e^x = 2^k e^r);
 *      log uses x = 2^k (1 + f); sin, cos and tan use x = n pi/2 + r;
 *      atan uses atan(x) = atan(c) + atan((x - c) / (1 + x c)).
 *   3. Polynomial (or rational) approximation on the small interval:
 *      fixed coefficients, chosen by fdlibm to minimise the worst error
 *      there (minimax), evaluated by Horner's rule (a + t (b + t (c ...))),
 *      one multiply and one add per coefficient.
 *   4. Reconstruction: undo the reduction, for exp by adding k straight
 *      into the result's exponent field.
 * Constants such as ln2 and pi/2 are split into a high part with
 * trailing zero bits and a low part (ln2HI and ln2LO, pio2_1 and
 * pio2_1t): k times the high part is then exact, and the low part
 * corrects for the rest, giving more precision than one double holds.
 * chop clears a value's low word for the same purpose, so that a product
 * of two chopped values is exact.
 *
 * Map: helpers; the exact operations (fabs, floor, ceil, modf, frexp,
 * ldexp, fmod, sqrt); exponentials and logarithms (exp, log, log10, and
 * __expm1 for sinh, cosh, tanh, and math99.c's expm1); pow; the trigonometric kernels k_sin,
 * k_cos, k_tan and the reductions rem_pio2 and rem_pio2_big, then sin,
 * cos, tan; atan, atan2, asin, acos.
 */

#include <math.h>
#include <errno.h>
#include <limits.h>

/* u32 and s32: exactly 32 bits, so that the word arithmetic is the same
 * on the target (32-bit long, 24-bit int) and on a host whose long is
 * 64 bits */
#if ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long u32;
typedef long s32;
#else
typedef unsigned int u32;
typedef int s32;
#endif

/* a double's words: w[0] the low one, w[1] the high one (sign, exponent,
 * the fraction's top 20 bits). The high word's layout: bit 31 the sign,
 * bits 20..30 the exponent biased by 1023 (0x3FF), bits 0..19 the top of
 * the fraction. So h & 0x7FFFFFFF is |x|'s high word, 0x7FF00000 an
 * infinity or NaN, 0x3FF00000 is 1.0, and adding k << 20 multiplies by
 * 2^k while the exponent stays in range. hiw, low and mk read and build
 * a double from its words. */
union dw {
    double d;
    u32 w[2];
};

static u32 hiw(double x)
{
    union dw u;

    u.d = x;
    return u.w[1];
}

static u32 low(double x)
{
    union dw u;

    u.d = x;
    return u.w[0];
}

static double mk(u32 h, u32 l)
{
    union dw u;

    u.w[0] = l;
    u.w[1] = h;
    return u.d;
}

/* x with its low word cleared: 21 significant bits, so the product of
 * two chopped values is exact */
static double chop(double x)
{
    return mk(hiw(x), 0);
}

/* two54 scales a subnormal up into the normal range (and twom54 back),
 * so that its exponent can be read from the high word */
static double one = 1.0;
static double tiny = 1.0e-300;
static double two54 = 1.80143985094819840000e+16;       /* 0x43500000, 0x00000000 */
static double twom54 = 5.55111512312578270212e-17;      /* 0x3C900000, 0x00000000 */

/* The error results: a domain error gives the default quiet NaN, a range
 * error a signed HUGE_VAL (an infinity) or a signed zero. */
static double nan_result(void)
{
    errno = EDOM;
    return mk(0x7FF80000UL, 0);
}

static double overflow(int neg)
{
    errno = ERANGE;
    return neg ? -HUGE_VAL : HUGE_VAL;
}

static double underflow(int neg)
{
    errno = ERANGE;
    return neg ? -0.0 : 0.0;
}

/* ---- exact operations ------------------------------------------------------ */

double fabs(double x)
{
    return mk(hiw(x) & 0x7FFFFFFFUL, low(x));
}

/* x toward zero to an integer (x finite). With unbiased exponent e, the
 * fraction bits worth less than 1 are the 52 - e lowest: the mask m
 * clears them, in the high word alone when e < 20 (and the low word
 * whole), otherwise in the low word. No arithmetic, so it is exact. */
double __trunc(double x)
{
    u32 h;
    u32 m;
    s32 e;

    h = hiw(x);
    e = (s32)((h >> 20) & 0x7FF) - 0x3FF;
    if (e < 0)
        return mk(h & 0x80000000UL, 0);                 /* |x| < 1: a signed zero */
    if (e < 20) {
        m = 0x000FFFFFUL >> e;
        return mk(h & ~m, 0);
    }
    if (e < 52) {
        m = 0xFFFFFFFFUL >> (e - 20);
        return mk(h, low(x) & ~m);
    }
    return x;                                           /* integral already, or not finite */
}

/* floor and ceil: __trunc, then one step down (or up) when it moved a
 * non-integer the wrong way. x + x returns a NaN quiet and an infinity
 * unchanged. */
double floor(double x)
{
    double t;

    if ((hiw(x) & 0x7FF00000UL) == 0x7FF00000UL)
        return x + x;
    t = __trunc(x);
    if (t != x && x < 0)
        t = t - 1.0;
    return t;
}

double ceil(double x)
{
    double t;

    if ((hiw(x) & 0x7FF00000UL) == 0x7FF00000UL)
        return x + x;
    t = __trunc(x);
    if (t != x && x > 0)
        t = t + 1.0;
    return t;
}

/* The integer part to *ip, the fraction returned; x - *ip is exact. The
 * fraction takes x's sign even when it is zero (modf(-3.0) is -0.0). */
double modf(double x, double *ip)
{
    u32 h;

    h = hiw(x);
    if ((h & 0x7FF00000UL) == 0x7FF00000UL) {
        *ip = x;
        if ((h & 0xFFFFFUL) != 0 || low(x) != 0)
            return x + x;                               /* NaN */
        return mk(h & 0x80000000UL, 0);                 /* an infinity: a signed zero */
    }
    *ip = __trunc(x);
    return mk((hiw(x - *ip) & 0x7FFFFFFFUL) | (h & 0x80000000UL), low(x - *ip));
}

/* x = m 2^*e with 0.5 <= |m| < 1: *e from the exponent field, m by
 * setting the field to 0x3FE (2^-1). A subnormal is scaled by 2^54 first
 * so that it has an exponent field to read. */
double frexp(double x, int *e)
{
    u32 h;
    s32 k;

    *e = 0;
    h = hiw(x);
    if ((h & 0x7FF00000UL) == 0x7FF00000UL || ((h & 0x7FFFFFFFUL) | low(x)) == 0)
        return x;                                       /* 0, an infinity or a NaN */
    k = 0;
    if ((h & 0x7FF00000UL) == 0) {
        x = x * two54;                                  /* subnormal */
        h = hiw(x);
        k = -54;
    }
    *e = (int)(k + (s32)((h >> 20) & 0x7FF) - 1022);
    return mk((h & 0x800FFFFFUL) | 0x3FE00000UL, low(x));
}

/* x 2^n by adding n to the exponent field: exact unless the result
 * leaves the normal range. Any |n| beyond about 2100 overflows or
 * underflows whatever x is, so clamping n to +-50000 changes no result.
 * A subnormal result is built 2^54 too large and scaled down by one
 * multiply, which rounds it correctly. */
double ldexp(double x, int n)
{
    u32 h;
    s32 k;

    h = hiw(x);
    k = (s32)((h >> 20) & 0x7FF);
    if (k == 0) {
        if (((h & 0x7FFFFFFFUL) | low(x)) == 0)
            return x;                                   /* a zero */
        x = x * two54;                                  /* subnormal */
        h = hiw(x);
        k = (s32)((h >> 20) & 0x7FF) - 54;
    }
    if (k == 0x7FF)
        return x + x;                                   /* an infinity or a NaN */
    if (n > 50000)
        n = 50000;
    if (n < -50000)
        n = -50000;
    k = k + n;
    if (k > 0x7FE)
        return overflow(h >> 31);
    if (k > 0)
        return mk((h & 0x800FFFFFUL) | (u32)k << 20, low(x));
    if (k <= -54)
        return underflow(h >> 31);
    x = mk((h & 0x800FFFFFUL) | (u32)(k + 54) << 20, low(x)) * twom54;
    if (x == 0)
        errno = ERANGE;
    return x;
}

/* x - n y for the integer n that leaves the result's sign x's and its
 * size below |y|: exact. Each step takes away the largest y 2^k not above
 * the remainder, which is exact too (Sterbenz). That is binary long
 * division on the magnitudes, one quotient bit per step: frexp gives the
 * exponents, ldexp lines |y| up under the remainder (exactly, as it only
 * changes the exponent), and Sterbenz's lemma says a - b is exact when b
 * lies between a/2 and a. */
double fmod(double x, double y)
{
    double r;
    double ay;
    double t;
    int ex;
    int ey;

    if ((hiw(x) & 0x7FF00000UL) == 0x7FF00000UL || y != y) {
        if (x != x || y != y)
            return x + y;
        return nan_result();                            /* an infinity */
    }
    if (y == 0)
        return nan_result();
    r = fabs(x);
    ay = fabs(y);
    if (r < ay)
        return x;
    frexp(ay, &ey);
    while (r >= ay) {
        frexp(r, &ex);
        t = ldexp(ay, ex - ey);
        if (t > r)
            t = ldexp(ay, ex - ey - 1);
        r = r - t;
    }
    return x < 0 ? -r : r;
}

/* sqrt, from softfp.c (fp.c), correctly rounded. -0 is not below 0, so
 * sqrt(-0) is -0, as IEEE 754 has it, and a NaN passes through. */
struct sf64;
void __sf64_sqrt(struct sf64 *r, const struct sf64 *a);

double sqrt(double x)
{
    double r;

    if (x < 0)
        return nan_result();
    __sf64_sqrt((struct sf64 *)&r, (struct sf64 *)&x);
    return r;
}

/* ---- exponentials and logarithms ---------------------------------------- */

/* exp's constants: the overflow and underflow thresholds, ln2 split in
 * two (the high part with trailing zeros so that k ln2HI is exact for
 * any k exp meets), indexed by the sign of x, and the coefficients P1 to
 * P5 of the polynomial (pow uses them too). */
static double halF[2] = { 0.5, -0.5 };
static double twom1000 = 9.33263618503218878990e-302;  /* 2^-1000: 0x01700000, 0 */
static double o_threshold = 7.09782712893383973096e+02; /* 0x40862E42, 0xFEFA39EF */
static double u_threshold = -7.45133219101941108420e+02;        /* 0xC0874910, 0xD52D3051 */
static double ln2HI[2] = { 6.93147180369123816490e-01, -6.93147180369123816490e-01 };  /* 0x3FE62E42, 0xFEE00000 */
static double ln2LO[2] = { 1.90821492927058770002e-10, -1.90821492927058770002e-10 };  /* 0x3DEA39EF, 0x35793C76 */
static double invln2 = 1.44269504088896338700e+00;      /* 0x3FF71547, 0x652B82FE */
static double P1 = 1.66666666666666019037e-01;          /* 0x3FC55555, 0x5555553E */
static double P2 = -2.77777777770155933842e-03;         /* 0xBF66C16C, 0x16BEBD93 */
static double P3 = 6.61375632143793436117e-05;          /* 0x3F11566A, 0xAF25DE2C */
static double P4 = -1.65339022054652515390e-06;         /* 0xBEBBBD41, 0xC5D26BF1 */
static double P5 = 4.13813679705723846039e-08;          /* 0x3E663769, 0x72BEA4D0 */

/* e^x. Reduction: x = k ln2 + r with |r| <= ln2/2, so e^x = 2^k e^r;
 * k is x/ln2 rounded to nearest (adding +-0.5 before the conversion,
 * which truncates), and r is kept as hi - lo, the exact x - k ln2HI less
 * the small k ln2LO. Approximation: with c = r - r^2 P(r^2), e^r is
 * 1 + r + r c / (2 - c), a rational form in which the polynomial needs
 * only five terms for full precision. Reconstruction: k goes straight
 * into the result's exponent field. When 2^k is below the normal range
 * the result is built 2^1000 too large and scaled down by one multiply,
 * which rounds it into a subnormal correctly. */
double exp(double x)
{
    double y;
    double hi;
    double lo;
    double c;
    double t;
    s32 k;
    int xsb;
    u32 hx;

    hi = 0;
    lo = 0;
    k = 0;
    hx = hiw(x);
    xsb = (int)(hx >> 31);
    hx = hx & 0x7FFFFFFFUL;
    if (hx >= 0x40862E42UL) {                           /* |x| >= 709.78... */
        if (hx >= 0x7FF00000UL) {
            if (((hx & 0xFFFFFUL) | low(x)) != 0)
                return x + x;                           /* NaN */
            return xsb == 0 ? x : 0.0;                  /* exp(+-inf) = inf, 0 */
        }
        if (x > o_threshold)
            return overflow(0);
        if (x < u_threshold)
            return underflow(0);
    }
    if (hx > 0x3FD62E42UL) {                            /* |x| > 0.5 ln2 */
        if (hx < 0x3FF0A2B2UL) {                        /* and |x| < 1.5 ln2 */
            hi = x - ln2HI[xsb];
            lo = ln2LO[xsb];
            k = 1 - xsb - xsb;                          /* +1 or -1 */
        } else {
            k = (s32)(invln2 * x + halF[xsb]);
            t = k;
            hi = x - t * ln2HI[0];                      /* exact */
            lo = t * ln2LO[0];
        }
        x = hi - lo;                                    /* r */
    } else if (hx < 0x3E300000UL) {                     /* |x| < 2^-28 */
        return one + x;
    }
    t = x * x;
    c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0)
        return one - ((x * c) / (c - 2.0) - x);         /* no reduction */
    /* e^r, with r's two parts hi and lo kept apart for accuracy */
    y = one - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021)
        return mk(hiw(y) + ((u32)k << 20), low(y));    /* y 2^k */
    y = mk(hiw(y) + ((u32)(k + 1000) << 20), low(y));
    y = y * twom1000;
    if (y == 0)
        errno = ERANGE;
    return y;
}

static double ln2_hi = 6.93147180369123816490e-01;     /* 0x3FE62E42, 0xFEE00000 */
static double ln2_lo = 1.90821492927058770002e-10;     /* 0x3DEA39EF, 0x35793C76 */
static double Lg1 = 6.666666666666735130e-01;           /* 0x3FE55555, 0x55555593 */
static double Lg2 = 3.999999999940941908e-01;           /* 0x3FD99999, 0x9997FA04 */
static double Lg3 = 2.857142874366239149e-01;           /* 0x3FD24924, 0x94229359 */
static double Lg4 = 2.222219843214978396e-01;           /* 0x3FCC71C5, 0x1D8E78AF */
static double Lg5 = 1.818357216161805012e-01;           /* 0x3FC74664, 0x96CB03DE */
static double Lg6 = 1.531383769920937332e-01;           /* 0x3FC39A09, 0xD078C69F */
static double Lg7 = 1.479819860511658591e-01;           /* 0x3FC2F112, 0xDF3E5244 */

/* Natural logarithm. Reduction: x = 2^k (1 + f) with 1 + f between
 * sqrt(2)/2 and sqrt(2), so log x = k ln2 + log(1 + f) and |f| < 0.42.
 * The bit trick: adding 0x95F64 to the high word's fraction carries into
 * bit 20 exactly when the fraction is at least 0x6A09C (1 + f >= sqrt 2);
 * that bit, i, then both lowers the stored exponent by one (0x3FF ^ 1 is
 * 0x3FE, halving x) and adds one to k. Approximation: with s = f/(2 + f),
 * log(1 + f) = 2s + 2s^3/3 + 2s^5/5 + ..., an odd series in s that a
 * polynomial R in z = s^2 replaces (its even- and odd-numbered
 * coefficients in t1 and t2, each a Horner chain in w = z^2). Several
 * algebraically equal final forms are used, each where its rounding
 * error is smallest: |f| < 2^-20 by a short series, and the hfsq = f^2/2
 * form when the fraction field lies in 0x6147A .. 0x6B851 (1 + f near
 * either end of its interval, where |f| is largest; i | j > 0 tests both
 * bounds at once, as either difference negative sets the sign bit). ln2
 * is split as in exp, so k ln2_hi is exact. */
double log(double x)
{
    double hfsq;
    double f;
    double s;
    double z;
    double R;
    double w;
    double t1;
    double t2;
    double dk;
    s32 k;
    s32 hx;
    s32 i;
    s32 j;
    u32 lx;

    hx = (s32)hiw(x);
    lx = low(x);
    k = 0;
    if (hx < 0x00100000L) {                             /* x < 2^-1022 */
        if (((hx & 0x7FFFFFFFL) | lx) == 0)
            return overflow(1);                         /* log(+-0) = -inf */
        if (hx < 0)
            return x != x ? x : nan_result();           /* log(-x) = NaN */
        k = k - 54;
        x = x * two54;                                  /* subnormal */
        hx = (s32)hiw(x);
    }
    if (hx >= 0x7FF00000L)
        return x + x;
    k = k + (hx >> 20) - 1023;
    hx = hx & 0x000FFFFFL;
    i = (hx + 0x95F64L) & 0x100000L;
    x = mk((u32)(hx | (i ^ 0x3FF00000L)), low(x));     /* x or x/2 into [sqrt(2)/2, sqrt(2)] */
    k = k + (i >> 20);
    f = x - 1.0;
    if ((0x000FFFFFL & (2 + hx)) < 3) {                 /* |f| < 2^-20 */
        if (f == 0) {
            if (k == 0)
                return 0.0;
            dk = (double)k;
            return dk * ln2_hi + dk * ln2_lo;
        }
        R = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0)
            return f - R;
        dk = (double)k;
        return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }
    s = f / (2.0 + f);
    dk = (double)k;
    z = s * s;
    i = hx - 0x6147AL;
    w = z * z;
    j = 0x6B851L - hx;
    t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i = i | j;
    R = t2 + t1;
    if (i > 0) {
        hfsq = 0.5 * f * f;
        if (k == 0)
            return f - (hfsq - s * (hfsq + R));
        return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    }
    if (k == 0)
        return f - s * (f - R);
    return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

static double ivln10 = 4.34294481903251816668e-01;     /* 0x3FDBCB7B, 0x1526E50E */
static double log10_2hi = 3.01029995663611771306e-01;  /* 0x3FD34413, 0x509F6000 */
static double log10_2lo = 3.69423907715893078616e-13;  /* 0x3D59FEF3, 0x11F12B36 */

/* log10 x = k log10(2) + log10(m) with x = 2^k m, log10(m) being
 * log(m) / ln 10 (ivln10 = 1/ln 10); log10(2) is split in two as ln2 is
 * in exp. For x < 1 (k < 0), m is taken in [0.5, 1) and k one larger,
 * so that both terms are negative and no cancellation occurs. */
double log10(double x)
{
    double y;
    double z;
    s32 i;
    s32 k;
    s32 hx;
    u32 lx;

    hx = (s32)hiw(x);
    lx = low(x);
    k = 0;
    if (hx < 0x00100000L) {                             /* x < 2^-1022 */
        if (((hx & 0x7FFFFFFFL) | lx) == 0)
            return overflow(1);
        if (hx < 0)
            return x != x ? x : nan_result();
        k = k - 54;
        x = x * two54;
        hx = (s32)hiw(x);
    }
    if (hx >= 0x7FF00000L)
        return x + x;
    k = k + (hx >> 20) - 1023;
    i = k < 0;
    hx = (hx & 0x000FFFFFL) | ((0x3FFL - i) << 20);
    y = (double)(k + i);
    x = mk((u32)hx, low(x));
    z = y * log10_2lo + ivln10 * log(x);
    return z + y * log10_2hi;
}

/* e^x - 1, accurately for small x (fdlibm's expm1), for sinh, cosh and
 * tanh. exp(x) - 1 would lose most of its digits to cancellation for x
 * near 0. The same reduction as exp (x = k ln2 + r), with c the rounding
 * error of r = hi - lo carried separately; e^r - 1 from a rational form
 * built on r^2/2 (hxs; r1 the polynomial, Q1 to Q5); then
 * e^x - 1 = 2^k (e^r - 1) + 2^k - 1,
 * arranged for each range of k so that nothing large cancels. C89 has
 * no expm1, so it has a reserved name here: math99.c's expm1 is C99's. */
static double Q1 = -3.33333333333331316428e-02;        /* 0xBFA11111, 0x111110F4 */
static double Q2 = 1.58730158725481460165e-03;         /* 0x3F5A01A0, 0x19FE5585 */
static double Q3 = -7.93650757867487942473e-05;        /* 0xBF14CE19, 0x9EAADBB7 */
static double Q4 = 4.00821782732936239552e-06;         /* 0x3ED0CFCA, 0x86E65239 */
static double Q5 = -2.01099218183624371326e-07;        /* 0xBE8AFDB7, 0x6E09C32D */

double __expm1(double x)
{
    double y;
    double hi;
    double lo;
    double c;
    double t;
    double e;
    double hxs;
    double hfx;
    double r1;
    s32 k;
    int xsb;
    u32 hx;

    c = 0;
    hx = hiw(x);
    xsb = (int)(hx >> 31);
    hx = hx & 0x7FFFFFFFUL;
    if (hx >= 0x4043687AUL) {                           /* |x| >= 56 ln2 */
        if (hx >= 0x40862E42UL) {                       /* |x| >= 709.78... */
            if (hx >= 0x7FF00000UL) {
                if (((hx & 0xFFFFFUL) | low(x)) != 0)
                    return x + x;
                return xsb == 0 ? x : -1.0;
            }
            if (x > o_threshold)
                return HUGE_VAL;                        /* the callers report the overflow */
        }
        if (xsb != 0)
            return tiny - one;                          /* x < -56 ln2: -1 */
    }
    if (hx > 0x3FD62E42UL) {                            /* |x| > 0.5 ln2 */
        if (hx < 0x3FF0A2B2UL) {                        /* and |x| < 1.5 ln2 */
            if (xsb == 0) {
                hi = x - ln2_hi;
                lo = ln2_lo;
                k = 1;
            } else {
                hi = x + ln2_hi;
                lo = -ln2_lo;
                k = -1;
            }
        } else {
            k = (s32)(invln2 * x + (xsb == 0 ? 0.5 : -0.5));
            t = k;
            hi = x - t * ln2_hi;                        /* exact */
            lo = t * ln2_lo;
        }
        x = hi - lo;
        c = (hi - x) - lo;
    } else if (hx < 0x3C900000UL) {                     /* |x| < 2^-54 */
        return x;
    } else {
        k = 0;
    }
    hfx = 0.5 * x;
    hxs = x * hfx;
    r1 = one + hxs * (Q1 + hxs * (Q2 + hxs * (Q3 + hxs * (Q4 + hxs * Q5))));
    t = 3.0 - r1 * hfx;
    e = hxs * ((r1 - t) / (6.0 - x * t));
    if (k == 0)
        return x - (x * e - hxs);
    e = (x * (e - c) - c);
    e = e - hxs;
    if (k == -1)
        return 0.5 * (x - e) - 0.5;
    if (k == 1) {
        if (x < -0.25)
            return -2.0 * (e - (x + 0.5));
        return one + 2.0 * (x - e);
    }
    if (k <= -2 || k > 56) {                            /* exp(x) - 1 will do */
        y = one - (e - x);
        y = mk(hiw(y) + ((u32)k << 20), low(y));
        return y - one;
    }
    if (k < 20) {
        t = mk(0x3FF00000UL - (0x200000UL >> k), 0);   /* 1 - 2^-k */
        y = t - (e - x);
    } else {
        t = mk((u32)(0x3FFL - k) << 20, 0);            /* 2^-k */
        y = x - (e + t);
        y = y + one;
    }
    return mk(hiw(y) + ((u32)k << 20), low(y));
}

/* sinh x = (e^x - e^-x) / 2, from t = expm1(|x|): (t + t/(t + 1)) / 2,
 * which does not cancel for small x. From |x| = 22 on, e^-|x| is too
 * small to matter and e^|x| / 2 is the answer; near the overflow
 * threshold it is (e^(|x|/2) / 2) e^(|x|/2), so that the intermediate
 * does not overflow before the result does. */
double sinh(double x)
{
    double t;
    double w;
    double h;
    u32 ix;
    u32 lx;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL)
        return x + x;
    h = x < 0 ? -0.5 : 0.5;
    if (ix < 0x40360000UL) {                            /* |x| < 22 */
        if (ix < 0x3E300000UL)                          /* |x| < 2^-28 */
            return x;
        t = __expm1(fabs(x));
        if (ix < 0x3FF00000UL)
            return h * (2.0 * t - t * t / (t + one));
        return h * (t + t / (t + one));
    }
    if (ix < 0x40862E42UL)                              /* |x| < log(DBL_MAX) */
        return h * exp(fabs(x));
    lx = low(x);
    if (ix < 0x408633CEUL || (ix == 0x408633CEUL && lx <= 0x8FB9F87DUL)) {
        w = exp(0.5 * fabs(x));                         /* |x| < overflow threshold */
        t = h * w;
        return t * w;
    }
    return overflow(x < 0);
}

/* cosh x = (e^x + e^-x) / 2, with the same ranges as sinh; below
 * ln2/2 it is 1 + t^2 / (2 (1 + t)), t = expm1(|x|), accurate near 1. */
double cosh(double x)
{
    double t;
    double w;
    u32 ix;
    u32 lx;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL)
        return x * x;
    if (ix < 0x3FD62E43UL) {                            /* |x| < 0.5 ln2 */
        t = __expm1(fabs(x));
        w = one + t;
        if (ix < 0x3C800000UL)
            return w;                                   /* cosh(tiny) = 1 */
        return one + (t * t) / (w + w);
    }
    if (ix < 0x40360000UL) {                            /* |x| < 22 */
        t = exp(fabs(x));
        return 0.5 * t + 0.5 / t;
    }
    if (ix < 0x40862E42UL)
        return 0.5 * exp(fabs(x));
    lx = low(x);
    if (ix < 0x408633CEUL || (ix == 0x408633CEUL && lx <= 0x8FB9F87DUL)) {
        w = exp(0.5 * fabs(x));
        t = 0.5 * w;
        return t * w;
    }
    return overflow(0);
}

/* tanh x = 1 - 2 / (e^2x + 1) for |x| >= 1, and -t / (t + 2) with
 * t = expm1(-2|x|) below 1, both forms free of cancellation in their
 * range; from |x| = 22 on it is 1 to double precision. */
double tanh(double x)
{
    double t;
    double z;
    u32 ix;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL) {
        if (x != x)
            return x + x;
        return x > 0 ? one : -one;                      /* tanh(+-inf) = +-1 */
    }
    if (ix < 0x40360000UL) {                            /* |x| < 22 */
        if (ix < 0x3C800000UL)                          /* |x| < 2^-55 */
            return x * (one + x);
        if (ix >= 0x3FF00000UL) {                       /* |x| >= 1 */
            t = __expm1(2.0 * fabs(x));
            z = one - 2.0 / (t + 2.0);
        } else {
            t = __expm1(-2.0 * fabs(x));
            z = -t / (t + 2.0);
        }
    } else {
        z = one - tiny;                                 /* 1 */
    }
    return x < 0 ? -z : z;
}

/* ---- pow ------------------------------------------------------------------- */

static double bp[2] = { 1.0, 1.5 };
static double dp_h[2] = { 0.0, 5.84962487220764160156e-01 };   /* 0x3FE2B803, 0x40000000 */
static double dp_l[2] = { 0.0, 1.35003920212974897128e-08 };   /* 0x3E4CFDEB, 0x43CFD006 */
static double two53 = 9007199254740992.0;              /* 0x43400000, 0x00000000 */
static double L1 = 5.99999999999994648725e-01;         /* 0x3FE33333, 0x33333303 */
static double L2 = 4.28571428578550184252e-01;         /* 0x3FDB6DB6, 0xDB6FABFF */
static double L3 = 3.33333329818377432918e-01;         /* 0x3FD55555, 0x518F264D */
static double L4 = 2.72728123808534006489e-01;         /* 0x3FD17460, 0xA91D4101 */
static double L5 = 2.30660745775561754067e-01;         /* 0x3FCD864A, 0x93C9DB65 */
static double L6 = 2.06975017800338417784e-01;         /* 0x3FCA7E28, 0x4A454EEF */
static double lg2 = 6.93147180559945286227e-01;        /* 0x3FE62E42, 0xFEFA39EF */
static double lg2_h = 6.93147182464599609375e-01;      /* 0x3FE62E43, 0x00000000 */
static double lg2_l = -1.90465429995776804525e-09;     /* 0xBE205C61, 0x0CA86C39 */
static double ovt = 8.0085662595372944372e-17;         /* -(1024 - log2(overflow + 0.5 ulp)) */
static double cp = 9.61796693925975554329e-01;         /* 0x3FEEC709, 0xDC3A03FD: 2 / (3 ln2) */
static double cp_h = 9.61796700954437255859e-01;       /* 0x3FEEC709, 0xE0000000 */
static double cp_l = -7.02846165095275826516e-09;      /* 0xBE3E2FE0, 0x145B01F5 */
static double ivln2 = 1.44269504088896338700e+00;      /* 0x3FF71547, 0x652B82FE */
static double ivln2_h = 1.44269502162933349609e+00;    /* 0x3FF71547, 0x60000000 */
static double ivln2_l = 1.92596299112661746887e-08;    /* 0x3E54AE0B, 0xF85DDF44 */

/* pow's method: x^y = 2^(y log2 x). log2 x is made to well beyond double
 * precision as t1 + t2 (pow_log2), y is split as y1 + (y - y1), and the
 * product is formed as p_h + p_l with p_h = y1 t1 exact (both chopped).
 * pow_exp2 then raises 2 to it. The extra precision matters because an
 * error in the exponent is magnified: y log2 x may be near 1024, and one
 * part in 2^53 of that is many units in the last place of the result.
 * The constants: bp, dp_h and dp_l are 1 and 1.5 and log2(1.5) in two
 * parts (pow_log2 reduces around one or the other); L1 to L6 its log
 * polynomial; lg2 and cp (2/(3 ln2)) and ivln2 split into _h and _l
 * parts; ovt the margin for the overflow test at exactly 1024. */

/* the result s * 2^(big or tiny): an overflow or an underflow */
static double pow_range(double s, int over)
{
    return over ? overflow(s < 0) : underflow(s < 0);
}

/* log2(ax) as t1 + t2, t1 with its low word clear, for ax (high word ix)
 * neither tiny nor huge nor near 1. ax = 2^n m with m reduced to lie
 * near 1 (k = 0) or near 1.5 (k = 1), so that log2 ax = n + log2(bp[k])
 * + log2(m / bp[k]). With s = (m - bp)/(m + bp), log(m/bp) is
 * 2s + 2s^3/3 + ..., written as 3s + s(s^2 + R) scaled by 2/(3 ln2) (cp).
 * Every quantity is kept as a chopped high part and a low remainder
 * (s_h + s_l, t_h + t_l, p_h + p_l, z_h + z_l) so that the products of
 * high parts are exact and the low parts carry the rest. */
static void pow_log2(double ax, s32 ix, double *t1p, double *t2p)
{
    double u;
    double v;
    double ss;
    double s2;
    double s_h;
    double s_l;
    double t_h;
    double t_l;
    double r;
    double p_h;
    double p_l;
    double z_h;
    double z_l;
    double t;
    double t1;
    s32 n;
    s32 j;
    s32 k;

    n = 0;
    if (ix < 0x00100000L) {                         /* subnormal */
        ax = ax * two53;
        n = n - 53;
        ix = (s32)hiw(ax);
    }
    n = n + (ix >> 20) - 0x3FF;
    j = ix & 0x000FFFFFL;
    ix = j | 0x3FF00000L;
    if (j <= 0x3988EL) {
        k = 0;                                      /* |x| < sqrt(3/2) */
    } else if (j < 0xBB67AL) {
        k = 1;                                      /* |x| < sqrt(3) */
    } else {
        k = 0;
        n = n + 1;
        ix = ix - 0x00100000L;
    }
    ax = mk((u32)ix, low(ax));
    /* ss = s_h + s_l = (x - 1) / (x + 1) or (x - 1.5) / (x + 1.5) */
    u = ax - bp[k];
    v = one / (ax + bp[k]);
    ss = u * v;
    s_h = chop(ss);
    /* t_h: m + bp[k] with its low bits dropped, built in the high word by
     * integer arithmetic: m's fraction shifted down one under exponent
     * 0x400 (times 2) is 2 + f = m + 1, and k << 18 adds the 0.5 more
     * that bp[1] needs; t_l is the exact remainder */
    t_h = mk(((u32)(ix >> 1) | 0x20000000UL) + 0x00080000UL + ((u32)k << 18), 0);
    t_l = ax - (t_h - bp[k]);
    s_l = v * ((u - s_h * t_h) - s_h * t_l);
    /* log(ax) */
    s2 = ss * ss;
    r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
    r = r + s_l * (s_h + ss);
    s2 = s_h * s_h;
    t_h = chop(3.0 + s2 + r);
    t_l = r - ((t_h - 3.0) - s2);
    u = s_h * t_h;
    v = s_l * t_h + t_l * ss;
    p_h = chop(u + v);
    p_l = v - (p_h - u);
    z_h = cp_h * p_h;
    z_l = cp_l * p_h + p_l * cp + dp_l[k];
    /* log2(ax) = n + dp_h + z_h + z_l */
    t = (double)n;
    t1 = chop(((z_h + z_l) + dp_h[k]) + t);
    *t1p = t1;
    *t2p = z_l - (((t1 - t) - dp_h[k]) - z_h);
}

/* 2^(p_h + p_l), j the high word of their sum, within the range. The
 * nearest integer n comes off by bit work on j (t, the same integer as a
 * double, is the rounded sum's bits with the fraction masked off) and is
 * taken from p_h, leaving |z| <= 1/2; z is converted to the base e as
 * u + v = z ln2, with lg2 split as ln2 is in exp; e^(u + v) uses exp's
 * polynomial and rational form; n is added into the exponent field, or
 * by ldexp when the result is subnormal. */
static double pow_exp2(double p_h, double p_l, s32 j)
{
    double t;
    double t1;
    double u;
    double v;
    double w;
    double r;
    double z;
    s32 i;
    s32 k;
    s32 n;

    i = j & 0x7FFFFFFFL;
    k = (i >> 20) - 0x3FF;
    n = 0;
    if (i > 0x3FE00000L) {                              /* |z| > 0.5: n = [z + 0.5] */
        n = j + (0x00100000L >> (k + 1));
        k = ((n & 0x7FFFFFFFL) >> 20) - 0x3FF;
        t = mk((u32)n & ~(0x000FFFFFUL >> k), 0);
        n = ((n & 0x000FFFFFL) | 0x00100000L) >> (20 - k);
        if (j < 0)
            n = -n;
        p_h = p_h - t;
    }
    t = chop(p_l + p_h);
    u = t * lg2_h;
    v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
    z = u + v;
    w = v - (z - u);
    t = z * z;
    t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    r = (z * t1) / (t1 - 2.0) - (w + z * w);
    z = one - (r - z);
    j = (s32)(hiw(z) + ((u32)n << 20));
    if ((j >> 20) <= 0)
        return ldexp(z, (int)n);                        /* a subnormal result */
    return mk((u32)j, low(z));
}

/* x^y. The steps: special values of y and x (C and IEEE 754 fix the
 * answer for zeros, infinities, NaNs, y = +-1, 2 and 0.5); whether y is
 * an odd or even integer, which decides the sign for x < 0 (a negative
 * x with a non-integer y is a domain error); huge |y|, which overflows or
 * underflows unless x is within 2^-20 of 1, where log x comes from the
 * short series instead; then log2 |x| (pow_log2), the product with y,
 * the range test, and 2^z (pow_exp2) times the sign s. */
double pow(double x, double y)
{
    double z;
    double ax;
    double p_h;
    double p_l;
    double y1;
    double t1;
    double t2;
    double s;
    double t;
    double u;
    double v;
    double w;
    s32 i;
    s32 j;
    s32 k;
    s32 n;
    int yisint;
    s32 hx;
    s32 hy;
    s32 ix;
    s32 iy;
    u32 lx;
    u32 ly;

    hx = (s32)hiw(x);
    lx = low(x);
    hy = (s32)hiw(y);
    ly = low(y);
    ix = hx & 0x7FFFFFFFL;
    iy = hy & 0x7FFFFFFFL;
    if ((iy | ly) == 0)
        return one;                                     /* x^0 = 1, even for a NaN */
    if (ix > 0x7FF00000L || (ix == 0x7FF00000L && lx != 0) || iy > 0x7FF00000L || (iy == 0x7FF00000L && ly != 0))
        return x + y;                                   /* a NaN */
    /* yisint: 0 y is not an integer, 1 an odd one, 2 an even one. With
     * unbiased exponent k, y's units bit is fraction bit 52 - k: in the
     * low word when k > 20, else in the high word (and then the low word
     * must be 0). y is an integer when shifting the bits below the units
     * bit out and back in changes nothing; j & 1 is then its parity. */
    yisint = 0;
    if (hx < 0) {
        if (iy >= 0x43400000L) {
            yisint = 2;                                 /* |y| >= 2^53: even */
        } else if (iy >= 0x3FF00000L) {
            k = (iy >> 20) - 0x3FF;
            if (k > 20) {
                j = (s32)(ly >> (52 - k));
                if (((u32)j << (52 - k)) == ly)
                    yisint = 2 - (int)(j & 1);
            } else if (ly == 0) {
                j = iy >> (20 - k);
                if ((j << (20 - k)) == iy)
                    yisint = 2 - (int)(j & 1);
            }
        }
    }
    if ((ix | lx) == 0 && hy < 0) {
        errno = EDOM;                                   /* 0 to a negative power */
        return yisint == 1 && hx < 0 ? -HUGE_VAL : HUGE_VAL;
    }
    if (ly == 0) {
        if (iy == 0x7FF00000L) {                        /* y is +-inf */
            if (((ix - 0x3FF00000L) | lx) == 0)
                return one;                             /* (+-1)^+-inf = 1 */
            if (ix >= 0x3FF00000L)
                return hy >= 0 ? y : 0.0;               /* (|x| > 1)^+-inf = inf, 0 */
            return hy < 0 ? -y : 0.0;                   /* (|x| < 1)^-,+inf = inf, 0 */
        }
        if (iy == 0x3FF00000L)                          /* y is +-1 */
            return hy < 0 ? one / x : x;
        if (hy == 0x40000000L)
            return x * x;                               /* y is 2 */
        if (hy == 0x3FE00000L && hx >= 0)
            return sqrt(x);                             /* y is 0.5, x >= +0 */
    }
    ax = fabs(x);
    if (lx == 0 && (ix == 0x7FF00000L || ix == 0 || ix == 0x3FF00000L)) {
        z = ax;                                         /* x is +-0, +-inf or +-1 */
        if (hy < 0)
            z = one / z;
        if (hx < 0) {
            if (((ix - 0x3FF00000L) | yisint) == 0)
                return nan_result();                    /* (-1)^non-integer */
            if (yisint == 1)
                z = -z;
        }
        return z;
    }
    /* hx >> 31 is an arithmetic shift of a signed word: -1 for x < 0 */
    n = (hx >> 31) + 1;                                 /* 0 if x < 0 */
    if ((n | yisint) == 0)
        return nan_result();                            /* (x < 0)^non-integer */
    s = one;                                            /* the result's sign */
    if ((n | (yisint - 1)) == 0)
        s = -one;
    if (iy > 0x41E00000L) {                             /* |y| > 2^31 */
        if (iy > 0x43F00000L) {                         /* |y| > 2^64: over or under */
            if (ix <= 0x3FEFFFFFL)
                return pow_range(one, hy < 0);
            if (ix >= 0x3FF00000L)
                return pow_range(one, hy > 0);
        }
        if (ix < 0x3FEFFFFFL)
            return pow_range(s, hy < 0);
        if (ix > 0x3FF00000L)
            return pow_range(s, hy > 0);
        /* |1 - x| <= 2^-20: log(x) from x - x^2/2 + x^3/3 - x^4/4 */
        t = ax - one;
        w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
        u = ivln2_h * t;
        v = t * ivln2_l - w * ivln2;
        t1 = chop(u + v);
        t2 = v - (t1 - u);
    } else {
        pow_log2(ax, ix, &t1, &t2);
    }
    /* (y1 + y2) (t1 + t2) */
    y1 = chop(y);
    p_l = (y - y1) * t1 + y * t2;
    p_h = y1 * t1;
    z = p_l + p_h;
    /* z = y log2|x|: at 1024 or more the result overflows, at -1075 or
     * less it underflows; when z is exactly a bound, comparing p_l with
     * z - p_h, the part of p_l that the rounded sum kept, shows which
     * side of the bound the true sum lies (ovt a margin for rounding) */
    j = (s32)hiw(z);
    i = (s32)low(z);
    if (j >= 0x40900000L) {                             /* z >= 1024 */
        if (((j - 0x40900000L) | i) != 0 || p_l + ovt > z - p_h)
            return pow_range(s, 1);
    } else if ((j & 0x7FFFFFFFL) >= 0x4090CC00L) {      /* z <= -1075 */
        if (((j - (s32)0xC090CC00UL) | i) != 0 || p_l <= z - p_h)
            return pow_range(s, 0);
    }
    return s * pow_exp2(p_h, p_l, j);
}

/* ---- trigonometric functions -------------------------------------------- */

static double S1 = -1.66666666666666324348e-01;        /* 0xBFC55555, 0x55555549 */
static double S2 = 8.33333333332248946124e-03;         /* 0x3F811111, 0x1110F8A6 */
static double S3 = -1.98412698298579493134e-04;        /* 0xBF2A01A0, 0x19C161D5 */
static double S4 = 2.75573137070700676789e-06;         /* 0x3EC71DE3, 0x57B1FE7D */
static double S5 = -2.50507602534068634195e-08;        /* 0xBE5AE5E6, 0x8A2B9CEB */
static double S6 = 1.58969099521155010221e-10;         /* 0x3DE5D93A, 0x5ACFD57C */

/* The trigonometric functions: reduce x to r = x - n pi/2 with
 * |r| <= pi/4 (rem_pio2), then n mod 4 picks the kernel and the sign:
 * sin x is sin r, cos r, -sin r or -cos r. The kernels k_sin, k_cos and
 * k_tan evaluate odd or even polynomials on |r| <= pi/4. The reduced
 * argument arrives as two doubles, x + y, because r is the small
 * difference of nearly equal numbers and needs more bits than one
 * double holds; the kernels fold the tail y in to first order. */

/* sin(x + y) for |x| <= pi/4, y the tail of x (iy 0: none). sin is odd:
 * x + x^3 (S1 + x^2 S2 + ...), the S coefficients a minimax fit to the
 * series -1/3!, 1/5!, ...; with a tail, sin(x + y) ~ sin x + y cos x,
 * cos x taken as 1 - x^2/2. */
static double k_sin(double x, double y, int iy)
{
    double z;
    double r;
    double v;

    if ((hiw(x) & 0x7FFFFFFFUL) < 0x3E400000UL)         /* |x| < 2^-27 */
        return x;
    z = x * x;
    v = z * x;
    r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0)
        return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

static double C1 = 4.16666666666666019037e-02;         /* 0x3FA55555, 0x5555554C */
static double C2 = -1.38888888888741095749e-03;        /* 0xBF56C16C, 0x16C15177 */
static double C3 = 2.48015872894767294178e-05;         /* 0x3EFA01A0, 0x19CB1590 */
static double C4 = -2.75573143513906633035e-07;        /* 0xBE927E4F, 0x809C52AD */
static double C5 = 2.08757232129817482790e-09;         /* 0x3E21EE9E, 0xBDB4B1C4 */
static double C6 = -1.13596475577881948265e-11;        /* 0xBDA8FAE9, 0xBE8838D4 */

/* cos(x + y) for |x| <= pi/4. cos is even: 1 - x^2/2 + x^4 r(x^2), with
 * the tail as - x y. For |x| >= 0.3, 1 - x^2/2 is computed as
 * (1 - qx) - (x^2/2 - qx), with qx = |x|/4 with its low word clear (or
 * 0.28125 above 0.78125): 1 - qx is exact, as qx has few bits, and the
 * rounding error of the rest is smaller than that of 1 - x^2/2 at once. */
static double k_cos(double x, double y)
{
    double a;
    double hz;
    double z;
    double r;
    double qx;
    u32 ix;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix < 0x3E400000UL)                              /* |x| < 2^-27 */
        return one;
    z = x * x;
    r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333UL)                              /* |x| < 0.3 */
        return one - (0.5 * z - (z * r - x * y));
    if (ix > 0x3FE90000UL)
        qx = 0.28125;                                   /* |x| > 0.78125 */
    else
        qx = mk(ix - 0x00200000UL, 0);                  /* x/4 */
    hz = 0.5 * z - qx;
    a = one - qx;
    return a - (hz - (z * r - x * y));
}

static double T[13] = {
    3.33333333333334091986e-01,                         /* 0x3FD55555, 0x55555563 */
    1.33333333333201242699e-01,                         /* 0x3FC11111, 0x1110FE7A */
    5.39682539762260521377e-02,                         /* 0x3FABA1BA, 0x1BB341FE */
    2.18694882948595424599e-02,                         /* 0x3F9664F4, 0x8406D637 */
    8.86323982359930005737e-03,                         /* 0x3F8226E3, 0xE96E8493 */
    3.59207910759131235356e-03,                         /* 0x3F6D6D22, 0xC9560328 */
    1.45620945432529025516e-03,                         /* 0x3F57DBC8, 0xFEE08315 */
    5.88041240820264096874e-04,                         /* 0x3F4344D8, 0xF2F26501 */
    2.46463134818469906812e-04,                         /* 0x3F3026F7, 0x1A8D1068 */
    7.81794442939557092300e-05,                         /* 0x3F147E88, 0xA03792A6 */
    7.14072491382608190305e-05,                         /* 0x3F12B80F, 0x32F0A7E9 */
    -1.85586374855275456654e-05,                        /* 0xBEF375CB, 0xDB605373 */
    2.59073051863633712884e-05                          /* 0x3EFB2A70, 0x74BF7AD4 */
};
static double pio4 = 7.85398163397448278999e-01;       /* 0x3FE921FB, 0x54442D18 */
static double pio4lo = 3.06161699786838301793e-17;     /* 0x3C81A626, 0x33145C07 */

/* tan(x + y) for |x| <= pi/4 (iy 1), or -1/tan (iy -1). tan is odd:
 * x + x^3 (T0 + T1 x^2 + ...), thirteen coefficients, evaluated as two
 * interleaved chains (r and v) in w = x^4. Near pi/4 (|x| >= 0.6744)
 * x is replaced by pi/4 - x (with pio4lo, pi/4's low part), and
 * tan(pi/4 - x) = (1 - tan x) / (1 + tan x) turns the result back. The
 * -1/tan case divides carefully: a chopped quotient t, then one
 * correction step that brings back the tail of x + r, which the rounded
 * sum w has lost. */
static double k_tan(double x, double y, int iy)
{
    double z;
    double r;
    double v;
    double w;
    double s;
    double a;
    double t;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix < 0x3E300000UL) {                            /* |x| < 2^-28 */
        if (iy == 1)
            return x;
        if ((ix | low(x)) == 0)
            return one / fabs(x);
        /* -1/(x + y), carefully */
        z = w = x + y;
        z = chop(z);
        v = y - (z - x);
        t = a = -one / w;
        t = chop(t);
        s = one + t * z;
        return t + a * (s + t * v);
    }
    if (ix >= 0x3FE59428UL) {                           /* |x| >= 0.6744 */
        if (hx >> 31) {
            x = -x;
            y = -y;
        }
        z = pio4 - x;
        w = pio4lo - y;
        x = z + w;
        y = 0.0;
    }
    z = x * x;
    w = z * z;
    r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    s = z * x;
    r = y + z * (s * (r + v) + y);
    r = r + T[0] * s;
    w = x + r;
    if (ix >= 0x3FE59428UL) {
        v = (double)iy;
        return (double)(1 - (int)((hx >> 30) & 2)) * (v - 2.0 * (x - (w * w / (w + v) - r)));
    }
    if (iy == 1)
        return w;
    /* -1/(x + r), carefully */
    z = chop(w);
    v = r - (z - x);
    t = a = -1.0 / w;
    t = chop(t);
    s = 1.0 + t * z;
    return t + a * (s + t * v);
}

/* rem_pio2's constants: 2/pi, and pi/2 in three pieces of 33 bits each
 * (pio2_1, pio2_2, pio2_3), each with the tail that follows it (the t
 * forms): n pio2_1 is exact for any n below 2^20. */
static double invpio2 = 6.36619772367581382433e-01;    /* 0x3FE45F30, 0x6DC9C883 */
static double pio2_1 = 1.57079632673412561417e+00;     /* 0x3FF921FB, 0x54400000 */
static double pio2_1t = 6.07710050650619224932e-11;    /* 0x3DD0B461, 0x1A626331 */
static double pio2_2 = 6.07710050630396597660e-11;     /* 0x3DD0B461, 0x1A600000 */
static double pio2_2t = 2.02226624879595063154e-21;    /* 0x3BA3198A, 0x2E037073 */
static double pio2_3 = 2.02226624871116645580e-21;     /* 0x3BA3198A, 0x2E000000 */
static double pio2_3t = 8.47842766036889956997e-32;    /* 0x397B839A, 0x252049C1 */

/* 2/pi's first 1216 fraction bits, 16 to a word */
static unsigned int two_over_pi[76] = {
    0xA2F9, 0x836E, 0x4E44, 0x1529, 0xFC27, 0x57D1, 0xF534, 0xDDC0, 0xDB62, 0x9599,
    0x3C43, 0x9041, 0xFE51, 0x63AB, 0xDEBB, 0xC561, 0xB724, 0x6E3A, 0x424D, 0xD2E0,
    0x0649, 0x2EEA, 0x09D1, 0x921C, 0xFE1D, 0xEB1C, 0xB129, 0xA73E, 0xE882, 0x35F5,
    0x2EBB, 0x4484, 0xE99C, 0x7026, 0xB45F, 0x7E41, 0x3991, 0xD639, 0x8353, 0x39F4,
    0x9C84, 0x5F8B, 0xBDF9, 0x283B, 0x1FF8, 0x97FF, 0xDE05, 0x980F, 0xEF2F, 0x118B,
    0x5A0A, 0x6D1F, 0x6D36, 0x7ECF, 0x27CB, 0x09B7, 0x4F46, 0x3F66, 0x9E5F, 0xEA2D,
    0x7527, 0xBAC7, 0xEBE5, 0xF17B, 0x3D07, 0x39F7, 0x8A52, 0x92EA, 0x6BFB, 0x5FB1,
    0x1F8D, 0x5D08, 0x5603, 0x3046, 0xFC7B, 0x6BAB
};

/* pi/2 in pieces of 24 bits */
static double pio2_24[8] = {
    1.570796251296997070312e+00,                        /* 0x3FF921FB, 0x40000000 */
    7.549789415861596353352e-08,                        /* 0x3E74442D, 0x00000000 */
    5.390302529957764765545e-15,                        /* 0x3CF84698, 0x80000000 */
    3.282003415807912941232e-22,                        /* 0x3B78CC51, 0x60000000 */
    1.270655753080676073491e-29,                        /* 0x39F01B83, 0x80000000 */
    1.229333089811113289322e-36,                        /* 0x387A2520, 0x40000000 */
    2.733700538164645596238e-44,                        /* 0x36E38222, 0x80000000 */
    2.167416838778048194440e-51                         /* 0x3569F31D, 0x00000000 */
};

/* 16 bits of a bit string (words of 16, most significant first; bits
 * outside [0, 16 n) are 0) from bit p on */
static unsigned int bits16(unsigned int *t, int n, int p)
{
    u32 v;
    int i;
    int r;

    if (p <= -16 || p >= 16 * n)
        return 0;
    if (p < 0)
        return t[0] >> -p;
    i = p / 16;
    r = p % 16;
    v = (u32)t[i] << 16;
    if (i + 1 < n)
        v = v | t[i + 1];
    return (unsigned int)((v << r) >> 16 & 0xFFFFUL);
}

/* |x| >= 2^19 pi/2, finite: x - n pi/2 = y[0] + y[1], |y| <= pi/4; returns
 * n. x = m 2^e, m a 53-bit integer, and x 2/pi mod 4 is m times the bits
 * of 2/pi that matter, whose product (in 16-bit limbs) has 192 bits below
 * its point: plenty, since x is never nearer than 2^-62 or so to a
 * multiple of pi/2. The fraction's 24-bit pieces times pi/2's are exact,
 * and summed as fdlibm's __kernel_rem_pio2 sums them.
 *
 * Payne and Hanek's idea, for a learner: x 2/pi = m 2^e times the bits
 * of 2/pi. Bits of 2/pi worth 2^-k with k well below e give products
 * that are whole multiples of 4, which change neither n mod 4 nor the
 * fraction, so they are skipped; bits far beyond e + 192 are too small
 * to matter. Only a window of 208 bits of 2/pi (W) is multiplied by m,
 * in integer arithmetic: 16-bit limbs, so that a limb product plus two
 * carries fits a u32. The product's integer part mod 4 is n, its
 * fraction times pi/2 the reduced argument. A fraction above 1/2 is
 * replaced by 1 - fraction (two's complement on the limbs) and n raised
 * by one, so that |r| <= pi/4. */
static int rem_pio2_big(double x, double *y)
{
    unsigned int m[4];
    unsigned int w[13];
    unsigned int p[17];
    unsigned int f[12];
    double q[8];
    double fq[8];
    double fw;
    double scale;
    u32 hx;
    u32 lx;
    u32 c;
    int e;
    int k1;
    int i;
    int j;
    int n;
    int neg;

    hx = hiw(x);
    lx = low(x);
    /* m: the 53-bit significand with its implicit 1 (0x10 in m[3]), in
     * 16-bit limbs, least significant first; x = m 2^e */
    e = (int)((hx >> 20) & 0x7FF) - 1075;
    m[0] = (unsigned int)(lx & 0xFFFF);
    m[1] = (unsigned int)(lx >> 16);
    m[2] = (unsigned int)(hx & 0xFFFF);
    m[3] = (unsigned int)((hx >> 16) & 0xF) | 0x10;
    /* W: 2/pi's bits k1 - 207 .. k1 (bit b_k is 2^-k, the string's bit
     * k - 1), 192 of them after e */
    k1 = e + 192;
    for (i = 0; i < 13; i++)
        w[i] = bits16(two_over_pi, 76, k1 - 16 * (i + 1));
    /* P = m W, schoolbook multiplication on the limbs (w[0], like m[0],
     * is the least significant); p[0..11] are then the 192 fraction bits
     * and p[12]'s low two bits the integer part mod 4 */
    for (i = 0; i < 17; i++)
        p[i] = 0;
    for (i = 0; i < 4; i++) {
        c = 0;
        for (j = 0; j < 13; j++) {
            c = (u32)m[i] * w[j] + p[i + j] + c;
            p[i + j] = (unsigned int)(c & 0xFFFF);
            c = c >> 16;
        }
        p[i + 13] = (unsigned int)c;
    }
    n = (int)(p[12] & 3);
    for (i = 0; i < 12; i++)
        f[i] = p[11 - i];                               /* the fraction, most significant first */
    neg = f[0] >= 0x8000;
    if (neg) {                                          /* above 1/2: to the next multiple */
        n++;
        c = 1;
        for (i = 11; i >= 0; i--) {
            c = (u32)(~f[i] & 0xFFFF) + c;
            f[i] = (unsigned int)(c & 0xFFFF);
            c = c >> 16;
        }
    }
    /* the fraction as eight 24-bit pieces q[i], worth 2^-24 (i + 1)
     * each: every piece converts to a double exactly, and so does every
     * piece times a 24-bit piece of pi/2 (48 bits). fq[i] gathers the
     * products of equal weight; summing them smallest first to y[0],
     * then what y[0] lost to y[1], gives r as a double and its tail. */
    scale = 1.0;
    for (i = 0; i < 8; i++) {
        scale = scale * 5.96046447753906250000e-08;     /* 2^-24 */
        q[i] = (double)(((u32)bits16(f, 12, 24 * i) << 8) | (bits16(f, 12, 24 * i + 16) >> 8)) * scale;
    }
    for (i = 0; i < 8; i++) {
        fw = 0.0;
        for (j = 0; j <= i; j++)
            fw = fw + pio2_24[j] * q[i - j];
        fq[i] = fw;
    }
    fw = 0.0;
    for (i = 7; i >= 0; i--)
        fw = fw + fq[i];
    y[0] = fw;
    fw = fq[0] - fw;
    for (i = 1; i < 8; i++)
        fw = fw + fq[i];
    y[1] = fw;
    if (neg) {
        y[0] = -y[0];
        y[1] = -y[1];
    }
    /* x < 0 was reduced as |x|: the reduction is odd. The callers take
     * n & 3, which is right for a negative n in two's complement. */
    if (hx >> 31) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

/* x - n pi/2 = y[0] + y[1] with |y| <= pi/4 (x finite); returns n.
 * Arguments up to 2^19 pi/2 use Cody and Waite's method: r = x - n pi/2
 * with pi/2 in pieces whose products with n are exact, so that the
 * cancellation between x and n pi/2 loses nothing. Each further piece is
 * used only when the result has lost enough leading bits (i, the drop
 * in exponent) to need it. Larger arguments go to rem_pio2_big. */
static int rem_pio2(double x, double *y)
{
    double z;
    double w;
    double t;
    double r;
    double fn;
    u32 hx;
    u32 ix;
    int j;
    int i;
    int n;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix > 0x413921FBUL)                              /* |x| > 2^19 pi/2 */
        return rem_pio2_big(x, y);
    /* Cody and Waite, pi/2 in three parts of 33 bits */
    t = fabs(x);
    n = (int)(t * invpio2 + 0.5);
    fn = (double)n;
    r = t - fn * pio2_1;
    w = fn * pio2_1t;                                   /* good to 85 bits */
    j = (int)(ix >> 20);
    y[0] = r - w;
    i = j - (int)((hiw(y[0]) >> 20) & 0x7FF);
    if (i > 16) {                                       /* a second step: 118 bits */
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        i = j - (int)((hiw(y[0]) >> 20) & 0x7FF);
        if (i > 49) {                                   /* a third: 151 bits */
            t = r;
            w = fn * pio2_3;
            r = t - w;
            w = fn * pio2_3t - ((t - r) - w);
            y[0] = r - w;
        }
    }
    z = r - y[0];
    y[1] = z - w;
    if (hx >> 31) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

double sin(double x)
{
    double y[2];
    u32 ix;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix <= 0x3FE921FBUL)                             /* |x| <= pi/4 */
        return k_sin(x, 0.0, 0);
    if (ix >= 0x7FF00000UL)
        return x != x ? x : nan_result();               /* sin(+-inf) */
    switch (rem_pio2(x, y) & 3) {
    case 0:
        return k_sin(y[0], y[1], 1);
    case 1:
        return k_cos(y[0], y[1]);
    case 2:
        return -k_sin(y[0], y[1], 1);
    default:
        return -k_cos(y[0], y[1]);
    }
}

double cos(double x)
{
    double y[2];
    u32 ix;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix <= 0x3FE921FBUL)
        return k_cos(x, 0.0);
    if (ix >= 0x7FF00000UL)
        return x != x ? x : nan_result();
    switch (rem_pio2(x, y) & 3) {
    case 0:
        return k_cos(y[0], y[1]);
    case 1:
        return -k_sin(y[0], y[1], 1);
    case 2:
        return -k_cos(y[0], y[1]);
    default:
        return k_sin(y[0], y[1], 1);
    }
}

double tan(double x)
{
    double y[2];
    u32 ix;
    int n;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix <= 0x3FE921FBUL)
        return k_tan(x, 0.0, 1);
    if (ix >= 0x7FF00000UL)
        return x != x ? x : nan_result();
    /* tan(r + n pi/2) is tan r for n even and -1/tan r for n odd */
    n = rem_pio2(x, y);
    return k_tan(y[0], y[1], 1 - ((n & 1) << 1));       /* tan or -1/tan */
}

/* ---- inverse trigonometric functions ----------------------------------- */

/* atan's constants: atan(0.5), atan(1), atan(1.5) and atan(inf) = pi/2,
 * each as a high and a low part, and the odd polynomial's coefficients
 * aT: atan t ~ t - t^3 (aT[0] + aT[1] t^2 + aT[2] t^4 + ...). */
static double atanhi[4] = {
    4.63647609000806093515e-01,                         /* atan(0.5): 0x3FDDAC67, 0x0561BB4F */
    7.85398163397448278999e-01,                         /* atan(1): 0x3FE921FB, 0x54442D18 */
    9.82793723247329054082e-01,                         /* atan(1.5): 0x3FEF730B, 0xD281F69B */
    1.57079632679489655800e+00                          /* atan(inf): 0x3FF921FB, 0x54442D18 */
};
static double atanlo[4] = {
    2.26987774529616870924e-17,                         /* 0x3C7A2B7F, 0x222F65E2 */
    3.06161699786838301793e-17,                         /* 0x3C81A626, 0x33145C07 */
    1.39033110312309984516e-17,                         /* 0x3C700788, 0x7AF0CBBD */
    6.12323399573676603587e-17                          /* 0x3C91A626, 0x33145C07 */
};
static double aT[11] = {
    3.33333333333329318027e-01,                         /* 0x3FD55555, 0x5555550D */
    -1.99999999998764832476e-01,                        /* 0xBFC99999, 0x9998EBC4 */
    1.42857142725034663711e-01,                         /* 0x3FC24924, 0x920083FF */
    -1.11111104054623557880e-01,                        /* 0xBFBC71C6, 0xFE231671 */
    9.09088713343650656196e-02,                         /* 0x3FB745CD, 0xC54C206E */
    -7.69187620504482999495e-02,                        /* 0xBFB3B0F2, 0xAF749A6D */
    6.66107313738753120669e-02,                         /* 0x3FB10D66, 0xA0D03D51 */
    -5.83357013379057348645e-02,                        /* 0xBFADDE2D, 0x52DEFD9A */
    4.97687799461593236017e-02,                         /* 0x3FA97B4B, 0x24760DEB */
    -3.65315727442169155270e-02,                        /* 0xBFA2B444, 0x2C6A6C2F */
    1.62858201153657823623e-02                          /* 0x3F90AD3A, 0xE322DA11 */
};

/* Arc tangent. Reduction: |x| is placed in one of five ranges, and for
 * all but the smallest the identity atan(x) = atan(c) + atan(t) with
 * t = (x - c) / (1 + x c) moves the work to a small t around 0, for
 * c = 0.5, 1, 1.5 or infinity (where t = -1/x). Approximation: an odd
 * polynomial in t, its even- and odd-numbered coefficients in s1 and s2,
 * each a short Horner chain in w = t^4. atan(c)'s low part is added
 * with the small terms, before the high part. */
double atan(double x)
{
    double w;
    double s1;
    double s2;
    double z;
    u32 hx;
    u32 ix;
    int id;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x44100000UL) {                           /* |x| >= 2^66 */
        if (ix > 0x7FF00000UL || (ix == 0x7FF00000UL && low(x) != 0))
            return x + x;                               /* NaN */
        if (hx >> 31)
            return -atanhi[3] - atanlo[3];
        return atanhi[3] + atanlo[3];
    }
    if (ix < 0x3FDC0000UL) {                            /* |x| < 0.4375 */
        if (ix < 0x3E200000UL)                          /* |x| < 2^-29 */
            return x;
        id = -1;
    } else {
        x = fabs(x);
        if (ix < 0x3FF30000UL) {                        /* |x| < 1.1875 */
            if (ix < 0x3FE60000UL) {                    /* 7/16 <= |x| < 11/16 */
                id = 0;
                x = (2.0 * x - one) / (2.0 + x);
            } else {                                    /* 11/16 <= |x| < 19/16 */
                id = 1;
                x = (x - one) / (x + one);
            }
        } else if (ix < 0x40038000UL) {                 /* |x| < 2.4375 */
            id = 2;
            x = (x - 1.5) / (one + 1.5 * x);
        } else {                                        /* 2.4375 <= |x| < 2^66 */
            id = 3;
            x = -1.0 / x;
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0)
        return x - x * (s1 + s2);
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return hx >> 31 ? -z : z;
}

static double pi_o_4 = 7.8539816339744827900e-01;      /* 0x3FE921FB, 0x54442D18 */
static double pi_o_2 = 1.5707963267948965580e+00;      /* 0x3FF921FB, 0x54442D18 */
static double pi = 3.1415926535897931160e+00;          /* 0x400921FB, 0x54442D18 */
static double pi_lo = 1.2246467991473531772e-16;       /* 0x3CA1A626, 0x33145C07 */

/* The angle of the point (x, y), in -pi .. pi. m holds both signs
 * (bit 0 y's, bit 1 x's) and picks the quadrant. After the special
 * cases (zeros and infinities, whose answers C and IEEE 754 fix), z is
 * atan |y/x|, and the quadrant maps it: z, -z, pi - z or z - pi, with
 * pi's low part pi_lo applied to z first. A ratio beyond 2^60 is pi/2
 * outright (before the quadrant), and below 2^-60 with x < 0 the angle
 * is +-pi. */
double atan2(double y, double x)
{
    double z;
    s32 k;
    int m;
    u32 hx;
    u32 hy;
    u32 ix;
    u32 iy;
    u32 lx;
    u32 ly;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    lx = low(x);
    hy = hiw(y);
    iy = hy & 0x7FFFFFFFUL;
    ly = low(y);
    if (x != x || y != y)
        return x + y;                                   /* a NaN */
    if (((hx - 0x3FF00000UL) | lx) == 0)
        return atan(y);                                 /* x = 1 */
    m = (int)((hy >> 31) | ((hx >> 30) & 2));           /* 2 sign(x) + sign(y) */
    if ((iy | ly) == 0) {                               /* y = 0 */
        if (m < 2)
            return y;                                   /* atan(+-0, +anything) = +-0 */
        return m == 2 ? pi : -pi;                       /* atan(+-0, -anything) = +-pi */
    }
    if ((ix | lx) == 0)                                 /* x = 0 */
        return hy >> 31 ? -pi_o_2 : pi_o_2;
    if (ix == 0x7FF00000UL) {                           /* x is +-inf */
        if (iy == 0x7FF00000UL) {
            switch (m) {
            case 0:
                return pi_o_4;
            case 1:
                return -pi_o_4;
            case 2:
                return 3.0 * pi_o_4;
            default:
                return -3.0 * pi_o_4;
            }
        }
        switch (m) {
        case 0:
            return 0.0;
        case 1:
            return -0.0;
        case 2:
            return pi;
        default:
            return -pi;
        }
    }
    if (iy == 0x7FF00000UL)                             /* y is +-inf */
        return hy >> 31 ? -pi_o_2 : pi_o_2;
    k = ((s32)iy - (s32)ix) >> 20;
    if (k > 60)
        z = pi_o_2 + 0.5 * pi_lo;                       /* |y/x| > 2^60 */
    else if ((hx >> 31) && k < -60)
        z = 0.0;                                        /* |y|/x < -2^60 */
    else
        z = atan(fabs(y / x));
    switch (m) {
    case 0:
        return z;
    case 1:
        return -z;
    case 2:
        return pi - (z - pi_lo);
    default:
        return (z - pi_lo) - pi;
    }
}

static double pio2_hi = 1.57079632679489655800e+00;    /* 0x3FF921FB, 0x54442D18 */
static double pio2_lo = 6.12323399573676603587e-17;    /* 0x3C91A626, 0x33145C07 */
static double pio4_hi = 7.85398163397448278999e-01;    /* 0x3FE921FB, 0x54442D18 */
static double pS0 = 1.66666666666666657415e-01;        /* 0x3FC55555, 0x55555555 */
static double pS1 = -3.25565818622400915405e-01;       /* 0xBFD4D612, 0x03EB6F7D */
static double pS2 = 2.01212532134862925881e-01;        /* 0x3FC9C155, 0x0E884455 */
static double pS3 = -4.00555345006794114027e-02;       /* 0xBFA48228, 0xB5688F3B */
static double pS4 = 7.91534994289814532176e-04;        /* 0x3F49EFE0, 0x7501B288 */
static double pS5 = 3.47933107596021167570e-05;        /* 0x3F023DE1, 0x0DFDF709 */
static double qS1 = -2.40339491173441421878e+00;       /* 0xC0033A27, 0x1C8A2D4B */
static double qS2 = 2.02094576023350569471e+00;        /* 0x40002AE5, 0x9C598AC8 */
static double qS3 = -6.88283971605453293030e-01;       /* 0xBFE6066C, 0x1B8D0159 */
static double qS4 = 7.70381505559019352791e-02;        /* 0x3FB3B8C5, 0xB12E9282 */

/* asin's and acos's rational approximation R(t): asin x ~ x + x R(x^2)
 * for |x| <= 0.5: a rational approximation, the ratio p/q of two
 * polynomials in t. */
static double asin_r(double t)
{
    double p;
    double q;

    p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
    q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
    return p / q;
}

/* Arc sine. Below 0.5 directly, x + x R(x^2). From 0.5 on, the identity
 * asin x = pi/2 - 2 asin(sqrt((1 - x) / 2)) brings the argument back
 * below 0.5 (as s = sqrt(t)); for |x| up to 0.975 s is split into a
 * chopped high part w and a correction c = (t - w^2) / (s + w) (the
 * error of w as sqrt t), and the result is formed as pi/4 - (p - q)
 * from pi/4's high part, carrying c and pi/2's low part in p. */
double asin(double x)
{
    double t;
    double w;
    double p;
    double q;
    double c;
    double r;
    double s;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x3FF00000UL) {                           /* |x| >= 1 */
        if (((ix - 0x3FF00000UL) | low(x)) == 0)
            return x * pio2_hi + x * pio2_lo;           /* asin(+-1) = +-pi/2 */
        return x != x ? x : nan_result();               /* |x| > 1 */
    }
    if (ix < 0x3FE00000UL) {                            /* |x| < 0.5 */
        if (ix < 0x3E400000UL)                          /* |x| < 2^-27 */
            return x;
        return x + x * asin_r(x * x);
    }
    w = one - fabs(x);                                  /* 0.5 <= |x| < 1 */
    t = w * 0.5;
    s = sqrt(t);
    if (ix >= 0x3FEF3333UL) {                           /* |x| > 0.975 */
        w = asin_r(t);
        t = pio2_hi - (2.0 * (s + s * w) - pio2_lo);
    } else {
        w = chop(s);
        c = (t - w * w) / (s + w);
        r = asin_r(t);
        p = 2.0 * s * r - (pio2_lo - 2.0 * c);
        q = pio4_hi - 2.0 * w;
        t = pio4_hi - (p - q);
    }
    return hx >> 31 ? -t : t;
}

/* Arc cosine, by the same R as asin: acos x = pi/2 - asin x below 0.5;
 * acos x = pi - 2 asin(sqrt((1 + x) / 2)) for x < -0.5; and
 * acos x = 2 asin(sqrt((1 - x) / 2)) for x > 0.5, where sqrt's result is
 * split into df (chopped) and the correction c, as in asin. */
double acos(double x)
{
    double z;
    double r;
    double w;
    double s;
    double c;
    double df;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x3FF00000UL) {                           /* |x| >= 1 */
        if (((ix - 0x3FF00000UL) | low(x)) == 0)
            return hx >> 31 ? pi + 2.0 * pio2_lo : 0.0; /* acos(-1) = pi, acos(1) = 0 */
        return x != x ? x : nan_result();               /* |x| > 1 */
    }
    if (ix < 0x3FE00000UL) {                            /* |x| < 0.5 */
        if (ix <= 0x3C600000UL)                         /* |x| < 2^-57 */
            return pio2_hi + pio2_lo;
        r = asin_r(x * x);
        return pio2_hi - (x - (pio2_lo - x * r));
    }
    if (hx >> 31) {                                     /* x < -0.5 */
        z = (one + x) * 0.5;
        s = sqrt(z);
        r = asin_r(z);
        w = r * s - pio2_lo;
        return pi - 2.0 * (s + w);
    }
    z = (one - x) * 0.5;                                /* x > 0.5 */
    s = sqrt(z);
    df = chop(s);
    c = (z - df * df) / (s + df);
    r = asin_r(z);
    w = r * s + c;
    return 2.0 * (df + w);
}
