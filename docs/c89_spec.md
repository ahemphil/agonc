# agonc: the language and library

agonc is a hosted implementation of **ANSI C89** (ANSI X3.159-1989,
identical in content to ISO/IEC 9899:1990 "C90") for the Agon Light and
Console8: eZ80 in ADL mode, MOS 2.3.3, and MOS 3.0 and later. The standard
defines the language and library; this document does not repeat it. It
states what the standard leaves to the implementation (§2–§12, following
the standard's own list of implementation-defined behaviour, ISO 9899:1990
annex G.3), what agonc adds (§13), and how conformance is checked (§14).

Companions: [abi.md](abi.md) (sizes, registers, calling convention,
frames, symbols, memory layout — authoritative where the two overlap),
[driver.md](driver.md) (the `agonc` command), [object_format.md](object_format.md),
[ir_format.md](ir_format.md).

## 1. Modes

agonc has two modes.

| | Default | Strict (`-ansi`, `-std=c89`, `-std=c90`) |
|---|---|---|
| `asm` | a keyword (§13) | an ordinary identifier; `__asm` still works |
| `//` comments | accepted | not comments (`a //* c */ b` is `a / b`, as C89 requires) |
| trigraphs (`??=` …) | not recognised, with a warning where one appears | recognised, as C89 requires |
| implicit `int` (`static x;`, `f() {…}`, an undeclared K&R parameter) | accepted, with a warning at each | accepted, as C89 requires, with a warning at each |
| implicit function declarations (calling an undeclared function) | **errors**, as in current GCC and Clang | accepted, as C89 requires, with a warning at each |
| a comma after the last enumerator, non-constant initialisers for local aggregates (a struct value for a member among them), `#warning` (C99 and later) | accepted | errors, as C89 requires |
| `long long` and `unsigned long long`, the `LL` and `ULL` suffixes (C99) | accepted (§13) | errors, as C89 requires |
| integer constants and `#if` arithmetic | typed and done as C99 does, in 64 bits at most (§6, §11) | as C89 requires, in 32 bits at most |
| every other extension (§13) | on | on: none of them changes the meaning of a conforming program |

Strict mode is the conforming mode, and the conformance suites run in it;
existing Agon programs are meant for the default mode. The default mode
is deliberately stricter than C89 about implicit function declarations,
which hide real errors: on this target a function returning `long`,
`float`, `double` or a structure, called through an implicit `int`
declaration, silently gives a wrong result. In strict mode, where they are
accepted, the linker still reports that case (§13). Implicit `int` in a
declaration hides nothing of the kind, and old code is full of it, so both
modes accept it with a warning (`-Werror` makes it an error). A declaration
after a statement in a block, a C99-ism, is accepted with a warning in both
modes (C89 requires only a diagnostic).

## 2. Translation (G.3.1)

