"""Generate exec.ir: a hand-written-IR program exercising every cc2 node kind
and pattern, for T2-T4 (see test_cc2.py).

The checks run in functions part1, part2, ... (25 each); each computes a
value in IR and compares it with the expected value, counting mismatches in
the global gfails. main() calls them and exits through agon_emu_exit(gfails),
or with --first, the 1-based number of the first failing check (0 if none).
The exit status is one byte, so a check numbered above 255 is reported
modulo 256 (check 270 shows as 14): bisect with --upto=N when in doubt.

Frame of each part (ABI section 5: scalars nearest IX, then arrays):
  (unused) -3, x -6, y -9, c (char) -12, p -15, buf[10] -25..-16,
  big[200] -225..-26 (beyond the (ix+d) window).
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from unitid import unit_id  # noqa: E402

FAILS, X, Y, C, P, BUF, BIG = -3, -6, -9, -12, -15, -25, -225
FRAME = 225


def s24(v):
    v &= 0xFFFFFF
    return v - 0x1000000 if v & 0x800000 else v


def lines(*items):
    out = []
    for it in items:
        out += it.split("\n") if isinstance(it, str) else list(it)
    return [l.strip() for l in out if l.strip()]


def ld_local(off, size=3, sign="s"):
    return [f"LA {off}", f"LD {size} {sign}"]


def st_local(off, value_lines, size=3):
    return [f"LA {off}"] + lines(value_lines) + [f"ST {size}", "DROP"]


def bin_(op, a, b):
    return [f"C {s24(a)}", f"C {s24(b)}", op]


# ---- the checks: (name, setup statements (each ends in DROP) or [], value IR, expected)

CHECKS = []


def check(name, value, expected, setup=()):
    CHECKS.append((name, lines(*setup) if setup else [], lines(value), s24(expected), "i"))


M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def check_l(name, value, expected, setup=()):
    """A check whose value is a long (IR 2's L kind), compared as 32 bits."""
    CHECKS.append((name, lines(*setup) if setup else [], lines(value), s32(expected), "l"))


def cl(v):
    return f"C.l {s32(v)}"


def lbin(op, a, b):
    return [cl(a), cl(b), op]


# arithmetic
check("add", bin_("ADD", 7, 5), 12)
check("sub", bin_("SUB", 7, 5), 2)
check("sub neg", bin_("SUB", 5, 7), -2)
check("add const right", ["C 100", "C -1", "ADD"], 99)
check("mul", bin_("MUL", -3, 4), -12)
check("mul big", bin_("MUL", 1000, 1000), 1000000)
# R2b: multiplication by a constant inline (shifts, or shifts and adds),
# or through __imul where that is shorter
for _k in (2, 3, 5, 12, 21, 23, 256, 512, 1000, 1024):
    check(f"mul by {_k}", bin_("MUL", -7, _k), -7 * _k)
check("mul by 23 wraps", bin_("MUL", 400000, 23), 400000 * 23)
# R2b: a byte condition tested in A (a LAND makes each a jump)
_neg = [["A gbyte", "C -1", "ST 1", "DROP"]]
check("byte eq signed", ["A gbyte", "LD 1 s", "C -1", "EQ", "C 1", "LAND"], 1, setup=_neg)
check("byte eq unsigned", ["A gbyte", "LD 1 u", "C 255", "EQ", "C 1", "LAND"], 1, setup=_neg)
check("byte eq out of range", ["A gbyte", "LD 1 s", "C 255", "EQ", "C 1", "LAND"], 0, setup=_neg)
check("byte ne 0", ["A gbyte", "LD 1 u", "C 0", "NE", "C 1", "LAND"], 1, setup=_neg)
check("byte as condition", ["A gbyte", "LD 1 u", "C 1", "LAND"], 1, setup=_neg)
check("zero byte as condition", ["A gbyte", "LD 1 s", "C 1", "LAND"], 0,
      setup=[["A gbyte", "C 0", "ST 1", "DROP"]])
check("local byte eq", [f"LA {C}", "LD 1 s", "C 127", "EQ", "C 1", "LAND"], 1, setup=[st_local(C, ["C 127"], 1)])
# R2b: a jump comparing with a constant
_x = [st_local(X, ["C -5"])]
check("jump eq const", ld_local(X) + ["C -5", "EQ", "C 1", "LAND"], 1, setup=_x)
check("jump ne const", ld_local(X) + ["C -5", "NE", "C 1", "LAND"], 0, setup=_x)
check("jump eq 0", ld_local(X) + ["C 0", "EQ", "C 1", "LAND"], 0, setup=_x)
check("jump ne 0", ld_local(X) + ["C 0", "NE", "C 1", "LAND"], 1, setup=_x)
check("jump ltu const", ld_local(X) + ["C 7", "LTU", "C 1", "LAND"], 0, setup=_x)
check("jump geu const", ld_local(X) + ["C 7", "GEU", "C 1", "LAND"], 1, setup=_x)
check("jump ltu big", ld_local(X) + ["C -4", "LTU", "C 1", "LAND"], 1, setup=_x)
# R2b: stores through a computed address of a value DE can take, with the
# value discarded (DROP) and used
check("byte store via pointer, dropped", [f"LA {BUF}", "C 1", "ADD", "LD 1 s"], 77,
      setup=[[f"LA {BUF}", "C 1", "ADD", "C 77", "ST 1", "DROP"]])
check("int store via pointer, value used", [f"LA {BUF}", "C 2", "ADD", "C 1234", "ST 3"], 1234)
check("int store via pointer, stored", [f"LA {BUF}", "C 2", "ADD", "LD 3 s"], -9,
      setup=[[f"LA {BUF}", "C 2", "ADD", "C -9", "ST 3", "DROP"]])
check("divs", bin_("DIVS", -7, 2), -3)
check("rems", bin_("REMS", -7, 2), -1)
check("divu", bin_("DIVU", 7, 2), 3)
check("remu", bin_("REMU", 7, 2), 1)
check("divu big", ["C -2", "C 2", "DIVU"], 0x7FFFFF)
check("shl", bin_("SHL", 1, 20), 1 << 20)
check("shrs", bin_("SHRS", -16, 2), -4)
check("shru", bin_("SHRU", -16, 2), 0x3FFFFC)
check("and", bin_("AND", 0xF0F0F0, 0x0FFF0F), 0x00F000)
check("or", bin_("OR", 0x800001, 0x7F0000), 0xFF0001)
check("xor", bin_("XOR", -1, 0x123456), 0xEDCBA9)
check("neg", ["C 5", "NEG"], -5)
check("neg min", ["C -8388608", "NEG"], -8388608)
check("cpl 0", ["C 0", "CPL"], -1)
check("cpl 5", ["C 5", "CPL"], -6)
check("not 0", ["C 0", "NOT"], 1)
check("not 7", ["C 7", "NOT"], 0)
check("not upper byte only", ["C -8388608", "NOT"], 0)

# comparisons as values (materialised), incl. differences only in the upper byte
for op, a, b, want in [
    ("EQ", 5, 5, 1), ("EQ", 65536, 0, 0), ("NE", 65536, 0, 1), ("NE", 3, 3, 0),
    ("LTS", -1, 1, 1), ("LTS", 1, -1, 0), ("LTS", 4, 4, 0),
    ("LTU", -1, 1, 0), ("LTU", 1, -1, 1),
    ("LES", 4, 4, 1), ("LES", 5, 4, 0), ("LEU", 4, 4, 1), ("LEU", -1, 4, 0),
    ("GTS", 1, -1, 1), ("GTS", -1, 1, 0), ("GTU", -1, 1, 1), ("GTU", 4, 4, 0),
    ("GES", 4, 4, 1), ("GES", -5, 4, 0), ("GEU", 4, 4, 1), ("GEU", 3, -3, 0),
    ("LTS", -8388608, 8388607, 1), ("GTS", -8388608, 8388607, 0),
]:
    check(f"{op} {a},{b} value", bin_(op, a, b), want)
    # the same comparison as a branch condition, through SEL -> gen_jump
    check(f"{op} {a},{b} branch", bin_(op, a, b) + ["C 11", "C 22", "SEL"], 11 if want else 22)

# EXT
check("ext 200 s", ["C 200", "EXT 1 s"], -56)
check("ext 200 u", ["C 200", "EXT 1 u"], 200)
check("ext -1 u", ["C -1", "EXT 1 u"], 255)
check("ext 300 u", ["C 300", "EXT 1 u"], 44)

# logical and conditional
check("land 3,0", ["C 3", "C 0", "LAND"], 0)
check("land 3,4", ["C 3", "C 4", "LAND"], 1)
check("lor 0,0", ["C 0", "C 0", "LOR"], 0)
check("lor 0,9", ["C 0", "C 9", "LOR"], 1)
check("land short-circuits", ld_local(X), 5,
      setup=[st_local(X, ["C 5"]), ["C 0", f"LA {X}", "INCPOST 3 s 1", "LAND", "DROP"]])
check("lor short-circuits", ld_local(X), 5,
      setup=[st_local(X, ["C 5"]), ["C 1", f"LA {X}", "INCPOST 3 s 1", "LOR", "DROP"]])
check("land evaluates right", ld_local(X), 6,
      setup=[st_local(X, ["C 5"]), ["C 1", f"LA {X}", "INCPOST 3 s 1", "LAND", "DROP"]])
check("not land in branch", ["C 1", "C 0", "LAND", "NOT", "C 7", "C 8", "SEL"], 7)
check("lor in branch", ["C 0", "C 1", "LOR", "C 7", "C 8", "SEL"], 7)
check("sel true", ["C 1", "C 10", "C 20", "SEL"], 10)
check("sel false", ["C 0", "C 10", "C 20", "SEL"], 20)
check("sel on value", [f"LA {X}", "LD 3 s", "C 10", "C 20", "SEL"], 10, setup=[st_local(X, ["C 65536"])])
check("seq", [f"LA {X}", "C 5", "ST 3", "C 9", "SEQ"], 9)
check("seq side effect", ld_local(X), 5, setup=[[f"LA {X}", "C 5", "ST 3", "C 9", "SEQ", "DROP"]])

# locals, globals, bytes
check("local int", ld_local(X), 123456, setup=[st_local(X, ["C 123456"])])
check("st yields value", [f"LA {Y}", "C 777", "ST 3"], 777)
check("char local signed", ld_local(C, 1, "s"), -5, setup=[st_local(C, ["C -5", "EXT 1 s"], 1)])
check("char local unsigned", ld_local(C, 1, "u"), 251, setup=[st_local(C, ["C -5", "EXT 1 s"], 1)])
check("global int", ["A gcount", "LD 3 s"], 77, setup=[["A gcount", "C 77", "ST 3", "DROP"]])
check("global byte signed", ["A gtab", "LD 1 s"], 1)
check("global byte 1", ["A gtab", "C 1", "ADD", "LD 1 s"], -2)
check("global T item", ["A gtab", "C 2", "ADD", "LD 3 s"], 100000)
check("global byte store", ["A gbyte", "LD 1 u"], 250, setup=[["A gbyte", "C 250", "EXT 1 s", "ST 1", "DROP"]])
check("static data", ["A .sstat", "LD 3 s"], 4242)
check("static bss", ["A .sbss", "LD 3 s"], 9, setup=[["A .sbss", "C 9", "ST 3", "DROP"]])
check("data address item", ["A gptr", "LD 3 s", "LD 3 s"], 100000)
check("data string item", ["A gmsg", "C 2", "ADD", "LD 1 s"], 10)
check("data zero item", ["A gtab", "C 5", "ADD", "LD 3 s"], 0)
check("computed byte store/load", [f"LA {BUF}", "C 3", "ADD", "LD 1 s"], 66,
      setup=[[f"LA {BUF}", "C 3", "ADD", "C 66", "ST 1", "DROP"]])
check("computed int store/load", [f"LA {BUF}", "C 4", "ADD", "LD 3 s"], -99999,
      setup=[[f"LA {BUF}", "C 4", "ADD", "C -99999", "ST 3", "DROP"]])
check("big frame store/load", ld_local(BIG), 42, setup=[st_local(BIG, ["C 42"])])
check("big frame byte", ld_local(BIG + 5, 1, "s"), -3, setup=[st_local(BIG + 5, ["C -3", "EXT 1 s"], 1)])
check("pointer in local", [f"LA {P}", "LD 3 s", "LD 3 s"], 55,
      setup=[st_local(P, [f"LA {BIG}"]), st_local(BIG, ["C 55"])])

# compound assignment
check("asg add local", [f"LA {X}", "C 5", "ASG ADD 3 s"], 15, setup=[st_local(X, ["C 10"])])
check("asg sub local after", ld_local(X), 3, setup=[st_local(X, ["C 10"]), [f"LA {X}", "C 7", "ASG SUB 3 s", "DROP"]])
check("asg mul global", ["A gcount", "C 3", "ASG MUL 3 s"], 21, setup=[["A gcount", "C 7", "ST 3", "DROP"]])
check("asg char wraps", [f"LA {C}", "C 100", "ASG ADD 1 s"], -56, setup=[st_local(C, ["C 100", "EXT 1 s"], 1)])
check("asg char stored", ld_local(C, 1, "u"), 200, setup=[st_local(C, ["C 100", "EXT 1 s"], 1), [f"LA {C}", "C 100", "ASG ADD 1 s", "DROP"]])
check("asg via pointer", [f"LA {BUF}", "C 1", "ADD", "C 5", "ASG SHL 3 s"], 96,
      setup=[[f"LA {BUF}", "C 1", "ADD", "C 3", "ST 3", "DROP"]])
check("asg byte via pointer", [f"LA {BUF}", "C 7", "ADD", "C 1", "ASG SUB 1 u"], 255,
      setup=[[f"LA {BUF}", "C 7", "ADD", "C 0", "ST 1", "DROP"]])
check("asg big frame", [f"LA {BIG}", "C 8", "ASG OR 3 s"], 13, setup=[st_local(BIG, ["C 5"])])

# increments
check("incpost value", [f"LA {X}", "INCPOST 3 s 1"], 10, setup=[st_local(X, ["C 10"])])
check("incpost stored", ld_local(X), 11, setup=[st_local(X, ["C 10"]), [f"LA {X}", "INCPOST 3 s 1", "DROP"]])
check("incpre", [f"LA {X}", "INCPRE 3 s 1"], 11, setup=[st_local(X, ["C 10"])])
check("decpost delta 3", [f"LA {X}", "DECPOST 3 s 3"], 10, setup=[st_local(X, ["C 10"])])
check("decpre delta 3", [f"LA {X}", "DECPRE 3 s 3"], 7, setup=[st_local(X, ["C 10"])])
check("incpre char wraps", [f"LA {C}", "INCPRE 1 s 1"], -128, setup=[st_local(C, ["C 127", "EXT 1 s"], 1)])
check("decpost uchar", [f"LA {C}", "DECPOST 1 u 1"], 0, setup=[st_local(C, ["C 0"], 1)])
check("decpost uchar stored", ld_local(C, 1, "u"), 255, setup=[st_local(C, ["C 0"], 1), [f"LA {C}", "DECPOST 1 u 1", "DROP"]])
check("inc global", ["A gcount", "INCPRE 3 s 1"], 1, setup=[["A gcount", "C 0", "ST 3", "DROP"]])
check("inc via pointer post", [f"LA {BUF}", "C 2", "ADD", "INCPOST 3 s 6"], 1000,
      setup=[[f"LA {BUF}", "C 2", "ADD", "C 1000", "ST 3", "DROP"]])
check("inc via pointer stored", [f"LA {BUF}", "C 2", "ADD", "LD 3 s"], 1006,
      setup=[[f"LA {BUF}", "C 2", "ADD", "C 1000", "ST 3", "DROP"], [f"LA {BUF}", "C 2", "ADD", "INCPOST 3 s 6", "DROP"]])
check("inc byte via pointer", [f"LA {BUF}", "C 0", "ADD", "INCPRE 1 s 1"], -127,
      setup=[[f"LA {BUF}", "C -128", "ST 1", "DROP"]])
check("inc byte via pointer post", [f"LA {BUF}", "C 0", "ADD", "INCPOST 1 u 1"], 128,
      setup=[[f"LA {BUF}", "C -128", "ST 1", "DROP"]])
check("inc big frame", [f"LA {BIG}", "INCPOST 3 s 1"], 5, setup=[st_local(BIG, ["C 5"])])

# struct-style copy
check("copy", [f"LA {BUF}", "C 2", "ADD", "LD 3 s"], 100000,
      setup=[[f"LA {BUF}", "A gtab", "COPY 5", "DROP"]])
check("copy yields dst", [f"LA {BUF}", "A gtab", "COPY 5", f"LA {BUF}", "EQ"], 1)

# strings in the pool
check("string byte 0", ["SA 1", "LD 1 s"], 72)
check("string byte 4", ["SA 1", "C 4", "ADD", "LD 1 s"], 111)
check("string NUL", ["SA 1", "C 5", "ADD", "LD 1 s"], 0)
check("second string", ["SA 2", "C 1", "ADD", "LD 1 u"], 200)

# calls
check("arg order", ["C 3", "ARG", "C 2", "ARG", "C 1", "ARG", "CALL add3 3"], 123)
check("five args", ["C 5", "ARG", "C 4", "ARG", "C 3", "ARG", "C 2", "ARG", "C 1", "ARG", "CALL sum5 5"], 54321)
check("nested call arg", ["C 3", "ARG", "C 2", "ARG", "C 1", "ARG", "C 0", "ARG", "C 0", "ARG",
                          "CALL add3 3", "ARG", "CALL add3 3"], 123)
check("call in expression", ["C 1000", "C 3", "ARG", "C 2", "ARG", "C 1", "ARG", "CALL add3 3", "ADD"], 1123)
check("static recursion", ["C 10", "ARG", "CALL .fact 1"], 3628800)
# a streamed function: its frame, locals and switch table after the body
check("streamed case 1", ["C 1", "ARG", "CALL streamed 1"], 107)
check("streamed case 2", ["C 2", "ARG", "CALL streamed 1"], 20)
check("streamed default", ["C 9", "ARG", "CALL streamed 1"], 3)
check("streamed block static", ["A .sbss2", "LD 3 s"], 2, setup=[["C 2", "ARG", "CALL streamed 1", "DROP"]])
check("strlen of pool string", ["SA 1", "ARG", "CALL mystrlen 1"], 5)
check("char return canonical", ["C 200", "ARG", "CALL tochar 1"], -56)
check("inline asm", ["CALL asmfn 0"], 4242 + 3)
check("switch 2", ["C 2", "ARG", "CALL sw 1"], 20)
check("switch 7", ["C 7", "ARG", "CALL sw 1"], 70)
check("switch default", ["C 3", "ARG", "CALL sw 1"], -1)
# R2b: a dense switch through a jump table: every case, a gap, both ends
for _v, _r in ((-2, 1), (-1, 2), (0, 3), (1, 4), (3, 5), (2, 99), (-3, 99), (4, 99), (-8388608, 99)):
    check(f"jump table {_v}", [f"C {_v}", "ARG", "CALL dense 1"], _r)
check("loop sum", ["C 100", "ARG", "CALL sumto 1"], 5050)
check("big loop body", ["CALL farjump 0"], 300)

# ---- longs (IR 2's L kind, M8) and shorts ---------------------------------------
LA_L1, LA_L2, LA_SH = BUF, BUF + 4, BUF + 8     # two longs and a short in buf[]

A_ = 0x12345678
B_ = 0x00FFFFFF
for op, fn in [("ADD.l", lambda a, b: a + b), ("SUB.l", lambda a, b: a - b), ("MUL.l", lambda a, b: a * b),
               ("AND.l", lambda a, b: a & b), ("OR.l", lambda a, b: a | b), ("XOR.l", lambda a, b: a ^ b)]:
    for a, b in [(A_, B_), (B_, 1), (0xFFFFFFFF, 0x80000001), (0x7FFFFFFF, 0x7FFFFFFF)]:
        check_l(f"{op} {a:x},{b:x}", lbin(op, a, b), fn(a, b))
for a, b in [(1000000007, 97), (0xFFFFFFFF, 3), (0x80000000, 7), (A_, 0x10000)]:
    check_l(f"DIVU.l {a:x},{b:x}", lbin("DIVU.l", a, b), a // b)
    check_l(f"REMU.l {a:x},{b:x}", lbin("REMU.l", a, b), a % b)
for a, b in [(-1000000007, 97), (1000000007, -97), (-7, 2), (0x7FFFFFFF, -1)]:
    q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
    check_l(f"DIVS.l {a},{b}", lbin("DIVS.l", a, b), q)
    check_l(f"REMS.l {a},{b}", lbin("REMS.l", a, b), a - q * b)
for n in (0, 1, 8, 23, 24, 31):
    check_l(f"SHL.l {n}", [cl(A_), f"C {n}", "SHL.l"], A_ << n)
    check_l(f"SHRU.l {n}", [cl(0x87654321), f"C {n}", "SHRU.l"], 0x87654321 >> n)
    check_l(f"SHRS.l {n}", [cl(0x87654321), f"C {n}", "SHRS.l"], s32(0x87654321) >> n)
check_l("NEG.l", [cl(A_), "NEG.l"], -A_)
check_l("NEG.l 0", [cl(0), "NEG.l"], 0)
check_l("CPL.l", [cl(A_), "CPL.l"], ~A_)
check("NOT.l 0", [cl(0), "NOT.l"], 1)
check("NOT.l high byte only", [cl(0x01000000), "NOT.l"], 0)
check("NOT.l low only", [cl(1), "NOT.l"], 0)
for op, a, b in [("EQ.l", A_, A_), ("EQ.l", A_, A_ ^ 0x01000000), ("NE.l", 0x01000000, 0), ("NE.l", 5, 5),
                 ("LTS.l", -1, 1), ("LTS.l", 1, -1), ("LTU.l", -1, 1), ("LTU.l", 1, -1),
                 ("LES.l", 7, 7), ("LEU.l", 0xFFFFFFFF, 0), ("GTS.l", 0x7FFFFFFF, -0x80000000),
                 ("GTU.l", 0x80000000, 0x7FFFFFFF), ("GES.l", -5, -5), ("GEU.l", 0x01000000, 0x00FFFFFF),
                 ("LTS.l", -0x80000000, 0x7FFFFFFF), ("GES.l", -0x80000000, 0x7FFFFFFF)]:
    x, y = s32(a), s32(b)
    ux, uy = a & M32, b & M32
    want = {"EQ": x == y, "NE": x != y, "LTS": x < y, "LTU": ux < uy, "LES": x <= y, "LEU": ux <= uy,
            "GTS": x > y, "GTU": ux > uy, "GES": x >= y, "GEU": ux >= uy}[op[:-2]]
    check(f"{op} {a},{b} value", lbin(op, a, b), int(want))
    check(f"{op} {a},{b} branch", lbin(op, a, b) + ["C 11", "C 22", "SEL"], 11 if want else 22)
check("long as condition", [cl(0x01000000), "C 11", "C 22", "SEL"], 11)
check("long 0 as condition", [cl(0), "C 11", "C 22", "SEL"], 22)
check("long in land", [cl(0x100), "C 1", "LAND"], 1)
check_l("sel of longs", ["C 1", cl(A_), cl(B_), "SEL"], A_)

# conversions
check_l("int -5 to long", ["C -5", "CV i3s l4s"], -5)
check_l("uint to long", ["C -1", "CV i3u l4u"], 0x00FFFFFF)
check_l("char -2 to long", [f"LA {C}", "LD 1 s", "CV i1s l4s"], -2, setup=[st_local(C, ["C -2", "EXT 1 s"], 1)])
check_l("uchar 254 to long", [f"LA {C}", "LD 1 u", "CV i1u l4s"], 254, setup=[st_local(C, ["C -2", "EXT 1 s"], 1)])
check("long to int", [cl(A_), "CV l4s i3s"], s24(A_))
check("long to short", [cl(0x1238000), "CV l4s i2s"], -32768)
check("long to ushort", [cl(0x1238000), "CV l4u i2u"], 0x8000)
check("long to char", [cl(0x1FF), "CV l4s i1s"], -1)

# memory
check_l("long local", [f"LA {LA_L1}", "LD.l"], A_, setup=[[f"LA {LA_L1}", cl(A_), "ST.l", "DROP"]])
check_l("st.l yields value", [f"LA {LA_L2}", cl(B_), "ST.l"], B_)
check_l("long global", ["A glong", "LD.l"], 0x80000001, setup=[["A glong", cl(0x80000001), "ST.l", "DROP"]])
check_l("long via pointer", [f"LA {BUF}", "C 5", "ADD", "LD.l"], 0xCAFEF00D,
        setup=[[f"LA {BUF}", "C 5", "ADD", cl(0xCAFEF00D), "ST.l", "DROP"]])
check_l("long big frame", [f"LA {BIG}", "LD.l"], -123456789, setup=[[f"LA {BIG}", cl(-123456789), "ST.l", "DROP"]])
check_l("long in data", ["A gldata", "LD.l"], 0x89ABCDEF)
check_l("asg.l add", [f"LA {LA_L1}", cl(1), "ASG.l ADD s"], 0x01000000, setup=[[f"LA {LA_L1}", cl(B_), "ST.l", "DROP"]])
check_l("asg.l stored", [f"LA {LA_L1}", "LD.l"], 0x01000000,
        setup=[[f"LA {LA_L1}", cl(B_), "ST.l", "DROP"], [f"LA {LA_L1}", cl(1), "ASG.l ADD s", "DROP"]])
check_l("asg.l shl", ["A glong", "C 4", "ASG.l SHL s"], 0x23456780, setup=[["A glong", cl(A_), "ST.l", "DROP"]])
check_l("asg.l divs", [f"LA {BUF}", "C 5", "ADD", cl(-3), "ASG.l DIVS s"], -33,
        setup=[[f"LA {BUF}", "C 5", "ADD", cl(100), "ST.l", "DROP"]])
check_l("incpost.l value", [f"LA {LA_L1}", "INCPOST.l"], B_, setup=[[f"LA {LA_L1}", cl(B_), "ST.l", "DROP"]])
check_l("incpost.l stored", [f"LA {LA_L1}", "LD.l"], 0x01000000,
        setup=[[f"LA {LA_L1}", cl(B_), "ST.l", "DROP"], [f"LA {LA_L1}", "INCPOST.l", "DROP"]])
check_l("decpre.l", ["A glong", "DECPRE.l"], 0xFFFFFFFF, setup=[["A glong", cl(0), "ST.l", "DROP"]])

# calls
check_l("long args and result", [cl(B_), "ARG", cl(A_), "ARG", "CALL ladd2 2 l"], A_ + B_)
check_l("mixed slots", ["C 3", "ARG", cl(0x01000000), "ARG", "C 2", "ARG", "CALL mixed 3 l"], 0x01000000 + 200 + 3)
check("long switch 1", [cl(100000), "ARG", "CALL lsw 1"], 1)
check("long switch 2", [cl(-1), "ARG", "CALL lsw 1"], 2)
check("long switch 3", [cl(0x01000000), "ARG", "CALL lsw 1"], 3)
check("long switch high byte only", [cl(0x02000000), "ARG", "CALL lsw 1"], 0)
check("long switch default", [cl(5), "ARG", "CALL lsw 1"], 0)
check_l("long local in callee", ["CALL lret 0 l"], 0x76543210)

# shorts
check("short store/load s", [f"LA {LA_SH}", "LD 2 s"], -2, setup=[[f"LA {LA_SH}", "C -2", "EXT 2 s", "ST 2", "DROP"]])
check("short load u", [f"LA {LA_SH}", "LD 2 u"], 65534, setup=[[f"LA {LA_SH}", "C -2", "EXT 2 s", "ST 2", "DROP"]])
check("short store keeps the next byte", [f"LA {LA_SH}", "C 2", "ADD", "LD 1 s"], 77,
      setup=[[f"LA {LA_SH}", "C 2", "ADD", "C 77", "ST 1", "DROP"], [f"LA {LA_SH}", "C -1", "ST 2", "DROP"]])
check("ext 2 s", ["C 40000", "EXT 2 s"], 40000 - 65536)
check("ext 2 u", ["C -1", "EXT 2 u"], 65535)
check("short global", ["A gshort", "LD 2 s"], -300, setup=[["A gshort", "C -300", "ST 2", "DROP"]])
check("short via pointer", [f"LA {BUF}", "C 1", "ADD", "LD 2 u"], 40000,
      setup=[[f"LA {BUF}", "C 1", "ADD", "C 40000", "ST 2", "DROP"]])
check("short asg wraps", [f"LA {LA_SH}", "C 1", "ASG ADD 2 s"], -32768,
      setup=[[f"LA {LA_SH}", "C 32767", "ST 2", "DROP"]])
check("short asg via pointer", [f"LA {BUF}", "C 1", "ADD", "C 3", "ASG MUL 2 u"], 30000 * 3 - 65536,
      setup=[[f"LA {BUF}", "C 1", "ADD", "C 30000", "ST 2", "DROP"]])
check("short incpre wraps", [f"LA {LA_SH}", "INCPRE 2 s 1"], -32768, setup=[[f"LA {LA_SH}", "C 32767", "ST 2", "DROP"]])
check("short incpost value", ["A gshort", "INCPOST 2 u 1"], 65535, setup=[["A gshort", "C -1", "ST 2", "DROP"]])
check("short incpost stored", ["A gshort", "LD 2 u"], 0,
      setup=[["A gshort", "C -1", "ST 2", "DROP"], ["A gshort", "INCPOST 2 u 1", "DROP"]])
check("short dec via pointer", [f"LA {BUF}", "C 1", "ADD", "DECPOST 2 s 1"], -32768,
      setup=[[f"LA {BUF}", "C 1", "ADD", "C -32768", "ST 2", "DROP"]])
check("short dec via pointer stored", [f"LA {BUF}", "C 1", "ADD", "LD 2 s"], 32767,
      setup=[[f"LA {BUF}", "C 1", "ADD", "C -32768", "ST 2", "DROP"],
             [f"LA {BUF}", "C 1", "ADD", "DECPOST 2 s 1", "DROP"]])

# ---- program text ------------------------------------------------------------


# ---- M9: structs by value (ARGB, RETB, call kind b) and call kind j
# struct S { char a; int b; char c[5]; } is 9 bytes (three slots); gs holds
# a = -3, b = 1000, c = "ABCDE". struct T { char x[4]; } is 4 bytes (two
# slots); gt holds 1, 2, 3, 4.
check("struct argument", ["C 5", "ARG", "A gs", "ARGB 9", "CALL ssum 2 i"], -3 + 1000 + ord("E") + 5)
check("4-byte struct between ints", ["C 100", "ARG", "A gt", "ARGB 4", "C 10", "ARG", "CALL tget 3 i"],
      1 + 4 * 10 + 100)
check("struct result", ["C 42", "ARG", f"LA {BUF}", "ARG", "CALL smake 2 b", "C 1", "ADD", "LD 3 s"], 42)
check("struct result, last byte", ["C 42", "ARG", f"LA {BUF}", "ARG", "CALL smake 2 b", "C 8", "ADD", "LD 1 s"], 9)
check("struct result far from IX", ["C -6", "ARG", f"LA {BIG}", "ARG", "CALL smake 2 b", "C 1", "ADD", "LD 3 s"], -6)
check("struct result as argument", ["C 1", "ARG", "C 3", "ARG", f"LA {BUF}", "ARG", "CALL smake 2 b", "ARGB 9",
                                    "CALL ssum 2 i"], 7 + 3 + 9 + 1)
check("two struct arguments", ["A gs", "ARGB 9", "A gt", "ARGB 4", "CALL stwo 2 i"], 4 + ord("A"))
check("implicit-kind call", ["C 3", "ARG", "C 2", "ARG", "C 1", "ARG", "CALL add3 3 j"], 123)
# ---- M10: calls through a pointer (CALLI)
check("call through a pointer", ["C 3", "ARG", "C 2", "ARG", "C 1", "ARG", "A add3", "CALLI 3 i"], 123)
check("call through a pointer in a variable", ["C 9", "ARG", "C 8", "ARG", "C 7", "ARG", f"LA {P}", "LD 3 u", "CALLI 3 i"],
      789, setup=[[f"LA {P}", "A add3", "ST 3", "DROP"]])
check_l("long result through a pointer", [cl(B_), "ARG", cl(A_), "ARG", "A ladd2", "CALLI 2 l"], A_ + B_)
check("struct result through a pointer", ["C 42", "ARG", f"LA {BUF}", "ARG", "A smake", "CALLI 2 b", "C 1", "ADD", "LD 3 s"],
      42)

PART = 25       # checks per test function, so no function is unrealistically big


def part_function(p, checks, first_base, first_mode):
    """int partP(void): runs its checks, counting failures in gfails."""
    body = [f"F part{p} g 0 {FRAME} -", 'S 1 "Hello"', 'S 2 "a\\xc8"']
    for i, (name, setup, value, want, kind) in enumerate(checks, 1):
        k = first_base + i
        body.append(f"# {k}: {name}")
        body += setup
        body += value
        body += ([f"C.l {want}", "NE.l"] if kind == "l" else [f"C {want}", "NE"])
        body += [f"JF {i}", "A gfails", "INCPOST 3 s 1", "DROP"]
        if first_mode:
            body += ["A firstfail", "LD 3 s", "C 0", "EQ", f"JF {i}", "A firstfail", f"C {k}", "ST 3", "DROP"]
        body.append(f"L {i}")
    return body + ["RETV", "E"]


def test_functions(first_mode):
    out, calls = [], []
    for p in range(0, len(CHECKS), PART):
        n = p // PART + 1
        out += part_function(n, CHECKS[p:p + PART], p, first_mode)
        calls += [f"CALL part{n} 0", "DROP"]
    exit_val = ["A firstfail", "LD 3 s"] if first_mode else ["A gfails", "LD 3 s"]
    out += (["F main g 0 0 -", "A gfails", "C 0", "ST 3", "DROP"] + calls +
            exit_val + ["ARG", "CALL agon_emu_exit 1", "DROP", "C 0", "RET", "E"])
    return out

LONG_HELPERS = """
# long ladd2(long a, long b) { return a + b; }  (a: slots ix+6, ix+9; b: ix+12, ix+15)
F ladd2 g 4 0 -
LA 6
LD.l
LA 12
LD.l
ADD.l
RET
E
# long mixed(int a, long b, int c) { return b + a * 100 + c; }  (a: ix+6, b: ix+9, c: ix+15)
F mixed g 4 0 -
LA 9
LD.l
LA 6
LD 3 s
C 100
MUL
CV i3s l4s
ADD.l
LA 15
LD 3 s
CV i3s l4s
ADD.l
RET
E
# int lsw(long v) { switch (v) { case 100000: 1; case -1: 2; case 0x1000000: 3; } return 0; }
F lsw g 2 0 -
LA 6
LD.l
SW 9 3
K 100000 1
K -1 2
K 16777216 3
L 1
C 1
RET
L 2
C 2
RET
L 3
C 3
RET
L 9
C 0
RET
E
# long lret(void) { long x; x = 0x76543210; return x; }  (x: two slots at ix-6)
F lret g 0 6 -
LA -6
C.l 1985229328
ST.l
DROP
LA -6
LD.l
RET
E
"""

STRUCT_HELPERS = """
# int ssum(struct S s, int k) { return s.a + s.b + s.c[4] + k; }  (s: ix+6..ix+14; k: ix+15)
F ssum g 4 0 -
LA 6
LD 1 s
LA 7
LD 3 s
ADD
LA 14
LD 1 s
ADD
LA 15
LD 3 s
ADD
RET
E
# int tget(int k, struct T t, int m) { return t.x[0] + t.x[3] * k + m; }  (k ix+6, t ix+9 and ix+12, m ix+15)
F tget g 4 0 -
LA 9
LD 1 s
LA 12
LD 1 s
LA 6
LD 3 s
MUL
ADD
LA 15
LD 3 s
ADD
RET
E
# int stwo(struct T t, struct S s) { return t.x[3] + s.c[0]; }  (t ix+6 and ix+9, s ix+12..ix+20)
F stwo g 5 0 -
LA 9
LD 1 s
LA 16
LD 1 s
ADD
RET
E
# struct S smake(int b) { struct S r; r.a = 7; r.b = b; r.c[4] = 9; return r; }  (result pointer ix+6, b ix+9, r at -9)
F smake g 2 9 r
LA -9
C 7
ST 1
DROP
LA -8
LA 9
LD 3 s
ST 3
DROP
LA -1
C 9
ST 1
DROP
LA -9
RETB 9
E
"""

HELPERS = """
# int add3(int a, int b, int c) { return a*100 + b*10 + c; }
F add3 g 3 0 -
LA 6
LD 3 s
C 100
MUL
LA 9
LD 3 s
C 10
MUL
ADD
LA 12
LD 3 s
ADD
RET
E
# int sum5(a,b,c,d,e) = a + 10b + 100c + 1000d + 10000e
F sum5 g 5 0 -
LA 6
LD 3 s
LA 9
LD 3 s
C 10
MUL
ADD
LA 12
LD 3 s
C 100
MUL
ADD
LA 15
LD 3 s
C 1000
MUL
ADD
LA 18
LD 3 s
C 10000
MUL
ADD
RET
E
# static int fact(int n) { if (n < 2) return 1; return n * fact(n - 1); }
F .fact s 1 0 -
LA 6
LD 3 s
C 2
LTS
JF 1
C 1
RET
L 1
LA 6
LD 3 s
LA 6
LD 3 s
C 1
SUB
ARG
CALL .fact 1
MUL
RET
E
# int streamed(int x) { static int seen; static int k = 7; int v = x; seen = x;
#     switch (v) { case 1: return 100 + k; case 2: { int w = 20; return w; } } return 3; }
# written as cc1 streams it (ir_format.md 4): F's frame "@", locals as LA @k,
# SW @0, a block-scope G and D inside the body, and FRAME, LOC and SWT at the end
F streamed g 1 @ -
G .sbss2 3 s
D .sdat2 s
T 7
E
LA @2
LA 6
LD 3 s
ST 3
DROP
A .sbss2
LA 6
LD 3 s
ST 3
DROP
LA @2
LD 3 s
SW @0
L 1
C 100
A .sdat2
LD 3 s
ADD
RET
L 2
LA @5
C 20
ST 3
DROP
LA @5
LD 3 s
RET
L 3
C 3
RET
FRAME 6
LOC 2 -3
LOC 5 -6
SWT 0 3 2
K 1 1
K 2 2
E
# int mystrlen(char *s) { int n; n = 0; while (*s) { s++; n++; } return n; }
F mystrlen g 1 3 -
LA -3
C 0
ST 3
DROP
L 1
LA 6
LD 3 s
LD 1 s
JF 2
LA 6
INCPOST 3 s 1
DROP
LA -3
INCPOST 3 s 1
DROP
J 1
L 2
LA -3
LD 3 s
RET
E
# char tochar(int v) { return v; }
F tochar g 1 0 -
LA 6
LD 3 s
EXT 1 s
RET
E
# int asmfn(void) { int r; asm("ld hl,4242 / ld (ix-3),hl"); loop across it; return r + 3; }
F asmfn g 0 6 -
LA -6
C 0
ST 3
DROP
L 1
LA -6
LD 3 s
C 3
LTS
JF 2
ASM 2
        ld      hl, 4242
        ld      (ix-3), hl
LA -6
INCPOST 3 s 1
DROP
J 1
L 2
LA -3
LD 3 s
LA -6
LD 3 s
ADD
RET
E
# int sw(int v) { switch (v) { case 2: return 20; case 7: return 70; default: return -1; } }
F sw g 1 0 -
LA 6
LD 3 s
SW 3 2
K 2 1
K 7 2
L 1
C 20
RET
L 2
C 70
RET
L 3
C -1
RET
E
# int dense(int v) { switch (v) { case -2: return 1; case -1: return 2; case 0: return 3;
#     case 1: return 4; case 3: return 5; default: return 99; } }   (R2b: a jump table)
F dense g 1 0 -
LA 6
LD 3 s
SW 6 5
K -2 1
K -1 2
K 0 3
K 1 4
K 3 5
L 1
C 1
RET
L 2
C 2
RET
L 3
C 3
RET
L 4
C 4
RET
L 5
C 5
RET
L 6
C 99
RET
E
# int sumto(int n) { int i, s; s = 0; for (i = 1; i <= n; i++) s += i; return s; }
F sumto g 1 6 -
LA -6
C 0
ST 3
DROP
LA -3
C 1
ST 3
DROP
L 1
LA -3
LD 3 s
LA 6
LD 3 s
LES
JF 2
LA -6
LA -3
LD 3 s
ASG ADD 3 s
DROP
LA -3
INCPOST 3 s 1
DROP
J 1
L 2
LA -6
LD 3 s
RET
E
"""


def farjump():
    # a loop whose body is long enough (>127 bytes) that its branches need jp
    body = ["F farjump g 0 3 -", "LA -3", "C 0", "ST 3", "DROP", "L 1",
            "LA -3", "LD 3 s", "C 300", "LTS", "JF 2"]
    for _ in range(40):
        body += ["LA -3", "LD 3 s", "C 0", "ADD", "DROP"]
    body += ["LA -3", "INCPOST 3 s 1", "DROP", "J 1", "L 2", "LA -3", "LD 3 s", "RET", "E"]
    return body


def program(first_mode=False):
    uid = unit_id("exec.c")
    out = ["IR 2", f"U exec.c {uid}", "G gcount 3 g", "G gbyte 1 g", "G firstfail 3 g", "G gfails 3 g", "G .sbss 3 s",
           "G glong 4 g", "G gshort 2 g", "D gldata g", "Q -1985229329", "E",
           "D gtab g", "B 1", "B -2", "T 100000", "Z 3", "E",
           "D .sstat s", "T 4242", "E",
           "D gptr g", "A gtab 2", "E",
           "D gmsg g", 'S "Hi\\x0a"', "B 0", "E",
           "D gs g", "B -3", "T 1000", 'S "ABCDE"', "E",
           "D gt g", "B 1", "B 2", "B 3", "B 4", "E"]
    out += lines(HELPERS)
    out += lines(LONG_HELPERS)
    out += lines(STRUCT_HELPERS)
    out += farjump()
    out += test_functions(first_mode)
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    first = "--first" in sys.argv
    for a in sys.argv[1:]:
        if a.startswith("--upto="):      # keep only the first N checks (bisecting a hang)
            del CHECKS[int(a[7:]):]
    path = [a for a in sys.argv[1:] if not a.startswith("--")]
    text = program(first)
    if path:
        with open(path[0], "w", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    print(f"{len(CHECKS)} checks", file=sys.stderr)
