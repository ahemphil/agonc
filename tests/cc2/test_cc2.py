"""Tests for cc2, fed hand-written IR (there is no cc1 yet).

    test_cc2.py [--update] [--no-emu] [--no-stage1]

T1  count.ir (docs/ir_format.md's example, with its real unit id) against
    a golden .s.
T2  exec.ir compiled with -a: every line's offset as cc2 computed it must
    equal its address in ez80asm's listing, relative to its function.
T3  exec.ir compiled with offset branches and with real labels
    (-fno-offset-branches) must assemble to identical binaries.
T4  exec.ir linked with the runtime and run on the emulator: 0 failed
    checks (148 for int, 143 for long, more for short, 8 for struct
    arguments and results, and 4 for calls through pointers). Two guards against a vacuous
    harness: with one expectation made
    wrong the program must report exactly 1 failure, and --first must name
    that check.
P   the peephole pass (-O), rule by rule: a small C function compiled by
    cc1, then by cc2 with and without -O; the plain code must hold the
    pattern the rule rewrites, and the -O code its replacement and not the
    pattern. With -O off the output is unchanged (T1's golden, and every
    other suite).
T2O T2 with -O: the peephole's rewritten instructions have the sizes
    ez80asm gives them.
T4O T4 with -O: the same checks pass (no guards: T4's cover the harness).
S1  the AgDev-built cc2 compiles exec.ir on the emulator, the AgDev-built
    ld and the on-device ez80asm build it there, and it runs: exit 0, with
    the device's .s identical to the host cc2's.
"""
import os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join("build", "test", "cc2")          # repo-relative: ez80asm wants short names
PY = sys.executable
CC2 = os.path.join("build", "host", "cc2.exe")
LD = os.path.join("build", "host", "ld.exe")
ASM = os.path.join("third_party", "bin", "ez80asm.exe")
RUNTIME = [os.path.join("lib", "rt", "crt0.s"), os.path.join("lib", "rt", "rt.s"),
           os.path.join("build", "agon", "lib", "libc.s")]


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, **kw)


def o(name):
    return os.path.join(OUT, name)


def build(ir, stem, cc2_flags=(), listing=False):
    """cc2 + ld + ez80asm; returns a problem string or None."""
    r = sh([CC2, ir, o(stem + ".s")] + list(cc2_flags))
    if r.returncode:
        return f"cc2 failed: {r.stderr.strip()}"
    r = sh([LD, "-o", o(stem + ".asm")] + RUNTIME + [o(stem + ".s")])
    if r.returncode:
        return f"ld failed: {r.stderr.strip()}"
    r = sh([ASM, o(stem + ".asm"), o(stem + ".bin")] + (["-l"] if listing else []))
    if r.returncode:
        return f"ez80asm failed: {r.stdout.strip()}"
    return None


def gen(stem, *flags):
    r = sh([PY, os.path.join("tests", "cc2", "gen_exec.py"), o(stem + ".ir")] + list(flags))
    if r.returncode:
        raise SystemExit("gen_exec.py failed: " + r.stderr)


def run_bin(stem, timeout=60):
    r = sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--bin", o(stem + ".bin"),
            "--cmd", stem, "--sdcard-name", "cc2", "--timeout", str(timeout)])
    return r.returncode


# ---- the cases ------------------------------------------------------------------

def t1(update):
    r = sh([CC2, os.path.join("tests", "cc2", "count.ir"), o("count.s")])
    if r.returncode:
        return [f"cc2 failed: {r.stderr.strip()}"]
    golden = os.path.join(HERE, "count.s.golden")
    got = open(os.path.join(REPO, o("count.s"))).read()
    if update:
        with open(golden, "w", newline="\n") as f:
            f.write(got)
    return [] if open(golden).read() == got else ["count.s differs from count.s.golden"]


LST_RE = re.compile(r"^([0-9A-F]{6}) (?:[0-9A-F]{2} )*\s*\d{4}\s(.*)$")


def t2(flags=("-a",), stem="exec_ann"):
    p = build(o("exec.ir"), stem, list(flags), listing=True)
    if p:
        return [p]
    problems, start, checked = [], None, 0
    for line in open(os.path.join(REPO, o(stem + ".lst"))):
        m = LST_RE.match(line.rstrip("\n"))
        if not m:
            continue
        addr, src = int(m.group(1), 16), m.group(2).strip()
        if re.match(r"^[_A-Za-z]\w*:$", src):
            start = addr
            continue
        a = re.search(r";\s*@(\d+)\s*$", src)
        if a and start is not None:
            checked += 1
            if addr - start != int(a.group(1)) and len(problems) < 5:
                problems.append(f"offset {a.group(1)} computed, {addr - start} assembled: {src}")
    if checked < 1000:
        problems.append(f"only {checked} annotated lines found in the listing")
    return problems


