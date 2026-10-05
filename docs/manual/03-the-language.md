# The language

agonc compiles ANSI C89 (the same language as ISO C90), with its whole
standard library, plus a few things from C99 and GCC that existing programs
use. This chapter covers what is particular to agonc; the formal account,
following the standard's list of implementation-defined behaviour, is
[the language specification](../c89_spec.md).

## The two modes

By default agonc accepts some of C99 and is strict about calls to undeclared
functions. `-ansi` (or `-std=c89`, `-std=c90`) selects strict C89, the
conforming mode.

| | Default | Strict (`-ansi`) |
|---|---|---|
| `//` comments | accepted | not comments, as C89 requires |
| `long long`, `LL` and `ULL` constants | accepted | errors |
| calling an undeclared function | an error | accepted, with a warning |
| implicit `int` (`static x;`, `f() { ... }`) | accepted, with a warning | accepted, with a warning |
| a declaration after a statement | accepted, with a warning | accepted, with a warning |
| a comma after the last enumerator, `#warning`, non-constant initialisers for local arrays and structures | accepted | errors |
| trigraphs (`??=` and the rest) | ignored, with a warning | recognised |
| `asm` | a keyword | an ordinary name; use `__asm` |

The default mode rejects calls to undeclared functions because, on this
target, they hide real errors: a function that returns `long`, `float`,
`double` or a structure, called through an implicit `int` declaration, gives
a wrong result without any other sign. In strict mode, where C89 requires
such calls to be accepted, the linker still stops the build if a function
called that way returns one of those types.

## Types

| Type | Bytes | Range or form |
|---|---:|---|
| `char` | 1 | signed: −128 to 127 |
| `unsigned char` | 1 | 0 to 255 |
| `short` | 2 | −32768 to 32767 |
| `int` | 3 | −8388608 to 8388607 |
| `unsigned int`, `size_t` | 3 | 0 to 16777215 |
| `long` | 4 | −2147483648 to 2147483647 |
| `unsigned long` | 4 | 0 to 4294967295 |
| `long long` (default mode) | 8 | −2⁶³ to 2⁶³−1 |
| `unsigned long long` (default mode) | 8 | 0 to 2⁶⁴−1 |
| `float` | 4 | IEEE 754 single precision |
| `double`, `long double` | 8 | IEEE 754 double precision |
| pointers | 3 | a 24-bit address |
| `enum` | 3 | as `int` |

**`int` is 24 bits**, the eZ80's natural word in ADL mode. Code written for
16-bit `int` usually works; code that assumes 32-bit `int` needs `long` where
values pass 8388607. Watch for:

- constants: `8388608` is a `long`, and `1 << 23` is negative;
- `printf`: a `long` needs `%ld`, and an `int` argument is 3 bytes;
- arithmetic on `int` that overflows wraps, as on most machines (agonc
  defines it; C leaves it undefined).

**`char` is signed**, as in AgDev and on most PCs. **`double` is 64 bits**,
correctly rounded, with subnormals, infinities and NaNs; `long double` is the
same type. Converting decimal to binary and back, whether in the compiler
(constants), `strtod`, `scanf` or `printf`, is correctly rounded too, so
results can be checked bit for bit against a PC.

Other details: `/` truncates toward zero and `%` takes the dividend's sign;
`>>` of a negative number shifts in sign bits; structures have **no
padding**, so `sizeof` a structure is the sum of its members; a plain `int`
bit-field is signed, fields fill each byte from its lowest bit, and no
bit-field is wider than 24 bits; `register` is accepted and does nothing.

## Floating point and long long cost time

The eZ80 has no floating-point hardware and no 64-bit arithmetic, so
`float`, `double` and `long long` are done by library routines. On an Agon,
a `double` addition or multiplication takes about 0.2 ms, `sin` or `exp`
about 5 ms, `pow` about 20 ms. `int` arithmetic is far faster, and `long`
somewhat slower than `int`. The floating-point library, `libm.s`, is linked
only into programs that use these types.

