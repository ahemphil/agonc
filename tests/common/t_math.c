/* t_math.c - lib/libc/math.c built by agonc gives the bits that the PC's
 * build of it gives (test_math.py's A test): a fixed stream of arguments
 * through every function, one checksum line per function, results and
 * errno. Then the special cases, whose values and errno C89 and IEEE 754
 * fix, <float.h>'s values and difftime, checked in both builds.
 *
 *     t_math [cases per function]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <float.h>
#include <time.h>

static unsigned long seed = 88172645UL;

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
};

static struct one ones[] = {
    { "sin", sin, -30, 20, 1 },
    { "sin-big", sin, 20, 1023, 1 },
    { "cos", cos, -30, 20, 1 },
    { "tan", tan, -30, 20, 1 },
    { "asin", asin, -30, 0, 1 },
    { "acos", acos, -30, 0, 1 },
    { "atan", atan, -30, 70, 1 },
    { "exp", exp, -30, 10, 1 },
    { "log", log, -1074, 1023, 1 },
    { "log10", log10, -1074, 1023, 1 },
    { "sinh", sinh, -30, 10, 1 },
    { "cosh", cosh, -30, 10, 1 },
    { "tanh", tanh, -30, 6, 1 },
    { "sqrt", sqrt, -1074, 1023, 1 },
    { "floor", floor, -10, 60, 1 },
    { "ceil", ceil, -10, 60, 1 },
    { "fabs", fabs, -1074, 1023, 1 }
};

static int count;
static int failures;

static void failed(void)
{
    failures++;
    printf("special %d failed\n", count);
}

/* got must have these bits, and errno this value */
static void t(double got, unsigned long hi, unsigned long lo, int err)
{
    unsigned char b[8];

    count++;
    memcpy(b, &got, 8);
    if (word(b + 4) != hi || word(b) != lo || errno != err)
        failed();
    errno = 0;
}

/* got must be a NaN, and errno this value */
static void tnan(double got, int err)
{
    count++;
    if (got == got || errno != err)
        failed();
    errno = 0;
}

static void ti(int got, int want)
{
    count++;
    if (got != want)
        failed();
}

