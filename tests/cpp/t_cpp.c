/* t_cpp.c - cpp's execution test: every preprocessor feature, checked
 * at run time after cpp, cc1, cc2 and ld (exit 0, or the number of the
 * first failing check).
 */

#include "t_cpp.h"
#include "t_cpp.h"              /* the include guard makes this a no-op */
#include <string.h>

/* ---- object-like macros ------------------------------------------------------------- */

#define A 10
#define B (A * 2)               /* A is looked up when B is used, not here */
#define EMPTY
#define STR "text"
#define NEG -1
#define TYPE unsigned char
#define FOREVER for (;;)
#define SELF SELF
#define LOOP1 LOOP2
#define LOOP2 LOOP1
#define A 10                    /* an identical redefinition is allowed */
#define   SPACED    1   +   2   /* white space in a definition is not significant */
#define SPACED 1 + 2
#define SPLICED 10 + \
2                               /* a line ending in a backslash joins the next */

int SELF;                       /* a macro is not expanded inside itself */
int LOOP1;                      /* LOOP1 -> LOOP2 -> LOOP1, which stops */
int b_before = B;

#undef A
#define A 3

#ifdef A
int a_defined = 1;
#else
int a_defined = 0;
#endif

#ifndef NOT_DEFINED_ANYWHERE
int ifndef_ok = 1;
#endif

#undef NOT_DEFINED_ANYWHERE     /* #undef of an unknown name is fine */

/* ---- conditionals ---------------------------------------------------------------------- */

#if 0
this is not C at all, and ' an unbalanced quote " does not matter
#error not reached
#include "no such file.h"
#if 1
#else
#endif
#endif

#if B == 6 && defined(A) && defined B && !defined(C_NOT)
int if_value = 1;
#elif 1
int if_value = 2;
#else
int if_value = 3;
#endif

#if 0
int elif_value = 1;
#elif B == 5
int elif_value = 2;
#elif B == 6
int elif_value = 3;
#elif 1
int elif_value = 4;
#else
int elif_value = 5;
#endif

#if 1
#if 0
int nested = 1;
#else
#if 1
int nested = 2;
#endif
#endif
#else
int nested = 3;
#endif

/* integer semantics in #if: the default mode's 64 bits (strict mode's 32: the D cases),
 * constants typed as in the program (24-bit int), C's signed and unsigned rules */
#if 0x10 == 16 && 010 == 8 && 'A' == 65 && '\n' == 10 && '\xFF' == -1 && L'\xFF' == 255 && L'\400' == 256 && L'\xFFFFFF' == -1
int consts_ok = 1;
#else
int consts_ok = 0;
#endif

#if 0x7FFFFFFFFFFFFFFF + 1 < 0 && (1 << 63) < 0 && 0xFFFFFFFFFFFFFFFF == -1 && 0x7FFFFFFF + 1 > 0 && -1 > 0u && -1 < 0 && 0x7FFFFF + 1 > 0 && -1 > 0x800000 && -1 < 8388608 && 4294967295 / 65536 == 65535 && 1L == 1 && 10UL == 10lu
int wrap_ok = 1;
#else
int wrap_ok = 0;
#endif

#if 7 / 2 == 3 && -7 / 2 == -3 && -7 % 2 == -1 && (5 ^ 3) == 6 && (5 | 2) == 7 && ~0 == -1
int arith_ok = 1;
#else
int arith_ok = 0;
#endif

#if (1 ? 2 : 1 / 0) == 2 && (0 && 1 / 0) == 0 && (1 || 1 / 0) == 1
int short_circuit_ok = 1;
#else
int short_circuit_ok = 0;
#endif

#if UNDEFINED_NAME == 0 && !UNDEFINED_NAME
int undefined_is_zero = 1;
#else
int undefined_is_zero = 0;
#endif

#if __STDC__ == 1 && __AGONC__ == 1 && defined __EZ80__ && defined(__ADL__) && defined __LINE__ && __SIZEOF_INT__ == 3 && __SIZEOF_LONG__ * __CHAR_BIT__ == 32 && __INT_MAX__ == 8388607 && __LONG_MAX__ == 2147483647
int predefined_ok = 1;
#else
int predefined_ok = 0;
#endif