CC1 = os.path.join("build", "host", "cc1.exe")

# (name, C source of a function f, a line the plain code has and -O does
# not, a line the -O code has)
PEEP_CASES = [
    ("signed compare with a constant", "int f(int x) { if (x < 10) return 1; return 2; }",
     "call __ilts", "ld de,-8388608"),
    ("signed compare of two values", "int f(int a, int b) { if (a <= b) return 1; return 0; }",
     "call __iles", "ld bc,-8388608"),
    ("subtract 1 and 2: dec hl", "int k(int);\nint f(int n) { return k(n - 1) + k(n - 2); }",
     "ld de,2", "dec hl"),
    ("add 1: inc hl", "int g(int, int);\nint f(int a, int b) { return g(a + 1, b); }",
     "ld de,1", "inc hl"),
    ("a byte constant stored through HL", "char buf[9];\nvoid f(int i) { buf[i] = 5; }",
     "ld (hl),e", "ld (hl),5"),
    ("ld hl,X; ex de,hl: ld de,X", "char buf[9];\nvoid f(int i) { buf[i] = 5; }",
     "ld hl,_buf", "ld de,_buf"),
    ("a store and its reload", "int k(int);\nint f(int a) { int b; b = a + 1; return k(b); }",
     "ld hl,(ix-3)", "ld (ix-3),hl"),
    ("a long into A:UBC without push af", "long f(long x) { return x + 1L; }",
     "push af", "pop bc"),
    ("an ex de,hl nothing reads", "int a[9];\nvoid f(int i, int x, int y) { a[i] = x * y; }",
     "ex de,hl\n\tld sp,ix", "ld (hl),de\n\tld sp,ix"),
    ("adding 0", "long *sp;\nvoid f(void) { sp[-1] += sp[0]; }",
     "ld de,0", "ld hl,(_sp)"),
]


def peephole():
    problems = []
    for name, src, old, new in PEEP_CASES:
        c = o("peep.c")
        open(os.path.join(REPO, c), "w", newline="\n").write(src + "\n")
        r = sh([CC1, c, o("peep.ir")])
        if r.returncode:
            problems.append(f"{name}: cc1 failed: {r.stderr.strip()[:200]}")
            continue
        bodies = []
        for flags in ([], ["-O"]):
            r = sh([CC2, o("peep.ir"), o("peep.s")] + flags)
            if r.returncode:
                problems.append(f"{name}: cc2 {' '.join(flags)} failed: {r.stderr.strip()[:200]}")
                break
            text = open(os.path.join(REPO, o("peep.s"))).read()
            bodies.append(text[text.index("_f:"):])
        if len(bodies) < 2:
            continue
        plain, opt = bodies
        if old not in plain:
            problems.append(f"{name}: the plain code has no {old!r} (the case no longer tests the rule)")
        elif old in opt:
            problems.append(f"{name}: -O left {old!r}")
        if new not in opt:
            problems.append(f"{name}: -O has no {new!r}")
    return problems


def t2_opt():
    return t2(["-O", "-a"], "exec_ann_o")


def t4_opt():
    p = build(o("exec.ir"), "cc2exec_o", ["-O"])
    if p:
        return [p]
    rc = run_bin("cc2exec_o")
    return [] if rc == 0 else [f"{rc} checks failed with -O"]


def t3():
    for stem, flags in (("exec_off", []), ("exec_lbl", ["-fno-offset-branches"])):
        p = build(o("exec.ir"), stem, flags)
        if p:
            return [p]
    a = open(os.path.join(REPO, o("exec_off.bin")), "rb").read()
    b = open(os.path.join(REPO, o("exec_lbl.bin")), "rb").read()
    return [] if a == b else [f"binaries differ ({len(a)} vs {len(b)} bytes)"]


