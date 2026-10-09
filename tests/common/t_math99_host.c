/* t_math99_host.c - lib/libc/math99.c's and mathf.c's exactly defined
 * functions (C99's rounding functions, logb, fma, remainder, remquo,
 * copysign, fdim, fmax, fmin, nextafter, ilogb, scalbn, lrint, llround,
 * and the float forms of the exact ones), built by the PC's compiler
 * (their names am_trunc and so on: math_names.h), against the PC's own
 * libm: random arguments over each function's range, which must give the
 * same bits. (The functions with rounding errors are measured against
 * true values instead, by math_ref.py, in test_math.py's R test: the
 * PC's libm is not accurate enough to judge them.)
 *
 *     t_math99_host cases [guard]
 *
 * With "guard", each of our results in the two tables is moved one ulp
 * up first: the test must fail.
 * One line per function: name, the largest difference, how many cases
 * exceeded its tolerance. C99, for the PC only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define D1(f) double am_##f(double);
#define D2(f) double am_##f(double, double);
#define F1(n) float am_##n##f(float);
#define F2(n) float am_##n##f(float, float);
D1(trunc) D1(round) D1(rint) D1(nearbyint) D1(logb)
D2(remainder) D2(copysign) D2(fdim) D2(fmax) D2(fmin) D2(nextafter)
F1(sqrt) F1(floor) F1(ceil) F1(round) F1(trunc) F1(rint) F1(fabs)
F2(fmod) F2(nextafter)
double am_fma(double, double, double);
double am_remquo(double, double, int *);
long am_lrint(double);
long long am_llround(double);
int am_ilogb(double);
double am_scalbn(double, int);

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

static uint64_t fulps(float a, float b)
{
    int32_t oa;
    int32_t ob;

    if (a != a || b != b)
        return a != a && b != b ? 0 : UINT64_MAX;
    memcpy(&oa, &a, 4);
    memcpy(&ob, &b, 4);
    if (oa < 0)
        oa = INT32_MIN - oa;
    if (ob < 0)
        ob = INT32_MIN - ob;
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
    double add;         /* added to the argument (acosh: from 1) */
    int tol;            /* ulps; 0: exact */
};

static struct one ones[] = {
    { "trunc", am_trunc, trunc, -10, 60, 1, 0, 0 },
    { "round", am_round, round, -10, 60, 1, 0, 0 },
    { "rint", am_rint, rint, -10, 60, 1, 0, 0 },
    { "nearbyint", am_nearbyint, nearbyint, -10, 60, 1, 0, 0 },
    { "logb", am_logb, logb, -1074, 1023, 1, 0, 0 }
};

struct onef {
    const char *name;
    float (*ours)(float);
    float (*ref)(float);
    int emin;
    int emax;
    int neg;
    float add;
    int tol;
};

static struct onef fones[] = {
    { "sqrtf", am_sqrtf, sqrtf, -149, 127, 0, 0, 0 },
    { "floorf", am_floorf, floorf, -10, 30, 1, 0, 0 },
    { "ceilf", am_ceilf, ceilf, -10, 30, 1, 0, 0 },
    { "roundf", am_roundf, roundf, -10, 30, 1, 0, 0 },
    { "truncf", am_truncf, truncf, -10, 30, 1, 0, 0 },
    { "rintf", am_rintf, rintf, -10, 30, 1, 0, 0 },
    { "fabsf", am_fabsf, fabsf, -149, 127, 1, 0, 0 },
};

static int report(const char *name, uint64_t worst, long over)
{
    printf("%-12s max %llu, over %ld\n", name, (unsigned long long)worst, over);
    return over != 0;
}

/* a random float of an exponent in [emin, emax] */
static float genf(int emin, int emax, int neg)
{
    return (float)gen(emin, emax, neg);
}

