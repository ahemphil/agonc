/* t_m8.c - cc1's execution test for M8's integer types: short, long,
 * unsigned long, signed char, const and volatile, C89's typing of integer
 * constants and the conversions between them, checked at run time.
 *
 * Compiled by cc1 + cc2 + ld and run on the emulator. Exits 0 if every
 * check passes, else the number of the first failing check (see main).
 * int is 24 bits, long 32 (abi.md 2).
 */

void agon_emu_exit(int status);

typedef char *va_list;

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

void checkl(long got, long want)
{
    count++;
    if (got != want) {
        fails++;
        if (first == 0)
            first = count;
    }
}

/* ---- declarations under test ------------------------------------------------------ */

long g_l = 100000L;
long g_neg = -2L;
unsigned long g_big = 4000000000UL;
short g_s = -5;
unsigned short g_us = 65535;
signed char g_sc = -3;
long g_tab[4] = { 1L, -1L, 0x12345678L, 16777216 };
short g_stab[3] = { 1, -2, 30000 };
const long g_cl = 123456789L;
const char g_msg[] = "ok";
volatile long g_vl;
long g_zero;
unsigned long g_uz;

struct rec {
    char c;
    long l;
    short s;
    unsigned long ul;
};

struct rec g_rec;

/* ---- constants and sizes ----------------------------------------------------------------- */

void test_constants(void)
{
    check(sizeof(short), 2);
    check(sizeof(long), 4);
    check(sizeof(unsigned long), 4);
    check(sizeof(signed char), 1);
    check(sizeof(struct rec), 11);
    check(sizeof(1L), 4);
    check(sizeof(1u), 3);
    check(sizeof(8388607), 3);          /* INT_MAX: int */
    check(sizeof(8388608), 4);          /* a decimal beyond int: long */
    check(sizeof(0x7FFFFF), 3);
    check(sizeof(0x800000), 3);         /* hex beyond int: unsigned int */
    check(sizeof(0x1000000), 4);        /* beyond unsigned int: long */
    check(sizeof(0xFFFFFFFF), 4);       /* unsigned long */
    check(sizeof(16777216u), 4);        /* u beyond unsigned int: unsigned long */
    check(sizeof(g_tab), 16);
    check(sizeof(g_stab), 6);
    check(0x800000 > 0, 1);             /* unsigned int */
    check(-1 < 0x800000, 0);            /* -1 converted to unsigned int */
    check(-1L < 0x800000, 1);           /* long holds every unsigned int */
    check(-1L < 1u, 1);
    check(-1L < 0UL, 0);
    check(8388608 > 0, 1);              /* long, not a wrapped int */
    checkl(0xFFFFFFFF + 1, 0L);
    checkl(2147483647 + 1L, -2147483647L - 1);
    checkl(1L << 31 >> 31, -1L);
    checkl(1UL << 31 >> 31, 1L);
    checkl(100000L * 100000L, 1410065408L);     /* 10^10 mod 2^32 */
    checkl(-7L / 2, -3L);
    checkl(-7L % 2, -1L);
    checkl(4000000000UL / 3, 1333333333L);
    check((int)0x12345678L, 0x345678);
    check((unsigned char)0x12345678L, 0x78);
    check((short)0x12348765L, -30875);
    check((unsigned short)-1L, 65535);
    checkl((long)-1, -1L);
    checkl((long)0xFFFFFFu, 16777215L);
    checkl((unsigned long)-1, 0xFFFFFFFFUL);
    checkl(~0L, -1L);
    checkl(-(-2147483647L - 1), -2147483647L - 1);
    check(!0x1000000L, 0);
    check(0x1000000L && 1, 1);
    check(0L || 0x1000000L, 1);
    checkl(0x1000000L ? 5L : 6L, 5L);
    checkl('a' + 0L, 97L);
    check(sizeof('a'), 3);
}

/* ---- run-time arithmetic --------------------------------------------------------------- */

long l_add(long a, long b) { return a + b; }
long l_mul(long a, long b) { return a * b; }
long l_div(long a, long b) { return a / b; }
long l_rem(long a, long b) { return a % b; }
unsigned long ul_div(unsigned long a, unsigned long b) { return a / b; }
unsigned long ul_rem(unsigned long a, unsigned long b) { return a % b; }
long l_shl(long a, int n) { return a << n; }
long l_shr(long a, int n) { return a >> n; }
unsigned long ul_shr(unsigned long a, int n) { return a >> n; }
int l_lt(long a, long b) { return a < b; }
int ul_lt(unsigned long a, unsigned long b) { return a < b; }

