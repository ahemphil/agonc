/* string.c - <string.h>. size_t is unsigned int, so the functions written
 * with unsigned are the prototypes' size_t ones.
 *
 * Every function is a loop over bytes in portable C; the busiest also
 * have eZ80 assembly beside it (STR_ASM, below). Comparisons
 * (strcmp, strncmp, memcmp) compare bytes as unsigned char, as C89 4.11.4
 * requires, so a byte above 0x7F sorts after every ASCII one even though
 * char is signed. strtok keeps its place in a static, so only one string
 * can be split at a time. */

#include <string.h>
#include <errno.h>

/* STR_ASM: eZ80 assembly beside the C for the busiest functions (1, under
 * agonc), or the C alone (0). The assembly uses the block instructions:
 * CPIR finds a byte (strlen, memchr, strcpy's length), LDIR and LDDR copy
 * (memcpy, memmove, memset, strcpy). Each block finds its parameters at
 * (ix+6), (ix+9), (ix+12) and leaves its result in the first local,
 * (ix-3), which the C returns (abi.md 10). A count of zero is tested
 * first everywhere: CPIR and LDIR with BC = 0 would run 2^24 times. */
#ifndef STR_ASM
#ifdef __AGONC__
#define STR_ASM 1
#else
#define STR_ASM 0
#endif
#endif

#if STR_ASM
/* CPIR from BC = 0 counts down past the NUL: the length is -BC - 1. */
unsigned strlen(const char *s)
{
    unsigned n;

#asm
        ld      hl, (ix+6)
        ld      bc, 0
        xor     a
        cpir
        ld      hl, 0
        scf
        sbc     hl, bc
        ld      (ix-3), hl
#endasm
    return n;
}
#else
unsigned strlen(const char *s)
{
    const char *p;

    p = s;
    while (*p)
        p++;
    return p - s;
}
#endif

/* Stops at the first difference or at a's end; b's end is then a
 * difference too, unless a ended at the same place. */
#if STR_ASM
/* DE = a, HL = b. At a difference, `sub (hl)` leaves the low byte of
 * *a - *b and the borrow, and `sbc hl,hl` / `ld l,a` widens it to the
 * int the C gives. */
int strcmp(const char *a, const char *b)
{
    int r;

#asm
        ld      de, (ix+6)
        ld      hl, (ix+9)
@loop:
        ld      a, (de)
        cp      (hl)
        jr      nz, @diff
        or      a
        jr      z, @same
        inc     de
        inc     hl
        jr      @loop
@diff:
        sub     (hl)
        sbc     hl, hl
        ld      l, a
        jr      @out
@same:
        ld      hl, 0
@out:
        ld      (ix-3), hl
#endasm
    return r;
}
#else
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#endif

int strncmp(const char *a, const char *b, unsigned n)
{
    while (n) {
        if (*a != *b || *a == 0)
            return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
        n--;
    }
    return 0;
}

#if STR_ASM
/* CPIR measures s with its NUL (-BC from BC = 0), then LDIR copies it. */
char *strcpy(char *d, const char *s)
{
    char *r;

#asm
        ld      hl, (ix+6)
        ld      (ix-3), hl
        ld      hl, (ix+9)
        ld      bc, 0
        xor     a
        cpir
        ld      hl, 0
        or      a
        sbc     hl, bc
        push    hl
        pop     bc
        ld      hl, (ix+9)
        ld      de, (ix+6)
        ldir
#endasm
    return r;
}
#else
char *strcpy(char *d, const char *s)
{
    char *r;

    r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}
#endif

/* Copies at most n characters and pads with NULs to n, as C89 requires;
 * no NUL is added if s is n characters or longer. */
char *strncpy(char *d, const char *s, unsigned n)
{
    char *r;

    r = d;
    while (n && *s) {
        *d++ = *s++;
        n--;
    }
    while (n) {
        *d++ = 0;
        n--;
    }
    return r;
}

