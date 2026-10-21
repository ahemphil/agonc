"""The cost of single integer operations: cycles per operation above an
empty loop.

    opcost.py [-O0] [--tree DIR]

opcost.c is built once per operation in OPS (-DOP=n) by the host driver,
each checked against the PC's answer (-DWANT, as bench.h does), and all
run in one emulator session at the real 18.432 MHz. Each loop runs N
times; clock()'s hundredths of a second give each figure to about +-5
cycles. --tree DIR measures another tree's build instead (DIR/build/host
and DIR/build/agon: tested/, say, for a before and after). A
measurement, not a test: outside make check. Run from PowerShell, with
nothing else using the emulator.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import AGON, EXE, HOSTCC, PY, RUN_EMULATOR, host, host_env  # noqa: E402

N = 40000                       # opcost.c's N
HZ = 18432000
OPS = [
    (0, "empty", "the loop alone (s += i)"),
    (1, "div2", "int i / 2"),
    (2, "shr1", "int i >> 1"),
    (3, "rem2", "int i % 2"),
    (4, "and1", "int i & 1"),
    (5, "udiv8", "unsigned u / 8"),
    (6, "urem8", "unsigned u % 8"),
    (7, "div10", "int i / 10"),
    (8, "rem10", "int i % 10"),
    (9, "div2neg", "int i / 2, i negative"),
    (10, "ldiv2", "long l / 2"),
    (11, "lshr1", "long l >> 1"),
    (12, "cdiv10", "unsigned char c / 10"),
    (13, "crem10", "unsigned char c % 10"),
]
OUT = os.path.join("build", "opcost")
DONE = os.path.join("build", "test", "testrt", "t_done.bin")
CARD = "opcost"
SRC = os.path.join("tests", "bench", "opcost", "opcost.c")


def main():
    args = sys.argv[1:]
    flags = ["-O0"] if "-O0" in args else []
    agonc, root = host("agonc"), AGON
    if "--tree" in args:
        tree = os.path.relpath(os.path.abspath(args[args.index("--tree") + 1]), REPO)  # the driver takes only short paths
        agonc = os.path.join(tree, "build", "host", "agonc" + EXE)
        root = os.path.join(tree, "build", "agon")
    if not os.path.exists(os.path.join(REPO, DONE)):
        sys.exit("opcost: build the test runtime first (any emulator suite, e.g. tests/libc_c)")
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    argv = [PY, RUN_EMULATOR, "--hold-stdin", "--sdcard-name", CARD, "--timeout", "1800", "--real-speed",
            "--bin", DONE]
    for op, name, _ in OPS:
        exe = os.path.join(REPO, OUT, name + "_pc.exe")
        r = subprocess.run([HOSTCC, "-std=c99", "-O0", "-fwrapv", "-w", "-DBENCH_HOST", f"-DOP={op}",
                            f"-DNAME={name}", "-o", exe, SRC], cwd=REPO, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"{name}: the PC's build failed: {r.stderr.strip()[:300]}")
        want = int(subprocess.run([exe], capture_output=True, text=True).stdout.strip())
        out = os.path.join(OUT, name + ".bin")
        r = subprocess.run([agonc] + flags + [f"-DOP={op}", f"-DNAME={name}", f"-DWANT={want}UL", "-o", out, SRC],
                           cwd=REPO, capture_output=True, text=True, env=host_env(root))
        if r.returncode:
            sys.exit(f"{name}: agonc failed: {(r.stderr or r.stdout).strip()[:300]}")
        argv += ["--bin", out]
    for _, name, _ in OPS:
        argv += ["--cmd", name]
    argv += ["--cmd", "t_done"]
    r = subprocess.run(argv, cwd=REPO, capture_output=True, text=True, errors="replace")
    card = os.path.join(REPO, "emulator_sdcard", CARD)
    ticks = {}
    for _, name, _ in OPS:
        p = os.path.join(card, "b_" + name + ".txt")
        if os.path.exists(p):
            ticks[name] = int(open(p).read().split()[0])
    base = ticks.get("empty")
    print(f"N = {N}, {HZ / 1e6:.3f} MHz, {'-O0' if flags else '-O'}"
          + (f", {args[args.index('--tree') + 1]}" if "--tree" in args else ""))
    print("| Operation | Time (s) | Cycles per operation |")
    print("|---|---:|---:|")
    missing = 0
    for _, name, desc in OPS:
        t = ticks.get(name)
        if t is None:
            print(f"| {desc} | (no time: a wrong answer or a crash) | |")
            missing += 1
        elif name == "empty":
            print(f"| {desc} | {t / 100.0:.2f} | {t / 100.0 * HZ / N:,.0f} per iteration |")
        else:
            print(f"| {desc} | {t / 100.0:.2f} | {(t - base) / 100.0 * HZ / N:,.0f} |")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
