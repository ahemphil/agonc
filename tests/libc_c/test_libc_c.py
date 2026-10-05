"""Tests for the C library (lib/libc/*.c and lib/agon/*.c),
compiled by our own compiler into libc.s.

    test_libc_c.py [--no-stage1]

T4  each test program, through cpp + cc1 + cc2 + ld (crt0.s, rt.s, libc.s),
    on the emulator: exit 0. Guards for each: a wrong expectation is reported
    by its number, and the number of checks executed is verified. And
    t_stdio, which has no floating point or long long, links none of
    printf's floating or ll conversions (their weak references are 0),
    while t_fpio and t_llio do.
      t_libc    string, ctype, stdlib (with M8s long functions), limits.h, stdint.h,
                stddef.h (offsetof), assert.h (with NDEBUG), EXIT_SUCCESS/FAILURE
      t_mos     <agon/mos.h>: the cases of the assembly mos.s's tests
      t_malloc  the allocator: the cases of the assembly malloc.s's tests
      t_stdio   stdio: the cases of the assembly stdio.s's tests, and the scanf family
      t_term    signal and raise, setjmp and longjmp, abort caught, atexit's limit
      t_stream  text and binary streams, update modes, ungetc, setvbuf, getc/putc
                macros, printf's %n, FOPEN_MAX files at once
      t_stdmore freopen (perror's text, gets), tmpnam and tmpfile, the vprintf
                family, fgetpos and fsetpos, strerror, errno on failures
      t_stdlib  qsort, bsearch, rand, div, getenv, system, the multibyte
                functions, the rest of <string.h> and <ctype.h>, <locale.h>
      t_time    <time.h>: known instants, mktime, strftime, the live clocks
      t_agon    <agon/vdp.h>'s commands, <agon/uart.h>'s port open and closed
      t_mosx    the rest of <agon/mos.h> (libagon.s): save, copy, fgetc and
                fputc, cd and getcwd, the FatFS calls and structure sizes, the
                system variables, the UART by MOS's names
      t_vdu     <agon/vdp.h>'s main, system, bitmap and sprite commands send
                the documented bytes (the units compiled in with
                AGON_VDU_CAPTURE, the test's vdu and vdu_n recording)
      t_vdu2    the same for audio, buffers, contexts, fonts, copper, tiles
      t_fpeq    softfp.c's assembly kernels give the C's exact bits: the two
                builds linked side by side over special and random operands
      t_i64eq   the same for int64.c's (long long) assembly
      t_fpio    printf's e f g E F G (text from Python's formatting, which is
                C's), '#' on o and x, scanf's floating conversions, strtod and
                atof (bits from the PC), rounding past 800 digits
      t_llio    long long: printf's and scanf's ll and j, %lln, strtoll,
                strtoull, atoll, llabs, lldiv, <limits.h> and <stdint.h>
K   t_keyh.c: a C key handler (mos_set_key_handler) sees the keys typed
    ("kq") while main divides alongside it, its quotients right (the
    runtime's cells saved around the handler); interrupt handlers take
    and give back vectors, four slots at most. Guard: "kx" fails check 3.
I   t_conin.c: a line typed at the keyboard ("Bob") reaches fgets(stdin)
    exactly, even when stdin's buffer held other bytes; guard: "Bobby"
    fails check 2.
X   t_exit.c: exit(0) from ten calls deep returns to MOS, which then runs
    t_after (exit 99); if exit() returned, t_exit would exit 1.
E   how programs end, in one testrun session (its runtime records each
    status and returns 0, so the batch goes on): t_atx (main's return runs
    the atexit functions in reverse, then closes an unclosed file: 7),
    t_abrt (abort: 134, the file closed unwritten), t_sig (SIGTERM's
    default: 143, the file written), t_exd (exit(3) from deep), t_tmpx (a
    tmpfile left open), t_args (crt0's quoted arguments), then t_echk
    checks the files (the tmpfile's gone) and that all 8 MOS handles are
    free; guard: t_echk expecting the wrong text fails 1.
C   Ctrl-C, pressed by the harness when each program has made its marker
    file (run_emulator.py --ctrl-c-when), with statuses recorded as in E:
    t_intd writing a file (SIGINT's default: 130), t_inth with a handler
    that returns (the program goes on: 5), t_intk in mos_getkey (130),
    t_intc at an input prompt, Ctrl-C then Enter (130); then t_ichk: the
    interrupted file holds what was written, and all 8 handles are free.
G   getenv (t_env.c) on MOS 3.0.2, after the script sets a variable: its
    value, in either case, and NULL for any other name; and on MOS 2.3.3,
    NULL for every name.
S1  on the emulator, with the headers in /lib and /lib/agon: the AgDev-built
    cpp, cc1 and cc2 compile every library source and t_stdio.c, the
    AgDev-built ld links them, the device's ez80asm assembles, and the
    program passes; every device .s equals the host's.
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join("build", "test", "libc_c")
PY = sys.executable
HOST = os.path.join("build", "host")
TESTS = os.path.join("tests", "libc_c")
LIB_DIRS = [os.path.join("lib", "libc"), os.path.join("lib", "agon")]
INCLUDE = ["-I", os.path.join("lib", "libc"), "-I", os.path.join("lib")]
RUNTIME = [os.path.join("lib", "rt", "crt0.s"), os.path.join("lib", "rt", "rt.s")]
LIBC = os.path.join("build", "agon", "lib", "libc.s")
LIBM = os.path.join("build", "agon", "lib", "libm.s")
LIBAGON = os.path.join("build", "agon", "lib", "libagon.s")
FIXTURES = [os.path.join(TESTS, "fixtures", f) for f in ("existing.txt", "exact16.bin")]

# (program, one check, the same check made wrong)
PROGRAMS = [
    ("t_libc", 'check(atoi("+7"), 7);', 'check(atoi("+7"), 8);'),
    ("t_mos", 'check(mos_fsize("existing.txt"), 13);', 'check(mos_fsize("existing.txt"), 14);'),
    ("t_malloc", "check(malloc(24) == a, 1);", "check(malloc(24) == a, 0);"),
    ("t_stdio", 'row("ABCDEF", sprintf(out, "%X", 0xABCDEF));', 'row("ABCDEf", sprintf(out, "%X", 0xABCDEF));'),
    ("t_term", "check(r, 42);", "check(r, 43);"),
    ("t_stream", "check(n, 4097);", "check(n, 4098);"),
    ("t_stdmore", 'check(to_buf("%d-%s", 42, "x"), 4);', 'check(to_buf("%d-%s", 42, "x"), 5);'),
    ("t_stdlib", "check(rand(), 5758);", "check(rand(), 5759);"),
    ("t_time", 'check((int)strftime(buf, 5, "%Y", &tm), 4);', 'check((int)strftime(buf, 5, "%Y", &tm), 5);'),
    ("t_agon", "check(uart_putc('A'), 'A');", "check(uart_putc('A'), 'B');"),
    ("t_mosx", "check((int)info.fsize, 13);", "check((int)info.fsize, 14);"),
    ("t_vdu", "vdp_bell(); want(1, 7);", "vdp_bell(); want(1, 8);"),
    ("t_vdu2", "vdp_audio_enable_channel(5); want(5, 23, 0, 0x85, 5, 8);",
     "vdp_audio_enable_channel(5); want(5, 23, 0, 0x85, 5, 9);"),
    ("t_fpeq", "check(bad32, 0);", "check(bad32, 1);"),
    ("t_i64eq", "check(bad_shift, 0);                /* (the random operands) */",
     "check(bad_shift, 1);                /* (the random operands) */"),
    ("t_fpio", 'check_str(buf, "0.1000000015");', 'check_str(buf, "0.1000000016");'),
    ("t_llio", 'check_str(buf, "123456789abcdef0");', 'check_str(buf, "123456789abcdef1");'),
]

# for each program: the wrong check's number, and how many checks run (mod 256)
EXPECT = {
    "t_libc": (69, 145),
    "t_mos": (54, 81),
    "t_malloc": (9, 44),
    "t_stdio": (100, 309 % 256),
    "t_term": (21, 30),
    "t_stream": (14, 95),
    "t_stdmore": (27, 56),
    "t_stdlib": (18, 82),
    "t_time": (71, 84),
    "t_agon": (3, 7),
    "t_mosx": (45, 67),
    "t_vdu": (17, 148),
    "t_vdu2": (25, 103),
    "t_fpeq": (8, 10),
    "t_i64eq": (6, 6),
    "t_fpio": (202, 276 % 256),
    "t_llio": (12, 108),
}


def sh(cmd):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)


def o(name):
    return os.path.join(OUT, name)


# Tests built with more units than their own: the VDU tests compile these
# lib/agon units in with AGON_VDU_CAPTURE defined, so that the test's own
# vdu and vdu_n record the bytes; t_fpeq links softfp.c twice, its
# assembly kernels and its C, under different names.
def _agon(*units):
    return [os.path.join("lib", "agon", u + ".c") for u in units]


EXTRA = {"t_vdu": (_agon("vdp", "vdpsys", "vdpbmp"), ["-DAGON_VDU_CAPTURE"]),
         "t_vdu2": (_agon("vdpaudio", "vdpbuf", "vdpmore", "vdpsys"), ["-DAGON_VDU_CAPTURE"]),
         "t_fpeq": ([os.path.join(TESTS, "fpeq_a.c"), os.path.join(TESTS, "fpeq_c.c")], []),
         "t_i64eq": ([os.path.join(TESTS, "i64eq_a.c"), os.path.join(TESTS, "i64eq_c.c")], [])}


def build(src, stem):
    """cpp + cc1 + cc2 for the test (and its EXTRA units), then ld +
    ez80asm; returns a problem string or None."""
    units, defines = EXTRA.get(stem.replace("_bad", "").replace("_cnt", ""), ([], []))
    parts = [(src, stem)] + [(u, stem + "_" + os.path.basename(u)[:-2]) for u in units]
    cmds = []
    for path, st in parts:
        cmds += [[os.path.join(HOST, "cpp.exe"), path, o(st + ".i")] + INCLUDE + defines,
                 [os.path.join(HOST, "cc1.exe"), o(st + ".i"), o(st + ".ir"), "-u", os.path.basename(path)],
                 [os.path.join(HOST, "cc2.exe"), o(st + ".ir"), o(st + ".s")] + ([] if os.environ.get("AGONC_TEST_O0") else ["-O"])]
    cmds += [[os.path.join(HOST, "ld.exe"), "-o", o(stem + ".asm")] + RUNTIME + [LIBC, LIBM] +
             [o(st + ".s") for _, st in parts] + ["--if-needed", LIBAGON],
             [os.path.join("third_party", "bin", "ez80asm.exe"), o(stem + ".asm"), o(stem + ".bin")]]
    for cmd in cmds:
        r = sh(cmd)
        if r.returncode:
            return f"{os.path.basename(cmd[0])}: {(r.stderr or r.stdout).strip()[:300]}"
    return None


def emulate(bins, cmds, name, typed=()):
    args = [PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin"]
    for t in typed:
        args += ["--type", t]
    for b in bins:
        args += ["--bin", o(b + ".bin")]
    for f in FIXTURES:
        args += ["--file", f]
    for c in cmds:
        args += ["--cmd", c]
    return sh(args + ["--sdcard-name", name, "--timeout", "120"]).returncode


def variant(stem, text, check_h, suffix):
    """A copy of a test in OUT beside its own check.h; returns its path."""
    open(os.path.join(REPO, o("check.h")), "w", newline="\n").write(check_h)
    path = o(stem + suffix + ".c")
    open(os.path.join(REPO, path), "w", newline="\n").write(text)
    return path


def t4_program(stem, bad_from, bad_to):
    src = os.path.join(TESTS, stem + ".c")
    p = build(src, stem)
    if p:
        return [p], None, None
    problems = []
    rc = emulate([stem], [stem], "libcc")
    if rc != 0:
        problems.append(f"{stem}: check {rc} failed")
    text = open(os.path.join(REPO, src)).read()
    check_h = open(os.path.join(HERE, "check.h")).read()
    assert bad_from in text
    # the first guard: one wrong expectation, reported by its number
    path = variant(stem, text.replace(bad_from, bad_to), check_h, "_bad")
    p = build(path, stem + "_bad")
    bad_rc = p or emulate([stem + "_bad"], [stem + "_bad"], "libcc")
    # the second: finish() reports how many checks ran
    counting = check_h.replace("agon_emu_exit(fails == 0 ? 0 : first < 250 ? first : 250);",
                               "agon_emu_exit(count % 256);")
    assert counting != check_h
    path = variant(stem, text, counting, "_cnt")
    p = build(path, stem + "_cnt")
    cnt_rc = p or emulate([stem + "_cnt"], [stem + "_cnt"], "libcc")
    return problems, bad_rc, cnt_rc


def t4():
    problems = []
    for stem, bad_from, bad_to in PROGRAMS:
        pr, bad_rc, cnt_rc = t4_program(stem, bad_from, bad_to)
        problems += pr
        want_bad, want_cnt = EXPECT[stem]
        if bad_rc != want_bad:
            problems.append(f"{stem} guard: a wrong expectation gave {bad_rc}, expected {want_bad}")
        if cnt_rc != want_cnt:
            problems.append(f"{stem} guard: {cnt_rc} checks ran (mod 256), expected {want_cnt}")
    # printf's floating conversions are linked only by a program with floating
    # point: t_stdio has none, so its weak reference is left 0
    for stem, want in (("t_stdio", "___fp_print:\tequ 0"), ("t_fpio", "___fp_print:\n"),
                       ("t_stdio", "___ll_print:\tequ 0"), ("t_llio", "___ll_print:\n")):
        text = open(os.path.join(REPO, o(stem + ".asm"))).read()
        if want not in text:
            problems.append(f"{stem}.asm: no {want!r}")
    return problems


def keyhandler():
    """K: t_keyh's key handler sees the typed keys; with a guard."""
    p = build(os.path.join(TESTS, "t_keyh.c"), "t_keyh")
    if p:
        return [p]
    problems = []
    rc = emulate(["t_keyh"], ["t_keyh"], "libck", ["kq"])
    if rc != 0:
        problems.append(f"typed kq: check {rc} failed")
    rc = emulate(["t_keyh"], ["t_keyh"], "libck", ["kx"])
    if rc != 3:
        problems.append(f"guard: typed kx gave {rc}, expected 3 (the second key)")
    return problems


