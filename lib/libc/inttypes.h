/* inttypes.h - C99's printf and scanf length macros for <stdint.h>'s
 * types, and the intmax_t functions (in ll.c; not in strict mode, whose
 * intmax_t is long). An extension to C89, as stdint.h is: the macros
 * work in strict mode too, but for the 64-bit types, which it lacks.
 *
 * printf("%" PRId32 "\n", x) prints an int32_t, which is a long here.
 * The 8- and 16-bit types print as their promoted int ("d"), but scan
 * with "hh" and "h", which store a char and a short. */

#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

#define PRId8 "d"
#define PRIi8 "i"
#define PRIo8 "o"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRIX8 "X"
#define PRId16 "d"
#define PRIi16 "i"
#define PRIo16 "o"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRIX16 "X"
#define PRId24 "d"
#define PRIi24 "i"
#define PRIo24 "o"
#define PRIu24 "u"
#define PRIx24 "x"
#define PRIX24 "X"
#define PRId32 "ld"
#define PRIi32 "li"
#define PRIo32 "lo"
#define PRIu32 "lu"
#define PRIx32 "lx"
#define PRIX32 "lX"

#define PRIdLEAST8 PRId8
#define PRIiLEAST8 PRIi8
#define PRIoLEAST8 PRIo8
#define PRIuLEAST8 PRIu8
#define PRIxLEAST8 PRIx8
#define PRIXLEAST8 PRIX8
#define PRIdLEAST16 PRId16
#define PRIiLEAST16 PRIi16
#define PRIoLEAST16 PRIo16
#define PRIuLEAST16 PRIu16
#define PRIxLEAST16 PRIx16
#define PRIXLEAST16 PRIX16
#define PRIdLEAST24 PRId24
#define PRIiLEAST24 PRIi24
#define PRIoLEAST24 PRIo24
#define PRIuLEAST24 PRIu24
#define PRIxLEAST24 PRIx24
#define PRIXLEAST24 PRIX24
#define PRIdLEAST32 PRId32
#define PRIiLEAST32 PRIi32
#define PRIoLEAST32 PRIo32
#define PRIuLEAST32 PRIu32
#define PRIxLEAST32 PRIx32
#define PRIXLEAST32 PRIX32

/* the fast types of 8 to 24 bits are int */
#define PRIdFAST8 "d"
#define PRIiFAST8 "i"
#define PRIoFAST8 "o"
#define PRIuFAST8 "u"
#define PRIxFAST8 "x"
#define PRIXFAST8 "X"
#define PRIdFAST16 "d"
#define PRIiFAST16 "i"
#define PRIoFAST16 "o"
#define PRIuFAST16 "u"
#define PRIxFAST16 "x"
#define PRIXFAST16 "X"
#define PRIdFAST24 "d"
#define PRIiFAST24 "i"
#define PRIoFAST24 "o"
#define PRIuFAST24 "u"
#define PRIxFAST24 "x"
#define PRIXFAST24 "X"
#define PRIdFAST32 PRId32
#define PRIiFAST32 PRIi32
#define PRIoFAST32 PRIo32
#define PRIuFAST32 PRIu32
#define PRIxFAST32 PRIx32
#define PRIXFAST32 PRIX32

#define PRIdPTR "d"
#define PRIiPTR "i"
#define PRIoPTR "o"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define PRIXPTR "X"

#define SCNd8 "hhd"
#define SCNi8 "hhi"
#define SCNo8 "hho"
#define SCNu8 "hhu"
#define SCNx8 "hhx"
#define SCNd16 "hd"
#define SCNi16 "hi"
#define SCNo16 "ho"
#define SCNu16 "hu"
#define SCNx16 "hx"
#define SCNd24 "d"
#define SCNi24 "i"
#define SCNo24 "o"
#define SCNu24 "u"
#define SCNx24 "x"
#define SCNd32 "ld"
#define SCNi32 "li"
#define SCNo32 "lo"
#define SCNu32 "lu"
#define SCNx32 "lx"

