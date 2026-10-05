"""T6: the memory profile of building the compiler, checked against a
recorded baseline.

    memprofile.py [--update] [--device]

Needs make host and make cross (make check builds them). For every source of every
program (cpp, cc1, cc2, ld, the agonc moslet) and of the C library, the host
passes run with -v and report each fixed table's peak against its limit;
each program's link reports ld's tables; ez80asm -x reports label and fixup
memory for each program's .asm; and each binary's layout gives its image,
bss and what is left for heap and stack. The host passes behave exactly as
the Agon ones (T5), so their peaks are the device's. ez80asm's figures are
the host build's, whose 64-bit pointers make them larger than the device's.

The profile is written to tests/selfhost/MEMORY.md. The check
fails if any table is above 90% of its limit, if any figure has grown more
than 10% (and by more than 64) since the baseline
(tests/selfhost/memory_baseline.json), or if a program leaves less
than 16 KB for heap and stack (the moslet, less than 2 KB for
its stack). --update records the current figures as the baseline.

--device also runs cc1, cc2 and ld on the emulator over the same inputs
and records each one's heap and stack high-water marks in
tests/selfhost/MEMORY_DEVICE.md (a few minutes; not part of make check).
"""
import json, os, re, subprocess, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
from common import PROGS, LIB_SOURCES, MOSLET_STACK, EZ80ASM, host, rsp_sources, stage_root  # noqa: E402

OUT = os.path.join("build", "memprofile")
SELFHOST = os.path.join(REPO, "tests", "selfhost")
BASELINE = os.path.join(SELFHOST, "memory_baseline.json")
REPORT = os.path.join(SELFHOST, "MEMORY.md")
ROOT = os.path.join(OUT, "root")
NEAR = 0.90
GROWTH = 0.10
MIN_FREE = 16384
PAIR = re.compile(r"([A-Za-z][A-Za-z ]*?) (\d+)/(\d+)")


def run(cmd):
    r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{' '.join(cmd)} failed:\n{r.stdout}{r.stderr}")
    return r.stdout


def tables(text, peaks, where):
    """Folds every "name used/limit" pair of -v output into peaks:
    key -> [peak, limit, where the peak was]."""
    for line in text.splitlines():
        m = re.match(r"(\w+): ", line)
        if not m:
            continue
        for name, used, limit in PAIR.findall(line[m.end():]):
            key = f"{m.group(1)} {name.strip()}"
            used, limit = int(used), int(limit)
            if key not in peaks or used > peaks[key][0]:
                peaks[key] = [used, limit, where]


def units():
    """(program, source) for everything built: the programs and the library."""
    found = []
    for p in PROGS:
        found += [(p, s) for s in rsp_sources(p)]
    for d in LIB_SOURCES:
        for f in sorted(os.listdir(os.path.join(REPO, d))):
            if f.endswith(".c"):
                found.append(("libc", os.path.join(d, f)))
    return found


def profile():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    stage_root(ROOT)
    root_lib = ROOT + "/lib"
    peaks = {}
    s_files = {}
    for prog, src in units():
        name = os.path.basename(src)
        stem = os.path.join(OUT, prog + "_" + name[:-2])
        where = f"{prog}: {name}"
        tables(run([host("cpp"), src, stem + ".i", "-v", "-I", os.path.join("src", "common"),
                    "-I" + ROOT + "/usrlib", "-I" + root_lib]), peaks, where)
        tables(run([host("cc1"), stem + ".i", stem + ".ir", "-u", name, "-v"]), peaks, where)
        tables(run([host("cc2"), stem + ".ir", stem + ".s", "-v", "-O"]), peaks, where)
        s_files.setdefault(prog, []).append(stem + ".s")
    rows = []
    for prog in PROGS:
        asm = os.path.join(OUT, prog + ".asm")
        layout = ["--moslet"] if prog == "agonc" else []
        tables(run([host("ld"), "-v", "-o", asm] + layout + [root_lib + "/crt0.s", root_lib + "/rt.s"] +
                   s_files[prog] + [root_lib + "/libc.s"]), peaks, f"{prog}: link")
        stats = run([EZ80ASM, asm, os.path.join(OUT, prog + ".bin"), "-x", "-c"])
        figures = {k: int(v) for k, v in re.findall(r"^(Label memory|Labels|Fixup memory \(peak\)|Output size)\s*:\s*(\d+)",
                                                    stats, re.M)}
        head = open(os.path.join(REPO, asm)).read(400)
        bss = int(re.search(r"__bss_size:\tequ (\d+)", head).group(1))
        size = os.path.getsize(os.path.join(REPO, OUT, prog + ".bin"))
        region = 0x8000 if prog == "agonc" else 0x70000
        rows.append({"program": prog, "image": size, "bss": bss, "free": region - size - bss,
                     "labels": figures.get("Labels", 0), "label memory": figures.get("Label memory", 0),
                     "fixup memory": figures.get("Fixup memory (peak)", 0)})
    return peaks, rows


