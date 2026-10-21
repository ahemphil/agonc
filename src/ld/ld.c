/* ld.c - the pseudo-linker (docs/object_format.md section 4).
 *
 *     ld -o out.asm file.s... [--if-needed lib.s]... [--entry=sym]... [--moslet] [-v]
 *     ld --index lib.s...
 *
 * --moslet lays the program out for the moslet area (0x0B0000-0x0B7FFF)
 * instead of user RAM; the driver is built that way.
 *
 * Reads every input twice: pass 1 indexes sections, symbols and references
 * from the ;; marker lines; pass 4 seeks back to each selected section and
 * copies its body. Section bodies are never held in memory - only the
 * index, in fixed tables (limits below, reported by -v).
 *
 * Pass 1 over a library is most of a link's time on the Agon, and a
 * library's sections would fill the tables whether used or not, so
 * --index writes what it finds in lib.s to lib.idx (object_format.md
 * section 9): each section's markers as a record, with its offsets, and a
 * directory of every name the library defines, each as a hash and the
 * offset of its section's record. A later link reads only lib.idx's units
 * and directory, provided it records lib.s's current size (otherwise it
 * reads lib.s as before), and loads a section's record only when a name
 * the program needs is defined there: so the tables hold the program's
 * own sections and the library sections it uses, not whole libraries.
 * Pass 4 checks each section's ;;sect line where the index says it is, so
 * an index made from a different file of the same size is an error, not a
 * wrong link.
 *
 * --if-needed lib.s reads lib.s (or its index) only if the other inputs
 * leave a strong reference from a reachable section undefined, so a
 * program that uses nothing in it does not pay for reading it (the driver
 * passes /lib/libagon.s so).
 *
 * Beyond the object format's rules, ld also rejects a ;;unit whose id is
 * not the FNV-1a hash of its name, and drops full-line comments and blank
 * lines while copying (they cost the on-device assembler time and nothing
 * else). After selection it checks each call through an implicit
 * declaration (;;implicit) against the callee's return kind (;;ret):
 * object_format.md section 8.
 *
 * Where it sits: the driver runs ld after cc2 has turned every C input
 * into a .s unit; the inputs are crt0.s, rt.s, those units, any -l
 * libraries, libc.s (and libm.s), then --if-needed libagon.s. Its output
 * is one .asm, which the driver hands to ez80asm. It is a "pseudo-linker"
 * because nothing is resolved to addresses here: symbols stay symbolic,
 * and the assembler resolves them when it assembles the joined text. What
 * ld does is choose and order the text.
 *
 * The passes:
 *   1. index: read each input's ;; markers into the tables (index_file),
 *      or an indexed library's units and directory (read_dir);
 *   2. select: mark every section reachable from the roots (__start and
 *      --entry symbols) through ;;ref, as the mark phase of a
 *      mark-and-sweep garbage collector marks what a program can reach;
 *      a name nothing read so far defines is looked up in the indexed
 *      libraries' directories and its section's record loaded; unmarked
 *      sections are simply never copied (resolve). The sections are then
 *      put in input order (sort_sects), so the output is the same as if
 *      every library had been read whole;
 *   3. order: put the selected code sections in depth-first post-order,
 *      so that every callee comes before its callers, except around a
 *      cycle of calls; a backward reference costs the assembler no fixup
 *      record, so its memory stays flat however large the program
 *      (object_format.md sections 3 and 4) (order_all);
 *   4. copy: write the layout prologue, then __start, the ordered code and
 *      the data, each section's body copied from its input (write_output).
 *
 * The tables: symbols are interned (each name stored once in `names`,
 * found again through an open-addressing hash table) so that the rest of
 * ld works with small integer symbol numbers; sym_def maps a symbol to
 * the section defining it. Each section's references are a run
 * refs[ref0 .. ref0+nref-1] of symbol numbers in one shared array, filled
 * in file order as the ;;ref lines come.
 *
 * Map: symbols, pass 1 (with --index's writer), pass 2 (with the
 * directory lookups and the loading of records), pass 3, pass 4, main.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"
#include "io.h"
#include "rd.h"

#define MAX_FILES 48            /* inputs: the driver's 32 and the libraries */
#define MAX_UNITS 128           /* every input's units, indexed libraries' included */
#define MAX_SECTS 1500          /* sections read or loaded */
#define MAX_DIR 4000            /* the indexed libraries' directory entries, all together */
#define MAX_WANT 1000           /* names being looked for in the libraries at once */
#define MAX_SYMS 3000
#define MAX_REFS 8000
#define MAX_IMPLICIT 400
#define MAX_WEAK 200            /* ;;wref lines, all units together */
#define MAX_LABELS 1000         /* labels besides sections' own, for --index */
#define NAME_TEXT 40000
#define HASH_SIZE 4096
#define MAX_ENTRIES 16
#define LINE_BUF 260
#define MAX_TOKS 8

/* a section's kind, from its ;;sect line */
#define K_CODE 0
#define K_DATA 1
#define K_BSS 2

/* sym_def values besides a section number */
#define DEF_NONE (-1)           /* not defined (yet) */
#define DEF_LAYOUT (-2)         /* one of the names the prologue defines */

/* pass 3's depth-first search: not reached, on the stack, finished */
#define ST_NEW 0
#define ST_OPEN 1
#define ST_DONE 2

struct sect {
    int sym;            /* its symbol (the section's own label) */
    int unit;
    int kind;
    int start;          /* the offsets in its file of the body pass 4 copies */
    int end;
    int size;           /* bss only: its size in bytes */
    int ref0;           /* its references: refs[ref0 .. ref0 + nref - 1] */
    int nref;
    int selected;       /* pass 2 reached it */
    int state;          /* pass 3's ST_ value */
    int mark;           /* the offset of its ;;sect line */
    int old;            /* sort_sects: its number before sorting */
    char ret;           /* ;;ret's kind (i, l, r, v), or 0 if none was given */
    char vis;           /* g or s, as its ;;sect line says */
    char clean;         /* nothing in the body for pass 4 to drop: copied whole */
};

struct unit {
    int name;           /* offset of its name in names */
    int file;           /* the input it came from */
};

/* the symbol table: each symbol's name (an offset into names), its
 * definition (a section, or DEF_NONE or DEF_LAYOUT), and hash_tab, which
 * holds symbol numbers (-1: empty slot) */
static char names[NAME_TEXT];
static int names_used;
static int sym_name[MAX_SYMS];
static int sym_def[MAX_SYMS];
static int nsyms;
static int hash_tab[HASH_SIZE];

static struct sect sects[MAX_SECTS];
static int nsects;
static int refs[MAX_REFS];
static int nrefs;
static int weak_unit[MAX_WEAK];         /* ;;wref: the unit whose references are weak */
static int weak_sym[MAX_WEAK];          /* and the symbol */
static int nweak;
static int imp_sect[MAX_IMPLICIT];      /* ;;implicit: the calling section */
static int imp_sym[MAX_IMPLICIT];       /* and the callee */
static int nimplicit;
static struct unit units[MAX_UNITS];
static int nunits;
static int label_sym[MAX_LABELS];       /* --index: the labels, in file order */
static int label_sect[MAX_LABELS];      /* and the section of each */
static int nlabels;

/* An indexed library's directory: for each name it defines, the name's
 * hash (unit_hash) and the offset in its index of the record of the
 * section defining it; each library's entries, dir[dir_lo .. dir_hi-1],
 * sorted by hash. Two names can share a hash, so a record is checked
 * for the name before it is loaded (record_defines). */
struct dent {
    unsigned short hash;
    int rec;
};

