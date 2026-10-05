/* t_llio.c - long long in the library (the default mode's, C99's): the
 * printf family's ll and j conversions with their flags, widths and
 * precisions, the scanf family's, %lln, strtoll, strtoull, atoll, llabs,
 * lldiv, and the limits of <limits.h> and <stdint.h>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include "check.h"

char buf[200];

/* v's two halves, as the target stores it */
void checkq(long long v, unsigned long hi, unsigned long lo)
{
    unsigned char *p;
    unsigned long glo;
    unsigned long ghi;

    p = (unsigned char *)&v;
    glo = p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
    ghi = p[4] | (unsigned long)p[5] << 8 | (unsigned long)p[6] << 16 | (unsigned long)p[7] << 24;
    check(glo == lo && ghi == hi, 1);
}

void print(void)
{
    long long n;

    check(sprintf(buf, "%lld", 0LL), 1);
    check_str(buf, "0");
    check(sprintf(buf, "%lld", -1LL), 2);
    check_str(buf, "-1");
    check(sprintf(buf, "%lld", LLONG_MAX), 19);
    check_str(buf, "9223372036854775807");
    check(sprintf(buf, "%lld", LLONG_MIN), 20);
    check_str(buf, "-9223372036854775808");
    check(sprintf(buf, "%llu", ULLONG_MAX), 20);
    check_str(buf, "18446744073709551615");
    check(sprintf(buf, "%llx", 0x123456789ABCDEF0LL), 16);
    check_str(buf, "123456789abcdef0");
    check(sprintf(buf, "%#llX", 0xFEDCBA9876543210ULL), 18);
    check_str(buf, "0XFEDCBA9876543210");
    check(sprintf(buf, "%llo", ULLONG_MAX), 22);
    check_str(buf, "1777777777777777777777");
    check(sprintf(buf, "%#llo", 8LL), 3);
    check_str(buf, "010");
    check(sprintf(buf, "%lli|%+lld|% lld", 42LL, 42LL, 42LL), 10);
    check_str(buf, "42|+42| 42");
    check(sprintf(buf, "%25lld|", -1234567890123LL), 26);
    check_str(buf, "           -1234567890123|");
    check(sprintf(buf, "%-15lldX", 1234567890123LL), 16);
    check_str(buf, "1234567890123  X");
    check(sprintf(buf, "%020lld", -1234567890123LL), 20);
    check_str(buf, "-0000001234567890123");
    check(sprintf(buf, "%.15lld", 1234567890123LL), 15);
    check_str(buf, "001234567890123");
    check(sprintf(buf, "%.0lld|%.0llx", 0LL, 0ULL), 1);
    check_str(buf, "|");
    check(sprintf(buf, "%#llx", 0ULL), 1);
    check_str(buf, "0");
    /* the arguments after a long long are where they should be */
    check(sprintf(buf, "%d %lld %s %llu %c %ld", 7, -5000000000LL, "s", 5000000000ULL, 'c', 12L), 31);
    check_str(buf, "7 -5000000000 s 5000000000 c 12");
    check(sprintf(buf, "%jd %jx", (intmax_t)-3, (uintmax_t)255), 5);
    check_str(buf, "-3 ff");
    check(sprintf(buf, "%*lld|%-*.*llu|", 6, 12LL, 5, 3, 7ULL), 13);
    check_str(buf, "    12|007  |");
    check(sprintf(buf, "abc%lln", &n), 3);
    checkq(n, 0, 3);
    n = -1;
    check(sprintf(buf, "%lldxy%lln", 123456789012LL, &n), 14);
    checkq(n, 0, 14);
}

