/* t_math99.c - lib/libc/math99.c and mathf.c built by agonc give the bits
 * that the PC's build of them gives (test_math.py's B test): a fixed
 * stream of arguments through every C99 function, double and float, one
 * checksum line per function, results and errno; then the special cases,
 * whose values and errno C99 and IEEE 754 fix, and the classification and
 * comparison macros, checked in both builds. (The PC's build uses the
 * PC's macros, so the device's are checked against them.)
 *
 *     t_math99 [cases per function]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <float.h>

static unsigned long seed = 1993UL;

static unsigned long rnd(void)
{
    seed ^= (seed << 13) & 0xFFFFFFFFUL;
    seed ^= seed >> 17;
    seed ^= (seed << 5) & 0xFFFFFFFFUL;
    return seed & 0xFFFFFFFFUL;
}

static unsigned long hash;

static void mix(unsigned long v)
{
    hash = (hash * 31 + (v & 0xFFFFFFFFUL)) & 0xFFFFFFFFUL;
}

static unsigned long word(unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static void put_word(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 255);
    p[1] = (unsigned char)(v >> 8 & 255);
    p[2] = (unsigned char)(v >> 16 & 255);
    p[3] = (unsigned char)(v >> 24 & 255);
}

static double make(unsigned long hi, unsigned long lo)
{
    double d;
    unsigned char b[8];

    put_word(b, lo);
    put_word(b + 4, hi);
    memcpy(&d, b, 8);
    return d;
}

static void mixd(double d)
{
    unsigned char b[8];

    memcpy(b, &d, 8);
    mix(word(b));
    mix(word(b + 4));
}

static void mixf(float f)
{
    unsigned char b[4];

    memcpy(b, &f, 4);
    mix(word(b));
}

/* an exponent in [emin, emax] (below -1022: a subnormal), negative too
 * if neg */
static double gen(int emin, int emax, int neg)
{
    long e;
    unsigned long hi;

    e = emin + (long)(rnd() % (unsigned long)(emax - emin + 1));
    hi = rnd() & 0xFFFFFUL;
    if (e >= -1022)
        hi = hi | (unsigned long)(e + 1023) << 20;
    if (neg && (rnd() & 1))
        hi = hi | 0x80000000UL;
    return make(hi, rnd());
}

struct one {
    char *name;
    double (*f)(double);
    int emin;
    int emax;
    int neg;
    double add;
};

static struct one ones[] = {
    { "cbrt", cbrt, -1074, 1023, 1, 0 },
    { "log1p", log1p, -40, 20, 0, 0 },
    { "expm1", expm1, -40, 9, 1, 0 },
    { "log2", log2, -1074, 1023, 0, 0 },
    { "exp2", exp2, -30, 10, 1, 0 },
    { "asinh", asinh, -30, 30, 1, 0 },
    { "acosh", acosh, -30, 30, 0, 1 },
    { "atanh", atanh, -30, -1, 1, 0 },
    { "erf", erf, -30, 2, 1, 0 },
    { "erfc", erfc, -30, 4, 1, 0 },
    { "lgamma", lgamma, -20, 7, 1, 0 },
    { "tgamma", tgamma, -20, 7, 1, 0 },
    { "trunc", trunc, -10, 60, 1, 0 },
    { "round", round, -10, 60, 1, 0 },
    { "rint", rint, -10, 60, 1, 0 },
    { "logb", logb, -1074, 1023, 1, 0 }
};

struct onef {
    char *name;
    float (*f)(float);
    int emin;
    int emax;
    int neg;
};

static struct onef fones[] = {
    { "sinf", sinf, -20, 20, 1 },
    { "expf", expf, -20, 6, 1 },
    { "logf", logf, -126, 127, 0 },
    { "sqrtf", sqrtf, -126, 127, 0 },
    { "cbrtf", cbrtf, -126, 127, 1 },
    { "erff", erff, -20, 2, 1 },
    { "tgammaf", tgammaf, -20, 5, 0 },
    { "floorf", floorf, -10, 30, 1 }
};

static int count;
static int failures;

static void failed(void)
{
    failures++;
    printf("special %d failed\n", count);
}

/* got must be want exactly (both NaN counts), and errno e */
static void same(double got, double want, int e)
{
    count++;
    if (!(got == want || (got != got && want != want)) || errno != e || signbit(got) != signbit(want))
        failed();
    errno = 0;
}

