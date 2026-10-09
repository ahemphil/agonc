# Agon C ABI

This is the contract between the code generator (`cc2`), the runtime
(`crt0`, `rt.s`, `libc.s`, `libm.s`), hand-written inline assembly, and the
linker (`ld`). Anything not stated here is not promised.

## 1. Machine model

- eZ80 in ADL mode: 24-bit `PC`, `SP` and register pairs; flat 16 MB address
  space; no segmentation, no alignment requirements, no padding anywhere.
- Little-endian. A 24-bit value at address `a` occupies `a`, `a+1`, `a+2`.
- User programs load at `0x040000` and may use `0x040000`–`0x0AFFFF`
  (448 KB). `0x0B0000`–`0x0B7FFF` belongs to moslets (including our driver)
  and is never touched by a compiled program. `0x0B8000` and above is MOS.
- Interrupts are enabled; MOS's timer and VDP handlers run underneath the
  program and preserve all registers.

## 2. Types

| Type | Size | Representation |
|---|---|---|
| `_Bool` | 1 | 0 or 1 (C99's; not in strict mode) |
| `char` | 1 | **signed**, two's complement (matches AgDev) |
| `signed char` | 1 | as `char`, but a distinct type |
| `unsigned char` | 1 | 0..255 |
| `short`, `unsigned short` | 2 | two's complement, −32768..32767 / 0..65535 |
| `int`, `unsigned int` | 3 | two's complement, −8388608..8388607 / 0..16777215 |
| `long`, `unsigned long` | 4 | two's complement, −2³¹..2³¹−1 / 0..2³²−1 |
| `long long`, `unsigned long long` | 8 | two's complement, −2⁶³..2⁶³−1 / 0..2⁶⁴−1 (C99's; not in strict mode) |
| `float` | 4 | IEEE 754 binary32 |
| `double`, `long double` | 8 | IEEE 754 binary64 (`long double` is `double`, as C89 allows) |
| all object pointers, `void *` | 3 | 24-bit address; null is all-zero bits |
| function pointers | 3 | the function's 24-bit entry address |
| `enum` | 3 | as `int` |
| `size_t` | 3 | `unsigned int` |
| `ptrdiff_t` | 3 | `int` |
| `wchar_t` | 3 | `int`: every Unicode code point fits |
| `struct` | sum of members | members in declaration order, no padding |
| `union` | largest member | every member at offset 0 |
| arrays | element size × count | contiguous |

