/* agonc.c - the compiler driver (docs/driver.md).
 *
 *     agonc [options] file...
 *
 * By extension, each input goes through cpp (.c), cc1 and cc2 (.c, .i) to
 * a .s file; then ld links the .s files with the runtime and libraries
 * into one .asm, which ez80asm assembles (an .asm input goes straight to
 * the assembler). -E, -c and -S stop earlier. Intermediate files go in
 * /tmp/agonc and are removed afterwards unless -save-temps. The status is 0, or
 * 200 after the first failure (130 after Ctrl-C); each pass prints its own
 * diagnostics.
 *
 * The same source is the host program and the Agon moslet (sys.c holds the
 * difference), so it uses no heap and no stdio: fixed tables, limits below.
 *
 * The flow, from main: each argument is copied into arg_text (an @file is
 * replaced by its words), parse() sorts them into inputs, flags and the
 * ordered option list (opt_kind/opt_val: -I -D -U -L -l -Wl, kept in
 * command-line order because search order matters), and build() checks
 * the inputs, runs compile() on each .c and .i and then link_all(). Last,
 * main removes every temporary file unless -save-temps.
 *
 * Running a pass: cmd_start() and cmd_add() build the pass's argument
 * string in cmd, one word at a time; run() passes it to sys_run(). A pass
 * on the Agon is a program whose start-up code splits that string into
 * argv, and the stage-1 passes, built with AgDev, keep only 14 words (see
 * DIRECT_ARGS): a longer string is written to /tmp/agonc/<pass>.rsp and
 * the pass gets "@/tmp/agonc/<pass>.rsp" instead, which every pass expands
 * (common/args.c). driver.md section 5.
 *
 * Failure: a pass reports its own errors and returns non-zero; run()
 * sets `failed`, and every later step returns at once when it is set.
 * ez80asm on the Agon returns 0 even when it fails (it removes its output
 * instead), so link_all() removes the binary before assembling and treats
 * a missing binary afterwards as the failure.
 *
 * Temporary files are named in /tmp/agonc from the input's base name (<base>.i,
 * <base>.ir, <base>.s, <out>.asm, <pass>.rsp) and recorded by temp() for
 * removal; that is why two inputs with one file name are refused.
 *
 * Map: messages, names (paths and extensions), the command line, running
 * a pass, the pipeline (compile, libraries, the link, the image check,
 * --index), help, main.
 */

#include <string.h>
#include "sys.h"

#define VERSION "agonc 1.1.0-beta.2 (Agon C, language C99; C89 with -ansi)\n"

#define MAX_ARGS 96             /* after @file expansion */
#define ARG_TEXT 2048           /* the arguments' text, all together */
#define MAX_INPUTS 32
#define MAX_OPTS 48             /* -I -D -U -L -l -Wl, in order */
#define PATH_SIZE 128
#define CMD_SIZE 1200           /* one pass's argument string */
#define DIRECT_LEN 200          /* beyond these a pass gets a response file */
/* AgDev's crt0 stops at argc 15, argv[0] included, and silently drops the
 * rest of the line */
#define DIRECT_ARGS 14
#define MAX_TEMPS 100
#define TEMP_TEXT 3000          /* the temporary files' names, all together */

/* an input's kind, from its extension (kind_of) */
#define K_C 1
#define K_I 2
#define K_S 3
#define K_ASM 4

static int failed;              /* an error so far: every later step does nothing */
static char *writing;           /* the final output the next program writes, if any */

/* the arguments after @file expansion, their text in arg_text */
static char *args[MAX_ARGS];
static int nargs;
static char arg_text[ARG_TEXT];
static int arg_used;

static char *inputs[MAX_INPUTS];
static int kinds[MAX_INPUTS];
static char *s_path[MAX_INPUTS];        /* each input's .s for the link */
static int ninputs;
/* -I -D -U -L -l and -Wl, in command-line order: the option letter
 * ('W' for -Wl) and its value */
static int opt_kind[MAX_OPTS];
static char *opt_val[MAX_OPTS];
static int nopts;

static char *out_path;
static int preprocess_only;
static int compile_only;
static int no_warnings;
static int werror;
static int strict;              /* -ansi, -std=c89, -std=c90; -std=c99, -std=gnu99 clear it */
static int optimize = 1;        /* -O (the default), -O1 .. -O3, -Os; -O0 turns it off (the last one counts) */
static int nostdlib;
static int save_temps;
static int make_index;          /* --index: index .s libraries */
static int verbose;
static int timing;              /* -time, or -v: each pass's time and the total */
static int version;
static int help;

