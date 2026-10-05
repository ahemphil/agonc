/* lex.c - input, tokens, identifier interning and diagnostics for cc1.
 *
 * The lexer (scanner) turns cpp's output, a .i file, into tokens for the
 * recursive-descent parser in expr.c and stmt.c. The parser sees one
 * token at a time, in the tok_* globals (cc1.h), and asks for the next
 * with next(); peek() looks one token further ahead without consuming
 * it, which the grammar needs in a few places (a typedef name followed by
 * ':' is a label, "(" followed by a type name is a cast).
 *
 * The input still holds three things cpp passes through: "# line "file""
 * markers, which keep diagnostics pointing at the original source; #asm
 * ... #endasm blocks, returned as one TK_ASMB token; and #pragma weak,
 * handed straight to emit.c. cpp has already replaced comments by spaces,
 * but the lexer skips any that are left all the same.
 *
 * Identifiers are interned: each distinct spelling is stored once and
 * known by its number from then on, so the symbol tables compare names
 * as ints. Numbers are converted here, exactly, on any host: integers in
 * 64-bit arithmetic (int64.c), floating constants by softfp.c.
 *
 * Sections: diagnostics, names, characters (with one character of
 * lookahead), line markers and white space, constants and literals,
 * #asm blocks, the token switch (lex), and next/peek.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd.h"
#include "int24.h"
#include "cc1.h"

int tok;
int tok_val;
int tok_hi8;
int tok_type;
struct sf64 tok_fval;
int tok_name;
char tok_str[MAX_STRLIT * 3];
int tok_len;
int tok_wide;                   /* TK_STR: a wide string literal, L"..." */
int tok_line;
int prev_line;
char cur_file[128];
int errors;
int warnings_off;
int werror;
int strict;
static int pending_asm;     /* line_marker met #asm */
static int asm_line;        /* the #asm's line */

/* The name table: the text of every name end to end in names[], and an
 * open-addressing hash table (name_hash, NAME_HASH slots, a power of two
 * well above MAX_NAMES so it never fills) from text to name number. */
char names[NAME_TEXT];
int name_off[MAX_NAMES];
static int nnames;
static int names_used;          /* bytes of names[] in use */
static int name_hash[NAME_HASH];        /* a name number, or -1 for an empty slot */

/* -v: the name table, as used/limit. */
void lex_report(void)
{
    printf("cc1: names %d/%d, name text %d/%d bytes\n", nnames, MAX_NAMES, names_used, NAME_TEXT);
}

static struct rd src;       /* the .i file, through rd.c's buffered reader */
static int ch;              /* the next unread character, or -1 */
static int line;
static int at_bol;          /* ch is the first character of a line */

static char *kw_names[NKW];

/* one saved token, for peek(): a copy of every tok_* global */
struct lexeme {
    int kind;
    int val;
    int hi8;
    int type;
    int name;
    int len;
    int line;
    int wide;
    struct sf64 fval;
    char str[MAX_STRLIT * 3];
};

static struct lexeme la;        /* the lookahead token peek() read */
static int have_la;             /* la holds a token next() has not yet taken */

/* ---- diagnostics --------------------------------------------------------------- */

/* Every diagnostic is one line, "file:line: error: message" (c89_spec.md
 * 2), at the current token's line. error() counts and carries on, so one
 * run reports several errors; the output is removed at the end if any
 * occurred. */

void error(char *msg)
{
    fprintf(stderr, "%s:%d: error: %s\n", cur_file, tok_line, msg);
    errors++;
}

void error_s(char *msg, char *arg)
{
    fprintf(stderr, "%s:%d: error: %s%s\n", cur_file, tok_line, msg, arg);
    errors++;
}

void warning_s(char *msg, char *arg)
{
    char buf[120];

    sprintf(buf, "%s%s", msg, arg);
    warning(buf);
}

void warning(char *msg)
{
    if (warnings_off)
        return;
    if (werror) {
        error(msg);
        return;
    }
    fprintf(stderr, "%s:%d: warning: %s\n", cur_file, tok_line, msg);
}

/* Stop at once, never leaving a partial output file behind: a truncated
 * IR file must not be taken for a good one by a later step. Exit status
 * 200 is the failure status every pass and the driver use. The input is
 * closed first (see lex_close). */
void give_up(void)
{
    rd_close(&src);
    if (ir_out != NULL) {
        fclose(ir_out);
        ir_out = NULL;
        remove(ir_path);
    }
    exit(200);
}

void fatal(char *msg)
{
    error(msg);
    give_up();
}

/* A fixed table is full: fatal, and the message says which. */
static void limit(char *what)
{
    fprintf(stderr, "%s:%d: error: too many %s (a cc1 table limit; raise it in cc1.h)\n", cur_file, tok_line, what);
    give_up();
}

