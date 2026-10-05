#!/usr/bin/env python3
"""Run a MOS program in fab-agon-emulator from a freshly built SD-card folder.

This is the ONLY supported way to start the emulator for this project.
It deletes and recreates <repo>/emulator_sdcard on every run, so nothing on
the emulated card can ever be stale.

Usage:
    run_emulator.py --bin path/to/prog.bin [--bin other.bin ...]
                    [--moslet path/to/tool.bin ...]
                    [--card-from DIR] [--file path ...] [--file-at card/path=host/path ...]
                    [--cmd "prog args" ...] [--timeout SECONDS]
                    [--sdcard-name NAME] [--gui] [--keep-open]

Each --bin is copied into /bin/ on the card; each --moslet into /mos/.
The --cmd lines (default: the first binary's base name) are written to
autoexec.txt in order, so MOS runs them at boot.  The program signals
completion by writing its exit status to eZ80 I/O port 0 (out0 (0),a),
which terminates the emulator with that byte as the process exit code;
bytes written to port 0x30 appear on the emulator's stdout and are echoed
here.  --sdcard-name selects a sub-folder of emulator_sdcard/ so parallel
runs do not collide (default "default").

Exit code: the program's port-0 status, or 124 on timeout.
"""

import argparse
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))        # AgonCompiler/
TOOLS = os.path.join(REPO, "third_party")
SDCARD = os.path.join(REPO, "emulator_sdcard")

EMULATOR_GUI = os.path.join(TOOLS, "emulator", "fab-agon-emulator.exe")
EMULATOR_CLI = os.path.join(TOOLS, "emulator", "agon-cli-emulator.exe")   # headless; default
# MOS 2.3.3 is the target; AGONC_TEST_MOS=3.0.2 runs a suite on MOS 3 instead,
# to check nothing stops the compiler working there (not part of make check).
MOS_BIN = os.path.join(TOOLS, "mos", "MOS-%s.bin" % os.environ.get("AGONC_TEST_MOS", "2.3.3"))
EZ80ASM_BIN = os.path.join(TOOLS, "ez80asm", "ez80asm.bin")    # on-device assembler