static struct dent dir[MAX_DIR];
static int ndir;

static char *files[MAX_FILES];
static char lazy[MAX_FILES];    /* --if-needed: read only if it may define something */
static char file_read[MAX_FILES];       /* read: its sections, or its index's units and directory */
static char file_idx[MAX_FILES];        /* read through its index */
static int file_rank[MAX_FILES];        /* the order the inputs were read in */
static int dir_lo[MAX_FILES];
static int dir_hi[MAX_FILES];
static int unit0[MAX_FILES];            /* its first unit's number */
static int nranked;
static int nfiles;

/* The names wanted from the indexed libraries: strong references from
 * selected sections, and roots, that nothing read so far defines. Each
 * one's search stands at a file and an entry of its directory (-1: not
 * yet looked for there). wanted[sym] says sym is on the list. */
static int want_sym[MAX_WANT];
static int want_file[MAX_WANT];
static int want_ent[MAX_WANT];
static int probe[MAX_WANT];             /* one round's searches, in file and record order */
static int nwant;
static int peak_want;
static char wanted[MAX_SYMS];
static int load_file;                   /* the index open in in_rd for loading, or -1 */
static int nloaded;                     /* sections loaded from indexes */
static char *entries[MAX_ENTRIES];
static int nentries;
static char *out_path;
static int verbose;
static int moslet;
static int make_index;          /* --index */
static int next_lazy;           /* the next input follows --if-needed */
static int nindexed;            /* inputs read through their index */
static int nlazy_read;          /* --if-needed libraries that were read */

static int errors;
static int order[MAX_SECTS];    /* pass 3's result: code sections in output order */
static int norder;
/* pass 3's explicit stack, in place of recursion, so a long chain of
 * calls needs no C stack: a section, and how many of its references have
 * been followed */
static int stack_s[MAX_SECTS];
static int stack_i[MAX_SECTS];
static int work[MAX_SECTS];     /* pass 2's work list: selected, not yet scanned */
static int fwd_calls;           /* calls to a section placed later (cycles) */

static char line[LINE_BUF];
static char *tok[MAX_TOKS];
static struct rd in_rd;         /* pass 1's input, then pass 4's */

/* ---- symbols ------------------------------------------------------------ */

/* The classic multiply-by-31 string hash, cut to a slot of hash_tab. */
static unsigned hash_name(char *s)
{
    unsigned h;

    h = 0;
    while (*s) {
        h = h * 31 + (unsigned char)*s;
        s++;
    }
    return h & (HASH_SIZE - 1);
}

/* A fixed table is full: stop at once with status 200. */
static void fail_limit(char *what)
{
    fprintf(stderr, "ld: too many %s (limit reached; raise it in ld.c)\n", what);
    rd_close(&in_rd);                   /* MOS closes nothing at exit */
    exit(200);
}

/* Returns the symbol index for s, adding it if new. This is hash
 * interning: every spelling of a name maps to one number, so later
 * comparisons are of integers, not strings. Collisions use linear
 * probing (the next slot, wrapping round); nothing is ever removed, so
 * an empty slot ends the search. The table must keep an empty slot:
 * HASH_SIZE (4096) is above MAX_SYMS (3000). */
static int intern(char *s)
{
    unsigned h;
    int i;
    int len;

    h = hash_name(s);
    while (hash_tab[h] >= 0) {
        i = hash_tab[h];
        if (strcmp(names + sym_name[i], s) == 0)
            return i;
        h = (h + 1) & (HASH_SIZE - 1);
    }
    len = strlen(s);
    if (nsyms >= MAX_SYMS)
        fail_limit("symbols");
    if (names_used + len + 1 > NAME_TEXT)
        fail_limit("name characters");
    strcpy(names + names_used, s);
    sym_name[nsyms] = names_used;
    sym_def[nsyms] = DEF_NONE;
    names_used = names_used + len + 1;
    hash_tab[h] = nsyms;
    nsyms++;
    return nsyms - 1;
}

static char *sym_str(int sym)
{
    return names + sym_name[sym];
}

static char *unit_str(int u)
{
    return names + units[u].name;
}

/* ---- pass 1: index ------------------------------------------------------- */

static char *cur_file;          /* for messages: the file pass 1 is reading */
static int cur_line;            /* and the line, or 0 for a record loaded from an index */

/* "file:line: " before a message, or "file: " when the line is not known. */
static void where(void)
{
    if (cur_line > 0)
        fprintf(stderr, "%s:%d: ", cur_file, cur_line);
    else
        fprintf(stderr, "%s: ", cur_file);
}

static void error_here(char *msg, char *arg)
{
    where();
    fprintf(stderr, "error: %s%s\n", msg, arg);
    errors++;
}

/* Splits `line` in place into tokens; stops at a token starting with ';'
 * after the first (a trailing comment). Returns the token count. */
static int split(void)
{
    int n;
    char *p;

    n = 0;
    p = line;
    while (*p && n < MAX_TOKS) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (*p == 0 || (n > 0 && *p == ';'))
            break;
        tok[n] = p;
        n++;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
        if (*p) {
            *p = 0;
            p++;
        }
    }
    return n;
}

/* The unit id: the low 16 bits of the 32-bit FNV-1a hash of the unit's
 * name (object_format.md section 2). FNV-1a XORs in each byte, then
 * multiplies by a prime. The low 16 bits of a product depend only on the
 * low 16 bits of its factors, so 32-bit FNV-1a (offset basis 0x811C9DC5,
 * prime 0x01000193) can be computed with only their low halves, 0x9DC5
 * and 0x0193, masking to 16 bits each time. That needs only unsigned
 * arithmetic: on the eZ80 an unsigned is 24 bits, and a product that
 * wraps past them still has the right low 16 bits. cc1 computes the
 * same hash. */
static unsigned unit_hash(char *s)
{
    unsigned h;

    h = 0x9DC5;
    while (*s) {
        h = ((h ^ (unsigned char)*s) * 0x193) & 0xFFFF;
        s++;
    }
    return h;
}

/* sym is defined by section s: its own label, a ;;label, or a bare label
 * found in its body. A second definition of any name is an error, both
 * places named, whatever the visibility (assembly names are unique: an
 * internal one carries its unit id). Under --index, every name besides
 * the section's own is remembered so that write_index can list it as a
 * ;;label. */
static void define(int sym, int s)
{
    int old;

    old = sym_def[sym];
    if (old == DEF_LAYOUT) {
        error_here("symbol is reserved for ld's layout: ", sym_str(sym));
    } else if (old >= 0) {
        where();
        fprintf(stderr, "error: duplicate definition of %s (unit %s section %s, and unit %s section %s)\n",
                sym_str(sym),
                unit_str(sects[old].unit), sym_str(sects[old].sym),
                unit_str(sects[s].unit), sym_str(sects[s].sym));
        errors++;
    } else {
        sym_def[sym] = s;
        if (make_index && sym != sects[s].sym) {
            if (nlabels >= MAX_LABELS)
                fail_limit("labels");
            label_sym[nlabels] = sym;
            label_sect[nlabels] = s;
            nlabels++;
        }
    }
}

static int is_ident_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* A body line of the form "name:" at column 0 defines a bare label. Such
 * a label in hand-written assembly without its ;;label line would still
 * be a global name to the assembler, so ld records it too, and two of
 * one name are reported here rather than by ez80asm. A name starting
 * with a digit, or with '@' (a local label), is not one. */
