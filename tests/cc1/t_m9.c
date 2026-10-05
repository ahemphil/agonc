/* t_m9.c - cc1's execution test for M9: structs passed and returned by
 * value, struct values in expressions, and (step 3 onwards) the rest of
 * M9's language, checked at run time.
 *
 * Compiled by cc1 + cc2 + ld and run on the emulator. Exits 0 if every
 * check passes, else the number of the first failing check (see main).
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

/* ---- structs by value --------------------------------------------------------------- */

struct pt {
    int x;
    int y;
};

struct big {
    char tag;
    long n;
    int a[10];
    char s[7];
};

struct four {
    char c[4];
};

struct one {
    char c;
};

struct line {
    struct pt from;
    struct pt to;
};

struct pt gp;
struct pt gpts[4];

struct pt mkpt(int x, int y)
{
    struct pt p;

    p.x = x;
    p.y = y;
    return p;
}

int ptsum(struct pt p)
{
    return p.x + p.y;
}

/* changes its own copy of a */
struct pt ptadd(struct pt a, struct pt b)
{
    a.x += b.x;
    a.y += b.y;
    return a;
}

struct pt swapxy(struct pt p)
{
    struct pt r;

    r.x = p.y;
    r.y = p.x;
    return r;
}

struct pt retglobal(void)
{
    return gp;
}

struct pt retparam(struct pt p)
{
    return p;
}

struct pt retcond(int c, struct pt a, struct pt b)
{
    return c ? a : b;
}

struct pt nested(int k)
{
    return ptadd(mkpt(k, k), mkpt(1, 2));
}

/* (F(n), F(n + 1)) */
struct pt fib(int n)
{
    struct pt p;

    if (n == 0)
        return mkpt(0, 1);
    p = fib(n - 1);
    return mkpt(p.y, p.x + p.y);
}

struct big mkbig(int k)
{
    struct big b;
    int i;

    b.tag = 'A' + k;
    b.n = 100000L * k;
    for (i = 0; i < 10; i++)
        b.a[i] = i * k;
    for (i = 0; i < 6; i++)
        b.s[i] = 'a' + i + k;
    b.s[6] = 0;
    return b;
}

long bigsum(struct big b)
{
    long t;
    int i;

    t = b.n + b.tag;
    for (i = 0; i < 10; i++)
        t += b.a[i];
    for (i = 0; b.s[i]; i++)
        t += b.s[i];
    return t;
}

struct four rev4(struct four f)
{
    struct four r;
    int i;

    for (i = 0; i < 4; i++)
        r.c[i] = f.c[3 - i];
    return r;
}

struct one onep(struct one o, int k)
{
    o.c = o.c + k;
    return o;
}

/* every kind of argument, in an order that mixes slot counts */
long mixed(int a, struct pt p, long l, struct four f, char c, struct one o)
{
    return a * 1000000L + p.x * 10000L + p.y * 1000L + l + f.c[0] * 10 + f.c[3] + c + o.c;
}

struct line mkline(int a, int b, int c, int d)
{
    struct line l;

    l.from = mkpt(a, b);
    l.to = mkpt(c, d);
    return l;
}

int vsum(int n, ...)
{
    va_list ap;
    int t;
    struct pt p;

    __va_start(ap, n);
    t = 0;
    while (n-- > 0) {
        p = __va_arg(ap, struct pt);
        t += p.x * 10 + p.y;
    }
    __va_end(ap);
    return t;
}

