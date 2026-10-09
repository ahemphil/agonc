/* mathf.c - the float and long double forms of <math.h>'s functions
 * (C99 7.12): sinf and sinl beside sin, and so on for every one.
 *
 * long double is double here, so each long double form is its double
 * function. A float form takes its argument to double exactly, calls the
 * double function, and rounds the result to float once (fl); that result
 * is within the double function's error of the true value, far below a
 * float's last place, so it is the correctly rounded float almost always.
 * A result that overflows only as a float, or underflows to zero only as
 * one, is a range error here (fl sets ERANGE). nextafterf and
 * nexttowardf step through floats themselves. fmaf rounds through double
 * (x y is exact there, the sum is rounded once to double, then to float),
 * which in rare cases differs from a single rounding.
 */

#include <math.h>
#include <errno.h>
#include <limits.h>

#if ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

union fw {
    float f;
    u32 w;
};

/* r as a float; ERANGE if only the float overflows, or only it is 0 */
static float fl(double r)
{
    float f;

    f = (float)r;
    if ((f > 3.4028234663852886e+38 || f < -3.4028234663852886e+38) && r == r
        && r <= 1.7976931348623157e+308 && r >= -1.7976931348623157e+308)
        errno = ERANGE;
    if (f == 0 && r != 0)
        errno = ERANGE;
    return f;
}

float acosf(float x)
{
    return fl(acos(x));
}

long double acosl(long double x)
{
    return acos(x);
}

float asinf(float x)
{
    return fl(asin(x));
}

long double asinl(long double x)
{
    return asin(x);
}

float atanf(float x)
{
    return fl(atan(x));
}

long double atanl(long double x)
{
    return atan(x);
}

float cosf(float x)
{
    return fl(cos(x));
}

long double cosl(long double x)
{
    return cos(x);
}

float sinf(float x)
{
    return fl(sin(x));
}

long double sinl(long double x)
{
    return sin(x);
}

float tanf(float x)
{
    return fl(tan(x));
}

long double tanl(long double x)
{
    return tan(x);
}

float acoshf(float x)
{
    return fl(acosh(x));
}

long double acoshl(long double x)
{
    return acosh(x);
}

float asinhf(float x)
{
    return fl(asinh(x));
}

long double asinhl(long double x)
{
    return asinh(x);
}

float atanhf(float x)
{
    return fl(atanh(x));
}

long double atanhl(long double x)
{
    return atanh(x);
}

float coshf(float x)
{
    return fl(cosh(x));
}

long double coshl(long double x)
{
    return cosh(x);
}

float sinhf(float x)
{
    return fl(sinh(x));
}

long double sinhl(long double x)
{
    return sinh(x);
}

float tanhf(float x)
{
    return fl(tanh(x));
}

long double tanhl(long double x)
{
    return tanh(x);
}

float expf(float x)
{
    return fl(exp(x));
}

long double expl(long double x)
{
    return exp(x);
}

float exp2f(float x)
{
    return fl(exp2(x));
}

long double exp2l(long double x)
{
    return exp2(x);
}

float expm1f(float x)
{
    return fl(expm1(x));
}

long double expm1l(long double x)
{
    return expm1(x);
}

float logf(float x)
{
    return fl(log(x));
}

long double logl(long double x)
{
    return log(x);
}

float log10f(float x)
{
    return fl(log10(x));
}

long double log10l(long double x)
{
    return log10(x);
}

float log1pf(float x)
{
    return fl(log1p(x));
}

long double log1pl(long double x)
{
    return log1p(x);
}

float log2f(float x)
{
    return fl(log2(x));
}

long double log2l(long double x)
{
    return log2(x);
}

float logbf(float x)
{
    return fl(logb(x));
}

long double logbl(long double x)
{
    return logb(x);
}

float cbrtf(float x)
{
    return fl(cbrt(x));
}

long double cbrtl(long double x)
{
    return cbrt(x);
}

float fabsf(float x)
{
    return fl(fabs(x));
}

long double fabsl(long double x)
{
    return fabs(x);
}

float sqrtf(float x)
{
    return fl(sqrt(x));
}

long double sqrtl(long double x)
{
    return sqrt(x);
}

float erff(float x)
{
    return fl(erf(x));
}

long double erfl(long double x)
{
    return erf(x);
}

float erfcf(float x)
{
    return fl(erfc(x));
}

long double erfcl(long double x)
{
    return erfc(x);
}

float lgammaf(float x)
{
    return fl(lgamma(x));
}

long double lgammal(long double x)
{
    return lgamma(x);
}

float tgammaf(float x)
{
    return fl(tgamma(x));
}

long double tgammal(long double x)
{
    return tgamma(x);
}

float ceilf(float x)
{
    return fl(ceil(x));
}

long double ceill(long double x)
{
    return ceil(x);
}

float floorf(float x)
{
    return fl(floor(x));
}

long double floorl(long double x)
{
    return floor(x);
}

float nearbyintf(float x)
{
    return fl(nearbyint(x));
}

