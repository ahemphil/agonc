/* math_names.h - lib/libc/math.c's functions under other names, so that
 * the PC's build of it links beside the PC's own libm (test_math.py
 * passes -include math_names.h). Its sqrt is softfp.c's sf64_sqrt. */

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
#define __sf64_sqrt sf64_sqrt
