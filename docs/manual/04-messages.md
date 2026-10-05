# Messages

A build that succeeds prints only the assembler's report. When something is
wrong, the program that found it prints one line for each problem and the
build stops with status 200. This chapter shows what the messages look
like, what `-w` and `-Werror` change, what the exit statuses mean, and then
the messages you are most likely to meet, pass by pass, with what each
means and what to do about it. It does not list every message: each one is
a string in the compiler's source, and the last section says where to look.

## What a build prints

`agonc -o hello.bin hello.c`, when it succeeds, shows ez80asm's four lines
and nothing else:

```text
Setting minimum memory configuration
Assembling /tmp/hello.asm
Wrote hello.bin, 2875 bytes
Done in 1.20 seconds
```

The size and time vary. The preprocessor, the compiler and the linker say
nothing unless they have something to report, so `-c` and `-S` print
nothing at all when they succeed; `-time` and `-v` add timing
lines ([Using agonc](02-using-agonc.md#options)).

## How a message looks

| From | Form |
|---|---|
| the preprocessor and the compiler | `file:line: error: text`, `file:line: warning: text` |
| the driver | `agonc: error: text`, `agonc: warning: text` |
| the linker | `ld: error: text`; for a fault in a `.s` file, `file:line: error: text` |
| any pass, about its own files | `cpp: cannot open name`, `cc1: cannot create name`, and so on |
| the code generator (rare) | `file.ir:line: cc2: text` |
| the assembler | `File "name.asm" line n - text 'detail'` |

For example:

```text
game.c:41: error: call to undeclared function draw_ship
game.c:58: warning: control reaches the end of a function that returns a value
agonc: error: cannot find -lsprite
ld: error: undefined symbol _score (referenced from unit hud.c, section _show_hud)
File "/tmp/game.asm" line 2210 - Unknown identifier 'loop1'
```

The file and line in a preprocessor or compiler message are those of your
source, a header included: the line is where the offending token is, and
for a missing token, the line of the token before it. The linker names
symbols as the assembler sees them: a C name with `_` in front (`_main`),
and a `static` function or object as `__s`, its file's unit id (four
hexadecimal digits) and its name (`__s3f2a_helper`). A message that names a file in
`/tmp` is about an intermediate file; `-save-temps` keeps those files so
that you can look at the line it gives.

ez80asm reports only its first error, with a line number in the linked
`.asm` file, which holds the whole program. It colours its messages on the
screen, but the text carries everything. The Agon's ez80asm returns success
even when it fails, so agonc checks for the program file and adds
`agonc: error: the assembler did not produce hello.bin` when there is none.

The preprocessor and the compiler carry on after most errors, so that one
run reports several, and then stop without passing anything on. A missing
token (`expected ';' after a declaration`), an unterminated string or
comment, or a full table stops the pass at once. agonc runs nothing after a
pass that failed: when the preprocessor reports errors the compiler never
sees the file, so fixing one set of errors can reveal the next.

## Warnings, -w and -Werror

The preprocessor and the compiler warn about code that is legal but
probably wrong, or old-fashioned. A warning does not stop the build.

- `-w` suppresses every warning from the preprocessor and the compiler.
- `-Werror` reports each of those warnings as an error, with the same text
  after `error:`, and the build fails with status 200.
- Neither touches the driver's one warning (an input that `-E`, `-c` or
  `-S` does not use), the linker (which gives no warnings), or ez80asm's
  warnings about values truncated in assembly you wrote, which never fail a
  build.