void test_arith(void)
{
    long a;
    long b;
    unsigned long u;

    a = 123456789L;
    b = -98765L;
    checkl(l_add(a, b), 123358024L);
    checkl(a - b, 123555554L);
    checkl(l_mul(a, 10L), 1234567890L);
    checkl(l_mul(b, b), 1164590633L);                   /* 9754525225 mod 2^32 */
    checkl(l_mul(-3L, 7L), -21L);
    checkl(l_div(a, 1000L), 123456L);
    checkl(l_div(a, b), -1250L);
    checkl(l_rem(a, b), 539L);
    checkl(l_div(-2147483647L - 1, -1L), -2147483647L - 1);   /* wraps */
    checkl(ul_div(4000000000UL, 7UL), 571428571L);
    checkl(ul_rem(4000000000UL, 7UL), 3L);
    checkl(l_shl(1L, 24), 16777216L);
    checkl(l_shl(0x12345678L, 4), 0x23456780L);
    checkl(l_shr(-256L, 4), -16L);
    checkl(ul_shr(0x80000000UL, 31), 1L);
    checkl(l_shr(0x40000000L, 30), 1L);
    checkl(a & 0xFF00FFL, 0x5B0015L);
    checkl(a & 0xFFFF0000L, 0x075B0000L);
    checkl(a | 0xF0000000L, (long)0xF75BCD15UL);
    checkl(a ^ -1L, ~a);
    checkl(-a, -123456789L);
    check(l_lt(-1L, 0L), 1);
    check(l_lt(0x1000000L, 0xFFFFFFL), 0);
    check(l_lt(-2147483647L - 1, 2147483647L), 1);
    check(ul_lt(0x80000000UL, 1UL), 0);
    check(ul_lt(1UL, 0xFFFFFFFFUL), 1);
    check(a == 123456789L, 1);
    check(a != 123456789L + 0x1000000L, 1);
    check(a >= a, 1);
    check(b <= a, 1);
    check(b > a, 0);
    u = 3000000000UL;
    check(u > 2000000000UL, 1);
    check(u / 1000000UL == 3000UL, 1);
    checkl((long)(u >> 16), 45776L);
    check(!a, 0);
    check(!g_zero, 1);
    check(a && b, 1);
    check(g_zero || g_uz, 0);
    checkl(g_zero ? a : b, b);
}

/* ---- conversions -------------------------------------------------------------------------- */

void test_conversions(void)
{
    long l;
    int i;
    unsigned int ui;
    char c;
    unsigned char uc;
    signed char sc;
    short s;
    unsigned short us;
    unsigned long ul;

    i = -2;
    l = i;
    checkl(l, -2L);
    ui = 0xFFFFFE;
    l = ui;
    checkl(l, 16777214L);
    ul = i;
    check(ul == 0xFFFFFFFEUL, 1);
    c = -1;
    l = c;
    checkl(l, -1L);
    uc = 200;
    l = uc;
    checkl(l, 200L);
    sc = -100;
    l = sc;
    checkl(l, -100L);
    s = -30000;
    l = s;
    checkl(l, -30000L);
    us = 50000;
    l = us;
    checkl(l, 50000L);
    l = 0x12345678L;
    i = l;
    check(i, 0x345678);
    c = l;
    check(c, 0x78);
    s = l;
    check(s, 0x5678);
    l = 0x0001FFFFL;
    s = l;
    check(s, -1);
    us = l;
    check(us, 65535);
    s = 40000;                          /* int to short wraps */
    check(s, 40000 - 65536);
    us = -1;
    check(us, 65535);
    i = s + us;                         /* both promote to int */
    check(i, 40000 - 65536 + 65535);
    sc = 200;
    check(sc, -56);
    check((signed char)200 == (char)200, 1);   /* plain char is signed */
    l = -1;
    ul = l;
    check(ul > 0UL, 1);
    check((int)(ul >> 8), -1);          /* the low 24 bits of 0xFFFFFF */
    checkl((long)(unsigned int)-1, 16777215L);
    checkl(i * 1L, (long)i);
    l = 3;
    i = 5;
    checkl(i / l, 1L);
    checkl(i - l * 7, -16L);
}

/* ---- shorts ------------------------------------------------------------------------------ */

short s_twice(short v) { return v * 2; }
unsigned short us_inc(unsigned short v) { return v + 1; }