Integer semantics: signed overflow wraps (two's complement); `/` truncates
toward zero and `%` takes the sign of the dividend; `>>` of a signed value is
arithmetic, of an unsigned value logical; shifting by a count outside
0..(width−1) is undefined. Integer promotion: `char`, `signed char`,
`unsigned char`, `short`, `unsigned short` and bit-fields promote to
`int` (all their values fit a 24-bit `int`). The usual arithmetic
conversions follow C89 with these widths: `int` op `unsigned int` is
`unsigned int`; `long` op `unsigned int` is `long` (every `unsigned int`
fits a `long`); any `unsigned long` operand makes the operation
`unsigned long`; above them, as in C99, `unsigned long long`, then `long
long` (which holds every `unsigned long`). Conversion to a narrower
integer type keeps the low bits.
Pointer ↔ `int`/`unsigned int` conversions are bit-for-bit; a pointer
converted to `long` is zero-extended, a `long` converted to a pointer keeps
its low 24 bits.

Floating point follows IEEE 754 formats; which arithmetic guarantees hold
(rounding, subnormals, infinities, NaNs) is recorded in the language
specification ([c89_spec.md](c89_spec.md) §6). A `float` converted to an integer truncates
toward zero; out of range is undefined, as in C89.

Relational and logical results are `int` 0 or 1.

### Bit-fields

A bit-field is declared `int`, `signed int` or `unsigned int`; a plain
`int` bit-field is **signed**. The storage unit is the **byte**, and
bit-fields are allocated from the least significant bit of the current
byte upwards:

- a bit-field of 8 bits or fewer that does not fit in what remains of the
  current byte starts at the next byte, so it never crosses a byte
  boundary and can be reached with byte operations;
- a wider bit-field (9 to 24 bits) starts at the next byte boundary, unless
  nothing of the current byte is used yet, and takes the bits it needs from
  there;
- an unnamed bit-field of width 0 moves to the next byte boundary.

The struct member that follows a run of bit-fields starts at the next
byte. The maximum width is 24. (This layout was chosen for the eZ80, which
has no instruction for a field that crosses a byte.)

## 3. Registers and the canonical-value invariant

| Register | Role |
|---|---|
| `HL` | primary: every expression result lands here (a `long` or `float` in `E:UHL`) |
| `DE` | secondary: left operand of a binary operation, address for stores |
| `A` | byte loads and stores, flags; high byte of a second 32-bit operand |
| `BC` | scratch; low 24 bits of a second 32-bit operand |
| `IX` | frame pointer; **callee-saved** |
| `IY` | scratch for generated code; `crt0` saves and restores it around `main` for MOS |
| `SP` | hardware stack; **callee-saved** (balanced on return) |

**Canonical values.** A value of any integer type of 3 bytes or fewer, or
of any pointer type, held in `HL` (or `DE`) always has all 24 bits
meaningful: `char`, `signed char` and `short` are sign-extended, `unsigned
char` and `unsigned short` zero-extended, at the moment they are loaded, and
every operation produces a full 24-bit result. Consequently:

- `add hl,de` and `or a` / `sbc hl,de` are correct `int` operations with no
  fix-up afterwards.
- A conversion to a narrower type is a truncate-and-re-extend of the low
  byte(s) in place (`EXT` in the IR).
- **No code may load only part of a 24-bit register and then use the whole
  register.** `ld d,0` / `ld e,a` leaves the upper byte stale; the correct
  sequence is `ld de,0` / `ld e,a`. (A stale upper byte is easy to cause
  and hard to find.) Zero-extending `A` is `ld hl,0` / `ld l,a`; sign-extending
  `A` uses the `__sext8` helper or its inline expansion.

**32-bit values** (`long`, `unsigned long`, `float`) are held in **`E:UHL`**:
`HL` holds the low 24 bits (bytes 0–2), `E` the high byte (byte 3); `D` and
the upper byte of `DE` are unspecified. `E:UHL` is the primary register for
32-bit values, as `HL` is for 24-bit ones. The secondary, for a helper's
left operand, is **`A:UBC`**: `BC` the low 24 bits, `A` the high byte. This
is the 24-bit helpers' shape too (left operand in the secondary, right in
the primary, result in the primary), which suits `cc2`'s evaluation order: the
left operand is saved (`ld a,e` / `push af` / `push hl`), the right one
computed into `E:UHL`, and the left restored into `A:UBC` (`pop bc` /
`pop af`) with nothing to swap. Pushing `DE` then `HL` lays a 32-bit value
down as its 4-byte little-endian object at the lower address, which is how
it is passed and stored (§4, §5).

**64-bit values** (`double`, `long long`) never live in registers. Each
is always an 8-byte object in memory, and code handles it by address, as
it handles structs: `HL` holds the address of a `double` or a `long long`,
never its bits.

Comparisons of 24-bit values use `or a` / `sbc hl,de` for equality and
unsigned order; signed order needs the sign-xor-overflow test, provided by
the `__ilt`-family helpers or inline sequences that implement it exactly.

## 4. Calling convention

- Arguments are pushed **right-to-left** and evaluated right-to-left.
- Every argument occupies one or more **3-byte slots**:
  - an integer of 3 bytes or fewer, an enumeration or a pointer: one slot
    holding its canonical 24-bit value (a `char` is pushed sign-extended, an
    `unsigned char` zero-extended). Unlike AgDev, the upper bytes of a slot
    are never garbage; a callee may read the slot as a full `int`.
  - `long`, `unsigned long`, `float`: two slots holding the 4-byte object at
    the lower address (`push de` / `push hl` from `E:UHL`); the upper 2 bytes
    of the pair are unspecified.
  - `double`, `long long`, `unsigned long long`: three slots holding the
    8-byte object at the lower address; the ninth byte is unspecified.
  - `struct` or `union` by value: ⌈size/3⌉ slots holding its bytes in memory
    order at the lower address; trailing bytes are unspecified.