/* ---- names --------------------------------------------------------------------------- */

/* A multiplicative string hash (h * 31 + c, as Java's String.hashCode),
 * cut to the table size by a mask, which works because NAME_HASH is a
 * power of two. The arithmetic is unsigned, so overflow just wraps. */
static unsigned hash_str(char *s)
{
    unsigned h;

    h = 0;
    while (*s) {
        h = h * 31 + (unsigned char)*s;
        s++;
    }
    return h & (NAME_HASH - 1);
}

/* Hash interning: the number of name s, adding it if it is new. Linear
 * probing: a collision tries the next slot, wrapping round, until it finds
 * s or an empty slot (where s then goes). Names are never removed, so an
 * empty slot really ends the search. */
int intern(char *s)
{
    unsigned h;
    int n;
    int len;

    h = hash_str(s);
    while (name_hash[h] >= 0) {
        n = name_hash[h];
        if (strcmp(names + name_off[n], s) == 0)
            return n;
        h = (h + 1) & (NAME_HASH - 1);
    }
    len = strlen(s);
    if (nnames >= MAX_NAMES)
        limit("identifiers");
    if (names_used + len + 1 > NAME_TEXT)
        limit("identifier characters");
    strcpy(names + names_used, s);
    name_off[nnames] = names_used;
    names_used = names_used + len + 1;
    name_hash[h] = nnames;
    nnames++;
    return nnames - 1;
}

char *name_str(int n)
{
    return names + name_off[n];
}

/* ---- characters ------------------------------------------------------------------------ */

/* The lexer reads through ch, the current character, with one more
 * character of lookahead (peekc) for the two-character decisions: "/" then
 * "*" or "/", "L" then a quote, "*" then "/" inside a comment. -1 is end
 * of file; -2 in nch means no lookahead character is held. */
static int nch = -2;        /* the character after ch, once peekc has read it */

/* The next source byte: every '\r' is dropped, so CR LF lines read as LF
 * (c89_spec.md 2), and 0x1A ends the file (c89_spec.md 13 item 3). */
static int raw(void)
{
    int c;

    c = rd_getc(&src);
    while (c == '\r')
        c = rd_getc(&src);
    if (c == 0x1A)
        c = -1;
    return c;
}

/* Advance ch, counting lines and noting the start of each (at_bol): a '#'
 * is a line marker or directive only there. */
static void getch(void)
{
    if (ch == '\n') {
        line++;
        at_bol = 1;
    } else if (ch != -1) {
        at_bol = 0;
    }
    if (nch != -2) {
        ch = nch;
        nch = -2;
    } else {
        ch = raw();
    }
}

static int peekc(void)
{
    if (nch == -2)
        nch = raw();
    return nch;
}

/* Set up the keyword names and the name table, then open the input; 0 if
 * it cannot be opened. The keywords are interned first, in kw_names
 * order, so keyword i is name number i: lex() then recognises a keyword
 * by its name number alone (below NKW), with no second lookup. */
int lex_open(char *path)
{
    int i;

    kw_names[0] = "auto"; kw_names[1] = "break"; kw_names[2] = "case"; kw_names[3] = "char";
    kw_names[4] = "const"; kw_names[5] = "continue"; kw_names[6] = "default"; kw_names[7] = "do";
    kw_names[8] = "double"; kw_names[9] = "else"; kw_names[10] = "enum"; kw_names[11] = "extern";
    kw_names[12] = "float"; kw_names[13] = "for"; kw_names[14] = "goto"; kw_names[15] = "if";
    kw_names[16] = "int"; kw_names[17] = "long"; kw_names[18] = "register"; kw_names[19] = "return";
    kw_names[20] = "short"; kw_names[21] = "signed"; kw_names[22] = "sizeof"; kw_names[23] = "static";
    kw_names[24] = "struct"; kw_names[25] = "switch"; kw_names[26] = "typedef"; kw_names[27] = "union";
    kw_names[28] = "unsigned"; kw_names[29] = "void"; kw_names[30] = "volatile"; kw_names[31] = "while";
    kw_names[32] = "asm"; kw_names[33] = "__asm";
    for (i = 0; i < NAME_HASH; i++)
        name_hash[i] = -1;
    for (i = 0; i < NKW; i++)
        intern(kw_names[i]);        /* so keyword i is name i */
    if (!rd_open(&src, path))
        return 0;
    strncpy(cur_file, path, 127);
    cur_file[127] = 0;
    line = 1;
    ch = raw();
    at_bol = 1;
    return 1;
}

