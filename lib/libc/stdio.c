/* stdio.c - buffered I/O: <stdio.h> (C89 4.9; c89_spec.md 15).
 *
 * stdio sits between the program and two MOS services: the file API (MOS
 * handles, reached through lib/agon/mos.c) and the console (RST 18h to
 * write, the line editor to read). A FILE is one of eleven static slots:
 * stdin, stdout, stderr and eight files (MOS has eight handles). A stream
 * gets its buffer (BUFSIZ for a file, or setvbuf's; CON_IN and CON_OUT
 * for the console) from malloc on first use.
 *
 * The buffer holds either bytes read from the file at __base (state
 * S_READ, up to __end) or bytes to be written there (S_WRITE, up to
 * __ptr); S_NONE is neither, with the stream's position at __base. getc
 * and putc (stdio.h) work on the buffer inline while __ptr is short of
 * __rend or __wend, which limits() sets: a text stream's __rend stops at
 * the next CR, so fgetc sees every CR and turns CR LF into '\n', and a
 * text stream's '\n' becomes CR LF only when the buffer is written
 * (flush). Only a fully buffered file stream has a __wend; on any other
 * every character goes through fputc, which sees each newline.
 *
 * MOS 2.x has no "tell", so a stream tracks positions itself: __base, and
 * __mospos, where MOS's own file pointer is. A seek only moves __base;
 * the MOS seek happens when bytes are next read or written somewhere
 * other than __mospos. An append stream writes at __fsize, the file's
 * end as far as the stream knows.
 *
 * Every buffer written or refilled is an interruption point: a Ctrl-C
 * pressed since the last one raises SIGINT there (exit.c's __interrupted;
 * c89_spec.md 15), so a program writing or reading stops within a
 * buffer's worth, and one waiting at the console as soon as it gets its
 * line.
 *
 * MOS does not close a program's files when it ends, so exit, abort and
 * a signal's default action all reach __stdio_exit, which closes every
 * stream; a handle left open would be lost until the machine is reset.
 *
 * The sections: the console; buffering (the state machine above: limits,
 * flush, fill, put); opening and closing; temporary files; characters and
 * lines; blocks; state; positioning; the printf family; the scanf family.
 * tests/libc_c/t_stdio.c defines the behaviour.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <agon/mos.h>

/* this file defines C89's scanf family, which the header's names mean
 * only in strict mode, as well as C99's */
#undef scanf
#undef fscanf
#undef sscanf

/* The floating-point conversions (fp.c), which a function with a float
 * or double value brings into a program (ir_format.md 3, R): reached
 * through weak references, so that a program without floating point
 * links none of that code. */
#pragma weak __fp_print
#pragma weak __fp_scan
void __fp_print(void (*out)(void *, int), void *k, const void *d, int conv, int plus, int alt, int left,
                int zero, int width, int prec);
int __fp_scan(int (*get)(void *), void (*unget)(void *, int), void *src, int width, int size, void *dest,
              int c99);

/* The same for long long's ll conversions (ll.c): a function with a long
 * long value brings them in. */
#pragma weak __ll_print
#pragma weak __ll_scan
int __ll_print(char *digits, const void *v, int base, const char *set, int is_signed);
void __ll_scan(void *q, int base, int digit);

#undef getc
#undef putc
#undef getchar
#undef putchar

extern char __intflag;
void __interrupted(void);

#define POLL() if (__intflag) __interrupted()

#define NFILES (3 + 8)
#define CON_OUT 128             /* the console's buffers */
#define CON_IN 256
#define STAGE 128               /* a text stream's bytes, CR LF expanded, go out in these */

#define K_CLOSED 0              /* __kind */
#define K_FILE 1
#define K_CONOUT 2
#define K_CONIN 3

#define F_READ 1                /* __flags */
#define F_WRITE 2
#define F_APPEND 4              /* every write goes to the end */
#define F_TEXT 8                /* '\n' is CR LF outside */
#define F_EOF 16
#define F_ERR 32
#define F_MYBUF 64              /* __buf is malloc's, freed by fclose */
#define F_UNGOT 128             /* __unget holds ungetc's character */

#define S_NONE 0                /* __state */
#define S_READ 1
#define S_WRITE 2

#define B_FULL _IOFBF           /* __bmode */
#define B_LINE _IOLBF
#define B_NONE _IONBF

void __stdio_exit(int flush_them);

/* The streams. The first member points at __stdio_exit, so a program that
 * uses stdio links it, and exit (exit.c) reaches it through a weak
 * reference. */
static struct {
    void (*exit)(int);
    FILE f[NFILES];
} streams = { __stdio_exit, { { K_CONIN, F_READ, S_NONE, B_LINE, CON_IN },
                              { K_CONOUT, F_WRITE | F_TEXT, S_NONE, B_LINE, CON_OUT },
                              { K_CONOUT, F_WRITE | F_TEXT, S_NONE, B_LINE, CON_OUT } } };

#define files (streams.f)

FILE *stdin = &streams.f[0];
FILE *stdout = &streams.f[1];
FILE *stderr = &streams.f[2];

/* assert's failure (assert.h, C89 4.2.1.1): the message on stderr, then
 * abort. */
void __assert(const char *expr, const char *file, int line)
{
    fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", expr, file, line);
    abort();
}

/* ---- the console (RST 18h and MOS's line editor) ------------------------------ */

/* n bytes at p to the screen: MOS's RST 18h writes BC bytes from HL to the
 * VDP. n is never 0 here, as a zero BC would make it write up to a
 * delimiter instead. p and n are the parameter slots ix+6 and ix+9
 * (abi.md 5). In a test build this is console_rst, beneath the
 * console_write below. */
#ifdef __AGONC_TEST
static void console_rst(const char *p, int n)
#else
static void console_write(const char *p, int n)
#endif
{
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix+9)\n"
        "rst.lis 18h");
}

#ifdef __AGONC_TEST
/* Test builds (tests/tools/testrun.py): the emulator's capture of the screen
 * is unreliable, so console output is appended to the file __test_out
 * names, opened and closed on every write so nothing is lost if the
 * program dies and no handle is left open. __test_status records the exit
 * status beside it, in <name>.st, after calling __test_at_status if the
 * entry point set it (memprofile.py --device's high-water marks). */
char *__test_out;
void (*__test_at_status)(void);

static void console_write(const char *p, int n)
{
    int h;

    if (__test_out == NULL) {
        console_rst(p, n);
        return;
    }
    h = mos_fopen(__test_out, FA_WRITE | FA_OPEN_APPEND);
    if (h != 0) {
        mos_fwrite(h, p, n);
        mos_fclose(h);
    }
}

void __test_status(int status)
{
    char name[80];
    char text[12];
    int h;

    if (__test_out == NULL || strlen(__test_out) > 70)
        return;
    if (__test_at_status != NULL)
        __test_at_status();
    strcpy(name, __test_out);
    strcat(name, ".st");
    sprintf(text, "%d\n", status);
    h = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
    if (h != 0) {
        mos_fwrite(h, text, strlen(text));
        mos_fclose(h);
    }
}
#endif

