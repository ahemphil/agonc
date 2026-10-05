/* string.c - <string.h>. size_t is unsigned int, so the functions written
 * with unsigned are the prototypes' size_t ones.
 *
 * Every function is a plain loop over bytes in portable C. Comparisons
 * (strcmp, strncmp, memcmp) compare bytes as unsigned char, as C89 4.11.4
 * requires, so a byte above 0x7F sorts after every ASCII one even though
 * char is signed. strtok keeps its place in a static, so only one string
 * can be split at a time. */

#include <string.h>
#include <errno.h>

unsigned strlen(const char *s)
{
    const char *p;

    p = s;
    while (*p)
        p++;
    return p - s;
}

/* Stops at the first difference or at a's end; b's end is then a
 * difference too, unless a ended at the same place. */
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

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

char *strcpy(char *d, const char *s)
{
    char *r;

    r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

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

/* The terminating NUL counts as part of the string (strchr(s, 0) finds it). */
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

/* Overlap-safe: copies backward when the destination starts inside the
 * source, so each byte is read before the copy overwrites it; forward
 * otherwise, as memcpy does. */
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
