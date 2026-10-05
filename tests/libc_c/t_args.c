/* t_args.c - E test (test_libc_c.py): crt0's argument splitting, run as
 * t_args "a b" c "" x"y" "d e : a leading quote runs to the next one and
 * both go, "" is an empty argument, a quote inside a token stays, and an
 * unclosed one runs to the end of the line. */

#include <stdio.h>

int main(int argc, char **argv)
{
    int i;

    printf("%d\n", argc);
    for (i = 1; i < argc; i++)
        printf("[%s]\n", argv[i]);
    return 0;
}
