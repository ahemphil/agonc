/* fp: double arithmetic, division and sqrt (the software floating point). */
#include <math.h>
#include "bench.h"

int main(void)
{
    double s;
    double x;
    int k;

    bench_start();
    s = 0.0;
    for (k = 1; k <= 100; k++) {
        x = (double)k;
        s += 1.0 / (x * x);
        s += sqrt(x) * 0.001;
    }
    return bench_end("fp", (unsigned long)(s * 1000000.0));
}
