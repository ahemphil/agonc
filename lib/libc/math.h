/* math.h - C89 4.5; the functions are in math.c. HUGE_VAL is an infinity.
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

#endif
