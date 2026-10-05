/* stdio.h - streams (C89 4.9; c89_spec.md 15), implemented in stdio.c.
 *
 * A text stream (opened without "b") writes '\n' as CR LF and reads CR LF
 * as '\n'; the console is a text stream. Files are fully buffered (BUFSIZ);
 * console output is written at every newline and at the end of every
 * output call; setvbuf and setbuf change either. exit (or a return from
 * main) writes and closes every stream, and removes tmpfile's files, which
 * are /tmp/tNNNNN.tmp (/tmp must exist).
 *
 * The printf family supports %d %i %u %x %X %o %c %s %p %n %% and
 * %e %f %g %E %G, with C99's %F (%f, but INF and NAN in capitals); the
 * flags '-', '0', '+', ' ' and '#'; width and precision ('*' accepted);
 * and the length modifiers 'h' (short), 'l' (long), 'L' (long double,
 * which is double), and C99's 'll' and 'j' (long long, which strict mode
 * lacks). %p is six lower-case hex digits; a null %s prints "(null)". The
 * scanf family supports every C89 conversion, %F, and the same length
 * modifiers; a '-' inside a %[ set makes a range (%[a-z]).
 *
 * FOPEN_MAX counts the three standard streams, but they hold no MOS
 * handle, so up to eight files may be open besides them (MOS's limit,
 * shared with anything else holding handles).
 */

#ifndef _STDIO_H
#define _STDIO_H

#define NULL ((void *)0)
#define EOF (-1)

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

typedef long fpos_t;

#define BUFSIZ 4096
#define FOPEN_MAX 8
#define FILENAME_MAX 256
#define L_tmpnam 16
#define TMP_MAX 65535

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* A stream. getc and putc below read and write its buffer directly while
 * __ptr is short of __rend or __wend; everything else is stdio.c's. */
typedef struct __file {
    unsigned char __kind;       /* closed, file, console out or console in */
    unsigned char __flags;      /* read, write, append, text, EOF, error ... */
    unsigned char __state;      /* the buffer holds nothing, a read or a write */
    unsigned char __bmode;      /* _IOFBF, _IOLBF or _IONBF */
    int __size;                 /* the buffer's size */
    unsigned char *__ptr;       /* the next byte in the buffer */
    unsigned char *__rend;      /* getc's own limit: the buffer's end, or a CR */
    unsigned char *__wend;      /* putc's */
    unsigned char *__buf;
    unsigned char *__end;       /* reading: the end of the bytes read;
                                 * writing: the first byte not yet written */
    long __base;                /* the file offset of __buf[0] */
    long __fsize;               /* the file's size, as far as this stream knows */
    long __mospos;              /* where MOS's own file pointer is */
    unsigned char __handle;
    unsigned char __unget;      /* ungetc's character */
    unsigned char __one;        /* an unbuffered stream's buffer */
    unsigned int __tmpno;       /* tmpfile's: its number, to remove it at close */
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define stdin stdin
#define stdout stdout
#define stderr stderr

FILE *fopen(const char *filename, const char *mode);
FILE *freopen(const char *filename, const char *mode, FILE *stream);
int fclose(FILE *stream);
FILE *tmpfile(void);
char *tmpnam(char *s);
int fflush(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);

int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int c, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int putchar(int c);

#define getc(f) ((f)->__ptr < (f)->__rend ? *(f)->__ptr++ : fgetc(f))
#define putc(c, f) ((f)->__ptr < (f)->__wend ? (*(f)->__ptr++ = (c)) : fputc((c), (f)))
#define getchar() getc(stdin)
#define putchar(c) putc((c), stdout)

char *fgets(char *s, int n, FILE *stream);
char *gets(char *s);
int fputs(const char *s, FILE *stream);
int puts(const char *s);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

int remove(const char *path);
int rename(const char *old, const char *new_name);

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *s, const char *format, ...);
int vprintf(const char *format, char *arg);         /* char *: <stdarg.h>'s va_list */
int vfprintf(FILE *stream, const char *format, char *arg);
int vsprintf(char *s, const char *format, char *arg);

int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *s, const char *format, ...);

#endif
