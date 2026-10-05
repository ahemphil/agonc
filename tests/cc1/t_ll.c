/* t_ll.c - cc1's execution test for long long and unsigned long long (the
 * default mode's, C99's): the operators, the conversions, constants,
 * lvalues of every kind, calls, and constant folding against run time.
 * Every expected value is spelled as its two 32-bit halves and checked
 * byte by byte, so that a check never depends on the helpers it tests.
 *
 * Compiled by cc1 + cc2 + ld in the default mode and run on the emulator
 * (no cpp); also built by the PC (test_cc1.py), whose long long is the
 * same. Exits 0 if every check passes, else the number of the first
 * failing check (see main).
 */

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

/* v's bytes, low word first, as the target stores it */
void checkq(long long v, unsigned long hi, unsigned long lo)
{
    unsigned char *p;
    unsigned long glo;
    unsigned long ghi;

    p = (unsigned char *)&v;
    glo = p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
    ghi = p[4] | (unsigned long)p[5] << 8 | (unsigned long)p[6] << 16 | (unsigned long)p[7] << 24;
    count++;
    if (glo != lo || ghi != hi) {
        fails++;
        if (first == 0)
            first = count;
    }
}

/* values the compiler cannot fold */
long long ga = 0x123456789ABCDEF0LL;
long long gb = -5;
unsigned long long gu = 0xFFFFFFFFFFFFFFFFULL;
long long gzero;
int gi = -3;
unsigned int gui = 0xFFFFFF;
long gl = -2;
unsigned long gul = 0x80000000UL;
double gd = -12345678901.75;
float gf = 1099511627776.0f;    /* 2^40 */

long long add(long long a, long long b)
{
    return a + b;
}

/* mixed arguments: ints and doubles on either side of long longs */
long long mix(int i, long long a, double d, unsigned long long b, long l)
{
    return a - (long long)b + i + (long long)d + l;
}

unsigned long long ushr(unsigned long long v, int n)
{
    return v >> n;
}

long long sshr(long long v, int n)
{
    return v >> n;
}

long long shl(long long v, int n)
{
    return v << n;
}

struct rec {
    char c;
    long long q;
    unsigned long long u[2];
    int i;
};

struct rec grec = { 1, -1LL, { 2ULL, 0x8000000000000000ULL }, 7 };
long long garr[3] = { 1, -2, 0x7FFFFFFFFFFFFFFFLL };
unsigned long long gfold = (0xFFFFFFFFULL << 32 | 0xFFFF) / 3 + (1ULL << 63) % 1000;
long gnarrow[3] = { -2147483648, 0x180000000LL, 0x123456789LL };   /* long long constants, narrowed */
int gnarrowi = (int)0x1FF808080LL;

struct rec pass_rec(struct rec r)
{
    r.q = r.q * 3;
    return r;
}

void arith(void)
{
    long long a;
    long long b;
    unsigned long long u;

    a = ga;
    b = gb;
    u = gu;
    checkq(a + b, 0x12345678UL, 0x9ABCDEEBUL);
    checkq(a - b, 0x12345678UL, 0x9ABCDEF5UL);
    checkq(b - a, 0xEDCBA987UL, 0x6543210BUL);
    checkq(a * b, 0xA4FA4FA4UL, 0xFA4FA550UL);
    checkq(a * a, 0xA5E20890UL, 0xF2A52100UL);
    checkq(u + 1, 0, 0);
    checkq(u * u, 0, 1);
    checkq(a / b, 0xFC5BEEB4UL, 0xADDA39D0UL);
    checkq(a % b, 0, 0);
    checkq(a / 7, 0x0299C335UL, 0xCCF668FDUL);
    checkq(a % 7, 0, 5);
    checkq(-a / 7, 0xFD663CCAUL, 0x33099703UL);
    checkq(-a % 7, 0xFFFFFFFFUL, 0xFFFFFFFBUL);
    checkq(u / 10, 0x19999999UL, 0x99999999UL);
    checkq(u % 10, 0, 5);
    checkq(u / 0x100000001ULL, 0, 0xFFFFFFFFUL);
    checkq(a / 0x100000000LL, 0, 0x12345678UL);
    checkq(ga % 0x100000000LL, 0, 0x9ABCDEF0UL);
    checkq(a & 0xFF00FF00FF00FF00LL, 0x12005600UL, 0x9A00DE00UL);
    checkq(a | 0xFFLL, 0x12345678UL, 0x9ABCDEFFUL);
    checkq(a ^ -1LL, 0xEDCBA987UL, 0x6543210FUL);
    checkq(~a, 0xEDCBA987UL, 0x6543210FUL);
    checkq(-a, 0xEDCBA987UL, 0x65432110UL);
    checkq(-b, 0, 5);
    checkq(+b, 0xFFFFFFFFUL, 0xFFFFFFFBUL);
}