/* A line from the keyboard into p (size bytes, NUL included) by MOS's line
 * editor, mos_editline (0x09; lib/agon/mos.h): it shows and edits the
 * line until Return or Escape, leaving the text NUL-terminated without
 * the key that ended it. */
static void console_read(char *p, int size)
{
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix+9)\n"
        "ld e,1\n"                      /* start from an empty line, not the buffer's old bytes */
        "ld a,0x09\n"
        "rst.lis 08h");
}

/* ---- buffering ------------------------------------------------------------------- */

/* getc's and putc's limits for the stream's state (see the top). Called
 * whenever the state, the flags or the buffer change, and when a text
 * stream's __ptr moves past a CR (its __rend is the next CR after __ptr).
 * A NULL limit sends every getc or putc to the function: a pointer is
 * never below NULL. */
static void limits(FILE *f)
{
    unsigned char *p;

    f->__rend = NULL;
    f->__wend = NULL;
    if (f->__state == S_READ && !(f->__flags & F_UNGOT)) {
        p = f->__end;
        if (f->__flags & F_TEXT) {
            p = f->__ptr;
            while (p < f->__end && *p != '\r')
                p++;
        }
        f->__rend = p;
    } else if (f->__state == S_WRITE && f->__kind == K_FILE && f->__bmode == B_FULL) {
        f->__wend = f->__buf + f->__size;
    }
}

/* The stream's buffer, from malloc if it has none yet: 1, or 0 with the
 * error flag set if malloc fails. */
static int have_buf(FILE *f)
{
    if (f->__buf == NULL) {
        f->__buf = malloc(f->__size);
        if (f->__buf == NULL) {
            f->__flags = f->__flags | F_ERR;
            return 0;
        }
        f->__flags = f->__flags | F_MYBUF;
        f->__ptr = f->__buf;
    }
    return 1;
}

/* Moves MOS's file pointer to at, if it is not there: 1, or 0 with the
 * error flag set if the seek fails. Tracking __mospos saves a MOS call
 * for every buffer of a file read or written straight through. */
static int mos_at(FILE *f, long at)
{
    if (f->__mospos == at)
        return 1;
    if (mos_flseek(f->__handle, at) != 0) {
        f->__flags = f->__flags | F_ERR;
        return 0;
    }
    f->__mospos = at;
    return 1;
}

/* n bytes to the file or the console; 0, or EOF with the error flag set
 * if fewer were written. A file's __base, __mospos and __fsize follow
 * the bytes MOS took, whether or not that was all of them. */
static int out(FILE *f, unsigned char *p, int n)
{
    int k;

    if (f->__kind != K_FILE) {
        console_write((char *)p, n);
        return 0;
    }
    k = mos_fwrite(f->__handle, p, n);
    f->__mospos = f->__mospos + k;
    f->__base = f->__base + k;
    if (f->__base > f->__fsize)
        f->__fsize = f->__base;
    if (k != n) {
        f->__flags = f->__flags | F_ERR;
        return EOF;
    }
    return 0;
}

/* Writes the bytes a write buffer holds; 0, or EOF on an error. The
 * stream stays in S_WRITE, its buffer empty. In S_WRITE, __end is the first
 * byte not yet written: when Ctrl-C's SIGINT closes the streams from the
 * middle of a flush (a MOS call's interruption point), the flush it makes
 * of this stream carries on from there instead of writing bytes twice. */
static int flush(FILE *f)
{
    unsigned char stage[STAGE];
    unsigned char *p;
    int k;
    int r;

    if (f->__state != S_WRITE || f->__ptr == f->__end)
        return 0;
    POLL();
    if (f->__kind == K_FILE) {
        if (f->__flags & F_APPEND)
            f->__base = f->__fsize;
        if (!mos_at(f, f->__base)) {
            f->__ptr = f->__buf;
            return EOF;
        }
    }
    r = 0;
    if (f->__flags & F_TEXT) {
        /* text: the bytes are copied into stage, each '\n' as CR LF, and
         * stage is written whenever it fills. STAGE - 1 leaves room for a
         * newline's two bytes at the last place. */
        p = f->__end;
        while (p < f->__ptr && r == 0) {
            k = 0;
            while (p < f->__ptr && k < STAGE - 1) {
                if (*p == '\n') {
                    stage[k] = '\r';
                    k++;
                }
                stage[k] = *p;
                k++;
                p++;
            }
            r = out(f, stage, k);
            f->__end = p;
        }
    } else {
        /* binary: the buffer as it is, in one write */
        r = out(f, f->__end, f->__ptr - f->__end);
    }
    f->__ptr = f->__buf;
    f->__end = f->__buf;
    limits(f);
    return r;
}

/* The end of every output call: the console is never left holding text,
 * unless setvbuf made it fully buffered. */
static void sync(FILE *f)
{
    if (f->__kind != K_FILE && f->__bmode != B_FULL)
        flush(f);
}

/* The position a read or write would start at, as a byte offset in the
 * file: __base plus the bytes consumed or staged, with a CR more for each
 * staged newline of a text stream, less one for an ungetc character. */
static long position(FILE *f)
{
    long p;
    unsigned char *q;

    p = f->__base + (f->__ptr - f->__buf);
    if (f->__state == S_WRITE && (f->__flags & F_TEXT))
        for (q = f->__buf; q < f->__ptr; q++)
            if (*q == '\n')
                p++;                    /* each will be CR LF */
    if (f->__flags & F_UNGOT)
        p--;
    return p;
}

/* Leaves S_READ or S_WRITE for S_NONE at the stream's position: 0, or EOF
 * if the writing failed. */
static int settle(FILE *f)
{
    int r;

    r = 0;
    if (f->__state == S_WRITE)
        r = flush(f);
    else if (f->__state == S_READ)
        f->__base = f->__base + (f->__ptr - f->__buf);
    f->__state = S_NONE;
    f->__ptr = f->__buf;
    limits(f);
    return r;
}

/* Refills a read buffer, keeping the bytes not yet read (a text stream's
 * CR waiting for its LF): 1, or 0 at the end of the file (setting the
 * end-of-file flag if nothing was kept) or on an error. */
static int fill(FILE *f)
{
    int keep;
    int n;

    if (!(f->__flags & F_READ)) {
        f->__flags = f->__flags | F_ERR;
        return 0;
    }
    /* __base moves up to the first byte kept, which will be __buf[0] */
    keep = 0;
    if (f->__state == S_READ) {
        keep = f->__end - f->__ptr;
        f->__base = f->__base + (f->__ptr - f->__buf);
    } else if (settle(f) != 0) {
        return 0;
    }
    if (!have_buf(f))
        return 0;
    POLL();
    /* the console: one edited line, given a '\n' at its end (the buffer's
     * last byte is kept for it); nothing is kept, as fgetc refills the
     * console only when it is empty */
    if (f->__kind == K_CONIN) {
        console_read((char *)f->__buf, f->__size - 1);
        console_write("\r\n", 2);       /* the line editor leaves the cursor on the line */
        POLL();                         /* Ctrl-C at the prompt: acted on with the line */
        n = strlen((char *)f->__buf);
        f->__buf[n] = '\n';
        f->__ptr = f->__buf;
        f->__end = f->__buf + n + 1;
        f->__state = S_READ;
        limits(f);
        return 1;
    }
    /* a file: the kept bytes to the front, then as many more as fit */
    memmove(f->__buf, f->__buf + (f->__ptr - f->__buf), keep);
    f->__ptr = f->__buf;
    f->__end = f->__buf + keep;
    f->__state = S_READ;
    n = 0;
    if (mos_at(f, f->__base + keep)) {
        n = mos_fread(f->__handle, f->__buf + keep, f->__size - keep);
        f->__mospos = f->__mospos + n;
    }
    f->__end = f->__end + n;
    limits(f);
    if (n == 0 && keep == 0) {
        if (!(f->__flags & F_ERR))
            f->__flags = f->__flags | F_EOF;
        return 0;
    }
    return 1;
}