def check(peaks, rows, base):
    problems = []
    for key, (used, limit, where) in sorted(peaks.items()):
        if used > NEAR * limit:
            problems.append(f"{key}: {used}/{limit} is above {NEAR:.0%} of its limit ({where})")
        old = base.get("tables", {}).get(key)
        if old is not None and used > old * (1 + GROWTH) and used - old > 64:
            problems.append(f"{key}: {used}, up from {old} ({where})")
    old_rows = {r["program"]: r for r in base.get("programs", [])}
    for r in rows:
        if r["free"] < (MOSLET_STACK if r["program"] == "agonc" else MIN_FREE):
            problems.append(f"{r['program']}: only {r['free']} bytes left for heap and stack")
        o = old_rows.get(r["program"], {})
        for k in ("image", "bss", "label memory", "fixup memory"):
            if k in o and r[k] > o[k] * (1 + GROWTH) and r[k] - o[k] > 64:
                problems.append(f"{r['program']} {k}: {r[k]}, up from {o[k]}")
    return problems


def report(peaks, rows):
    lines = ["# Memory profile of building the compiler (T6)", "",
             "Generated by `tests/tools/memprofile.py`; do not edit. Every fixed table's",
             "peak over the compiler's own sources and the C library, against its limit",
             "(the limits are `#define`s at the top of each pass).", "",
             "| Table | Peak | Limit | Use | Peak reached in |", "|---|---:|---:|---:|---|"]
    for key, (used, limit, where) in sorted(peaks.items()):
        lines.append(f"| {key} | {used} | {limit} | {100 * used // limit}% | {where} |")
    lines += ["", "(Per-function and per-initialiser tables are the largest single function's or",
              "initialiser's; the rest are per translation unit.)", "",
              "Each program as built by our own toolchain (bytes; free = heap and stack; the",
              "moslet's area is 32 KB, a program's 448 KB). ez80asm figures are from its host",
              "build (64-bit pointers), so they overstate the device's.", "",
              "| Program | Image | bss | Free | Labels | Label memory | Fixup memory |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        lines.append(f"| {r['program']} | {r['image']} | {r['bss']} | {r['free']} | {r['labels']} | "
                     f"{r['label memory']} | {r['fixup memory']} |")
    open(REPORT, "w", newline="\n").write("\n".join(lines) + "\n")


DEVICE_REPORT = os.path.join(SELFHOST, "MEMORY_DEVICE.md")