void shifts(void)
{
    checkq(shl(1, 0), 0, 1);
    checkq(shl(1, 31), 0, 0x80000000UL);
    checkq(shl(1, 32), 1, 0);
    checkq(shl(1, 63), 0x80000000UL, 0);
    checkq(shl(ga, 4), 0x23456789UL, 0xABCDEF00UL);
    checkq(shl(ga, 36), 0xABCDEF00UL, 0);
    checkq(sshr(ga, 4), 0x01234567UL, 0x89ABCDEFUL);
    checkq(sshr(-1, 63), 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    checkq(sshr(gb, 1), 0xFFFFFFFFUL, 0xFFFFFFFDUL);
    checkq(sshr(0x8000000000000000LL, 32), 0xFFFFFFFFUL, 0x80000000UL);
    checkq(sshr(0x8000000000000000LL, 33), 0xFFFFFFFFUL, 0xC0000000UL);
    checkq((long long)ushr(gu, 63), 0, 1);
    checkq((long long)ushr(gu, 32), 0, 0xFFFFFFFFUL);
    checkq((long long)ushr(gu, 0), 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    checkq(ga << gi + 4, 0x2468ACF1UL, 0x3579BDE0UL);
}

void compares(void)
{
    long long a;
    long long b;
    unsigned long long u;
    unsigned long long v;

    a = gb;                     /* -5 */
    b = 3;
    u = (unsigned long long)gb;
    v = 3;
    check(a < b, 1);
    check(a <= b, 1);
    check(a > b, 0);
    check(a >= b, 0);
    check(a == b, 0);
    check(a != b, 1);
    check(u < v, 0);
    check(u <= v, 0);
    check(u > v, 1);
    check(u >= v, 1);
    check(u == (unsigned long long)a, 1);
    check(a == a, 1);
    /* pairs that differ only in the high word, or only in the low */
    check(0x100000000LL > 0xFFFFFFFFLL, 1);
    check(ga > ga + 1, 0);
    check(ga - 0x100000000LL < ga, 1);
    /* in conditions, && || ! and ?: */
    check(gzero ? 1 : 2, 2);
    check(ga ? 1 : 2, 1);
    check(!gzero, 1);
    check(!ga, 0);
    check(ga && gzero, 0);
    check(gzero || ga, 1);
    if (gzero)
        check(0, 1);
    if (!(ga > 0))
        check(0, 2);
    while (a < 0)
        a++;
    checkq(a, 0, 0);
    check(0x100000000LL ? 5 : 6, 5);     /* only the high word non-zero */
}

void conversions(void)
{
    long long q;
    unsigned long long u;
    char c;
    int i;
    long l;
    unsigned long ul;
    double d;
    float f;

    q = gi;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFFDUL);
    q = gui;
    checkq(q, 0, 0xFFFFFFUL);
    q = gl;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFFEUL);
    q = gul;
    checkq(q, 0, 0x80000000UL);
    u = gi;
    checkq((long long)u, 0xFFFFFFFFUL, 0xFFFFFFFDUL);
    q = (char)-7;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFF9UL);
    q = (unsigned char)200;
    checkq(q, 0, 200);
    q = (short)-300;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFED4UL);
    c = (char)ga;
    check(c, -16);
    i = (int)(ga >> 8);
    check(i & 0xFFFF, 0xBCDE);
    l = (long)ga;
    check(l, (long)0x9ABCDEF0UL);
    ul = (unsigned long)(ga >> 32);
    check((long)ul, 0x12345678L);
    d = ga;
    check(d == 1311768467463790320.0, 1);    /* 0x123456789ABCDEF0 rounds to ...DF00 */
    d = gu;
    check(d == 18446744073709551616.0, 1);
    d = (double)gb;
    check(d == -5.0, 1);
    f = ga;
    check(f == 1311768467463790320.0f, 1);
    f = (float)gu;
    check(f == 18446744073709551616.0f, 1);
    q = (long long)gd;
    checkq(q, 0xFFFFFFFDUL, 0x2023E3CBUL);   /* -12345678901, truncated */
    u = (unsigned long long)1e19;
    checkq((long long)u, 0x8AC72304UL, 0x89E80000UL);
    q = (long long)gf;
    checkq(q, 0x100UL, 0);
    q = (long long)-gf;
    checkq(q, 0xFFFFFF00UL, 0);
    u = (unsigned long long)gf;
    checkq((long long)u, 0x100UL, 0);
    check(sizeof q, 8);
    check(sizeof(unsigned long long), 8);
    check(sizeof(0x100000000), 8);
    check(sizeof(0xFFFFFFFF), 4);
    check(sizeof(4294967295), 8);    /* a decimal: long long in C99 */
    check(sizeof(1LL), 8);
    check(sizeof(ga + 1), 8);
    check(sizeof(gi + 1LL), 8);
    check(-1LL < 0ULL, 0);           /* the usual conversions: unsigned long long */
    check(-1LL < 0UL, 1);            /* long long holds every unsigned long */
}

