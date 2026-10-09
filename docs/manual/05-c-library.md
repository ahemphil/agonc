# The C library

agonc comes with C99's library but for `<complex.h>`, `<fenv.h>`,
`<tgmath.h>`, `<wchar.h>` and `<wctype.h>`: C89's fifteen headers with
every function and macro they declare, C99's additions to them, and C99's
`<stdint.h>`, `<stdbool.h>`, `<inttypes.h>` and `<iso646.h>`. With `-ansi`
it is the C89 library alone, and the four new headers. This chapter is a
reference to it. It
does not explain the standard library itself; for each header it lists what
is there and then says what is particular to agonc and the Agon: the limits,
the choices the standard leaves to the implementation, and the places where
MOS shapes the behaviour. The formal summary is section 15 of
[the language specification](../c89_spec.md#15-the-library-g314). The
interface to MOS and the VDP beyond the standard library is in
[the Agon library](06-agon-library.md).

## How the library is linked

The library lives in `/lib/agonc` on the SD card, with the headers:

| File | Contents | Linked |
|---|---|---|
| `crt0.s` | start-up: the stack, `argv`, the Ctrl-C handler, the return to MOS | always |
| `rt.s` | arithmetic helpers, `setjmp`, `longjmp` | always |
| `libc.s` | the C library, without floating point and `long long` | always |
| `libm.s` | `float`, `double` and `long long` arithmetic and conversions, `<math.h>`, the `long long` functions | when needed |
| `libagon.s` | the rest of the MOS and VDP interface | when needed |

`libm.s` holds everything with a floating-point or `long long` value in it:
the arithmetic helpers the compiler calls for those types, the `%e %f %g
%a` and `%lld` conversions of `printf` and `scanf`, the conversion behind
`strtod`, `strtof` and `atof`, `<math.h>`, and `strtoll` with the other
`long long` and `intmax_t` functions. The driver adds it when any function in the program
has a `float`, `double` or `long long` value, so a program that uses none
is linked without it; `-lm` adds it regardless, for a program whose only
such values are in assembly. `-nostdlib` leaves out all five files. See
[Libraries](02-using-agonc.md#libraries).

Within a library, the linker takes only the functions the program calls,
and what they call in turn. A program pays for what it uses: `printf` alone
brings in the integer conversions, and the floating-point ones come only
with floating point.

### What is there from C99

The library is C89's, with C99's additions. `-ansi` hides the
additions' names, as a C89 program may use them for its own functions; the
four C99 headers stay, without what needs `long long`.

| Addition | Default mode | Strict mode |
|---|---|---|
| `<stdint.h>`, `<inttypes.h>` | yes, with 64-bit types | yes, without 64-bit types |
| `<stdbool.h>` | yes | yes, with `bool` an `int` |
| `<iso646.h>` | yes | yes |
| `LLONG_MIN`, `LLONG_MAX`, `ULLONG_MAX` | yes | no |
| `llabs`, `lldiv`, `lldiv_t`, `atoll`, `strtoll`, `strtoull` | yes | no |
| `imaxabs`, `imaxdiv`, `imaxdiv_t`, `strtoimax`, `strtoumax` | yes | no |
| `snprintf`, `vsnprintf`, `vscanf`, `vfscanf`, `vsscanf` | yes | no |
| `strtof`, `strtold`, `_Exit`, `isblank`, `va_copy` | yes | no |
| `printf` and `scanf` length modifiers `ll` and `j` | yes | no |
| C99's `<math.h>`: its functions, their `float` and `long double` forms, the macros | yes | no |
| `printf` and `scanf` length modifiers `hh`, `z` and `t`, and `%a` | yes | yes |
| hexadecimal, `inf` and `nan` read by `strtod`, `atof` and `scanf` | yes | no, as C89 requires |
| `%F` in `printf` and `scanf` | yes | yes |
| `%e` in `strftime` | yes | yes |

C99's `<math.h>` is all there in the default mode
([below](#c99s-additions)). Not planned: `<complex.h>`, `<fenv.h>`,
`<tgmath.h>`, `<wchar.h>` and `<wctype.h>`.

## The library on the Agon

### File names

The file functions use MOS's FAT file system on the SD card. A name is
MOS's: long names are allowed, folders are separated by `/`, case does not
matter, and a name without a leading `/` is relative to MOS's current folder
(the one set by `cd`). `FILENAME_MAX` is 256.

MOS's file system does not lock files. Opening the same file in two streams
at once is not prevented, and what happens then is undefined. `remove`
deletes a file that a stream has open, and that stream's later writes are
lost; close the stream first.

### Eight handles for the whole session

MOS has eight file handles, shared by everything that runs until the Agon
is reset, and it does **not** close a program's files when the program
ends. A file left open keeps its handle, and after eight such files nothing
can open a file at all until a reset.

The library therefore closes every stream on every way out it controls:
`exit` and a return from `main` flush and close them, and so do `abort` and
the default action of a signal (`abort` without writing out what the
buffers hold). A program that leaves any other way, such as by crashing or
by jumping into MOS, loses the handles of the files it had open; restart the
Agon to get them back.

The three standard streams use no handle, and the library has room for
eight files besides them (`FOPEN_MAX`, 8, counts the standard streams, as
C asks, so it understates this). `fopen` fails with `EMFILE` when all eight
of its slots are in use, and with `EIO` when MOS itself has no handle left
because something else holds them.

### Text and binary streams

A file opened without `b` in its mode is a **text** stream: each `'\n'`
written goes to the file as CR LF, and CR LF read from the file comes back
as `'\n'`. A lone CR or a lone LF is read unchanged, so files with LF
endings read correctly too. A file opened with `b` is a **binary** stream,
and every byte is read and written unchanged.

On both kinds, `ftell` and `fseek` work in byte offsets within the file, so
on a text stream a position counts the CR of each CR LF. The last line of a
text file need not end in a newline. Nothing is appended to a binary file.

### The console

`stdin`, `stdout` and `stderr` are all the Agon's console: the keyboard and
the screen. `stdout` and `stderr` are text streams, so `'\n'` moves to the
start of the next line, as the VDP needs a CR LF to do that. Everything
written to them goes to the VDP as it is, so a byte below 32 is a VDU
command rather than a character: `putchar(12)` clears the screen. The VDU
commands are listed in the
[Agon documentation](https://agonplatform.github.io/agon-docs/vdp/VDU-Commands/)
and given names in [the Agon library](06-agon-library.md).

Console output is buffered, 128 bytes at a time, but the buffer is written
out at every newline and at the end of every output call, so a prompt
printed without a newline appears at once and text never waits in the
buffer. `setvbuf` can make the console fully buffered, which is then
written only when the buffer fills, at `fflush`, or at exit. A console
stream cannot be positioned: `fseek`, `ftell` and `fgetpos` fail on it
with `EBADF`.

### Reading the keyboard

`stdin` reads a line at a time through MOS's line editor
([MOS documentation](https://agonplatform.github.io/agon-docs/MOS/#the-mos-line-editor)).
When the program first needs a character, the editor starts on an empty
line where the user can type and edit, until Return (or Escape) ends it.
The library then moves the cursor to the next line and hands the program
the line's characters followed by `'\n'`. A line holds at most 254
characters. Only when the program has read the whole line, newline
included, does the editor run again.

The console never reports end of file. A loop such as
`while ((c = getchar()) != EOF)` that reads from the keyboard never ends by
itself; give the user an explicit way out instead, such as an empty line or
a word that ends the input.

### Ctrl-C

Pressing Ctrl-C raises `SIGINT`, whose default action closes the streams
and ends the program with status 130. The key does not stop the program at
once: the library acts on it at its next interruption point, which is the
next time a stream's buffer is written or refilled, a line typed at the
keyboard is returned, or a MOS call is made through `<agon/mos.h>`. Compiled
code has no checks of its own, so a loop that does no input or output
cannot be stopped this way. The details are under
[`<signal.h>`](#signalh).

### The clock

`time` reads the Agon's real-time clock through MOS, whose `*TIME` command
sets it
([star commands](https://agonplatform.github.io/agon-docs/mos/Star-Commands/)).
A clock that has never been set reports day 0 of 1980, which is no date,
and `time` then returns `(time_t)-1`. MOS has no time zones, so the clock
is taken to be both local time and UTC. `clock` counts hundredths of a second from MOS's
timer since the program started. See [`<time.h>`](#timeh).

### errno

`errno` is an `int` variable. Its values are the usual Unix numbers
(`ENOENT` is 2, `EDOM` 33), and each has a short English message from
`strerror`. The functions that set it, and when, are listed under
[`<errno.h>`](#errnoh). A function may change `errno` even when it
succeeds (`fopen` does), so test `errno` only after a function has reported
a failure.

### Exit status

The status a program returns from `main` or passes to `exit` goes to MOS.
0 is success. `EXIT_FAILURE` is **200**, not 1: MOS prints a message of its
own for statuses 1 to 25, and 26 on MOS 3 (for 1, "Error accessing SD card"), which would
mislead the user, and 200 is also the status agonc itself returns when a
build fails. `abort` ends a program with 134, and a signal's default action
with 128 plus the signal's number (130 for Ctrl-C). Any non-zero status
stops a MOS `exec` script.

### The heap

`malloc`'s heap grows up from the end of the program's code and data
towards the stack, and the two share the memory between them with no fixed
split ([Memory](03-the-language.md#memory)). `malloc` refuses to come
within 256 bytes of the stack pointer and returns a null pointer instead,
setting `errno` to `ENOMEM`. A stack that grows down into the heap is not
detected.

Each block has a 4-byte header and no alignment padding (the eZ80 reads any
object at any address). The allocator takes the first free block that is
large enough, splits off what is left, and on `free` merges neighbouring
free blocks; a free block at the top of the heap is given back to the
space shared with the stack. `free` does not check its argument: freeing a
pointer twice, or one that `malloc` did not return, damages the heap.

The streams take their buffers from the heap when first used: 4096 bytes
for each open file, 128 for console output and 256 for keyboard input. A
file's buffer is freed by `fclose`.

## `<assert.h>`

| Macro | What it does |
|---|---|
| `assert(expr)` | if `expr` is zero, reports the failure and calls `abort` |

A failed assertion prints

```text
Assertion failed: expr, file name, line n
```

on `stderr` and calls `abort`, which ends the program with status 134,
closing the files. With `NDEBUG` defined where `<assert.h>` is included,
`assert` does nothing and does not evaluate its argument. The header has no
include guard, so including it again after changing `NDEBUG` redefines
`assert` accordingly.

## `<ctype.h>`

| Function | True for, or returns |
|---|---|
| `int isalnum(int c)` | a letter or a digit |
| `int isalpha(int c)` | `A`-`Z`, `a`-`z` |
| `int isblank(int c)` | space and `\t` (C99; default mode) |
| `int iscntrl(int c)` | 0 to 31, and 127 |
| `int isdigit(int c)` | `0`-`9` |
| `int isgraph(int c)` | 33 to 126: printable, not space |
| `int islower(int c)` | `a`-`z` |
| `int isprint(int c)` | 32 to 126, space included |
| `int ispunct(int c)` | printable, not space, not a letter or digit |
| `int isspace(int c)` | space, `\t \n \v \f \r` (9 to 13) |
| `int isupper(int c)` | `A`-`Z` |
| `int isxdigit(int c)` | `0`-`9`, `a`-`f`, `A`-`F` |
| `int tolower(int c)` | the lower-case letter for `A`-`Z`, else `c` |
| `int toupper(int c)` | the upper-case letter for `a`-`z`, else `c` |

The classes are those of 7-bit ASCII in the "C" locale, the only locale.
Bytes 0x80 to 0xFF, `EOF`, and any negative value are in no class, and
`tolower` and `toupper` return them unchanged. These are functions, not
macros, and each class is a range test rather than a table lookup, so
passing a plain `char` holding a byte above 0x7F (which is negative, as
`char` is signed) is safe and simply gives false.

## `<errno.h>`

| Name | Value | `strerror` message | Set by |
|---|---:|---|---|
| `ENOENT` | 2 | no such file or directory | `fopen` (mode `r`), `remove`, `rename` |
| `EIO` | 5 | input/output error | `fopen` and `freopen`, when MOS refuses the open |
| `EBADF` | 9 | not an open file stream | `fseek`, `ftell`, `fgetpos`, `fsetpos` on a console or closed stream |
| `ENOMEM` | 12 | not enough memory | `malloc`, and `calloc` and `realloc` through it |
| `EEXIST` | 17 | file exists | `rename` |
| `EINVAL` | 22 | invalid argument | `fopen` and `freopen` (bad mode), `signal`, `fsetpos` |
| `EMFILE` | 24 | too many open files | `fopen`, `tmpfile` |
| `EDOM` | 33 | argument out of domain | `<math.h>` |
| `ERANGE` | 34 | result out of range | `<math.h>`, `strtod`, `atof`, `strtol`, `strtoul`, `strtoll`, `strtoull`, `atoll` |

`errno` is declared as `extern int errno;`. It is 0 when the program
starts, and no library function sets it to 0. `strerror(0)` is "no error";
any number not in the table gives "unknown error".

Some failures leave `errno` alone: `calloc` with a count and size whose
product is too large, `realloc` asked for more than the machine could ever
hold, `fseek` with a bad `whence` or a target before the start of the file,
and `raise` with an unknown signal. `fsetpos` sets `EINVAL` when its seek
fails and `errno` is still 0. `remove` and `rename` report any failure from
MOS as `ENOENT`.

## `<float.h>`

`float` is IEEE 754 binary32; `double` and `long double` are both IEEE 754
binary64.

| Macro | `float` (`FLT_`) | `double` (`DBL_`) |
|---|---|---|
| `_MANT_DIG` | 24 | 53 |
| `_DIG` | 6 | 15 |
| `_EPSILON` | 1.19209290e-07F | 2.2204460492503131e-16 |
| `_MIN` | 1.17549435e-38F | 2.2250738585072014e-308 |
| `_MAX` | 3.40282347e+38F | 1.7976931348623157e+308 |
| `_MIN_EXP` | -125 | -1021 |
| `_MAX_EXP` | 128 | 1024 |
| `_MIN_10_EXP` | -37 | -307 |
| `_MAX_10_EXP` | 38 | 308 |

The `LDBL_` macros have the `DBL_` values (with an `L` suffix on the
floating ones). `FLT_RADIX` is 2 and `FLT_ROUNDS` is 1, round to nearest.

### Floating-point arithmetic

The eZ80 has no floating-point unit, so every operation on a `float` or
`double` is a call into `libm.s`, done in software (see
[Floating point and long long cost time](03-the-language.md#floating-point-and-long-long-cost-time)).
The software is exact IEEE 754:

- Addition, subtraction, multiplication, division and `sqrt` are correctly
  rounded, to nearest with ties to even, the only rounding mode. `float`
  arithmetic is done in `float`, `double` arithmetic in `double`.
- Subnormal numbers, infinities, NaNs and signed zeros behave as IEEE 754
  defines them. There are no exception flags and nothing traps: a division
  by zero gives an infinity or a NaN.
- Converting an integer to a floating type, or a `double` to a `float`,
  rounds to nearest, ties to even. Converting a floating value to an integer
  truncates toward zero; a value outside the integer type's range gives an
  undefined result, as C allows.
- Decimal conversion is correctly rounded everywhere: the compiler reading
  constants, `strtod`, `atof`, `scanf`, and `printf`'s `%e %f %g`. They
  share one algorithm, so a value printed with 17 significant digits reads
  back as the same bits.

The results are deterministic and the same, bit for bit, as a PC's IEEE
arithmetic, which makes a PC a good place to check them.

## `<inttypes.h>`

C99's header, provided in both modes. It includes `<stdint.h>`, and adds
the macros that give `printf` and `scanf` the right length modifier for
each of its types, and (default mode) four functions for `intmax_t`.

```c
int32_t n = 100000;
printf("%" PRId32 " items\n", n);      /* "%ld" here */
```

The `PRI` macros are for `printf`: `PRId`*N*, `PRIi`*N*, `PRIo`*N*,
`PRIu`*N*, `PRIx`*N* and `PRIX`*N* for N = 8, 16, 24, 32 and (default mode)
64, with the `LEAST` and `FAST` forms, `PTR` and `MAX`. The `SCN` macros
are for `scanf`, the same without `X`. The 8- and 16-bit types print as
the `int` they are promoted to, but scan with `hh` and `h`, which store
just their bytes.

| Function (default mode) | What it does |
|---|---|
| `intmax_t imaxabs(intmax_t j)` | `llabs` |
| `imaxdiv_t imaxdiv(intmax_t n, intmax_t d)` | `lldiv`, with members `quot` and `rem` |
| `intmax_t strtoimax(const char *s, char **end, int base)` | `strtoll` |
| `uintmax_t strtoumax(const char *s, char **end, int base)` | `strtoull` |

`intmax_t` is `long long`, so these are the `long long` functions under
other names. The wide-character forms, `wcstoimax` and `wcstoumax`, are
not provided.

## `<iso646.h>`

Macros that spell operators as words, for keyboards without the
characters: `and` (`&&`), `and_eq` (`&=`), `bitand` (`&`), `bitor` (`|`),
`compl` (`~`), `not` (`!`), `not_eq` (`!=`), `or` (`||`), `or_eq` (`|=`),
`xor` (`^`) and `xor_eq` (`^=`). Provided in both modes.

## `<limits.h>`

| Macro | Value |
|---|---|
| `CHAR_BIT` | 8 |
| `SCHAR_MIN`, `SCHAR_MAX` | -128, 127 |
| `UCHAR_MAX` | 255 |
| `CHAR_MIN`, `CHAR_MAX` | -128, 127 (`char` is signed) |
| `MB_LEN_MAX` | 1 |
| `SHRT_MIN`, `SHRT_MAX` | -32768, 32767 |
| `USHRT_MAX` | 65535 |
| `INT_MIN`, `INT_MAX` | -8388608, 8388607 (`int` is 24 bits) |
| `UINT_MAX` | 16777215 |
| `LONG_MIN`, `LONG_MAX` | -2147483648, 2147483647 |
| `ULONG_MAX` | 4294967295 |
| `LLONG_MIN`, `LLONG_MAX` | -9223372036854775808, 9223372036854775807 (default mode) |
| `ULLONG_MAX` | 18446744073709551615 (default mode) |

A program written for a 32-bit `int` most often goes wrong here: `INT_MAX`
is 8388607, and a count or a product that passes it needs `long`.

## `<locale.h>`

| Function | What it does |
|---|---|
| `char *setlocale(int category, const char *locale)` | selects a locale; only `"C"` exists |
| `struct lconv *localeconv(void)` | the current locale's number formatting |

The macros `LC_ALL`, `LC_COLLATE`, `LC_CTYPE`, `LC_MONETARY`, `LC_NUMERIC`
and `LC_TIME` (0 to 5) and `NULL` are defined, and `struct lconv` has C89's
members.

Only the "C" locale exists. `setlocale` accepts `"C"` and `""` (the
native locale, which is the same) for any category and returns `"C"`; a
null `locale` asks which locale is in use and also returns `"C"`. Any other
name, or an unknown category, returns a null pointer. `localeconv`'s
`decimal_point` is `"."`, its other strings are empty, and its `char`
members are `CHAR_MAX`, meaning "not available".

## `<math.h>`

| Function | Returns |
|---|---|
| `double acos(double x)` | arc cosine, 0 to pi |
| `double asin(double x)` | arc sine, -pi/2 to pi/2 |
| `double atan(double x)` | arc tangent, -pi/2 to pi/2 |
| `double atan2(double y, double x)` | the angle of the point (x, y), -pi to pi |
| `double cos(double x)` | cosine |
| `double sin(double x)` | sine |
| `double tan(double x)` | tangent |
| `double cosh(double x)` | hyperbolic cosine |
| `double sinh(double x)` | hyperbolic sine |
| `double tanh(double x)` | hyperbolic tangent |
| `double exp(double x)` | e to the power x |
| `double log(double x)` | natural logarithm |
| `double log10(double x)` | base-10 logarithm |
| `double pow(double x, double y)` | x to the power y |
| `double sqrt(double x)` | square root |
| `double frexp(double x, int *e)` | x split into a fraction in [0.5, 1) and a power of 2 in `*e` |
| `double ldexp(double x, int e)` | x times 2 to the power e |
| `double modf(double x, double *ip)` | the fraction of x; its integer part to `*ip` |
| `double ceil(double x)` | the smallest integer not below x |
| `double floor(double x)` | the largest integer not above x |
| `double fabs(double x)` | the absolute value |
| `double fmod(double x, double y)` | the remainder of x / y, with x's sign |

`HUGE_VAL` is positive infinity.

**Accuracy.** The algorithms are fdlibm's. The results are within about one
unit in the last place: below one for most functions, below two for
`sinh`, `cosh` and `tanh`. `sqrt` is correctly rounded. `fabs`, `floor`,
`ceil`, `fmod`, `modf` and `frexp` are exact, and so is `ldexp` unless its
result is subnormal, when it is correctly rounded. Large arguments of
`sin`, `cos` and `tan` are reduced by pi/2 with all the bits they need (the
Payne-Hanek method), so `sin(1e300)` is as accurate as `sin(1.0)`. The
results are the same bits as fdlibm's on a PC.

**Errors.** A NaN argument gives a NaN and sets nothing. Otherwise:

| Case | Result | `errno` |
|---|---|---|
| argument outside the domain | a NaN | `EDOM` |
| result too large | `HUGE_VAL` or `-HUGE_VAL` | `ERANGE` |
| result too small to be anything but zero | 0 with the right sign | `ERANGE` |
| subnormal result | the subnormal value | unchanged |

The domain errors are `acos` and `asin` of a value beyond 1 in magnitude,
`sin`, `cos` and `tan` of an infinity, `log` and `log10` of a negative
number, `sqrt` of a negative number (`sqrt(-0.0)` is -0), `fmod` with `y`
zero or `x` infinite, `pow` of a negative number to a non-integer power,
and `pow(0, y)` with `y` negative, which returns `HUGE_VAL` (negative for
`-0` to an odd integer power) rather than a NaN. `log(0)` and `log10(0)`
return `-HUGE_VAL` with `ERANGE`. `atan2(0, 0)` is not an error: it gives 0
or pi with the signs IEEE 754 prescribes. `exp`, `pow` and `ldexp` can
overflow or underflow, and `sinh` and `cosh` can overflow; `pow(x, 0)` is 1
for every `x`, even a NaN.

### C99's additions

In the default mode `<math.h>` has the whole of C99's 7.12, in the
library units `math99.c` and `mathf.c`:

| Kind | Functions |
|---|---|
| exponentials, logarithms | `exp2`, `expm1`, `log1p`, `log2`, `logb`, `ilogb`, `scalbn`, `scalbln` |
| powers | `cbrt`, `hypot` |
| hyperbolic | `asinh`, `acosh`, `atanh` |
| error and gamma | `erf`, `erfc`, `lgamma`, `tgamma` |
| to integers | `trunc`, `round`, `lround`, `llround`, `rint`, `lrint`, `llrint`, `nearbyint` |
| remainders | `remainder`, `remquo` |
| the rest | `copysign`, `nan`, `nextafter`, `nexttoward`, `fdim`, `fmax`, `fmin`, `fma` |

Every function, C89's and C99's, also has a `float` form and a `long
double` form: `sinf`, `sinl` and so on. `long double` is `double`, so the
`l` forms are the double functions. A `float` form works in `double` and
rounds once, so its results are nearly always the correctly rounded
`float`. The macros are there too: `fpclassify`, `isfinite`, `isinf`,
`isnan`, `isnormal` and `signbit`, which take a `float` or a `double`; the
comparisons `isgreater` and the rest, which are quiet for a NaN; `INFINITY`,
`NAN`, `HUGE_VALF`, `HUGE_VALL`, `FP_NAN` and the other classes, `FP_ILOGB0`,
`FP_ILOGBNAN`, `float_t` and `double_t` (`float` and `double`), and
`math_errhandling`, which is `MATH_ERRNO`: there is no floating-point
environment, `<fenv.h>`, so errors are reported in `errno` only, and the
rounding is always to nearest.

**Accuracy.** Most of the algorithms are fdlibm's again. `exp2`, `log2`,
`tgamma`, `remquo` and `fma` are agonc's own, built on the others: `fma`
forms its product exactly and rounds once, as C99 asks. Measured against
true values, the errors are below one unit in the last place for most
functions, below two for `acosh`, `atanh`, `erfc` and `lgamma`, and below
five for `tgamma`. `lgamma` of a negative argument near one of its zeros
(near -2.457, say) has a small result made as the difference of two large
ones, so its relative error grows there. The rounding functions, `fma`,
`remainder`, `remquo`, `fdim`, `fmax`, `fmin`, `nextafter`, `copysign`,
`logb` and `ilogb` are exact.

**Errors** follow the table above: `log1p(-1)`, `log2(0)`, `logb(0)`,
`atanh(+-1)`, `lgamma` and `tgamma` at their poles (0 and the negative
integers for `lgamma`, +-0 for `tgamma`) give an infinity and `ERANGE`;
`tgamma` of a negative integer, `ilogb` of 0, an infinity or a NaN, and
`fma` of an infinity times 0 are domain errors. `nextafter` sets `ERANGE`
when its result is subnormal, zero or infinite. A `float` form also sets
`ERANGE` when only the `float` overflows or underflows. The integer
results of `lrint`, `lround` and their `ll` forms are unspecified when the
value does not fit, as in C99.

## `<setjmp.h>`

| Name | What it does |
|---|---|
| `jmp_buf` | an array type that holds a place to return to |
| `int setjmp(jmp_buf env)` | records the place; returns 0 |
| `void longjmp(jmp_buf env, int val)` | returns from that `setjmp` again, with `val` |

`setjmp` and `longjmp` are in `rt.s`, so they cost nothing to link. A
`jmp_buf` holds three addresses: the return address, the frame pointer and
the stack pointer. `longjmp(env, 0)` makes `setjmp` return 1, as C
requires. The function that called `setjmp` must not have returned.

agonc keeps no variable in a register from one statement to the next, so
after a `longjmp` every local variable has the value last stored in it,
whether it is `volatile` or not. Portable code should still declare such
variables `volatile`.

## `<signal.h>`

| Name | What it does |
|---|---|
| `void (*signal(int sig, void (*func)(int)))(int)` | sets how a signal is handled; returns the previous handler |
| `int raise(int sig)` | sends a signal to the program |
| `sig_atomic_t` | `int` |
| `SIG_DFL`, `SIG_IGN` | the default action, and ignoring the signal |
| `SIG_ERR` | what `signal` returns on failure |

| Signal | Number | Raised by |
|---|---:|---|
| `SIGINT` | 2 | Ctrl-C, or `raise` |
| `SIGILL` | 4 | `raise` only |
| `SIGABRT` | 6 | `abort`, or `raise` |
| `SIGFPE` | 8 | `raise` only |
| `SIGSEGV` | 11 | `raise` only |
| `SIGTERM` | 15 | `raise` only |

MOS has no signals of its own. The six signals of C89 are the only ones;
`signal` with any other number returns `SIG_ERR` and sets `errno` to
`EINVAL`, and `raise` with one returns -1. Otherwise `raise` returns 0
once the handler has returned or the signal has been ignored. A division
by zero or a bad pointer raises nothing.

**Default action.** Every signal's default action flushes and closes the
streams (`SIGABRT`'s closes them without flushing), removes `tmpfile`'s
files, and ends the program with status 128 plus the signal's number,
without running the `atexit` functions. `SIG_IGN` ignores the signal.

**Handlers.** Before a handler is called, the signal's handling goes back
to `SIG_DFL`, so a handler that wants the next signal too must call
`signal` again. Nothing is blocked while a handler runs. A handler may
return, and the program carries on from where the signal was raised, or it
may leave by `longjmp` or `exit`. If `SIGABRT`'s handler returns, `abort`
still ends the program.

**Ctrl-C.** A keyboard handler that the start-up code installs only notes
the key. The library raises `SIGINT` at its next interruption point:

- a stream's buffer written or refilled (for the console, that is every
  output call, and every line read);
- a line typed at the keyboard being returned, so at a prompt Ctrl-C takes
  effect when the line is entered;
- the start of a MOS call made through `<agon/mos.h>` (all but those that
  only read MOS's system variables, such as `mos_sysvars`); `mos_getkey`,
  waiting for a key, acts on a Ctrl-C at its next call.

Nothing happens inside the keyboard interrupt itself, so the signal arrives
only inside a library call, at one of these points, and never in the middle
of `malloc` or of the program's own code. Compiled code contains no checks
of its own: a loop that does no input, output or MOS call cannot be
interrupted. A long calculation that should be stoppable can print its
progress now and then, since every output call to the console is an
interruption point.

## `<stdarg.h>`

| Name | What it does |
|---|---|
| `va_list` | `char *`, a cursor over the variable arguments |
| `va_start(ap, last)` | starts `ap` after the last named parameter |
| `va_arg(ap, type)` | the next argument, of `type` |
| `va_end(ap)` | ends the use of `ap` |

The macros are built into the compiler. `va_start` is accepted only in a
function declared with `...`, and only with its last named parameter. Every
argument takes a multiple of 3 bytes on the stack: an `int` or a pointer
takes 3, a `long` 6, and a `double` or `long long` 9. As in any C,
arguments of a variable list are promoted, so `va_arg(ap, int)`
reads a `char` or `short` argument and `va_arg(ap, double)` a `float`.
C99's `va_copy(dest, src)` (default mode) copies a `va_list`; as `va_list`
is a pointer, it is an assignment.

## `<stdbool.h>`

C99's `bool`, `true` (1), `false` (0) and `__bool_true_false_are_defined`
(1). In the default mode `bool` is `_Bool`, so storing any non-zero value
in one stores 1. In strict mode, which has no `_Bool`, `bool` is `int`, and
a value stored in one is kept as it is.

## `<stddef.h>`

| Name | What it is |
|---|---|
| `size_t` | `unsigned int`, 24 bits |
| `ptrdiff_t` | `int`, 24 bits |
| `wchar_t` | `int`, 24 bits |
| `NULL` | `((void *)0)` |
| `offsetof(type, member)` | the byte offset of `member` in the structure `type` |

Structures have no padding, so a member's offset is the sum of the sizes of
the members before it. `size_t` is large enough for any object the Agon can
hold.

## `<stdint.h>`

C99's header, provided in both modes; the 64-bit types and macros are left
out in strict mode, which has no `long long`.

| Types | Width | Underlying type |
|---|---:|---|
| `int8_t`, `uint8_t` | 8 | `signed char`, `unsigned char` |
| `int16_t`, `uint16_t` | 16 | `short`, `unsigned short` |
| `int24_t`, `uint24_t` | 24 | `int`, `unsigned int` |
| `int32_t`, `uint32_t` | 32 | `long`, `unsigned long` |
| `int64_t`, `uint64_t` | 64 | `long long`, `unsigned long long` (default mode) |
| `int_leastN_t`, `uint_leastN_t` | N | as `intN_t`, for N = 8, 16, 24, 32, 64 |
| `int_fastN_t`, `uint_fastN_t` | 24 or more | `int` for N = 8, 16, 24; then as `intN_t` |
| `intptr_t`, `uintptr_t` | 24 | `int`, `unsigned int` |
| `intmax_t`, `uintmax_t` | 64 | `long long` (strict mode: `long`, 32 bits) |

The 24-bit types are agonc's addition, since 24 bits is the eZ80's natural
width and the size of `int` and of a pointer. The fast types of 8 and 16
bits are `int` for the same reason: the eZ80 is fastest at 24 bits.

The limits are there for every type: `INTN_MIN`, `INTN_MAX` and
`UINTN_MAX`, the `LEAST` and `FAST` forms, `INTPTR_MIN`, `INTPTR_MAX`,
`UINTPTR_MAX`, `INTMAX_MIN`, `INTMAX_MAX`, `UINTMAX_MAX`, and also
`PTRDIFF_MIN`, `PTRDIFF_MAX`, `SIZE_MAX` (16777215), `WCHAR_MIN` and
`WCHAR_MAX`. The constant macros `INTN_C` and `UINTN_C` (N = 8 to 64),
`INTMAX_C` and `UINTMAX_C` add the right suffix to a constant. To print
these types, use [`<inttypes.h>`](#inttypesh)'s macros.

## `<stdio.h>`

The macros: `NULL`, `EOF` (-1), `BUFSIZ` (4096), `FOPEN_MAX` (8),
`FILENAME_MAX` (256), `L_tmpnam` (16), `TMP_MAX` (65535), `_IOFBF`,
`_IOLBF`, `_IONBF`, `SEEK_SET`, `SEEK_CUR`, `SEEK_END`, and `stdin`,
`stdout`, `stderr`. `fpos_t` is `long`, a byte offset. `FILE` is a
structure; its members are the library's own.

### Opening and closing

| Function | What it does |
|---|---|
| `FILE *fopen(const char *name, const char *mode)` | opens a file |
| `FILE *freopen(const char *name, const char *mode, FILE *f)` | closes `f` and opens a file in its place |
| `int fclose(FILE *f)` | writes out the buffer and closes the stream |

A mode is `r`, `w` or `a`, followed by `b`, `+`, both or neither, in either
order; any other mode, `t` included, fails with `EINVAL`. `w` creates the
file or empties it; `a` creates it if need be, and every write goes to its
end, wherever the stream has been positioned. A `+` mode can both read and
write. C requires an `fflush` or a positioning call between reading and
writing; agonc's streams do not need one, but a portable program should
still make it. `fopen` of a file that does not exist with mode
`r` sets `ENOENT`; any other refusal by MOS sets `EIO`.

`freopen` works on the standard streams too: `freopen("log.txt", "w",
stdout)` sends `stdout` to a file for the rest of the program. Errors in
closing the old stream are ignored.

`fclose` returns `EOF` if the last bytes could not be written or the stream
had an error, and closes the stream either way. The streams still open at
exit are closed by `exit` ([eight handles](#eight-handles-for-the-whole-session)).

### Buffering

| Function | What it does |
|---|---|
| `int fflush(FILE *f)` | writes out what `f`'s buffer holds; every stream's if `f` is null |
| `int setvbuf(FILE *f, char *buf, int mode, size_t size)` | sets the buffer and the buffering mode |
| `void setbuf(FILE *f, char *buf)` | `setvbuf` with `_IOFBF` and `BUFSIZ`, or `_IONBF` if `buf` is null |

Files are fully buffered, in 4096-byte buffers from `malloc`; the console
is described [above](#the-console). `setvbuf` accepts `_IOFBF`, `_IOLBF`
(written at each newline) and `_IONBF` (written at once), with the
program's own buffer of `size` bytes, or with `buf` null for one from
`malloc` of `size` bytes (4096 if `size` is 0). It returns non-zero for a
closed stream or an unknown mode. C asks for `setvbuf` before any other use
of the stream; agonc's also works later, writing out any buffered output
first.

`fflush` on a stream being read does nothing and returns 0. A failure to
write returns `EOF` and sets the stream's error indicator.

### Characters and lines

| Function | What it does |
|---|---|
| `int fgetc(FILE *f)` | the next character, or `EOF` |
| `int getc(FILE *f)` | the same, as a macro |
| `int getchar(void)` | `getc(stdin)` |
| `int ungetc(int c, FILE *f)` | pushes one character back |
| `int fputc(int c, FILE *f)` | writes one character |
| `int putc(int c, FILE *f)` | the same, as a macro |
| `int putchar(int c)` | `putc(c, stdout)` |
| `char *fgets(char *s, int n, FILE *f)` | reads a line of at most `n - 1` characters, keeping the newline |
| `char *gets(char *s)` | reads a line from `stdin`, dropping the newline |
| `int fputs(const char *s, FILE *f)` | writes a string |
| `int puts(const char *s)` | writes a string and a newline to `stdout` |

`getc`, `putc`, `getchar` and `putchar` are macros that read or write the
buffer directly and call the functions only when it is empty or full, so
they are faster per character than `fgetc` and `fputc`. `getc` and
`putc` evaluate their stream argument more than once, as C permits; each is
also a function, reached with `#undef` or by taking its address.

`ungetc` keeps one character, which may differ from the one read; a second
`ungetc` before a read fails. It clears the end-of-file indicator. `fgets`
with `n` below 2 returns a null pointer. `gets` cannot know the size of its
buffer and is kept only because C89 has it; a keyboard line is at most 254
characters, but a file's lines can be any length. `fputs` and `puts`
return 0 on success, `EOF` on an error.

### Blocks

| Function | What it does |
|---|---|
| `size_t fread(void *p, size_t size, size_t n, FILE *f)` | reads up to `n` elements of `size` bytes |
| `size_t fwrite(const void *p, size_t size, size_t n, FILE *f)` | writes `n` elements of `size` bytes |

Both return the number of whole elements transferred. On a binary file,
`fread` reads a request of a buffer's size or more straight into the
program's memory, without copying. On a text stream both translate
newlines, so `fread` of a text file can return fewer bytes than the file
holds.

### Positioning

| Function | What it does |
|---|---|
| `int fseek(FILE *f, long offset, int whence)` | moves to `offset` from the start, the current position or the end |
| `long ftell(FILE *f)` | the current position |
| `void rewind(FILE *f)` | moves to the start and clears the error indicator |
| `int fgetpos(FILE *f, fpos_t *pos)` | stores the current position |
| `int fsetpos(FILE *f, const fpos_t *pos)` | returns to a stored position |

Positions are byte offsets from the start of the file, text streams
included, and `fpos_t` is the same `long` that `ftell` returns. `fseek`
accepts any offset on a text stream, not only one that `ftell` returned.
A position beyond the end is allowed: reading there meets end of file, and
writing there extends the file. A position before the start fails. A
successful `fseek` clears the end-of-file indicator and any `ungetc`
character. On the console all of these fail, with `EBADF` (`rewind` has
no way to report it).

### Errors

| Function | What it does |
|---|---|
| `void clearerr(FILE *f)` | clears the end-of-file and error indicators |
| `int feof(FILE *f)` | non-zero if a read has met the end of the file |
| `int ferror(FILE *f)` | non-zero if an operation on `f` has failed |
| `void perror(const char *s)` | prints `s`, a colon and `strerror(errno)` on `stderr` |

`perror(s)` prints `s: message` and a newline, or only the message and a
newline when `s` is null or empty. Writing to a stream opened only for
reading, or reading one opened only for writing, sets its error indicator.

### Files

| Function | What it does |
|---|---|
| `int remove(const char *name)` | deletes a file |
| `int rename(const char *old, const char *new)` | renames a file |
| `FILE *tmpfile(void)` | creates a temporary file, removed when it is closed |
| `char *tmpnam(char *s)` | makes a name that no file has |

`remove` and `rename` return 0 on success and -1 on failure. `rename`
refuses to replace an existing file (`EEXIST`); remove the old one first if
that is what you want.

`tmpnam` makes names of the form `/tmp/tNNNNN.tmp`, a different one at each
call, skipping any name whose file exists, and returns a null pointer if
it finds none in `TMP_MAX` tries. It writes the name into `s` (which needs
`L_tmpnam` bytes, 16) or, with `s` null, into a buffer of its own that the
next call overwrites. `tmpfile` opens such a file with mode
`wb+` and removes it when the stream is closed, by `fclose`, `exit`,
`abort` or a signal's default action. Both need the folder `/tmp`, which
the agonc installation creates. A program that ends any other way leaves
its temporary file behind in `/tmp`.

### Formatted output

| Function | Writes to |
|---|---|
| `int printf(const char *fmt, ...)` | `stdout` |
| `int fprintf(FILE *f, const char *fmt, ...)` | `f` |
| `int sprintf(char *s, const char *fmt, ...)` | memory at `s`, with a terminating null |
| `int vprintf(const char *fmt, va_list ap)` | `stdout`, from a `va_list` |
| `int vfprintf(FILE *f, const char *fmt, va_list ap)` | `f`, from a `va_list` |
| `int vsprintf(char *s, const char *fmt, va_list ap)` | memory, from a `va_list` |
| `int snprintf(char *s, size_t n, const char *fmt, ...)` | memory, at most `n` bytes with the null (default mode) |
| `int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)` | the same, from a `va_list` (default mode) |

Each returns the number of characters produced. A write error does not
change the count; check `ferror`. `sprintf` and `vsprintf` cannot check the
size of their buffer; `snprintf` and `vsnprintf` store at most `n - 1`
characters and a null, and return the length the whole result would have
had, so a return of `n` or more means it was cut short. With `n` 0 they
store nothing, and `s` may be a null pointer: `snprintf(NULL, 0, ...)`
measures a result.

The conversions:

| Conversion | Argument | Output |
|---|---|---|
| `%d`, `%i` | `int` | signed decimal |
| `%u` | `unsigned int` | unsigned decimal |
| `%o` | `unsigned int` | octal |
| `%x`, `%X` | `unsigned int` | hexadecimal, `abcdef` or `ABCDEF` |
| `%c` | `int` | the character |
| `%s` | `char *` | the string; `(null)` for a null pointer |
| `%p` | `void *` | at least six lower-case hex digits: `040a3c` |
| `%f`, `%F` | `double` | `[-]ddd.dddddd` |
| `%e`, `%E` | `double` | `[-]d.dddddde+dd` |
| `%g`, `%G` | `double` | `%e` or `%f` style by the exponent, without trailing zeros |
| `%a`, `%A` | `double` | hexadecimal: `[-]0x1.hhhp+d`, `0X` and `P` for `%A` |
| `%n` | `int *` | nothing; stores the count so far |
| `%%` | none | `%` |

| Flag | Effect |
|---|---|
| `-` | left-justify in the field |
| `0` | pad with zeros after the sign, for numbers without `-` (and, for integers, without a precision) |
| `+` | a `+` before a non-negative signed number |
| space | a space before a non-negative signed number (`+` wins) |
| `#` | `0` before octal, `0x` or `0X` before non-zero hex; `%e %f %g` always have a point, and `%g` keeps its zeros |

| Length | Argument |
|---|---|
| `hh` | `signed char` or `unsigned char` (passed as `int`, converted back) |
| `h` | `short` or `unsigned short` (passed as `int`, converted back) |
| `l` | `long` or `unsigned long`; for `%n`, `long *` |
| `ll`, `j` | `long long` or `unsigned long long` (default mode) |
| `z`, `t` | `size_t` and `ptrdiff_t`, which are `unsigned int` and `int` |
| `L` | `long double` for `%e %f %g %a`, which is `double` |

A width and a precision may be numbers or `*`, taken from the arguments; a
negative `*` width means `-` and that width, and a negative `*` precision
means none. An unknown conversion character prints as itself; a format
ending in a lone `%` just ends.

**Floating point.** The default precision is 6. Every floating conversion
is correctly rounded, and printed to any precision the digits are the exact
decimal value of the `double`: `printf("%.20f", 0.1)` prints
`0.10000000000000000555`. An infinity prints as `inf` and a NaN as `nan`,
after a `-` when the sign bit is set, and in capitals for `%E %F %G`; the
`0` flag pads them with spaces. `%F` is C99's: `%f` with `INF` and `NAN` in
capitals. A `float` argument arrives as a `double`, as in any C.

`%a` prints the bits themselves, so it is exact: the leading digit is 1 (0
for zero and the subnormals, whose exponent is then `-1022`), then the
fraction's hexadecimal digits without trailing zeros, then `p` and the
power of two in decimal: `printf("%a", 0.1)` prints
`0x1.999999999999ap-4`. A precision rounds the fraction to that many
digits, to nearest even, and may carry into the leading digit:
`printf("%.0a", 1.5)` prints `0x2p+0`, as glibc does. The `0` flag pads
after the `0x`.

Flags that make no sense for a conversion (a `+` on `%u`, a `#` on `%d`)
are ignored.

### Formatted input

| Function | Reads from |
|---|---|
| `int scanf(const char *fmt, ...)` | `stdin` |
| `int fscanf(FILE *f, const char *fmt, ...)` | `f` |
| `int sscanf(const char *s, const char *fmt, ...)` | the string `s` |

Each returns the number of items assigned, or `EOF` if the input ended
before the first conversion. In the default mode C99's `vscanf`,
`vfscanf` and `vsscanf` take a `va_list` instead.

| Conversion | Reads | Stores to |
|---|---|---|
| `%d` | a decimal integer, optionally signed | `int *` |
| `%i` | an integer written as in C: `0x1F`, `017`, `15` | `int *` |
| `%u` | a decimal integer | `unsigned int *` |
| `%o` | an octal integer | `unsigned int *` |
| `%x`, `%X` | a hexadecimal integer, with or without `0x` | `unsigned int *` |
| `%p` | hexadecimal, as `%p` prints it | `void **` |
| `%e %f %g %a %E %F %G %A` | a floating constant, as `strtod` reads it | `float *` |
| `%s` | characters up to white space | `char *`, with a null added |
| `%c` | exactly the width in characters (default 1) | `char *`, no null added |
| `%[...]` | characters in the set (or, after `^`, not in it) | `char *`, with a null added |
| `%n` | nothing | `int *`: the characters read so far |
| `%%` | a `%` | nothing |

`hh` stores to a `char`, `h` to a `short`, `l` to a `long` (or, for the
floating conversions, a `double`), `L` to a `long double`, which is a
`double`, `ll` or `j` to a `long long` (default mode), and `z` and `t` to
a `size_t` and a `ptrdiff_t`. Remember the `l` for a
`double`: `%f` stores a `float`. `*` after the `%` reads an item without
storing it, and a width limits the characters read; a sign and a `0x`
prefix count towards it.

All but `%c`, `%[` and `%n` skip white space before the item; white space in
the format skips any amount of white space in the input, none included. The
first character that does not match ends the scan, and is left unread.

In a `%[` set, `]` straight after `[` (or `[^`) is a member. A `-` between
two characters, the second not below the first, is a range: `%[a-z]`,
`%[0-9A-F]`. A `-` first or last in the set, or between two characters in
descending order, is an ordinary member: `%[-a]`, `%[a-]`, and `%[z-a]` is
the three characters `z`, `-` and `a`.

Integers that overflow wrap round, without an error. A floating value is
converted directly from what was read to the type stored, so a `float` is
correctly rounded too. In the default mode C99's forms are read as well,
as `strtod` reads them: hexadecimal (`0x1.8p3`), `inf`, `infinity` and
`nan`; strict mode reads C89's only. The input can
be given back by only one character, so `"1e"` followed by a letter is a
matching failure for `%f`, with both characters consumed, and so is
`"0x"` without a hexadecimal digit after it.

`scanf` reads the keyboard a line at a time
([Reading the keyboard](#reading-the-keyboard)), and the console never
reports end of file, so `scanf` at the keyboard never returns `EOF`.
Reading a whole line with `fgets` and taking it apart with `sscanf` gives a
program more control over bad input.

## `<stdlib.h>`

The types `size_t`, `wchar_t`, `div_t`, `ldiv_t` and (default mode)
`lldiv_t`; the macros `NULL`, `EXIT_SUCCESS` (0), `EXIT_FAILURE` (200),
`RAND_MAX` (32767) and `MB_CUR_MAX` (1).

### Numbers from strings

| Function | What it does |
|---|---|
| `int atoi(const char *s)` | a decimal `int` |
| `long atol(const char *s)` | a decimal `long` |
| `long long atoll(const char *s)` | a decimal `long long` (default mode) |
| `double atof(const char *s)` | a `double` |
| `double strtod(const char *s, char **end)` | a `double`, and where it ended |
| `float strtof(const char *s, char **end)` | a `float` (default mode) |
| `long double strtold(const char *s, char **end)` | a `long double`, which is `strtod` (default mode) |
| `long strtol(const char *s, char **end, int base)` | a `long` in base 2 to 36, or by its prefix with base 0 |
| `unsigned long strtoul(const char *s, char **end, int base)` | the same, `unsigned long` |
| `long long strtoll(const char *s, char **end, int base)` | the same, `long long` (default mode) |
| `unsigned long long strtoull(const char *s, char **end, int base)` | the same, `unsigned long long` (default mode) |

All of them skip leading white space and accept a sign. `atoi` and `atol`
read decimal digits and wrap round on overflow, without an error, as C
leaves it undefined; `atoll` is `strtoll` in base 10, so it saturates and
sets `ERANGE`. `atof(s)` is `strtod(s, NULL)`.

`strtol` and its relatives take base 2 to 36, with letters in either case
for digits from 10. With base 16 or 0 a `0x` or `0X` prefix is skipped, but
only when a hexadecimal digit follows (`"0xg"` is the number 0, ending at
the `x`); base 0 also reads a leading `0` as octal. A base outside 2 to 36
converts nothing. With no digits at all the result is 0 and `*end` is `s`
itself. A value out of range gives the type's maximum (or, for the signed
functions, minimum) and sets `ERANGE`; the digits after the point where it
overflowed are still consumed. `strtoul` and `strtoull` negate a value
after a `-` in their unsigned type, as C says.

`strtod` reads digits with an optional point and an optional exponent,
and in the default mode C99's forms: hexadecimal digits after `0x` with an
optional point and an optional `p` exponent, a power of two (`0x1.8p3` is
12); `inf` or `infinity`; and `nan`, optionally followed by letters, digits
and `_` in parentheses, all in either case. In strict mode, as C89
requires, `strtod("inf", &end)` converts nothing and `strtod("0x1p3",
&end)` reads only the `0`; `atof` and `scanf` follow `strtod`. The result
is correctly rounded. A result
that overflows gives `HUGE_VAL` (with the sign) and one that underflows to
zero gives 0, both setting `ERANGE`; a subnormal result does not set it. A
trailing `e` or `p` with no digits after it is not part of the number:
`strtod("1e", &end)` is 1, with `end` at the `e`, and `strtod("0x", &end)`
is 0, with `end` at the `x`. `strtof` does the same for a `float`, rounded
once from the text itself rather than through a `double`.

### Memory

| Function | What it does |
|---|---|
| `void *malloc(size_t size)` | allocates `size` bytes |
| `void *calloc(size_t n, size_t size)` | allocates `n` times `size` bytes, zeroed |
| `void *realloc(void *p, size_t size)` | changes the size of a block, moving it if need be |
| `void free(void *p)` | gives a block back |

`malloc(0)`, and `calloc` with a zero count or size, return a distinct
pointer (to a one-byte block) that may be passed to `free`. `calloc` checks
that `n` times `size` does not overflow. `realloc(NULL, n)` is `malloc(n)`;
`realloc(p, 0)` frees `p` and returns a null pointer. `realloc` grows or
shrinks a block where it is whenever it can (the last block in the heap, or
one followed by a free block), and otherwise copies it; when it fails it
returns a null pointer and leaves the old block as it was. `free(NULL)`
does nothing. The heap itself is described [above](#the-heap).

### Ending the program

| Function | What it does |
|---|---|
| `void exit(int status)` | runs the `atexit` functions, closes the streams, ends the program |
| `void abort(void)` | raises `SIGABRT`; closes the files unflushed and ends with status 134 |
| `int atexit(void (*f)(void))` | registers a function for `exit` to call |
| `void _Exit(int status)` | closes the streams and ends the program, without the `atexit` functions (default mode) |

`exit` calls the `atexit` functions in the reverse of the order they were
registered, then writes out and closes every stream, removes the files
`tmpfile` created, and returns `status` to MOS. Returning from `main` is
the same as calling `exit`. `atexit` holds 32 functions and returns
non-zero when it is full. A function that calls `exit` again does not run
itself a second time.

`abort` raises `SIGABRT`. Unless a handler leaves by `longjmp`, it then
closes every file without writing what its buffer holds, so that no MOS
handle is lost, and ends the program with status 134. The `atexit`
functions do not run.

C99's `_Exit` runs neither the `atexit` functions nor any signal handler,
but it still writes out and closes the streams, which C99 leaves to the
implementation, since otherwise MOS would lose their handles.

### Arithmetic and random numbers

| Function | What it does |
|---|---|
| `int abs(int n)` | the absolute value |
| `long labs(long n)` | the same, `long` |
| `long long llabs(long long n)` | the same, `long long` (default mode) |
| `div_t div(int num, int den)` | quotient and remainder together |
| `ldiv_t ldiv(long num, long den)` | the same, `long` |
| `lldiv_t lldiv(long long num, long long den)` | the same, `long long` (default mode) |
| `int rand(void)` | a pseudo-random number, 0 to 32767 |
| `void srand(unsigned int seed)` | starts `rand`'s sequence again from `seed` |

The quotient truncates toward zero and the remainder has the numerator's
sign. The absolute value of the most negative number of a type is that
number itself.

`rand` is the linear congruential generator that C89 gives as its example:
the state is multiplied by 1103515245 and 12345 added, modulo 2 to the 32,
and `rand` returns bits 16 to 30. The sequence starts as if `srand(1)` had
been called, so it is the same at every run; a program that wants a
different one can seed it from `time`, when the clock is set, or from
`clock` after waiting for a key.

### Sorting and searching

| Function | What it does |
|---|---|
| `qsort(base, n, size, cmp)` | sorts an array |
| `bsearch(key, base, n, size, cmp)` | finds an element in a sorted array |

```c
void qsort(void *base, size_t n, size_t size,
           int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));
```

`qsort` is a heapsort: it takes time in proportion to n log n whatever the
order of the input, uses no extra memory and does not recurse. It is not
stable: equal elements may end in any order. `bsearch` is a binary search;
with several equal elements it may find any of them.

### The environment

| Function | What it does |
|---|---|
| `char *getenv(const char *name)` | the value of a MOS 3 system variable |
| `int system(const char *command)` | runs a MOS command |

`getenv` returns the value of the MOS system variable of that name when the
program runs on MOS 3 or later
([system variables](https://agonplatform.github.io/agon-docs/mos/System-Variables/)),
and a null pointer if there is none. MOS 2 has no system variables, so
there `getenv` always returns a null pointer. Names are compared without
regard to case. The value is at most 255 characters, in a buffer that the
next call overwrites.

`system(NULL)` returns non-zero, since MOS can run commands.
`system(command)` passes the command to MOS as if it had been typed at
the prompt and returns MOS's status for it: 0 for success. That runs MOS's
own commands (`*CAT`, `*CD`, `*DELETE` and the rest) and moslets, the small
commands in `/mos`, but not a program in `/bin`: MOS looks for one only at
its own prompt, and it would load over the program that called it.

### Multibyte characters

| Function | What it does |
|---|---|
| `int mblen(const char *s, size_t n)` | the length of a multibyte character |
| `int mbtowc(wchar_t *wc, const char *s, size_t n)` | a multibyte character to a wide one |
| `int wctomb(char *s, wchar_t wc)` | a wide character to a multibyte one |
| `size_t mbstowcs(wchar_t *wcs, const char *s, size_t n)` | a multibyte string to a wide one |
| `size_t wcstombs(char *s, const wchar_t *wcs, size_t n)` | a wide string to a multibyte one |

In the "C" locale every byte is one character and there are no shift
states, so these convert byte for byte: a byte becomes a `wchar_t` from 0
to 255, and a wide character outside 0 to 255 cannot be converted
(`wctomb` returns -1, `wcstombs` returns `(size_t)-1`). `mblen`, `mbtowc`
and `wctomb` with a null `s` return 0: there are no shift states.

## `<string.h>`

| Function | What it does |
|---|---|
| `void *memcpy(void *d, const void *s, size_t n)` | copies `n` bytes; the areas must not overlap |
| `void *memmove(void *d, const void *s, size_t n)` | copies `n` bytes; the areas may overlap |
| `void *memset(void *d, int c, size_t n)` | fills `n` bytes with `c` |
| `int memcmp(const void *a, const void *b, size_t n)` | compares `n` bytes |
| `void *memchr(const void *s, int c, size_t n)` | finds a byte in `n` bytes |
| `size_t strlen(const char *s)` | the length of a string |
| `char *strcpy(char *d, const char *s)` | copies a string |
| `char *strncpy(char *d, const char *s, size_t n)` | copies at most `n` characters, padding with nulls to `n` |
| `char *strcat(char *d, const char *s)` | appends a string |
| `char *strncat(char *d, const char *s, size_t n)` | appends at most `n` characters, then a null |
| `int strcmp(const char *a, const char *b)` | compares two strings |
| `int strncmp(const char *a, const char *b, size_t n)` | compares at most `n` characters |
| `int strcoll(const char *a, const char *b)` | compares by the locale: `strcmp` |
| `size_t strxfrm(char *d, const char *s, size_t n)` | transforms for `strcmp`: a copy |
| `char *strchr(const char *s, int c)` | the first `c` in `s` |
| `char *strrchr(const char *s, int c)` | the last `c` in `s` |
| `size_t strspn(const char *s, const char *set)` | the length of `s`'s start made of `set`'s characters |
| `size_t strcspn(const char *s, const char *set)` | the length of `s`'s start made of none of them |
| `char *strpbrk(const char *s, const char *set)` | the first character of `s` that is in `set` |
| `char *strstr(const char *s, const char *find)` | the first occurrence of `find` in `s` |
| `char *strtok(char *s, const char *sep)` | the next token of `s`, separated by `sep`'s characters |
| `char *strerror(int n)` | the message for an `errno` value |

The comparisons treat bytes as `unsigned char`, as C requires, so a byte
above 0x7F sorts after every ASCII character even though `char` is signed.
`strlen`, `strcmp`, `strcpy`, `strcat`, `strchr`, `memcpy`, `memmove`,
`memset` and `memchr` are in assembly, using the eZ80's block
instructions; the rest are byte loops in C. `strchr` and `strrchr` find the
terminating null when `c` is 0. `strncpy` adds no null when `s` has `n`
characters or more; `strncat` always adds one. `strstr` with an empty
`find` returns `s`. `strxfrm` copies `s` only if it fits in `n` bytes with
its null, and returns its length either way, so a caller can size the
buffer. `strtok` remembers its place in a static variable, so only one
string can be split at a time. `strerror`'s messages are listed under
[`<errno.h>`](#errnoh).

## `<time.h>`

The types `size_t`, `clock_t` (`long`), `time_t` (`long`) and `struct tm`,
with C89's members; the macros `NULL` and `CLOCKS_PER_SEC` (100).

| Function | What it does |
|---|---|
| `clock_t clock(void)` | hundredths of a second since the program started |
| `time_t time(time_t *t)` | the current time from the real-time clock |
| `double difftime(time_t t1, time_t t0)` | `t1 - t0` in seconds |
| `time_t mktime(struct tm *tm)` | a broken-down time to a `time_t`, normalising `*tm` |
| `struct tm *gmtime(const time_t *t)` | a `time_t` to a broken-down time |
| `struct tm *localtime(const time_t *t)` | the same as `gmtime` |
| `char *asctime(const struct tm *tm)` | the form `Sun Sep 16 01:03:52 1973` and a newline |
| `char *ctime(const time_t *t)` | `asctime(localtime(t))` |
| `size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)` | formats a time under the control of `fmt` |

A `time_t` is a count of seconds since 1970-01-01 00:00:00, in a 32-bit
`long`, so it covers 13 December 1901 to 19 January 2038. Dates follow the
Gregorian calendar throughout.

**`time`** reads the real-time clock through MOS, to the second. If the
clock has never been set it reports day 0 of 1980, and `time` returns
`(time_t)-1` (and stores it in `*t` too). The clock is set with MOS's
`*TIME` command.

**Time zones.** MOS has none, so the clock is taken to be local time and
UTC alike: `gmtime` and `localtime` give the same result, `tm_isdst` is
always -1 (not known), and `%Z` in `strftime` produces nothing. `gmtime`
and `localtime` return a pointer to the same static structure, and
`asctime` and `ctime` to the same static buffer; each call overwrites the
last.

**`mktime`** accepts fields outside their usual ranges (a `tm_mday` of 32,
a negative `tm_min`) and carries them into the others, then rewrites
`*tm` normalised, with `tm_wday` and `tm_yday` set and `tm_isdst` -1. It
ignores the `tm_isdst` it is given. A time outside `time_t`'s range returns
`(time_t)-1`.

**`clock`** counts MOS's timer, which runs in hundredths of a second, from
the moment the program started. `clock() / CLOCKS_PER_SEC` is seconds.
`difftime` is exact, and returns a `double`, so a program that calls it
links `libm.s`.

**`strftime`** writes at most `max` characters including the terminating
null, and returns the count without the null, or 0 if the result did not
fit. It knows C89's conversions in the "C" locale and C99's `%e`:

| Conversion | Gives | Conversion | Gives |
|---|---|---|---|
| `%a` | `Sun` | `%m` | month, `01`-`12` |
| `%A` | `Sunday` | `%M` | minute, `00`-`59` |
| `%b` | `Sep` | `%p` | `AM` or `PM` |
| `%B` | `September` | `%S` | second, `00`-`60` |
| `%c` | `Sun Sep 16 01:03:52 1973` | `%U` | week of the year, from Sunday, `00`-`53` |
| `%d` | day, `01`-`31` | `%w` | weekday, `0`-`6`, Sunday 0 |
| `%e` | day, space-padded | `%W` | week of the year, from Monday, `00`-`53` |
| `%H` | hour, `00`-`23` | `%x` | `09/16/73` |
| `%I` | hour, `01`-`12` | `%X` | `01:03:52` |
| `%j` | day of the year, `001`-`366` | `%y` | year, `00`-`99` |
| `%Z` | nothing | `%Y` | year, `1973` |
| `%%` | `%` | | |

Any other character after a `%` is copied as written, `%` included.
