/* math99.c - C99's additions to <math.h> (7.12), for double: the
 * classification and comparison macros' helpers; copysign, nextafter,
 * nexttoward, fdim, fmax, fmin, nan; the rounding functions (trunc,
 * round, rint, nearbyint and their long and long long forms); remainder,
 * remquo; ilogb, logb, scalbn, scalbln; cbrt, hypot; log1p, expm1, log2,
 * exp2; asinh, acosh, atanh; erf, erfc; lgamma, tgamma; fma. mathf.c
 * has the float and long double forms of every function.
 *
 * Most of the algorithms are fdlibm's, as math.c's are (Copyright (C)
 * 1993 by Sun Microsystems, Inc.; "Permission to use, copy, modify, and
 * distribute this software is freely granted, provided that this notice
 * is preserved."), rewritten in the same way for a target whose int is
 * 24 bits. fdlibm has no exp2, log2, tgamma, remquo or fma: those are
 * built here on the others (exp, log, lgamma, fmod's method) and on
 * softfp (fma), each said where it is.
 *
 * C99 leaves the floating-point environment optional: there is none
 * here, the rounding is always to nearest, and errors are reported by
 * errno alone (math_errhandling is MATH_ERRNO): EDOM with a NaN for an
 * argument outside the domain, ERANGE for a pole (an infinite result from
 * a finite argument, such as lgamma(0)) and for a result that overflows,
 * or underflows to zero. A NaN argument gives a NaN without an error.
 *
 * The library's C89 functions call none of C99's names, which a C89
 * program may use for functions of its own: math.c's sinh, cosh and tanh
 * use __expm1, and trunc's work is math.c's __trunc. The functions here
 * are C99's, and call each other freely.
 */

#include <math.h>
#include <errno.h>
#include <limits.h>

/* u32 and s32: exactly 32 bits, as in math.c */
#if ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long u32;
typedef long s32;
#else
typedef unsigned int u32;
typedef int s32;
#endif

/* a double's words, as in math.c: w[0] the low one, w[1] the high one */
union dw {
    double d;
    u32 w[2];
};

