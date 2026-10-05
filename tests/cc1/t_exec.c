/* t_exec.c - cc1's execution test: every M3 feature, checked at run time.
 *
 * Compiled by cc1 + cc2 + ld and run on the emulator. Exits 0 if every
 * check passes, else the number of the first failing check (see main).
 * No preprocessor exists yet, so there are no #includes or macros.
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

/* ---- globals and static initialisers ---------------------------------------- */

int g_zero;
int g_init = 1234567;
int g_neg = (-8388607 - 1);
unsigned g_big = 0xFFFFFE;
char g_c = -5;
unsigned char g_uc = 250;
int g_arr[5] = { 10, 20, 30 };
int g_arr2[] = { 1, 2, 3, 4 };
char g_str[] = "hello";
char g_str8[8] = "abc";
char *g_ptr = g_str + 1;
int *g_iptr = &g_init;
char *g_lit = "literal";
char *g_list[] = { "zero", "one", "two" };
int g_const = (3 + 4) * 5 - 1;
static int s_counter;
static int s_table[4] = { 7, 8, 9, 10 };
int g_bss_arr[10];

/* ---- functions under test ------------------------------------------------------- */

int add3(int a, int b, int c)
{
    return a * 100 + b * 10 + c;
}

int seven(int a, int b, int c, int d, int e, int f, int g)
{
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
}

static int fact(int n)
{
    if (n < 2)
        return 1;
    return n * fact(n - 1);
}

int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

char to_char(int v)
{
    return v;
}

unsigned char to_uchar(int v)
{
    return v;
}

int char_param(char c)
{
    return c;
}

void bump(void)
{
    s_counter++;
}

int my_strlen(char *s)
{
    int n;

    n = 0;
    while (*s++)
        n++;
    return n;
}

