"""Paths and helpers shared by the test suites.

Every path here is relative to the top of the repository, the folder the
suites run in: ez80asm rejects file names over 64 characters, so paths are
kept short. The suites test what `make` built (make stage1, sdcard, cross);
`make check` builds those first.
"""
import os, re, shutil, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = sys.executable
EXE = ".exe" if os.name == "nt" else ""

HOST = os.path.join("build", "host")                  # make host: the passes for this computer
STAGE1 = os.path.join("build", "stage1")              # make stage1: AgDev-built passes
AGON = os.path.join("build", "agon")                  # make cross: the compiler as an SD-card tree
CROSS = os.path.join("build", "cross")                # make cross: the C library's units
SDCARD = os.path.join("build", "sdcard")              # make sdcard: the bootstrap card
LIBC = os.path.join(AGON, "lib", "agonc", "libc.s")
LIBM = os.path.join(AGON, "lib", "agonc", "libm.s")    # the floating-point part
LIBAGON = os.path.join(AGON, "lib", "agonc", "libagon.s")  # the MOS and VDU interface
LIBAGON_UNITS = ["uart", "vdp", "mosapi", "sysvar", "vdpsys", "vdpbmp", "vdpaudio", "vdpbuf", "vdpmore", "handler"]  # lib/agon/*.c in libagon.s, as the Makefile and bootstrap list them
RUNTIME = [os.path.join("lib", "rt", "crt0.s"), os.path.join("lib", "rt", "rt.s")]
INCLUDE = ["-I", os.path.join("lib", "libc"), "-I", "lib"]      # headers as the Agon's /lib/agonc and /lib/agonc/agon
LIB_SOURCES = [os.path.join("lib", "libc"), os.path.join("lib", "agon")]
EZ80ASM = os.path.join("third_party", "bin", "ez80asm" + EXE)   # ez80asm built for this computer
RUN_EMULATOR = os.path.join("tests", "tools", "run_emulator.py")
PASSES = ["cpp", "cc1", "cc2", "ld"]
PROGS = PASSES + ["agonc"]
OPT = ["-O0"] if os.environ.get("AGONC_TEST_O0") else []   # AGONC_TEST_O0=1: test builds without the optimiser


def _hostcc():
    """The PC's C compiler, as the Makefile has it: $HOSTCC (make exports
    it), else config.local.mk's or config.mk's HOSTCC line, else cc."""
    if os.environ.get("HOSTCC"):
        return os.environ["HOSTCC"]
    for name in ("config.local.mk", "config.mk"):
        try:
            with open(os.path.join(REPO, name)) as f:
                for line in f:
                    m = re.match(r"\s*HOSTCC\s*\??=\s*(\S.*?)\s*$", line)
                    if m:
                        return m.group(1)
        except OSError:
            pass
    return "cc"


HOSTCC = _hostcc()
MOSLET_STACK = 2048             # the least stack the moslet driver may be left


def host(prog):
    return os.path.join(HOST, prog + EXE)


def stage1(prog):
    return os.path.join(STAGE1, prog + ".bin")


def agon(prog):
    """The cross-built Agon binary of a program."""
    return os.path.join(AGON, "mos", "agonc.bin") if prog == "agonc" else os.path.join(AGON, "bin", "agonc", prog + ".bin")


def rsp_sources(prog):
    """A program's sources, in order, as its bootstrap response file lists them."""
    words = open(os.path.join(REPO, "bootstrap", prog + ".rsp")).read().split()
    return [w for w in words if w.endswith(".c")]


def stage_root(dest):
    """A library layout (driver.md section 4) under dest for the host
    driver's AGONC_ROOT: lib/agonc/ (headers, crt0.s, rt.s, libc.s, libm.s,
    libagon.s), lib/agonc/agon/, and empty usrlib/ and tmp/agonc/. Rebuilt
    from scratch."""
    dest = os.path.join(REPO, dest)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    for d in ("lib/agonc/agon", "usrlib", "tmp/agonc"):
        os.makedirs(os.path.join(dest, d))
    for f in os.listdir(os.path.join(REPO, "lib", "libc")):
        if f.endswith(".h"):
            shutil.copy(os.path.join(REPO, "lib", "libc", f), os.path.join(dest, "lib", "agonc"))
    for f in os.listdir(os.path.join(REPO, "lib", "agon")):
        if f.endswith(".h"):
            shutil.copy(os.path.join(REPO, "lib", "agon", f), os.path.join(dest, "lib", "agonc", "agon"))
    for f in RUNTIME + [LIBC, LIBM, LIBAGON]:
        shutil.copy(os.path.join(REPO, f), os.path.join(dest, "lib", "agonc"))


def host_env(root):
    """The environment the host driver needs: AGONC_ROOT, and ez80asm on the PATH."""
    env = dict(os.environ)
    env["AGONC_ROOT"] = root
    env["PATH"] = os.path.join(REPO, "third_party", "bin") + os.pathsep + env["PATH"]
    return env
