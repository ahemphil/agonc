# Quick start

## What you need

- An Agon Light, Agon Light 2 or Agon Console8 running MOS 2.3.3, or MOS 3.
  The emulator (fab-agon-emulator) works too.
- Its SD card, with about 2 MB free.
- A way to write C source: an editor on the Agon, or a PC and a card reader.

agonc runs entirely on the Agon. Nothing has to be installed on a PC unless
you want to build agonc itself ([BUILDING.md](../../BUILDING.md)).

## Install

The release is a zip file. Unzip it onto the root of the SD card, merging
its folders with the ones already there. It adds:

| Path on the card | What it is |
|---|---|
| `/mos/agonc.bin` | the `agonc` command |
| `/bin/agonc/` | the compiler's passes: `cpp.bin`, `cc1.bin`, `cc2.bin`, `ld.bin` |
| `/bin/ez80asm.bin` | the assembler agonc was tested with |
| `/lib/agonc/` | the C headers and libraries |
| `/lib/agonc/agon/` | the Agon headers: `mos.h`, `uart.h`, `vdp.h` |
| `/usrlib/` | an empty folder for your own libraries |
| `/tmp/` | an empty folder for temporary files (agonc's own go in `/tmp/agonc`) |

If the card already has an `ez80asm.bin`, the zip's copy replaces it; agonc
needs ez80asm 2.3 or later. Nothing else on the card is touched. To remove
agonc, delete the files above.

**Upgrading from 1.1.0-beta.1,** which kept its library directly in
`/lib`: after unzipping, delete `/lib`'s `assert.h`, `ctype.h`, `errno.h`, `float.h`, `limits.h`, `locale.h`,
`math.h`, `setjmp.h`, `signal.h`, `stdarg.h`, `stddef.h`, `stdint.h`,
`stdio.h`, `stdlib.h`, `string.h`, `time.h`, `crt0.s`, `rt.s`, `libc.s`,
`libm.s`, `libagon.s` and the `.idx` files beside them, and the folder
`/lib/agon`. agonc no longer
uses them.

Put the card back and type `agonc --version` at the MOS prompt. It prints
the version; `Invalid command` means `/mos/agonc.bin` is not where MOS looks
for commands.

## A first program

Save this as `hello.c` in the root of the card:

```c
#include <stdio.h>

int main(void)
{
    printf("Hello from agonc\n");
    return 0;
}
```

At the MOS prompt:

```text
cd /
agonc -o hello.bin hello.c
hello
```

The second line compiles `hello.c` into the program `hello.bin`; the third
runs it. Without `-o`, agonc names the program `a.bin`. Compiling a
small program takes about five seconds on the Agon: agonc runs five programs
one after another (the preprocessor, the compiler's two halves, the
linker and the assembler), each loaded from the card. Add `-time` to see
where the time goes.

## When something is wrong

Each error names the file and line and says what is wrong:

```text
hello.c:5: error: call to undeclared function prinft
```

agonc stops at the first pass that finds an error, removes its temporary
files, and returns status 200, which also stops a MOS `exec` script. The
[Messages](04-messages.md) chapter explains the messages you are likely to
see.

A running program stops when you press Ctrl-C, at its next input or output
(the [C library](05-c-library.md#signalh) chapter has the details); Ctrl-C
during a compile stops agonc too.

## Next

- More programs to try and read: [the examples](../../examples/README.md)
  (files, graphics, sound, the serial port, floating point).
- Several source files, libraries and options:
  [Using agonc](02-using-agonc.md).
- What C is accepted, and the sizes of the types:
  [The language](03-the-language.md). `int` is 24 bits.
- Files, the console, `printf` and the rest:
  [The C library](05-c-library.md).
- Graphics, sound, the keyboard and MOS:
  [The Agon library](06-agon-library.md).
