/* sys.c - the driver's two back ends; see sys.h.
 *
 * Exactly one half is compiled: the Agon back end when our own compiler
 * builds the driver (it predefines __AGONC__), the host back end when
 * the host's C compiler does. Each half implements every function in
 * sys.h.
 *
 * Map: the Agon (running a pass in place, console, clock, MOS file
 * calls), then the host (child processes through system(), stdio).
 */

#ifdef __AGONC__

/* ---- the Agon: a moslet ----------------------------------------------------
 *
 * The driver is linked for the moslet area (ld --moslet), so a pass loaded
 * into user RAM can use all of it, and the pass's crt0 saves and restores
 * the driver's stack pointer around its own stack. MOS does not close a
 * program's files when it exits, and it has only a few handles, so after
 * every pass the driver closes them all.
 */

#include <string.h>
#include <signal.h>
#include <agon/mos.h>
#include "sys.h"

void __kb_on(void);

#define LOAD_AT 0x040000
#define LOAD_MAX 0x070000       /* user RAM; a larger file would reach the driver */

/* n bytes at p to the VDP, in one MOS call: RST 18h writes the block at
 * HL, BC bytes long. p and n are the first two argument slots, (ix+6)
 * and (ix+9) (abi.md). */
static void vdu(char *p, int n)
{
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix+9)\n"
        "rst.lis 18h");
}

/* Calls the program loaded at LOAD_AT the way MOS does: A = MB (0), DE =
 * its address, HL = the argument string; the status comes back in HL. IX
 * and IY are saved here in case it does not preserve them. args is the
 * first argument slot (ix+6); the status is stored into r, the first
 * local (ix-3). The program runs on its own stack and returns here
 * through its crt0's exit path, which reloads the stack pointer saved on
 * entry, so the driver's frame survives however the program ends. */
static int call_loaded(char *args)
{
    int r;

    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "push iy\n"
        "ld de,0x040000\n"
        "xor a\n"
        "call 0x040000\n"
        "pop iy\n"
        "pop ix\n"
        "ld (ix-3),hl");
    return r;
}

static int interrupted;         /* Ctrl-C seen: sys_interrupted's answer */

/* Ctrl-C while the driver itself runs: noted, and the build stops after
 * the program running now. A handler's disposition goes back to SIG_DFL
 * when it is called, so it installs itself again. */
static void on_interrupt(int sig)
{
    interrupted = 1;
    signal(SIGINT, on_interrupt);
}

void sys_init(char *argv0)
{
    signal(SIGINT, on_interrupt);
}

int sys_interrupted(void)
{
    return interrupted;
}

/* The screen wants CR LF for a new line: each run of text up to a '\n'
 * goes out as one block, then CR LF in place of the '\n'. sys_err is the
 * same, since the Agon has one console. */
void sys_out(char *s)
{
    int n;

    while (*s) {
        n = 0;
        while (s[n] && s[n] != '\n')
            n++;
        if (n > 0)
            vdu(s, n);
        s = s + n;
        if (*s == '\n') {
            vdu("\r\n", 2);
            s++;
        }
    }
}

void sys_err(char *s)
{
    sys_out(s);
}

char *sys_root(void)
{
    return "";
}

void sys_pass(char *buf, char *name)
{
    strcpy(buf, "/bin/agonc/");
    strcat(buf, name);
    strcat(buf, ".bin");
}

char *sys_asm(void)
{
    return "/bin/ez80asm.bin";
}

/* Load prog over user RAM and call it. Afterwards: the keyboard vector
 * goes back to the driver's Ctrl-C handler, every file the program left
 * open is closed (mos_fclose(0) closes all), and a status of 130 says
 * the program stopped at Ctrl-C. args is passed as the program's command
 * line, which its start-up code may split in place. */
int sys_run(char *prog, char *args)
{
    int r;

    if (mos_load(prog, (void *)LOAD_AT, LOAD_MAX) != 0)
        return -1;
    r = call_loaded(args);
    __kb_on();                          /* the program's __exit removed the driver's */
    mos_fclose(0);
    if (r == 130)
        interrupted = 1;                /* the program was stopped by Ctrl-C */
    return r;
}

/* sysvar_time, at offset 0 of MOS's system variables, counts hundredths
 * of a second since start-up; reading it as an unsigned takes its low 24
 * bits, which wrap (about every 46 hours) harmlessly for a difference. */
unsigned sys_clock(void)
{
    return *(unsigned *)mos_sysvars();
}