def device(rows):
    """--device: cc1, cc2 and ld run on the emulator over the profile's own
    inputs (the .i, .ir and .s files just made), each built against the test
    runtime with testmain.c's __AGONC_MEMPROFILE variant, which records the
    heap's and the stack's high-water marks. cpp is not run: it has the most
    room of any pass, and would need the whole include tree on the card."""
    import testrun
    rt = dict(testrun.runtime())
    main_s = os.path.join(testrun.RT, "testmain_mem.s")
    r = testrun.driver(["-Werror", "-c", "-D", "__AGONC_TEST", "-D", "__AGONC_MEMPROFILE", "-o", main_s,
                        os.path.join("tests", "tools", "testrt", "testmain.c")])
    if r.returncode:
        sys.exit("testmain_mem: " + (r.stderr or r.stdout))
    rt["main"] = main_s
    bins = os.path.join(OUT, "device")
    for prog in ("cc1", "cc2", "ld"):
        err = testrun.build(rt, prog + "m", rsp_sources(prog), bins, ["-I", os.path.join("src", "common")])
        if err:
            sys.exit(f"{prog} (memprofile build): {err}")
    programs, what = [], {}
    by_prog = {}
    for prog, src in units():
        by_prog.setdefault(prog, []).append(src)
    k = 0
    for prog in PROGS:
        for src in by_prog[prog]:
            name = os.path.basename(src)[:-2]
            stem = os.path.join(OUT, prog + "_" + name)
            k += 1
            programs.append(testrun.Program(f"ma{k:02d}", os.path.join(bins, "cc1m.bin"),
                                            ["u.i", "u.ir", "-u", name + ".c"], [("u.i", stem + ".i")]))
            what[f"ma{k:02d}"] = ("cc1", f"{prog}: {name}.c")
            programs.append(testrun.Program(f"mb{k:02d}", os.path.join(bins, "cc2m.bin"),
                                            ["u.ir", "u.s"], [("u.ir", stem + ".ir")]))
            what[f"mb{k:02d}"] = ("cc2", f"{prog}: {name}.c")
    root_lib = ROOT + "/lib"
    for i, prog in enumerate(PROGS):
        files = [("crt0.s", root_lib + "/crt0.s"), ("rt.s", root_lib + "/rt.s"), ("libc.s", root_lib + "/libc.s")]
        units_s = []
        for j, src in enumerate(by_prog[prog]):
            files.append((f"u{j}.s", os.path.join(OUT, prog + "_" + os.path.basename(src)[:-2] + ".s")))
            units_s.append(f"u{j}.s")
        layout = ["--moslet"] if prog == "agonc" else []
        programs.append(testrun.Program(f"mc{i}", os.path.join(bins, "ldm.bin"),
                                        ["-o", "p.asm"] + layout + ["crt0.s", "rt.s"] + units_s + ["libc.s"], files))
        what[f"mc{i}"] = ("ld", f"{prog}: link")
    results = testrun.run(programs, "memprofile", timeout_each=60)
    card = os.path.join(REPO, "emulator_sdcard", "memprofile", "out")
    worst = {}
    lines = ["# Heap and stack of building the compiler, on the device (T6 --device)", "",
             "Generated by `tests/tools/memprofile.py --device`; do not edit. Each pass",
             "runs on the emulator over the compiler's own sources and the C library,",
             "built with the test runtime, which records the heap's and the stack's",
             "high-water marks (bytes).", "",
             "| Pass | Input | Status | Heap | Stack |", "|---|---|---:|---:|---:|"]
    problems = 0
    for p in programs:
        pass_, where = what[p.name]
        res = results.get(p.name)
        mem = os.path.join(card, p.name + ".txt.mem")
        m = re.match(r"heap (\d+) stack (\d+)", open(mem).read()) if os.path.exists(mem) else None
        status = "none" if res is None or res.status is None else str(res.status)
        if status != "0" or not m:
            problems += 1
        heap, stack = (int(m.group(1)), int(m.group(2))) if m else (None, None)
        lines.append(f"| {pass_} | {where} | {status} | {heap if m else '?'} | {stack if m else '?'} |")
        if m:
            w = worst.setdefault(pass_, [0, "", 0, ""])
            if heap > w[0]:
                w[0], w[1] = heap, where
            if stack > w[2]:
                w[2], w[3] = stack, where
    free = {r["program"]: r["free"] for r in rows}
    lines += ["", "| Pass | Free (image and bss aside) | Heap peak | Stack peak | Left |", "|---|---:|---:|---:|---:|"]
    for pass_ in ("cc1", "cc2", "ld"):
        if pass_ in worst:
            w = worst[pass_]
            lines.append(f"| {pass_} | {free[pass_]} | {w[0]} ({w[1]}) | {w[2]} ({w[3]}) | "
                         f"{free[pass_] - w[0] - w[2]} |")
    open(DEVICE_REPORT, "w", newline="\n").write("\n".join(lines) + "\n")
    print("\n".join(lines[-5:]))
    print(f"device: {len(programs)} runs, {problems} without a status of 0 and a record "
          f"(report in {os.path.relpath(DEVICE_REPORT, REPO)})")
    return problems


def main():
    peaks, rows = profile()
    report(peaks, rows)
    if "--device" in sys.argv:
        return 1 if device(rows) else 0
    current = {"tables": {k: v[0] for k, v in peaks.items()}, "programs": rows}
    if "--update" in sys.argv:
        json.dump(current, open(BASELINE, "w", newline="\n"), indent=1, sort_keys=True)
        print(f"baseline recorded: {len(peaks)} tables, {len(rows)} programs")
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    problems = check(peaks, rows, base)
    for p in problems:
        print("FAIL  " + p)
    print(f"{len(peaks)} tables, {len(rows)} programs; {len(problems)} problems (profile in {os.path.relpath(REPORT, REPO)})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