- The **caller removes** the arguments (`pop de` per slot, or an `SP`
  adjustment).
- **Default argument promotions** apply to arguments of a variadic function
  after the last named parameter, and to every argument of a call to a
  function with no prototype: `char`, `short` and their `signed`/`unsigned`
  forms become `int`; `float` becomes `double`. A K&R-style definition that
  declares a parameter `float` receives a `double` and converts it on entry;
  one that declares `char` or `short` receives an `int` and uses its low
  bytes.
- Variadic functions receive exactly the layout above; `va_arg(ap, T)`
  advances by T's slot count × 3 (`int` 3, `long` 6, `double` and `long
  long` 9).
- **Return value**:
  - every integer, enumeration and pointer type of 3 bytes or fewer
    **including `char`**: `HL`, canonical (this deviates from AgDev, which
    returns `char` in `A`);
  - `long`, `unsigned long`, `float`: `E:UHL`;
  - `double`, `long long`, `struct`, `union`: through a **hidden result
    pointer**. The
    caller allocates the result object and passes its address as an extra
    first argument (so it occupies the `ix+6` slot and every declared
    parameter moves up one slot); the callee stores the result there and
    also returns the pointer in `HL`;
  - `void`: `HL` unspecified.
- A call through a function pointer passes and returns exactly as a direct
  call (the code generator calls the address through the runtime's
  `__callhl`, §6).
- **Preservation**: a callee may clobber `A`, `F`, `BC`, `DE`, `HL`, `IY`
  and the alternate register set. It must preserve `IX` and return with
  `SP` exactly as on entry (after the caller's return address pop).
- `main` is called as `int main(int argc, char **argv)` with two pushed
  slots whether or not it declares them.

## 5. Stack frame

Prologue and epilogue emitted for every C function:

```
        push ix
        ld   ix,0
        add  ix,sp              ; IX = frame base
        ld   hl,-FRAME          ; only if FRAME > 0
        add  hl,sp
        ld   sp,hl
        ...
        ld   sp,ix
        pop  ix
        ret
