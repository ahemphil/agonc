/* ll: long long arithmetic - a 64-bit linear congruential generator,
 * shifts, and division. */
#include "bench.h"

int main(void)
{
    unsigned long long x;
    unsigned long long d;
    int i;

    bench_start();
    x = 1;
    d = 0;
    for (i = 0; i < 120; i++) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        d += x >> 33;
        d ^= x / 1000003ULL % 65536ULL;
    }
    return bench_end("ll", (unsigned long)(d ^ d >> 32));
}
