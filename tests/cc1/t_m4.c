/* t_m4.c - cc1's execution test for M4's additions: struct, enum, typedef,
 * switch and goto, checked at run time.
 *
 * Compiled by cc1 + cc2 + ld and run on the emulator. Exits 0 if every
 * check passes, else the number of the first failing check (see main).
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

/* ---- declarations under test ------------------------------------------------------ */

struct point {
    int x;
    int y;
};

struct rec {
    char tag;
    int n;
    char name[5];
    struct point at;
    struct rec *next;
    unsigned char flags;
};

typedef struct node node;
struct node {
    int v;
    node *next;
};

typedef int count_t;
typedef char name_t[12];
typedef struct point *point_p;
typedef unsigned char byte;

enum colour { RED, GREEN, BLUE, VIOLET = 10, ULTRA, INFRA = -3, NEAR_ };
enum { K_A = 1 << 4, K_B = K_A * 2 + 1, K_C = sizeof(struct point) };

struct point g_pt;
struct point g_pts[4];
struct rec g_rec;
int *g_py = &g_pt.y;
struct point *g_second = &g_pts[1];
int g_table[BLUE + 2];
enum colour g_col = ULTRA;

struct big {
    int head;
    char pad[200];
    int tail;
};

struct big g_big;

/* ---- structs ----------------------------------------------------------------------- */

void set_point(struct point *p, int x, int y)
{
    p->x = x;
    p->y = y;
}

int sum_points(struct point *p, int n)
{
    int s;

    s = 0;
    while (n-- > 0) {
        s += p->x * 10 + p->y;
        p++;
    }
    return s;
}

void test_struct_basic(void)
{
    struct point a;
    struct point b;
    struct rec r;
    int k;

    check(sizeof(struct point), 6);
    check(sizeof(struct rec), 1 + 3 + 5 + 6 + 3 + 1);
    check(sizeof a, 6);
    check(sizeof r.name, 5);
    check(sizeof(r.at), 6);
    a.x = 3;
    a.y = -4;
    check(a.x, 3);
    check(a.y, -4);
    b = a;
    check(b.x * 10 + b.y, 26);
    a.x = 100;
    check(b.x, 3);                      /* a copy, not an alias */
    set_point(&b, 7, 8);
    check(b.x + b.y, 15);
    r.tag = 'q';
    r.n = 123456;
    r.name[0] = 'h';
    r.name[4] = 'z';
    r.at = b;
    r.at.y = 9;
    r.flags = 200;
    r.next = &r;
    check(r.tag, 'q');
    check(r.n, 123456);
    check(r.name[4], 'z');
    check(r.at.x, 7);
    check(r.at.y, 9);
    check(b.y, 8);
    check(r.flags, 200);
    check(r.next->next->n, 123456);
    check(r.next->at.x, 7);
    k = 0;
    r.next->name[k + 1] = 'i';
    check(r.name[1], 'i');
    check((char *)&r.n - (char *)&r, 1);
    check((char *)&r.at.y - (char *)&r, 12);
    check((char *)&r.flags - (char *)&r, 18);
    check((char *)&r.next - (char *)&r, 15);
}

void test_struct_globals(void)
{
    struct point *p;
    int i;

    g_pt.x = 11;
    g_pt.y = 22;
    check(*g_py, 22);
    *g_py = 33;
    check(g_pt.y, 33);
    for (i = 0; i < 4; i++)
        set_point(&g_pts[i], i, i + 1);
    check(sum_points(g_pts, 4), 0 * 10 + 1 + 1 * 10 + 2 + 2 * 10 + 3 + 3 * 10 + 4);
    check(g_second->x, 1);
    p = g_pts;
    p = p + 2;
    check(p->y, 3);
    check((p - 1)->x, 1);
    check(p - g_pts, 2);
    check(p[1].x, 3);
    p++;
    check(p->x, 3);
    g_pts[0] = g_pts[3];
    check(g_pts[0].x + g_pts[0].y, 7);
    g_rec.at = g_pts[2];
    check(g_rec.at.y, 3);
    g_rec.next = &g_rec;
    g_rec.next->at.x = 50;
    check(g_rec.at.x, 50);
}

void test_struct_assign_chain(void)
{
    struct point a;
    struct point b;
    struct point c;
    struct point *p;

    a.x = 1;
    a.y = 2;
    c = b = a;
    check(c.x + b.y, 3);
    p = &c;
    *p = g_pts[1];
    check(c.x, 1);
    check(c.y, 2);
    b = *p;
    check(b.y, 2);
}

