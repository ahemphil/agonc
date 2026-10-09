"""Tests for <math.h> (lib/libc/math.c, and C99's: math99.c, mathf.c).

    test_math.py

H   the PC's build of math.c (math_names.h renames its functions) against
    the PC's libm, t_math_host.c: random arguments over each function's
    range; the exact functions agree exactly, the rest within 1 ulp (2 for
    sinh, cosh, tanh and log10).
G   the same with no tolerance must fail: a difference is reported.
X   C99's exactly defined functions (rounding, logb, fma, remainder, remquo,
    copysign, fdim, fmax, fmin, nextafter, ilogb, scalbn, lrint, llround,
    and the float forms of the exact ones), the PC's build against the
    PC's libm, t_math99_host.c: the same bits every time; and with each of
    our results moved one ulp, the differences reported.
R   C99's other functions, double and float, the PC's build against true
    values (math_ref.py, in decimal), since the PC's libm is not accurate
    enough to judge them: each within its tolerance in ulps (a float
    form's in a float's ulps); and with every error made one ulp worse,
    the excess reported.
A   t_math.c, built by the PC and by agonc (on the emulator): the same
    checksum of every function's results and errno over the same
    arguments, and the special cases (values and errno) pass in both.
B   the same for C99's functions, t_math99.c.
"""
import math, os, random, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "tools"))
sys.path.insert(0, HERE)
from common import EXE, HOSTCC  # noqa: E402
import testrun  # noqa: E402
import math_ref  # noqa: E402

OUT = os.path.join("build", "test", "math")
CASES = "300000"
REF_CASES = 200
DEVICE_CASES = "40"
C89 = ["-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsigned-char", "-O2",
       "-ffp-contract=off"]
C99 = ["-std=c99"] + C89[1:]
RENAMED = ["-include", os.path.join(REPO, "tests", "common", "math_names.h"), "-Wno-static-in-inline"]

# R's cases: (name, arguments, emin, emax, negative too, added, tolerance
# in ulps); a float form's tolerance is in a float's ulps. lgamma's for
# negative arguments is wide: there, near its zeros, a small result is the
# difference of two large ones (fdlibm's reflection), and only its
# relative error grows.
REF = [
    ("cbrt", 1, -1074, 1023, 1, 0, 1),
    ("log1p", 1, -60, 30, 0, 0, 1),
    ("log1p", 1, -60, -2, 1, 0, 1),
    ("expm1", 1, -60, 9, 1, 0, 1),
    ("log2", 1, -1074, 1023, 0, 0, 1),
    ("exp2", 1, -40, 10, 1, 0, 1),
    ("asinh", 1, -40, 40, 1, 0, 1),
    ("acosh", 1, -40, 40, 0, 1, 2),
    ("atanh", 1, -40, -1, 1, 0, 2),
    ("erf", 1, -40, 2, 1, 0, 1),
    ("erfc", 1, -40, 4, 1, 0, 2),
    ("lgamma", 1, -30, 7, 0, 0, 2),
    ("lgamma", 1, -6, 5, 1, 0, 16),
    ("tgamma", 1, -30, 7, 0, 0, 5),
    ("tgamma", 1, -6, 6, 1, 0, 5),
    ("hypot", 2, -1074, 1023, 1, 0, 1),
    ("sinf", 1, -30, 20, 1, 0, 1),
    ("cosf", 1, -30, 20, 1, 0, 1),
    ("tanf", 1, -30, 20, 1, 0, 1),
    ("asinf", 1, -30, -1, 1, 0, 1),
    ("acosf", 1, -30, -1, 1, 0, 1),
    ("atanf", 1, -30, 60, 1, 0, 1),
    ("expf", 1, -30, 6, 1, 0, 1),
    ("logf", 1, -149, 127, 0, 0, 1),
    ("log10f", 1, -149, 127, 0, 0, 1),
    ("sqrtf", 1, -149, 127, 0, 0, 0.5),
    ("sinhf", 1, -30, 6, 1, 0, 1),
    ("coshf", 1, -30, 6, 1, 0, 1),
    ("tanhf", 1, -30, 4, 1, 0, 1),
    ("cbrtf", 1, -149, 127, 1, 0, 1),
    ("exp2f", 1, -30, 7, 1, 0, 1),
    ("log2f", 1, -149, 127, 0, 0, 1),
    ("erff", 1, -30, 2, 1, 0, 1),
    ("erfcf", 1, -30, 3, 1, 0, 1),
    ("lgammaf", 1, -20, 6, 0, 0, 1),
    ("tgammaf", 1, -20, 5, 0, 0, 1),
    ("log1pf", 1, -30, 20, 0, 0, 1),
    ("expm1f", 1, -30, 6, 1, 0, 1),
    ("asinhf", 1, -30, 30, 1, 0, 1),
    ("acoshf", 1, -30, 30, 0, 1, 1),
    ("atanhf", 1, -30, -1, 1, 0, 1),
    ("powf", 2, -10, 10, 0, 0, 1),
    ("atan2f", 2, -30, 30, 1, 0, 1),
    ("hypotf", 2, -60, 60, 1, 0, 1),
]
FLT_MAX = 3.4028234663852886e38
DBL_MAX = 1.7976931348623157e308


