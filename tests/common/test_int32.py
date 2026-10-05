"""Tests for src/common/int32.c, the compiler's own 32-bit arithmetic.

    test_int32.py

H  t_int32.c built by the host compiler (int is 32 bits there): every line
   it prints is checked against Python's arithmetic modulo 2^32.
A  the same program built by our own toolchain and run on the emulator
   (int is 24 bits there) prints exactly the same lines: the module gives
   the same answers under both widths.
G  the guard: a line with its result changed is reported.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import EXE, HOSTCC, HOST  # noqa: E402
import testrun  # noqa: E402

OUT = os.path.join("build", "test", "int32")
M = 0xFFFFFFFF


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def expected(op, a, b):
    if op == "add": return (a + b) & M
    if op == "sub": return (a - b) & M
    if op == "mul": return (a * b) & M
    if op == "and": return a & b
    if op == "or": return a | b
    if op == "xor": return a ^ b
    if op == "neg": return -a & M
    if op == "cpl": return ~a & M
    if op == "divu": return a // b
    if op == "remu": return a % b
    if op == "divs": return tdiv(s32(a), s32(b)) & M
    if op == "rems": return (s32(a) - tdiv(s32(a), s32(b)) * s32(b)) & M
    if op == "shl": return (a << b) & M
    if op == "shru": return a >> b
    if op == "shrs": return (s32(a) >> b) & M
    if op == "cmpu": return (a > b) - (a < b) + 1
    if op == "cmps": return (s32(a) > s32(b)) - (s32(a) < s32(b)) + 1
    if op in ("strs", "stru"): return a
    if op == "sext24":
        v = a & 0xFFFFFF
        return (v - (1 << 24) if v & 0x800000 else v) & M
    if op == "high8": return a >> 24
    if op == "join": return a
    if op == "fits":
        return ((a >> 24 == 0) << 16) | (-(1 << 23) <= s32(a) < (1 << 23))
    raise ValueError(op)


def check(text):
    """The problems in a t_int32 transcript."""
    problems = []
    lines = [l for l in text.replace("\r\n", "\n").split("\n") if l]
    count = 0
    last_a = None
    for l in lines:
        f = l.split()
        if f[0] == "text":
            if last_a is not None and f[1] != str(last_a):
                problems.append(f"text {f[1]}, expected {last_a}")
            continue
        op, a, b, r = f[0], int(f[1], 16), int(f[2], 16), int(f[3], 16)
        count += 1
        want = expected(op, a, b)
        if r != want:
            problems.append(f"{op} {f[1]} {f[2]}: {f[3]}, expected {want:08x}")
        if op == "stru":
            last_a = a
    if count < 200 * 20:
        problems.append(f"only {count} operations checked")
    return problems[:10]


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    cc = HOSTCC
    exe = os.path.join(REPO, OUT, "t_int32" + EXE)
    failed = 0
    r = subprocess.run([cc, "-std=c89", "-Wall", "-Werror", "-fsigned-char", "-I", os.path.join(REPO, "src", "common"),
                        "-o", exe, os.path.join(REPO, "tests", "common", "t_int32.c"),
                        os.path.join(REPO, "src", "common", "int32.c"), os.path.join(REPO, "src", "common", "int24.c")],
                       capture_output=True, text=True)
    host_out = ""
    if r.returncode:
        problems = ["host build: " + r.stderr.strip()[:300]]
    else:
        host_out = subprocess.run([exe], capture_output=True, text=True).stdout
        problems = check(host_out)
    print(("PASS  " if not problems else "FAIL  ") + "H  host build: every result matches Python")
    for p in problems:
        print("      " + p)
    failed += bool(problems)

    rt = testrun.runtime()
    err = testrun.build(rt, "t_int32", [os.path.join("tests", "common", "t_int32.c"),
                                        os.path.join("src", "common", "int32.c"),
                                        os.path.join("src", "common", "int24.c")], OUT,
                        flags=["-I", os.path.join("src", "common")])
    if err:
        problems = ["build: " + err[:300]]
    else:
        res = testrun.run([testrun.Program("t_int32", os.path.join(OUT, "t_int32.bin"))], "int32", timeout_each=120)
        got = res["t_int32"]
        if got.status != 0:
            problems = [f"status {got.status}"]
        else:
            problems = check(got.output.decode("latin-1"))
            if not problems and got.output.replace(b"\r\n", b"\n") != host_out.encode():
                problems = ["the emulator's output differs from the host's"]
    print(("PASS  " if not problems else "FAIL  ") + "A  built by agonc, on the emulator: the same lines")
    for p in problems:
        print("      " + p)
    failed += bool(problems)

    guard = host_out.replace("add 00000000 ", "add 00000000 ", 1)
    first = [l for l in host_out.split("\n") if l.startswith("mul ")][0]
    f = first.split()
    wrong = host_out.replace(first, f"mul {f[1]} {f[2]} {(int(f[3], 16) ^ 1):08x}", 1)
    gp = check(wrong)
    print(("PASS  " if gp else "FAIL  ") + "G  a changed result is reported")
    failed += not gp
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
