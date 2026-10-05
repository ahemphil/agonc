"""Tests for the test runner (tests/tools/testrun.py), which the library,
arithmetic and conformance tests use to capture what programs print.

    test_testrun.py

R  eight programs in one batch on the emulator. Each must give exactly its
   expected output and status: printing (including printf), exit() with a
   status, a return status with no output, abort(), arguments after the
   output-file argument, a file read from the program's own folder, a
   program that hangs (status None), and one after it, which must still
   run, in a new session.
G  the guard: an output or status that differs by one byte or one number
   is reported as a mismatch.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import testrun  # noqa: E402

OUT = os.path.join("build", "test", "harness")
T = os.path.join("tests", "harness")

# name, arguments, files, expected status, expected output
CASES = [
    ("h_print", [], [], 0, b"hello\na5b\n"),
    ("h_exit", [], [], 7, b"x"),
    ("h_ret", [], [], 3, b""),
    ("h_abort", [], [], 134, b"before\n"),
    ("h_args", ["one", "two"], [], 0, b"3 one two\n"),
    ("h_file", [], [("in.txt", os.path.join(T, "in.txt"))], 0, b"data line\n"),
    ("h_hang", [], [], None, b""),
    ("h_after", [], [], 0, b"after\n"),
]


def main():
    rt = testrun.runtime()
    programs = []
    problems = []
    for name, args, files, _, _ in CASES:
        err = testrun.build(rt, name, [os.path.join(T, name + ".c")], OUT)
        if err:
            problems.append(f"{name} does not build: {err}")
        programs.append(testrun.Program(name, os.path.join(OUT, name + ".bin"), args, files))
    if problems:
        print("FAIL  R  the runner's programs build\n" + "\n".join("      " + p for p in problems))
        return 1
    res = testrun.run(programs, "harness", timeout_each=10)
    for name, _, _, status, output in CASES:
        r = res.get(name)
        if r is None:
            problems.append(f"{name}: no result")
        elif not testrun.expected_ok(r, status, output):
            problems.append(f"{name}: status {r.status!r} output {r.output!r}, expected {status!r} {output!r}")
    print(("PASS  " if not problems else "FAIL  ") + "R  eight programs: output, status, exit, abort, "
          "arguments, a file, a hang and recovery")
    for p in problems:
        print("      " + p)
    guard = []
    r = res.get("h_print")
    if r is not None and testrun.expected_ok(r, 0, b"hello\na6b\n"):
        guard.append("a one-byte output difference was not noticed")
    if r is not None and testrun.expected_ok(r, 1, b"hello\na5b\n"):
        guard.append("a status difference was not noticed")
    print(("PASS  " if not guard else "FAIL  ") + "G  a differing output or status is a mismatch")
    for g in guard:
        print("      " + g)
    failed = bool(problems) + bool(guard)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
