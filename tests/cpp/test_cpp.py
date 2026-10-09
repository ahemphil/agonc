"""Tests for cpp (with cc1, cc2, ld and the library behind it).

    test_cpp.py [--update] [--no-emu] [--no-stage1]

T1  t_cpp.c preprocessed against a golden .i (markers, spacing, blank-line
    catch-up, every directive); and t_c89ex.c, C89 3.8.3.5's two examples,
    against the text the standard gives (checked by hand into its golden).
D   diagnostics: small sources, each with the exact message cpp must print
    (file:line: error|warning: text) and its exit status; after any error
    the output file must not exist.
T4  t_cpp.c (the preprocessor's features, checked at run time) through
    cpp + cc1 + cc2 + ld on the emulator: exit 0. Guards: a wrong
    expectation is reported by its index; the number of checks is verified.
S1  on the emulator: the AgDev-built cpp preprocesses t_cpp.c twice, once
    with -I lib/agonc and once relying on the compiled-in /lib/agonc search;
    the first equals the host's output byte for byte, the second differs
    only in naming /lib/agonc/string.h. The AgDev-built cc1, cc2 and ld and the device's
    ez80asm then build it, and it passes.
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join("build", "test", "cpp")
PY = sys.executable
HOST = os.path.join("build", "host")
RUNTIME = [os.path.join("lib", "rt", "crt0.s"), os.path.join("lib", "rt", "rt.s")]
LIBC = os.path.join("build", "agon", "lib", "agonc", "libc.s")
DEFS = ["-D", "FROM_COMMAND_LINE=42", "-DONE_BY_DEFAULT", "-D", "UNDONE_ON_COMMAND_LINE", "-UUNDONE_ON_COMMAND_LINE"]


def sh(cmd, cwd=REPO):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def o(name):
    return os.path.join(OUT, name)


def build(src, stem, cpp_flags):
    """cpp + cc1 + cc2 + ld + ez80asm; returns a problem string or None."""
    steps = [
        [os.path.join(HOST, "cpp.exe"), src, o(stem + ".i"), "-I", os.path.join("lib", "libc")] + cpp_flags,
        [os.path.join(HOST, "cc1.exe"), o(stem + ".i"), o(stem + ".ir"), "-u", "t_cpp.c"],
        [os.path.join(HOST, "cc2.exe"), o(stem + ".ir"), o(stem + ".s")] + ([] if os.environ.get("AGONC_TEST_O0") else ["-O"]),
        [os.path.join(HOST, "ld.exe"), "-o", o(stem + ".asm")] + RUNTIME + [LIBC, o(stem + ".s")],
        [os.path.join("third_party", "bin", "ez80asm.exe"), o(stem + ".asm"), o(stem + ".bin")],
    ]
    for cmd in steps:
        r = sh(cmd)
        if r.returncode:
            return f"{os.path.basename(cmd[0])} failed: {(r.stderr or r.stdout).strip()[:400]}"
    return None


def run_bin(stem):
    return sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--bin", o(stem + ".bin"),
               "--cmd", stem, "--sdcard-name", "cpp", "--timeout", "60"]).returncode


# ---- T1 -----------------------------------------------------------------------------

def t1(update):
    r = sh([os.path.join(HOST, "cpp.exe"), "tests/cpp/t_cpp.c", o("golden.i"), "-I", "lib/libc"] + DEFS)
    if r.returncode:
        return [f"cpp failed: {r.stderr.strip()}"]
    got = open(os.path.join(REPO, o("golden.i"))).read()
    golden = os.path.join(HERE, "t_cpp.i.golden")
    if update:
        open(golden, "w", newline="\n").write(got)
    problems = [] if open(golden).read() == got else ["t_cpp.i differs from t_cpp.i.golden"]
    r = sh([os.path.join(HOST, "cpp.exe"), "tests/cpp/t_c89ex.c", o("c89ex.i")])
    if r.returncode:
        return problems + [f"cpp failed on t_c89ex.c: {r.stderr.strip()}"]
    got = open(os.path.join(REPO, o("c89ex.i"))).read()
    golden = os.path.join(HERE, "t_c89ex.i.golden")
    if update:
        open(golden, "w", newline="\n").write(got)
    if open(golden).read() != got:
        problems.append("t_c89ex.i differs from t_c89ex.i.golden")
    return problems


# ---- D: diagnostics ----------------------------------------------------------------------
# (name, source, extra files {name: text}, flags, expected status, expected lines)

DIAG = [
    ("wrong number of arguments", "#define F(a, b) a\nint x = F(1);", {}, [], 200,
     ["d.c:2: error: F takes 2 arguments, not 1"]),
    ("arguments to a macro without parameters", "#define F() 1\nint x = F(2);", {}, [], 200,
     ["d.c:2: error: F takes 0 arguments, not 1"]),
    ("unterminated call", "#define F(a) a\nint x = F(1,", {}, [], 200,
     ["d.c:2: error: unterminated call of macro F"]),
    ("a call cut off by a directive", "#define F(a) a\nint x = F(1,\n#define G 2\n2);", {}, [], 200,
     ["d.c:2: error: unterminated call of macro F"]),
    ("# without a parameter", "#define F(a) #b", {}, [], 200, ["d.c:1: error: '#' must be followed by a macro parameter"]),
    ("## at the start", "#define F(a) ## a", {}, [], 200, ["d.c:1: error: '##' at the start of a macro's replacement"]),
    ("## at the end", "#define F(a) a ##", {}, [], 200, ["d.c:1: error: '##' at the end of a macro's replacement"]),
    ("duplicate parameter", "#define F(a, a) a", {}, [], 200, ["d.c:1: error: duplicate macro parameter a"]),
    ("missing ) in parameters", "#define F(a b", {}, [], 200, ["d.c:1: error: missing ')' in a macro's parameters"]),
    ("a parameter that is not a name", "#define F(1) 1", {}, [], 200, ["d.c:1: error: #define needs a parameter name"]),
    ("variadic macro in strict mode", "#define F(...) 1", {}, ["-ansi"], 200,
     ["d.c:1: error: '...' in a macro's parameters (a C99 feature)"]),
    ("variadic macro", "#define F(a, ...) a\n#if F(3, 4, 5) != 3\n#error no\n#endif", {}, [], 0, []),
    ("'...' not last", "#define F(..., a) a", {}, [], 200, ["d.c:1: error: '...' must be a macro's last parameter"]),
    ("_Pragma", "_Pragma(\"once\") int x;", {}, [], 0, ["d.c:1: warning: _Pragma is ignored"]),
    ("_Pragma from a macro", "#define P(x) _Pragma(#x)\nP(pack) int y;", {}, [], 0, ["d.c:2: warning: _Pragma is ignored"]),
    ("_Pragma weak", "_Pragma(\"weak f\")", {}, [], 0,
     ["d.c:1: warning: _Pragma(\"weak ...\") is ignored: use the #pragma weak directive"]),
    ("_Pragma without a string", "_Pragma(x)", {}, [], 200, ["d.c:1: error: _Pragma needs a string literal"]),
    ("variadic macro with too few arguments", "#define F(a, b, ...) a\nint x = F(1);", {}, [], 200,
     ["d.c:2: error: F takes 3 arguments, not 1"]),
    ("different parameters", "#define F(a) a\n#define F(b) b", {}, [], 200, ["d.c:2: error: macro redefined differently: F"]),
    ("a function-like macro in #if", "#define F(a) (a * 2)\n#if F(3) != 6\n#error no\n#endif", {}, [], 0, []),
    ("#include from a macro", "#define H \"h.h\"\n#include H\nint x = HV;", {"h.h": "#define HV 1"}, [], 0, []),
    ("different redefinition", "#define X 1\n#define X 2", {}, [], 200,
     ["d.c:2: error: macro redefined differently: X"]),
    ("identical redefinition", "#define X  1 +  2\n#define X 1 + 2 /* same */", {}, [], 0, []),
    ("defined as a macro name", "#define defined 1", {}, [], 200, ["d.c:1: error: 'defined' cannot be a macro name"]),
    ("#define without a name", "#define", {}, [], 200, ["d.c:1: error: #define needs a macro name"]),
    ("#define without a space", "#define X\"a\"", {}, [], 200,
     ["d.c:1: error: #define needs white space after the macro name"]),
    ("#ifdef without a name", "#ifdef\n#endif", {}, [], 200, ["d.c:1: error: #ifdef needs a macro name"]),
    ("missing include", "\n#include \"nope.h\"", {}, [], 200, ["d.c:2: error: cannot find include file nope.h"]),
    ("malformed include", "#include nope.h", {}, [], 200, ["d.c:1: error: #include needs \"file\" or <file>"]),
    ("error inside an include", "int a;\n#include \"h.h\"\nint b;", {"h.h": "int c;\n#error inside"}, [], 200,
     ["h.h:2: error: #error inside"]),
    ("include too deep", "#include \"d.c\"", {}, [], 200, ["d.c:1: error: #include nested too deeply (more than 8)"]),
    ("unterminated #if", "#if 1\nint x;", {}, [], 200, ["d.c:1: error: unterminated #if"]),
    ("#if unterminated in an include", "#include \"h.h\"\n#endif",
     {"h.h": "#if 1"}, [], 200, ["h.h:1: error: unterminated #if", "d.c:2: error: #endif without #if"]),
    ("#else without #if", "#else", {}, [], 200, ["d.c:1: error: #else without #if"]),
    ("#endif without #if", "\n#endif", {}, [], 200, ["d.c:2: error: #endif without #if"]),
    ("#elif without #if", "#elif 1", {}, [], 200, ["d.c:1: error: #elif without #if"]),
    ("#else after #else", "#if 1\n#else\n#else\n#endif", {}, [], 200, ["d.c:3: error: #else after #else"]),
    ("#elif after #else", "#if 0\n#else\n#elif 1\n#endif", {}, [], 200, ["d.c:3: error: #elif after #else"]),
    ("#error", "#error stop  here", {}, [], 200, ["d.c:1: error: #error stop  here"]),
    ("skipped #error", "#if 0\n#error no\n#endif\nint x;", {}, [], 0, []),
    ("unknown directive", "#foo", {}, [], 200, ["d.c:1: error: unknown directive #foo"]),
    ("skipped unknown directive", "#if 0\n#foo\n#endif", {}, [], 0, []),
    ("null directive", "#\nint x;", {}, [], 0, []),
    ("division by zero", "#if 1 / 0\n#endif", {}, [], 200, ["d.c:1: error: division by zero in #if"]),
    ("empty #if", "#if\n#endif", {}, [], 200, ["d.c:1: error: #if with no expression"]),
    ("incomplete #if", "#if 1 +\n#endif", {}, [], 200, ["d.c:1: error: invalid #if expression"]),
    ("missing ')' in #if", "#if (1\n#endif", {}, [], 200, ["d.c:1: error: missing ')' in #if"]),
    ("'defined' without a name", "#if defined\n#endif", {}, [], 200, ["d.c:1: error: 'defined' needs a macro name"]),
    ("constant too large", "#if 4294967296\n#endif", {}, ["-ansi"], 200,
     ["d.c:1: error: integer constant too large (more than 32 bits) in #if"]),
    ("stray #endasm", "#endasm", {}, [], 200, ["d.c:1: error: #endasm without #asm"]),
    ("unclosed #asm", "#asm\n nop", {}, [], 200, ["d.c:1: error: #asm without #endasm"]),
    ("skipped #asm", "#if 0\n#asm\n#bogus\n#endasm\n#endif", {}, [], 0, []),
    ("trigraph in the default mode", "char *s = \"??=\";", {}, [], 0,
     ["d.c:1: warning: a trigraph, which only strict mode (-ansi) replaces"]),
    ("trigraph in strict mode", "??=define X 1\n#if X != 1\n#error no\n#endif", {}, ["-ansi"], 0, []),
    ("32-bit constant", "#if 4294967295 != 0xFFFFFFFFUL\n#error no\n#endif", {}, [], 0, []),
    ("bad suffix in #if", "#if 1LLL\n#endif", {}, [], 200, ["d.c:1: error: invalid integer constant in #if"]),
    ("unterminated comment", "int x;\n/* open\nmore", {}, [], 200, ["d.c:2: error: unterminated comment"]),
    ("line splicing", "#define X 1 \\\n+ 2\n#if X != 3\n#error no\n#endif\n#error after", {}, [], 200,
     ["d.c:6: error: #error after"]),
    ("a backslash at the end of the file", "int x; \\", {}, [], 0,
     ["d.c:1: warning: a backslash at the end of the file"]),
    ("#line", "\n#line 50\n#error here", {}, [], 200, ["d.c:50: error: #error here"]),
    ("#line with a file name", "#line 7 \"x.c\"\n#error here", {}, [], 200, ["x.c:7: error: #error here"]),
    ("#line from a macro", "#define N 20\n#line N\n#error here", {}, [], 200, ["d.c:20: error: #error here"]),
    ("#line without a number", "#line x", {}, [], 200, ["d.c:1: error: #line needs a line number"]),
    ("#line 0", "#line 0", {}, [], 200, ["d.c:1: error: #line number must not be 0"]),
    ("#line at the largest line", "#line 8388607\n#error here", {}, [], 200, ["d.c:8388607: error: #error here"]),
    ("#line too large", "#line 8388608", {}, [], 200, ["d.c:1: error: #line number too large"]),
    ("#line far too large", "#line 99999999999999", {}, [], 200, ["d.c:1: error: #line number too large"]),
    ("defined(__DATE__) and defined __TIME__", "#if defined(__DATE__) && defined __TIME__\n#error both\n#endif", {}, [],
     200, ["d.c:2: error: #error both"]),
    ("#line with more", "#line 5 \"x.c\" 3", {}, [], 200,
     ["d.c:1: error: #line has more after its line number and file name"]),
    ("#line in an include", "#include \"h.h\"\n#error back", {"h.h": "#line 90 \"z.h\""}, [], 200,
     ["d.c:2: error: #error back"]),
    ("comment hides a directive", "/*\n#error no\n*/\nint x;", {}, [], 0, []),
    # warnings: status 0
    ("// comment", "int x; // hi", {}, [], 0, []),
    ("// comment in #if", "#if 1 // x\n#endif", {}, [], 0, []),
    ("#warning", "#warning look out\nint x;", {}, [], 0, ["d.c:1: warning: #warning look out"]),
    ("#warning in strict mode", "#warning look out\nint x;", {}, ["-ansi"], 200, ["d.c:1: error: unknown directive #warning"]),
    ("GCC's type macros", "typedef __SIZE_TYPE__ size_t;\n#if __FLT_MANT_DIG__ != 24 || __DBL_MANT_DIG__ != 53\n"
     "#error wrong\n#endif", {}, ["-ansi"], 0, []),
    ("#if in 64 bits (default mode)",
     "#if 0xFFFFFFFF + 1 != 4294967296 || 0xFFFFFFFFFFFFFFFFULL + 1 != 0 || -1LL >= 0 || -1ULL < 0\n#error a\n#endif\n"
     "#if (1LL << 40) / 1024 != 1073741824 || 18446744073709551615ULL % 1000 != 615\n#error b\n#endif\n"
     "#if 9223372036854775807 / 3 != 3074457345618258602 || defined(__STRICT_ANSI__)\n#error c\n#endif",
     {}, [], 0, []),
    ("#if in 32 bits (strict mode)",
     "#if 0xFFFFFFFF + 1 != 0 || 2147483647 + 1 != -2147483647 - 1 || 1 << 31 >= 0\n#error a\n#endif\n"
     "#if -2147483648 < 0 || 0x80000000 < 0 || !defined(__STRICT_ANSI__)\n#error b\n#endif",
     {}, ["-ansi"], 0, []),
    ("LL in strict mode", "#if 1LL\n#endif", {}, ["-ansi"], 200, ["d.c:1: error: invalid integer constant in #if"]),
    ("GCC's long long macros", "typedef __INT64_TYPE__ q;\ntypedef __UINTMAX_TYPE__ m;\n"
     "#if __SIZEOF_LONG_LONG__ != 8 || __LONG_LONG_MAX__ != 9223372036854775807\n#error wrong\n#endif",
     {}, [], 0, []),
    ("no long long macros in strict mode", "typedef __INTMAX_TYPE__ m;\n"
     "#if defined(__SIZEOF_LONG_LONG__) || defined(__INT64_TYPE__) || defined(__LONG_LONG_MAX__)\n#error wrong\n#endif",
     {}, ["-ansi"], 0, []),
    ("beyond 64 bits", "#if 18446744073709551616\n#endif", {}, [], 200,
     ["d.c:1: error: integer constant too large (more than 64 bits) in #if"]),
    ("GCC's byte order", "#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__ || __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__\n"
     "#error wrong\n#endif", {}, ["-ansi"], 0, []),
    ("a parameter L beside a wide literal", "#define m(L) (L'1' + (L))\n#if m(0) != L'1'\n#error wrong\n#endif",
     {}, ["-ansi"], 0, []),
    ("// is not a comment in strict mode", "#if 1 // x\n#endif", {}, ["-ansi"], 200, ["d.c:1: error: invalid #if expression"]),
    ("// in a skipped region", "#if 0\nint x; // hi\n#endif", {}, [], 0, []),
    ("C99's version in the default mode", "#if __STDC_VERSION__ != 199901L || !__STDC_HOSTED__ || !__STDC_NO_VLA__\n"
     "#error wrong\n#endif", {}, [], 0, []),
    ("no C99 version in strict mode", "#if defined(__STDC_VERSION__) || defined(__STDC_HOSTED__) || defined(__STDC_NO_VLA__)\n"
     "#error wrong\n#endif", {}, ["-ansi"], 0, []),
    ("#pragma", "#pragma once", {}, [], 0, ["d.c:1: warning: #pragma is ignored"]),
    ("#pragma weak without a name", "#pragma weak", {}, [], 200, ["d.c:1: error: #pragma weak needs a name"]),
    ("#pragma weak with two names", "#pragma weak a b", {}, [], 200, ["d.c:1: error: #pragma weak takes one name"]),
    ("-Werror", "#pragma once", {}, ["-Werror"], 200, ["d.c:1: error: #pragma is ignored"]),
    ("-w", "#pragma once", {}, ["-w"], 0, []),
]


def diagnostics():
    problems = []
    for name, src, extra, flags, status, want in DIAG:
        for f, text in extra.items():
            open(os.path.join(REPO, o(f)), "w", newline="\n").write(text + "\n")
        open(os.path.join(REPO, o("d.c")), "w", newline="\n").write(src + "\n")
        out = os.path.join(REPO, o("d.i"))
        if os.path.exists(out):
            os.remove(out)
        r = sh([os.path.join(HOST, "cpp.exe"), o("d.c"), o("d.i")] + flags)
        lines = [l.replace(OUT + os.sep, "") for l in r.stderr.splitlines()]
        if r.returncode != status:
            problems.append(f"{name}: status {r.returncode}, expected {status}; stderr {lines}")
        for w in want:
            if w not in lines:
                problems.append(f"{name}: missing {w!r}; got {lines}")
        if not want and lines:
            problems.append(f"{name}: expected no diagnostics; got {lines}")
        if status != 0 and os.path.exists(out):
            problems.append(f"{name}: an output file was left behind after an error")
        for f in extra:
            os.remove(os.path.join(REPO, o(f)))
    return problems


# ---- T4 ---------------------------------------------------------------------------------

def run_checks(stem, bad_from, bad_to, bad_index, checks):
    """tests/cpp/<stem>.c built and run: exit 0; then the two guards, a
    wrong expectation reported by its index and the number of checks run."""
    src = os.path.join("tests", "cpp", stem + ".c")
    p = build(src, stem, DEFS)
    if p:
        return [p]
    problems = []
    rc = run_bin(stem)
    if rc != 0:
        problems.append(f"check {rc} failed (the first failing check's index)")
    text = open(os.path.join(REPO, src)).read()
    # the copies live in OUT, so their headers are found through -I
    inc = ["-I", os.path.join("tests", "cpp")]
    bad = text.replace(bad_from, bad_to)
    assert bad != text
    open(os.path.join(REPO, o("t_bad.c")), "w", newline="\n").write(bad)
    p = build(o("t_bad.c"), "t_bad", DEFS + inc)
    rc = run_bin("t_bad") if not p else p
    if rc != bad_index:
        problems.append(f"guard: a wrong expectation gave {rc}, expected its index ({bad_index})")
    cnt = text.replace("    if (fails == 0)\n        agon_emu_exit(0);\n    else\n        agon_emu_exit(first < 250 ? first : 250);",
                       "    agon_emu_exit(count);")
    assert cnt != text
    open(os.path.join(REPO, o("t_cnt.c")), "w", newline="\n").write(cnt)
    p = build(o("t_cnt.c"), "t_cnt", DEFS + inc)
    rc = run_bin("t_cnt") if not p else p
    if rc != checks:
        problems.append(f"guard: {rc} checks ran, expected {checks}")
    return problems


def t4():
    return run_checks("t_cpp", "check(nested_line, 7);", "check(nested_line, 8);", 18, 54)


def t5():
    return run_checks("t_cpp99", "check(CALL(two, 3, 4), 34);", "check(CALL(two, 3, 4), 35);", 8, 14)


# ---- S1 ---------------------------------------------------------------------------------

def s1():
    for b in ("cpp", "cc1", "cc2", "ld"):
        if not os.path.exists(os.path.join(REPO, "build", "stage1", b + ".bin")):
            return [f"build/stage1/{b}.bin missing (make stage1)"]
    if not os.path.exists(os.path.join(REPO, LIBC)):
        return [f"{LIBC} missing (make cross)"]
    # the host's reference, run where the files have the same relative names as on the card
    stage = os.path.join(REPO, o("s1"))
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, "lib", "agonc"))
    for f in ("t_cpp.c", "t_cpp.h", "t_cpp2.h"):
        shutil.copy(os.path.join(HERE, f), stage)
    shutil.copy(os.path.join(REPO, "lib", "libc", "string.h"), os.path.join(stage, "lib", "agonc"))
    r = sh([os.path.join(REPO, HOST, "cpp.exe"), "t_cpp.c", "a.i", "-I", "lib/agonc"] + DEFS, cwd=stage)
    if r.returncode:
        return [f"host cpp failed: {r.stderr.strip()}"]
    host_i = open(os.path.join(stage, "a.i"), "rb").read()

    args = [PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--with-ez80asm"]
    for b in ("cpp", "cc1", "cc2", "ld"):
        args += ["--bin", os.path.join("build", "stage1", b + ".bin")]
    for f in ("t_cpp.c", "t_cpp.h", "t_cpp2.h"):
        args += ["--file", os.path.join("tests", "cpp", f)]
    for f in RUNTIME + [LIBC]:
        args += ["--file", f]
    args += ["--file-at", "lib/agonc/string.h=" + os.path.join("lib", "libc", "string.h")]
    defs = " ".join(DEFS)
    for c in (f"cpp t_cpp.c /a.i -I lib/agonc {defs}",
              f"cpp t_cpp.c /b.i {defs}",
              "cc1 /a.i /a.ir -u t_cpp.c",
              "cc2 /a.ir /a.s",
              "ld -o /a.asm crt0.s rt.s libc.s /a.s",
              "ez80asm /a.asm /bin/dt_cpp.bin -m",
              "dt_cpp"):
        args += ["--cmd", c]
    r = sh(args + ["--sdcard-name", "cpps1", "--timeout", "600"])
    problems = [] if r.returncode == 0 else [f"device-built t_cpp gave {r.returncode}, expected 0"]
    card = os.path.join(REPO, "emulator_sdcard", "cpps1")
    for f, want in (("a.i", host_i), ("b.i", host_i.replace(b'"lib/agonc/string.h"', b'"/lib/agonc/string.h"'))):
        path = os.path.join(card, f)
        if not os.path.exists(path):
            problems.append(f"the device produced no {f}")
        elif open(path, "rb").read() != want:
            problems.append(f"device {f} differs from the expected output")
    if b'"lib/agonc/string.h"' not in host_i:
        problems.append("the host output does not name lib/agonc/string.h (the /lib/agonc check would be vacuous)")
    return problems


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    if not os.path.exists(os.path.join(REPO, "build", "agon", "lib", "agonc", "libc.s")):
        print("build/agon/lib/agonc/libc.s is missing: run make cross first")
        return 1
    cases = [("T1 golden t_cpp.i", lambda: t1("--update" in sys.argv)),
             (f"D  diagnostics ({len(DIAG)} cases)", diagnostics)]
    if "--no-emu" not in sys.argv:
        cases.append(("T4 t_cpp.c on the emulator + guards", t4))
        cases.append(("T5 t_cpp99.c (C99: variadic macros) on the emulator + guards", t5))
        if "--no-stage1" not in sys.argv:
            cases.append(("S1 AgDev cpp (-I and /lib/agonc) then cc1+cc2+ld and device ez80asm build t_cpp.c on the emulator", s1))
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