/* mos_getrtc's text is "Sun, 27/09/2026 10:54:26": the six numbers are
 * taken in that order, day first. Each run of digits is one number; text
 * with fewer than six, or a year beyond four digits, leaves both strings
 * empty. The range of each field is left to cpp. */
void sys_date(char *date, char *time)
{
    char buf[64];
    int v[6];
    int n;
    int i;
    char *p;

    for (i = 0; i < 64; i++)
        buf[i] = 0;
    mos_getrtc(buf);
    date[0] = 0;
    time[0] = 0;
    n = 0;
    for (p = buf; *p && n < 6; p++) {
        if (*p >= '0' && *p <= '9') {
            v[n] = 0;
            while (*p >= '0' && *p <= '9') {
                v[n] = v[n] * 10 + (*p - '0');
                p++;
            }
            n++;
            p--;                        /* the for loop's p++ steps past the number */
        }
    }
    if (n < 6 || v[2] > 9999)
        return;
    /* v: day, month, year, hour, minute, second; date is year month day */
    date[0] = (char)('0' + v[2] / 1000);
    date[1] = (char)('0' + v[2] / 100 % 10);
    date[2] = (char)('0' + v[2] / 10 % 10);
    date[3] = (char)('0' + v[2] % 10);
    date[4] = (char)('0' + v[1] / 10 % 10);
    date[5] = (char)('0' + v[1] % 10);
    date[6] = (char)('0' + v[0] / 10 % 10);
    date[7] = (char)('0' + v[0] % 10);
    date[8] = 0;
    for (i = 0; i < 3; i++) {
        time[2 * i] = (char)('0' + v[3 + i] / 10 % 10);
        time[2 * i + 1] = (char)('0' + v[3 + i] % 10);
    }
    time[6] = 0;
}

int sys_size(char *path)
{
    return mos_fsize(path);
}

int sys_read(char *path, char *buf, int max)
{
    int h;
    int n;

    h = mos_fopen(path, FA_READ);
    if (h == 0)
        return -1;
    n = mos_fread(h, buf, max);
    mos_fclose(h);
    return n;
}

