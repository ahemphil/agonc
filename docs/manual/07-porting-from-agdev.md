# Porting from AgonDev and AgDev

[AgonDev](https://github.com/AgonPlatform/agondev) is the PC-hosted C
toolchain most Agon programs now come from: an LLVM-based compiler (clang
15) with the GNU assembler and linker, and a makefile-driven build.
[AgDev](https://github.com/pcawte/AgDev), the TI-84 Plus CE toolchain
adapted for MOS, came before it, and AgonDev took over much of its
library and its Agon headers' names. Their programs and agonc's are all
MOS executables loaded at `0x040000`, with 24-bit `int` and pointers, so
most C programs move across with a short list of changes. This chapter is
that list, checked against AgonDev v0.22 and AgDev 3.1.0: the language,
the types, the C library, the Agon headers, assembly, the build, the
command line, the exit status and memory, and then what one real program
needed. The last section goes the other way: back to HI-TECH C and CP/M.

In short:

1. The Agon headers need no change: agonc has AgonDev's
   (`<agon/mos.h>`, `<agon/vdp.h>`, `<agon/keyboard.h>` and the rest),
   with their names and arguments. AgDev's `<mos_api.h>` and
   `<agon/vdp_vdu.h>` work too, with a warning, as in AgonDev.
2. Rewrite C11's additions (`_Static_assert` is the common one),
   variable-length arrays, complex numbers and uses of `<wchar.h>` and
   `<wctype.h>`, which agonc does not have; the rest of C99 compiles in
   the default mode.
3. Replace the few library functions agonc does not have, such as
   `strdup`, and add the newline that both toolchains' `fputs` wrote.
4. Expect `double` to be 64 bits, and slower.
5. A program that uses Ctrl-C as a key must ignore `SIGINT`: agonc stops
   a program at Ctrl-C.
6. Build with `agonc` commands instead of a makefile: one per library
   (`--lib`) and one for the program. A prebuilt `.a` library cannot be
   linked; it must be built from its source.

## The language

AgonDev's and AgDev's compiler takes C99 to C17, and C++. agonc takes C99 but for
variable-length arrays, complex numbers and the wide-character library,
with C89 in strict mode
([The language](03-the-language.md#the-two-modes)). C11 and C17's
additions (`_Generic`, `_Static_assert`, anonymous structures and the
rest) and C++ have no counterpart. A `_Static_assert` at file scope has
a C99 form that fails the same way, at compile time:

```c
/* _Static_assert(sizeof(buf) >= NAME_MAX + 1, "buf holds a name"); */
typedef char buf_holds_a_name[(sizeof(buf) >= NAME_MAX + 1) ? 1 : -1];
```

The features agonc lacks fail with plain errors, such as `not an integer
constant expression` for `int a[n];`. The
[Messages](04-messages.md#code-written-for-c99-or-gcc) chapter has a table
of these constructs, the message each produces and what to write
instead. Two more differences catch ported code:

- Calling a function with no declaration in scope is an error in the
  default mode (`call to undeclared function f`). Include the header, or
  declare the function before its first use.
- GCC's `__attribute__((...))`, which AgonDev's and AgDev's headers use,
  is accepted and ignored; GCC's extended `asm` with operands is not
  ([below](#inline-assembly)).

Identifiers are significant to 47 characters and a longer one is an error,
which every name in AgonDev's Agon headers satisfies.

## Types

| Type | AgonDev and AgDev | agonc |
|---|---|---|
| `char` | 8 bits, signed | 8 bits, signed |
| `short`, `int`, `long` | 16, 24, 32 bits | 16, 24, 32 bits |
| `long long` | 64 bits | 64 bits (default mode only) |
| pointers, `size_t` | 24 bits | 24 bits |
| `float` | 32 bits | 32 bits |
| `double` | **32 bits**, the same as `float` | **64 bits** |
| `long double` | 64 bits | 64 bits |

The one difference that changes results is `double`. In AgonDev and
AgDev it is `float` under another name (their compiler is the same, and
AgDev's library defines `acos` as `acosf`, for instance); in agonc it is
IEEE 754 binary64, with about 16 significant
digits instead of 7, and every operation done in software on 8-byte
values. A program that used `double` for speed gets more precision and
less speed: change it to `float` where 7 digits are enough. agonc's
`<math.h>` has the `float` functions too (`sqrtf` and the rest), but they
work in `double` and round the result, so they are no faster than the
`double` ones. A `float` passed to `printf` is
promoted to `double`, as in any C. A file of `double` values written by an
AgonDev or AgDev program holds 4-byte values: read them into `float`. What floating
point and `long long` cost is in [The
language](03-the-language.md#floating-point-and-long-long-cost-time).

Structures have no padding in agonc, and bit-fields are allocated in
bytes from the least significant bit ([the ABI](../abi.md), section 2). Do
not rely on the other toolchains' layout matching it in data written to
files.

## The C library

agonc has the standard library, C99's included ([The C
library](05-c-library.md)), and AgonDev's and AgDev's programs mostly use
that part. AgonDev's C library is AgDev's, so the differences are the same
for both:

| AgonDev and AgDev | agonc |
|---|---|
| `fputs(s, f)` writes a newline after `s` | writes `s` only, as C requires: add the `'\n'` |
| `strdup`, `strndup`, `strnlen` | not provided: `malloc` and `strcpy`, or a loop |
| `strcasecmp`, `strncasecmp` | not provided: compare `tolower` of each character |
| `isascii(c)` | `((c) & ~0x7F) == 0` |
| `gets_s(s, n)` | `fgets(s, n, stdin)`, which keeps the newline |
| `quick_exit`, `at_quick_exit`, `on_exit` | `exit` and `atexit` |
| `<wchar.h>`, `<alloca.h>` | not provided |
| `EXIT_FAILURE` is 1 | `EXIT_FAILURE` is 200 ([exit status](#exit-status)) |
| `RAND_MAX` is 8388607 | `RAND_MAX` is 32767, and the sequence differs |

The `fputs` difference is the one to look for: output that relied on the
extra newline runs together in agonc. `puts` adds a newline in all three.
The headers AgDev keeps from the calculator toolchain (`graphx.h`,
`keypadc.h`, `fileioc.h` and the like) have no counterpart.

agonc's library also has what theirs lacks, among them `setvbuf`,
`tmpfile`, `rename`, `perror`, `rewind`, `clearerr`, `fgetpos`,
`<locale.h>` and `<signal.h>`, through which Ctrl-C stops a program
([below](#exit-status)).

## The Agon headers

| AgDev or AgonDev | agonc |
|---|---|
| `<agon/mos.h>`, `<agon/vdp.h>` (AgonDev) | the same |
| `<mos_api.h>`, `<agon/vdp_vdu.h>` (AgDev's names, kept by AgonDev as deprecated forwards) | the same forwards, with the same warning: change them to `<agon/mos.h>`, `<agon/vdp.h>` |
| `<agon/keyboard.h>`, `<agon/gpio.h>`, `<agon/joystick.h>`, `<agon/timer.h>`, `<ez80f92.h>` (AgonDev) | the same |
| `<agon/vdp_key.h>` (AgDev) | `<agon/keyboard.h>`, or `mos_set_key_handler` and `mos_getkbmap` in `<agon/mos.h>` |
| UART calls in `<agon/mos.h>` | the same calls, or `<agon/uart.h>` |

agonc's Agon headers follow AgonDev's, release v0.22 ([The Agon
library](06-agon-library.md#names)): where AgonDev has a call that MOS
2.3.3 provides, agonc's has its name and arguments, with `int` where
AgonDev has `uint8_t` or `bool`, and its constants, `sysvar_` offsets and
`SYSVAR` structure are there too. AgonDev took over AgDev's names, so most
programs written for either compile unchanged. These need changing:

| AgDev or AgonDev | agonc |
|---|---|
| `vdp_font_select(id)` (AgDev) | `vdp_font_select(id, flags)`, flags 0 for the plain case (AgDev's sends no flags byte, though the command has one) |
| `SYSVAR`'s `scrpixelR`, `scrpixelB`, `scrpixelG` | `scrpixel`, the three bytes as one 24-bit value, red lowest (the byte names need C11's anonymous structures) |
| `getsysvar_rtc()` returns a pointer to a structure | returns the six bytes; read the clock with `time` and `localtime` |
| `UART` structure for `mos_uopen` | `struct mos_uart`, or `uart_open(baud, bits, stop, parity, flow)` |
| `mos_ugetc()` above 255 when no byte came | -1 |
| `mos_editline(buf, len, clear)` | `clear` is bit 0 of a flags argument, so 1 still clears; returns the key that ended the edit |
| `mos_port_read`, `mos_port_write` (AgDev) | `io_in`, `io_out` in `<agon/gpio.h>`, as in AgonDev |
| `BYTE` (AgDev) | `unsigned char` |
| AgonDev's calls for MOS 3 only (`ffs_unlink`, `mos_gsinit`, `mos_readvarval`, ...) | not provided: agonc targets MOS 2.3.3 |

Calls whose AgDev form differed from AgonDev's follow AgonDev:
`mos_oscli(cmd, argv, argc)`, `ffs_fread` and `ffs_fwrite` returning the
count, `ffs_stat(info, path)`, `vdp_mode` returning the mode,
`vdp_capture_bitmap(top, left, bottom, right, n)`, the audio envelopes
with their data, and `vdp_triangle` and `vdp_circle`, which take the
corners, and the centre and radius, and draw outlines. AgDev's
`vdp_triangle`, `vdp_circle_radius` and `vdp_circle` used the last points
plotted and sent PLOT 80, 144 and 148, which move the graphics cursor
without drawing; agonc's `vdp_circle_radius`, which keeps AgDev's form,
sends 145 and draws. `vdp_cursor_tab` sends its first argument as the
column, whatever AgDev's parameter names suggest.

**Keyboard events.** AgonDev's queue of key events, `<agon/keyboard.h>`
(`kbuf_init`, `kbuf_poll_event` and the rest), is agonc's too, built on
agonc's key handler so that Ctrl-C keeps working beside it. AgDev's `vdp_set_key_event_handler` passes a
`KEY_EVENT` whose fields are the ASCII code, the modifiers, the virtual key
code and the up or down state. agonc's `mos_set_key_handler` passes the
same four bytes, in that order, as `packet[0]` to `packet[3]`. For keys held
down, `mos_getkbmap` returns MOS's map, which is numbered by BBC BASIC's
key numbers, not by the virtual key codes that index AgDev's `vdp_key_bits`;
`MOS_SYSVAR->vkeycode` and `vkeydown` give the last key by virtual code.

**Keeping the include lines.** AgDev's include lines work, with a warning
at each. A small header file of your own, in a folder given with `-I`, is
found first, so a large program can keep them quietly while you port it,
and add what else it needs:

```c
/* compat/mos_api.h */
#include <agon/mos.h>
typedef unsigned char BYTE;
```

with `agonc -Icompat ...`. The compiler then reports the calls that still
differ, such as `wrong number of arguments to vdp_font_select (1 given, 2
expected)`.

## Inline assembly

agonc has two forms, both only inside a function: `asm("...")`, whose
string may hold several lines separated by `\n`, and a `#asm` ... `#endasm`
block. The text is copied as written into the function and assembled by
ez80asm, the Agon's assembler, not by the GNU assembler AgonDev uses or
the fasmg-based one AgDev's, so check directives and number syntax against
ez80asm. GCC's extended
form, with operands and clobbers after colons, is not accepted. The rules,
from [the ABI](../abi.md) (sections 4, 5 and 10):

- The code may change any register except `IX` and `SP`, and must leave
  the stack balanced. Nothing in a register survives it, and nothing is in
  a register when it starts: unlike AgonDev's and AgDev's code generator,
  agonc keeps no variable in a register.
- Arguments are in 3-byte slots from `(ix+6)`, the first argument
  lowest; a `long` or `float` takes two slots, a `double` or `long long`
  three. A `char` argument is sign-extended through its whole slot, where
  their compiler leaves the upper bytes undefined.
- Locals are in the frame below `IX`: the scalars first, in declaration
  order from `(ix-3)` downwards, each in whole 3-byte slots (two for a
  `long` or `float`, three for a `double` or `long long`), then arrays and
  structures. A result is passed back by storing it in a local.
- A function returns a `char` in `HL`, sign-extended, where their
  compiler returns it in `A`; a `long` or `float` in `E:UHL`.
- Labels are ez80asm `@local` labels; a global label must be unique in the
  whole program, and agonc warns at each.

A function reading an eZ80 port (`<agon/gpio.h>`'s `io_in` is this one):

```c
int port_read(int port)
{
    int r;                      /* the first local: (ix-3) */

    asm("ld bc,(ix+6)\n"        /* port: the first argument */
        "in a,(bc)\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");        /* into r */
    return r;
}
```

A whole function in assembly goes in a `.s` file linked with the C files.
AgonDev's `.s` and `.asm` files, in the GNU assembler's syntax (`.global`,
`.section`), and AgDev's `.asm` and `.src` files, with fasmg's `section`,
`public` and `extern`, become agonc `.s` files with marker lines for the
linker: a `;;sect` line for each function or object and a `;;ref` line for
each symbol it uses ([Assembly](02-using-agonc.md#assembly)). C names carry
a leading `_` in all three.

**Prebuilt libraries.** AgonDev's and AgDev's `.a` and `.o` files hold
ELF objects, which agonc's linker cannot read: its libraries are `.s`
files. A library that comes only prebuilt has to be built again from its
source, its assembly rewritten as above.

## The build

AgonDev builds on a PC with `make`, from a makefile that includes
`agondev-config --makefile`, names the program and its options, takes the
sources from `src/` and leaves a `.bin` file in `bin/` to copy to the SD
card; a library is a project of its own, built with `make lib` into a `.a`
file. AgDev's makefiles work the same way. agonc needs no makefile. On the
Agon:

```text
agonc -o game.bin main.c sprites.c sound.c
```

For many files, a wildcard (`agonc -o game.bin src/*.c`) or a response
file (`agonc -o game.bin @game.rsp`) keeps the command within MOS's 240
characters. A makefile's libraries become `--lib` commands, each compiling
a folder into an indexed library (`agonc --lib -o libcore.s core/*.c`),
linked with `-L. -lcore`. The same commands work on a PC with agonc's PC
build, which produces the same program byte for byte, within the same
limits ([Building on a PC](02-using-agonc.md#building-on-a-pc)).

agonc compiles every file it is given, every time; a makefile's header
tracking has no counterpart. A library compiled once is used as it is
until its sources change, so the folders that change least are the ones
to make into libraries.

The makefile settings mostly have no counterpart:

| Makefile setting | In agonc |
|---|---|
| `NAME` | `-o name.bin` |
| `LIBS = -lname` (AgonDev) | `-L folder -lname`, the library built with `--lib` |
| `CFLAGS = -Wall -Wextra -Oz` | leave out: `-Wall` is accepted, but `-Wextra` and `-Oz` are unknown options; `-O` is the default |
| `RAM_START`, `RAM_SIZE`, `STACKTOP` (AgonDev); `INIT_LOC`, `BSSHEAP_LOW`, `BSSHEAP_HIGH`, `STACK_HIGH` (AgDev) | none: the layout is fixed ([memory](#memory)) |
| `LDHAS_ARG_PROCESSING` (AgonDev), `HAS_ARG_PROCESSING` (AgDev) | none: quotes always work, redirection never ([below](#command-line-arguments)) |
| `LDHAS_EXIT_HANDLER` | none: the status always reaches MOS |
| `SERIALPORT`, `BAUDRATE`, `FAE_HOME` and the other upload and emulator settings (AgonDev) | none: agonc builds on the Agon itself, or the `.bin` is copied |
| `ALLOCATOR` (AgDev) | none: `free` always returns memory to the heap |
| `LTO` (AgDev) | none: the linker keeps only the functions a program reaches |

Two source files may not have the same name, even in different folders,
and one command takes at most 32 source files; more go into libraries
([Messages](04-messages.md#when-a-table-is-full)).

## Command-line arguments

| Item | AgonDev and AgDev (by default) | agonc |
|---|---|---|
| `argv[0]` | `progname.bin` (AgonDev), the program's name from the makefile with `.bin` (AgDev) | an empty string |
| separators | spaces | spaces and tabs |
| `"two words"` | two arguments, quotes kept | one argument, quotes removed |
| most entries, `argv[0]` included | 15 | 32 |

Further words are dropped without a message in all three. The optional
`LDHAS_ARG_PROCESSING` (AgonDev) and `HAS_ARG_PROCESSING` (AgDev) add
quoting and `<`, `>` and `>>` redirection; agonc
has no redirection, so a program that read redirected input should take a
file name and open it (`freopen(name, "r", stdin)` keeps the rest of the
code as it was). A program that prints its own name from `argv[0]` needs it
written in.

## Exit status

agonc passes the status to MOS unchanged, as AgonDev's start-up code does
by default. AgDev's default start-up code instead ends every program by
printing a short word (Okay, Quit or Abort, for statuses 0, 1 and anything
else) and returning 0 to MOS, so MOS never sees a failure. MOS prints its
own message for statuses 1 to 25 (26 on MOS 3), so a program that returns
1 for a failure makes MOS print "Error accessing SD card", and their
`EXIT_FAILURE` is 1. agonc's `EXIT_FAILURE` is 200, which prints nothing;
use it, not 1, for a failure. A non-zero status also stops a MOS `exec`
script, as a failure should ([Messages](04-messages.md#exit-statuses)).

A program built with agonc also stops when Ctrl-C is pressed, at its next
input or output, with status 130; AgonDev and AgDev have no `<signal.h>`.
A program that must not stop that way, such as an editor that uses Ctrl-C
for copy, can ignore the signal with `signal(SIGINT, SIG_IGN)` at the
start of `main`, or catch it ([The C library](05-c-library.md#signalh)).

## Memory

agonc has one pool from `0x040000` to `0x0AFFFF`: the program file at the
bottom, the zeroed data at the top, the heap growing up from the program
and the stack down from the zeroed data, with nothing to configure
([memory](03-the-language.md#memory)). AgonDev's default is much the same,
one area from `0x040000` with the stack starting at `0x0B0000`, which its
`RAM_START`, `RAM_SIZE` and `STACKTOP` move; a program that moved it, to
keep memory of its own at a fixed address, must take that memory from
`malloc` or a `static` array instead. AgDev's default layout puts code and
data from `0x040000`, the zeroed data and the heap together in `0x080000`
to `0x09FFFF`, and the stack below `0x0AFFFF`, so a program's code is
limited to 256 KB and its heap to what its zeroed data leave of 128 KB; a
large program or heap fits in agonc where it did not, but memory a program
used at a fixed address in that layout must come from `malloc` or a
`static` array too. A program built with agonc never touches `0x0B0000`
and above, where agonc itself and MOS live; nor does one built with the
other toolchains' default settings.

MOS's file handles work the same for all three: eight in all, not closed by MOS
when a program ends. agonc's `exit` closes every stream, but a handle from
`mos_fopen` must be closed with `mos_fclose`
([The C library](05-c-library.md#eight-handles-for-the-whole-session)).

## An example: the AED editor

[AED](https://github.com/avalonbits/aed), avalonbits' text editor
(release 1.6.3), is a sizeable AgonDev program: 29 C files in three
folders with 39 headers of its own, written in C99 throughout, built by a
makefile into two static libraries and the program, and linked with
`libhub.a`, the prebuilt client library of the hub launcher. It uses most
of AgonDev's Agon interface: the MOS file calls, VDU output through
`mos_puts` and `VDP_PUTS`, the system variables by their `sysvar_`
offsets, `waitvblank` and the `kbuf_` key queue. What it needed:

- Its three `_Static_assert`s rewritten as above.
- `libhub.a` rebuilt from hub's source, its assembly glue rewritten as a
  C file with inline assembly.
- One GNU-assembler file that nothing called, left out.
- `signal(SIGINT, SIG_IGN)` at the start of `main`, since Ctrl-C is AED's
  copy.

Nothing else changed. It builds in three commands, on the Agon or a PC,
from its own folder:

```text
agonc -w --lib -o libedcore.s -Isrc/core -Isrc/ui -Isrc -Ithird_party/hub-0.4.1/include src/core/*.c
agonc -w --lib -o libedui.s -Isrc/core -Isrc/ui -Isrc -Ithird_party/hub-0.4.1/include src/ui/*.c
agonc -w -o aed.bin -Isrc/core -Isrc/ui -Isrc -Ithird_party/hub-0.4.1/include src/*.c third_party/hub-0.4.1/lib/*.c -L. -ledui -ledcore
```

`-w` silences about 230 warnings for `static inline` functions in its
headers that a file does not call, which GCC and clang leave unremarked.
On an Agon the three take about nine minutes, most of it the
preprocessor reading the headers again for each file. The program is
143,898 bytes, against AgonDev's 102,529: agonc's code is larger, since
it keeps no variable in a register.

## Back to HI-TECH C and CP/M

A program written for agonc can often be carried back to
[HI-TECH C](https://github.com/agn453/HI-TECH-Z80-C) 3.09, the CP/M
compiler for the Z80, to run under CP/M on a Z80 machine or under
[ZINC](https://github.com/nihirash/ZINC) on the Agon. HI-TECH C follows
the draft ANSI standard of its time, prototypes included, so the best
starting point is code that agonc accepts with `-ansi`: strict mode turns
off `long long`, `//` comments, C99's other features and agonc's
extensions. What remains
is the machine:

| | agonc | HI-TECH C 3.09 on the Z80 |
|---|---|---|
| `int`, `unsigned int` | 24 bits | 16 bits |
| `short`, `long` | 16 and 32 bits | 16 and 32 bits |
| `long long` | 64 bits (default mode) | none |
| `float` | IEEE 754 single precision | HI-TECH's own 32-bit format |
| `double` | IEEE 754 double precision, 64 bits | the same as `float` |
| pointers | 24 bits | 16 bits |
| memory for a program | 448 KB | what CP/M leaves of 64 KB |
| files | folders and long names | CP/M's 8.3 names, one folder per drive |

**`int` shrinks.** A 24-bit `int` holds values to 8388607, so code that is
correct under agonc can overflow at 16 bits: a counter that reaches
40000, an `int` product of two values above 181, an `int` index past
32767. Where the size matters, say `short` or `long` rather than `int`, as
HI-TECH C's own manual advises; then the program means the same thing to
both compilers. `printf`'s `%d` follows `int`, so a value that needs more
than 16 bits needs `long` and `%ld`.

**`double` becomes single precision.** Results agree with agonc's to about
seven significant digits and differ after that, and a `double` holds
integers exactly only up to 2^24 (16777216) instead of 2^53. A program that
prints with `%.15g`, or counts in a `double`, behaves differently. HI-TECH
C keeps floating point in a library of its own: a program that uses it is
linked with `-LF`, or its `printf` and `scanf` will not convert floating
point.

**Memory is the big one.** A table that fits easily in agonc's 448 KB may
not fit in a CP/M program's space at all, which is why such programs end
up keeping data on disk. Plan the data structures for the smaller machine
first if both are the goal.

**Files and the rest.** File names must fit CP/M's 8.3 form, with no
folders (ZINC uses the current folder as its drive). `<agon/mos.h>`,
`<agon/uart.h>` and `<agon/vdp.h>` do not exist under CP/M, so keep
Agon-specific code in a file of its own. Both machines are little-endian,
but the two compilers need not lay out structures alike, so a file written
with `fwrite` of a whole structure is best not shared between them. And
where the signedness of a `char` matters, say `signed char` or `unsigned
char`.
