"""Tests for cc1 (with cc2, ld and the runtime behind it).

    test_cc1.py [--update] [--no-emu] [--no-stage1]

T1  hello.c's IR against a golden file.
D   diagnostics: small sources, each with the exact message cc1 must print
    (file:line: error|warning: text) and its exit status; -w and -Werror.
I   IR shapes: what a source must (not) produce - sizeof's unevaluated
    operand leaves no temporary in the frame and no R record.
T4  t_exec.c (every M3 feature, ~170 run-time checks) compiled by
    cc1+cc2+ld and run on the emulator: exit 0. Guards against a vacuous
    harness: one wrong expectation must be reported by its index, and the
    number of checks executed must be at least 150.
T4  t_m4.c (M4's struct, enum, typedef, switch and goto; 115 checks), with
    the same two guards.
T4  t_m8.c (M8's short, long, signed char, const and volatile, constant
    typing and conversions; 218 checks), with the same two guards.
T4  t_m9.c (M9: structs passed and returned by value, struct values in
    expressions, compound division through a complex lvalue,
    multi-dimensional arrays, and initialisers for aggregates, static and
    local), with the same two guards.
T4  t_m10.c (M10: block scope, tentative and K&R definitions, implicit
    int, unions, bit-fields and function pointers; compiled with -ansi),
    with the same two guards.
T4  t_m11.c (M11: wide characters and strings), with the same two guards.
T4  t_m14.c (M14: C89's scopes, GCC's spellings, the default mode's enum
    comma and non-constant local initialisers), with the same two guards.
T4  t_m13.c (M13: float and double, every result checked bit for bit):
    first built by the PC's C compiler (HOSTCC, as config.mk sets it) and
    run there, so the expected bits are the PC's too; then on the emulator
    with the same two guards.
T4  t_ll.c (long long and unsigned long long, checked byte by byte): the
    same, the PC's build with wrapping overflow (-fwrapv), as the target's.
S1  M3's milestone: on the emulator, the AgDev-built cc1, cc2 and ld and the
    on-device ez80asm compile hello.c and the t_*.c execution tests;
    each runs (exit 42 or 0), and every intermediate file (.ir, .s, .asm)
    equals the host's.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join("build", "test", "cc1")
PY = sys.executable
HOST = os.path.join("build", "host")
RUNTIME = [os.path.join("lib", "rt", "crt0.s"), os.path.join("lib", "rt", "rt.s"),
           os.path.join("build", "agon", "lib", "agonc", "libc.s"), os.path.join("build", "agon", "lib", "agonc", "libm.s")]


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, **kw)


def o(name):
    return os.path.join(OUT, name)


def compile_c(src, stem, cc1_flags=()):
    """cc1 + cc2 + ld + ez80asm; returns a problem string or None."""
    steps = [
        [os.path.join(HOST, "cc1.exe"), src, o(stem + ".ir"), "-u", os.path.basename(src)] + list(cc1_flags),
        [os.path.join(HOST, "cc2.exe"), o(stem + ".ir"), o(stem + ".s")] + ([] if os.environ.get("AGONC_TEST_O0") else ["-O"]),
        [os.path.join(HOST, "ld.exe"), "-o", o(stem + ".asm")] + RUNTIME + [o(stem + ".s")],
        [os.path.join("third_party", "bin", "ez80asm.exe"), o(stem + ".asm"), o(stem + ".bin")],
    ]
    for cmd in steps:
        r = sh(cmd)
        if r.returncode:
            return f"{os.path.basename(cmd[0])} failed: {(r.stderr or r.stdout).strip()[:400]}"
    return None


def run_bin(stem, timeout=60):
    return sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--bin", o(stem + ".bin"),
               "--cmd", stem, "--sdcard-name", "cc1", "--timeout", str(timeout)]).returncode


# ---- T1 -----------------------------------------------------------------------------

def t1(update):
    r = sh([os.path.join(HOST, "cc1.exe"), os.path.join("tests", "cc1", "hello.c"), o("hello.ir")])
    if r.returncode:
        return [f"cc1 failed: {r.stderr.strip()}"]
    got = open(os.path.join(REPO, o("hello.ir"))).read()
    golden = os.path.join(HERE, "hello.ir.golden")
    if update:
        open(golden, "w", newline="\n").write(got)
    return [] if open(golden).read() == got else ["hello.ir differs from hello.ir.golden"]


# ---- D: diagnostics ----------------------------------------------------------------------
# (name, source, flags, expected status, expected lines - each must appear exactly)

DIAG = [
    ("undeclared", "int main(void) { return x; }", [], 200, ["d.c:1: error: undeclared identifier x"]),
    ("undeclared call", "int main(void) { foo(); return 0; }", [], 200,
     ["d.c:1: error: call to undeclared function foo"]),
    ("arg count", "int f(int a);\nint main(void) { return f(1, 2); }", [], 200,
     ["d.c:2: error: wrong number of arguments to f (2 given, 1 expected)"]),
    ("31 arguments in a call", "int f();\nint g(void) { return f(" + ", ".join(["1"] * 31) + "); }", [], 0, []),
    ("32 arguments in a call", "int f();\nint g(void) { return f(" + ", ".join(["1"] * 32) + "); }", [], 200,
     ["d.c:2: error: more than 31 arguments in a call"]),
    ("32 K&R parameter names", "int f(" + ", ".join("p%d" % i for i in range(32)) + ") { return 0; }", [], 200,
     ["d.c:1: error: more than 31 parameters"]),
    ("local redeclaration", "int main(void) { int a; int a; return 0; }", [], 200,
     ["d.c:1: error: redeclaration in the same scope: a"]),
    ("a nested block may shadow", "int main(void) {\n int a;\n {\n  int a;\n }\n return 0;\n}", [], 0, []),
    ("a parameter redeclared in the body", "int f(int a) { int a; return a; }", [], 200,
     ["d.c:1: error: redeclaration in the same scope: a"]),
    ("a name out of scope", "int main(void) {\n {\n  int a;\n }\n return a;\n}", [], 200,
     ["d.c:5: error: undeclared identifier a"]),
    ("block-scope static function", "int main(void) { static int f(void); return 0; }", [], 200,
     ["d.c:1: error: a function declared static at block scope"]),
    ("block-scope extern twice", "int main(void) { extern int x; extern int x; return x; }", [], 0, []),
    ("void variable", "void v;", [], 200, ["d.c:1: error: variable of type void"]),
    ("return value in void", "void f(void) { return 1; }", [], 200,
     ["d.c:1: error: return with a value in a void function"]),
    ("break outside loop", "int main(void) { break; }", [], 200, ["d.c:1: error: break outside a loop"]),
    ("not an lvalue", "int main(void) { 3 = 4; return 0; }", [], 200,
     ["d.c:1: error: assignment to something that is not an lvalue"]),
    ("union", "union u { int a; long b; };\nunion u x = { 1 };", [], 0, []),
    ("a union tag as a struct", "union u { int a; };\nstruct u x;", [], 200,
     ["d.c:2: error: 'struct' used with a union or enum tag: u"]),
    ("a union initialiser takes one value", "union u { int a; char b; } x = { 1, 2 };", [], 200,
     ["d.c:1: error: too many initialisers"]),
    ("a bit-field too wide", "struct s { int a : 25; };", [], 200,
     ["d.c:1: error: a bit-field wider than its type (or than 24 bits) or of negative width"]),
    ("a char bit-field too wide", "struct s { char a : 9; };", [], 200,
     ["d.c:1: error: a bit-field wider than its type (or than 24 bits) or of negative width"]),
    ("multi-character constant", "int c = 'ab';", [], 0, ["d.c:1: warning: a multi-character character constant"]),
    ("four characters", "int c = 'abcd';", [], 200, ["d.c:1: error: a character constant with more than three characters"]),
    ("a narrow escape beyond a byte", "int c = '\\x100';", [], 200, ["d.c:1: error: \\x escape out of range"]),
    ("a wide escape beyond wchar_t", "int c = L'\\x1000000';", [], 200, ["d.c:1: error: \\x escape out of range"]),
    ("a wide constant of two characters", "int c = L'ab';", [], 200,
     ["d.c:1: error: a wide character constant with more than one character"]),
    ("a wide string next to a narrow one", "char *s = \"a\" L\"b\";", [], 200,
     ["d.c:1: error: a wide string literal next to a narrow one"]),
    ("a narrow string next to a wide one", "int *s = L\"a\" \"b\";", [], 200,
     ["d.c:1: error: a narrow string literal next to a wide one"]),
    ("a wide string for a char array", "char c[] = L\"a\";", [], 200, ["d.c:1: error: a wide string for a char array"]),
    ("a narrow string for a wchar_t array", "int c[] = \"a\";", [], 200,
     ["d.c:1: error: a narrow string for a wchar_t array"]),
    ("a bit-field of a pointer", "struct s { char *p : 3; };", [], 200,
     ["d.c:1: error: a bit-field must have an integer type"]),
    ("a named bit-field of width 0", "struct s { int b : 1; int a : 0; };", [], 200,
     ["d.c:1: error: a named bit-field of width 0"]),
    ("unnamed bit-fields pad", "struct s { int a : 3; int : 5; unsigned : 0; int b : 2; };", [], 0, []),
    ("the address of a bit-field", "struct s { int a : 3; };\nint main(void) { struct s x; int *p; p = &x.a; return 0; }",
     [], 200, ["d.c:2: error: the address of a bit-field"]),
    ("sizeof a bit-field", "struct s { int a : 3; };\nint main(void) { struct s x; return sizeof x.a; }", [], 200,
     ["d.c:2: error: sizeof a bit-field"]),
    # M13: float and double
    ("float with long", "long float f;", [], 200,
     ["d.c:1: error: 'float' with 'short', 'long', 'signed' or 'unsigned'"]),
    ("unsigned double", "unsigned double d;", [], 200, ["d.c:1: error: 'double' with 'short', 'long long', 'signed' or 'unsigned'"]),
    ("% on a double", "double d;\nint main(void) { return d % 2; }", [], 200,
     ["d.c:2: error: operand must be an integer: arithmetic"]),
    ("~ on a float", "float f;\nint main(void) { return ~f; }", [], 200, ["d.c:2: error: operand must be an integer: '~'"]),
    ("<<= on a double", "double d;\nvoid f(void) { d <<= 1; }", [], 200,
     ["d.c:2: error: only += -= *= /= apply to a floating value"]),
    ("pointer compared with a double", "double d;\nint *p;\nint f(void) { return p == d; }", [], 200,
     ["d.c:3: error: comparison between a pointer and a floating value"]),
    ("a double cast to a pointer", "double d;\nint *p;\nvoid f(void) { p = (int *)d; }", [], 200,
     ["d.c:3: error: a cast between a pointer and a floating type"]),
    ("switch on a double", "double d;\nvoid f(void) { switch (d) { } }", [], 200,
     ["d.c:2: error: a switch needs an integer expression"]),
    ("a double array size", "int a[2.0];", [], 200, ["d.c:1: error: not an integer constant expression"]),
    ("a double bit-field", "struct s { double d : 3; };", [], 200, ["d.c:1: error: a bit-field must have an integer type"]),
    ("float constant out of range", "float f = 1e39f;", [], 0,
     ["d.c:1: warning: floating constant out of range for float (it is infinity)"]),
    ("double constant out of range", "double d = 1e309;", [], 0,
     ["d.c:1: warning: floating constant out of range for double (it is infinity)"]),
    ("bad floating suffix", "double d = 1.5u;", [], 200, ["d.c:1: error: invalid suffix on floating constant"]),
    ("a double where an integer constant is needed", "enum { A = 1.5 };", [], 200,
     ["d.c:1: error: not an integer constant expression"]),
    ("float, double and long double", "float f = 1;\ndouble d = 2.5e-3;\nlong double l = .5L;\n"
     "double g(float a, double b) { return a < b ? -a : b / 2; }", [], 0, []),
    # M4: struct, enum, typedef, switch, goto
    ("unknown member", "struct s { int a; };\nint main(void) { struct s v; return v.b; }", [], 200,
     ["d.c:2: error: no member named b"]),
    ("'.' on a non-struct", "int main(void) { int x; return x.a; }", [], 200, ["d.c:1: error: '.' needs a struct"]),
    ("'->' on a struct", "struct s { int a; };\nint main(void) { struct s v; return v->a; }", [], 200,
     ["d.c:2: error: '->' needs a pointer to a struct"]),
    ("incomplete struct object", "struct s v;", [], 200, ["d.c:1: error: size unknown for v"]),
    ("struct parameter", "struct s { int a; };\nint f(struct s v);", [], 0, []),
    ("struct return", "struct s { int a; };\nstruct s f(void);", [], 0, []),
    ("incomplete struct parameter", "struct s;\nint f(struct s v) { return 0; }", [], 200,
     ["d.c:2: error: a parameter has an incomplete struct type"]),
    ("incomplete struct result", "struct s;\nstruct s f(void);\nint main(void) { f(); return 0; }", [], 200,
     ["d.c:3: error: call to a function returning an incomplete struct: f"]),
    ("wrong struct argument", "struct s { int a; };\nstruct t { int a; };\nint f(struct s v);\n"
     "int main(void) { struct t x; return f(x); }", [], 200,
     ["d.c:4: error: incompatible struct value in argument"]),
    ("wrong struct returned", "struct s { int a; };\nstruct t { int a; };\nstruct s f(void) { struct t x; return x; }",
     [], 200, ["d.c:3: error: incompatible struct value in return"]),
    ("member of a call's struct", "struct s { int a; };\nstruct s f(void);\nint main(void) { return f().a; }",
     [], 0, []),
    ("struct initialiser", "struct s { int a; };\nstruct s v = { 1 };", [], 0, []),
    ("too many initialisers", "int a[2] = { 1, 2, 3 };", [], 200, ["d.c:1: error: too many initialisers"]),
    ("too many member initialisers", "struct s { int a; char b; };\nstruct s v = { 1, 2, 3 };", [], 200,
     ["d.c:2: error: too many initialisers"]),
    ("string too long for its array", "char s[3] = \"abcd\";", [], 200,
     ["d.c:1: error: string initialiser longer than the array"]),
    ("exact string fills its array", "char s[3] = \"abc\";", [], 0, []),
    ("non-constant local aggregate, strict", "int main(void) { int x; int a[2] = { x, 1 }; return a[0]; }", ["-ansi"],
     200, ["d.c:1: error: initialiser is not a constant"]),
    ("non-constant local aggregate, default mode", "int main(void) { int x = 1; int a[2] = { x, 1 }; return a[0]; }",
     [], 0, []),
    ("non-constant static aggregate", "int x;\nint main(void) { static int a[2] = { x, 1 }; return a[0]; }", [],
     200, ["d.c:2: error: initialiser is not a constant"]),
    ("a struct value for a member, default mode",
     "struct b { int x; };\nstruct a { int z; struct b b; };\nint f(struct b v) { struct a a = { 1, v }; return a.b.x; }",
     [], 0, []),
    ("a struct value for a member, strict",
     "struct b { int x; };\nstruct a { int z; struct b b; };\nint f(struct b v) { struct a a = { 1, v }; return a.b.x; }",
     ["-ansi"], 200, ["d.c:3: error: initialiser is not a constant"]),
    ("a struct value for a static member",
     "struct b { int x; };\nstruct a { int z; struct b b; };\nstruct b v;\nstruct a a = { 1, v };",
     [], 200, ["d.c:4: error: initialiser is not a constant"]),
    ("an incompatible struct value for a member",
     "struct b { int x; };\nstruct c { int x; };\nstruct a { int z; struct b b; };\n"
     "int f(struct c v) { struct a a = { 1, v }; return a.z; }",
     [], 200, ["d.c:4: error: incompatible struct value in initialiser"]),
    ("address constants", "int a[4];\nint *p = &a[2];\nint *q = a + 1;\nchar *r = \"xy\" + 1;", [], 0, []),
    ("a variable is not an address constant", "int x;\nint *p;\nint *q = p;", [], 200,
     ["d.c:3: error: initialiser is not a constant"]),
    ("struct redefinition", "struct s { int a; };\nstruct s { int b; };", [], 200,
     ["d.c:2: error: redefinition of tag s"]),
    ("duplicate member", "struct s { int a; char a; };", [], 200, ["d.c:1: error: duplicate member a"]),
    ("struct containing itself", "struct s { int a; struct s b; };", [], 200,
     ["d.c:1: error: member of void or incomplete type: b"]),
    ("different struct types", "struct s { int a; };\nstruct t { int a; };\nstruct s x;\nstruct t y;\n"
     "int main(void) { x = y; return 0; }", [], 200, ["d.c:5: error: incompatible struct value in assignment"]),
    ("struct comparison", "struct s { int a; };\nstruct s x;\nint main(void) { return x == x; }", [], 200,
     ["d.c:3: error: operand must be a number or pointer: comparison"]),
    ("struct tag forward declaration", "struct s;\nstruct s *p;\nstruct s { int a; };\nint main(void) { return p->a; }",
     [], 0, []),
    ("undefined enum", "enum e x;", [], 200, ["d.c:1: error: undefined enum e"]),
    ("enum trailing comma, strict", "enum e { A, B, };", ["-ansi"], 200,
     ["d.c:1: error: a comma after the last enumerator (a C99 feature)"]),
    ("enum trailing comma, default mode", "enum e { A, B, };", [], 0, []),
    ("enumerator clash", "int A;\nenum e { A };", [], 200,
     ["d.c:2: error: redeclared as a different kind of symbol: A"]),
    ("block-scope typedef", "int main(void) { typedef int t; t x = 1; return x; }", [], 0, []),
    ("block-scope typedef as a value", "int main(void) { typedef int t; return t; }", [], 200,
     ["d.c:1: error: a type name where a value is expected: t"]),
    ("typedef as a value", "typedef int t;\nint main(void) { return t; }", [], 200,
     ["d.c:2: error: a type name where a value is expected: t"]),
    ("conflicting typedef", "typedef int t;\ntypedef char t;", [], 200,
     ["d.c:2: error: redeclared as a different kind of symbol: t"]),
    ("repeated identical typedef", "typedef int t;\ntypedef int t;\nt x;", [], 0, []),
    ("local hides a typedef", "typedef int t;\nint main(void) { int t; t = 3; return t; }", [], 0, []),
    ("duplicate case", "int f(int v) { switch (v) { case 1: case 1: return 0; } return 1; }", [], 200,
     ["d.c:1: error: duplicate case value"]),
    ("case outside a switch", "int f(void) { case 1: return 0; }", [], 200, ["d.c:1: error: case outside a switch"]),
    ("two defaults", "int f(int v) { switch (v) { default: default: return 0; } }", [], 200,
     ["d.c:1: error: more than one default in a switch"]),
    ("non-constant case", "int f(int v) { switch (v) { case v: return 0; } return 1; }", [], 200,
     ["d.c:1: error: not an integer constant expression"]),
    ("switch on a pointer", "int f(char *p) { switch (p) { } return 1; }", [], 200,
     ["d.c:1: error: a switch needs an integer expression"]),
    ("undefined label", "int f(void) {\n goto nowhere;\n return 0;\n}", [], 200,
     ["d.c:2: error: goto to an undefined label nowhere"]),
    ("duplicate label", "int f(void) {\nx: ;\nx: ;\n return 0;\n}", [], 200, ["d.c:3: error: duplicate label x"]),
    ("switch returning on every path", "int f(int v) { switch (v) { case 1: return 1; default: return 2; } }",
     [], 0, []),
    ("switch without default falls off", "int f(int v) {\n switch (v) { case 1: return 1; }\n}", [], 0,
     ["d.c:3: warning: control reaches the end of a function that returns a value"]),
    ("switch with break falls off", "int f(int v) {\n switch (v) { default: break; }\n}", [], 0,
     ["d.c:3: warning: control reaches the end of a function that returns a value"]),
    ("goto loop never falls off", "int f(void) {\ntop:\n goto top;\n}", [], 0, []),
    ("va_start without '...'", "int f(int a) { char *ap; __va_start(ap, a); return 0; }", [], 200,
     ["d.c:1: error: va_start in a function without '...'"]),
    ("va_start on an earlier parameter", "int f(int a, int b, ...) { char *ap; __va_start(ap, a); return 0; }",
     [], 200, ["d.c:1: error: va_start needs the last named parameter"]),
    ("va_arg of a struct", "struct s { int a; };\nint f(int a, ...) {\n char *ap;\n __va_start(ap, a);\n"
     " return __va_arg(ap, struct s).a;\n}", [], 0, []),
    ("va_arg of an incomplete struct", "struct s;\nint f(int a, ...) {\n char *ap;\n __va_start(ap, a);\n"
     " __va_arg(ap, struct s);\n return 0;\n}", [], 200,
     ["d.c:5: error: va_arg needs a number, pointer or struct type"]),
    ("va_arg on a non-lvalue", "int f(int a, ...) { return __va_arg(0, int); }", [], 200,
     ["d.c:1: error: the first argument must be a va_list variable: __va_arg"]),
    ("constant division by zero", "int x = 1 / 0;", [], 200,
     ["d.c:1: error: division by zero in a constant expression"]),
    ("long identifier", "int abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKL;", [], 200,
     ["d.c:1: error: identifier longer than 47 characters: abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKL"]),
    ("47-character identifier", "int abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJK;", [], 0, []),
    ("conflicting types", "int f(int);\nchar f(int);", [], 200, ["d.c:2: error: conflicting types for f"]),
    ("a parameter's own const does not count", "int f(const int);\nint f(int x) { return x; }", [], 0, []),
    ("prototypes with different parameters", "int f(int);\nint f(long);\nint g(int, ...);\nint g(int);\n"
     "int h(const char *);\nint h(char *p) { return 0; }", [], 200,
     ["d.c:2: error: conflicting types for f", "d.c:4: error: conflicting types for g",
      "d.c:6: error: conflicting types for h"]),
    ("redefinition", "int f(void) { return 0; }\nint f(void) { return 1; }", [], 200,
     ["d.c:2: error: redefinition of f"]),
    ("static after extern", "int f(void);\nstatic int f(void) { return 0; }", [], 200,
     ["d.c:2: error: static declaration follows a non-static one: f"]),
    ("missing semicolon", "int x", [], 200, ["d.c:1: error: expected ';' after a declaration before end of file"]),
    ("multi-dim array", "int a[2][3];", [], 0, []),
    ("inner dimension omitted", "int a[2][];", [], 200,
     ["d.c:1: error: only an array's first dimension may be omitted"]),
    ("function pointer", "int (*fp)(int);\nint (*tab[2])(void);\ntypedef int (*op)(int, int);", [], 0, []),
    ("calling something that is not a function", "int x;\nint main(void) { return x(1); }", [], 200,
     ["d.c:2: error: a call to something that is not a function"]),
    ("a function pointer with the wrong arguments", "int f(int a);\nint (*fp)(int) = f;\nint main(void) { return fp(1, 2); }",
     [], 200, ["d.c:3: error: wrong number of arguments to a function through a pointer (2 given, 1 expected)"]),
    ("incompatible function pointers", "int f(int a);\nlong (*fp)(int) = f;", [], 0,
     ["d.c:2: warning: incompatible pointer types (initialiser)"]),
    ("a function without a prototype and a pointer with one", "int f();\nint (*fp)(int) = f;", [], 0, []),
    ("local array initialiser", "int main(void) { int a[2] = { 1 }; char s[] = \"x\"; return a[1] + s[1]; }",
     [], 0, []),
    ("line markers", '# 40 "orig.c"\nint main(void) { return y; }', [], 200,
     ["orig.c:40: error: undeclared identifier y"]),
    # warnings: status 0
    ("// comment", "int x; // hi", [], 0, []),
    ("late declaration, strict", "int main(void) {\n int a;\n a = 1;\n int b;\n return a;\n}", ["-ansi"], 0,
     ["d.c:4: warning: declaration after a statement (a C99 feature)"]),
    ("late declaration", "int main(void) {\n int a;\n a = 1;\n int b;\n return a;\n}", [], 0, []),
    ("falls off", "int f(int x) {\n if (x)\n  return 1;\n}", [], 0,
     ["d.c:4: warning: control reaches the end of a function that returns a value"]),
    ("no fall-off after if/else returns", "int f(int x) { if (x) return 1; else return 2; }", [], 0, []),
    ("no fall-off after for(;;)", "int f(void) { for (;;) { } }", [], 0, []),
    ("return; in non-void", "int f(void) { return; }", [], 0,
     ["d.c:1: warning: 'return;' in a function that returns a value"]),
    ("big decimal is long", "unsigned u = 9000000;", [], 0, []),
    ("big hex is quiet", "unsigned u = 0x900000;", [], 0, []),
    ("pointer types", "int *p;\nchar *q;\nint main(void) { p = q; return 0; }", [], 0,
     ["d.c:3: warning: incompatible pointer types (assignment)"]),
    # M8: integer types and qualifiers
    ("constant beyond 32 bits, strict", "long x = 4294967296;", ["-ansi"], 200,
     ["d.c:1: error: integer constant too large (more than 32 bits)"]),
    ("biggest constant", "unsigned long x = 4294967295;", ["-ansi"], 0, []),
    ("long long, strict", "long long x;", ["-ansi"], 200, ["d.c:1: error: 'long long' is not C89"]),
    ("LL suffix, strict", "long x = 1LL;", ["-ansi"], 200, ["d.c:1: error: invalid suffix on integer constant"]),
    # M14 step 7: long long in the default mode
    ("long long", "long long x;\nunsigned long long y = 18446744073709551615ULL;\nsigned long long int z = -1LL;",
     [], 0, []),
    ("constant beyond 64 bits", "long long x = 18446744073709551616;", [], 200,
     ["d.c:1: error: integer constant too large (more than 64 bits)"]),
    ("huge decimal is unsigned", "unsigned long long x = 18446744073709551615;", [], 0,
     ["d.c:1: warning: integer constant is so large that it is unsigned"]),
    ("long long long", "long long long x;", [], 200, ["d.c:1: error: 'long long long' is too long"]),
    ("long long double", "long long double x;", [], 200,
     ["d.c:1: error: 'double' with 'short', 'long long', 'signed' or 'unsigned'"]),
    ("duplicate long long case", "void f(long long x) { switch (x) { case 0x100000000LL: case 4294967296: break; } }",
     [], 200, ["d.c:1: error: duplicate case value"]),
    ("long long cases apart in the high word",
     "void f(long long x) { switch (x) { case 0x100000000LL: case 0x200000000LL: case 0: break; } }", [], 0, []),
    ("long long shift count", "long long f(long long x) { return x << 64; }", [], 0,
     ["d.c:1: warning: shift count outside 0..63"]),
    ("short long", "short long x;", [], 200, ["d.c:1: error: both 'short' and 'long'"]),
    ("short char", "short char x;", [], 200, ["d.c:1: error: 'char' with 'short' or 'long'"]),
    ("signed unsigned", "signed unsigned x;", [], 200, ["d.c:1: error: more than one 'signed' or 'unsigned'"]),
    ("unsigned void", "unsigned void f(void);", [], 200,
     ["d.c:1: error: 'void' with 'short', 'long', 'signed' or 'unsigned'"]),
    ("long struct", "struct s { int a; };\nlong struct s x;", [], 200,
     ["d.c:2: error: more than one type in a declaration"]),
    ("register at file scope", "register int x;", [], 200,
     ["d.c:1: error: 'auto' or 'register' outside a function"]),
    ("register parameter", "int f(register int x) { return x; }", [], 0, []),
    ("static parameter", "int f(static int x) { return x; }", [], 200,
     ["d.c:1: error: storage class on a parameter"]),
    ("assign to const", "const int c = 1;\nint main(void) { c = 2; return 0; }", [], 200,
     ["d.c:2: error: assignment to a const object"]),
    ("compound-assign to const", "int main(void) { const long c = 1; c += 2; return 0; }", [], 200,
     ["d.c:1: error: assignment to a const object"]),
    ("increment const", "const char *p;\nint main(void) { (*p)++; return 0; }", [], 200,
     ["d.c:2: error: '++' or '--' of a const object"]),
    ("const member", "struct s { int a; };\nconst struct s v;\nint main(void) { v.a = 1; return 0; }", [], 200,
     ["d.c:3: error: assignment to a const object"]),
    ("const pointer", "char c;\nchar *const p = &c;\nint main(void) { p = 0; *p = 1; return 0; }", [], 200,
     ["d.c:3: error: assignment to a const object"]),
    ("const function pointer", "int (* const fp)(void) = 0;\nvoid h(void) { fp = 0; }", [], 200,
     ["d.c:2: error: assignment to a const object"]),
    ("discarded const", "const char *p;\nchar *q;\nint main(void) { q = p; return 0; }", [], 0,
     ["d.c:3: warning: assignment discards a const or volatile qualifier"]),
    ("?: of const and plain pointers", "const char *p;\nchar *q;\nint c;\nint main(void) { p = c ? p : q; return 0; }", [], 0, []),
    ("?: result keeps const", "const char *p;\nchar *q;\nint c;\nint main(void) { q = c ? q : p; return 0; }", [], 0,
     ["d.c:4: warning: assignment discards a const or volatile qualifier"]),
    ("added const is quiet", "const char *p;\nchar *q;\nint main(void) { p = q; return 0; }", [], 0, []),
    ("?: with (void *)0 has the pointer's type", "struct s { int m; };\n"
     "int g(struct s *sp, int c) { return (c ? sp : (void *)0)->m + (c ? (void *)0 : sp)->m; }", [], 0, []),
    ("?: with (void *)0, assigned to another pointer type", "char *p;\nint c;\nint *q;\n"
     "int main(void) { q = c ? p : (void *)0; return 0; }", [], 0,
     ["d.c:4: warning: incompatible pointer types (assignment)"]),
    ("long shift", "long f(long x) { return x << 32; }", [], 0, ["d.c:1: warning: shift count outside 0..31"]),
    ("long shift by 31 is quiet", "long f(long x) { return x << 31; }", [], 0, []),
    ("duplicate long case", "int f(long v) { switch (v) { case 0x1000000L: case 16777216: return 0; } return 1; }",
     [], 200, ["d.c:1: error: duplicate case value"]),
    ("case differing above bit 23", "int f(long v) { switch (v) { case 1: case 0x1000001L: return 0; } return 1; }",
     [], 0, []),
    ("long division by zero", "long x = 1L / 0L;", [], 200,
     ["d.c:1: error: division by zero in a constant expression"]),
    ("narrow /= long on a complex lvalue", "int *p;\nint main(void) { *p++ /= 2L; return 0; }", [], 0, []),
    # M9: strict mode, declarations without prototypes, implicit declarations, #asm
    ("implicit declaration in strict mode", "int main(void) { return f(); }", ["-ansi"], 0,
     ["d.c:1: warning: implicit declaration of function f"]),
    ("asm is an identifier in strict mode", "int asm;\nvoid f(void) { __asm(\"nop\"); }", ["-ansi"], 0, []),
    ("asm is a keyword by default", "int asm;", [], 200, ["d.c:1: error: declaration without a name"]),
    ("// is not a comment in strict mode", "int x; // hi", ["-ansi"], 200,
     ["d.c:1: error: declaration without a name"]),
    ("no prototype", "int f();\nint main(void) { return f(1, 2L, \"x\"); }", [], 0, []),
    ("a prototype after none", "int f();\nint f(int a, long b);\nint main(void) { return f(1, 2L); }", [], 0, []),
    ("no prototype, then one it conflicts with", "int f();\nint f(char c);", [], 200,
     ["d.c:2: error: conflicting types for f"]),
    ("#asm in a function", "int g;\nvoid f(void)\n{\n#asm\n    ld hl,0\n#endasm\n    g++;\n}", [], 0, []),
    ("#asm at file scope", "#asm\n nop\n#endasm\nint x;", [], 200,
     ["d.c:1: error: assembly outside a function: put it in a .s file (driver.md 2)"]),
    ("asm at file scope", "asm(\"nop\");\nint x;", [], 200,
     ["d.c:1: error: assembly outside a function: put it in a .s file (driver.md 2)"]),
    ("#asm without #endasm", "void f(void) {\n#asm\n nop\n", [], 200, ["d.c:2: error: #asm without #endasm"]),
    # M10: tentative definitions, K&R definitions, implicit int
    ("tentative definitions", "int x;\nint x = 5;\nint x;\nstruct s v;\nstruct s { int a; };", [], 0, []),
    ("two initialised definitions", "int x = 1;\nint x = 2;", [], 200, ["d.c:2: error: redefinition of x"]),
    ("an array of unknown size", "int a[];", [], 0, ["d.c:1: warning: an array assumed to have one element: a"]),
    ("K&R definition", "int kr(a, b) char a; long b; { return a; }", [], 0,
     ["d.c:1: warning: an old-style (K&R) function definition; a prototype-style one is checked at every call"]),
    ("K&R definition in strict mode", "int kr(a, b) char a; long b; { return a; }", ["-ansi"], 0, []),
    ("K&R parameter without a type", "int kr(a, b) char a; { return a + b; }", [], 0,
     ["d.c:1: warning: parameter defaults to int (implicit int): b"]),
    ("K&R parameter without a type, strict", "int kr(a, b) char a; { return a + b; }", ["-ansi"], 0,
     ["d.c:1: warning: parameter defaults to int (implicit int): b"]),
    ("K&R declaration of a non-parameter", "int f(a) int a; int b; { return a; }", [], 200,
     ["d.c:1: error: a declaration of something that is not a parameter: b"]),
    ("identifier list outside a definition", "int f(a, b);", [], 200,
     ["d.c:1: error: a parameter list without types belongs only in a function definition"]),
    ("implicit int in strict mode", "static x;\nf() { return x; }", ["-ansi"], 0,
     ["d.c:1: warning: type defaults to int (implicit int)", "d.c:2: warning: type defaults to int (implicit int)"]),
    ("implicit int in the default mode", "static x;\nf() { return x; }\nconst y = 1;", [], 0,
     ["d.c:1: warning: type defaults to int (implicit int)", "d.c:2: warning: type defaults to int (implicit int)",
      "d.c:3: warning: type defaults to int (implicit int)"]),
    ("implicit int with -Werror", "static x;", ["-Werror"], 200, ["d.c:1: error: type defaults to int (implicit int)"]),
    ("int main() is not old-style", "int main() { return 0; }", [], 0, []),
    ("a global label in inline assembly",
     "void f(void)\n{\n    asm(\"here: nop\\n@ok: nop\\n  there:\");\n    asm(\"ld a,(ix+6)\");\n}", [], 0,
     ["d.c:3: warning: a global label in inline assembly, which must be unique in the program (abi.md 10): here",
      "d.c:3: warning: a global label in inline assembly, which must be unique in the program (abi.md 10): there"]),
    # M14: C89's scopes
    ("a parameter in scope in the later ones", "int f(unsigned j, char d[sizeof(j)], int k[sizeof d]);", ["-ansi"],
     0, []),
    ("a parameter is not a constant", "int f(int n, char d[n]);", ["-ansi"], 200,
     ["d.c:1: error: not an integer constant expression"]),
    ("struct tag; in a block hides the outer tag",
     "struct t { int x; };\nint f(void)\n{\n    struct t;\n    struct u { struct t *p; } a;\n"
     "    struct t { char c; } b;\n    a.p = &b;\n    return a.p->c;\n}", ["-ansi"], 0, []),
    ("an initialised extern at file scope is a definition", "extern int x = 5;\nint f(void) { return x; }", ["-ansi"],
     0, []),
    ("an initialised extern in a block", "int f(void) { extern int y = 1; return y; }", ["-ansi"], 200,
     ["d.c:1: error: a block-scope extern takes no initialiser"]),
    ("GNU spellings", "static __inline__ int f(int *__restrict p) __attribute__((noinline));\n"
     "__extension__ typedef __const__ int ci;\nint g(void) { return (int)__builtin_strlen(\"ab\") + "
     "__builtin_expect(1, 1) + __builtin_constant_p(3) + (int)__builtin_offsetof(struct { int a; int b; }, b); }",
     ["-ansi"], 0, []),
    ("-Werror", "int x = 'ab';", ["-Werror"], 200, ["d.c:1: error: a multi-character character constant"]),
    # the default mode's C99 (t_c99.c runs them)
    ("a declaration in for, strict", "int f(void) {\n int t = 0;\n for (int i = 0; i < 3; i++)\n  t += i;\n"
     " return t;\n}", ["-ansi"], 200, ["d.c:3: error: expected an expression"]),
    ("a for's declaration ends with the loop", "int f(void) {\n for (int i = 0; i < 3; i++)\n  ;\n return i;\n}",
     [], 200, ["d.c:4: error: undeclared identifier i"]),
    ("inline, strict", "inline int f(void) { return 1; }", ["-ansi"], 200,
     ["d.c:1: error: expected ';' after a declaration"]),
    ("restrict, strict", "void f(int *restrict p);", ["-ansi"], 200, ["d.c:1: error: expected ')' after parameters"]),
    ("__func__ outside a function", "char *p = __func__;", [], 200, ["d.c:1: error: undeclared identifier __func__"]),
    ("_Bool, strict", "_Bool b;", ["-ansi"], 200, ["d.c:1: error: expected ';' after a declaration"]),
    ("unsigned _Bool", "unsigned _Bool b;", [], 200,
     ["d.c:1: error: '_Bool' with 'short', 'long', 'signed' or 'unsigned'"]),
    ("a _Bool bit-field of 2 bits", "struct s { int n; _Bool f : 2; };", [], 200,
     ["d.c:1: error: a bit-field wider than its type (or than 24 bits) or of negative width"]),
    ("a pointer to _Bool needs no cast", "int f(void) { int x; _Bool b = &x; return b; }", [], 0, []),
    ("a struct to _Bool", "struct s { int a; } v;\n_Bool f(void) { return v; }", [], 200,
     ["d.c:2: error: incompatible struct value in return"]),
    ("a flexible array member", "struct s { int n; int d[]; };\nstruct s a = { 1 };", [], 0, []),
    ("a flexible array member, strict", "struct s { int n; int d[]; };", ["-ansi"], 200,
     ["d.c:1: error: member of void or incomplete type: d"]),
    ("a flexible array member not last", "struct s { int n; int d[]; int m; };", [], 200,
     ["d.c:1: error: a flexible array member must be the last member"]),
    ("a flexible array member in a union", "union u { int n; int d[]; };", [], 200,
     ["d.c:1: error: a flexible array member in a union"]),
    ("a flexible array member alone", "struct s { int d[]; };", [], 200,
     ["d.c:1: error: a flexible array member needs a member before it"]),
    ("a struct with a flexible array member as a member", "struct s { int n; int d[]; };\n"
     "struct t { struct s a; int k; };", [], 200,
     ["d.c:2: error: a struct with a flexible array member cannot be a member"]),
    ("an array of a struct with a flexible array member", "struct s { int n; int d[]; };\nstruct s a[2];", [], 200,
     ["d.c:2: error: array of a struct with a flexible array member"]),
    ("a flexible array member initialised", "struct s { int n; int d[]; };\nstruct s a = { 1, { 2, 3 } };", [], 200,
     ["d.c:2: error: a flexible array member cannot be initialised"]),
    ("a hexadecimal floating constant without p", "double d = 0x1.8;", [], 200,
     ["d.c:1: error: a hexadecimal floating constant needs an exponent (p)"]),
    ("a hexadecimal floating constant without digits", "double d = 0x.p1;", [], 200,
     ["d.c:1: error: a hexadecimal floating constant with no digits"]),
    ("a hexadecimal floating constant, strict", "double d = 0x1p3;", ["-ansi"], 200,
     ["d.c:1: error: invalid suffix on integer constant"]),
    ("a hexadecimal floating constant beyond double", "double d = 0x1p1024;", [], 0,
     ["d.c:1: warning: floating constant out of range for double (it is infinity)"]),
    ("a hexadecimal floating constant beyond float", "float d = 0x1p128f;", [], 0,
     ["d.c:1: warning: floating constant out of range for float (it is infinity)"]),
    # N5: designated initialisers and compound literals (t_c99.c runs them)
    ("a designator, strict", "int a[3] = { [1] = 2 };", ["-ansi"], 200, ["d.c:1: error: expected an expression"]),
    ("a member designator, strict", "struct s { int x; } v = { .x = 1 };", ["-ansi"], 200,
     ["d.c:1: error: expected an expression"]),
    ("an array designator beyond the bounds", "int a[3] = { [3] = 2 };", [], 200,
     ["d.c:1: error: an array designator beyond the array's bounds"]),
    ("a negative array designator", "int a[3] = { [-1] = 2 };", [], 200,
     ["d.c:1: error: an array designator beyond the array's bounds"]),
    ("a designator naming no member", "struct s { int x; } v = { .y = 1 };", [], 200,
     ["d.c:1: error: no member named y"]),
    ("a member designator for an array", "int a[3] = { .x = 1 };", [], 200,
     ["d.c:1: error: a member designator for an array"]),
    ("an array designator for a struct", "struct s { int x; } v = { [0] = 1 };", [], 200,
     ["d.c:1: error: an array designator for a struct"]),
    ("a designator into a scalar", "struct s { int x; } v = { .x.y = 1 };", [], 200,
     ["d.c:1: error: a designator for a part of something that is not an array or a struct"]),
    ("a designator without =", "int a[3] = { [1] 2 };", [], 200, ["d.c:1: error: expected '=' after a designator"]),
    ("an array designator that is not a constant", "int n; int a[3] = { [n] = 2 };", [], 200,
     ["d.c:1: error: not an integer constant expression"]),
    ("too many after a designator", "int a[3] = { [2] = 1, 2 };", [], 200, ["d.c:1: error: too many initialisers"]),
    ("a union's members by designators", "union u { int i; char c; } v = { .c = 1, .i = 2 };", [], 0, []),
    ("a designator going back too far", "int big[1000] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, [3] = 9 };", [], 200,
     ["d.c:1: error: a designator goes back before what agonc has already written of this initialiser "
      "(it holds 512 items)"]),
    ("a long initialiser in order", "int big[1000] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672, 673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688, 689, 690, 691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701, 702, 703, 704, 705, 706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716, 717, 718, 719, 720, 721, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731, 732, 733, 734, 735, 736, 737, 738, 739, 740, 741, 742, 743, 744, 745, 746, 747, 748, 749, 750, 751, 752, 753, 754, 755, 756, 757, 758, 759, 760, 761, 762, 763, 764, 765, 766, 767, 768, 769, 770, 771, 772, 773, 774, 775, 776, 777, 778, 779, 780, 781, 782, 783, 784, 785, 786, 787, 788, 789, 790, 791, 792, 793, 794, 795, 796, 797, 798, 799, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809, 810, 811, 812, 813, 814, 815, 816, 817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832, 833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848, 849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864, 865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880, 881, 882, 883, 884, 885, 886, 887, 888, 889, 890, 891, 892, 893, 894, 895, 896, 897, 898, 899 };", [], 0, []),
    ("a compound literal, strict", "int f(void) { return (int){ 1 }; }", ["-ansi"], 200,
     ["d.c:1: error: expected an expression"]),
    ("a compound literal of void", "int f(void) { (void){ 0 }; return 0; }", [], 200,
     ["d.c:1: error: a compound literal of a function, void or an incomplete type"]),
    ("a compound literal of an incomplete struct", "struct t; int f(void) { (struct t){ 0 }; return 0; }", [], 200,
     ["d.c:1: error: a compound literal of a function, void or an incomplete type"]),
    ("a file-scope compound literal is constant", "int g; int *p = (int[]){ g };", [], 200,
     ["d.c:1: error: initialiser is not a constant"]),
    ("a compound literal in a function is not a static's constant",
     "int f(void) { static int *p = (int[]){ 1 }; return *p; }", [], 200,
     ["d.c:1: error: initialiser is not a constant"]),
    ("sizeof a compound literal", "int n = sizeof (char[]){ 1, 2, 3 };", [], 0, []),
    ("-w", "int x = 'ab';", ["-w"], 0, []),
]


def diagnostics():
    problems = []
    for name, src, flags, status, want in DIAG:
        path = os.path.join(REPO, o("d.c"))
        open(path, "w", newline="\n").write(src + "\n")
        out = os.path.join(REPO, o("d.ir"))
        if os.path.exists(out):
            os.remove(out)
        r = sh([os.path.join(HOST, "cc1.exe"), o("d.c"), o("d.ir")] + flags)
        if status != 0 and os.path.exists(out):
            problems.append(f"{name}: an output file was left behind after an error")
        lines = [l.replace(OUT + os.sep, "") for l in r.stderr.splitlines()]
        if r.returncode != status:
            problems.append(f"{name}: status {r.returncode}, expected {status}; stderr {lines}")
        for w in want:
            if w not in lines:
                problems.append(f"{name}: missing {w!r}; got {lines}")
        if not want and lines:
            problems.append(f"{name}: expected no diagnostics; got {lines}")
    return problems


# ---- I ----------------------------------------------------------------------------------

IR_CASES = [
    # (name, source, lines the IR must have, lines it must not)
    ("sizeof of a call returning a struct",
     "struct s { int a[5]; };\nstruct s f(void);\nint g(void) { return sizeof(f()); }", ["FRAME 0"], ["LOC "]),
    ("sizeof of a double and a long long",
     "int g(void) { return sizeof(1.0) + sizeof(2LL); }", [], ["R __fp_print", "R __ll_print"]),
    ("a double value does bring in printf's conversions",
     "double d;\nint g(void) { return d > 0; }", ["R __fp_print"], []),
    ("inline's linkage: static unless declared, or defined, extern",
     "inline int sq(int x) { return x * x; }\nint ext(int x);\ninline int ext(int x) { return x; }\n"
     "static inline int s2(int x) { return x; }\nextern inline int e2(int x) { return x; }\n"
     "int use(void) { return sq(2) + ext(1) + s2(3) + e2(4); }",
     ["F .sq s 1 @ -", "F ext g 1 @ -", "F .s2 s 1 @ -", "F e2 g 1 @ -"], []),
]


def ir_checks():
    problems = []
    for name, src, want, unwanted in IR_CASES:
        open(os.path.join(REPO, o("i.c")), "w", newline="\n").write(src + "\n")
        r = sh([os.path.join(HOST, "cc1.exe"), o("i.c"), o("i.ir")])
        if r.returncode:
            problems.append(f"{name}: cc1 failed: {r.stderr.strip()[:200]}")
            continue
        lines = open(os.path.join(REPO, o("i.ir"))).read().splitlines()
        for w in want:
            if w not in lines:
                problems.append(f"{name}: no {w!r} in the IR")
        for u in unwanted:
            if any(l.startswith(u) for l in lines):
                problems.append(f"{name}: {u!r} in the IR")
    return problems


# ---- T4 ---------------------------------------------------------------------------------

def t4_file(stem, bad_from, bad_to, min_index, min_checks, cc1_flags=()):
    """Run one execution test, then its two guards: a wrong expectation must
    be reported by its index (at least min_index), and at least min_checks
    checks must run."""
    src = os.path.join("tests", "cc1", stem + ".c")
    p = compile_c(src, stem, cc1_flags)
    if p:
        return [p]
    problems = []
    rc = run_bin(stem)
    if rc != 0:
        problems.append(f"{stem}: check {rc} failed (the first failing check's index)")
    text = open(os.path.join(REPO, src)).read()
    assert bad_from in text
    bad = text.replace(bad_from, bad_to)
    open(os.path.join(REPO, o("tbad.c")), "w", newline="\n").write(bad)
    p = compile_c(o("tbad.c"), "tbad", cc1_flags)
    rc = run_bin("tbad") if not p else p
    if not isinstance(rc, int) or rc < min_index:
        problems.append(f"{stem} guard: a wrong expectation gave {rc}, expected its index (>= {min_index})")
    cnt = text.replace("    if (fails == 0)\n        agon_emu_exit(0);\n    else\n        agon_emu_exit(first < 250 ? first : 250);",
                       "    agon_emu_exit(count - 100);")
    assert cnt != text
    open(os.path.join(REPO, o("tcnt.c")), "w", newline="\n").write(cnt)
    p = compile_c(o("tcnt.c"), "tcnt", cc1_flags)
    rc = run_bin("tcnt") if not p else p
    if not isinstance(rc, int) or rc + 100 < min_checks:
        problems.append(f"{stem} guard: only {rc} + 100 checks ran, expected {min_checks}")
    return problems


def t4():
    return t4_file("t_exec", "check(fib(15), 610)", "check(fib(15), 611)", 100, 150)


def t4_m4():
    return t4_file("t_m4", "check(goto_loop(10), 55)", "check(goto_loop(10), 56)", 109, 124)


STEM_FLAGS = {"t_m10": " -ansi"}      # t_m10 uses implicit int


def t4_m10():
    return t4_file("t_m10", "check(shadow(1), 437875);", "check(shadow(1), 437876);", 1, 88, ["-ansi"])


def t4_m11():
    return t4_file("t_m11", "check(gp[2], 'z');", "check(gp[2], 'y');", 15, 37)


sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from common import HOSTCC  # noqa: E402


def t4_m13():
    # the PC's own float and double first: every expected bit pattern is one it computes
    stub = os.path.join(REPO, o("hostexit.c"))
    open(stub, "w", newline="\n").write("#include <stdlib.h>\nvoid agon_emu_exit(int s) { exit(s); }\n")
    exe = os.path.join(REPO, o("t_m13_host.exe"))
    r = sh([HOSTCC, "-std=c89", "-O2", "-ffp-contract=off", "-w", "-o", exe,
            os.path.join("tests", "cc1", "t_m13.c"), stub])
    if r.returncode:
        return ["the PC's build of t_m13.c failed: " + r.stderr.strip()[:300]]
    rc = subprocess.run([exe]).returncode
    if rc != 0:
        return [f"t_m13 built by the PC: check {rc} failed"]
    return t4_file("t_m13", "checkf(wf, 0xBE639868UL);", "checkf(wf, 0xBE639869UL);", 140, 145)


def t4_m14():
    return t4_file("t_m14", "check(o.w, 9);", "check(o.w, 8);", 21, 75)


def t4_ll():
    stub = os.path.join(REPO, o("hostexit.c"))
    open(stub, "w", newline="\n").write("#include <stdlib.h>\nvoid agon_emu_exit(int s) { exit(s); }\n")
    exe = os.path.join(REPO, o("t_ll_host.exe"))
    r = sh([HOSTCC, "-std=c99", "-O2", "-fwrapv", "-w", "-o", exe, os.path.join("tests", "cc1", "t_ll.c"), stub])
    if r.returncode:
        return ["the PC's build of t_ll.c failed: " + r.stderr.strip()[:300]]
    rc = subprocess.run([exe]).returncode
    if rc != 0:
        return [f"t_ll built by the PC: check {rc} failed"]
    return t4_file("t_ll", "checkq(u % 10, 0, 5);", "checkq(u % 10, 0, 6);", 15, 165)


def t4_c99():
    stub = os.path.join(REPO, o("hostexit.c"))
    open(stub, "w", newline="\n").write("#include <stdlib.h>\nvoid agon_emu_exit(int s) { exit(s); }\n")
    exe = os.path.join(REPO, o("t_c99_host.exe"))
    r = sh([HOSTCC, "-std=c99", "-O2", "-w", "-o", exe, os.path.join("tests", "cc1", "t_c99.c"), stub])
    if r.returncode:
        return ["the PC's build of t_c99.c failed: " + r.stderr.strip()[:300]]
    rc = subprocess.run([exe]).returncode
    if rc != 0:
        return [f"t_c99 built by the PC: check {rc} failed"]
    return t4_file("t_c99", "check(f.whole, 1);", "check(f.whole, 16);", 70, 145)


def t4_m9():
    return t4_file("t_m9", "check(ptsum(mkpt(3, 4)), 7);", "check(ptsum(mkpt(3, 4)), 8);", 1, 102)


def t4_m8():
    return t4_file("t_m8", "checkl(after_long(0x1000000L, -1), 0xFFFFFFL)",
                   "checkl(after_long(0x1000000L, -1), 0x1FFFFFFL)", 218, 218)


# ---- S1 ---------------------------------------------------------------------------------

def s1():
    bins = []
    for b in ("cc1", "cc2", "ld"):
        path = os.path.join("build", "stage1", b + ".bin")
        if not os.path.exists(os.path.join(REPO, path)):
            return [f"{path} missing (make stage1)"]
        bins += ["--bin", path]
    files = []
    stems = ("hello", "t_exec", "t_m4", "t_m8", "t_m9", "t_m10", "t_m11", "t_m13", "t_m14", "t_ll", "t_c99")
    for f in RUNTIME + [os.path.join("tests", "cc1", s + ".c") for s in stems]:
        files += ["--file", f]
    libs = "crt0.s rt.s libc.s libm.s"
    cmds = []
    for stem in stems:
        cmds += ["--cmd", f"cc1 {stem}.c /{stem}.ir -u {stem}.c" + STEM_FLAGS.get(stem, ""),
                 "--cmd", f"cc2 /{stem}.ir /{stem}.s" + ("" if os.environ.get("AGONC_TEST_O0") else " -O"),
                 "--cmd", f"ld -o /{stem}.asm {libs} /{stem}.s",
                 "--cmd", f"ez80asm /{stem}.asm /bin/d{stem}.bin -m"]
    # agon_emu_exit ends the emulator, so one program per session: t_exec here
    # (exit 0 only if every check passes), hello in a session of its own below
    cmds += ["--cmd", "dt_exec"]
    r = sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--with-ez80asm"] + bins + files +
           cmds + ["--sdcard-name", "cc1s1", "--timeout", "900"])
    problems = [] if r.returncode == 0 else [f"device-built t_exec gave {r.returncode}, expected 0"]
    card = os.path.join(REPO, "emulator_sdcard", "cc1s1")
    for stem in stems:
        if compile_c(os.path.join("tests", "cc1", stem + ".c"), "h_" + stem, STEM_FLAGS.get(stem, "").split()):
            problems.append(f"host build of {stem} failed")
            continue
        for dev, host in ((stem + ".ir", "h_" + stem + ".ir"), (stem + ".s", "h_" + stem + ".s"),
                          (stem + ".asm", "h_" + stem + ".asm"), (os.path.join("bin", "d" + stem + ".bin"), "h_" + stem + ".bin")):
            d = os.path.join(card, dev)
            h = os.path.join(REPO, o(host))
            if not os.path.exists(d):
                problems.append(f"the device produced no {dev}")
            elif open(d, "rb").read() != open(h, "rb").read():
                problems.append(f"device {dev} differs from the host's")
    # the device-built hello, t_m4 and t_m8, each run in a session of its own
    for stem, want in (("hello", 42), ("t_m4", 0), ("t_m8", 0), ("t_m9", 0), ("t_m10", 0), ("t_m11", 0),
                       ("t_m13", 0), ("t_m14", 0), ("t_ll", 0), ("t_c99", 0)):
        dev_bin = os.path.join(card, "bin", "d" + stem + ".bin")
        if os.path.exists(dev_bin):
            keep = os.path.join(REPO, o("d" + stem + ".bin"))
            open(keep, "wb").write(open(dev_bin, "rb").read())
            rc = sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--bin", o("d" + stem + ".bin"),
                     "--cmd", "d" + stem, "--sdcard-name", "cc1s1b", "--timeout", "60"]).returncode
            if rc != want:
                problems.append(f"device-built {stem} gave {rc}, expected {want}")
    return problems


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    if not os.path.exists(os.path.join(REPO, "build", "agon", "lib", "agonc", "libc.s")):
        print("build/agon/lib/agonc/libc.s is missing: run make cross first")
        return 1
    cases = [("T1 golden hello.ir", lambda: t1("--update" in sys.argv)),
             (f"D  diagnostics ({len(DIAG)} cases)", diagnostics),
             (f"I  IR shapes ({len(IR_CASES)} cases)", ir_checks)]
    if "--no-emu" not in sys.argv:
        cases.append(("T4 t_exec.c on the emulator + guards", t4))
        cases.append(("T4 t_m4.c (struct, enum, typedef, switch, goto) on the emulator + guards", t4_m4))
        cases.append(("T4 t_m8.c (short, long, qualifiers, conversions) on the emulator + guards", t4_m8))
        cases.append(("T4 t_m9.c (structs by value, ...) on the emulator + guards", t4_m9))
        cases.append(("T4 t_m10.c (block scope, ...) on the emulator + guards", t4_m10))
        cases.append(("T4 t_m11.c (wide characters and strings) on the emulator + guards", t4_m11))
        cases.append(("T4 t_m13.c (float and double, bit for bit) on the PC, then the emulator + guards", t4_m13))
        cases.append(("T4 t_m14.c (scopes, GCC's spellings, the default mode's C99) on the emulator + guards", t4_m14))
        cases.append(("T4 t_ll.c (long long, byte for byte) on the PC, then the emulator + guards", t4_ll))
        cases.append(("T4 t_c99.c (the default mode's C99) on the PC, then the emulator + guards", t4_c99))
        if "--no-stage1" not in sys.argv:
            cases.append(("S1 AgDev cc1+cc2+ld and device ez80asm build hello.c and t_exec, t_m4, t_m8, t_m9, t_m10, t_m11, t_m13, t_m14, t_ll,"
                          " t_c99 on the emulator", s1))
    failed = 0
    for name, fn in cases:
        problems = fn()
        print(("PASS  " if not problems else "FAIL  ") + name)
        for p in problems:
            print("      " + p)
        failed += bool(problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