static void check_bare_label(int s)
{
    int i;
    int sym;

    if (!is_ident_char(line[0]) || (line[0] >= '0' && line[0] <= '9'))
        return;
    i = 0;
    while (is_ident_char(line[i]))
        i++;
    if (line[i] != ':')
        return;
    line[i] = 0;
    sym = intern(line);
    if (sym != sects[s].sym && sym_def[sym] != s)   /* not the section's own, nor its ;;label */
        define(sym, s);
}

/* ";;unit <name> <id>", or ";;unit <name>" alone in a hand-written .s,
 * whose id ld works out itself (object_format.md 1). */
static void new_unit(int ntok, int file)
{
    char want[8];

    if (ntok != 2 && ntok != 3) {
        error_here("malformed ;;unit line", "");
        return;
    }
    if (nunits >= MAX_UNITS)
        fail_limit("units");
    sprintf(want, "%04x", unit_hash(tok[1]));
    if (ntok == 3 && strcmp(want, tok[2]) != 0) {
        fprintf(stderr, "%s:%d: error: unit %s has id %s, but its name hashes to %s\n",
                cur_file, cur_line, tok[1], tok[2], want);
        errors++;
    }
    units[nunits].name = sym_name[intern(tok[1])];
    units[nunits].file = file;
    nunits++;
}

/* ";;sect <kind> <sym> <g|s> [<size>]" (tok[], ntok words), at offset mark,
 * the next line at pos: a new section of unit u, defining sym. Its body
 * starts at pos until tidy() moves the start; its end is set when the
 * next marker that ends it comes (end_sect), or by an index's ;;at. -1 if
 * malformed. */
static int new_sect(int ntok, int u, int mark, int pos)
{
    int kind;
    int s;

    if (u < 0) {
        error_here("section before any ;;unit line", "");
        return -1;
    }
    if (strcmp(tok[1], "code") == 0)
        kind = K_CODE;
    else if (strcmp(tok[1], "data") == 0)
        kind = K_DATA;
    else if (strcmp(tok[1], "bss") == 0)
        kind = K_BSS;
    else {
        error_here("unknown section kind ", tok[1]);
        return -1;
    }
    if (ntok != (kind == K_BSS ? 5 : 4) || (strcmp(tok[3], "g") != 0 && strcmp(tok[3], "s") != 0)) {
        error_here("malformed ;;sect line", "");
        return -1;
    }
    if (nsects >= MAX_SECTS)
        fail_limit("sections");
    s = nsects;
    nsects++;
    sects[s].sym = intern(tok[2]);
    sects[s].unit = u;
    sects[s].kind = kind;
    sects[s].mark = mark;
    sects[s].start = pos;
    sects[s].end = pos;
    sects[s].vis = tok[3][0];
    sects[s].clean = 1;
    sects[s].ret = 0;                   /* (--index reuses the table for each file) */
    sects[s].size = kind == K_BSS ? atoi(tok[4]) : 0;
    sects[s].ref0 = nrefs;
    sects[s].nref = 0;
    sects[s].selected = 0;
    sects[s].state = ST_NEW;
    define(sects[s].sym, s);
    return s;
}

static char idx_path[LINE_BUF];

/* idx_path: path with its .s replaced by .idx (or .idx added). */
static void index_name(char *path)
{
    int n;

    n = strlen(path);
    if (n + 5 > LINE_BUF)
        fail_limit("characters in a file name");
    strcpy(idx_path, path);
    if (n >= 2 && idx_path[n - 2] == '.' && (idx_path[n - 1] == 's' || idx_path[n - 1] == 'S'))
        n = n - 2;
    strcpy(idx_path + n, ".idx");
}

/* The file's size in bytes, or -1: what an index records to tell whether
 * it was made from the file as it is. */
static long file_size(char *path)
{
    FILE *f;
    long n;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    n = -1;
    if (fseek(f, 0L, SEEK_END) == 0)
        n = ftell(f);
    fclose(f);
    return n;
}

static int idx_dir;              /* open_index: where the index's directory starts */
static int idx_ndir;             /* and how many entries it has */

/* Opens in_rd on file's index if there is one of this version made from
 * the file as it is now (its first line records the file's size, then
 * the directory's offset and length), leaving it just past that line.
 * The size is a cheap test of staleness: editing or remaking a library
 * almost always changes its length, and copy_sect also checks each
 * ;;sect line where the index says it is. An index of version 1, which
 * had no directory, counts as stale. 0 means read the .s itself. */
static int open_index(int file)
{
    long size;

    index_name(files[file]);
    if (!rd_open(&in_rd, idx_path))
        return 0;
    if (rd_gets(&in_rd, line, LINE_BUF) > 0 && split() == 5 && strcmp(tok[0], ";;agonc-index") == 0
        && strcmp(tok[1], "2") == 0) {
        size = file_size(files[file]);
        idx_dir = atoi(tok[3]);
        idx_ndir = atoi(tok[4]);
        if (size >= 0 && atol(tok[2]) == size)
            return 1;
    }
    rd_close(&in_rd);
    if (verbose)
        printf("ld: %s does not match %s; reading %s\n", idx_path, files[file], files[file]);
    return 0;
}

/* A line pass 4 would drop: blank, or a comment (markers included). */
static int dropped(void)
{
    char *p;

    p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    return *p == 0 || *p == '\n' || *p == '\r' || *p == ';';
}

/* Pass 4 copies a clean section's body as it is, in blocks, rather than
 * line by line (most of a link's time on the Agon went to writing lines).
 * Lines it would drop before the first instruction move the body's start
 * past them; one after it, or a line ending in CR or without a newline,
 * leaves the section to be copied line by line. A compiled unit's
 * sections are all clean; hand-written ones with comments are not.
 * (Line-by-line copying writes each line with a bare LF; a block copy
 * writes the bytes as they are. The two give the same output only when
 * the body has nothing to drop and every line already ends in a bare LF.)
 * cur is the section, drop whether this line would be dropped, len its
 * length in `line`, *body whether an instruction has been seen in the
 * section yet, pos the offset just after this line. */
static void tidy(int cur, int drop, int len, int *body, int pos)
{
    if (cur < 0 || sects[cur].kind == K_BSS)
        return;
    if (drop) {
        if (*body)
            sects[cur].clean = 0;
        else
            sects[cur].start = pos;
    } else {
        *body = 1;
        if (line[len - 1] != '\n' || (len >= 2 && line[len - 2] == '\r'))
            sects[cur].clean = 0;
    }
}

/* A section ends where the next marker starts. */
static void end_sect(int cur, int at)
{
    if (cur >= 0)
        sects[cur].end = at;
}

/* ";;wref <sym>": unit u's references to sym are weak (object_format.md 8). */
static void add_weak(int u, char *sym)
{
    if (nweak >= MAX_WEAK)
        fail_limit("weak references");
    weak_unit[nweak] = u;
    weak_sym[nweak] = intern(sym);
    nweak++;
}

/* Pass 1 over one .s. Every line is read, but only the ;; markers matter
 * to the tables; other lines feed tidy() and the bare label check. pos
 * counts bytes, so each section's offsets are known without holding its
 * text. */
