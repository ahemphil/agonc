/* interp: a small stack-machine interpreter - a switch in a loop, the
 * shape of many real programs' inner loops. */
#include "bench.h"

enum { PUSH, ADD, MUL, DUP, OVER, SWAP, DEC, DROP, JNZ, HALT };

/* the sum of i*i for i = n down to 1, n (at least 1) on the stack */
static const int code[] = {
    PUSH, 0,        /* 0:  n acc */
    OVER,           /* 2:  n acc n */
    DUP,            /* 3:  n acc n n */
    MUL,            /* 4:  n acc n*n */
    ADD,            /* 5:  n acc' */
    SWAP,           /* 6:  acc' n */
    DEC,            /* 7:  acc' n-1 */
    DUP,            /* 8:  acc' n-1 n-1 */
    JNZ, 13,        /* 9:  acc' n-1, on to 13 unless n-1 is 0 */
    DROP,           /* 11: acc' */
    HALT,           /* 12 */
    SWAP,           /* 13: n-1 acc' */
    PUSH, 1,        /* 14 */
    JNZ, 2          /* 16: always */
};

static long stack[16];

static long run(long n)
{
    const int *pc;
    long *sp;
    long t;

    pc = code;
    sp = stack;
    *sp++ = n;
    for (;;) {
        switch (*pc++) {
        case PUSH: *sp++ = *pc++; break;
        case ADD: sp--; sp[-1] += sp[0]; break;
        case MUL: sp--; sp[-1] *= sp[0]; break;
        case DUP: sp[0] = sp[-1]; sp++; break;
        case OVER: sp[0] = sp[-2]; sp++; break;
        case SWAP: t = sp[-1]; sp[-1] = sp[-2]; sp[-2] = t; break;
        case DEC: sp[-1]--; break;
        case DROP: sp--; break;
        case JNZ:
            sp--;
            if (sp[0] != 0)
                pc = code + *pc;
            else
                pc++;
            break;
        default:
            return sp[-1];
        }
    }
}

int main(void)
{
    int i;
    unsigned long sum;

    bench_start();
    sum = 0;
    for (i = 0; i < 400; i++)
        sum = (sum * 7UL + (unsigned long)run(20 + i % 5)) & 0xFFFFFFFFUL;
    return bench_end("interp", sum);
}