/* a float's bits */
union fw {
    float f;
    u32 w;
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

static double one = 1.0;
static double tiny = 1.0e-300;
static double two54 = 1.80143985094819840000e+16;       /* 0x43500000, 0x00000000 */
static double ln2 = 6.93147180559945286227e-01;         /* 0x3FE62E42, 0xFEFA39EF */

/* in math.c */
double __trunc(double x);
double __expm1(double x);

/* The error results, as math.c's. */
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

/* a result that came out infinite from finite arguments: a range error */
static double check_inf(double r)
{
    if ((hiw(r) & 0x7FFFFFFFUL) == 0x7FF00000UL && low(r) == 0)
        errno = ERANGE;
    return r;
}

static int is_nan(double x)
{
    return (hiw(x) & 0x7FFFFFFFUL) > 0x7FF00000UL || ((hiw(x) & 0x7FFFFFFFUL) == 0x7FF00000UL && low(x) != 0);
}

/* ---- classification and comparison ------------------------------------------ */

/* fpclassify's helpers, for a double and a float (math.h's macros pick
 * by the argument's size): from the exponent field, all ones an infinity
 * or a NaN, all zeros a zero or a subnormal. */
int __fpclassify(double x)
{
    u32 h;

    h = hiw(x) & 0x7FFFFFFFUL;
    if (h >= 0x7FF00000UL)
        return h > 0x7FF00000UL || low(x) != 0 ? FP_NAN : FP_INFINITE;
    if (h < 0x00100000UL)
        return (h | low(x)) == 0 ? FP_ZERO : FP_SUBNORMAL;
    return FP_NORMAL;
}

int __fpclassifyf(float x)
{
    union fw u;

    u.f = x;
    u.w = u.w & 0x7FFFFFFFUL;
    if (u.w >= 0x7F800000UL)
        return u.w > 0x7F800000UL ? FP_NAN : FP_INFINITE;
    if (u.w < 0x00800000UL)
        return u.w == 0 ? FP_ZERO : FP_SUBNORMAL;
    return FP_NORMAL;
}

int __signbit(double x)
{
    return (int)(hiw(x) >> 31);
}

int __signbitf(float x)
{
    union fw u;

    u.f = x;
    return (int)(u.w >> 31);
}

/* The comparison macros' helpers: C's own comparisons are already
 * false for a NaN, and raise nothing here, as there are no exceptions.
 * A float argument becomes a double exactly. */
int __isgreater(double x, double y)
{
    return x > y;
}

int __isgreaterequal(double x, double y)
{
    return x >= y;
}

int __isless(double x, double y)
{
    return x < y;
}

int __islessequal(double x, double y)
{
    return x <= y;
}

int __islessgreater(double x, double y)
{
    return x < y || x > y;
}

int __isunordered(double x, double y)
{
    return x != x || y != y;
}

/* ---- sign, neighbours, differences ------------------------------------------- */

double copysign(double x, double y)
{
    return mk((hiw(x) & 0x7FFFFFFFUL) | (hiw(y) & 0x80000000UL), low(x));
}

/* The next double after x towards y: the bit pattern of |x| one up or
 * one down, as the doubles of one sign are in the order of their bits;
 * from zero, the smallest subnormal with y's sign. Overflow to an
 * infinity, and a result that is subnormal or zero, are range errors
 * (C99 7.12.11.3). */
double nextafter(double x, double y)
{
    u32 hx;
    u32 lx;
    u32 ax;
    int up;
    double r;

    if (is_nan(x) || is_nan(y))
        return x + y;
    if (x == y)
        return y;
    hx = hiw(x);
    lx = low(x);
    ax = hx & 0x7FFFFFFFUL;
    if ((ax | lx) == 0) {
        r = mk(hiw(y) & 0x80000000UL, 1);                /* the smallest subnormal */
        errno = ERANGE;
        return r;
    }
    up = (x < y) == ((hx >> 31) == 0);                  /* |x| grows */
    if (up) {
        lx = (lx + 1) & 0xFFFFFFFFUL;
        if (lx == 0)
            hx++;
    } else {
        if (lx == 0)
            hx--;
        lx = (lx - 1) & 0xFFFFFFFFUL;
    }
    r = mk(hx, lx);
    ax = hx & 0x7FFFFFFFUL;
    if (ax >= 0x7FF00000UL || ax < 0x00100000UL)
        errno = ERANGE;
    return r;
}

/* long double is double here */
double nexttoward(double x, long double y)
{
    return nextafter(x, (double)y);
}

/* x - y if x > y, else +0 */
double fdim(double x, double y)
{
    if (is_nan(x) || is_nan(y))
        return x + y;
    return x > y ? check_inf(x - y) : 0.0;
}

/* The larger and the smaller; a NaN loses to a number (C99 F.9.9.2), and
 * +0 counts as larger than -0. */
double fmax(double x, double y)
{
    if (is_nan(x))
        return y;
    if (is_nan(y))
        return x;
    if (x == y)
        return hiw(x) >> 31 ? y : x;
    return x > y ? x : y;
}

double fmin(double x, double y)
{
    if (is_nan(x))
        return y;
    if (is_nan(y))
        return x;
    if (x == y)
        return hiw(x) >> 31 ? x : y;
    return x < y ? x : y;
}

/* A quiet NaN; the string, which C99 leaves to the implementation, is
 * not used. */
double nan(const char *tag)
{
    return mk(0x7FF80000UL, 0);
}

/* ---- rounding to integers ------------------------------------------------------ */

double trunc(double x)
{
    return __trunc(x);
}

/* Half way rounds away from zero: x - trunc(x) is exact. */
double round(double x)
{
    double t;
    double f;

    t = __trunc(x);
    f = x - t;
    if (f >= 0.5)
        return t + 1.0;
    if (f <= -0.5)
        return t - 1.0;
    return t;
}

/* To nearest, half way to even (the only rounding there is): adding and
 * taking away 2^52 does it, as every double from 2^52 up is an integer.
 * The sign is put back, so that rint(-0.25) is -0. */
static double two52 = 4.50359962737049600000e+15;       /* 0x43300000, 0x00000000 */

double rint(double x)
{
    u32 h;
    double r;

    h = hiw(x) & 0x7FFFFFFFUL;
    if (h >= 0x7FF00000UL)
        return x + x;                                   /* an infinity, or a NaN made quiet */
    if (h >= 0x43300000UL)
        return x;                                       /* 2^52 and up: an integer already */
    r = (two52 + fabs(x)) - two52;
    return copysign(r, x);
}

/* rint, with no inexact exception to leave out */
double nearbyint(double x)
{
    return rint(x);
}

/* The long forms: a value out of the type's range gives an unspecified
 * result (C99 7.12.9.5), here the conversion's, saturated. */
long lrint(double x)
{
    return (long)rint(x);
}

long long llrint(double x)
{
    return (long long)rint(x);
}

long lround(double x)
{
    return (long)round(x);
}

long long llround(double x)
{
    return (long long)round(x);
}

/* ---- remainders ------------------------------------------------------------------ */

/* x - n y with n the integer nearest x/y, half way to even (fdlibm's
 * e_remainder): fmod by 2|y| first leaves |x| below 2|y|, then at most two
 * subtractions of |y| bring it within |y|/2. Exact. */
double remainder(double x, double y)
{
    double p;
    double ax;
    double half_p;
    u32 sx;

    if (is_nan(x) || is_nan(y))
        return x + y;
    if ((hiw(x) & 0x7FFFFFFFUL) >= 0x7FF00000UL || y == 0)
        return nan_result();
    sx = hiw(x) & 0x80000000UL;
    p = fabs(y);
    if ((hiw(p) & 0x7FFFFFFFUL) < 0x7FE00000UL)        /* 2p does not overflow */
        x = fmod(x, p + p);
    ax = fabs(x);
    if ((hiw(p) & 0x7FFFFFFFUL) < 0x00200000UL) {      /* p small: p/2 would lose a bit */
        if (ax + ax > p) {
            ax = ax - p;
            if (ax + ax >= p)
                ax = ax - p;
        }
    } else {
        half_p = 0.5 * p;
        if (ax > half_p) {
            ax = ax - p;
            if (ax >= half_p)
                ax = ax - p;
        }
    }
    return mk(hiw(ax) ^ sx, low(ax));
}

/* remainder's result, and in *quo the low 16 bits of the quotient's
 * magnitude n with the sign of x/y (C99 asks for at least 3). The
 * quotient's bits come from fmod's long division (math.c), done here:
 * each step takes away the largest |y| 2^k not above the remainder,
 * adding 2^k to n, which only its low bits are kept of. */
double remquo(double x, double y, int *quo)
{
    double r;
    double ay;
    double t;
    int ex;
    int ey;
    int k;
    unsigned q;
    int neg;

    *quo = 0;
    if (is_nan(x) || is_nan(y))
        return x + y;
    if ((hiw(x) & 0x7FFFFFFFUL) >= 0x7FF00000UL || y == 0)
        return nan_result();
    neg = (hiw(x) >> 31) != (hiw(y) >> 31);
    r = fabs(x);
    ay = fabs(y);
    q = 0;
    if ((hiw(ay) & 0x7FFFFFFFUL) >= 0x7FF00000UL) {
        /* y infinite: x itself, quotient 0 */
    } else if (r >= ay) {
        frexp(ay, &ey);
        while (r >= ay) {
            frexp(r, &ex);
            k = ex - ey;
            t = ldexp(ay, k);
            if (t > r) {
                k--;
                t = ldexp(ay, k);
            }
            r = r - t;
            if (k < 16)
                q = (q + (1U << k)) & 0xFFFF;
        }
    }
    /* now 0 <= r < |y|: to the nearest multiple, half way to even (r + r
     * is exact, and an overflow to infinity still compares right) */
    if (r + r > ay || (r + r == ay && (q & 1))) {
        r = r - ay;
        q = (q + 1) & 0xFFFF;
    }
    if (hiw(x) >> 31)
        r = -r;
    *quo = neg ? -(int)q : (int)q;
    return r;
}

/* ---- exponents ----------------------------------------------------------------------- */

/* The unbiased exponent of x, as an int: frexp's less one. 0 gives
 * FP_ILOGB0, an infinity INT_MAX and a NaN FP_ILOGBNAN, each a domain
 * error (C99 7.12.6.5). */
int ilogb(double x)
{
    int e;
    u32 h;

    h = hiw(x) & 0x7FFFFFFFUL;
    if (h >= 0x7FF00000UL) {
        errno = EDOM;
        return h > 0x7FF00000UL || low(x) != 0 ? FP_ILOGBNAN : INT_MAX;
    }
    if ((h | low(x)) == 0) {
        errno = EDOM;
        return FP_ILOGB0;
    }
    frexp(x, &e);
    return e - 1;
}

/* The same as a double; logb(0) is -infinity, a pole error. */
double logb(double x)
{
    int e;
    u32 h;

    h = hiw(x) & 0x7FFFFFFFUL;
    if (h >= 0x7FF00000UL)
        return x * x;                                   /* inf for an infinity, the NaN for a NaN */
    if ((h | low(x)) == 0)
        return overflow(1);
    frexp(x, &e);
    return (double)(e - 1);
}

/* x 2^n: FLT_RADIX is 2, so ldexp */
double scalbn(double x, int n)
{
    return ldexp(x, n);
}

/* ldexp clamps beyond +-50000, where every result has over- or
 * underflowed, so a long n can be clamped there too */
double scalbln(double x, long n)
{
    if (n > 50000L)
        n = 50000L;
    if (n < -50000L)
        n = -50000L;
    return ldexp(x, (int)n);
}

/* ---- cbrt and hypot ------------------------------------------------------------------ */

/* The cube root (fdlibm's s_cbrt): a first guess to 5 bits by dividing
 * the high word by 3 (as the exponent field is a logarithm), a rational
 * step to 23 bits, then the guess chopped to 20 bits and made larger than
 * the root, and one Newton step in the form t + t (x/t^2 - t)/(2t + x/t^2),
 * whose error is below 0.667 ulp. A subnormal is scaled by 2^54 first. */
static u32 B1 = 715094163UL;    /* B1 = (682 - 0.03306235651) 2^20 */
static u32 B2 = 696219795UL;    /* B2 = (664 - 0.03306235651) 2^20 */
static double C = 5.42857142857142815906e-01;           /* 19/35: 0x3FE15F15, 0xF15F15F1 */
static double D = -7.05306122448979611050e-01;          /* -864/1225: 0xBFE691DE, 0x2532C834 */
static double E = 1.41428571428571436819e+00;           /* 99/70: 0x3FF6A0EA, 0x0EA0EA0F */
static double F = 1.60714285714285720630e+00;           /* 45/28: 0x3FF9B6DB, 0x6DB6DB6E */
static double G = 3.57142857142857150787e-01;           /* 5/14: 0x3FD6DB6D, 0xB6DB6DB7 */

double cbrt(double x)
{
    u32 hx;
    u32 sign;
    double r;
    double s;
    double t;
    double w;

    hx = hiw(x);
    sign = hx & 0x80000000UL;
    hx = hx & 0x7FFFFFFFUL;
    if (hx >= 0x7FF00000UL)
        return x + x;                                   /* an infinity or a NaN */
    if ((hx | low(x)) == 0)
        return x;                                       /* +-0 */
    x = mk(hx, low(x));                                 /* |x| */
    if (hx < 0x00100000UL) {
        t = two54 * x;                                  /* subnormal */
        t = mk(hiw(t) / 3 + B2, 0);
    } else {
        t = mk(hx / 3 + B1, 0);
    }
    r = t * t / x;
    s = C + r * t;
    t = t * (G + F / (s + E + D / s));
    t = mk(hiw(t) + 1, 0);                              /* chopped, and just above the root */
    s = t * t;
    r = x / s;
    w = t + t;
    r = (r - t) / (w + r);
    t = t + t * r;
    return mk(hiw(t) | sign, low(t));
}

/* sqrt(x^2 + y^2) without overflow or underflow on the way (fdlibm's
 * e_hypot): scaled by 2^-600 or 2^600 when large or small, then the sum
 * of squares formed exactly enough from halves of the operands (their
 * high words alone, t1 or y1, and the rest) so that the error stays below
 * one ulp. If one operand is 2^60 times the other, the sum is the answer. */
double hypot(double x, double y)
{
    double a;
    double b;
    double t1;
    double t2;
    double y1;
    double y2;
    double w;
    u32 ha;
    u32 hb;
    s32 k;

    ha = hiw(x) & 0x7FFFFFFFUL;
    hb = hiw(y) & 0x7FFFFFFFUL;
    if (hb > ha) {
        a = y;
        b = x;
        k = (s32)ha;
        ha = hb;
        hb = (u32)k;
    } else {
        a = x;
        b = y;
    }
    a = mk(ha, low(a));                                 /* |a| */
    b = mk(hb, low(b));                                 /* |b| */
    if (ha - hb > 0x3C00000UL)
        return a + b;                                   /* a/b > 2^60 */
    k = 0;
    if (ha > 0x5F300000UL) {                            /* a > 2^500 */
        if (ha >= 0x7FF00000UL) {                       /* an infinity or a NaN */
            w = a + b;
            if (((ha & 0xFFFFFUL) | low(a)) == 0)
                w = a;                                  /* an infinity wins over a NaN */
            if (((hb ^ 0x7FF00000UL) | low(b)) == 0)
                w = b;
            return w;
        }
        ha = ha - 0x25800000UL;                         /* both by 2^-600 */
        hb = hb - 0x25800000UL;
        k = k + 600;
        a = mk(ha, low(a));
        b = mk(hb, low(b));
    }
    if (hb < 0x20B00000UL) {                            /* b < 2^-500 */
        if (hb <= 0x000FFFFFUL) {                       /* subnormal, or 0 */
            if ((hb | low(b)) == 0)
                return a;
            t1 = mk(0x7FD00000UL, 0);                   /* 2^1022 */
            b = b * t1;
            a = a * t1;
            k = k - 1022;
            ha = hiw(a);
            hb = hiw(b);
        } else {
            ha = ha + 0x25800000UL;                     /* both by 2^600 */
            hb = hb + 0x25800000UL;
            k = k - 600;
            a = mk(ha, low(a));
            b = mk(hb, low(b));
        }
    }
    w = a - b;
    if (w > b) {
        t1 = mk(ha, 0);
        t2 = a - t1;
        w = sqrt(t1 * t1 - (b * (-b) - t2 * (a + t1)));
    } else {
        a = a + a;
        y1 = mk(hb, 0);
        y2 = b - y1;
        t1 = mk(ha + 0x00100000UL, 0);
        t2 = a - t1;
        w = sqrt(t1 * y1 - (w * (-w) - (t1 * y2 + t2 * b)));
    }
    if (k != 0)
        w = mk(0x3FF00000UL + ((u32)k << 20), 0) * w;  /* 2^k */
    return check_inf(w);
}

/* ---- logarithms and exponentials --------------------------------------------- */

static double ln2_hi = 6.93147180369123816490e-01;     /* 0x3FE62E42, 0xFEE00000 */
static double ln2_lo = 1.90821492927058770002e-10;     /* 0x3DEA39EF, 0x35793C76 */
static double Lp1 = 6.666666666666735130e-01;           /* 0x3FE55555, 0x55555593 */
static double Lp2 = 3.999999999940941908e-01;           /* 0x3FD99999, 0x9997FA04 */
static double Lp3 = 2.857142874366239149e-01;           /* 0x3FD24924, 0x94229359 */
static double Lp4 = 2.222219843214978396e-01;           /* 0x3FCC71C5, 0x1D8E78AF */
static double Lp5 = 1.818357216161805012e-01;           /* 0x3FC74664, 0x96CB03DE */
static double Lp6 = 1.531383769920937332e-01;           /* 0x3FC39A09, 0xD078C69F */
static double Lp7 = 1.479819860511658591e-01;           /* 0x3FC2F112, 0xDF3E5244 */

/* log(1 + x), accurately for small x (fdlibm's s_log1p): the method of
 * math.c's log, on 1 + x = 2^k (1 + f), with c the rounding error of
 * forming 1 + x carried into the result, so that nothing is lost for x
 * near 0. log1p(-1) is -infinity (a pole error), below -1 a domain
 * error. The library's own calls (asinh, acosh, atanh) use log1p_. */
static double log1p_(double x)
{
    double hfsq;
    double f;
    double c;
    double s;
    double z;
    double R;
    double u;
    double dk;
    s32 k;
    s32 hx;
    s32 hu;
    s32 ax;

    f = 0;
    c = 0;
    hu = 0;
    hx = (s32)hiw(x);
    ax = hx & 0x7FFFFFFFL;
    k = 1;
    if (hx < 0x3FDA827AL) {                             /* x < 0.41422 */
        if (ax >= 0x3FF00000L) {                        /* x <= -1 */
            if (x == -1.0)
                return overflow(1);
            return x != x ? x : nan_result();
        }
        if (ax < 0x3E200000L) {                         /* |x| < 2^-29 */
            if (ax < 0x3C900000L)                       /* |x| < 2^-54 */
                return x;
            return x - x * x * 0.5;
        }
        if (hx > 0 || hx <= (s32)0xBFD2BEC3L) {        /* -0.2929 < x < 0.41422 */
            k = 0;
            f = x;
            hu = 1;
        }
    }
    if (hx >= 0x7FF00000L)
        return x + x;
    if (k != 0) {
        if (hx < 0x43400000L) {
            u = 1.0 + x;
            hu = (s32)hiw(u);
            k = (hu >> 20) - 1023;
            c = k > 0 ? 1.0 - (u - x) : x - (u - 1.0);  /* the rounding error of 1 + x */
            c = c / u;
        } else {
            u = x;
            hu = (s32)hiw(u);
            k = (hu >> 20) - 1023;
            c = 0;
        }
        hu = hu & 0x000FFFFFL;
        if (hu < 0x6A09EL) {
            u = mk((u32)hu | 0x3FF00000UL, low(u));     /* u into [1, sqrt 2) */
        } else {
            k = k + 1;
            u = mk((u32)hu | 0x3FE00000UL, low(u));     /* u/2 into [sqrt(2)/2, 1) */
            hu = (0x00100000L - hu) >> 2;
        }
        f = u - 1.0;
    }
    hfsq = 0.5 * f * f;
    dk = (double)k;
    if (hu == 0) {                                      /* |f| < 2^-20 */
        if (f == 0) {
            if (k == 0)
                return 0.0;
            c = c + dk * ln2_lo;
            return dk * ln2_hi + c;
        }
        R = hfsq * (1.0 - 0.66666666666666666 * f);
        if (k == 0)
            return f - R;
        return dk * ln2_hi - ((R - (dk * ln2_lo + c)) - f);
    }
    s = f / (2.0 + f);
    z = s * s;
    R = z * (Lp1 + z * (Lp2 + z * (Lp3 + z * (Lp4 + z * (Lp5 + z * (Lp6 + z * Lp7))))));
    if (k == 0)
        return f - (hfsq - s * (hfsq + R));
    return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + (dk * ln2_lo + c))) - f);
}