static void index_file(int file)
{
    int pos;
    int start;
    int len;
    int cur;
    int ntok;
    int body;
    char *m;

    body = 0;
    cur_file = files[file];
    cur_line = 0;
    if (!rd_open(&in_rd, cur_file)) {
        fprintf(stderr, "ld: cannot open %s\n", cur_file);
        errors++;
        return;
    }
    pos = 0;
    cur = -1;
    for (;;) {
        len = rd_gets(&in_rd, line, LINE_BUF);
        if (len == 0)
            break;
        cur_line++;
        start = pos;
        pos = pos + len;
        /* a full buffer without a newline: the line goes on past it */
        if (len > 0 && line[len - 1] != '\n' && len >= LINE_BUF - 1) {
            error_here("line longer than 255 characters", "");
            break;
        }
        if (line[0] == ';' && line[1] == ';') {
            ntok = split();
            m = tok[0] + 2;             /* the marker's name, after the ;; */
            if (strcmp(m, "sect") != 0 && strcmp(m, "unit") != 0 && strcmp(m, "end") != 0)
                tidy(cur, 1, len, &body, pos);  /* a marker inside the section */
            if (strcmp(m, "ref") == 0) {
                /* refs are appended in order, so a section's run stays
                 * contiguous: its ;;ref lines all come before the next ;;sect */
                if (cur < 0 || ntok != 2) {
                    error_here("misplaced or malformed ;;ref", "");
                } else {
                    if (nrefs >= MAX_REFS)
                        fail_limit("references");
                    refs[nrefs] = intern(tok[1]);
                    nrefs++;
                    sects[cur].nref++;
                }
            } else if (strcmp(m, "wref") == 0) {
                /* this unit's references to the symbol are weak (object_format.md 8) */
                if (nunits == 0 || ntok != 2)
                    error_here("misplaced or malformed ;;wref", "");
                else
                    add_weak(nunits - 1, tok[1]);
            } else if (strcmp(m, "ret") == 0) {
                if (cur < 0 || ntok != 2 || strlen(tok[1]) != 1 || strchr("ilrv", tok[1][0]) == NULL)
                    error_here("misplaced or malformed ;;ret", "");
                else
                    sects[cur].ret = tok[1][0];
            } else if (strcmp(m, "implicit") == 0) {
                if (cur < 0 || ntok != 2) {
                    error_here("misplaced or malformed ;;implicit", "");
                } else {
                    if (nimplicit >= MAX_IMPLICIT)
                        fail_limit("implicit calls");
                    imp_sect[nimplicit] = cur;
                    imp_sym[nimplicit] = intern(tok[1]);
                    nimplicit++;
                }
            } else if (strcmp(m, "sect") == 0) {
                end_sect(cur, start);
                cur = ntok >= 4 ? new_sect(ntok, nunits - 1, start, pos) : -1;
                body = 0;
                if (ntok < 4)
                    error_here("malformed ;;sect line", "");
            } else if (strcmp(m, "at") == 0) {
                error_here("misplaced or malformed ;;at", "");  /* an index's alone */
            } else if (strcmp(m, "label") == 0) {
                if (cur < 0 || ntok != 2)
                    error_here("misplaced or malformed ;;label", "");
                else
                    define(intern(tok[1]), cur);
            } else if (strcmp(m, "unit") == 0) {
                end_sect(cur, start);
                cur = -1;
                new_unit(ntok, file);
            } else if (strcmp(m, "end") == 0) {
                end_sect(cur, start);
                cur = -1;
            } else if (strcmp(m, "agonc-object") == 0) {
                if (ntok != 2 || strcmp(tok[1], "1") != 0)
                    error_here("unsupported object format version", "");
            } else {
                error_here("unknown marker ", tok[0]);
            }
        } else {
            tidy(cur, dropped(), len, &body, pos);
            if (line[0] != ' ' && cur >= 0 && sects[cur].kind != K_BSS)
                check_bare_label(cur);  /* (an indented line is never a label) */
        }
    }
    end_sect(cur, pos);
    rd_close(&in_rd);
}

/* An indexed library's units, each with its ;;wref lines, which come
 * first in the index, then its directory: all a link reads of it until a
 * section of it is needed (load_record). in_rd is open on the index, just
 * past its first line (open_index). */
static void read_dir(int file)
{
    int i;
    int ntok;

    cur_file = idx_path;
    cur_line = 1;
    for (;;) {
        if (rd_gets(&in_rd, line, LINE_BUF) == 0)
            break;
        cur_line++;
        ntok = split();
        if (ntok >= 2 && strcmp(tok[0], ";;unit") == 0)
            new_unit(ntok, file);
        else if (ntok == 2 && strcmp(tok[0], ";;wref") == 0 && nunits > unit0[file])
            add_weak(nunits - 1, tok[1]);
        else
            break;                      /* the first record: the units are done */
    }
    rd_seek(&in_rd, idx_dir);
    cur_line = 0;
    dir_lo[file] = ndir;
    if (rd_gets(&in_rd, line, LINE_BUF) == 0 || split() != 1 || strcmp(tok[0], ";;dir") != 0) {
        error_here("no directory where the index's first line says", "");
        idx_ndir = 0;
    }
    for (i = 0; i < idx_ndir; i++) {
        if (rd_gets(&in_rd, line, LINE_BUF) == 0 || split() != 2) {
            error_here("malformed directory entry", "");
            break;
        }
        if (ndir >= MAX_DIR)
            fail_limit("directory entries");
        dir[ndir].hash = (unsigned short)strtol(tok[0], NULL, 16);
        dir[ndir].rec = atoi(tok[1]);
        ndir++;
    }
    dir_hi[file] = ndir;
    rd_close(&in_rd);
}

/* Pass 1 over one input: an indexed library's units and directory, or a
 * .s whole. The order inputs are read in is the order their sections
 * take in the output (sort_sects). */
static void read_input(int file)
{
    file_read[file] = 1;
    file_rank[file] = nranked;
    nranked++;
    unit0[file] = nunits;
    if (open_index(file)) {
        file_idx[file] = 1;
        nindexed++;
        read_dir(file);
    } else {
        index_file(file);
    }
}

static char *kind_name[3] = { "code", "data", "bss" };

static FILE *idx_out;           /* write_index: the .idx, or NULL while sizing */
static long idx_bytes;          /* the bytes written (or that would be) so far */
static int rec_at[MAX_SECTS];   /* each section's record's offset in the .idx */
static char ib[LINE_BUF + 80];  /* one line of the .idx */

/* One line of the .idx: written, or only counted while sizing. */
static void put(void)
{
    idx_bytes = idx_bytes + strlen(ib);
    if (idx_out != NULL)
        out_str(idx_out, ib);
}

/* The units, then a record per section, in file order: write_index runs
 * this twice, first only counting bytes so that the first line can give
 * the directory's offset, then writing. A unit's ;;wref lines all go
 * straight after its ;;unit line, since a ;;wref covers the whole unit
 * wherever it stood. A record starts with ;;in, its unit's number in this
 * file (load_record reads that before the section's names), and ends
 * where the next starts. */
static void put_records(void)
{
    int u;
    int s;
    int i;

    for (u = 0; u < nunits; u++) {
        sprintf(ib, ";;unit %s\n", unit_str(u));
        put();
        for (i = 0; i < nweak; i++) {
            if (weak_unit[i] == u) {
                sprintf(ib, ";;wref %s\n", sym_str(weak_sym[i]));
                put();
            }
        }
    }
    for (s = 0; s < nsects; s++) {
        rec_at[s] = (int)idx_bytes;
        sprintf(ib, ";;in %d\n", sects[s].unit);
        put();
        sprintf(ib, ";;sect %s %s %c", kind_name[sects[s].kind], sym_str(sects[s].sym), sects[s].vis);
        put();
        if (sects[s].kind == K_BSS) {
            sprintf(ib, " %d", sects[s].size);
            put();
        }
        sprintf(ib, "\n;;at %d %d %d %d\n", sects[s].mark, sects[s].start, sects[s].end, sects[s].clean);
        put();
        if (sects[s].ret) {
            sprintf(ib, ";;ret %c\n", sects[s].ret);
            put();
        }
        for (i = 0; i < nlabels; i++) {
            if (label_sect[i] == s) {
                sprintf(ib, ";;label %s\n", sym_str(label_sym[i]));
                put();
            }
        }
        for (i = 0; i < sects[s].nref; i++) {
            sprintf(ib, ";;ref %s\n", sym_str(refs[sects[s].ref0 + i]));
            put();
        }
        for (i = 0; i < nimplicit; i++) {
            if (imp_sect[i] == s) {
                sprintf(ib, ";;implicit %s\n", sym_str(imp_sym[i]));
                put();
            }
        }
    }
}