def cc_run(cc, args):
    r = subprocess.run([cc] + args, capture_output=True, text=True)
    return r.stderr.strip()[:300] if r.returncode else None


def report(name, problems):
    print(("PASS  " if not problems else "FAIL  ") + name)
    for p in problems:
        print("      " + p)
    return bool(problems)


def gen(rnd, emin, emax, neg, single):
    """a random double, or float, with an exponent in [emin, emax]"""
    e = rnd.randint(emin, emax)
    if single:
        e = max(e, -126)
        u = (e + 127) << 23 | rnd.getrandbits(23)
        if neg and rnd.random() < 0.5:
            u |= 1 << 31
        return struct.unpack("<f", struct.pack("<I", u))[0]
    if e < -1022:
        u = (rnd.getrandbits(52) | 1 << 52) >> (-1022 - e)
    else:
        u = (e + 1023) << 52 | rnd.getrandbits(52)
    if neg and rnd.random() < 0.5:
        u |= 1 << 63
    return math_ref.from_bits(u)


def ref_test(exe, n, guard=False):
    """R: each case's results from the evaluator exe against the true
    values; returns the cases over their tolerance"""
    rnd = random.Random(1993)
    problems = []
    for name, nargs, emin, emax, neg, add, tol in REF:
        single = name.endswith("f") and name != "erf"
        args = []
        for _ in range(n):
            a = [gen(rnd, emin, emax, neg, single) + add for _ in range(nargs)]
            if name == "powf":
                a[1] = gen(rnd, -10, 3, 1, True)        # an exponent that keeps most results in range
            if single:
                a = [struct.unpack("<f", struct.pack("<f", v))[0] for v in a]
            args.append(a)
        inp = "".join(name + "".join(" %016x" % math_ref.bits(v) for v in a) + "\n" for a in args)
        out = subprocess.run([exe], input=inp, capture_output=True, text=True).stdout.split()
        over = 0
        first = None
        for a, h in zip(args, out):
            got = struct.unpack("<f", struct.pack("<I", int(h, 16)))[0] if single else math_ref.from_bits(int(h, 16))
            t = math_ref.true_value(name, a)
            if abs(t) > math_ref.D(FLT_MAX if single else DBL_MAX) * (1 + math_ref.D(2) ** -25):
                e = 0.0 if math.isinf(got) and (got > 0) == (t > 0) else float("inf")      # an overflow
            else:
                e = math_ref.ulps(got, t, single)
            if guard:
                e = e + 1
            if e > tol:
                over += 1
                if first is None:
                    first = f"{name}({', '.join(repr(v) for v in a)}): {e:.2f} ulps"
        if len(out) < n:
            problems.append(f"{name}: the evaluator gave {len(out)} results of {n}")
        elif over:
            problems.append(f"{name}: {over} over {tol} ulps, the first {first}")
    return problems


def device_pair(stem, extra, cases):
    """A and B: stem.c built by the PC (with the objects extra) and by
    agonc; both outputs must be the same, and the PC's must pass"""
    o = lambda name: os.path.join(REPO, OUT, name)
    std = C99 if stem == "t_math99" else C89
    err = cc_run(HOSTCC, std + RENAMED + ["-o", o(stem + "_pc" + EXE), os.path.join(REPO, "tests", "common",
                                                                                  stem + ".c")] + extra)
    if err:
        return ["host build: " + err]
    r = subprocess.run([o(stem + "_pc" + EXE), cases], capture_output=True, text=True)
    host_out = r.stdout
    if r.returncode != 0:
        return ["the PC's build: " + l for l in host_out.splitlines() if "failed" in l][:8] or [f"status {r.returncode}"]
    rt = testrun.runtime()
    err = testrun.build(rt, stem, [os.path.join("tests", "common", stem + ".c")], OUT)
    if err:
        return ["agonc build: " + err[:300]]
    res = testrun.run([testrun.Program(stem, os.path.join(OUT, stem + ".bin"), [cases])], "math", timeout_each=900)
    got = res[stem]
    dev = got.output.decode("latin-1").replace("\r\n", "\n") if got.output else ""
    if got.status != 0 or dev != host_out:
        return [f"status {got.status}"] + [f"PC {h!r} / Agon {d!r}" for h, d in zip(host_out.splitlines(),
                                                                               dev.splitlines()) if h != d][:8]
    return []


