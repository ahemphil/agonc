/* stddef.h - common definitions (C89 4.1.5). */

#ifndef _STDDEF_H
#define _STDDEF_H

typedef int ptrdiff_t;
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
typedef int wchar_t;            /* 24 bits, as int (c89_spec.md 5) */
#endif

#define NULL ((void *)0)

/* A member's offset: the address of the member of a struct at address 0,
 * which cc1 folds to a constant. */
#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif
