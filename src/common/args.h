/* args.h - command-line handling shared by every pass.
 *
 * driver.md section 5: every pass accepts "@file", whose whitespace-separated
 * tokens are read in place of the argument (no quoting, no nesting).
 */

#ifndef ARGS_H
#define ARGS_H

#define MAX_ARGS 128
#define ARG_TEXT 4096

/* Expands argv (argv[0] is skipped) into args[], returning the count, or -1
 * after printing a message naming `prog` if a response file cannot be read
 * or a limit is exceeded. The strings stay valid for the whole run. */
int expand_args(char *prog, int argc, char **argv, char **args);

#endif
