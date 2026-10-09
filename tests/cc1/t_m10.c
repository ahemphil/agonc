/* t_m10.c - cc1's execution test for M10: block scope (shadowing, sibling
 * blocks, block-scope static, extern and typedef), tentative definitions,
 * K&R definitions and implicit int, unions, bit-fields and function
 * pointers, checked at run time.
 *
 * Compiled by cc1 -ansi (strict mode, for implicit int) + cc2 + ld and run
 * on the emulator. Exits 0 if every check passes, else the number of the
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

/* ---- block scope --------------------------------------------------------------------------- */

static int v = 3;
int gx = 100;

int shadow(int k)
{
    int v = 4;
    int r;

    r = v;
    {
        extern int v;                   /* the file-scope v, not the local (GCC's scope-1.c) */
        r = r * 10 + v;
    }
    {
        int v = 7;

        r = r * 10 + v;
        {
            int v = 8;

            r = r * 10 + v;
        }
        r = r * 10 + v;
    }
    return r * 10 + v + k;
}

/* each call counts in its own static; the two blocks' statics are distinct */
int counter(int which)
{
    if (which) {
        static int n = 10;

        return ++n;
    } else {
        static int n = 20;

        return ++n;
    }
}

/* siblings share frame space; each block's locals are its own while it runs */
int siblings(int k)
{
    int t;

    t = 0;
    {
        int a[4];
        int i;

        for (i = 0; i < 4; i++)
            a[i] = i + k;
        t += a[3];
    }
    {
        long b;
        char c[3];

        b = 100000L * k;
        c[2] = 5;
        t += (int)(b / 100000L) + c[2];
    }
    return t;
}

int recurse(int n)
{
    int here = n;

    if (n == 0)
        return 0;
    {
        int inner = here * 2;

        return inner + recurse(n - 1);
    }
}

typedef int tint;

int typedefs(void)
{
    tint a = 5;
    {
        typedef char tint;              /* hides the file-scope typedef */
        tint c = 300;                   /* a char: wraps */

        a = a + c;
    }
    {
        int tint = 2;                   /* a variable hides the typedef */

        a = a * tint;
    }
    return a + sizeof(tint);
}

int tags(void)
{
    struct s { int a; };
    struct s x;
    int r;

    x.a = 1;
    r = x.a;
    {
        struct s { char c[10]; };       /* a new struct s, hiding the outer one */
        struct s y;

        r = r * 100 + sizeof(y);
    }
    {
        enum { ONE = 1, TWO };

        r = r * 10 + TWO;
    }
    return r * 10 + sizeof(struct s);
}

int loop_blocks(void)
{
    int i;
    int t;

    t = 0;
    for (i = 0; i < 3; i++) {
        int x = i * 10;                 /* initialised on every pass */
        static int calls;

        calls++;
        t += x + calls;
    }
    return t;
}

int goto_out(int k)
{
    int r = 0;

    {
        int deep = k;

        if (deep > 2)
            goto done;
        r = deep;
    }
    r = r + 100;
done:
    return r;
}

int extern_fn(void)
{
    int shadow(int k);                  /* a block-scope function declaration */

    return shadow(0) % 10;
}

void test_scope(void)
{
    check(shadow(1), 437875);           /* 4, 43, 437, 4378, 43787, then *10 + 4 + 1 */
    check(counter(1), 11);
    check(counter(1), 12);
    check(counter(0), 21);
    check(counter(1), 13);
    check(siblings(2), 5 + 2 + 5);
    check(recurse(4), 2 * (4 + 3 + 2 + 1));
    check(typedefs(), (5 + 44) * 2 + 3);
    check(tags(), ((1 * 100 + 10) * 10 + 2) * 10 + 3);
    check(loop_blocks(), 0 + 1 + 10 + 2 + 20 + 3);
    check(goto_out(1), 101);
    check(goto_out(5), 0);
    check(extern_fn(), 4);
    check(gx, 100);
}

/* ---- tentative definitions, K&R definitions, implicit int ------------------------------------ */

int tx;
int tx = 5;                             /* the definition; the others are only tentative */
int tx;
static int ts;
static int ts;
struct later tl;                        /* completed below, before the unit's end */
struct later {
    int a;
    long b;
};
static sx;                              /* implicit int */

int kr(a, b)
char a;                                 /* receives an int, and uses its low byte */
long b;
{
    return a + (int)(b / 1000);
}

kr2(x, y)                               /* implicit int result; y implicitly int */
int x;
{
    return x * y;
}

