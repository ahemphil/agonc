# Changelog

Versions follow [semantic versioning](https://semver.org): a version like
1.1.0-beta.1 is a pre-release of 1.1.0.

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