/* Into S_WRITE: 1, or 0 for a stream that cannot be written. From S_READ
 * the bytes read ahead are dropped: writing starts at the position the
 * program has reached, an ungetc character forgotten. */
static int start_write(FILE *f)
{
    if (!(f->__flags & F_WRITE)) {
        f->__flags = f->__flags | F_ERR;
        return 0;
    }
    if (f->__state == S_READ) {
        f->__base = position(f);
        f->__flags = f->__flags & ~F_UNGOT;
    }
    if (!have_buf(f))
        return 0;
    f->__state = S_WRITE;
    f->__ptr = f->__buf;
    f->__end = f->__buf;
    limits(f);
    return 1;
}

/* Stages one byte (0-255 returned), writing the buffer when it is full, at
 * a newline if line-buffered, and at once if unbuffered; EOF on an error.
 * The end of an output call is sync's. */
static int put(int c, FILE *f)
{
    if (f->__state != S_WRITE && !start_write(f))
        return EOF;
    if (f->__ptr >= f->__buf + f->__size && flush(f) != 0)
        return EOF;
    *f->__ptr = c;
    f->__ptr++;
    if ((f->__bmode == B_NONE || (f->__bmode == B_LINE && c == '\n')) && flush(f) != 0)
        return EOF;
    return c & 255;
}

/* ---- opening and closing ---------------------------------------------------------- */

/* Opens name into slot f by mode (C89 4.9.5.3): "r", "w" or "a", then "b"
 * and "+" in either order. NULL if the mode is not one of those or MOS
 * cannot open the file. */
static FILE *open_in(FILE *f, const char *name, const char *mode)
{
    int plus;
    int binary;
    int fa;
    int h;
    int i;
    int saved;
    long size;

    saved = errno;                      /* given back on success */
    errno = EINVAL;                     /* for every bad-mode return below */
    if (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')
        return NULL;
    plus = 0;
    binary = 0;
    for (i = 1; mode[i]; i++) {
        if (mode[i] == '+')
            plus = 1;
        else if (mode[i] == 'b')
            binary = 1;
        else
            return NULL;
    }
    if (i > 3)
        return NULL;
    /* FatFs's open flags: "w" creates or truncates, "a" creates if need
     * be; "+" adds the other direction */
    if (mode[0] == 'r')
        fa = FA_READ | (plus ? FA_WRITE : 0);
    else if (mode[0] == 'w')
        fa = FA_WRITE | FA_CREATE_ALWAYS | (plus ? FA_READ : 0);
    else if (mode[0] == 'a')
        fa = FA_WRITE | FA_OPEN_ALWAYS | (plus ? FA_READ : 0);
    else
        return NULL;
    /* the file's size, which the stream tracks from here on (for "a" and
     * SEEK_END), is looked up by name before the open; "w" empties it */
    size = 0;
    if (mode[0] != 'w') {
        size = mos_fsize(name);
        if (size < 0)
            size = 0;                   /* "a" of a file that does not exist yet */
    }
    /* MOS's handle 0 means failure: ENOENT if "r" found no file, else EIO */
    h = mos_fopen(name, fa);
    if (h == 0) {
        errno = mode[0] == 'r' && size == 0 && mos_fsize(name) < 0 ? ENOENT : EIO;
        return NULL;
    }
    memset(f, 0, sizeof(FILE));
    f->__kind = K_FILE;
    f->__handle = h;
    f->__flags = (fa & FA_READ ? F_READ : 0) | (fa & FA_WRITE ? F_WRITE : 0) | (mode[0] == 'a' ? F_APPEND : 0)
                 | (binary ? 0 : F_TEXT);
    f->__bmode = B_FULL;
    f->__size = BUFSIZ;
    f->__fsize = size;
    f->__base = mode[0] == 'a' ? size : 0;
    errno = saved;
    return f;
}

/* The first free file slot (3 onwards; 0-2 are the standard streams), or
 * NULL with errno EMFILE. */
FILE *fopen(const char *filename, const char *mode)
{
    int i;

    for (i = 3; i < NFILES; i++)
        if (files[i].__kind == K_CLOSED)
            return open_in(files + i, filename, mode);
    errno = EMFILE;
    return NULL;
}

/* The stream closed (errors ignored), then name opened in its place, even
 * one of the standard streams (C89 4.9.5.4); NULL if that fails. */
FILE *freopen(const char *filename, const char *mode, FILE *f)
{
    if (f->__kind != K_CLOSED)
        fclose(f);
    return open_in(f, filename, mode);
}

/* ---- temporary files ------------------------------------------------------------ */

static unsigned int tmp_last;           /* the last number tmpnam tried, 1-99999 */

/* /tmp/tNNNNN.tmp for n: its five digits, from the last, into s[6..10]. */
static void tmp_path(char *s, unsigned int n)
{
    int i;

    strcpy(s, "/tmp/t00000.tmp");
    for (i = 10; i >= 6; i--) {
        s[i] = '0' + n % 10;
        n = n / 10;
    }
}

/* A name no file has, a different one each call (C89 4.9.4.4): the next
 * number in turn, skipping names whose file exists (mos_fsize fails for a
 * missing file); NULL after TMP_MAX tries. */
char *tmpnam(char *s)
{
    static char name[L_tmpnam];
    unsigned int k;

    if (s == NULL)
        s = name;
    for (k = 0; k < TMP_MAX; k++) {
        tmp_last = tmp_last % 99999 + 1;
        tmp_path(s, tmp_last);
        if (mos_fsize(s) < 0)
            return s;
    }
    return NULL;
}

/* A binary update stream on a file of tmpnam's, removed when it is closed
 * (by fclose or at exit). The stream keeps only the name's number, in
 * __tmpno (0: not a temporary file), and rebuilds the name to remove it. */
FILE *tmpfile(void)
{
    char name[L_tmpnam];
    FILE *f;

    if (tmpnam(name) == NULL)
        return NULL;
    f = fopen(name, "wb+");
    if (f != NULL)
        f->__tmpno = tmp_last;
    return f;
}

/* A file stream's MOS handle closed, and tmpfile's file removed. */
static void release(FILE *f)
{
    char name[L_tmpnam];

    mos_fclose(f->__handle);
    if (f->__tmpno != 0) {
        tmp_path(name, f->__tmpno);
        mos_del(name);
    }
}

/* 0, or EOF if its last bytes could not be written; the stream is closed
 * either way. */
int fclose(FILE *f)
{
    int r;

    if (f->__kind == K_CLOSED)
        return EOF;
    r = flush(f);
    if (f->__flags & F_ERR)
        r = EOF;
    if (f->__kind == K_FILE)
        release(f);
    if (f->__flags & F_MYBUF)
        free(f->__buf);
    memset(f, 0, sizeof(FILE));
    return r;
}

/* exit's last step, and a signal's default action (exit.c): every stream
 * closed, its buffer written first if flush_them. The buffers are not
 * freed, as the program is ending. A Ctrl-C noticed in one of the MOS
 * calls here raises SIGINT, whose default action runs this again from
 * the start: a slot is marked closed only after its handle is, and flush
 * carries on from __end, so the second run repeats no bytes. */
void __stdio_exit(int flush_them)
{
    int i;

    for (i = 0; i < NFILES; i++) {
        if (files[i].__kind != K_CLOSED) {
            if (flush_them)
                flush(files + i);
            if (files[i].__kind == K_FILE)
                release(files + i);
            files[i].__kind = K_CLOSED;
        }
    }
}

/* Before any other operation on the stream (C89 4.9.5.6): mode _IOFBF,
 * _IOLBF or _IONBF, and buf of size bytes, or NULL for one from malloc of
 * that size (BUFSIZ if 0). Any earlier buffering is settled first, so it
 * also works later. _IONBF uses the stream's own one-byte __one, so every
 * put fills it and flushes it at once. */
int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    if (f->__kind == K_CLOSED || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF))
        return -1;
    if (settle(f) != 0)
        return -1;
    if (f->__flags & F_MYBUF)
        free(f->__buf);
    f->__flags = f->__flags & ~F_MYBUF;
    /* the keyboard is read a whole line at a time by MOS's line editor, so
     * console input keeps a line buffer whatever is asked: unbuffered (as
     * setbuf(stdin, NULL) asks) would give the editor no room */
    if (f->__kind == K_CONIN && (mode == _IONBF || size < 2)) {
        mode = _IOLBF;
        buf = NULL;
        size = CON_IN;
    }
    if (mode == _IONBF) {
        buf = (char *)&f->__one;
        size = 1;
    } else if (size == 0) {
        size = BUFSIZ;
    }
    f->__buf = (unsigned char *)buf;
    f->__ptr = f->__buf;
    f->__size = size;
    f->__bmode = mode;
    limits(f);
    return 0;
}