/* Neither MOS nor AgDev's exit closes a program's files, so a pass that
 * leaves one open loses that MOS handle for the rest of the session; MOS
 * has only a few, and one compilation runs several passes. */
void lex_close(void)
{
    rd_close(&src);
}

/* ---- line markers and white space ------------------------------------------------ */

static int is_alpha(int c);
static int is_digit(int c);

static char weak_name[128];

/* A '#' at the start of a line, the current character. cpp's output has
 * three kinds: "# <line> "<file>"", which sets the line number of the next
 * line and the file name diagnostics use; "#asm", whose following lines
 * the next lex() takes as a block (pending_asm); and "#pragma weak name",
 * passed to emit.c. Anything else is a directive cpp should have handled,
 * and fatal. Each kind consumes its whole line. */
static void line_marker(void)
{
    int n;
    int i;
    char word[8];

    asm_line = line;
    getch();
    while (ch == ' ' || ch == '\t')
        getch();
    if (ch == 'a') {
        /* #asm, which cpp passes on untouched with its lines */
        n = 0;
        while (is_alpha(ch) && n < 7) {
            word[n] = ch;
            n++;
            getch();
        }
        word[n] = 0;
        if (strcmp(word, "asm") == 0 && !is_alpha(ch) && !is_digit(ch)) {
            while (ch != '\n' && ch != -1)
                getch();
            if (ch == '\n')
                getch();
            pending_asm = 1;
            return;
        }
    }
    if (ch == 'p') {
        /* #pragma weak name, which cpp passes on: a WK record */
        n = 0;
        while (is_alpha(ch) && n < 7) {
            word[n] = ch;
            n++;
            getch();
        }
        word[n] = 0;
        while (ch == ' ' || ch == '\t')
            getch();
        if (strcmp(word, "pragma") == 0 && ch == 'w') {
            n = 0;
            while (is_alpha(ch) && n < 7) {
                word[n] = ch;
                n++;
                getch();
            }
            word[n] = 0;
            while (ch == ' ' || ch == '\t')
                getch();
            i = 0;
            while ((is_alpha(ch) || is_digit(ch)) && i < 127) {
                weak_name[i] = ch;
                i++;
                getch();
            }
            weak_name[i] = 0;
            if (strcmp(word, "weak") == 0 && i > 0) {
                while (ch != '\n' && ch != -1)
                    getch();
                if (ch == '\n')
                    getch();
                pragma_weak(weak_name);
                return;
            }
        }
        tok_line = line;
        fatal("preprocessor directive in cc1's input (run cpp first)");
    }
    if (ch < '0' || ch > '9') {
        tok_line = line;
        fatal("preprocessor directive in cc1's input (run cpp first)");
    }
    /* a line marker: the number, then an optional quoted file name */
    n = 0;
    while (ch >= '0' && ch <= '9') {
        n = n * 10 + ch - '0';
        getch();
    }
    while (ch == ' ' || ch == '\t')
        getch();
    if (ch == '"') {
        getch();
        i = 0;
        while (ch != '"' && ch != '\n' && ch != -1) {
            if (i < 127) {
                cur_file[i] = ch;
                i++;
            }
            getch();
        }
        cur_file[i] = 0;
    }
    while (ch != '\n' && ch != -1)
        getch();
    if (ch == '\n')
        getch();
    line = n;                           /* the next line's number, replacing getch's count */
}

/* Skip white space, comments and '#' lines up to the next token's first
 * character. Returns early when a #asm line was met, so that lex() takes
 * the block's lines raw instead of skipping their spaces. */
static void skip_space(void)
{
    for (;;) {
        if (ch == '#' && at_bol) {
            line_marker();
            if (pending_asm)
                return;
        } else if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\f' || ch == '\v') {
            getch();
        } else if (ch == '/' && peekc() == '*') {
            getch();
            getch();
            for (;;) {
                if (ch == -1) {
                    tok_line = line;
                    fatal("unterminated comment");
                }
                if (ch == '*' && peekc() == '/') {
                    getch();
                    getch();
                    break;
                }
                getch();
            }
        } else if (ch == '/' && peekc() == '/' && !strict) {
            /* a // comment; in strict mode, as in C89, two slashes */
            while (ch != '\n' && ch != -1)
                getch();
        } else {
            return;
        }
    }
}

/* ---- constants and literals -------------------------------------------------------- */

/* letters and '_': what may start an identifier */
static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/* a hexadecimal digit's value, or -1 (decimal and octal digits use it too) */
static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* One escape sequence after the backslash: a byte's value 0-255, or in a
 * wide literal any value of wchar_t's unsigned type (C89 3.1.3.4), returned
 * as the wchar_t it converts to. */