#if STR_ASM
char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}
#else
char *strcat(char *d, const char *s)
{
    char *r;

    r = d;
    while (*d)
        d++;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}
#endif

/* The terminating NUL counts as part of the string (strchr(s, 0) finds it). */
#if STR_ASM
char *strchr(const char *s, int c)
{
    char *r;

#asm
        ld      hl, (ix+6)
        ld      c, (ix+9)
@loop:
        ld      a, (hl)
        cp      c
        jr      z, @found
        or      a
        jr      z, @none
        inc     hl
        jr      @loop
@none:
        ld      hl, 0
@found:
        ld      (ix-3), hl
#endasm
    return r;
}
#else
char *strchr(const char *s, int c)
{
    for (;;) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == 0)
            return 0;
        s++;
    }
}
#endif

char *strrchr(const char *s, int c)
{
    const char *r;

    r = 0;
    for (;;) {
        if (*s == (char)c)
            r = s;
        if (*s == 0)
            return (char *)r;
        s++;
    }
}

#if STR_ASM
void *memcpy(void *d, const void *s, unsigned n)
{
#asm
        ld      bc, (ix+12)
        ld      hl, 0
        or      a
        sbc     hl, bc
        jr      z, @done
        ld      de, (ix+6)
        ld      hl, (ix+9)
        ldir
@done:
#endasm
    return d;
}
#else
void *memcpy(void *d, const void *s, unsigned n)
{
    char *dp;
    const char *sp;

    dp = d;
    sp = s;
    while (n) {
        *dp++ = *sp++;
        n--;
    }
    return d;
}
#endif

/* Overlap-safe: copies backward when the destination starts inside the
 * source, so each byte is read before the copy overwrites it; forward
 * otherwise, as memcpy does. */
#if STR_ASM
/* Forward with LDIR unless d starts inside s's n bytes (s < d < s + n),
 * then backward with LDDR from the last bytes. */
void *memmove(void *d, const void *s, unsigned n)
{
#asm
        ld      bc, (ix+12)
        ld      hl, 0
        or      a
        sbc     hl, bc
        jr      z, @done
        ld      hl, (ix+6)
        ld      de, (ix+9)
        or      a
        sbc     hl, de
        jr      z, @done
        jr      c, @fwd
        or      a
        sbc     hl, bc
        jr      nc, @fwd
#endasm
#asm
        ld      hl, (ix+9)
        add     hl, bc
        dec     hl
        push    hl
        ld      hl, (ix+6)
        add     hl, bc
        dec     hl
        ex      de, hl
        pop     hl
        lddr
        jr      @done
@fwd:
        ld      de, (ix+6)
        ld      hl, (ix+9)
        ldir
@done:
#endasm
    return d;
}
#else
void *memmove(void *d, const void *s, unsigned n)
{
    char *dp;
    const char *sp;

    dp = d;
    sp = s;
    if (dp <= sp || dp >= sp + n) {
        while (n) {
            *dp++ = *sp++;
            n--;
        }
    } else {
        dp = dp + n;
        sp = sp + n;
        while (n) {
            *--dp = *--sp;
            n--;
        }
    }
    return d;
}
#endif

#if STR_ASM
/* The first byte stored, then LDIR copies each byte to the next. */
void *memset(void *d, int c, unsigned n)
{
#asm
        ld      bc, (ix+12)
        ld      hl, 0
        or      a
        sbc     hl, bc
        jr      z, @done
        ld      hl, (ix+6)
        ld      a, (ix+9)
        ld      (hl), a
        dec     bc
        ld      de, 0
        ex      de, hl
        or      a
        sbc     hl, bc
        jr      z, @done
        ex      de, hl
        push    hl
        pop     de
        inc     de
        ldir
@done:
#endasm
    return d;
}
#else
void *memset(void *d, int c, unsigned n)
{
    char *p;

    p = d;
    while (n) {
        *p++ = c;
        n--;
    }
    return d;
}
#endif

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p;
    const unsigned char *q;

    p = a;
    q = b;
    while (n) {
        if (*p != *q)
            return *p - *q;
        p++;
        q++;
        n--;
    }
    return 0;
}