All warnings are on by default, so `-Wall` changes nothing. agonc has no
options for individual warnings: `-Wextra`, `-Wno-unused` and the like are
unknown options, and so errors. Strict mode (`-ansi`) changes some messages:
see [Code written for C99 or GCC](#code-written-for-c99-or-gcc) below.

## Exit statuses

agonc itself returns one of three statuses:

| Status | Meaning |
|---|---|
| 0 | the program (or the `-c`, `-S` or `-E` output) was written |
| 200 | any error: a pass, the assembler, a missing file, a bad option |
| 130 | Ctrl-C stopped the build; agonc prints `agonc: interrupted` and removes what it was writing |

A program compiled by agonc returns these:

| Status | When |
|---|---|
| 0 | `main` returned 0 or `EXIT_SUCCESS`, or fell off its end |
| 200 | `exit(EXIT_FAILURE)` |
| 134 | `abort()`, or a failed `assert` |
| 128 + n | signal n's default action: 130 for Ctrl-C (`SIGINT`) |
| anything else | what `main` returned or `exit` was given, passed to MOS unchanged |

MOS prints a message of its own when a program ends with a status from 1
to 25 (MOS 3 adds 26). These are its error codes: 1 to 19 are FatFS's
results and the rest are MOS's own, and MOS prints the message for the code
whatever the program meant by it:

| Status | MOS prints |
|---|---|
| 1 | Error accessing SD card |
| 4 | Could not find file |
| 5 | Could not find path |
| 18 | Too many open files |
| 20 | Invalid command |
| 21 | Invalid executable |
| 22 | Out of memory |

So a program that ends with `exit(1)` appears to have a disk fault. That is
why `EXIT_FAILURE` is 200, and why agonc returns 200: MOS prints nothing for
it. The full table is in the MOS documentation's [status
codes](https://agonplatform.github.io/agon-docs/mos/API/#status-codes). In
an `exec` script any non-zero status stops the script.

## Driver messages

The driver checks the command line and every input before it runs
anything, so most of these come before any other message; the ones about
libraries, passes, the assembler and the image come later, at the link.

| Message | Meaning and remedy |
|---|---|
| `unknown option -Wextra` | agonc does not have it; `agonc -h` lists the options |
| `-g: there is no debugging information` | also `-x`, `-M...`, `-shared`, `-static`: options with no meaning here, each with its reason |
| `-o needs an argument` | `-o`, `-I`, `-L`, `-D`, `-U` and `-l` need a value, joined or as the next word |
| `no input files (agonc -h lists the options)` | nothing to compile |
| `prog.c: no such file` | check the name and the current folder |
| `prog.cpp: unknown file type (use .c, .i, .s or .asm)` | agonc goes by the extension |
| `b/util.c: another input has the same name` | two inputs called `util.c` would share `/tmp` files; rename one |
| `prog.c: the output would overwrite an input` | `-o` names an input |
| `prog.c: the program would overwrite a source file ...` | `-o` names a source file; a program's name ends in `.bin` |
| `-o with -E, -c or -S needs a single input` | name one input, or leave out `-o` |
| `cannot find -lutil` | no `libutil.s` in the `-L` folders, `/usrlib`, `/lib/agon` or `/lib` |
| `cannot run /bin/agonc/cc1.bin` | a pass or `/bin/ez80asm.bin` is missing, or would not load |
| `the assembler did not produce prog.bin` | ez80asm failed; its message is just above |
| `prog.bin: image too large: ...` | the program does not fit in memory (below) |
| `prog.asm: an .asm file is assembled alone` | an `.asm` input must be the only input |
| `cannot open response file files.rsp` | the `@file` named does not exist |
| `too many arguments` | more than 96 words, or 2,048 characters, after `@file` expansion |
| `too many input files` | more than 32 inputs |
| `path too long: ...` | a path, or a temporary file's, reaches 128 characters |
| warning: `util.s: input not used with -E, -c or -S` | that input was ignored |

**Image too large.** The message has a second line with the sizes:

```text
agonc: error: big.bin: image too large: code and data reach the bss
agonc: code and data 301234 bytes, bss 160000 bytes
```

The program file (code and initialised data) and its zeroed data (global
and `static` objects without an initialiser) must fit together in the 448 KB
from `0x040000` to `0x0AFFFF`, and the heap and the stack need what is left
([memory](03-the-language.md#memory)). agonc removes the program file. Make
large global arrays smaller, or allocate them with `malloc` at run time, so
that the program can choose a size and see a failure as a null pointer;
leave out `float`, `double` and `long long` if they are not needed, since
they bring in the floating-point library. A program that only just fits
can still fail at run time when its stack meets its heap, which nothing
detects.

**Response files.** MOS limits a command line to about 240 characters, and
a program's start-up code keeps at most 31 words after the program name,
dropping the rest without a message; agonc is no exception. For a long list
of files or options, put them in a file and give `@file`: the file's words
are separated by spaces, tabs or line ends, with no quoting and no nested
`@`, and the file must be under 1,200 bytes (`response file too long`
otherwise). agonc passes long command lines to its passes the same way, as
`/tmp/cpp.rsp` and the like, so a message such as `cc1: cannot open
response file /tmp/cc1.rsp` means `/tmp` could not be written.

**Files that will not open.** MOS has eight file handles and does not close
a program's files when it ends. agonc closes them after every pass, but a
program that crashed can leave handles open, and then a pass reports
`cannot open` for a file that exists. Restart the Agon.

## Preprocessor messages

| Message | Meaning and remedy |
|---|---|
| `cannot find include file mos_api.h` | not in the including file's folder, the `-I` folders, `/usrlib` or `/lib`; for a program from AgDev see [Porting from AgDev](07-porting-from-agdev.md) |
| `#error text` | the source asked for this error |
| `#warning text` | a warning the source asked for (default mode) |
| `unknown directive #warning` | `#warning` in strict mode, or a misspelt directive |
| `macro redefined differently: NAME` | two `#define`s of one name disagree; `#undef` it first |
| `MAX takes 2 arguments, not 3` | a function-like macro called with the wrong count |
| `unterminated call of macro MAX` | the closing `)` never came |
| `'...' in a macro's parameters (a C99 feature)` | variadic macros are not provided |
| `unterminated #if` | an `#if` without `#endif` in the same file; the line is the `#if`'s |
| `#endif without #if` | also `#else` and `#elif`; or `#else after #else` |
| `unterminated comment` | a `/*` never closed; the line is where it starts |
| `invalid #if expression` | often an undefined function-like macro used in `#if` |
| `#asm without #endasm` | the block runs to the end of the file |
| warning: `#pragma is ignored` | every `#pragma` except `#pragma weak` |
| warning: `a trigraph, which only strict mode (-ansi) replaces` | `??=` and the like stay as written in the default mode |

Table limits in the preprocessor are under [When a table is
full](#when-a-table-is-full).

## Compiler messages

### Declarations, calls and types

| Message | Meaning and remedy |
|---|---|
| `call to undeclared function printf` | no declaration in scope: include its header, or declare the function before the call |
| `undeclared identifier x` | misspelt, out of scope, or its header is missing |
| `wrong number of arguments to f (2 given, 1 expected)` | the call does not match the prototype |
| `conflicting types for f` | two declarations of `f` disagree, often a header and a definition |
| `redefinition of x` | two definitions in one file |
| `redeclaration in the same scope: x` | declared twice in one block |
| `size unknown for x` | an object of an incomplete type: the `struct` is declared but not defined |
| `no member named y` | not a member of that `struct` or `union` |
| `incompatible struct value in assignment` | also in `argument`, `return`, `initialiser`: different structure types |
| `assignment to a const object` | the object, or what the pointer points to, is `const` |
| `initialiser is not a constant` | a static object's initialiser must be constant; in strict mode, so must a local array's or structure's |
| `identifier longer than 47 characters: name` | names are significant to 47 characters, and a longer one is refused rather than cut |
| `string literal longer than 509 characters` | the limit holds after adjacent literals are joined; use an array of shorter strings, or a file |
| `more than 31 arguments in a call` | also `more than 31 parameters`: C89's limit |
| `more than 127 members in a struct` | also `more than 127 enumerators in an enum` |
| `static function used but never defined: f` | declared `static`, called, and not defined in this file |
| `expected ';' after a declaration` | a syntax error; the line is that of the token before the gap |
| `expected an expression` | a syntax error, often C99 or GCC syntax (below) |

**Implicit declarations.** In the default mode, calling a function with no
declaration in scope is an error, as in current GCC and Clang, because on
this machine a function returning `long`, `float`, `double` or a structure,
called as if it returned `int`, gives a wrong result without any sign. In
strict mode (`-ansi`) C89 allows the call, and agonc gives the warning
`implicit declaration of function f`. If `f` turns out to return one of
those types, the link fails with `ld: error: implicit call to _f, which
returns long (or float) (unit main.c, section _main)`, or `which returns
double, a structure or a union`. Either way the remedy is a prototype,
normally by including the right header.

### Code written for C99 or GCC

The default mode takes a few C99 features (`//` comments, `long long`,
declarations after statements, a comma after the last enumerator,
non-constant initialisers for local arrays and structures) but not the
rest. Code that uses the rest fails with a syntax error that does not name
the feature:

| In the source | Message | Rewrite as |
|---|---|---|
| `for (int i = 0; ...)` | `expected an expression` | declare `i` before the loop |
| `{ .x = 1 }`, `[3] = 1` | `expected an expression` | positional initialisers |
| `(int[]){ 1, 2 }` | `expected an expression` | a named object |
| `inline int f(void)` | `type defaults to int` and `expected ';' after a declaration` | drop `inline`, or `#define inline` |
| `bool`, `_Bool` | as for `inline` | `int`, or your own `typedef` |
| `uint8_t` without `<stdint.h>` | as for `inline` | include `<stdint.h>` |
| `int a[n];` | `not an integer constant expression` | `malloc`, or a fixed size |
| `#define F(a, ...)` | `'...' in a macro's parameters (a C99 feature)` | a fixed parameter list |
| `asm("..." : "=r"(x))` | `expected ')'` | basic `asm("...")` reading the frame ([inline assembly](#inline-assembly)) |
| `asm volatile ("...")` | `expected '(' after asm` | `asm("...")`, which agonc copies as written, where it stands |

In strict mode `long long` gives `'long long' is not C89`, a trailing comma
in an `enum` gives `a comma after the last enumerator (a C99 feature)`, and
`//` is not a comment, which usually shows as `declaration without a name`
or `expected an expression`. [The language](03-the-language.md) lists what
each mode accepts.

### Inline assembly

| Message | Meaning and remedy |
|---|---|
| `assembly outside a function: put it in a .s file (driver.md 2)` | `asm` and `#asm` work only inside a function; whole functions and data go in a `.s` file ([Assembly](02-using-agonc.md#assembly)) |
| `#asm block longer than 598 bytes (split it)` | end the block and start another |
| `asm needs a string literal` | `asm(...)` takes a string literal (adjacent literals join), not an expression |
| warning: `a global label in inline assembly, which must be unique in the program (abi.md 10): here` | use an `@local` label, which the function's own label scopes |

A label that is defined twice in the program is reported by the linker as a
duplicate definition (below); a name the assembly uses that nothing
defines, by ez80asm.

### Warnings

| Warning | Meaning |
|---|---|
| `type defaults to int (implicit int)` | `static x;` or `f() { ... }`: add `int` |
| `parameter defaults to int (implicit int): b` | a K&R parameter with no declaration |
| `an old-style (K&R) function definition; ...` | default mode only; a prototype-style definition has its calls checked |
| `declaration after a statement (a C99 feature)` | accepted in both modes |
| `control reaches the end of a function that returns a value` | a path ends without `return` |
| `'return;' in a function that returns a value` | the caller gets an undefined value |
| `incompatible pointer types (assignment)` | also `argument`, `initialiser`, `return`: a cast says you mean it |
| `assignment discards a const or volatile qualifier` | a `const char *` stored in a `char *`, say |
| `integer converted to pointer without a cast (argument)` | often a missing header, or `int` where a pointer is meant |
| `pointer converted to integer without a cast (assignment)` | as above, the other way |
| `comparison between a pointer and an integer` | compare with a pointer, or `0` |
| `shift count outside 0..23` | `int` is 24 bits: 0..31 for `long`, 0..63 for `long long` |
| `integer constant is so large that it is unsigned` | a decimal constant beyond `long long` |
| `floating constant out of range for double (it is infinity)` | also `for float` |
| `a multi-character character constant` | `'ab'` is `0x6162` |
| `an array assumed to have one element: a` | `int a[];` with no size anywhere in the file |
| `a declaration that declares nothing` | `struct { int a; };`, with neither a tag nor a name |
| `unused static function` | a `static` function nothing calls |

The shift warning catches a common surprise in code written for a 32-bit
`int`: `1 << 24` is a `long`'s job here, `1L << 24`.

## When a table is full

Each pass keeps what it knows in tables of fixed size, which is how it
fits in the Agon's memory. A pass does not allocate memory as it goes, so
instead of running out of memory, a source too large for one of its tables
stops it with a message naming the table, such as

```text
game.c:812: error: too many locals in one function (a cc1 table limit; raise it in cc1.h)
game.c:900: error: expression too complex (a cc1 table limit; raise MAX_NODES)
/tmp/game.ir:20711: cc2: too many instructions in one function (raise the limit in cc2.c)
ld: too many sections (limit reached; raise it in ld.c)
```

"Raise it" means rebuilding the compiler with a larger table
([BUILDING.md](../../BUILDING.md)). The usual answer is to make the piece
smaller: what the limit counts says which piece.

| Table, as the message names it | Limit | Counted over |
|---|---:|---|
| cpp: macros | 1,200 | defined at once |
| cpp: macro text | 32,000 bytes | names and replacements |
| cpp: line | 4,096 | characters in one logical line |
| cpp: `#include` nesting, `#if` nesting | 8, 32 | levels |
| cc1: identifiers, identifier characters | 2,000, 20,000 | distinct names in one file, headers included |
| cc1: global symbols | 1,200 | file-scope names in one file |
| cc1: types, parameter types | 800, 1,500 | one file |
| cc1: struct and enum tags, struct members | 150, 800 | one file |
| cc1: locals in one function | 300 | one function, its blocks included |
| cc1: blocks, nested blocks | 200, 64 | one function |
| cc1: case labels, switch statements | 300, 100 | one function |
| cc1: labels, branches, string literals | 100, 1,500, 400 | one function |
| cc1: expression too complex | 500 nodes | one expression |
| cc2: instructions in one function | 4,000 | one function's generated code |
| cc2: string literals and double constants | 400 | one function |
| ld: input files | 32 | the link, library files included |
| ld: sections, symbols, references | 1,500, 3,000, 8,000 | the whole program, library included |

A per-file table means splitting the source file, or including fewer
headers: every name a header declares counts, used or not. A per-function
one means splitting the function; a long `switch` is often best turned into
a table. An expression of more than a few hundred operators is usually
generated code, and can be broken up with temporary variables. For a cc2
limit, the `.ir` line is in the intermediate file: with `-save-temps`, the
nearest line above it of the form `F name g ...` (or `s` for a `static`
function) names the function.

The linker's limits cover the whole program. Up to five of its 32 input
files are the library's own (`crt0.s`, `rt.s`, `libc.s`, `libm.s` and
`libagon.s`), so 27 source files and `-l` libraries together always fit;
join small libraries into one. `agonc -v` prints the linker's use of each
of its tables (`ld: tables: sections 612/1500, ...`); the other passes print
theirs (`cc1: peaks: ...`) only when run on their own with `-v`.

## Linker messages

| Message | Meaning and remedy |
|---|---|
| `undefined symbol _f (referenced from unit main.c, section _main)` | `_main` in `main.c` uses `f`, which no input defines |
| `undefined symbol _f (a root)` | `-Wl,--entry=_f` names something that does not exist |
| `implicit call to _f, which returns long (or float) (...)` | strict mode: see [implicit declarations](#declarations-calls-and-types) |
| `/tmp/b.s:13: error: duplicate definition of _count (unit a.c section _count, and unit b.c section _count)` | two files define the same global name |
| `lib.idx does not match lib.s (section _f); make it again with agonc --index` | the library changed since it was indexed |
| `no __start code section (is crt0.s missing?)` | `-nostdlib` without start-up code of your own |
| `unit x.s has id 1a2b, but its name hashes to 3c4d` | a hand-written `.s` file's `;;unit` line; leave the id out and the linker works it out |
| `file.s:7: error: unknown marker ;;sectn` | also `malformed ;;sect line` and the like: a hand-written `.s` file's marker lines ([the object format](../object_format.md)) |
| `file.s:9: error: line longer than 255 characters` | split the line |

**Undefined symbols.** The linker reports only symbols that reachable code
uses: a function nothing calls may refer to anything. An undefined symbol
usually means a source file or a `-l` library left off the command line, a
name misspelt in a declaration, or a library function that agonc does not
have (`snprintf` and `strdup`, for example, are not in its C89 library).

**Duplicate definitions.** In agonc an external object has exactly one
definition in the program. `int count;` at file scope in two files, which
some older compilers merged, is two definitions here: define it in one file
and declare it `extern int count;` in a header the others include. A name
that should be private to its file can be made `static`. The same message
reports a label defined twice in inline assembly, even when both are in one
file.

## Assembler messages

ez80asm assembles the linked program, so its messages are almost always
about assembly you wrote: `asm` statements, `#asm` blocks or `.s` files.
The line number is in `/tmp/prog.asm` (named after the output); build again
with `-save-temps` to look at it. When the message comes from ez80asm the
line itself is often printed under it.

| Message | Meaning and remedy |
|---|---|
| `Unknown identifier 'name'` | a label or symbol nothing defines; a C name needs its `_` |
| `Index register offset exceeded '200'` | `(ix+d)` reaches only -128 to 127; go through `HL` for a far local |
| `Relative jump too large` | a `jr` or `djnz` more than about 128 bytes away; use `jp` |
| `Invalid mnemonic 'mvi'` | not an eZ80 instruction |
| `Operand(s) not matching mnemonic` | that instruction does not take those operands |
| `Label already defined 'name'` | a second definition the linker did not see |
| `Label too long 'name'` | labels are at most 64 characters |
| `Error allocating memory; try the -m option` | the assembler ran out of memory (below) |
| `Filename too long` | ez80asm takes paths of at most 64 characters, including the folder |

agonc already passes `-m`, ez80asm's smaller memory configuration, so the
advice in the memory message cannot be followed. The assembler holds every
label of the program, and the code agonc generates keeps that small (one
label for each function and object); assembly with many global labels, or
an exceptionally large program, can still run out. Use `@local` labels in
your own assembly, and reduce the number of global ones.

## Where the rest are

Each message is a string literal in the source of the program that prints
it, so searching for its text finds the code and the reason:
`src/agonc/agonc.c` for the driver, `src/cpp/cpp.c` for the preprocessor,
`src/cc1/` for the compiler (most in `stmt.c` and `expr.c`), `src/cc2/cc2.c`
for the code generator and `src/ld/ld.c` for the linker. ez80asm's messages
are listed in its [source](https://github.com/AgonPlatform/agon-ez80asm). A
message that starts `internal` or `cc2 internal error` is a fault in the
compiler; the source that caused it is worth reporting.