void test_big_struct(void)
{
    struct big a;
    struct big b;
    int i;

    a.head = 17;
    a.tail = -9;
    for (i = 0; i < 200; i++)
        a.pad[i] = i;
    b = a;
    check(b.head, 17);
    check(b.tail, -9);
    check(b.pad[199], 199 - 256);
    check(b.pad[100], 100);
    g_big = b;
    check(g_big.tail, -9);
    check(g_big.pad[150], 150 - 256);
    check(sizeof(struct big), 206);
}

int list_sum(node *n)
{
    int s;

    s = 0;
    while (n) {
        s += n->v;
        n = n->next;
    }
    return s;
}

void test_list(void)
{
    node a;
    node b;
    node c;

    a.v = 1;
    b.v = 20;
    c.v = 300;
    a.next = &b;
    b.next = &c;
    c.next = 0;
    check(list_sum(&a), 321);
    b.next = 0;
    check(list_sum(&a), 21);
}

/* ---- enums and typedefs -------------------------------------------------------------- */

void test_enum(void)
{
    enum colour c;
    enum { LOCAL_A = 5, LOCAL_B };
    count_t n;
    name_t nm;
    point_p pp;
    byte by;

    check(RED, 0);
    check(GREEN, 1);
    check(BLUE, 2);
    check(VIOLET, 10);
    check(ULTRA, 11);
    check(INFRA, -3);
    check(NEAR_, -2);
    check(K_A, 16);
    check(K_B, 33);
    check(K_C, 6);
    check(sizeof g_table, 12);
    check(g_col, 11);
    c = BLUE;
    check(c + 1, 3);
    check(sizeof c, 3);
    check(LOCAL_B, 6);
    n = 5;
    check(n * 2, 10);
    check(sizeof(count_t), 3);
    check(sizeof nm, 12);
    check(sizeof(name_t), 12);
    nm[11] = 'x';
    check(nm[11], 'x');
    pp = &g_pt;
    check(pp->x, 11);
    by = 255;
    by++;
    check(by, 0);
    check((byte)300, 44);
}

/* ---- switch ---------------------------------------------------------------------------- */

int classify(int v)
{
    switch (v) {
    case 0:
        return 100;
    case 1:
    case 2:
        return 200;
    case -1:
        return 300;
    case 0x7FFFFF:
        return 400;
    case (-8388607 - 1):
        return 500;
    case VIOLET:
        return 600;
    default:
        return 999;
    }
}

int fallthrough(int v)
{
    int r;

    r = 0;
    switch (v) {
    case 1:
        r += 1;
    case 2:
        r += 10;
        break;
    default:
        r += 1000;
    case 3:
        r += 100;
    }
    return r;
}

int no_default(int v)
{
    int r;

    r = 7;
    switch (v) {
    case 4:
        r = 8;
        break;
    }
    return r;
}

int nested(int a, int b)
{
    switch (a) {
    case 1:
        switch (b) {
        case 1:
            return 11;
        case 2:
            break;
        default:
            return 19;
        }
        return 12;
    case 2:
        return 20;
    }
    return 0;
}

int in_loop(int n)
{
    int i;
    int s;

    s = 0;
    for (i = 0; i < n; i++) {
        switch (i % 4) {
        case 0:
            continue;
        case 1:
            s += 1;
            break;
        case 2:
            s += 10;
            break;
        default:
            s += 100;
        }
        s += 1000;
    }
    return s;
}

int char_switch(char c)
{
    switch (c) {
    case 'a':
        return 1;
    case -2:
        return 2;
    case '\n':
        return 3;
    }
    return 0;
}

int inside_block(int v)
{
    int r;

    r = 0;
    switch (v) {
    case 0:
        r = 1;
        if (v == 0) {
    case 5:
            r += 50;
        }
        break;
    }
    return r;
}

int only_default(int v)
{
    switch (v) {
    default:
        v = v * 2;
    }
    return v;
}

int by_colour(enum colour c)
{
    switch (c) {
    case RED: return 'r';
    case GREEN: return 'g';
    case BLUE: return 'b';
    case VIOLET: return 'v';
    }
    return '?';
}

int many(int v)
{
    switch (v) {
    case 0: return 3;
    case 1: return 5;
    case 2: return 7;
    case 3: return 11;
    case 4: return 13;
    case 5: return 17;
    case 6: return 19;
    case 7: return 23;
    case 8: return 29;
    case 9: return 31;
    case 10: return 37;
    case 11: return 41;
    case 12: return 43;
    case 13: return 47;
    case 14: return 53;
    case 15: return 59;
    case 1000: return 61;
    case 100000: return 67;
    }
    return -1;
}

