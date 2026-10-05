/* t_term.c - signals, non-local jumps and atexit's limit (M12 step 1):
 * signal and raise (C89 4.7), setjmp and longjmp (4.6), abort caught by a
 * handler that does not return (4.10.4.1), atexit's 32 (4.10.4.2). How a
 * program actually ends (the atexit functions run, streams closed, the
 * statuses) is test_libc_c.py's E test. */

#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <errno.h>
#include "check.h"

int caught;
int caught_sig;
jmp_buf env;
int depth;

void on_sig(int sig)
{
    caught++;
    caught_sig = sig;
}

void again(int sig)
{
    caught++;
    signal(sig, again);         /* reset to SIG_DFL before the call: re-install */
}

void jump_out(int sig)
{
    longjmp(env, sig);
}

void nop(void)
{
}

void dive(int n, int val)
{
    depth = n;
    if (n == 0)
        longjmp(env, val);
    dive(n - 1, val);
}

int main(void)
{
    int local;
    int r;
    int i;
    int n;

    /* signal and raise */
    check(signal(SIGINT, on_sig) == SIG_DFL, 1);        /* the initial disposition */
    check(raise(SIGINT), 0);
    check(caught, 1);
    check(caught_sig, SIGINT);
    check(signal(SIGINT, on_sig) == SIG_DFL, 1);        /* reset before the handler ran */
    check(signal(SIGINT, SIG_IGN) == on_sig, 1);
    check(raise(SIGINT), 0);
    check(caught, 1);                                   /* ignored */
    check(signal(SIGINT, SIG_DFL) == SIG_IGN, 1);
    signal(SIGTERM, again);
    raise(SIGTERM);
    raise(SIGTERM);
    check(caught, 3);
    errno = 0;
    check(signal(99, on_sig) == SIG_ERR, 1);
    check(errno, EINVAL);
    check(signal(0, on_sig) == SIG_ERR, 1);
    check(raise(99) != 0, 1);
    check(SIGINT, 2);
    check(SIGILL, 4);
    check(SIGABRT, 6);
    check(SIGFPE, 8);
    check(SIGSEGV, 11);
    check(SIGTERM, 15);

    /* setjmp and longjmp: from ten calls deep, with a local kept */
    local = 5;
    r = setjmp(env);
    if (r == 0) {
        local = 6;
        dive(10, 42);
        check(0, 1);                                    /* not reached */
    }
    check(r, 42);
    check(local, 6);
    check(depth, 0);
    r = setjmp(env);
    if (r == 0)
        longjmp(env, 0);
    check(r, 1);                                        /* longjmp(env, 0) returns 1 */
    i = 0;
    r = setjmp(env);                                    /* a loop through setjmp */
    i++;
    if (i < 5)
        longjmp(env, i);
    check(r, 4);
    check(i, 5);

    /* abort, with a SIGABRT handler that jumps out */
    signal(SIGABRT, jump_out);
    r = setjmp(env);
    if (r == 0) {
        abort();
        check(0, 1);                                    /* not reached */
    }
    check(r, SIGABRT);
    check(signal(SIGABRT, SIG_DFL) == SIG_DFL, 1);      /* reset when it ran */

    /* atexit: room for 32 functions */
    n = 0;
    for (i = 0; i < 32; i++)
        if (atexit(nop) == 0)
            n++;
    check(n, 32);
    check(atexit(nop) != 0, 1);
    return finish();
}
