# Changelog

Versions follow [semantic versioning](https://semver.org): a version like
1.1.0-beta.1 is a pre-release of 1.1.0.

## 1.1.0-beta.2 (2026-10-08)

- **agonc's library moves to `/lib/agonc`**, and its intermediate files to
  `/tmp/agonc`, so that `/lib` and `/tmp` stay free for other programs.
  `#include <agon/vdp.h>` and the rest work as before, and `/usrlib` stays
  where it is: it is your folder, which other compilers may share.
  **Upgrading from 1.1.0-beta.1:** after unzipping the new release, delete
  `/lib`'s `assert.h`, `ctype.h`, `errno.h`, `float.h`, `limits.h`, `locale.h`,
  `math.h`, `setjmp.h`, `signal.h`, `stdarg.h`, `stddef.h`, `stdint.h`,
  `stdio.h`, `stdlib.h`, `string.h`, `time.h`, `crt0.s`, `rt.s`, `libc.s`,
  `libm.s`, `libagon.s` and the `.idx` files beside them, and the folder
  `/lib/agon`, which agonc no longer uses.
- **C99 in the default mode**, all of it but variable-length arrays and
  complex numbers (`__STDC_VERSION__` is now `199901L` there):
  - declarations in a `for` and anywhere in a block, now without a warning
    (strict mode still warns);
  - `_Bool`, `inline`, `restrict`, `__func__`, flexible array members and
    hexadecimal floating constants (`0x1.8p3`);
  - designated initialisers (`{ [2] = 1, .y = 3 }`, in any order) and
    compound literals (`(struct point){ 1, 2 }`);
  - variadic macros (`__VA_ARGS__`) and `_Pragma`;
  - `snprintf`, `vsnprintf`, `vscanf`, `vfscanf`, `vsscanf`, `strtof`,
    `strtold`, `_Exit`, `isblank` and `va_copy`;
  - `printf`'s and `scanf`'s `hh`, `z` and `t` length modifiers and `%a`;
    hexadecimal, `inf` and `nan` read by `strtod`, `atof` and `scanf`;
  - the headers `<stdbool.h>`, `<inttypes.h>` and `<iso646.h>`;
  - C99's `<math.h>`: `cbrt`, `hypot`, `expm1`, `log1p`, `log2`, `exp2`,
    the inverse hyperbolic functions, `erf`, `erfc`, `lgamma`, `tgamma`,
    the rounding functions, `remainder`, `remquo`, `fma` and the rest, the
    `float` and `long double` forms of every function, and the
    classification and comparison macros.

  Strict mode (`-ansi`) is C89 as before: there, `strtod("inf", &end)`
  still converts nothing. `-std=c99` and `-std=gnu99` select the default
  mode, as gcc's makefiles expect.
- **Faster runtime and library**, every assembly routine with its C kept
  beside it or checked against a C reference:
  - `strlen`, `strcmp`, `strcpy`, `strcat`, `strchr`, `memcpy`,
    `memmove`, `memset` and `memchr` use the eZ80's block instructions:
    string-heavy code runs about twice as fast.
  - `int` division and remainder work in registers and take rounds only
    for the dividend's significant bytes: `x % 26` about 10 times as fast.
  - `int` multiplication works in registers (about 3 times as fast), and
    `long` multiplication uses the eZ80's 8-bit multiply.
  - `long` division and remainder take rounds only for the quotient's
    bytes: about 12 times as fast.
  - `long long` division is sized to its operands: about 35 times as fast
    for small ones.
  - `qsort` steps through the array without multiplying: about 3 times as
    fast.

  Together, the benchmarks in `tests/bench` run in about half the time.
  The multiply and divide helpers are now reentrant, and a C interrupt or
  key handler may use any arithmetic while the main program does too.

Fixes:

- A bit-field of 9 to 16 bits was read and stored as three bytes, so a
  store could undo a change to the byte after it, as in
  `s.field = (s.next = 1)`. It is now two bytes.
- `setbuf(stdin, NULL)` and `setvbuf(stdin, ..., _IONBF, ...)` gave the
  keyboard a one-byte buffer, which the next line of input overran. Console
  input now keeps a line buffer whatever is asked.
- `argv[argc]` is now a null pointer even when all 32 entries are used.
- `fgets(s, 1, f)` returns `s` holding an empty string, as C requires.
- `fsetpos` no longer reports an unrelated earlier `errno`, `fopen` no
  longer leaves `errno` set after succeeding, and `calloc` and `realloc`
  set `ENOMEM` when a request is too large, as `malloc` does.
- The serial-port functions in `<agon/uart.h>` keep the frame pointer
  across their MOS calls.
- The passes close their input when they cannot create their output, so
  no MOS file handle is lost.
- `#line` rejects numbers above 8388607 instead of overflowing, and
  `defined(__DATE__)` and `defined(__TIME__)` are 1.
- A call with 32 arguments is now the error the limit of 31 promises.

## 1.1.0-beta.1 (2026-10-04)

The first public release. agonc compiles ANSI C89, with its whole standard
library and some of C99, on the Agon Light and Console8, under MOS 2.3.3
and MOS 3, and builds itself there.

What it has:

- C89 with a conforming strict mode (`-ansi`), and a default mode with C99's
  `long long`, `//` comments, declarations after statements and a few other
  extensions.
- The C89 library, `<stdint.h>`, and C99's `long long` functions; IEEE 754
  `float` and `double`, correctly rounded, in software, with the most-used
  operations in assembly.
- `<agon/mos.h>`, `<agon/uart.h>` and `<agon/vdp.h>`: every MOS 2.3.3 call, the
  FatFS calls, the system variables, keyboard and interrupt handlers in C,
  the second serial port, and every documented VDU command.
- An optimiser, on by default (`-O0` turns it off).
- Library indexes, so that a link reads only what a program uses.
- The manual (`docs/manual/`, and a PDF from `make manual`), examples, and
  the specifications.

Known issue: the graphics contexts (`vdp_context_save` and the rest)
send the documented commands, but a text background colour set after a
save did not show as documented, on the emulator or on a real Agon.

Version 1.0 was an internal milestone and was never released.