double log1p(double x)
{
    return log1p_(x);
}

/* math.c's, made public; its overflow is reported here */
double expm1(double x)
{
    double r;

    r = __expm1(x);
    if (r == HUGE_VAL && x != HUGE_VAL)
        errno = ERANGE;
    return r;
}

/* log2 x = k + log(m)/ln2 with x = 2^k m and m in [sqrt(2)/2, sqrt(2)),
 * so that the logarithm's part is small (|log m| < 0.35) and its error
 * shrinks beside the integer k; 1/ln2 is split in two so that the
 * product is formed with extra bits. A power of two is exact. */
static double ivln2hi = 1.44269502162933349609e+00;    /* 0x3FF71547, 0x60000000 */
static double ivln2lo = 1.92596299112661746887e-08;    /* 0x3E54AE0B, 0xF85DDF44 */

double log2(double x)
{
    double m;
    double L;
    s32 k;
    s32 hx;
    u32 lx;

    hx = (s32)hiw(x);
    lx = low(x);
    k = 0;
    if (hx < 0x00100000L) {                             /* x < 2^-1022 */
        if (((hx & 0x7FFFFFFFL) | lx) == 0)
            return overflow(1);                         /* log2(+-0) = -inf */
        if (hx < 0)
            return x != x ? x : nan_result();
        k = -54;
        x = x * two54;
        hx = (s32)hiw(x);
    }
    if (hx >= 0x7FF00000L)
        return x + x;
    k = k + (hx >> 20) - 1023;
    hx = hx & 0x000FFFFFL;
    if (hx >= 0x6A09EL) {                               /* m >= sqrt 2: halve it */
        hx = hx | 0x3FE00000L;
        k = k + 1;
    } else {
        hx = hx | 0x3FF00000L;
    }
    m = mk((u32)hx, low(x));
    L = log(m);
    return (double)k + (L * ivln2lo + L * ivln2hi);
}

