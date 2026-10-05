/* t_math_host.c - lib/libc/math.c, built by the PC's compiler (its names
 * am_sin and so on: math_names.h), against the PC's own libm: random
 * arguments over each function's range, the difference in units in the
 * last place. The exact functions (sqrt, floor, ceil, fabs, fmod, modf,
 * frexp, ldexp) must agree exactly; the rest within 1 ulp, or 2 for sinh,
 * cosh and tanh (fdlibm's, whose own error reaches 1.8 ulp in tanh, where
 * the PC's is below 1) and log10 (both below 1, in opposite directions).
 *
 *     t_math_host cases [guard]
 *
 * With "guard", a tolerance of 0 for every function: the test must fail.
 *
 * One line per function: name, the largest difference, how many cases
 * exceeded its tolerance. C99 (for uint64_t), for the PC only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

double am_acos(double);
double am_asin(double);
double am_atan(double);
double am_atan2(double, double);
double am_cos(double);
double am_sin(double);
double am_tan(double);
double am_cosh(double);
double am_sinh(double);
double am_tanh(double);
double am_exp(double);
double am_frexp(double, int *);
double am_ldexp(double, int);
double am_log(double);
double am_log10(double);
double am_modf(double, double *);
double am_pow(double, double);
double am_sqrt(double);
double am_ceil(double);
double am_fabs(double);
double am_floor(double);
double am_fmod(double, double);

static uint64_t state = 88172645463325252ULL;

static uint64_t rnd(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static uint64_t bits(double x)
{
    uint64_t u;

    memcpy(&u, &x, 8);
    return u;
}

static double from(uint64_t u)
{
    double x;

    memcpy(&x, &u, 8);
    return x;
}

/* The distance in ulps (0 for two NaNs, huge for one). */
static uint64_t ulps(double a, double b)
{
    int64_t oa;
    int64_t ob;

    if (a != a || b != b)
        return a != a && b != b ? 0 : UINT64_MAX;
    oa = (int64_t)bits(a);
    ob = (int64_t)bits(b);
    if (oa < 0)
        oa = INT64_MIN - oa;
    if (ob < 0)
        ob = INT64_MIN - ob;
    return oa > ob ? (uint64_t)(oa - ob) : (uint64_t)(ob - oa);
}

/* A random double with an exponent in [emin, emax] (below -1022: a
 * subnormal), negative too if neg. */
static double gen(int emin, int emax, int neg)
{
    int e;
    uint64_t f;
    uint64_t u;

    e = emin + (int)(rnd() % (uint64_t)(emax - emin + 1));
    f = rnd() & 0xFFFFFFFFFFFFFULL;
    if (e < -1022) {
        f = (f | 0x10000000000000ULL) >> (-1022 - e);
        u = f;
    } else {
        u = (uint64_t)(e + 1023) << 52 | f;
    }
    if (neg && (rnd() & 1))
        u |= 0x8000000000000000ULL;
    return from(u);
}

struct one {
    const char *name;
    double (*ours)(double);
    double (*ref)(double);
    int emin;
    int emax;
    int neg;
    int tol;            /* ulps; 0: exact */
};

static struct one ones[] = {
    { "sin", am_sin, sin, -40, 20, 1, 1 },
    { "sin-big", am_sin, sin, 20, 1023, 1, 1 },
    { "cos", am_cos, cos, -40, 20, 1, 1 },
    { "cos-big", am_cos, cos, 20, 1023, 1, 1 },
    { "tan", am_tan, tan, -40, 20, 1, 1 },
    { "tan-big", am_tan, tan, 20, 1023, 1, 1 },
    { "asin", am_asin, asin, -40, 0, 1, 1 },
    { "acos", am_acos, acos, -40, 0, 1, 1 },
    { "atan", am_atan, atan, -40, 80, 1, 1 },
    { "exp", am_exp, exp, -40, 10, 1, 1 },
    { "log", am_log, log, -1074, 1023, 0, 1 },
    { "log-near1", am_log, log, -1, 0, 0, 1 },
    { "log10", am_log10, log10, -1074, 1023, 0, 2 },
    { "sinh", am_sinh, sinh, -40, 10, 1, 2 },
    { "cosh", am_cosh, cosh, -40, 10, 1, 2 },
    { "tanh", am_tanh, tanh, -40, 6, 1, 2 },
    { "sqrt", am_sqrt, sqrt, -1074, 1023, 0, 0 },
    { "floor", am_floor, floor, -10, 60, 1, 0 },
    { "ceil", am_ceil, ceil, -10, 60, 1, 0 },
    { "fabs", am_fabs, fabs, -1074, 1023, 1, 0 }
};

