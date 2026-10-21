# Changelog

Versions follow [semantic versioning](https://semver.org): a version like
1.1.0-beta.1 is a pre-release of 1.1.0.

## 1.1.0-beta.3 (2026-10-20)

- **Faster code for constant operands** (with `-O`, the default): on
  `int` and `long`, a shift by a constant, `&`, `|` and `^` with a
  constant, and `/` and `%` by a power of two are made inline instead of
  calling the runtime, in expressions, compound assignments and
  conditions; a `long` tested against 0 or compared with a constant is
  tested a byte at a time; and an `unsigned char` divided by any other
  constant uses the eZ80's 8-bit multiply. Measured: `i / 2` 249 cycles
  to 55, `i % 2` 258 to 37, `i & 1` 101 to 18, `i >> 1` 92 to 46, `u % 8`
  240 to 18, `l / 2` 562 to 111, `l >> 1` 175 to 111, an `unsigned char`
  `/ 10` 203 to 46 and `% 10` 212 to 65. The new `constops` benchmark
  (packing two digits a byte, as Project Euler programs do) runs 3.18 s
  to 1.24 s, and `crc32` 1.78 s to 0.76 s. Programs are about 3% smaller,
  the library's own code being compiled the same way.
- **The linker takes from an indexed library only what a program uses.**
  A library's index (`agonc --index`, and every library agonc ships) now
  lists the names the library defines; the linker reads that list and
  loads just the sections the program needs, where it used to read every
  section of every library into its tables. Programs built from large
  libraries now fit: a 29-file editor whose link needed 1,729 sections
  against the limit of 1,500 needs 999 with its libraries indexed. Links
  are a little faster too (on an Agon, hello world's 1.28 s to 1.22 s, a
  floating-point program's 4.04 s to 3.34 s), and every program links to
  the same bytes as before.
  **Indexes made by an earlier agonc** are not used (the linker reads the
  library itself instead, as for a library that has changed): index your
  own libraries again with `agonc --index`. One behaviour changes: a name
  that both your program and an indexed library define is taken from your
  program, as other linkers do, instead of being reported as a duplicate
  definition.
- **Wildcards and one-command libraries, on the Agon too.** File names may
  hold `*` and `?`, which agonc expands itself, since MOS does not:
  `agonc -o game.bin src/*.c`, in alphabetical order so that a build is
  the same on every card. `--lib` makes a library in one command:
  `agonc --lib -o libutil.s util/*.c` compiles the sources, joins them
  (MOS has no command that joins files) and indexes the result. A
  29-file editor now builds on an Agon in three commands, two libraries
  and the program, to the same bytes as on a PC.
- **Response files may be of any length** (they were limited to 1,200
  bytes), and wildcards in them are expanded.
- **agonc on a PC has the Agon's limits**, so that what builds on a PC
  builds on the Agon. It used to count the full `AGONC_ROOT` path in its
  temporary file names, which made a PC build of a dozen files fail where
  the Agon managed thirty; paths now count as the Agon's. `--big-tables`
  raises the driver's limits eight times, for builds meant for the PC
  alone.
- **Larger source files with many headers**: cc1 holds 3,000 distinct
  names (was 2,000) in 32,000 bytes (was 20,000), and 2,000 file-scope
  declarations (was 1,200), since AgonDev's `<agon/vdp.h>` alone takes
  about 700 names and 10,000 bytes. A file of the AED editor that
  includes it with a dozen of its own headers stopped at `too many
  identifier characters`.
- A `--lib` command whose source fails to compile reports that error
  alone; it also said `cannot write` the library.
- **32 source files in one command**, as `too many input files` always
  said (a link of more than 27 failed in the linker, whose input limit is
  now 48), and the temporary `.i` and `.ir` files are removed as each file
  is compiled.
- **`<agon/mos.h>` follows AgonDev** (release v0.22), the toolchain most
  Agon programs come from, so that its programs build unchanged. Added:
  its `sysvar_` offsets (`mos_sysvars()[sysvar_cursorX]`) and `vdp_pflag_`
  flags, its `SYSVAR` structure through `sys_vars`, `FRESULT`,
  `FA_OPEN_EXISTING`, the `RET_` and `I2C_SPEED_` constants,
  `mos_puts`, `mos_putstring`, `putch`, `getch`, `waitvblank`,
  `mos_ugetc_nb` and the six mouse `getsysvar_` readers; and the header
  includes `<stdint.h>` (but under `-ansi`). **Two changes to existing
  calls:** `mos_oscli` takes AgonDev's three arguments,
  `mos_oscli(command, NULL, 0)`, and `ffs_fread` and `ffs_fwrite` take
  three, returning the number of bytes transferred instead of a status.
  AgonDev's calls that only MOS 3 has are not provided.
- **`<agon/vdp.h>` follows AgonDev too** (v0.22): its shapes drawn whole
  from their corners (`vdp_line`, `vdp_rectangle`, `vdp_filled_triangle`,
  `vdp_filled_circle`, `vdp_arc`, `vdp_sector`, `vdp_fill_path` and the
  rest), colours, coordinates, sprite settings, the sprite-file loaders,
  key readers, `vdp_vdu_init`, `VDP_PUTS` and its constants; the header
  includes `<agon/mos.h>`. **Changes to existing calls:** `vdp_triangle`
  and `vdp_circle` take the three corners, and the centre and radius, and
  draw outlines (`vdp_filled_triangle` and `vdp_filled_circle` fill);
  `vdp_capture_bitmap` takes the area's corners; the two multi-part audio
  envelopes take their data; `vdp_mode` returns the mode, or -1 for one
  out of range. Where AgonDev's own call is wrong (its
  `vdp_capture_bitmap` sends the wrong bytes), agonc's does what the call
  is documented to do.
- **AgonDev's other headers** (v0.22): `<agon/keyboard.h>`, a queue of
  key events (`kbuf_init`, `kbuf_poll_event`, `kbuf_clear`,
  `kbuf_deinit`), which leaves Ctrl-C working; `<agon/gpio.h>`, the
  eZ80's ports (`io_in`, `io_out`) and GPIO pins (`outputMode`,
  `inputPinID` and the rest, `PB0` to `PD7`); `<agon/joystick.h>`;
  `<agon/timer.h>`'s `delay(ms)`, timed by the eZ80's timer 0;
  `<ez80f92.h>`, the eZ80's internal registers by name; and AgDev's
  header names, `<mos_api.h>` and `<agon/vdp_vdu.h>`, as AgonDev keeps
  them: forwards that include agonc's headers with a warning that they
  are deprecated.
