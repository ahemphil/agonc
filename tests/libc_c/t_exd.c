/* t_exd.c - E test (test_libc_c.py): exit(3) from ten calls deep, after
 * writing e_ex.txt, which exit closes. */

#include <stdio.h>
#include <stdlib.h>

void deep(int n)
{
    if (n == 0)
        exit(3);
    deep(n - 1);
}

int main(void)
{
    FILE *f;

    f = fopen("/out/e_ex.txt", "w");
    fputs("deep", f);
    deep(10);
    return 1;
}
