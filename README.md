# agonc, a C compiler for the Agon

## Preamble: History and motivation

I have been playing with the
[Agon Light](https://www.thebyteattic.com/p/agon.html),
[RC2014](https://rc2014.co.uk/), and other retro or
[CP/M](https://en.wikipedia.org/wiki/CP/M) systems for a few years now.
There's something beautiful about trying to do a lot with just a little
hardware. In pursuit of trying to do a lot with only a little hardware, I've
taken to solving [Project Euler](https://projecteuler.net/) problems on
these systems. Obviously, I've relaxed the "one minute" time limit, but I
still enjoy much of the challenge of trying to fit the solution to a
problem within the constraints of such small systems.

For most of this time, I've been using the [HI-TECH C
Compiler](https://github.com/agn453/HI-TECH-Z80-C) running on the
[ZINC](https://github.com/nihirash/ZINC) compatibility layer. And that's a
fantastic way to do things! You end up with code that could theoretically
run on systems from the 70s. But I found myself writing complicated
extensions and work-arounds to get the problems solvable, and working in a
flat filesystem is NOT a good way to enable code-reuse.

For example, [Problem 22](https://projecteuler.net/problem=22) requires
sorting a 46KB text file. Well, there goes all your memory, or you have to
sort on disk. Either way, it gets out of hand!

I found myself writing a library to use ALL of the Agon Light's memory. To
do so, I had to get down to assembly, and then all the way down to in-line
machine code, since the assembler included with HI-TECH C is not aware of
the ez80 ADL mnemonics. Absolutely a fantastic challenge, and one I probably
should share with others in the future! Good enough, if that's the only
option.

But now it's not the only option. With AI, I can choose the level of
difficulty that I want to accept. I DO want to be constrained by the memory
and processing speed of the Agon Light, but I DON'T want to be constrained
to keeping all my files in one directory, manually shuffling files in and
out, or writing non-portable libraries to use the system's full memory.

So, with a few weeks of effort in [Claude
Code](https://claude.com/product/claude-code) (and a couple of attempts),
I've managed to create a C compiler that really seems to hit a sweet spot
for me. I still get to solve my problems on a constrained system, and if I
want, I still can backport them to HI-TECH C running on ZINC, or my RC2014.

And I think other people may find this useful as well, so I've decided to
release it. Claude will take it from here.

-Adam Hemphill

## About agonc

agonc is a C compiler that runs on the Agon Light and Agon Console8, the
eZ80 computers, and compiles C programs into MOS programs on the Agon
itself. No PC is needed to use it. It compiles ANSI C89, with its whole
standard library, plus some of C99, and it compiles itself.

**This is a beta: version 1.1.0-beta.1, the first public release.** It
passes its own tests and three free C test suites, and it has run on real
hardware, but few people have used it yet. Reports of anything that goes
wrong are very welcome ([Contributing](#contributing)).

```text
agonc -o hello.bin hello.c       compile, link and assemble a program
hello                            run it
agonc -o game.bin main.c gfx.c   several files
```

## What it has

- **C89, all of it**, with a strict mode (`-ansi`) that conforms, and a
  default mode that adds C99's `long long`, `//` comments and a few other
  things existing programs use.
- **The whole C89 library**: files on the SD card, `printf` and `scanf` with
  floating point, `<math.h>`, `<time.h>` on the Agon's clock, `<signal.h>`
  with Ctrl-C, and the rest.
- **IEEE 754 `float` and `double`**, correctly rounded, done in software
  (the eZ80 has no floating-point hardware), and 64-bit `long long`.
- **The Agon's own interface**: every MOS 2.3.3 call, the serial port, and
  every documented VDU command for the VDP's graphics, sprites, sound and
  buffers, in `<agon/mos.h>`, `<agon/uart.h>` and `<agon/vdp.h>`.
- **gcc-style options** (`-o`, `-c`, `-I`, `-D`, `-l`, `-O0`, `-Werror`,
  ...), inline assembly, and a linker that keeps only the code a program
  uses.
- **It builds itself on the Agon**, and reproduces itself byte for byte.

It runs under MOS 2.3.3, and under MOS 3, which has been tested on the
emulator.

## Install

Download `agonc-1.1.0-beta.1.zip` from the
[Releases page](https://github.com/ahemphil/agonc/releases) and unzip it
onto the root of the Agon's SD card, merging its folders with the
ones already there. It adds `/mos/agonc.bin`, `/bin/agonc/`, `/lib/`,
`/usrlib/`, `/tmp/` and the ez80asm assembler it was tested with. Then, at
the MOS prompt, `agonc --version`.

To build agonc from source instead, on an Agon or a PC, see
[BUILDING.md](BUILDING.md).

## Documentation

- **[The manual](docs/manual/README.md)**: a quick start, the command and
  its options, the language, the compiler's messages, the C library, the
  Agon library, porting from AgDev, and how agonc works. `make manual`
  turns it into a PDF for printing; the PDF is also attached to each
  release.
- **[Examples](examples/README.md)**: short commented programs for files,
  graphics, sound, the serial port and floating point.
- **The specifications** in [docs/](docs): the language and library
  ([c89_spec.md](docs/c89_spec.md)), the command
  ([driver.md](docs/driver.md)), the ABI ([abi.md](docs/abi.md)), the object
  format ([object_format.md](docs/object_format.md)) and the intermediate
  format ([ir_format.md](docs/ir_format.md)).
- **[BUILDING.md](BUILDING.md)**: building agonc on an Agon or a PC, checking
  the result, and running the tests.

## How it was tested

agonc is built from source on an Agon (or the emulator), starting from a
first copy compiled on a PC with [AgDev](https://github.com/pcawte/AgDev),
and the copy it builds of itself is identical, byte for byte, to the copy
that built it, under MOS 2.3.3 and (on the emulator) MOS 3.0.2. Every
usable C89 test of three free C test suites passes in strict mode:
c-testsuite 137 of 137, GCC's torture tests 577 of 577, and SDCC's
regression tests 644 of 644 files. agonc's own suites test each pass, the
libraries clause by clause, the examples and the bootstrap, on the emulator
(`make check`).

## Built with Claude

agonc was written by Claude, Anthropic's AI model, working with Adam
Hemphill, who set its goals and requirements, made the decisions, and
tested it on real hardware. Commits made this way carry a
`Co-Authored-By: Claude` line. Work Adam does by hand will be identified as
such.

## Contributing

Bug reports and suggestions are welcome as GitHub issues: say what you
compiled, what you expected and what happened, ideally with a small
program that shows it. Pull requests are welcome too, though what goes in
is decided case by case.

## Licence

MIT: see [LICENSE](LICENSE). Copyright (c) 2026 Adam Hemphill. The ez80asm
assembler in `third_party/ez80asm/` and in the release is Jeroen Venema's,
under its own MIT licence; `lib/libc/math.c` keeps the notice of fdlibm,
from which its algorithms come.

## Layout

| Folder | What it is |
|---|---|
| `src/` | the compiler: `cpp`, `cc1`, `cc2`, `ld` and the driver `agonc`, plus `common/` code they share |
| `lib/` | what compiled programs use: the runtime (`rt/`), the C library (`libc/`) and the Agon interface (`agon/`) |
| `examples/` | short commented programs |
| `docs/` | the manual (`manual/`) and the specifications |
| `bootstrap/` | the scripts and response files that build the compiler on an Agon |
| `tools/` | `concat` and `compare`, two small utilities the bootstrap builds and uses |
| `stage1/` | the AgDev makefile for the first, PC-built copy |
| `tests/` | the test suites (Python, and the emulator) |
| `third_party/` | ez80asm's Agon binary and licence; local installs of other tools (not in git) |