void test_structs(void)
{
    struct pt p;
    struct pt q;
    struct pt r = mkpt(4, 5);
    struct big b;
    struct four f;
    struct one o;
    struct line l;
    int i;
    int s;

    check(ptsum(mkpt(3, 4)), 7);
    check(mkpt(5, 6).y, 6);
    check(r.x * 10 + r.y, 45);
    p = mkpt(7, 8);
    q = ptadd(p, p);
    check(p.x, 7);                      /* the callee changed only its copy */
    check(q.x, 14);
    check(q.y, 16);
    q = ptadd(mkpt(1, 2), mkpt(10, 20));
    check(q.x * 100 + q.y, 1122);
    q = swapxy(q);                      /* the same object in and out */
    check(q.x * 100 + q.y, 2211);
    check(ptsum(swapxy(mkpt(1, 9))), 10);
    check(swapxy(swapxy(mkpt(2, 3))).y, 3);
    gp = mkpt(-1, -2);
    check(retglobal().y, -2);
    check(retparam(mkpt(8, 9)).x, 8);
    check(retcond(1, p, q).x, 7);
    check(retcond(0, p, q).x, 22);
    q = nested(5);
    check(q.x * 100 + q.y, 607);
    check(fib(20).x, 6765);
    check((1 ? p : q).y, 8);
    check((0 ? p : q).y, 7);
    i = 0;
    q = i ? p : mkpt(1, 1);
    check(q.x, 1);
    i = 1;
    q = i ? p : mkpt(1, 1);
    check(q.x, 7);
    b = mkbig(2);
    check(b.tag, 'C');
    checkl(b.n, 200000L);
    check(b.a[9], 18);
    check(b.s[0], 'c');
    checkl(bigsum(b), 200000L + 'C' + 90 + ('c' + 'd' + 'e' + 'f' + 'g' + 'h'));
    checkl(bigsum(mkbig(1)), 100000L + 'B' + 45 + ('b' + 'c' + 'd' + 'e' + 'f' + 'g'));
    check(mkbig(3).s[5], 'i');
    f.c[0] = 1;
    f.c[1] = 2;
    f.c[2] = 3;
    f.c[3] = 4;
    f = rev4(f);
    check(f.c[0] * 1000 + f.c[1] * 100 + f.c[2] * 10 + f.c[3], 4321);
    o.c = 'a';
    o = onep(o, 2);
    check(o.c, 'c');
    check(onep(o, -2).c, 'a');
    checkl(mixed(3, mkpt(4, 5), 600L, f, 7, o), 3000000L + 40000L + 5000L + 600L + 40 + 1 + 7 + 'c');
    l = mkline(1, 2, 3, 4);
    check(l.from.x + l.to.y * 10, 41);
    check(mkline(5, 6, 7, 8).to.x, 7);
    l.to = swapxy(l.from);
    check(l.to.x * 10 + l.to.y, 21);
    for (i = 0; i < 4; i++)
        gpts[i] = mkpt(i, -i * 2);
    s = 0;
    for (i = 0; i < 4; i++)
        s += ptsum(gpts[i]);
    check(s, -6);
    gp = mkpt(0, 0);
    for (i = 0; i < 1000; i++)          /* the stack does not grow per call */
        gp = ptadd(gp, mkpt(1, 2));
    check(gp.x + gp.y, 3000);
    check(vsum(3, mkpt(1, 2), mkpt(3, 4), mkpt(5, 6)), 12 + 34 + 56);
    check(sizeof(mkpt(1, 2)), 6);
}

/* ---- compound division through a complex lvalue (M8's leftover) ------------------------------- */

void test_compound(void)
{
    int a[3];
    int *p;
    short sh[2];
    short *sp;
    char c[2];
    int i;

    a[0] = 1000;
    a[1] = 2000;
    a[2] = 3000;
    p = a;
    *p++ /= 2L;                         /* the address is taken once */
    check(a[0], 500);
    check(p - a, 1);
    *p++ %= 300L;
    check(a[1], 200);
    check(p - a, 2);
    i = 0;
    a[i++] /= 0x10000L;
    check(a[0], 0);
    check(i, 1);
    sh[0] = -30000;
    sh[1] = 7;
    sp = sh;
    *sp++ /= -3L;
    check(sh[0], 10000);
    c[0] = 100;
    c[1] = 0;
    i = 0;
    c[i++] %= 30L;
    check(c[0], 10);
    check(i, 1);
}

/* ---- multi-dimensional arrays and initialisers --------------------------------------------- */

struct lab {
    char name[8];
    int n;
    long big;
    struct pt at;
    char *msg;
};

