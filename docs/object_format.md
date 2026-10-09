# Object format: `.s` files and the pseudo-linker

There is no binary object format on this platform. The unit of separate
compilation is a **`.s` file**: valid ez80asm text produced by `cc2` (or
written by hand for the runtime) that carries marker comments describing
its sections and references. `ld` reads several `.s` files, decides which
sections are needed, orders them, adds the image layout, and writes one
`.asm` file for ez80asm. A library is nothing more than `.s` files
concatenated.

## 1. Markers

Every marker is an assembler comment beginning with `;;` in column 1, so a
`.s` file is also valid input to ez80asm on its own (it just is not a
program). `ld` reads only marker lines and section bodies; it never parses
instructions.

```
;;agonc-object 1                      first line: format version
;;unit <name> [<id>]                  unit name and 4-hex-digit id (ld computes an omitted id)
;;sect code <sym> <g|s>               a function: body follows
;;sect data <sym> <g|s>               initialised object: body follows
;;sect bss  <sym> <g|s> <size>        zero-initialised object: no body
;;ref <sym>                           a symbol this section references (repeatable, before the body)
;;wref <sym>                          this unit's references to <sym> are weak (§8)
;;label <name>                        a bare (global) label the body defines besides <sym>
;;end                                 end of the file's sections
```

A section runs from its `;;sect` line to the next `;;sect` or `;;end`. The
body of a `code` section starts with the section's own label line, contains
the code, and ends with the function's string pool (unlabelled `db` lines
addressed as `sym+offset`). The body of a `data` section is the label and
its `db`/`dw`/`dl` lines. A `bss` section has no body at all.

## 2. Symbols

- `<sym>` in a marker is the **assembly** name: `_name` for external
  linkage, `__s<id>_name` for internal linkage (ABI §7). `cc2` mangles;
  `ld` compares strings.
- The unit id is the low 16 bits of an FNV-1a hash of the unit name as
  given to the driver, printed as 4 lowercase hex digits. Units are not
  required to have different names: a program's `fp.c` may be linked with
  the library's. Two units with the same id (the same name, or two names
  that hash alike) matter only if both have a static of the same name,
  which is then a duplicate definition.
- Every reference a section makes to a symbol outside itself is listed with
  `;;ref`, including helpers (`__imul`), library functions and data objects.
  `cc2` derives the list from the IR. For inline assembly text `cc2` adds a
  `;;ref` for every token that looks like a symbol (`_` or `__` followed by
  identifier characters); a false positive costs nothing because an
  unknown symbol that is never assembled is ignored, but a false negative
  would drop a needed section, so the scan is deliberately generous.
- `;;label` declares an additional bare label that the section body
  defines. `ld` treats it as a definition belonging to that section (so a
  `;;ref` to it from elsewhere selects the section, as `crt0`'s `__exit`
  inside the `__start` section relies on) and uses it to detect duplicate
  bare labels across the whole program, naming both definitions before
  ez80asm produces a less helpful error.

## 3. Rules for section bodies

These are what keep ez80asm's memory flat (measured: 22 KB of labels and
2 KB of fixups for a 195 KB program, versus a machine reset for the naive
style at half that size):

1. **Exactly one global label per section**, the section symbol. Local
   control flow uses `$`-relative `jr`/`jp`. Hand-written runtime code may
   use `@local` labels sparingly.
2. **No forward references inside a section**: strings and tables follow
   the code and are addressed as `sym+offset`.
3. **No `equ`, `org`, `align`, `include` or macro** inside a section.
   Layout directives belong to `ld`'s prologue only.
4. Lines shorter than 256 bytes; identifiers at most 64 characters.
5. A section body never mentions absolute addresses; bss objects are
   reached by name, layout constants by their `__` names.

## 4. What `ld` does

Inputs: an ordered list of `.s` files (user units, then `-l` libraries, with
`crt0.s`, `rt.s` and `libc.s` added implicitly unless `-nostdlib`, and
`libm.s` for a program with floating point or `long long`: driver.md §4), and
options (`--entry=sym`, `-o file`).

**Pass 1, index.** Read every file once, line by line. For each section
record its file, byte offset, length, kind, symbol, visibility, size (bss)
and reference list. Record every `;;label`. Build:

- the definition table: symbol → section, with "duplicate definition"
  errors for two `g` definitions of the same name, or two sections of any
  kind with the same assembly name;
- the reference graph: section → referenced symbols.

**Pass 2, select.** Starting from the roots (`__start`, plus `--entry`
symbols), walk the
graph and mark reachable sections. Unresolved references in reachable
sections are "undefined symbol" errors, reported with the referencing unit
and section. Unreachable sections are dropped: this is what lets a library
be one big file.