static void specials(void)
{
    double ip;
    double zero;
    int e;

    zero = 0.0;
    errno = 0;
    t(sqrt(4.0), 0x40000000UL, 0, 0);
    tnan(sqrt(-1.0), EDOM);
    t(sqrt(-zero), 0x80000000UL, 0, 0);
    t(sqrt(HUGE_VAL), 0x7FF00000UL, 0, 0);
    t(log(zero), 0xFFF00000UL, 0, ERANGE);
    tnan(log(-1.0), EDOM);
    t(log(1.0), 0, 0, 0);
    t(log(HUGE_VAL), 0x7FF00000UL, 0, 0);
    t(log10(1.0), 0, 0, 0);
    t(log10(zero), 0xFFF00000UL, 0, ERANGE);
    tnan(log10(-2.0), EDOM);
    t(exp(zero), 0x3FF00000UL, 0, 0);
    t(exp(1000.0), 0x7FF00000UL, 0, ERANGE);
    t(exp(-1000.0), 0, 0, ERANGE);
    t(exp(-HUGE_VAL), 0, 0, 0);
    t(exp(HUGE_VAL), 0x7FF00000UL, 0, 0);
    t(pow(2.0, 10.0), 0x40900000UL, 0, 0);
    t(pow(-2.0, 3.0), 0xC0200000UL, 0, 0);
    t(pow(zero, zero), 0x3FF00000UL, 0, 0);
    tnan(pow(-8.0, 1.0 / 3.0), EDOM);
    t(pow(zero, -1.0), 0x7FF00000UL, 0, EDOM);
    t(pow(-zero, -3.0), 0xFFF00000UL, 0, EDOM);
    t(pow(10.0, 400.0), 0x7FF00000UL, 0, ERANGE);
    t(pow(-10.0, 401.0), 0xFFF00000UL, 0, ERANGE);
    t(pow(10.0, -400.0), 0, 0, ERANGE);
    t(pow(4.0, 0.5), 0x40000000UL, 0, 0);
    t(sin(zero), 0, 0, 0);
    t(sin(-zero), 0x80000000UL, 0, 0);
    t(cos(zero), 0x3FF00000UL, 0, 0);
    t(tan(-zero), 0x80000000UL, 0, 0);
    tnan(sin(HUGE_VAL), EDOM);
    tnan(cos(-HUGE_VAL), EDOM);
    tnan(tan(HUGE_VAL), EDOM);
    tnan(asin(2.0), EDOM);
    tnan(acos(-1.5), EDOM);
    t(asin(1.0), 0x3FF921FBUL, 0x54442D18UL, 0);
    t(acos(1.0), 0, 0, 0);
    t(acos(-1.0), 0x400921FBUL, 0x54442D18UL, 0);
    t(atan(HUGE_VAL), 0x3FF921FBUL, 0x54442D18UL, 0);
    t(atan(-1.0), 0xBFE921FBUL, 0x54442D18UL, 0);
    t(atan2(1.0, zero), 0x3FF921FBUL, 0x54442D18UL, 0);
    t(atan2(zero, -1.0), 0x400921FBUL, 0x54442D18UL, 0);
    t(atan2(-zero, -1.0), 0xC00921FBUL, 0x54442D18UL, 0);
    t(atan2(zero, zero), 0, 0, 0);
    t(atan2(-1.0, -1.0), 0xC002D97CUL, 0x7F3321D2UL, 0);
    t(sinh(1000.0), 0x7FF00000UL, 0, ERANGE);
    t(sinh(-1000.0), 0xFFF00000UL, 0, ERANGE);
    t(cosh(-1000.0), 0x7FF00000UL, 0, ERANGE);
    t(cosh(zero), 0x3FF00000UL, 0, 0);
    t(tanh(-HUGE_VAL), 0xBFF00000UL, 0, 0);
    t(tanh(1000.0), 0x3FF00000UL, 0, 0);
    t(floor(-0.5), 0xBFF00000UL, 0, 0);
    t(ceil(-0.5), 0x80000000UL, 0, 0);
    t(floor(2.5), 0x40000000UL, 0, 0);
    t(ceil(2.5), 0x40080000UL, 0, 0);
    t(floor(-zero), 0x80000000UL, 0, 0);
    t(ceil(1e300), 0x7E37E43CUL, 0x8800759CUL, 0);
    t(fmod(5.5, 2.0), 0x3FF80000UL, 0, 0);
    t(fmod(-5.5, 2.0), 0xBFF80000UL, 0, 0);
    t(fmod(1e300, 7.0), 0x3FF00000UL, 0, 0);
    t(fmod(-1e300, 0.1), 0xBF1D66E8UL, 0x1BC37800UL, 0);
    tnan(fmod(1.0, zero), EDOM);
    tnan(fmod(HUGE_VAL, 1.0), EDOM);
    t(frexp(8.0, &e), 0x3FE00000UL, 0, 0);
    ti(e, 4);
    t(frexp(-zero, &e), 0x80000000UL, 0, 0);
    ti(e, 0);
    t(frexp(make(0, 1), &e), 0x3FE00000UL, 0, 0);
    ti(e, -1073);
    t(ldexp(1.0, 1024), 0x7FF00000UL, 0, ERANGE);
    t(ldexp(-1.0, -1074), 0x80000000UL, 1, 0);
    t(ldexp(1.0, -1075), 0, 0, ERANGE);
    t(ldexp(3.0, -1075), 0, 2, 0);
    t(ldexp(make(0, 1), 1074), 0x3FF00000UL, 0, 0);
    t(modf(-3.5, &ip), 0xBFE00000UL, 0, 0);
    t(ip, 0xC0080000UL, 0, 0);
    t(modf(-4.0, &ip), 0x80000000UL, 0, 0);
    t(modf(HUGE_VAL, &ip), 0, 0, 0);
    t(ip, 0x7FF00000UL, 0, 0);
    t(fabs(-zero), 0, 0, 0);
    t(fabs(-HUGE_VAL), 0x7FF00000UL, 0, 0);
    /* <float.h> */
    t(FLT_MAX, 0x47EFFFFFUL, 0xE0000000UL, 0);
    t(FLT_MIN, 0x38100000UL, 0, 0);
    t(FLT_EPSILON, 0x3E800000UL, 0, 0);
    t(DBL_MAX, 0x7FEFFFFFUL, 0xFFFFFFFFUL, 0);
    t(DBL_MIN, 0x00100000UL, 0, 0);
    t(DBL_EPSILON, 0x3CB00000UL, 0, 0);
    t(LDBL_MAX, 0x7FEFFFFFUL, 0xFFFFFFFFUL, 0);
    ti(FLT_RADIX, 2);
    ti(FLT_MANT_DIG, 24);
    ti(DBL_MANT_DIG, 53);
    ti(FLT_DIG, 6);
    ti(DBL_DIG, 15);
    ti(FLT_MIN_EXP, -125);
    ti(DBL_MIN_EXP, -1021);
    ti(FLT_MAX_10_EXP, 38);
    ti(DBL_MAX_10_EXP, 308);
    ti(DBL_MIN_10_EXP, -307);
    /* difftime */
    t(difftime((time_t)100, (time_t)40), 0x404E0000UL, 0, 0);
}

int main(int argc, char **argv)
{
    long n;
    long i;
    int k;
    int e;
    double x;
    double y;
    double ip;

    n = argc > 1 ? atol(argv[1]) : 100;
    for (k = 0; k < (int)(sizeof ones / sizeof ones[0]); k++) {
        hash = 0;
        for (i = 0; i < n; i++) {
            x = gen(ones[k].emin, ones[k].emax, ones[k].neg);
            errno = 0;
            mixd(ones[k].f(x));
            mix((unsigned long)errno);
        }
        printf("%s %08lx\n", ones[k].name, hash);
    }
    hash = 0;
    for (i = 0; i < n; i++) {
        y = gen(-30, 30, 1);
        x = gen(-30, 30, 1);
        errno = 0;
        mixd(atan2(y, x));
        x = gen(-10, 10, 0);
        y = gen(-8, 7, 1);
        if (i % 4 == 0) {
            x = -x;
            y = floor(y * 8);
        }
        mixd(pow(x, y));
        x = gen(-60, 60, 1);
        y = gen(-60, 60, 1);
        mixd(fmod(x, y));
        mixd(modf(x, &ip));
        mixd(ip);
        mixd(frexp(x, &e));
        mix((unsigned long)e);
        mixd(ldexp(x, (int)(rnd() % 2200) - 1100));
        mix((unsigned long)errno);
    }
    printf("two %08lx\n", hash);
    specials();
    printf("specials %d, %d failed\n", count, failures);
    return failures != 0;
}