void test_short(void)
{
    short s;
    short arr[4];
    short *p;
    unsigned short u;
    int i;

    s = 1000;
    check(s_twice(s), 2000);
    check(s_twice(20000), 40000 - 65536);
    check(us_inc(65535), 0);
    u = 65535;
    u++;
    check(u, 0);
    s = 32767;
    s++;
    check(s, -32768);
    s = -32768;
    s--;
    check(s, 32767);
    s = 100;
    s += 32700;
    check(s, 32800 - 65536);
    s = 10;
    s *= -3;
    check(s, -30);
    s <<= 12;
    check(s, 8192);                     /* -122880 mod 65536 */
    for (i = 0; i < 4; i++)
        arr[i] = i * 10000 - 15000;
    check(arr[0], -15000);
    check(arr[3], 15000);
    p = arr;
    p++;
    check(*p, -5000);
    check(p[2], 15000);
    check(*p++, -5000);
    check(*p, 5000);
    check(g_s, -5);
    check(g_us, 65535);
    check(g_sc, -3);
    check(g_stab[1], -2);
    check(g_stab[2], 30000);
    g_s = -300;
    g_s -= 1;
    check(g_s, -301);
    check(g_s++, -301);
    check(g_s, -300);
    check(++g_us, 0);
    check(sizeof(arr), 8);
    check((char *)&arr[3] - (char *)&arr[0], 6);
}

/* ---- longs in memory, parameters and returns --------------------------------------------- */

long sum3(long a, int b, long c) { return a + b + c; }
long pick(int k, long a, long b) { return k ? a : b; }
long *next_long(long *p) { return p + 1; }

void test_long_memory(void)
{
    long arr[5];
    long *p;
    long x;
    long y;
    int i;
    struct rec r;
    struct rec *rp;

    for (i = 0; i < 5; i++)
        arr[i] = (long)i * 1000000L - 2000000L;
    check(arr[0] == -2000000L, 1);
    checkl(arr[4], 2000000L);
    p = arr;
    checkl(*next_long(p), -1000000L);
    p += 2;
    checkl(*p, 0L);
    checkl(p[1], 1000000L);
    checkl(*(p - 2), -2000000L);
    check(p - arr, 2);
    check((char *)p - (char *)arr, 8);
    x = 3;
    checkl(arr[x], 1000000L);           /* a long index */
    checkl(*(arr + x + 1L), 2000000L);
    checkl(sum3(1000000L, -1, 0x1000000L), 17777215L);
    checkl(pick(1, 5L, 6L), 5L);
    checkl(pick(0, 5L, -6L), -6L);
    checkl(g_l, 100000L);
    checkl(g_neg, -2L);
    check(g_big == 4000000000UL, 1);
    checkl(g_tab[1], -1L);
    checkl(g_tab[2], 0x12345678L);
    checkl(g_tab[3], 0x1000000L);
    checkl(g_cl, 123456789L);
    check(g_msg[1], 'k');
    g_vl = 7;
    g_vl += 0x10000000L;
    checkl(g_vl, 0x10000007L);
    r.c = 'x';
    r.l = -123456789L;
    r.s = -2;
    r.ul = 0xDEADBEEFUL;
    rp = &r;
    check(rp->c, 'x');
    checkl(rp->l, -123456789L);
    check(rp->s, -2);
    check(rp->ul == 0xDEADBEEFUL, 1);
    check((char *)&r.ul - (char *)&r, 7);
    rp->l += rp->s;
    checkl(r.l, -123456791L);
    g_rec = r;
    check(g_rec.ul == 0xDEADBEEFUL, 1);
    x = 10;
    y = x++;
    checkl(y, 10L);
    checkl(x, 11L);
    y = ++x;
    checkl(y, 12L);
    y = x--;
    checkl(y, 12L);
    y = --x;
    checkl(y, 10L);
    x = 0xFFFFFFL;
    x++;
    checkl(x, 0x1000000L);
    x--;
    checkl(x, 0xFFFFFFL);
    x = 0;
    x--;
    checkl(x, -1L);
    p = arr;
    checkl(*p++, -2000000L);
    checkl(*++p, 0L);
}

/* ---- compound assignments with mixed widths --------------------------------------------- */

