"""Tests for src/common/int64.c, the 64-bit arithmetic behind long long.

    test_int64.py

H  t_int64.c built by the host compiler: every line it prints is checked
   against Python's arithmetic modulo 2^64 (HOST_ROUNDS rounds).
A  the same program built by our own toolchain and run on the emulator
   (int 24 bits, long 32) prints exactly the host's lines (DEVICE_ROUNDS).
G  the guard: a line with its result changed is reported.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import EXE, HOSTCC  # noqa: E402
import testrun  # noqa: E402

OUT = os.path.join("build", "test", "int64")
M = (1 << 64) - 1
HOST_ROUNDS = "20000"
DEVICE_ROUNDS = "120"


def s64(v):
    return v - (1 << 64) if v >> 63 else v


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def expected(op, a, b):
    """The results a line of op must show, as a tuple."""
    if op == "add": return ((a + b) & M,)
    if op == "sub": return ((a - b) & M,)
    if op == "mul": return ((a * b) & M,)
    if op == "and": return (a & b,)
    if op == "or": return (a | b,)
    if op == "xor": return (a ^ b,)
    if op == "neg": return (-a & M,)
    if op == "cpl": return (~a & M,)
    if op in ("divu", "divsmall"): return (a // b, a % b)
    if op == "divs":
        q = tdiv(s64(a), s64(b))
        return (q & M, (s64(a) - q * s64(b)) & M)
    if op == "shl": return ((a << b) & M,)
    if op == "shru": return (a >> b,)
    if op == "shrs": return ((s64(a) >> b) & M,)
    if op == "cmpu": return (((a > b) - (a < b)) & M,)
    if op == "cmps": return (((s64(a) > s64(b)) - (s64(a) < s64(b))) & M,)
    if op == "muladd":
        v = a * (b & 0xFFFF) + (b >> 16 & 0xFFFF)
        return (v & M, int(v > M))
    raise ValueError(op)


ONE = ("neg", "cpl")


def check(text, rounds):
    """The problems in a t_int64 transcript."""
    problems = []
    lines = [l for l in text.replace("\r\n", "\n").split("\n") if l]
    count = 0
    for l in lines:
        f = l.split()
        if f[0] in ("strs", "stru"):
            v = int(f[2], 16)
            want = str(s64(v)) if f[0] == "strs" else str(v)
            if f[1] != want:
                problems.append(f"{l}: expected {want}")
            count += 1
            continue
        if f[0] == "parse":
            problems.append(l)
            continue
        op = f[0]
        a = int(f[1], 16)
        b = 0 if op in ONE else int(f[2], 16)
        got = tuple(int(x, 16) for x in f[2 if op in ONE else 3:])
        want = expected(op, a, b)
        count += 1
        if got != want:
            problems.append(f"{l}: expected " + " ".join(f"{w:016x}" for w in want))
    if count < rounds * 18:
        problems.append(f"only {count} operations checked")
    return problems[:10]


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    cc = HOSTCC
    exe = os.path.join(REPO, OUT, "t_int64" + EXE)
    failed = 0
    r = subprocess.run([cc, "-std=c89", "-pedantic", "-Wall", "-Werror", "-fsigned-char", "-I",
                        os.path.join(REPO, "src", "common"), "-o", exe, os.path.join(REPO, "tests", "common", "t_int64.c"),
                        os.path.join(REPO, "src", "common", "int64.c")], capture_output=True, text=True)
    if r.returncode:
        problems = ["host build: " + r.stderr.strip()[:300]]
        host_small = ""
    else:
        problems = check(subprocess.run([exe, HOST_ROUNDS], capture_output=True, text=True).stdout, int(HOST_ROUNDS))
        host_small = subprocess.run([exe, DEVICE_ROUNDS], capture_output=True, text=True).stdout
    print(("PASS  " if not problems else "FAIL  ") + f"H  host build: every result matches Python ({HOST_ROUNDS} rounds)")
    for p in problems:
        print("      " + p)
    failed += bool(problems)

    rt = testrun.runtime()
    err = testrun.build(rt, "t_int64", [os.path.join("tests", "common", "t_int64.c"),
                                        os.path.join("src", "common", "int64.c")], OUT,
                        flags=["-I", os.path.join("src", "common")])
    if err:
        problems = ["build: " + err[:300]]
    else:
        res = testrun.run([testrun.Program("t_int64", os.path.join(OUT, "t_int64.bin"), [DEVICE_ROUNDS])], "int64",
                          timeout_each=300)
        got = res["t_int64"]
        dev = got.output.replace(b"\r\n", b"\n").decode("latin-1") if got.output else ""
        if got.status != 0:
            problems = [f"status {got.status}"]
        elif dev != host_small:
            problems = ["the emulator's output differs from the host's"] + \
                       [f"host {h!r} / device {d!r}" for h, d in zip(host_small.split("\n"), dev.split("\n")) if h != d][:5]
        else:
            problems = []
    print(("PASS  " if not problems else "FAIL  ") + f"A  built by agonc, on the emulator: the host's lines ({DEVICE_ROUNDS} rounds)")
    for p in problems:
        print("      " + p)
    failed += bool(problems)

    first = [l for l in host_small.split("\n") if l.startswith("mul ")]
    if first:
        f = first[0].split()
        wrong = host_small.replace(first[0], f"mul {f[1]} {f[2]} {(int(f[3], 16) ^ 1):016x}", 1)
        gp = check(wrong, int(DEVICE_ROUNDS))
    else:
        gp = []
    print(("PASS  " if gp else "FAIL  ") + "G  a changed result is reported")
    failed += not gp
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