/* Directory entries in hash order, and records in file order among one
 * hash, so that an index is the same however it was made. */
static int dent_cmp(const void *a, const void *b)
{
    const struct dent *x;
    const struct dent *y;

    x = (const struct dent *)a;
    y = (const struct dent *)b;
    if (x->hash != y->hash)
        return x->hash < y->hash ? -1 : 1;
    if (x->rec != y->rec)
        return x->rec < y->rec ? -1 : 1;
    return 0;
}

/* One directory entry: name, defined by the section whose record is at rec. */
static void add_dent(char *name, int rec)
{
    if (ndir >= MAX_DIR)
        fail_limit("directory entries");
    dir[ndir].hash = (unsigned short)unit_hash(name);
    dir[ndir].rec = rec;
    ndir++;
}

/* --index: file's units and section records, then the directory of the
 * names it defines, into file's .idx (object_format.md section 9). The
 * tables hold this file alone (main starts them afresh for each). The
 * first line records the .s's size, which open_index compares, and the
 * directory's offset and length, in fixed widths so that the line's own
 * length is known before they are. */
static int write_index(int file)
{
    long size;
    int dir_at;
    int s;
    int i;

    index_name(files[file]);
    size = file_size(files[file]);
    sprintf(ib, ";;agonc-index 2 %ld %08d %05d\n", size, 0, 0);
    idx_out = NULL;
    idx_bytes = strlen(ib);
    put_records();
    dir_at = (int)idx_bytes;
    ndir = 0;
    for (s = 0; s < nsects; s++)
        add_dent(sym_str(sects[s].sym), rec_at[s]);
    for (i = 0; i < nlabels; i++)
        add_dent(sym_str(label_sym[i]), rec_at[label_sect[i]]);
    qsort(dir, ndir, sizeof dir[0], dent_cmp);

    idx_out = fopen(idx_path, "wb");
    if (idx_out == NULL) {
        fprintf(stderr, "ld: cannot create %s\n", idx_path);
        return 0;
    }
    sprintf(ib, ";;agonc-index 2 %ld %08d %05d\n", size, dir_at, ndir);
    idx_bytes = 0;
    put();
    put_records();
    sprintf(ib, ";;dir\n");
    put();
    for (i = 0; i < ndir; i++) {
        sprintf(ib, "%04x %d\n", (unsigned)dir[i].hash, dir[i].rec);
        put();
    }
    if (fclose(idx_out) != 0) {
        idx_out = NULL;
        fprintf(stderr, "ld: cannot write %s\n", idx_path);
        remove(idx_path);
        return 0;
    }
    idx_out = NULL;
    return 1;
}

/* ---- pass 2: select ----------------------------------------------------- */

static int nwork;

/* Is sym referenced weakly by unit u? A ;;wref covers every reference to
 * sym from all of u's sections. A linear search, but the list is short
 * and callers skip it when it is empty. */
static int is_weak(int u, int sym)
{
    int i;

    for (i = 0; i < nweak; i++)
        if (weak_unit[i] == u && weak_sym[i] == sym)
            return 1;
    return 0;
}

/* Marks s and puts it on the work list, once: the selected flag means a
 * section is never scanned twice, so the walk ends even with cycles, and
 * work[] (MAX_SECTS long) cannot overflow. */
static void select_sect(int s)
{
    if (!sects[s].selected) {
        sects[s].selected = 1;
        work[nwork] = s;
        nwork++;
    }
}

/* sym is wanted: nothing read so far defines it, but a root or a strong
 * reference from a selected section names it, so the indexed libraries'
 * directories are searched for it (resolve). Once only. */
static void want(int sym)
{
    if (wanted[sym])
        return;
    if (nwant >= MAX_WANT)
        fail_limit("names wanted from libraries");
    wanted[sym] = 1;
    want_sym[nwant] = sym;
    want_file[nwant] = 0;
    want_ent[nwant] = -1;
    nwant++;
    if (nwant > peak_want)
        peak_want = nwant;
}

/* Reachability: a work-list graph walk. A section taken off the list has
 * each strong reference followed, selecting the defining section, or
 * wanting the name if nothing read so far defines it; the walk ends when
 * the list is empty. Only a reachable section's references matter, so an
 * undefined symbol is an error only if something that will be linked
 * needs it. A weak reference selects nothing (the symbol is linked only
 * if a strong one elsewhere selects it), and a reference to a layout name
 * (DEF_LAYOUT) needs no section. */
static void walk(void)
{
    int s;
    int r;
    int sym;
    int d;

    while (nwork > 0) {
        nwork--;
        s = work[nwork];
        for (r = 0; r < sects[s].nref; r++) {
            sym = refs[sects[s].ref0 + r];
            d = sym_def[sym];
            if (nweak > 0 && is_weak(sects[s].unit, sym))
                continue;
            if (d >= 0)
                select_sect(d);
            else if (d == DEF_NONE)
                want(sym);
        }
    }
}

/* Moves want i's search to the next directory entry with its name's hash,
 * in the indexed libraries read so far, in command order, from where its
 * search stands. 0 if there is none. */
