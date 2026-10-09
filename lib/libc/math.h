/* math.h - C89 4.5 (math.c), and outside strict mode C99's 7.12
 * (math99.c, mathf.c). HUGE_VAL is an infinity.
 * A domain error returns a NaN with errno EDOM; a result too large returns
 * plus or minus HUGE_VAL, and one too small for anything but zero returns
 * 0, both with errno ERANGE (c89_spec.md 15). */

#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL (1e300 * 1e300)

double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double cos(double x);
double sin(double x);
double tan(double x);
double cosh(double x);
double sinh(double x);
double tanh(double x);
double exp(double x);
double frexp(double value, int *exp);
double ldexp(double x, int exp);
double log(double x);
double log10(double x);
double modf(double value, double *iptr);
double pow(double x, double y);
double sqrt(double x);
double ceil(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);

#if !defined(__STRICT_ANSI__)
/* C99's (7.12), in math99.c (double) and mathf.c (float, and long double,
 * which is double). There is no floating-point environment: rounding is
 * to nearest, and errors set errno only. */

typedef float float_t;
typedef double double_t;

#define HUGE_VALF ((float)HUGE_VAL)
#define HUGE_VALL ((long double)HUGE_VAL)
#define INFINITY HUGE_VALF
#define NAN ((float)(HUGE_VAL - HUGE_VAL))     /* infinity less infinity: a quiet NaN */

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGB0 (-8388607 - 1)
#define FP_ILOGBNAN 8388607
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling MATH_ERRNO

/* The type-generic macros pick by the argument's size: a float is 4
 * bytes, a double or long double 8 (an integer, 3 or 4, goes as a
 * double). */
int __fpclassify(double x);
int __fpclassifyf(float x);
int __signbit(double x);
int __signbitf(float x);
#define fpclassify(x) (sizeof(x) == sizeof(float) ? __fpclassifyf(x) : __fpclassify(x))
#define isfinite(x) (fpclassify(x) >= FP_ZERO)
#define isinf(x) (fpclassify(x) == FP_INFINITE)
#define isnan(x) (fpclassify(x) == FP_NAN)
#define isnormal(x) (fpclassify(x) == FP_NORMAL)
#define signbit(x) (sizeof(x) == sizeof(float) ? __signbitf(x) : __signbit(x))

int __isgreater(double x, double y);
int __isgreaterequal(double x, double y);
int __isless(double x, double y);
int __islessequal(double x, double y);
int __islessgreater(double x, double y);
int __isunordered(double x, double y);
#define isgreater(x, y) __isgreater(x, y)
#define isgreaterequal(x, y) __isgreaterequal(x, y)
#define isless(x, y) __isless(x, y)
#define islessequal(x, y) __islessequal(x, y)
#define islessgreater(x, y) __islessgreater(x, y)
#define isunordered(x, y) __isunordered(x, y)

/* double: what C89 lacks */
double acosh(double x);
double asinh(double x);
double atanh(double x);
double exp2(double x);
double expm1(double x);
double log1p(double x);
double log2(double x);
double logb(double x);
double cbrt(double x);
double erf(double x);
double erfc(double x);
double lgamma(double x);
double tgamma(double x);
double nearbyint(double x);
double rint(double x);
double round(double x);
double trunc(double x);
double hypot(double x, double y);
double remainder(double x, double y);
double copysign(double x, double y);
double fdim(double x, double y);
double fmax(double x, double y);
double fmin(double x, double y);
double nextafter(double x, double y);
double nexttoward(double x, long double y);
double nan(const char *tag);
double fma(double x, double y, double z);
double remquo(double x, double y, int *quo);
double scalbn(double x, int n);
double scalbln(double x, long n);
int ilogb(double x);
long lrint(double x);
long long llrint(double x);
long lround(double x);
long long llround(double x);

/* float and long double: every function */
float acosf(float x);
long double acosl(long double x);
float asinf(float x);
long double asinl(long double x);
float atanf(float x);
long double atanl(long double x);
float cosf(float x);
long double cosl(long double x);
float sinf(float x);
long double sinl(long double x);
float tanf(float x);
long double tanl(long double x);
float acoshf(float x);
long double acoshl(long double x);
float asinhf(float x);
long double asinhl(long double x);
float atanhf(float x);
long double atanhl(long double x);
float coshf(float x);
long double coshl(long double x);
float sinhf(float x);
long double sinhl(long double x);
float tanhf(float x);
long double tanhl(long double x);
float expf(float x);
long double expl(long double x);
float exp2f(float x);
long double exp2l(long double x);
float expm1f(float x);
long double expm1l(long double x);
float logf(float x);
long double logl(long double x);
float log10f(float x);
long double log10l(long double x);
float log1pf(float x);
long double log1pl(long double x);
float log2f(float x);
long double log2l(long double x);
float logbf(float x);
long double logbl(long double x);
float cbrtf(float x);
long double cbrtl(long double x);
float fabsf(float x);
long double fabsl(long double x);
float sqrtf(float x);
long double sqrtl(long double x);
float erff(float x);
long double erfl(long double x);
float erfcf(float x);
long double erfcl(long double x);
float lgammaf(float x);
long double lgammal(long double x);
float tgammaf(float x);
long double tgammal(long double x);
float ceilf(float x);
long double ceill(long double x);
float floorf(float x);
long double floorl(long double x);
float nearbyintf(float x);
long double nearbyintl(long double x);
float rintf(float x);
long double rintl(long double x);
float roundf(float x);
long double roundl(long double x);
float truncf(float x);
long double truncl(long double x);
float atan2f(float x, float y);
long double atan2l(long double x, long double y);
float hypotf(float x, float y);
long double hypotl(long double x, long double y);
float powf(float x, float y);
long double powl(long double x, long double y);
float fmodf(float x, float y);
long double fmodl(long double x, long double y);
float remainderf(float x, float y);
long double remainderl(long double x, long double y);
float copysignf(float x, float y);
long double copysignl(long double x, long double y);
float fdimf(float x, float y);
long double fdiml(long double x, long double y);
float fmaxf(float x, float y);
long double fmaxl(long double x, long double y);
float fminf(float x, float y);
long double fminl(long double x, long double y);
float nextafterf(float x, float y);
long double nextafterl(long double x, long double y);
float nexttowardf(float x, long double y);
long double nexttowardl(long double x, long double y);
float frexpf(float x, int *e);
long double frexpl(long double x, int *e);
float ldexpf(float x, int n);
long double ldexpl(long double x, int n);
float modff(float x, float *ip);
long double modfl(long double x, long double *ip);
float scalbnf(float x, int n);
long double scalbnl(long double x, int n);
float scalblnf(float x, long n);
long double scalblnl(long double x, long n);
int ilogbf(float x);
int ilogbl(long double x);
long lrintf(float x);
long lrintl(long double x);
long long llrintf(float x);
long long llrintl(long double x);
long lroundf(float x);
long lroundl(long double x);
long long llroundf(float x);
long long llroundl(long double x);
float remquof(float x, float y, int *quo);
long double remquol(long double x, long double y, int *quo);
float nanf(const char *tag);
long double nanl(const char *tag);
float fmaf(float x, float y, float z);
long double fmal(long double x, long double y, long double z);
#endif

#endif
