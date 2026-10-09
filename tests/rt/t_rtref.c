/* t_rtref.c - rt.s's multiply and divide helpers against their C
 * reference (lib/rt/rt_ref.c), on the emulator. rtref_gen.c, run on the
 * PC, wrote the cases and the reference's answers into rtref_cases.h;
 * here the same operations are done with C's operators, which compile to
 * calls of __imul, __idivu, __iremu, __idivs, __irems, __lmul, __ldivu,
 * __lremu, __ldivs and __lrems. The operands come from the table at run
 * time, so nothing is folded. Exits 0 if every row agrees, else the
 * number of the first row that does not (the 24-bit rows first, then the
 * 32-bit ones), at most 250. Division by zero is in the 24-bit rows
 * only: __idivu keeps an answer for it (rt_ref.c says which).
 */

#include <agon/mos.h>
#include "rtref_cases.h"

int first;

void fail(int row)
{
    if (first == 0)
        first = row < 250 ? row : 250;
}

int main(void)
{
    int i;
    unsigned un;
    unsigned ud;
    int sn;
    int sd;
    unsigned long ln;
    unsigned long ld;
    const unsigned long *w;

    for (i = 0; i < N24; i++) {
        w = rows24[i];
        un = (unsigned)w[0];
        ud = (unsigned)w[1];
        sn = (int)un;
        sd = (int)ud;
        if (un * ud != (unsigned)w[2] || un / ud != (unsigned)w[3] || un % ud != (unsigned)w[4]
            || (unsigned)(sn / sd) != (unsigned)w[5] || (unsigned)(sn % sd) != (unsigned)w[6])
            fail(i + 1);
    }
    for (i = 0; i < N32; i++) {
        w = rows32[i];
        ln = w[0];
        ld = w[1];
        if (ln * ld != w[2] || ln / ld != w[3] || ln % ld != w[4]
            || (unsigned long)((long)ln / (long)ld) != w[5] || (unsigned long)((long)ln % (long)ld) != w[6])
            fail(N24 + i + 1);
    }
    agon_emu_exit(first);
    return 0;
}
