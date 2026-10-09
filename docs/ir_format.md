# IR format: the `cc1` → `cc2` contract

Every `.ir` file begins with `IR 2`, the format's version; a reader
rejects any other version. Everything streams:
`cc1` writes initialiser items as it parses them, and a function as it
goes, with its frame layout and switch tables after its body (§4), so
neither pass holds a whole unit.

## 1. Purpose and shape

`cc1` owns C: lexing, parsing, types, scopes, constant folding, all
diagnostics, and every conversion between decimal and binary (so floating
constants reach `cc2` as exact bit patterns) and the layout of locals in
the frame. `cc2` owns the eZ80: instruction selection, its own
temporaries, branch offsets, helper calls. The IR
is what passes between them, one file per translation unit, and it
deliberately contains **no C types, only value kinds and sizes**, and **no
control-flow structure, only jumps and labels**. A statement is one
expression tree in postfix form; `cc2` rebuilds the tree, then walks it.

The file is plain text so it can be read, diffed, hand-written for tests
and produced by the compiler's own C. Lines are terminated by `\n`; `\r`
before it is ignored. Tokens are separated by single spaces. A line
beginning with `#` is a comment. Nothing on a line follows the last token
of a record except a comment.

## 2. Value kinds

Every value on the tree-building stack has one of five kinds, and every
operator says which kind it works on:

| Kind | Holds | Lives in | Suffix |
|---|---|---|---|
| **I** | integers of 1–3 bytes, enumerations, pointers | `HL`, canonical 24-bit (ABI §3) | none |
| **L** | `long`, `unsigned long` | `E:UHL` | `.l` |
| **F** | `float` | `E:UHL`, IEEE binary32 bits | `.f` |
| **D** | `double`, `long double` | memory: the value on the stack is the **address** of an 8-byte object | `.d` |
| **Q** | `long long`, `unsigned long long` | memory, as D | `.q` |

A D or Q value is always an address: of a variable, of a function's result
object, or of a temporary `cc2` allocates in the frame for an
intermediate result. A struct or union value is likewise an address; there
is no struct kind.

Sizes are 1, 2, 3, 4 or 8 bytes; signedness is `s` or `u`.

## 3. Unit-level records

```
IR 2
U <name> <id>              unit name (source file) and 4-hex-digit unit id
G <sym> <size> <vis>       zero-initialised object (bss): symbol, bytes, g|s
D <sym> <vis>              initialised object; data items follow until E
F <sym> <vis> <nparams> <frame> <flags>   function; body follows until E
E                          end of the current D or F
WK <sym>                   a weak reference to <sym> from this unit
R <sym>                    a reference to <sym> from this function that no expression shows
```

`<sym>` is the C identifier without any prefix, written with a leading
`.` wherever it appears when it has internal linkage (`G .count 3 s`,
`A .count`, `CALL .helper 1 i`). `cc2` applies the ABI naming (`_name`,
or `__s<id>_name` for a `.` name) from that alone, with no table and no
reading ahead. `<vis>` is `g` (external linkage) or `s` (internal).
`<frame>` is the function's local frame size in bytes, computed by `cc1`
per the ABI allocation rule, or `@` when the function is streamed and its
layout follows its body (§4); `<nparams>` is the number of declared
parameter slots (informational).
`<flags>` is `-`, or letters: `r` if the function returns
through a hidden result pointer (ABI §4: `double`, `long long`, struct, union), `l` if
it returns in `E:UHL` (`long`, `float`), `n` if it returns nothing
(`void`), `v` if it is variadic. Without `r`, `l` or `n` it returns an
`int`-sized value or pointer in `HL`. `cc2` writes the return kind as the
section's `;;ret` marker (object_format.md §8).