void test_compound(void)
{
    long l;
    int i;
    char c;
    unsigned long ul;
    int *ip;
    int ia[2];

    l = 1000000L;
    l += 5;
    checkl(l, 1000005L);
    l -= 0x1000000L;
    checkl(l, 1000005L - 16777216L);
    l = 7;
    l *= 1000000;
    checkl(l, 7000000L);
    l /= -3;
    checkl(l, -2333333L);
    l %= 1000;
    checkl(l, -333L);
    l = 1;
    l <<= 30;
    checkl(l, 0x40000000L);
    l >>= 29;
    checkl(l, 2L);
    l |= 0x10000000L;
    l &= 0x10000003L;
    l ^= 1L;
    checkl(l, 0x10000003L);
    i = 100;
    i += 0x1000005L;                    /* computed in long, stored as int */
    check(i, 105);
    i = 1000;
    i *= 3L;
    check(i, 3000);
    i = -1000;
    i /= 7L;
    check(i, -142);
    i = 1000;
    i %= 300L;
    check(i, 100);
    i = 5;
    i <<= 2L;
    check(i, 20);
    c = 100;
    c += 0x10000000L + 100;
    check(c, -56);
    ul = 5;
    ul -= 6;
    check(ul == 0xFFFFFFFFUL, 1);
    ia[0] = 10;
    ia[1] = 20;
    ip = ia;
    *ip += 1L;
    check(ia[0], 11);
    ip[1] -= 0x1000001L;
    check(ia[1], 19);
}

/* ---- switch on a long -------------------------------------------------------------------- */

int classify(long v)
{
    switch (v) {
    case 0:
        return 1;
    case -1:
        return 2;
    case 0x1000000L:
        return 3;
    case 0x7FFFFFFFL:
        return 4;
    case -2147483647L - 1:
        return 5;
    case 16777215:
        return 6;
    }
    return 0;
}

int classify_u(unsigned long v)
{
    switch (v) {
    case 0xFFFFFFFFUL:
        return 1;
    case 0x80000000UL:
        return 2;
    default:
        return 3;
    }
}

int classify_s(short v)
{
    switch (v) {
    case -1:
        return 1;
    case 32767:
        return 2;
    }
    return 0;
}

void test_switch(void)
{
    check(classify(0L), 1);
    check(classify(-1L), 2);
    check(classify(0x1000000L), 3);
    check(classify(0x7FFFFFFFL), 4);
    check(classify(-2147483647L - 1), 5);
    check(classify(16777215L), 6);
    check(classify(0xFFFFFFFFL), 2);    /* -1 */
    check(classify(0x01000001L), 0);
    check(classify(0xFFFFFFL), 6);
    check(classify_u(-1L), 1);
    check(classify_u(0x80000000UL), 2);
    check(classify_u(0x7FFFFFFFUL), 3);
    check(classify_s(-1), 1);
    check(classify_s(32767), 2);
    check(classify_s(65535), 1);        /* converted to short */
}

/* ---- const and volatile -------------------------------------------------------------------- */

int read_through(const int *p) { return *p; }
long lread_through(const volatile long *p) { return *p; }

void test_qualifiers(void)
{
    const int ci = 5;
    volatile int vi;
    const char *s;
    char *const cp = "xyz";
    register int ri;
    auto long al;

    check(ci, 5);
    vi = 3;
    vi += ci;
    check(vi, 8);
    s = "abc";
    s++;
    check(*s, 'b');
    check(read_through(&ci), 5);
    check(cp[2], 'z');
    ri = 4;
    al = 0x10000L * ri;
    checkl(lread_through(&al), 0x40000L);
    check(sizeof(const long), 4);
    check(sizeof(volatile short), 2);
}

/* ---- variable arguments -------------------------------------------------------------------- */

long sum_longs(int n, ...)
{
    va_list ap;
    long s;

    __va_start(ap, n);
    s = 0;
    while (n-- > 0)
        s += __va_arg(ap, long);
    __va_end(ap);
    return s;
}

/* "l" a long, "i" an int, "s" a short (passed as an int) */
long mixed(char *fmt, ...)
{
    va_list ap;
    long s;

    __va_start(ap, fmt);
    s = 0;
    for (; *fmt; fmt++) {
        s = s * 10;
        if (*fmt == 'l')
            s += __va_arg(ap, long);
        else
            s += __va_arg(ap, int);
    }
    __va_end(ap);
    return s;
}

long after_long(long first, ...)
{
    va_list ap;
    int v;

    __va_start(ap, first);
    v = __va_arg(ap, int);
    __va_end(ap);
    return first + v;
}

void test_varargs(void)
{
    short sh;

    sh = -7;
    checkl(sum_longs(0), 0L);
    checkl(sum_longs(3, 1L, 0x1000000L, -2L), 0x0FFFFFFL);
    checkl(mixed("lil", 1L, 2, 3L), 123L);
    checkl(mixed("isl", 5, sh, 0x10000000L), 500L - 70L + 0x10000000L);
    checkl(after_long(0x1000000L, -1), 0xFFFFFFL);
}

int main(void)
{
    test_constants();
    test_arith();
    test_conversions();
    test_short();
    test_long_memory();
    test_compound();
    test_switch();
    test_qualifiers();
    test_varargs();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