void my_strcpy(char *d, char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

int sum_array(int *a, int n)
{
    int i;
    int s;

    s = 0;
    for (i = 0; i < n; i++)
        s += a[i];
    return s;
}

int side;

int touch(int v)
{
    side++;
    return v;
}

int asm_value(void)
{
    int r;

    r = 0;
    asm("        ld      hl, 4321\n        ld      (ix-3), hl");
    return r;
}

int early(int x)
{
    if (x > 0)
        return 1;
    else
        return -1;
}

int loop_forever_until(int n)
{
    int i;

    i = 0;
    for (;;) {
        if (i == n)
            return i * 2;
        i++;
    }
}

/* ---- the tests ---------------------------------------------------------------------- */

void test_arith(void)
{
    int a;
    int b;
    unsigned u;

    a = 7;
    b = -3;
    check(a + b, 4);
    check(a - b, 10);
    check(a * b, -21);
    check(a / b, -2);
    check(a % b, 1);
    check(-7 / 2, -3);
    check(-7 % 2, -1);
    check(1000 * 1000, 1000000);
    check(8388607 + 1, (-8388607 - 1));           /* 24-bit wrap */
    u = 0xFFFFFF;
    check(u / 2, 8388607);
    check(u % 10, 5);
    u = 0xFFFFFF;
    check(u >> 20, 15);
    check(-16 >> 2, -4);
    check(1 << 23, (-8388607 - 1));
    a = -16;
    check(a >> 2, -4);
    u = a;
    check(u >> 2, 0x3FFFFC);
    check(0xF0F0F0 & 0x0FFF0F, 0x00F000);
    check(0x800001 | 0x7F0000, 0xFF0001);
    check(-1 ^ 0x123456, 0xEDCBA9);
    check(~5, -6);
    check(-a, 16);
    check(!a, 0);
    check(!0, 1);
    check(+a, -16);
    check(g_const, 34);
    b = 100;
    b = b * b / 7 - b % 7;
    check(b, 1426);
}

void test_compare(void)
{
    int a;
    int b;
    unsigned ua;
    unsigned ub;

    a = -1;
    b = 1;
    ua = a;
    ub = b;
    check(a < b, 1);
    check(ua < ub, 0);
    check(a > b, 0);
    check(ua > ub, 1);
    check(a <= -1, 1);
    check(a >= 0, 0);
    check(ua >= 0, 1);
    check(a == -1, 1);
    check(a != -1, 0);
    check(65536 == 0, 0);
    check((-8388607 - 1) < 8388607, 1);
    if (a < b)
        check(1, 1);
    else
        check(0, 1);
    if (ua < ub)
        check(0, 1);
    else
        check(1, 1);
}

void test_chars(void)
{
    char c;
    unsigned char uc;
    int i;

    c = 200;
    check(c, -56);
    uc = 200;
    check(uc, 200);
    uc = -1;
    check(uc, 255);
    c = 127;
    c++;
    check(c, -128);
    uc = 255;
    uc++;
    check(uc, 0);
    c = 'A';
    check(c + 1, 66);
    check('\n', 10);
    check('\xFF', -1);
    check('\377', -1);
    check(g_c, -5);
    check(g_uc, 250);
    i = 300;
    c = i;
    check(c, 44);
    check((char)300, 44);
    check((unsigned char)-1, 255);
    check(to_char(200), -56);
    check(to_uchar(-1), 255);
    check(char_param(300), 44);
    c = 100;
    c += 100;
    check(c, -56);
    uc = 10;
    uc -= 20;
    check(uc, 246);
}

void test_pointers(void)
{
    int a[5];
    int *p;
    int *q;
    char *cp;
    int **pp;
    int i;

    for (i = 0; i < 5; i++)
        a[i] = i * i;
    p = a;
    check(*p, 0);
    check(*(p + 2), 4);
    check(p[3], 9);
    q = &a[4];
    check(q - p, 4);
    check(q > p, 1);
    check(*q--, 16);
    check(*q, 9);
    p++;
    check(*p, 1);
    p += 2;
    check(*p, 9);
    cp = (char *)a;
    check((int)(cp + 3) - (int)cp, 3);
    check((int)(p + 1) - (int)p, 3);        /* int is 3 bytes */
    pp = &p;
    check(**pp, 9);
    **pp = 99;
    check(a[3], 99);
    check(sizeof(int), 3);
    check(sizeof(char), 1);
    check(sizeof(int *), 3);
    check(sizeof a, 15);
    check(sizeof(a) / sizeof(a[0]), 5);
    check(sizeof g_str, 6);
    p = 0;
    check(p == 0, 1);
    check(!p, 1);
}

void test_globals(void)
{
    int i;

    check(g_zero, 0);
    check(g_init, 1234567);
    check(g_neg, (-8388607 - 1));
    check(g_big, 0xFFFFFE);
    check(g_arr[0], 10);
    check(g_arr[2], 30);
    check(g_arr[4], 0);
    check(sizeof g_arr2, 12);
    check(g_arr2[3], 4);
    check(g_str[0], 'h');
    check(g_str[5], 0);
    check(my_strlen(g_str), 5);
    check(g_str8[2], 'c');
    check(g_str8[7], 0);
    check(*g_ptr, 'e');
    check(*g_iptr, 1234567);
    check(g_lit[3], 'e');
    check(g_list[2][1], 'w');
    check(my_strlen(g_list[0]), 4);
    check(s_table[3], 10);
    for (i = 0; i < 10; i++)
        check(g_bss_arr[i], 0);
    g_bss_arr[9] = 5;
    check(g_bss_arr[9], 5);
    bump();
    bump();
    check(s_counter, 2);
    g_init += 3;
    check(g_init, 1234570);
}

void test_calls(void)
{
    char buf[20];

    check(add3(1, 2, 3), 123);
    check(add3(add3(0, 0, 1), 2, 3), 123);
    check(1000 + add3(1, 2, 3), 1123);
    check(seven(1, 2, 3, 4, 5, 6, 7), 140);
    check(fact(10), 3628800);
    check(fib(15), 610);
    my_strcpy(buf, "copy me");
    check(my_strlen(buf), 7);
    check(buf[5], 'm');
    check(sum_array(g_arr, 5), 60);
    check(asm_value(), 4321);
    check(early(5), 1);
    check(early(-5), -1);
    check(loop_forever_until(7), 14);
}

void test_control(void)
{
    int i;
    int j;
    int s;

    s = 0;
    for (i = 0; i < 10; i++) {
        if (i == 3)
            continue;
        if (i == 8)
            break;
        s += i;
    }
    check(s, 25);
    s = 0;
    i = 0;
    while (i < 100) {
        i++;
        if (i % 2)
            continue;
        s += i;
    }
    check(s, 2550);
    s = 0;
    i = 10;
    do {
        s += i;
        i--;
    } while (i > 0);
    check(s, 55);
    s = 0;
    for (i = 0; i < 5; i++)
        for (j = 0; j < 5; j++) {
            if (j > i)
                break;
            s++;
        }
    check(s, 15);
    i = 5;
    if (i > 3)
        if (i > 10)
            s = 1;
        else
            s = 2;
    check(s, 2);
    s = i > 3 ? 100 : 200;
    check(s, 100);
    s = (i = 7, i + 1);
    check(s, 8);
    side = 0;
    s = 0 && touch(1);
    check(s, 0);
    check(side, 0);
    s = 1 || touch(1);
    check(s, 1);
    check(side, 0);
    s = touch(1) && touch(2);
    check(s, 1);
    check(side, 2);
    s = touch(0) || touch(0);
    check(s, 0);
    check(side, 4);
    if (touch(0) || touch(3))
        check(side, 6);
    else
        check(0, 1);
}

void test_incdec(void)
{
    int a;
    int b;
    char c;
    int arr[3];
    int *p;

    a = 5;
    b = a++;
    check(b, 5);
    check(a, 6);
    b = ++a;
    check(b, 7);
    b = a--;
    check(b, 7);
    check(a, 6);
    b = --a;
    check(b, 5);
    c = -128;
    c--;
    check(c, 127);
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    p = arr;
    check(*p++, 1);
    check(*p, 2);
    check(++*p, 3);
    check(arr[1], 3);
    check((*p)++, 3);
    check(arr[1], 4);
    a = 10;
    a *= 3;
    check(a, 30);
    a /= 4;
    check(a, 7);
    a %= 4;
    check(a, 3);
    a <<= 4;
    check(a, 48);
    a >>= 2;
    check(a, 12);
    a &= 6;
    check(a, 4);
    a |= 3;
    check(a, 7);
    a ^= 5;
    check(a, 2);
    p = arr;
    p += 2;
    check(*p, 3);
    p -= 1;
    check(*p, 4);
}

void test_big_frame(void)
{
    char big[300];
    int x;
    int i;

    for (i = 0; i < 300; i++)
        big[i] = i;
    x = 0;
    for (i = 0; i < 300; i++)
        x += big[i];
    check(x, 818);                      /* chars wrap: 0..127, -128..-1, 0..43 */
    check(big[299], 43);
    check(big[200], -56);
}

int main(void)
{
    test_arith();
    test_compare();
    test_chars();
    test_pointers();
    test_globals();
    test_calls();
    test_control();
    test_incdec();
    test_big_frame();
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
