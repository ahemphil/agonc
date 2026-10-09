# The agonc manual

agonc is a C compiler that runs on the Agon Light and the Agon Console8. It
compiles C99, all of it but variable-length arrays and complex numbers, into
programs for MOS, on the Agon itself; `-ansi` gives strict C89.
This manual is for programmers who know C and want to use it on the Agon.

1. [Quick start](01-quick-start.md): install agonc, compile and run a
   first program.
2. [Using agonc](02-using-agonc.md): the command, its options, files and
   folders, and building larger programs.
3. [The language](03-the-language.md): the two modes, the types and their
   sizes, extensions, limits and memory.
4. [Messages](04-messages.md): what the compiler's errors and warnings mean,
   and what to do about them.
5. [The C library](05-c-library.md): every function of the standard
   library, and how each behaves on the Agon.
6. [The Agon library](06-agon-library.md): MOS, the serial port, and the
   VDP's graphics, sound and system commands.
7. [Porting from AgDev](07-porting-from-agdev.md): moving a program from
   the PC-hosted AgDev toolchain, and back to HI-TECH C and CP/M.
8. [How agonc works](08-how-agonc-works.md): the passes, the files between
   them, and where to read more.

`make manual` turns these chapters into a PDF for printing (Letter,
double-sided); see [BUILDING.md](../../BUILDING.md). The formal
specifications behind this manual are in [docs/](..):
[the language and library](../c89_spec.md), [the driver](../driver.md),
[the ABI](../abi.md), [the object format](../object_format.md) and
[the intermediate format](../ir_format.md).
