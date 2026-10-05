/* testfwk.c - our stand-in for SDCC's regression-test framework
 * (support/regression/fwk/lib/testfwk.c), for run_suite.py sdcc. main runs
 * the suite the case generator appended to the test (__runSuite), prints
 * each failed assertion (the test runtime puts console output in a file),
 * and returns 0 only if every assertion held; otherwise the number that
 * failed. SDCC's own always exits 0 and leaves only printed text, which
 * the emulator cannot be trusted to deliver. */

#include <stdio.h>
#include <testfwk.h>

int __numTests;
static int failures;

void __fail(const char *szMsg, const char *szCond, const char *szFile, int line)
{
    failures++;
    printf("--- FAIL: %s: %s at %s:%d\n", szMsg, szCond, szFile, line);
}

void __prints(const char *s)
{
    (void)s;
}

void __printu(unsigned int n)
{
    (void)n;
}

void __printf(const char *szFormat, ...)
{
    (void)szFormat;
}

void _putchar(char c)
{
    (void)c;
}

void _initEmu(void)
{
}

void _exitEmu(void)
{
}

int main(void)
{
    __runSuite();
    printf("%d tests, %d failed\n", __numTests, failures);
    if (failures > 0)
        return failures < 199 ? failures : 199;
    return 0;
}
