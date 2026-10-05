/* t_env.c - G test (test_libc_c.py): getenv on MOS 3.0.2, after the
 * script's "set AgoncTest hello there": the value, in either case, and
 * NULL for names MOS 3 does not have (it would otherwise answer with a
 * neighbouring variable). With an argument (run on MOS 2, which has no
 * variables), every name is NULL. */

#include <stdlib.h>
#include "check.h"

int main(int argc, char **argv)
{
    char *v;

    if (argc > 1) {
        check(getenv("AgoncTest") == NULL, 1);
    } else {
        v = getenv("AgoncTest");
        check(v != NULL, 1);
        check_str(v != NULL ? v : "", "hello there");
        check(getenv("AGONCTEST") != NULL, 1);
    }
    check(getenv("NoSuchVariable") == NULL, 1);
    check(getenv("AgoncTes") == NULL, 1);
    check(getenv("AgoncTestX") == NULL, 1);
    check(getenv("Agonc*") == NULL, 1);          /* a pattern is not a name */
    return finish();
}