static char root[PATH_SIZE];    /* sys_root(): "" on the Agon */
static char date_now[12];       /* for cpp's __DATE__ and __TIME__ (sys_date) */
static char time_now[12];
static char tmp_dir[PATH_SIZE]; /* root + "/tmp/agonc/" */
/* the argument string for the next pass: its length and word count, which
 * run() compares with DIRECT_LEN and DIRECT_ARGS */
static char cmd[CMD_SIZE + 1];
static int cmd_len;
static int cmd_n;
/* every temporary file made, for removal at the end; names in temp_text */
static char *temps[MAX_TEMPS];
static int ntemps;
static char temp_text[TEMP_TEXT];
static int temp_used;

/* ---- messages -------------------------------------------------------------- */

/* "agonc: error: " a b; also sets failed, which stops the build. */
static void error(char *a, char *b)
{
    sys_err("agonc: error: ");
    sys_err(a);
    sys_err(b);
    sys_err("\n");
    failed = 1;
}

static void warning(char *a, char *b)
{
    sys_err("agonc: warning: ");
    sys_err(a);
    sys_err(b);
    sys_err("\n");
}

/* n (not negative) in decimal, built backwards from the end of buf, which
 * must hold 12 characters; returns where the digits start. */
static char *decimal(char *buf, int n)
{
    int i;

    i = 11;
    buf[i] = 0;
    do {
        i--;
        buf[i] = '0' + n % 10;
        n = n / 10;
    } while (n > 0);
    return buf + i;
}

/* ---- names ------------------------------------------------------------------ */

/* Where the file name in a path starts. */
static char *file_part(char *path)
{
    char *p;

    p = path;
    while (*path) {
        if (*path == '/' || *path == '\\' || *path == ':')
            p = path + 1;
        path++;
    }
    return p;
}

/* The extension's dot in a path, or its end. */
static char *ext_part(char *path)
{
    char *f;
    char *dot;

    f = file_part(path);
    dot = NULL;
    while (*f) {
        if (*f == '.')
            dot = f;
        f++;
    }
    return dot == NULL ? f : dot;
}

/* K_C, K_I, K_S or K_ASM from the extension (lower or upper case, since
 * FAT keeps either), or 0 for anything else. */
static int kind_of(char *path)
{
    char *e;

    e = ext_part(path);
    if (strcmp(e, ".c") == 0 || strcmp(e, ".C") == 0)
        return K_C;
    if (strcmp(e, ".i") == 0 || strcmp(e, ".I") == 0)
        return K_I;
    if (strcmp(e, ".s") == 0 || strcmp(e, ".S") == 0)
        return K_S;
    if (strcmp(e, ".asm") == 0 || strcmp(e, ".ASM") == 0)
        return K_ASM;
    return 0;
}

/* dst = a + b + c, the part of b before its extension if cut is set;
 * returns dst, or NULL (after an error) if it does not fit. */
static char *join(char *dst, char *a, char *b, int cut, char *c)
{
    int nb;

    nb = cut ? ext_part(b) - b : strlen(b);
    if (strlen(a) + nb + strlen(c) >= PATH_SIZE) {
        error("path too long: ", *b ? b : a);   /* b is "" when the root itself is too long */
        return NULL;
    }
    strcpy(dst, a);
    memcpy(dst + strlen(a), b, nb);
    strcpy(dst + strlen(a) + nb, c);
    return dst;
}

/* Like join, but the result is a temporary file, kept in the driver's
 * list for removal at the end. */
static char *temp(char *a, char *b, int cut, char *c)
{
    char buf[PATH_SIZE];
    char *t;
    int i;

    if (join(buf, a, b, cut, c) == NULL)
        return NULL;
    for (i = 0; i < ntemps; i++)        /* a response file is made again for each pass that needs one */
        if (strcmp(temps[i], buf) == 0)
            return temps[i];
    if (ntemps >= MAX_TEMPS || temp_used + strlen(buf) + 1 > TEMP_TEXT) {
        error("too many temporary files", "");
        return NULL;
    }
    t = temp_text + temp_used;
    strcpy(t, buf);
    temp_used = temp_used + strlen(buf) + 1;
    temps[ntemps] = t;
    ntemps++;
    return t;
}

/* Do two file names agree up to their first '.', letters in either case?
 * Two such inputs would share /tmp/agonc/<base>.i, .ir and .s (FAT ignores
 * case), and two of exactly one name would give cc1 one unit name, so
 * one unit id. */
static int same_name(char *a, char *b)
{
    int ca;
    int cb;

    for (;;) {
        ca = *a;
        cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = ca + 32;
        if (cb >= 'A' && cb <= 'Z')
            cb = cb + 32;
        if (ca != cb)
            return 0;
        if (ca == 0 || ca == '.')
            return 1;
        a++;
        b++;
    }
}