int g2[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
int g2e[2][3] = { 1, 2, 3, 4 };                         /* braces elided; the rest zero */
char gnames[3][6] = { "ab", "cde", "fghij" };
struct pt gpt = { 3, 4 };
struct pt gpts3[] = { { 1, 2 }, { 3, 4 }, 5, 6 };       /* three elements */
struct line gline = { { 1, 2 }, { 3 } };
struct lab glabs[2] = { { "one", 1, 100000L, { 7, 8 }, "hi" }, { "two" } };
int gscal = { 42 };
long glong3[3] = { 1L, -1L };
short gsh[2][2] = { { 1, -1 }, { -2, 2 } };
int g3d[2][2][2] = { { { 1, 2 }, { 3, 4 } }, { { 5, 6 }, { 7, 8 } } };
char gstr2[] = { "pq" };
int gtrail[] = { 1, 2, 3, };
int *gptrs[2] = { &gscal, g2e[0] + 1 };
char gpart[2][4] = { 'a', 'b', 'c', 'd', 'e' };          /* elided, then zero */
unsigned char gbytes[4] = { 255, 256, -1 };

int sum2(int m[][3], int rows)
{
    int i;
    int j;
    int t;

    t = 0;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            t += m[i][j] * (i + 1);
    return t;
}

void test_arrays(void)
{
    int (*rowp)[3];
    int l2[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    char s[] = "hello";
    char s2[10] = "hi";
    struct pt lp = { 5, 6 };
    struct lab ll = { "loc", 9 };
    int arr[5] = { 1, 2 };
    int x = { 7 };
    struct pt grid[2][2];
    int i;
    int t;

    check(g2[1][2], 6);
    check(g2e[1][0], 4);
    check(g2e[1][2], 0);
    check(gnames[2][4], 'j');
    check(gnames[0][2], 0);
    check(gnames[1][0], 'c');
    check(gpt.x * 10 + gpt.y, 34);
    check(sizeof(gpts3), 18);
    check(gpts3[2].y, 6);
    check(gline.to.x, 3);
    check(gline.to.y, 0);
    check(glabs[0].msg[1], 'i');
    check(glabs[0].at.y, 8);
    checkl(glabs[0].big, 100000L);
    check(glabs[1].name[2], 'o');
    check(glabs[1].n, 0);
    check(glabs[1].msg == 0, 1);
    check(gscal, 42);
    checkl(glong3[1], -1L);
    checkl(glong3[2], 0L);
    check(gsh[1][0], -2);
    check(g3d[1][0][1], 6);
    check(sizeof(g3d), 24);
    check(sizeof(gstr2), 3);
    check(sizeof(gtrail), 9);
    check(*gptrs[0], 42);
    check(*gptrs[1], 2);
    check(gpart[1][0], 'e');
    check(gpart[1][3], 0);
    check(gbytes[0] + gbytes[1] + gbytes[2] + gbytes[3], 255 + 0 + 255 + 0);
    check(sum2(g2, 2), 6 + 2 * 15);
    check(sizeof(g2[0]), 9);
    check(&g2[1][0] - &g2[0][0], 3);
    rowp = g2;
    check(rowp[1][1], 5);
    check((*rowp)[2], 3);
    rowp++;
    check((*rowp)[0], 4);
    check(l2[1][1], 5);
    check(sum2(l2, 1), 6);
    check(sizeof(s), 6);
    check(s[4], 'o');
    check(s2[1], 'i');
    check(s2[9], 0);
    check(lp.x, 5);
    check(ll.name[1], 'o');
    check(ll.n, 9);
    checkl(ll.big, 0L);
    check(arr[1], 2);
    check(arr[4], 0);
    check(x, 7);
    t = 0;
    for (i = 0; i < 3; i++) {
        int a[3] = { 10, 20, 30 };      /* initialised again on every pass */

        a[i] += i;
        t += a[i];
    }
    check(t, 10 + 21 + 32);
    grid[1][0] = mkpt(1, 2);
    grid[0][1] = grid[1][0];
    check(grid[0][1].y, 2);
    check(sizeof(grid), 24);
}

/* ---- #asm, and a function declared without a prototype ------------------------------------- */

int gasm;
int np();

void set_by_asm(void)
{
#asm
    ld hl,1234
    ld (_gasm),hl       ; the global, by its assembly name
#endasm
}

void test_misc(void)
{
    set_by_asm();
    check(gasm, 1234);
    check(np(3, 4), 34);                /* the arguments get the default promotions */
    check(np((char)5, (short)6), 56);
}

int np(int a, int b)
{
    return a * 10 + b;
}

int main(void)
{
    test_structs();
    test_compound();
    test_arrays();
    test_misc();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