static int find(int i)
{
    unsigned h;
    int f;
    int e;
    int lo;
    int hi;
    int mid;

    h = unit_hash(sym_str(want_sym[i]));
    f = want_file[i];
    e = want_ent[i];
    while (f < nfiles) {
        if (file_idx[f]) {
            if (e < 0) {
                /* the first entry with hash h, by binary search */
                lo = dir_lo[f];
                hi = dir_hi[f];
                while (lo < hi) {
                    mid = lo + (hi - lo) / 2;
                    if (dir[mid].hash < h)
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                e = lo;
            }
            if (e < dir_hi[f] && dir[e].hash == h) {
                want_file[i] = f;
                want_ent[i] = e;
                return 1;
            }
        }
        f++;
        e = -1;
    }
    want_file[i] = f;
    want_ent[i] = -1;
    return 0;
}

/* in_rd on file f's index, opened again only when the file changes. */
static int open_for_load(int f)
{
    if (load_file == f)
        return 1;
    if (load_file >= 0)
        rd_close(&in_rd);
    load_file = -1;
    index_name(files[f]);
    if (!rd_open(&in_rd, idx_path)) {
        fprintf(stderr, "ld: cannot reopen %s\n", idx_path);
        errors++;
        return 0;
    }
    load_file = f;
    return 1;
}

static void close_load(void)
{
    if (load_file >= 0)
        rd_close(&in_rd);
    load_file = -1;
}

/* Does the record at rec in file f's index define name, as its section's
 * own name or one of its labels? A hash can be shared, so a directory
 * entry is only a candidate until this says so. Reads the record alone:
 * it ends where the next record (;;in) or the directory (;;dir) begins. */
static int record_defines(int f, int rec, char *name)
{
    int n;
    int first;

    if (!open_for_load(f))
        return 0;
    rd_seek(&in_rd, rec);
    first = 1;
    while (rd_gets(&in_rd, line, LINE_BUF) > 0) {
        n = split();
        if (n == 0 || strcmp(tok[0], ";;dir") == 0)
            break;
        if (strcmp(tok[0], ";;in") == 0) {
            if (!first)
                break;
            first = 0;
        } else if (n >= 3 && strcmp(tok[0], ";;sect") == 0) {
            if (strcmp(tok[2], name) == 0)
                return 1;
        } else if (n == 2 && strcmp(tok[0], ";;label") == 0) {
            if (strcmp(tok[1], name) == 0)
                return 1;
        }
    }
    return 0;
}

/* Loads the record at rec in file f's index into the tables, as pass 1
 * would have read the section from the .s: the section (defining its
 * names, so a name a unit already defines is a duplicate here too), its
 * offsets in the .s, return kind, labels, references and implicit calls.
 * Returns the section, or -1 on an error. */
static int load_record(int f, int rec)
{
    int s;
    int u;
    int n;

    if (!open_for_load(f))
        return -1;
    rd_seek(&in_rd, rec);
    cur_file = files[f];
    cur_line = 0;
    s = -1;
    u = -1;
    while (rd_gets(&in_rd, line, LINE_BUF) > 0) {
        n = split();
        if (n == 0 || strcmp(tok[0], ";;dir") == 0)
            break;
        if (strcmp(tok[0], ";;in") == 0) {
            if (u >= 0)
                break;                  /* the next record */
            u = unit0[f] + atoi(tok[1]);
        } else if (strcmp(tok[0], ";;sect") == 0 && s < 0 && n >= 4) {
            s = new_sect(n, u, 0, 0);
            if (s < 0)
                return -1;
            nloaded++;
        } else if (s < 0) {
            break;
        } else if (strcmp(tok[0], ";;at") == 0 && n == 5) {
            sects[s].mark = atoi(tok[1]);
            sects[s].start = atoi(tok[2]);
            sects[s].end = atoi(tok[3]);
            sects[s].clean = tok[4][0] == '1';
        } else if (strcmp(tok[0], ";;ret") == 0 && n == 2) {
            sects[s].ret = tok[1][0];
        } else if (strcmp(tok[0], ";;label") == 0 && n == 2) {
            define(intern(tok[1]), s);
        } else if (strcmp(tok[0], ";;ref") == 0 && n == 2) {
            /* the record is read whole before any other, so its run stays contiguous */
            if (nrefs >= MAX_REFS)
                fail_limit("references");
            refs[nrefs] = intern(tok[1]);
            nrefs++;
            sects[s].nref++;
        } else if (strcmp(tok[0], ";;implicit") == 0 && n == 2) {
            if (nimplicit >= MAX_IMPLICIT)
                fail_limit("implicit calls");
            imp_sect[nimplicit] = s;
            imp_sym[nimplicit] = intern(tok[1]);
            nimplicit++;
        } else {
            error_here("malformed record in the index at ", tok[0]);
            return -1;
        }
    }
    if (s < 0)
        error_here("no section where the index's directory says", "");
    return s;
}

/* The next --if-needed library not yet read, read now; 0 if none is left. */
static int read_next_lazy(void)
{
    int f;

    for (f = 0; f < nfiles; f++) {
        if (lazy[f] && !file_read[f]) {
            close_load();
            read_input(f);
            nlazy_read++;
            return 1;
        }
    }
    return 0;
}

/* Wants in the order their candidate records lie: by file, then by
 * offset, so each round reads each index from front to back. */
static int probe_cmp(const void *a, const void *b)
{
    int i;
    int j;

    i = *(const int *)a;
    j = *(const int *)b;
    if (want_file[i] != want_file[j])
        return want_file[i] < want_file[j] ? -1 : 1;
    if (dir[want_ent[i]].rec != dir[want_ent[j]].rec)
        return dir[want_ent[i]].rec < dir[want_ent[j]].rec ? -1 : 1;
    return 0;
}

/* Pass 2: reachability from the roots (the --entry symbols, with __start
 * added by main), loading library sections as they are needed. In rounds:
 * the walk selects what the sections read so far reach and wants what
 * nothing defines; each want then finds a candidate in the directories,
 * and the candidates are checked and loaded in file order, which selects
 * them for the next round's walk. A want with no candidate left makes
 * ld read the next --if-needed library, if any (that is the only time one
 * is read); otherwise it stays undefined, for report_undefined. A want
 * defined since (by a library read whole, or by a label of a section
 * loaded for another name) is selected through its definition. */
static void resolve(void)
{
    int i;
    int k;
    int n;
    int sym;
    int s;

    nwork = 0;
    for (i = 0; i < nentries; i++) {
        sym = intern(entries[i]);
        if (sym_def[sym] >= 0)
            select_sect(sym_def[sym]);
        else if (sym_def[sym] == DEF_NONE)
            want(sym);
    }
    while (!errors) {
        walk();
        n = 0;
        k = 0;
        for (i = 0; i < nwant; i++) {
            sym = want_sym[i];
            if (sym_def[sym] >= 0) {
                select_sect(sym_def[sym]);
                continue;
            }
            want_sym[k] = sym;
            want_file[k] = want_file[i];
            want_ent[k] = want_ent[i];
            if (find(k)) {
                probe[n] = k;
                n++;
            }
            k++;
        }
        nwant = k;
        if (nwork > 0)
            continue;                   /* wants defined since: walk from them first */
        if (n == 0) {
            if (nwant == 0 || !read_next_lazy())
                break;
            for (i = 0; i < nwant; i++) {
                want_file[i] = 0;       /* look again, in the new library too */
                want_ent[i] = -1;
            }
            continue;
        }
        qsort(probe, n, sizeof probe[0], probe_cmp);
        for (k = 0; k < n; k++) {
            i = probe[k];
            sym = want_sym[i];
            if (sym_def[sym] != DEF_NONE)
                continue;               /* a label of a section loaded this round */
            if (!record_defines(want_file[i], dir[want_ent[i]].rec, sym_str(sym))) {
                want_ent[i]++;          /* another name with the same hash: try the next entry */
                continue;
            }
            s = load_record(want_file[i], dir[want_ent[i]].rec);
            if (s < 0)
                break;
            if (sym_def[sym] < 0) {
                error_here("the index's record does not define ", sym_str(sym));
                break;
            }
            select_sect(s);
        }
    }
    close_load();
}

/* Every strong reference from a selected section to a name nothing
 * defines, and every --entry root nothing defines, as an error. */
static void report_undefined(void)
{
    int i;
    int s;
    int r;
    int sym;

    for (i = 0; i < nentries; i++) {
        if (sym_def[intern(entries[i])] == DEF_NONE) {
            fprintf(stderr, "ld: error: undefined symbol %s (a root)\n", entries[i]);
            errors++;
        }
    }
    for (s = 0; s < nsects; s++) {
        if (!sects[s].selected)
            continue;
        for (r = 0; r < sects[s].nref; r++) {
            sym = refs[sects[s].ref0 + r];
            if (sym_def[sym] == DEF_NONE && !(nweak > 0 && is_weak(sects[s].unit, sym))) {
                fprintf(stderr, "ld: error: undefined symbol %s (referenced from unit %s, section %s)\n",
                        sym_str(sym), unit_str(sects[s].unit), sym_str(sects[s].sym));
                errors++;
            }
        }
    }
}

/* Sections in the order the inputs were read in, and by their offset in
 * the .s within an input: the order they would have had if every library
 * had been read whole, which is the order passes 3 and 4 follow, so the
 * output does not depend on which sections came from an index or when. */
static int sect_cmp(const void *a, const void *b)
{
    const struct sect *x;
    const struct sect *y;
    int fx;
    int fy;

    x = (const struct sect *)a;
    y = (const struct sect *)b;
    fx = file_rank[units[x->unit].file];
    fy = file_rank[units[y->unit].file];
    if (fx != fy)
        return fx < fy ? -1 : 1;
    if (x->mark != y->mark)
        return x->mark < y->mark ? -1 : 1;
    return 0;
}

/* Sorts sects[] by sect_cmp and renumbers what refers to a section by its
 * number: the symbols' definitions and the implicit calls. work[] is free
 * after pass 2, so it holds the map from old numbers to new. */
static void sort_sects(void)
{
    int s;
    int i;

    for (s = 0; s < nsects; s++)
        sects[s].old = s;
    qsort(sects, nsects, sizeof sects[0], sect_cmp);
    for (s = 0; s < nsects; s++)
        work[sects[s].old] = s;
    for (i = 0; i < nsyms; i++)
        if (sym_def[i] >= 0)
            sym_def[i] = work[sym_def[i]];
    for (i = 0; i < nimplicit; i++)
        imp_sect[i] = work[imp_sect[i]];
}

/* An implicit call assumes an int result (or none); a callee that returns
 * a long, float, double, structure or union was called wrongly. Only
 * calls from selected sections, to defined callees with a ;;ret, are
 * checked. The kinds are where the value comes back (object_format.md
 * section 8): i in HL, l in E:UHL (long and float), r through a hidden
 * result pointer (double, structure, union), v nothing. */
static void check_implicit(void)
{
    int i;
    int s;
    int d;
    char *what;

    for (i = 0; i < nimplicit; i++) {
        s = imp_sect[i];
        d = sym_def[imp_sym[i]];
        if (!sects[s].selected || d < 0)
            continue;
        what = sects[d].ret == 'l' ? "long (or float)" : sects[d].ret == 'r' ? "double, a structure or a union" : NULL;
        if (what != NULL) {
            fprintf(stderr, "ld: error: implicit call to %s, which returns %s (unit %s, section %s)\n",
                    sym_str(imp_sym[i]), what, unit_str(sects[s].unit), sym_str(sects[s].sym));
            errors++;
        }
    }
}

/* ---- pass 3: order ------------------------------------------------------ */

static int start_sect;          /* __start's section: always first, so left out here */

/* Depth-first search from root over references to selected code
 * sections, appending each section to order[] when all it calls are
 * done (post-order): so a callee is placed before its callers, which
 * makes calls backward references for the assembler. A reference to a
 * section still ST_OPEN (on the stack) closes a cycle (mutual recursion);
 * that call has to be forward, and is counted for -v. Data and bss are
 * not ordered: references to them are never followed here. */
static void order_from(int root)
{
    int sp;
    int s;
    int t;

    sects[root].state = ST_OPEN;
    stack_s[0] = root;
    stack_i[0] = 0;
    sp = 1;
    while (sp > 0) {
        /* the top section: follow its next reference, or finish it */
        s = stack_s[sp - 1];
        if (stack_i[sp - 1] < sects[s].nref) {
            t = sym_def[refs[sects[s].ref0 + stack_i[sp - 1]]];
            stack_i[sp - 1]++;
            if (t >= 0 && t != start_sect && sects[t].kind == K_CODE && sects[t].selected) {
                if (sects[t].state == ST_NEW) {
                    sects[t].state = ST_OPEN;
                    stack_s[sp] = t;
                    stack_i[sp] = 0;
                    sp++;
                } else if (sects[t].state == ST_OPEN) {
                    fwd_calls++;
                }
            }
        } else {
            sects[s].state = ST_DONE;
            order[norder] = s;
            norder++;
            sp--;
        }
    }
}

/* From __start first (its own entry in order[] is skipped when copying,
 * since it is written before everything), then from any selected code
 * section not reached that way, such as one reached only from data (a
 * table of function pointers) or from an --entry root. */
static void order_all(void)
{
    int s;

    norder = 0;
    order_from(start_sect);
    for (s = 0; s < nsects; s++)
        if (sects[s].selected && sects[s].kind == K_CODE && sects[s].state == ST_NEW)
            order_from(s);
}

/* ---- pass 4: layout and copy -------------------------------------------- */

static int copy_file;           /* the file open in in_rd, or -1 */

/* Is line (its line ending removed) blank or a full-line comment? Such
 * lines cost the on-device assembler time and change nothing. */
static int skip_line(void)
{
    char *p;

    p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    return *p == 0 || *p == ';';
}

/* Section s's body to out, read again from its .s (never from an index:
 * an index has no bodies). Sections come in output order, which jumps
 * between files, so the input is reopened only when the file changes;
 * one file is open at a time. Before the body, the section's ;;sect line
 * is read at its recorded offset and must name the section: the check
 * that an index really describes this .s. A clean section is copied in
 * blocks straight from the read buffer; any other line by line, dropping
 * blank and comment lines. */
static void copy_sect(FILE *out, int s)
{
    int file;
    int pos;
    int len;
    int end;
    unsigned char *p;

    file = units[sects[s].unit].file;
    if (file != copy_file) {
        if (copy_file >= 0)
            rd_close(&in_rd);
        copy_file = -1;
        if (!rd_open(&in_rd, files[file])) {
            fprintf(stderr, "ld: cannot reopen %s\n", files[file]);
            errors++;
            return;
        }
        copy_file = file;
    }
    /* (reads no more than the section needs if the buffer must be filled) */
    rd_seek_len(&in_rd, sects[s].mark, sects[s].end - sects[s].mark);
    rd_gets(&in_rd, line, LINE_BUF);
    if (split() < 3 || strcmp(tok[0], ";;sect") != 0 || strcmp(tok[2], sym_str(sects[s].sym)) != 0) {
        index_name(files[file]);
        if (errors == 0)                /* once: every later section would say the same */
            fprintf(stderr, "ld: error: %s does not match %s (section %s); make it again with agonc --index\n",
                    idx_path, files[file], sym_str(sects[s].sym));
        errors++;
        return;
    }
    rd_seek(&in_rd, sects[s].start);
    pos = sects[s].start;
    end = sects[s].end;
    if (sects[s].clean) {
        while (pos < end && (len = rd_block(&in_rd, &p, end - pos)) > 0) {
            fwrite(p, 1, len, out);
            pos = pos + len;
        }
        return;
    }
    while (pos < end) {
        len = rd_gets(&in_rd, line, LINE_BUF);
        if (len == 0)
            break;
        pos = pos + len;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            len--;
            line[len] = 0;
        }
        if (!skip_line()) {
            out_str(out, line);
            fputc('\n', out);
        }
    }
}