/* Do two paths name the same file? Letters in either case, / or \, and a
 * leading ./ are all the same to FAT. */
static int same_path(char *a, char *b)
{
    int ca;
    int cb;

    if (a[0] == '.' && (a[1] == '/' || a[1] == '\\'))
        a = a + 2;
    if (b[0] == '.' && (b[1] == '/' || b[1] == '\\'))
        b = b + 2;
    for (;;) {
        ca = *a;
        cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = ca + 32;
        if (cb >= 'A' && cb <= 'Z')
            cb = cb + 32;
        if (ca == '\\')
            ca = '/';
        if (cb == '\\')
            cb = '/';
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++;
        b++;
    }
}

/* ---- the command line ------------------------------------------------------- */

/* The n characters at s as the next argument, copied into arg_text. */
static void add_arg(char *s, int n)
{
    if (nargs >= MAX_ARGS || arg_used + n + 1 > ARG_TEXT) {
        if (!failed)
            error("too many arguments", "");
        return;
    }
    args[nargs] = arg_text + arg_used;
    memcpy(args[nargs], s, n);
    args[nargs][n] = 0;
    arg_used = arg_used + n + 1;
    nargs++;
}

/* Every argument is copied, so none points into MOS's command buffer; an
 * @file's whitespace-separated words take its place. The file is read
 * into cmd, free until the first pass runs; its words are added as they
 * are, so an @ inside a response file is not expanded again. */
static void expand(char *a)
{
    int n;
    int i;
    int j;

    if (a[0] != '@') {
        add_arg(a, strlen(a));
        return;
    }
    n = sys_read(a + 1, cmd, CMD_SIZE);
    if (n < 0) {
        error("cannot open response file ", a + 1);
        return;
    }
    if (n >= CMD_SIZE) {
        error("response file too long: ", a + 1);
        return;
    }
    i = 0;
    while (i < n) {
        if (cmd[i] == ' ' || cmd[i] == '\t' || cmd[i] == '\r' || cmd[i] == '\n') {
            i++;
        } else {
            j = i;
            while (j < n && cmd[j] != ' ' && cmd[j] != '\t' && cmd[j] != '\r' && cmd[j] != '\n')
                j++;
            add_arg(cmd + i, j - i);
            i = j;
        }
    }
}

static void add_opt(int kind, char *val)
{
    if (nopts >= MAX_OPTS) {
        error("too many -I, -D, -U, -L, -l and -Wl options", "");
        return;
    }
    opt_kind[nopts] = kind;
    opt_val[nopts] = val;
    nopts++;
}

static void add_input(char *path)
{
    if (ninputs >= MAX_INPUTS) {
        error("too many input files", "");
        return;
    }
    inputs[ninputs] = path;
    ninputs++;
}

/* The options gcc has that cannot mean anything here, and why. */
static int rejected(char *a)
{
    char *why;

    why = NULL;
    if (strcmp(a, "-shared") == 0 || strcmp(a, "-static") == 0)
        why = ": there are no shared libraries; every program is linked whole";
    else if (a[1] == 'g')
        why = ": there is no debugging information";
    else if (a[1] == 'x')
        why = ": the language comes from the extension (.c .i .s .asm)";
    else if (a[1] == 'M')
        why = ": dependency output is not supported";
    if (why == NULL)
        return 0;
    error(a, why);
    return 1;
}

/* Options accepted with nothing further to do here: -Wall (all warnings
 * are on by default) and the -O forms (-O, -O0 to -O3, -Os), which parse()
 * turns into the optimise flag before it asks. */
static int ignored(char *a)
{
    if (a[1] == 'O')
        return a[2] == 0 || (a[3] == 0 && ((a[2] >= '0' && a[2] <= '3') || a[2] == 's'));
    return strcmp(a, "-Wall") == 0;
}

/* gcc-style options from args[], in any order with the inputs. A lone
 * "-" and everything after "--" are inputs. -o, -I, -L, -D, -U and -l take
 * their value joined ("-Idir") or as the next argument ("-I dir"). An
 * option starting -O that ignored() rejects (such as -O4) falls through
 * to "unknown option". */