static int escape(int wide)
{
    unsigned v;
    unsigned max;
    int over;
    int n;

    switch (ch) {
    case 'n': getch(); return 10;
    case 't': getch(); return 9;
    case 'r': getch(); return 13;
    case 'a': getch(); return 7;
    case 'b': getch(); return 8;
    case 'f': getch(); return 12;
    case 'v': getch(); return 11;
    case '\\': getch(); return '\\';
    case '\'': getch(); return '\'';
    case '"': getch(); return '"';
    case '?': getch(); return '?';
    case 'x':
        getch();
        if (hex_digit(ch) < 0)
            error("\\x with no hex digits");
        max = wide ? 0xFFFFFFu : 255u;
        v = 0;
        over = 0;
        while (hex_digit(ch) >= 0) {
            /* v * 16 + digit > max, tested without computing it, so v
             * never overflows (unsigned is 24 bits on the Agon); the
             * remaining digits are still consumed */
            if (v > (max - hex_digit(ch)) / 16)
                over = 1;
            else
                v = v * 16 + hex_digit(ch);
            getch();
        }
        if (over)
            error("\\x escape out of range");
        return wrap24((int)v);
    default:
        /* an octal escape: one to three digits, so at most 0777 */
        if (ch >= '0' && ch <= '7') {
            v = 0;
            n = 0;
            while (n < 3 && ch >= '0' && ch <= '7') {
                v = v * 8 + ch - '0';
                getch();
                n++;
            }
            if (v > 255 && !wide)
                error("octal escape out of range");
            return wide ? (int)v : (int)(v & 255);
        }
        error("unknown escape sequence");
        getch();
        return 0;
    }
}

/* An integer constant's text (C89 3.1.3.2), already read whole by
 * number(). Its value is accumulated in 64 bits (int64.c, since the
 * compiler's own int may be 24 bits): up to 32 in strict mode, 64 in the
 * default mode. Typed as C89 says with this target's widths (a 24-bit
 * int, a 32-bit long): decimal takes the first of int, long, unsigned long
 * that holds it; octal and hexadecimal the first of int, unsigned int,
 * long, unsigned long; a u suffix skips the signed types, an l suffix the
 * int-sized ones. In the default mode, C99's rules (6.4.4.1): long long
 * and unsigned long long too, a decimal never unsigned but by a u, and an
 * ll suffix. Sets tok_type, tok_val and tok_hi8 (the low 32 bits), and
 * tok_fval (all 64). */
static void int_const(char *s)
{
    struct i64 w;
    struct i32 v;
    int base;
    int d;
    int overflow;
    int suf_u;
    int suf_l;
    int dec;

    /* the base, from the prefix: 0x hexadecimal, 0 octal (so "0" itself
     * is an octal zero), else decimal */
    base = 10;
    i64_set(&w, 0, 0);
    overflow = 0;
    if (*s == '0') {
        s++;
        if (*s == 'x' || *s == 'X') {
            s++;
            base = 16;
            if (hex_digit(*s) < 0)
                error("hexadecimal constant with no digits");
        } else {
            base = 8;
        }
    }
    /* the digits: an 8 or 9 in an octal constant is reported and read as
     * 0; any other non-digit (a hex letter in a decimal, say) begins the
     * suffix */
    for (;; s++) {
        d = hex_digit(*s);
        if (d < 0 || d >= base) {
            if (is_digit(*s))
                error("invalid digit in octal constant");
            if (d < 0 || !is_digit(*s))
                break;
            d = 0;
        }
        if (i64_muladd(&w, (unsigned int)base, (unsigned int)d))
            overflow = 1;
    }
    suf_u = 0;
    suf_l = 0;                          /* 1: l, 2: ll (the default mode's) */
    for (;; s++) {
        if ((*s == 'u' || *s == 'U') && !suf_u) {
            suf_u = 1;
        } else if ((*s == 'l' || *s == 'L') && !suf_l) {
            suf_l = 1;
            if (s[1] == *s && !strict) {        /* ll or LL, never lL (C99) */
                suf_l = 2;
                s++;
            }
        } else {
            break;
        }
    }
    if (*s)
        error("invalid suffix on integer constant");
    if (overflow || (strict && w.hi != 0)) {
        error(strict ? "integer constant too large (more than 32 bits)" : "integer constant too large (more than 64 bits)");
        i64_set(&w, 0, 0);
    }
    /* the type: the first in the list that the value fits, the conditions
     * on each line saying which suffixes and bases may take it */
    dec = base == 10;
    if (!suf_u && !suf_l && w.hi == 0 && w.lo <= 0x7FFFFFUL) {
        tok_type = T_INT;
    } else if (!suf_l && (suf_u || !dec) && w.hi == 0 && w.lo <= 0xFFFFFFUL) {
        tok_type = T_UINT;
    } else if (suf_l < 2 && !suf_u && w.hi == 0 && w.lo <= 0x7FFFFFFFUL) {
        tok_type = T_LONG;
    } else if (suf_l < 2 && (suf_u || !dec || strict) && w.hi == 0) {
        tok_type = T_ULONG;             /* (a decimal: C89's, as strict mode has it) */
    } else if (!suf_u && !(w.hi & 0x80000000UL)) {
        tok_type = T_LLONG;
    } else {
        /* (a decimal beyond long long's range has nowhere else to go) */
        if (dec && !suf_u)
            warning("integer constant is so large that it is unsigned");
        tok_type = T_ULLONG;
    }
    /* the low 32 bits as val and hi8, split by way of struct i32 */
    i32_set(&v, (int)(w.lo >> 16 & 0xFFFF), (int)(w.lo & 0xFFFF));
    tok_val = i32_low24(&v);
    tok_hi8 = i32_high8(&v);
    tok_fval.lo = w.lo;                 /* a long long's 64 bits */
    tok_fval.hi = w.hi;
}

