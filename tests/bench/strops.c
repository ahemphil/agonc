/* strops: the library's string functions and a hand-written byte loop. */
#include <string.h>
#include "bench.h"

static char a[600];
static char b[600];

/* a character loop the way programs write their own */
static int count_char(const char *s, int c)
{
    int n;

    n = 0;
    while (*s)
        if (*s++ == c)
            n++;
    return n;
}

int main(void)
{
    int i;
    int j;
    unsigned long sum;

    bench_start();
    sum = 0;
    for (i = 0; i < 100; i++) {
        memset(a, 'x', sizeof a - 1);
        a[sizeof a - 1] = 0;
        for (j = 0; j < 500; j += 7)
            a[j] = (char)('a' + (i + j) % 26);
        strcpy(b, a);
        sum += (unsigned long)strlen(b);
        sum += (unsigned long)(strcmp(a, b) == 0);
        b[300] = 'z';
        sum += (unsigned long)(strcmp(a, b) < 0);
        sum += (unsigned long)(strchr(a, 'q') != NULL);
        memcpy(b, a + 100, 400);
        sum += (unsigned long)count_char(b, 'x');
        sum += (unsigned long)count_char(a, 'a' + i % 26);
    }
    return bench_end("strops", sum);
}