static void parse(void)
{
    int i;
    int rest;
    char *a;
    char *v;

    rest = 0;
    for (i = 0; i < nargs && !failed; i++) {
        a = args[i];
        if (rest || a[0] != '-' || a[1] == 0) {
            add_input(a);
        } else if (strcmp(a, "--") == 0) {
            rest = 1;
        } else if (strcmp(a, "-c") == 0 || strcmp(a, "-S") == 0) {
            compile_only = 1;
        } else if (strcmp(a, "-E") == 0) {
            preprocess_only = 1;
        } else if (strcmp(a, "-w") == 0) {
            no_warnings = 1;
        } else if (strcmp(a, "-Werror") == 0) {
            werror = 1;
        } else if (strcmp(a, "-ansi") == 0 || strcmp(a, "-std=c89") == 0 || strcmp(a, "-std=c90") == 0) {
            strict = 1;
        } else if (strcmp(a, "-std=c99") == 0 || strcmp(a, "-std=gnu99") == 0) {
            strict = 0;     /* the default mode; the last -std or -ansi wins, as in gcc */
        } else if (strcmp(a, "-nostdlib") == 0) {
            nostdlib = 1;
        } else if (strcmp(a, "--index") == 0) {
            make_index = 1;
        } else if (strcmp(a, "-save-temps") == 0) {
            save_temps = 1;
        } else if (strcmp(a, "-v") == 0) {
            verbose = 1;
            timing = 1;
        } else if (strcmp(a, "-time") == 0) {
            timing = 1;
        } else if (strcmp(a, "--version") == 0) {
            version = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            help = 1;
        } else if (strncmp(a, "-Wl,", 4) == 0 && a[4]) {
            add_opt('W', a + 4);
        } else if (a[1] == 'O' && ignored(a)) {
            optimize = a[2] != '0';
        } else if (ignored(a) || rejected(a)) {
            /* nothing more */
        } else if (a[1] == 'o' || a[1] == 'I' || a[1] == 'L' || a[1] == 'D' || a[1] == 'U' || a[1] == 'l') {
            if (a[2]) {
                v = a + 2;
            } else if (i + 1 < nargs) {
                i++;
                v = args[i];
            } else {
                error(a, " needs an argument");
                return;
            }
            if (a[1] == 'o')
                out_path = v;
            else
                add_opt(a[1], v);
        } else {
            error("unknown option ", a);
        }
    }
}

/* ---- running a pass ---------------------------------------------------------- */

/* An empty argument string for the next pass. */
static void cmd_start(void)
{
    cmd_len = 0;
    cmd_n = 0;
    cmd[0] = 0;
}

/* Appends one argument, a followed by b (so "-I" and a directory make
 * "-Idir", one word), space-separated from the one before. Words cannot
 * contain spaces: nothing quotes them, here or in the passes. */
static void cmd_add(char *a, char *b)
{
    int n;

    n = strlen(a) + strlen(b);
    if (cmd_len + n + 1 >= CMD_SIZE) {
        if (!failed)
            error("command too long for a pass", "");
        return;
    }
    if (cmd_len > 0) {
        cmd[cmd_len] = ' ';
        cmd_len++;
    }
    strcpy(cmd + cmd_len, a);
    strcat(cmd + cmd_len, b);
    cmd_len = cmd_len + n;
    cmd_n++;
}

/* -time and -v: "time: cc1 12.34 s", from a count of hundredths of a second.
 * The digits are put from the end of buf backwards: two decimals, the
 * point, then the whole seconds. */
static void show_time(char *name, unsigned cs)
{
    char buf[16];
    int i;

    i = sizeof(buf) - 1;
    buf[i] = 0;
    buf[--i] = (char)('0' + cs % 10);
    buf[--i] = (char)('0' + cs / 10 % 10);
    buf[--i] = '.';
    cs = cs / 100;
    do {
        buf[--i] = (char)('0' + cs % 10);
        cs = cs / 10;
    } while (cs > 0 && i > 0);
    sys_out("time: ");
    sys_out(name);
    sys_out(" ");
    sys_out(buf + i);
    sys_out(" s\n");
}

/* Runs the pass (NULL: the assembler) on cmd; 1 if it succeeded. A long
 * argument string goes through /tmp/agonc/<pass>.rsp (driver.md section 5).
 * The pass's status decides: 0 is success; anything else (200 from a
 * pass that reported errors, -1 if it could not be run at all) sets
 * failed. After Ctrl-C the output the program was writing (`writing`,
 * set by the caller) is removed, since it may be partial. The assembler
 * is never given a response file; its command line is three words. */
