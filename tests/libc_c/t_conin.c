/* t_conin.c - console input: the test types "Bob" and Enter, and fgets
 * on stdin must return exactly "Bob\n". stdin's buffer comes from malloc,
 * so the heap is filled with 'x' first: a line editor that started from
 * the buffer's old bytes would hand back x...xBob. With an argument, stdin
 * is made unbuffered first (setbuf(stdin, NULL)), which must still give
 * the line editor a line's room. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

int main(int argc, char **argv)
{
    char *p;
    char line[80];

    p = malloc(4096);
    memset(p, 'x', 4095);
    p[4095] = 0;
    free(p);
    if (argc > 1)
        setbuf(stdin, NULL);
    check(fgets(line, 80, stdin) == line, 1);
    check_str(line, "Bob\n");
    return finish();
}
