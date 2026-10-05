/* cpp.c - the preprocessor: a C source to a preprocessed source (.i) for
 * cc1: C89's preprocessor (docs/c89_spec.md).
 *
 *     cpp in.c out.i [-I dir]... [-D name[=value]]... [-U name]... [-date YYYYMMDD]
 *         [-time HHMMSS] [-w] [-Werror] [-v]
 *
 * Line by line: comments become a space (a comment spanning lines leaves
 * each line it touches, so line numbers stay in step), a line whose first
 * character is '#' is a directive, and any other line in an active region
 * is macro-expanded and written. The output carries "# <line> "<file>""
 * markers wherever its line numbering would otherwise drift from the
 * source's (after an #include, a skipped region, or blank lines), so cc1's
 * diagnostics name the original file and line.
 *
 * Macros are expanded as C89 3.8.3 has it (the engine is described where
 * it is, below): object-like and function-like, # and ##. Only
 * one input file is open at a time: an #include records where its parent
 * stopped, closes it, and reopens it there afterwards (rd_open_at), so
 * nesting to depth 8 never needs more of MOS's file handles.
 *
 * #include <name> searches the -I directories, then /usrlib, then /lib;
 * #include "name" first searches the including file's directory
 * (driver.md section 4).
 *
 * Where it sits: the first pass. The driver runs it on each .c input
 * (with the user's -I, -D and -U, and the clock's date and time); its
 * .i output is plain C with no directives left except the line markers,
 * "#pragma weak" and "#asm" blocks, which cc1 reads.
 *
 * The phases of C89 2.1.1.2, as done here, one logical line at a time:
 *   1-2. read_logical: a physical line (CR dropped), trigraphs, then
 *        lines ending in a backslash spliced to the next;
 *   3.   strip_comments: each comment becomes one space;
 *   4.   process: a directive is obeyed (directive), any other line in an
 *        active region is macro-expanded (expand) and written (emit).
 *
 * The main structures: the macro table (struct macro, chained in a hash
 * table, names and encoded replacement lists in mtext); the conditional
 * stack (cond_state and friends: one entry per open #if); the include
 * stack (struct saved_file); and, for expansion, a stack of text frames
 * with the buffers xtext and obuf (described at "macro expansion").
 *
 * Map: state, diagnostics, the macro table, characters, input, macro
 * expansion, output, #if expressions, files, directives, the translation
 * unit (the main loop), main (options and predefined macros).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"
#include "io.h"
#include "int24.h"
#include "int32.h"
#include "int64.h"
#include "rd.h"

#define CPP_LINE 4096           /* one logical source line (C89: 509) */
#define OUT_MAX 16384           /* one line after macro expansion */
#define MAX_MACROS 1200         /* defined at once (C89: 1024); #undef frees an entry */
#define MACRO_TEXT 32000        /* macro names and replacements */
#define MACRO_HASH 2048
#define MAX_DEPTH 8             /* #include nesting */
#define MAX_COND 32             /* #if nesting */
#define MAX_INCDIRS 16
#define CPP_PATH 128

/* ---- state ------------------------------------------------------------------------ */

static FILE *out;
static char *out_path;
static int errors;
static int warnings_off;
static int werror;
static int verbose;

/* the current input, and where each enclosing file stopped */
static struct rd src;
static char cur_path[CPP_PATH]; /* the file being read */
static char pres_path[CPP_PATH];        /* its name as markers, messages and __FILE__ give it (#line) */
static int phys_line;           /* physical lines read from it */
static int line_delta;          /* a line's number is its physical one plus this (#line) */
static int cur_line;            /* the number of the (logical) line being processed */
static int depth;

/* An including file, while its #include is read: what pop_file needs to
 * resume it, from the byte after the #include line. */
struct saved_file {
    char path[CPP_PATH];
    char pres[CPP_PATH];
    int line;           /* phys_line */
    int delta;          /* line_delta */
    int offset;         /* where reading stopped (rd_tell) */
    int cond_base;      /* file_cond_base */
};

/* __DATE__ and __TIME__: -date and -time, or C89's "some valid date" */
static char date_text[16] = "\"Jan  1 1980\"";
static char time_text[16] = "\"00:00:00\"";

static struct saved_file stack[MAX_DEPTH];
static int file_cond_base;      /* the #if depth when the current file began */

static char *incdirs[MAX_INCDIRS + 2];
static int nincdirs;

/* conditionals: a stack with one entry per #if (or #ifdef, #ifndef) not
 * yet closed by its #endif. Each entry's state says what its group does
 * now; a line is processed only if the top entry is C_ACTIVE (or there is
 * none). #elif and #else move the top entry on:
 *
 *     C_WAITING --(#elif true, #else)--> C_ACTIVE --(#elif, #else)--> C_DONE
 *
 * and C_DONE and C_OUTER never change. An #if met inside a skipped group
 * is pushed as C_OUTER, unevaluated, only so that its #endif is matched. */
#define C_ACTIVE 0              /* this branch is being processed */
#define C_WAITING 1             /* no branch taken yet */
#define C_DONE 2                /* a branch was taken; skip the rest */
#define C_OUTER 3               /* inside a skipped region; skip all */

static int cond_state[MAX_COND];
static int cond_else[MAX_COND]; /* its #else has been seen */
static int cond_line[MAX_COND]; /* where its #if was, for "unterminated #if" */
static int ncond;

/* output line tracking */
static int out_line;            /* the source line the next output line would be */
static int out_synced;          /* 0: the next line needs a marker */

/* one line at a time: raw is the logical line as read, line the same with
 * its comments removed */
static char raw[CPP_LINE + 2];
static char line[CPP_LINE + 2];
static int in_comment;          /* a comment is open at the end of the line */
static int comment_line;        /* where it began, for "unterminated comment" */

/* ---- diagnostics ---------------------------------------------------------------- */

static void error(char *msg)
{
    fprintf(stderr, "%s:%d: error: %s\n", pres_path, cur_line, msg);
    errors++;
}

static void error_s(char *msg, char *arg)
{
    fprintf(stderr, "%s:%d: error: %s%s\n", pres_path, cur_line, msg, arg);
    errors++;
}

static void warning(char *msg)
{
    if (warnings_off)
        return;
    if (werror) {
        error(msg);
        return;
    }
    fprintf(stderr, "%s:%d: warning: %s\n", pres_path, cur_line, msg);
}

/* Give up: no partial output is ever left behind. */
static void fatal(char *msg)
{
    error(msg);
    rd_close(&src);                     /* MOS closes nothing at exit */
    if (out != NULL) {
        fclose(out);
        remove(out_path);
    }
    exit(200);
}

/* ---- the macro table -------------------------------------------------------------- */

/* A replacement list is stored with its parameters encoded: each use
 * of one is a marker byte followed by the parameter's number plus 1. The
 * markers are control characters, which C source does not otherwise use.
 * So "#define max(a, b) ((a) > (b) ? (a) : (b))" is stored as
 * "((\1\1) > (\1\2) ? (\1\1) : (\1\2))", writing a byte of value k as \k:
 * M_ARG, then the parameter's number plus 1. Substitution then needs no
 * name lookups: the byte after a marker indexes the call's arguments.
 * The plus 1 keeps that byte from being 0, the string's end. */
#define M_ARG 1                 /* the argument, fully macro-expanded */
#define M_RAW 2                 /* the argument as written (an operand of ##) */
#define M_STR 3                 /* the argument as a string literal (#) */
#define M_PASTE 4               /* ## */
#define M_PAINT 5               /* before a name that must never be expanded (C89 3.8.3.4) */
#define MAX_PARAMS 32           /* parameters of one macro (C89: 31) */

struct macro {
    int name;           /* offsets into mtext */
    int repl;
    int params;         /* the parameter names, "a,b" (to compare redefinitions) */
    int nparams;        /* -1: an object-like macro */
    int active;         /* being rescanned: not expanded again inside itself */
    int next;           /* hash chain, or the free list */
};

static struct macro macros[MAX_MACROS];
static int nmacros;             /* entries ever used */
static int free_macro = -1;     /* entries #undef gave back, chained by next */
static char mtext[MACRO_TEXT];  /* every name and replacement, never freed */
static int mtext_used;
static int mhash[MACRO_HASH];   /* each bucket's first macro, or -1 */
static int live_macros;         /* defined now, and the most at once (-v) */
static int peak_macros;

/* The multiply-by-31 string hash, cut to a bucket of mhash. Collisions
 * are chained through macros[].next. */
static int hash_name(char *s)
{
    unsigned h;

    h = 0;
    while (*s) {
        h = h * 31 + (unsigned char)*s;
        s++;
    }
    return h & (MACRO_HASH - 1);
}

static int find_macro(char *name)
{
    int m;

    for (m = mhash[hash_name(name)]; m >= 0; m = macros[m].next)
        if (strcmp(mtext + macros[m].name, name) == 0)
            return m;
    return -1;
}

/* A copy of s at the end of mtext; returns its offset. */
static int store(char *s)
{
    int n;
    int at;

    n = strlen(s);
    if (mtext_used + n + 1 > MACRO_TEXT)
        fatal("macro text too large (a cpp table limit; raise MACRO_TEXT)");
    at = mtext_used;
    strcpy(mtext + at, s);
    mtext_used = mtext_used + n + 1;
    return at;
}

/* name with nparams parameters ("a,b" in params; -1 and "" for an
 * object-like macro) and its encoded replacement. C89 3.8.3 allows a
 * macro to be defined again only identically (same parameters, same
 * replacement with white space normalised); that is accepted and changes
 * nothing, anything else is an error and the first definition stays. */
static void define(char *name, int nparams, char *params, char *repl)
{
    int m;
    int h;

    m = find_macro(name);
    if (m >= 0) {
        if (macros[m].nparams != nparams || strcmp(mtext + macros[m].params, params) != 0
            || strcmp(mtext + macros[m].repl, repl) != 0)
            error_s("macro redefined differently: ", name);
        return;
    }
    if (free_macro >= 0) {
        m = free_macro;
        free_macro = macros[m].next;
    } else {
        if (nmacros >= MAX_MACROS)
            fatal("too many macros (a cpp table limit; raise MAX_MACROS)");
        m = nmacros;
        nmacros++;
    }
    h = hash_name(name);
    macros[m].name = store(name);
    macros[m].repl = store(repl);
    macros[m].params = store(params);
    macros[m].nparams = nparams;
    macros[m].active = 0;
    macros[m].next = mhash[h];
    mhash[h] = m;
    live_macros++;
    if (live_macros > peak_macros)
        peak_macros = live_macros;
}

/* #undef: the macro leaves its hash chain and its entry goes on the free
 * list; nothing happens if name is not a macro. */