void setbuf(FILE *f, char *buf)
{
    setvbuf(f, buf, buf != NULL ? _IOFBF : _IONBF, BUFSIZ);
}

/* ---- characters and lines ---------------------------------------------------------- */

/* getc's slow path (stdio.h): ungetc's character, a refill, and a text
 * stream's CR (CR LF is '\n'; a lone CR is itself). */
int fgetc(FILE *f)
{
    int c;

    if (f->__flags & F_UNGOT) {
        f->__flags = f->__flags & ~F_UNGOT;
        limits(f);
        return f->__unget;
    }
    if ((f->__state != S_READ || f->__ptr >= f->__end) && !fill(f))
        return EOF;
    c = *f->__ptr;
    f->__ptr++;
    /* a text stream's CR: the next byte decides, so if the CR was the
     * buffer's last byte the buffer is refilled with the CR kept, and the
     * LF, if it is one, is taken with it */
    if (c == '\r' && (f->__flags & F_TEXT)) {
        if (f->__ptr == f->__end) {
            f->__ptr--;                 /* keep the CR while what follows comes in */
            fill(f);
            f->__ptr++;
        }
        if (f->__ptr < f->__end && *f->__ptr == '\n') {
            f->__ptr++;
            c = '\n';
        }
        limits(f);
    }
    return c;
}

/* The function forms of the getc, getchar, putc and putchar macros (the
 * #undefs above let these be defined), for a program that takes their
 * address or #undefs them. */
int getc(FILE *f)
{
    return fgetc(f);
}

int getchar(void)
{
    return fgetc(stdin);
}

/* One character of pushback (C89 4.9.7.11), which clears end of file.
 * The character is held in __unget, not written into the buffer, so a
 * character other than the one read can be pushed back; the NULL limits
 * F_UNGOT gives send the next getc to fgetc, which returns it. */
int ungetc(int c, FILE *f)
{
    if (c == EOF || (f->__flags & F_UNGOT) || !(f->__flags & F_READ) || f->__state == S_WRITE)
        return EOF;
    f->__unget = c;
    f->__flags = (f->__flags | F_UNGOT) & ~F_EOF;
    limits(f);
    return c & 255;
}

/* putc's slow path (stdio.h), and every console character. */
int fputc(int c, FILE *f)
{
    c = put(c, f);
    sync(f);
    return c;
}

int putc(int c, FILE *f)
{
    return fputc(c, f);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

/* Up to n-1 characters, stopping after a newline (kept); NULL if end of
 * file comes before any character, or n < 1. With n == 1 nothing is read
 * and s is the empty string, as C89 4.9.7.2 has it. */
char *fgets(char *s, int n, FILE *f)
{
    int i;
    int c;

    if (n < 1)
        return NULL;
    i = 0;
    while (i < n - 1) {
        c = getc(f);
        if (c == EOF) {
            if (i == 0 || (f->__flags & F_ERR))
                return NULL;
            break;
        }
        s[i] = c;
        i++;
        if (c == '\n')
            break;
    }
    s[i] = 0;
    return s;
}

/* A line from stdin without its newline (C89 4.9.7.7): NULL at end of
 * file before any character, or on an error. */
char *gets(char *s)
{
    char *p;
    int c;

    p = s;
    while ((c = getc(stdin)) != EOF && c != '\n') {
        *p = c;
        p++;
    }
    if (c == EOF && (p == s || ferror(stdin)))
        return NULL;
    *p = 0;
    return s;
}

/* fputs and puts stage the whole string with put and sync once at the
 * end, so console text goes out a line (or a buffer) at a time, not a
 * character at a time. */
int fputs(const char *s, FILE *f)
{
    int r;

    r = 0;
    while (*s && r != EOF) {
        r = put(*s, f);
        s++;
    }
    sync(f);
    return r == EOF ? EOF : 0;
}

int puts(const char *s)
{
    int r;

    r = 0;
    while (*s && r != EOF) {
        r = put(*s, stdout);
        s++;
    }
    if (r != EOF)
        r = put('\n', stdout);
    sync(stdout);
    return r == EOF ? EOF : 0;
}

/* ---- blocks -------------------------------------------------------------------------- */

/* size * nmemb bytes, by whichever of three ways suits each part: a copy
 * of what the buffer holds, a MOS read straight into ptr for a large part
 * of a binary file, or fgetc for the rest (a refill, a text stream's CR,
 * an ungetc character). Returns the whole elements read. */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
    unsigned char *p;
    size_t total;
    size_t done;
    size_t k;
    int c;

    if (size == 0 || nmemb == 0)
        return 0;
    p = ptr;
    total = size * nmemb;
    done = 0;
    while (done < total) {
        if (f->__ptr < f->__rend) {
            /* straight from the buffer, up to its end (or a text stream's CR) */
            k = f->__rend - f->__ptr;
            if (k > total - done)
                k = total - done;
            memcpy(p + done, f->__ptr, k);
            f->__ptr = f->__ptr + k;
            done = done + k;
        } else if (f->__kind == K_FILE && !(f->__flags & (F_TEXT | F_UNGOT)) && (f->__flags & F_READ)
                   && total - done >= (size_t)f->__size) {
            /* a buffer's worth or more of a binary file, the buffer used up:
             * straight into the caller's memory */
            if (settle(f) != 0 || !mos_at(f, f->__base))
                break;
            k = mos_fread(f->__handle, p + done, total - done);
            f->__mospos = f->__mospos + k;
            f->__base = f->__base + k;
            if (k == 0) {
                f->__flags = f->__flags | F_EOF;
                break;
            }
            done = done + k;
        } else {
            c = fgetc(f);               /* refills the buffer */
            if (c == EOF)
                break;
            p[done] = c;
            done++;
        }
    }
    return done / size;
}

