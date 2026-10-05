/* t_sig.c - E test (test_libc_c.py): SIGTERM's default action ends with
 * status 143 (128 + 15), after writing and closing e_rs.txt. */

#include <stdio.h>
#include <signal.h>

int main(void)
{
    FILE *f;

    f = fopen("/out/e_rs.txt", "w");
    fputs("kept", f);
    raise(SIGTERM);
    return 1;
}