def build_sdcard(sdcard, bins, moslets, files, cmds, with_ez80asm, files_at=(), card_from=None):
    if os.path.isdir(sdcard):
        shutil.rmtree(sdcard)
    if card_from:
        shutil.copytree(card_from, sdcard)
    os.makedirs(os.path.join(sdcard, "bin"), exist_ok=True)
    os.makedirs(os.path.join(sdcard, "mos"), exist_ok=True)
    for b in bins:
        shutil.copy(b, os.path.join(sdcard, "bin", os.path.basename(b).lower()))
    for m in moslets:
        shutil.copy(m, os.path.join(sdcard, "mos", os.path.basename(m).lower()))
    for f in files:
        shutil.copy(f, os.path.join(sdcard, os.path.basename(f).lower()))
    for dest, src in files_at:
        path = os.path.join(sdcard, *dest.lower().split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        shutil.copy(src, path)
    if with_ez80asm:
        shutil.copy(EZ80ASM_BIN, os.path.join(sdcard, "bin", "ez80asm.bin"))
    with open(os.path.join(sdcard, "autoexec.txt"), "w", newline="\r\n") as f:
        for c in cmds:
            f.write(c + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", action="append", default=[], help="binary to place in /bin/")
    ap.add_argument("--moslet", action="append", default=[], help="moslet binary to place in /mos/")
    ap.add_argument("--file", action="append", default=[], help="arbitrary file to place in the card's root")
    ap.add_argument("--card-from", metavar="DIR", help="start the card as a copy of DIR (e.g. build/sdcard)")
    ap.add_argument("--file-at", action="append", default=[], metavar="DEST=SRC",
                    help="place file SRC at card path DEST (e.g. lib/string.h=lib/libc/string.h)")
    ap.add_argument("--real-speed", action="store_true", help="emulate the real 18.432 MHz clock (for timing) instead of unlimited CPU")
    ap.add_argument("--cmd", action="append", default=[], help="command line for autoexec.txt (repeatable, in order)")
    ap.add_argument("--type", action="append", default=[], dest="typed",
                    help="command to type at the MOS prompt via stdin after boot (repeatable; unlike autoexec, "
                         "a non-zero status does not stop the sequence)")
    ap.add_argument("--sdcard-name", default="default", help="sub-folder of emulator_sdcard/ to build")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--with-ez80asm", action="store_true", help="also place ez80asm.bin in /bin/")
    ap.add_argument("--gui", action="store_true", help="use the windowed emulator instead of the headless CLI one")
    ap.add_argument("--keep-open", action="store_true", help="do not pass --unlimited-cpu; leave window up until timeout")
    ap.add_argument("--hold-stdin", action="store_true",
                    help="keep the emulator's stdin open until it exits. Needed for programs that run for more "
                         "than about a second without printing (the emulator quits at end of stdin once it "
                         "thinks the machine is idle - it killed the AgDev-built ld mid-run). The run must end "
                         "with a port-0 exit or it lasts until --timeout.")
    ap.add_argument("--ctrl-c-when", action="append", default=[], metavar="PATH",
                    help="press Ctrl-C once the card path PATH exists (repeatable: each waits for its own path, "
                         "in order, after the one before). The emulator reads stdin a line at a time and sends "
                         "each byte as a key press, so this types a line holding byte 3. Implies --hold-stdin.")
    args = ap.parse_args()
    if args.ctrl_c_when:
        args.hold_stdin = True

    if not args.bin and not args.moslet and not args.cmd and not args.typed and not args.card_from:
        ap.error("need at least one --bin/--moslet, a --cmd, or a --type")
    files_at = []
    for spec in args.file_at:
        if "=" not in spec:
            ap.error("--file-at needs DEST=SRC: " + spec)
        dest, src = spec.split("=", 1)
        files_at.append((dest.strip("/"), src))
    for b in args.bin + args.moslet + args.file + [s for _, s in files_at]:
        if not os.path.isfile(b):
            ap.error("no such file: " + b)
    first = (args.bin + args.moslet)[0] if (args.bin + args.moslet) else None
    if args.cmd:
        cmds = args.cmd
    elif args.typed:
        cmds = []                      # nothing at boot; everything is typed
    else:
        cmds = [os.path.splitext(os.path.basename(first))[0].lower()]
    cmd = " / ".join(cmds) if cmds else "(none; typed: " + " / ".join(args.typed) + ")"
    typed_input = "".join(t + "\n" for t in args.typed)   # '\n' is what MOS's line editor wants here

    sdcard = os.path.join(SDCARD, args.sdcard_name)
    build_sdcard(sdcard, args.bin, args.moslet, args.file, cmds, args.with_ez80asm, files_at, args.card_from)

    # The emulator needs the MOS .map beside the .bin for host-folder SD card support.
    if not os.path.isfile(os.path.splitext(MOS_BIN)[0] + ".map"):
        print("[run_emulator] ERROR: missing symbol map next to " + MOS_BIN)
        return 2
    if args.gui:
        emulator = EMULATOR_GUI
        argv = [emulator, "--sdcard", sdcard, "--firmware", "console8", "--mos", MOS_BIN, "-z"]
    else:
        emulator = EMULATOR_CLI
        argv = [emulator, "--sdcard", sdcard, "--mos", MOS_BIN, "-z"]
    if not args.keep_open and not args.real_speed:
        argv.append("-u")
    print("[run_emulator] autoexec: " + cmd)
    print("[run_emulator] " + " ".join(argv))
    # The emulator treats end-of-file on stdin as "quit", so the stdin pipe must
    # stay open (unclosed) until the emulator exits by itself or we time out.
    p = subprocess.Popen(argv, cwd=os.path.dirname(emulator), stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out_buf, err_buf = [], []

    def pump(stream, buf):
        for chunk in iter(lambda: stream.read(4096), b""):
            buf.append(chunk)

    t_out = threading.Thread(target=pump, args=(p.stdout, out_buf), daemon=True)
    t_err = threading.Thread(target=pump, args=(p.stderr, err_buf), daemon=True)
    t_out.start()
    t_err.start()
    # Write whatever --type input there is, then close stdin immediately
    # (not after wait/timeout): the emulator treats end-of-file on stdin as
    # an implicit "quit" once it is idle at a prompt with nothing queued.
    # Leaving the pipe open past this point (e.g. to support --type) would
    # otherwise make it wait forever at an idle prompt after autoexec halts
    # or finishes with no more typed input - it has no way to know no more
    # input is coming - which previously showed up as a spurious 30s+
    # timeout on tests that expect autoexec to halt cleanly on an error.
    if typed_input:
        p.stdin.write(typed_input.encode())
        p.stdin.flush()
    if not args.hold_stdin:
        try:
            p.stdin.close()
        except OSError:
            pass

    def ctrl_c():
        for path in args.ctrl_c_when:
            full = os.path.join(sdcard, *path.strip("/").lower().split("/"))
            while not os.path.exists(full):
                if p.poll() is not None:
                    return
                time.sleep(0.05)
            try:
                p.stdin.write(b"\x03\n")
                p.stdin.flush()
            except OSError:
                return

    if args.ctrl_c_when:
        threading.Thread(target=ctrl_c, daemon=True).start()
    try:
        p.wait(timeout=args.timeout)
        timed_out = False
    except subprocess.TimeoutExpired:
        timed_out = True
        p.kill()
        p.wait()
    if args.hold_stdin:
        try:
            p.stdin.close()
        except OSError:
            pass
    t_out.join(5)
    t_err.join(5)
    stdout = b"".join(out_buf).decode(errors="replace")
    stderr = b"".join(err_buf).decode(errors="replace")
    sys.stdout.write(stdout)
    if stderr.strip():
        sys.stderr.write(stderr)
    if timed_out:
        print("[run_emulator] TIMEOUT after %.0fs" % args.timeout)
        return 124
    # The headless emulator also exits (with 0) on its own in some situations.
    # Only trust the exit code when the program actually wrote port 0.
    if "Emulator shutdown triggered by writing" not in stdout:
        print("[run_emulator] emulator ended without a port-0 exit (program never finished, or MOS stopped the script); returning 125")
        return 125
    print("[run_emulator] exit code %d" % p.returncode)
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