/* 2^x = 2^k 2^r with k the integer nearest x and r = x - k (exact, and
 * |r| <= 0.5), 2^r being e^(r ln2); ldexp then puts in k exactly. An
 * integer x gives a power of two exactly. */
double exp2(double x)
{
    double k;
    u32 h;

    h = hiw(x) & 0x7FFFFFFFUL;
    if (h >= 0x7FF00000UL) {
        if (((h & 0xFFFFFUL) | low(x)) != 0)
            return x + x;                               /* NaN */
        return hiw(x) >> 31 ? 0.0 : x;                  /* 2^-inf = 0, 2^inf = inf */
    }
    if (x >= 1024.0)
        return overflow(0);
    if (x < -1075.0)
        return underflow(0);
    k = rint(x);
    x = x - k;
    return ldexp(exp(x * ln2), (int)k);
}

/* ---- inverse hyperbolic functions ---------------------------------------------- */

/* asinh x = sign(x) log(|x| + sqrt(x^2 + 1)) (fdlibm's s_asinh), in three
 * forms: log(2|x|) beyond 2^28, where the 1 does not count; log of a
 * rearranged sum from 2 to 2^28; log1p of one for small |x|, which keeps
 * its precision near 0. */
double asinh(double x)
{
    double t;
    double w;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL)
        return x + x;                                   /* an infinity or a NaN */
    if (ix < 0x3E300000UL)                              /* |x| < 2^-28 */
        return x;
    if (ix > 0x41B00000UL) {                            /* |x| > 2^28 */
        w = log(fabs(x)) + ln2;
    } else if (ix > 0x40000000UL) {                     /* 2 < |x| <= 2^28 */
        t = fabs(x);
        w = log(2.0 * t + one / (sqrt(x * x + one) + t));
    } else {                                            /* 2^-28 <= |x| <= 2 */
        t = x * x;
        w = log1p_(fabs(x) + t / (one + sqrt(one + t)));
    }
    return hx >> 31 ? -w : w;
}

/* acosh x = log(x + sqrt(x^2 - 1)) for x >= 1 (fdlibm's e_acosh), in the
 * same three forms as asinh. */
double acosh(double x)
{
    double t;
    s32 hx;

    if (is_nan(x))
        return x + x;
    hx = (s32)hiw(x);
    if (hx < 0x3FF00000L)
        return nan_result();                            /* x < 1 */
    if (hx >= 0x41B00000L) {                            /* x >= 2^28 */
        if (hx >= 0x7FF00000L)
            return x + x;                               /* an infinity */
        return log(x) + ln2;
    }
    if (((hx - 0x3FF00000L) | (s32)low(x)) == 0)
        return 0.0;                                     /* acosh(1) */
    if (hx > 0x40000000L) {                             /* 2 < x < 2^28 */
        t = x * x;
        return log(2.0 * x - one / (x + sqrt(t - one)));
    }
    t = x - one;                                        /* 1 < x <= 2 */
    return log1p_(t + sqrt(2.0 * t + t * t));
}

/* atanh x = log((1 + x)/(1 - x)) / 2 for |x| < 1 (fdlibm's e_atanh), as
 * log1p of 2x/(1 - x), one form below 1/2 and one above. +-1 are poles. */
double atanh(double x)
{
    double t;
    u32 hx;
    u32 ix;

    if (is_nan(x))
        return x + x;
    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix > 0x3FF00000UL || (ix == 0x3FF00000UL && low(x) != 0))
        return nan_result();                            /* |x| > 1 */
    if (ix == 0x3FF00000UL)
        return overflow((int)(hx >> 31));               /* +-1: a pole */
    if (ix < 0x3E300000UL)                              /* |x| < 2^-28 */
        return x;
    x = mk(ix, low(x));                                 /* |x| */
    if (ix < 0x3FE00000UL) {                            /* |x| < 0.5 */
        t = x + x;
        t = 0.5 * log1p_(t + t * x / (one - x));
    } else {
        t = 0.5 * log1p_((x + x) / (one - x));
    }
    return hx >> 31 ? -t : t;
}

/* ---- erf and erfc -------------------------------------------------------------------- */

/* The error function and its complement (fdlibm's s_erf), each by
 * rational approximations on four ranges of |x|:
 *   below 0.84375: erf x = x + x R(x^2) (pp over qq);
 *   to 1.25: erf x = erx + P(s)/Q(s) with s = |x| - 1 (pa over qa), erx
 *     being erf(1) to 24 bits;
 *   to 6 (erf) or 28 (erfc): erfc x = exp(-x^2 - 0.5625 + R(1/x^2)/S(1/x^2))/x,
 *     in two pieces split at 1/0.35 (ra over sa, rb over sb), with x^2
 *     formed from x's high word alone (z) and the rest, so that it is exact
 *     enough;
 *   beyond: erf is +-1, erfc 0 (an underflow) or 2.
 * erfc near 0, and erf beyond 0.84375, are 1 minus the other, arranged
 * so that the subtraction does not cancel. */