def conin():
    p = build(os.path.join(TESTS, "t_conin.c"), "t_conin")
    if p:
        return [p]
    problems = []
    rc = emulate(["t_conin"], ["t_conin"], "libcc", ["Bob"])
    if rc != 0:
        problems.append(f"typed Bob: check {rc} failed")
    rc = emulate(["t_conin"], ["t_conin"], "libcc", ["Bobby"])
    if rc != 2:
        problems.append(f"guard: typed Bobby gave {rc}, expected 2")
    return problems


def x():
    for s in ("t_exit", "t_after"):
        p = build(os.path.join(TESTS, s + ".c"), s)
        if p:
            return [p]
    rc = emulate(["t_exit", "t_after"], ["t_exit", "t_after"], "libcc")
    return [] if rc == 99 else [f"got {rc}, expected 99 (1: exit() returned; other: it did not reach MOS)"]


def ending():
    sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
    import testrun
    rt = testrun.runtime()
    runs = [("t_atx", "t_atx", ()), ("t_abrt", "t_abrt", ()), ("t_sig", "t_sig", ()), ("t_exd", "t_exd", ()),
            ("t_tmpx", "t_tmpx", ()),
            ("t_args", "t_args", ('"a b"', "c", '""', 'x"y"', '"d e')), ("t_echk", "t_echk", ()),
            ("t_echkb", "t_echk", ("bad",))]
    want = {"t_atx": (7, b"main\ntwo\none\n"), "t_abrt": (134, b""), "t_sig": (143, b""), "t_exd": (3, b""),
            "t_tmpx": (0, b""),
            "t_args": (0, b'6\n[a b]\n[c]\n[]\n[x"y"]\n[d e]\n'), "t_echk": (0, b""), "t_echkb": (1, b"")}
    programs = []
    for name, src, args in runs:
        if name == src:
            err = testrun.build(rt, name, [os.path.join(TESTS, src + ".c")], OUT)
            if err:
                return [f"{name}: {err}"]
        programs.append(testrun.Program(name, os.path.join(OUT, src + ".bin"), args))
    res = testrun.run(programs, "libce", timeout_each=10)
    problems = []
    for name, (status, output) in want.items():
        r = res.get(name)
        if r is None or not testrun.expected_ok(r, status, output):
            got = None if r is None else (r.status, r.output)
            problems.append(f"{name}: got {got}, expected {(status, output)}")
    return problems