/* As fread: a copy into the buffer while there is room below __wend (a
 * fully buffered file), put for the rest. Returns the whole elements
 * written. */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
    const unsigned char *p;
    size_t total;
    size_t done;
    size_t k;

    if (size == 0 || nmemb == 0)
        return 0;
    p = ptr;
    total = size * nmemb;
    done = 0;
    while (done < total) {
        if (f->__ptr < f->__wend) {
            /* straight into the buffer */
            k = f->__wend - f->__ptr;
            if (k > total - done)
                k = total - done;
            memcpy(f->__ptr, p + done, k);
            f->__ptr = f->__ptr + k;
            done = done + k;
        } else {
            if (put(p[done], f) == EOF)
                break;
            done++;
        }
    }
    sync(f);
    return done / size;
}

/* ---- state --------------------------------------------------------------------------- */

void clearerr(FILE *f)
{
    f->__flags = f->__flags & ~(F_EOF | F_ERR);
}

int feof(FILE *f)
{
    return (f->__flags & F_EOF) != 0;
}

int ferror(FILE *f)
{
    return (f->__flags & F_ERR) != 0;
}

/* "s: message" and a newline on stderr, or just the message if s is null
 * or empty (c89_spec.md 15). */
void perror(const char *s)
{
    char *m;

    m = strerror(errno);
    if (s != NULL && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(m, stderr);
    fputs("\n", stderr);
}

/* One stream's written bytes, or every stream's when f is NULL. */
int fflush(FILE *f)
{
    int i;
    int r;

    if (f != NULL)
        return f->__kind == K_CLOSED ? EOF : flush(f);
    r = 0;
    for (i = 0; i < NFILES; i++)
        if (files[i].__kind != K_CLOSED && flush(files + i) != 0)
            r = EOF;
    return r;
}

/* ---- positioning ----------------------------------------------------------------------- */

/* 0, or -1 for a closed or console stream, an unknown whence, a target
 * before the start, or the stream's bytes failing to be written. Success
 * clears end of file and ungetc's character. A text stream's positions are
 * byte offsets in the file, as ftell gives them. A read inside the bytes
 * buffered needs no MOS call; otherwise only the stream's position moves,
 * and MOS's follows when bytes are next read or written. Targets past the
 * end are allowed: reading there meets end of file, writing extends the
 * file. An append stream's writes go to the end whatever its position. */
int fseek(FILE *f, long offset, int whence)
{
    long target;

    if (f->__kind != K_FILE) {
        errno = EBADF;
        return -1;
    }
    if (whence == SEEK_SET)
        target = offset;
    else if (whence == SEEK_CUR)
        target = position(f) + offset;
    else if (whence == SEEK_END) {
        if (flush(f) != 0)              /* staged bytes may extend the file */
            return -1;
        target = f->__fsize + offset;
    } else {
        return -1;
    }
    if (target < 0)
        return -1;
    f->__flags = f->__flags & ~(F_EOF | F_UNGOT);
    /* inside the bytes the read buffer holds: only __ptr moves */
    if (f->__state == S_READ && target >= f->__base && target <= f->__base + (f->__end - f->__buf)) {
        f->__ptr = f->__buf + (target - f->__base);
        limits(f);
        return 0;
    }
    if (settle(f) != 0)
        return -1;
    f->__base = target;
    return 0;
}

/* The position, or -1 with errno EBADF for a closed or console stream. */
long ftell(FILE *f)
{
    if (f->__kind != K_FILE) {
        errno = EBADF;
        return -1;
    }
    return position(f);
}

int fgetpos(FILE *f, fpos_t *pos)
{
    long p;

    p = ftell(f);
    if (p < 0)
        return -1;
    *pos = p;
    return 0;
}

int fsetpos(FILE *f, const fpos_t *pos)
{
    int saved;

    saved = errno;
    errno = 0;
    if (fseek(f, *pos, SEEK_SET) != 0) {
        if (errno == 0)                 /* fseek gave no reason of its own */
            errno = EINVAL;
        return -1;
    }
    errno = saved;
    return 0;
}

void rewind(FILE *f)
{
    fseek(f, 0L, SEEK_SET);
    clearerr(f);
}

/* MOS's FatFs does no locking (FF_FS_LOCK 0), so removing a file a stream
 * has open succeeds, and that stream's later writes are lost (c89_spec.md
 * 15). */
int remove(const char *path)
{
    if (mos_del(path) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

/* Fails if new_name exists, as MOS's FatFs does; checked here too, since
 * the emulator's host-folder card replaces it instead (mos.h). */
int rename(const char *old, const char *new_name)
{
    if (mos_fsize(new_name) >= 0) {
        errno = EEXIST;
        return -1;
    }
    if (mos_ren(old, new_name) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

/* ---- the printf family -------------------------------------------------------------- */

/* One engine, format, serves all six functions: it walks the format,
 * copying ordinary characters and expanding each conversion, and hands
 * every character to emit, which sends it to a stream or to memory.
 *
 * A conversion is %, flags, width, precision, length modifier, then the
 * conversion character: in "%-08.3lx", '-' and '0' are flags, 8 the
 * minimum width, .3 the precision, l the length (a long argument) and x
 * the conversion (hexadecimal). An integer is printed in three steps: its
 * magnitude to digits (to_digits, or ll.c's __ll_print for long long), a
 * sign or prefix chosen, and then number lays out padding, prefix, zeros
 * and digits to the width. */

/* Where formatted characters go: a stream, or memory (sprintf), room
 * more characters of it (snprintf's limit; past it, they are counted but
 * not stored). */
struct sink {
    FILE *f;
    char *out;
    unsigned int room;
    int count;
};

static void emit(struct sink *k, int c)
{
    if (k->f != NULL) {
        put(c, k->f);
    } else if (k->room > 0) {
        *k->out = c;
        k->out++;
        k->room--;
    }
    k->count++;
}

static void emit_n(struct sink *k, int c, int n)
{
    while (n > 0) {
        emit(k, c);
        n--;
    }
}

/* u's digits in base, least significant first; returns how many (none
 * for 0). They come out of 32-bit division only while u needs more than
 * 24 bits; the rest come from int division, rt.s's 24-bit helpers, which
 * are cheaper than the 32-bit ones. Each step is the remainder (the next
 * digit) and the quotient (what is left): 1234 gives 4, 3, 2, 1. */
static int to_digits(char *digits, unsigned long u, int base, char *digit_set)
{
    int n;
    unsigned int w;

    n = 0;
    while (u > 0xFFFFFFUL) {
        digits[n] = digit_set[(int)(u % base)];
        n++;
        u = u / base;
    }
    w = (unsigned int)u;
    while (w != 0) {
        digits[n] = digit_set[w % base];
        n++;
        w = w / base;
    }
    return n;
}

/* A number's n digits (to_digits's), after the prefix pre ("-", "+" or " "
 * from those flags, "0x" for '#'): prec is the minimum digit count (-1 for
 * the default of 1; 0 lets zero print no digits), and a '0' flag pads with
 * zeros only without '-' or a precision (C89 4.9.6.1); octal makes its
 * first digit 0 (the '#' flag). The layout, left to right:
 *
 *   [spaces] pre [zeros] digits [spaces, with '-']
 *
 * so printf("%+06d", 42) is "+00042": the '0' flag turns the padding
 * into zeros after the sign, where spaces would go before it. */
static void number(struct sink *k, char *digits, int n, char *pre, int width, int prec, int left, int zero,
                   int octal)
{
    int zeros;
    int pad;

    if (prec < 0)
        zeros = n == 0 ? 1 : 0;
    else
        zeros = prec > n ? prec - n : 0;
    if (octal && zeros == 0 && n > 0)
        zeros = 1;
    pad = width - (int)strlen(pre) - zeros - n;
    if (pad < 0)
        pad = 0;
    if (zero && !left && prec < 0) {
        zeros = zeros + pad;
        pad = 0;
    }
    if (!left)
        emit_n(k, ' ', pad);
    while (*pre) {
        emit(k, *pre);
        pre++;
    }
    emit_n(k, '0', zeros);
    while (n > 0) {
        n--;
        emit(k, digits[n]);
    }
    if (left)
        emit_n(k, ' ', pad);
}

/* A double or long long argument's eight bytes, taken from the arguments
 * without a double or long long in this unit (which would bring in the
 * floating or long long conversions). An 8-byte struct takes the same
 * three 3-byte argument slots as a double or long long (abi.md 4), so
 * va_arg(ap, struct eight) steps over exactly one such argument. */
struct eight {
    char b[8];
};

/* A conversion's length modifier, which *fmt points at: 'h', 'l', 'L', 0
 * for none, and for C99's: 'q' for ll and j (intmax_t is long long), 'H'
 * for hh (char), and 0 for z and t (size_t and ptrdiff_t are int-sized). */
static int length(const char **fmt)
{
    int c;

    c = **fmt;
    if ((c == 'l' || c == 'h') && (*fmt)[1] == c) {
        *fmt = *fmt + 2;
        return c == 'l' ? 'q' : 'H';
    }
    if (c == 'h' || c == 'l' || c == 'L' || c == 'j' || c == 'z' || c == 't') {
        *fmt = *fmt + 1;
        return c == 'j' ? 'q' : c == 'z' || c == 't' ? 0 : c;
    }
    return 0;
}

/* %n: the count so far, to an object of the length modifier's size; a
 * long and a long long are written as bytes, low first, the long long's
 * upper four zero */
static void store_count(void *p, int size, int count)
{
    long v;

    v = count;
    if (size == 'H') {
        *(signed char *)p = (signed char)count;
    } else if (size == 'h') {
        *(short *)p = (short)count;
    } else if (size == 'l' || size == 'q') {
        memcpy(p, &v, 4);
        if (size == 'q')
            memset((char *)p + 4, 0, 4);        /* never negative */
    } else {
        *(int *)p = count;
    }
}

/* emit with the void * sink fp.c's __fp_print takes. */
static void emit_to(void *k, int c)
{
    emit(k, c);
}

/* The conversions d i u x X o c s p n % and e f g E F G, with C99's a A,
 * the flags '-', '0', '+', ' ' (the two for d i and the floating ones
 * only; '+' wins) and '#' (o x X and the floating ones), a width and a
 * precision (either may be '*'; a negative '*' width means '-', a
 * negative '*' precision means none), and the length modifiers 'h' (the
 * int argument is converted to short or unsigned short), 'l' (the
 * argument is a long or unsigned long) and C99's 'hh' (converted to
 * char), 'll' and 'j' (a long long or unsigned long long) and 'z' and 't'
 * (size_t and ptrdiff_t, which are int-sized) on d i u x X o n, and 'L'
 * (long double, which is double) on the floating ones. A null %s prints "(null)"; %p is at least 6 lowercase
 * hex digits; an unknown conversion character prints as itself; a format
 * ending in '%' just ends. A program without floating point or long long
 * has no such arguments, and prints nothing for them. */
static void format(struct sink *k, const char *fmt, va_list ap)
{
    int left;
    int zero;
    int plus;
    int alt;
    char sign[2];
    char *d;
    int width;
    int prec;
    int c;
    int v;
    int n;
    int i;
    int size;           /* 'h', 'l', 'q' (ll) or 0 */
    int base;
    long lv;
    unsigned long u;
    char *s;
    char *lower;
    char *upper;
    char *set;
    char digits[24];    /* a long long's in octal */

    lower = "0123456789abcdef";
    upper = "0123456789ABCDEF";
    while (*fmt) {
        if (*fmt != '%') {
            emit(k, *fmt);
            fmt++;
            continue;
        }
        fmt++;
        /* the flags, in any order; plus holds the sign a non-negative
         * value gets: 0 (none), ' ' or '+' */
        left = 0;
        zero = 0;
        plus = 0;
        alt = 0;
        for (;;) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '#')
                alt = 1;
            else if (*fmt == '0')
                zero = 1;
            else if (*fmt == '+')
                plus = '+';
            else if (*fmt == ' ') {
                if (plus == 0)
                    plus = ' ';
            } else
                break;
            fmt++;
        }
        /* the width */
        width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + *fmt - '0';
                fmt++;
            }
        }
        /* the precision: -1 for none, and a bare '.' is 0 */
        prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + *fmt - '0';
                    fmt++;
                }
            }
        }
        size = length(&fmt);
        c = *fmt;
        if (c == 0)
            break;
        fmt++;
        /* sign is a one-character string, or empty when sign[0] is 0
         * (plus with no flag) */
        sign[1] = 0;
        switch (c) {
        case 'd':
        case 'i':
            if (size == 'q') {
                /* the sign is the top bit of the last (most significant)
                 * byte, which as a signed char is then negative */
                d = va_arg(ap, struct eight).b;
                sign[0] = d[7] < 0 ? '-' : plus;
                n = __ll_print ? __ll_print(digits, d, 10, lower, 1) : 0;
            } else {
                if (size == 'l')
                    lv = va_arg(ap, long);
                else if (size == 'h')
                    lv = (short)va_arg(ap, int);
                else if (size == 'H')
                    lv = (signed char)va_arg(ap, int);
                else
                    lv = va_arg(ap, int);
                sign[0] = lv < 0 ? '-' : plus;
                /* the magnitude, negated in unsigned long: -LONG_MIN does
                 * not fit in a long, but 2^31 fits in an unsigned long */
                n = to_digits(digits, lv < 0 ? 0UL - lv : lv, 10, lower);
            }
            number(k, digits, n, sign, width, prec, left, zero, 0);
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            base = c == 'u' ? 10 : c == 'o' ? 8 : 16;
            set = c == 'X' ? upper : lower;
            if (size == 'q') {
                d = va_arg(ap, struct eight).b;
                n = __ll_print ? __ll_print(digits, d, base, set, 0) : 0;
            } else {
                if (size == 'l')
                    u = va_arg(ap, unsigned long);
                else if (size == 'h')
                    u = (unsigned short)va_arg(ap, unsigned int);
                else if (size == 'H')
                    u = (unsigned char)va_arg(ap, unsigned int);
                else
                    u = va_arg(ap, unsigned int);
                n = to_digits(digits, u, base, set);
            }
            /* '#' gives 0x or 0X only to a nonzero value (n > 0) */
            number(k, digits, n, alt && n > 0 && c == 'x' ? "0x" : alt && n > 0 && c == 'X' ? "0X" : "", width,
                   prec, left, zero, alt && c == 'o');
            break;
        case 'p':
            /* a pointer is 24 bits, the same as unsigned int */
            n = to_digits(digits, (unsigned int)va_arg(ap, char *), 16, lower);
            number(k, digits, n, "", width, prec < 6 ? 6 : prec, left, 0, 0);
            break;
        case 'e':
        case 'f':
        case 'g':
        case 'E':
        case 'F':
        case 'G':
        case 'a':
        case 'A':
            /* the argument is always consumed, so the ones after it stay
             * in step even when nothing is printed */
            d = va_arg(ap, struct eight).b;
            if (__fp_print)
                __fp_print(emit_to, k, d, c, plus, alt, left, zero, width, prec);
            break;
        case 'n':                       /* the count so far (C89 4.9.6.1) */
            store_count(va_arg(ap, void *), size, k->count);
            break;
        case 'c':
            v = va_arg(ap, int);
            if (!left)
                emit_n(k, ' ', width - 1);
            emit(k, v);
            if (left)
                emit_n(k, ' ', width - 1);
            break;
        case 's':
            s = va_arg(ap, char *);
            if (s == NULL)
                s = "(null)";
            n = 0;
            while (s[n] && (prec < 0 || n < prec))
                n++;
            if (!left)
                emit_n(k, ' ', width - n);
            for (i = 0; i < n; i++)
                emit(k, s[i]);
            if (left)
                emit_n(k, ' ', width - n);
            break;
        default:
            emit(k, c);                 /* '%', or an unknown conversion */
            break;
        }
    }
}