#if defined(FROM_COMMAND_LINE) && FROM_COMMAND_LINE == 42 && defined ONE_BY_DEFAULT && ONE_BY_DEFAULT == 1
int command_line_ok = 1;
#else
int command_line_ok = 0;
#endif

#ifdef UNDONE_ON_COMMAND_LINE
int undone = 0;
#else
int undone = 1;
#endif

/* ---- use ----------------------------------------------------------------------------------- */

int line_here = __LINE__;

/* #asm: its lines reach the assembler untouched (ASMVAL is not expanded) */
#define ASMVAL 99
int gasm;

void set_by_asm(void)
{
#asm
    ld hl,1234          ; ASMVAL stays as it is
    ld (_gasm),hl
#endasm
}

int main(void)
{
    TYPE t;
    int n;
    int x;
    char *s;

    check(b_before, 20);             /* expanded while A was 10 */
    check(B, 6);
    check(a_defined, 1);
    check(ifndef_ok, 1);
    check(if_value, 1);
    check(elif_value, 3);
    check(nested, 2);
    check(consts_ok, 1);
    check(wrap_ok, 1);
    check(arith_ok, 1);
    check(short_circuit_ok, 1);
    check(undefined_is_zero, 1);
    check(predefined_ok, 1);
    check(command_line_ok, 1);
    check(undone, 1);
    check(HEADER_VALUE, 77);
    check(NESTED_VALUE, 78);
    check(nested_line, 7);
    check(line_here, 142);
    check(__LINE__, 182);
    check(SPACED * 2, 5);       /* 1 + 2 * 2: the replacement is text, not a value */
    check(-NEG, 1);             /* "- -1", not "--1" */
    check(strlen(STR), 4);
    check(strlen("A B STR"), 7);        /* no expansion inside a string */
    check('A', 65);                     /* nor inside a character constant */
    t = 300;
    check(t, 44);
    check(sizeof(TYPE), 1);
    SELF = 5;
    check(SELF, 5);
    LOOP1 = 6;
    check(LOOP1, 6);
    s = __FILE__;
    n = strlen(s);
    check(n >= 7 && strcmp(s + n - 7, "t_cpp.c") == 0, 1);
    x = 0 EMPTY + 1;
    check(x, 1);
    n = 0;
    FOREVER {
        n++;
        if (n == 3)
            break;
    }
    check(n, 3);
    set_by_asm();
    check(gasm, 1234);
    check(SPLICED, 12);
    x = 1 +\
2;
    check(x, 3);
    check(strlen("ab\
cd"), 4);                       /* a string spliced too */
    check(sizeof(__DATE__), 12);
    check(strcmp(__DATE__, "Jan  1 1980"), 0);  /* no -date: C89's "some valid date" */
    check(strcmp(__TIME__, "00:00:00"), 0);
#line 900
    check(__LINE__, 900);

    /* function-like macros (M11) */
#define SQ(x) ((x) * (x))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define STR2(s) #s
#define XSTR(s) STR2(s)
#define CAT(a, b) a ## b
#define XCAT(a, b) CAT(a, b)
#define NOARGS() 7
#define APPLY(f, v) f(v)
#define SUM3(a, b, c) (a + b + c)
    check(SQ(3), 9);
    check(SQ(1 + 2), 9);                /* the parentheses in the replacement */
    check(MAX2(4, SQ(3)), 9);
    check(strcmp(STR2(a  +   "b\n"), "a + \"b\\n\""), 0);
    check(strcmp(XSTR(SQ(2)), "((2) * (2))"), 0);
    check(strcmp(STR2(SQ(2)), "SQ(2)"), 0);     /* # takes the argument as written */
    check(CAT(1, 2), 12);
    check(XCAT(1, CAT(2, 3)), 123);
    check(NOARGS(), 7);
    check(APPLY(SQ, 4), 16);
    check(SUM3(1,
               2,
               3), 6);                  /* a call over three lines */
    check(MAX2((1, 5), 2), 5);          /* a comma inside parentheses */
    n = 5;
    check(SQ(n + 1), 36);               /* (n + 1) * (n + 1) */
#pragma weak absent_fn
    {
        int absent_fn(void);

        check(absent_fn == 0, 1);       /* weak, and defined nowhere: 0 */
    }
    if (fails == 0)
        agon_emu_exit(0);
    else
        agon_emu_exit(first < 250 ? first : 250);
    return 0;
}