static void yes(int c)
{
    count++;
    if (!c)
        failed();
    errno = 0;
}

static void specials1(void);
static void specials2(void);
static void specials3(void);

int main(int argc, char **argv)
{
    long cases;
    long i;
    int k;
    int q;
    double x;
    double y;
    double z;
    float fx;

    cases = argc > 1 ? atol(argv[1]) : 40;
    for (k = 0; k < (int)(sizeof ones / sizeof ones[0]); k++) {
        hash = 0;
        for (i = 0; i < cases; i++) {
            errno = 0;
            x = gen(ones[k].emin, ones[k].emax, ones[k].neg) + ones[k].add;
            mixd(ones[k].f(x));
            mix((unsigned long)errno);
        }
        printf("%s %08lx\n", ones[k].name, hash);
    }
    for (k = 0; k < (int)(sizeof fones / sizeof fones[0]); k++) {
        hash = 0;
        for (i = 0; i < cases; i++) {
            errno = 0;
            fx = (float)gen(fones[k].emin, fones[k].emax, fones[k].neg);
            mixf(fones[k].f(fx));
            mix((unsigned long)errno);
        }
        printf("%s %08lx\n", fones[k].name, hash);
    }
    hash = 0;
    for (i = 0; i < cases; i++) {
        errno = 0;
        x = gen(-40, 40, 1);
        y = gen(-40, 40, 1);
        z = gen(-40, 40, 1);
        mixd(hypot(x, y));
        mixd(fma(x, y, z));
        mixd(remainder(x, y));
        mixd(remquo(x, y, &q));
        mix((unsigned long)(q & 7));
        mixd(nextafter(x, y));
        mixd(fdim(x, y));
        mixd(fmax(x, y));
        mixd(copysign(x, y));
        mix((unsigned long)ilogb(x));
        mix((unsigned long)lround(x / 1e6));
        mixf(powf((float)fabs(x) / 16, (float)y / 1e6f));
        mix((unsigned long)errno);
    }
    printf("binary %08lx\n", hash);
    errno = 0;
    specials1();
    specials2();
    specials3();
    printf("%d special cases, %d failed\n", count, failures);
    return failures != 0;
}

/* The special cases: values and errno, in three parts (cc2 limits how
 * long one function may be). */
static void specials1(void)
{
    double inf;
    double nan_;

    inf = HUGE_VAL;
    nan_ = nan("");
    same(cbrt(-27.0), -3.0, 0);
    same(cbrt(-0.0), -0.0, 0);
    same(log1p(-1.0), -HUGE_VAL, ERANGE);
    same(log1p(-2.0), nan_, EDOM);
    same(expm1(-inf), -1.0, 0);
    same(expm1(1000.0), HUGE_VAL, ERANGE);
    same(log2(8.0), 3.0, 0);
    same(log2(0.0), -HUGE_VAL, ERANGE);
    same(log2(-1.0), nan_, EDOM);
    same(exp2(10.0), 1024.0, 0);
    same(exp2(-1074.0), make(0, 1), 0);
    same(exp2(2000.0), HUGE_VAL, ERANGE);
}

static void specials2(void)
{
    double inf;
    double nan_;
    int q;

    inf = HUGE_VAL;
    nan_ = nan("");
    same(acosh(1.0), 0.0, 0);
    same(acosh(0.5), nan_, EDOM);
    same(atanh(1.0), HUGE_VAL, ERANGE);
    same(atanh(-1.0), -HUGE_VAL, ERANGE);
    same(atanh(2.0), nan_, EDOM);
    same(asinh(-0.0), -0.0, 0);
    same(erf(inf), 1.0, 0);
    same(erfc(-inf), 2.0, 0);
    same(erf(-0.0), -0.0, 0);
    same(lgamma(1.0), 0.0, 0);
    same(lgamma(2.0), 0.0, 0);
    same(lgamma(0.0), HUGE_VAL, ERANGE);
    same(lgamma(-3.0), HUGE_VAL, ERANGE);
    same(tgamma(5.0), 24.0, 0);
    same(tgamma(23.0), 1124000727777607680000.0, 0);
    same(tgamma(0.0), HUGE_VAL, ERANGE);
    same(tgamma(-0.0), -HUGE_VAL, ERANGE);
    same(tgamma(-2.0), nan_, EDOM);
    same(tgamma(200.0), HUGE_VAL, ERANGE);
    same(trunc(-2.5), -2.0, 0);
    same(round(2.5), 3.0, 0);
    same(round(-2.5), -3.0, 0);
    same(round(-0.4), -0.0, 0);
    same(rint(2.5), 2.0, 0);
    same(rint(3.5), 4.0, 0);
    same(rint(-0.25), -0.0, 0);
    same(nearbyint(1e300), 1e300, 0);
    same(remainder(5.0, 2.0), 1.0, 0);
    same(remainder(7.0, 2.0), -1.0, 0);
    same(remainder(1.0, 0.0), nan_, EDOM);
    same(remquo(10.0, 3.0, &q), 1.0, 0);
    yes(q == 3);
    same(remquo(-10.0, 3.0, &q), -1.0, 0);
    yes(q == -3);
    same(copysign(3.0, -0.0), -3.0, 0);
    same(fmax(nan_, 2.0), 2.0, 0);
    same(fmin(1.0, nan_), 1.0, 0);
    same(fdim(1.0, 3.0), 0.0, 0);
}

