# Porting from AgDev

[AgDev](https://github.com/pcawte/AgDev) is the Agon's PC-hosted C
toolchain: the TI-84 Plus CE toolchain adapted for MOS, with an LLVM-based
compiler (clang 15) and a makefile-driven build. Its programs and agonc's
are both MOS executables loaded at `0x040000`, with 24-bit `int` and
pointers, so most C programs move across with a modest list of changes.
This chapter is that list, checked against AgDev 3.1.0's headers and
library: the language, the types, the C library, the Agon headers, inline
assembly, the build, the command line, the exit status and memory. The last
section goes the other way: back to HI-TECH C and CP/M.

In short:

1. Change `#include <mos_api.h>` to `<agon/mos.h>` and `<agon/vdp_vdu.h>`
   to `<agon/vdp.h>`, and include `<stdint.h>` wherever `uint8_t` and its
   kind are used.
2. Rewrite what C89 lacks: declarations in `for`, designated initialisers,
   `bool`, `inline`, compound literals, variable-length arrays.
3. Replace the few library functions agonc does not have, such as
   `snprintf` and `strdup`.
4. Expect `double` to be 64 bits, and slower.
5. Return `EXIT_FAILURE`, not 1, for a failure.
6. Build with one `agonc` command instead of a makefile.

## The language

AgDev's compiler takes C99 to C17, and C++. agonc takes C89 and, in
its default mode, a few C99 features: `//` comments, `long long`,
declarations after statements (with a warning), a comma after the last
enumerator, and non-constant initialisers for local arrays and structures
([The language](03-the-language.md#the-two-modes)). C++ has no counterpart.

The C99 features agonc lacks fail with plain syntax errors, such as
`expected an expression` for `for (int i = 0; ...)`. The
[Messages](04-messages.md#code-written-for-c99-or-gcc) chapter has a table
of these constructs, the message each produces and the C89 to write
instead. Two more differences catch ported code:

- Calling a function with no declaration in scope is an error in the
  default mode (`call to undeclared function f`). Include the header, or
  declare the function before its first use.
- GCC's `__attribute__((...))`, which AgDev's headers use, is accepted and
  ignored; GCC's extended `asm` with operands is not
  ([below](#inline-assembly)).

Identifiers are significant to 47 characters and a longer one is an error,
which every name in AgDev's Agon headers satisfies.

## Types

| Type | AgDev | agonc |
|---|---|---|
| `char` | 8 bits, signed | 8 bits, signed |
| `short`, `int`, `long` | 16, 24, 32 bits | 16, 24, 32 bits |
| `long long` | 64 bits | 64 bits (default mode only) |
| pointers, `size_t` | 24 bits | 24 bits |
| `float` | 32 bits | 32 bits |
| `double` | **32 bits**, the same as `float` | **64 bits** |
| `long double` | 64 bits | 64 bits |

The one difference that changes results is `double`. In AgDev it is
`float` under another name (its library defines `acos` as `acosf`, for
instance); in agonc it is IEEE 754 binary64, with about 16 significant
digits instead of 7, and every operation done in software on 8-byte
values. A program that used `double` for speed gets more precision and
less speed: change it to `float` where 7 digits are enough. agonc's
`<math.h>` has only C89's `double` functions, so `sqrtf(x)` becomes
`(float)sqrt(x)`, computed in `double`. A `float` passed to `printf` is
promoted to `double`, as in any C. A file of `double` values written by an
AgDev program holds 4-byte values: read them into `float`. What floating
point and `long long` cost is in [The
language](03-the-language.md#floating-point-and-long-long-cost-time).

Structures have no padding in agonc, and bit-fields are allocated in
bytes from the least significant bit ([the ABI](../abi.md), section 2). Do
not rely on AgDev's layout matching it in data written to files.

## The C library

agonc has the whole C89 library ([The C library](05-c-library.md)), and
AgDev's programs mostly use that part. The differences:

| AgDev | agonc |
|---|---|
| `fputs(s, f)` writes a newline after `s` | writes `s` only, as C requires: add the `'\n'` |
| `snprintf`, `vsnprintf` | not provided: `sprintf` into a buffer large enough |
| `strdup`, `strndup`, `strnlen` | not provided: `malloc` and `strcpy`, or a loop |
| `strcasecmp`, `strncasecmp` | not provided: compare `tolower` of each character |
| `strtof` | `(float)strtod(s, &end)` |
| `isascii(c)` | `((c) & ~0x7F) == 0` |
| `gets_s(s, n)` | `fgets(s, n, stdin)`, which keeps the newline |
| `quick_exit`, `at_quick_exit`, `on_exit` | `exit` and `atexit` |
| `<stdbool.h>`, `<inttypes.h>`, `<iso646.h>`, `<wchar.h>`, `<alloca.h>` | not provided |
| `EXIT_FAILURE` is 1 | `EXIT_FAILURE` is 200 ([exit status](#exit-status)) |
| `RAND_MAX` is 8388607 | `RAND_MAX` is 32767, and the sequence differs |

The `fputs` difference is the one to look for: output that relied on
AgDev's extra newline runs together in agonc. `puts` adds a newline in both.
The headers AgDev keeps from the calculator toolchain (`graphx.h`,
`keypadc.h`, `fileioc.h` and the like) have no counterpart.

agonc's library also has what AgDev's lacks, among them `setvbuf`,
`tmpfile`, `rename`, `perror`, `rewind`, `clearerr`, `fgetpos`,
`<locale.h>` and `<signal.h>`, through which Ctrl-C stops a program
([below](#exit-status)).

## The Agon headers

| AgDev | agonc |
|---|---|
| `<mos_api.h>` | `<agon/mos.h>`, which does not include `<stdint.h>` |
| `<agon/vdp_vdu.h>` | `<agon/vdp.h>` |
| `<agon/vdp_key.h>` | `mos_set_key_handler` and `mos_getkbmap` in `<agon/mos.h>` |
| UART calls in `<mos_api.h>` | the same calls, or `<agon/uart.h>` |

Where AgDev has a function, agonc's has its name and argument order
([The Agon library](06-agon-library.md#names)), with `int` where AgDev has
`uint8_t` or `bool`, so most calls compile unchanged. These need changing:

| AgDev | agonc |
|---|---|
| `mos_oscli(cmd, argv, argc)` | `mos_oscli(cmd)`: one argument |
| `vdp_font_select(id)` | `vdp_font_select(id, flags)`, flags 0 for the plain case (AgDev's sends no flags byte, though the command has one) |
| `vdp_mode(n)` returns an `int` | returns nothing |
| `vdp_vdu_init()` | not needed; the system variables are `MOS_SYSVAR` |
| `SYSVAR` structure, fields `cursorX`, `scrWidth`, ... | `struct mos_sysvar` through `MOS_SYSVAR`, fields `cursor_x`, `scr_width`, ...; the `getsysvar_` functions are the same |
| `getsysvar_rtc()` returns an `RTC_DATA *` | returns bytes; read the clock with `time` and `localtime` |
| `UART` structure for `mos_uopen` | `struct mos_uart`, or `uart_open(baud, bits, stop, parity, flow)` |
| `mos_ugetc()` above 255 when no byte came | -1 |
| `mos_editline(buf, len, clear)` | `clear` is bit 0 of a flags argument, so 1 still clears; returns the key that ended the edit |
| `putch(c)`, `getch()` | `vdu(c)`, `mos_getkey()` |
| `mos_puts(buf, n, 0)`, `VDP_PUTS(s)` | `vdu_n(buf, n)`, `vdu_n((const char *)&s, sizeof s)` |
| `waitvblank()` | wait for `getsysvar_time()` to change |
| `mos_port_read`, `mos_port_write` | not provided: a few lines of assembly (below) |
| `vdp_load_sprite_bitmaps`, `vdp_adv_load_sprite_bitmaps` | `vdp_load_bitmap_file` for each frame, then `vdp_create_sprite` |
| `FA_OPEN_EXISTING`, `BYTE`, `FRESULT` | 0, `unsigned char`, `int` |
| `sysvar_` offsets, `vdp_pflag_` flags | `MOS_SYSVAR`'s fields, `VDP_PFLAG_` in `<agon/vdp.h>` |
| `I2C_SPEED_`, `RET_`, `VDP_AUDIO_` constants | the numbers, which the headers' comments give |

Three functions behave differently on purpose. AgDev's `vdp_triangle`,
`vdp_circle_radius` and `vdp_circle` send PLOT 80, 144 and 148, which move
the graphics cursor without drawing; agonc's send 85, 145 and 149, which
draw a filled triangle and the two kinds of circle. A program that worked
around AgDev's with `vdp_plot` keeps working. `vdp_cursor_tab` sends its
first argument as the column in both, whatever AgDev's parameter names
suggest.

**Keyboard events.** AgDev's `vdp_set_key_event_handler` passes a
`KEY_EVENT` whose fields are the ASCII code, the modifiers, the virtual key
code and the up or down state. agonc's `mos_set_key_handler` passes the
same four bytes, in that order, as `packet[0]` to `packet[3]`. For keys held
down, `mos_getkbmap` returns MOS's map, which is numbered by BBC BASIC's
key numbers, not by the virtual key codes that index AgDev's `vdp_key_bits`;
`MOS_SYSVAR->vkeycode` and `vkeydown` give the last key by virtual code.

**Keeping the include lines.** Small header files of your own, in a folder
given with `-I`, let a large program keep AgDev's include lines while you
port it:

```c
/* compat/mos_api.h */
#include <stdint.h>
#include <agon/mos.h>
#define FA_OPEN_EXISTING 0x00
typedef unsigned char BYTE;
```

```c
/* compat/agon/vdp_vdu.h */
#include <agon/vdp.h>
#define VDP_PUTS(S) vdu_n((const char *)&(S), sizeof(S))
```

with `agonc -Icompat ...`. The compiler then reports the calls that still
differ, such as `wrong number of arguments to mos_oscli (3 given, 1
expected)`.

## Inline assembly

agonc has two forms, both only inside a function: `asm("...")`, whose
string may hold several lines separated by `\n`, and a `#asm` ... `#endasm`
block. The text is copied as written into the function and assembled by
ez80asm, the Agon's assembler, not by the fasmg-based assembler AgDev
uses, so check directives and number syntax against ez80asm. GCC's extended
form, with operands and clobbers after colons, is not accepted. The rules,
from [the ABI](../abi.md) (sections 4, 5 and 10):

- The code may change any register except `IX` and `SP`, and must leave
  the stack balanced. Nothing in a register survives it, and nothing is in
  a register when it starts: unlike AgDev's code generator, agonc keeps no
  variable in a register.
- Arguments are in 3-byte slots from `(ix+6)`, the first argument
  lowest; a `long` or `float` takes two slots, a `double` or `long long`
  three. A `char` argument is sign-extended through its whole slot, where
  AgDev leaves the upper bytes undefined.
- Locals are in the frame below `IX`: the scalars first, in declaration
  order from `(ix-3)` downwards, each in whole 3-byte slots (two for a
  `long` or `float`, three for a `double` or `long long`), then arrays and
  structures. A result is passed back by storing it in a local.
- A function returns a `char` in `HL`, sign-extended, where AgDev returns
  it in `A`; a `long` or `float` in `E:UHL`.
- Labels are ez80asm `@local` labels; a global label must be unique in the
  whole program, and agonc warns at each.

AgDev's `mos_port_read`, written for agonc:

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
AgDev's `.asm` and `.src` files, with fasmg's `section`, `public` and
`extern`, become agonc `.s` files with marker lines for the linker: a
`;;sect` line for each function or object and a `;;ref` line for each
symbol it uses ([Assembly](02-using-agonc.md#assembly)). C names carry a
leading `_` in both.

## The build

AgDev builds on a PC with `make`, from a makefile that names the program
and its options, and leaves a `.bin` file in `bin/` to copy to the SD
card. agonc needs no makefile. On the Agon:

```text
agonc -o game.bin main.c sprites.c sound.c
```

For many files, list them in a response file and give `agonc -o game.bin
@game.rsp`, since MOS limits the command line to about 240 characters. The
same command works on a PC with agonc's PC build, which produces the same
program byte for byte ([Building on a PC](02-using-agonc.md#building-on-a-pc)).

AgDev's makefile settings mostly have no counterpart:

| Makefile setting | In agonc |
|---|---|
| `NAME` | `-o name.bin` |
| `CFLAGS = -Wall -Wextra -Oz` | leave out: `-Wall` is accepted, but `-Wextra` and `-Oz` are unknown options; `-O` is the default |
| `INIT_LOC`, `BSSHEAP_LOW`, `BSSHEAP_HIGH`, `STACK_HIGH` | none: the layout is fixed ([memory](#memory)) |
| `HAS_ARG_PROCESSING` | none: quotes always work, redirection never ([below](#command-line-arguments)) |
| `LDHAS_EXIT_HANDLER` | none: the status always reaches MOS |
| `ALLOCATOR` | none: `free` always returns memory to the heap |
| `LTO` | none: the linker keeps only the functions a program reaches |

Two source files may not have the same name, even in different folders,
and a program has at most 27 source files and libraries together
([Messages](04-messages.md#when-a-table-is-full)).

## Command-line arguments

| Item | AgDev (by default) | agonc |
|---|---|---|
| `argv[0]` | the program's name from the makefile, with `.bin` | an empty string |
| separators | spaces | spaces and tabs |
| `"two words"` | two arguments, quotes kept | one argument, quotes removed |
| most entries, `argv[0]` included | 15 | 32 |

Further words are dropped without a message in both. AgDev's optional
`HAS_ARG_PROCESSING` adds quoting and `<`, `>` and `>>` redirection; agonc
has no redirection, so a program that read redirected input should take a
file name and open it (`freopen(name, "r", stdin)` keeps the rest of the
code as it was). A program that prints its own name from `argv[0]` needs it
written in.

## Exit status

AgDev's default start-up code ends every program by printing a short word
(Okay, Quit or Abort, for statuses 0, 1 and anything else) and returning 0
to MOS, so MOS never sees a failure. agonc passes the status to MOS
unchanged. MOS prints its own message for statuses 1 to 25 (26 on MOS 3), so a program
that returns 1 for a failure now makes MOS print "Error accessing SD card".
Use `EXIT_FAILURE`, which is 200 and prints nothing; a non-zero status also
stops a MOS `exec` script, as a failure should
([Messages](04-messages.md#exit-statuses)).

A program built with agonc also stops when Ctrl-C is pressed, at its next
input or output, with status 130; AgDev has no `<signal.h>`. A program that
must not stop that way can ignore the signal with `signal(SIGINT,
SIG_IGN)` or catch it ([The C library](05-c-library.md#signalh)).

## Memory

AgDev's default layout puts code and data from `0x040000`, the zeroed data
and the heap together in `0x080000` to `0x09FFFF`, and the stack below
`0x0AFFFF`, so a program's code is limited to 256 KB and its heap to what
its zeroed data leave of 128 KB. agonc has one pool from `0x040000` to
`0x0AFFFF`: the program file at the bottom, the zeroed data at the top, the
heap growing up from the program and the stack down from the zeroed data,
with nothing to configure ([memory](03-the-language.md#memory)). A large
program or heap fits where it did not before, but no address above the
program is free for the taking: memory a program used at a fixed address
in AgDev's layout must come from `malloc` or a `static` array. A
program built with agonc never touches `0x0B0000` and above, where agonc
itself and MOS live; nor does one built with AgDev's default settings.

MOS's file handles work the same for both: eight in all, not closed by MOS
when a program ends. agonc's `exit` closes every stream, but a handle from
`mos_fopen` must be closed with `mos_fclose`
([The C library](05-c-library.md#eight-handles-for-the-whole-session)).

## Back to HI-TECH C and CP/M

A program written for agonc can often be carried back to
[HI-TECH C](https://github.com/agn453/HI-TECH-Z80-C) 3.09, the CP/M
compiler for the Z80, to run under CP/M on a Z80 machine or under
[ZINC](https://github.com/nihirash/ZINC) on the Agon. HI-TECH C follows
the draft ANSI standard of its time, prototypes included, so the best
starting point is code that agonc accepts with `-ansi`: strict mode turns
off `long long`, `//` comments and agonc's other extensions. What remains
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
