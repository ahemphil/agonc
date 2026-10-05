/* io.c - output helpers; see io.h for why the passes write strings through
 * out_str rather than fputs. */

#include <stdio.h>
#include <string.h>
#include "io.h"

void out_str(FILE *f, char *s)
{
    int n;

    n = strlen(s);
    if (n > 0)
        fwrite(s, 1, n, f);
}