/* Pass 4: the .asm (object_format.md section 4). First the prologue: the
 * MOS executable header (a jump over it to __start at offset 0x45, and
 * "MOS" with its version and ADL flag at 0x40), then the layout equates
 * in dependency order, since an equ's own expression may not name a
 * symbol not yet defined: __bss_size, __bss_base, __stack_top, one equ
 * per selected bss object, and 0 for each unlinked weak symbol. Then the
 * bodies: __start, the code in pass 3's order, the data in input order,
 * and __image_end, the first free byte, where the heap begins. A bss
 * object takes no bytes in the binary; it is only an address. */
static int write_output(void)
{
    FILE *out;
    int s;
    int i;
    int j;
    int bss;

    out = fopen(out_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "ld: cannot create %s\n", out_path);
        return 0;
    }
    bss = 0;
    for (s = 0; s < nsects; s++)
        if (sects[s].selected && sects[s].kind == K_BSS)
            bss = bss + sects[s].size;
    /* A program owns user RAM, 0x040000-0x0AFFFF; a moslet owns the 32 KB
     * at 0x0B0000. Either way bss sits at the top and the stack below it. */
    fprintf(out, "\tassume adl=1\n\torg 0x%06X\n\tjp $+0x45\n\talign 64\n\tdb \"MOS\",0,1\n",
            moslet ? 0x0B0000 : 0x040000);
    fprintf(out, "__bss_size:\tequ %d\n", bss);
    fprintf(out, "__bss_base:\tequ 0x%06X-__bss_size\n__stack_top:\tequ __bss_base\n",
            moslet ? 0x0B8000 : 0x0B0000);
    /* the bss objects packed upward from __bss_base, in input order, with
     * no alignment (the eZ80 needs none) */
    bss = 0;
    for (s = 0; s < nsects; s++) {
        if (sects[s].selected && sects[s].kind == K_BSS) {
            fprintf(out, "%s:\tequ __bss_base+%d\n", sym_str(sects[s].sym), bss);
            bss = bss + sects[s].size;
        }
    }
    /* a weakly referenced symbol that no selected section defines is 0
     * (once, though several units may name it), so code can test for it */
    for (i = 0; i < nweak; i++) {
        s = sym_def[weak_sym[i]];
        if ((s >= 0 && sects[s].selected) || s == DEF_LAYOUT)
            continue;
        for (j = 0; j < i; j++)
            if (weak_sym[j] == weak_sym[i])
                break;
        if (j == i)
            fprintf(out, "%s:\tequ 0\n", sym_str(weak_sym[i]));
    }
    copy_file = -1;
    copy_sect(out, start_sect);
    for (i = 0; i < norder; i++)
        if (order[i] != start_sect)
            copy_sect(out, order[i]);
    for (s = 0; s < nsects; s++)
        if (sects[s].selected && sects[s].kind == K_DATA)
            copy_sect(out, s);
    out_str(out, "__image_end:\n");
    if (copy_file >= 0)
        rd_close(&in_rd);
    fclose(out);
    return errors == 0;
}