void lvalues(void)
{
    long long q;
    long long *p;
    struct rec r;
    long long arr[3];
    int k;

    q = 10;
    q += ga;
    checkq(q, 0x12345678UL, 0x9ABCDEFAUL);
    q -= ga;
    checkq(q, 0, 10);
    q *= -3;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFE2UL);
    q /= 4;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFF9UL);
    q %= 4;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFFDUL);
    q <<= 40;
    checkq(q, 0xFFFFFD00UL, 0);
    q >>= 36;
    checkq(q, 0xFFFFFFFFUL, 0xFFFFFFD0UL);
    q &= 0xFF;
    checkq(q, 0, 0xD0);
    q |= 0x100000000LL;
    checkq(q, 1, 0xD0);
    q ^= 0x1000000D0LL;
    checkq(q, 0, 0);
    checkq(q++, 0, 0);
    checkq(q, 0, 1);
    checkq(++q, 0, 2);
    checkq(q--, 0, 2);
    checkq(--q, 0, 0);
    checkq(--q, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    checkq(++q, 0, 0);
    q = 0xFFFFFFFFLL;
    checkq(++q, 1, 0);
    /* a narrower lvalue op= a long long */
    k = 5;
    k += 0x100000003LL;
    check(k, 8);
    k *= gb;
    check(k, -40);
    /* through pointers, struct members, array elements */
    p = &q;
    *p = ga;
    checkq(q, 0x12345678UL, 0x9ABCDEF0UL);
    *p += 1;
    (*p)++;
    checkq(*p, 0x12345678UL, 0x9ABCDEF2UL);
    r = grec;
    checkq(r.q, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    check(r.c, 1);
    check(r.i, 7);
    r.q += 2;
    r.u[1] >>= 63;
    r.u[0]--;
    checkq(r.q, 0, 1);
    checkq((long long)r.u[1], 0, 1);
    checkq((long long)r.u[0], 0, 1);
    r = pass_rec(grec);
    checkq(r.q, 0xFFFFFFFFUL, 0xFFFFFFFDUL);
    checkq((long long)r.u[1], 0x80000000UL, 0);
    check(r.i, 7);
    arr[0] = garr[2];
    arr[1] = arr[0] + garr[0];
    arr[2] = arr[1] - 1;
    checkq(arr[1], 0x80000000UL, 0);
    checkq(arr[2], 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    for (k = 0; k < 3; k++)
        arr[k] = k * 0x100000000LL;
    checkq(arr[2], 2, 0);
    checkq(garr[1], 0xFFFFFFFFUL, 0xFFFFFFFEUL);
    /* a local initialised from an expression */
    {
        long long w = ga - 0xF0;
        unsigned long long z = gu;
        checkq(w, 0x12345678UL, 0x9ABCDE00UL);
        checkq((long long)(z >> 60), 0, 15);
    }
}

void calls(void)
{
    checkq(add(ga, gb), 0x12345678UL, 0x9ABCDEEBUL);
    checkq(add(add(1, 2), add(3, 4)), 0, 10);
    checkq(mix(-1, 100, -2.5, 30ULL, -1000L), 0xFFFFFFFFUL, 0xFFFFFC5BUL);   /* 100-30-1-2-1000 = -933 */
    /* a long long result used while another is pending */
    checkq(add(1, 2) * add(3, 4) + add(5, 6) * add(7, 8), 0, 186);
}

/* a switch on a long long: cases that differ only in the high word,
 * fall-through, a default in the middle, a nested switch of each kind */
int qswitch(long long v)
{
    int r;

    r = 0;
    switch (v) {
    case 0:
        r = 1;
        break;
    case 0x100000000LL:
        r = 2;
        break;
    case -1:
        r = 3;
        /* fall through */
    case 0xFFFFFFFFLL:
        r = r + 10;
        break;
    default:
        r = 99;
        break;
    case 'a':
        switch ((int)v) {
        case 'a':
            r = 4;
            break;
        }
        break;
    case 0x7FFFFFFFFFFFFFFFLL:
        switch (v - 1) {
        case 0x7FFFFFFFFFFFFFFELL:
            r = 5;
            break;
        default:
            r = 6;
        }
        break;
    }
    return r;
}

unsigned long long uq(unsigned long long v)
{
    switch (v) {
    case 18446744073709551615ULL:
        return 1;
    case 9223372036854775808ULL:
        return 2;
    }
    return 0;
}

void folding(void)
{
    long long rt;
    unsigned long long ru;

    /* the same expressions at run time (from globals) and folded */
    rt = ga * gb + (ga >> 7) - (ga / 3) % 1000;
    checkq(rt - (0x123456789ABCDEF0LL * -5 + (0x123456789ABCDEF0LL >> 7) - (0x123456789ABCDEF0LL / 3) % 1000), 0, 0);
    ru = (unsigned long long)gb / 3 + (gu >> 1);
    checkq((long long)(ru - ((unsigned long long)-5LL / 3 + (0xFFFFFFFFFFFFFFFFULL >> 1))), 0, 0);
    checkq((long long)gfold, 0x55555555UL, 0x0000587DUL);
    checkq(9223372036854775807LL, 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    checkq(-9223372036854775807LL - 1, 0x80000000UL, 0);
    checkq(0x8000000000000000LL >> 63, 0, 1);   /* unsigned long long */
    checkq(-0x7FFFFFFFFFFFFFFFLL - 1 >> 63, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    checkq(0777777777777777777777LL, 0x7FFFFFFFUL, 0xFFFFFFFFUL);
    /* long long constants converted to narrower types, folded */
    check(gnarrow[0], -2147483647L - 1);
    check(gnarrow[1], (long)0x80000000UL);
    check(gnarrow[2], 0x23456789L);
    check(gnarrowi & 0xFFFF, 0x8080);
    check((long)-2147483648 < 0, 1);
    check((long)0x1FFFFFFFFLL, -1L);
    check((unsigned char)0x1234567890LL, 0x90);
    check((short)0xFFFF8000LL, -32768);
}

void switches(void)
{
    check(qswitch(0), 1);
    check(qswitch(0x100000000LL), 2);
    check(qswitch(-1), 13);
    check(qswitch(0xFFFFFFFFLL), 10);
    check(qswitch(0x1FFFFFFFFLL), 99);
    check(qswitch(0xFFFFFFFF00000000LL), 99);
    check(qswitch('a'), 4);
    check(qswitch(0x100000061LL), 99);
    check(qswitch(0x7FFFFFFFFFFFFFFFLL), 5);
    check(qswitch(ga), 99);
    check((int)uq(gu), 1);
    check((int)uq(gu / 2 + 1), 2);
    check((int)uq(gu / 2), 0);
}

int main(void)
{
    arith();
    shifts();
    compares();
    conversions();
    lvalues();
    calls();
    folding();
    switches();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
