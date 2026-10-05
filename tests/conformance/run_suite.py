"""Run a downloaded C test suite on the emulator and report how much passes.

    run_suite.py ctest|gcc|sdcc [--limit N] [--only NAME...]
    run_suite.py --update-skips

The suites are fetched by fetch_suites.py into third_party/suites/ (not in
git); this script skips cleanly when one is missing. Which tests count is
fixed in git by the skip lists in skip/: <suite>.txt, generated from the
classifier (classify.py) by --update-skips, and <suite>.manual.txt, added
by hand. Every line names a test and why it is skipped. Every other test is
attempted: compiled with the host driver against the test runtime
(tests/tools/testrun.py) and run on the emulator. A C89 test is compiled in
strict mode (-ansi, the conforming mode: c89_spec.md section 1), which it
must pass; one that is not C89 in the default mode: the classifier's
tests with // comments or GNU spellings (GNU_soft), those whose only later
standard is C99's long long (which the default mode has), and those
listed with their reasons in skip/<suite>.default.txt. Each result line
gives the mode, and the summary counts each mode apart.

  ctest  c-testsuite: passes when it exits 0 and prints exactly its
         .expected output.
  gcc    GCC's C torture execution tests: passes when it exits 0 (a failing
         test calls abort()).

  sdcc   SDCC's regression tests: each test file (a .c.in template once for
         every combination of its values) with the __runSuite table of its
         test functions appended, as SDCC's case generator does, built with
         our framework (tests/conformance/sdcc: testfwk.h, testfwk.c) as
         SDCC's generic host port (-DPORT_HOST), and linked with the
         sources of SDCC's framework library (fwk/lib: statics.c,
         extern1.c, extern2.c), which some tests call; passes when it exits
         0, which our framework does only if every ASSERT held. A file
         passes when all its instances do.

A measurement, not part of make check. Run the suites one at a time: they share the test runtime, its build, and the
emulator card. Results go to
build/test/conformance/<suite>.txt.
"""
import csv, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
from common import REPO  # noqa: E402

SUITES = os.path.join(REPO, "third_party", "suites")
SKIP = os.path.join(HERE, "skip")
OUT = os.path.join("build", "test", "conformance")
TESTS = {
    "ctest": "c-testsuite/tests/single-exec",
    "gcc": "gcc/gcc/testsuite/gcc.c-torture/execute",
    "sdcc": "sdcc-regression/tests",
}
# The classifier's categories that make a test unusable (conformance_suites.md §1).
CATEGORIES = ("C99", "GNU_hard", "INT32", "ENV")
INFO = {"file", "lines", "C99", "GNU_hard", "GNU_soft", "INT32", "FLOAT", "ENV", "includes", "lib_funcs",
        "lib_headers", "notes", "max_static_bytes", "sdcc_test_funcs", "line_comments", "pp_uncertain"}
# The classifier's C99 flags for long long, which the default mode provides.
LONG_LONG = {"c99_long_long", "c99_long_long_suffix", "c99_huge_const"}


def later_standard(row):
    """The classifier's flags for a later standard than C89 (its C99
    category) that a row has."""
    return {k for k, v in row.items() if v not in ("", "0", None)
            and (k.startswith(("c99_", "c11_", "c23_")) or k in ("c95_header", "dg_std_c99plus"))}


def long_long_only(row):
    """Is C99's long long the only later standard a row uses?"""
    flags = later_standard(row)
    return bool(flags) and flags <= LONG_LONG


def update_skips():
    """Regenerate skip/<suite>.txt from the classifier's CSVs."""
    os.makedirs(SKIP, exist_ok=True)
    for suite in TESTS:
        table = os.path.join(SUITES, "out", suite + ".csv")
        if not os.path.exists(table):
            print(f"{suite}: no classifier output (run fetch_suites.py): left alone")
            continue
        lines = []
        for row in csv.DictReader(open(table)):
            cats = [c for c in CATEGORIES if row.get(c) not in ("", "0", None)
                    and not (c == "C99" and long_long_only(row))]
            no_checks = row.get("no_checks") not in ("", "0", None)
            if not cats and not no_checks:
                continue
            flags = sorted(k for k, v in row.items() if k not in INFO and not k.startswith(("cov_", "hdr"))
                           and v not in ("", "0", None) and k != "no_checks")
            reason = " ".join(cats + (["no-checks"] if no_checks else [])) + (" (" + ", ".join(flags) + ")" if flags else "")
            lines.append(f"{row['file']} {reason}")
        with open(os.path.join(SKIP, suite + ".txt"), "w", newline="\n") as f:
            f.write(f"# {suite}: tests skipped by the classifier (run_suite.py --update-skips).\n")
            f.write("# Categories: C99 (a later standard), GNU_hard (a GNU extension not provided),\n")
            f.write("# INT32 (assumes int has 32 bits), ENV (something MOS cannot do), no-checks.\n")
            for line in sorted(lines):
                f.write(line + "\n")
        print(f"{suite}: {len(lines)} skipped, written to tests/conformance/skip/{suite}.txt")
    return 0