void scan(void)
{
    long long a;
    long long b;
    unsigned long long u;
    long long n;
    int i;

    check(sscanf("-9223372036854775808 18446744073709551615", "%lld %llu", &a, &u), 2);
    checkq(a, 0x80000000UL, 0);
    checkq((long long)u, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    check(sscanf("0x123456789abcdef0 0777 -0x10", "%lli %lli %lli", &a, &b, &n), 3);
    checkq(a, 0x12345678UL, 0x9ABCDEF0UL);
    checkq(b, 0, 511);
    checkq(n, 0xFFFFFFFFUL, 0xFFFFFFF0UL);
    check(sscanf("ffffffffff 12345678901234", "%llx %5lld%lld", &u, &a, &b), 3);
    checkq((long long)u, 0xFFUL, 0xFFFFFFFFUL);
    checkq(a, 0, 12345);
    checkq(b, 0, 678901234);
    check(sscanf("7 1099511627776", "%d %jd%lln", &i, &a, &n), 2);
    check(i, 7);
    checkq(a, 0x100UL, 0);
    checkq(n, 0, 15);
    check(sscanf("x", "%lld", &a), 0);
    check(sscanf("-x", "%lld", &a), 0);
}

void convert(void)
{
    char *end;

    checkq(strtoll("  -123456789012345", &end, 10), 0xFFFF8FB7UL, 0x79F22087UL);
    check(*end, 0);
    checkq(strtoll("9223372036854775807", NULL, 10), 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    checkq(strtoll("-9223372036854775808", NULL, 10), 0x80000000UL, 0);
    errno = 0;
    checkq(strtoll("9223372036854775808", NULL, 10), 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    check(errno, ERANGE);
    errno = 0;
    checkq(strtoll("-9223372036854775809", &end, 10), 0x80000000UL, 0);
    check(errno, ERANGE);
    check(*end, 0);
    errno = 0;
    checkq(strtoll("99999999999999999999999", &end, 0), 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    check(errno, ERANGE);
    check(*end, 0);
    checkq(strtoll("0x7fffffffffffffffz", &end, 0), 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    check(*end, 'z');
    checkq(strtoll("zz", &end, 36), 0, 1295);
    checkq(strtoll("0x", &end, 16), 0, 0);
    check(*end, 'x');
    checkq(strtoll("junk", &end, 10), 0, 0);
    check(*end, 'j');
    checkq((long long)strtoull("18446744073709551615", NULL, 10), 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    errno = 0;
    checkq((long long)strtoull("18446744073709551616", NULL, 10), 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    check(errno, ERANGE);
    checkq((long long)strtoull("-1", NULL, 10), 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    checkq((long long)strtoull("0XABCDEF0123", NULL, 16), 0xABUL, 0xCDEF0123UL);
    checkq(atoll("  +4294967296x"), 1, 0);
    checkq(llabs(-5000000000LL), 1, 0x2A05F200UL);
    checkq(llabs(LLONG_MIN + 1), 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    {
        lldiv_t d = lldiv(-5000000007LL, 1000000000LL);
        checkq(d.quot, 0xFFFFFFFFUL, 0xFFFFFFFBUL);
        checkq(d.rem, 0xFFFFFFFFUL, 0xFFFFFFF9UL);
    }
}

void limits(void)
{
    checkq(LLONG_MAX, 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    checkq(LLONG_MIN, 0x80000000UL, 0);
    checkq((long long)ULLONG_MAX, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    check(sizeof(LLONG_MIN), 8);
    check(sizeof(ULLONG_MAX), 8);
    check(sizeof(int64_t), 8);
    check(sizeof(uint64_t), 8);
    check(sizeof(intmax_t), 8);
    check(sizeof(uintmax_t), 8);
    checkq(INT64_MIN, 0x80000000UL, 0);
    checkq(INTMAX_MAX, 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    checkq((long long)UINTMAX_MAX, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    check((int64_t)-1 < 0, 1);
    check((uint64_t)-1 > 0, 1);
    check(sizeof(INT64_C(1)), 8);
    checkq((long long)UINT64_C(18446744073709551615), 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    check(sizeof(INTMAX_C(0)), 8);
    check(sizeof(INT32_C(1)), 4);
    check(sizeof(UINT24_C(1)), 3);
    check(UINT24_C(1) - 2 > 0, 1);
#if INTMAX_MAX != 9223372036854775807
    check(0, 1);
#endif
#if -1 > 0 || ULLONG_MAX != 18446744073709551615u || (ULLONG_MAX >> 32) != 0xFFFFFFFF
    check(0, 2);
#endif
}

int main(void)
{
    print();
    scan();
    convert();
    limits();
    return finish();
}