static double erx = 8.45062911510467529297e-01;        /* 0x3FEB0AC1, 0x60000000 */
static double efx = 1.28379167095512586316e-01;        /* 0x3FC06EBA, 0x8214DB69 */
static double efx8 = 1.02703333676410069053e+00;       /* 0x3FF06EBA, 0x8214DB69 */
static double pp0 = 1.28379167095512558561e-01;        /* 0x3FC06EBA, 0x8214DB68 */
static double pp1 = -3.25042107247001499370e-01;       /* 0xBFD4CD7D, 0x691CB913 */
static double pp2 = -2.84817495755985104766e-02;       /* 0xBF9D2A51, 0xDBD7194F */
static double pp3 = -5.77027029648944159157e-03;       /* 0xBF77A291, 0x236668E4 */
static double pp4 = -2.37630166566501626084e-05;       /* 0xBEF8EAD6, 0x120016AC */
static double qq1 = 3.97917223959155352819e-01;        /* 0x3FD97779, 0xCDDADC09 */
static double qq2 = 6.50222499887672944485e-02;        /* 0x3FB0A54C, 0x5536CEBA */
static double qq3 = 5.08130628187576562776e-03;        /* 0x3F74D022, 0xC4D36B0F */
static double qq4 = 1.32494738004321644526e-04;        /* 0x3F215DC9, 0x221C1A10 */
static double qq5 = -3.96022827877536812320e-06;       /* 0xBED09C43, 0x42A26120 */
static double pa0 = -2.36211856075265944077e-03;       /* 0xBF6359B8, 0xBEF77538 */
static double pa1 = 4.14856118683748331666e-01;        /* 0x3FDA8D00, 0xAD92B34D */
static double pa2 = -3.72207876035701323847e-01;       /* 0xBFD7D240, 0xFBB8C3F1 */
static double pa3 = 3.18346619901161753674e-01;        /* 0x3FD45FCA, 0x805120E4 */
static double pa4 = -1.10894694282396677476e-01;       /* 0xBFBC6398, 0x3D3E28EC */
static double pa5 = 3.54783043256182359371e-02;        /* 0x3FA22A36, 0x599795EB */
static double pa6 = -2.16637559486879084300e-03;       /* 0xBF61BF38, 0x0A96073F */
static double qa1 = 1.06420880400844228286e-01;        /* 0x3FBB3E66, 0x18EEE323 */
static double qa2 = 5.40397917702171048937e-01;        /* 0x3FE14AF0, 0x92EB6F33 */
static double qa3 = 7.18286544141962662868e-02;        /* 0x3FB2635C, 0xD99FE9A7 */
static double qa4 = 1.26171219808761642112e-01;        /* 0x3FC02660, 0xE763351F */
static double qa5 = 1.36370839120290507362e-02;        /* 0x3F8BEDC2, 0x6B51DD1C */
static double qa6 = 1.19844998467991074170e-02;        /* 0x3F888B54, 0x5735151D */
static double ra0 = -9.86494403484714822705e-03;       /* 0xBF843412, 0x600D6435 */
static double ra1 = -6.93858572707181764372e-01;       /* 0xBFE63416, 0xE4BA7360 */
static double ra2 = -1.05586262253232909814e+01;       /* 0xC0251E04, 0x41B0E726 */
static double ra3 = -6.23753324503260060396e+01;       /* 0xC04F300A, 0xE4CBA38D */
static double ra4 = -1.62396669462573470355e+02;       /* 0xC0644CB1, 0x84282266 */
static double ra5 = -1.84605092906711035994e+02;       /* 0xC067135C, 0xEBCCABB2 */
static double ra6 = -8.12874355063065934246e+01;       /* 0xC0545265, 0x57E4D2F2 */
static double ra7 = -9.81432934416914548592e+00;       /* 0xC023A0EF, 0xC69AC25C */
static double sa1 = 1.96512716674392571292e+01;        /* 0x4033A6B9, 0xBD707687 */
static double sa2 = 1.37657754143519042600e+02;        /* 0x4061350C, 0x526AE721 */
static double sa3 = 4.34565877475229228821e+02;        /* 0x407B290D, 0xD58A1A71 */
static double sa4 = 6.45387271733267880336e+02;        /* 0x40842B19, 0x21EC2868 */
static double sa5 = 4.29008140027567833386e+02;        /* 0x407AD021, 0x57700314 */
static double sa6 = 1.08635005541779435134e+02;        /* 0x405B28A3, 0xEE48AE2C */
static double sa7 = 6.57024977031928170135e+00;        /* 0x401A47EF, 0x8E484A93 */
static double sa8 = -6.04244152148580987438e-02;       /* 0xBFAEEFF2, 0xEE749A62 */
static double rb0 = -9.86494292470009928597e-03;       /* 0xBF843412, 0x39E86F4A */
static double rb1 = -7.99283237680523006574e-01;       /* 0xBFE993BA, 0x70C285DE */
static double rb2 = -1.77579549177547519889e+01;       /* 0xC031C209, 0x555F995A */
static double rb3 = -1.60636384855821916062e+02;       /* 0xC064145D, 0x43C5ED98 */
static double rb4 = -6.37566443368389627722e+02;       /* 0xC083EC88, 0x1375F228 */
static double rb5 = -1.02509513161107724954e+03;       /* 0xC0900461, 0x6A2E5992 */
static double rb6 = -4.83519191608651397019e+02;       /* 0xC07E384E, 0x9BDC383F */
static double sb1 = 3.03380607434824582924e+01;        /* 0x403E568B, 0x261D5190 */
static double sb2 = 3.25792512996573918826e+02;        /* 0x40745CAE, 0x221B9F0A */
static double sb3 = 1.53672958608443695994e+03;        /* 0x409802EB, 0x189D5118 */
static double sb4 = 3.19985821950859553908e+03;        /* 0x40A8FFB7, 0x688C246A */
static double sb5 = 2.55305040643316442583e+03;        /* 0x40A3F219, 0xCEDF3BE6 */
static double sb6 = 4.74528541206955367215e+02;        /* 0x407DA874, 0xE79FE763 */
static double sb7 = -2.24409524465858183362e+01;       /* 0xC03670E2, 0x42712D62 */

/* erfc(x) x e^(x^2) for 1.25 <= |x| (the factor that both erf and erfc
 * use beyond 1.25): split selects the piece, ra/sa below 1/0.35. */
static double erfc_tail(double x, int first)
{
    double s;
    double R;
    double S;
    double z;

    s = one / (x * x);
    if (first) {
        R = ra0 + s * (ra1 + s * (ra2 + s * (ra3 + s * (ra4 + s * (ra5 + s * (ra6 + s * ra7))))));
        S = one + s * (sa1 + s * (sa2 + s * (sa3 + s * (sa4 + s * (sa5 + s * (sa6 + s * (sa7 + s * sa8)))))));
    } else {
        R = rb0 + s * (rb1 + s * (rb2 + s * (rb3 + s * (rb4 + s * (rb5 + s * rb6)))));
        S = one + s * (sb1 + s * (sb2 + s * (sb3 + s * (sb4 + s * (sb5 + s * (sb6 + s * sb7))))));
    }
    z = mk(hiw(x), 0);
    return exp(-z * z - 0.5625) * exp((z - x) * (z + x) + R / S);
}

