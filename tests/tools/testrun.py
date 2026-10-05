"""Run C test programs on the emulator and capture what they print.

The emulator's capture of the screen is unreliable, so tests never judge by
it. Instead a test program is built against a test runtime:

    crt0_test.s   crt0.s, calling __test_main (testrt/testmain.c) instead
                  of main
    testmain.s    __test_main: takes the output file's name from the first
                  argument, runs main, records its status
    libc_test.s   the library built with -D__AGONC_TEST: console output is
                  appended to that file, and exit records its status

Programs run in batches, many to an emulator session, each in its own
folder on the card (so input files can sit beside it). MOS always gets
status 0 back, whatever the program returned, so one failing program does
not stop the batch; the real status is in `<output>.st`. A session that
hangs is cut off by its timeout: the first program with no status is
recorded as hung, and the ones after it run again in a new session.

    rt = runtime()
    err = build(rt, "t1", ["x.c"], "build/test/foo")     # None, or the error
    res = run([Program("t1", "build/test/foo/t1.bin")], "foo")
    res["t1"].status, res["t1"].output                  # None if it never finished

Needs `make cross` (the host driver and the headers in build/agon).
Run from PowerShell on Windows (BUILDING.md section 11).
"""
import os, re, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import REPO, PY, AGON, RUN_EMULATOR, OPT, host, host_env  # noqa: E402
from unitid import unit_id  # noqa: E402

RT = os.path.join("build", "test", "testrt")
LIB_UNITS = [os.path.join("lib", "libc", u + ".c") for u in ("ctype", "malloc", "stdio", "stdlib", "string", "exit", "time", "fp", "math", "ll")] + \
            [os.path.join("lib", "agon", "mos.c")]
DONE_C = "#include <agon/mos.h>\nint main(void)\n{\n    agon_emu_exit(0);\n    return 0;\n}\n"


class Program:
    """A built test program: its name (the MOS command, so it must not be a
    MOS built-in's), binary, arguments, and files to place in its folder as
    (name, host path) pairs."""

    def __init__(self, name, binary, args=(), files=()):
        self.name, self.binary, self.args, self.files = name.lower(), binary, list(args), list(files)


class Result:
    def __init__(self, status, output, ticks=None):
        self.status = status        # int, or None if the program never finished
        self.output = output        # bytes, CR LF made LF; b"" if it printed nothing
        self.ticks = ticks          # a timing runtime's main time, hundredths of a second


def driver(args):
    return subprocess.run([host("agonc")] + OPT + args, cwd=REPO, env=host_env(AGON), capture_output=True, text=True)


def runtime():
    """Build the test runtime into build/test/testrt; returns its paths.
    Raises RuntimeError if the library or the entry point does not build."""
    out = os.path.join(REPO, RT)
    os.makedirs(out, exist_ok=True)
    units = []
    for src in LIB_UNITS + [os.path.join("tests", "tools", "testrt", "testmain.c")]:
        s = os.path.join(RT, os.path.splitext(os.path.basename(src))[0] + ".s")
        r = driver(["-Werror", "-c", "-D", "__AGONC_TEST", "-o", s, src])
        if r.returncode:
            raise RuntimeError(f"{src}: {(r.stderr or r.stdout).strip()}")
        units.append(s)
    with open(os.path.join(REPO, RT, "libc_test.s"), "wb") as f:
        for s in units[:-1]:
            f.write(open(os.path.join(REPO, s), "rb").read())
    text = open(os.path.join(REPO, "lib", "rt", "crt0.s"), newline="").read()
    for old, new in ((";;unit crt0.s " + unit_id("crt0.s"), ";;unit crt0_test.s " + unit_id("crt0_test.s")),
                     (";;ref _main\n", ";;ref ___test_main\n"),
                     ("call    _main\n", "call    ___test_main\n")):
        if text.count(old) != 1:
            raise RuntimeError("crt0.s changed: cannot find " + repr(old))
        text = text.replace(old, new)
    open(os.path.join(REPO, RT, "crt0_test.s"), "w", newline="").write(text)
    return {"crt0": os.path.join(RT, "crt0_test.s"), "rt": os.path.join("lib", "rt", "rt.s"),
            "main": units[-1], "libc": os.path.join(RT, "libc_test.s")}


def timing_runtime():
    """The test runtime with testmain.c's __AGONC_TIMING variant, which
    records how long each program's main ran (run(..., real_speed=True)
    gives the times the real Agon's clock would)."""
    rt = dict(runtime())
    main_s = os.path.join(RT, "testmain_time.s")
    r = driver(["-Werror", "-c", "-D", "__AGONC_TEST", "-D", "__AGONC_TIMING", "-o", main_s,
                os.path.join("tests", "tools", "testrt", "testmain.c")])
    if r.returncode:
        raise RuntimeError("testmain_time: " + (r.stderr or r.stdout).strip())
    rt["main"] = main_s
    return rt


