# Building agonc

agonc is written in the C it compiles, so building it from source is a
*bootstrap*: another compiler builds a first version, and that version builds
the real one. This guide covers that bootstrap, a quicker cross-build on a PC,
and the test suite.

## 1. How the bootstrap works

```
 On a PC                 On the Agon (or the emulator)
 ─────────               ──────────────────────────────────────────────────────
 AgDev ──► stage 1  ──►  stage 1 builds the C library, two small tools and
 (make stage1)           the driver                          (1-stage1.txt)
                         stage 1 + the driver build stage 2  (2-stage2.txt)
                         stage 2 is installed                (3-install.txt)
                         stage 2 builds stage 3, and every
                         file is compared with stage 2       (4-stage3.txt)
```

- **Stage 1** is the compiler's four passes (`cpp`, `cc1`, `cc2`, `ld`)
  built by [AgDev](https://github.com/pcawte/AgDev), an existing C compiler
  for the Agon that runs on a PC. It is only a stepping stone: nothing it
  produces contains any AgDev code.
- **Stage 2** is the compiler built by stage 1, on the Agon, from source.
  This is the finished compiler.
- **Stage 3** is stage 2 building itself again. If stage 3 is identical to
  stage 2, byte for byte, the compiler reproduces itself exactly; the last
  script checks that.

The driver, `agonc`, is the command you type. It runs the passes and then
ez80asm, the Agon's own assembler. It is a *moslet*: it lives in `/mos` and
runs in the 32 KB at `0x0B0000`, so each pass can have all of user memory.

## 2. Choose a path

**No build at all:** each release on the
[Releases page](https://github.com/ahemphil/agonc/releases) has a zip of
the compiler, built and tested. Unzip it onto the root of the Agon's
SD card, merging with the folders already there: it adds `/mos/agonc.bin`,
`/bin/agonc/`, `/lib/agonc/`, `/usrlib/`, `/tmp/` and `/bin/ez80asm.bin`, and
nothing else ([the manual's quick start](docs/manual/01-quick-start.md)).

| Path | You get | You need |
|---|---|---|
| **A. Bootstrap on the Agon** (sections 5–6) | the compiler built on the Agon from source, proven to reproduce itself | a PC with AgDev; an Agon or the emulator |
| **B. Cross-build on a PC** (section 7) | the same compiler, byte for byte, built in seconds, ready to copy to an SD card | a PC with a C compiler and ez80asm built for the PC |
| **C. Develop and test** (section 11) | the full test suite | path B's tools, Python 3, the emulator |

## 3. Prerequisites

| Tool | Version | For | Where |
|---|---|---|---|
| GNU make | 3.81 or later | every path | AgDev ships one (`bin/make`); on Linux and macOS the system's `make` works too |
| AgDev | 3.1.0 | A | https://github.com/pcawte/AgDev/releases. Follow its instructions, which include putting its `bin` folder on your `PATH` |
| An Agon Light or Console8 with MOS 2.3.x or 3.0.x | | A | or the emulator below |
| fab-agon-emulator | 1.2.5 | A (instead of an Agon), C | https://github.com/tomm/fab-agon-emulator/releases |
| MOS firmware | 2.3.3 (3.0.2 also works) | the emulator | https://github.com/AgonPlatform/agon-mos/releases: `MOS-2.3.3.bin` **and** `MOS-2.3.3.map`, in the same folder (without the `.map` the emulator cannot use a folder as its SD card) |
| A C89 compiler for the PC | gcc or clang | B, C | |
| ez80asm built for the PC | 2.3 | B, C | build it from https://github.com/AgonPlatform/agon-ez80asm (tag v2.3) |
| Python | 3.10 or later | C | https://www.python.org |

ez80asm's Agon build is included, in `third_party/ez80asm/` (MIT licence),
so path A needs no copy of it.

## 4. Get the source and configure

Clone the repository, or unpack a source archive. Every command below runs
in its top folder.

Tool locations are set in `config.mk`, which you should not need to edit.
Its defaults look for tools under `third_party/`. To use tools installed
elsewhere, create `config.local.mk` beside it (git ignores it) and set any
of these:

| Setting | Default | Used by |
|---|---|---|
| `HOSTCC` | `cc` | `make host`, `make cross` |
| `HOST_EZ80ASM_DIR` | `third_party/bin` | `make cross` (the folder holding the PC's `ez80asm`) |
| `PYTHON` | `python3` | `make lint`, `make check` |
| `EMULATOR` | `third_party/emulator/fab-agon-emulator` | `make emulator` |
| `MOS_ROM` | `third_party/mos/MOS-2.3.3.bin` | `make emulator` |
| `EMULATOR_FLAGS` | `--firmware console8 -u` | `make emulator` (`-u` runs the eZ80 as fast as your PC can) |
| `PANDOC`, `TYPST` | `pandoc`, `typst` | `make manual` |

Give `EMULATOR` and `MOS_ROM` as full paths: `make emulator` starts the
emulator from its own folder, where it finds its firmware.

For example, on Windows with MSYS2's clang:

```
HOSTCC = C:/msys64/clang64/bin/clang.exe
PYTHON = C:/Python312/python.exe
```

On Windows run `make` from PowerShell or `cmd`; the Makefile uses `cmd.exe`
for its commands there, as AgDev's own makefiles do.

## 5. Path A, on the PC: stage 1 and the SD card

**Build stage 1.**

```
make stage1
```

AgDev compiles each pass, printing `[compiling]` and `[linking]` lines, and
leaves them in `build/stage1/`:

| File | Size (bytes) |
|---|---:|
| `cpp.bin` | 48,190 |
| `cc1.bin` | 124,526 |
| `cc2.bin` | 68,044 |
| `ld.bin` | 28,023 |

The sizes depend on AgDev's version and do not matter: stage 2 comes out
the same either way.

**Build the SD-card tree.**

```
make sdcard
```

(This runs `make stage1` first.) `build/sdcard/` now holds
everything the Agon needs:

| On the card | What it is |
|---|---|
| `/bin/agonc/` | stage 1's four passes; the bootstrap replaces them with stage 2 |
| `/bin/ez80asm.bin` | the Agon's assembler |
| `/lib/agonc/` | the C headers and the runtime (`crt0.s`, `rt.s`); the bootstrap adds the library, `libc.s`, `libm.s` and `libagon.s`, and the indexes that make links fast (`.idx`) |
| `/usrlib/` | empty; for your own libraries |
| `/mos/` | empty; the bootstrap puts the driver here |
| `/agonc/` | the compiler's sources (`src/`, `lib/`, `tools/`), the bootstrap (`bootstrap/`), work folders (`out/`, `s2/`, `s3/`) and `licenses/` |

**Put it on the Agon.** Copy the *contents* of `build/sdcard/` to the root
of the Agon's SD card. This adds folders and files; the only things it can
replace are `/bin/ez80asm.bin` and files of the same names in `/lib/agonc`
and `/mos`.

**Or use the emulator.**

```
make emulator
```

This starts fab-agon-emulator with `build/sdcard/` as its SD card. The
emulator writes to that folder, so you can look at the results from the PC.
To start again from scratch, run `make sdcard` again.

## 6. Path A, on the Agon: the bootstrap

At the MOS prompt, run the four scripts in order. Each one prints what it
is doing, and its last line names the next script. If any command in a
script fails, MOS stops the script with `Error executing ...`; see section
10.

The times below were measured on the emulator running at the Agon's real
18.432 MHz, and, in brackets, with `-u` (as fast as the PC can go). All four
take about 21 minutes at 18.432 MHz. The emulator's SD card is a folder on
the PC, so on a real Agon the card's slower reads and writes add some
time.

**1. Stage 1 builds the library, the tools and the driver.**

```
exec /agonc/bootstrap/1-stage1.txt
```

It runs stage 1's passes directly, 48 times, to compile the C library's
eleven units, then `concat` (which joins them into `/lib/agonc/libc.s` and
`/lib/agonc/libm.s`), then `ld --index` (the libraries' indexes), then
`compare`, then the driver, which it writes to
`/mos/agonc.bin`. It works from `/` and names every file by its full path,
because MOS 3 runs a program given by its full path only from there. The
only output is ez80asm's, four lines for each program it assembles. Time: about 3 minutes
(50 seconds). It ends:

```
1-stage1 finished. Next: exec /agonc/bootstrap/2-stage2.txt
```

**2. Stage 2 is built.**

```
exec /agonc/bootstrap/2-stage2.txt
```

Now the driver is in charge: one `agonc` command per program, for example

```
agonc -o s2/cc1.bin @bootstrap/cc1.rsp
```

where `bootstrap/cc1.rsp` lists `cc1`'s sources and options. First it
builds `/lib/agonc/libagon.s`, the MOS and VDU interface, and its index. The
compiler says nothing when all is well, so again only ez80asm is heard
from, once per program (`Wrote s2/cc1.bin, 139234 bytes`). Time: about 6½ minutes (2
minutes).

**3. Stage 2 is installed.**

```
exec /agonc/bootstrap/3-install.txt
```

It replaces the four passes in `/bin/agonc` and the driver in `/mos` with
stage 2's (`Copying s2/cc1.bin to /bin/agonc/cc1.bin`, ...). From here on,
the compiler on your Agon is the one it built itself.

**4. Stage 2 builds stage 3, and they are compared.**

```
exec /agonc/bootstrap/4-stage3.txt
```

It builds every program again into `/agonc/s3`, and the library's units,
then compares each with stage 2's:

```
same: s2/cpp.bin s3/cpp.bin (50894 bytes)
same: s2/cc1.bin s3/cc1.bin (139234 bytes)
...
same: out/mos.s s3/mos.s (13092 bytes)
The bootstrap is complete: stage 3 is identical to stage 2.
```

A difference would print `DIFFERENT: ...` and stop the script. Time:
about 12 minutes (3 minutes).

**Afterwards.** The compiler is installed: the driver in `/mos`, the passes
in `/bin/agonc`, the library in `/lib/agonc`. `/agonc/out`, `/agonc/s2` and
`/agonc/s3` can be deleted; keep `/agonc/src` if you want the sources on
the Agon.

## 7. Path B: cross-building on a PC

```
make cross
```

This builds the compiler for the PC (`make host`, into `build/host/`), and
then uses it to build the Agon compiler from the same response files the
Agon uses. It takes a few seconds. `build/agon/` is then a complete SD-card
tree of the finished compiler, ready to copy to the root of a card:

| On the card | |
|---|---|
| `/mos/agonc.bin` | the driver |
| `/bin/agonc/cpp.bin`, `cc1.bin`, `cc2.bin`, `ld.bin` | the passes |
| `/bin/ez80asm.bin` | the assembler |
| `/lib/agonc/` | headers, runtime, `libc.s`, `libm.s` (the floating-point and `long long` part) and `libagon.s` (the MOS and VDU interface), and their indexes (`.idx`) |
| `/usrlib/`, `/tmp/` | empty (agonc keeps its own intermediate files in `/tmp/agonc`) |

These files are byte-for-byte identical to the ones the bootstrap builds on
the Agon (section 8 gives their checksums), which the test suite checks.

## 8. Checking your build

The finished compiler, from either path, should be exactly these files:

| File | Size (bytes) | SHA-256 |
|---|---:|---|
| `/mos/agonc.bin` | 18,716 | `bb4ea0506433378e46ea59ee7ecc436988040b59af31afc453d106747bf84cdb` |
| `/bin/agonc/cpp.bin` | 52,670 | `3313a9ed796ede02b3206adc4f4a1951263699f2d18033c1bdff0e9c5830909a` |
| `/bin/agonc/cc1.bin` | 151,363 | `61bc8477f7f7da18605af4e48c174faacae8d10b75058890c5da98f0d019e8b7` |
| `/bin/agonc/cc2.bin` | 81,700 | `2d4bcf9ee5cc386c5d52a1523b9a92d6b59e328da99d6ad8face9dee93d73e2c` |
| `/bin/agonc/ld.bin` | 33,839 | `c54e98d0fb023aa2cba30582d3ef54e5605e891484ec68795fdc8a9aca2a370b` |
| `/lib/agonc/libc.s` | 184,156 | `4a36745b881226ef51487b03dbb933cf9481d6f67e6e746f8990a5c8b914dbb0` |
| `/lib/agonc/libm.s` | 715,413 | `8f3628b3a7d7ca6e5004c7500ccce54503dd5f01b1accceff6a2cbc1448c6948` |
| `/lib/agonc/libagon.s` | 156,089 | `f1cd33da1abba1bcc134f56453a815c4b0488dc5b8358fae558f71fb9367b4d2` |
| `/lib/agonc/crt0.idx` | 729 | `d564384a2a734244a672ddff20794d93e26b98cb391a9c6d46f363b2eef1dd68` |
| `/lib/agonc/rt.idx` | 2,636 | `9ac6741ec40979e7f807c4555d1ad3cc734b50571dd9f516d029ff3be52c5888` |
| `/lib/agonc/libc.idx` | 24,083 | `3f99b45cb0cbbde474c65c5ced180dfe5228c431c8a584051844303d8ba9c814` |
| `/lib/agonc/libm.idx` | 83,389 | `25c85261f41d530dabe1bab46ce8e1823e7adb4f68233f5d03373d311fea8f30` |
| `/lib/agonc/libagon.idx` | 33,090 | `44e84fdc9bee8953ea34da31fb7cc57cc3f4b10ef97c37eb81b6b33bc2302e16` |

On a PC: `sha256sum` (Linux), `shasum -a 256` (macOS), or
`Get-FileHash` (PowerShell). The bootstrap's last script already checks
that stage 3 equals stage 2 on the Agon itself.

## 9. Your first program

Save this as `/hello.c` on the SD card:

```c
#include <stdio.h>

int main(void)
{
    printf("Hello from agonc\n");
    return 0;
}
```

Then, at the MOS prompt:

```
cd /
agonc -o hello.bin hello.c
hello
```

`agonc` without `-o` writes `a.bin`. Its options follow gcc's: `-c`, `-S`,
`-E`, `-I`, `-L`, `-l`, `-D`, `-U`, `-O0` (no optimising), `-Werror`, `-v` and more; `agonc -h`
lists them, `-time` shows how long each pass took, and [docs/driver.md](docs/driver.md)
has the details. `-o` refuses a source file's name, so a slip such as
`agonc -o hello.c hello.c` cannot overwrite your program. The language is
C99 but for variable-length arrays and complex numbers; `-ansi` is strict C89
([docs/c89_spec.md](docs/c89_spec.md) §1).

## 10. Troubleshooting

| What you see | What it means |
|---|---|
| `Error executing ... at line N` | a command in the script failed; MOS stops at the first failure. The message just above it says why (a compiler error prints `file:line: message`) |
| `Invalid command` | MOS could not find the program: `/mos/agonc.bin` or a file in `/bin` is missing or misnamed |
| `agonc: error: cannot run /bin/agonc/cc1.bin` | a pass is missing from `/bin/agonc` |
| a compile fails with `cannot open` after many runs | MOS has only 8 file handles and does not close a program's files when it ends; the driver closes them after every pass, but a crashed program can leave some open. Restart the Agon |
| ez80asm says `Filename too long` | it accepts names of at most 64 characters, path included |
| on the emulator, text on the screen is garbled or doubled | a display quirk of the emulator; it does not affect the files written |
| `make stage1` fails with an error about `cedev-config` | AgDev's `bin` folder is not on your `PATH` |
| `make cross` cannot find `ez80asm` | set `HOST_EZ80ASM_DIR` to the folder holding the PC's ez80asm |

## 11. Developing: the test suite

```
make check
```

builds everything (`make lint`, `make sdcard`, `make cross`) and runs every
test suite: the runtime, each pass, the C library, the driver, the
bootstrap itself on the emulator (section 6's four scripts, run unchanged on
`build/sdcard`, with every result compared against `make cross`), and a
check of the compiler's memory use while it builds itself. It takes about
twenty-five minutes. The suites are Python scripts in `tests/`, each opening with a
description of what it covers; they drive
the emulator headless through `tests/tools/run_emulator.py`, which expects
fab-agon-emulator in `third_party/emulator/` and MOS in `third_party/mos/`.
The suites were developed on Windows and run from PowerShell.

Other targets:

| Target | Does |
|---|---|
| `make host` | the compiler for the PC, in `build/host/` |
| `make lint` | checks what a C compiler cannot: no floating point in the compiler (the bootstrap compiler's `double` is 32 bits), no `//` comments, only the `printf` formats agonc's library has, `#asm` only beside a C version, and the bootstrap's response files |
| `make manual` | the user manual ([docs/manual](docs/manual/README.md)) as a PDF for printing, `build/manual/agonc-manual.pdf`: US Letter, double-sided, the cover in colour and the rest in black and white; needs [pandoc](https://pandoc.org) and [Typst](https://typst.app) |
| `make clean` | removes `build/` |

The user manual is [docs/manual](docs/manual/README.md). The design is
documented in `docs/`: the language and library
([c89_spec.md](docs/c89_spec.md)), the ABI
([abi.md](docs/abi.md)), the passes' intermediate format
([ir_format.md](docs/ir_format.md)), the object format and linker
([object_format.md](docs/object_format.md)) and the driver
([driver.md](docs/driver.md)).