- **The porting chapter covers AgonDev** ("Porting from AgonDev and
  AgDev"): what changes moving a program from either toolchain, their
  makefile settings, and what the AED editor needed.
- **`ffs_stat` takes its arguments in MOS's order**, `ffs_stat(&info,
  path)`, as MOS's call 0x96 does, as AgonDev's does, and as agonc's
  other `ffs_` calls take theirs; it took the path first, FatFs's order.
  **A program that calls it must swap them**; agonc warns about the old
  order (incompatible pointer types).
- **An `enum` may have 1,023 constants**, C99's minimum, instead of 127.
- **More of C99 in the default mode:** a local structure's bit-field may be
  given a value that is not a constant, as its other members could; and a
  parameter's array brackets may hold `static` and qualifiers
  (`int a[static 3]`, `int b[const]`, the qualifiers applying to the
  pointer the parameter becomes).
- `return f();` in a `void` function, `f` being `void` too, which C forbids
  and GCC allows, is accepted in the default mode with a warning.
- The documentation now names C99's wide-character library (`<wchar.h>`,
  `<wctype.h>`) beside variable-length arrays and complex numbers as
  what the default mode leaves out of C99. It was never provided; a
  program that includes it, as some do when `__STDC_VERSION__` says
  C99, stops with `cannot find include file`.

Fixes:

- A warning or error about a function or array declared in a header, made
  when the whole file has been read (`unused static function`, `static
  function used but never defined`, `an array assumed to have one
  element`), gave the `.c` file's name with the header's line number. It
  now gives the header's name, and `unused static function` names the
  function.
- An enumerator whose value does not fit in `int` (-8388608 to 8388607),
  such as `0x800000`, `10000000L` or `-1U`, or one counted up past
  8388607, was silently cut to 24 bits. It is now an error, as C requires.
- Unary `+` kept a `char`, `short` or `_Bool` operand's type, so
  `sizeof (+c)` was 1 instead of `sizeof (int)`; its value was already
  promoted. Found by SDCC's `sizeof` tests.
- A signed constant expression that overflowed its type, such as
  `8388607 + 1`, `4096 * 4096` or `65536L * 65536L`, wrapped without a
  word. It now gives the warning `integer overflow in a constant
  expression`, the diagnostic C requires; the value is still the wrapped
  one.

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