static int run(char *pass)
{
    char prog[PATH_SIZE];
    char *rsp;
    int r;
    unsigned start;

    if (failed)
        return 0;
    if (pass == NULL)
        strcpy(prog, sys_asm());
    else
        sys_pass(prog, pass);
    /* too long or too many words for the pass's start-up code: the whole
     * string goes into a response file, and the pass is given its name */
    if (pass != NULL && (cmd_len > DIRECT_LEN || cmd_n > DIRECT_ARGS)) {
        rsp = temp(tmp_dir, pass, 0, ".rsp");
        if (rsp == NULL)
            return 0;
        if (!sys_write(rsp, cmd)) {
            error("cannot write ", rsp);
            return 0;
        }
        strcpy(cmd, "@");
        strcat(cmd, rsp);
    }
    if (verbose) {
        sys_out(prog);
        sys_out(" ");
        sys_out(cmd);
        sys_out("\n");
    }
    /* (sys_run may change cmd: a pass's start-up code splits it in place) */
    start = sys_clock();
    r = sys_run(prog, cmd);
    if (timing)
        show_time(pass == NULL ? "ez80asm" : pass, sys_clock() - start);
    if (sys_interrupted()) {
        if (writing != NULL)
            sys_remove(writing);        /* whatever it had written is not wanted */
        sys_err("agonc: interrupted\n");
        failed = 1;
        return 0;
    }
    writing = NULL;
    if (r == -1)
        error("cannot run ", prog);
    /* failed was 0 on entry, so this records only this program's result */
    failed = r != 0;
    return r == 0;
}

/* The options cpp and cc1 share. */
static void add_warning_opts(void)
{
    if (strict)
        cmd_add("-ansi", "");
    if (no_warnings)
        cmd_add("-w", "");
    if (werror)
        cmd_add("-Werror", "");
}

/* ---- the pipeline ------------------------------------------------------------ */

/* cpp, cc1 and cc2 for input n, as far as the mode goes. Each pass's
 * output is the next one's input:
 *
 *     cpp <in.c> <out.i> [-Idir -Dname -Uname]... [-date D -time T] [-ansi -w -Werror]
 *     cc1 <in.i> <out.ir> -u <unit name> [-ansi -w -Werror]
 *     cc2 <in.ir> <out.s> [-O]
 *
 * The .i, .ir and (for a link) .s are temporary files in /tmp/agonc; with -E
 * and -o the .i is the output, and with -c or -S the .s is (-o's, or the
 * input's name with .s beside it). s_path[n] records the .s for
 * link_all(). The unit name is the input's file name with its extension;
 * cc1 hashes it into the unit id that makes the unit's internal names
 * unique in the link. */
static void compile(int n)
{
    char *in;
    char *name;
    char *ipath;
    char *irpath;
    char spath[PATH_SIZE];
    char buf[PATH_SIZE + 8];
    int i;

    in = inputs[n];
    name = file_part(in);
    ipath = in;
    /* cpp, for a .c (a .i input is already preprocessed) */
    if (kinds[n] == K_C) {
        if (preprocess_only && out_path != NULL)
            ipath = out_path;
        else
            ipath = temp(tmp_dir, name, 1, ".i");
        if (ipath == NULL)
            return;
        cmd_start();
        cmd_add(in, "");
        cmd_add(ipath, "");
        for (i = 0; i < nopts; i++)
            if (opt_kind[i] == 'I' || opt_kind[i] == 'D' || opt_kind[i] == 'U')
                cmd_add(opt_kind[i] == 'I' ? "-I" : opt_kind[i] == 'D' ? "-D" : "-U", opt_val[i]);
        if (date_now[0]) {
            cmd_add("-date", "");
            cmd_add(date_now, "");
            cmd_add("-time", "");
            cmd_add(time_now, "");
        }
        if (root[0]) {                  /* cpp's own /usrlib and /lib/agonc are the Agon's */
            cmd_add("-I", strcat(strcpy(buf, root), "/usrlib"));
            cmd_add("-I", strcat(strcpy(buf, root), "/lib/agonc"));
        }
        add_warning_opts();
        writing = preprocess_only ? out_path : NULL;
        if (!run("cpp"))
            return;
        if (preprocess_only) {
            if (out_path == NULL)
                sys_cat(ipath);         /* -E without -o: the .i to the console */
            return;
        }
    }
    /* cc1 */
    irpath = temp(tmp_dir, name, 1, ".ir");
    if (irpath == NULL)
        return;
    cmd_start();
    cmd_add(ipath, "");
    cmd_add(irpath, "");
    cmd_add("-u", "");
    cmd_add(name, "");
    add_warning_opts();
    if (!run("cc1"))
        return;
    /* cc2 */
    if (!compile_only)
        s_path[n] = temp(tmp_dir, name, 1, ".s");
    else if (out_path != NULL)
        s_path[n] = out_path;
    else
        s_path[n] = join(spath, "", in, 1, ".s");      /* beside the input */
    if (s_path[n] == NULL)
        return;
    cmd_start();
    cmd_add(irpath, "");
    cmd_add(s_path[n], "");
    if (optimize)
        cmd_add("-O", "");
    writing = compile_only ? s_path[n] : NULL;
    run("cc2");
}