/* -v: what was selected, how full each table got (to show which limit is
 * near), and how much the indexes and --if-needed saved. */
static void report(void)
{
    int s;
    int n[3];
    int bss;
    int lazies;

    n[0] = 0;
    n[1] = 0;
    n[2] = 0;
    bss = 0;
    for (s = 0; s < nsects; s++) {
        if (sects[s].selected) {
            n[sects[s].kind]++;
            if (sects[s].kind == K_BSS)
                bss = bss + sects[s].size;
        }
    }
    printf("ld: %d of %d sections selected (%d code, %d data, %d bss); bss %d bytes; %d forward calls\n",
           n[0] + n[1] + n[2], nsects, n[0], n[1], n[2], bss, fwd_calls);
    printf("ld: tables: sections %d/%d, symbols %d/%d, references %d/%d, name bytes %d/%d, units %d/%d,"
           " implicit calls %d/%d, directory %d/%d, wants %d/%d\n", nsects, MAX_SECTS, nsyms, MAX_SYMS,
           nrefs, MAX_REFS, names_used, NAME_TEXT, nunits, MAX_UNITS, nimplicit, MAX_IMPLICIT,
           ndir, MAX_DIR, peak_want, MAX_WANT);
    printf("ld: %d of %d inputs read through an index; %d sections loaded from indexes\n",
           nindexed, nfiles, nloaded);
    lazies = 0;
    for (s = 0; s < nfiles; s++)
        lazies = lazies + lazy[s];
    if (lazies > 0)
        printf("ld: %d of %d --if-needed libraries read\n", nlazy_read, lazies);
}

/* ---- main ---------------------------------------------------------------- */

static char *args[MAX_ARGS];

/* The options and input files, after @file expansion (common/args.c).
 * --if-needed applies to the next input file only. 0 after an error. */
static int parse_args(int argc, char **argv)
{
    int n;
    int i;

    n = expand_args("ld", argc, argv, args);
    if (n < 0)
        return 0;
    out_path = "a.asm";
    for (i = 0; i < n; i++) {
        if (strcmp(args[i], "-o") == 0 && i + 1 < n) {
            i++;
            out_path = args[i];
        } else if (strncmp(args[i], "--entry=", 8) == 0) {
            if (nentries >= MAX_ENTRIES)
                fail_limit("--entry options");
            entries[nentries] = args[i] + 8;
            nentries++;
        } else if (strcmp(args[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(args[i], "--moslet") == 0) {
            moslet = 1;
        } else if (strcmp(args[i], "--index") == 0) {
            make_index = 1;
        } else if (strcmp(args[i], "--if-needed") == 0) {
            next_lazy = 1;
        } else if (args[i][0] == '-') {
            fprintf(stderr, "ld: unknown option %s\n", args[i]);
            return 0;
        } else {
            if (nfiles >= MAX_FILES)
                fail_limit("input files");
            files[nfiles] = args[i];
            lazy[nfiles] = next_lazy;
            next_lazy = 0;
            nfiles++;
        }
    }
    if (nfiles == 0) {
        fprintf(stderr, "ld: no input files\n");
        return 0;
    }
    return 1;
}

/* Empty tables, with the layout's own symbols reserved: a section that
 * tried to define one would clash with the prologue's equ. */
static void start_tables(void)
{
    int i;

    for (i = 0; i < HASH_SIZE; i++)
        hash_tab[i] = -1;
    nsyms = 0;
    names_used = 0;
    nsects = 0;
    nrefs = 0;
    nweak = 0;
    nimplicit = 0;
    nunits = 0;
    nlabels = 0;
    sym_def[intern("__bss_size")] = DEF_LAYOUT;
    sym_def[intern("__bss_base")] = DEF_LAYOUT;
    sym_def[intern("__stack_top")] = DEF_LAYOUT;
    sym_def[intern("__image_end")] = DEF_LAYOUT;
}

/* --index: pass 1 over each library alone and its .idx written. A link:
 * pass 1 over the inputs but the --if-needed ones, pass 2 (which reads an
 * --if-needed library only when still wanted), the sections sorted into
 * input order, then passes 3 and 4. Any error gives status 200, and an
 * output that pass 4 had begun is removed. */
int main(int argc, char **argv)
{
    int i;
    int start;

    start_tables();
    if (!parse_args(argc, argv))
        return 200;
    if (make_index) {
        /* each library on its own: they need not link together */
        for (i = 0; i < nfiles; i++) {
            start_tables();
            index_file(i);
            if (errors || !write_index(i))
                return 200;
        }
        return 0;
    }
    load_file = -1;
    for (i = 0; i < nfiles; i++)
        if (!lazy[i])
            read_input(i);
    if (errors)
        return 200;
    if (nentries >= MAX_ENTRIES)
        fail_limit("--entry options");
    entries[nentries] = "__start";
    nentries++;
    resolve();
    if (errors)
        return 200;
    start = intern("__start");
    if (sym_def[start] < 0 || sects[sym_def[start]].kind != K_CODE) {
        fprintf(stderr, "ld: error: no __start code section (is crt0.s missing?)\n");
        return 200;
    }
    report_undefined();
    if (errors)
        return 200;
    sort_sects();
    start_sect = sym_def[start];
    check_implicit();
    if (errors)
        return 200;
    order_all();
    if (!write_output()) {
        remove(out_path);
        return 200;
    }
    if (verbose)
        report();
    return 0;
}