```

Layout relative to `IX`:

| Offset | Contents |
|---|---|
| `ix+0` | saved caller `IX` (3) |
| `ix+3` | return address (3) |
| `ix+6` | first argument slot (the hidden result pointer, for a function returning `double`, `long long`, `struct` or `union`); then the declared parameters' slots in order, per §4 |
| `ix-3`, `ix-6`, … | locals, in declaration order (laid out by `cc1`, which gives each local's offset in IR 2's `LOC` records) |

Local allocation rule (part of the ABI, since inline assembly reads
locals): scalars are allocated first, nearest `IX`, in declaration order,
each in whole 3-byte slots with the value at the slot pair's lowest
address: `char`, `short`, `int`, pointers and enums one slot, `long` and
`float` two, `double` and `long long` three. Arrays, structs and unions take exactly
`sizeof` bytes and are allocated after all scalars, also in declaration
order. A block-scope `static` is not in the frame. `FRAME` is the total.
A value narrower than its slot occupies the slot's low bytes; the rest are
unspecified, so inline assembly must read it with a load of its own size.

With block scope, locals of sibling blocks may share frame space; the
rule above then holds for the locals of the function's outermost block,
which is what inline assembly can rely on.

Offsets beyond the −128..+127 displacement window are reached with
`lea hl,ix+0` / `ld de,off` / `add hl,de`; the code generator does this
transparently, so frames may exceed 128 bytes.

Expression temporaries, including the result objects of calls returning
`double`, `long long`, `struct` or `union` and the results of `double` and
`long long` operations, live in the frame below the locals.
Nothing is preserved in registers across a call, an inline assembly block,
or a helper call.

## 6. Runtime helpers

Helpers are called with `call __name`. They follow one of these register
contracts:

| Kind | Inputs | Output | Example |
|---|---|---|---|
| binary | left in `DE`, right in `HL` | `HL` | `__imul`: `HL = DE * HL` |
| shift | value in `DE`, count in `HL` | `HL` | `__ishl`, `__ishrs`, `__ishru` |
| unary | `HL` | `HL` | `__sext8`, `__sext16` |
| compare | left `DE`, right `HL` | `HL` = 0 or 1, **and `Z` set iff `HL` = 0** | `__ilts`: `HL = (DE < HL)` signed |
| 32-bit binary | left in `A:UBC`, right in `E:UHL` | `E:UHL` | `__lsub`: `E:UHL = A:UBC - E:UHL` |
| 32-bit shift | value in `A:UBC`, count in `HL` | `E:UHL` | `__lshl`: `E:UHL = A:UBC << HL` |
| 32-bit unary | `E:UHL` | `E:UHL` | `__lneg` |
| 32-bit compare | left `A:UBC`, right `E:UHL` | `HL` = 0 or 1, **`Z` set iff `HL` = 0** | `__llts`: `HL = (A:UBC < E:UHL)` signed |
| floating point, `long long` | stack arguments, as a C call | as a C function | `__fsub(b, a)`: `E:UHL = a - b` |

24-bit helpers clobber `A`, `F`, `BC`, `DE`, `IY` (and `HL` as the result);
32-bit helpers clobber `A`, `F`, `BC`, `D`, `IY` and return `E:UHL`. Both
preserve `IX` and `SP` and take no stack arguments.

The multiply and divide helpers keep their working values in registers
and on the stack, so they are reentrant: an interrupt handler written in C
may multiply or divide while the main program is inside one. The shifts
and sign extensions keep values in fixed bss cells, which
`lib/agon/kbint.s`'s `__handler_call` saves around a C handler, as it saves
the alternate registers, which the `long long` division uses. Division by
zero, which C leaves undefined, never faults: `__idivu` gives the quotient
0xFFFFFF and the dividend as the remainder, the 32-bit division some
value, and the `long long` division a quotient of 0. `lib/rt/rt_ref.c` is
the C of the multiply and divide helpers, by the same steps; the tests
check the helpers against it.

The floating-point helpers are C functions in the library (`fp.c`, in
`libm.s`), called by the C convention (§4) and clobbering what any C
function may.
Their parameters are in the reverse of the operands' order, so that the
code generator pushes each operand as soon as it has made it: `__fsub(b,
a)` is `a - b`. A `float` is passed and returned as its bits (an `unsigned
long`); a `double` as its address. A helper whose result is a `double`
takes the address of an object for it first and returns that address:
`__dsub(r, b, a)` sets `*r = *a - *b`. Every operation is correctly
rounded, bit for bit the same as `cc1`'s constant folding.

The `long long` helpers (`ll.c`, also in `libm.s`) follow the same rules,
a `long long` passed and returned as a `double` is, by address:
`__qsub(r, b, a)` sets `*r = *a - *b`. Their arithmetic is `int64.c`'s,
which `cc1` also folds constants with.

Each helper is in its own section, so a program links only what it uses.
The 24-bit set (in `rt.s`): `__imul`, `__idivs`, `__idivu`, `__irems`,
`__iremu`, `__ishl`, `__ishrs`, `__ishru`, `__ilts`, `__iltu`, `__iles`,
`__ileu` (greater-than forms are the same helpers with operands swapped),
`__sext8`, `__sext16`, `__memcpy` (`DE` = dst, `HL` = src, `BC` = count;
returns `HL` = dst), `__memset` (`DE` = dst, `HL` = fill value — only the
low byte is used — `BC` = count; returns `HL` = dst), `__iand`, `__ior`,
`__ixor`.

The others:

- Calls through pointers (in `rt.s`): `__callhl` calls the function whose
  address is in `HL` (`jp (hl)`; the eZ80 has no `call (hl)`). The
  arguments are pushed as for a direct call.
- 32-bit integer (in `rt.s`): `__ladd`, `__lsub`, `__lmul`, `__ldivs`,
  `__ldivu`, `__lrems`, `__lremu`, `__land`, `__lor`, `__lxor`, `__lneg`,
  `__lcpl`, `__lshl`, `__lshrs`, `__lshru`, `__lltu`, `__llts`, `__lleu`,
  `__lles`, `__leq`, `__lnz` (a long as a condition: `HL` = 0/1 with `Z`,
  from `E:UHL` alone), the shared `__ldivmodu` (quotient in `E:UHL`,
  remainder in `A:UBC`), and the conversions `__ltoi` (truncate), `__itol`
  / `__utol` (extend from `int` / `unsigned int`). They take their scratch
  space from the stack, not bss.
- `float` (in `fp.c`): `__fadd`, `__fsub`, `__fmul`, `__fdiv`; the
  comparisons `__feq`, `__fne`, `__flt`, `__fle`, `__fgt`, `__fge` (an
  `int` 0 or 1); `__fasg(op, b, p)` for `*p op= b` (`op` 0 to 3: `+ - *
  /`), `__fincpre(d, p)` and `__fincpost(d, p)` for `*p += d` (`d` 1 or
  -1); the conversions `__ltof`, `__ultof`, `__ftol`, `__ftoul`. A `float`
  is negated inline, by flipping its sign bit.
- `double` (in `fp.c`): `__dadd`, `__dsub`, `__dmul`, `__ddiv`, `__dneg`;
  `__deq`, `__dne`, `__dlt`, `__dle`, `__dgt`, `__dge`; `__dasg`,
  `__dincpre(d, p)`, `__dincpost(r, d, p)`; `__ltod`, `__ultod`, `__ftod`,
  `__dtof`, `__dtol`, `__dtoul`. A conversion between `double` or `float`
  and an `int` goes through `long`.
- `long long` (in `ll.c`): `__qadd`, `__qsub`, `__qmul`, `__qdivs`,
  `__qdivu`, `__qrems`, `__qremu`, `__qand`, `__qor`, `__qxor`;
  `__qshl(r, n, a)`, `__qshrs`, `__qshru` (the count an `int`);
  `__qeq`, `__qne`, `__qlts`, `__qltu`, `__qles`, `__qleu`, `__qgts`,
  `__qgtu`, `__qges`, `__qgeu`; `__qneg`, `__qcpl`; `__qasg(op, b, p)` (`op`
  as `cc2` numbers the operators, 0 to 12) and `__qasgsh(op, n, p)`,
  `__qincpre(d, p)`, `__qincpost(r, d, p)`; `__ltoq`, `__ultoq`. And in
  `fp.c`: `__qtof`, `__uqtof`, `__qtod`, `__uqtod`, `__ftoq`, `__ftouq`,
  `__dtoq`, `__dtouq`. A `long long` converted to an `int` or a `long` is
  its low bytes, loaded inline; an `int` converted to a `long long` goes
  through `long`. Division by zero, which C leaves undefined, gives a
  quotient of 0 and does not fault.

Every helper is the reference semantics for its operator, and the code
generator may inline any of them when a faster open-coded sequence is
known.

## 7. Symbol naming in assembly

| C entity | Assembly symbol |
|---|---|
| external function or object `name` | `_name` |
| `static` function or object `name` in unit with id `XXXX` | `__sXXXX_name` |
| block-scope `static` object `name` in unit `XXXX` | `__sXXXX_name_N`, with `N` making it unique in the unit |
| runtime helper | `__name` (no `_s`, no digit after `__s`) |
| entry point, exit | `__start`, `__exit` |
| layout constants (from `ld`) | `__bss_base`, `__bss_size`, `__stack_top`, `__image_end` |

Identifiers are significant to 47 characters; longer is an error (the
longest label, a block-scope static's `__sXXXX_name_N`, then stays within
ez80asm's 64). A unit id
is four hex digits derived from the unit's source file name (see
`object_format.md`). Because every user symbol begins with a single `_`
and C reserves identifiers beginning with `__` for the implementation,
these namespaces cannot collide. The `_` prefix is mandatory: ez80asm
rejects labels that spell an instruction, condition code or directive.

## 8. Program image and memory layout

`ld` produces one file, assembled with `org 0x040000`:

```
0x040000  jp $+0x45                     ; entry jump, no label, no fixup
0x040004  (padding to 0x40)
0x040040  db "MOS",0,1                  ; MOS header, ADL executable
0x040045  __start:  crt0 code
          equates: __bss_size, __bss_base, __stack_top
          runtime helpers (rt.s) and the library's (fp.c, ll.c), in dependency order
          library and user code sections, callees before callers
          initialised data sections