/* The first place -lname's libname.s is found: -L directories, then
 * /usrlib, /lib/agonc/agon and /lib/agonc. */
static void add_library(char *name)
{
    char file[PATH_SIZE];
    char dir[PATH_SIZE];
    char path[PATH_SIZE];
    int i;
    char *d;
    char *sep;

    if (join(file, "lib", name, 0, ".s") == NULL)
        return;
    for (i = 0; i < nopts + 3; i++) {
        if (i < nopts) {
            if (opt_kind[i] != 'L')
                continue;
            d = opt_val[i];
            sep = d[0] && (d[strlen(d) - 1] == '/' || d[strlen(d) - 1] == '\\') ? "" : "/";
            d = join(dir, d, sep, 0, "");
        } else {
            d = join(dir, root, i == nopts ? "/usrlib/" : i == nopts + 1 ? "/lib/agonc/agon/" : "/lib/agonc/", 0, "");
        }
        if (d == NULL || join(path, d, file, 0, "") == NULL)
            return;
        if (sys_size(path) >= 0) {
            cmd_add(path, "");
            return;
        }
    }
    error("cannot find -l", name);
}

/* Each -Wl option's comma-separated words, as ld arguments. */
static void add_ld_opts(void)
{
    char word[PATH_SIZE];
    char *v;
    int i;
    int n;

    for (i = 0; i < nopts; i++) {
        if (opt_kind[i] != 'W')
            continue;
        v = opt_val[i];
        while (*v) {
            n = 0;
            while (v[n] && v[n] != ',')
                n++;
            if (n >= PATH_SIZE) {
                error("-Wl option too long", "");
                return;
            }
            memcpy(word, v, n);
            word[n] = 0;
            if (n > 0)
                cmd_add(word, "");
            v = v + n;
            if (*v == ',')
                v++;
        }
    }
}

/* Position of the text after the first key in s, or NULL. */
static char *after(char *s, char *key)
{
    int n;

    n = strlen(key);
    while (*s) {
        if (strncmp(s, key, n) == 0)
            return s + n;
        s++;
    }
    return NULL;
}

/* The digits at s in base 10 or 16, up to the first other character;
 * -1 if s is NULL (after() found nothing). */
static int number(char *s, int base)
{
    int n;
    int d;

    if (s == NULL)
        return -1;
    n = 0;
    for (;;) {
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else
            return n;
        n = n * base + d;
        s++;
    }
}

/* ld writes its layout at the top of the .asm (object_format.md section
 * 4): the origin, the bss size and the top of the area. The image must end
 * below the bss; otherwise the binary is removed. The lines read are
 *
 *     org 0x040000                     (the origin)
 *     __bss_size: equ <bytes>
 *     __bss_base: equ 0x0B0000-__bss_size   (the top, before the minus)
 *
 * with a tab before org and after each colon, all within the first 256
 * bytes. The bss starts at top - bss, so the image (org + its size) must
 * not pass that. */
static void check_image(char *asm_path, char *out)
{
    char head[257];
    char nb[12];
    int n;
    int org;
    int top;
    int bss;
    int size;

    n = sys_read(asm_path, head, 256);
    if (n <= 0)
        return;
    head[n] = 0;
    org = number(after(head, "\torg 0x"), 16);
    bss = number(after(head, "__bss_size:\tequ "), 10);
    top = number(after(head, "__bss_base:\tequ 0x"), 16);
    size = sys_size(out);
    if (org < 0 || bss < 0 || top < 0 || size < 0)
        return;                         /* not ld's layout: nothing to check */
    if (org + size > top - bss) {
        sys_remove(out);
        error(out, ": image too large: code and data reach the bss");
        sys_err("agonc: code and data ");
        sys_err(decimal(nb, size));
        sys_err(" bytes, bss ");
        sys_err(decimal(nb, bss));
        sys_err(" bytes\n");
    }
}

/* Does the program use floating point or long long? cc1 marks every
 * function with a float, double or long long value with a reference to
 * printf's floating or ll conversions (ir_format.md 3, R), which cc2
 * writes as a ;;ref. Such a program also needs /lib/libm.s, the part of
 * the library for those, kept apart so that other programs link without
 * reading it. An explicit -lm adds it anyway, through add_library, so the
 * answer is then 0 and libm.s is not named twice. sys_find reads each .s
 * once for both markers; an input .s the user wrote counts as well. */