static void undefine(char *name)
{
    int h;
    int m;
    int prev;

    h = hash_name(name);
    prev = -1;
    for (m = mhash[h]; m >= 0; m = macros[m].next) {
        if (strcmp(mtext + macros[m].name, name) == 0) {
            if (prev < 0)
                mhash[h] = macros[m].next;
            else
                macros[prev].next = macros[m].next;
            macros[m].next = free_macro;        /* the entry is free again (its text is not) */
            free_macro = m;
            live_macros--;
            return;
        }
        prev = m;
    }
}

/* ---- characters ------------------------------------------------------------------ */

static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\f' || c == '\v';
}

static char *skip_ws(char *p)
{
    while (is_space(*p))
        p++;
    return p;
}

/* An identifier at p into buf (up to 63 characters); returns the end. */
static char *ident(char *p, char *buf)
{
    int n;

    n = 0;
    while (is_alpha(*p) || is_digit(*p)) {
        if (n < 63) {
            buf[n] = *p;
            n++;
        }
        p++;
    }
    buf[n] = 0;
    return p;
}

/* The end of the string or character literal starting at p (at its quote);
 * an unterminated one ends with the line. */
static char *literal_end(char *p)
{
    int q;

    q = *p;
    p++;
    while (*p && *p != q) {
        if (*p == '\\' && p[1])
            p++;
        p++;
    }
    if (*p == q)
        p++;
    return p;
}

/* The end of the preprocessing number starting at p (C89 3.1.8): digits,
 * letters, '_' and '.', and a sign straight after an e or E, so that
 * 1e+5 and 0x1Fu are each one token and nothing inside them is taken for
 * a macro name. p is past the first character, so p[-1] is safe. */
static char *number_end(char *p)
{
    while (is_alpha(*p) || is_digit(*p) || *p == '.'
           || ((*p == '+' || *p == '-') && (p[-1] == 'e' || p[-1] == 'E')))
        p++;
    return p;
}

/* ---- input ------------------------------------------------------------------------ */

/* One physical line into raw from raw[n] on ('\r' dropped, 0x1A ends the
 * file); 0 at end of file. */
static int read_line_at(int n)
{
    int c;

    c = rd_getc(&src);
    if (c < 0 || c == 0x1A)
        return 0;
    while (c >= 0 && c != '\n' && c != 0x1A) {
        if (c != '\r') {
            if (n >= CPP_LINE)
                fatal("line longer than 4096 characters (after splicing; a cpp limit, CPP_LINE)");
            raw[n] = c;
            n++;
        }
        c = rd_getc(&src);
    }
    raw[n] = 0;
    phys_line++;
    cur_line = phys_line + line_delta;
    return 1;
}

static int read_line(void)
{
    return read_line_at(0);
}

static int strict;              /* -ansi: trigraphs are replaced (c89_spec.md section 1) */

/* C89 2.1.1.2's first phase: the nine trigraphs, replaced in strict mode;
 * in the default mode they stay, with a warning (c89_spec.md 1). raw is
 * rewritten in place from raw[at] on (a trigraph's three characters
 * become one, so the write position n never passes the read one); at
 * lets a spliced continuation be done on its own, as phase 1 is per
 * physical line. */
static void trigraphs(int at)
{
    char *from;
    char *to;
    char *r;
    int n;

    from = "=(/)'<!>-";
    to = "#[\\]^{|}~";
    n = at;
    for (r = raw + at; *r; r++) {
        if (r[0] == '?' && r[1] == '?' && r[2] != 0 && strchr(from, r[2]) != NULL) {
            if (!strict) {
                warning("a trigraph, which only strict mode (-ansi) replaces");
            } else {
                raw[n] = to[strchr(from, r[2]) - from];
                n++;
                r = r + 2;
                continue;
            }
        }
        raw[n] = *r;
        n++;
    }
    raw[n] = 0;
}

/* One logical line into raw (C89 2.1.1.2, phases 1 and 2): each physical
 * line's trigraphs, then a line ending in a backslash joined to the next.
 * cur_line is the first physical line's number; 0 at end of file. */
static int read_logical(void)
{
    int start;
    int n;

    if (!read_line())
        return 0;
    trigraphs(0);
    start = cur_line;
    for (;;) {
        n = strlen(raw);
        if (n == 0 || raw[n - 1] != '\\')
            break;
        /* the next physical line is read over the backslash */
        if (!read_line_at(n - 1)) {
            raw[n - 1] = 0;
            warning("a backslash at the end of the file");
            break;
        }
        trigraphs(n - 1);
    }
    cur_line = start;
    return 1;
}

/* Whether p (after a '#') names the directive d. */
static int is_directive(char *p, char *d)
{
    int n;

    p = skip_ws(p);
    n = strlen(d);
    return strncmp(p, d, n) == 0 && !is_alpha(p[n]) && !is_digit(p[n]);
}

/* Is the current line in a group being processed? */
static int active(void)
{
    return ncond == 0 || cond_state[ncond - 1] == C_ACTIVE;
}

/* raw -> line with comments replaced by a space (a comment continuing past
 * the end of the line leaves nothing). Literals are copied untouched, so
 * a slash-star inside a string does not open a comment. in_comment carries
 * an open comment into the next line. // comments are a C99 feature,
 * recognised only in the default mode. */
static void strip_comments(void)
{
    char *p;
    char *e;
    int o;

    p = raw;
    o = 0;
    while (*p) {
        if (in_comment) {
            if (p[0] == '*' && p[1] == '/') {
                in_comment = 0;
                p = p + 2;
                line[o] = ' ';
                o++;
            } else {
                p++;
            }
        } else if (p[0] == '/' && p[1] == '*') {
            in_comment = 1;
            comment_line = cur_line;
            p = p + 2;
        } else if (p[0] == '/' && p[1] == '/' && !strict) {
            break;                      /* a // comment; in strict mode, as in C89, two slashes */
        } else if (*p == '"' || *p == '\'') {
            e = literal_end(p);
            while (p < e) {
                line[o] = *p;
                o++;
                p++;
            }
        } else {
            line[o] = *p;
            o++;
            p++;
        }
    }
    line[o] = 0;
}

/* ---- macro expansion --------------------------------------------------------------- */

/* C89 3.8.3. The text being scanned is a stack of frames: the source line
 * at the bottom, and above it each macro's replacement while it is being
 * rescanned, so that a replacement is rescanned together with the text
 * after it (a function-like macro's name at the end of one can take its
 * arguments from there). A macro is disabled while its frame lasts; a name
 * met while its macro is disabled is painted (M_PAINT) and never expanded,
 * even if it is carried somewhere else in an argument.
 *
 * A call's arguments are collected across frames, and across source lines
 * for a call in the text (frame 0 then takes more lines). Each parameter
 * becomes its argument fully expanded on its own, or as written next to
 * ##, or as a string after #; the result is pushed as the macro's frame.
 *
 * Arguments and substituted replacements live in xtext, which is emptied
 * for each source line. The text a scan produces goes to outbuf (the
 * line's output, with paint marks dropped) or, while an argument is being
 * expanded, to the top of obuf, a stack, from which it is copied to xtext.
 *
 * For a learner, the rules this implements:
 *
 * - Expansion and rescanning. When a macro name is met, it is replaced
 *   by its replacement list, and the result is scanned again for more
 *   macro names ("rescanned"). With "#define A B+1" and "#define B 2",
 *   A becomes B+1, and the rescan turns that into 2+1. Here the
 *   replacement is pushed as a new frame and the scan simply carries on
 *   reading, now from that frame.
 *
 * - No recursion (C89 3.8.3.4). A macro's name inside its own expansion
 *   is not replaced again, so "#define x (x+1)" gives (x+1) and stops.
 *   The textbook mechanism is a "hide set": every token carries the set
 *   of macros that produced it, and a name whose own macro is in its set
 *   is left alone. This preprocessor approximates that more cheaply: a
 *   macro is marked active while its frame is being read (struct macro's
 *   active), and a name met while its macro is active is "painted": an
 *   M_PAINT byte is put before it. A painted name is never expanded
 *   afterwards, even when it travels out of the frame inside an argument
 *   and is rescanned somewhere else; the mark is dropped only on output
 *   (or when ## makes a new token from it).
 *
 * - Function-like macros. "#define sq(n) ((n)*(n))" is replaced only when
 *   its name is followed by '('; the arguments are split at top-level
 *   commas (call). Each argument is first macro-expanded on its own, as
 *   if it were a whole line (expand_arg), and that is what replaces the
 *   parameter; the substituted list is then rescanned with the rest of
 *   the text.
 *
 * - # and ##. "#n" makes the argument as written (not expanded) into a
 *   string literal (stringify): with "#define str(n) #n", str(a + b) is
 *   "a + b". "x ## y" joins the tokens either side into one; an argument
 *   next to ## is also used as written, not expanded first. So with
 *   "#define cat(a, b) a ## b", cat(x, 1) is the single name x1, which
 *   the rescan may then expand. */

#define MAX_FRAMES 64           /* macro-within-macro nesting */
#define XTEXT 48000             /* one source line's arguments and expansions (c-testsuite 00200.c needs over 32000) */
#define OBUF 16000              /* arguments being expanded */
#define IBUF (CPP_LINE * 4)     /* a source line and the lines a macro call spans */

/* One text being scanned: frames[0] is the source line (in ibuf), each
 * frame above it a macro's substituted replacement (in xtext) or an
 * argument being expanded. */
struct frame {
    char *p;            /* the next character */
    int m;              /* the macro re-enabled when the frame is done, or -1 */
};

static struct frame frames[MAX_FRAMES];
static int nframes;
static int fbase;               /* the lowest frame the current scan reads */
static int pulling;             /* frames[0] is source text that may take more lines */
static int sep_pending;         /* a frame ended: keep what follows apart from it */
static char xtext[XTEXT];
static int xused;
static char obuf[OBUF];
static int otop;
static int sink_at;             /* where the current scan's output starts in obuf; -1: outbuf */
static char ibuf[IBUF + 2];     /* frames[0]'s text: the line, and any lines a call took */
static int ilen;
static char pending[CPP_LINE + 2];      /* a directive line met while taking more lines */
static int pending_line;
static int have_pending;

static char outbuf[OUT_MAX + 2];        /* the output line being built */
static int olen;

static int streaming;            /* a text line: outbuf may be written out as it fills */
static int line_started;        /* part of the current output line is written already */
static int out_last = ' ';      /* the last character put, even if written out */
static void flush_out(void);

/* One character to outbuf. A text line that outgrows it is written out
 * in pieces (streaming); outbuf is also used for a directive's expanded
 * text (#if, #include, #line), which must fit whole, being parsed after. */
static void put(int c)
{
    if (olen >= OUT_MAX) {
        if (!streaming)
            fatal("line too long after macro expansion");
        flush_out();
    }
    outbuf[olen] = c;
    olen++;
    out_last = c;
}

/* One character to the current scan's output: outbuf (paint marks
 * dropped, as the output is final) or, inside expand_arg, the top of obuf
 * (paint marks kept, as the text will be scanned again). */