- **Diagnostics** are one line each, `file:line: error: message` or
  `file:line: warning: message`, on standard error. An error stops the
  pass after the current translation unit and makes the compilation fail
  (the driver's exit status is 200). A warning does not, unless `-Werror`
  is given; `-w` silences warnings. C89 requires a diagnostic for every
  syntax rule and constraint violation; agonc's is an error unless this
  document says it is a warning.
- **Source files** are byte streams. `\n` ends a line; a `\r` immediately
  before it is ignored, so CR LF and LF files are both accepted. A `0x1A`
  byte ends the file (§13).

## 3. Environment (G.3.2)

- `main` may be defined as `int main(void)` or `int main(int argc, char
  *argv[])`. `argv[0]` is an empty string (MOS does not tell a program its
  own name); the arguments are the words of the MOS command line after the
  program name, separated by spaces and tabs. A word may be enclosed in
  double quotes, which are removed and inside which spaces are kept. At most
  31 arguments are passed; further words are dropped. The strings are
  modifiable, as C89 requires.
- Returning from `main` with no value, or falling off its end, returns
  status 0.
- The **interactive device** is the Agon's keyboard and screen: `stdin`,
  `stdout` and `stderr` are all connected to it.

## 4. Identifiers (G.3.3)

- Identifiers are significant to **47 characters**, with or without
  external linkage, and case is significant in both. A longer identifier is
  an error rather than being silently truncated (ABI §7: the assembler's
  64-character labels leave no room for more once a static's prefix is
  added). C89 asks for at least 31; 47 lets the Agon interface keep
  AgDev's longer names.

## 5. Characters (G.3.4)

- The source and execution character sets are **ASCII**. Bytes 0x80–0xFF
  may appear in character constants, string literals and comments and are
  passed through unchanged; their meaning is the Agon's screen font's.
- A character is **8 bits**. Plain `char` is **signed** (−128..127), with
  the same range as `signed char`.
- **Multibyte characters**: only the "C" locale exists, and in it every
  byte is one character and there are no shift states. `mblen`, `mbtowc`,
  `wctomb`, `mbstowcs` and `wcstombs` convert byte for byte.
- `wchar_t` is `int` (24 bits, ABI §2). A wide character constant `L'x'`
  has the value of `x` as an `unsigned char`; a wide string literal
  `L"..."` is an array of `wchar_t`, one element per byte, plus the
  terminator. An octal or hexadecimal escape sequence in either may have
  any value up to 0xFFFFFF (C89 3.1.3.4: `wchar_t`'s unsigned type), and
  the element is that value converted to `wchar_t` (`L'\xFFFFFF'` is −1).
- An escape sequence or character outside the basic character set in a
  character constant has the value of its byte as a `char` (so `'\xFF'` is
  −1). A **multi-character constant** (`'ab'`) is accepted with a warning;
  its value is the first character's byte in bits 8–15 and the second's in
  bits 0–7, as an `int` (`'ab'` is 0x6162), and more than three characters
  is an error. A wide character constant with more than one character is
  an error.

## 6. Integers and floating point (G.3.5, G.3.6)

**Integers** (ABI §2):

| Type | Bits | Range |
|---|---|---|
| `signed char`, `char` | 8 | −128 .. 127 |
| `unsigned char` | 8 | 0 .. 255 |
| `short` | 16 | −32768 .. 32767 |
| `unsigned short` | 16 | 0 .. 65535 |
| `int` | 24 | −8388608 .. 8388607 |
| `unsigned int` | 24 | 0 .. 16777215 |
| `long` | 32 | −2147483648 .. 2147483647 |
| `unsigned long` | 32 | 0 .. 4294967295 |
| `long long` (default mode) | 64 | −9223372036854775808 .. 9223372036854775807 |
| `unsigned long long` (default mode) | 64 | 0 .. 18446744073709551615 |

- Two's complement throughout; no padding bits, no trap representations.
- Converting to a signed type a value it cannot represent keeps the low
  bits (the result is the value modulo 2ⁿ, read as two's complement).
- Bitwise operators act on the two's complement bits, signed or not.
- `/` truncates toward zero; `%` has the sign of the dividend.
- `>>` of a negative signed value is arithmetic (sign bits shift in).
- Signed overflow wraps. C89 leaves it undefined; agonc defines it.
- An unsuffixed decimal constant has the first of `int`, `long`, `unsigned
  long` that holds it; an unsuffixed octal or hexadecimal constant the first
  of `int`, `unsigned int`, `long`, `unsigned long`; with `u`, `l` or both,
  as C89 says. So `8388608` is `long` and `0xFFFFFF` is `unsigned int`.
  In the default mode C99's rules extend these with `long long` and
  `unsigned long long` and the `ll` suffix: an unsuffixed decimal constant
  is never unsigned, so `4294967295` is `unsigned long` in strict mode and
  `long long` in the default mode. A decimal constant beyond `long long`
  is `unsigned long long`, with a warning.

**Floating point** (ABI §2):

- `float` is IEEE 754 binary32; `double` and `long double` are IEEE 754
  binary64. `<float.h>` gives their characteristics (`FLT_RADIX` 2,
  `FLT_MANT_DIG` 24, `DBL_MANT_DIG` 53, `FLT_DIG` 6, `DBL_DIG` 15,
  `DBL_MAX` ≈ 1.7976931348623157e308, and so on).
- **IEEE 754 arithmetic**: addition, subtraction, multiplication, division and
  `sqrt` are correctly rounded, round to nearest with ties to even, the
  only rounding mode (`FLT_ROUNDS` is 1). Subnormals, infinities, NaNs and
  signed zeros are supported as IEEE 754 defines them; there are no
  exception flags or traps (C89 has neither). Converting an integer that a
  floating type cannot represent exactly rounds to nearest, ties to even;
  converting `double` to `float` likewise. Converting a floating value to
  an integer truncates toward zero; a value out of the integer type's range
  is undefined, as in C89. Decimal conversion — the compiler reading
  constants, `strtod`, `atof`, `scanf`, and `printf`'s `%e %f %g` — is
  correctly rounded, one algorithm shared by the compiler and the library.
  `<math.h>` functions are accurate to about one unit in the last place
  (fdlibm's algorithms: below one for most, below two for `sinh`, `cosh`
  and `tanh`; `sqrt`, `fabs`, `floor`, `ceil`, `fmod`, `modf`, `frexp` and
  `ldexp` are exact).
  Everything is deterministic, so every result can be checked bit for bit
  against a PC's IEEE arithmetic.

## 7. Arrays, pointers and registers (G.3.7, G.3.8)

- `size_t` is `unsigned int` (24 bits: larger than any object the Agon can
  hold); `ptrdiff_t` is `int`.
- A pointer converted to an integer type of 24 bits or wider keeps its
  address bits (zero-extended to `long`); narrower types keep the low bits.
  An integer converted to a pointer keeps its low 24 bits as the address.
  So `(char *)0x080000` is that address.
- `register` is accepted and has no effect: every object stays in memory.
  Taking the address of a `register` object is still the constraint error
  C89 requires.

## 8. Structures, unions, enumerations and bit-fields (G.3.9)

- **No padding** anywhere: members are at consecutive byte offsets in
  declaration order, and a structure's size is the sum of its members'.
- A **union** member read after a different member was stored gives the
  bytes of the stored value reinterpreted as the read member's type (all
  members start at offset 0).
- **Bit-fields** (ABI §2): declared `int`, `signed int` or `unsigned int`;
  a plain `int` bit-field is **signed**. The storage unit is the byte:
  fields are allocated from the least significant bit; one of 8 bits or
  fewer never crosses a byte boundary; a wider one (up to 24 bits) starts at
  a byte boundary; a zero-width one moves to the next byte.
- An **enumeration** type is `int`; its constants are `int`.

## 9. Qualifiers and declarators (G.3.10, G.3.11)

- An access to a `volatile` object is every read or write of it that the
  program's source performs; agonc keeps no object in a register, so every
  such access reaches memory, in the order of the source's sequence points.
- A type may be modified by at least 12 pointer, array and function
  declarators (C89's minimum; the actual limit is in §12).

## 10. Statements (G.3.12)

- A `switch` may have at least 257 `case` labels (C89's minimum; the actual
  limit is in §12). Its controlling expression may be of any integer type,
  `long` and (in the default mode) `long long` included.

## 11. Preprocessing (G.3.13)

- A character constant in `#if` has the same value as in the program (so it
  may be negative: `'\xFF'` is −1). `#if` arithmetic is done in `long` and
  `unsigned long` in strict mode, as C89 requires, and in the default mode
  in `intmax_t` and `uintmax_t` (`long long` and `unsigned long long`), as
  C99 requires.
- **Include files**: `#include "name"` looks first in the including file's
  folder, then as `#include <name>` does: each `-I` folder in order, then
  `/usrlib`, then `/lib` (driver.md). Names are matched without regard to
  case (FAT is case-insensitive). A name may contain folders separated by
  `/`.
- `#pragma weak name` makes `name` a weak reference (§13). Every other
  `#pragma` is ignored, with a warning.
- `__DATE__` and `__TIME__` are the date and time of compilation from the
  Agon's real-time clock (MOS keeps it; on an Agon without a set clock they
  are whatever the clock reads). A clock that reads no valid date (an unset
  one reads day 0 of 1980) gives `"Jan  1 1980"` and `"00:00:00"`.
  `__STDC__` is 1 in both modes.
- Predefined: `__AGONC__` (the compiler), `__EZ80__`, `__ADL__`. They are in
  the implementation's namespace, so strict mode keeps them.

## 12. Translation limits

C89 requires at least the limits in its §2.2.4.1. agonc meets each of them;
what it actually allows is set by table sizes in each pass, reported by
the pass's own `-v` (peak use against the limit), and raised by recompiling
the compiler.

| Limit | C89 minimum | agonc |
|---|---:|---:|
| nesting of compound statements, iteration and selection statements | 15 | ≥ 15 |
| nesting of conditional inclusion | 8 | 32 |
| declarators modifying a type | 12 | ≥ 12 |
| nesting of parenthesised declarators | 31 | ≥ 31 |
| nesting of parenthesised expressions | 32 | ≥ 32 |
| significant characters in an identifier | 31 (6 external) | 47 |
| external identifiers in one translation unit | 511 | 1,200 (with its file-scope statics) |
| identifiers with block scope in one block | 127 | 300 (in a whole function) |
| macro identifiers defined at once | 1024 | 1,200 |
| parameters in a function definition, arguments in a call | 31 | 31 |
| parameters in a macro definition, arguments in a macro call | 31 | 32 |
| characters in a logical source line | 509 | 4,096 |
| characters in a string literal (after concatenation) | 509 | 509 |
| bytes in an object | 32767 | limited by memory |
| nesting of `#include` | 8 | 8 |
| `case` labels in a `switch` | 257 | 300 (in a whole function) |
| members of a structure or union | 127 | 127 (and 800 in all structures together) |
| enumeration constants in an enumeration | 127 | 127 |
| nesting of structure or union definitions | 15 | ≥ 15 |

A number is the size of a pass's table; ≥ marks a limit that depends on
other tables or on memory, and is at least C89's.

## 13. Extensions

Each is either invisible to a conforming program, or turned off in strict
mode (§1).

1. **Inline assembly**: `asm("text");` and `__asm("text");` as a
   statement, and `#asm` … `#endasm` blocks inside a function, whose lines
   are copied verbatim (a `\r` before a line's end is dropped). The
   contract is ABI §10. `asm` is a keyword except in strict mode; `__asm`
   and `#asm` work in both modes (no C89 program contains `#asm`).
   Assembly outside a function is an error: a function or data written in
   assembly goes in a `.s` file linked with the program (driver.md §2).
2. **`//` comments**, **a comma after the last enumerator**, **non-constant
   initialisers for local aggregates** (`int a[2] = { x, f() };`, and a
   struct value for a struct member whose braces are elided, `struct seg s
   = { 'k', p, q };`) and **`#warning`**, as C99 and later have them
   (default mode only).
3. **`0x1A` ends a source file**, and everything after it is ignored: CP/M
   files end in padding after a `0x1A`. A file without one is read to its
   end.
4. **CR LF line endings** in source files (§2).
5. **Case-insensitive `#include` names** (§11).
6. **`#pragma weak name`**: references to `name` from this translation unit
   are weak (object_format.md §8): they do not bring `name` into the
   program, and if nothing else does, `name` is 0.
7. **Declarations after statements** in a block, with a warning (both
   modes; C89 requires only a diagnostic).
8. **`%F`** in `printf` and `scanf` means `%f`, except that infinity and NaN
   print in capitals (Hi-Tech C used `%F`; this is C99's meaning of it).
9. **Predefined macros** `__AGONC__`, `__EZ80__`, `__ADL__`, and gcc's
   `__CHAR_BIT__`, `__SIZEOF_SHORT__`, `__SIZEOF_INT__`,
   `__SIZEOF_LONG__`, `__SIZEOF_POINTER__`, `__SIZEOF_FLOAT__`,
   `__SIZEOF_DOUBLE__`, `__SCHAR_MAX__`, `__SHRT_MAX__`, `__INT_MAX__`,
   `__LONG_MAX__`, the type macros (`__SIZE_TYPE__`, `__PTRDIFF_TYPE__`,
   `__WCHAR_TYPE__`, `__WINT_TYPE__`, `__INTMAX_TYPE__`, `__INTPTR_TYPE__`,
   `__INT8_TYPE__` … `__UINT32_TYPE__` and their least and fast forms, as
   `<stddef.h>` and `<stdint.h>` have them), the byte order
   (`__BYTE_ORDER__` is `__ORDER_LITTLE_ENDIAN__`; `__ORDER_BIG_ENDIAN__`
   and `__ORDER_PDP_ENDIAN__` are defined too) and the floating-point ones
   (`__FLT_MANT_DIG__`, `__FLT_DIG__`, `__FLT_MAX__`, `__FLT_MIN__`,
   `__FLT_EPSILON__` and the `__DBL_` forms), with this target's values;
   in the default mode also `__SIZEOF_LONG_LONG__`, `__LONG_LONG_MAX__`
   and the 64-bit type macros (`__INT64_TYPE__` and the rest), with
   `__INTMAX_TYPE__` `long long`. Strict mode defines `__STRICT_ANSI__`,
   as GCC does, and the headers then declare none of C99's names.
10. **Bit-fields of type `char`, `short` and `long`** and their `signed`
    and `unsigned` forms; the type sets the field's signedness and maximum
    width, which is never above 24 bits, as for every bit-field (§8; C89
    defines only `int` bit-fields and requires no diagnostic for others).
11. **`<stdint.h>`** (§15).
12. **Agon headers** in `<agon/...>` (§15), outside the standard's names.
13. **A link-time check of implicit calls**: when a function called
    through an implicit declaration (strict mode) turns out to return
    `long`, `unsigned long`, `float`, `double`, a structure or a union, the
    link fails with a message naming the function and the calling unit.
    C89 makes such a call undefined; agonc reports it.
14. **GCC's spellings**, which programs written for GCC use, in both modes
    (a name beginning with two underscores is the implementation's, so a
    conforming program cannot notice them): `__attribute__((...))` is
    accepted and ignored wherever it appears; `__extension__`,
    `__inline__`, `__inline`, `__restrict` and `__restrict__` are
    ignored; `__const__`, `__volatile__` and `__signed__` (and the forms
    with two underscores at the front only) are the keywords. The
    `__builtin_` forms of `abort`, `exit`, `abs`, `labs`, `malloc`,
    `calloc`, `free`, `memcpy`, `memmove`, `memset`, `memcmp`, `strcpy`,
    `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strlen`,
    `strchr`, `strrchr`, `strstr`, `printf`, `sprintf`, `puts` and
    `putchar` are those functions, declared with their C89 prototypes;
    `__builtin_expect(e, c)` is `e`; `__builtin_constant_p(e)` is 1 if `e`
    is an integer constant expression, else 0, and `e` is not evaluated;
    `__builtin_prefetch` evaluates its first argument and does nothing
    else; `__builtin_trap()` and `__builtin_unreachable()` call `abort`;
    `__builtin_offsetof(type, designator)` is `offsetof`. Not provided:
    GCC's extended `asm` with operands, statement expressions, and the
    rest of its builtins.
15. **`long long` and `unsigned long long`**, as C99 has them (default
    mode only): 64-bit integers with every operator, conversion and
    constant form C99 gives them (§6). With them come `LLONG_MIN`, `LLONG_MAX` and
    `ULLONG_MAX` in `<limits.h>`; `int64_t`, `uint64_t` and their least and
    fast forms and limits in `<stdint.h>`, where `intmax_t` is then `long
    long`; `lldiv_t`, `llabs`, `lldiv`, `atoll`, `strtoll` and `strtoull`
    in `<stdlib.h>`; and the `ll` and `j` length modifiers in the `printf`
    and `scanf` families.

## 14. Conformance

No free, complete C89 conformance suite is known (Plum Hall, Perennial
and ACE SuperTest are commercial). agonc is complete when it passes
every usable test of three freely licensed suites: c-testsuite, GCC's C
torture execution tests and SDCC's regression tests, about 2,700 programs.
A test that is C89 runs in strict mode and must pass there; one that is
not (it uses `//` comments, GCC's spellings, or a later standard's
feature the default mode has) runs in the default mode. "Usable"
excludes, test by test and each with its reason, tests that need a later
C standard or a GNU extension that is not provided, an `int` of 32 bits,
or something MOS cannot do; the suites are fetched at pinned revisions and
never edited. Our own tests cover what
they do not: the library, clause by clause against the standard, the
required diagnostics, and the translation limits. Every remaining
deviation from C89 is listed in this section; there are none known.

Every usable C89 test passes in strict mode:
c-testsuite 137 of 137, GCC 577 of 577, SDCC 644 of 644 files. In the
default mode what fails needs a feature agonc does not provide (GCC's
extended `asm`, compound literals, statement expressions, variable-length
arrays, calls to undeclared functions) or makes a host's assumption.

## 15. The library (G.3.14)

All fifteen C89 headers: `<assert.h>`, `<ctype.h>`, `<errno.h>`,
`<float.h>`, `<limits.h>`, `<locale.h>`, `<math.h>`, `<setjmp.h>`,
`<signal.h>`, `<stdarg.h>`, `<stddef.h>`, `<stdio.h>`, `<stdlib.h>`,
`<string.h>`, `<time.h>`. Also `<stdint.h>`, as an extension: the
exact-width types for the widths that exist (`int8_t`, `int16_t`,
`int24_t`, `int32_t`, in the default mode `int64_t`, and unsigned forms),
the least and fast types, `intptr_t` (`int`), `intmax_t` (`long long` in
the default mode, `long` in strict mode), and their limits and constant
macros. A function the program does not call is not in
its image (ABI §6, object_format.md), so a program pays only for what it
uses, floating-point formatting in `printf` included.

**Basics.** `NULL` is `((void *)0)`. `assert` failing prints `Assertion
failed: expression, file name, line n` on `stderr` and calls `abort`.
`abort` raises `SIGABRT`; unless a handler leaves by `longjmp`, it then
closes every open file without writing what its stream's buffer holds (so
no MOS file handle is lost; MOS does not close a program's files), and
ends the program with status 134. `exit` runs the `atexit` functions in reverse order, flushes and closes
every open stream, removes the files `tmpfile` created, and ends the
program; `EXIT_SUCCESS` is 0 and `EXIT_FAILURE` is **200** (MOS prints its
own, unrelated messages for statuses 1–25, and 26 on MOS 3). Any other status is passed to
MOS unchanged, modulo 2²⁴.

**Characters.** In the "C" locale, the only one: `isalpha` and the others
are true only for ASCII characters (bytes 0x80–0xFF are none of alpha,
digit, space, punctuation, control or printable); `iscntrl` is 0x00–0x1F
and 0x7F; `isprint` is 0x20–0x7E.

**Memory.** `malloc(0)` and `calloc` with a zero count or size return a
unique pointer (to a one-byte block) that may be passed to `free`;
`realloc(p, 0)` frees `p` and returns `NULL`. The heap grows up from the end of the
program towards the stack and never within 256 bytes of it (ABI §8).

**Files and streams** (MOS's FAT file system):

- A **text stream** (opened without `b`) writes `'\n'` as CR LF and reads
  CR LF as `'\n'`; a lone CR or LF read is passed through. A **binary
  stream** (`b`) reads and writes every byte unchanged. The console is a
  text stream: `'\n'` on `stdout` or `stderr` moves to the start of the next
  line.
- The last line of a text stream need not end in a newline. Spaces written
  before a newline are read back. No null characters are appended to a
  binary stream.
- A stream opened for append starts at the end of the file, and every write
  goes to the end. A write to a text stream does not truncate the file
  beyond it.
- Files are fully buffered (`BUFSIZ` 4096); the console is flushed at every
  newline and at the end of every output call; `setvbuf` and `setbuf` are
  honoured. A zero-length file exists.
- **File names** are MOS's: FAT names, long names allowed, folders separated
  by `/`, case-insensitive; `FILENAME_MAX` is 256. The same file should not
  be open in two streams at once (MOS does not prevent it, and the results
  are undefined). `remove` of a file a stream has open succeeds (MOS's FAT
  file system does no locking), and that stream's later writes are lost;
  close it first. `rename` fails if the new name exists. Failures set
  `errno`: `ENOENT` (no such file), `EEXIST`, `EINVAL` (a bad `fopen`
  mode), `EMFILE` (no stream free), `EIO`, and `ENOMEM` from `malloc`.
- `FOPEN_MAX` is 8, counting the three standard streams (MOS allows 8 open
  files, and the console streams use none of them, so up to 8 files may be
  open besides them when nothing else, such as the compiler's driver, holds
  handles).
- `tmpfile` creates its file in `/tmp`, which must exist (the compiler's
  SD-card tree has it); `tmpnam` names files `/tmp/tNNNNN.tmp`; `L_tmpnam` is
  16 and `TMP_MAX` 65535.
- `%p` in `printf` prints the address as six lower-case hexadecimal digits
  (`040a3c`); `%p` in `scanf` reads the same form. A `-` in a `%[` scan
  list that is neither first nor last, between a character and one not
  below it, denotes a range (`%[a-z]`, `%[0-9A-F]`); otherwise it is an
  ordinary member (`%[-a]`, `%[a-]`, and `%[z-a]`, the three characters).
- `printf` prints an infinity as `inf` and a NaN as `nan`, after a `-` if
  the sign bit is set (`INF` and `NAN` for `%E %F %G`); the `0` flag pads
  them with spaces. `strtod`, `atof` and `scanf` read C89's forms only (no
  `inf`, `nan` or hexadecimal). `strtod` sets `errno` to `ERANGE` when the
  result overflows to `HUGE_VAL` or underflows to zero.
- `fgetpos` and `ftell` set `errno` to `EBADF` on failure (a console stream,
  or a closed one). `perror(s)` prints `s: message` and a newline on
  `stderr`, or only the message if `s` is null or empty. `strerror` gives
  one short English message per `errno` value.

**Mathematics.** `HUGE_VAL` is an infinity. A domain error returns a NaN
and sets `errno` to `EDOM`; a result too large returns `±HUGE_VAL` and sets
`ERANGE`; one too small to be anything but zero returns zero and also sets
`ERANGE` (a subnormal result does not). `fmod(x, 0)`, and `sin`, `cos` and
`tan` of an infinity, are domain errors; so is `pow(0, y)` for negative `y`,
which returns `HUGE_VAL`. A NaN argument gives a NaN without an error.

**Signals.** `<signal.h>` defines `SIGABRT` (6), `SIGFPE` (8), `SIGILL` (4),
`SIGINT` (2), `SIGSEGV` (11) and `SIGTERM` (15). MOS has no signals: `raise`
and `abort` (which raises `SIGABRT`) generate them, and **Ctrl-C** raises
`SIGINT`. A keyboard handler only notes the key; the library raises the
signal at its next interruption point: a stream's buffer written or
refilled, a console input line returned (so at a prompt it takes effect
when the line is entered), or the start of a call in `<agon/mos.h>`
(`mos_getkey` waiting for a key acts on it at the next call). Nothing is
delivered from the interrupt itself, and compiled code has no checks of
its own, so a loop that does no I/O cannot be stopped this way. A signal's
default action flushes and closes
every stream (`SIGABRT`'s closes them without flushing) and ends the program
with status 128 plus its number, without running the `atexit` functions;
`SIG_IGN` ignores it. `signal` accepts only these six. Before a handler is
called its disposition is reset to `SIG_DFL`, and nothing is blocked.

**Environment.** `getenv` returns MOS 3's system variable of that name when
the program runs on MOS 3 or later, and `NULL` on MOS 2 (which has none).
`system(NULL)` returns non-zero; `system(command)` passes the command to MOS
as if typed at its prompt, and returns MOS's status for it. MOS runs its
built-in commands and moslets that way, but not a program in `/bin`, since
the calling program occupies the memory such a program would load into.

**Time.** `time` reads the Agon's real-time clock, or returns
`(time_t)-1` if it has never been set (MOS then reports day 0 of 1980);
`time_t` is `long` seconds since 1970-01-01 00:00:00, so it covers
1901-12-13 to 2038-01-19. MOS has no time zones, so the clock is
taken to be local time and UTC alike: `gmtime` and `localtime` agree, and
`tm_isdst` is −1. `clock` counts in hundredths of a second from MOS's timer
since the program started (`CLOCKS_PER_SEC` 100).

**Locale.** Only the "C" locale; `setlocale` accepts `"C"` and `""` (which
is the same) and refuses anything else. The decimal point is `.`.

**Errors.** `errno` values: `EDOM` 33, `ERANGE` 34, `EBADF` 9, `ENOENT` 2,
`ENOMEM` 12, `EINVAL` 22, `EIO` 5, `EEXIST` 17, `EMFILE` 24 (the usual Unix
numbers).

**Agon extensions** (`<agon/...>`, not part of C89): `<agon/mos.h>` (the MOS
calls of MOS 2.3.3, its FatFS calls, the system variables, keyboard and
interrupt handlers), `<agon/uart.h>` (the second serial port, UART1:
`uart_open`, `uart_close`, `uart_getc`, `uart_putc`, `uart_write`) and
`<agon/vdp.h>` (every documented VDU command: text, graphics, bitmaps
and sprites, audio, the buffered command API, and the system commands).
The headers are their reference.