void test_switch(void)
{
    int i;
    int s;

    check(classify(0), 100);
    check(classify(1), 200);
    check(classify(2), 200);
    check(classify(-1), 300);
    check(classify(8388607), 400);
    check(classify(-8388607 - 1), 500);
    check(classify(10), 600);
    check(classify(3), 999);
    check(classify(65536), 999);
    check(fallthrough(1), 11);
    check(fallthrough(2), 10);
    check(fallthrough(3), 100);
    check(fallthrough(9), 1100);
    check(no_default(4), 8);
    check(no_default(5), 7);
    check(nested(1, 1), 11);
    check(nested(1, 2), 12);
    check(nested(1, 3), 19);
    check(nested(2, 1), 20);
    check(nested(3, 1), 0);
    check(in_loop(8), 2 * (1 + 10 + 100) + 6 * 1000);
    check(char_switch('a'), 1);
    check(char_switch(-2), 2);
    check(char_switch('\n'), 3);
    check(char_switch('b'), 0);
    check(inside_block(0), 51);
    check(inside_block(5), 50);
    check(inside_block(6), 0);
    check(only_default(21), 42);
    check(by_colour(GREEN), 'g');
    check(by_colour(VIOLET), 'v');
    check(by_colour(ULTRA), '?');
    s = 0;
    for (i = 0; i < 16; i++)
        s += many(i);
    check(s, 3 + 5 + 7 + 11 + 13 + 17 + 19 + 23 + 29 + 31 + 37 + 41 + 43 + 47 + 53 + 59);
    check(many(1000), 61);
    check(many(100000), 67);
    check(many(16), -1);
}

/* ---- goto ------------------------------------------------------------------------------ */

int goto_loop(int n)
{
    int s;

    s = 0;
again:
    if (n > 0) {
        s += n;
        n--;
        goto again;
    }
    return s;
}

int goto_out(int limit)
{
    int i;
    int j;

    for (i = 0; i < 10; i++)
        for (j = 0; j < 10; j++)
            if (i * j > limit)
                goto found;
    return -1;
found:
    return i * 100 + j;
}

int goto_forward(int v)
{
    if (v)
        goto skip;
    v = 50;
skip:
    return v + 1;
}

int goto_into_block(int v)
{
    if (v > 5)
        goto inner;
    v = 0;
    {
        v += 1;
inner:
        v += 100;
    }
    return v;
}

void test_goto(void)
{
    check(goto_loop(10), 55);
    check(goto_out(20), 307);
    check(goto_out(100), -1);
    check(goto_forward(0), 51);
    check(goto_forward(7), 8);
    check(goto_into_block(1), 101);
    check(goto_into_block(9), 109);
}

/* ---- variable arguments: <stdarg.h>'s built-ins, called directly ------------------- */

typedef char *va_list;

int sum_ints(int n, ...)
{
    va_list ap;
    int s;

    __va_start(ap, n);
    s = 0;
    while (n-- > 0)
        s += __va_arg(ap, int);
    __va_end(ap);
    return s;
}

/* each letter of fmt takes one argument: i int, u unsigned, s string, c char */
int walk(int bias, char *fmt, ...)
{
    va_list ap;
    int r;
    char *s;

    __va_start(ap, fmt);
    r = bias;
    while (*fmt) {
        if (*fmt == 'i')
            r = r * 3 + __va_arg(ap, int);
        else if (*fmt == 'u')
            r = r + (__va_arg(ap, unsigned) >> 20);
        else if (*fmt == 's') {
            s = __va_arg(ap, char *);
            r = r * 5 + s[0] + s[1];
        } else if (*fmt == 'c')
            r = r * 2 + __va_arg(ap, int);
        fmt++;
    }
    __va_end(ap);
    return r;
}

void test_varargs(void)
{
    check(sum_ints(0), 0);
    check(sum_ints(3, 1, 2, 3), 6);
    check(sum_ints(4, -1, 100000, -8388607, 8388607), 99999);
    check(walk(1, ""), 1);
    check(walk(1, "i", 5), 8);
    check(walk(0, "u", 0xF00000), 15);
    check(walk(2, "s", "AB"), 10 + 'A' + 'B');
    check(walk(0, "c", 'z'), 'z');
    check(walk(1, "icsu", -4, 'q', "xy", 0x300000), ((1 * 3 - 4) * 2 + 'q') * 5 + 'x' + 'y' + 3);
}

int main(void)
{
    test_struct_basic();
    test_struct_globals();
    test_struct_assign_chain();
    test_big_struct();
    test_list();
    test_enum();
    test_switch();
    test_goto();
    test_varargs();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
