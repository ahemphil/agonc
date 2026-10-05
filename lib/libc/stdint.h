/* stdint.h - C99's integer types for the widths this target has (8, 16,
 * 24, 32 and, but in strict mode, which has no long long, 64 bits), with
 * their limits. An extension to C89: agonc ships it in /lib. The
 * greatest-width types are long long's, or strict mode's long. The fast
 * types of 8 to 24 bits are int, the eZ80's natural width. */

#ifndef _STDINT_H
#define _STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int24_t;
typedef unsigned int uint24_t;
typedef long int32_t;
typedef unsigned long uint32_t;
#if !defined(__STRICT_ANSI__)
typedef long long int64_t;
typedef unsigned long long uint64_t;
#endif

typedef signed char int_least8_t;
typedef unsigned char uint_least8_t;
typedef short int_least16_t;
typedef unsigned short uint_least16_t;
typedef int int_least24_t;
typedef unsigned int uint_least24_t;
typedef long int_least32_t;
typedef unsigned long uint_least32_t;
#if !defined(__STRICT_ANSI__)
typedef long long int_least64_t;
typedef unsigned long long uint_least64_t;
#endif

/* the eZ80 is fastest at 24 bits */
typedef int int_fast8_t;
typedef unsigned int uint_fast8_t;
typedef int int_fast16_t;
typedef unsigned int uint_fast16_t;
typedef int int_fast24_t;
typedef unsigned int uint_fast24_t;
typedef long int_fast32_t;
typedef unsigned long uint_fast32_t;
#if !defined(__STRICT_ANSI__)
typedef long long int_fast64_t;
typedef unsigned long long uint_fast64_t;
#endif

typedef int intptr_t;
typedef unsigned int uintptr_t;
#if !defined(__STRICT_ANSI__)
typedef long long intmax_t;
typedef unsigned long long uintmax_t;
#else
typedef long intmax_t;
typedef unsigned long uintmax_t;
#endif

#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32767 - 1)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT24_MIN (-8388607 - 1)
#define INT24_MAX 8388607
#define UINT24_MAX 16777215U
#define INT32_MIN (-2147483647L - 1)
#define INT32_MAX 2147483647L
#define UINT32_MAX 4294967295UL
#if !defined(__STRICT_ANSI__)
#define INT64_MIN (-9223372036854775807LL - 1)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#endif

#define INT_LEAST8_MIN INT8_MIN
#define INT_LEAST8_MAX INT8_MAX
#define UINT_LEAST8_MAX UINT8_MAX
#define INT_LEAST16_MIN INT16_MIN
#define INT_LEAST16_MAX INT16_MAX
#define UINT_LEAST16_MAX UINT16_MAX
#define INT_LEAST24_MIN INT24_MIN
#define INT_LEAST24_MAX INT24_MAX
#define UINT_LEAST24_MAX UINT24_MAX
#define INT_LEAST32_MIN INT32_MIN
#define INT_LEAST32_MAX INT32_MAX
#define UINT_LEAST32_MAX UINT32_MAX
#if !defined(__STRICT_ANSI__)
#define INT_LEAST64_MIN INT64_MIN
#define INT_LEAST64_MAX INT64_MAX
#define UINT_LEAST64_MAX UINT64_MAX
#endif

#define INT_FAST8_MIN INT24_MIN
#define INT_FAST8_MAX INT24_MAX
#define UINT_FAST8_MAX UINT24_MAX
#define INT_FAST16_MIN INT24_MIN
#define INT_FAST16_MAX INT24_MAX
#define UINT_FAST16_MAX UINT24_MAX
#define INT_FAST24_MIN INT24_MIN
#define INT_FAST24_MAX INT24_MAX
#define UINT_FAST24_MAX UINT24_MAX
#define INT_FAST32_MIN INT32_MIN
#define INT_FAST32_MAX INT32_MAX
#define UINT_FAST32_MAX UINT32_MAX
#if !defined(__STRICT_ANSI__)
#define INT_FAST64_MIN INT64_MIN
#define INT_FAST64_MAX INT64_MAX
#define UINT_FAST64_MAX UINT64_MAX
#endif

#define INTPTR_MIN INT24_MIN
#define INTPTR_MAX INT24_MAX
#define UINTPTR_MAX UINT24_MAX
#if !defined(__STRICT_ANSI__)
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#else
#define INTMAX_MIN INT32_MIN
#define INTMAX_MAX INT32_MAX
#define UINTMAX_MAX UINT32_MAX
#endif

#define PTRDIFF_MIN INT24_MIN
#define PTRDIFF_MAX INT24_MAX
#define SIZE_MAX UINT24_MAX
#define WCHAR_MIN INT24_MIN
#define WCHAR_MAX INT24_MAX

#define INT8_C(c) c
#define UINT8_C(c) c
#define INT16_C(c) c
#define UINT16_C(c) c
#define INT24_C(c) c
#define UINT24_C(c) c ## U
#define INT32_C(c) c ## L
#define UINT32_C(c) c ## UL
#if !defined(__STRICT_ANSI__)
#define INT64_C(c) c ## LL
#define UINT64_C(c) c ## ULL
#define INTMAX_C(c) c ## LL
#define UINTMAX_C(c) c ## ULL
#else
#define INTMAX_C(c) c ## L
#define UINTMAX_C(c) c ## UL
#endif

#endif
