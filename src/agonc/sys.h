/* sys.h - what the driver needs from the machine it runs on.
 *
 * driver.md section 1: one driver, two back ends, in sys.c. On the Agon
 * (compiled by our own compiler, which defines __AGONC__) the driver is a
 * moslet that loads each pass to 0x040000 and calls it; on the host it is
 * an ordinary program that runs each pass as a child process. Everything
 * else in agonc.c is shared.
 *
 * The interface is small and string-based (no FILE, no heap, no struct):
 * the Agon back end sits directly on MOS's calls (agon/mos.h), the host
 * one on stdio, and agonc.c never knows which it has. Every path is a
 * plain C string; "/lib" and the like are made by agonc.c from sys_root().
 */

#ifndef SYS_H
#define SYS_H

/* Called first, with the driver's own argv[0]: the host back end takes
 * the directory the passes are in from it; the Agon one installs its
 * Ctrl-C handler. */
void sys_init(char *argv0);

/* Text to the console: sys_out for results, sys_err for diagnostics. */
void sys_out(char *s);
void sys_err(char *s);

/* The prefix of /lib, /usrlib and /tmp: "" on the Agon, $AGONC_ROOT on the
 * host (driver.md section 4). */
char *sys_root(void);

/* The path of a pass binary (cpp, cc1, cc2, ld) into buf, and of the
 * assembler. */
void sys_pass(char *buf, char *name);
char *sys_asm(void);

/* Runs prog with the argument string args (which it may modify): 0 if it
 * succeeded, -1 if it could not be run, else its failure status. */
int sys_run(char *prog, char *args);

/* Whether Ctrl-C has stopped the build: pressed while the driver ran, or
 * while a program it ran did, which then ended with status 130
 * (driver.md section 6). Always 0 on the host, where Ctrl-C ends the
 * driver at once. */
int sys_interrupted(void);

/* A clock in hundredths of a second, for -v's timings: it wraps, so only
 * the difference of two readings means anything. On the Agon it is MOS's
 * (sysvar_time); on the host it counts whole seconds. */
unsigned sys_clock(void);

/* The date and time for __DATE__ and __TIME__: "YYYYMMDD" into date and
 * "HHMMSS" into time, or both empty if the clock cannot be read. On the
 * Agon it is MOS's real-time clock; cpp checks the values. */
void sys_date(char *date, char *time);

/* File operations: a file's size (-1 if it cannot be opened); up to max
 * bytes of it into buf (the count, or -1); whether the file holds text or
 * text2 (two strings of one length under 64 that start with the same
 * character, 0 if it cannot be opened); a whole file written from a
 * string (1 on success); remove; make a directory (errors ignored); copy a
 * file to the console (0 if it cannot be opened). */
int sys_size(char *path);
int sys_read(char *path, char *buf, int max);
int sys_find(char *path, char *text, char *text2);
int sys_write(char *path, char *text);
void sys_remove(char *path);
void sys_mkdir(char *path);
int sys_cat(char *path);

#endif