static void sput(int c)
{
    if (sink_at < 0) {
        if (c != M_PAINT)
            put(c);
        return;
    }
    if (otop >= OBUF)
        fatal("macro arguments too large (a cpp table limit; raise OBUF)");
    obuf[otop] = c;
    otop++;
}

static void sput_n(char *s, int n)
{
    while (n > 0) {
        sput(*s);
        s++;
        n--;
    }
}

/* Would a and b, written side by side, run together into one token (so a
 * macro's replacement needs a space against its neighbour)? Macro
 * expansion works on tokens, but this preprocessor keeps text; so where
 * two pieces of text meet (an expansion and what is beside it), a space
 * is put between them if they would otherwise read as one token: with
 * "#define m -1", "-m" must give "- -1", not "--1". The test is
 * conservative (a space too many is harmless): names and numbers
 * together, '.' next to a digit or a '.', an 'L' before a quote (which
 * would make a wide literal), and operator characters that could form a
 * longer operator. A paint mark, which stands before a name, always
 * counts as joining. */
static int joins(int a, int b)
{
    if (a == M_PAINT || b == M_PAINT)
        return 1;
    if ((is_alpha(a) || is_digit(a)) && (is_alpha(b) || is_digit(b)))
        return 1;
    if ((a == '.' && is_digit(b)) || (is_digit(a) && b == '.') || (a == '.' && b == '.'))
        return 1;
    if (a == '"' || a == '\'' || b == '"' || b == '\'')
        return is_alpha(a) && a == 'L';
    return strchr("+-<>&|=!*/%^#:", a) != NULL && strchr("+-<>&|=#:", b) != NULL;
}

/* A space between the output so far and c, if they would run together. A
 * '.' joins a number but not a name (vers2.h is vers2 . h). The output so
 * far is outbuf or the current obuf region; if outbuf was just written
 * out (streaming), out_last stands for its end. */
static void sput_sep(int c)
{
    char *s;
    int n;
    int k;
    int last;

    if (c == 0 || is_space(c))
        return;
    if (sink_at < 0 && olen == 0) {
        if (!is_space(out_last) && joins(out_last, c))
            sput(' ');                  /* (what came before was written out) */
        return;
    }
    if (sink_at < 0) {
        s = outbuf;
        n = olen;
    } else {
        s = obuf + sink_at;
        n = otop - sink_at;
    }
    last = n > 0 ? (unsigned char)s[n - 1] : ' ';
    if (is_space(last) || !joins(last, c))
        return;
    if (c == '.' && is_digit(last)) {
        k = n;
        while (k > 0 && (is_alpha(s[k - 1]) || is_digit(s[k - 1])))
            k--;
        if (!is_digit(s[k]))
            return;                     /* the end of a name */
    }
    sput(' ');
}

/* xtext's allocation: a string grows at xused and is ended with xend. */
static void xput(int c)
{
    if (xused >= XTEXT)
        fatal("macro expansion too large (a cpp table limit; raise XTEXT)");
    xtext[xused] = c;
    xused++;
}

static char *xend(int start)
{
    xput(0);
    return xtext + start;
}

static void strip_comments(void);
static int read_logical(void);
static int closes_comment(char *s);

/* frames[0] takes the next source line, joined by a space; 0 at the end of
 * the file, or if that line is a directive (kept for process). This is
 * how a call such as "f(1,\n 2)" whose arguments run over several lines
 * is collected: C89 treats the newlines inside it as white space. Lines
 * wholly inside a comment are passed over. cur_line stays the call's
 * first line, which the output line is numbered by. */
static int more_line(void)
{
    char *p;
    int save;
    int n;

    if (have_pending)
        return 0;
    save = cur_line;
    for (;;) {
        if (!read_logical()) {
            cur_line = save;
            return 0;
        }
        if (in_comment && !closes_comment(raw))
            continue;
        strip_comments();
        break;
    }
    p = skip_ws(line);
    if (*p == '#') {
        /* a directive ends the call's search; process() obeys it next */
        strcpy(pending, line);
        pending_line = cur_line;
        have_pending = 1;
        cur_line = save;
        return 0;
    }
    cur_line = save;
    /* appended to ibuf, which frames[0] points into, so the frame reads on */
    n = strlen(line);
    if (ilen + n + 2 > IBUF)
        fatal("a macro call spans too many lines");
    ibuf[ilen] = ' ';
    strcpy(ibuf + ilen + 1, line);
    ilen = ilen + n + 1;
    return 1;
}

/* The character the scan is at, after leaving frames that are done (their
 * macros enabled again) and, at the end of a source line while a macro
 * call is being read (in_call), taking the next line when allowed; 0 at
 * the end. The scan never leaves frames[fbase]: while an argument is
 * expanded, its end is the end of the text. Leaving a frame sets
 * sep_pending, so that what follows an expansion is kept apart from it. */
static int in_call;

static int cur(void)
{
    struct frame *f;

    for (;;) {
        f = &frames[nframes - 1];
        if (*f->p)
            return (unsigned char)*f->p;
        if (nframes - 1 > fbase) {
            if (f->m >= 0)
                macros[f->m].active = 0;
            nframes--;
            sep_pending = 1;
            continue;
        }
        if (in_call && fbase == 0 && pulling && more_line())
            continue;
        return 0;
    }
}

/* The scan position in the top frame, and moving it on. */
static char *at(void)
{
    return frames[nframes - 1].p;
}

static void skip_to(char *p)
{
    frames[nframes - 1].p = p;
}

/* text becomes the top frame, to be scanned next; m (or -1 for an
 * argument) is disabled until the frame is done. A space goes before the
 * text if it would otherwise run into the output so far. */
static void push_frame(char *text, int m)
{
    if (nframes >= MAX_FRAMES)
        fatal("macros nested too deeply");
    if (m >= 0)
        macros[m].active = 1;
    frames[nframes].p = text;
    frames[nframes].m = m;
    nframes++;
    sput_sep((unsigned char)*skip_ws(text));
}

/* The end of a literal at p, which may be L"..." or L'...'. */
static char *any_literal_end(char *p)
{
    return literal_end(*p == 'L' ? p + 1 : p);
}

static int starts_literal(char *p)
{
    return *p == '"' || *p == '\'' || (*p == 'L' && (p[1] == '"' || p[1] == '\''));
}

static void scan(void);

/* An argument, fully macro-expanded on its own (C89 3.8.3.1), into xtext.
 * A nested scan: the argument is pushed as a frame that becomes the
 * scan's floor (fbase), so the scan stops at its end and cannot take
 * text after it, nor more source lines (pulling 0); its output goes to a
 * fresh region at the top of obuf (sink_at), since the caller's output
 * may itself be an obuf region. The result is copied to xtext and obuf
 * popped, and the caller's scan state is put back. Macros that are
 * active in the caller stay active here, so their names in the argument
 * are painted. */
static char *expand_arg(char *a)
{
    int save_base;
    int save_pull;
    int save_sink;
    int start;
    int i;

    save_base = fbase;
    save_pull = pulling;
    save_sink = sink_at;
    fbase = nframes;
    pulling = 0;
    sink_at = otop;
    push_frame(a, -1);
    scan();
    nframes = fbase;
    start = xused;
    for (i = sink_at; i < otop; i++)
        xput(obuf[i]);
    otop = sink_at;
    fbase = save_base;
    pulling = save_pull;
    sink_at = save_sink;
    return xend(start);
}

/* # a: the argument's spelling as a string literal, white space made one
 * space and none at either end, and \ and " escaped inside literals
 * (C89 3.8.3.2), so str("x\n") is "\"x\\n\"". Paint marks are not part of
 * the spelling and are left out. */
static void stringify(char *a)
{
    char *e;

    xput('"');
    a = skip_ws(a);
    while (*a) {
        if (*a == '"' || *a == '\'') {
            e = literal_end(a);
            while (a < e) {
                if (*a == '\\' || *a == '"')
                    xput('\\');
                xput(*a);
                a++;
            }
        } else if (is_space(*a)) {
            a = skip_ws(a);
            if (*a)
                xput(' ');
        } else {
            if (*a != M_PAINT)
                xput(*a);
            a++;
        }
    }
    xput('"');
}

/* A space in the substitution from start on, before c, if an argument and
 * the text beside it would otherwise run together into one token. */
static void xsep(int start, int c)
{
    int last;

    if (xused <= start || c == 0 || is_space(c))
        return;
    last = (unsigned char)xtext[xused - 1];
    if (!is_space(last) && joins(last, c))
        xput(' ');
}

/* An argument as written, without the white space at either end (##);
 * after a ##, without a paint mark on its first token, which is joined
 * into a new one. */
static void raw_arg(char *a, int pasted)
{
    char *e;

    a = skip_ws(a);
    if (pasted && *a == M_PAINT)
        a++;
    e = a + strlen(a);
    while (e > a && is_space(e[-1]))
        e--;
    while (a < e) {
        xput(*a);
        a++;
    }
}

/* A function-like macro's name was just read: if a '(' follows (white
 * space and line ends may come between), its arguments are collected and
 * substituted and the result is pushed for rescanning; 0 if none follows.
 *
 * Three steps. Collect: characters up to the matching ')' go into xtext,
 * one string per argument, split at commas outside nested parentheses;
 * literals are copied whole, so a comma or parenthesis inside one does
 * not count. The text may come from several frames and, for a call in
 * the source, from following lines. Expand: each parameter used plainly
 * (M_ARG) has its argument expanded once, however often it is used.
 * Substitute: the encoded replacement is copied to xtext with each
 * marker replaced by the argument expanded, as written (M_RAW) or as a
 * string (M_STR), and ## joining its neighbours; that text is pushed as
 * the macro's frame. After an error 1 is returned with nothing pushed. */