def main():
    os.makedirs(os.path.join(REPO, OUT), exist_ok=True)
    cc = HOSTCC
    o = lambda name: os.path.join(REPO, OUT, name)
    src = lambda path: os.path.join(REPO, path)
    failed = 0
    err = cc_run(cc, C89 + RENAMED + ["-c", "-o", o("math.o"), src("lib/libc/math.c")]) or \
        cc_run(cc, C99 + RENAMED + ["-c", "-o", o("math99.o"), src("lib/libc/math99.c")]) or \
        cc_run(cc, C99 + RENAMED + ["-c", "-o", o("mathf.o"), src("lib/libc/mathf.c")]) or \
        cc_run(cc, C89 + ["-DSOFTFP_FMA=1", "-c", "-o", o("softfp.o"), src("src/cc1/softfp.c")]) or \
        cc_run(cc, ["-std=c99", "-Wall", "-Werror", "-O2", "-ffp-contract=off", "-o", o("t_math_host" + EXE),
                    src("tests/common/t_math_host.c"), o("math.o"), o("softfp.o")])
    objs = [o("math.o"), o("math99.o"), o("mathf.o"), o("softfp.o")]
    if err:
        problems = ["host build: " + err]
        guard = ["not run"]
    else:
        r = subprocess.run([o("t_math_host" + EXE), CASES], capture_output=True, text=True)
        problems = [] if r.returncode == 0 and "0 functions over" in r.stdout else \
            [l for l in r.stdout.splitlines() if not l.endswith("over 0")][:12] or [f"status {r.returncode}"]
        g = subprocess.run([o("t_math_host" + EXE), "1000", "guard"], capture_output=True, text=True)
        guard = [] if g.returncode != 0 and "sin        max 1, over" in g.stdout else ["the differences went unreported"]
    failed += report(f"H  the PC's build against the PC's libm ({CASES} cases each)", problems)
    failed += report("G  with no tolerance, a difference is reported", guard)

    err = err or cc_run(cc, ["-std=c99", "-Wall", "-Werror", "-O2", "-ffp-contract=off", "-o",
                             o("t_math99_host" + EXE), src("tests/common/t_math99_host.c")] + objs) or \
        cc_run(cc, ["-std=c99", "-Wall", "-Werror", "-O2", "-ffp-contract=off", "-o", o("t_math99_eval" + EXE),
                    src("tests/common/t_math99_eval.c")] + objs)
    if err:
        problems = ["host build: " + err]
    else:
        r = subprocess.run([o("t_math99_host" + EXE), "100000"], capture_output=True, text=True)
        problems = [] if r.returncode == 0 and "0 functions over" in r.stdout else \
            [l for l in r.stdout.splitlines() if not l.endswith("over 0")][:12] or [f"status {r.returncode}"]
        g = subprocess.run([o("t_math99_host" + EXE), "1000", "guard"], capture_output=True, text=True)
        if g.returncode == 0 or "trunc        max 1, over 1000" not in g.stdout:
            problems.append("guard: a result one ulp off went unreported")
    failed += report("X  C99's exact functions, the PC's build against the PC's libm (100000 cases each)",
                     problems)
    if err:
        problems = ["not run"]
    else:
        problems = ref_test(o("t_math99_eval" + EXE), REF_CASES)
        if not ref_test(o("t_math99_eval" + EXE), 20, guard=True):
            problems.append("guard: every result one ulp worse, and nothing reported")
    failed += report(f"R  C99's other functions, double and float, against true values ({REF_CASES} cases each)",
                     problems)

    problems = ["host build: " + err] if err else device_pair("t_math", [o("math.o"), o("softfp.o")], DEVICE_CASES)
    failed += report(f"A  built by agonc, on the emulator: the PC's checksums ({DEVICE_CASES} cases each) and "
                     "the special cases", problems)
    problems = ["host build: " + err] if err else device_pair("t_math99", objs, DEVICE_CASES)
    failed += report(f"B  C99's, t_math99.c, the same ({DEVICE_CASES} cases each)", problems)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