## Extensions

- **Inline assembly**: `asm("ld a,0");` or `__asm(...)` inside a function
  adds one line; `#asm` ... `#endasm` adds a block. The lines are copied into
  the function as written. What the assembly may change, and how to reach
  arguments and locals, is set out in [the ABI](../abi.md#10-inline-assembly-contract).
  Whole functions in assembly go in a `.s` file
  ([Using agonc](02-using-agonc.md#assembly)).
- **`#pragma weak name`**: references to `name` from this file do not pull it
  into the program; if nothing else does, its address is 0.
- **GCC spellings**: `__attribute__((...))` is accepted and ignored;
  `__inline__`, `__inline`, `__restrict` and `__extension__` are ignored;
  `__const__`, `__volatile__` and `__signed__` are the keywords. The common
  `__builtin_` functions (`__builtin_memcpy`, `__builtin_expect`,
  `__builtin_offsetof` and others) work.
- **Bit-fields** of `char`, `short` and `long` type, up to 24 bits.
- **`<stdint.h>`**, and in the default mode `long long` with its library
  functions and `printf`'s `%lld`.
- **Source files**: CR LF line endings are fine, `#include` names are not
  case-sensitive, and a `0x1A` byte ends a file (CP/M's end-of-file mark).
- **Predefined macros**: `__AGONC__`, `__EZ80__` and `__ADL__` are 1; GCC's
  `__SIZEOF_INT__` (3), `__INT_MAX__` and the like have this target's values.

Not provided, from C99 or GCC: declarations in a `for` statement,
designated initialisers, `inline`, `_Bool`, variable-length arrays, compound
literals, `restrict`, hexadecimal floating constants, statement
expressions, and GCC's extended `asm` with operands.

## Limits

The compiler's tables have fixed sizes. Each is at least what C89 requires,
and a source that exceeds one gets a message naming the limit. The ones a
large program may meet:

| Limit | agonc |
|---|---:|
| characters in an identifier (all significant) | 47 |
| external names and file-scope statics in one source file | 1,200 |
| macros defined at once | 1,200 |
| local names in one function | 300 |
| parameters of a function, arguments of a call | 31 |
| `case` labels in one function | 300 |
| members of one structure or union | 127 |
| members of all structures and unions in one file, together | 800 |
| constants in one enumeration | 127 |
| characters in one string literal, after joining adjacent ones | 509 |
| characters in a logical source line | 4,096 |
| nesting of `#include` | 8 |
| nesting of `#if` | 32 |

When a limit is reached, splitting the source file, or a long function, is
the usual answer. A pass run on its own with `-v` (`cc1 file.i out.ir -v`,
say) reports how much of each of its tables it used. The full list is in [the language specification](../c89_spec.md#12-translation-limits).

## Memory

A program loads at `0x040000` and has the 448 KB up to `0x0AFFFF` for its
code, data, heap and stack together:

```text
0x040000  code and initialised data, as loaded from the file
          heap: malloc's memory, growing up
          ...free memory, shared...
          stack: locals and calls, growing down
          zeroed data: static and global objects without an initialiser
0x0AFFFF
```

Above that, `0x0B0000` to `0x0B7FFF` belongs to agonc itself and other
*moslets* (small MOS commands), and MOS lives above `0x0B8000`. A compiled
program never touches either.

The heap and the stack share whatever the program's code and data leave
free; neither has a fixed size. `malloc` returns a null pointer rather than
grow to within 256 bytes of the stack. Nothing checks the stack, so deep
recursion or very large local arrays can overwrite the heap; a large array
is better `static` or allocated. A global or `static` object without an
initialiser takes no space in the program file: it is zeroed when the
program starts.

agonc's own passes are programs like any other, which is why the compiler
itself can only use the same 448 KB. The limits above are what keeps each
pass inside it.
