/* t_c99.c - cc1's execution test for the default mode's C99 language
 * features: declarations in for and after statements, inline, restrict,
 * __func__, _Bool (its conversions, ++ and --, compound assignment,
 * bit-fields, parameters and results), flexible array members,
 * hexadecimal floating constants (bit for bit, rounding included),
 * designated initialisers (out of order, overriding, nested, going on
 * after a designator, bit-fields, unions, local ones with values that are
 * not constants) and compound literals (at file scope, in a function,
 * as lvalues, filled again each time, inside other initialisers).
 *
 * Compiled by cc1 + cc2 + ld in the default mode and run on the emulator
 * (no cpp, so no stdbool.h); also built by the PC (test_cc1.py) as C99.
 * Exits 0 if every check passes, else the number of the first failing
 * check (see main).
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

/* bytes, low first, as the target and the PC store them */
unsigned long word_at(unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

void checkd(double d, unsigned long hi, unsigned long lo)
{
    unsigned char *p;

    p = (unsigned char *)&d;
    check(word_at(p) == lo && word_at(p + 4) == hi, 1);
}

void checkf(float f, unsigned long bits)
{
    check(word_at((unsigned char *)&f) == bits, 1);
}

/* ---- declarations in for, and after statements ---- */

int for_decls(int n)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += i;
    int i = 100;                        /* the loop's i has gone out of scope */
    for (int i = 0, j = 10; i < j; i++, j--)
        total += j - i;
    total += i;
    for (long k = 70000L; k > 69998L; k--)
        total++;
    return total;
}

/* ---- inline ---- */

static inline int sq(int x)
{
    return x * x;
}

inline int cube(int x)
{
    return x * sq(x);
}

extern inline int twice(int x);         /* extern: an external definition below */

inline int twice(int x)
{
    return x + x;
}

/* ---- restrict ---- */

void copy3(int *restrict to, const int *restrict from)
{
    for (int i = 0; i < 3; i++)
        to[i] = from[i];
}

/* ---- __func__ ---- */

const char *my_name(void)
{
    return __func__;
}

int name_length(void)
{
    int n = 0;
    while (__func__[n] != 0)
        n++;
    return n + (int)sizeof __func__;
}

/* ---- _Bool ---- */

_Bool gb;
_Bool gtrue = 2;                        /* converted at compile time: 1 */
_Bool gfrac = 0.25;
long glong = 0x10000L;                  /* low 16 bits 0 */
long long gll = 0x10000000000LL;        /* low 32 bits 0 */
double ghalf = 0.5;
double gnegzero = -0.0;
int gint = 256;                         /* low byte 0 */
char *gnull;

struct flags {
    _Bool a : 1;
    unsigned b : 3;
    _Bool c : 1;
    _Bool whole;
};

_Bool to_bool(int x)
{
    return x;
}

int from_bool(_Bool b)
{
    return b + 10;
}

_Bool not_null(void *p)
{
    return p;
}

void bool_conversions(void)
{
    _Bool b;
    int x;

    check(sizeof(_Bool), 1);
    check(gtrue, 1);
    check(gfrac, 1);
    b = gint;
    check(b, 1);
    b = glong;
    check(b, 1);
    b = gll;
    check(b, 1);
    b = ghalf;
    check(b, 1);
    b = gnegzero;
    check(b, 0);
    b = &x;
    check(b, 1);
    b = gnull;
    check(b, 0);
    check((_Bool)gint, 1);
    check((_Bool)512, 1);
    check((_Bool)0.0, 0);
    check((int)(_Bool)-1, 1);
    check(to_bool(768), 1);
    check(to_bool(0), 0);
    check(from_bool(gint), 11);
    check(not_null(&x), 1);
    check(not_null(gnull), 0);
    check(gb, 0);
    check(-gtrue, -1);                  /* promoted to int */
    check(gtrue + gtrue, 2);
}