int kr3(p, n)
char *p;
{
    return p[n];
}

void test_decls(void)
{
    check(tx, 5);
    check(ts, 0);
    ts = 7;
    check(ts, 7);
    check(sizeof(tl), 7);
    tl.b = 123456L;
    check((int)(tl.b / 1000) + tl.a, 123);
    sx = 9;
    check(sx, 9);
    check(kr(300, 5000L), 44 + 5);      /* no prototype: 300 is passed as an int */
    check(kr('a', 0L), 'a');
    check(kr2(6, 7), 42);
    check(kr3("hello", 4), 'o');
}

/* ---- union ---------------------------------------------------------------------------------- */

union num {
    long l;
    char b[4];
    short s[2];
};

union num gnum = { 0x04030201L };       /* initialises the first member */

struct tagged {
    char kind;
    union num v;
};

union num unum_make(long l)
{
    union num n;

    n.l = l;
    return n;
}

int unum_sum(union num n)
{
    return n.b[0] + n.b[1] + n.b[2] + n.b[3];
}

void test_union(void)
{
    union num a;
    struct tagged t;
    union {
        int i;
        char c;
    } anon;

    check(sizeof(union num), 4);
    check(sizeof(struct tagged), 5);
    a.l = 0x11223344L;
    check(a.b[0], 0x44);                /* little-endian */
    check(a.s[1], 0x1122);
    a.b[3] = 0x55;
    check((int)(a.l >> 24), 0x55);
    check(gnum.b[2], 3);
    t.kind = 1;
    t.v = unum_make(0x01010101L);
    check(unum_sum(t.v), 4);
    check(unum_sum(unum_make(0x7F000000L)), 0x7F);
    anon.i = 0x123456;
    check(anon.c, 0x56);
    check(sizeof(anon), 3);
}

/* ---- bit-fields (abi.md 2: byte storage units) ------------------------------------------------ */

struct bits {
    unsigned a : 3;                     /* byte 0, bits 0-2 */
    int b : 5;                          /* byte 0, bits 3-7; plain int is signed */
    unsigned c : 1;                     /* byte 1: b filled byte 0 */
    unsigned wide : 12;                 /* bytes 2-3: a wide field starts a byte */
    char tail;                          /* byte 4 */
    unsigned : 0;
    signed char sc : 4;                 /* byte 5 (a char bit-field) */
    unsigned long big : 20;             /* bytes 6-8 */
};

struct bits gb = { 5, -2, 1, 3000, 'z', -1, 0x12345 };

union overlay {
    struct bits s;
    unsigned char raw[9];
};

void test_bits(void)
{
    struct bits x;
    struct bits *p;
    struct bits arr[3];
    struct bits lb = { 1, 2 };
    union overlay ov;
    union {
        unsigned f : 4;
        char c;
    } u;
    int i;

    check(sizeof(struct bits), 9);
    x.a = 7;
    x.b = -3;
    x.c = 1;
    x.wide = 4000;
    x.tail = 'T';
    x.sc = -8;
    x.big = 0xABCDEL;
    check(x.a, 7);
    check(x.b, -3);
    check(x.c, 1);
    check(x.wide, 4000);
    check(x.tail, 'T');
    check(x.sc, -8);
    check(x.big, 0xABCDE);
    check(x.a = 12, 4);                 /* the value is the field as stored */
    check(x.b, -3);                     /* its neighbour is untouched */
    x.b = 16;
    check(x.b, -16);                    /* 5 signed bits */
    x.wide += 100;
    check(x.wide, 4);                   /* 4000 + 100 wraps in 12 unsigned bits */
    x.a = 7;
    check(x.a++, 7);
    check(x.a, 0);                      /* 3 unsigned bits wrap */
    check(++x.c, 0);
    check(x.tail, 'T');
    p = &x;
    p->b = -1;
    check(p->b--, -1);
    check(p->b, -2);
    i = 0;
    arr[0].a = 0;
    arr[0].b = -5;
    arr[1].a = 0;
    arr[i++].a = 5;                     /* the lvalue is evaluated once */
    check(i, 1);
    check(arr[0].b, -5);                /* and the byte's other field is kept */
    check(arr[0].a * 10 + arr[1].a, 50);
    check(gb.a * 100 + gb.c * 10 + (gb.b + 2), 510);
    check(gb.wide, 3000);
    check(gb.sc, -1);
    check(gb.big, 0x12345);
    check(gb.tail, 'z');
    check(lb.a * 10 + lb.b, 12);
    check(lb.wide, 0);
    for (i = 0; i < 9; i++)
        ov.raw[i] = 0;
    ov.s.b = -1;
    check(ov.raw[0], 0xF8);             /* bits 3-7 of byte 0 */
    ov.s.wide = 0xFFF;
    check(ov.raw[2] + ov.raw[3] * 256, 0xFFF);
    check(ov.raw[4], 0);                /* tail, after it, untouched */
    u.c = 0x5A;
    check(u.f, 0xA);
}