/* The characters go to the stream through put; sync at the end writes a
 * console's text out. Returns the count; write errors show in ferror. */
int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct sink k;

    k.f = f;
    k.out = NULL;
    k.count = 0;
    format(&k, fmt, ap);
    sync(f);
    return k.count;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

/* Into memory at s, NUL-terminated; no length check, as C89 has none. */
int vsprintf(char *s, const char *fmt, va_list ap)
{
    struct sink k;

    k.f = NULL;
    k.out = s;
    k.room = ~0U;
    k.count = 0;
    format(&k, fmt, ap);
    *k.out = 0;
    return k.count;
}

/* C99's: at most n - 1 characters stored and a NUL after them (nothing
 * at all for n 0, when s may be null); the return is the length the
 * whole output would have had, so a result of n or more means it was
 * cut short. */
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    struct sink k;

    k.f = NULL;
    k.out = s;
    k.room = n > 0 ? n - 1 : 0;
    k.count = 0;
    format(&k, fmt, ap);
    if (n > 0)
        *k.out = 0;
    return k.count;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsprintf(s, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *s, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(s, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- scanf -------------------------------------------------------------------------- */

/* One input for the scanf family: a stream, or sscanf's string, read a
 * character at a time with one character of pushback (a stream's through
 * ungetc). */
struct source {
    FILE *f;
    const char *s;
    int count;          /* characters consumed, for %n */
    int ended;          /* the input has run out */
    int c99;            /* floating conversions read C99's forms too */
};

/* The next character, or EOF (which marks the input ended). */
static int sget(struct source *in)
{
    int c;

    if (in->f != NULL)
        c = getc(in->f);
    else if (*in->s)
        c = *in->s++ & 255;
    else
        c = EOF;
    if (c == EOF)
        in->ended = 1;
    else
        in->count++;
    return c;
}

/* Give back c, the character sget just returned. */
static void sunget(struct source *in, int c)
{
    if (c == EOF)
        return;
    in->count--;
    if (in->f != NULL)
        ungetc(c, in->f);
    else
        in->s--;
}

/* The next character, left unread. */
static int speek(struct source *in)
{
    int c;

    c = sget(in);
    sunget(in, c);
    return c;
}

static int scan_space(int c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

static void skip_input_space(struct source *in)
{
    while (scan_space(speek(in)))
        sget(in);
}

/* A digit or letter's value as a digit (letters 10-35), 99 for anything
 * else, so a single "< base" test accepts exactly base's digits. */
static int scan_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

/* An integer of at most width characters in base (0: by its prefix, as C
 * writes it; 16 also takes a 0x prefix); *ok is 0 if it has no digits.
 * With q, the long long value too, in the eight bytes q (zeroed). Every
 * character read, the sign and prefix included, counts against width.
 * Overflow wraps, modulo 2^32 (or 2^64 in q). A lone "0x" reads as 0. */
static unsigned long scan_int(struct source *in, int width, int base, int *ok, char *q)
{
    unsigned long v;
    int n;
    int c;
    int neg;

    v = 0;
    n = 0;
    neg = 0;
    *ok = 0;
    c = speek(in);
    if (n < width && (c == '-' || c == '+')) {
        neg = c == '-';
        sget(in);
        n++;
        c = speek(in);
    }
    if (n < width && c == '0' && (base == 0 || base == 16)) {
        sget(in);
        n++;
        *ok = 1;
        c = speek(in);
        if (n < width && (c == 'x' || c == 'X')) {
            sget(in);
            n++;
            base = 16;
            c = speek(in);
        } else if (base == 0) {
            base = 8;
        }
    }
    if (base == 0)
        base = 10;
    while (n < width && scan_digit(c) < base) {
        v = v * base + scan_digit(c);
        if (q != NULL)
            __ll_scan(q, base, scan_digit(c));
        sget(in);
        n++;
        *ok = 1;
        c = speek(in);
    }
    /* base 0 asks __ll_scan to negate q */
    if (neg && q != NULL)
        __ll_scan(q, 0, 0);
    return neg ? 0UL - v : v;
}

/* sget and sunget with the void * source fp.c's __fp_scan takes. */
static int get_from(void *in)
{
    return sget(in);
}

static void unget_to(void *in, int c)
{
    sunget(in, c);
}

/* Whether c is in the scanset set[0..n) (negated if neg). A '-' that is
 * neither first nor last, between a character and one not below it, is a
 * range (C89 4.9.6.2 leaves it to the implementation): %[a-z]. */
static int in_set(int c, const char *set, int n, int neg)
{
    int i;

    for (i = 0; i < n; i++) {
        if (set[i] == '-' && i > 0 && i < n - 1 && (set[i - 1] & 255) <= (set[i + 1] & 255)) {
            if (c >= (set[i - 1] & 255) && c <= (set[i + 1] & 255))
                return !neg;
        } else if ((set[i] & 255) == c) {
            return !neg;
        }
    }
    return neg;
}

/* The scanf family's engine (C89 4.9.6.2): the number of items assigned,
 * or EOF if the input ended before the first conversion. The format is
 * three kinds of thing: white space, which skips any amount of input
 * white space (none included); an ordinary character, which must match
 * the next input character; and a conversion. The first mismatch or
 * failed conversion ends the scan, its character left unread. */
static int scan(struct source *in, const char *fmt, va_list ap)
{
    int assigned;
    int converted;
    int suppress;
    int width;
    int size;
    int conv;
    int c;
    int n;
    int ok;
    int neg;
    unsigned long v;
    char *out;
    const char *set;
    int nset;
    char q[8];

    assigned = 0;
    converted = 0;
    while (*fmt) {
        if (scan_space(*fmt & 255)) {
            while (scan_space(*fmt & 255))
                fmt++;
            skip_input_space(in);
            continue;
        }
        /* an ordinary character, or %% (which matches a '%' after any
         * white space) */
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') {
                fmt++;
                skip_input_space(in);
            }
            c = sget(in);
            if (c != (*fmt & 255)) {
                sunget(in, c);
                break;
            }
            fmt++;
            continue;
        }
        /* a conversion: '*' (read but do not assign), width, length,
         * then the conversion character */
        fmt++;
        suppress = *fmt == '*';
        if (suppress)
            fmt++;
        width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + *fmt - '0';
            fmt++;
        }
        size = length(&fmt);
        conv = *fmt & 255;
        if (conv == 0)
            break;
        fmt++;
        if (conv == 'n') {
            if (!suppress)
                store_count(va_arg(ap, void *), size, in->count);
            continue;
        }
        /* %n reads nothing; every conversion but %c and %[ skips leading
         * white space */
        if (conv != 'c' && conv != '[')
            skip_input_space(in);
        if (conv == 'd' || conv == 'i' || conv == 'o' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'p') {
            /* a program without long long has nowhere to store one */
            if (size == 'q' && !__ll_scan)
                break;
            memset(q, 0, 8);
            v = scan_int(in, width > 0 ? width : 32767,
                         conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16, &ok,
                         size == 'q' ? q : NULL);
            if (!ok)
                break;
            if (!suppress) {
                if (conv == 'p')
                    *va_arg(ap, void **) = (void *)(unsigned int)v;
                else if (size == 'q')
                    memcpy(va_arg(ap, void *), q, 8);
                else if (size == 'h')
                    *va_arg(ap, short *) = (short)v;
                else if (size == 'H')
                    *va_arg(ap, signed char *) = (signed char)v;
                else if (size == 'l')
                    *va_arg(ap, long *) = (long)v;
                else
                    *va_arg(ap, int *) = (int)v;
                assigned++;
            }
        } else if (conv == 's' || conv == 'c' || conv == '[') {
            /* %s stops at white space, %[ at a character outside its set
             * (set[0..nset), within the format itself), %c only at its
             * width (default 1); %s and %[ add a NUL, %c does not */
            set = NULL;
            nset = 0;
            neg = 0;
            if (conv == '[') {
                neg = *fmt == '^';
                if (neg)
                    fmt++;
                set = fmt;
                if (*fmt == ']')
                    fmt++;              /* a leading ] is a member */
                while (*fmt && *fmt != ']')
                    fmt++;
                nset = fmt - set;
                if (*fmt)
                    fmt++;
            }
            if (width <= 0)
                width = conv == 'c' ? 1 : 32767;
            out = suppress ? NULL : va_arg(ap, char *);
            n = 0;
            while (n < width) {
                c = sget(in);
                if (c == EOF || (conv == 's' && scan_space(c)) || (conv == '[' && !in_set(c, set, nset, neg))) {
                    sunget(in, c);
                    break;
                }
                if (out != NULL)
                    out[n] = c;
                n++;
            }
            if (n == 0 || (conv == 'c' && n < width))
                break;
            if (out != NULL) {
                if (conv != 'c')
                    out[n] = 0;
                assigned++;
            }
        } else if (conv == 'e' || conv == 'f' || conv == 'g' || conv == 'E' || conv == 'F' || conv == 'G'
                   || conv == 'a' || conv == 'A') {
            /* a program without floating point has nowhere to store one */
            if (!__fp_scan || !__fp_scan(get_from, unget_to, in, width > 0 ? width : 32767, size,
                                          suppress ? NULL : va_arg(ap, void *), in->c99))
                break;
            if (!suppress)
                assigned++;
        } else {
            break;
        }
        converted = 1;
    }
    /* EOF only for an input failure before any conversion; a matching
     * failure, or a failure after one, gives the count */
    if (!converted && assigned == 0 && in->ended)
        return EOF;
    return assigned;
}