void bool_inc(void)
{
    _Bool b = 0;
    _Bool v;
    _Bool arr[2] = { 0, 1 };
    _Bool *p = arr;

    v = b++;
    check(v, 0);
    check(b, 1);
    v = b++;
    check(v, 1);
    check(b, 1);
    v = ++b;
    check(v, 1);
    check(b, 1);
    v = b--;
    check(v, 1);
    check(b, 0);
    v = b--;
    check(v, 0);
    check(b, 1);
    v = --b;
    check(v, 0);
    check(b, 0);
    (*p++)++;                           /* the address once */
    check(arr[0], 1);
    check(p - arr, 1);
    (*p++)--;
    check(arr[1], 0);
    check(p - arr, 2);
}

void bool_assign(void)
{
    _Bool b = 0;
    _Bool arr[3] = { 0, 0, 0 };
    int i = 0;

    b += 2;
    check(b, 1);
    b -= 1;
    check(b, 0);
    b += 0.5;
    check(b, 1);
    b *= 256;
    check(b, 1);
    b &= 2;
    check(b, 0);
    b |= 4;
    check(b, 1);
    b ^= 1;
    check(b, 0);
    b = 1;
    b <<= 8;
    check(b, 1);
    arr[i++] += 2;                      /* the address once */
    arr[i++] |= 6;
    check(arr[0], 1);
    check(arr[1], 1);
    check(arr[2], 0);
    check(i, 2);
}

void bool_fields(void)
{
    struct flags f;
    struct flags *pf = &f;

    f.a = 0;
    f.b = 5;
    f.c = 0;
    f.whole = 0;
    f.a = 2;
    check(f.a, 1);
    check(f.b, 5);
    f.c = 0.5;
    check(f.c, 1);
    f.c ^= 1;
    check(f.c, 0);
    check(f.c++, 0);
    check(f.c++, 1);
    check(f.c, 1);
    check(--f.c, 0);
    check(f.c--, 0);
    check(f.c, 1);
    check(f.b, 5);
    pf->a += 4;
    check(pf->a, 1);
    f.whole = 16;
    check(f.whole, 1);
    check(f.b, 5);
}

/* ---- flexible array members ---- */

struct fam {
    int n;
    char tag;
    int d[];
};

struct pt {
    int x;
    int y;
};

struct poly {
    int n;
    struct pt p[];
};

int store[12];

int fam_sum(struct fam *f)
{
    int s = 0;
    for (int i = 0; i < f->n; i++)
        s += f->d[i];
    return s;
}

void flexible(void)
{
    struct fam *f = (struct fam *)store;
    struct poly *q = (struct poly *)store;
    struct fam copy;

    check((char *)f->d - (char *)f, sizeof(struct fam));      /* sizeof stops before it */
    f->n = 4;
    f->tag = 'x';
    for (int i = 0; i < 4; i++)
        f->d[i] = i * i + 1;
    check(fam_sum(f), 1 + 2 + 5 + 10);
    copy = *f;                          /* the members before it only */
    check(copy.n, 4);
    check(copy.tag, 'x');
    q->n = 2;
    q->p[1].y = 77;
    q->p[0].x = 5;
    check(q->p[1].y + q->p[0].x, 82);
    check((char *)&q->p[1] - (char *)q, sizeof(struct poly) + sizeof(struct pt));
}

/* ---- hexadecimal floating constants ---- */

