"""Test vectors for softfp.c's decimal conversions (test_softfp.py's D
tests), computed exactly with Python's integers and fractions.

    gen_decimal.py OUT_FILE [SEED]

Lines:
  D <sign> <digits> <exp10> <binary64 hex> <binary32 hex>
      the decimal sign * digits * 10^exp10, rounded to nearest (ties to
      even) as binary64 and, separately (not through binary64), binary32
  P <binary64 hex> <e|f> <precision> <text>
      printf's %.<precision>e or %.<precision>f of that double's magnitude
      (Python's, which is correctly rounded)

The decimal cases: doubles and floats written shortest and long; random
digit strings over the whole exponent range; the exact midpoint between
two neighbouring doubles (and floats), a tie, and the same nudged either
way; long strings near the boundaries of the subnormals and of overflow.
"""
import random, struct, sys
from fractions import Fraction


def round_binary(q, p, emin, emax):
    """q (a non-negative Fraction) to nearest with p bits of precision,
    normal exponents emin..emax: (biased exponent field, fraction field)."""
    if q == 0:
        return 0, 0
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    while Fraction(2) ** (e + 1) <= q:
        e += 1
    if e < emin:
        e = emin
    scaled = q / Fraction(2) ** (e - p + 1)
    m = scaled.numerator // scaled.denominator
    rem = scaled - m
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and m & 1):
        m += 1
    if m == 1 << p:
        m >>= 1
        e += 1
    if e > emax:
        return (1 << (p_exp_bits(p))) - 1, 0
    if m < 1 << (p - 1):
        return 0, m                                         # subnormal
    return e - emin + 1, m - (1 << (p - 1))


def p_exp_bits(p):
    return 11 if p == 53 else 8


def to64(sign, q):
    ex, fr = round_binary(q, 53, -1022, 1023)
    return (sign << 63) | (ex << 52) | fr


def to32(sign, q):
    ex, fr = round_binary(q, 24, -126, 127)
    return (sign << 31) | (ex << 23) | fr


def dline(sign, digits, exp10):
    digits = digits.lstrip("0") or "0"
    q = Fraction(int(digits)) * (Fraction(10) ** exp10)
    return f"D {sign} {digits} {exp10} {to64(sign, q):016x} {to32(sign, q):08x}"


def exact_decimal(q):
    """A Fraction whose denominator is a power of 2, as (digits, exp10)."""
    k = 0
    while q.denominator != 1:
        q *= 10
        k += 1
    return str(q.numerator), -k


def value64(bits):
    return struct.unpack("<d", struct.pack("<Q", bits))[0]


def frac64(bits):
    ex = bits >> 52 & 0x7FF
    fr = bits & ((1 << 52) - 1)
    if ex == 0:
        return Fraction(fr) * Fraction(2) ** -1074
    return Fraction(fr | 1 << 52) * Fraction(2) ** (ex - 1075)


def frac32(bits):
    ex = bits >> 23 & 0xFF
    fr = bits & ((1 << 23) - 1)
    if ex == 0:
        return Fraction(fr) * Fraction(2) ** -149
    return Fraction(fr | 1 << 23) * Fraction(2) ** (ex - 150)


def random_double_bits(rng):
    k = rng.randrange(8)
    if k == 0:
        ex = rng.randrange(0, 40)
    elif k == 1:
        ex = rng.randrange(2000, 2047)
    else:
        ex = rng.randrange(1, 2047)
    return ex << 52 | rng.getrandbits(52)


def main():
    out = sys.argv[1]
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1989)
    lines = []
    # doubles, written shortest (repr) and with all their digits
    for _ in range(600):
        bits = random_double_bits(rng)
        x = value64(bits)
        s = repr(x)
        mant, _, ex = s.partition("e")
        ip, _, fp = mant.partition(".")
        lines.append(dline(0, ip + fp, (int(ex) if ex else 0) - len(fp)))
        d, e = exact_decimal(frac64(bits))
        lines.append(dline(rng.randrange(2), d, e))
    # random digit strings
    for _ in range(800):
        n = rng.choice([1, 2, 5, 9, 15, 16, 17, 18, 19, 20, 25, 40])
        digits = str(rng.randrange(1, 10)) + "".join(str(rng.randrange(10)) for _ in range(n - 1))
        exp10 = rng.randrange(-360, 330)
        lines.append(dline(rng.randrange(2), digits, exp10))
    # ties: the midpoint of two neighbouring doubles, and nudged
    for _ in range(500):
        bits = random_double_bits(rng) & ~(1 << 63)
        mid = (frac64(bits) + frac64(bits + 1)) / 2
        d, e = exact_decimal(mid)
        lines.append(dline(0, d, e))
        lines.append(dline(0, d + "1", e - 1))                  # just above
        low = int(d) * 10 - 1
        lines.append(dline(0, str(low), e - 1))                 # just below
    # ties between floats
    for _ in range(400):
        bits = rng.randrange(1, 0x7F7FFFFF)
        mid = (frac32(bits) + frac32(bits + 1)) / 2
        d, e = exact_decimal(mid)
        lines.append(dline(0, d, e))
        lines.append(dline(0, d + "1", e - 1))
        lines.append(dline(0, str(int(d) * 10 - 1), e - 1))
    # the edges: subnormals, the smallest and largest values, overflow
    for s, e in (("49406564584124654", -340), ("24703282292062327", -340), ("24703282292062328", -340),
                 ("22250738585072011", -324), ("22250738585072014", -324), ("17976931348623157", 292),
                 ("17976931348623159", 292), ("1", -400), ("1", 400), ("0", 0), ("0", -500),
                 ("140129846432481707", -62), ("70064923216240854", -62), ("117549435082228750", -55),
                 ("340282346638528859", 21), ("340282356779733661", 21), ("1", 39), ("9", -46)):
        lines.append(dline(0, s, e))
        lines.append(dline(1, s, e))
    # printf: %.Pe and %.Pf of doubles
    for _ in range(1500):
        bits = random_double_bits(rng)
        if rng.randrange(4) == 0:
            bits = struct.unpack("<Q", struct.pack("<d", rng.choice([0.5, 1.5, 2.5, 0.125, 1e22, 123.456,
                                                                    9.995, 0.1, 1e-5, 999999.5])))[0]
        x = value64(bits)
        mode = rng.choice("ef")
        prec = rng.choice([0, 1, 2, 3, 5, 6, 10, 15, 17, 20, 30])
        if mode == "f" and abs(x) >= 1e30:
            mode = "e"
        text = f"%.{prec}{mode}" % x
        lines.append(f"P {bits:016x} {mode} {prec} {text}")
    with open(out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