int sys_write(char *path, char *text)
{
    int h;
    int n;
    int len;

    h = mos_fopen(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (h == 0)
        return 0;
    len = strlen(text);
    n = mos_fwrite(h, text, len);
    mos_fclose(h);
    return n == len;
}

/* Does the file hold text (under 64 characters)? Read a block at a time,
 * each after the last characters of the one before, which a match could
 * start in. text2, of the same length and first character, is tried
 * wherever text's first character matches; the driver uses this to look
 * for either of two ;;ref lines in one reading of a .s file. */
int sys_find(char *path, char *text, char *text2)
{
    char buf[256 + 64];
    int h;
    int n;
    int i;
    int keep;
    int len;

    h = mos_fopen(path, FA_READ);
    if (h == 0)
        return 0;
    len = strlen(text);
    keep = 0;
    for (;;) {
        n = mos_fread(h, buf + keep, 256);
        if (n <= 0)
            break;
        n = n + keep;
        for (i = 0; i + len <= n; i++) {
            if (buf[i] == text[0] && (memcmp(buf + i, text, len) == 0 || memcmp(buf + i, text2, len) == 0)) {
                mos_fclose(h);
                return 1;
            }
        }
        /* the last len - 1 bytes, where a match ending in the next block
         * could start, move to the front */
        keep = len - 1 < n ? len - 1 : n;
        memmove(buf, buf + n - keep, keep);
    }
    mos_fclose(h);
    return 0;
}

void sys_remove(char *path)
{
    mos_del(path);
}

void sys_mkdir(char *path)
{
    mos_mkdir(path);
}

int sys_cat(char *path)
{
    char buf[129];
    int h;
    int n;

    h = mos_fopen(path, FA_READ);
    if (h == 0)
        return 0;
    for (;;) {
        n = mos_fread(h, buf, 128);
        if (n <= 0)
            break;
        buf[n] = 0;
        sys_out(buf);
    }
    mos_fclose(h);
    return 1;
}

#else

/* ---- the host: an ordinary program ----------------------------------------
 *
 * Passes are found beside the driver, the assembler on the PATH, and the
 * library layout under $AGONC_ROOT, whose tmp/ must exist.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "io.h"
#include "sys.h"

#define PATH_SIZE 260
#define LINE_SIZE 2400

static char pass_dir[PATH_SIZE];        /* the driver's directory, with its separator */
static char line[LINE_SIZE];            /* the command system() runs */

int sys_interrupted(void)
{
    return 0;
}

/* pass_dir: argv0 up to its last / or \ (empty if it has none, or if it
 * is too long, when the passes are looked for on the PATH instead). */
void sys_init(char *argv0)
{
    int i;
    int end;

    end = 0;
    for (i = 0; argv0[i]; i++)
        if (argv0[i] == '/' || argv0[i] == '\\')
            end = i + 1;
    if (end > PATH_SIZE - 16)
        end = 0;
    for (i = 0; i < end; i++) {
        pass_dir[i] = argv0[i];
#ifdef _WIN32
        if (pass_dir[i] == '/')         /* cmd.exe takes / in a command name for a switch */
            pass_dir[i] = '\\';
#endif
    }
    pass_dir[end] = 0;
}

void sys_out(char *s)
{
    out_str(stdout, s);
}

void sys_err(char *s)
{
    out_str(stderr, s);
}

char *sys_root(void)
{
    char *r;

    r = getenv("AGONC_ROOT");
    return r == NULL ? "" : r;
}

void sys_pass(char *buf, char *name)
{
    strcpy(buf, pass_dir);
    strcat(buf, name);
#ifdef _WIN32
    strcat(buf, ".exe");
#endif
}

char *sys_asm(void)
{
    return "ez80asm";
}

/* time() counts seconds, so -time's figures on the host are whole
 * seconds, scaled to the Agon's hundredths. */
unsigned sys_clock(void)
{
    return (unsigned)time(NULL) * 100;
}

/* The local time, in the same YYYYMMDD and HHMMSS forms as the Agon's. */
void sys_date(char *date, char *time_of_day)
{
    time_t now;
    struct tm *t;

    date[0] = 0;
    time_of_day[0] = 0;
    now = time(NULL);
    t = localtime(&now);
    if (t == NULL)
        return;
    sprintf(date, "%04d%02d%02d", (t->tm_year + 1900) % 10000, t->tm_mon + 1, t->tm_mday);
    sprintf(time_of_day, "%02d%02d%02d", t->tm_hour, t->tm_min, t->tm_sec);
}

/* Through the shell, as "prog args". The driver's own buffered output is
 * flushed first so that it comes before the child's. Any non-zero result
 * from system() counts as the passes' failure status, 200. */
int sys_run(char *prog, char *args)
{
    if (strlen(prog) + strlen(args) + 2 > LINE_SIZE)
        return -1;
    strcpy(line, prog);
    strcat(line, " ");
    strcat(line, args);
    fflush(stdout);
    fflush(stderr);
    return system(line) == 0 ? 0 : 200;
}

int sys_size(char *path)
{
    FILE *f;
    int n;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

int sys_read(char *path, char *buf, int max)
{
    FILE *f;
    int n;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    n = fread(buf, 1, max, f);
    fclose(f);
    return n;
}

int sys_write(char *path, char *text)
{
    FILE *f;
    int ok;

    f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    out_str(f, text);
    ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

/* Does the file hold text (under 64 characters)? Read a block at a time,
 * each after the last characters of the one before, which a match could
 * start in. */
int sys_find(char *path, char *text, char *text2)
{
    char buf[256 + 64];
    FILE *f;
    int n;
    int i;
    int keep;
    int len;

    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    len = strlen(text);
    keep = 0;
    for (;;) {
        n = (int)fread(buf + keep, 1, 256, f);
        if (n <= 0)
            break;
        n = n + keep;
        for (i = 0; i + len <= n; i++) {
            if (buf[i] == text[0] && (memcmp(buf + i, text, len) == 0 || memcmp(buf + i, text2, len) == 0)) {
                fclose(f);
                return 1;
            }
        }
        /* the last len - 1 bytes, where a match ending in the next block
         * could start, move to the front */
        keep = len - 1 < n ? len - 1 : n;
        memmove(buf, buf + n - keep, keep);
    }
    fclose(f);
    return 0;
}

void sys_remove(char *path)
{
    remove(path);
}

/* Nothing: on the host $AGONC_ROOT/tmp must exist already. */
void sys_mkdir(char *path)
{
}

int sys_cat(char *path)
{
    FILE *f;
    char buf[256];
    size_t len;

    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    while ((len = fread(buf, 1, sizeof buf, f)) > 0)
        fwrite(buf, 1, len, stdout);
    fclose(f);
    return 1;
}

#endif
