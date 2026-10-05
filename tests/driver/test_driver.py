"""Tests for the driver, agonc (src/agonc/).

    test_driver.py [--no-stage1]

The host driver (build/host/agonc.exe, make host) runs the host passes, with
a library root staged under OUT/root (common.stage_root) as AGONC_ROOT.

H1  hello.c through the driver equals the passes run by hand, byte for byte.
H2  every way in and out: -E (to a file and to the console), -c, -S -o,
    and .i, .s and .asm inputs, each equal to the hand-run pass's output.
H3  temporary files: all removed after a success and after a failure;
    -save-temps keeps them.
H4  options: the accepted-and-ignored ones, -O (the default, reaching cc2;
    -O0 last turns it off), --version, -v, -w, -Werror,
    the driver's own @file, and a pass given a response file.
H5  link options: -nostdlib with the runtime named explicitly, -L and -l,
    -l found in /usrlib, -Wl,--entry.
H6  every error: status 200, the expected message, and no output file.
H7  -h lists the options; -time prints each pass's time and the total;
    __DATE__ comes from the clock the driver reads;
    -o naming an input, or a source file of another kind than the mode
    writes, is refused before anything runs, and the file is untouched.
T4  t_drv.c + t_util.c + libextra.s built by the driver, with -I, -D, -U
    and -l, run on the emulator: exit 0. Guards: built with -DDEFINED=41 it
    fails check 2; built with -DCOUNT it reports 11 checks. And twice.c
    with twice.s, driver.md's example of a function in a hand-written .s:
    exit 42.
S1  on the emulator, the moslet driver (make cross) and the AgDev-built
    passes build libextra.s (-c) and the same program with the same options
    (the library found in /usrlib); it runs (exit 0, which includes all 8
    MOS handles being free afterwards), and the device's .bin and .s equal
    the host driver's.
F   on the emulator, a build that fails in ld returns non-zero, so MOS
    stops the script (the next command would exit 0; the run times out),
    and it leaves neither the binary nor anything in /tmp.
C   on the emulator, Ctrl-C pressed while cc1 compiles a large file stops
    the build the same way (the run times out rather than going on to a
    program that would exit 0), leaving neither the binary nor anything in
    /tmp: with the AgDev-built passes, which leave MOS's keyboard vector
    alone, the driver's own handler notices it; with the passes the driver
    builds (make cross), the pass itself stops with 130 and the driver
    sees that.
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import stage_root, host_env  # noqa: E402

OUT = os.path.join("build", "test", "driver")      # repo-relative: ez80asm wants short names
ROOT = os.path.join(OUT, "root")
TESTS = os.path.join("tests", "driver")
HOST = os.path.join("build", "host")
AGONC = os.path.join(HOST, "agonc.exe")
PY = sys.executable
CHECKS = 11

FIXTURES = {
    "bad.c": "#error deliberately bad\nint main(void) { return 0; }\n",
    "cc1err.c": "int main(void) { return undeclared; }\n",
    "undef.c": "int missing(void);\nint main(void) { return missing(); }\n",
    "big.c": "char big[460000];\nint main(void) { big[0] = 1; return big[0]; }\n",
    "warn.c": "int f(int x) { if (x) return 1; }\nint main(void) { return f(1); }\n",
    "notc.txt": "not a source file\n",
    "dup1/x.c": "int x1(void) { return 1; }\n",
    "dup2/x.c": "int x2(void) { return 2; }\n",
}


def sh(cmd, env=None):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, env=env)


def o(name):
    return os.path.join(OUT, name)


def t(name):
    return os.path.join(TESTS, name)


def agonc(*args):
    return sh([AGONC] + list(args), env=host_env(ROOT))


def read(path):
    p = os.path.join(REPO, path)
    return open(p, "rb").read() if os.path.exists(p) else None


def exists(path):
    return os.path.exists(os.path.join(REPO, path))


def tmp_files():
    return sorted(os.listdir(os.path.join(REPO, ROOT, "tmp")))


def by_hand(src, stem):
    """cpp, cc1, cc2, ld and ez80asm run directly, as the driver should."""
    lib = os.path.join(ROOT, "lib")
    # the include paths spelled as the driver spells them: they reach the .i's line markers
    for cmd in ([os.path.join(HOST, "cpp.exe"), src, o(stem + ".i"), "-I" + ROOT + "/usrlib", "-I" + ROOT + "/lib"],
                [os.path.join(HOST, "cc1.exe"), o(stem + ".i"), o(stem + ".ir"), "-u", os.path.basename(src)],
                [os.path.join(HOST, "cc2.exe"), o(stem + ".ir"), o(stem + ".s"), "-O"],
                [os.path.join(HOST, "ld.exe"), "-o", o(stem + ".asm"), os.path.join(lib, "crt0.s"),
                 os.path.join(lib, "rt.s"), o(stem + ".s"), os.path.join(lib, "libc.s")],
                [os.path.join("third_party", "bin", "ez80asm.exe"), o(stem + ".asm"), o(stem + ".bin"), "-m"]):
        r = sh(cmd)
        if r.returncode:
            return f"{os.path.basename(cmd[0])}: {(r.stderr or r.stdout).strip()[:300]}"
    return None


def expect_ok(r, what):
    return [] if r.returncode == 0 else [f"{what}: status {r.returncode}: {(r.stderr + r.stdout).strip()[:300]}"]


def same(a, b, what):
    da, db = read(a), read(b)
    if da is None or db is None:
        return [f"{what}: {a if da is None else b} missing"]
    return [] if da == db else [f"{what}: {a} differs from {b}"]


# ---- host ------------------------------------------------------------------

def h1():
    p = by_hand(t("hello.c"), "hand")
    if p:
        return [p]
    return expect_ok(agonc("-o", o("hello.bin"), t("hello.c")), "agonc hello.c") + \
        same(o("hello.bin"), o("hand.bin"), "the driver's binary")


def h2():
    problems = []
    problems += expect_ok(agonc("-E", "-o", o("e.i"), t("hello.c")), "-E -o")
    problems += same(o("e.i"), o("hand.i"), "-E -o")
    r = agonc("-E", t("hello.c"))
    problems += expect_ok(r, "-E to the console")
    if r.stdout.replace("\r\n", "\n").encode() != read(o("hand.i")).replace(b"\r\n", b"\n"):
        problems.append("-E to the console: the output differs from cpp's .i")
    os.makedirs(os.path.join(REPO, o("c")), exist_ok=True)
    shutil.copy(os.path.join(REPO, t("hello.c")), os.path.join(REPO, o("c/hello.c")))
    problems += expect_ok(agonc("-c", o("c/hello.c")), "-c")
    problems += same(o("c/hello.s"), o("hand.s"), "-c (beside the input)")
    problems += expect_ok(agonc("-S", "-o", o("s_named.s"), t("hello.c")), "-S -o")
    problems += same(o("s_named.s"), o("hand.s"), "-S -o")
    # the .i keeps hello.c's unit name only if named so: copy it
    shutil.copy(os.path.join(REPO, o("hand.i")), os.path.join(REPO, o("c/hello.i")))
    # (the unit is named hello.i, but hello.c has no statics to show it)
    problems += expect_ok(agonc("-o", o("from_i.bin"), o("c/hello.i")), ".i input")
    problems += same(o("from_i.bin"), o("hand.bin"), ".i input")
    problems += expect_ok(agonc("-o", o("from_s.bin"), o("hand.s")), ".s input")
    problems += same(o("from_s.bin"), o("hand.bin"), ".s input")
    problems += expect_ok(agonc("-o", o("from_asm.bin"), o("hand.asm")), ".asm input")
    problems += same(o("from_asm.bin"), o("hand.bin"), ".asm input")
    return problems


def h3():
    problems = []
    if tmp_files():
        return [f"tmp not empty before the test: {tmp_files()}"]
    problems += expect_ok(agonc("-o", o("t1.bin"), t("hello.c")), "build")
    if tmp_files():
        problems.append(f"after a success tmp holds {tmp_files()}")
    r = agonc("-o", o("t2.bin"), o("cc1err.c"))
    if r.returncode != 200 or tmp_files() or exists(o("t2.bin")):
        problems.append(f"after a failure: status {r.returncode}, tmp {tmp_files()}, binary {exists(o('t2.bin'))}")
    problems += expect_ok(agonc("-save-temps", "-o", o("t3.bin"), t("hello.c")), "-save-temps")
    want = ["hello.i", "hello.ir", "hello.s", "t3.asm"]
    kept = [f for f in tmp_files() if not f.endswith(".rsp")]   # ld gets a response file if its paths are long
    if kept != want:
        problems.append(f"-save-temps kept {kept}, expected {want} (and any .rsp)")
    for f in tmp_files():
        os.remove(os.path.join(REPO, ROOT, "tmp", f))
    return problems


def h4():
    problems = []
    problems += expect_ok(agonc("-O", "-O2", "-Os", "-O0", "-std=c89", "-std=c90", "-ansi", "-Wall",
                                "-o", o("opts.bin"), t("hello.c")), "ignored options")
    problems += same(o("opts.bin"), o("hand.bin"), "ignored options")
    r = agonc("--version")
    if r.returncode or not r.stdout.startswith("agonc "):
        problems.append(f"--version: status {r.returncode}, output {r.stdout!r}")
    r = agonc("-v", "-o", o("v.bin"), t("hello.c"))
    problems += expect_ok(r, "-v")
    for want in ("cpp.exe " + t("hello.c").replace("\\", "/"), "cc1.exe ", "cc2.exe ", "ld.exe ", "ez80asm ", "ld: tables:"):
        if want not in r.stdout.replace("\\", "/"):
            problems.append(f"-v: no {want!r} in the output")
    # -O reaches cc2 (the peephole pass), and is the default; -O0, last, turns it off
    for flags, want in ((["-O"], True), (["-O2", "-O0"], False), ([], True), (["-O0"], False),
                        (["-O0", "-Os"], True)):
        r = agonc("-v", *flags, "-o", o("opt.bin"), t("hello.c"))
        problems += expect_ok(r, " ".join(flags) or "no -O")
        cc2 = [line for line in r.stdout.replace("\\", "/").splitlines() if "cc2.exe " in line]
        if not cc2 or (" -O" in cc2[0]) != want:
            problems.append(f"{' '.join(flags) or 'no -O'}: cc2's command {cc2[:1]}, -O expected {want}")
    # --index has ld index a library beside it (object_format.md section 9); .s files only
    shutil.copy(os.path.join("tests", "ld", "cases", "basic_a.s"), o("idxlib.s"))
    if os.path.exists(o("idxlib.idx")):
        os.remove(o("idxlib.idx"))
    r = agonc("-v", "--index", o("idxlib.s"))
    problems += expect_ok(r, "--index")
    if "ld.exe --index " not in r.stdout or not os.path.exists(o("idxlib.idx")):
        problems.append(f"--index: no index made ({r.stdout.strip()[-200:]})")
    r = agonc("--index", t("hello.c"))
    if r.returncode != 200 or "--index takes .s libraries only" not in r.stderr:
        problems.append(f"--index of a .c: status {r.returncode}, {r.stderr.strip()[:200]!r}")
    # /lib/libm.s is linked for floating point or long long only, and once
    # with -lm: ld's command line and its response file, kept by
    # -save-temps, say what it was given
    open(os.path.join(REPO, o("fp.c")), "w", newline="\n").write(
        "#include <math.h>\nint main(void)\n{\n    return (int)sqrt(49.0);\n}\n")
    open(os.path.join(REPO, o("ll.c")), "w", newline="\n").write(
        "long long q = 5;\nint main(void)\n{\n    return (int)(q * q);\n}\n")
    for src, extra, want in ((t("hello.c"), (), 0), (o("fp.c"), (), 1), (o("fp.c"), ("-lm",), 1), (o("ll.c"), (), 1)):
        r = agonc("-v", "-save-temps", "-o", o("fp.bin"), src, *extra)
        problems += expect_ok(r, f"{os.path.basename(src)} {' '.join(extra)}")
        rsp = (read(os.path.join(ROOT, "tmp", "ld.rsp")) or b"").decode() + \
            "".join(line for line in r.stdout.splitlines() if "ld.exe " in line)
        if "crt0.s" not in rsp:
            problems.append(f"{os.path.basename(src)}: ld's arguments not found")
        elif rsp.count("libm.s") != want:
            problems.append(f"{os.path.basename(src)} {' '.join(extra)}: libm.s linked {rsp.count('libm.s')} times, "
                            f"expected {want}")
        for f in tmp_files():
            os.remove(os.path.join(REPO, ROOT, "tmp", f))
    r = agonc("-o", o("w.bin"), o("warn.c"))
    if r.returncode or "control reaches the end" not in r.stderr:
        problems.append(f"warn.c: status {r.returncode}, expected 0 and a warning")
    r = agonc("-w", "-o", o("w.bin"), o("warn.c"))
    if r.returncode or "warning" in r.stderr:
        problems.append(f"-w: status {r.returncode}, stderr {r.stderr.strip()!r}")
    r = agonc("-Werror", "-o", o("w2.bin"), o("warn.c"))
    if r.returncode != 200 or exists(o("w2.bin")):
        problems.append(f"-Werror: status {r.returncode}, binary {exists(o('w2.bin'))}")
    # strict mode: an implicit declaration is an error by default, a warning
    # with -ansi; and ld rejects one whose callee returns a long
    open(os.path.join(REPO, o("impl.c")), "w").write("int main(void) { return f(); }\nint f(void) { return 0; }\n")
    r = agonc("-o", o("impl.bin"), o("impl.c"))
    if r.returncode != 200 or "call to undeclared function f" not in r.stderr:
        problems.append(f"implicit call, default mode: status {r.returncode}, stderr {r.stderr.strip()!r}")
    r = agonc("-ansi", "-o", o("impl.bin"), o("impl.c"))
    if r.returncode or "implicit declaration of function f" not in r.stderr:
        problems.append(f"implicit call, -ansi: status {r.returncode}, stderr {r.stderr.strip()!r}")
    open(os.path.join(REPO, o("impl2.c")), "w").write("int main(void) { return (int)f(); }\nlong f(void) { return 0; }\n")
    r = agonc("-std=c89", "-c", "-o", o("impl2a.s"), o("impl2.c"))
    if r.returncode != 200 or "conflicting types for f" not in r.stderr:
        problems.append(f"implicit call then long definition: status {r.returncode}, stderr {r.stderr.strip()!r}")
    open(os.path.join(REPO, o("impl3.c")), "w").write("int main(void) { return f(); }\n")
    open(os.path.join(REPO, o("impl4.c")), "w").write("long f(void) { return 0; }\n")
    r = agonc("-ansi", "-o", o("impl3.bin"), o("impl3.c"), o("impl4.c"))
    if r.returncode != 200 or "implicit call to _f, which returns long" not in r.stderr or exists(o("impl3.bin")):
        problems.append(f"implicit call to a long function: status {r.returncode}, stderr {r.stderr.strip()!r}")
    open(os.path.join(REPO, o("args.rsp")), "w").write(f"-o {o('rsp.bin')}\n{t('hello.c')}\n")
    problems += expect_ok(agonc("@" + o("args.rsp")), "the driver's @file")
    problems += same(o("rsp.bin"), o("hand.bin"), "the driver's @file")
    defs = [f"-DUNUSED_MACRO_{i}=1" for i in range(30)]
    r = agonc("-v", "-o", o("many.bin"), *defs, t("hello.c"))
    problems += expect_ok(r, "30 -D options")
    if "@" + os.path.join(ROOT, "tmp", "cpp.rsp").replace("\\", "/") not in r.stdout.replace("\\", "/"):
        problems.append("30 -D options: cpp was not given a response file")
    problems += same(o("many.bin"), o("hand.bin"), "30 -D options")
    return problems


def h7():
    problems = []
    r = agonc("-h")
    if r.returncode or not r.stdout.startswith("usage: agonc") or "-time" not in r.stdout:
        problems.append(f"-h: status {r.returncode}, output {r.stdout[:80]!r}")
    if max((len(l) for l in r.stdout.splitlines()), default=0) > 60:
        problems.append("-h: a line longer than 60 characters")
    r = agonc("-time", "-o", o("time.bin"), t("hello.c"))
    problems += expect_ok(r, "-time")
    for want in ("time: cc1 ", "time: ez80asm ", "time: total "):
        if want not in r.stdout:
            problems.append(f"-time: no {want!r} in the output")
    if "cc1.exe " in r.stdout.replace("\\", "/"):
        problems.append("-time: printed the pass command lines, which are -v's")
    keep = o("keep.c")
    text = open(os.path.join(REPO, t("hello.c")), "rb").read()
    open(os.path.join(REPO, keep), "wb").write(text)
    for args, want in ((["-o", keep, keep], "would overwrite an input"),
                       (["-o", keep.upper(), keep], "would overwrite an input"),
                       (["-c", "-o", keep, keep], "would overwrite an input"),
                       (["-E", "-o", keep, keep], "would overwrite an input"),
                       (["-o", keep, o("keep.bin")], "would overwrite a source file"),
                       (["-o", o("keep.h"), t("hello.c")], "would overwrite a source file"),
                       (["-o", o("keep.s"), t("hello.c")], "would overwrite a source file"),
                       (["-c", "-o", o("keep.i"), t("hello.c")], "should end in .s"),
                       (["-E", "-o", o("keep.s"), t("hello.c")], "should end in .i")):
        r = agonc(*args)
        if r.returncode != 200 or want not in r.stderr:
            problems.append(f"{' '.join(args)}: status {r.returncode}, stderr {r.stderr.strip()!r}")
    if open(os.path.join(REPO, keep), "rb").read() != text:
        problems.append("keep.c was changed")
    # the driver passes the clock to cpp: __DATE__ is not C89's fallback
    open(os.path.join(REPO, o("date.c")), "w").write("char *d = __DATE__;\nchar *t = __TIME__;\n")
    r = agonc("-E", o("date.c"))
    if r.returncode or "Jan  1 1980" in r.stdout or '"' not in r.stdout:
        problems.append(f"__DATE__ through the driver: {r.stdout.strip()!r}")
    problems += expect_ok(agonc("-c", "-o", o("keep2.s"), keep), "-c -o name.s")
    problems += expect_ok(agonc("-E", "-o", o("keep2.i"), keep), "-E -o name.i")
    return problems


def h5():
    problems = []
    lib = os.path.join(ROOT, "lib")
    problems += expect_ok(agonc("-nostdlib", "-o", o("nostd.bin"), os.path.join(lib, "crt0.s"),
                                os.path.join(lib, "rt.s"), t("hello.c"), os.path.join(lib, "libc.s")), "-nostdlib")
    problems += same(o("nostd.bin"), o("hand.bin"), "-nostdlib with the runtime named")
    r = agonc("-nostdlib", "-o", o("nostd2.bin"), t("hello.c"))
    if r.returncode != 200 or "__start" not in r.stderr:
        problems.append(f"-nostdlib alone: status {r.returncode}, expected ld's missing-__start error")
    os.makedirs(os.path.join(REPO, o("ulib")), exist_ok=True)
    problems += expect_ok(agonc("-c", "-o", o("ulib/libextra.s"), t("extra.c")), "-c libextra.s")
    common = ["-I", t("inc"), "-DDEFINED=42", "-DFLAG", "-DGONE", "-UGONE", t("t_drv.c"), t("t_util.c")]
    problems += expect_ok(agonc("-o", o("drv_L.bin"), "-L", o("ulib"), "-lextra", *common), "-L -l")
    shutil.copy(os.path.join(REPO, o("ulib/libextra.s")), os.path.join(REPO, ROOT, "usrlib"))
    problems += expect_ok(agonc("-o", o("drv_u.bin"), "-lextra", *common), "-l from /usrlib")
    problems += same(o("drv_u.bin"), o("drv_L.bin"), "-l from /usrlib")
    for entry, want in (([], False), (["-Wl,--entry=_extra_unused"], True)):
        problems += expect_ok(agonc("-save-temps", *entry, "-o", o("entry.bin"), "-lextra", *common), "-Wl,--entry")
        asm = (read(os.path.join(ROOT, "tmp", "entry.asm")) or b"").decode()
        if ("_extra_unused:" in asm) != want:
            problems.append(f"with options {entry}: _extra_unused {'not ' if want else ''}linked")
    for f in tmp_files():
        os.remove(os.path.join(REPO, ROOT, "tmp", f))
    return problems


HELLO = t("hello.c")
ERRORS = [      # each run as agonc -o OUT/err.bin <args>
    (["-Wextra", HELLO], "unknown option -Wextra"),
    (["-g", HELLO], "no debugging information"),
    (["-x", "c", HELLO], "the language comes from the extension"),
    (["-static", HELLO], "no shared libraries"),
    (["-MD", HELLO], "dependency output"),
    (["-lmissing", HELLO], "cannot find -lmissing"),
    (["@" + o("nosuch.rsp")], "cannot open response file"),
    ([o("nosuch.c")], "no such file"),
    ([o("notc.txt")], "unknown file type"),
    ([o("bad.c")], "#error deliberately bad"),
    ([o("cc1err.c")], "undeclared"),
    ([o("undef.c")], "undefined symbol _missing"),
    ([o("big.c")], "image too large"),
    ([o("dup1/x.c"), o("dup2/x.c")], "another input has the same name"),
    ([o("hand.asm"), HELLO], "an .asm file is assembled alone"),
    (["-c", o("dup1/x.c"), o("dup2/x.c"), "-o"], "-o needs an argument"),
]


def h6():
    problems = []
    for args, message in ERRORS:
        r = agonc("-o", o("err.bin"), *args)
        text = r.stdout + r.stderr
        if r.returncode != 200 or message not in text or exists(o("err.bin")):
            problems.append(f"{' '.join(args)}: status {r.returncode}, binary {exists(o('err.bin'))}, "
                            f"output {text.strip()[:200]!r} (expected 200 and {message!r})")
    for args, message in (([], "no input files"),
                          (["-c", "-o", o("x.s"), HELLO, t("t_util.c")], "needs a single input")):
        r = agonc(*args)
        if r.returncode != 200 or message not in r.stderr:
            problems.append(f"{' '.join(args)}: status {r.returncode}, stderr {r.stderr.strip()[:200]!r}")
    if tmp_files():
        problems.append(f"the failures left {tmp_files()} in tmp")
    return problems


# ---- the emulator ----------------------------------------------------------

def emulate(bins, cmds, name, timeout=120, extra=()):
    args = [PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin"]
    for b in bins:
        args += ["--bin", b]
    for c in cmds:
        args += ["--cmd", c]
    return sh(args + list(extra) + ["--sdcard-name", name, "--timeout", str(timeout)]).returncode


DRV_OPTS = ["-I", "inc", "-DDEFINED=42", "-DFLAG", "-DGONE", "-UGONE"]


def t4():
    problems = []
    common = ["-I", t("inc"), "-DFLAG", "-DGONE", "-UGONE", "-L", o("ulib"), "-lextra", t("t_drv.c"), t("t_util.c")]
    for name, defs, want in (("t_drv", ["-DDEFINED=42"], 0), ("t_drv_bad", ["-DDEFINED=41"], 2),
                             ("t_drv_cnt", ["-DDEFINED=42", "-DCOUNT"], CHECKS)):
        r = agonc("-o", o(name + ".bin"), *defs, *common)
        if r.returncode:
            problems.append(f"{name}: the build failed: {(r.stderr + r.stdout).strip()[:300]}")
            continue
        rc = emulate([o(name + ".bin")], [name], "drvt4")
        if rc != want:
            problems.append(f"{name}: exit {rc}, expected {want}")
    # driver.md 2's example: a function written in assembly, in a hand-written
    # .s whose ;;unit line has no id (ld works it out)
    r = agonc("-o", o("twice.bin"), t("twice.c"), t("twice.s"))
    if r.returncode:
        problems.append(f"twice: the build failed: {(r.stderr + r.stdout).strip()[:300]}")
    else:
        rc = emulate([o("twice.bin")], ["twice"], "drvt4")
        if rc != 42:
            problems.append(f"twice: exit {rc}, expected 42")
    return problems


def card_files(passes=os.path.join("build", "stage1")):
    """Everything the device build needs: passes (AgDev's stage 1 unless
    passes names another folder), library, sources."""
    files = ["--moslet", os.path.join("build", "agon", "mos", "agonc.bin"), "--with-ez80asm"]
    for p in ("cpp", "cc1", "cc2", "ld"):
        files += ["--file-at", f"bin/agonc/{p}.bin=" + os.path.join(passes, p + ".bin")]
    for dirpath, _, names in os.walk(os.path.join(REPO, ROOT, "lib")):
        for f in names:
            src = os.path.relpath(os.path.join(dirpath, f), REPO)
            files += ["--file-at", os.path.relpath(src, ROOT).replace("\\", "/") + "=" + src]
    for f in ("t_drv.c", "t_util.c", "check.h", "extra.c"):
        files += ["--file", t(f)]
    files += ["--file-at", "inc/drv.h=" + t("inc/drv.h"), "--file", o("undef.c"), "--file", o("many.c"),
              "--file-at", "usrlib/notc.txt=" + o("notc.txt")]       # so /usrlib exists
    return files


def s1():
    for p in ("cpp", "cc1", "cc2", "ld"):
        if not exists(os.path.join("build", "stage1", p + ".bin")):
            return [f"build/stage1/{p}.bin missing (make stage1)"]
    if not exists(os.path.join("build", "agon", "mos", "agonc.bin")):
        return ["build/agon/mos/agonc.bin missing (make cross)"]
    # the host's reference build, with the same options and file names
    os.makedirs(os.path.join(REPO, o("s1")), exist_ok=True)
    for f in ("t_drv.c", "t_util.c", "check.h", "extra.c"):
        shutil.copy(os.path.join(REPO, t(f)), os.path.join(REPO, o("s1/" + f)))
    os.makedirs(os.path.join(REPO, o("s1/inc")), exist_ok=True)
    shutil.copy(os.path.join(REPO, t("inc/drv.h")), os.path.join(REPO, o("s1/inc/drv.h")))
    env = host_env(os.path.relpath(os.path.join(REPO, ROOT), os.path.join(REPO, o("s1"))))
    for args in (["-c", "-o", "libextra.s", "extra.c"], ["-o", "dprog.bin", "-lextra"] + DRV_OPTS + ["t_drv.c", "t_util.c"]):
        r = subprocess.run([os.path.join(REPO, AGONC)] + args, cwd=os.path.join(REPO, o("s1")),
                           capture_output=True, text=True, env=env)
        if r.returncode:
            return [f"host reference build failed: {(r.stderr + r.stdout).strip()[:300]}"]
        if args[0] == "-c":
            shutil.copy(os.path.join(REPO, o("s1/libextra.s")), os.path.join(REPO, ROOT, "usrlib"))
    cmds = ["agonc -c -o /usrlib/libextra.s extra.c",
            "agonc -o /bin/dprog.bin -lextra " + " ".join(DRV_OPTS) + " t_drv.c t_util.c", "dprog"]
    rc = emulate([], cmds, "drvs1", 1500, card_files())
    problems = [] if rc == 0 else [f"the device-built program gave {rc}, expected 0 (124: a command failed)"]
    card = os.path.join("emulator_sdcard", "drvs1")
    problems += same(os.path.join(card, "usrlib", "libextra.s"), o("s1/libextra.s"), "device libextra.s")
    problems += same(os.path.join(card, "bin", "dprog.bin"), o("s1/dprog.bin"), "device dprog.bin")
    left = os.listdir(os.path.join(REPO, card, "tmp")) if exists(os.path.join(card, "tmp")) else []
    if left:
        problems.append(f"the device builds left {left} in /tmp")
    return problems


def fail_stops():
    """A failing build must stop the script: the next command would exit 0."""
    rc = emulate([o("t_drv.bin")], ["agonc -o /bin/x.bin undef.c", "t_drv"], "drvf", 240, card_files())
    card = os.path.join("emulator_sdcard", "drvf")
    problems = [] if rc == 124 else [f"got {rc}, expected 124 (0: the script went on after the failure)"]
    if exists(os.path.join(card, "bin", "x.bin")):
        problems.append("the failed build left /bin/x.bin")
    left = os.listdir(os.path.join(REPO, card, "tmp")) if exists(os.path.join(card, "tmp")) else []
    if left:
        problems.append(f"the failed build left {left} in /tmp")
    return problems


def interrupted(card, passes, press=True):
    """Ctrl-C while cc1 writes /tmp/many.ir must stop the script too; the
    guard (press False) is the same build left alone, which finishes."""
    extra = ["--ctrl-c-when", "/tmp/many.ir"] if press else []
    rc = emulate([o("t_drv.bin")], ["agonc -o /bin/many.bin many.c", "t_drv"], card, 60, card_files(passes) + extra)
    if not press:
        return [] if rc == 0 else [f"guard: without Ctrl-C the build and t_drv gave {rc}, expected 0"]
    card_dir = os.path.join("emulator_sdcard", card)
    problems = [] if rc == 124 else [f"got {rc}, expected 124 (0: the build went on after Ctrl-C)"]
    if exists(os.path.join(card_dir, "bin", "many.bin")):
        problems.append("the interrupted build left /bin/many.bin")
    left = os.listdir(os.path.join(REPO, card_dir, "tmp")) if exists(os.path.join(card_dir, "tmp")) else []
    if left:
        problems.append(f"the interrupted build left {left} in /tmp")
    return problems


def main():
    if os.path.isdir(os.path.join(REPO, OUT)):          # nothing from an earlier run can pass for output
        shutil.rmtree(os.path.join(REPO, OUT))
    os.makedirs(os.path.join(REPO, OUT))
    for f_name in ("dup1", "dup2"):
        os.makedirs(os.path.join(REPO, OUT, f_name), exist_ok=True)
    for name, text in FIXTURES.items():
        open(os.path.join(REPO, o(name)), "w", newline="\n").write(text)
    with open(os.path.join(REPO, o("many.c")), "w", newline="\n") as f:     # for C: slow enough to stop
        for i in range(1000):                          # (cc1 allows 1,200 globals)
            f.write(f"int f{i}(int x) {{ return x * {i} + {i % 7}; }}\n")
        f.write("int main(void) { return f1(1) - 1; }\n")
    stage_root(ROOT)
    cases = [("H1 hello.c through the driver equals the passes run by hand", h1),
             ("H2 -E, -c, -S -o, and .i/.s/.asm inputs", h2),
             ("H3 temporary files removed after success and failure; -save-temps keeps them", h3),
             ("H4 ignored options, --version, -v, -w, -Werror, @file, pass response files", h4),
             ("H5 -nostdlib, -L/-l, -l from /usrlib, -Wl,--entry", h5),
             ("H7 -h, -time, and -o never overwriting a source", h7),
             ("H6 errors: status 200, the message, no output", h6),
             ("T4 t_drv (2 units, a user library, -I/-D/-U/-l) on the emulator + guards; a hand-written .s", t4)]
    if "--no-stage1" not in sys.argv:
        cases.append(("S1 the moslet driver builds libextra.s and t_drv on the emulator; equal to the host's", s1))
        cases.append(("F  a failing device build stops the script and leaves nothing behind", fail_stops))
        cases.append(("C  Ctrl-C stops a device build (AgDev's passes: the driver notices)",
                      lambda: interrupted("drvc1", os.path.join("build", "stage1"))))
        cases.append(("C  Ctrl-C stops a device build (our passes: cc1 stops with 130)",
                      lambda: interrupted("drvc2", os.path.join("build", "agon", "bin", "agonc"))))
        cases.append(("C  guard: the same build without Ctrl-C finishes",
                      lambda: interrupted("drvc3", os.path.join("build", "agon", "bin", "agonc"), False)))
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
