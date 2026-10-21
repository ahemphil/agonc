/* opcost.c: the cost of one integer operation in a loop, built once per
 * operation (-DOP=n, -DNAME=its name) by opcost.py, against an empty loop
 * (OP 0). The sum is unsigned int and the checksum its low 16 bits, so
 * the PC's 32-bit int and the Agon's 24-bit int agree. */
#include "../bench.h"

#ifndef OP
#define OP 0
#endif

#define N 40000
#define STR(x) #x
#define XSTR(x) STR(x)

int main(void)
{
    int i;
    int lo;
    unsigned u;
    unsigned char c;
    unsigned s;
    long l;

    lo = OP == 9 ? -N : 0;
    s = 0;
    bench_start();
    for (i = lo; i < lo + N; i++) {
#if OP == 0
        s += i;                 /* the loop alone */
#elif OP == 1
        s += i / 2;             /* signed / by a power of two: __idivs */
#elif OP == 2
        s += i >> 1;            /* what A1 makes of it (for i >= 0): __ishrs */
#elif OP == 3
        s += i % 2;             /* signed % by a power of two: __irems */
#elif OP == 4
        s += i & 1;             /* what A1 makes of it (for i >= 0): __iand */
#elif OP == 5
        u = i;
        s += u / 8;             /* unsigned / by a power of two: __idivu */
#elif OP == 6
        u = i;
        s += u % 8;             /* unsigned % by a power of two: __iremu */
#elif OP == 7
        s += i / 10;            /* signed / by another constant (A5) */
#elif OP == 8
        s += i % 10;            /* signed % by another constant (A5) */
#elif OP == 9
        s += i / 2;             /* as 1, over negative i */
#elif OP == 10
        l = i;
        s += (unsigned)(l / 2); /* long / by a power of two: __ldivs */
#elif OP == 11
        l = i;
        s += (unsigned)(l >> 1); /* long >> 1: __lshrs */
#elif OP == 12
        c = (unsigned char)i;
        s += c / 10;            /* an unsigned byte / 10 */
#elif OP == 13
        c = (unsigned char)i;
        s += c % 10;            /* an unsigned byte % 10 */
#endif
    }
    return bench_end(XSTR(NAME), s & 0xFFFFUL);
}
