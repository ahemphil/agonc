/* t_m14.c - cc1's execution test for M14: C89's scopes (a parameter in
 * the later parameters' declarators, "struct tag;" in a block, an
 * initialised extern at file scope), GCC's spellings, and the default
 * mode's comma after the last enumerator, non-constant initialisers for
 * local aggregates (struct values for members among them) and implicit
 * int (with a warning).
 *
 * Compiled by cc1 + cc2 + ld in the default mode and run on the emulator
 * (no cpp). Exits 0 if every check passes, else the number of the first
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

/* ---- C89's scopes ---- */

int dsize(unsigned int j, char d[sizeof(j)], long e[sizeof d])
{
    return (int)sizeof(d) + (int)sizeof e[0] + (int)j;
}

struct tag2 {
    int x;
};

struct tag2 s2 = { 4 };

int hidden_tag(void)
{
    struct tag2;                        /* a new tag, hiding the file's */
    struct tag1 {
        struct tag2 *p;
    } a;
    struct tag2 {
        unsigned char c;
        long l;
    } b;

    a.p = &b;
    a.p->c = 9;
    a.p->l = 100000L;
    return (int)sizeof(struct tag2) + a.p->c;
}

extern int initialised = 42;

/* ---- GCC's spellings ---- */

struct pair {
    int a;
    char b[4];
    struct {
        int x;
        int y;
    } in;
};

static __inline__ int twice(int x) __attribute__((noinline));

static __inline__ int twice(int x)
{
    return 2 * x;
}

int __attribute__((noinline, unused)) gnu(char *__restrict p, int n __attribute__((unused)))
{
    __extension__ int r = 0;
    __const__ int k = 3;
    char buf[8];

    if (__builtin_expect(n > 0, 1))
        r = twice(n);
    __builtin_memset(buf, 'z', sizeof buf);
    __builtin_strcpy(buf, "abc");
    __builtin_prefetch(p, 0, 3);
    check(__builtin_strlen(buf), 3);
    check(__builtin_strcmp(buf, "abc"), 0);
    check(buf[4], 'z');
    check(__builtin_abs(-5), 5);
    check(__builtin_constant_p(k + 1), 0);
    check(__builtin_constant_p(3 * 4), 1);
    check(__builtin_offsetof(struct pair, in.y), 3 + 4 + 3);
    check(__builtin_offsetof(struct pair, b[2]), 5);
    return r + k;
}

/* ---- the default mode's conveniences ---- */

enum colour { RED, GREEN, BLUE, };

struct inner {
    int u;
    long v;
};

struct outer {
    char tag;
    struct inner in[2];
    int *p;
    int w;
};

static int seven(void)
{
    return 7;
}

void locals(int x, long y)
{
    struct outer o = { 'q', { { 1, 2L }, { x, y } }, &x, 9 };
    int a[4] = { x, seven(), 3 };
    struct inner i2 = { seven() + x, y * 2 };
    char s[3] = { 'a', (char)x, 'c' };

    check(o.tag, 'q');
    check(o.in[0].u, 1);
    check(o.in[0].v, 2L);
    check(o.in[1].u, x);
    check(o.in[1].v, y);
    check(o.p == &x, 1);
    check(o.w, 9);
    check(a[0], x);
    check(a[1], 7);
    check(a[2], 3);
    check(a[3], 0);
    check(i2.u, 7 + x);
    check(i2.v, y * 2);
    check(s[1], (char)x);
    check(s[2], 'c');
}

/* a struct value for a member whose braces are elided (C99 6.7.8) */
struct pt {
    int x;
    int y;
};

struct seg {
    char id;
    struct pt a;
    struct pt b;
};

struct bits {
    unsigned f : 3;
    int g;
};

struct holder {
    struct bits bb;
    int h;
};

struct pt gpt = { 30, 40 };

static struct pt mkpt(int x, int y)
{
    struct pt p = { x, y };

    return p;
}

void members(int v)
{
    struct pt p = { v, v + 1 };
    struct seg s = { 'k', p, mkpt(7, 8) };
    struct seg t = { 'm', gpt, v, 9 };         /* b's braces elided: its scalars */
    struct pt arr[3] = { p, gpt };
    struct holder h = { 5, v, 6 };             /* bb's braces elided, a bit-field first */

    check(s.id, 'k');
    check(s.a.x, v);
    check(s.a.y, v + 1);
    check(s.b.x, 7);
    check(s.b.y, 8);
    check(t.a.x, 30);
    check(t.a.y, 40);
    check(t.b.x, v);
    check(t.b.y, 9);
    check(arr[0].y, v + 1);
    check(arr[1].x, 30);
    check(arr[2].x, 0);
    check(h.bb.f, 5);
    check(h.bb.g, v);
    check(h.h, 6);
}

/* implicit int, with a warning */
static counted = 3;

twice_old(n)
{
    return n * 2 + counted;
}

int main(void)
{
    char c;

    check(dsize(5, "abc", 0), 3 + 4 + 5);
    check(hidden_tag(), 1 + 4 + 9);
    check(s2.x, 4);
    check(initialised, 42);
    check(gnu(&c, 4), 11);
    check(BLUE, 2);
    locals(5, 100000L);
    locals(-3, -7L);
    members(11);
    members(-2);
    check(twice_old(20), 43);
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