#if STR_ASM
/* CPIR stops just past a match (Z set) or when BC runs out. */
void *memchr(const void *s, int c, size_t n)
{
    void *r;

#asm
        ld      bc, (ix+12)
        ld      hl, 0
        or      a
        sbc     hl, bc
        jr      z, @none
        ld      hl, (ix+6)
        ld      a, (ix+9)
        cpir
        jr      nz, @none
        dec     hl
        jr      @out
@none:
        ld      hl, 0
@out:
        ld      (ix-3), hl
#endasm
    return r;
}
#else
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p;

    p = s;
    while (n > 0) {
        if (*p == (unsigned char)c)
            return (void *)p;
        p++;
        n--;
    }
    return NULL;
}
#endif

/* At most n characters of s after d's, then always a NUL (unlike strncpy). */
char *strncat(char *d, const char *s, size_t n)
{
    char *p;

    p = d + strlen(d);
    while (n > 0 && *s) {
        *p = *s;
        p++;
        s++;
        n--;
    }
    *p = 0;
    return d;
}

/* The "C" locale collates by byte value. */
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* The "C" locale's transformation is the identity: s is copied if it fits
 * in n bytes with its NUL; the length is returned either way, so a caller
 * can size a buffer. */
size_t strxfrm(char *d, const char *s, size_t n)
{
    size_t len;

    len = strlen(s);
    if (len < n)
        strcpy(d, s);
    return len;
}

/* strspn: the length of s's start made only of set's characters; strcspn:
 * of its start made of none of them. strchr finds a character in set (but
 * never the NUL, which the *p test stops at first). */
size_t strspn(const char *s, const char *set)
{
    const char *p;

    p = s;
    while (*p && strchr(set, *p) != NULL)
        p++;
    return p - s;
}

size_t strcspn(const char *s, const char *set)
{
    const char *p;

    p = s;
    while (*p && strchr(set, *p) == NULL)
        p++;
    return p - s;
}

char *strpbrk(const char *s, const char *set)
{
    s = s + strcspn(s, set);
    return *s ? (char *)s : NULL;
}

/* The simple search: at each position whose first character matches, a
 * full comparison; O(length of s times length of find) at worst. An empty
 * find matches at s. */
char *strstr(const char *s, const char *find)
{
    size_t k;

    k = strlen(find);
    if (k == 0)
        return (char *)s;
    for (; *s; s++)
        if (*s == *find && strncmp(s, find, k) == 0)
            return (char *)s;
    return NULL;
}

/* Tokens separated by runs of sep's characters; the string is remembered
 * between calls, and NULL continues it (C89 4.11.5.8). Each token's end is
 * overwritten with a NUL, and next points past it; next is NULL once the
 * string is used up. */
char *strtok(char *s, const char *sep)
{
    static char *next;
    char *start;

    if (s == NULL)
        s = next;
    if (s == NULL)
        return NULL;
    s = s + strspn(s, sep);
    if (*s == 0) {
        next = NULL;
        return NULL;
    }
    start = s;
    s = s + strcspn(s, sep);
    if (*s) {
        *s = 0;
        next = s + 1;
    } else {
        next = NULL;
    }
    return start;
}

/* One short message per errno value (c89_spec.md 15). */
char *strerror(int errnum)
{
    switch (errnum) {
    case 0:
        return "no error";
    case ENOENT:
        return "no such file or directory";
    case EIO:
        return "input/output error";
    case EBADF:
        return "not an open file stream";
    case ENOMEM:
        return "not enough memory";
    case EEXIST:
        return "file exists";
    case EINVAL:
        return "invalid argument";
    case EMFILE:
        return "too many open files";
    case EDOM:
        return "argument out of domain";
    case ERANGE:
        return "result out of range";
    }
    return "unknown error";
}