static int uses_fp(void)
{
    int i;

    for (i = 0; i < nopts; i++)
        if (opt_kind[i] == 'l' && strcmp(opt_val[i], "m") == 0)
            return 0;
    for (i = 0; i < ninputs; i++)
        if (sys_find(s_path[i], ";;ref ___fp_print", ";;ref ___ll_print"))
            return 1;
    return 0;
}

/* ld, then ez80asm:
 *
 *     ld -o <out.asm> [-v] [-Wl words] crt0.s rt.s <units.s>... <-l libraries>
 *         libc.s [libm.s] --if-needed libagon.s
 *     ez80asm <out.asm> <out.bin> -m
 *
 * Which sections are linked does not depend on the order (ld keeps what
 * is reachable from __start), except that --if-needed marks the one file
 * after it as read only if something is still undefined.
 * An .asm input skips ld and goes straight to the assembler. Because the
 * Agon's ez80asm returns 0 even after errors, the binary is removed
 * first and its absence afterwards is the failure. */
static void link_all(void)
{
    char *asm_path;
    char *out;
    int i;

    out = out_path == NULL ? "a.bin" : out_path;
    asm_path = NULL;
    for (i = 0; i < ninputs; i++)
        if (kinds[i] == K_ASM)
            asm_path = inputs[i];
    if (asm_path != NULL && ninputs > 1) {
        error(asm_path, ": an .asm file is assembled alone");
        return;
    }
    if (asm_path == NULL) {
        asm_path = temp(tmp_dir, file_part(out), 1, ".asm");
        if (asm_path == NULL)
            return;
        cmd_start();
        cmd_add("-o", "");
        cmd_add(asm_path, "");
        if (verbose)
            cmd_add("-v", "");
        add_ld_opts();
        if (!nostdlib) {
            cmd_add(root, "/lib/agonc/crt0.s");
            cmd_add(root, "/lib/agonc/rt.s");
        }
        for (i = 0; i < ninputs; i++)
            cmd_add(s_path[i], "");
        for (i = 0; i < nopts; i++)
            if (opt_kind[i] == 'l')
                add_library(opt_val[i]);
        if (!nostdlib)
            cmd_add(root, "/lib/agonc/libc.s");
        if (!nostdlib && uses_fp())
            cmd_add(root, "/lib/agonc/libm.s");
        if (!nostdlib) {
            /* the MOS and VDU interface: read only if the program uses it */
            cmd_add("--if-needed", "");
            cmd_add(root, "/lib/agonc/libagon.s");
        }
        if (!run("ld"))
            return;
    }
    sys_remove(out);                    /* the Agon's ez80asm returns 0 even on failure */
    cmd_start();
    cmd_add(asm_path, "");
    cmd_add(out, "");
    cmd_add("-m", "");
    writing = out;
    if (!run(NULL))
        return;
    if (sys_size(out) < 0) {
        error("the assembler did not produce ", out);
        return;
    }
    check_image(asm_path, out);
}

/* -o must not name an input, nor a source file of a kind this mode does
 * not write: a slip such as "agonc -o prog.c prog.c" would otherwise
 * replace the source, since the output is removed before it is written. */
static void check_output(void)
{
    int i;
    int k;
    char *e;

    for (i = 0; i < ninputs; i++) {
        if (same_path(out_path, inputs[i])) {
            error(out_path, ": the output would overwrite an input");
            return;
        }
    }
    k = kind_of(out_path);
    e = ext_part(out_path);
    if (k == 0 && strcmp(e, ".h") != 0 && strcmp(e, ".H") != 0)
        return;                         /* not a source file's name */
    if (preprocess_only && k != K_I)
        error(out_path, ": -E writes preprocessed C, so -o should end in .i, not a source file's extension");
    else if (compile_only && !preprocess_only && k != K_S)
        error(out_path, ": -c and -S write assembly, so -o should end in .s, not another source file's extension");
    else if (!preprocess_only && !compile_only)
        error(out_path, ": the program would overwrite a source file (a program's name ends in .bin)");
}

/* --index: ld writes each library's index beside it (object_format.md
 * section 9), which later links read instead of the whole library. */
static void index_all(void)
{
    int i;

    for (i = 0; i < ninputs; i++)
        if (kinds[i] != K_S)
            error(inputs[i], ": --index takes .s libraries only");
    if (failed)
        return;
    cmd_start();
    cmd_add("--index", "");
    for (i = 0; i < ninputs; i++)
        cmd_add(inputs[i], "");
    run("ld");
}

/* Checks every input first (kind, existence, clashing names), so nothing
 * runs if one is wrong; then --index, or the output check, /tmp/agonc, each
 * input's compile and the link. n counts the inputs that are compiled. */