def build(rt, name, sources, out_dir, flags=()):
    """Build sources (.c or .s) into out_dir/<name>.bin against the test
    runtime. Returns None, or the compiler's messages if it failed."""
    os.makedirs(os.path.join(REPO, out_dir), exist_ok=True)
    binary = os.path.join(out_dir, name + ".bin")
    r = driver(list(flags) + ["-nostdlib", "-o", binary, rt["crt0"], rt["rt"], rt["main"]] + list(sources) +
               [rt["libc"]])
    if r.returncode:
        return (r.stderr or r.stdout).strip() or f"agonc exited {r.returncode}"
    return None


def _done_program():
    """t_done: ends the emulator session with status 0 (built normally)."""
    d = os.path.join(REPO, RT)
    src = os.path.join(d, "t_done.c")
    if not os.path.exists(src):
        open(src, "w", newline="\n").write(DONE_C)
    binary = os.path.join(RT, "t_done.bin")
    if not os.path.exists(os.path.join(REPO, binary)):
        r = driver(["-o", binary, os.path.join(RT, "t_done.c")])
        if r.returncode:
            raise RuntimeError("t_done: " + (r.stderr or r.stdout).strip())
    return binary


SESSION_MAX = 300


def _session(programs, card_name, timeout, real_speed=False):
    """One emulator session over programs; returns the port-0 status."""
    stage = os.path.join(REPO, "build", "test", "cards", card_name)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, "bin"))
    os.makedirs(os.path.join(stage, "out"))
    cmds = []
    for p in programs:
        folder = os.path.join(stage, "w", p.name)
        os.makedirs(folder)
        shutil.copy(os.path.join(REPO, p.binary), os.path.join(stage, "bin", p.name + ".bin"))
        for card_name_, src in p.files:
            shutil.copy(os.path.join(REPO, src), os.path.join(folder, card_name_))
        cmds += ["cd /w/" + p.name, " ".join([p.name, "/out/" + p.name + ".txt"] + p.args)]
    shutil.copy(os.path.join(REPO, _done_program()), os.path.join(stage, "bin", "t_done.bin"))
    cmds += ["cd /", "t_done"]
    args = [PY, RUN_EMULATOR, "--hold-stdin", "--card-from", stage, "--sdcard-name", card_name,
            "--timeout", str(timeout)] + (["--real-speed"] if real_speed else [])
    for c in cmds:
        args += ["--cmd", c]
    return subprocess.run(args, cwd=REPO, capture_output=True, text=True).returncode


def _collect(programs, card_name):
    card = os.path.join(REPO, "emulator_sdcard", card_name, "out")
    found = {}
    for p in programs:
        st = os.path.join(card, p.name + ".txt.st")
        if not os.path.exists(st):
            continue
        txt = os.path.join(card, p.name + ".txt")
        out = open(txt, "rb").read().replace(b"\r\n", b"\n") if os.path.exists(txt) else b""
        tm = os.path.join(card, p.name + ".txt.tm")
        ticks = int(open(tm).read().strip()) if os.path.exists(tm) else None
        found[p.name] = Result(int(open(st).read().strip()), out, ticks)
    return found


def run(programs, card_name, timeout_each=20, timeout_base=30, real_speed=False):
    """Run programs in as few emulator sessions as possible; returns
    {name: Result}. A program that hangs or crashes gets status None.
    real_speed runs the emulator at the real Agon's clock, for timing."""
    results = {}
    # At most SESSION_MAX programs a session: each is two --cmd arguments,
    # and Windows limits a command line to 32,767 characters.
    for k in range(0, len(programs), SESSION_MAX):
        pending = list(programs[k:k + SESSION_MAX])
        while pending:
            _session(pending, card_name, timeout_base + timeout_each * len(pending), real_speed)
            found = _collect(pending, card_name)
            results.update(found)
            if len(found) == len(pending):
                break
            # Programs run in order, so the first one without a status is the
            # one that hung or crashed; the rest never ran.
            for i, p in enumerate(pending):
                if p.name not in found:
                    results[p.name] = Result(None, b"")
                    pending = [q for q in pending[i + 1:] if q.name not in found]
                    break
    return results


def expected_ok(result, status, output):
    """True if result matches: status as given, output exactly (CR LF → LF)."""
    return result.status == status and result.output == output.replace(b"\r\n", b"\n")


if __name__ == "__main__":
    print(__doc__)
