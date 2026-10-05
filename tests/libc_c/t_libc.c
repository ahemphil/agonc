/* t_libc.c - the library's string.c, ctype.c and stdlib.c, and the
 * limits.h and stdint.h headers, compiled by our own compiler and checked
 * at run time on the emulator. Exits 0, or the number of the first failing
 * check. */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include "check.h"

void test_string(void)
{
    char buf[32];
    char *p;
    int i;

    check(strlen(""), 0);
    check(strlen("hello"), 5);
    check(strcmp("abc", "abc"), 0);
    check(strcmp("abc", "abd") < 0, 1);
    check(strcmp("abd", "abc") > 0, 1);
    check(strcmp("ab", "abc") < 0, 1);
    check(strcmp("\xff", "a") > 0, 1);         /* compared as unsigned char */
    check(strncmp("abcdef", "abcxyz", 3), 0);
    check(strncmp("abcdef", "abcxyz", 4) < 0, 1);
    check(strncmp("ab", "ab", 10), 0);
    check(strncmp("x", "y", 0), 0);
    p = strcpy(buf, "copy");
    check(p == buf, 1);
    check(strcmp(buf, "copy"), 0);
    strcat(buf, " me");
    check(strcmp(buf, "copy me"), 0);
    memset(buf, 'z', 10);
    strncpy(buf, "ab", 5);
    check(buf[1], 'b');
    check(buf[2], 0);
    check(buf[4], 0);
    check(buf[5], 'z');                          /* padding stops at n */
    strncpy(buf, "abcdef", 3);
    check(buf[2], 'c');
    check(buf[3], 0);                            /* the earlier padding, untouched */
    p = "hello world";
    check(strchr(p, 'o') - p, 4);
    check(strrchr(p, 'o') - p, 7);
    check(strchr(p, 'z') == 0, 1);
    check(strchr(p, 0) - p, 11);
    check(strrchr(p, 'h') - p, 0);
    for (i = 0; i < 10; i++)
        buf[i] = i;
    memmove(buf + 2, buf, 5);                    /* overlapping, forward destination */
    check(buf[2], 0);
    check(buf[6], 4);
    check(buf[7], 7);
    for (i = 0; i < 10; i++)
        buf[i] = i;
    memmove(buf, buf + 3, 5);                    /* overlapping, backward destination */
    check(buf[0], 3);
    check(buf[4], 7);
    check(buf[5], 5);
    memcpy(buf, "12345", 6);
    check(strcmp(buf, "12345"), 0);
    check(memcmp("abc", "abd", 3) < 0, 1);
    check(memcmp("abc", "abd", 2), 0);
    check(memcmp("\x80", "\x7f", 1) > 0, 1);     /* unsigned */
    check(memset(buf, 0, 0) == buf, 1);
}

void test_ctype(void)
{
    int c;
    int n;

    n = 0;
    for (c = -1; c < 256; c++)
        if (isdigit(c))
            n++;
    check(n, 10);
    n = 0;
    for (c = -1; c < 256; c++)
        if (isalpha(c))
            n++;
    check(n, 52);
    n = 0;
    for (c = -1; c < 256; c++)
        if (isalnum(c))
            n++;
    check(n, 62);
    n = 0;
    for (c = -1; c < 256; c++)
        if (isspace(c))
            n++;
    check(n, 6);
    n = 0;
    for (c = -1; c < 256; c++)
        if (isxdigit(c))
            n++;
    check(n, 22);
    n = 0;
    for (c = -1; c < 256; c++)
        if (isprint(c))
            n++;
    check(n, 95);
    check(isupper('Q'), 1);
    check(isupper('q'), 0);
    check(islower('q'), 1);
    check(toupper('a'), 'A');
    check(toupper('Z'), 'Z');
    check(toupper('1'), '1');
    check(tolower('M'), 'm');
    check(tolower(-1), -1);
}

void test_stdlib(void)
{
    check(abs(-5), 5);
    check(abs(5), 5);
    check(abs(0), 0);
    check(atoi("123"), 123);
    check(atoi("  -42x"), -42);
    check(atoi("+7"), 7);
    check(atoi("\t\n 8388607"), 8388607);
    check(atoi("abc"), 0);
    check(atoi(""), 0);
}