static int call(int m, char *name)
{
    char *args[MAX_PARAMS];
    char *exp[MAX_PARAMS];
    char *r;
    char *e;
    char *p;
    int nargs;
    int nest;
    int c;
    int start;
    int i;
    int k;
    int pasted;
    int after_arg;              /* an argument was just put in: keep the next token apart */
    int save_in;
    char nb[48];

    save_in = in_call;
    in_call = 1;                        /* the call may go on over source lines */
    for (;;) {
        c = cur();
        if (!is_space(c))
            break;
        skip_to(at() + 1);
    }
    if (c != '(') {
        in_call = save_in;
        return 0;
    }
    /* collect the arguments, each ended in xtext by xend at its comma or
     * the closing parenthesis */
    skip_to(at() + 1);
    nargs = 0;
    nest = 0;
    start = xused;
    for (;;) {
        c = cur();
        if (c == 0) {
            error_s("unterminated call of macro ", name);
            in_call = save_in;
            return 1;
        }
        p = at();
        if (starts_literal(p)) {
            e = any_literal_end(p);
            while (p < e) {
                xput(*p);
                p++;
            }
            skip_to(e);
            continue;
        }
        if (c == '(') {
            nest++;
        } else if (c == ')' || (c == ',' && nest == 0)) {
            if (c == ')' && nest > 0) {
                nest--;
            } else {
                if (nargs >= MAX_PARAMS) {
                    error_s("too many arguments to macro ", name);
                    in_call = save_in;
                    return 1;
                }
                args[nargs] = xend(start);
                nargs++;
                start = xused;
                skip_to(p + 1);
                if (c == ')')
                    break;
                continue;
            }
        }
        xput(c);
        skip_to(p + 1);
    }
    in_call = save_in;
    if (macros[m].nparams == 0 && nargs == 1 && *skip_ws(args[0]) == 0)
        nargs = 0;                      /* f() */
    if (nargs != macros[m].nparams) {
        sprintf(nb, " takes %d argument%s, not %d", macros[m].nparams, macros[m].nparams == 1 ? "" : "s", nargs);
        error_s(name, nb);
        return 1;
    }
    /* expand each argument a plain parameter use needs, once (exp[i]) */
    r = mtext + macros[m].repl;
    for (i = 0; i < nargs; i++)
        exp[i] = NULL;
    for (p = r; *p; p++) {
        if (*p == M_ARG && exp[p[1] - 1] == NULL)
            exp[p[1] - 1] = expand_arg(args[p[1] - 1]);
        if (*p == M_ARG || *p == M_RAW || *p == M_STR)
            p++;                        /* past the parameter number */
    }
    /* substitute: the replacement with its markers filled in; pasted says
     * the previous marker was ##, so this piece joins what came before */
    start = xused;
    pasted = 0;
    after_arg = 0;
    for (p = r; *p; p++) {
        c = *p;
        if (c == M_ARG) {
            p++;
            e = exp[*p - 1];
            xsep(start, (unsigned char)*skip_ws(e));
            while (*e) {
                xput(*e);
                e++;
            }
            after_arg = 1;
            pasted = 0;
            continue;
        } else if (c == M_RAW) {
            p++;
            if (!pasted)
                xsep(start, (unsigned char)*skip_ws(args[*p - 1]));
            raw_arg(args[*p - 1], pasted);
            after_arg = 1;
            pasted = 0;
            continue;
        } else if (c == M_STR) {
            p++;
            xsep(start, '"');
            stringify(args[*p - 1]);
            after_arg = 1;
            pasted = 0;
            continue;
        } else if (c == M_PASTE) {
            after_arg = 0;
            while (xused > start && is_space(xtext[xused - 1]))
                xused--;                /* the tokens either side are joined */
            while (is_space(p[1]))
                p++;
            /* the joined token is new: a paint mark on the left one goes */
            k = xused;
            while (k > start && (is_alpha(xtext[k - 1]) || is_digit(xtext[k - 1])))
                k--;
            if (k > start && xtext[k - 1] == M_PAINT) {
                for (i = k - 1; i < xused - 1; i++)
                    xtext[i] = xtext[i + 1];
                xused--;
            }
            pasted = 1;
            continue;
        } else {
            /* replacement text after an argument must not run into it;
             * "+ ## (empty) +" is two tokens */
            if (after_arg && !is_space(c))
                xsep(start, (unsigned char)c);
            xput(c);
            after_arg = 0;
        }
        pasted = 0;
    }
    /* rescanned next, with m disabled until its frame is done */
    push_frame(xend(start), m);
    return 1;
}

/* Scan from the current frame to the end of frames[fbase], expanding
 * macros, into the current output. The text is taken a token at a time
 * as far as macros care: a literal or a preprocessing number is copied
 * whole (nothing inside it is a name), a painted name is copied with its
 * mark, and a name is looked up. An active macro's name is painted; an
 * object-like macro's replacement is pushed as a frame; a function-like
 * one goes to call(), and is copied as a plain name if no '(' follows.
 * The predefined __LINE__, __FILE__, __DATE__ and __TIME__ are not in the
 * macro table but made here (the first two change as the input goes on).
 * Every other character is copied. */
static void scan(void)
{
    char name[64];
    char buf[CPP_PATH * 2 + 4];
    char *p;
    char *e;
    char *q;
    int m;
    int c;

    for (;;) {
        c = cur();
        if (c == 0)
            return;
        if (sep_pending) {
            /* the text after a finished expansion must not run into it */
            sep_pending = 0;
            sput_sep(c);
        }
        p = at();
        if (starts_literal(p)) {
            e = any_literal_end(p);
            sput_n(p, e - p);
            skip_to(e);
        } else if (is_digit(c) || (c == '.' && is_digit(p[1]))) {
            e = number_end(p + 1);
            sput_n(p, e - p);
            skip_to(e);
        } else if (c == M_PAINT) {
            /* a painted name: copied, mark and all, never looked up */
            e = ident(p + 1, name);
            sput_n(p, e - p);
            skip_to(e);
        } else if (is_alpha(c)) {
            e = ident(p, name);
            skip_to(e);
            m = find_macro(name);
            if (m >= 0 && macros[m].active) {
                sput(M_PAINT);          /* never expanded again */
                sput_n(p, e - p);
            } else if (m >= 0 && macros[m].nparams < 0) {
                push_frame(mtext + macros[m].repl, m);
            } else if (m >= 0) {
                if (!call(m, name))
                    sput_n(p, e - p);
            } else if (strcmp(name, "__LINE__") == 0) {
                sprintf(buf, "%d", cur_line);
                sput_n(buf, strlen(buf));
            } else if (strcmp(name, "__FILE__") == 0) {
                sput('"');
                for (q = pres_path; *q; q++) {
                    if (*q == '\\' || *q == '"')
                        sput('\\');
                    sput(*q);
                }
                sput('"');
            } else if (strcmp(name, "__DATE__") == 0) {
                sput_n(date_text, strlen(date_text));
            } else if (strcmp(name, "__TIME__") == 0) {
                sput_n(time_text, strlen(time_text));
            } else {
                sput_n(p, e - p);
            }
        } else {
            sput(c);
            skip_to(p + 1);
        }
    }
}

/* Expand s into outbuf (appending); pull: s is a source line, and a macro
 * call in it may go on over the following lines. xtext is emptied first.
 * Afterwards no macro may stay marked active; scan ends only when every
 * frame above frames[0] is done, which clears them, and the loop makes
 * sure of it. */
static void expand(char *s, int pull)
{
    int n;
    int i;

    n = strlen(s);
    if (n > IBUF)
        fatal("line too long");
    strcpy(ibuf, s);
    ilen = n;
    xused = 0;
    otop = 0;
    sink_at = -1;
    nframes = 0;
    fbase = 0;
    pulling = pull;
    sep_pending = 0;
    push_frame(ibuf, -1);
    scan();
    for (i = 1; i < nframes; i++)
        if (frames[i].m >= 0)
            macros[frames[i].m].active = 0;
    nframes = 0;
}

/* ---- output ------------------------------------------------------------------------- */

/* A line marker, "# 12 "file.c"": cc1 numbers the next output line 12 of
 * file.c, so its diagnostics point at the source, not at the .i. */
static void marker(void)
{
    char buf[CPP_PATH + 20];

    sprintf(buf, "# %d \"%s\"\n", cur_line, pres_path);
    out_str(out, buf);
    out_line = cur_line;
    out_synced = 1;
}

/* Before cur_line's output: a marker, or blank lines, to bring the output's
 * line numbering up to it. A gap of up to 8 lines is filled with blank
 * lines; a larger gap, a step backwards, or a change of file or of
 * numbering (out_synced 0) takes a marker. */
static void start_line(void)
{
    if (!out_synced || cur_line < out_line || cur_line - out_line > 8)
        marker();
    while (out_line < cur_line) {
        out_str(out, "\n");
        out_line++;
    }
    line_started = 1;
}

/* The part of a long line collected so far, written out. */
static void flush_out(void)
{
    if (!line_started)
        start_line();
    outbuf[olen] = 0;
    out_str(out, outbuf);
    olen = 0;
}

/* Write outbuf as the output line for cur_line (finishing it, if part was
 * written out already). */
static void emit(void)
{
    while (olen > 0 && is_space(outbuf[olen - 1]))
        olen--;                         /* what a trailing comment leaves */
    outbuf[olen] = 0;
    if (!line_started)
        start_line();
    out_str(out, outbuf);
    out_str(out, "\n");
    out_line++;
    line_started = 0;
    out_last = ' ';
}

/* ---- #if expressions ---------------------------------------------------------------- */

static char *ep;                /* the expression being parsed */
static int e_uns;               /* the last value's type is unsigned long */
static int e_quiet;             /* inside the unevaluated side of && || ?: */
static int e_bad;

/* #if arithmetic is C99 6.10.1's: every signed type acts as intmax_t and
 * every unsigned one as uintmax_t, 64 bits (int64.c); in strict mode C89
 * 3.8.1's, long and unsigned long, 32 bits (e_fit). The flag e_uns says
 * whether the value is unsigned.
 *
 * The parser is recursive descent with precedence climbing for the binary
 * operators: e_cond (?:) calls e_binary, which reads a unary operand
 * (e_unary, e_primary) and then, while the next operator binds at least
 * as tightly as the level it was asked for, reads the right operand at
 * one level tighter and combines. Asking for one level tighter makes
 * operators of equal precedence group from the left: a - b - c is
 * (a - b) - c. Each function leaves its value in *r and its type in
 * e_uns; ep is the read position. By the time an expression is parsed,
 * "defined" has been replaced and every macro expanded, so any name left
 * is 0 (C89 3.8.1). */

/* Only the first error in an expression is reported. */
static void e_error(char *msg)
{
    if (!e_bad)
        error(msg);
    e_bad = 1;
}

static void e_cond(struct i64 *r);

/* An escape in an #if character constant: a byte, or in a wide one
 * (L'x') a wchar_t, as cc1 reads it. ep is at the backslash. A hex escape
 * keeps its low 8 bits, or in a wide constant is wrapped to a 24-bit int
 * (wrap24, so the host build gets what the Agon's arithmetic gives); an
 * unknown escape such as \q is the character itself. */
static int e_escape(int wide)
{
    int v;
    int n;

    ep++;
    switch (*ep) {
    case 'n': ep++; return 10;
    case 't': ep++; return 9;
    case 'r': ep++; return 13;
    case 'a': ep++; return 7;
    case 'b': ep++; return 8;
    case 'f': ep++; return 12;
    case 'v': ep++; return 11;
    case 'x':
        ep++;
        v = 0;
        while (is_digit(*ep) || (*ep >= 'a' && *ep <= 'f') || (*ep >= 'A' && *ep <= 'F')) {
            v = v * 16 + (is_digit(*ep) ? *ep - '0' : (*ep | 32) - 'a' + 10);
            ep++;
        }
        return wide ? wrap24(v) : v & 255;
    }
    if (*ep >= '0' && *ep <= '7') {
        v = 0;
        n = 0;
        while (n < 3 && *ep >= '0' && *ep <= '7') {
            v = v * 8 + *ep - '0';
            ep++;
            n++;
        }
        return wide ? v : v & 255;
    }
    v = *ep;
    if (*ep)
        ep++;
    return v;
}

