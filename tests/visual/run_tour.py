"""Builds tests/visual/vdu_tour.c with the host compiler and runs it in the
windowed emulator, for a person to look at (make visual).

    run_tour.py [--build-only]

The tour's screens each say what should be on them; note the number of
any that does not match. Needs make cross first. Run from PowerShell on
Windows (BUILDING.md section 11).
"""
import os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from common import REPO, PY, RUN_EMULATOR, host, host_env, stage_root  # noqa: E402

ROOT = os.path.join("build", "visual", "root")
OUT = os.path.join("build", "visual")


def main():
    stage_root(ROOT)
    out = os.path.join(OUT, "vdu_tour.bin")
    r = subprocess.run([host("agonc"), "-o", out, os.path.join("tests", "visual", "vdu_tour.c")],
                       cwd=REPO, env=host_env(ROOT), capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return 1
    print("built", out)
    if "--build-only" in sys.argv:
        return 0
    # --hold-stdin: the emulator quits at end of input on stdin, so keep the
    # pipe open until the window is closed
    return subprocess.run([PY, RUN_EMULATOR, "--gui", "--keep-open", "--hold-stdin", "--bin", out, "--cmd", "vdu_tour",
                           "--sdcard-name", "visual", "--timeout", "3600"], cwd=REPO).returncode


if __name__ == "__main__":
    sys.exit(main())
