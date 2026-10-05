/* t_cpp.h - the check harness for t_cpp.c, behind an include guard, and
 * including a second header (nesting). */

#ifndef T_CPP_H
#define T_CPP_H

#include "t_cpp2.h"

void agon_emu_exit(int status);

int fails;
int first;
int count;

void check(int got, int want)
{
    count++;
    if (got != want) {
        fails++;
        if (first == 0)
            first = count;
    }
}

#define HEADER_VALUE 77

#endif
