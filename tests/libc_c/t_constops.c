/* t_constops.c - cc2's inline code for constant operands (-O): shifts by
 * a constant count; &, | and ^ with a constant; / and % by a power of
 * two; as values, in compound assignments (direct, through a pointer, and
 * on char and short lvalues) and as conditions. Every result is compared
 * with the same operation on an operand that is not a constant (id()'s
 * result), which always calls the runtime helper, over values chosen for
 * their signs and byte boundaries; a few known answers pin the meaning
 * itself. With -O0 both sides call the helpers, and the checks still
 * hold. Each check() counts the mismatches of one group.
 */

#include "check.h"

int vals[] = {
    0, 1, -1, 2, -2, 3, -3, 5, -5, 7, -7, 8, -8, 15, -15, 16, -16,
    127, -127, 128, -128, 129, -129, 255, -255, 256, -256, 257, -257,
    0x7FFF, -0x7FFF, 0x8000, -0x8000, 0xFFFF, -0xFFFF, 0x10000, -0x10000,
    0x12345, -0x12345, 0x123456, -0x123456, 0x3FFFFF, 0x400000, -0x400000,
    0x7FFFFE, 0x7FFFFF, -0x7FFFFF, -0x7FFFFF - 1, 0x5A5A5A, -0x5A5A5A,
    0x00FF00, 0x0F0F0F, 0x7F7F7F, -0x00FF01
};
#define NV (int)(sizeof vals / sizeof vals[0])

/* v, as a value cc1 cannot fold: the operation it takes part in calls the
 * helper */
int id(int v)
{
    return v;
}

unsigned uid(unsigned v)
{
    return v;
}

#define KA(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10) M(11)
#define KB(M) M(12) M(13) M(14) M(15) M(16) M(17) M(18) M(19) M(20) M(21) M(22)

/* signed / and % by 2^k, k 0 to 22 */
#define DS(k) \
    if (x / (1 << k) != x / id(1 << k)) bad++; \
    if (x % (1 << k) != x % id(1 << k)) bad++;

int divs_a(void)
{
    int i;
    int x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        KA(DS)
    }
    return bad;
}

int divs_b(void)
{
    int i;
    int x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        KB(DS)
    }
    return bad;
}

/* unsigned / and % by 2^k, k 0 to 23 */
#define DU(k) \
    if (u / (1U << k) != u / uid(1U << k)) bad++; \
    if (u % (1U << k) != u % uid(1U << k)) bad++;

int divu(void)
{
    int i;
    unsigned u;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        u = (unsigned)vals[i];
        KA(DU)
        KB(DU)
        DU(23)
    }
    return bad;
}

/* << and >> (signed and unsigned) by k, 0 to 23 */
#define SH(k) \
    if ((x << k) != (x << id(k))) bad++; \
    if ((x >> k) != (x >> id(k))) bad++; \
    if (((unsigned)x >> k) != ((unsigned)x >> id(k))) bad++;

int shifts_a(void)
{
    int i;
    int x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        KA(SH)
    }
    return bad;
}

int shifts_b(void)
{
    int i;
    int x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        KB(SH)
        SH(23)
    }
    return bad;
}

/* & | ^ with constants of every byte pattern, either side */
#define BC(c) \
    if ((x & (c)) != (x & id(c))) bad++; \
    if ((x | (c)) != (x | id(c))) bad++; \
    if ((x ^ (c)) != (x ^ id(c))) bad++; \
    if (((c) & x) != (id(c) & x)) bad++;
#define CA(M) M(0) M(1) M(0x0F) M(0x80) M(0xF0) M(0xFF) M(0x100) M(0x1FF) M(0x8000) \
    M(0xFF00) M(0xFFFF) M(0x10000) M(0x7FFFFF) M(0x123456) M(0x00FF00)
#define CB(M) M(0x0F0F0F) M(-1) M(-2) M(-8) M(-256) M(-257) M(-0x10000) M(-0x8000) \
    M(-0xFF01) M(-0x100) M(0x10001) M(0x7F00FF)

int bits_a(void)
{
    int i;
    int x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        CA(BC)
    }
    return bad;
}

int bits_b(void)
{
    int i;
    int x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        CB(BC)
    }
    return bad;
}

/* a byte loaded without (and with) its sign: >>, /, %, & in A */
#define BY(k) \
    if ((uc >> k) != (uc >> id(k))) bad++; \
    if ((sc >> k) != (sc >> id(k))) bad++;

int bytes(void)
{
    int i;
    unsigned char uc;
    signed char sc;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        uc = (unsigned char)vals[i];
        sc = (signed char)vals[i];
        KA(BY)
        if (uc / 4 != uc / id(4)) bad++;
        if (uc % 16 != uc % id(16)) bad++;
        if (sc / 4 != sc / id(4)) bad++;
        if (sc % 4 != sc % id(4)) bad++;
        if ((uc & 0x0F) != (uc & id(0x0F))) bad++;
        if ((sc & 0x0F) != (sc & id(0x0F))) bad++;
        if ((uc & 0xF0) != (uc & id(0xF0))) bad++;
        if ((uc & 0x1F0) != (uc & id(0x1F0))) bad++;
        if ((sc & -16) != (sc & id(-16))) bad++;
    }
    return bad;
}

