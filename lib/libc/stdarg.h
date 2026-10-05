/* stdarg.h - variable arguments.
 *
 * The macros name cc1's built-ins, which walk the 3-byte argument slots
 * (abi.md section 4) and check what they can: va_start only in a function
 * with '...', and on its last named parameter. va_arg(ap, int) reads an
 * int-sized slot; a char argument is the low byte of its slot.
 */

#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define va_start(ap, last) __va_start(ap, last)
#define va_arg(ap, type) __va_arg(ap, type)
#define va_end(ap) __va_end(ap)

#endif