static int report(const char *name, uint64_t worst, long over)
{
    printf("%-10s max %llu, over %ld\n", name, (unsigned long long)worst, over);
    return over != 0;
}

int main(int argc, char **argv)
{
    long cases;
    long i;
    uint64_t guard;
    uint64_t d;
    uint64_t worst;
    long over;
    int k;
    int bad;
    int e1;
    int e2;
    double x;
    double y;
    double a;
    double b;
    double ia;
    double ib;

    cases = argc > 1 ? atol(argv[1]) : 100000;
    guard = argc > 2 && strcmp(argv[2], "guard") == 0;
    bad = 0;
    for (k = 0; k < (int)(sizeof ones / sizeof ones[0]); k++) {
        worst = 0;
        over = 0;
        for (i = 0; i < cases; i++) {
            x = gen(ones[k].emin, ones[k].emax, ones[k].neg);
            if (ones[k].name[0] == 'l' && ones[k].name[3] == '-')
                x = 0.5 + gen(-60, -2, 1);              /* log near 1 */
            d = ulps(ones[k].ours(x), ones[k].ref(x));
            if (d > worst)
                worst = d;
            if (d > (guard ? 0 : (uint64_t)ones[k].tol)) {
                if (over < 3)
                    printf("  %s(%.17g): %.17g, the PC %.17g\n", ones[k].name, x, ones[k].ours(x), ones[k].ref(x));
                over++;
            }
        }
        bad += report(ones[k].name, worst, over);
    }
    /* two arguments */
    worst = 0;
    over = 0;
    for (i = 0; i < cases; i++) {
        y = gen(-40, 40, 1);
        x = gen(-40, 40, 1);
        d = ulps(am_atan2(y, x), atan2(y, x));
        if (d > worst)
            worst = d;
        if (d > (guard ? 0 : 1)) {
            if (over < 3)
                printf("  atan2(%.17g, %.17g): %.17g, the PC %.17g\n", y, x, am_atan2(y, x), atan2(y, x));
            over++;
        }
    }
    bad += report("atan2", worst, over);
    worst = 0;
    over = 0;
    for (i = 0; i < cases; i++) {
        x = gen(-12, 12, 0);
        y = gen(-8, 7, 1);
        if (i % 4 == 0) {
            x = -x;
            y = (double)(long)(y * 8);                  /* a negative x to an integer */
        }
        d = ulps(am_pow(x, y), pow(x, y));
        if (d > worst)
            worst = d;
        if (d > (guard ? 0 : 1)) {
            if (over < 3)
                printf("  pow(%.17g, %.17g): %.17g, the PC %.17g\n", x, y, am_pow(x, y), pow(x, y));
            over++;
        }
    }
    bad += report("pow", worst, over);
    worst = 0;
    over = 0;
    for (i = 0; i < cases; i++) {
        x = gen(-60, 60, 1);
        y = gen(-60, 60, 1);
        a = am_fmod(x, y);
        d = ulps(a, fmod(x, y));
        a = am_modf(x, &ia);
        b = modf(x, &ib);
        d += ulps(a, b) + ulps(ia, ib);
        a = am_frexp(x, &e1);
        b = frexp(x, &e2);
        d += ulps(a, b) + (uint64_t)(e1 != e2);
        e1 = (int)(rnd() % 2200) - 1100;
        d += ulps(am_ldexp(x, e1), ldexp(x, e1));
        if (d > worst)
            worst = d;
        if (d != 0) {
            if (over < 3)
                printf("  fmod/modf/frexp/ldexp(%.17g, %.17g, %d)\n", x, y, e1);
            over++;
        }
    }
    bad += report("exact", worst, over);
    printf("%d functions over\n", bad);
    return bad != 0;
}