double erf(double x)
{
    double z;
    double r;
    double s;
    double y;
    double P;
    double Q;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL) {
        if (is_nan(x))
            return x + x;
        return hx >> 31 ? -1.0 : 1.0;
    }
    if (ix < 0x3FEB0000UL) {                            /* |x| < 0.84375 */
        if (ix < 0x3E300000UL) {                        /* |x| < 2^-28 */
            if (ix < 0x00800000UL)
                return 0.125 * (8.0 * x + efx8 * x);    /* avoids underflow */
            return x + efx * x;
        }
        z = x * x;
        r = pp0 + z * (pp1 + z * (pp2 + z * (pp3 + z * pp4)));
        s = one + z * (qq1 + z * (qq2 + z * (qq3 + z * (qq4 + z * qq5))));
        y = r / s;
        return x + x * y;
    }
    if (ix < 0x3FF40000UL) {                            /* 0.84375 <= |x| < 1.25 */
        s = fabs(x) - one;
        P = pa0 + s * (pa1 + s * (pa2 + s * (pa3 + s * (pa4 + s * (pa5 + s * pa6)))));
        Q = one + s * (qa1 + s * (qa2 + s * (qa3 + s * (qa4 + s * (qa5 + s * qa6)))));
        return hx >> 31 ? -erx - P / Q : erx + P / Q;
    }
    if (ix >= 0x40180000UL)                             /* |x| >= 6 */
        return hx >> 31 ? tiny - one : one - tiny;
    x = fabs(x);
    r = erfc_tail(x, ix < 0x4006DB6EUL);
    return hx >> 31 ? r / x - one : one - r / x;
}

double erfc(double x)
{
    double z;
    double r;
    double s;
    double y;
    double P;
    double Q;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL) {
        if (is_nan(x))
            return x + x;
        return hx >> 31 ? 2.0 : 0.0;
    }
    if (ix < 0x3FEB0000UL) {                            /* |x| < 0.84375 */
        if (ix < 0x3C700000UL)                          /* |x| < 2^-56 */
            return one - x;
        z = x * x;
        r = pp0 + z * (pp1 + z * (pp2 + z * (pp3 + z * pp4)));
        s = one + z * (qq1 + z * (qq2 + z * (qq3 + z * (qq4 + z * qq5))));
        y = r / s;
        if (hx < 0x3FD00000UL)                          /* x < 1/4 */
            return one - (x + x * y);
        r = x * y;
        r = r + (x - 0.5);
        return 0.5 - r;
    }
    if (ix < 0x3FF40000UL) {                            /* 0.84375 <= |x| < 1.25 */
        s = fabs(x) - one;
        P = pa0 + s * (pa1 + s * (pa2 + s * (pa3 + s * (pa4 + s * (pa5 + s * pa6)))));
        Q = one + s * (qa1 + s * (qa2 + s * (qa3 + s * (qa4 + s * (qa5 + s * qa6)))));
        if (hx >> 31)
            return one + (erx + P / Q);
        return (one - erx) - P / Q;
    }
    if (ix < 0x403C0000UL) {                            /* |x| < 28 */
        if (hx >> 31 && ix >= 0x40180000UL)
            return 2.0 - tiny;                          /* x <= -6 */
        x = fabs(x);
        r = erfc_tail(x, ix < 0x4006DB6DUL);
        if (hx >> 31)
            return 2.0 - r / x;
        r = r / x;
        if (r == 0)
            errno = ERANGE;
        return r;
    }
    if (hx >> 31)
        return 2.0 - tiny;
    return underflow(0);
}

/* ---- lgamma and tgamma ----------------------------------------------------------- */

/* sin(pi x) for x < 0, exactly enough at the integers (fdlibm's sin_pi
 * in e_lgamma_r): x reduced to y = |x| mod 2 by floors (an integer gives
 * exactly 0), then the octant n of y picks sin or cos of a small
 * argument. The sign is that of sin(pi x). */
static double pi = 3.14159265358979311600e+00;          /* 0x400921FB, 0x54442D18 */

static double sin_pi(double x)
{
    double y;
    double z;
    int n;
    u32 ix;

    ix = hiw(x) & 0x7FFFFFFFUL;
    if (ix < 0x3FD00000UL)
        return sin(pi * x);
    y = -x;                                             /* x is negative */
    z = floor(y);
    if (z != y) {
        y = y * 0.5;
        y = 2.0 * (y - floor(y));                       /* |x| mod 2 */
        n = (int)(y * 4.0);
    } else {
        if (ix >= 0x43400000UL) {
            y = 0;                                      /* even: every double this large is */
            n = 0;
        } else {
            if (ix < 0x43300000UL)
                z = y + two52;                          /* exact */
            n = (int)(low(z) & 1);
            y = n;
            n = n << 2;
        }
    }
    switch (n) {
    case 0:
        y = sin(pi * y);
        break;
    case 1:
    case 2:
        y = cos(pi * (0.5 - y));
        break;
    case 3:
    case 4:
        y = sin(pi * (one - y));
        break;
    case 5:
    case 6:
        y = -cos(pi * (y - 1.5));
        break;
    default:
        y = sin(pi * (y - 2.0));
        break;
    }
    return -y;
}

/* lgamma (fdlibm's e_lgamma_r), the log of |Gamma(x)|, by ranges:
 *   |x| < 2^-70: -log|x|;
 *   x < 0: log(pi / |x sin(pi x)|) - lgamma(-x), the reflection, the
 *     negative integers being poles;
 *   1 and 2: exactly 0;
 *   below 2: around the minimum tc = 1.4616 and at 1 and 2, where lgamma
 *     has zeros, three polynomials (a, t and u over v) in y, the distance
 *     from the nearest of them, below 0.9 after lgamma(x) = lgamma(x+1) -
 *     log x;
 *   below 8: a rational approximation (s over r) on [2, 3) and the
 *     recurrence lgamma(x) = log(x-1) + lgamma(x-1) back down to it;
 *   to 2^58: Stirling's formula, (x - 1/2)(log x - 1) plus a polynomial w
 *     in 1/x that approximates the rest;
 *   beyond: x (log x - 1).
 * C99's lgamma does not report the sign; *sgn gets it (tgamma uses it). */
