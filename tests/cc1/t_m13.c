/* t_m13.c - the execution test for M13: float and double (IEEE 754
 * binary32 and binary64, c89_spec.md 6.5), checked bit for bit.
 *
 * Compiled by cc1 + cc2 + ld and run on the emulator (no cpp). The PC's
 * compiler, whose float and double are the same formats, builds and runs
 * it too (test_cc1.py), so every expected bit pattern here is one the PC
 * computes as well. Exits 0 if every check passes, else the number of the
 * first failing check (see main).
 */

void agon_emu_exit(int status);

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

void checkl(unsigned long got, unsigned long want)
{
    count++;
    if ((got & 0xFFFFFFFFUL) != (want & 0xFFFFFFFFUL)) {
        fails++;
        if (first == 0)
            first = count;
    }
}

/* a float's and a double's bits, read a byte at a time (little-endian) */
unsigned long word(unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

void checkf(float f, unsigned long want)
{
    checkl(word((unsigned char *)&f), want);
}

void checkd(double d, unsigned long hi, unsigned long lo)
{
    unsigned char *p;

    p = (unsigned char *)&d;
    count++;
    if (word(p + 4) != hi || word(p) != lo) {
        fails++;
        if (first == 0)
            first = count;
    }
}

int is_nan(double d)
{
    return d != d;
}

/* ---- initialised objects (constants folded by cc1) ---- */

double gd[4] = { 1.5, -0.0, 1e-310, 1.0 / 3.0 };
float gf[3] = { 0.1f, 3.4028235e38f, 1e-45f };
static double half = 0.5;
long double gld = 2.0L;
int gi = (int)-2.75;
float gfi = 7;                          /* an int converted */
struct pt {
    double x;
    float y;
    char c;
} gp = { 2.5, -1.25f, 'q' };

/* ---- functions ---- */

double dadd(double a, double b)
{
    return a + b;
}

float fmul(float a, float b)
{
    return a * b;
}

double kr_sum(x, y)
    float x;
    double y;
{
    return x + y;
}

double poly(double x)
{
    return ((2.0 * x - 3.0) * x + 1.0) * x - 7.0;
}

double hyp2(double a, double b)
{
    return a * a + b * b;
}

double fact(int n)
{
    return n <= 1 ? 1.0 : n * fact(n - 1);
}

struct pt mkpt(double x, float y)
{
    struct pt p;

    p.x = x;
    p.y = y;
    p.c = 'k';
    return p;
}

double ptsum(struct pt p)
{
    return p.x + p.y;
}

float fneg(float f)
{
    return -f;
}

int sign_of(double d)
{
    if (d < 0)
        return -1;
    if (d > 0)
        return 1;
    return 0;
}

void mandel(double *zr_out, double *zi_out)
{
    double zr;
    double zi;
    double t;
    double cr;
    double ci;
    int i;

    zr = zi = 0.0;
    cr = -0.75;
    ci = 0.1;
    for (i = 0; i < 20; i++) {
        t = zr * zr - zi * zi + cr;
        zi = 2.0 * zr * zi + ci;
        zr = t;
    }
    *zr_out = zr;
    *zi_out = zi;
}

void mandelf(float *zr_out, float *zi_out)
{
    float zr;
    float zi;
    float t;
    float cr;
    float ci;
    int i;

    zr = zi = 0.0f;
    cr = -0.75f;
    ci = 0.1f;
    for (i = 0; i < 20; i++) {
        t = zr * zr - zi * zi + cr;
        zi = 2.0f * zr * zi + ci;
        zr = t;
    }
    *zr_out = zr;
    *zi_out = zi;
}

/* sizes, initialised objects and arithmetic */
void t_arith(void)
{
    double a;
    double b;
    double d;
    float f;
    float g;

    check(sizeof(float), 4);
    check(sizeof(double), 8);
    check(sizeof(long double), 8);
    check(sizeof 1.0f, 4);
    check(sizeof 1.0, 8);

    /* initialised objects */
    checkd(gd[0], 0x3FF80000UL, 0);
    checkd(gd[1], 0x80000000UL, 0);
    checkd(gd[2], 0x00001268UL, 0x8B70E62BUL);
    checkd(gd[3], 0x3FD55555UL, 0x55555555UL);
    checkf(gf[0], 0x3DCCCCCDUL);
    checkf(gf[1], 0x7F7FFFFFUL);
    checkf(gf[2], 0x00000001UL);
    checkd(half, 0x3FE00000UL, 0);
    checkd(gld, 0x40000000UL, 0);
    check(gi, -2);
    checkf(gfi, 0x40E00000UL);
    checkd(gp.x, 0x40040000UL, 0);
    checkf(gp.y, 0xBFA00000UL);
    check(gp.c, 'q');

    /* arithmetic at run time: the same bits as folded */
    a = 1.0;
    b = 3.0;
    checkd(a / b, 0x3FD55555UL, 0x55555555UL);
    a = 0.1;
    b = 0.2;
    checkd(a + b, 0x3FD33333UL, 0x33333334UL);
    checkd(0.1 + 0.2, 0x3FD33333UL, 0x33333334UL);
    f = 0.1f;
    g = 0.2f;
    checkf(f + g, 0x3E99999AUL);
    a = 1.1;
    checkd(a * a, 0x3FF35C28UL, 0xF5C28F5DUL);
    f = 1.0f;
    g = 3.0f;
    checkf(f / g, 0x3EAAAAABUL);
    checkd(b - a, 0xBFECCCCCUL, 0xCCCCCCCEUL);
    checkf(g - f, 0x40000000UL);
    a = 1e-310;
    b = 1e-10;
    checkd(a * b, 0, 0x000007E8UL);         /* subnormal */
    a = 1e308;
    checkd(a * 10, 0x7FF00000UL, 0);        /* overflow: infinity */
    checkd(-a * 10, 0xFFF00000UL, 0);
    a = 0.0;
    d = a / a;
    check(is_nan(d), 1);                    /* 0/0 */
    checkl(word((unsigned char *)&d + 4) & 0x7FFFFFFFUL, 0x7FF80000UL);
    b = 1.0;
    checkd(b / a, 0x7FF00000UL, 0);         /* 1/0 */
    checkd(-a, 0x80000000UL, 0);            /* -0.0 */
    checkd(a - a, 0, 0);
    checkd(+b, 0x3FF00000UL, 0);
    f = 3.0f;
    checkf(-f, 0xC0400000UL);
    checkf(fneg(f), 0xC0400000UL);
    g = 0.0f;
    checkf(-g, 0x80000000UL);
}

/* comparisons and truth values */
void t_compare(void)
{
    double a;
    double b;
    double d;
    double z;
    float f;
    float g;
    int i;

    z = 0.0;
    d = z / z;                          /* a NaN */
    a = 1.0;
    b = 2.0;
    check(a < b, 1);
    check(a > b, 0);
    check(a <= a, 1);
    check(b >= a, 1);
    check(a == b, 0);
    check(a != b, 1);
    check(-0.0 == 0.0, 1);
    z = 0.0;
    check(-z == z, 1);
    check(d == d, 0);                       /* NaN */
    check(d != d, 1);
    check(d < 1.0, 0);
    check(d > 1.0, 0);
    check(d <= d, 0);
    check(d >= 1.0, 0);
    f = 1.5f;
    g = 1.25f;
    check(f > g, 1);
    check(f < g, 0);
    check(f == 1.5f, 1);
    check(f != 1.5, 0);                     /* 1.5f converts exactly */
    check(sign_of(-3.5), -1);
    check(sign_of(1e-300), 1);
    check(sign_of(-0.0), 0);

    /* truth values: if, !, &&, ||, ?: */
    check(!z, 1);
    check(!-z, 1);
    check(!d, 0);                           /* a NaN is not 0 */
    check(!a, 0);
    check(a && f, 1);
    check(z && f, 0);
    check(z || g, 1);
    check(z ? 1 : 2, 2);
    check(a ? 1 : 2, 1);
    i = 0;
    if (a)
        i = 1;
    check(i, 1);
    i = 0;
    for (z = 0.0; z < 1.0; z += 0.125)
        i++;
    check(i, 8);
    i = 0;
    for (f = 2.0f; f; f = f - 1)
        i++;
    check(i, 2);
    a = 2.0;
    b = 3.0;
    checkd(i ? a * b : a + b, 0x40180000UL, 0);
    checkd(!i ? a * b : a + b, 0x40140000UL, 0);
}

/* conversions */
void t_convert(void)
{
    double a;
    float f;
    int i;
    unsigned u;
    unsigned char uc;
    long l;
    unsigned long ul;

    check((int)2.9, 2);
    check((int)-2.9, -2);
    a = 2.9;
    check((int)a, 2);
    a = -2.9;
    check((int)a, -2);
    a = 3e9;
    checkl((unsigned long)a, 3000000000UL);
    a = -2147483648.0;
    checkl((unsigned long)(long)a, 0x80000000UL);
    a = 65.7;
    check((char)a, 'A');
    a = 200.5;
    check((unsigned char)a, 200);
    a = -1234.9;
    check((short)a, -1234);
    a = 8388607.5;
    check((int)(unsigned)a, 8388607);
    f = 1234.75f;
    check((int)f, 1234);
    checkl((unsigned long)f, 1234);
    f = -9.5f;
    checkl((unsigned long)(long)f, 0xFFFFFFF7UL);
    l = 16777217L;
    checkf((float)l, 0x4B800000UL);
    ul = 0xFFFFFFFFUL;
    checkf((float)ul, 0x4F800000UL);
    checkd((double)ul, 0x41EFFFFFUL, 0xFFE00000UL);
    i = -5;
    a = i;
    checkd(a, 0xC0140000UL, 0);
    u = 0xFFFFFFU;
    a = u;
    checkd(a, 0x416FFFFFUL, 0xE0000000UL);
    uc = 250;
    f = uc;
    checkf(f, 0x437A0000UL);
    f = 0.1f;
    a = f;
    checkd(a, 0x3FB99999UL, 0xA0000000UL);
    a = 1.0 / 3.0;
    f = a;
    checkf(f, 0x3EAAAAABUL);
    a = 1e300;
    f = a;
    checkf(f, 0x7F800000UL);                /* float overflow: infinity */
    f = 2.5f;
    i = 3;
    checkd(f + i, 0x40160000UL, 0);         /* float + int: float */
    checkf(f + i, 0x40B00000UL);
    checkd(f + 0.5, 0x40080000UL, 0);       /* float + double: double */
}

/* assignment operators, increment and decrement */
void t_assign(void)
{
    double a;
    double b;
    double d;
    double *p;
    double loc[3];
    float f;
    float g;
    int i;
    unsigned char uc;
    long l;

    d = 10.0;
    d += 1.5;
    d -= 0.25;
    d *= 2;
    d /= 3;
    checkd(d, 0x401E0000UL, 0);
    f = 1.0f;
    f += 0.5f;
    f *= 4;
    f -= 1;
    f /= 2.0;
    checkf(f, 0x40200000UL);
    i = 5;
    i += 2.7;
    check(i, 7);
    i *= 1.5;
    check(i, 10);
    l = 100;
    l /= 8.0;
    checkl(l, 12);
    uc = 10;
    uc -= 0.5;
    check(uc, 9);
    p = gd;
    p[3] = 2.0;
    p[3] += 1.0;
    checkd(gd[3], 0x40080000UL, 0);
    *p++ *= 2;
    checkd(gd[0], 0x40080000UL, 0);
    check(p == gd + 1, 1);
    gp.x *= 2;
    gp.y -= 1;
    checkd(gp.x, 0x40140000UL, 0);
    checkf(gp.y, 0xC0100000UL);
    a = b = 4.25;
    checkd(a, 0x40110000UL, 0);
    checkd(b, 0x40110000UL, 0);
    f = g = 0.75f;
    checkf(f, 0x3F400000UL);
    loc[0] = 1.0;
    loc[1] = loc[0] + 1;
    loc[2] = loc[1] * loc[1];
    checkd(loc[2], 0x40100000UL, 0);

    /* increment and decrement */
    d = 1.5;
    checkd(d++, 0x3FF80000UL, 0);
    checkd(d, 0x40040000UL, 0);
    checkd(++d, 0x400C0000UL, 0);
    checkd(d--, 0x400C0000UL, 0);
    checkd(--d, 0x3FF80000UL, 0);
    f = 0.5f;
    checkf(f++, 0x3F000000UL);
    checkf(f, 0x3FC00000UL);
    checkf(--f, 0x3F000000UL);
    checkf(f--, 0x3F000000UL);
    checkf(f, 0xBF000000UL);
    gd[1] = 1e16;
    gd[1]++;                                /* 1e16 + 1 rounds back to 1e16 */
    checkd(gd[1], 0x4341C379UL, 0x37E08000UL);
}

/* calls, temporaries and Mandelbrot iterations */
void t_calls(void)
{
    double a;
    double b;
    double (*fp)(double, double);
    float zf;
    float wf;
    struct pt q;

    checkd(dadd(1.5, 2.25), 0x400E0000UL, 0);
    checkd(dadd(dadd(1.0, 2.0), dadd(3.0, 4.0)), 0x40240000UL, 0);
    checkf(fmul(1.5f, 2.0f), 0x40400000UL);
    checkf(fmul(3, 0.5), 0x3FC00000UL);     /* converted to float by the prototype */
    checkd(kr_sum(0.1f, 0.2), 0x3FD33333UL, 0x34CCCCCDUL);
    checkd(poly(1.5), 0xC0160000UL, 0);
    checkd(hyp2(3.0, 4.0), 0x40390000UL, 0);
    checkd(fact(20), 0x43C0E1B3UL, 0xBE415A00UL);
    fp = dadd;
    checkd(fp(0.5, 0.25), 0x3FE80000UL, 0);
    checkd((*fp)(dadd(1.0, 1.0), poly(0.0)), 0xC0140000UL, 0);
    q = mkpt(1.25, 2.5f);
    checkd(q.x, 0x3FF40000UL, 0);
    checkf(q.y, 0x40200000UL);
    check(q.c, 'k');
    checkd(ptsum(q), 0x400E0000UL, 0);
    check(dadd(1.0, 2.0) > 2.5, 1);
    if (dadd(1.0, 2.0) < poly(1.5))
        check(0, 1);

    /* several temporaries in one statement */
    a = 1.5;
    b = 2.5;
    checkd((a * b + a * a) * (b - a) / (a + b), 0x3FF80000UL, 0);

    /* Mandelbrot iterations, in double and float */
    mandel(&a, &b);
    checkd(a, 0xBFDDB225UL, 0x9C94ADD3UL);
    checkd(b, 0xBFCC730CUL, 0x222F9BFFUL);
    mandelf(&zf, &wf);
    checkf(zf, 0xBEED9127UL);
    checkf(wf, 0xBE639868UL);
}

int main(void)
{
    t_arith();
    t_compare();
    t_convert();
    t_assign();
    t_calls();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
