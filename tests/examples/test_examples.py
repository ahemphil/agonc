"""Tests for examples/: the programs a newcomer reads first must keep
building and keep working.

    test_examples.py

B   every examples/*.c builds with the host driver as a user would build it
    (all the standard libraries), with -Werror.
H   hello.c, files.c and numbers.c built by the PC's C compiler (HOSTCC)
    and run in a scratch folder: their output is the reference.
A   the same three built by agonc against the test runtime and run on the
    emulator: each prints exactly the PC's output (numbers.c's sizeof line
    excepted, which gives the Agon's sizes) and exits 0.
G   the guard: the comparison with one character changed must fail.

graphics.c, sound.c and serial.c need eyes, ears or a cable: they are
built here and tried on an Agon by hand.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import EXE, HOSTCC  # noqa: E402
import testrun  # noqa: E402

EXAMPLES = os.path.join("examples")
OUT = os.path.join("build", "test", "examples")
PORTABLE = ["hello", "files", "numbers"]
AGON_SIZES = b"sizeof: int 3, long 4, long long 8, double 8\n"


def report(name, problems):
    print(("PASS  " if not problems else "FAIL  ") + name)
    for p in problems:
        print("      " + p)
    return bool(problems)


def host_output(name, scratch):
    """Build examples/<name>.c for the PC and run it in scratch; returns its
    output (CR LF made LF), or raises RuntimeError."""
    exe = os.path.join(scratch, name + EXE)
    std = "-std=c99" if name == "numbers" else "-std=c89"
    r = subprocess.run([HOSTCC, std, "-pedantic", "-Wall", "-Werror", "-O1", "-ffp-contract=off", "-o", exe,
                        os.path.join(REPO, EXAMPLES, name + ".c"), "-lm"], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"{name}.c on the PC: {(r.stderr or r.stdout).strip()[:300]}")
    r = subprocess.run([exe], cwd=scratch, capture_output=True)
    if r.returncode:
        raise RuntimeError(f"{name} on the PC exited {r.returncode}")
    out = r.stdout.replace(b"\r\n", b"\n")
    if name == "numbers":
        lines = out.split(b"\n")
        lines[-2] = AGON_SIZES.rstrip(b"\n")       # the PC's int is 4 bytes
        out = b"\n".join(lines)
    return out


def main():
    failed = 0
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    names = sorted(f[:-2] for f in os.listdir(os.path.join(REPO, EXAMPLES)) if f.endswith(".c"))

    problems = []
    for name in names:
        r = testrun.driver(["-Werror", "-o", os.path.join(OUT, name + ".bin"), os.path.join(EXAMPLES, name + ".c")])
        if r.returncode:
            problems.append(f"{name}.c: {(r.stderr or r.stdout).strip()[:300]}")
    failed += report(f"B  every example builds with -Werror ({len(names)})", problems)

    problems = []
    want = {}
    scratch = tempfile.mkdtemp(prefix="agonc_examples_")
    try:
        for name in PORTABLE:
            try:
                want[name] = host_output(name, scratch)
            except RuntimeError as e:
                problems.append(str(e))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    for name in want:
        if not want[name]:
            problems.append(f"{name} printed nothing on the PC")
    failed += report("H  hello, files and numbers on the PC", problems)
    if problems:
        print(f"{failed} failed")
        return 1

    problems = []
    rt = testrun.runtime()
    programs = []
    for name in PORTABLE:
        err = testrun.build(rt, "ex_" + name, [os.path.join(EXAMPLES, name + ".c")], OUT)
        if err:
            problems.append(f"{name}.c against the test runtime: {err[:300]}")
        else:
            programs.append(testrun.Program("ex_" + name, os.path.join(OUT, "ex_" + name + ".bin")))
    results = testrun.run(programs, "examples", timeout_each=60) if not problems else {}
    for name in PORTABLE:
        res = results.get("ex_" + name)
        if res is None:
            continue
        if not testrun.expected_ok(res, 0, want[name]):
            got = res.output.split(b"\n")
            exp = want[name].split(b"\n")
            k = next((i for i in range(min(len(got), len(exp))) if got[i] != exp[i]), min(len(got), len(exp)))
            problems.append(f"{name}: status {res.status}; first difference at line {k + 1}: "
                            f"got {got[k] if k < len(got) else b'(end)'!r}, "
                            f"want {exp[k] if k < len(exp) else b'(end)'!r}")
    failed += report("A  the same on the emulator, output equal to the PC's", problems)

    res = results.get("ex_numbers")
    wrong = want["numbers"].replace(b"20! = ", b"20! = 1", 1)
    problems = [] if res is not None and not testrun.expected_ok(res, 0, wrong) else \
        ["a changed expectation still compared equal"]
    failed += report("G  guard: one changed character is noticed", problems)

    print(f"{failed} failed")
    return 1 if failed else 0


sys.exit(main())
