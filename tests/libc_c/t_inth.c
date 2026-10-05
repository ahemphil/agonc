/* t_inth.c - C test (test_libc_c.py): Ctrl-C with a SIGINT handler that
 * returns: the handler runs once, with SIGINT, and the program goes on
 * (status 5; 1 if the handler saw something else). */

#include <stdio.h>
#include <signal.h>

int caught;
int sig_seen;

void on_int(int sig)
{
    caught++;
    sig_seen = sig;
}

int main(void)
{
    FILE *f;

    signal(SIGINT, on_int);
    f = fopen("/out/inth.txt", "w");
    fclose(fopen("/out/m_h", "w"));
    while (caught == 0)
        fputs("line\n", f);
    fclose(f);
    return caught == 1 && sig_seen == SIGINT ? 5 : 1;
}
