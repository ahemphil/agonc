/* t_intmath.c - multiply and divide in a C interrupt handler while the
 * main program multiplies and divides too. The helpers keep their working
 * values in registers and on the stack (rt.s's __imul, __idivu, __lmul,
 * __ldivmodu), and int64.c's division uses the alternate registers, which
 * kbint.s's __handler_call saves around a C handler; nothing of one
 * call may leak into another. The handler takes the vertical-blank
 * interrupt (vector 0x32, which the emulator raises every frame),
 * acknowledges it as MOS's own handler does (bit 1 of port B's data
 * register, 0x9A), and works through a few operands each time; main
 * works continuously until 200 frames have passed. Every result checks
 * itself: a quotient and remainder by q * d + r == n with r < d, a
 * product against a shift-and-add multiply that calls no multiply
 * helper. Adapted from the acc comparison's tintmul.c and vbtest.c
 * (2026-10-06).
 */

#include <agon/mos.h>
#include "check.h"

volatile long frames;
volatile long int_bad;
long main_bad;
long main_rounds;
unsigned long hseed = 99;

unsigned long next(unsigned long *seed)
{
    *seed = *seed * 1103515245UL + 12345UL;
    return *seed >> 7;
}

/* a * b mod 2^24 by shifts and adds */
unsigned slowmul(unsigned a, unsigned b)
{
    unsigned r;

    r = 0;
    while (b != 0) {
        if (b & 1)
            r += a;
        a += a;
        b >>= 1;
    }
    return r;
}

unsigned long slowlmul(unsigned long a, unsigned long b)
{
    unsigned long r;

    r = 0;
    while (b != 0) {
        if (b & 1)
            r += a;
        a += a;
        b >>= 1;
    }
    return r;
}

/* One round of every operation on operands from seed; returns the
 * number of results that are wrong. */
int round_of(unsigned long *seed)
{
    unsigned n;
    unsigned d;
    unsigned q;
    unsigned r;
    int sn;
    int sd;
    unsigned long ln;
    unsigned long ld;
    unsigned long long qn;
    unsigned long long qd;
    unsigned long long qq;
    unsigned long long qr;
    int bad;

    bad = 0;
    n = (unsigned)next(seed);
    d = (unsigned)(next(seed) >> (next(seed) & 15));
    if (d == 0)
        d = 7;
    q = n / d;
    r = n % d;
    bad += q * d + r != n || r >= d;
    sn = (int)n;
    sd = (int)d;
    bad += (sn / sd) * sd + sn % sd != sn;
    bad += n * d != slowmul(n, d);
    ln = next(seed) << 9 ^ next(seed);
    ld = next(seed) >> (next(seed) & 15);
    if (ld == 0)
        ld = 13;
    bad += (ln / ld) * ld + ln % ld != ln || ln % ld >= ld;
    bad += ln * ld != slowlmul(ln, ld);
    qn = (unsigned long long)next(seed) << 40 ^ (unsigned long long)next(seed) << 17 ^ next(seed);
    qd = (unsigned long long)next(seed) << (next(seed) & 31) ^ next(seed) >> 3;
    if (qd == 0)
        qd = 3;
    qq = qn / qd;
    qr = qn % qd;
    bad += qq * qd + qr != qn || qr >= qd;
    return bad;
}

void on_vblank(void)
{
    int i;

    asm("in0 a,(9Ah)\n"
        "or a,2\n"
        "out0 (9Ah),a");
    frames++;
    for (i = 0; i < 3; i++)
        int_bad += round_of(&hseed);
}

int main(void)
{
    unsigned long seed;

    seed = 7;
    if (mos_set_interrupt_handler(0x32, on_vblank) != 0) {
        check(0, 1);
        return finish();
    }
    while (frames < 200 && main_rounds < 20000L) {
        main_bad += round_of(&seed);
        main_rounds++;
    }
    mos_set_interrupt_handler(0x32, 0);
    check(frames >= 200, 1);            /* the handler really ran, often */
    check(main_bad == 0, 1);
    check(int_bad == 0, 1);
    return finish();
}