/* An integer constant, typed as cc1 types it: C89 3.1.3.2 with a 24-bit
 * int in strict mode, C99 6.4.4.1 (long long too) in the default mode; it
 * is unsigned if that type is unsigned. The value is accumulated in 64
 * bits (i64_muladd reports an overflow past them). The type is the first
 * of the list that holds it: int (up to 0x7FFFFF); unsigned int (up to
 * 0xFFFFFF, for a u suffix or an octal or hex constant); long or unsigned
 * long (32 bits); then long long or unsigned long long. A decimal with
 * bit 31 set and no suffix is unsigned long in C89 but long long in C99. */
static void e_number(struct i64 *r)
{
    int base;
    int d;
    int overflow;
    int suf_u;
    int suf_l;
    int wide;

    base = 10;
    i64_set(r, 0, 0);
    overflow = 0;
    if (*ep == '0') {
        base = 8;
        ep++;
        if (*ep == 'x' || *ep == 'X') {
            base = 16;
            ep++;
        }
    }
    for (;;) {
        if (is_digit(*ep))
            d = *ep - '0';
        else if ((*ep >= 'a' && *ep <= 'f') || (*ep >= 'A' && *ep <= 'F'))
            d = (*ep | 32) - 'a' + 10;
        else
            break;
        if (d >= base)
            break;
        if (i64_muladd(r, (unsigned int)base, (unsigned int)d))
            overflow = 1;
        ep++;
    }
    suf_u = 0;
    suf_l = 0;                          /* 1: l, 2: ll (the default mode's) */
    for (;;) {
        if ((*ep == 'u' || *ep == 'U') && !suf_u) {
            suf_u = 1;
        } else if ((*ep == 'l' || *ep == 'L') && !suf_l) {
            suf_l = 1;
            if (ep[1] == *ep && !strict) {
                suf_l = 2;
                ep++;
            }
        } else {
            break;
        }
        ep++;
    }
    if (is_alpha(*ep) || is_digit(*ep))
        e_error("invalid integer constant in #if");
    wide = r->hi != 0;                  /* beyond 32 bits: long long's */
    if (overflow || (strict && wide))
        e_error(strict ? "integer constant too large (more than 32 bits) in #if"
                       : "integer constant too large (more than 64 bits) in #if");
    if (!suf_u && !suf_l && r->hi == 0 && r->lo <= 0x7FFFFFUL)
        e_uns = 0;                                      /* int */
    else if (!suf_l && (suf_u || base != 10) && r->hi == 0 && r->lo <= 0xFFFFFFUL)
        e_uns = 1;                                      /* unsigned int */
    else if (suf_l < 2 && !wide && (suf_u || (r->lo & 0x80000000UL)))
        e_uns = suf_u || base != 10 || strict;          /* unsigned long (a decimal: C89's) */
    else if (suf_l < 2 && !wide)
        e_uns = 0;                                      /* long */
    else
        e_uns = suf_u || i64_is_neg(r);                 /* long long, else unsigned long long */
}

/* In strict mode #if computes in 32 bits (C89 3.8.1: long and unsigned
 * long): each result is cut to them again, extended by its type. */
static void e_fit(struct i64 *r, int uns)
{
    if (strict)
        i64_from_long(r, r->lo, !uns);
}

/* A parenthesised expression, an integer constant, a character constant
 * (plain char is signed here, so '\xFF' is -1, as in the program), or a
 * name, which after macro expansion can only be one that is not a macro:
 * 0. */
static void e_primary(struct i64 *r)
{
    int d;
    int wide;
    char name[64];

    ep = skip_ws(ep);
    e_uns = 0;
    if (*ep == '(') {
        ep++;
        e_cond(r);
        ep = skip_ws(ep);
        if (*ep != ')')
            e_error("missing ')' in #if");
        else
            ep++;
        return;
    }
    if (is_digit(*ep)) {
        e_number(r);
        return;
    }
    wide = *ep == 'L' && ep[1] == '\'';
    if (wide)
        ep++;
    if (*ep == '\'') {
        ep++;
        if (*ep == '\\')
            d = e_escape(wide);
        else {
            d = (unsigned char)*ep;
            if (*ep)
                ep++;
        }
        if (*ep != '\'')
            e_error("invalid character constant in #if");
        else
            ep++;
        i64_from_long(r, (unsigned long)(long)(!wide && d > 127 ? d - 256 : d), 1);
        return;
    }
    i64_set(r, 0, 0);
    if (is_alpha(*ep)) {
        ep = ident(ep, name);
        return;                         /* an identifier that is not a macro */
    }
    e_error("invalid #if expression");
}

/* The prefix operators - + ~ !, applied right to left by recursion. ! gives
 * an int 0 or 1; the others keep the operand's type. */
static void e_unary(struct i64 *r)
{
    ep = skip_ws(ep);
    if (*ep == '-') {
        ep++;
        e_unary(r);
        i64_neg(r, r);
        e_fit(r, e_uns);
        return;
    }
    if (*ep == '+') {
        ep++;
        e_unary(r);
        return;
    }
    if (*ep == '~') {
        ep++;
        e_unary(r);
        i64_cpl(r, r);
        e_fit(r, e_uns);
        return;
    }
    if (*ep == '!') {
        ep++;
        e_unary(r);
        i64_set(r, 0, i64_is_zero(r));
        e_uns = 0;
        return;
    }
    e_primary(r);
}

/* The binary operator at ep: its precedence (0 if none) and length. Higher
 * binds tighter, from || (1) to * / % (10), as in C. Two-character
 * operators are tried first, so that <= is not read as <. */
static int e_op(int *len)
{
    char a;
    char b;

    ep = skip_ws(ep);
    a = ep[0];
    b = ep[1];
    *len = 2;
    if (a == '|' && b == '|') return 1;
    if (a == '&' && b == '&') return 2;
    if (a == '=' && b == '=') return 6;
    if (a == '!' && b == '=') return 6;
    if (a == '<' && b == '=') return 7;
    if (a == '>' && b == '=') return 7;
    if (a == '<' && b == '<') return 8;
    if (a == '>' && b == '>') return 8;
    *len = 1;
    if (a == '|') return 3;
    if (a == '^') return 4;
    if (a == '&') return 5;
    if (a == '<' || a == '>') return 7;
    if (a == '+' || a == '-') return 9;
    if (a == '*' || a == '/' || a == '%') return 10;
    return 0;
}

/* Precedence climbing: an operand, then every binary operator of
 * precedence level or above, each with its right operand read at the
 * next level up (so equal precedence groups from the left). The usual
 * arithmetic conversions reduce here to one rule: the result is unsigned
 * if either operand is. A comparison or a logical operator gives an int
 * 0 or 1; a shift has the left operand's type. */
static void e_binary(struct i64 *a, int level)
{
    struct i64 b;
    struct i64 q;
    struct i64 m;
    int ua;
    int u;
    int c;
    int p;
    int len;
    int skip;
    char op;
    char op2;

    e_unary(a);
    ua = e_uns;
    for (;;) {
        p = e_op(&len);
        if (p == 0 || p < level)
            break;
        op = ep[0];
        op2 = len == 2 ? ep[1] : 0;
        ep = ep + len;
        if (p <= 2) {
            /* && and ||: the right side is evaluated only if it matters */
            skip = (p == 2 && i64_is_zero(a)) || (p == 1 && !i64_is_zero(a));
            if (skip)
                e_quiet++;
            e_binary(&b, p + 1);
            if (skip)
                e_quiet--;
            if (p == 2)
                i64_set(a, 0, !i64_is_zero(a) && !i64_is_zero(&b));
            else
                i64_set(a, 0, !i64_is_zero(a) || !i64_is_zero(&b));
            ua = 0;
            continue;
        }
        e_binary(&b, p + 1);
        u = ua || e_uns;
        if (p == 8) {
            /* shifts take the left operand's type */
            u = ua;
            if (op == '<')
                i64_shl(a, a, (int)(b.lo & (strict ? 31 : 63)));
            else
                i64_shr(a, a, (int)(b.lo & (strict ? 31 : 63)), !u);
        } else if (p == 6 || p == 7) {
            c = i64_cmp(a, &b, !u);
            if (op == '=')
                c = c == 0;
            else if (op == '!')
                c = c != 0;
            else if (op == '<')
                c = op2 == '=' ? c <= 0 : c < 0;
            else
                c = op2 == '=' ? c >= 0 : c > 0;
            i64_set(a, 0, c);
            u = 0;
        } else if (op == '/' || op == '%') {
            if (i64_is_zero(&b)) {
                if (!e_quiet)
                    e_error("division by zero in #if");
                i64_set(a, 0, 0);
            } else {
                if (u)
                    i64_divu(&q, &m, a, &b);
                else
                    i64_divs(&q, &m, a, &b);
                if (op == '/')
                    *a = q;
                else
                    *a = m;
            }
        } else if (op == '*') {
            i64_mul(a, a, &b);
        } else if (op == '+') {
            i64_add(a, a, &b);
        } else if (op == '-') {
            i64_sub(a, a, &b);
        } else if (op == '&') {
            i64_and(a, a, &b);
        } else if (op == '^') {
            i64_xor(a, a, &b);
        } else {
            i64_or(a, a, &b);
        }
        e_fit(a, u);
        ua = u;
    }
    e_uns = ua;
}

/* c ? a : b, the lowest precedence, grouping from the right (each arm is
 * itself an e_cond). Both arms are parsed, the one not taken with e_quiet
 * raised so that a division by zero there is no error; the result is
 * unsigned if either arm is. */
static void e_cond(struct i64 *r)
{
    struct i64 c;
    struct i64 a;
    struct i64 b;
    int ua;
    int t;

    e_binary(&c, 1);
    ep = skip_ws(ep);
    if (*ep != '?') {
        *r = c;
        return;
    }
    ep++;
    t = !i64_is_zero(&c);
    if (!t)
        e_quiet++;
    e_cond(&a);
    ua = e_uns;
    if (!t)
        e_quiet--;
    ep = skip_ws(ep);
    if (*ep != ':') {
        e_error("missing ':' in #if");
        i64_set(r, 0, 0);
        return;
    }
    ep++;
    if (t)
        e_quiet++;
    e_cond(&b);
    if (t)
        e_quiet--;
    e_uns = ua || e_uns;
    if (t)
        *r = a;
    else
        *r = b;
}

