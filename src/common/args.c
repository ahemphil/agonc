/* args.c - "@file" response-file expansion; see args.h.
 *
 * Every pass (cpp, cc1, cc2, ld) calls expand_args first thing in main.
 * The driver writes a response file when a command line would be too long
 * for MOS or for the C start-up code's argument limit, and passes "@file"
 * instead. Plain arguments are used where they are (argv's own strings);
 * tokens from a response file are copied, NUL-terminated, one after the
 * other into the static arg_text pool, so they stay valid for the run.
 */

#include <stdio.h>
#include <string.h>
#include "args.h"

/* The pool every response file's tokens are copied into, and its fill. */
static char arg_text[ARG_TEXT];
static int arg_used;

/* Stores s as args[n]; returns the new count, or -1 (after a message) when
 * MAX_ARGS is reached. Callers stop as soon as the count goes negative. */
static int add_arg(char *prog, char **args, int n, char *s)
{
    if (n >= MAX_ARGS) {
        fprintf(stderr, "%s: more than %d arguments\n", prog, MAX_ARGS);
        return -1;
    }
    args[n] = s;
    return n + 1;
}

/* Appends the whitespace-separated tokens of the file at path to args[],
 * starting at index n; returns the new count or -1. A token is any run of
 * bytes other than space, tab, CR and LF: no quoting, and a token that
 * starts with '@' is kept as it is (no nesting).
 *
 * The file is read in blocks with fread, never with fgetc: the stage-1
 * passes run on AgDev's library, whose fgetc takes the end of a file from
 * the byte MOS returns (0 or 0xFF) rather than asking MOS, and on a real
 * Agon that byte can be anything, so it never ends. fread counts what MOS
 * actually read. */
static int read_response(char *prog, char *path, char **args, int n)
{
    FILE *f;
    char buf[128];
    int len;
    int i;
    int c;
    char *tok;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "%s: cannot open response file %s\n", prog, path);
        return -1;
    }
    tok = NULL;                         /* the token being copied, if any */
    while (n >= 0 && (len = (int)fread(buf, 1, sizeof buf, f)) > 0) {
        for (i = 0; i < len && n >= 0; i++) {
            c = buf[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (tok != NULL) {      /* a token ends */
                    arg_text[arg_used] = 0;
                    arg_used++;
                    n = add_arg(prog, args, n, tok);
                    tok = NULL;
                }
                continue;
            }
            /* a byte of a token, into the pool, leaving room for its NUL */
            if (arg_used >= ARG_TEXT - 1) {
                fprintf(stderr, "%s: response file %s too long\n", prog, path);
                fclose(f);
                return -1;
            }
            if (tok == NULL)
                tok = arg_text + arg_used;
            arg_text[arg_used] = c;
            arg_used++;
        }
    }
    if (tok != NULL && n >= 0) {        /* the last token, at the end of the file */
        arg_text[arg_used] = 0;
        arg_used++;
        n = add_arg(prog, args, n, tok);
    }
    fclose(f);
    return n;
}

int expand_args(char *prog, int argc, char **argv, char **args)
{
    int i;
    int n;

    n = 0;
    for (i = 1; i < argc && n >= 0; i++) {
        if (argv[i][0] == '@')
            n = read_response(prog, argv[i] + 1, args, n);
        else
            n = add_arg(prog, args, n, argv[i]);
    }
    return n;
}
