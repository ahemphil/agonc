/* math_names.h - lib/libc/math.c's functions under other names, so that
 * the PC's build of it links beside the PC's own libm (test_math.py
 * passes -include math_names.h), and math99.c's and mathf.c's beside them.
 * Its sqrt is softfp.c's sf64_sqrt, its fma sf64_fma, both reached
 * through math_shim.c (a struct sf64 is larger than a double where long
 * is 64 bits).
 *
 * The PC's <math.h> is read first, before any name is changed: glibc's
 * builds its declarations from the names (and warns about a macro
 * called log), so renaming macros ahead of it break it. The functions
 * are then declared here under their new names, as lib/libc/math.h
 * declares them (the helpers __sf64_sqrt, __trunc, __expm1, __sf64_fma are
 * declared by the sources that use them). The sources' own
 * #include <math.h> then adds nothing, so they see the PC's macros and
 * constants, as before. */

#include <math.h>

#define acos am_acos
#define asin am_asin
#define atan am_atan
#define atan2 am_atan2
#define cos am_cos
#define sin am_sin
#define tan am_tan
#define cosh am_cosh
#define sinh am_sinh
#define tanh am_tanh
#define exp am_exp
#define frexp am_frexp
#define ldexp am_ldexp
#define log am_log
#define log10 am_log10
#define modf am_modf
#define pow am_pow
#define sqrt am_sqrt
#define ceil am_ceil
#define fabs am_fabs
#define floor am_floor
#define fmod am_fmod
#define __sf64_sqrt am_sf64_sqrt
/* C99's (math99.c, mathf.c), and the helpers they share with math.c */
#define acosh am_acosh
#define asinh am_asinh
#define atanh am_atanh
#define exp2 am_exp2
#define expm1 am_expm1
#define log1p am_log1p
#define log2 am_log2
#define logb am_logb
#define cbrt am_cbrt
#define erf am_erf
#define erfc am_erfc
#define lgamma am_lgamma
#define tgamma am_tgamma
#define nearbyint am_nearbyint
#define rint am_rint
#define round am_round
#define trunc am_trunc
#define hypot am_hypot
#define remainder am_remainder
#define copysign am_copysign
#define fdim am_fdim
#define fmax am_fmax
#define fmin am_fmin
#define nextafter am_nextafter
#define nexttoward am_nexttoward
#define scalbn am_scalbn
#define scalbln am_scalbln
#define ilogb am_ilogb
#define lrint am_lrint
#define llrint am_llrint
#define lround am_lround
#define llround am_llround
#define remquo am_remquo
#define nan am_nan
#define fma am_fma
#define acosf am_acosf
#define acosl am_acosl
#define asinf am_asinf
#define asinl am_asinl
#define atanf am_atanf
#define atanl am_atanl
#define cosf am_cosf
#define cosl am_cosl
#define sinf am_sinf
#define sinl am_sinl
#define tanf am_tanf
#define tanl am_tanl
#define acoshf am_acoshf
#define acoshl am_acoshl
#define asinhf am_asinhf
#define asinhl am_asinhl
#define atanhf am_atanhf
#define atanhl am_atanhl
#define coshf am_coshf
#define coshl am_coshl
#define sinhf am_sinhf
#define sinhl am_sinhl
#define tanhf am_tanhf
#define tanhl am_tanhl
#define expf am_expf
#define expl am_expl
#define exp2f am_exp2f
#define exp2l am_exp2l
#define expm1f am_expm1f
#define expm1l am_expm1l
#define logf am_logf
#define logl am_logl
#define log10f am_log10f
#define log10l am_log10l
#define log1pf am_log1pf
#define log1pl am_log1pl
#define log2f am_log2f
#define log2l am_log2l
#define logbf am_logbf
#define logbl am_logbl
#define cbrtf am_cbrtf
#define cbrtl am_cbrtl
#define fabsf am_fabsf
#define fabsl am_fabsl
#define sqrtf am_sqrtf
#define sqrtl am_sqrtl
#define erff am_erff
#define erfl am_erfl
#define erfcf am_erfcf
#define erfcl am_erfcl
#define lgammaf am_lgammaf
#define lgammal am_lgammal
#define tgammaf am_tgammaf
#define tgammal am_tgammal
#define ceilf am_ceilf
#define ceill am_ceill
#define floorf am_floorf
#define floorl am_floorl
#define nearbyintf am_nearbyintf
#define nearbyintl am_nearbyintl
#define rintf am_rintf
#define rintl am_rintl
#define roundf am_roundf
#define roundl am_roundl
#define truncf am_truncf
#define truncl am_truncl
#define atan2f am_atan2f
#define atan2l am_atan2l
#define hypotf am_hypotf
#define hypotl am_hypotl
#define powf am_powf
#define powl am_powl
#define fmodf am_fmodf
#define fmodl am_fmodl
#define remainderf am_remainderf
#define remainderl am_remainderl
#define copysignf am_copysignf
#define copysignl am_copysignl
#define fdimf am_fdimf
#define fdiml am_fdiml
#define fmaxf am_fmaxf
#define fmaxl am_fmaxl
#define fminf am_fminf
#define fminl am_fminl
#define nextafterf am_nextafterf
#define nextafterl am_nextafterl
#define nexttowardf am_nexttowardf
#define nexttowardl am_nexttowardl
#define frexpf am_frexpf
#define frexpl am_frexpl
#define ldexpf am_ldexpf
#define ldexpl am_ldexpl
#define modff am_modff
#define modfl am_modfl
#define scalbnf am_scalbnf
#define scalbnl am_scalbnl
#define scalblnf am_scalblnf
#define scalblnl am_scalblnl
#define ilogbf am_ilogbf
#define ilogbl am_ilogbl
#define lrintf am_lrintf
#define lrintl am_lrintl
#define llrintf am_llrintf
#define llrintl am_llrintl
#define lroundf am_lroundf
#define lroundl am_lroundl
#define llroundf am_llroundf
#define llroundl am_llroundl
#define remquof am_remquof
#define remquol am_remquol
#define nanf am_nanf
#define nanl am_nanl
#define fmaf am_fmaf
#define fmal am_fmal
#define __fpclassify am__fpclassify
#define __fpclassifyf am__fpclassifyf
#define __signbit am__signbit
#define __signbitf am__signbitf
#define __isgreater am__isgreater
#define __isgreaterequal am__isgreaterequal
#define __isless am__isless
#define __islessequal am__islessequal
#define __islessgreater am__islessgreater
#define __isunordered am__isunordered
#define __trunc am__trunc
#define __expm1 am__expm1
#define __sf64_fma am_sf64_fma

/* lib/libc/math.h's declarations, under the names above */
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
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
int __fpclassify(double x);
int __fpclassifyf(float x);
int __signbit(double x);
int __signbitf(float x);
int __isgreater(double x, double y);
int __isgreaterequal(double x, double y);
int __isless(double x, double y);
int __islessequal(double x, double y);
int __islessgreater(double x, double y);
int __isunordered(double x, double y);
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
