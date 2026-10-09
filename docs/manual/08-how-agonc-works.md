# How agonc works

agonc is small enough to read. This chapter is a map of it, for the curious
and for anyone who wants to change it; the source files open with longer
explanations of their own.

## Five programs in a row

A compile is a pipeline of separate programs, each reading a file and writing
the next:

```text
hello.c ─cpp─► hello.i ─cc1─► hello.ir ─cc2─► hello.s ─┐
                                                       ├─ld─► a.asm ─ez80asm─► a.bin
                   crt0.s, rt.s, libc.s, ... ──────────┘
```

| Pass | Reads | Writes | Its job |
|---|---|---|---|
| `cpp` | C source | `.i`, preprocessed C | `#include`, macros, `#if`, comments removed |
| `cc1` | `.i` | `.ir`, the intermediate form | parsing, types, scopes, constant folding, every diagnostic about C |
| `cc2` | `.ir` | `.s`, assembly with markers | choosing eZ80 instructions, the peephole optimiser |
| `ld` | `.s` files and libraries | one `.asm` | keeping what the program uses, laying out memory |
| `ez80asm` | `.asm` | the program, `.bin` | assembling |

`agonc` itself only runs the passes in turn, with the right file names and
options, and stops at the first failure. On the Agon it is a *moslet*: it
runs in the 32 KB set aside for MOS commands at `0x0B0000`, so that each pass
can load into the 448 KB of user memory below it. A pass is loaded from the
card, runs, and returns its status to the driver.

Splitting the work this way keeps each program small enough for the Agon's
memory, and every file between the passes is plain text that can be read,
compared and even written by hand: `agonc -save-temps` keeps them all in
`/tmp/agonc`.

## The passes

**cpp** handles C's preprocessing language, with C99's variadic macros
and `_Pragma` outside strict mode: it reads the source line
by line, expands macros (with the rules for `#`, `##` and rescanning), keeps
a stack for `#if`, and opens only one file at a time.

**cc1** is the front end. It reads the preprocessed source once, from start
to end, and writes each function's intermediate form as it goes, so it never
holds a whole file in memory. Expressions become trees, which it types and
folds; a statement's tree is written in postfix order, with jumps and labels
for control flow. The intermediate form has no C types, only kinds of value
(24-bit, 32-bit, float, double, 64-bit) and sizes; it is described in
[the intermediate format](../ir_format.md).

**cc2** is the code generator. It rebuilds each statement's tree and walks it
with a simple register model: the value being computed is in `HL` (or `E:UHL`
for 32 bits), the other operand of a binary operator in `DE`, and anything
waiting is on the stack. What the eZ80 cannot do in a few instructions (a
multiplication, a division, anything on `long` or `double`) is a call to the
runtime. With `-O`, the default, a peephole pass then rewrites short runs of
instructions into shorter ones. cc2 knows the size of every instruction, so it
can choose the short jump wherever it reaches and leave the assembler fewer
labels to keep.

**ld** is not a full linker, because there is no binary object format: a
`.s` file is assembly with marker lines that name its sections and what each
one refers to. ld starts from `main` and the start-up code, follows the
references to find every section the program can reach, orders them, and
writes them out as one file for ez80asm. Everything else, including most of
the library, is left out. A library's index (`.idx`) lets ld find its sections
without reading the whole library. The format is described in
[the object format](../object_format.md).

**The runtime** is `crt0.s`, which starts a program (a stack, zeroed data,
`argc` and `argv`, Ctrl-C) and ends it, and `rt.s`, the helpers compiled code
calls. How the two halves agree on registers, arguments and the stack is
[the ABI](../abi.md).

## How agonc was built

agonc is written in the C it compiles, and builds itself on the Agon. The
first copy was compiled on a PC by AgDev; that copy then compiled agonc's own
source on the Agon, and the result compiled it again. The second and third
builds are identical, byte for byte, which is the evidence that the compiler
reproduces itself faithfully. [BUILDING.md](../../BUILDING.md) explains the
steps, which anyone can repeat.

The arithmetic the compiler does on its own (folding constants, reading
floating-point numbers) is written so that it gives the same answers built by
any C compiler, on a PC or on the Agon: the compiler uses no floating point
itself, and its 32-bit and 64-bit arithmetic is done in pieces that fit
24-bit `int`. The same code, built into the library, does `float`, `double`
and `long long` for programs, with the most-used parts in assembly.

## Where to read more

| To learn about | Read |
|---|---|
| the language and library, precisely | [docs/c89_spec.md](../c89_spec.md) |
| the `agonc` command, precisely | [docs/driver.md](../driver.md) |
| registers, calls, frames, memory | [docs/abi.md](../abi.md) |
| `.s` files and the linker | [docs/object_format.md](../object_format.md) |
| the intermediate form | [docs/ir_format.md](../ir_format.md) |
| each pass | the comment at the top of its source in `src/` |
| building and testing agonc | [BUILDING.md](../../BUILDING.md) |
