# The driver: `agonc`

`agonc` is the one command a user runs. It follows gcc's conventions
wherever the platform allows, sequences the passes, and reports one exit
status.

## 1. Two builds of one program

The driver's source is shared between two targets:

| Build | What it is | How it runs a pass |
|---|---|---|
| target | a **moslet** (`/mos/agonc.bin`, assembled with `org 0x0B0000`, under 32 KB) | `mos_load` the pass binary to `0x040000`, then `call 0x040000` with `A=0`, `DE=0x040000`, `HL` = its argument string; the status comes back in `HL` |
| host | an ordinary program (`agonc.exe`) | spawn the pass as a child process |

Everything else (option parsing, temp-file naming, sequencing, error
handling, exit status) is the same code, so the driver's logic is tested on
the host before it runs as a moslet. On the target the driver has no heap,
uses a few KB of MOS's system stack, and keeps no files open across a pass.

Pass binaries live in **`/bin/agonc/`** on the target (`cpp.bin`,
`cc1.bin`, `cc2.bin`, `ld.bin`) and next to the driver on the host. The
assembler is `/bin/ez80asm.bin` on the target and `ez80asm` on the host's
`PATH`.

## 2. Pipeline

For each input, by extension:

| Extension | Steps |
|---|---|
| `.c` | `cpp` → `.i` → `cc1` → `.ir` → `cc2` → `.s` |
| `.i` | `cc1` → `.ir` → `cc2` → `.s` |
| `.s` | linked as is: the compiler's own output, or assembly you write (below) |
| `.asm` | assembled as is (no linking) |

**Functions in assembly.** A function or data written in assembly goes in
a `.s` file of its own, passed to agonc with the C files. It needs a few
marker lines for `ld` (object_format.md §1): the `;;unit` line may leave out
the id, which `ld` then works out; each function or object is a section
whose label is its C name with a `_` in front; and a section that uses
another symbol lists it with `;;ref`, so that `ld` links it in. The ABI
says where arguments are and what may be clobbered (abi.md §4, §10). For
example, `int twice(int n);` in C, with this `twice.s`:

```
;;agonc-object 1
;;unit twice.s
;;sect code _twice g
_twice:                         ; int twice(int n)
        ld      hl,3
        add     hl,sp
        ld      hl,(hl)         ; n: the first argument slot, above the return address
        add     hl,hl
        ret
;;end
```

`agonc -o prog.bin prog.c twice.s` builds the program. Inside a function,
`asm("...")` or `#asm` … `#endasm` adds a few instructions in place
(c89_spec.md §13).

