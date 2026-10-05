/* matmul: two-dimensional arrays, int and long multiplication. */
#include "bench.h"

#define N 14

static int a[N][N];
static int b[N][N];
static int c[N][N];
static long la[N][N];
static long lc[N][N];

int main(void)
{
    int i;
    int j;
    int k;
    int r;
    int s;
    long ls;
    unsigned long sum;

    bench_start();
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            a[i][j] = (i * 3 + j) % 10;
            b[i][j] = (i + j * 7) % 10;
            la[i][j] = (long)(i * 1000 + j) * 1000L;
        }
    sum = 0;
    for (r = 0; r < 3; r++) {
        for (i = 0; i < N; i++)
            for (j = 0; j < N; j++) {
                s = 0;
                for (k = 0; k < N; k++)
                    s += a[i][k] * b[k][j];
                c[i][j] = s;
            }
        for (i = 0; i < N; i++)
            for (j = 0; j < N; j++) {
                ls = 0;
                for (k = 0; k < N; k++)
                    ls += la[i][k] / 1000L * (long)b[k][j];
                lc[i][j] = ls;
            }
        for (i = 0; i < N; i++)
            for (j = 0; j < N; j++)
                sum = (sum + (unsigned long)c[i][j] + (unsigned long)lc[i][j]) & 0xFFFFFFFFUL;
        a[r][r]++;
    }
    return bench_end("matmul", sum);
}
