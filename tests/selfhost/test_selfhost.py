"""T5, the self-hosting fixed point: the bootstrap exactly as a user runs it.

    test_selfhost.py

Needs make sdcard and make cross (make check builds both).

On the emulator, with build/sdcard as the SD card, the lines of the four
bootstrap scripts run in order (bootstrap/1-stage1.txt ... 4-stage3.txt,
inlined into autoexec.txt because MOS does not return from a nested exec):
stage 1, the AgDev-built passes, builds the C library, concat, compare and
the driver; the driver and stage 1 build stage 2; stage 2 is installed; it
builds everything again as stage 3, and compare checks each program and
library unit against stage 2 (a difference stops the script). Then the
installed stage-2 driver builds cc1's t_exec.c, t_m4.c and t_m13.c (floating
point: the driver adds /lib/agonc/libm.s), and t_m13 runs.

B   the run ends with t_m13 exiting 0: every step succeeded, and a program
    built by stage 2 works, its floating point checked bit for bit.
S2  each stage-2 program equals make cross's build of it, and so do the
    libraries the device joined (/lib/agonc/libc.s, libm.s and libagon.s).
S3  each stage-3 program equals stage 2, and each stage-3 library unit
    equals make cross's (compare checked stage 3 against stage 2's; this
    checks it independently, on the host).
P   t_exec.bin, t_m4.bin and t_m13.bin built on the device equal the host
    driver's.
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import (PY, PROGS, SDCARD, CROSS, LIBC, LIBM, LIBAGON, LIBAGON_UNITS, RUN_EMULATOR, agon,  # noqa: E402
                    host, host_env,
                    stage_root)

OUT = os.path.join("build", "test", "selfhost")
ROOT = os.path.join(OUT, "root")
CARD = os.path.join("emulator_sdcard", "selfhost")
SCRIPTS = ["1-stage1.txt", "2-stage2.txt", "3-install.txt", "4-stage3.txt"]
UNITS = ["ctype", "malloc", "stdio", "stdlib", "string", "exit", "time", "fp", "math", "math99", "mathf", "ll", "mos"] + LIBAGON_UNITS
PROGRAMS = ["t_exec", "t_m4", "t_m13"]
TIMEOUT = 900                   # the whole bootstrap takes about 4 minutes at emulator speed


def same(a, b, what):
    pa, pb = os.path.join(REPO, a), os.path.join(REPO, b)
    if not os.path.exists(pa) or not os.path.exists(pb):
        return [f"{what}: {a if not os.path.exists(pa) else b} missing"]
    return [] if open(pa, "rb").read() == open(pb, "rb").read() else [f"{what}: {a} differs from {b}"]


def run_bootstrap():
    cmds = []
    for s in SCRIPTS:
        cmds += [l for l in open(os.path.join(REPO, "bootstrap", s)).read().splitlines() if l.strip()]
    cmds += ["cd /"] + [f"agonc -o /bin/{p}.bin /{p}.c" for p in PROGRAMS] + ["t_m13"]
    args = [PY, RUN_EMULATOR, "--hold-stdin", "--card-from", SDCARD, "--sdcard-name", "selfhost",
            "--timeout", str(TIMEOUT)]
    for p in PROGRAMS:
        args += ["--file", os.path.join("tests", "cc1", p + ".c")]
    for c in cmds:
        args += ["--cmd", c]
    start = time.time()
    r = subprocess.run(args, cwd=REPO, capture_output=True, text=True)
    print(f"      (the bootstrap on the emulator: {time.time() - start:.0f} s)")
    open(os.path.join(REPO, OUT, "bootstrap.log"), "w").write(r.stdout + r.stderr)
    return r.returncode


def main():
    for f in [SDCARD, LIBC] + [agon(p) for p in PROGS]:
        if not os.path.exists(os.path.join(REPO, f)):
            print(f"{f} is missing: run make sdcard and make cross first")
            return 1
    if os.path.isdir(os.path.join(REPO, OUT)):          # nothing from an earlier run can pass for output
        shutil.rmtree(os.path.join(REPO, OUT))
    os.makedirs(os.path.join(REPO, OUT))
    stage_root(ROOT)
    for p in PROGRAMS:                                   # the host driver's builds, for P
        r = subprocess.run([os.path.join(REPO, host("agonc")), "-o", os.path.join(OUT, p + ".bin"),
                            os.path.join("tests", "cc1", p + ".c")], cwd=REPO, env=host_env(ROOT),
                           capture_output=True, text=True)
        if r.returncode:
            print(f"host build of {p}: {(r.stdout + r.stderr).strip()[:300]}")
            return 1
    rc = run_bootstrap()
    s2 = os.path.join(CARD, "agonc", "s2")
    s3 = os.path.join(CARD, "agonc", "s3")
    cases = [
        ("B  the bootstrap scripts run to the end, and t_m13 (built by stage 2) exits 0",
         [] if rc == 0 else [f"status {rc} (124: a command failed and MOS stopped the script; "
                             f"see {OUT}/bootstrap.log and {CARD})"]),
        ("S2 stage 2 equals make cross's build, the device's libc.s, libm.s and libagon.s the host's",
         sum([same(os.path.join(s2, p + ".bin"), agon(p), f"stage-2 {p}") for p in PROGS], []) +
         same(os.path.join(CARD, "lib", "agonc", "libc.s"), LIBC, "libc.s") +
         same(os.path.join(CARD, "lib", "agonc", "libm.s"), LIBM, "libm.s") +
         same(os.path.join(CARD, "lib", "agonc", "libagon.s"), LIBAGON, "libagon.s")),
        ("S3 stage 3 equals stage 2; its library units equal make cross's",
         sum([same(os.path.join(s3, p + ".bin"), os.path.join(s2, p + ".bin"), f"stage-3 {p}") for p in PROGS], []) +
         sum([same(os.path.join(s3, u + ".s"), os.path.join(CROSS, u + ".s"), f"stage-3 {u}.s") for u in UNITS], [])),
        ("P  t_exec, t_m4 and t_m13 built by stage 2 equal the host driver's",
         sum([same(os.path.join(CARD, "bin", p + ".bin"), os.path.join(OUT, p + ".bin"), p) for p in PROGRAMS], [])),
    ]
    failed = 0
    for name, problems in cases:
        print(("PASS  " if not problems else "FAIL  ") + name)
        for p in problems:
            print("      " + p)
        failed += bool(problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
