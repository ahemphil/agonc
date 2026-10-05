/* signal.h - signals (C89 4.7; c89_spec.md 15), implemented in exit.c.
 * MOS has none: raise and abort generate them, and Ctrl-C raises SIGINT
 * at the library's next I/O or MOS call. A signal's default action ends
 * the program with status 128 plus its number, closing the streams
 * (flushed, except for SIGABRT). */

#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_ERR ((void (*)(int))-1)
#define SIG_IGN ((void (*)(int))1)

#define SIGINT 2
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15

void (*signal(int sig, void (*func)(int)))(int);
int raise(int sig);

#endif