/* a number's text may be as long as a logical source line (C89's 509) */
#define MAX_NUMBER 509

static char numbuf[MAX_NUMBER + 1];     /* the preprocessing number's text */
static char fdigits[MAX_NUMBER + 1];    /* a floating constant's digits, the point taken out */

/* A floating constant's text (C89 3.1.3.1), converted exactly by softfp.c:
 * double, or float with an f suffix (rounded from the decimal itself), or
 * long double with an l. The text is taken apart into what softfp.c's
 * decimal conversion wants: the digits with the point removed (fdigits,
 * nd of them) and a power of ten (exp10), so "12.5e3" is 125 times 10^2.
 * It is never negative: a leading '-' is unary minus, folded in expr.c. */
static void float_const(char *s)
{
    int nd;
    int esign;
    long exp10;
    long e;

    nd = 0;
    exp10 = 0;
    while (is_digit(*s)) {
        fdigits[nd] = *s;
        nd++;
        s++;
    }
    if (*s == '.') {
        s++;
        while (is_digit(*s)) {
            fdigits[nd] = *s;
            nd++;
            s++;
            exp10--;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        esign = 1;
        if (*s == '+') {
            s++;
        } else if (*s == '-') {
            esign = -1;
            s++;
        }
        if (!is_digit(*s))
            error("an exponent with no digits");
        e = 0;
        while (is_digit(*s)) {
            /* capped: an exponent this large is already far beyond any
             * double, and the cap keeps e within a long */
            if (e < 100000)
                e = e * 10 + (*s - '0');
            s++;
        }
        exp10 = exp10 + esign * e;
    }
    tok_type = T_DOUBLE;
    if (*s == 'f' || *s == 'F') {
        tok_type = T_FLOAT;
        s++;
    } else if (*s == 'l' || *s == 'L') {
        tok_type = T_LDOUBLE;
        s++;
    }
    if (*s)
        error("invalid suffix on floating constant");
    /* an all-ones exponent field is infinity (a decimal never gives NaN) */
    if (tok_type == T_FLOAT) {
        tok_fval.hi = 0;
        tok_fval.lo = sf32_from_decimal(0, fdigits, nd, exp10);
        if ((tok_fval.lo & 0x7F800000UL) == 0x7F800000UL)
            warning("floating constant out of range for float (it is infinity)");
    } else {
        sf64_from_decimal(&tok_fval, 0, fdigits, nd, exp10);
        if ((tok_fval.hi & 0x7FF00000UL) == 0x7FF00000UL)
            warning("floating constant out of range for double (it is infinity)");
    }
    tok_val = 0;
    tok_hi8 = 0;
}

/* A preprocessing number (C89 3.1.8: digits, letters, '.', and a sign after
 * an e or E) read whole, then taken as an integer or a floating constant.
 * dot: the '.' that began it has been read. Reading the whole pp-number
 * first is what C requires: "0x1e+1" is one (invalid) token, not 0x1e + 1,
 * and a bad suffix is reported rather than starting a new token. A '.' or
 * an exponent makes it floating, except in a hexadecimal number, where e
 * is a digit. */
static void number(int dot)
{
    int n;
    int i;
    int fl;

    n = 0;
    if (dot) {
        numbuf[0] = '.';
        n = 1;
    }
    while (is_digit(ch) || is_alpha(ch) || ch == '.'
           || ((ch == '+' || ch == '-') && n > 0 && (numbuf[n - 1] == 'e' || numbuf[n - 1] == 'E'))) {
        if (n >= MAX_NUMBER)
            fatal("a number longer than 509 characters");
        numbuf[n] = ch;
        n++;
        getch();
    }
    numbuf[n] = 0;
    tok = TK_NUM;
    fl = 0;
    if (!(numbuf[0] == '0' && (numbuf[1] == 'x' || numbuf[1] == 'X')))
        for (i = 0; i < n; i++)
            if (numbuf[i] == '.' || numbuf[i] == 'e' || numbuf[i] == 'E')
                fl = 1;
    if (fl)
        float_const(numbuf);
    else
        int_const(numbuf);
}

/* A string literal, at its opening quote (after the L if wide), with any
 * literals that follow it concatenated (C89 3.1.4: done in translation
 * phase 6, which here is the lexer), so the parser sees one TK_STR.
 * tok_str gets the bytes, escapes already resolved; a wide string holds
 * each character in 3 bytes, little-endian, as wchar_t is stored. The
 * 509-character limit (C89's minimum) applies to the joined result.
 * Mixing narrow and wide pieces is an error (C89 leaves it undefined). */
static void string_lit(int wide)
{
    char *p;
    int v;

    tok_len = 0;
    tok_wide = wide;
    for (;;) {
        getch();                        /* past the opening quote */
        while (ch != '"') {
            if (ch == '\n' || ch == -1)
                fatal("unterminated string literal");
            if (tok_len >= 509)
                fatal("string literal longer than 509 characters");
            if (ch == '\\') {
                getch();
                v = escape(wide);
            } else {
                v = ch & 255;
                getch();
            }
            if (wide) {
                p = tok_str + 3 * tok_len;      /* little-endian, as the item */
                p[0] = v & 255;
                p[1] = (v >> 8) & 255;
                p[2] = (v >> 16) & 255;
            } else {
                tok_str[tok_len] = v;
            }
            tok_len++;
        }
        getch();                        /* past the closing quote */
        skip_space();
        if (ch == 'L' && peekc() == '"') {
            if (!wide)
                error("a wide string literal next to a narrow one");
            getch();
        } else if (ch != '"') {
            break;                      /* adjacent literals concatenate */
        } else if (wide) {
            error("a narrow string literal next to a wide one");
        }
    }
    tok_str[wide ? 3 * tok_len : tok_len] = 0;
    tok = TK_STR;
}

/* The wide character at p in a wide string's tok_str. */
int wide_char_at(char *p)
{
    return wrap24((p[0] & 255) | (p[1] & 255) << 8 | (p[2] & 255) << 16);
}

/* The lines up to #endasm, as one TK_ASMB token (c89_spec.md 13 item 1).
 * tok_str gets the lines verbatim, each ending '\n', without the #endasm
 * line; stmt.c writes them as an ASM record. A line is copied first and
 * then checked for "#endasm", hence the truncation back to start. */
static void asm_block(void)
{
    int start;
    int i;

    pending_asm = 0;
    tok = TK_ASMB;
    tok_line = asm_line;
    tok_len = 0;
    for (;;) {
        if (ch == -1) {
            fatal("#asm without #endasm");
            return;
        }
        start = tok_len;
        while (ch != '\n' && ch != -1) {
            if (tok_len >= MAX_STRLIT - 2)
                fatal("#asm block longer than 598 bytes (split it)");
            tok_str[tok_len] = ch;
            tok_len++;
            getch();
        }
        if (ch == '\n')
            getch();
        tok_str[tok_len] = 0;
        i = start;
        while (tok_str[i] == ' ' || tok_str[i] == '\t')
            i++;
        if (tok_str[i] == '#') {
            i++;
            while (tok_str[i] == ' ' || tok_str[i] == '\t')
                i++;
            if (strncmp(tok_str + i, "endasm", 6) == 0 && !is_alpha(tok_str[i + 6])) {
                tok_len = start;
                tok_str[tok_len] = 0;
                return;
            }
        }
        tok_str[tok_len] = '\n';
        tok_len++;
    }
}

/* A character constant, after its opening quote's L if wide (c89_spec.md
 * 5): a char's value as int; L'x' the byte as unsigned char, or an escape's
 * value, as wchar_t (int); 'ab' (a warning) the bytes big-endian, up to
 * three. */
static void char_const(int wide)
{
    int c[4];
    int n;
    int v;

    getch();                            /* past the quote */
    n = 0;
    while (ch != '\'' && ch != '\n' && ch != -1) {
        if (ch == '\\') {
            getch();
            v = escape(wide);
        } else {
            v = ch & 255;
            getch();
        }
        if (n < 4)
            c[n] = wide ? v : v & 255;
        n++;
    }
    if (ch == '\'')
        getch();
    else
        error("unterminated character constant");
    tok = TK_NUM;
    tok_type = T_INT;
    tok_val = 0;
    tok_hi8 = 0;
    if (n == 0) {
        error("empty character constant");
    } else if (wide) {
        if (n > 1)
            error("a wide character constant with more than one character");
        tok_val = c[0];
        tok_hi8 = tok_val < 0 ? 255 : 0;
    } else if (n == 1) {
        tok_val = c[0] > 127 ? c[0] - 256 : c[0];      /* the char value, as int */
        tok_hi8 = tok_val < 0 ? 255 : 0;                /* hi8 extends val's sign */
    } else if (n > 3) {
        error("a character constant with more than three characters");
    } else {
        warning("a multi-character character constant");
        tok_val = n == 2 ? c[0] * 256 + c[1] : wrap24(c[0] * 65536 + c[1] * 256 + c[2]);
        tok_hi8 = tok_val < 0 ? 255 : 0;
    }
}

static void lex(void);

/* GCC's spellings, which programs written for it use: names with two
 * underscores are the implementation's, so they are there in both modes
 * (c89_spec.md 13). __const__ and its kin are the keywords;
 * __attribute__((...)), __extension__, __inline__ and __restrict are
 * dropped, and the token after them read instead (by a recursive lex(),
 * so a run of them all goes). Any other name with two underscores stays
 * an identifier: expr.c recognises __builtin_ and __va_ names itself.
 * Called by lex() with tok_name already set to id's name. */
static void gnu_spelling(char *id)
{
    int depth;

    if (strcmp(id, "__const") == 0 || strcmp(id, "__const__") == 0) {
        tok = KW_CONST;
    } else if (strcmp(id, "__volatile") == 0 || strcmp(id, "__volatile__") == 0) {
        tok = KW_VOLATILE;
    } else if (strcmp(id, "__signed") == 0 || strcmp(id, "__signed__") == 0) {
        tok = KW_SIGNED;
    } else if (strcmp(id, "__attribute__") == 0 || strcmp(id, "__attribute") == 0) {
        lex();
        if (tok != TK_P + '(')
            return;                     /* not an attribute after all: the next token */
        /* skip to the matching ')', counting nesting depth */
        depth = 0;
        do {
            if (tok == TK_P + '(')
                depth++;
            else if (tok == TK_P + ')')
                depth--;
            if (depth > 0)
                lex();
        } while (depth > 0 && tok != TK_EOF);
        lex();
    } else if (strcmp(id, "__extension__") == 0 || strcmp(id, "__inline__") == 0 || strcmp(id, "__inline") == 0
               || strcmp(id, "__restrict") == 0 || strcmp(id, "__restrict__") == 0) {
        lex();
    }
}

/* ---- the scanner ------------------------------------------------------------------- */

/* Read the next token into the tok_* globals. The first character decides
 * the kind: a letter or '_' an identifier or keyword, a digit (or '.'
 * and a digit) a number, a quote a literal; anything else is a
 * punctuator, matched longest first ("<<=" before "<<" before "<"). */
static void lex(void)
{
    char id[64];
    int n;
    int c2;

    skip_space();
    if (pending_asm) {
        asm_block();
        return;
    }
    tok_line = line;
    if (ch == -1) {
        tok = TK_EOF;
        return;
    }
    if (ch == 'L' && (peekc() == '\'' || peekc() == '"')) {
        getch();                        /* L'x' and L"...": wide (c89_spec.md 5) */
        if (ch == '\'')
            char_const(1);
        else
            string_lit(1);
        return;
    }
    if (is_alpha(ch)) {
        n = 0;
        while (is_alpha(ch) || is_digit(ch)) {
            if (n < 63) {
                id[n] = ch;
                n++;
            }
            getch();
        }
        id[n] = 0;
        if (n > MAX_IDENT)
            error_s("identifier longer than 47 characters: ", id);
        /* keyword i is name i (lex_open), so the name number says it */
        tok_name = intern(id);
        tok = tok_name < NKW ? TK_KW + tok_name : TK_IDENT;
        if (tok_name == NKW - 1)
            tok = KW_ASM;                       /* __asm */
        else if (tok == KW_ASM && strict)
            tok = TK_IDENT;                     /* asm is not a keyword in C89 */
        else if (id[0] == '_' && id[1] == '_')
            gnu_spelling(id);
        return;
    }
    if (is_digit(ch)) {
        number(0);
        return;
    }
    if (ch == '\'') {
        char_const(0);
        return;
    }
    if (ch == '"') {
        string_lit(0);
        return;
    }
    /* punctuators: a longer match returns at once; a break falls through
     * to the single character token at the end */
    c2 = ch;
    getch();
    switch (c2) {
    case '-':
        if (ch == '>') { getch(); tok = P_ARROW; return; }
        if (ch == '-') { getch(); tok = P_DEC; return; }
        if (ch == '=') { getch(); tok = P_SUBEQ; return; }
        break;
    case '+':
        if (ch == '+') { getch(); tok = P_INC; return; }
        if (ch == '=') { getch(); tok = P_ADDEQ; return; }
        break;
    case '<':
        if (ch == '<') {
            getch();
            if (ch == '=') { getch(); tok = P_SHLEQ; return; }
            tok = P_SHL;
            return;
        }
        if (ch == '=') { getch(); tok = P_LE; return; }
        break;
    case '>':
        if (ch == '>') {
            getch();
            if (ch == '=') { getch(); tok = P_SHREQ; return; }
            tok = P_SHR;
            return;
        }
        if (ch == '=') { getch(); tok = P_GE; return; }
        break;
    case '=':
        if (ch == '=') { getch(); tok = P_EQ; return; }
        break;
    case '!':
        if (ch == '=') { getch(); tok = P_NE; return; }
        break;
    case '&':
        if (ch == '&') { getch(); tok = P_ANDAND; return; }
        if (ch == '=') { getch(); tok = P_ANDEQ; return; }
        break;
    case '|':
        if (ch == '|') { getch(); tok = P_OROR; return; }
        if (ch == '=') { getch(); tok = P_OREQ; return; }
        break;
    case '*':
        if (ch == '=') { getch(); tok = P_MULEQ; return; }
        break;
    case '/':
        if (ch == '=') { getch(); tok = P_DIVEQ; return; }
        break;
    case '%':
        if (ch == '=') { getch(); tok = P_MODEQ; return; }
        break;
    case '^':
        if (ch == '=') { getch(); tok = P_XOREQ; return; }
        break;
    case '.':
        if (is_digit(ch)) {
            number(1);                  /* .5 */
            return;
        }
        if (ch == '.') {
            getch();
            if (ch != '.')
                fatal("'..' is not a token");
            getch();
            tok = P_ELLIPSIS;
            return;
        }
        break;
    case '(': case ')': case '[': case ']': case '{': case '}': case ',': case ';':
    case ':': case '?': case '~':
        break;
    default:
        tok_line = line;
        fatal("stray character in source");
    }
    tok = TK_P + c2;
}

/* ---- next and peek ----------------------------------------------------------------- */

/* save and load copy the current token to and from a lexeme; the text is
 * copied only for the kinds that have one, since it can be long. */
static void save(struct lexeme *l)
{
    l->kind = tok;
    l->val = tok_val;
    l->hi8 = tok_hi8;
    l->type = tok_type;
    l->name = tok_name;
    l->len = tok_len;
    l->line = tok_line;
    l->wide = tok_wide;
    l->fval = tok_fval;
    if (tok == TK_STR || tok == TK_ASMB)
        memcpy(l->str, tok_str, (tok == TK_STR && tok_wide ? 3 * tok_len : tok_len) + 1);
}

static void load(struct lexeme *l)
{
    tok = l->kind;
    tok_val = l->val;
    tok_hi8 = l->hi8;
    tok_type = l->type;
    tok_name = l->name;
    tok_len = l->len;
    tok_line = l->line;
    tok_wide = l->wide;
    tok_fval = l->fval;
    if (tok == TK_STR || tok == TK_ASMB)
        memcpy(tok_str, l->str, (tok == TK_STR && tok_wide ? 3 * tok_len : tok_len) + 1);
}

static struct lexeme saved;     /* the current token, kept while peek() lexes */

/* Advance to the next token: the one peek() already read, if any. */
void next(void)
{
    prev_line = tok_line;
    if (have_la) {
        load(&la);
        have_la = 0;
    } else {
        lex();
    }
}

/* One token of lookahead. lex() only writes the tok_* globals, so the
 * current token is set aside, the next one lexed and kept in la, and the
 * current one put back. Only one token can be peeked at. */
int peek(void)
{
    if (!have_la) {
        save(&saved);
        lex();
        save(&la);
        load(&saved);
        have_la = 1;
    }
    return la.kind;
}

int peek_name(void)
{
    return la.name;
}

/* Consume a token of the given kind; a missing one is fatal, with no
 * attempt to resynchronise, and is reported at the previous token's line. */
void expect(int kind, char *what)
{
    char buf[80];

    if (tok != kind) {
        sprintf(buf, "expected %s", what);
        if (tok == TK_EOF)
            strcat(buf, " before end of file");
        tok_line = prev_line;           /* where the missing token belongs */
        fatal(buf);
    }
    next();
}

int accept(int kind)
{
    if (tok == kind) {
        next();
        return 1;
    }
    return 0;
}
