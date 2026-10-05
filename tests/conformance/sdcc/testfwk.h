/* testfwk.h - our stand-in for SDCC's regression-test framework header
 * (support/regression/fwk/include/testfwk.h), for run_suite.py sdcc. The
 * tests are built as SDCC's generic host port (-DPORT_HOST), so SDCC's
 * memory-space and calling-convention keywords are empty, and ASSERT counts
 * and reports as theirs does. Theirs uses C11 (_Noreturn) and #pragma
 * disable_warning, which is why the suite's own is not used. */

#ifndef __TESTFWK_H
#define __TESTFWK_H 1

#define SDCC_PDK_BITS(cond) 0

extern int __numTests;
extern const int __numCases;

void __printf(const char *szFormat, ...);
#define LOG(_a) __printf _a

#define _AUTOMEM
#define _STATMEM
#define __critical
#define __data
#define __idata
#define __pdata
#define __xdata
#define __code
#define __near
#define __far
#define __reentrant
#define __dynamicc
#define __smallc
#define __z88dk_fastcall
#define __z88dk_callee
#define __at(x)

void _initEmu(void);
void _exitEmu(void);
void __fail(const char *szMsg, const char *szCond, const char *szFile, int line);
void _putchar(char c);
void __prints(const char *s);
void __printu(unsigned int n);
const char *__getSuiteName(void);
void __runSuite(void);

#define ASSERT(_a) (++__numTests, (_a) ? (void)0 : __fail("Assertion failed", #_a, __FILE__, __LINE__))

#define UNUSED(_a) if (_a) { }

#endif