**Pass 3, order.** Topologically sort the selected `code` sections so that
every callee precedes its callers (depth-first post-order over the
reference graph, restricted to code sections). Cycles (mutual recursion)
are broken at the back edge; those are the only forward `call`s left in
the output, and `ld` reports their count under `-v`. Runtime helper
sections (`rt.s`) sort first automatically because they reference nothing.

**Pass 4, layout and copy.** Write the output in this order:

```
        assume adl=1
        org 0x040000
        jp $+0x45
        align 64
        db "MOS",0,1
__bss_size:  equ <sum of bss sizes>
__bss_base:  equ 0x0B0000 - __bss_size
__stack_top: equ __bss_base
<one "sym: equ __bss_base+off" line per selected bss section>
;; crt0 (the __start section body)
;; selected code sections in pass-3 order, bodies copied verbatim
;; selected data sections
__image_end:
```

**The equates come before `__start`'s own body, not after.** `equ` emits
no bytes, so this costs nothing — `__start` still lands at exactly file
offset `0x45` and the entry jump's `$+0x45` still reaches it. It is not
required for correctness: an ordinary instruction may forward-reference an
`equ` (verified empirically — `ld hl,FOO` before `FOO: equ 0x123456`
assembles and resolves correctly via the same fixup mechanism labels use).
The one real restriction, also verified, is narrower: an `equ`'s **own
right-hand-side expression** may not reference a symbol not yet defined
(`FOO: equ BAR+1` before `BAR` is defined fails with "Unknown
identifier"), which is why `__bss_size`, `__bss_base`, `__stack_top` and
the per-object bss equates must stay in that dependency order among
themselves. Placing the whole equate block before `__start` is done only
because it is free and removes three otherwise-harmless fixups (`crt0`'s
own references to `__stack_top` and `__bss_base` in ABI §9 steps 2–3).

Bodies are copied by seeking to the recorded offset and copying the
recorded length, so `ld` never holds a section in memory. `__start` is
placed as the very first code so the entry jump needs no fixup; its own
`call _main` is consequently a forward reference to wherever `_main`'s
dependency-sorted position lands, exactly as `__image_end` is a forward
reference from the heap initialiser — the two expected exceptions to
"everything backward", each costing one fixup, never scaling with program
size.

**Diagnostics.** `undefined symbol X (referenced from unit U, section S)`,
`duplicate definition of X (units U1 and U2)`, `duplicate label X (units U1
function F1 and U2 function F2)`, `image too large:
code+data reaches __bss_base` (checked after assembly from the binary size,
by the driver). Exit status 200 on any error.

**Memory.** `ld` holds only the index: per section roughly the symbol name,
five integers and the reference list. At 40 bytes per symbol and a few
thousand symbols this is well under 200 KB; a `-v` report prints the peak.

## 5. Libraries

`agonc -c foo.c bar.c` produces `foo.s` and `bar.s`. A library is made with
plain concatenation (`copy foo.s+bar.s libx.s` on MOS, `cat` on the host);
`ld` treats each `;;unit` block independently, and dead-section removal
means a program pays only for what it calls. Libraries live in the
directories listed in `driver.md` and are found with `-lx` as `libx.s`.
`agonc --index libx.s` writes `libx.idx` beside it, which makes every
link that uses the library faster (section 9).

## 6. Example

```
;;agonc-object 1
;;unit count.c 3f2a
;;sect bss __s3f2a_total s 3
;;sect code _add g
;;ref __s3f2a_total
;;ref __ilts
_add:
        push ix
        ld ix,0
        add ix,sp
        ld hl,-3
        add hl,sp
        ld sp,hl
        ...
        ld sp,ix
        pop ix
        ret
;;end
```

## 7. The moslet layout

`ld --moslet` lays a program out
for the moslet area instead of user RAM: `org 0x0B0000` and `__bss_base:
equ 0x0B8000 - __bss_size`, everything else as in §4. `crt0.s` works
unchanged: its stack is the moslet area's own, below the bss, so a program
the moslet loads into user RAM may use all of it. This is how the driver
(driver.md §1) is linked. The first lines of `ld`'s output (`org`,
`__bss_size`, `__bss_base`) are also what the driver reads for its image
check (§4, Diagnostics).

## 8. Weak references, return kinds and data

