/* exit.c - how a program ends: exit and abort (<stdlib.h>), signal and
 * raise (<signal.h>).
 *
 * exit reaches the atexit functions (stdlib.c) and the streams (stdio.c)
 * through weak references (c89_spec.md 13 item 6), so a program that uses
 * neither does not link them: each is brought in by the table it works on,
 * whose first member points at it. A #pragma weak covers its whole unit,
 * which is why these functions are not in stdlib.c beside atexit.
 *
 * Every way out ends at crt0's __exit, which restores the stack MOS gave
 * the program (abi.md 9); main's return is exit's call.
 *
 * Closing the streams on the way out matters more here than on most
 * systems: MOS does not close a program's files when it ends, and has only
 * eight handles, so a file left open stays lost until the machine is
 * reset. Even abort and a signal's default action close every file,
 * though abort does not write out what the buffers hold.
 *
 * The three ways out:
 *   exit(status)      atexit functions, streams flushed and closed, status
 *   abort()           SIGABRT, then streams closed unflushed, status 134
 *   a signal's default action
 *                     streams closed (flushed unless SIGABRT), 128 + number
 */

#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#pragma weak __atexit_run
#pragma weak __stdio_exit

void __atexit_run(void);
void __stdio_exit(int flush);

/* Signal dispositions, by signal number (the largest is SIGTERM, 15);
 * SIG_DFL is 0, so the zeroed bss starts every signal at its default. */
static void (*handlers[16])(int);

/* ---- leaving -------------------------------------------------------------- */

/* leave jumps to crt0's __exit with the status in HL (abi.md 9). It is a
 * jp, not a call: __exit restores MOS's stack pointer and returns to MOS,
 * so nothing comes back here. */
#ifdef __AGONC_TEST
/* A test build records the status in a file (stdio.c) and always returns
 * 0, so a failing test does not stop the batch script it runs in. */
void __test_status(int status);

static void leave(int status)
{
    __test_status(status);
    asm("        ld      hl, 0\n        jp      __exit");
}
#else
/* (ix+6) is status, the first parameter's slot (abi.md 5). */
static void leave(int status)
{
    asm("        ld      hl, (ix+6)\n        jp      __exit");
}
#endif

/* The streams closed (their buffers written first if flush), then back
 * to MOS with status. */
static void end(int status, int flush)
{
    if (__stdio_exit)
        __stdio_exit(flush);
    leave(status);
}

/* C89 4.10.4.3: the atexit functions (if any were registered: the weak
 * reference is 0 otherwise), then the streams flushed and closed. */
void exit(int status)
{
    if (__atexit_run)
        __atexit_run();
    end(status, 1);
}

/* SIGABRT's handler, if any, runs first; if it returns, or the signal is
 * ignored, the program still ends (C89 4.10.4.1): the files are closed,
 * without writing what their buffers hold, and the status is 134. */
void abort(void)
{
    raise(SIGABRT);
    end(128 + SIGABRT, 0);
}

/* ---- signals -------------------------------------------------------------- */

/* The six signals C89 defines, the only ones signal and raise accept. */
static int valid(int sig)
{
    return sig == SIGINT || sig == SIGILL || sig == SIGABRT || sig == SIGFPE || sig == SIGSEGV
           || sig == SIGTERM;
}

void (*signal(int sig, void (*func)(int)))(int)
{
    void (*old)(int);

    if (!valid(sig)) {
        errno = EINVAL;
        return SIG_ERR;
    }
    old = handlers[sig];
    handlers[sig] = func;
    return old;
}

extern char __intflag;

/* Ctrl-C (c89_spec.md 15): crt0's keyboard handler sets __intflag from the
 * interrupt, and the library calls this at its next I/O or MOS call, where
 * raising SIGINT is safe. The default action closes the streams and ends
 * the program with 130; a handler that returns lets the program go on.
 * Nothing is raised from the interrupt itself, where the program could be
 * anywhere, even inside malloc or a stream's own bookkeeping. */
void __interrupted(void)
{
    __intflag = 0;
    raise(SIGINT);
}

/* A handler's disposition goes back to SIG_DFL before it is called (C89
 * 4.7.1.1). The default action ends the program with 128 plus the number. */
int raise(int sig)
{
    void (*h)(int);

    if (!valid(sig))
        return -1;
    h = handlers[sig];
    if (h == SIG_IGN)
        return 0;
    /* the default action, which does not return; the atexit functions do
     * not run */
    if (h == SIG_DFL)
        end(128 + sig, sig != SIGABRT);
    handlers[sig] = SIG_DFL;
    h(sig);
    return 0;
}
