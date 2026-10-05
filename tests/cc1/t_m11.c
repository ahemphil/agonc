/* t_m11.c - cc1's execution test for M11: wide character constants and
 * string literals (c89_spec.md 5), and multi-character constants, checked
 * at run time.
 *
 * Compiled by cc1 + cc2 + ld and run on the emulator (no cpp: wchar_t is
 * declared here as <stddef.h> has it). Exits 0 if every check passes, else
 * the number of the first failing check (see main).
 */

void agon_emu_exit(int status);

typedef int wchar_t;

int fails;
int first;
int count;

void check(int got, int want)
{
    count++;
    if (got != want) {
        fails++;
        if (first == 0)
            first = count;
    }
}

wchar_t gw[] = L"hi";                   /* an array of unknown size */
wchar_t gfix[5] = L"ab";                /* the rest zero */
wchar_t *gp = L"xyz";                   /* a wide string object inside an initialiser */
wchar_t *gtab[2] = { L"one", L"two" };  /* two of them */
char narrow[] = "ab" "cd";
wchar_t gesc[] = L"\x10000z\777";     /* wide escapes beyond a byte (C89 3.1.3.4) */

/* A function-pointer parameter after a named one, and a definition
 * returning a function pointer (M12: cc1 lost the earlier names). */
int twice(int x)
{
    return 2 * x;
}

int negate(int x)
{
    return -x;
}

int apply(int a, int (*g)(int))
{
    return g(a);
}

int (*pick(int which, int (*a)(int), int (*b)(int)))(int)
{
    return which ? a : b;
}

/* va_end's argument is evaluated for its side effects (M12: GCC's
 * va-arg-21.c). */
int va_side(int n, ...)
{
    char *ap[2];
    char **p;

    p = ap;
    __va_start(ap[0], n);
    __va_end(*p++);
    return p - ap;
}

int wsum(wchar_t *s)
{
    int n;

    n = 0;
    while (*s) {
        n = n + *s;
        s++;
    }
    return n;
}

int main(void)
{
    wchar_t lw[] = L"ok";
    wchar_t *p;
    static wchar_t sw[] = L"s";

    check(L'a', 97);
    check(L'\xff', 255);                /* a byte as unsigned char */
    check('\xff', -1);                  /* but a char is signed */
    check(sizeof(L'a'), 3);             /* wchar_t is int */
    check('ab', 0x6162);                /* multi-character: a warning */
    check('abc', 0x616263);
    check(sizeof(L"ab"), 9);
    check(sizeof(gw), 9);
    check(gw[0], 'h');
    check(gw[1], 'i');
    check(gw[2], 0);
    check(gfix[1], 'b');
    check(gfix[2], 0);
    check(gfix[4], 0);
    check(gp[2], 'z');
    check(gp[3], 0);
    check(gtab[1][2], 'o');
    check(lw[1], 'k');
    check(lw[2], 0);
    check(sw[0], 's');
    p = L"\xff" L"b";                   /* adjacent wide literals join */
    check(p[0], 255);
    check(p[1], 'b');
    check(p[2], 0);
    check(sizeof(L"a" L"bc"), 12);
    check(wsum(L"AB"), 65 + 66);
    check(narrow[3], 'd');
    check(L'\400', 256);                /* a wide escape beyond a byte */
    check(L'\x123456', 0x123456);
    check(L'\xffffff', -1);             /* converted to wchar_t */
    check(gesc[0], 0x10000);
    check(gesc[1], 'z');
    check(gesc[2], 511);
    p = L"a\x1234";
    check(p[1], 0x1234);
    check(apply(21, twice), 42);
    check(pick(1, twice, negate)(5), 10);
    check(pick(0, twice, negate)(5), -5);
    check(va_side(1, 2), 1);
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