static void build(void)
{
    char dir[PATH_SIZE];
    int i;
    int j;
    int n;

    n = 0;
    for (i = 0; i < ninputs; i++) {
        kinds[i] = kind_of(inputs[i]);
        s_path[i] = inputs[i];
        if (kinds[i] == 0)
            error(inputs[i], ": unknown file type (use .c, .i, .s or .asm)");
        else if (sys_size(inputs[i]) < 0)
            error(inputs[i], ": no such file");
        if (kinds[i] == K_C || kinds[i] == K_I) {
            n++;
            for (j = 0; j < i; j++)
                if ((kinds[j] == K_C || kinds[j] == K_I) && same_name(file_part(inputs[i]), file_part(inputs[j])))
                    error(inputs[i], ": another input has the same name");
        }
    }
    if (make_index) {
        index_all();
        return;
    }
    if ((preprocess_only || compile_only) && out_path != NULL && n > 1)
        error("-o with -E, -c or -S needs a single input", "");
    if (out_path != NULL)
        check_output();
    /* /tmp/agonc, and /tmp first if need be: MOS makes one folder at a time */
    if (failed || join(dir, root, "/tmp", 0, "") == NULL)
        return;
    sys_mkdir(dir);
    if (join(dir, root, "/tmp/agonc", 0, "") == NULL)
        return;
    sys_mkdir(dir);
    for (i = 0; i < ninputs && !failed; i++) {
        if (kinds[i] == K_C || (kinds[i] == K_I && !preprocess_only))
            compile(i);
        else if (preprocess_only || compile_only)
            warning(inputs[i], ": input not used with -E, -c or -S");
    }
    if (!preprocess_only && !compile_only)
        link_all();
}

/* -h: lines of at most 60 characters, for the Agon's narrower screen modes. */
static void show_help(void)
{
    sys_out("usage: agonc [options] file...\n");
    sys_out("Compiles C (.c, .i) and links it with assembly (.s) into a\n");
    sys_out("program; an .asm file is assembled alone.\n");
    sys_out("  -o file      the output: a.bin by default; a .s with -c\n");
    sys_out("               or -S, a .i with -E\n");
    sys_out("  -c, -S       compile each input to a .s only\n");
    sys_out("  -E           preprocess only (to -o, or the screen)\n");
    sys_out("  -I dir       add an include directory\n");
    sys_out("  -D name[=v]  define a macro; -U name undefines one\n");
    sys_out("  -L dir       add a library directory\n");
    sys_out("  -lname       link libname.s\n");
    sys_out("  -ansi        strict C89 (also -std=c89, -std=c90)\n");
    sys_out("  -std=c99     the default mode (also -std=gnu99)\n");
    sys_out("  -w           no warnings; -Werror makes them errors\n");
    sys_out("  -O0          no peephole pass (-O, the default, has it)\n");
    sys_out("  -nostdlib    no startup code and no C library\n");
    sys_out("  -save-temps  keep the intermediate files in /tmp/agonc\n");
    sys_out("  -Wl,--entry=sym  keep sym, and what it uses, in the link\n");
    sys_out("  --index lib.s  index a library: links using it are faster\n");
    sys_out("  -time        show each pass's time and the total\n");
    sys_out("  -v           show each pass's command line as well\n");
    sys_out("  --version    the compiler's version\n");
    sys_out("  -h, --help   this list\n");
    sys_out("  @file        read more arguments from file\n");
}

/* The status is 0, 200 after any error, or 130 if Ctrl-C stopped the
 * build (driver.md section 6). The temporary files go whether or not the
 * build succeeded. */
int main(int argc, char **argv)
{
    int i;
    unsigned start;

    sys_init(argv[0]);
    for (i = 1; i < argc; i++)
        expand(argv[i]);
    if (!failed)
        parse();
    if (help) {
        show_help();
        return failed ? 200 : 0;
    }
    if (failed)
        return 200;
    if (version) {
        sys_out(VERSION);
        if (ninputs == 0)
            return 0;
    }
    if (ninputs == 0) {
        error("no input files", " (agonc -h lists the options)");
        return 200;
    }
    if (join(root, sys_root(), "", 0, "") == NULL || join(tmp_dir, root, "/tmp/agonc/", 0, "") == NULL)
        return 200;
    start = sys_clock();
    sys_date(date_now, time_now);
    build();
    if (!save_temps)
        for (i = 0; i < ntemps; i++)
            sys_remove(temps[i]);
    if (timing)
        show_time("total", sys_clock() - start);
    return failed ? (sys_interrupted() ? 130 : 200) : 0;
}
