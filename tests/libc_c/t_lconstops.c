/* t_lconstops.c - cc2's inline code for long constant operands (-O):
 * shifts by a constant count; &, | and ^ with a constant; / and % by a
 * power of two; in compound assignments; and as conditions: a long
 * tested against 0, compared with a constant, and masked. As in
 * t_constops.c, each result is compared with the same operation on an
 * operand that is not a constant (lid()'s result), which calls the
 * helper, over values chosen for their signs and byte boundaries; a few
 * known answers pin the meaning. Each check() counts one group's
 * mismatches.
 */

#include "check.h"

long vals[] = {
    0L, 1L, -1L, 2L, -2L, 3L, -3L, 7L, -7L, 8L, -8L, 127L, -128L, 255L, -255L,
    256L, -256L, 0xFFFFL, -0xFFFFL, 0x10000L, -0x10000L, 0x7FFFFFL, 0x800000L,
    -0x800000L, 0xFFFFFFL, -0xFFFFFFL, 0x1000000L, -0x1000000L, 0x12345678L,
    -0x12345678L, 0x7FFFFFFFL, -0x7FFFFFFFL, -0x7FFFFFFFL - 1L, 0x00FF00FFL,
    0x7F00FF00L, -0x00FF0100L, 0x7F7F7F7FL, 0x5A5A5A5AL, -0x5A5A5A5AL,
    0x00800000L, 0x0000FF80L, 0x40000000L, -0x40000000L, 0x01010101L
};
#define NV (int)(sizeof vals / sizeof vals[0])

long lid(long v)
{
    return v;
}

unsigned long ulid(unsigned long v)
{
    return v;
}

int id(int v)
{
    return v;
}

#define K0(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7)
#define K1(M) M(8) M(9) M(10) M(11) M(12) M(13) M(14) M(15)
#define K2(M) M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23)
#define K3(M) M(24) M(25) M(26) M(27) M(28) M(29) M(30) M(31)

/* << and >> (signed and unsigned) by k */
#define SH(k) \
    if ((x << k) != (x << id(k))) bad++; \
    if ((x >> k) != (x >> id(k))) bad++; \
    if (((unsigned long)x >> k) != ((unsigned long)x >> id(k))) bad++;

#define FN(name, body) \
int name(void) \
{ \
    int i; \
    long x; \
    unsigned long u; \
    int bad; \
 \
    bad = 0; \
    for (i = 0; i < NV; i++) { \
        x = vals[i]; \
        u = (unsigned long)x; \
        body \
    } \
    return bad + (int)(u & 0); \
}

FN(shifts_0, K0(SH))
FN(shifts_1, K1(SH))
FN(shifts_2, K2(SH))
FN(shifts_3, K3(SH))

/* signed / and % by 2^k (k 0 to 30: 2^31 is no positive long) */
#define DS(k) \
    if (x / (1L << k) != x / lid(1L << k)) bad++; \
    if (x % (1L << k) != x % lid(1L << k)) bad++;

FN(divs_0, K0(DS))
FN(divs_1, K1(DS))
FN(divs_2, K2(DS))
FN(divs_3, DS(24) DS(25) DS(26) DS(27) DS(28) DS(29) DS(30))

/* unsigned / and % by 2^k, k 0 to 31 */
#define DU(k) \
    if (u / (1UL << k) != u / ulid(1UL << k)) bad++; \
    if (u % (1UL << k) != u % ulid(1UL << k)) bad++;

FN(divu_0, K0(DU) K1(DU))
FN(divu_1, K2(DU) K3(DU))

/* & | ^ with constants of every byte pattern, either side */
#define BC(c) \
    if ((x & (c)) != (x & lid(c))) bad++; \
    if ((x | (c)) != (x | lid(c))) bad++; \
    if ((x ^ (c)) != (x ^ lid(c))) bad++; \
    if (((c) & x) != (lid(c) & x)) bad++;