Externals need no declaration: a reference to a symbol not defined in this
unit is simply a reference; `ld` resolves it or reports it undefined.
`WK` marks every reference to `<sym>` from this unit as weak
(object_format.md §8; C's `#pragma weak`); it may come anywhere, inside a
function body too, and covers the whole unit. `R` adds a strong reference that
no expression shows, inside an `F`: `cc1` writes `R __fp_print` and `R
__fp_scan` in any function that has a `float` or `double` value, and `R
__ll_print` and `R __ll_scan` in any that has a `long long` value, which
is what brings the library's floating-point and `ll` conversions for the
`printf` and `scanf` families into a program that needs them.

### Data items (inside `D` … `E`)

```
B <n>          one byte
W <n>          two bytes, little-endian
T <n>          three bytes, little-endian
Q <n>          four bytes, little-endian (a long, or a float's bits)
H <hex>        the bytes of a hex string, in memory order (a double's or long long's eight bytes)
A <sym> <off>  three-byte address constant: symbol + offset
S "<text>"     the bytes of the string, no terminator
Z <n>          n zero bytes
SO <sym> "<text>"   a string object named <sym>, visibility s, defined beside this one
```

A string literal whose address an initialiser needs (`char *p = "hi";`) is
written as `SO __str<id>_<n> "hi"` inside the `D` that uses it, before or
after the `A` that refers to it; `cc2` emits it as its own data section
with the terminating NUL. `cc1` holds an object's initialiser and writes
its items in order of address when it is complete, the gaps as `Z`
(C99's designators may go back), writing early only when its table is
full. A compound literal's object, `__ini<id>_<n>`, is a `D` of its own:
written before the code that uses it, or, when the literal is in another
object's initialiser, after that object's `E` (as a wide string is).

## 4. Function body records

Inside `F` … `E`: string literals, control records and expression nodes.
Locals are addressed by their frame offsets (`LA`), which `cc1` computes by
ABI §5's rule.

### Streamed functions

`cc1` writes a function as it parses it, so it cannot know the frame
layout, which depends on every local, or a switch's cases before the end.
Such a function's `F` has `@` for its frame, and:

```
LA @<k>                 the address of local number <k>
SW @<k>                 pop the switch value; its table is SWT <k>
FRAME <size>            the frame size                        } after the body,
LOC <k> <offset>        local <k>'s frame offset              } before its E,
SWT <k> <default> <count>  switch <k>'s table; <count> K lines follow  } in any order
```

`cc2` reads ahead to these records before translating the body, and reads
a switch's table where it is, so the result is the same as for the
numbered form. A `G` or a `D` … `E` may also appear between the
statements of a function body (a block-scope static, or a local aggregate's
initial value): `cc2` prints it at once, before the function's own
section, as for an object written before the function.

### String literals

```
S <n> "<text>"
```

Defines string literal number `<n>` of this function; must precede its
first use. `cc2` places all of a function's strings in a pool immediately
after the function's code and addresses them as `_func+offset`, adding the
terminating NUL. Text escapes: `\\`, `\"`, and `\xHH` for any byte below
0x20 or above 0x7E; nothing else is escaped.

### Control records

```
L <n>                   label n (virtual: exists only in the IR)
J <n>                   unconditional jump
JF <n>                  pop the condition; jump if it is zero
JT <n>                  pop the condition; jump if it is non-zero
SW <default> <count>    pop the switch value (I or L); <count> K lines follow
K <value> <label>       one case of the preceding SW
RET                     pop the return value (I, L or F) into HL / E:UHL; jump to the epilogue
RETB <n>                pop an address; copy <n> bytes to the hidden result object; return its address
RETV                    return without a value
DROP                    pop and discard (ends an expression statement)
ASM <lines>             the next <lines> lines are copied verbatim into the output
```

Labels are numbered per function starting at 1 and each is defined exactly
once. `cc2` resolves every jump to a `$`-relative `jr` or `jp` and never
emits a label for them. A `SW` is lowered by `cc2` to a compare chain; a
jump table is a permitted implementation. A `JF`, `JT` or `SW` on an L
value tests all 32 bits. There is no `SW` on a Q value: `cc1` compares a
`long long` switch's value with each case in turn (`EQ.q`, `JF`), and
writes a Q condition as `NE.q` with a zero constant.

Falling off the end of a function is equivalent to `RETV` (and `HL` is
unspecified, per the ABI). `cc1` emits `RETV` explicitly for `void`
functions and `C 0` / `RET` for `main`. A function with the `r` flag
returns only through `RETB`, with `<n>` = 8 for `double` and `long long`.

### Expression nodes

Each node line pushes one value onto the tree-building stack or pops its
operands from it. At every control record and at `DROP`, the stack must be
empty afterwards; at `RET`, `RETB`, `JF`, `JT`, `SW` it must hold exactly
the one value they pop. Operands are pushed left to right: for a binary
node the left operand's subtree comes first.

Leaves:

```
C <n>              I constant, decimal, may be negative (24-bit two's complement)
C.l <n>            L constant, decimal: −2147483648 .. 4294967295, taken modulo 2³²
C.f <hex8>         F constant: the binary32 bits, 8 hex digits
C.d <hex16>        D constant: the binary64 bits, 16 hex digits; pushes the address of a copy
C.q <hex16>        Q constant: its 64 bits, 16 hex digits, most significant first; likewise
A <sym>            address of a global, static or function
LA <off>           address of a local or parameter: IX + off (off may be negative)
SA <n>             address of string literal n of this function
```

Memory:

```
LD <size> <sign>   pop address; push the I value loaded (sizes 1-3), extended to 24 bits
LD.l               pop address; push the L value at it
LD.f               pop address; push the F value at it
ST <size>          pop I value, pop address; store its low <size> bytes; push the value
ST.l, ST.f         likewise for an L or F value (4 bytes)
ST.d, ST.q         pop D or Q value (an address), pop address; copy 8 bytes; push the destination
COPY <n>           pop src, pop dst; copy n bytes; push dst        (struct and union assignment)
ASG <op> <size> <sign>   pop rhs, pop address; *addr = *addr op rhs; push the new value
ASG.l <op> <sign>, ASG.f <op>, ASG.d <op>, ASG.q <op> <sign>   likewise for L, F, D and Q lvalues
INCPRE <size> <sign> <delta>    pop address; *addr += delta; push the new value
INCPOST <size> <sign> <delta>   pop address; push the old value; *addr += delta
DECPRE, DECPOST                 likewise with -delta
INCPRE.l ... DECPOST.l, INCPRE.f ... DECPOST.q   the same for L, F, D and Q lvalues; no operands (the step is 1)
```

For `ST`, `ASG` and the increments, the **address is pushed first, then
the right-hand side**, so the lvalue's side effects happen once and before
the right-hand side. `<delta>` is 1 for arithmetic types and the element
size for pointers. `<op>` for `ASG` is one of the binary operators below,
without its suffix. Bit-fields have no records of their own: `cc1` reads
one as its byte (or, for a field wider than 8 bits, which starts a byte,
its three bytes) shifted and masked or sign-extended, and writes one by
reading, masking and storing those bytes (ABI §2's layout makes this
enough).

The value an assignment yields is the value **after** conversion to the
lvalue's type. For `ST`, `cc1` therefore emits `EXT <size> <sign>` on the
right-hand side before `ST` whenever the lvalue is narrower than `int`, so
`ST` stores the low bytes and pushes a value that is already canonical.
For `ASG`, `INCPRE` and the others, `cc2` re-extends the stored result
itself using the record's `<size>` and `<sign>`.

Binary operators (pop two, push one). The plain forms take and give I
values, `.l` forms L values, `.f` F values, `.d` D values, `.q` Q values
(comparisons give I 0/1 in every kind):

```
ADD SUB MUL DIVS DIVU REMS REMU SHL SHRS SHRU AND OR XOR
EQ NE LTS LTU LES LEU GTS GTU GES GEU
.l, .q:  all of the above; for SHL, SHRS and SHRU the right operand (the count) is I
.f, .d:  ADD SUB MUL DIV EQ NE LT LE GT GE
```

Pointer arithmetic is already scaled by `cc1` (`p + i` becomes `p`, `i`,
`C size`, `MUL`, `ADD`, with the multiply folded when `i` is a constant);
pointer difference is divided by the element size likewise.

Unary operators (pop one, push one):

```
NEG, NEG.l, NEG.f, NEG.d, NEG.q   negate
CPL, CPL.l, CPL.q          bitwise complement
NOT, NOT.l                 logical not: 0 -> 1, anything else -> 0 (an I result)
EXT <size> <sign>          I: truncate to <size> bytes and re-extend (conversion to char, short)
CV <from> <to>             convert between kinds (below)
```

Conversions between `int`, `unsigned int` and pointers are no-ops and are
not emitted. Widening from `char` or `short` is done by the `LD` that
loaded it. Conversions that change kind are `CV`, whose operands name a
kind with its size and signedness: `i1s i1u i2s i2u i3s i3u` (I), `l4s
l4u` (L), `f4` (F), `d8` (D), `q8s q8u` (Q). For example `CV i3s l4s` sign-extends an
`int` to `long`, `CV l4u i2u` truncates, `CV f4 d8` widens a `float`, `CV d8
i3s` truncates a `double` toward zero. `cc1` writes a floating `!`, and
a Q one, as `EQ` with a zero constant, so there is no `NOT.f`, `NOT.d` or
`NOT.q` in its output. `cc1` applies every conversion C
requires, the default argument promotions included (ABI §4); `cc2` never
converts implicitly.

Conditional evaluation (operands are subtrees; `cc2` emits them under
its own branches):

```
LAND               pop two subtrees (any kinds); push I 0/1 with short-circuit evaluation
LOR                likewise
SEL                pop else, pop then, pop cond; push the selected value   (?:, same kind both sides)
SEQ                pop right, pop left; evaluate both, push right           (comma)
```

Calls:

```
ARG                    pop a value; it becomes the next argument (I: 1 slot, L and F: 2)
ARGB <n>               pop an address; its <n> bytes become the next argument (⌈n/3⌉ slots):
                       a struct, a union, or a D or Q value (ARGB 8)
CALL <sym> <nargs>     call the function; push the return value
CALLI <nargs> <kind>   pop the function's address (pushed after the arguments); call it
```

`cc1` emits the **last** argument's subtree first, followed by `ARG`, then
the previous, so the machine stack receives them right-to-left as the ABI
requires. `<nargs>` counts arguments (the `ARG` and `ARGB` records the call
takes); `cc2` works out their slots from their kinds and removes that
many after the call. The kind of the pushed
return value is written after the call as `CALL <sym> <nargs> <kind>` (`i`,
`l`, `f`, `d`, `q`, or `v` for none): for `d` and `q`, and for a struct or union return
(kind `b`), `cc1` has passed the address of a result object (a local it
declares) as the first argument, the one pushed last, and the call pushes
that address. A call through an implicit declaration (strict mode only)
has kind `j`: an `int` result, as for `i`, which `cc2` also records as
`;;implicit <sym>` for `ld`'s return-kind check (object_format.md §8).
`cc2` removes the slots after the call. A call used as a statement is
followed by `DROP`.

## 5. What `cc2` may assume

- The tree-building stack discipline above holds, and every operator's
  operands are of the kind its suffix names; `cc2` reports a malformed file
  as an internal error and stops.
- Every `LA` offset is within the frame or the parameter area.
- Every `L` used by a jump is defined in the same function.

## 6. What `cc2` may do

- Rebuild each statement into a tree and pattern-match: `LA`+`LD` becomes
  `ld hl,(ix+d)`; `A`+`LD` becomes `ld hl,(_sym)`; a compare feeding `JF`
  becomes a conditional jump on flags; a constant right operand becomes an
  immediate.
- Choose `jr` or `jp` per branch by measured distance; choose helpers or
  inline sequences per operator.
- Allocate D and Q temporaries on the stack below the frame, and reuse their
  space between statements.
- Anything else that preserves the semantics and the ABI.

## 7. Example

C source (unit `count.c`):

```c
static int total;
int add(int n) { int i; for (i = 0; i < n; i++) total += i; return total; }
```

IR:

```
IR 2
U count.c 3f2a
G total 3 s
F add g 1 3 -
# i = 0
LA -3
C 0
ST 3
DROP
L 1
# i < n ?
LA -3
LD 3 s
LA 6
LD 3 s
LTS
JF 3
# total += i
A total
LA -3
LD 3 s
ASG ADD 3 s
DROP
# i++
LA -3
INCPOST 3 s 1
DROP
J 1
L 3
A total
LD 3 s
RET
E
```

`cc1` writes the streamed form instead: `F add g 1 @ -`, `LA @1` for `i`,
and `FRAME 3` and `LOC 1 -3` before the `E`.

## 8. Reading cheaply

`cc2` dispatches on a record name's first characters rather than
comparing whole names with `strcmp`, which once took much of its time.
Record names stay short and mostly distinct in their first two
characters.
