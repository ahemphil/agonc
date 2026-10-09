/* t_cpp99.c - the preprocessor's C99 features in the default mode,
 * checked at run time: variadic macros (__VA_ARGS__, with arguments and
 * without, through # and ##), and a sign after p in a preprocessing number
 * (hexadecimal floating constants).
 *
 * Built by cpp + cc1 + cc2 + ld (test_cpp.py, T5) and run on the emulator.
 * Exits 0 if every check passes, else the number of the first failing
 * check (see main).
 */

#include <stdarg.h>
#include <string.h>

void agon_emu_exit(int status);

int fails;
int first;
int count;

void check(long got, long want)
{
    count++;
    if (got != want) {
        fails++;
        if (first == 0)
            first = count;
    }
}

/* ---- variadic macros ---- */

#define COUNT_ARGS(...) count_args(0, __VA_ARGS__, -1)
#define FIRST_OF(x, ...) (x)
#define REST_SUM(x, ...) (x + sum3(__VA_ARGS__))
#define SHOW(...) #__VA_ARGS__
#define CALL(f, ...) f(__VA_ARGS__)
#define JOIN(a, ...) a ## __VA_ARGS__
#define NOTHING(...) 7

/* how many arguments come before the -1 that ends them */
int count_args(int dummy, ...)
{
    va_list ap;
    int n;

    va_start(ap, dummy);
    n = 0;
    while (va_arg(ap, int) != -1)
        n++;
    va_end(ap);
    return n;
}

int sum3(int a, int b, int c)
{
    return a + b + c;
}

int two(int a, int b)
{
    return a * 10 + b;
}

void test_variadic(void)
{
    int xy;

    xy = 5;
    check(COUNT_ARGS(1), 1);
    check(COUNT_ARGS(1, 2, 3), 3);
    check(FIRST_OF(4, 5, 6), 4);
    check(FIRST_OF(9), 9);                      /* nothing for the ... */
    check(REST_SUM(1, 2, 3, 4), 10);
    check(strcmp(SHOW(a, b, c), "a, b, c"), 0);
    check(strcmp(SHOW(), ""), 0);
    check(CALL(two, 3, 4), 34);
    check(CALL(sum3, (1, 2), 3, 4), 2 + 3 + 4);  /* a parenthesised comma stays inside */
    check(JOIN(x, y), 5);
    check(NOTHING(), 7);
    check(NOTHING(a, b, c), 7);
}

/* 0x1p-D is one preprocessing number, so D in it is not a macro call;
 * C89's rules would make it 0x1p - D, with D replaced */
#define D 5
#define STR(x) #x
#define XSTR(x) STR(x)

void test_numbers(void)
{
    check(strcmp(XSTR(0x1p-D), "0x1p-D"), 0);
    check(sizeof 0x1p-3, sizeof(double));      /* one floating constant */
}

int main(void)
{
    test_variadic();
    test_numbers();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
