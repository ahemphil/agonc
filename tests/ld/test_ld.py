"""T1 tests for ld, plus one T4 run of a program linked against the runtime.

    test_ld.py [--update] [--no-emu] [--exe path/to/ld.exe]

Each case runs ld and checks its exit status and either a golden .asm
(cases/<name>.asm.golden) or one expected diagnostic line on stderr. On any
error ld must not leave an output file behind. --update rewrites the golden
files from the current output (review the diff before committing).

The index cases (object_format.md section 9) index copies of the cases
and the runtime with --index, link through the indexes, and want the
same output as without them; a library whose size has changed is read
instead of its index, and an index whose sections are not where it says
is an error. The stage-1 case also indexes libc.s on the device.
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CASES = os.path.join(HERE, "cases")
OUT = os.path.join(REPO, "build", "test", "ld")
PY = sys.executable

RUNTIME = [os.path.join(REPO, p) for p in (
    r"lib\rt\crt0.s", r"lib\rt\rt.s", r"build\agon\lib\agonc\libc.s")]


def c(name):
    return os.path.join(CASES, name)


# name, arguments (OUT_ASM is replaced), expected status, golden or error text
CASES_LIST = [
    ("basic", ["-v", "-o", "OUT_ASM", c("basic_a.s"), c("basic_b.s")], 0, "golden:basic"),
    ("response_file", ["-o", "OUT_ASM", "@" + c("basic.rsp")], 0, "golden:basic"),
    ("entry_selects_more", ["-o", "OUT_ASM", "--entry=_unused", c("basic_a.s"), c("basic_b.s")], 200,
     "ld: error: undefined symbol _missing (referenced from unit a.c, section _unused)"),
    ("undefined", ["-o", "OUT_ASM", c("err_undef.s")], 200,
     "ld: error: undefined symbol _nothere (referenced from unit c.c, section __start)"),
    ("duplicate", ["-o", "OUT_ASM", c("basic_a.s"), c("err_dup.s")], 200,
     "err_dup.s:3: error: duplicate definition of _main (unit a.c section _main, and unit c.c section _main)"),
    ("duplicate_bare_label", ["-o", "OUT_ASM", c("basic_a.s"), c("basic_b.s"), c("err_label.s")], 200,
     "err_label.s:6: error: duplicate definition of _odd (unit a.c section _odd, and unit c.c section __start)"),
    ("bad_unit_id", ["-o", "OUT_ASM", c("err_badid.s")], 200,
     "err_badid.s:2: error: unit c.c has id 1234, but its name hashes to 8df5"),
    ("unknown_marker", ["-o", "OUT_ASM", c("err_marker.s")], 200,
     "err_marker.s:4: error: unknown marker ;;bogus"),
    ("at_outside_index", ["-o", "OUT_ASM", c("err_at.s")], 200,
     "err_at.s:4: error: misplaced or malformed ;;at"),
    ("no_start", ["-o", "OUT_ASM", c("err_nostart.s")], 200,
     "ld: error: no __start code section (is crt0.s missing?)"),
    ("section_before_unit", ["-o", "OUT_ASM", c("err_nounit.s")], 200,
     "err_nounit.s:2: error: section before any ;;unit line"),
    ("layout_symbol", ["-o", "OUT_ASM", c("err_layout.s")], 200,
     "err_layout.s:3: error: symbol is reserved for ld's layout: __image_end"),
    ("implicit_long", ["-o", "OUT_ASM", c("err_implicit.s")], 200,
     "ld: error: implicit call to _getl, which returns long (or float) (unit c.c, section __start)"),
    ("implicit_struct", ["-o", "OUT_ASM", c("err_implicit.s")], 200,
     "ld: error: implicit call to _gets, which returns double, a structure or a union (unit c.c, section __start)"),
    ("bad_ret", ["-o", "OUT_ASM", c("err_ret.s")], 200, "err_ret.s:4: error: misplaced or malformed ;;ret"),
    ("weak", ["-o", "OUT_ASM", c("weak_a.s"), c("weak_b.s")], 0, "golden:weak"),
    ("bad_wref", ["-o", "OUT_ASM", c("err_wref.s")], 200, "err_wref.s:4: error: misplaced or malformed ;;wref"),
    ("no_inputs", ["-o", "OUT_ASM"], 200, "ld: no input files"),
    ("unknown_option", ["-q", c("basic_a.s")], 200, "ld: unknown option -q"),
]


def run_case(exe, name, args, want_status, want, update):
    out_asm = os.path.join(OUT, name + ".asm")
    if os.path.exists(out_asm):
        os.remove(out_asm)
    args = [out_asm if a == "OUT_ASM" else a for a in args]
    r = subprocess.run([exe] + args, capture_output=True, text=True, cwd=CASES)
    problems = []
    if r.returncode != want_status:
        problems.append(f"status {r.returncode}, expected {want_status}")
    if want.startswith("golden:"):
        golden = c(want[7:] + ".asm.golden")
        if not os.path.exists(out_asm):
            problems.append("no output file")
        else:
            with open(out_asm, encoding="ascii") as f:
                got = f.read()
            if update and name == want[7:]:
                with open(golden, "w", encoding="ascii", newline="\n") as f:
                    f.write(got)
            with open(golden, encoding="ascii") as f:
                if f.read() != got:
                    problems.append(f"output differs from {os.path.basename(golden)}")
    else:
        lines = [l.replace(CASES + os.sep, "") for l in r.stderr.splitlines()]
        if want not in lines:
            problems.append(f"missing diagnostic: {want!r}; got {r.stderr.strip()!r}")
        if os.path.exists(out_asm):
            problems.append("output file left behind after an error")
    return problems


def link(exe, out_asm, files, verbose=False):
    if os.path.exists(out_asm):
        os.remove(out_asm)
    return subprocess.run([exe] + (["-v"] if verbose else []) + ["-o", out_asm] + files,
                          capture_output=True, text=True)


def read(path):
    return open(path, "rb").read() if os.path.exists(path) else None


def index_cases(exe):
    """Links through indexes: (name, problems) for each case."""
    d = os.path.join(OUT, "idx")
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    for f in RUNTIME + [os.path.join(HERE, "hello.s")] + \
            [c(n) for n in ("basic_a.s", "basic_b.s", "weak_a.s", "weak_b.s", "err_implicit.s")]:
        shutil.copy(f, d)
    p = lambda n: os.path.join(d, n)
    rt = [p("crt0.s"), p("rt.s"), p("hello.s"), p("libc.s")]
    results = []

    # the runtime and libc without and with their indexes (rt.s has comments: copied line by line)
    problems = []
    r = link(exe, p("plain.asm"), rt)
    if r.returncode:
        problems.append(f"plain link: {r.stderr.strip()}")
    r = subprocess.run([exe, "--index", p("crt0.s"), p("libc.s"), p("rt.s")], capture_output=True, text=True)
    if r.returncode or not all(os.path.exists(p(n)) for n in ("crt0.idx", "rt.idx", "libc.idx")):
        problems.append(f"--index: status {r.returncode} {r.stderr.strip()}")
    r = link(exe, p("indexed.asm"), rt, verbose=True)
    if "ld: 3 of 4 inputs read through an index" not in r.stdout:
        problems.append(f"indexes not used: {r.stdout.strip()[-200:]}")
    if read(p("indexed.asm")) != read(p("plain.asm")) or read(p("plain.asm")) is None:
        problems.append("output through the indexes differs from the output without them")
    # indexing several files at once gives each the index it would get alone
    alone = os.path.join(d, "alone")
    os.makedirs(alone)
    for n in ("rt.s", "libc.s"):
        shutil.copy(p(n), alone)
        subprocess.run([exe, "--index", os.path.join(alone, n)], capture_output=True)
        if read(os.path.join(alone, n[:-2] + ".idx")) != read(p(n[:-2] + ".idx")):
            problems.append(f"{n}'s index differs when made with other files")
    results.append(("index_runtime (same output through crt0.idx, rt.idx, libc.idx)", problems))

    # the cases' labels, weak references, comments and implicit calls through indexes
    problems = []
    r = subprocess.run([exe, "--index"] + [p(n) for n in ("basic_a.s", "basic_b.s", "weak_a.s", "weak_b.s",
                                                       "err_implicit.s")], capture_output=True, text=True)
    if r.returncode:
        problems.append(f"--index: status {r.returncode} {r.stderr.strip()}")
    for name, files in (("basic", ["basic_a.s", "basic_b.s"]), ("weak", ["weak_a.s", "weak_b.s"])):
        r = link(exe, p(name + ".asm"), [p(f) for f in files], verbose=True)
        if "ld: 2 of 2 inputs read through an index" not in r.stdout:
            problems.append(f"{name}: indexes not used")
        if read(p(name + ".asm")) != read(c(name + ".asm.golden")):
            problems.append(f"{name}: output differs from {name}.asm.golden")
    r = link(exe, p("implicit.asm"), [p("err_implicit.s")])
    want = "ld: error: implicit call to _getl, which returns long (or float) (unit c.c, section __start)"
    if r.returncode != 200 or want not in r.stderr.splitlines():
        problems.append(f"implicit call through an index: {r.returncode} {r.stderr.strip()[:200]}")
    results.append(("index_cases (basic, weak and an implicit call through indexes)", problems))

    # --if-needed: read when something is still undefined, else never opened
    problems = []
    r = link(exe, p("lazy_basic.asm"), [p("basic_a.s"), "--if-needed", p("basic_b.s")], verbose=True)
    if "ld: 1 of 1 --if-needed libraries read" not in r.stdout:
        problems.append(f"basic_b.s not read though needed: {r.stdout.strip()[-200:]} {r.stderr.strip()[:200]}")
    if read(p("lazy_basic.asm")) != read(c("basic.asm.golden")):
        problems.append("output with basic_b.s --if-needed differs from basic.asm.golden")
    r = link(exe, p("lazy_none.asm"), rt + ["--if-needed", p("nonexistent.s")], verbose=True)
    if r.returncode or "ld: 0 of 1 --if-needed libraries read" not in r.stdout:
        problems.append(f"an unneeded library was read: {r.returncode} {r.stdout.strip()[-200:]} {r.stderr.strip()[:200]}")
    if read(p("lazy_none.asm")) != read(p("plain.asm")):
        problems.append("output with an unneeded --if-needed library differs")
    results.append(("if_needed (read only when a strong reference is undefined)", problems))

    # libc.s grown by a line: its index is ignored, and the output is the same
    problems = []
    saved = read(p("libc.s"))
    open(p("libc.s"), "ab").write(b"; a comment\n")
    r = link(exe, p("stale.asm"), rt, verbose=True)
    if "does not match" not in r.stdout or "ld: 2 of 4 inputs read through an index" not in r.stdout:
        problems.append(f"the stale index was used: {r.stdout.strip()[-200:]}")
    if read(p("stale.asm")) != read(p("plain.asm")):
        problems.append("output with a stale index differs")
    results.append(("index_stale (a changed size: the library is read instead)", problems))

    # the same size but shifted by a byte: an error, and no output
    problems = []
    i = saved.index(b";;sect code _isupper")
    open(p("libc.s"), "wb").write(saved[:i - 1] + saved[i:] + b"\n")
    r = link(exe, p("shifted.asm"), rt)
    lines = [l for l in r.stderr.splitlines() if "does not match" in l]
    if r.returncode != 200 or len(lines) != 1 or not lines[0].startswith("ld: error: "):
        problems.append(f"status {r.returncode}, {r.stderr.strip()[:300]!r}")
    if os.path.exists(p("shifted.asm")):
        problems.append("output file left behind after an error")
    open(p("libc.s"), "wb").write(saved)
    results.append(("index_mismatch (sections moved, same size: one error)", problems))
    return results


def emulator_case(exe):
    out_asm = os.path.join(OUT, "hello.asm")
    out_bin = os.path.join(OUT, "hello.bin")
    r = subprocess.run([exe, "-o", out_asm] + RUNTIME + [os.path.join(HERE, "hello.s")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return [f"link failed: {r.stderr.strip()}"]
    # ez80asm rejects long file names, so give it repo-relative paths
    r = subprocess.run([os.path.join(REPO, "third_party", "bin", "ez80asm.exe"),
                        os.path.relpath(out_asm, REPO), os.path.relpath(out_bin, REPO)],
                       capture_output=True, text=True, cwd=REPO)
    if r.returncode != 0:
        return [f"assembly failed: {r.stdout.strip()} {r.stderr.strip()}"]
    r = subprocess.run([PY, os.path.join(REPO, "tests", "tools", "run_emulator.py"), "--bin", out_bin,
                        "--cmd", "hello", "--sdcard-name", "ld", "--timeout", "30"],
                       capture_output=True, text=True)
    return [] if r.returncode == 42 else [f"emulator exit {r.returncode}, expected 42"]


def stage1_case():
    """The AgDev-built ld linking the runtime on the emulator, the on-device
    ez80asm assembling the result, and the program running: exit 42, with
    the device's .asm byte-identical to the host ld's."""
    ld_bin = os.path.join(REPO, "build", "stage1", "ld.bin")
    if not os.path.exists(ld_bin):
        return ["build/stage1/ld.bin missing (make stage1)"]
    files = []
    for f in RUNTIME + [os.path.join(HERE, "hello.s")]:
        files += ["--file", f]
    names = " ".join(os.path.basename(f) for f in RUNTIME) + " hello.s"
    r = subprocess.run([PY, os.path.join(REPO, "tests", "tools", "run_emulator.py"), "--hold-stdin",
                        "--bin", ld_bin, "--with-ez80asm"] + files +
                       ["--cmd", "ld --index libc.s",
                        "--cmd", "ld -o /hello.asm " + names, "--cmd", "ez80asm /hello.asm /bin/hello.bin -m",
                        "--cmd", "hello", "--sdcard-name", "ldstage1", "--timeout", "300"],
                       capture_output=True, text=True)
    problems = [] if r.returncode == 42 else [f"emulator exit {r.returncode}, expected 42"]
    dev = os.path.join(REPO, "emulator_sdcard", "ldstage1", "hello.asm")
    host = os.path.join(OUT, "hello.asm")
    if not os.path.exists(dev):
        problems.append("the device produced no hello.asm")
    elif open(dev, "rb").read() != open(host, "rb").read():
        problems.append("device .asm differs from the host ld's")
    # the device's index records libc.s's size (AgDev's fseek and ftell), so the link read it
    idx = os.path.join(REPO, "emulator_sdcard", "ldstage1", "libc.idx")
    first = open(idx, "rb").readline().decode("ascii", "replace").split() if os.path.exists(idx) else []
    if first != [";;agonc-index", "1", str(os.path.getsize(RUNTIME[2]))]:
        problems.append(f"the device's libc.idx starts {first}")
    return problems


def main():
    update = "--update" in sys.argv
    exe = os.path.join(REPO, "build", "host", "ld.exe")
    if "--exe" in sys.argv:
        exe = sys.argv[sys.argv.index("--exe") + 1]
    os.makedirs(OUT, exist_ok=True)
    failed = 0
    for name, args, status, want in CASES_LIST:
        problems = run_case(exe, name, args, status, want, update)
        print(("PASS  " if not problems else "FAIL  ") + name)
        for p in problems:
            print("      " + p)
        failed += bool(problems)
    for name, problems in index_cases(exe):
        print(("PASS  " if not problems else "FAIL  ") + name)
        for p in problems:
            print("      " + p)
        failed += bool(problems)
    if "--no-emu" not in sys.argv:
        if not os.path.exists(RUNTIME[2]):
            print("build/agon/lib/agonc/libc.s is missing: run make cross first")
            return 1
        problems = emulator_case(exe)
        print(("PASS  " if not problems else "FAIL  ") + "emulator_hello (runtime link, exit 42)")
        for p in problems:
            print("      " + p)
        failed += bool(problems)
        problems = stage1_case()
        print(("PASS  " if not problems else "FAIL  ") + "stage1_on_device (AgDev ld + device ez80asm, exit 42, .asm == host)")
        for p in problems:
            print("      " + p)
        failed += bool(problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