/* "defined NAME" and "defined(NAME)" become 1 or 0, then the rest is
 * macro-expanded and evaluated. The order matters: the name after
 * defined must be looked at before expansion, or a macro's name there
 * would be replaced by its value. The result: 1 if the expression is
 * non-zero; 0 if it is zero or has an error. */
static char defbuf[CPP_LINE + 2];

static int if_value(char *p)
{
    char name[64];
    int o;
    int paren;
    char *e;
    struct i64 v;

    o = 0;
    while (*p) {
        if (*p == '"' || *p == '\'') {
            e = literal_end(p);
            while (p < e) {
                defbuf[o] = *p;
                o++;
                p++;
            }
        } else if (is_alpha(*p)) {
            e = ident(p, name);
            if (strcmp(name, "defined") == 0) {
                p = skip_ws(e);
                paren = *p == '(';
                if (paren)
                    p = skip_ws(p + 1);
                if (!is_alpha(*p)) {
                    error("'defined' needs a macro name");
                    return 0;
                }
                p = ident(p, name);
                if (paren) {
                    p = skip_ws(p);
                    if (*p != ')') {
                        error("missing ')' after 'defined'");
                        return 0;
                    }
                    p++;
                }
                defbuf[o] = find_macro(name) >= 0 || strcmp(name, "__LINE__") == 0
                            || strcmp(name, "__FILE__") == 0 ? '1' : '0';
                defbuf[o + 1] = ' ';
                o = o + 2;
            } else {
                while (p < e) {
                    defbuf[o] = *p;
                    o++;
                    p++;
                }
            }
        } else {
            defbuf[o] = *p;
            o++;
            p++;
        }
        if (o > CPP_LINE)
            fatal("#if line too long");
    }
    defbuf[o] = 0;
    /* expanded into outbuf (paint marks dropped there), then parsed */
    olen = 0;
    expand(defbuf, 0);
    outbuf[olen] = 0;
    ep = skip_ws(outbuf);
    if (*ep == 0) {
        error("#if with no expression");
        return 0;
    }
    e_bad = 0;
    e_quiet = 0;
    e_cond(&v);
    ep = skip_ws(ep);
    if (*ep)
        e_error("invalid #if expression");
    return e_bad ? 0 : !i64_is_zero(&v);
}

/* ---- files ----------------------------------------------------------------------------- */