static double a0 = 7.72156649015328655494e-02;          /* 0x3FB3C467, 0xE37DB0C8 */
static double a1 = 3.22467033424113591611e-01;          /* 0x3FD4A34C, 0xC4A60FAD */
static double a2 = 6.73523010531292681824e-02;          /* 0x3FB13E00, 0x1A5562A7 */
static double a3 = 2.05808084325167332806e-02;          /* 0x3F951322, 0xAC92547B */
static double a4 = 7.38555086081402883957e-03;          /* 0x3F7E404F, 0xB68FEFE8 */
static double a5 = 2.89051383673415629091e-03;          /* 0x3F67ADD8, 0xCCB7926B */
static double a6 = 1.19270763183362067845e-03;          /* 0x3F538A94, 0x116F3F5D */
static double a7 = 5.10069792153511336608e-04;          /* 0x3F40B6C6, 0x89B99C00 */
static double a8 = 2.20862790713908385557e-04;          /* 0x3F2CF2EC, 0xED10E54D */
static double a9 = 1.08011567247583939954e-04;          /* 0x3F1C5088, 0x987DFB07 */
static double a10 = 2.52144565451257326939e-05;         /* 0x3EFA7074, 0x428CFA52 */
static double a11 = 4.48640949618915160150e-05;         /* 0x3F07858E, 0x90A45837 */
static double tc = 1.46163214496836224576e+00;          /* 0x3FF762D8, 0x6356BE3F */
static double tf = -1.21486290535849611461e-01;         /* 0xBFBF19B9, 0xBCC38A42 */
static double tt = -3.63867699703950536541e-18;         /* 0xBC50C7CA, 0xA48A971F: -(tf's tail) */
static double t0 = 4.83836122723810047042e-01;          /* 0x3FDEF72B, 0xC8EE38A2 */
static double t1 = -1.47587722994593911752e-01;         /* 0xBFC2E427, 0x8DC6C509 */
static double t2 = 6.46249402391333854778e-02;          /* 0x3FB08B42, 0x94D5419B */
static double t3 = -3.27885410759859649565e-02;         /* 0xBFA0C9A8, 0xDF35B713 */
static double t4 = 1.79706750811820387126e-02;          /* 0x3F9266E7, 0x970AF9EC */
static double t5 = -1.03142241298341437450e-02;         /* 0xBF851F9F, 0xBA91EC6A */
static double t6 = 6.10053870246291332635e-03;          /* 0x3F78FCE0, 0xE370E344 */
static double t7 = -3.68452016781138256760e-03;         /* 0xBF6E2EFF, 0xB3E914D7 */
static double t8 = 2.25964780900612472250e-03;          /* 0x3F6282D3, 0x2E15C915 */
static double t9 = -1.40346469989232843813e-03;         /* 0xBF56FE8E, 0xBF2D1AF1 */
static double t10 = 8.81081882437654011382e-04;         /* 0x3F4CDF0C, 0xEF61A8E9 */
static double t11 = -5.38595305356740546715e-04;        /* 0xBF41A610, 0x9C73E0EC */
static double t12 = 3.15632070903625950361e-04;         /* 0x3F34AF6D, 0x6C0EBBF7 */
static double t13 = -3.12754168375120860518e-04;        /* 0xBF347F24, 0xECC38C38 */
static double t14 = 3.35529192635519073543e-04;         /* 0x3F35FD3E, 0xE8C2D3F4 */
static double u0 = -7.72156649015328655494e-02;         /* 0xBFB3C467, 0xE37DB0C8 */
static double u1 = 6.32827064025093366517e-01;          /* 0x3FE4401E, 0x8B005DFF */
static double u2 = 1.45492250137234768737e+00;          /* 0x3FF7475C, 0xD119BD6F */
static double u3 = 9.77717527963372745603e-01;          /* 0x3FEF4976, 0x44EA8450 */
static double u4 = 2.28963728064692451092e-01;          /* 0x3FCD4EAE, 0xF6010924 */
static double u5 = 1.33810918536787660377e-02;          /* 0x3F8B678B, 0xBF2BAB09 */
static double v1 = 2.45597793713041134822e+00;          /* 0x4003A5D7, 0xC2BD619C */
static double v2 = 2.12848976379893395361e+00;          /* 0x40010725, 0xA42B18F5 */
static double v3 = 7.69285150456672783825e-01;          /* 0x3FE89DFB, 0xE45050AF */
static double v4 = 1.04222645593369134254e-01;          /* 0x3FBAAE55, 0xD6537C88 */
static double v5 = 3.21709242282423911810e-03;          /* 0x3F6A5ABB, 0x57D0CF61 */
static double s0 = -7.72156649015328655494e-02;         /* 0xBFB3C467, 0xE37DB0C8 */
static double s1 = 2.14982415960608852501e-01;          /* 0x3FCB848B, 0x36E20878 */
static double s2 = 3.25778796408930981787e-01;          /* 0x3FD4D98F, 0x4F139F59 */
static double s3 = 1.46350472652464452805e-01;          /* 0x3FC2BB9C, 0xBEE5F2F7 */
static double s4 = 2.66422703033638609560e-02;          /* 0x3F9B481C, 0x7E939961 */
static double s5 = 1.84028451407337715652e-03;          /* 0x3F5E26B6, 0x7368F239 */
static double s6 = 3.19475326584100867617e-05;          /* 0x3F00BFEC, 0xDD17E945 */
static double r1 = 1.39200533467621045958e+00;          /* 0x3FF645A7, 0x62C4AB74 */
static double r2 = 7.21935547567138069525e-01;          /* 0x3FE71A18, 0x93D3DCDC */
static double r3 = 1.71933865632803078993e-01;          /* 0x3FC601ED, 0xCCFBDF27 */
static double r4 = 1.86459191715652901344e-02;          /* 0x3F9317EA, 0x742ED475 */
static double r5 = 7.77942496381893596434e-04;          /* 0x3F497DDA, 0xCA41A95B */
static double r6 = 7.32668430744625636189e-06;          /* 0x3EDEBAF7, 0xA5B38140 */
static double w0 = 4.18938533204672725052e-01;          /* 0x3FDACFE3, 0x90C97D69 */
static double w1 = 8.33333333333329678849e-02;          /* 0x3FB55555, 0x5555553B */
static double w2 = -2.77777777728775536470e-03;         /* 0xBF66C16C, 0x16B02E5C */
static double w3 = 7.93650558643019558500e-04;          /* 0x3F4A019F, 0x98CF38B6 */
static double w4 = -5.95187557450339963135e-04;         /* 0xBF4380CB, 0x8C0FE741 */
static double w5 = 8.36339918996282139126e-04;          /* 0x3F4B67BA, 0x4CDAD5D1 */
static double w6 = -1.63092934096575273989e-03;         /* 0xBF5AB89D, 0x0B9E43E4 */

