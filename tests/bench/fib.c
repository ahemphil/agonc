/* fib: recursive calls, the cost of a call and its frame. */
#include "bench.h"

static int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(void)
{
    unsigned long sum;
    int i;

    bench_start();
    sum = 0;
    for (i = 0; i < 6; i++)
        sum += (unsigned long)fib(21 + i % 2);
    return bench_end("fib", sum);
}