/* ---- function pointers ------------------------------------------------------------------------ */

int add1(int x)
{
    return x + 1;
}

int dbl(int x)
{
    return x * 2;
}

long ladd(long a, long b)
{
    return a + b;
}

struct pt2 {
    int x;
    int y;
};

struct pt2 mk2(int x, int y)
{
    struct pt2 p;

    p.x = x;
    p.y = y;
    return p;
}

int (*gfp)(int) = add1;
int (*ops[3])(int) = { add1, dbl, 0 };
typedef int (*unop)(int);

struct opdesc {
    char name;
    unop fn;
};

struct opdesc table[2] = { { 'a', add1 }, { 'd', &dbl } };

int apply(int f(int), int x)            /* a function parameter is a pointer to it */
{
    return f(x);
}

int apply2(unop f, int x)
{
    return (*f)(x);
}

unop pick(int k)
{
    return k ? dbl : add1;
}

int (*pick2(int k))(int)
{
    return ops[k];
}

void test_fnptr(void)
{
    int (*fp)(int);
    long (*lf)(long, long);
    struct pt2 (*sf)(int, int);
    int i;
    int t;

    fp = add1;
    check(fp(1), 2);
    check((*fp)(2), 3);
    fp = &dbl;
    check(fp(5), 10);
    check(gfp(10), 11);
    check(ops[1](4), 8);
    check(ops[2] == 0, 1);
    check(apply(dbl, 7), 14);
    check(apply(add1, 7), 8);
    check(apply2(dbl, 3), 6);
    check(pick(0)(5), 6);
    check(pick(1)(5), 10);
    check(pick2(1)(9), 18);
    check(table[1].fn(21), 42);
    check(table[0].name, 'a');
    lf = ladd;
    check((int)(lf(0x10000L, 5L) >> 16), 1);
    sf = mk2;
    check(sf(3, 4).y, 4);
    check(fp == dbl, 1);
    check(fp != add1, 1);
    t = 0;
    for (i = 0; i < 2; i++)
        t = t * 10 + ops[i](i + 1);
    check(t, 2 * 10 + 4);
    check(sizeof(fp), 3);
    check((dbl)(4), 8);
    check((**fp)(6), 12);
}

/* A bit-field is read and stored as the bytes it occupies and no more: one
 * byte up to 8 bits, two up to 16, three up to 24. The byte after each
 * field here is assigned inside the field's own assignment, which the
 * field's store must not undo (it once wrote back a third byte it had read
 * before the right-hand side ran). */
struct units {
    unsigned w12 : 12;
    unsigned char after12;
    unsigned w16 : 16;
    unsigned char after16;
    unsigned w20 : 20;
    unsigned char after20;
    int s12 : 12;
    unsigned char after_s;
};

void test_bit_units(void)
{
    struct units u;

    check(sizeof(struct units), 2 + 1 + 2 + 1 + 3 + 1 + 2 + 1);
    u.after12 = 1;
    u.after16 = 2;
    u.after20 = 3;
    u.after_s = 4;
    u.w12 = (u.after12 = 7);
    check(u.after12, 7);
    check(u.w12, 7);
    u.w16 = (u.after16 = 0x55);
    check(u.after16, 0x55);
    check(u.w16, 0x55);
    u.w20 = (u.after20 = 9);
    check(u.after20, 9);
    check(u.w20, 9);
    u.s12 = (u.after_s = 6) - 8;
    check(u.after_s, 6);
    check(u.s12, -2);
    u.w16 = 0xFFFF;                     /* all 16 bits, and the next byte kept */
    check(u.w16, 0xFFFF);
    check(u.after16, 0x55);
    u.w12 += 0xFFF;                     /* 7 + 4095 wraps in 12 bits */
    check(u.w12, 6);
    check(u.after12, 7);
}

int main(void)
{
    test_scope();
    test_decls();
    test_union();
    test_bits();
    test_bit_units();
    test_fnptr();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
