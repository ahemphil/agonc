/* stdlib.c - the <stdlib.h> functions other than the allocator (malloc.c)
 * and exit and abort (exit.c); and errno.
 *
 * The sections: number conversion (atoi to strtoul, strtod handing off to
 * fp.c), atexit's table, arithmetic and random numbers, sorting and
 * searching, the environment (getenv and system, on MOS), and the
 * multibyte functions of the "C" locale. The long long forms (strtoll and
 * the rest) are in ll.c, beside the long long arithmetic helpers. */

#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <agon/mos.h>

int errno;

/* ---- number conversion ---------------------------------------------------- */

int abs(int n)
{
    return n < 0 ? -n : n;
}

/* Optional leading white space, an optional sign, then decimal digits.
 * Overflow wraps (24-bit), as C89 leaves it undefined. Each digit is
 * added to ten times the value so far (Horner's rule): "472" is
 * ((4 * 10) + 7) * 10 + 2. */
int atoi(const char *s)
{
    int n;
    int neg;

    while (*s == ' ' || (*s >= 9 && *s <= 13))
        s++;
    neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

long labs(long n)
{
    return n < 0 ? -n : n;
}

/* As atoi, in long. */
long atol(const char *s)
{
    long n;
    int neg;

    while (*s == ' ' || (*s >= 9 && *s <= 13))
        s++;
    neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

/* The value of digit or letter c (either case), or 99 if it is neither. */
static int digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

/* The common part of strtol and strtoul (C89 4.10.1.5, 4.10.1.6): white
 * space, a sign, a 0x/0X prefix (base 16, or base 0 which also picks octal
 * for a leading 0), then digits below base. The magnitude saturates at
 * ULONG_MAX, setting *overflow. *endptr (if not NULL) is the first
 * unconverted character, or s itself when there are no digits. A base
 * outside 2-36 converts nothing. */
static unsigned long scan(const char *s, char **endptr, int base, int *neg, int *overflow)
{
    const char *p;
    unsigned long v;
    unsigned long limit;
    int d;
    int any;

    p = s;
    while (*p == ' ' || (*p >= 9 && *p <= 13))
        p++;
    *neg = 0;
    if (*p == '-') {
        *neg = 1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    /* the prefix counts only with a hex digit after it: "0xg" is the
     * number 0 followed by "xg" */
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_value(p[2]) < 16) {
        p = p + 2;
        base = 16;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    v = 0;
    any = 0;
    *overflow = 0;
    if (base >= 2 && base <= 36) {
        limit = ULONG_MAX / base;
        for (;;) {
            d = digit_value(*p);
            if (d >= base)
                break;
            /* v * base + d would pass ULONG_MAX: tested before it is
             * computed, since the unsigned arithmetic would wrap silently.
             * The remaining digits are still consumed. */
            if (v > limit || (v == limit && (unsigned long)d > ULONG_MAX % base))
                *overflow = 1;
            else
                v = v * base + d;
            any = 1;
            p++;
        }
    }
    if (endptr != NULL)
        *endptr = (char *)(any ? p : s);
    return any ? v : 0;
}

/* Out of range: LONG_MAX or LONG_MIN, with errno ERANGE. */
long strtol(const char *s, char **endptr, int base)
{
    unsigned long v;
    int neg;
    int overflow;

    /* a negative value may reach 2^31, one more than LONG_MAX: LONG_MIN.
     * It is negated in unsigned long (0UL - v), where 2^31 cannot
     * overflow, and the bits are then read as a long. */
    v = scan(s, endptr, base, &neg, &overflow);
    if (!overflow && (neg ? v <= 0x80000000UL : v <= (unsigned long)LONG_MAX))
        return neg ? (long)(0UL - v) : (long)v;
    errno = ERANGE;
    return neg ? LONG_MIN : LONG_MAX;
}

struct sf64;
void __fp_strtod(const char *nptr, char **endptr, struct sf64 *r);

/* C89 4.10.1.4, correctly rounded; out of range, HUGE_VAL or 0 with errno
 * ERANGE (fp.c).
 *
 * Most decimal fractions (0.1, say) have no exact binary value, so a
 * conversion must pick a double; "correctly rounded" means it picks the
 * double nearest the exact decimal value, and on an exact tie the one
 * whose last bit is 0 (round to nearest even). Converting digit by digit
 * in double arithmetic would round at every step and could land one unit
 * off. fp.c instead reads the text as a string of decimal digits and a
 * power of ten, and softfp turns that into the double's bits with big
 * integers: the value made an exact fraction, divided, and rounded once.
 * Nothing is approximated; a "sticky" digit stands for any nonzero digits
 * past the many it keeps, since they can matter only by being nonzero.
 *
 * The Agon has no floating-point hardware: a double is 8 bytes that
 * fp.c's integer code builds, and it is written here as a struct sf64,
 * the two 32-bit halves softfp works on. */
double strtod(const char *s, char **endptr)
{
    double d;

    __fp_strtod(s, endptr, (struct sf64 *)&d);
    return d;
}

double atof(const char *s)
{
    return strtod(s, NULL);
}

/* Out of range: ULONG_MAX, with errno ERANGE. A '-' negates the value in
 * unsigned long, as C89 says. */
unsigned long strtoul(const char *s, char **endptr, int base)
{
    unsigned long v;
    int neg;
    int overflow;

    v = scan(s, endptr, base, &neg, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    return neg ? 0UL - v : v;
}

/* ---- atexit ---------------------------------------------------------------- */

/* atexit's functions, which exit (exit.c) runs in reverse order. C89
 * requires room for 32. The table's first member points at __atexit_run,
 * so a program that calls atexit links it, and exit reaches it through a
 * weak reference. */
void __atexit_run(void);

static struct {
    void (*run)(void);
    int n;
    void (*fn[32])(void);
} at = { __atexit_run };

/* Each function is taken off the table before it is called, so one that
 * calls exit does not run itself again. */
void __atexit_run(void)
{
    while (at.n > 0) {
        at.n--;
        at.fn[at.n]();
    }
}

int atexit(void (*func)(void))
{
    if (at.n >= 32)
        return -1;
    at.fn[at.n] = func;
    at.n++;
    return 0;
}

/* ---- arithmetic, random numbers ----------------------------------------------- */

/* The quotient truncated toward zero, and the remainder with the
 * numerator's sign (C89 4.10.6.2), as the eZ80 helpers divide. */
div_t div(int numer, int denom)
{
    div_t r;

    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

ldiv_t ldiv(long numer, long denom)
{
    ldiv_t r;

    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

static unsigned long rand_next = 1;

/* C89 4.10.2.2's example generator, a linear congruential one: each state
 * is a * state + c modulo 2^32 (the wrap of unsigned long does the
 * modulo). The low bits of such a state repeat with short periods, so the
 * result is 15 bits from the middle, bits 16-30. */
int rand(void)
{
    rand_next = rand_next * 1103515245UL + 12345;
    return (int)((rand_next >> 16) & RAND_MAX);
}

void srand(unsigned int seed)
{
    rand_next = seed;
}

/* ---- sorting and searching ------------------------------------------------------ */

static void swap(char *a, char *b, size_t size)
{
    char t;

    while (size > 0) {
        t = *a;
        *a = *b;
        *b = t;
        a++;
        b++;
        size--;
    }
}

/* Moves base[root] down the heap of n until neither child is larger. The
 * heap is the array itself: element i's children are 2i+1 and 2i+2, and
 * no element is smaller than its children (a "max-heap"). */
static void sift(char *base, size_t root, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    size_t child;

    for (;;) {
        child = 2 * root + 1;
        if (child >= n)
            return;
        if (child + 1 < n && cmp(base + child * size, base + (child + 1) * size) < 0)
            child++;
        if (cmp(base + root * size, base + child * size) >= 0)
            return;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}

/* Heapsort: C89 names no method, and this one takes O(n log n) comparisons
 * whatever the input, no memory and no recursion. */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    char *b;
    size_t i;

    b = base;
    if (nmemb < 2 || size == 0)
        return;
    /* build the heap bottom up: sift each element that has children,
     * the last first, so each sift starts above two finished heaps */
    for (i = nmemb / 2; i > 0; i--)
        sift(b, i - 1, nmemb, size, compar);
    /* the largest is at the root: swap it to the end, shrink the heap by
     * one, and sift the new root down; the sorted tail grows leftwards */
    for (i = nmemb - 1; i > 0; i--) {
        swap(b, b + i * size, size);
        sift(b, 0, i, size, compar);
    }
}

/* Binary search over [lo, hi), halving it at each comparison. The middle
 * is lo + (hi - lo) / 2, not (lo + hi) / 2, whose sum could overflow. */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    const char *b;
    size_t lo;
    size_t hi;
    size_t mid;
    int c;

    b = base;
    lo = 0;
    hi = nmemb;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        c = compar(key, b + mid * size);
        if (c == 0)
            return (void *)(b + mid * size);
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

/* ---- the environment ------------------------------------------------------------ */

/* Whether MOS's name for a variable is name: letters in either case (MOS
 * compares them so), up to a space or the end. */
static int same_name(const char *actual, const char *name)
{
    int a;
    int n;

    for (;;) {
        a = *actual;
        n = *name;
        if (a >= 'a' && a <= 'z')
            a = a - 32;
        if (n >= 'a' && n <= 'z')
            n = n - 32;
        if (a == ' ')
            a = 0;
        if (a != n)
            return 0;
        if (a == 0)
            return 1;
        actual++;
        name++;
    }
}

/* MOS 3's system variable of that name, as a string (readvarval, 0x31, with
 * C = 3 to expand it), or NULL: MOS 2 has no variables, and answers 0x31
 * with status 23, "not implemented". MOS 3.0.2 answers a name it does not
 * have with a neighbouring variable, so the name it returns (in IY) is
 * checked. The value is overwritten by the next call. */
char *getenv(const char *name)
{
    static char value[256];
    int r;
    char *v;
    int len;
    char *actual;

    /* The asm reaches the locals by the frame layout (abi.md 5): r at
     * ix-3, v at ix-6, len at ix-9, actual at ix-12, and name at ix+6. v
     * carries value's address in, since a block-scope static's assembly
     * name is not promised (abi.md 11). The call wants the buffer in IX,
     * so the frame pointer is saved on the stack around it, with IY, and
     * the results are kept in registers until IX is back: the variable's
     * own name (IY, via BC) to actual, its length (DE) to len, the status
     * (A) to r. DE = 255 going in is the buffer's room, one byte short of
     * value's 256 for the NUL added after. */
    v = value;
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix-6)\n"
        "push ix\n"
        "push iy\n"
        "push bc\n"
        "pop ix\n"                     /* IX: the buffer */
        "ld iy,0\n"                    /* the first match */
        "ld de,255\n"
        "ld c,3\n"
        "ld a,0x31\n"
        "rst.lis 08h\n"
        "push iy\n"                    /* the variable's own name */
        "pop bc\n"
        "pop iy\n"
        "pop ix\n"
        "ld (ix-12),bc\n"
        "ld (ix-9),de\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    if (r != 0 || len < 0 || len > 255 || actual == NULL || !same_name(actual, name))
        return NULL;
    value[len] = 0;
    return value;
}

/* The command run by MOS as if typed at its prompt (c89_spec.md 15): its
 * built-in commands and moslets, not a program in /bin, which would load
 * over this one (MOS looks for one only when the command comes from its own
 * prompt). Non-zero for a null command: there is a command processor. */
int system(const char *string)
{
    if (string == NULL)
        return 1;
    return mos_oscli(string);
}

/* ---- multibyte characters: in the "C" locale, one byte each ------------------- */

int mblen(const char *s, size_t n)
{
    if (s == NULL)
        return 0;                       /* no shift states */
    if (n == 0)
        return -1;
    return *s != 0;
}

int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
    if (s == NULL)
        return 0;
    if (n == 0)
        return -1;
    if (pwc != NULL)
        *pwc = *s & 255;
    return *s != 0;
}

int wctomb(char *s, wchar_t wchar)
{
    if (s == NULL)
        return 0;
    if (wchar < 0 || wchar > 255)
        return -1;
    *s = wchar;
    return 1;
}

size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        pwcs[i] = s[i] & 255;
        if (s[i] == 0)
            return i;
    }
    return n;
}

/* (size_t)-1 if a wide character is not a byte. */
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (pwcs[i] < 0 || pwcs[i] > 255)
            return (size_t)-1;
        s[i] = pwcs[i];
        if (pwcs[i] == 0)
            return i;
    }
    return n;
}