/* Whether path can be opened for reading (the file is closed again). */
static int file_exists(char *path)
{
    FILE *f;

    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

/* dir and name into buf, with a '/' between unless dir is empty or ends
 * in one; 0 if it would not fit in CPP_PATH. */
static int join(char *buf, char *dir, char *name)
{
    int n;

    n = strlen(dir);
    if (n + strlen(name) + 2 > CPP_PATH)
        return 0;
    strcpy(buf, dir);
    if (n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\') {
        buf[n] = '/';
        n++;
    }
    strcpy(buf + n, name);
    return 1;
}

/* The file an #include names, into path; 0 if there is none. An absolute
 * name (starting / or \, or with a drive's ':') is used as it is. Then,
 * for "name" only, the including file's directory (from cur_path, the
 * real path, not the #line name); then incdirs: the -I directories in
 * order, then /usrlib and /lib, which main appends. */
static int find_include(char *name, int quoted, char *path)
{
    char dir[CPP_PATH];
    int i;
    int n;

    if (name[0] == '/' || name[0] == '\\' || strchr(name, ':') != NULL) {
        strcpy(path, name);
        return file_exists(path);
    }
    if (quoted) {
        strcpy(dir, cur_path);
        n = strlen(dir);
        while (n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\' && dir[n - 1] != ':')
            n--;
        dir[n] = 0;
        if (join(path, dir, name) && file_exists(path))
            return 1;
    }
    for (i = 0; i < nincdirs; i++)
        if (join(path, incdirs[i], name) && file_exists(path))
            return 1;
    return 0;
}

/* Enter an included file. The current file's place is saved and the file
 * closed before the new one opens, so only one input is ever open: MOS
 * has only a few file handles, and the output holds another. The new
 * file's #if groups must close within it (file_cond_base). */
static void push_file(char *path)
{
    if (depth >= MAX_DEPTH)
        fatal("#include nested too deeply (more than 8)");
    strcpy(stack[depth].path, cur_path);
    strcpy(stack[depth].pres, pres_path);
    stack[depth].line = phys_line;
    stack[depth].delta = line_delta;
    stack[depth].offset = rd_tell(&src);
    stack[depth].cond_base = file_cond_base;
    depth++;
    rd_close(&src);
    if (!rd_open(&src, path))
        fatal("cannot reopen an include file");
    strcpy(cur_path, path);
    strcpy(pres_path, path);
    phys_line = 0;
    line_delta = 0;
    cur_line = 0;
    file_cond_base = ncond;
    out_synced = 0;
}

/* Back to the including file at the end of an included one: reopened and
 * read forward to where it stopped (rd_open_at). The next output line
 * needs a marker, since the file has changed. */
static void pop_file(void)
{
    rd_close(&src);
    depth--;
    strcpy(cur_path, stack[depth].path);
    strcpy(pres_path, stack[depth].pres);
    phys_line = stack[depth].line;
    line_delta = stack[depth].delta;
    cur_line = phys_line + line_delta;
    file_cond_base = stack[depth].cond_base;
    if (!rd_open_at(&src, cur_path, stack[depth].offset))
        fatal("cannot reopen a file after its #include");
    out_synced = 0;
}

/* ---- directives -------------------------------------------------------------------------- */

/* #include "name", #include <name>, or a line of macros that expands to
 * one of those (C89 3.8.2). The included file is read next, by the main
 * loop, through push_file. */
static void do_include(char *p)
{
    char name[CPP_PATH];
    char path[CPP_PATH];
    int close;
    int n;
    int quoted;

    p = skip_ws(p);
    if (*p != '"' && *p != '<' && *p) {
        olen = 0;                       /* #include MACRO: expanded first */
        expand(p, 0);
        outbuf[olen] = 0;
        p = skip_ws(outbuf);
    }
    if (*p != '"' && *p != '<') {
        error("#include needs \"file\" or <file>");
        return;
    }
    quoted = *p == '"';
    close = quoted ? '"' : '>';
    p++;
    n = 0;
    while (*p && *p != close) {
        if (n >= CPP_PATH - 1) {
            error("#include file name too long");
            return;
        }
        name[n] = *p;
        n++;
        p++;
    }
    name[n] = 0;
    if (*p != close || n == 0) {
        error("#include needs \"file\" or <file>");
        return;
    }
    if (!find_include(name, quoted, path)) {
        error_s("cannot find include file ", name);
        return;
    }
    push_file(path);
}

/* The macro name after a directive, into name; an empty name (with an
 * error) if none. Returns the position after it. */
static char *macro_name(char *p, char *name, char *what)
{
    p = skip_ws(p);
    if (!is_alpha(*p)) {
        error_s(what, " needs a macro name");
        name[0] = 0;
        return p;
    }
    return ident(p, name);
}

/* The replacement list, encoded: white space outside literals made one
 * space and none at either end (so identical definitions compare equal),
 * each parameter a marker (M_ARG, M_RAW next to ##, M_STR after #), and
 * ## a marker, or in an object-like macro the two tokens joined now.
 *
 * do_define parses "#define name(params) replacement" (p is after
 * "define"; main also uses it for -D): the parameter list, checked for
 * duplicates and limits, then the replacement, encoded into rbuf in one
 * left-to-right pass. A parameter met before a ## is first written as
 * M_ARG; when the ## comes, last_param finds it and makes it M_RAW, and
 * raw_next makes the parameter after the ## M_RAW too. In a function-like
 * macro # must be followed by a parameter; in an object-like one # is an
 * ordinary character. 0 if the definition is malformed, else 1. */
static char rbuf[CPP_LINE * 2 + 2];

static int do_define(char *p)
{
    char name[64];
    char pname[MAX_PARAMS][64];
    char plist[MAX_PARAMS * 64 + 2];
    char id[64];
    char *e;
    int np;
    int o;
    int i;
    int k;
    int last_param;             /* where the last parameter marker is in rbuf, or -1 */
    int raw_next;               /* the next parameter is an operand of ## */

    p = macro_name(p, name, "#define");
    if (name[0] == 0)
        return 0;
    if (strcmp(name, "defined") == 0) {
        error("'defined' cannot be a macro name");
        return 0;
    }
    /* a '(' straight after the name makes it function-like (C89 3.8.3);
     * "#define f (x)" is object-like */
    np = -1;
    plist[0] = 0;
    if (*p == '(') {
        np = 0;
        p = skip_ws(p + 1);
        while (*p != ')') {
            if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
                error("'...' in a macro's parameters (a C99 feature)");
                return 0;
            }
            if (!is_alpha(*p)) {
                error("#define needs a parameter name");
                return 0;
            }
            if (np >= MAX_PARAMS - 1) {
                error("too many macro parameters (more than 31)");
                return 0;
            }
            p = ident(p, pname[np]);
            for (i = 0; i < np; i++) {
                if (strcmp(pname[i], pname[np]) == 0) {
                    error_s("duplicate macro parameter ", pname[np]);
                    return 0;
                }
            }
            if (np > 0)
                strcat(plist, ",");
            strcat(plist, pname[np]);
            np++;
            p = skip_ws(p);
            if (*p == ',')
                p = skip_ws(p + 1);
            else if (*p != ')') {
                error("missing ')' in a macro's parameters");
                return 0;
            }
        }
        p++;
    } else if (*p && !is_space(*p)) {
        error("#define needs white space after the macro name");
        return 0;
    }
    /* the replacement list */
    p = skip_ws(p);
    o = 0;
    last_param = -1;
    raw_next = 0;
    while (*p) {
        if (o > CPP_LINE * 2 - 4)
            fatal("macro definition too long");
        if (*p == '"' || *p == '\'') {
            e = literal_end(p);
            while (p < e) {
                rbuf[o] = *p;
                o++;
                p++;
            }
            raw_next = 0;
        } else if (is_digit(*p) || (*p == '.' && is_digit(p[1]))) {
            e = number_end(p + 1);          /* a number, never a parameter (1e5) */
            while (p < e) {
                rbuf[o] = *p;
                o++;
                p++;
            }
            raw_next = 0;
        } else if (is_space(*p)) {
            p = skip_ws(p);
            if (*p) {
                rbuf[o] = ' ';
                o++;
            }
        } else if (p[0] == '#' && p[1] == '#') {
            while (o > 0 && rbuf[o - 1] == ' ')
                o--;
            if (o == 0) {
                error("'##' at the start of a macro's replacement");
                return 0;
            }
            p = skip_ws(p + 2);
            if (*p == 0) {
                error("'##' at the end of a macro's replacement");
                return 0;
            }
            /* a parameter just before the ## (its two bytes end rbuf) */
            if (last_param == o - 2)
                rbuf[last_param] = M_RAW;       /* its left operand is used as written */
            if (np >= 0) {
                rbuf[o] = M_PASTE;
                o++;
            }                               /* object-like: the tokens are joined now */
            raw_next = 1;
        } else if (*p == '#' && np >= 0) {
            p = skip_ws(p + 1);
            e = p;
            k = -1;
            if (is_alpha(*p)) {
                e = ident(p, id);
                for (i = 0; i < np; i++)
                    if (strcmp(pname[i], id) == 0)
                        k = i;
            }
            if (k < 0) {
                error("'#' must be followed by a macro parameter");
                return 0;
            }
            p = e;
            rbuf[o] = M_STR;
            rbuf[o + 1] = (char)(k + 1);
            o = o + 2;
            raw_next = 0;
        } else if (is_alpha(*p)) {
            e = ident(p, id);
            k = -1;
            for (i = 0; i < np; i++)
                if (strcmp(pname[i], id) == 0)
                    k = i;
            if (e == p + 1 && *p == 'L' && (*e == '\'' || *e == '"'))
                k = -1;                 /* L'x', L"x": a wide literal's prefix, not a parameter L */
            if (k >= 0) {
                last_param = o;
                rbuf[o] = raw_next ? M_RAW : M_ARG;
                rbuf[o + 1] = (char)(k + 1);
                o = o + 2;
                p = e;
            } else {
                while (p < e) {
                    rbuf[o] = *p;
                    o++;
                    p++;
                }
            }
            raw_next = 0;
        } else {
            rbuf[o] = *p;
            o++;
            p++;
            raw_next = 0;
        }
    }
    rbuf[o] = 0;
    define(name, np, plist, rbuf);
    return 1;
}

/* #pragma weak name is passed on to cc1 (c89_spec.md 13 item 6), as an
 * output line of its own; every other #pragma is ignored, with a warning
 * (c89_spec.md 11). */
static void do_pragma(char *p)
{
    char word[64];
    char name[64];

    p = skip_ws(p);
    if (!is_alpha(*p)) {
        warning("#pragma is ignored");
        return;
    }
    p = ident(p, word);
    if (strcmp(word, "weak") != 0) {
        warning("#pragma is ignored");
        return;
    }
    p = skip_ws(p);
    if (!is_alpha(*p)) {
        error("#pragma weak needs a name");
        return;
    }
    p = skip_ws(ident(p, name));
    if (*p)
        error("#pragma weak takes one name");
    sprintf(outbuf, "#pragma weak %s", name);
    olen = strlen(outbuf);
    emit();
}

/* #line digits ["name"], after macro expansion (C89 3.8.4): the next
 * line has that number, and the file that name. Only line_delta and
 * pres_path change: the physical count goes on, and a line's number is
 * phys_line + line_delta from then on. A marker follows (out_synced 0). */
static void do_line(char *p)
{
    char name[CPP_PATH];
    char *q;
    long n;
    int k;

    olen = 0;
    expand(p, 0);
    outbuf[olen] = 0;
    q = skip_ws(outbuf);
    if (!is_digit(*q)) {
        error("#line needs a line number");
        return;
    }
    n = 0;
    while (is_digit(*q)) {
        n = n * 10 + (*q - '0');
        if (n > 2147483647L) {
            error("#line number too large");
            return;
        }
        q++;
    }
    if (n == 0) {
        error("#line number must not be 0");
        return;
    }
    q = skip_ws(q);
    k = -1;                             /* no file name */
    if (*q == '"') {
        q++;
        k = 0;
        while (*q && *q != '"' && k < CPP_PATH - 1) {
            if (*q == '\\' && q[1])
                q++;
            name[k] = *q;
            k++;
            q++;
        }
        name[k] = 0;
        if (*q != '"') {
            error("#line file name not closed");
            return;
        }
        q = skip_ws(q + 1);
    }
    if (*q) {
        error("#line has more after its line number and file name");
        return;
    }
    if (k >= 0)
        strcpy(pres_path, name);
    line_delta = (int)(n - (phys_line + 1));
    out_synced = 0;
}

/* A new entry on the conditional stack for an #if, #ifdef or #ifndef. */
static void push_cond(int state)
{
    if (ncond >= MAX_COND)
        fatal("#if nested too deeply (more than 32)");
    cond_state[ncond] = state;
    cond_else[ncond] = 0;
    cond_line[ncond] = cur_line;
    ncond++;
}

static char wbuf[160];              /* #warning's message */

/* One directive; p is after the '#'. The conditional directives are
 * obeyed even in a skipped group, since they decide where it ends; all
 * the others only in an active one, where an unknown name is an error.
 * In a skipped group a line starting with '#' may hold anything (C89
 * 3.8.1), so nothing else in it is checked. */
static void directive(char *p)
{
    char dname[64];
    char name[64];
    int was_active;

    p = skip_ws(p);
    if (*p == 0)
        return;                         /* the null directive */
    p = ident(p, dname);
    was_active = active();
    /* #if, #ifdef, #ifndef: dname[2] is 0, 'd' or 'n' */
    if (strcmp(dname, "if") == 0 || strcmp(dname, "ifdef") == 0 || strcmp(dname, "ifndef") == 0) {
        if (!was_active) {
            push_cond(C_OUTER);
        } else if (dname[2] == 0) {
            push_cond(if_value(p) ? C_ACTIVE : C_WAITING);
        } else {
            p = macro_name(p, name, dname[2] == 'd' ? "#ifdef" : "#ifndef");
            if ((find_macro(name) >= 0) == (dname[2] == 'd'))
                push_cond(C_ACTIVE);
            else
                push_cond(C_WAITING);
        }
        return;
    }
    /* #elif, #else, #endif (told apart by dname[1] and dname[2]); an
     * entry from an including file is out of reach */
    if (strcmp(dname, "elif") == 0 || strcmp(dname, "else") == 0 || strcmp(dname, "endif") == 0) {
        if (ncond <= file_cond_base) {
            error_s(dname[1] == 'n' ? "#endif" : dname[1] == 'l' && dname[2] == 'i' ? "#elif" : "#else",
                    " without #if");
            return;
        }
        if (dname[1] == 'n') {
            ncond--;
        } else if (cond_else[ncond - 1]) {
            error(dname[2] == 'i' ? "#elif after #else" : "#else after #else");
        } else if (dname[2] == 'i') {
            if (cond_state[ncond - 1] == C_ACTIVE)
                cond_state[ncond - 1] = C_DONE;
            else if (cond_state[ncond - 1] == C_WAITING && if_value(p))
                cond_state[ncond - 1] = C_ACTIVE;
        } else {
            cond_else[ncond - 1] = 1;
            if (cond_state[ncond - 1] == C_ACTIVE)
                cond_state[ncond - 1] = C_DONE;
            else if (cond_state[ncond - 1] == C_WAITING)
                cond_state[ncond - 1] = C_ACTIVE;
        }
        return;
    }
    if (!was_active)
        return;                         /* skipped: nothing else is even looked at */
    if (strcmp(dname, "define") == 0) {
        do_define(p);
    } else if (strcmp(dname, "undef") == 0) {
        macro_name(p, name, "#undef");
        if (name[0])
            undefine(name);
    } else if (strcmp(dname, "include") == 0) {
        do_include(p);
    } else if (strcmp(dname, "error") == 0) {
        p = skip_ws(p);
        error_s("#error ", p);
    } else if (strcmp(dname, "warning") == 0 && !strict) {
        /* #warning (C23; GCC's before that): the default mode only */
        strcpy(wbuf, "#warning ");
        strncat(wbuf, skip_ws(p), sizeof wbuf - 10);
        warning(wbuf);
    } else if (strcmp(dname, "line") == 0) {
        do_line(p);
    } else if (strcmp(dname, "pragma") == 0) {
        do_pragma(p);
    } else {
        error_s("unknown directive #", dname);
    }
}

/* ---- the translation unit ------------------------------------------------------------------ */

/* At the end of each file: a comment or an #if group still open in it is
 * an error (each must end in the file it began in, C89 3.8.1), reported
 * at the line where it began; the groups are closed. */
static void end_of_file(void)
{
    int save;

    if (in_comment) {
        save = cur_line;
        cur_line = comment_line;
        error("unterminated comment");
        cur_line = save;
        in_comment = 0;
    }
    while (ncond > file_cond_base) {
        ncond--;
        save = cur_line;
        cur_line = cond_line[ncond];
        error("unterminated #if");
        cur_line = save;
    }
}

/* #asm ... #endasm: the lines between are passed to cc1 as they are, with
 * no comments removed and no macros expanded (c89_spec.md 13 item 1); in
 * a skipped region they are skipped whatever they contain. They are read
 * as physical lines (read_line): no trigraphs, no splicing. */
static void asm_block(int on)
{
    char *p;
    int start;

    start = cur_line;
    if (on) {
        strcpy(outbuf, "#asm");
        olen = 4;
        emit();
    }
    for (;;) {
        if (!read_line()) {
            cur_line = start;
            error("#asm without #endasm");
            return;
        }
        p = skip_ws(raw);
        if (*p == '#' && is_directive(p + 1, "endasm")) {
            if (on) {
                strcpy(outbuf, "#endasm");
                olen = 7;
                emit();
            }
            return;
        }
        if (on) {
            if (strlen(raw) > OUT_MAX)
                fatal("#asm line too long");
            strcpy(outbuf, raw);
            olen = strlen(raw);
            emit();
        }
    }
}

/* Does s hold a star-slash? A line read while a comment is open and
 * without one is all comment, and is passed over. */
static int closes_comment(char *s)
{
    while (*s) {
        if (s[0] == '*' && s[1] == '/')
            return 1;
        s++;
    }
    return 0;
}

/* The main loop, a logical line at a time to the end of the input file:
 * a directive line waiting from more_line first, else the next line (at
 * the end of an included file, back to its includer). Then #asm blocks,
 * other directives, or a text line, which in an active group is expanded
 * and written. A text line may stream (outbuf written out as it fills),
 * since an expansion can far outgrow its source line. */
static void process(void)
{
    char *p;

    for (;;) {
        if (have_pending) {
            strcpy(line, pending);      /* a directive a macro call's arguments ran into */
            have_pending = 0;
            cur_line = pending_line;
        } else {
            if (!read_logical()) {
                end_of_file();
                if (depth == 0)
                    return;
                pop_file();
                continue;
            }
            if (in_comment && !closes_comment(raw))
                continue;               /* a whole line of comment */
            strip_comments();
        }
        p = skip_ws(line);
        if (*p == '#' && is_directive(p + 1, "asm")) {
            asm_block(active());
        } else if (*p == '#' && is_directive(p + 1, "endasm")) {
            if (active())
                error("#endasm without #asm");
        } else if (*p == '#') {
            directive(p + 1);
        } else if (*p && active()) {
            olen = 0;
            streaming = 1;
            expand(line, 1);
            emit();
            streaming = 0;
        }
    }
}

/* ---- main ----------------------------------------------------------------------------- */

static char *args[MAX_ARGS];
static char dtext[CPP_LINE + 2];

/* -date YYYYMMDD: __DATE__ as "Mmm dd yyyy"; ignored unless a valid date. */
static void set_date(char *s)
{
    char *months;
    int y;
    int m;
    int d;
    int i;

    months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (i = 0; i < 8; i++)
        if (!is_digit(s[i]))
            return;
    if (s[8])
        return;
    y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    m = (s[4] - '0') * 10 + (s[5] - '0');
    d = (s[6] - '0') * 10 + (s[7] - '0');
    if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1)
        return;
    sprintf(date_text, "\"%.3s %2d %04d\"", months + 3 * (m - 1), d, y);
}

