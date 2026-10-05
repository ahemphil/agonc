"""Tests for <math.h> (lib/libc/math.c).

    test_math.py

H   the PC's build of math.c (math_names.h renames its functions) against
    the PC's libm, t_math_host.c: random arguments over each function's
    range; the exact functions agree exactly, the rest within 1 ulp (2 for
    sinh, cosh, tanh and log10).
G   the same with no tolerance must fail: a difference is reported.
A   t_math.c, built by the PC and by agonc (on the emulator): the same
    checksum of every function's results and errno over the same
    arguments, and the special cases (values and errno) pass in both.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import EXE, HOSTCC  # noqa: E402
import testrun  # noqa: E402

OUT = os.path.join("build", "test", "math")
CASES = "300000"
DEVICE_CASES = "40"
C89 = ["-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsigned-char", "-O2",
       "-ffp-contract=off"]
RENAMED = ["-include", os.path.join(REPO, "tests", "common", "math_names.h"), "-Wno-static-in-inline"]


def cc_run(cc, args):
    r = subprocess.run([cc] + args, capture_output=True, text=True)
    return r.stderr.strip()[:300] if r.returncode else None


def report(name, problems):
    print(("PASS  " if not problems else "FAIL  ") + name)
    for p in problems:
        print("      " + p)
    return bool(problems)


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    cc = HOSTCC
    o = lambda name: os.path.join(REPO, OUT, name)
    src = lambda path: os.path.join(REPO, path)
    failed = 0
    err = cc_run(cc, C89 + RENAMED + ["-c", "-o", o("math.o"), src("lib/libc/math.c")]) or \
        cc_run(cc, C89 + ["-c", "-o", o("softfp.o"), src("src/cc1/softfp.c")]) or \
        cc_run(cc, ["-std=c99", "-Wall", "-Werror", "-O2", "-ffp-contract=off", "-o", o("t_math_host" + EXE),
                    src("tests/common/t_math_host.c"), o("math.o"), o("softfp.o")])
    if err:
        problems = ["host build: " + err]
        guard = ["not run"]
    else:
        r = subprocess.run([o("t_math_host" + EXE), CASES], capture_output=True, text=True)
        problems = [] if r.returncode == 0 and "0 functions over" in r.stdout else \
            [l for l in r.stdout.splitlines() if not l.endswith("over 0")][:12] or [f"status {r.returncode}"]
        g = subprocess.run([o("t_math_host" + EXE), "1000", "guard"], capture_output=True, text=True)
        guard = [] if g.returncode != 0 and "sin        max 1, over" in g.stdout else ["the differences went unreported"]
    failed += report(f"H  the PC's build against the PC's libm ({CASES} cases each)", problems)
    failed += report("G  with no tolerance, a difference is reported", guard)

    err = err or cc_run(cc, C89 + RENAMED + ["-o", o("t_math_pc" + EXE), src("tests/common/t_math.c"), o("math.o"),
                                             o("softfp.o")])
    if err:
        problems = ["host build: " + err]
    else:
        r = subprocess.run([o("t_math_pc" + EXE), DEVICE_CASES], capture_output=True, text=True)
        host_out = r.stdout
        if r.returncode != 0:
            problems = ["the PC's build: " + l for l in host_out.splitlines() if "failed" in l][:8]
        else:
            rt = testrun.runtime()
            err = testrun.build(rt, "t_math", [os.path.join("tests", "common", "t_math.c")], OUT)
            if err:
                problems = ["agonc build: " + err[:300]]
            else:
                res = testrun.run([testrun.Program("t_math", os.path.join(OUT, "t_math.bin"), [DEVICE_CASES])],
                                  "math", timeout_each=900)
                got = res["t_math"]
                dev = got.output.decode("latin-1").replace("\r\n", "\n") if got.output else ""
                if got.status != 0 or dev != host_out:
                    problems = [f"status {got.status}"] + \
                               [f"PC {h!r} / Agon {d!r}" for h, d in zip(host_out.splitlines(), dev.splitlines())
                                if h != d][:8]
                else:
                    problems = []
    failed += report(f"A  built by agonc, on the emulator: the PC's checksums ({DEVICE_CASES} cases each) and "
                     "the special cases", problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
