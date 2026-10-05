/* assert.h - diagnostics (C89 4.2). No include guard: each inclusion
 * defines assert anew by whether NDEBUG is defined then. A failed
 * assertion prints "Assertion failed: expr, file name, line n" on stderr
 * and calls abort (status 134). */

#undef assert
#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else
void __assert(const char *expr, const char *file, int line);
#define assert(expr) ((expr) ? (void)0 : __assert(#expr, __FILE__, __LINE__))
#endif