The pass command lines are `cpp <in.c> <out.i> [-I dir]... [-D/-U ...]
[-date YYYYMMDD -time HHMMSS]` (the clock's date and time, which the driver
reads for `__DATE__` and `__TIME__`; without them `cpp` uses Jan 1 1980),
`cc1 <in.i> <out.ir> -u <unitname>` (the unit name, normally the input
file's base name with extension, is what `cc1` hashes into the unit id),
`cc2 <in.ir> <out.s> [-O]`, and `ld -o <out.asm> <files.s>... [--entry=sym]`.

Then, unless `-c`/`-S`/`-E` stops earlier: `ld` over all `.s` files plus
the implicit runtime and libraries → one `.asm`, then `ez80asm <file>.asm
<out> -m` → the binary. The driver checks the binary's size against the
layout printed by `ld` and reports "image too large" if code and data reach
the bss base.

Intermediate files go in `/tmp/` (created if missing) as
`<base>.i`, `<base>.ir`, `<base>.s`, `<out>.asm`, and are deleted after a
successful build unless `-save-temps`. The default output name is `a.bin`.

The driver stops at the first pass that fails and returns 200. Each pass
prints its own diagnostics (`file:line: message`) and returns 0 or 200; a
pass never prints a summary on success. `ez80asm` failures are reported
with its output, which the driver lets through unfiltered.

## 3. Options

| Option | Meaning |
|---|---|
| `file...` | inputs, any of the extensions above; at least one |
| `-o file` | output name (binary, or `.s`/`.i`/`.ir` under `-S`/`-E`) |
| `-c` | compile to `.s` only, one per input, named `<base>.s` beside the input |
| `-S` | stop after `cc2`; same as `-c` here (there is no separate assembler stage for a unit) |
| `-E` | preprocess only; write `.i` to `-o` or the console |
| `-I dir` | add an include directory (repeatable, searched in order) |
| `-L dir` | add a library directory (repeatable) |
| `-lname` | link `libname.s` found on the library path |
| `-D name[=val]`, `-U name` | define or undefine a macro for `cpp` |
| `-O`, `-O1`..`-O3`, `-Os` | optimise: `cc2`'s peephole pass and its faster code for signed comparisons (§8). On by default; `-O0` turns it off (the last one counts) |
| `-w`, `-Wall`, `-Werror` | suppress warnings; all warnings (default); warnings are errors |
| `--index lib.s...` | have `ld` write each library's index, `lib.idx`, which makes links that use it faster (object_format.md §9); nothing else is done |
| `-std=c89`, `-std=c90`, `-ansi` | strict mode (c89_spec.md §1), passed to `cpp` and `cc1` as `-ansi` |
| `-nostdlib` | do not add `crt0.s`, `rt.s`, `libc.s`, `libm.s`, `libagon.s` |
| `-lm` | link `libm.s` even if no unit asks for it (§4) |
| `-save-temps` | keep the intermediate files |
| `-Wl,--entry=sym` | extra root symbol for section selection |
| `-time` | print each pass's time and the whole build's (`time: cc1 1.24 s` ... `time: total 9.87 s`): hundredths of a second from MOS's clock; whole seconds on a PC |
| `-v` | as `-time`, and each pass's command line and the linker's layout summary |
| `--version` | print the compiler version and target language version |
| `-h`, `--help` | list the options |
| `--` | end of options |
| `@file` | read more arguments from `file`, one per whitespace-separated token (the bootstrap's response files) |

Unknown options are errors, as are gcc options that imply things this
platform cannot do (`-shared`, `-static`, `-g`, `-x`, `-M*`), each with a
one-line explanation. Options and inputs may be mixed in any order.

**`-o` never overwrites a source.** Before anything runs, the driver
refuses an `-o` that names one of the inputs (letters in either case, `/`
or `\`, and a leading `./` count as the same), or a source file's name
(`.c`, `.h`, `.i`, `.s`, `.asm`) other than the kind the mode writes: `.i`
for `-E`, `.s` for `-c` and `-S`, and none for a program. So
`agonc -o prog.c prog.c` and `agonc -o prog.c prog.bin` stop with an error
and leave `prog.c` as it was. (The output is removed before it is written,
so an `-o` naming a source would otherwise lose it even when the build
fails.)

Predefined macros: `__AGONC__` (1), `__EZ80__`, `__ADL__`, `__STDC__`,
`__FILE__`, `__LINE__`, `__DATE__`, `__TIME__`, GCC's size, type,
byte-order and floating-point macros, and `__STRICT_ANSI__` in strict
mode (c89_spec.md §13, item 9).

## 4. Library directories

| Directory | Contents |
|---|---|
| `/lib` | `stdio.h`, `string.h`, …, `libc.s`, `libm.s`, `libagon.s`, `crt0.s`, `rt.s`, and their indexes |
| `/lib/agon` | `mos.h`, `uart.h`, `vdp.h`, included as `<agon/vdp.h>`; their functions are in `libc.s` (what the C library itself uses) and `libagon.s` |
| `/usrlib` | user libraries: `foo.h` and `libfoo.s`, or a sub-folder per library |

`#include <name>`: `-I` directories, then `/usrlib`, then `/lib`.
`#include "name"`: the including file's directory first, then as for `<>`.
`-lfoo`: `-L` directories, then `/usrlib`, `/lib/agon`, `/lib`.

The library is in two parts. `libc.s` is linked into every program;
`libm.s`, the floating-point and `long long` part (their arithmetic
helpers, `printf`'s and `scanf`'s floating and `ll` conversions, `strtod`,
`<math.h>`, `strtoll` and the other `long long` functions), only into a
program that uses floating point or `long long`, since `ld` reads every
line of what it is given. The driver adds it when one of the program's
`.s` files has `;;ref ___fp_print` or `;;ref ___ll_print`, which `cc1` puts
in every function with a `float`, `double` or `long long` value
(ir_format.md §3, `R`); `-lm` adds it too, as on Unix, for a program whose
only such values are in hand-written assembly.

A third part, `libagon.s`, holds the Agon interface beyond what the C
library itself uses: the rest of the MOS API, the FatFS calls, the
serial port and the VDU commands (`<agon/mos.h>`, `<agon/uart.h>`,
`<agon/vdp.h>`). The driver always passes it last, as `--if-needed
/lib/libagon.s`: `ld` reads it (through its index) only when the rest of
the program leaves a function undefined, so a program that uses none
of it links as fast as before (object_format.md §9).
MOS 2.3.3 has no system variables, so these paths are compiled in; `-I`
and `-L` are the overrides. On the host the same relative layout is found
under the directory named by the `AGONC_ROOT` environment variable.

## 5. Command-line limits on the target

MOS rejects a command line over 246 characters and reads script lines into
256-byte buffers. The driver keeps each pass's argument string short by
passing file names only; include directories, macro definitions and
library lists that would not fit are written to `/tmp/<pass>.rsp` and
passed as `@/tmp/<pass>.rsp`, so **every pass accepts `@file`**, as the
driver does.

## 6. Exit status

0 on success. **200** on any failure (a pass error, an assembler error, a
missing file, a bad option). MOS prints no message of its own for 200, so
the only text the user sees is the driver's and the passes' diagnostics.
**130** after Ctrl-C (on the Agon): the program running when it was
pressed finishes or stops (the passes stop at once, with 130; the
assembler runs to its end), the driver prints `agonc: interrupted`,
removes the output that program was writing and the temporary files, and
runs nothing more.
In a `*Exec` script a non-zero status stops the script, which is the
gcc-style behaviour build scripts expect.

## 7. Examples

```
agonc hello.c                          -> a.bin
agonc -o prog.bin main.c util.c -lm
agonc -c util.c                        -> util.s
agonc -E -o main.i main.c
agonc -S main.c                        -> main.s
agonc -v -o t.bin t.c -Wl,--entry=_irq_handler
```

## 8. Implementation notes

What the implementation (`src/agonc/`) settles that the sections above
leave open.

- **The host layout.** Passes are found beside the driver (the directory of
  its `argv[0]`), `ez80asm` on the `PATH`, and `/lib`, `/usrlib` and `/tmp`
  under `$AGONC_ROOT`. On the host `$AGONC_ROOT/tmp` must already exist; on
  the Agon the driver creates `/tmp`. Because `cpp`'s compiled-in `/usrlib`
  and `/lib` are the Agon's, the host driver passes `-I$AGONC_ROOT/usrlib
  -I$AGONC_ROOT/lib` after the user's `-I` options.
- **`-Wl,a,b,...`** passes each comma-separated word to `ld`, not only
  `--entry=sym`. `-Wl,--moslet` links for the moslet area
  (object_format.md §7); that is how the driver itself is built.
- **Response files** (§5): the driver calls a pass directly, not through
  MOS's command line, so the limits that matter are the pass's own. A pass
  whose argument string exceeds 200 characters or 14 words gets all of it
  as `@/tmp/<pass>.rsp`. The 14 is the stage-1 passes' limit: AgDev's
  start-up code stops splitting at `argc` 15, the program's own name
  included, and silently drops the rest of the line.
- **The assembler's success is its output file.** The Agon build of
  ez80asm 2.3 returns 0 even after errors (it removes its output instead),
  so the driver removes the output before assembling and fails if none
  appears. The image check (§2) reads `ld`'s layout lines (`org`,
  `__bss_size`, `__bss_base`) from the head of the `.asm`; an `.asm` input
  without them is not checked.
- **Inputs.** An `.asm` input must be the only input. An input that `-E`,
  `-c` or `-S` does not use (a `.s`, say) draws a warning. Two `.c`/`.i`
  inputs with the same file name, compared without case as FAT does, are
  an error: they would share temporary files and a unit id. `-o` with `-E`,
  `-c` or `-S` needs a single compiled input.
- **Messages** are `agonc: error: ...` and `agonc: warning: ...`; `-v`
  prints each command as it runs it.
- **`-O`**: `cc2`'s peephole pass. After a
  function's instructions are made, short runs are rewritten: a local's
  reload after its store goes, `push hl`/`pop de` around a load becomes
  `ex de,hl`, small constants become `inc`/`dec` and byte stores, and
  signed comparisons are made inline instead of through the helpers. A
  rule that needs a register to be unused afterwards checks the code that
  follows and gives up at anything it cannot follow. With `-O0` the
  peephole pass is off. The compiler and its library are built with `-O`
  (the bootstrap's response files and scripts). It is the driver's
  default; `cc2` run on its own needs `-O`.