int main(int argc, char **argv)
{
    long cases;
    long i;
    int guard;
    uint64_t d;
    uint64_t worst;
    long over;
    int k;
    int bad;
    int q1;
    int q2;
    double x;
    double y;
    double z;
    float fx;
    float fy;

    cases = argc > 1 ? atol(argv[1]) : 100000;
    guard = argc > 2 && strcmp(argv[2], "guard") == 0;
    bad = 0;
    for (k = 0; k < (int)(sizeof ones / sizeof ones[0]); k++) {
        worst = 0;
        over = 0;
        for (i = 0; i < cases; i++) {
            x = gen(ones[k].emin, ones[k].emax, ones[k].neg) + ones[k].add;
            /* the guard moves our result one ulp up: that must be reported */
            d = ulps(guard ? nextafter(ones[k].ours(x), INFINITY) : ones[k].ours(x), ones[k].ref(x));
            if (d > worst)
                worst = d;
            if (d > (uint64_t)ones[k].tol) {
                if (over < 3)
                    printf("  %s(%.17g): %.17g, the PC %.17g\n", ones[k].name, x, ones[k].ours(x), ones[k].ref(x));
                over++;
            }
        }
        bad += report(ones[k].name, worst, over);
    }
    for (k = 0; k < (int)(sizeof fones / sizeof fones[0]); k++) {
        worst = 0;
        over = 0;
        for (i = 0; i < cases; i++) {
            fx = genf(fones[k].emin, fones[k].emax, fones[k].neg) + fones[k].add;
            d = fulps(guard ? nextafterf(fones[k].ours(fx), INFINITY) : fones[k].ours(fx), fones[k].ref(fx));
            if (d > worst)
                worst = d;
            if (d > (uint64_t)fones[k].tol) {
                if (over < 3)
                    printf("  %s(%.9g): %.9g, the PC %.9g\n", fones[k].name, fx, fones[k].ours(fx),
                           fones[k].ref(fx));
                over++;
            }
        }
        bad += report(fones[k].name, worst, over);
    }
    /* two and three arguments, and the integer results */
    worst = 0;
    over = 0;
    for (i = 0; i < cases; i++) {
        x = gen(-60, 60, 1);
        y = gen(-60, 60, 1);
        z = gen(-60, 60, 1);
        if (i % 4 == 0)
            z = -x * y;                                 /* cancellation */
        d = ulps(am_fma(x, y, z), fma(x, y, z));
        d += ulps(am_remainder(x, y), remainder(x, y));
        q1 = 0;
        q2 = 0;
        d += ulps(am_remquo(x, y, &q1), remquo(x, y, &q2));
        d += (uint64_t)((q1 & 7) != (q2 & 7) || (q1 < 0) != (q2 < 0));
        d += ulps(am_copysign(x, y), copysign(x, y)) + ulps(am_fdim(x, y), fdim(x, y));
        d += ulps(am_fmax(x, y), fmax(x, y)) + ulps(am_fmin(x, y), fmin(x, y));
        d += ulps(am_nextafter(x, y), nextafter(x, y));
        d += (uint64_t)(am_ilogb(x) != ilogb(x)) + ulps(am_scalbn(x, (int)(i % 200) - 100), scalbn(x, (int)(i % 200) - 100));
        x = gen(-10, 29, 1);                            /* within a 32-bit long */
        d += (uint64_t)(am_lrint(x) != lrint(x)) + (uint64_t)(am_llround(x) != llround(x));
        if (d > worst)
            worst = d;
        if (d != 0) {
            if (over < 3)
                printf("  fma/remainder/remquo/... (%.17g, %.17g, %.17g)\n", x, y, z);
            over++;
        }
    }
    bad += report("exact", worst, over);
    worst = 0;
    over = 0;
    for (i = 0; i < cases; i++) {
        fx = genf(-30, 30, 1);
        fy = genf(-30, 30, 1);
        d = fulps(am_fmodf(fx, fy), fmodf(fx, fy)) + fulps(am_nextafterf(fx, fy), nextafterf(fx, fy));
        if (d > worst)
            worst = d;
        if (d != 0) {
            if (over < 3)
                printf("  fmodf/nextafterf(%.9g, %.9g)\n", fx, fy);
            over++;
        }
    }
    bad += report("fmodf etc", worst, over);
    printf("%d functions over\n", bad);
    return bad != 0;
}
