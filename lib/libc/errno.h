/* errno.h - the error numbers of c89_spec.md section 15 (the usual Unix
 * ones). errno is defined in stdlib.c. The file functions set ENOENT,
 * EEXIST, EINVAL (a bad fopen mode; a failed fsetpos), EMFILE (no stream
 * free), EBADF and EIO; signal sets EINVAL; malloc sets ENOMEM; the maths
 * functions set EDOM and ERANGE, and strtod, strtol, strtoul, strtoll and
 * strtoull (so atoll too) set ERANGE. strerror gives each a short
 * message. */

#ifndef _ERRNO_H
#define _ERRNO_H

extern int errno;

#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EEXIST 17
#define EINVAL 22
#define EMFILE 24
#define EDOM 33
#define ERANGE 34

#endif