/* an unsigned byte / and % a constant that is no power of two: every byte */
#define BD(k) \
    if (uc / (k) != uc / id(k)) bad++; \
    if (uc % (k) != uc % id(k)) bad++;

int bytediv(void)
{
    int i;
    unsigned char uc;
    int bad;

    bad = 0;
    for (i = 0; i < 256; i++) {
        uc = (unsigned char)i;
        BD(3) BD(5) BD(6) BD(7) BD(9) BD(10) BD(11) BD(12) BD(13) BD(25)
        BD(100) BD(127) BD(129) BD(200) BD(254) BD(255) BD(256) BD(1000)
    }
    return bad;
}

/* x op= constant: on a local, through a pointer, on char and short */
int arr[3];
short sh;
signed char sch;
unsigned char uch;

#define AS(op, c, opv) \
    y = x; y op c; if (y != (x opv id(c))) bad++; \
    arr[i % 3] = x; p = &arr[i % 3]; *p op c; if (*p != (x opv id(c))) bad++; \
    sh = (short)x; sh op c; if (sh != (short)((short)x opv id(c))) bad++; \
    sch = (signed char)x; sch op c; if (sch != (signed char)((signed char)x opv id(c))) bad++; \
    uch = (unsigned char)x; uch op c; if (uch != (unsigned char)((unsigned char)x opv id(c))) bad++;

/* a function of AS checks (split so that each fits cc2's tables) */
#define AS_FN(name, ops) \
int name(void) \
{ \
    int i; \
    int x; \
    int y; \
    int *p; \
    int bad; \
 \
    bad = 0; \
    for (i = 0; i < NV; i++) { \
        x = vals[i]; \
        ops \
    } \
    return bad; \
}

AS_FN(assigns_a, AS(/=, 2, /) AS(%=, 2, %) AS(/=, 8, /) AS(%=, 16, %))
AS_FN(assigns_b, AS(>>=, 1, >>) AS(>>=, 5, >>) AS(>>=, 12, >>) AS(>>=, 20, >>) AS(<<=, 3, <<) AS(<<=, 13, <<))
AS_FN(assigns_c, AS(&=, 0xF0, &) AS(&=, -8, &) AS(|=, 0x101, |) AS(^=, 0x5A, ^))

int assigns_u(void)
{
    int i;
    unsigned uy;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        uy = (unsigned)vals[i];
        uy /= 4;
        if (uy != (unsigned)vals[i] / uid(4)) bad++;
        uy = (unsigned)vals[i];
        uy %= 8;
        if (uy != (unsigned)vals[i] % uid(8)) bad++;
    }
    return bad;
}

/* x & constant as a condition, and !(x & constant) */
#define CJ(c) \
    t = 0; if (x & (c)) t = 1; if (t != ((x & id(c)) != 0)) bad++; \
    t = 0; if (!(x & (c))) t = 1; if (t != ((x & id(c)) == 0)) bad++; \
    t = 0; if ((c) & uc) t = 1; if (t != ((uc & id(c)) != 0)) bad++;

int conds(void)
{
    int i;
    int x;
    int t;
    unsigned char uc;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        uc = (unsigned char)x;
        CJ(0) CJ(1) CJ(0x80) CJ(0xFF) CJ(0x100) CJ(0xFF00) CJ(0x8001)
        CJ(0xFFFF) CJ(0x0F0F) CJ(0x10000) CJ(-1)
    }
    return bad;
}

int main(void)
{
    int m7;
    int big;

    m7 = id(-7);
    big = id(-0x7FFFFF - 1);
    check(m7 / 2, -3);
    check(m7 % 2, -1);
    check(m7 % 4, -3);
    check(m7 >> 1, -4);
    check((int)((unsigned)m7 >> 20), 0xF);
    check(m7 & 0xFF, 0xF9);
    check(m7 | 0x100, -7);
    check(m7 ^ 0xFFFF, -65530);         /* 0xFFFFF9 ^ 0x00FFFF = 0xFF0006 */
    check(big / 0x400000, -2);
    check(big % 0x10000, 0);
    check(big >> 23, -1);
    check(divs_a(), 0);
    check(divs_b(), 0);
    check(divu(), 0);
    check(shifts_a(), 0);
    check(shifts_b(), 0);
    check(bits_a(), 0);
    check(bits_b(), 0);
    check(bytes(), 0);
    check(bytediv(), 0);
    check(assigns_a(), 0);
    check(assigns_b(), 0);
    check(assigns_c(), 0);
    check(assigns_u(), 0);
    check(conds(), 0);
    return finish();
}