/* The stream f, or (f null) the string s; c99, whether the floating
 * conversions read C99's hexadecimal, inf and nan. */
static int scan_from(FILE *f, const char *s, int c99, const char *fmt, va_list ap)
{
    struct source in;

    in.f = f;
    in.s = s;
    in.count = 0;
    in.ended = 0;
    in.c99 = c99;
    return scan(&in, fmt, ap);
}

/* C99's va_list forms */
int vfscanf(FILE *f, const char *fmt, va_list ap)
{
    return scan_from(f, NULL, 1, fmt, ap);
}

int vscanf(const char *fmt, va_list ap)
{
    return scan_from(stdin, NULL, 1, fmt, ap);
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    return scan_from(NULL, s, 1, fmt, ap);
}

/* C89's: "inf" is no number, as C89 requires */
int scanf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = scan_from(stdin, NULL, 0, fmt, ap);
    va_end(ap);
    return n;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = scan_from(f, NULL, 0, fmt, ap);
    va_end(ap);
    return n;
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = scan_from(NULL, s, 0, fmt, ap);
    va_end(ap);
    return n;
}

/* C99's, which <stdio.h> makes scanf, fscanf and sscanf mean outside
 * strict mode */
int __scanf99(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = scan_from(stdin, NULL, 1, fmt, ap);
    va_end(ap);
    return n;
}

int __fscanf99(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = scan_from(f, NULL, 1, fmt, ap);
    va_end(ap);
    return n;
}

int __sscanf99(const char *s, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = scan_from(NULL, s, 1, fmt, ap);
    va_end(ap);
    return n;
}