**Weak references.** `;;wref <sym>` makes every reference to `<sym>` from
the unit it appears in weak: the unit uses the symbol only if something
else brings it into the program. It may stand anywhere after the unit's
`;;unit` line, between sections or inside one, and covers all of the
unit's sections whichever it is in (a `#pragma weak` covers the whole
translation unit, and `cc2` prints a function's sections only at its end).
In pass 2 a weak reference does not select the section that defines the
symbol. After selection, every weakly referenced symbol that no selected
section defines gets `<sym>: equ 0` in the prologue, beside the bss
equates, so the code that refers to it assembles and can test it against
zero. A weak reference to a symbol defined nowhere is therefore not an
error. `cc2` writes `;;wref` for a symbol declared weak in C
(`#pragma weak name`, as gcc and others accept it), at the point the IR's
`WK` record comes. The library uses this twice:

- `printf` and `scanf` reach their floating-point and `long long`
  conversions through weak references, and every unit that uses `float`,
  `double` or `long long` makes a strong reference to them. A program
  without floating point or `long long` links none of that code.
- `exit` reaches the flush-and-close-all-streams routine through a weak
  reference, which `stdio`'s `fopen` and standard streams reference
  strongly, so a program without stdio does not link it (ABI §9).

**Return kinds and implicit calls.** `;;ret <kind>` in a code section
(the line after `;;sect`) says what the function returns: `i` (an
`int`-sized value or pointer, in `HL`), `l` (`E:UHL`), `r` (through the
hidden result pointer), or `v` (nothing).
`;;implicit <sym>` in a code section says the section calls `<sym>`
through an implicit declaration, and so expects `i` or `v`. After
selection, `ld` reports `implicit call to X, which returns long (unit U,
section S)` (or `float`, `double`, a structure) when the definition's kind
is `l` or `r`, and fails the link (c89_spec.md §13). A section
without `;;ret`, such as hand-written assembly, is not checked. `cc2` writes
both markers from the IR (`F`'s flags and the call's kind).

**Floating-point and 64-bit data.** `double`, `float` and `long long`
initialisers are emitted as their bytes with `db`/`dw`/`dl`, like any
other data; nothing in the format changes.

## 9. Library indexes

On the Agon, most of a link's time went to pass 1 reading the whole of
`libc.s` (and `libm.s`), and to pass 4 writing the output line by line.
An index removes the first; clean sections remove the second.

**The index.** `ld --index lib.s...` (or `agonc --index lib.s...`) runs pass
1 over each file on its own and writes `lib.idx` beside it (`.s` replaced
by `.idx`): the file's markers alone, without the bodies, each section
followed by an `;;at` line.

```
;;agonc-index 1 <size of lib.s in bytes>
;;unit ctype.c
;;sect code _isdigit g
;;at 37 60 326 1
;;ret i
...
```

`;;at <mark> <start> <end> <clean>` gives the offsets in `lib.s` of the
section's `;;sect` line, of the first body line pass 4 copies, and of the
section's end, and whether the section is clean (below). Bare labels
appear as `;;label` lines; `;;ref`, `;;ret`, `;;implicit` and `;;wref`
as in the `.s`. `;;at` anywhere but an index is an error.

**Using it.** For every input `x.s`, `ld` looks for `x.idx`. If its first
line records the version 1 and `x.s`'s present size, pass 1 reads the
index instead of `x.s`; otherwise (`-v` says so) it reads `x.s` as usual,
so a stale index costs time, never a wrong link. Pass 4 checks each
section's `;;sect` line at `<mark>` before copying, so an index made from a
different file of the same size is an error ("`x.idx` does not match
`x.s`"), reported once. The output is byte-identical with and without
indexes. The bootstrap and `make cross` index `crt0.s`, `rt.s`, `libc.s`
and `libm.s` in `/lib/agonc`; `-v` reports how many inputs were read through an
index.

**Clean sections.** Pass 4 drops full-line comments and blank lines. A
section whose body has none of them after its first instruction (and no
line ending in CR or lacking a newline) is clean: its start moves past any
markers and comments before that instruction, and pass 4 copies it in
blocks as it is, not line by line. Every section a compiled unit has is
clean; hand-written assembly with comments (`rt.s`, `crt0.s`) is copied
line by line as before, its comments dropped. Cleanness is decided from
the file each time it is read or indexed.

**Libraries read only if needed.** `ld --if-needed lib.s` reads `lib.s`
(or its index) only if, once the other inputs are read, the sections
reachable from the roots (`__start` and `--entry`) still make a strong
reference to an undefined symbol; several such libraries are tried in
order. Reading even an index costs time on the Agon (about 0.8 s for
`libc.idx`), and a program that uses nothing in a library should not
pay it. The driver passes `/lib/agonc/libagon.s` this way. `-v` reports how
many were read.

Measured on the emulator at the Agon's speed: `ld` for a
hello-world program 5.6 s to 2.0 s, for a floating-point program 16.7 s
to 5.1 s; whole compiles 30 to 45% faster.
