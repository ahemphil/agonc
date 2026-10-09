/* stdlib.h. exit and abort are in exit.c, the allocator in malloc.c, the
 * long long functions (C99's, not in strict mode) in ll.c, the rest in
 * stdlib.c (strtod's conversion in fp.c).
 *
 * Notes for programs: the heap grows up from the program's end towards
 * the stack and never within 256 bytes of it; malloc(0) returns a unique
 * pointer. atexit holds 32 functions. rand is C89's example generator.
 * qsort is a heapsort (not stable). strtod and C99's strtof are correctly
 * rounded, and outside strict mode read C99's hexadecimal, inf and nan
 * forms too. getenv
 * reads MOS 3's system variables (always NULL on MOS 2). system runs a
 * MOS command or moslet, not a program in /bin. */

#ifndef _STDLIB_H
#define _STDLIB_H

#define NULL ((void *)0)

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#ifndef _WCHAR_T
#define _WCHAR_T
typedef int wchar_t;
#endif

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

#if !defined(__STRICT_ANSI__)
typedef struct {
    long long quot;
    long long rem;
} lldiv_t;
#endif

#define RAND_MAX 32767
#define MB_CUR_MAX 1

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 200        /* MOS has messages for 1-25, not 200 */

void *malloc(unsigned int size);
void *calloc(unsigned int nmemb, unsigned int size);
void *realloc(void *ptr, unsigned int newsize);
void free(void *ptr);
void exit(int status);
int atexit(void (*func)(void));
void abort(void);
int abs(int n);
long labs(long n);
int atoi(const char *s);
long atol(const char *s);
double atof(const char *s);
double strtod(const char *s, char **endptr);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
int rand(void);
void srand(unsigned int seed);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
char *getenv(const char *name);
int system(const char *string);
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wchar);
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);

#if !defined(__STRICT_ANSI__)
/* C99's strtod and atof, which read hexadecimal, inf and nan as well:
 * the plain names are C89's, which strict mode keeps */
double __strtod99(const char *s, char **endptr);
double __atof99(const char *s);
#define strtod __strtod99
#define atof __atof99
void _Exit(int status);
float strtof(const char *s, char **endptr);
long double strtold(const char *s, char **endptr);
long long llabs(long long n);
long long atoll(const char *s);
long long strtoll(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);
lldiv_t lldiv(long long numer, long long denom);
#endif

#endif
