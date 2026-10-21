# Using agonc

## The command

```text
agonc [options] file...
```

agonc follows the conventions of gcc and other Unix compilers: give it the
source files and it produces a program; options and files may come in any
order. Each input is handled by its extension:

| Input | What agonc does with it |
|---|---|
| `name.c` | preprocesses, compiles and links it |
| `name.i` | compiles and links already-preprocessed C |
| `name.s` | links it as it is: compiled C, or assembly you wrote |
| `name.asm` | assembles it alone into a program, without linking |

Without `-c`, `-S` or `-E`, all the inputs become one program, `a.bin` or the
name given with `-o`. `agonc -h` lists the options.

## Options

| Option | Effect |
|---|---|
| `-o file` | name the output |
| `-c`, `-S` | compile each input to a `.s` file only, beside the input |
| `-E` | preprocess only, to `-o` or the screen |
| `-I dir` | search `dir` for `#include` files, before the standard places |
| `-D name[=value]` | define a macro, as `#define name value` would (`1` if no value) |
| `-U name` | undefine a macro |
| `-L dir` | search `dir` for `-l` libraries |
| `-lname` | link the library `libname.s` |
| `-lm` | link the floating-point library even if agonc does not see it is needed |
| `-ansi`, `-std=c89`, `-std=c90` | strict C89 ([The language](03-the-language.md#the-two-modes)) |
| `-std=c99`, `-std=gnu99` | the default mode, C99; the last of these and `-ansi` wins |
| `-w` | no warnings |
| `-Werror` | warnings count as errors |
| `-Wall` | all warnings, which is the default anyway |
| `-O0` | no optimisation |
| `-O`, `-O1`, `-O2`, `-O3`, `-Os` | optimise, which is the default; all the same |
| `-nostdlib` | no start-up code and no libraries: for programs that supply their own |
| `-save-temps` | keep the intermediate files in `/tmp/agonc` |
| `-Wl,option` | pass `option` to the linker; `-Wl,--entry=sym` keeps `sym` and what it uses |
| `--lib` | with `-o libname.s`: compile the inputs, join them into one library and index it ([Libraries](#libraries)) |
| `--index lib.s` | index a library, so that links using it are faster ([Libraries](#libraries)) |
| `-time` | show how long each pass took, and the total |
| `-v` | as `-time`, and each pass's command line and the program's memory layout |
| `--version` | print the version |
| `-h`, `--help` | list the options |
| `@file` | read more arguments from `file` |
| `--big-tables` | on a PC, tables larger than the Agon's ([Building on a PC](#building-on-a-pc)) |
| `--` | the end of the options: what follows are file names |

The last of `-O0` and `-O` wins. Options that make no sense on the Agon,
such as `-g`, `-shared` or `-M`, are errors that say why. An unknown option
is an error too.

`-o` never overwrites a source file. If it names one of the inputs, or a
`.c`, `.h`, `.i`, `.s` or `.asm` file that is not what the command
produces, agonc stops before doing anything. So a slip such as
`agonc -o hello.c hello.c` leaves `hello.c` alone.

## Several source files

```text
agonc -o game.bin main.c sprites.c sound.c
```

compiles each file and links them into one program. There is no separate
object format: each `.c` file compiles to a `.s` file (assembly with a few
marker lines for the linker), and linking joins `.s` files. To compile one
file and keep the result:

```text
agonc -c sprites.c
agonc -o game.bin main.c sprites.s sound.c
```

The linker keeps only the functions and data a program can reach from
`main`, so unused code costs nothing, whether in your files or the library.
Two inputs may not share a file name, even in different folders or letter
cases, because their intermediate files would collide.

File names may hold wildcards, which agonc expands itself, since MOS does
not: `*` matches any run of characters and `?` any one, in the file name
(not the folder), letters in either case.

```text
agonc -o game.bin src/*.c
```

The files come in alphabetical order, so a build is the same on every
card, and a pattern that matches nothing is an error. Wildcards work in
response files too. A program may have up to 32 source files and
libraries in one command; a larger one is best split into libraries.

## Files and folders

agonc finds its own files in fixed places on the SD card:

| Folder | Holds | Used for |
|---|---|---|
| `/mos` | `agonc.bin` | the command |
| `/bin/agonc` | `cpp.bin`, `cc1.bin`, `cc2.bin`, `ld.bin` | the passes |
| `/bin` | `ez80asm.bin` | the assembler |
| `/lib/agonc` | the C headers, `crt0.s`, `rt.s`, `libc.s`, `libm.s`, `libagon.s`, and their `.idx` indexes | the standard library |
| `/lib/agonc/agon` | `mos.h`, `uart.h`, `vdp.h`, `keyboard.h`, `gpio.h`, `joystick.h`, `timer.h`, `vdp_vdu.h` | `#include <agon/vdp.h>` and the others |
| `/usrlib` | your libraries | `#include` and `-l` |
| `/tmp/agonc` | intermediate files | created if missing |

`#include <name>` searches the `-I` folders in order, then `/usrlib`, then
`/lib/agonc`. `#include "name"` first searches the folder of the file that
includes it, then the same places. File names are not case-sensitive, as
on the card itself.

The intermediate files of a compile (`name.i`, `name.ir`, `name.s` and the
linked `out.asm`) go in `/tmp/agonc` and are deleted after a successful
build, unless `-save-temps` keeps them. Looking at `/tmp/agonc/name.s` is the
easiest way
to see the code agonc generates for a function.

## Libraries

Every program is linked with the start-up code (`crt0.s`), the runtime
helpers (`rt.s`) and the C library (`libc.s`). agonc adds the floating-point
and `long long` part, `libm.s`, when a source file uses `float`, `double` or
`long long`, and the Agon library, `libagon.s`, when the program calls one
of its functions. Each of these is read only when it is needed, because
reading a large file from the card is the slowest part of a link.

A library of your own is a `.s` file named `libname.s` in `/usrlib` (or a
folder given with `-L`), with its header beside it. `--lib` makes one in a
single command: it compiles the sources, joins them into the library and
indexes it.

```text
agonc --lib -o /usrlib/libutil.s util/*.c
copy util.h /usrlib/util.h
agonc -o prog.bin prog.c -lutil
```

`.s` files given to `--lib` are joined as they are, so a library can hold
assembly too. The index, `/usrlib/libutil.idx`, lets the linker read only
the index's list of names and take from the library just the functions
and data the program uses, instead of scanning the whole library on every
link. That is faster, and it leaves room in the linker's tables for
programs built from big libraries. A library made some other way (with
`-c` and `cat` on a PC, say) is indexed with `agonc --index
/usrlib/libutil.s`. The index records the library's size, so after the
library changes the linker notices and falls back to reading the library
until you index it again (`--lib` always does).

A name that both your program and an indexed library define is taken from
your program, and the library's is left out, as other linkers do. Without
the index, the two are reported as a duplicate definition.

## Assembly

There are two ways to use assembly with C:

- Inside a function, `asm("instruction");` adds one line, and `#asm` ...
  `#endasm` adds a block, copied as written into the function's code.
- A whole function or table in assembly goes in a `.s` file of its own,
  linked with the C files. It needs a few marker lines for the linker, as
  in this function for `int twice(int n);`:

```text
;;agonc-object 1
;;unit twice.s
;;sect code _twice g
_twice:                         ; int twice(int n)
        ld      hl,3
        add     hl,sp
        ld      hl,(hl)         ; n, the first argument, above the return address
        add     hl,hl
        ret
;;end
```

A C name gets a `_` in front in assembly. `;;sect code _twice g` starts a
global function; a section that uses another symbol says so with
`;;ref symbol`, so that the linker keeps it too. Where the arguments are,
what a function may change and how values are returned are set out in
[the ABI](../abi.md) (sections 3, 4 and 10); the marker lines are in
[the object format](../object_format.md).

## Running programs

A program is a MOS executable. MOS runs it when you type its name without
`.bin`, from the current folder or from `/bin`; `load prog.bin` followed by
`run` works too. Arguments after the name reach `main` as `argc` and
`argv`:

| Command line | `argc` | `argv[1]`, `argv[2]`, ... |
|---|---|---|
| `prog a b` | 3 | `a`, `b` |
| `prog a   b` | 3 | `a`, `b`: runs of spaces separate |
| `prog "a b" c` | 3 | `a b`, `c`: quotes group, and are removed |
| `prog ""` | 2 | an empty string |

`argv[0]` is an empty string: MOS does not pass the program's name. At most
32 entries are kept, `argv[0]` included; MOS limits a command line to about
240 characters anyway.

`main`'s return value is the program's status, as is `exit`'s argument. 0
means success; `EXIT_FAILURE` is 200, the same status agonc itself returns,
because MOS prints a message of its own for statuses 1 to 25 (26 too on
MOS 3; 1, for
instance, prints "Error accessing SD card"). A non-zero status stops a MOS
`exec` script.

## Status and Ctrl-C

agonc returns 0 when the program was built, 200 on any error (in a pass, in
the assembler, a missing file, a bad option), and 130 when Ctrl-C stopped
it. After Ctrl-C it prints `agonc: interrupted`, removes the output it was
writing and its temporary files, and runs nothing more.

## Building on a PC

The same `agonc` builds for Windows, Linux or macOS
([BUILDING.md](../../BUILDING.md), path B). It produces the same programs,
byte for byte, as agonc on the Agon. On a PC the folders above are found
under the folder named by the `AGONC_ROOT` environment variable, the
passes beside `agonc` itself, and `ez80asm` on the `PATH`.

agonc on a PC has the Agon's limits (on arguments, inputs, temporary files
and command lengths: [driver.md](../driver.md) section 5), counting paths
as the Agon would, so a build that works on a PC works on the Agon too.
For a build meant for the PC alone, `--big-tables` raises them.