long double nearbyintl(long double x)
{
    return nearbyint(x);
}

float rintf(float x)
{
    return fl(rint(x));
}

long double rintl(long double x)
{
    return rint(x);
}

float roundf(float x)
{
    return fl(round(x));
}

long double roundl(long double x)
{
    return round(x);
}

float truncf(float x)
{
    return fl(trunc(x));
}

long double truncl(long double x)
{
    return trunc(x);
}

float atan2f(float x, float y)
{
    return fl(atan2(x, y));
}

long double atan2l(long double x, long double y)
{
    return atan2(x, y);
}

float hypotf(float x, float y)
{
    return fl(hypot(x, y));
}

long double hypotl(long double x, long double y)
{
    return hypot(x, y);
}

float powf(float x, float y)
{
    return fl(pow(x, y));
}

long double powl(long double x, long double y)
{
    return pow(x, y);
}

float fmodf(float x, float y)
{
    return fl(fmod(x, y));
}

long double fmodl(long double x, long double y)
{
    return fmod(x, y);
}

float remainderf(float x, float y)
{
    return fl(remainder(x, y));
}

long double remainderl(long double x, long double y)
{
    return remainder(x, y);
}

float copysignf(float x, float y)
{
    return fl(copysign(x, y));
}

long double copysignl(long double x, long double y)
{
    return copysign(x, y);
}

float fdimf(float x, float y)
{
    return fl(fdim(x, y));
}

long double fdiml(long double x, long double y)
{
    return fdim(x, y);
}

float fmaxf(float x, float y)
{
    return fl(fmax(x, y));
}

long double fmaxl(long double x, long double y)
{
    return fmax(x, y);
}

float fminf(float x, float y)
{
    return fl(fmin(x, y));
}

long double fminl(long double x, long double y)
{
    return fmin(x, y);
}

float frexpf(float x, int *e)
{
    return (float)frexp(x, e);
}

long double frexpl(long double x, int *e)
{
    return frexp(x, e);
}

float ldexpf(float x, int n)
{
    return fl(ldexp(x, n));
}

long double ldexpl(long double x, int n)
{
    return ldexp(x, n);
}

float scalbnf(float x, int n)
{
    return fl(ldexp(x, n));
}

long double scalbnl(long double x, int n)
{
    return scalbn(x, n);
}

float scalblnf(float x, long n)
{
    return fl(scalbln(x, n));
}

long double scalblnl(long double x, long n)
{
    return scalbln(x, n);
}

float modff(float x, float *ip)
{
    double i;
    double f;

    f = modf(x, &i);
    *ip = (float)i;
    return (float)f;
}

long double modfl(long double x, long double *ip)
{
    double i;
    double f;

    f = modf(x, &i);
    *ip = i;
    return f;
}

int ilogbf(float x)
{
    return ilogb(x);
}

int ilogbl(long double x)
{
    return ilogb(x);
}

long lrintf(float x)
{
    return lrint(x);
}

long lrintl(long double x)
{
    return lrint(x);
}

long long llrintf(float x)
{
    return llrint(x);
}

long long llrintl(long double x)
{
    return llrint(x);
}

long lroundf(float x)
{
    return lround(x);
}

long lroundl(long double x)
{
    return lround(x);
}

long long llroundf(float x)
{
    return llround(x);
}

long long llroundl(long double x)
{
    return llround(x);
}

float remquof(float x, float y, int *quo)
{
    return (float)remquo(x, y, quo);
}

long double remquol(long double x, long double y, int *quo)
{
    return remquo(x, y, quo);
}

float nanf(const char *tag)
{
    return (float)nan(tag);
}

long double nanl(const char *tag)
{
    return nan(tag);
}

float fmaf(float x, float y, float z)
{
    return fl(fma(x, y, z));
}

long double fmal(long double x, long double y, long double z)
{
    return fma(x, y, z);
}

/* The next float after x towards y: its bits one up or down, as in
 * nextafter; from zero, the smallest subnormal float. */
static float next_float(float x, double y)
{
    union fw u;
    u32 a;

    if (x != x || y != y)
        return (float)(x + y);
    if (x == y)
        return (float)y;
    u.f = x;
    a = u.w & 0x7FFFFFFFUL;
    if (a == 0) {
        u.w = y < 0 ? 0x80000001UL : 1;
    } else if ((x < y) == ((u.w >> 31) == 0)) {
        u.w++;
    } else {
        u.w--;
    }
    a = u.w & 0x7FFFFFFFUL;
    if (a >= 0x7F800000UL || a < 0x00800000UL)
        errno = ERANGE;
    return u.f;
}

float nextafterf(float x, float y)
{
    return next_float(x, y);
}

long double nextafterl(long double x, long double y)
{
    return nextafter(x, y);
}

float nexttowardf(float x, long double y)
{
    return next_float(x, (double)y);
}

long double nexttowardl(long double x, long double y)
{
    return nextafter(x, y);
}