def interrupts():
    sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
    import testrun
    rt = testrun.runtime()
    want = {"t_intd": 130, "t_inth": 5, "t_intk": 130, "t_intc": 130}
    for name in want:
        err = testrun.build(rt, name, [os.path.join(TESTS, name + ".c")], OUT)
        if err:
            return [f"{name}: {err}"]
    p = build(os.path.join(TESTS, "t_ichk.c"), "t_ichk")
    if p:
        return [p]
    args = [PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin",
            "--file-at", "out/existing.txt=" + FIXTURES[0]]
    for name in list(want) + ["t_ichk"]:
        args += ["--bin", o(name + ".bin")]
    for name in want:
        args += ["--cmd", f"{name} /out/{name}.txt"]
    for marker in ("m_d", "m_h", "m_k", "m_c"):
        args += ["--ctrl-c-when", "/out/" + marker]
    r = sh(args + ["--cmd", "t_ichk", "--sdcard-name", "libci", "--timeout", "120"])
    problems = [] if r.returncode == 0 else [f"t_ichk gave {r.returncode} (124: a program was never interrupted)"]
    for name, status in want.items():
        st = os.path.join(REPO, "emulator_sdcard", "libci", "out", name + ".txt.st")
        got = open(st).read().strip() if os.path.exists(st) else None
        if got != str(status):
            problems.append(f"{name}: status {got}, expected {status}")
    return problems