def t4():
    problems = []
    p = build(o("exec.ir"), "cc2exec")
    if p:
        return [p]
    rc = run_bin("cc2exec")
    if rc != 0:
        problems.append(f"{rc} checks failed (rerun gen_exec.py --first to find the first)")
    # guards: break one expectation (check 7, 'mul big') and expect exactly 1 failure
    ir = open(os.path.join(REPO, o("exec.ir"))).read().replace("C 1000000\nNE", "C 999999\nNE", 1)
    open(os.path.join(REPO, o("cc2bad.ir")), "w", newline="\n").write(ir)
    p = build(o("cc2bad.ir"), "cc2bad")
    rc = run_bin("cc2bad") if not p else p
    if rc != 1:
        problems.append(f"guard: one wrong expectation gave {rc}, expected 1")
    gen("cc2first", "--first")
    ir = open(os.path.join(REPO, o("cc2first.ir"))).read().replace("C 1000000\nNE", "C 999999\nNE", 1)
    open(os.path.join(REPO, o("cc2first.ir")), "w", newline="\n").write(ir)
    p = build(o("cc2first.ir"), "cc2first")
    rc = run_bin("cc2first") if not p else p
    if rc != 6:
        problems.append(f"guard: --first reported {rc}, expected check 6 ('mul big')")
    return problems


def s1():
    for b in ("cc2", "ld"):
        if not os.path.exists(os.path.join(REPO, "build", "stage1", b + ".bin")):
            return [f"build/stage1/{b}.bin missing (make stage1)"]
    files = []
    for f in RUNTIME + [o("exec.ir")]:
        files += ["--file", f]
    r = sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin",
            "--bin", os.path.join("build", "stage1", "cc2.bin"),
            "--bin", os.path.join("build", "stage1", "ld.bin"), "--with-ez80asm"] + files +
           ["--cmd", "cc2 exec.ir /exec.s", "--cmd", "ld -o /exec.asm crt0.s rt.s libc.s /exec.s",
            "--cmd", "ez80asm /exec.asm /bin/devexec.bin -m", "--cmd", "devexec",
            "--sdcard-name", "cc2s1", "--timeout", "600"])
    problems = [] if r.returncode == 0 else [f"on-device run gave {r.returncode}, expected 0"]
    dev = os.path.join(REPO, "emulator_sdcard", "cc2s1", "exec.s")
    host = os.path.join(REPO, o("exec_s1_host.s"))
    sh([CC2, o("exec.ir"), host])
    if not os.path.exists(dev):
        problems.append("the device produced no exec.s")
    elif open(dev, "rb").read() != open(host, "rb").read():
        problems.append("device .s differs from the host cc2's")
    return problems


BAD_IR = [
    # (name, IR text, expected message) - each fails after output has begun
    ("unknown record", "IR 2\nU bad 0001\nF f g 0 0 -\nC 1\nRET\nE\nF g g 0 0 -\nFROB\nE\n", "cc2 internal error: unknown record"),
    ("bad label", "IR 2\nU bad 0001\nF f g 0 0 -\nJ 0\nE\n", "cc2 internal error"),
    ("missing ASM line", "IR 2\nU bad 0001\nF f g 0 0 -\nASM 3\nnop\n", "cc2 internal error: missing ASM line"),
]


def errors():
    """A failing cc2 exits 200 and leaves no partial output file."""
    problems = []
    for name, text, want in BAD_IR:
        open(os.path.join(REPO, o("bad.ir")), "w", newline="\n").write(text)
        out = os.path.join(REPO, o("bad.s"))
        if os.path.exists(out):
            os.remove(out)
        r = sh([CC2, o("bad.ir"), o("bad.s")])
        if r.returncode != 200:
            problems.append(f"{name}: status {r.returncode}, expected 200")
        if want not in r.stderr:
            problems.append(f"{name}: expected {want!r}; got {r.stderr.strip()!r}")
        if os.path.exists(out):
            problems.append(f"{name}: a partial output file was left behind")
    return problems


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    if not os.path.exists(os.path.join(REPO, "build", "agon", "lib", "libc.s")):
        print("build/agon/lib/libc.s is missing: run make cross first")
        return 1
    gen("exec")
    cases = [("T1 golden count.ir", lambda: t1("--update" in sys.argv)),
             ("T2 computed offsets == listing", t2),
             (f"P  the peephole pass, rule by rule ({len(PEEP_CASES)} cases)", peephole),
             ("T2O computed offsets == listing, with -O", t2_opt),
             ("T3 offset branches == labelled branches", t3),
             (f"E  errors leave no output ({len(BAD_IR)} cases)", errors)]
    if "--no-emu" not in sys.argv:
        cases.append(("T4 execution (344 checks) + guards", t4))
        cases.append(("T4O execution with -O", t4_opt))
        if "--no-stage1" not in sys.argv:
            cases.append(("S1 AgDev cc2+ld and device ez80asm on the emulator", s1))
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
