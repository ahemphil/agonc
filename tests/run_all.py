"""Run every test suite and report each; `make check` runs this after its
builds (make lint, sdcard and cross).

    run_all.py

The suites: the runtime (rt), the compiler's 32-bit arithmetic (int32) and
IEEE arithmetic (softfp), ld,
cc2, cc1, cpp, the C library, the examples, the test runner (harness), the driver, the self-hosting fixed point (T5) and the
memory profile check (T6). Every
suite runs even if an earlier one failed. Run from PowerShell on Windows
(BUILDING.md section 11). Exit status 0 only if every suite passed. About
fifteen minutes.
"""
import os, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY = sys.executable
SUITES = [os.path.join("tests", s) for s in
          ("rt/test_rt.py", "common/test_int32.py", "common/test_int64.py", "common/test_softfp.py", "common/test_math.py", "ld/test_ld.py", "cc2/test_cc2.py", "cc1/test_cc1.py", "cpp/test_cpp.py",
           "libc_c/test_libc_c.py", "examples/test_examples.py", "harness/test_testrun.py", "driver/test_driver.py", "selfhost/test_selfhost.py",
           "tools/memprofile.py")]


def main():
    failed = []
    for s in SUITES:
        start = time.time()
        r = subprocess.run([PY, s], cwd=REPO, capture_output=True, text=True)
        print(f"{'PASS  ' if r.returncode == 0 else 'FAIL  '} {s} ({time.time() - start:.0f} s)", flush=True)
        if r.returncode:
            failed.append(s)
            print("".join("        " + l + "\n" for l in r.stdout.splitlines() if not l.startswith("PASS")))
    print(f"{len(failed)} of {len(SUITES)} suites failed" + (f": {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
