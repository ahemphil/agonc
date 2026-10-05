/* sort: qsort through a compare function, and a shell sort in place. */
#include <stdlib.h>
#include "bench.h"

#define N 800

static int v[N];
static int w[N];
static unsigned long seed;

static int next(void)
{
    seed = (seed * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (int)(seed >> 16 & 0x7FFF);
}

static int cmp(const void *p, const void *q)
{
    int x;
    int y;

    x = *(const int *)p;
    y = *(const int *)q;
    return x < y ? -1 : x > y;
}

static void shell(int *s, int n)
{
    int gap;
    int i;
    int j;
    int t;

    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            t = s[i];
            for (j = i; j >= gap && s[j - gap] > t; j -= gap)
                s[j] = s[j - gap];
            s[j] = t;
        }
}

int main(void)
{
    int i;
    int round;
    unsigned long sum;

    bench_start();
    sum = 0;
    seed = 1;
    for (round = 0; round < 2; round++) {
        for (i = 0; i < N; i++)
            v[i] = w[i] = next();
        qsort(v, N, sizeof v[0], cmp);
        shell(w, N);
        for (i = 0; i < N; i++)
            sum = (sum * 31UL + (unsigned long)v[i] + (unsigned long)(v[i] == w[i])) & 0xFFFFFFFFUL;
    }
    return bench_end("sort", sum);
}