void test_long(void)
{
    char *end;
    char *p;

    check(labs(-5L) == 5L, 1);
    check(labs(-2147483647L) == 2147483647L, 1);
    check(labs(0x10000000L) == 0x10000000L, 1);
    check(atol("  -2147483648") == LONG_MIN, 1);
    check(atol("123456789x") == 123456789L, 1);
    check(atol("+16777216") == 16777216L, 1);
    check(strtol("  -123abc", &end, 10) == -123L, 1);
    check(*end, 'a');
    check(strtol("0x1F", &end, 0) == 31L, 1);
    check(*end, 0);
    check(strtol("0X1f", &end, 16) == 31L, 1);
    check(strtol("1f", &end, 16) == 31L, 1);
    check(strtol("017", &end, 0) == 15L, 1);
    check(strtol("019", &end, 0) == 1L, 1);         /* octal stops at 9 */
    check(*end, '9');
    check(strtol("0", &end, 0) == 0L, 1);
    check(*end, 0);
    check(strtol("zz", &end, 36) == 1295L, 1);
    check(strtol("Zz", &end, 36) == 1295L, 1);
    p = "  xyz";
    check(strtol(p, &end, 10) == 0L, 1);
    check(end == p, 1);                              /* no digits: end is the start */
    p = "-";
    check(strtol(p, &end, 10) == 0L, 1);
    check(end == p, 1);
    check(strtol("0x", &end, 16) == 0L, 1);         /* the "0", then 'x' is not a prefix */
    check(*end, 'x');
    check(strtol("12", NULL, 1) == 0L, 1);          /* an invalid base */
    errno = 0;
    check(strtol("2147483647", &end, 10) == LONG_MAX, 1);
    check(errno, 0);
    check(strtol("2147483648", &end, 10) == LONG_MAX, 1);
    check(errno, ERANGE);
    errno = 0;
    check(strtol("-2147483648", &end, 10) == LONG_MIN, 1);
    check(errno, 0);
    check(strtol("-2147483649", &end, 10) == LONG_MIN, 1);
    check(errno, ERANGE);
    check(strtol("99999999999999999999z", &end, 10) == LONG_MAX, 1);
    check(*end, 'z');                                /* every digit is consumed */
    errno = 0;
    check(strtoul("4294967295", &end, 10) == ULONG_MAX, 1);
    check(errno, 0);
    check(strtoul("4294967296", &end, 10) == ULONG_MAX, 1);
    check(errno, ERANGE);
    check(strtoul("-1", &end, 10) == ULONG_MAX, 1); /* negated in unsigned long */
    check(strtoul("ffffffff", NULL, 16) == 0xFFFFFFFFUL, 1);
    check(strtoul("11111111111111111111111111111111", NULL, 2) == 0xFFFFFFFFUL, 1);
    check(strtoul("100000000", NULL, 16) == ULONG_MAX, 1);
}

void test_limits(void)
{
    int8_t i8;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;

    check(CHAR_BIT, 8);
    check(CHAR_MIN, -128);
    check(CHAR_MAX, 127);
    check(SCHAR_MIN, -128);
    check(UCHAR_MAX, 255);
    check(SHRT_MIN, -32768);
    check(SHRT_MAX, 32767);
    check(USHRT_MAX, 65535);
    check(INT_MAX, 8388607);
    check(INT_MIN, -8388607 - 1);
    check(sizeof(UINT_MAX), 3);
    check(UINT_MAX + 1, 0);                          /* unsigned int */
    check(sizeof(LONG_MAX), 4);
    check(LONG_MIN + LONG_MAX == -1L, 1);
    check(ULONG_MAX == 0xFFFFFFFFUL, 1);
    check(ULONG_MAX > 0, 1);                         /* unsigned long */
    check(sizeof(int8_t), 1);
    check(sizeof(int16_t), 2);
    check(sizeof(uint24_t), 3);
    check(sizeof(int32_t), 4);
    check(sizeof(intptr_t), sizeof(char *));
    check(sizeof(intmax_t), 8);                      /* long long, in the default mode */
    i8 = 200;
    check(i8, -56);
    u16 = -1;
    check(u16, 65535);
    i32 = INT32_MIN;
    check(i32 < 0 && i32 - 1 > 0, 1);
    u32 = UINT32_MAX;
    check(u32 + 1 == 0, 1);
    check(INT16_MAX, 32767);
    check(UINT8_MAX, 255);
    check(SIZE_MAX, 16777215);
}

/* ---- stddef.h, assert.h, EXIT_* (M11) ---------------------------------------- */

struct hdr {
    char a;
    int b;
    long c;
};

static int hdr_arr[offsetof(struct hdr, c)];    /* a constant */

static void test_headers(void)
{
    ptrdiff_t d;
    size_t z;
    wchar_t w;
    char buf[4];

    check(offsetof(struct hdr, a), 0);
    check(offsetof(struct hdr, b), 1);
    check(offsetof(struct hdr, c), 4);
    check(sizeof(hdr_arr), 4 * sizeof(int));
    d = &buf[3] - &buf[0];
    check(d, 3);
    z = sizeof(size_t);
    check(z, 3);
    check(sizeof(ptrdiff_t), 3);
    w = 'x';
    check(sizeof(wchar_t), 3);
    check(w, 'x');
    check(EXIT_SUCCESS, 0);
    check(EXIT_FAILURE, 200);
    assert(1 + 1 == 2);                 /* true: nothing happens */
    check(1, 1);
}

#define NDEBUG
#include <assert.h>

static void test_ndebug(void)
{
    int n;

    n = 0;
    assert(n++ == 5);                   /* NDEBUG: not even evaluated */
    check(n, 0);
}

int main(void)
{
    test_headers();
    test_ndebug();
    test_string();
    test_ctype();
    test_stdlib();
    test_long();
    test_limits();
    return finish();
}