def default_mode(suite):
    """The test files that are not C89, and so run in the default mode: the
    classifier's with // comments (which strict mode, as C89 requires, does
    not take as comments), GNU spellings or long long, and
    skip/<suite>.default.txt's."""
    names = set()
    table = os.path.join(SUITES, "out", suite + ".csv")
    if os.path.exists(table):
        for row in csv.DictReader(open(table)):
            if any(row.get(k) not in ("", "0", None) for k in ("line_comments", "GNU_soft")) or long_long_only(row):
                names.add(row["file"])
    path = os.path.join(SKIP, suite + ".default.txt")
    if os.path.exists(path):
        for line in open(path):
            if line.strip() and not line.startswith("#"):
                names.add(line.split()[0])
    return names


def skipped(suite):
    names = set()
    for name in (suite + ".txt", suite + ".manual.txt"):
        path = os.path.join(SKIP, name)
        if os.path.exists(path):
            for line in open(path):
                if line.strip() and not line.startswith("#"):
                    names.add(line.split()[0])
    return names


FWK = os.path.join("tests", "conformance", "sdcc")
GEN = os.path.join(OUT, "sdcc_gen")
SUITE_TABLE = """
void
__runSuite(void)
{
%s}

const int __numCases = %d;

__code const char *
__getSuiteName(void)
{
  return "%s";
}
"""


def sdcc_instances(path):
    """SDCC's case generator (cases/generate-cases.py), rewritten without
    HTMLgen: a .c.in template's substitutions ("name: v1, v2" lines in its
    first comment) in every combination, "{name}" replaced in the text, and
    the table of its test functions appended. Returns (instance name, text)
    for each."""
    text = open(path, encoding="latin-1").read()
    base = os.path.basename(path)
    template = base.endswith(".c.in")
    stem = base[:-5] if template else base[:-2]
    repl = []
    funcs = []
    in_header = True
    for line in text.split("\n"):
        t = line.strip()
        if in_header:
            if template and ":" in t:
                name, values = t.split(":", 1)
                repl.append((name.strip(), [v.strip() for v in values.split(",")]))
            elif "*/" in t:
                in_header = False
        else:
            m = re.match(r"^(?:\W*void\W+)?\W*(test\w*)\W*\(\W*void\W*\)", t)
            if m:
                funcs.append(m.group(1))
    calls = "".join(f"  {f}();\n" for f in funcs)
    out = []

    def permute(name, rest, trans):
        if not rest:
            body = re.sub(r"\{(\w+)\}", lambda m: trans.get(m.group(1), m.group(0)), text)
            out.append((name, body + SUITE_TABLE % (calls, len(funcs), name)))
            return
        key, values = rest[0]
        for v in values:
            part = re.sub(r"\s+", "_", v or "none")
            trans2 = dict(trans)
            trans2[key] = v
            permute(name + "_" + key + "_" + part, rest[1:], trans2)

    permute(stem, repl, {})
    return out


def cases(suite):
    """(program name, source, expected output or None) for a suite's attempted tests."""
    base = os.path.join(SUITES, *TESTS[suite].split("/"))
    if not os.path.isdir(base):
        return None
    skip = skipped(suite)
    found = []
    if suite == "sdcc":
        os.makedirs(os.path.join(REPO, GEN), exist_ok=True)
        k = 0
        for f in sorted(x for x in os.listdir(base) if x.endswith(".c") or x.endswith(".c.in")):
            if f in skip:
                continue
            for name, body in sdcc_instances(os.path.join(base, f)):
                k += 1
                if len(name) > 56:              # cc1 takes a unit name of up to 60
                    name = f"{name[:48]}_{k}"
                src = os.path.join(GEN, name + ".c")
                open(os.path.join(REPO, src), "w", encoding="latin-1", newline="\n").write(body)
                found.append((f"s{k:04d}", src, None, f))
        return found
    for i, f in enumerate(sorted(x for x in os.listdir(base) if x.endswith(".c"))):
        if f in skip:
            continue
        src = os.path.relpath(os.path.join(base, f), REPO)
        if suite == "ctest":
            found.append(("c" + f[:-2], src, open(os.path.join(base, f + ".expected"), "rb").read(), f))
        else:
            found.append((f"g{i:04d}", src, None, f))
    return found


