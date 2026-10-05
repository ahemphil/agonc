"""The benchmarks: the speed and size of the code agonc generates.

    bench.py [-O0] [--fast] [--out FILE] [--save-baseline] [--only NAME...]

Each tests/bench/<name>.c is built on the PC (HOSTCC, -DBENCH_HOST) and run
there for its checksum, then built by the host driver for the Agon (make
cross's build/agon) with that checksum as -DWANT, and all of them run in
one emulator session at the real 18.432 MHz. Each times itself with
clock() (hundredths of a second, MOS's timer) and writes its time to the
card, and exits non-zero on a wrong checksum, which stops the session: a
missing time means a wrong answer or a crash.

The report gives each benchmark's time and code size, and the change from
tests/bench/baseline.json (--save-baseline records the current figures
there). -O0 builds without the optimiser; --fast runs the emulator unthrottled,
whose clock then follows the PC's, so only proportions count (baselines
are for one mode). A measurement, not a test: outside make check. Run from
PowerShell (BUILDING.md section 11), with nothing else using the emulator.
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import AGON, HOSTCC, PY, RUN_EMULATOR, host, host_env  # noqa: E402

OUT = os.path.join("build", "bench")
BASELINE = os.path.join(HERE, "baseline.json")
DONE = os.path.join("build", "test", "testrt", "t_done.bin")
CARD = "bench"


def sh(cmd, env=None):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, env=env)


def names(args):
    all_names = sorted(f[:-2] for f in os.listdir(HERE) if f.endswith(".c"))
    if "--only" in args:
        want = args[args.index("--only") + 1:]
        return [n for n in all_names if n in want]
    return all_names


def build(name, flags=()):
    """PC checksum, then the Agon binary; returns (checksum, size) or a problem string."""
    src = os.path.join("tests", "bench", name + ".c")
    exe = os.path.join(REPO, OUT, name + "_pc.exe")
    r = sh([HOSTCC, "-std=c99", "-O1", "-fwrapv", "-w", "-DBENCH_HOST", "-o", exe, src])
    if r.returncode:
        return f"{name}: the PC's build failed: {r.stderr.strip()[:200]}"
    r = subprocess.run([exe], capture_output=True, text=True)
    if r.returncode or not r.stdout.strip().isdigit():
        return f"{name}: the PC's run failed: {r.stdout.strip()[:100]}"
    want = int(r.stdout.strip())
    out = os.path.join(OUT, name + ".bin")
    r = sh([host("agonc")] + list(flags) + [f"-DWANT={want}UL", "-o", out, src], env=host_env(AGON))
    if r.returncode:
        return f"{name}: agonc failed: {(r.stderr or r.stdout).strip()[:300]}"
    return want, os.path.getsize(os.path.join(REPO, out))


def main():
    args = sys.argv[1:]
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    if not os.path.exists(os.path.join(REPO, DONE)):
        sys.exit("bench: build the test runtime first (any emulator suite, e.g. tests/libc_c)")
    sizes = {}
    problems = []
    run = []
    for n in names(args):
        b = build(n, ["-O0"] if "-O0" in args else [])
        if isinstance(b, str):
            problems.append(b)
            continue
        sizes[n] = b[1]
        run.append(n)
    argv = [PY, RUN_EMULATOR, "--hold-stdin", "--sdcard-name", CARD, "--timeout", "1800", "--bin", DONE]
    if "--fast" not in args:
        argv.append("--real-speed")
    for n in run:
        argv += ["--bin", os.path.join(OUT, n + ".bin")]
    for n in run:
        argv += ["--cmd", n]
    argv += ["--cmd", "t_done"]
    r = subprocess.run(argv, cwd=REPO, capture_output=True, text=True, errors="replace")
    card = os.path.join(REPO, "emulator_sdcard", CARD)
    times = {}
    for n in run:
        path = os.path.join(card, "b_" + n + ".txt")
        if os.path.exists(path):
            times[n] = int(open(path).read().split()[0])
        else:
            problems.append(f"{n}: no time (a wrong checksum or a crash stops the session)")
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    lines = ["| Benchmark | Time (s) | vs baseline | Size (bytes) | vs baseline |", "|---|---:|---:|---:|---:|"]
    for n in run:
        t = times.get(n)
        bt, bs = base.get(n, {}).get("time"), base.get(n, {}).get("size")
        dt = f"{(t - bt) * 100.0 / bt:+.1f}%" if t is not None and bt else ""
        ds = f"{(sizes[n] - bs) * 100.0 / bs:+.1f}%" if bs else ""
        lines.append(f"| {n} | {t / 100.0:.2f} | {dt} | {sizes[n]} | {ds} |" if t is not None else
                     f"| {n} | — | | {sizes[n]} | {ds} |")
    tt = sum(times.values())
    bt = sum(base.get(n, {}).get("time", 0) for n in times)
    lines.append(f"| **all** | {tt / 100.0:.2f} | {f'{(tt - bt) * 100.0 / bt:+.1f}%' if bt else ''} | "
                 f"{sum(sizes.values())} | |")
    report = "\n".join(lines)
    print(report)
    for p in problems:
        print("problem: " + p)
    print(f"(emulator status {r.returncode})")
    if "--out" in args:
        open(args[args.index("--out") + 1], "w", newline="\n").write(report + "\n")
    if "--save-baseline" in args and not problems:
        json.dump({n: {"time": times[n], "size": sizes[n]} for n in run}, open(BASELINE, "w"), indent=1)
        print("baseline saved")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