void hex_floats(void)
{
    checkd(0x1p0, 0x3FF00000UL, 0);
    checkd(0x1.8p3, 0x40280000UL, 0);           /* 12 */
    checkd(0X.8P1, 0x3FF00000UL, 0);
    checkd(0xAp-2, 0x40040000UL, 0);            /* 2.5 */
    checkd(0x0000.0001p16, 0x3FF00000UL, 0);    /* leading zeros */
    checkd(0x100000000000000000p0, 0x44300000UL, 0);    /* 2^68: digits past 16 */
    checkd(0x1.FFFFFFFFFFFFFp1023, 0x7FEFFFFFUL, 0xFFFFFFFFUL);    /* DBL_MAX */
    checkd(0x1p-1022, 0x00100000UL, 0);         /* the smallest normal */
    checkd(0x1p-1074, 0, 1);                    /* the smallest subnormal */
    checkd(0x1p-1075, 0, 0);                    /* half of it: a tie, to even */
    checkd(0x1.8p-1074, 0, 2);                  /* 1.5 of it: a tie, to even */
    checkd(0x1.00000000000008p0, 0x3FF00000UL, 0);      /* 1 + 2^-53: a tie, to even */
    checkd(0x1.000000000000081p0, 0x3FF00000UL, 1);     /* just above the tie */
    checkd(0x1.0000000000000800000001p0, 0x3FF00000UL, 1);      /* above it by a dropped digit */
    checkd(0x1.00000000000018p0, 0x3FF00000UL, 2);      /* a tie, odd below: up */
    checkd(0x1p+1L, 0x40000000UL, 0);           /* long double */
    checkd(-0x1p-2, 0xBFD00000UL, 0);
    checkf(0x1.8p1f, 0x40400000UL);             /* 3 */
    checkf(0x1.fffffep127f, 0x7F7FFFFFUL);      /* FLT_MAX */
    checkf(0x1p-149f, 1);                       /* the smallest subnormal */
    checkf(0x1.000001p0f, 0x3F800000UL);        /* a tie, to even */
    checkf(0x1.0000011p0f, 0x3F800001UL);       /* rounded once, from the constant */
    checkf(0x1.000003p0F, 0x3F800002UL);        /* a tie, odd below: up */
    check(0x1p4 == 16, 1);
    check(0xep+1 == 28, 1);                     /* e a digit, then the exponent */
}

/* ---- designated initialisers ---- */

struct pt3 {
    int x;
    int y;
    int z;
};

struct seg {
    struct pt3 a;
    struct pt3 b;
};

struct arr_b {
    int a[3];
    int b;
};

struct bits {
    unsigned a : 3;
    unsigned b : 5;
    unsigned c : 4;
};

union num {
    long l;
    char c[3];
};

struct named {
    char s[8];
    int n;
};

int d1[10] = { [5] = 1, [2] = 3, 9 };
int d2[4] = { 1, 2, 3, 4, [1] = 7 };
int d3[] = { [9] = 1, [3] = 4 };
struct pt3 dp = { .z = 3, .x = 1 };
struct seg ds = { .b.y = 5, .a = { .z = 2 } };
struct arr_b dw = { .a[1] = 2, 3, .b = 4 };
struct pt3 dps[3] = { [2].y = 7, [0] = { 1, 2, 3 } };
struct bits dbits = { .c = 9, .a = 5 };
union num du = { .l = 0x01020304L };
union num du2 = { .c = { 9, 8 } };
struct named dn = { .s = "hello", .s[1] = 'a', .n = 2 };
const char *dnames[] = { [2] = "two", [0] = "zero" };
double dd[3] = { [1] = 2.5 };
long dl[2] = { [1] = 70000L };
int gx = 6;

