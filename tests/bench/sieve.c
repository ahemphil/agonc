/* sieve: the classic byte sieve (BYTE, 1981): array indexing and loops. */
#include "bench.h"

#define SIZE 8190

static char flags[SIZE + 1];

int main(void)
{
    int i;
    int k;
    int prime;
    int count;
    int iter;
    unsigned long sum;

    bench_start();
    sum = 0;
    for (iter = 0; iter < 10; iter++) {
        count = 0;
        for (i = 0; i <= SIZE; i++)
            flags[i] = 1;
        for (i = 0; i <= SIZE; i++) {
            if (flags[i]) {
                prime = i + i + 3;
                for (k = i + prime; k <= SIZE; k += prime)
                    flags[k] = 0;
                count++;
            }
        }
        sum += (unsigned long)count;
    }
    return bench_end("sieve", sum);
}
