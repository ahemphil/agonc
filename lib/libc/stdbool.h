/* stdbool.h - C99's bool, true and false. Strict mode has no _Bool, so
 * there bool is int, which holds the same values; a conversion to it
 * keeps the value rather than making it 0 or 1. */

#ifndef _STDBOOL_H
#define _STDBOOL_H

#if !defined(__STRICT_ANSI__)
#define bool _Bool
#else
#define bool int
#endif
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#endif