static double lgamma_r(double x, int *sgn)
{
    double t;
    double y;
    double z;
    double nadj;
    double p;
    double p1;
    double p2;
    double p3;
    double q;
    double r;
    double w;
    int i;
    u32 hx;
    u32 ix;
    u32 lx;

    nadj = 0;
    *sgn = 1;
    hx = hiw(x);
    lx = low(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL)
        return x * x;                                   /* an infinity, or a NaN */
    if ((ix | lx) == 0)
        return overflow(0);                             /* a pole */
    if (ix < 0x3B900000UL) {                            /* |x| < 2^-70 */
        if (hx >> 31) {
            *sgn = -1;
            return -log(-x);
        }
        return -log(x);
    }
    if (hx >> 31) {
        if (ix >= 0x43300000UL)                         /* |x| >= 2^52: an integer */
            return overflow(0);
        t = sin_pi(x);
        if (t == 0)
            return overflow(0);                         /* a negative integer */
        nadj = log(pi / fabs(t * x));
        if (t < 0)
            *sgn = -1;
        x = -x;
    }
    if ((((ix - 0x3FF00000UL) | lx) == 0) || (((ix - 0x40000000UL) | lx) == 0)) {
        r = 0;                                          /* 1 and 2 */
    } else if (ix < 0x40000000UL) {                     /* x < 2 */
        if (ix <= 0x3FECCCCCUL) {                       /* x <= 0.9: lgamma(x) = lgamma(x+1) - log x */
            r = -log(x);
            if (ix >= 0x3FE76944UL) {
                y = one - x;
                i = 0;
            } else if (ix >= 0x3FCDA661UL) {
                y = x - (tc - one);
                i = 1;
            } else {
                y = x;
                i = 2;
            }
        } else {
            r = 0;
            if (ix >= 0x3FFBB4C3UL) {                   /* [1.7316, 2] */
                y = 2.0 - x;
                i = 0;
            } else if (ix >= 0x3FF3B4C4UL) {            /* [1.23, 1.73] */
                y = x - tc;
                i = 1;
            } else {
                y = x - one;
                i = 2;
            }
        }
        if (i == 0) {
            z = y * y;
            p1 = a0 + z * (a2 + z * (a4 + z * (a6 + z * (a8 + z * a10))));
            p2 = z * (a1 + z * (a3 + z * (a5 + z * (a7 + z * (a9 + z * a11)))));
            p = y * p1 + p2;
            r = r + (p - 0.5 * y);
        } else if (i == 1) {
            z = y * y;
            w = z * y;
            p1 = t0 + w * (t3 + w * (t6 + w * (t9 + w * t12)));
            p2 = t1 + w * (t4 + w * (t7 + w * (t10 + w * t13)));
            p3 = t2 + w * (t5 + w * (t8 + w * (t11 + w * t14)));
            p = z * p1 - (tt - w * (p2 + y * p3));
            r = r + (tf + p);
        } else {
            p1 = y * (u0 + y * (u1 + y * (u2 + y * (u3 + y * (u4 + y * u5)))));
            p2 = one + y * (v1 + y * (v2 + y * (v3 + y * (v4 + y * v5))));
            r = r + (-0.5 * y + p1 / p2);
        }
    } else if (ix < 0x40200000UL) {                     /* 2 < x < 8 */
        i = (int)x;
        y = x - (double)i;
        p = y * (s0 + y * (s1 + y * (s2 + y * (s3 + y * (s4 + y * (s5 + y * s6))))));
        q = one + y * (r1 + y * (r2 + y * (r3 + y * (r4 + y * (r5 + y * r6)))));
        r = 0.5 * y + p / q;
        z = one;                                        /* lgamma(1 + s) = log s + lgamma s */
        while (i > 2) {
            i--;
            z = z * (y + (double)i);
        }
        if (z != one)
            r = r + log(z);
    } else if (ix < 0x43900000UL) {                     /* 8 <= x < 2^58 */
        t = log(x);
        z = one / x;
        y = z * z;
        w = w0 + z * (w1 + y * (w2 + y * (w3 + y * (w4 + y * (w5 + y * w6)))));
        r = (x - 0.5) * (t - one) + w;
    } else {
        r = x * (log(x) - one);                         /* 2^58 <= x */
    }
    if (hx >> 31)
        r = nadj - r;
    return check_inf(r);
}

double lgamma(double x)
{
    int sgn;

    return lgamma_r(x, &sgn);
}

/* Gamma(x), which fdlibm does not have:
 *   a NaN, +-0 and the negative integers first (poles at +-0, domain
 *     errors at the integers), then overflow above 171.6 and underflow
 *     below -184;
 *   the integers 1 to 23: the factorial, exactly (22! is the last below
 *     2^53 with room; a product of the integers is exact until then);
 *   x < 0: the reflection, pi / (sin(pi x) (-x) Gamma(-x)), with sin_pi
 *     exact enough at the integers, and -x exact where 1 - x would round
 *     (and cost as many ulps as Gamma is steep); where Gamma(-x) would
 *     overflow, exp(lgamma) of the result's own small size instead;
 *   1 to 2: exp(lgamma(x)), lgamma being below 0.13 there, so that its
 *     error does not grow;
 *   0 to 8: from 1 to 2 by Gamma(x) = Gamma(x+1)/x, or up by the
 *     product (y)(y+1)...(x-1);
 *   8 and above: Stirling's formula as lgamma has it, Gamma(x) =
 *     x^(x-1/2) e^-(x-1/2) e^w(1/x), x^(x-1/2) taken as the square of
 *     x^((x-1/2)/2) so that it does not overflow before the result does.
 * Each is a few roundings: within a few ulps. */
static double tgamma_pos(double x)
{
    double y;
    double p;
    double h;
    double z;
    double w;
    int sgn;

    if (x < 1.0)
        return exp(lgamma_r(x + 1.0, &sgn)) / x;
    if (x < 2.0)
        return exp(lgamma_r(x, &sgn));
    if (x < 8.0) {
        y = x - floor(x) + 1.0;                         /* in [1, 2) */
        p = exp(lgamma_r(y, &sgn));
        while (y + 0.5 < x) {
            p = p * y;
            y = y + 1.0;
        }
        return p;
    }
    z = one / x;
    y = z * z;
    w = w0 + z * (w1 + y * (w2 + y * (w3 + y * (w4 + y * (w5 + y * w6)))));
    h = pow(x, 0.5 * (x - 0.5));
    /* e^-(x-1/2) and e^w apart: x - 1/2 is exact, and the sum's rounding
     * would cost as many ulps as the exponent is large */
    return h * (h * (exp(-(x - 0.5)) * exp(w)));
}

double tgamma(double x)
{
    double r;
    double f;
    int i;
    int sgn;
    u32 hx;
    u32 ix;

    hx = hiw(x);
    ix = hx & 0x7FFFFFFFUL;
    if (ix >= 0x7FF00000UL) {
        if (is_nan(x))
            return x + x;
        return hx >> 31 ? nan_result() : x;             /* Gamma(-inf) is a domain error */
    }
    if ((ix | low(x)) == 0)
        return overflow((int)(hx >> 31));               /* +-0: a pole */
    f = floor(x);
    if (x < 0 && f == x)
        return nan_result();                            /* a negative integer */
    if (x > 171.62437695630272)
        return overflow(0);
    if (x == f && x <= 23.0) {                          /* exactly the factorial */
        r = 1.0;
        for (i = 2; i < (int)x; i++)
            r = r * (double)i;
        return r;
    }
    if (ix < 0x3C900000UL)                              /* |x| < 2^-54 */
        return one / x;
    if (x > 0)
        return check_inf(tgamma_pos(x));
    if (x < -184.0)
        return underflow(((int)f & 1) != 0);           /* negative where floor(x) is odd */
    if (-x > 171.0) {
        r = exp(lgamma_r(x, &sgn));                     /* tiny: Gamma(-x) would overflow */
        if (r == 0)
            errno = ERANGE;
        return sgn < 0 ? -r : r;
    }
    return pi / (sin_pi(x) * -x * tgamma_pos(-x));
}

/* ---- fma ----------------------------------------------------------------------------- */

/* x y + z, rounded once (C99 7.12.13.1): softfp's sf64_fma (in fp.c as
 * __sf64_fma), which forms the product exactly. */
struct sf64;
void __sf64_fma(struct sf64 *r, const struct sf64 *x, const struct sf64 *y, const struct sf64 *z);

double fma(double x, double y, double z)
{
    double r;

    __sf64_fma((struct sf64 *)&r, (struct sf64 *)&x, (struct sf64 *)&y, (struct sf64 *)&z);
    if ((hiw(r) & 0x7FFFFFFFUL) == 0x7FF00000UL && low(r) == 0 && (hiw(x) & 0x7FF00000UL) != 0x7FF00000UL
        && (hiw(y) & 0x7FF00000UL) != 0x7FF00000UL && (hiw(z) & 0x7FF00000UL) != 0x7FF00000UL)
        errno = ERANGE;
    if (is_nan(r) && !is_nan(x) && !is_nan(y) && !is_nan(z))
        errno = EDOM;                                   /* inf * 0, or inf - inf */
    return r;
}