__image_end:
```

Memory map at run time (addresses grow upward):

| Region | Bounds | Notes |
|---|---|---|
| code + data | `0x040000` .. `__image_end` | the loaded file |
| heap | grows up from `__image_end` | `malloc` refuses to grow past `SP` minus a safety margin (256 bytes) and returns null instead |
| free | between heap top and `SP` | shared by heap and stack with no fixed split |
| stack | grows down from `__stack_top` | `__stack_top equ __bss_base` |
| bss | `__bss_base` .. `0x0B0000` | `__bss_base equ 0x0B0000 - __bss_size`; zeroed by `crt0`; **no bytes in the file**: every bss object is an `equ` placed before any code, so all references to it are backward |

`__bss_size` is the sum of all bss objects. There is no reserved stack
size and no link option for one: the stack and the heap share everything
between the image and bss. A heap that reaches the stack is caught by
`malloc` and reported as an allocation failure; a stack that reaches the
heap is not detected (an optional prologue probe is a possible debug
flag). A program whose image reaches `__bss_base` fails to link.

## 9. Startup and termination

`crt0` (`__start`), entered by MOS or by the driver with `A` = `MB`,
`DE` = execution address, `HL` = pointer to the NUL-terminated argument
string, `SP` in MOS's 2 KB system stack:

1. `push iy`; save `SP` in the data cell `__exit_sp`.
2. `ld sp,__stack_top`.
3. Zero `__bss_size` bytes at `__bss_base` (skipped when zero).
4. Split the argument string **in place** into `__argv[]` (up to 32
   entries; extra tokens are dropped), with `argv[0]` a fixed empty string;
   `argc` counts it, and `argv[argc]` is a null pointer (the table has a
   33rd slot for it). Tokens are separated by runs of spaces and tabs.
   A token that starts with a double quote runs to the next one, spaces
   included, and both quotes are removed (`""` is an empty argument; a
   token with no closing quote runs to the end of the line); a quote inside
   a token is kept (MOS 3 quotes file names containing spaces).
5. Install the Ctrl-C handler (`__kb_on`: `mos_setkbvector`,
   0x1D; c89_spec.md §15), then `push argv`, `push argc`, `call _main`,
   `pop`, `pop`.
6. Returning from `main` is `exit(status)`, as C89 requires:
   `crt0` calls `_exit` with `main`'s result. `exit` runs the `atexit`
   functions in reverse order, then flushes and closes every open stream,
   then jumps to `__exit`. The stream step is reached through a weak
   reference (`object_format.md`), so a program that does not use stdio
   does not link it.
7. `__exit:` with `HL` = status: remove the Ctrl-C handler (MOS must not
   call into a program that has gone), `ld sp,(__exit_sp)`, `pop iy`,
   `ret`.

The status returned to MOS is `main`'s return value or `exit`'s argument.
MOS prints its own message for statuses 1–25 (1–26 on MOS 3; for 1, `Error accessing SD
card`), so `EXIT_FAILURE` is **200**, the same status the driver returns on
any build failure; `EXIT_SUCCESS` is 0.

## 10. Inline assembly contract

An `asm("...")` statement (or `__asm("...")`, which strict mode keeps) or a
`#asm` … `#endasm` block is copied verbatim into the function's
code at that point (it is allowed only inside a function; whole functions
in assembly go in a `.s` file, driver.md §2). It:

- may use every register except `IX` and `SP`, which it must leave as found;
- may assume nothing is live in any other register on entry, and nothing it
  leaves in a register survives;
- reaches C objects as `_name`, parameters as `(ix+6)`, `(ix+9)`, … (per
  §4's slot counts), and locals by the §5 allocation rule;
- must keep the stack balanced;
- must define labels only as ez80asm `@local` labels (the function's own
  global label scopes them) or as names guaranteed unique program-wide
  (`cc1` warns at every label of the second kind);
- calls MOS with `ld a,fn` / `rst.lis 08h` and its documented register
  usage; MOS preserves the registers it documents.

## 11. What is deliberately not promised

Register-passed arguments, callee-cleanup, and comparison results left in
the flags are possible future optimisations. They would change §4 and §6
and therefore require rebuilding every `.s` file, which is acceptable
because libraries ship as source and `.s`, never as binaries. Inline
assembly that follows §10 survives any such change.
