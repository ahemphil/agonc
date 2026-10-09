"""True values for <math.h>'s C99 functions, in Python's decimal (110
digits), for test_math.py's R test: the PC's build of math99.c and
mathf.c is measured against them in units in the last place, since the
PC's own libm is not accurate enough to judge by (its erf, expm1 and
lgamma, among others, are off by several ulps).

Each reference works on the exact value of its double argument (Decimal
of a float is exact) and is good to far more digits than a double holds:
series for erf, sin and atan, a continued fraction for erfc beyond 6,
Stirling's series with Bernoulli numbers for lgamma after shifting the
argument above 60, and decimal's own exp, ln, sqrt and power for the rest.
"""
import math, struct
from decimal import Decimal as D, getcontext
from fractions import Fraction

getcontext().prec = 110
PI = D("3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982"
       "1480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644")
EPS = D(10) ** -105


def bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def from_bits(u):
    return struct.unpack("<d", struct.pack("<Q", u))[0]


def ulps(d, t, single=False):
    """|d - t| in units in the last place of t: a double's, or (single) a
    float's; a NaN or an infinity against a finite t counts as huge"""
    if math.isnan(d) or math.isinf(d):
        return float("inf")
    if t == 0:
        return 0.0 if d == 0 else float("inf")
    e = math.frexp(float(abs(t)))[1] if abs(t) < D("1e308") else 1025
    if single:
        ulp = D(2) ** max(e - 24, -149)
    else:
        ulp = D(2) ** max(e - 53, -1074)
    return float(abs(D(d) - t) / ulp)


def sin_(x):
    two_pi = 2 * PI
    x = x - two_pi * (x / two_pi).to_integral_value()
    s = D(0)
    term = x
    n = 1
    while abs(term) > EPS:
        s += term
        term = -term * x * x / ((n + 1) * (n + 2))
        n += 2
    return s


def cos_(x):
    return sin_(x + PI / 2)


def atan_(x):
    if x < 0:
        return -atan_(-x)
    k = 0
    while x > D("0.1"):                         # atan x = 2 atan(x / (1 + sqrt(1 + x^2)))
        x = x / (1 + (1 + x * x).sqrt())
        k += 1
    s = D(0)
    term = x
    n = 0
    while abs(term) > EPS:
        s += term / (2 * n + 1)
        term = -term * x * x
        n += 1
    return s * 2 ** k


def asin_(x):
    if abs(x) == 1:
        return PI / 2 * x
    return atan_(x / (1 - x * x).sqrt())


def atan2_(y, x):
    if x > 0:
        return atan_(y / x)
    if x < 0:
        return atan_(y / x) + (PI if y >= 0 else -PI)
    return PI / 2 if y > 0 else -PI / 2


def erf_(x):
    if abs(x) > 6:
        return 1 - erfc_(x) if x > 0 else erfc_(-x) - 1
    s = D(0)
    term = x
    n = 0
    while True:
        t = term / (2 * n + 1)
        s += t
        if abs(t) < EPS and n > 5:
            break
        n += 1
        term = -term * x * x / n
    return 2 * s / PI.sqrt()


def erfc_(x):
    if x < 0:
        return 2 - erfc_(-x)
    if x < 6:
        return 1 - erf_(x)
    f = x                                       # exp(-x^2)/sqrt(pi) / (x + 1/2/(x + 1/(x + 3/2/(x + ...))))
    for k in range(400, 0, -1):
        f = x + D(k) / 2 / f
    return (-x * x).exp() / PI.sqrt() / f


_bern = []


def bernoulli(n):
    if not _bern:
        B = [Fraction(1)]
        for m in range(1, n + 1):
            B.append(-sum(Fraction(math.comb(m + 1, k)) * B[k] for k in range(m)) / (m + 1))
        _bern.extend(B)
    return _bern


def lgamma_pos(x):
    prod = D(1)
    while x < 60:
        prod *= x
        x += 1
    B = bernoulli(60)
    s = (x - D("0.5")) * x.ln() - x + (2 * PI).ln() / 2
    xp = x
    for k in range(1, 30):
        b = B[2 * k]
        s += D(b.numerator) / D(b.denominator) / (2 * k * (2 * k - 1) * xp)
        xp *= x * x
    return s - prod.ln()


def lgamma_(x):
    if x > 0:
        return lgamma_pos(x)
    return (PI / abs(sin_(PI * x) * x)).ln() - lgamma_pos(-x)


def tgamma_(x):
    if x > 0:
        return lgamma_pos(x).exp()
    return -PI / (sin_(PI * x) * x) / lgamma_pos(-x).exp()


def expm1_(x):
    if abs(x) < D("1e-5"):
        s = D(0)
        term = x
        n = 1
        while abs(term) > EPS:
            s += term
            n += 1
            term = term * x / n
        return s
    return x.exp() - 1


def pow_(x, y):
    if x == 0:
        return D(0)
    return (y * abs(x).ln()).exp()


LN2 = D(2).ln()

# name: (reference, number of arguments)
REF = {
    "sin": (sin_, 1), "cos": (cos_, 1), "tan": (lambda x: sin_(x) / cos_(x), 1),
    "asin": (asin_, 1), "acos": (lambda x: PI / 2 - asin_(x), 1), "atan": (atan_, 1),
    "exp": (lambda x: x.exp(), 1), "log": (lambda x: x.ln(), 1), "log10": (lambda x: x.ln() / D(10).ln(), 1),
    "sqrt": (lambda x: x.sqrt(), 1),
    "sinh": (lambda x: (x.exp() - (-x).exp()) / 2, 1), "cosh": (lambda x: (x.exp() + (-x).exp()) / 2, 1),
    "tanh": (lambda x: 1 - 2 / ((2 * x).exp() + 1), 1),
    "cbrt": (lambda x: (abs(x) ** (D(1) / 3)).copy_sign(x) if x != 0 else x, 1),
    "log1p": (lambda x: (1 + x).ln(), 1), "expm1": (expm1_, 1),
    "log2": (lambda x: x.ln() / LN2, 1), "exp2": (lambda x: (x * LN2).exp(), 1),
    "asinh": (lambda x: (abs(x) + (x * x + 1).sqrt()).ln().copy_sign(x), 1),
    "acosh": (lambda x: (x + (x * x - 1).sqrt()).ln(), 1),
    "atanh": (lambda x: ((1 + x) / (1 - x)).ln() / 2, 1),
    "erf": (erf_, 1), "erfc": (erfc_, 1), "lgamma": (lgamma_, 1), "tgamma": (tgamma_, 1),
    "hypot": (lambda x, y: (x * x + y * y).sqrt(), 2), "pow": (pow_, 2), "atan2": (atan2_, 2),
}


def true_value(name, args):
    """The reference for name (a float form, sinf, has sin's)"""
    f, n = REF[name] if name in REF else REF[name[:-1]]
    return f(*[D(a) for a in args])