FN(bits_0, BC(0L) BC(1L) BC(0xFFL) BC(0x100L) BC(0xFF00L) BC(0xFF0000L) BC(0x800000L)
           BC(0x1000000L) BC(0x7F000000L))
FN(bits_1, BC(-1L) BC(-2L) BC(-256L) BC(0xFFFFFFL) BC(-0x1000000L) BC(0x12345678L)
           BC(0x00FF00FFL) BC(-0x12345678L) BC(0x7FFFFFFFL))

/* x op= constant, on a local and through a pointer */
long arr[3];

#define AS(op, c, opv) \
    y = x; y op c; if (y != (x opv lid(c))) bad++; \
    arr[i % 3] = x; p = &arr[i % 3]; *p op c; if (*p != (x opv lid(c))) bad++;
#define ASI(op, c, opv) \
    y = x; y op c; if (y != (x opv id(c))) bad++; \
    arr[i % 3] = x; p = &arr[i % 3]; *p op c; if (*p != (x opv id(c))) bad++;

int assigns(void)
{
    int i;
    long x;
    long y;
    long *p;
    unsigned long uy;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        AS(/=, 4L, /) AS(%=, 16L, %) AS(&=, 0xF0F0L, &) AS(|=, 0x10001L, |)
        AS(^=, 0xEDB88320L, ^)
        ASI(>>=, 1, >>) ASI(>>=, 13, >>) ASI(<<=, 3, <<) ASI(<<=, 25, <<)
        uy = (unsigned long)x;
        uy >>= 7;
        if (uy != (unsigned long)x >> id(7)) bad++;
        uy = (unsigned long)x;
        uy %= 256UL;
        if (uy != (unsigned long)x % ulid(256UL)) bad++;
    }
    return bad;
}

/* conditions: x itself, !x, x == k, x != k, x & k */
#define CT(t1, t2) \
    t = 0; if (t1) t = 1; if (t != ((t2) != 0)) bad++;
#define CK(k) \
    CT(x == (k), x == lid(k)) CT(x != (k), x != lid(k)) CT((k) == x, lid(k) == x) \
    CT(x & (k), x & lid(k)) CT(!(x & (k)), !(x & lid(k)))

int conds(void)
{
    int i;
    int t;
    long x;
    int bad;

    bad = 0;
    for (i = 0; i < NV; i++) {
        x = vals[i];
        CT(x, x != lid(0L)) CT(!x, x == lid(0L))
        CK(0L) CK(1L) CK(-1L) CK(0x80L) CK(0x8000L) CK(0x800000L) CK(0x80000000L)
        CK(0xFF00FFL) CK(0x12345678L) CK(0x01000001L)
    }
    return bad;
}

int main(void)
{
    long m7;
    unsigned long crc;
    int k;

    m7 = lid(-7L);
    check(m7 / 2L == -3L, 1);
    check(m7 % 2L == -1L, 1);
    check(m7 % 4L == -3L, 1);
    check(m7 >> 1 == -4L, 1);
    check((unsigned long)m7 >> 28 == 0xFUL, 1);
    check((m7 & 0xFFL) == 0xF9L, 1);
    check((m7 ^ 0xFFFF0000L) == 0x0000FFF9L, 1);
    crc = ulid(0xFFFFFFFFUL);
    for (k = 0; k < 8; k++)
        crc = crc & 1 ? crc >> 1 ^ 0xEDB88320UL : crc >> 1;
    check(crc == 0x2DFD1072UL, 1);      /* eight steps from all ones (Python's answer) */
    check(shifts_0(), 0);
    check(shifts_1(), 0);
    check(shifts_2(), 0);
    check(shifts_3(), 0);
    check(divs_0(), 0);
    check(divs_1(), 0);
    check(divs_2(), 0);
    check(divs_3(), 0);
    check(divu_0(), 0);
    check(divu_1(), 0);
    check(bits_0(), 0);
    check(bits_1(), 0);
    check(assigns(), 0);
    check(conds(), 0);
    return finish();
}