int designated(void)
{
    int x = gx;
    struct pt3 q = { .y = x, .x = x + 1 };
    struct pt3 r = { .x = x, .z = 4, .x = 5 };
    int la[5] = { [3] = x, [1] = 2 };

    check(d1[2], 3);
    check(d1[3], 9);
    check(d1[4], 0);
    check(d1[5], 1);
    check(d1[9], 0);
    check(d2[0] + d2[1] * 10 + d2[2] * 100 + d2[3] * 1000, 4371);
    check(sizeof d3 / sizeof d3[0], 10);
    check(d3[3] + d3[9] * 10, 14);
    check(dp.x * 100 + dp.y * 10 + dp.z, 103);
    check(ds.a.z * 10 + ds.b.y, 25);
    check(ds.a.x + ds.b.x + ds.b.z, 0);
    check(dw.a[0] * 100 + dw.a[1] * 10 + dw.a[2], 23);
    check(dw.b, 4);
    check(dps[0].z * 10 + dps[2].y, 37);
    check(dps[1].x + dps[2].x, 0);
    check(dbits.a * 100 + dbits.b * 10 + dbits.c, 509);
    check(du.l == 0x01020304L, 1);
    check(du2.c[0] * 10 + du2.c[1], 98);
    check(dn.s[0] == 'h' && dn.s[1] == 'a' && dn.s[4] == 'o' && dn.s[5] == 0, 1);
    check(dn.n, 2);
    check(sizeof dnames / sizeof dnames[0], 3);
    check(dnames[1] == 0 && dnames[0][0] == 'z' && dnames[2][1] == 'w', 1);
    check(dd[1] == 2.5 && dd[0] == 0, 1);
    check(dl[1] == 70000L && dl[0] == 0, 1);
    check(q.x * 100 + q.y * 10 + q.z, 760);
    check(r.x * 100 + r.y * 10 + r.z, 504);
    check(la[1] * 10 + la[3] + la[0] + la[4], 26);
    return 0;
}

/* ---- compound literals ---- */

struct holder {
    int *vals;
    int n;
};

int *cl_p = (int[]){ 1, 2, 3 };
struct holder cl_h = { (int[]){ 10, 20 }, 2 };
int **cl_pp = (int *[]){ (int[]){ 1 }, (int[]){ 2 } };
char **cl_s = (char *[]){ "ab", "cd" };

int sum3(int *v)
{
    return v[0] + v[1] + v[2];
}

int ptsum3(struct pt3 p)
{
    return p.x + p.y + p.z;
}

int compound(void)
{
    int v = 7;
    int i;
    int total;
    int *q;
    struct pt3 t = (struct pt3){ v, v * 2 };
    struct holder h2 = { (int[]){ v, 3 }, 2 };
    struct pt3 *pp;

    check(cl_p[2], 3);
    check(cl_h.vals[1] * 10 + cl_h.n, 202);
    check(*cl_pp[0] * 10 + *cl_pp[1], 12);
    check(cl_s[1][0], 'c');
    check(sum3((int[]){ 4, 5, 6 }), 15);
    check(ptsum3((struct pt3){ .x = 1, .y = 2 }), 3);
    check((struct pt3){ 1, 2, 3 }.y, 2);
    pp = &(struct pt3){ 4, 5, 6 };
    pp->z = 9;
    check(pp->x * 100 + pp->y * 10 + pp->z, 459);
    check(t.x * 10 + t.y + t.z, 84);
    total = 0;
    for (i = 0; i < 4; i++) {
        q = (int[]){ i, i + 1 };
        total += q[0] * 10 + q[1];
        q[0] = 99;                      /* filled again next time round */
    }
    check(total, 60 + 10);
    check((int){ 42 }, 42);
    check(sizeof (int[]){ 1, 2, 3 } / sizeof(int), 3);
    check((char *[]){ "ab", "cd" }[1][1], 'd');
    check(h2.vals[0] * 10 + h2.vals[1], 73);
    return 0;
}

/* ---- main ---- */

int main(void)
{
    int a[3] = { 4, 5, 6 };
    int b[3];

    check(for_decls(5), 10 + 30 + 100 + 2);
    check(sq(7), 49);
    check(cube(3), 27);
    check(twice(21), 42);
    copy3(b, a);
    check(b[0] + b[1] + b[2], 15);
    check(my_name()[0], 'm');
    check(my_name()[6], 'e');
    check(my_name()[7], 0);
    check(name_length(), 11 + 12);
    bool_conversions();
    bool_inc();
    bool_assign();
    bool_fields();
    flexible();
    hex_floats();
    designated();
    compound();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