/* -time HHMMSS: __TIME__ as "hh:mm:ss"; ignored unless a valid time. */
static void set_time(char *s)
{
    int i;

    for (i = 0; i < 6; i++)
        if (!is_digit(s[i]))
            return;
    if (s[6] || s[0] > '2' || (s[0] == '2' && s[1] > '3') || s[2] > '5' || s[4] > '5')
        return;
    sprintf(time_text, "\"%c%c:%c%c:%c%c\"", s[0], s[1], s[2], s[3], s[4], s[5]);
}

/* -D name[=value], defined as "#define name value" would be: the '=' is
 * made a space, and a name alone is defined as 1, as gcc does. */
static void cmd_define(char *s)
{
    char *eq;
    int n;

    eq = strchr(s, '=');
    n = eq == NULL ? strlen(s) : eq - s;
    if (n <= 0 || n > 63 || n + 3 > CPP_LINE) {
        fprintf(stderr, "cpp: bad -D %s\n", s);
        exit(200);
    }
    strcpy(dtext, s);
    dtext[n] = ' ';
    if (eq == NULL)
        strcpy(dtext + n + 1, "1");
    do_define(dtext);
}

/* The predefined macros first, then the options in order (so -D, -U and
 * -ansi act in command-line order on them), then one run of process()
 * over the input. Any error removes the output and gives status 200. */
int main(int argc, char **argv)
{
    int n;
    int i;
    int opt;
    char *in_path;
    char *a;

    n = expand_args("cpp", argc, argv, args);
    if (n < 0)
        return 200;
    for (i = 0; i < MACRO_HASH; i++)
        mhash[i] = -1;
    strcpy(cur_path, "<command line>");
    strcpy(pres_path, cur_path);
    define("__STDC__", -1, "", "1");
    define("__AGONC__", -1, "", "1");
    define("__EZ80__", -1, "", "1");
    define("__ADL__", -1, "", "1");
    /* gcc's, with this target's sizes (c89_spec.md 13, item 9) */
    define("__CHAR_BIT__", -1, "", "8");
    define("__SIZEOF_SHORT__", -1, "", "2");
    define("__SIZEOF_INT__", -1, "", "3");
    define("__SIZEOF_LONG__", -1, "", "4");
    define("__SIZEOF_POINTER__", -1, "", "3");
    define("__SIZEOF_FLOAT__", -1, "", "4");
    define("__SIZEOF_DOUBLE__", -1, "", "8");
    define("__SCHAR_MAX__", -1, "", "127");
    define("__SHRT_MAX__", -1, "", "32767");
    define("__INT_MAX__", -1, "", "8388607");
    define("__LONG_MAX__", -1, "", "2147483647L");
    define("__SIZEOF_LONG_LONG__", -1, "", "8");       /* the default mode's (-ansi removes them) */
    define("__LONG_LONG_MAX__", -1, "", "9223372036854775807LL");
    /* GCC's type macros, as <stddef.h> and <stdint.h> have the types */
    define("__SIZE_TYPE__", -1, "", "unsigned int");
    define("__PTRDIFF_TYPE__", -1, "", "int");
    define("__WCHAR_TYPE__", -1, "", "int");
    define("__WINT_TYPE__", -1, "", "int");
    define("__INTMAX_TYPE__", -1, "", "long long");
    define("__UINTMAX_TYPE__", -1, "", "unsigned long long");
    define("__INTPTR_TYPE__", -1, "", "int");
    define("__UINTPTR_TYPE__", -1, "", "unsigned int");
    define("__INT8_TYPE__", -1, "", "signed char");
    define("__UINT8_TYPE__", -1, "", "unsigned char");
    define("__INT16_TYPE__", -1, "", "short");
    define("__UINT16_TYPE__", -1, "", "unsigned short");
    define("__INT32_TYPE__", -1, "", "long");
    define("__UINT32_TYPE__", -1, "", "unsigned long");
    define("__INT64_TYPE__", -1, "", "long long");
    define("__UINT64_TYPE__", -1, "", "unsigned long long");
    define("__INT_LEAST8_TYPE__", -1, "", "signed char");
    define("__UINT_LEAST8_TYPE__", -1, "", "unsigned char");
    define("__INT_LEAST16_TYPE__", -1, "", "short");
    define("__UINT_LEAST16_TYPE__", -1, "", "unsigned short");
    define("__INT_LEAST32_TYPE__", -1, "", "long");
    define("__UINT_LEAST32_TYPE__", -1, "", "unsigned long");
    define("__INT_LEAST64_TYPE__", -1, "", "long long");
    define("__UINT_LEAST64_TYPE__", -1, "", "unsigned long long");
    define("__INT_FAST8_TYPE__", -1, "", "int");
    define("__UINT_FAST8_TYPE__", -1, "", "unsigned int");
    define("__INT_FAST16_TYPE__", -1, "", "int");
    define("__UINT_FAST16_TYPE__", -1, "", "unsigned int");
    define("__INT_FAST32_TYPE__", -1, "", "long");
    define("__UINT_FAST32_TYPE__", -1, "", "unsigned long");
    define("__INT_FAST64_TYPE__", -1, "", "long long");
    define("__UINT_FAST64_TYPE__", -1, "", "unsigned long long");
    /* its byte orders: the eZ80's is little-endian */
    define("__ORDER_LITTLE_ENDIAN__", -1, "", "1234");
    define("__ORDER_BIG_ENDIAN__", -1, "", "4321");
    define("__ORDER_PDP_ENDIAN__", -1, "", "3412");
    define("__BYTE_ORDER__", -1, "", "__ORDER_LITTLE_ENDIAN__");
    /* and its floating-point limits, as <float.h> has them */
    define("__FLT_MANT_DIG__", -1, "", "24");
    define("__FLT_DIG__", -1, "", "6");
    define("__FLT_MAX__", -1, "", "3.40282347e+38F");
    define("__FLT_MIN__", -1, "", "1.17549435e-38F");
    define("__FLT_EPSILON__", -1, "", "1.19209290e-07F");
    define("__DBL_MANT_DIG__", -1, "", "53");
    define("__DBL_DIG__", -1, "", "15");
    define("__DBL_MAX__", -1, "", "1.7976931348623157e+308");
    define("__DBL_MIN__", -1, "", "2.2250738585072014e-308");
    define("__DBL_EPSILON__", -1, "", "2.2204460492503131e-16");
    in_path = NULL;
    out_path = NULL;
    for (i = 0; i < n; i++) {
        a = args[i];
        if (strcmp(a, "-ansi") == 0) {
            strict = 1;
            define("__STRICT_ANSI__", -1, "", "1");     /* as GCC: headers hide C99's names */
            undefine("__SIZEOF_LONG_LONG__");           /* and no long long */
            undefine("__LONG_LONG_MAX__");
            undefine("__INT64_TYPE__");
            undefine("__UINT64_TYPE__");
            undefine("__INT_LEAST64_TYPE__");
            undefine("__UINT_LEAST64_TYPE__");
            undefine("__INT_FAST64_TYPE__");
            undefine("__UINT_FAST64_TYPE__");
            undefine("__INTMAX_TYPE__");
            undefine("__UINTMAX_TYPE__");
            define("__INTMAX_TYPE__", -1, "", "long");
            define("__UINTMAX_TYPE__", -1, "", "unsigned long");
        } else if (a[0] == '-' && (a[1] == 'I' || a[1] == 'D' || a[1] == 'U')) {
            opt = a[1];
            if (a[2] == 0) {
                if (i + 1 >= n) {
                    fprintf(stderr, "cpp: %s needs an argument\n", a);
                    return 200;
                }
                i++;
                a = args[i];
            } else {
                a = a + 2;
            }
            if (opt == 'I') {
                if (nincdirs >= MAX_INCDIRS) {
                    fprintf(stderr, "cpp: too many -I directories\n");
                    return 200;
                }
                incdirs[nincdirs] = a;
                nincdirs++;
            } else if (opt == 'D') {
                cmd_define(a);
            } else {
                undefine(a);
            }
        } else if ((strcmp(a, "-date") == 0 || strcmp(a, "-time") == 0) && i + 1 < n) {
            if (a[1] == 'd')
                set_date(args[i + 1]);
            else
                set_time(args[i + 1]);
            i++;
        } else if (strcmp(a, "-w") == 0) {
            warnings_off = 1;
        } else if (strcmp(a, "-Werror") == 0) {
            werror = 1;
        } else if (strcmp(a, "-v") == 0) {
            verbose = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "cpp: unknown option %s\n", a);
            return 200;
        } else if (in_path == NULL) {
            in_path = a;
        } else if (out_path == NULL) {
            out_path = a;
        } else {
            fprintf(stderr, "cpp: too many file names\n");
            return 200;
        }
    }
    if (in_path == NULL || out_path == NULL) {
        fprintf(stderr, "usage: cpp in.c out.i [-I dir]... [-D name[=value]]... [-U name]... [-date YYYYMMDD]"
                " [-time HHMMSS] [-w] [-Werror] [-v]\n");
        return 200;
    }
    if (errors)
        return 200;
    incdirs[nincdirs] = "/usrlib";
    incdirs[nincdirs + 1] = "/lib";
    nincdirs = nincdirs + 2;
    if (strlen(in_path) >= CPP_PATH) {
        fprintf(stderr, "cpp: file name too long: %s\n", in_path);
        return 200;
    }
    if (!rd_open(&src, in_path)) {
        fprintf(stderr, "cpp: cannot open %s\n", in_path);
        return 200;
    }
    strcpy(cur_path, in_path);
    strcpy(pres_path, in_path);
    out = fopen(out_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "cpp: cannot create %s\n", out_path);
        return 200;
    }
    process();
    rd_close(&src);
    fclose(out);
    out = NULL;
    if (verbose)
        printf("cpp: peaks: macros %d/%d, macro text %d/%d bytes\n", peak_macros, MAX_MACROS, mtext_used, MACRO_TEXT);
    if (errors) {
        remove(out_path);
        return 200;
    }
    return 0;
}