#define SCNdLEAST8 SCNd8
#define SCNiLEAST8 SCNi8
#define SCNoLEAST8 SCNo8
#define SCNuLEAST8 SCNu8
#define SCNxLEAST8 SCNx8
#define SCNdLEAST16 SCNd16
#define SCNiLEAST16 SCNi16
#define SCNoLEAST16 SCNo16
#define SCNuLEAST16 SCNu16
#define SCNxLEAST16 SCNx16
#define SCNdLEAST24 SCNd24
#define SCNiLEAST24 SCNi24
#define SCNoLEAST24 SCNo24
#define SCNuLEAST24 SCNu24
#define SCNxLEAST24 SCNx24
#define SCNdLEAST32 SCNd32
#define SCNiLEAST32 SCNi32
#define SCNoLEAST32 SCNo32
#define SCNuLEAST32 SCNu32
#define SCNxLEAST32 SCNx32

#define SCNdFAST8 "d"
#define SCNiFAST8 "i"
#define SCNoFAST8 "o"
#define SCNuFAST8 "u"
#define SCNxFAST8 "x"
#define SCNdFAST16 "d"
#define SCNiFAST16 "i"
#define SCNoFAST16 "o"
#define SCNuFAST16 "u"
#define SCNxFAST16 "x"
#define SCNdFAST24 "d"
#define SCNiFAST24 "i"
#define SCNoFAST24 "o"
#define SCNuFAST24 "u"
#define SCNxFAST24 "x"
#define SCNdFAST32 SCNd32
#define SCNiFAST32 SCNi32
#define SCNoFAST32 SCNo32
#define SCNuFAST32 SCNu32
#define SCNxFAST32 SCNx32

#define SCNdPTR "d"
#define SCNiPTR "i"
#define SCNoPTR "o"
#define SCNuPTR "u"
#define SCNxPTR "x"

#if !defined(__STRICT_ANSI__)
#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIo64 "llo"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIX64 "llX"
#define PRIdLEAST64 PRId64
#define PRIiLEAST64 PRIi64
#define PRIoLEAST64 PRIo64
#define PRIuLEAST64 PRIu64
#define PRIxLEAST64 PRIx64
#define PRIXLEAST64 PRIX64
#define PRIdFAST64 PRId64
#define PRIiFAST64 PRIi64
#define PRIoFAST64 PRIo64
#define PRIuFAST64 PRIu64
#define PRIxFAST64 PRIx64
#define PRIXFAST64 PRIX64
#define PRIdMAX PRId64
#define PRIiMAX PRIi64
#define PRIoMAX PRIo64
#define PRIuMAX PRIu64
#define PRIxMAX PRIx64
#define PRIXMAX PRIX64
#define SCNd64 "lld"
#define SCNi64 "lli"
#define SCNo64 "llo"
#define SCNu64 "llu"
#define SCNx64 "llx"
#define SCNdLEAST64 SCNd64
#define SCNiLEAST64 SCNi64
#define SCNoLEAST64 SCNo64
#define SCNuLEAST64 SCNu64
#define SCNxLEAST64 SCNx64
#define SCNdFAST64 SCNd64
#define SCNiFAST64 SCNi64
#define SCNoFAST64 SCNo64
#define SCNuFAST64 SCNu64
#define SCNxFAST64 SCNx64
#define SCNdMAX SCNd64
#define SCNiMAX SCNi64
#define SCNoMAX SCNo64
#define SCNuMAX SCNu64
#define SCNxMAX SCNx64

typedef struct {
    long long quot;
    long long rem;
} imaxdiv_t;

intmax_t imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t strtoimax(const char *s, char **endptr, int base);
uintmax_t strtoumax(const char *s, char **endptr, int base);
#else
#define PRIdMAX PRId32
#define PRIiMAX PRIi32
#define PRIoMAX PRIo32
#define PRIuMAX PRIu32
#define PRIxMAX PRIx32
#define PRIXMAX PRIX32
#define SCNdMAX SCNd32
#define SCNiMAX SCNi32
#define SCNoMAX SCNo32
#define SCNuMAX SCNu32
#define SCNxMAX SCNx32
#endif

#endif