def main():
    import testrun
    args = sys.argv[1:]
    if args == ["--update-skips"]:
        return update_skips()
    if not args or args[0] not in ("ctest", "gcc", "sdcc"):
        print(__doc__)
        return 2
    suite = args[0]
    todo = cases(suite)
    if todo is None:
        print(f"{suite} is not in third_party/suites (run fetch_suites.py): skipped")
        return 0
    if "--limit" in args:
        todo = todo[:int(args[args.index("--limit") + 1])]
    if "--only" in args:
        want = set(args[args.index("--only") + 1:])
        todo = [c for c in todo if c[0] in want or os.path.basename(c[1]) in want]
    rt = testrun.runtime()
    bin_dir = os.path.join(OUT, suite)
    lines, programs, info = [], [], {}
    lax = default_mode(suite)
    flags = []
    extra = []
    if suite == "sdcc":
        flags = ["-DPORT_HOST", "-I", FWK]
        fwk_s = os.path.join(OUT, "testfwk.s")
        r = testrun.driver(["-ansi", "-DPORT_HOST", "-I", FWK, "-c", "-o", fwk_s, os.path.join(FWK, "testfwk.c")])
        if r.returncode:
            print("sdcc: testfwk.c does not compile: " + (r.stderr or r.stdout))
            return 1
        extra = [fwk_s]
        lib = os.path.join(SUITES, "sdcc-regression", "fwk", "lib")
        for f in ("statics.c", "extern1.c", "extern2.c"):
            s_path = os.path.join(OUT, "fwk_" + f[:-2] + ".s")
            r = testrun.driver(["-DPORT_HOST", "-I", FWK, "-c", "-o", s_path, os.path.join(lib, f)])
            if r.returncode:
                print(f"sdcc: fwk/lib/{f} does not compile: " + (r.stderr or r.stdout))
                return 1
            extra.append(s_path)
    of_file = {}
    mode_of = {}
    for name, src, exp, orig in todo:
        test = os.path.basename(src)
        of_file[test] = orig
        mode_of[test] = "default" if orig in lax else "strict"
        err = testrun.build(rt, name, [src] + extra, bin_dir, (["-ansi"] if mode_of[test] == "strict" else []) + flags)
        if err:
            lines.append((test, "compile", err.splitlines()[0] if err else ""))
            continue
        programs.append(testrun.Program(name, os.path.join(bin_dir, name + ".bin")))
        info[name] = (test, exp)
    results = testrun.run(programs, "conformance", timeout_each=10) if programs else {}
    for p in programs:
        test, exp = info[p.name]
        r = results[p.name]
        if r.status is None:
            lines.append((test, "hang", "no status (hung or crashed)"))
        elif r.status == 0 and (exp is None or testrun.expected_ok(r, 0, exp)):
            lines.append((test, "pass", ""))
        elif r.status != 0:
            lines.append((test, "fail", f"status {r.status}"))
        else:
            lines.append((test, "fail", f"output differs ({len(r.output)} bytes, expected {len(exp)})"))
    lines.sort()
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    with open(os.path.join(REPO, OUT, suite + ".txt"), "w", newline="\n") as f:
        for test, kind, note in lines:
            f.write(f"{test} {kind} [{mode_of[test]}] {note}".rstrip() + "\n")
    for mode in ("strict", "default"):
        counts = {}
        mine = [kind for test, kind, _ in lines if mode_of[test] == mode]
        for kind in mine:
            counts[kind] = counts.get(kind, 0) + 1
        print(f"{suite} ({mode} mode): {len(mine)} attempted: " +
              ", ".join(f"{counts.get(k, 0)} {k}" for k in ("pass", "fail", "hang", "compile")))
    if suite == "sdcc":
        for mode in ("strict", "default"):
            files = {}
            for test, kind, _ in lines:
                if mode_of[test] == mode:
                    orig = of_file.get(test, test)
                    files[orig] = files.get(orig, True) and kind == "pass"
            print(f"sdcc ({mode} mode): {sum(1 for v in files.values() if v)} of {len(files)} files pass "
                  "(all their instances)")
    print("details: " + os.path.join(OUT, suite + ".txt"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