static void specials3(void)
{
    double inf;
    double nan_;

    inf = HUGE_VAL;
    nan_ = nan("");
    same(nextafter(1.0, 2.0), 1.0 + DBL_EPSILON, 0);
    same(nextafter(0.0, -1.0), -make(0, 1), ERANGE);
    same(logb(8.0), 3.0, 0);
    same(logb(0.0), -HUGE_VAL, ERANGE);
    yes(ilogb(1.0) == 0 && ilogb(0x1p-1074) == -1074);
    same(scalbn(1.0, 10), 1024.0, 0);
    same(scalbln(1.0, -1075L), 0.0, ERANGE);
    same(fma(0x1p-60, 0x1p-60, 1.0), 1.0, 0);
    same(fma(1.0 + 0x1p-52, 1.0 - 0x1p-52, -1.0), -0x1p-104, 0);
    same(fma(inf, 0.0, 1.0), nan_, EDOM);
    same(hypot(3.0, 4.0), 5.0, 0);
    same(hypot(inf, nan_), HUGE_VAL, 0);
    same(hypot(1.5e308, 1.5e308), HUGE_VAL, ERANGE);
    yes(lround(-2.5) == -3 && llround(2.5) == 3 && lrint(2.5) == 2 && llrint(-3.5) == -4);
    yes(isnan(nan_) && !isnan(1.0) && isinf(-inf) && !isinf(DBL_MAX) && isfinite(0.0) && !isfinite(inf));
    yes(isnormal(1.0) && !isnormal(make(0, 1)) && !isnormal(0.0));
    yes(fpclassify(make(0, 1)) == FP_SUBNORMAL && fpclassify(0.0) == FP_ZERO && fpclassify(nan_) == FP_NAN);
    yes(fpclassify(1.0f) == FP_NORMAL && fpclassify((float)inf) == FP_INFINITE && fpclassify(1e-40f) == FP_SUBNORMAL);
    yes(signbit(-0.0) && !signbit(0.0) && signbit(-1.0f) && !signbit(2.0f));
    yes(isgreater(2.0, 1.0) && !isgreater(nan_, 1.0) && isless(1.0, 2.0) && islessequal(1.0, 1.0));
    yes(isgreaterequal(1.0, 1.0) && islessgreater(1.0, 2.0) && !islessgreater(1.0, 1.0));
    yes(isunordered(nan_, 1.0) && !isunordered(1.0, 2.0));
    yes(isinf(INFINITY) && isnan(NAN) && HUGE_VALF == INFINITY);
    errno = 0;
    yes(expf(100.0f) == INFINITY && errno == ERANGE);
    yes(expf(-200.0f) == 0 && errno == ERANGE);
    yes(nextafterf(1.0f, 2.0f) == 1.0f + FLT_EPSILON);
    yes(fmaf(2.0f, 3.0f, 1.0f) == 7.0f && lgammaf(1.0f) == 0 && tgammaf(4.0f) == 6.0f);
    yes(roundf(-1.5f) == -2.0f && truncf(1.9f) == 1.0f && cbrtf(8.0f) == 2.0f);
    yes(sinl(0.0L) == 0 && expl(0.0L) == 1 && fabsl(-2.0L) == 2.0L);
}