def environment():
    p = build(os.path.join(TESTS, "t_env.c"), "t_env")
    if p:
        return [p]
    problems = []
    env = dict(os.environ, AGONC_TEST_MOS="3.0.2")
    r = subprocess.run([PY, os.path.join("tests", "tools", "run_emulator.py"), "--bin", o("t_env.bin"),
                        "--cmd", "set AgoncTest hello there", "--cmd", "t_env", "--sdcard-name", "libcg",
                        "--timeout", "60"], cwd=REPO, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        problems.append(f"MOS 3.0.2: check {r.returncode} failed")
    rc = emulate(["t_env"], ["t_env mos2"], "libcg")
    if rc != 0:
        problems.append(f"MOS 2.3.3: check {rc} failed")
    return problems


def handles():
    """Nine runs of each AgDev-built pass in one session, then t_handles: all
    8 MOS handles must still be free (a pass leaking one loses it for good)."""
    p = build(os.path.join(TESTS, "t_handles.c"), "t_handles")
    if p:
        return [p]
    args = [PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--bin", o("t_handles.bin")]
    for b in ("cpp", "cc1", "cc2", "ld"):
        args += ["--bin", os.path.join("build", "stage1", b + ".bin")]
    for f in FIXTURES + RUNTIME + [LIBC, os.path.join(TESTS, "t_after.c")]:
        args += ["--file", f]
    args += ["--file-at", "lib/agon/mos.h=" + os.path.join("lib", "agon", "mos.h")]
    for i in range(9):
        args += ["--cmd", f"cpp t_after.c /a{i}.i", "--cmd", f"cc1 /a{i}.i /a{i}.ir -u t_after.c",
                 "--cmd", f"cc2 /a{i}.ir /a{i}.s", "--cmd", f"ld -o /a{i}.asm crt0.s rt.s libc.s /a{i}.s"]
    # (a failing command stops the script, leaving the emulator to time out: 124)
    r = sh(args + ["--cmd", "t_handles", "--sdcard-name", "libch", "--timeout", "400"])
    return [] if r.returncode == 0 else [f"t_handles gave {r.returncode} (0: all 8 handles free; 124: a pass failed)"]


def s1():
    bins = []
    for b in ("cpp", "cc1", "cc2", "ld"):
        path = os.path.join("build", "stage1", b + ".bin")
        if not os.path.exists(os.path.join(REPO, path)):
            return [f"{path} missing"]
        bins += ["--bin", path]
    # each unit at its own path, as on the SD card, since fp.c includes
    # ../../src/cc1/softfp.c and ll.c ../../src/common/int64.c
    files = ["--file-at", "src/cc1/softfp.c=" + os.path.join("src", "cc1", "softfp.c"),
             "--file-at", "src/cc1/softfp.h=" + os.path.join("src", "cc1", "softfp.h"),
             "--file-at", "src/common/int64.c=" + os.path.join("src", "common", "int64.c"),
             "--file-at", "src/common/int64.h=" + os.path.join("src", "common", "int64.h")]
    units = []
    for d in LIB_DIRS:
        for f in sorted(os.listdir(os.path.join(REPO, d))):
            if f.endswith(".c"):
                units.append(d.replace(os.sep, "/") + "/" + f)
                files += ["--file-at", units[-1] + "=" + os.path.join(d, f)]
            elif f.endswith(".h"):
                dest = "lib/agon/" + f if d.endswith("agon") else "lib/" + f
                files += ["--file-at", dest + "=" + os.path.join(d, f)]
    for f in RUNTIME + FIXTURES + [os.path.join(TESTS, "t_stdio.c"), os.path.join(TESTS, "check.h")]:
        files += ["--file", f]
    cmds = []
    for u in units + ["t_stdio.c"]:
        name = u.split("/")[-1]
        stem = name[:-2]
        cmds += ["--cmd", f"cpp /{u} /{stem}.i", "--cmd", f"cc1 /{stem}.i /{stem}.ir -u {name} -Werror",
                 "--cmd", f"cc2 /{stem}.ir /{stem}.s -O"]
    stems = [u.split("/")[-1][:-2] for u in units]
    # the units in a response file: AgDev's crt0 keeps only 14 arguments
    rsp = os.path.join(REPO, OUT, "units.rsp")
    open(rsp, "w", newline="\n").write("".join("/" + t + ".s\n" for t in stems))
    files += ["--file-at", "units.rsp=" + rsp]
    cmds += ["--cmd", "ld -o /t.asm crt0.s rt.s @units.rsp /t_stdio.s",
             "--cmd", "ez80asm /t.asm /bin/dstdio.bin -m", "--cmd", "dstdio"]
    r = sh([PY, os.path.join("tests", "tools", "run_emulator.py"), "--hold-stdin", "--with-ez80asm"] + bins + files +
           cmds + ["--sdcard-name", "libcs1", "--timeout", "2400"])
    problems = [] if r.returncode == 0 else [f"device-built t_stdio gave {r.returncode}, expected 0"]
    card = os.path.join(REPO, "emulator_sdcard", "libcs1")
    for stem in stems:
        dev = os.path.join(card, stem + ".s")
        host = os.path.join(REPO, "build", "cross", stem + ".s")
        if not os.path.exists(dev):
            problems.append(f"the device produced no {stem}.s")
        elif open(dev, "rb").read() != open(host, "rb").read():
            problems.append(f"device {stem}.s differs from the host's")
    return problems


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    if not os.path.exists(os.path.join(REPO, "build", "agon", "lib", "libc.s")):
        print("build/agon/lib/libc.s is missing: run make cross first")
        return 1
    cases = [("T4 t_libc, t_mos, t_malloc, t_stdio on the emulator + guards", t4),
             ("I  console input: a typed line reaches fgets(stdin)", conin),
             ("K  a C key handler sees typed keys, dividing alongside main; interrupt slots", keyhandler),
             ("X  exit() from deep in the stack", x),
             ("E  how programs end: atexit, streams closed, abort, signals, quoted arguments", ending),
             ("C  Ctrl-C: SIGINT's default and a handler, writing, in mos_getkey, at a prompt", interrupts),
             ("G  getenv on MOS 3.0.2 (a variable set by the script) and on MOS 2.3.3", environment)]
    if "--no-stage1" not in sys.argv:
        cases.append(("H  36 AgDev pass runs in one session leave all 8 MOS file handles free", handles))
        cases.append(("S1 the library and t_stdio built on the emulator (headers in /lib), and run", s1))
    failed = 0
    for name, fn in cases:
        problems = fn()
        print(("PASS  " if not problems else "FAIL  ") + name)
        for p in problems:
            print("      " + p)
        failed += bool(problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
