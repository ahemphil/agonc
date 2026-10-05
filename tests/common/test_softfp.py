"""Tests for src/cc1/softfp.c, the IEEE 754 arithmetic that cc1 folds
constants with and the C library's floating-point helpers are (M13).

    test_softfp.py

H  t_softfp_host.c, built by the host compiler (contraction off): every
   operation's result on 300,000 random and edge-case operands equals the
   PC's own IEEE arithmetic, bit for bit (a NaN matches any NaN).
G  the guard: with one expected result made wrong, H reports it.
L  t_softfp64_host.c (C99, for long long): the conversions between the
   floating types and 64-bit integers equal the PC's, on 300,000 integers
   of every size and sign and doubles in range, and the saturating edges;
   with a guard.
A  t_softfp.c built by the host compiler and by agonc (on the emulator,
   where int is 24 bits) prints the same checksums for the same operands:
   the module computes the same bits under both.
D  decimal conversion (t_decimal.c against gen_decimal.py's exact
   answers): 6,000 decimals to binary64 and to binary32 (shortest and long
   forms, random strings, ties and near-ties, the ends of the range), and
   1,500 doubles formatted %.Pe and %.Pf, on the host; and on the
   emulator, built by agonc, 600 lines with decimals of up to 40 digits.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import EXE, HOSTCC  # noqa: E402
import testrun  # noqa: E402

OUT = os.path.join("build", "test", "softfp")
CASES = "300000"
DEVICE_CASES = "2000"
FLAGS = ["-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsigned-char", "-O2",
         "-ffp-contract=off"]


def host_build(cc, name, sources):
    exe = os.path.join(REPO, OUT, name + EXE)
    r = subprocess.run([cc] + FLAGS + ["-I", os.path.join(REPO, "src", "cc1"), "-o", exe] +
                       [os.path.join(REPO, s) for s in sources], capture_output=True, text=True)
    return exe, (r.stderr.strip()[:300] if r.returncode else None)


def report(name, problems):
    print(("PASS  " if not problems else "FAIL  ") + name)
    for p in problems:
        print("      " + p)
    return bool(problems)


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    cc = HOSTCC
    failed = 0
    exe, err = host_build(cc, "t_softfp_host", ["tests/common/t_softfp_host.c", "src/cc1/softfp.c"])
    if err:
        problems = ["host build: " + err]
        guard = ["not run"]
    else:
        r = subprocess.run([exe, CASES], capture_output=True, text=True)
        lines = r.stdout.splitlines()
        problems = [] if r.returncode == 0 and "total mismatches 0" in lines else \
            [l.strip() for l in lines if not l.endswith(" 0")][:12] or [f"status {r.returncode}"]
        g = subprocess.run([exe, "1000", "guard"], capture_output=True, text=True)
        guard = [] if g.returncode != 0 and "dadd 1000 1" in g.stdout.splitlines() else ["the wrong result went unreported"]
    failed += report(f"H  host build: every operation equals the PC's arithmetic ({CASES} cases each)", problems)
    failed += report("G  a wrong expected result is reported", guard)

    # the conversions to and from 64-bit integers, against the PC's long long (C99)
    exe = os.path.join(REPO, OUT, "t_softfp64_host" + EXE)
    r = subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-O2", "-ffp-contract=off", "-I",
                        os.path.join(REPO, "src", "cc1"), "-o", exe, os.path.join(REPO, "tests", "common", "t_softfp64_host.c"),
                        os.path.join(REPO, "src", "cc1", "softfp.c")], capture_output=True, text=True)
    if r.returncode:
        problems = ["host build: " + r.stderr.strip()[:300]]
    else:
        r = subprocess.run([exe, CASES], capture_output=True, text=True)
        g = subprocess.run([exe, "1000", "guard"], capture_output=True, text=True)
        problems = ([] if r.returncode == 0 else r.stdout.strip().splitlines()[:10]) + \
                   ([] if g.returncode != 0 else ["the guard's wrong result went unreported"])
    failed += report(f"L  conversions to and from 64-bit integers equal the PC's ({CASES} cases each), and a guard",
                     problems)

    exe, err = host_build(cc, "t_softfp", ["tests/common/t_softfp.c", "src/cc1/softfp.c"])
    host_out = None
    if err:
        problems = ["host build: " + err]
    else:
        host_out = subprocess.run([exe, DEVICE_CASES], capture_output=True, text=True).stdout
        rt = testrun.runtime()
        err = testrun.build(rt, "t_softfp", [os.path.join("tests", "common", "t_softfp.c"),
                                             os.path.join("src", "cc1", "softfp.c")], OUT,
                            flags=["-I", os.path.join("src", "cc1")])
        if err:
            problems = ["agonc build: " + err[:300]]
        else:
            res = testrun.run([testrun.Program("t_softfp", os.path.join(OUT, "t_softfp.bin"), [DEVICE_CASES])],
                              "softfp", timeout_each=600)
            got = res["t_softfp"]
            dev = got.output.decode("latin-1").replace("\r\n", "\n") if got.output else ""
            if got.status != 0:
                problems = [f"status {got.status}"]
            elif dev != host_out or dev.count("\n") != 13:
                problems = ["the emulator's checksums differ from the host's:"] + \
                           [f"host {h!r} / device {d!r}" for h, d in zip(host_out.splitlines(), dev.splitlines())
                            if h != d][:6]
            else:
                problems = []
    failed += report(f"A  built by agonc, on the emulator: the host's checksums ({DEVICE_CASES} cases each)", problems)

    vectors = os.path.join(OUT, "vectors.txt")
    subprocess.run([sys.executable, os.path.join(HERE, "gen_decimal.py"), os.path.join(REPO, vectors)], check=True)
    exe, err = host_build(cc, "t_decimal", ["tests/common/t_decimal.c", "src/cc1/softfp.c"])
    if err:
        problems = ["host build: " + err]
    else:
        r = subprocess.run([exe, os.path.join(REPO, vectors)], capture_output=True, text=True)
        problems = [] if r.returncode == 0 else r.stdout.strip().splitlines()[:12]
    failed += report("D  decimal to binary64 and binary32, and printf's digits, on the host", problems)
    err = testrun.build(rt, "t_decimal", [os.path.join("tests", "common", "t_decimal.c"),
                                          os.path.join("src", "cc1", "softfp.c")], OUT,
                        flags=["-I", os.path.join("src", "cc1")]) if host_out is not None else "not built"
    if err:
        problems = ["agonc build: " + err[:300]]
    else:
        res = testrun.run([testrun.Program("t_decimal", os.path.join(OUT, "t_decimal.bin"), ["vectors.txt", "600", "40"],
                                           [("vectors.txt", vectors)])], "decimal", timeout_each=600)
        got = res["t_decimal"]
        text = got.output.decode("latin-1") if got.output else ""
        problems = [] if got.status == 0 and "D " in text else [f"status {got.status}"] + text.splitlines()[:10]
    failed += report("D  the same, built by agonc, on the emulator (600 lines, decimals up to 40 digits)", problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
