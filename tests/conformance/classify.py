"""Heuristic classifier for C test programs: which tests of a downloaded suite
agonc can attempt (written for the conformance-suite evaluation, 2026-09-25;
tests/conformance/run_suite.py turns its output into the committed skip lists).

For every test file it:
  1. strips comments (noting // comments),
  2. evaluates #if/#ifdef/#elif with the macros a C89 compiler for our target would
     predefine (see PREDEF) so that code a test compiles out for us is not scanned,
  3. scans the remaining code with regexes and a small brace/statement scanner,
and writes one CSV row of flags per test plus a summary.

usage: classify.py OUT_PREFIX SUITE_NAME ROOT_DIR GLOB [GLOB...] [--sdcc]
"""
import csv, glob, json, os, re, sys
from collections import Counter, defaultdict

# ---------------------------------------------------------------- target model
INT_MAX = 8388607          # 24-bit int
UINT_MAX = 16777215
PREDEF = {
    "__STDC__": "1",
    # limits.h values for the target (used when a test tests them in #if)
    "CHAR_BIT": "8", "SCHAR_MAX": "127", "SCHAR_MIN": "(-128)", "UCHAR_MAX": "255",
    "CHAR_MAX": "127", "CHAR_MIN": "(-128)", "SHRT_MAX": "32767", "SHRT_MIN": "(-32768)",
    "USHRT_MAX": "65535", "INT_MAX": str(INT_MAX), "INT_MIN": "(-8388608)",
    "UINT_MAX": str(UINT_MAX), "LONG_MAX": "2147483647", "LONG_MIN": "(-2147483648)",
    "ULONG_MAX": "4294967295",
    # gcc-style predefines we could cheaply provide; gcc torture tests adapt to them
    "__CHAR_BIT__": "8", "__SIZEOF_INT__": "3", "__SIZEOF_LONG__": "4",
    "__SIZEOF_SHORT__": "2", "__SIZEOF_POINTER__": "3", "__SIZEOF_FLOAT__": "4",
    "__SIZEOF_DOUBLE__": "8", "__SIZEOF_LONG_DOUBLE__": "8", "__INT_MAX__": str(INT_MAX),
    "__LONG_MAX__": "2147483647", "__SCHAR_MAX__": "127", "__SHRT_MAX__": "32767",
    "__ORDER_LITTLE_ENDIAN__": "1234", "__ORDER_BIG_ENDIAN__": "4321",
    "__BYTE_ORDER__": "1234",
    # gcc testsuite target macros a small-target board file would pass
    "STACK_SIZE": "16384", "SIGNAL_SUPPRESS": "1",
}
SDCC_PREDEF = {"PORT_HOST": "1"}   # SDCC tests: behave as the generic "host" port

# ---------------------------------------------------------------- tables
TYPE_KW = {"void", "char", "short", "int", "long", "float", "double", "signed",
           "unsigned", "struct", "union", "enum", "_Bool", "_Complex", "const",
           "volatile", "typedef", "extern", "static", "auto", "register", "inline",
           "restrict", "__inline", "__inline__", "__restrict", "__restrict__",
           "__const", "__volatile__", "__signed__", "__extension__", "__attribute__",
           "__attribute", "typeof", "__typeof__", "__typeof", "_Static_assert",
           "static_assert", "_Alignas", "_Thread_local", "_Atomic", "_Noreturn",
           "__int128", "_BitInt", "bool", "__complex__", "_Float16", "__fp16",
           "constexpr", "auto"}
STD_TYPEDEFS = {"FILE", "size_t", "ptrdiff_t", "wchar_t", "va_list", "jmp_buf", "div_t",
                "ldiv_t", "time_t", "clock_t", "fpos_t", "sig_atomic_t", "__builtin_va_list",
                "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
                "uint32_t", "uint64_t", "intptr_t", "uintptr_t", "intmax_t", "uintmax_t",
                "int_least8_t", "int_least16_t", "int_least32_t", "uint_least8_t",
                "uint_least16_t", "uint_least32_t", "int_fast8_t", "int_fast16_t",
                "int_fast32_t", "uint_fast8_t", "uint_fast16_t", "uint_fast32_t",
                "ssize_t", "wint_t", "mbstate_t", "char16_t", "char32_t", "max_align_t"}
KEYWORDS = {"auto", "break", "case", "char", "const", "continue", "default", "do",
            "double", "else", "enum", "extern", "float", "for", "goto", "if", "int",
            "long", "register", "return", "short", "signed", "sizeof", "static",
            "struct", "switch", "typedef", "union", "unsigned", "void", "volatile",
            "while"}

C89_LIB = {
    "assert.h": "assert",
    "ctype.h": "isalnum isalpha iscntrl isdigit isgraph islower isprint ispunct isspace "
               "isupper isxdigit tolower toupper",
    "errno.h": "errno EDOM ERANGE",
    "float.h": "FLT_RADIX FLT_ROUNDS FLT_DIG FLT_EPSILON FLT_MANT_DIG FLT_MAX FLT_MAX_EXP "
               "FLT_MIN FLT_MIN_EXP DBL_DIG DBL_EPSILON DBL_MANT_DIG DBL_MAX DBL_MAX_EXP "
               "DBL_MIN DBL_MIN_EXP LDBL_DIG LDBL_EPSILON LDBL_MANT_DIG LDBL_MAX LDBL_MIN "
               "FLT_MAX_10_EXP DBL_MAX_10_EXP FLT_MIN_10_EXP DBL_MIN_10_EXP",
    "limits.h": "CHAR_BIT SCHAR_MIN SCHAR_MAX UCHAR_MAX CHAR_MIN CHAR_MAX MB_LEN_MAX "
                "SHRT_MIN SHRT_MAX USHRT_MAX INT_MIN INT_MAX UINT_MAX LONG_MIN LONG_MAX "
                "ULONG_MAX",
    "locale.h": "setlocale localeconv LC_ALL LC_COLLATE LC_CTYPE LC_MONETARY LC_NUMERIC LC_TIME",
    "math.h": "acos asin atan atan2 cos sin tan cosh sinh tanh exp frexp ldexp log log10 "
              "modf pow sqrt ceil fabs floor fmod HUGE_VAL",
    "setjmp.h": "setjmp longjmp jmp_buf",
    "signal.h": "signal raise SIGABRT SIGFPE SIGILL SIGINT SIGSEGV SIGTERM SIG_DFL SIG_IGN SIG_ERR",
    "stdarg.h": "va_start va_arg va_end va_list",
    "stddef.h": "offsetof ptrdiff_t",
    "stdio.h": "remove rename tmpfile tmpnam fclose fflush fopen freopen setbuf setvbuf "
               "fprintf fscanf printf scanf sprintf sscanf vfprintf vprintf vsprintf fgetc "
               "fgets fputc fputs getc getchar gets putc putchar puts ungetc fread fwrite "
               "fgetpos fseek fsetpos ftell rewind clearerr feof ferror perror stdin stdout "
               "stderr BUFSIZ EOF FILENAME_MAX L_tmpnam SEEK_SET SEEK_CUR SEEK_END TMP_MAX",
    "stdlib.h": "atof atoi atol strtod strtol strtoul rand srand calloc free malloc realloc "
                "abort atexit exit getenv system bsearch qsort abs div labs ldiv mblen mbtowc "
                "wctomb mbstowcs wcstombs EXIT_SUCCESS EXIT_FAILURE RAND_MAX MB_CUR_MAX",
    "string.h": "memcpy memmove strcpy strncpy strcat strncat memcmp strcmp strcoll strncmp "
                "strxfrm memchr strchr strcspn strpbrk strrchr strspn strstr strtok memset "
                "strerror strlen",
    "time.h": "clock difftime mktime time asctime ctime gmtime localtime strftime CLOCKS_PER_SEC",
}
C89_HEADERS = set(C89_LIB) | {"stddef.h"}
LIB_FUNC_HEADER = {}
for h, names in C89_LIB.items():
    for n in names.split():
        LIB_FUNC_HEADER[n] = h
C99_HEADERS = {"stdint.h", "stdbool.h", "inttypes.h", "complex.h", "fenv.h", "tgmath.h",
               "stdalign.h", "stdatomic.h", "stdnoreturn.h", "threads.h", "uchar.h",
               "stdckdint.h", "stdbit.h"}
C95_HEADERS = {"wchar.h", "wctype.h", "iso646.h"}
POSIX_HEADERS_RE = re.compile(r"^(unistd|fcntl|pthread|dirent|dlfcn|poll|termios|pwd|grp|"
                              r"sys/.*|netinet/.*|arpa/.*|sched|semaphore|spawn|alloca|"
                              r"malloc|memory|endian|byteswap|execinfo|ucontext|link|elf|"
                              r"windows|io|x86intrin|immintrin|.*intrin|altivec|arm_neon)\.h$")
C99_LIBFUNCS = set("snprintf vsnprintf vscanf vsscanf vfscanf strtoll strtoull llabs lldiv "
                   "atoll strtof strtold isblank round roundf lround llround trunc truncf "
                   "fmin fmax fminf fmaxf fma copysign copysignf nan nanf isnan isinf "
                   "isfinite isnormal signbit fpclassify cbrt hypot log2 exp2 expm1 log1p "
                   "remainder remquo rint lrint llrint nearbyint scalbn scalbln ilogb logb "
                   "erf erfc lgamma tgamma asinh acosh atanh fdim nextafter nexttoward "
                   "sqrtf sinf cosf tanf expf logf powf fabsf floorf ceilf fmodf ldexpf frexpf "
                   "_Exit va_copy imaxabs strtoimax strtoumax wcstoll aligned_alloc "
                   "quick_exit at_quick_exit timespec_get strnlen strdup memccpy".split())
MOS_UNSUPPORTED = set("fork vfork execl execv execve execvp execlp wait waitpid pipe kill "
                      "alarm sigaction sigprocmask sigsetjmp siglongjmp setitimer getpid "
                      "getppid mmap munmap mprotect sbrk brk dlopen dlsym pthread_create "
                      "pthread_join popen pclose sleep usleep nanosleep gettimeofday "
                      "clock_gettime getrlimit setrlimit sysconf getpagesize posix_memalign "
                      "memalign valloc signal_handler ucontext getcontext setcontext "
                      "makecontext swapcontext open close read write lseek ioctl select "
                      "isatty".split())
HARMLESS_ATTRS = {"noinline", "noclone", "noipa", "unused", "used", "always_inline",
                  "const", "pure", "noreturn", "cold", "hot", "format", "nonnull",
                  "warn_unused_result", "malloc", "returns_twice", "sentinel", "nothrow",
                  "leaf", "optimize", "no_icf", "no_reorder", "flatten", "externally_visible",
                  "no_instrument_function", "__noinline__", "__noclone__", "__noipa__",
                  "__unused__", "__used__", "__always_inline__", "__const__", "__pure__",
                  "__noreturn__", "__format__", "__nonnull__", "__malloc__", "artificial",
                  "gnu_inline", "__gnu_inline__", "deprecated", "unavailable", "no_split_stack",
                  "stack_protect", "no_stack_protector", "fallthrough", "access", "target_clones",
                  "maybe_unused", "nodiscard"}
# __builtin_X that a prelude header can map to plain C89 (X is a C89 library name or trivial)
SHIM_BUILTINS = {"expect", "prefetch", "trap", "unreachable", "offsetof", "va_start",
                 "va_end", "va_arg", "va_list", "constant_p", "assume_aligned",
                 "LINE", "FILE", "FUNCTION"}


def strip_comments(src):
    """Remove comments, keep strings/char literals and line structure."""
    out = []
    i, n = 0, len(src)
    line_comments = 0
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            if j < 0:
                j = n - 2
            out.append(" " + "\n" * src.count("\n", i, j + 2))
            i = j + 2
        elif c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            if j < 0:
                j = n
            line_comments += 1
            out.append(" ")
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                if src[j] == "\\":
                    j += 1
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out), line_comments


# ---------------------------------------------------------------- #if evaluation
PP_TOK = re.compile(r"\s*(?:(0[xX][0-9a-fA-F]+|\d+)[uUlL]*|('(?:\\.|[^'])*')|([A-Za-z_]\w*)|"
                    r"(&&|\|\||<<|>>|<=|>=|==|!=|[-+*/%<>&|^!~?:(),]))")


class PPError(Exception):
    pass


def pp_eval(expr, macros, depth=0):
    toks = []
    pos = 0
    expr = expr.strip()
    while pos < len(expr):
        m = PP_TOK.match(expr, pos)
        if not m:
            if expr[pos:].strip() == "":
                break
            raise PPError(expr[pos:])
        pos = m.end()
        if m.group(1):
            s = m.group(1)
            if s.lower().startswith("0x"):
                v = int(s, 16)
            elif len(s) > 1 and s.startswith("0"):
                v = int(s, 8)
            else:
                v = int(s)
            toks.append(("n", v))
        elif m.group(2):
            body = m.group(2)[1:-1]
            if body.startswith("\\"):
                esc = {"n": 10, "t": 9, "0": 0, "\\": 92, "'": 39, "r": 13, "a": 7}
                v = esc.get(body[1:2], 0)
            else:
                v = ord(body[0]) if body else 0
            toks.append(("n", v))
        elif m.group(3):
            toks.append(("id", m.group(3)))
        else:
            toks.append(("op", m.group(4)))
    # handle defined, macro expansion
    out = []
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == ("id", "defined"):
            if i + 1 < len(toks) and toks[i + 1] == ("op", "("):
                name = toks[i + 2][1]
                i += 4
            else:
                name = toks[i + 1][1]
                i += 2
            out.append(("n", 1 if name in macros else 0))
            continue
        if t[0] == "id":
            if t[1] in macros:
                val = macros[t[1]]
                if val is None:
                    raise PPError("function-like macro " + t[1])
                if depth > 20:
                    raise PPError("deep")
                if i + 1 < len(toks) and toks[i + 1] == ("op", "("):
                    raise PPError("call")
                v = pp_eval(val, macros, depth + 1) if val.strip() else 0
                out.append(("n", v))
            else:
                if i + 1 < len(toks) and toks[i + 1] == ("op", "("):
                    raise PPError("unknown function-like " + t[1])
                out.append(("n", 0))
            i += 1
            continue
        out.append(t)
        i += 1
    p = _Parser(out)
    v = p.expr()
    if p.i != len(out):
        raise PPError("trailing")
    return v


class _Parser:
    BIN = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", ">", "<=", ">="],
           ["<<", ">>"], ["+", "-"], ["*", "/", "%"]]

    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else (None, None)

    def expr(self):
        v = self.cond()
        while self.peek() == ("op", ","):
            self.i += 1
            v = self.cond()
        return v

    def cond(self):
        c = self.binary(0)
        if self.peek() == ("op", "?"):
            self.i += 1
            a = self.expr()
            if self.peek() != ("op", ":"):
                raise PPError("?:")
            self.i += 1
            b = self.cond()
            return a if c else b
        return c

    def binary(self, lvl):
        if lvl == len(self.BIN):
            return self.unary()
        v = self.binary(lvl + 1)
        while self.peek()[0] == "op" and self.peek()[1] in self.BIN[lvl]:
            op = self.peek()[1]
            self.i += 1
            r = self.binary(lvl + 1)
            if op == "||": v = int(bool(v) or bool(r))
            elif op == "&&": v = int(bool(v) and bool(r))
            elif op == "|": v = v | r
            elif op == "^": v = v ^ r
            elif op == "&": v = v & r
            elif op == "==": v = int(v == r)
            elif op == "!=": v = int(v != r)
            elif op == "<": v = int(v < r)
            elif op == ">": v = int(v > r)
            elif op == "<=": v = int(v <= r)
            elif op == ">=": v = int(v >= r)
            elif op == "<<": v = v << min(r, 256)
            elif op == ">>": v = v >> min(r, 256)
            elif op == "+": v = v + r
            elif op == "-": v = v - r
            elif op == "*": v = v * r
            elif op in "/%":
                if r == 0:
                    raise PPError("div0")
                q = abs(v) // abs(r) * (1 if (v >= 0) == (r >= 0) else -1)
                v = q if op == "/" else v - q * r
        return v

    def unary(self):
        t = self.peek()
        if t[0] == "op" and t[1] in "-+!~":
            self.i += 1
            v = self.unary()
            return {"-": -v, "+": v, "!": int(not v), "~": ~v}[t[1]]
        if t == ("op", "("):
            self.i += 1
            v = self.expr()
            if self.peek() != ("op", ")"):
                raise PPError(")")
            self.i += 1
            return v
        if t[0] == "n":
            self.i += 1
            return t[1]
        raise PPError("unexpected %r" % (t,))


DEF_RE = re.compile(r"#\s*define\s+([A-Za-z_]\w*)(\()?(.*)$", re.S)


def preprocess(text, predef):
    """Return (active_code, macro_bodies, includes, pp_uncertain, defines)."""
    macros = dict(predef)
    lines = text.split("\n")
    # join continuations
    joined = []
    buf = ""
    for ln in lines:
        if ln.endswith("\\"):
            buf += ln[:-1] + " "
            continue
        joined.append(buf + ln)
        buf = ""
    if buf:
        joined.append(buf)
    stack = []  # (parent_active, taken, active, uncertain)
    active = True
    uncertain = 0
    code, bodies, includes, defines = [], [], [], {}
    for ln in joined:
        s = ln.strip()
        if s.startswith("#"):
            d = re.match(r"#\s*(\w*)\s*(.*)$", s, re.S)
            kw, rest = d.group(1), d.group(2)
            if kw in ("if", "ifdef", "ifndef"):
                if not active:
                    stack.append((active, True, False, False))
                    active = False
                    continue
                try:
                    if kw == "if":
                        v = bool(pp_eval(rest, macros))
                    else:
                        name = rest.split()[0] if rest.split() else ""
                        v = (name in macros) == (kw == "ifdef")
                    stack.append((active, v, v, False))
                    active = v
                except (PPError, IndexError, RecursionError):
                    uncertain += 1
                    stack.append((active, True, True, True))
                    active = True
            elif kw in ("elif", "else"):
                if not stack:
                    continue
                parent, taken, cur, unc = stack.pop()
                if not parent:
                    stack.append((parent, True, False, unc))
                    active = False
                    continue
                if unc:
                    stack.append((parent, True, True, True))
                    active = True
                    continue
                if taken:
                    stack.append((parent, True, False, False))
                    active = False
                    continue
                if kw == "else":
                    v = True
                else:
                    try:
                        v = bool(pp_eval(rest, macros))
                    except (PPError, IndexError, RecursionError):
                        uncertain += 1
                        stack.append((parent, True, True, True))
                        active = True
                        continue
                stack.append((parent, v, v, False))
                active = v
            elif kw == "endif":
                if stack:
                    parent = stack.pop()[0]
                    active = parent
                else:
                    active = True
            elif not active:
                continue
            elif kw == "define":
                m = DEF_RE.match(s)
                if m:
                    name, fn, body = m.group(1), m.group(2), m.group(3)
                    if fn:
                        macros[name] = None
                        defines[name] = None
                        bodies.append("(" + body)
                    else:
                        macros[name] = body.strip()
                        defines[name] = body.strip()
                        bodies.append(body)
            elif kw == "undef":
                name = rest.split()[0] if rest.split() else ""
                macros.pop(name, None)
            elif kw == "include":
                m = re.match(r'[<"]([^>"]+)[>"]', rest)
                if m:
                    includes.append(m.group(1))
            code.append("")
        else:
            code.append(ln if active else "")
    return "\n".join(code), "\n".join(bodies), includes, uncertain, defines


# ---------------------------------------------------------------- token scanner
TOK_RE = re.compile(r"""
    (?P<str>"(?:\\.|[^"\\])*")
  | (?P<chr>'(?:\\.|[^'\\])*')
  | (?P<num>\.?\d(?:[eEpP][+-]|[\w.])*)
  | (?P<id>[A-Za-z_$]\w*)
  | (?P<op>\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\#\#|[^\s])
""", re.X)


def tokenize(code):
    return [(m.lastgroup, m.group(m.lastgroup)) for m in TOK_RE.finditer(code)]


class Scanner:
    def __init__(self, toks, typedefs, macros, enums):
        self.t = toks
        self.typedefs = typedefs
        self.macros = macros
        self.enums = enums
        self.flags = Counter()

    def v(self, i):
        return self.t[i][1] if 0 <= i < len(self.t) else None

    def kind(self, i):
        return self.t[i][0] if 0 <= i < len(self.t) else None

    def skip_balanced(self, i, open_, close):
        depth = 0
        while i < len(self.t):
            x = self.v(i)
            if x == open_:
                depth += 1
            elif x == close:
                depth -= 1
                if depth == 0:
                    return i + 1
            i += 1
        return i

    def is_decl_start(self, i):
        x = self.v(i)
        if x in TYPE_KW:
            return True
        if self.kind(i) == "id" and x not in KEYWORDS:
            nx = self.v(i + 1)
            if x in self.typedefs and (self.kind(i + 1) == "id" or nx in ("*", "(")):
                if nx == "(" and x not in self.typedefs:
                    return False
                return True
            if self.kind(i + 1) == "id" and self.v(i + 1) not in KEYWORDS and \
                    self.v(i + 2) in (";", "=", ",", "[") and x not in self.macros:
                return True
        return False

    def check_aggregate(self, i):
        """i at '{' of struct/union/enum body; flag flexible/zero arrays; return end."""
        end = self.skip_balanced(i, "{", "}")
        body = [self.v(k) for k in range(i, end)]
        # anonymous struct/union members (C11): struct|union { ... } ;
        d = 0
        k = 0
        while k < len(body):
            if body[k] == "{":
                d += 1
            elif body[k] == "}":
                d -= 1
            elif d == 1 and body[k] in ("struct", "union") and k + 1 < len(body) and body[k + 1] == "{":
                dd = 0
                q = k + 1
                while q < len(body):
                    if body[q] == "{":
                        dd += 1
                    elif body[q] == "}":
                        dd -= 1
                        if dd == 0:
                            break
                    q += 1
                if q + 1 < len(body) and body[q + 1] == ";":
                    self.flags["c11_anon_member"] += 1
                k = q
            k += 1
        for k in range(len(body) - 2):
            if body[k] == "[" and body[k + 1] == "]" and body[k + 2] == ";":
                self.flags["c99_flexible_array_member"] += 1
            if body[k] == "[" and body[k + 1] == "0" and body[k + 2] == "]":
                self.flags["gnu_zero_length_array"] += 1
        # bit-field widths
        for k in range(1, len(body) - 2):
            if body[k] == ":" and re.fullmatch(r"\d+", body[k + 1] or "") and body[k + 2] in (";", ","):
                w = int(body[k + 1])
                # find base type of this member: look back to previous ';' or '{'
                j = k - 1
                while j > 0 and body[j] not in (";", "{", ","):
                    j -= 1
                decl = body[j + 1:k]
                if "long" in decl or ("short" in decl) or ("char" in decl) or \
                        any(d in ("_Bool", "bool") for d in decl) or \
                        (decl and decl[0] == "enum"):
                    self.flags["ext_nonint_bitfield"] += 1
                    if "long" in decl and w > 24:
                        pass
                elif w > 24:
                    self.flags["int32_bitfield_gt24"] += 1
        return end

    def top(self):
        i = 0
        n = len(self.t)
        prev = None
        while i < n:
            x = self.v(i)
            if x == "{":
                p = self.v(i - 1)
                pp_ = self.v(i - 2)
                if p in ("struct", "union", "enum") or pp_ in ("struct", "union", "enum"):
                    i = self.check_aggregate(i)
                    continue
                if p == "=" or p == ",":
                    i = self.skip_init(i)
                    continue
                if p == ")" or p == ";" or (self.kind(i - 1) == "id" and p not in KEYWORDS) or p == "]":
                    i = self.block(i + 1)
                    continue
                i = self.skip_balanced(i, "{", "}")
                continue
            if x == "(":
                # VLA params: [ident] inside a parameter list at top level
                end = self.skip_balanced(i, "(", ")")
                for k in range(i, end - 2):
                    if self.v(k) == "[" and self.v(k + 2) == "]" and self.vla_ident(self.v(k + 1), self.kind(k + 1)):
                        self.flags["c99_vla"] += 1
                    if self.v(k) == "[" and self.v(k + 1) in ("*", "static"):
                        self.flags["c99_array_param_qual"] += 1
                i = end
                continue
            i += 1

    def vla_ident(self, x, kind):
        return kind == "id" and x not in self.macros and x not in self.enums and \
            not x.isupper() and x not in KEYWORDS and x not in ("sizeof",) and \
            not re.fullmatch(r"[A-Z0-9_]+", x)

    def skip_init(self, i):
        end = self.skip_balanced(i, "{", "}")
        self.scan_init(i, end)
        return end

    def scan_init(self, i, end):
        for k in range(i, end - 2):
            a, b, c = self.v(k), self.v(k + 1), self.v(k + 2)
            if a in ("{", ",") and b == "." and self.kind(k + 2) == "id":
                self.flags["c99_designated_init"] += 1
            if a in ("{", ",") and b == "[":
                # [const] = or [a ... b] =
                j = self.skip_balanced(k + 1, "[", "]")
                inner = [self.v(q) for q in range(k + 2, j - 1)]
                if self.v(j) == "=" or self.v(j) == "[" or self.v(j) == ".":
                    if "..." in inner:
                        self.flags["gnu_range_designator"] += 1
                    self.flags["c99_designated_init"] += 1

    def block(self, i):
        """i after '{' of a compound statement; returns index after '}'."""
        seen_stmt = False
        while i < len(self.t) and self.v(i) != "}":
            if self.is_decl_start(i) and not self.is_label(i):
                if seen_stmt:
                    self.flags["c99_mixed_decl_code"] += 1
                i = self.decl(i)
            else:
                seen_stmt = True
                i = self.stmt(i)
        return i + 1

    def is_label(self, i):
        return self.kind(i) == "id" and self.v(i + 1) == ":" and self.v(i) not in KEYWORDS

    def decl(self, i):
        start = i
        depth = 0
        while i < len(self.t):
            x = self.v(i)
            if x == "(":
                depth += 1
            elif x == ")":
                depth -= 1
            elif x == ";" and depth <= 0:
                self.decl_checks(start, i)
                return i + 1
            elif x == "{" and depth <= 0:
                p = self.v(i - 1)
                if p == ")" :
                    self.flags["gnu_nested_function"] += 1
                    return self.block(i + 1)
                if p == "=" or p == ",":
                    i = self.skip_init(i)
                    continue
                i = self.check_aggregate(i)
                continue
            elif x == "}" and depth <= 0:
                return i
            i += 1
        return i

    def decl_checks(self, start, end):
        # VLA: [ ident-expression ] before any '=' at paren depth 0
        depth = 0
        k = start
        while k < end:
            x = self.v(k)
            if x == "(":
                depth += 1
            elif x == ")":
                depth -= 1
            elif x == "=" and depth == 0:
                # skip initializer to next top-level comma
                j = k + 1
                d2 = 0
                while j < end:
                    y = self.v(j)
                    if y in ("(", "[", "{"):
                        d2 += 1
                    elif y in (")", "]", "}"):
                        d2 -= 1
                    elif y == "," and d2 == 0:
                        break
                    j += 1
                self.scan_init(k, j)
                k = j
                continue
            elif x == "[":
                j = self.skip_balanced(k, "[", "]")
                inner = [(self.kind(q), self.v(q)) for q in range(k + 1, j - 1)]
                if inner and "sizeof" not in [v for _, v in inner] and \
                        any(self.vla_ident(v, kd) for kd, v in inner):
                    self.flags["c99_vla"] += 1
                k = j
                continue
            k += 1

    def paren_then_stmt(self, i):
        # i at '('
        end = self.skip_balanced(i, "(", ")")
        self.scan_expr(i, end)
        return self.stmt(end)

    def stmt(self, i):
        x = self.v(i)
        if x is None:
            return i
        if x == "{":
            return self.block(i + 1)
        if x == ";":
            return i + 1
        if x in ("if", "while", "switch"):
            j = self.paren_then_stmt(i + 1)
            if x == "if" and self.v(j) == "else":
                j = self.stmt(j + 1)
            return j
        if x == "for":
            if self.is_decl_start(i + 2):
                self.flags["c99_for_decl"] += 1
            return self.paren_then_stmt(i + 1)
        if x == "do":
            j = self.stmt(i + 1)
            if self.v(j) == "while":
                j = self.skip_balanced(j + 1, "(", ")")
                if self.v(j) == ";":
                    j += 1
            return j
        if x == "case":
            j = i + 1
            while j < len(self.t) and self.v(j) != ":":
                if self.v(j) == "...":
                    self.flags["gnu_case_range"] += 1
                j += 1
            return self.stmt(j + 1)
        if x == "default" and self.v(i + 1) == ":":
            return self.stmt(i + 2)
        if self.is_label(i):
            if self.v(i + 2) == "}":
                self.flags["c23_label_at_end"] += 1
                return i + 2
            if self.is_decl_start(i + 2):
                self.flags["c99_mixed_decl_code"] += 1  # label before declaration
            return self.stmt(i + 2)
        if x == "goto" and self.v(i + 1) == "*":
            self.flags["gnu_computed_goto"] += 1
        # expression / jump statement
        j = i
        depth = 0
        while j < len(self.t):
            y = self.v(j)
            if y in ("(", "["):
                depth += 1
            elif y in (")", "]"):
                depth -= 1
            elif y == "{":
                if self.v(j - 1) == "(":
                    self.flags["gnu_statement_expr"] += 1
                    j = self.block(j + 1)
                    continue
                if self.v(j - 1) == ")" and self.is_compound_literal(j - 1):
                    self.flags["c99_compound_literal"] += 1
                    e = self.skip_balanced(j, "{", "}")
                    self.scan_init(j, e)
                    j = e
                    continue
                j = self.skip_balanced(j, "{", "}")
                continue
            elif y == ";" and depth <= 0:
                self.scan_expr(i, j)
                return j + 1
            elif y == "}" and depth <= 0:
                return j
            j += 1
        return j

    def is_compound_literal(self, close):
        # close is index of ')' ; find its '('
        depth = 0
        k = close
        while k >= 0:
            if self.v(k) == ")":
                depth += 1
            elif self.v(k) == "(":
                depth -= 1
                if depth == 0:
                    break
            k -= 1
        inner = [(self.kind(q), self.v(q)) for q in range(k + 1, close)]
        if not inner:
            return False
        before = self.v(k - 1)
        if self.kind(k - 1) == "id" and before not in ("return", "sizeof"):
            return False
        if before in (")", "]"):
            return False
        first = inner[0][1]
        if not (first in TYPE_KW or first in self.typedefs):
            return False
        for kd, v in inner:
            if not (v in TYPE_KW or v in self.typedefs or v in ("*", "[", "]") or
                    kd == "num" or (kd == "id")):
                return False
        return True

    def scan_expr(self, i, end):
        for k in range(i, end):
            if self.v(k) == "&&" and self.kind(k + 1) == "id" and \
                    self.v(k - 1) in ("=", "(", ",", "{", "return", "?", ":"):
                self.flags["gnu_label_address"] += 1
            if self.v(k) == "?" and self.v(k + 1) == ":":
                self.flags["gnu_elvis"] += 1


def collect_typedefs(toks):
    names = set(STD_TYPEDEFS)
    i = 0
    n = len(toks)
    while i < n:
        if toks[i][1] == "typedef":
            j = i + 1
            depth = 0
            parts = [[]]
            while j < n:
                x = toks[j][1]
                if x == "{":
                    # skip body
                    d = 0
                    while j < n:
                        if toks[j][1] == "{":
                            d += 1
                        elif toks[j][1] == "}":
                            d -= 1
                            if d == 0:
                                break
                        j += 1
                    j += 1
                    continue
                if x == "(":
                    depth += 1
                elif x == ")":
                    depth -= 1
                if x == ";" and depth <= 0:
                    break
                if x == "," and depth == 0:
                    parts.append([])
                else:
                    parts[-1].append(toks[j])
                j += 1
            for p in parts:
                name = None
                for k in range(len(p) - 2):
                    if p[k][1] == "(" and p[k + 1][1] == "*" and p[k + 2][0] == "id":
                        name = p[k + 2][1]
                        break
                if not name:
                    d = 0
                    for kd, v in p:
                        if v in ("(", "["):
                            if d == 0 and name:
                                break
                            d += 1
                        elif v in (")", "]"):
                            d -= 1
                        elif kd == "id" and d == 0 and v not in TYPE_KW and v not in KEYWORDS:
                            name = v
                if name:
                    names.add(name)
            i = j
        i += 1
    return names


def collect_enums(toks):
    names = set()
    for i, (k, v) in enumerate(toks):
        if v == "enum":
            j = i + 1
            if j < len(toks) and toks[j][0] == "id":
                j += 1
            if j < len(toks) and toks[j][1] == "{":
                j += 1
                expect = True
                d = 0
                while j < len(toks) and not (toks[j][1] == "}" and d == 0):
                    if toks[j][1] == "(":
                        d += 1
                    elif toks[j][1] == ")":
                        d -= 1
                    if expect and toks[j][0] == "id":
                        names.add(toks[j][1])
                        expect = False
                    if toks[j][1] == "," and d == 0:
                        expect = True
                    j += 1
    return names


# ---------------------------------------------------------------- regex detectors
NUM_RE = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9]\d*)([uUlL]*)(?![\w.])")
FLOAT_LIT_RE = re.compile(r"(?<![\w.])(\d+\.\d*(?!\.)|\.\d+|\d+[eE][+-]?\d+)([fFlL]?)(?![\w])")
HEXFLOAT_RE = re.compile(r"\b0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*[pP][+-]?\d+")
SHIFT_RE = re.compile(r"(\S+)\s*(<<|>>)=?\s*(\d+)\b")
ELEM_SIZE = {"char": 1, "short": 2, "int": 3, "long": 4, "float": 4, "double": 8,
             "signed": 3, "unsigned": 3}


def int_value(s):
    if s.lower().startswith("0x"):
        return int(s, 16)
    if len(s) > 1 and s.startswith("0"):
        return int(s, 8)
    return int(s)


def classify_file(path, predef, sdcc=False, dg_text=None):
    raw = open(path, encoding="latin-1").read()
    f = Counter()
    info = {}
    # .c.in templates (SDCC): take header var lists, substitute C89-friendly value
    if path.endswith(".c.in"):
        m = re.match(r"\s*/\*(.*?)\*/", raw, re.S)
        vars_ = {}
        if m:
            for ln in m.group(1).split("\n"):
                mm = re.match(r"\s*(\w+)\s*:\s*(.+)$", ln)
                if mm:
                    vals = [v.strip() for v in mm.group(2).split(",")]
                    vars_[mm.group(1)] = vals
        for k, vals in vars_.items():
            good = [v for v in vals if not re.search(r"long\s+long|_Bool|_BitInt|bool|float|double|int64|uint64", v)]
            if len(good) < len(vals):
                f["tmpl_instances_dropped"] += 1
            pick = good[0] if good else vals[0]
            raw = raw.replace("{" + k + "}", pick)
        info["template_vars"] = len(vars_)
    lines = raw.count("\n") + 1
    if re.search(r"[\x80-\xff]", raw):
        f["non_ascii_source"] = 1
    # gcc dg directives (read before comments are stripped)
    for m in re.finditer(r"dg-require-effective-target\s+(\w+)", raw):
        f["dg_req_" + m.group(1)] += 1
    for m in re.finditer(r"\{\s*dg-(?:skip-if|xfail-run-if|require-[\w-]+)[^}]*\{([^}]*)\}", raw):
        pass
    if re.search(r"dg-skip-if[^\n]*\bint16\b", raw):
        f["dg_skip_int16"] += 1
    if re.search(r"dg-(additional-)?options[^\n]*-std=(gnu|c)(99|9x|11|1x|17|2x|23)", raw):
        f["dg_std_c99plus"] += 1
    if re.search(r"dg-(additional-)?options[^\n]*-std=(gnu|c)(89|90|iso9899:1990)", raw):
        f["dg_std_c89"] += 1
    text, line_comments = strip_comments(raw)
    if line_comments:
        f["line_comments"] = 1
    code, macro_text, includes, unc, defines = preprocess(text, predef)
    if unc:
        f["pp_uncertain"] = 1
    info["includes"] = sorted(set(includes))
    # strings
    strings = re.findall(r'"(?:\\.|[^"\\\n])*"', code + "\n" + macro_text)
    body = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', code)
    body = re.sub(r"'(?:\\.|[^'\\\n])+'", "0", body)
    mbody = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', macro_text)
    mbody = re.sub(r"'(?:\\.|[^'\\\n])+'", "0", mbody)
    allc = body + "\n" + mbody
    fmt = " ".join(strings)

    # ---- C99+ language
    def rx(name, pat, where=allc, flags=0):
        n = len(re.findall(pat, where, flags))
        if n:
            f[name] += n
    rx("c99_long_long", r"\blong\s+long\b")
    rx("c99_long_long", r"\b(?:u?int64_t|int_least64_t|intmax_t|uintmax_t)\b")
    rx("c99_long_long_suffix", r"(?<![\w.])(?:0[xX][0-9a-fA-F]+|\d+)(?:[uU]?(?:ll|LL)|(?:ll|LL)[uU])(?!\w)")
    rx("c99_bool", r"\b_Bool\b")
    rx("c99_inline", r"(?<!_)\binline\b")
    rx("c99_restrict", r"(?<!_)\brestrict\b")
    rx("c99_func", r"\b__func__\b")
    rx("c99_variadic_macro", r"__VA_ARGS__|#\s*define\s+\w+\([^)]*\.\.\.\)", text)
    rx("c99_complex", r"\b_Complex\b|\b_Imaginary\b")
    rx("c99_hex_float", HEXFLOAT_RE.pattern)
    rx("c99_pragma_op", r"\b_Pragma\s*\(")
    rx("c99_ucn", r"\\[uU][0-9a-fA-F]{4}", fmt + " " + allc)
    rx("c11_keyword", r"\b(?:_Static_assert|_Generic|_Alignof|_Alignas|_Noreturn|_Thread_local|_Atomic)\b")
    rx("c11_u8_string", r'\bu8""|\b[uU]""')
    rx("c23_keyword", r"\b(?:_BitInt|nullptr|constexpr|static_assert|typeof_unqual|alignas|alignof|thread_local)\b")
    rx("c23_attr", r"\[\[\s*\w+")
    rx("c23_binary_const", r"(?<![\w.])0[bB][01]+")
    rx("c23_digit_sep", r"\b\d+'\d+")
    rx("c99_printf_len", r"%[-+ #0-9.*]*(?:ll|hh|z|j|t)[diouxXn]|%[-+ #0-9.*]*[aA]", fmt)
    rx("enum_trailing_comma", r"\benum\b[^;{]*\{[^}]*,\s*\}")
    if re.search(r"\bbool\b", allc) and "stdbool.h" not in includes and \
            not re.search(r"typedef[^;]*\bbool\s*;|#\s*define\s+bool\b", text):
        f["c23_bool_keyword"] += 1
    for h in includes:
        if h in C99_HEADERS:
            f["c99_header"] += 1
            f["hdr99_" + h] += 1
        elif h in C95_HEADERS:
            f["c95_header"] += 1
        elif POSIX_HEADERS_RE.match(h):
            f["posix_header"] += 1
    called = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", allc))
    idents = set(re.findall(r"\b[A-Za-z_]\w*\b", allc))
    c99f = called & C99_LIBFUNCS
    if c99f:
        f["c99_libfunc"] += len(c99f)
        info["c99_libfuncs"] = sorted(c99f)
    # ---- GNU
    attrs = []
    for m in re.finditer(r"\b__attribute(?:__)?\s*\(\(", allc):
        j = m.end()
        d = 2
        k = j
        while k < len(allc) and d > 0:
            if allc[k] == "(":
                d += 1
            elif allc[k] == ")":
                d -= 1
            k += 1
        inner = allc[j:k - 2]
        # top-level names
        depth = 0
        cur = ""
        for ch in inner:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                attrs.append(cur.strip())
                cur = ""
                continue
            if depth == 0 and ch not in "()":
                cur += ch
        attrs.append(cur.strip())
    attrs = [re.match(r"\w*", a).group(0) for a in attrs if a]
    for a in attrs:
        if a.strip("_") in {x.strip("_") for x in HARMLESS_ATTRS}:
            f["gnu_attr_harmless"] += 1
        else:
            f["gnu_attr_semantic"] += 1
            info.setdefault("attrs", set()).add(a)
    rx("gnu_typeof", r"\b(?:__typeof__|__typeof|typeof)\b")
    for m in re.finditer(r'\b(?:__asm__|__asm|asm)\b\s*(?:volatile|__volatile__|__volatile|goto)?\s*\(\s*("[^"]*")?',
                         code + "\n" + macro_text):
        if m.group(1) == '""':
            f["gnu_asm_barrier"] += 1      # asm("" ...): optimisation barrier only
        else:
            f["gnu_asm"] += 1
    rx("gnu_soft_kw", r"\b(?:__inline__|__inline|__restrict__|__restrict|__extension__|__const__|__const|__volatile__|__signed__|__signed)\b")
    rx("gnu_hard_kw", r"\b(?:__label__|__complex__|__real__|__imag__|__int128|__alignof__|__alignof|_Float16|__fp16|__auto_type|__seg_fs|__seg_gs|_Decimal32|_Decimal64|_Float128|__float128|_Float32|_Float64)\b")
    rx("gnu_func_name", r"\b__(?:FUNCTION|PRETTY_FUNCTION)__\b")
    rx("gnu_predef_macro", r"\b__(?:SIZEOF_\w+|INT_MAX|LONG_MAX|LONG_LONG_MAX|SCHAR_MAX|SHRT_MAX|CHAR_BIT|INTPTR_\w+|UINT\w*_TYPE|INT\w*_TYPE|SIZE_TYPE|PTRDIFF_TYPE|WCHAR_\w+|BYTE_ORDER|ORDER_\w+|FLT_\w+|DBL_\w+|BIGGEST_ALIGNMENT|GNUC\w*|OPTIMIZE)__\b")
    rx("gnu_dollar_ident", r"\b[A-Za-z_]\w*\$\w*")
    for m in re.findall(r"\b__builtin_(\w+)", allc):
        if m in SHIM_BUILTINS or m in LIB_FUNC_HEADER:
            f["gnu_builtin_shim"] += 1
        else:
            f["gnu_builtin_hard"] += 1
            info.setdefault("builtins", set()).add(m)
    if re.search(r"\b(?:__builtin_)?alloca\s*\(", allc):
        f["gnu_alloca"] += 1
    # SDCC keywords that survive PORT_HOST
    rx("sdcc_ext", r"\b(?:__interrupt|__banked|__nonbanked|__naked|__sfr|__sfr16|__sfr32|__sbit|__bit|__z88dk_\w+|__preserves_regs|__smallc|__sdcccall|__trap|__using|__shadowregs|__raisonance|__iar|__cosmic|__wparam|__dynamicc|__addressmod|__eeprom|__asm|__endasm)\b")
    # ---- structural
    toks = tokenize(body)
    typedefs = collect_typedefs(toks)
    enums = collect_enums(toks)
    sc = Scanner(toks, typedefs, set(defines) | set(predef), enums)
    try:
        sc.top()
    except RecursionError:
        f["scan_error"] += 1
    f.update(sc.flags)
    # ---- int size assumptions
    rx("int32_sizeof_int_4", r"sizeof\s*\(?\s*(?:unsigned\s+|signed\s+)?int\s*\)?\s*[!=<>]=?\s*4\b|\b4\s*[!=<>]=?\s*sizeof\s*\(?\s*(?:unsigned\s+)?int\b")
    rx("int32_sizeof_int_eq_long", r"sizeof\s*\(\s*(?:unsigned\s+)?int\s*\)\s*==\s*sizeof\s*\(\s*(?:unsigned\s+)?long\s*\)|sizeof\s*\(\s*(?:unsigned\s+)?long\s*\)\s*==\s*sizeof\s*\(\s*(?:unsigned\s+)?int\s*\)")
    rx("int16_sizeof_int_2", r"sizeof\s*\(?\s*(?:unsigned\s+)?int\s*\)?\s*[!=]=\s*2\b")
    big = 0
    huge = 0
    for m in NUM_RE.finditer(allc):
        s, suf = m.group(1), m.group(2)
        try:
            v = int_value(s)
        except ValueError:
            continue
        if v > 0xFFFFFFFF and "l" not in suf.lower():
            huge += 1
        elif v > 0xFFFFFFFF:
            huge += 1
        elif "l" not in suf.lower():
            dec = not s.lower().startswith("0x") and not (len(s) > 1 and s.startswith("0"))
            if (dec and v > INT_MAX) or (not dec and v > UINT_MAX):
                big += 1
    if big:
        f["int32_big_const"] = big
    if huge:
        f["c99_huge_const"] = huge
    # big constant in an int/unsigned context
    ctx = re.findall(r"\b(?:(?:unsigned|signed)\s+int|int|unsigned)\s+\**\s*\w+(?:\s*\[[^\]]*\])?\s*=\s*\{?\s*[-~(]*\s*(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)\b|\(\s*(?:unsigned\s+int|unsigned|int)\s*\)\s*\(?\s*(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)\b", allc)
    for a, asuf, b, bsuf in ctx:
        s, suf = (a, asuf) if a else (b, bsuf)
        try:
            v = int_value(s)
        except ValueError:
            continue
        if "l" not in suf.lower() and v > INT_MAX and v <= 0xFFFFFFFF:
            if v > UINT_MAX or not s.lower().startswith("0x"):
                f["int32_big_const_int_ctx"] += 1
    for m in SHIFT_RE.finditer(allc):
        lhs, cnt = m.group(1), int(m.group(3))
        if 24 <= cnt <= 31 and not re.search(r"[lL]\)?$", lhs) and "long" not in lhs:
            f["int32_shift_ge24"] += 1
    if f.get("dg_req_int32plus") or f.get("dg_req_int32"):
        f["int32_dg"] = 1
    # ---- float
    rx("float_kw", r"\b(?:float|double)\b")
    fl = [m for m in FLOAT_LIT_RE.finditer(allc)]
    if fl:
        f["float_literal"] = len(fl)
    if "math.h" in includes or "float.h" in includes:
        f["float_header"] = 1
    if re.search(r"%[-+ #0-9.*]*l?[fFeEgG]", fmt):
        f["float_printf"] = 1
    # ---- library
    lib_hdr = Counter()
    lib_fn = Counter()
    for name in idents:
        h = LIB_FUNC_HEADER.get(name)
        if h:
            if name in called or not re.fullmatch(r"[a-z_]+", name) or h in ("errno.h",):
                lib_hdr[h] += 1
                lib_fn[name] += 1
    info["lib_headers"] = dict(lib_hdr)
    info["lib_funcs"] = sorted(lib_fn)
    mos_bad = (called & MOS_UNSUPPORTED)
    if mos_bad:
        f["mos_unsupported_call"] += len(mos_bad)
        info["mos_bad"] = sorted(mos_bad)
    if re.search(r"\b(?:scanf|getchar|gets)\s*\(|\bstdin\b", allc):
        f["needs_stdin"] = 1
    if re.search(r"\bargv\b", allc) and re.search(r"\bargv\s*\[", allc):
        f["uses_argv"] = 1
    if re.search(r"\bsetlocale\s*\(", allc):
        f["uses_locale"] = 1
    if re.search(r"\b(?:localtime|mktime|strftime|gmtime|ctime|asctime)\s*\(", allc):
        f["uses_calendar_time"] = 1
    if re.search(r"\b(?:fopen|freopen|tmpfile|remove|rename|tmpnam)\s*\(", allc):
        f["uses_files"] = 1
    # ---- memory: arrays with constant dims
    maxbytes = 0
    sub = dict((k, v) for k, v in defines.items() if v)
    for m in re.finditer(r"\b(char|short|int|long|float|double|unsigned|signed|struct\s+\w+|\w+)\s+\**\s*(\w+)\s*((?:\[\s*[^\]\[]+\s*\]\s*)+)", allc):
        tname, dims = m.group(1), m.group(3)
        size = ELEM_SIZE.get(tname.split()[0] if tname.split()[0] in ELEM_SIZE else tname, 1)
        if tname == "long" or re.match(r"long\b", tname):
            size = 4
        total = size
        ok = True
        for d in re.findall(r"\[\s*([^\]]+?)\s*\]", dims):
            expr = d
            for _ in range(5):
                expr2 = re.sub(r"\b[A-Za-z_]\w*\b", lambda mm: "(" + sub[mm.group(0)] + ")" if mm.group(0) in sub else mm.group(0), expr)
                if expr2 == expr:
                    break
                expr = expr2
            expr = re.sub(r"(?<=\d)[uUlL]+", "", expr)
            if not re.fullmatch(r"[\d\s()+\-*/<>xXa-fA-F]+", expr):
                ok = False
                break
            try:
                v = pp_eval(expr, {})
            except Exception:
                ok = False
                break
            total *= max(v, 0)
        if ok:
            maxbytes = max(maxbytes, total)
    for m in re.finditer(r"\b(?:malloc|calloc|alloca|realloc)\s*\(\s*([0-9xXa-fA-F]+)[uUlL]*\s*[,)]", allc):
        try:
            maxbytes = max(maxbytes, int_value(m.group(1)))
        except ValueError:
            pass
    info["max_static_bytes"] = maxbytes
    # ---- checks present in the active code (a test with none passes vacuously)
    nchk = len(re.findall(r"\b(?:__builtin_)?abort\s*\(|\bASSERT\s*\(|\bexit\s*\(\s*[^0\s)]|"
                          r"\breturn\s+[^0;\s][^;]*;|\bprintf\s*\(|\bputs\s*\(", allc))
    if nchk == 0:
        f["no_checks"] = 1
    # ---- coverage features (informational; prefix cov_)
    dtext = text  # comment-stripped, directives intact
    cov = {
        "pp_funclike_macro": (r"#\s*define\s+\w+\(", dtext),
        "pp_stringize": (r"#\s*define[^\n]*[^#]#\s*[A-Za-z_]", dtext),
        "pp_paste": (r"##", dtext),
        "pp_conditional": (r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b", dtext),
        "pp_include_quoted": (r'^\s*#\s*include\s*"', dtext),
        "pp_line_error": (r"^\s*#\s*(?:line|error)\b", dtext),
        "pp_predef": (r"\b__(?:LINE|FILE|DATE|TIME|STDC)__\b", allc + dtext),
        "trigraphs": (r"\?\?[=/'()!<>-]", raw),
        "kr_definition": (r"\b[A-Za-z_]\w*\s*\(\s*[A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*\s*\)\s*\n\s*(?:register\s+)?[A-Za-z_][^;(){}=]*;\s*(?:[A-Za-z_][^;(){}=]*;\s*)*\{", allc),
        "function_pointer": (r"\(\s*\*\s*\w*\s*\)\s*\(", allc),
        "varargs": (r"\bva_(?:start|arg)\b", allc),
        "struct_by_value_param": (r"[(,]\s*(?:const\s+)?(?:struct|union)\s+\w+\s+\w+\s*[,)]", allc),
        "struct_return": (r"(?:^|\n)\s*(?:static\s+)?(?:struct|union)\s+\w+\s+\w+\s*\([^;]*\)\s*\{", allc),
        "struct_assign": (r"\bstruct\s+\w+\s+\w+\s*=\s*[A-Za-z_*(]", allc),
        "bitfield": (r"\b(?:int|unsigned|signed|long|short|char|_Bool)\s+\w*\s*:\s*\d+\s*[;,]|\b(?:int|unsigned|signed)\s*:\s*\d+", allc),
        "union": (r"\bunion\b", allc),
        "enum": (r"\benum\b", allc),
        "typedef": (r"\btypedef\b", allc),
        "aggregate_init": (r"=\s*\{", allc),
        "nested_init_braces": (r"\{\s*\{", allc),
        "char_array_string_init": (r"char\s+\w+\s*\[[^\]]*\]\s*=\s*\"", code),
        "switch": (r"\bswitch\b", allc),
        "goto": (r"\bgoto\b", allc),
        "do_while": (r"\bdo\b", allc),
        "cast_arith": (r"\(\s*(?:unsigned|signed|char|short|int|long)[\w\s]*\)\s*[\w(~-]", allc),
        "unsigned": (r"\bunsigned\b", allc),
        "long": (r"\blong\b(?!\s+long)", allc),
        "short": (r"\bshort\b", allc),
        "signed_or_unsigned_char": (r"\b(?:signed|unsigned)\s+char\b", allc),
        "volatile": (r"\bvolatile\b", allc),
        "const": (r"\bconst\b", allc),
        "static": (r"\bstatic\b", allc),
        "register": (r"\bregister\b", allc),
        "multidim_array": (r"\]\s*\[", allc),
        "setjmp": (r"\b(?:setjmp|longjmp)\b", allc),
        "string_concat": (r'""\s*""', allc),
        "conditional_op": (r"\?", allc),
        "sizeof": (r"\bsizeof\b", allc),
        "wide_char": (r"\bL['\"]|\bwchar_t\b", code),
    }
    for name, (pat, where) in cov.items():
        if re.search(pat, where, re.M):
            f["cov_" + name] = 1
    if maxbytes > 100_000:
        f["mem_gt_100k"] = 1
    if sdcc:
        tf = re.findall(r"^\s*void\s*\n?\s*(test\w+)\s*\(\s*void\s*\)", raw, re.M)
        info["sdcc_test_funcs"] = len(tf)
    info["lines"] = lines
    return f, info


# ---------------------------------------------------------------- categories
def categories(f):
    c99 = [k for k in f if (k.startswith("c99_") or k.startswith("c11_") or k.startswith("c23_")
                            or k in ("c99_header", "c95_header", "dg_std_c99plus"))]
    gnu_hard = [k for k in f if k in ("gnu_attr_semantic", "gnu_typeof", "gnu_asm", "gnu_hard_kw",
                                      "gnu_builtin_hard", "gnu_alloca", "gnu_nested_function",
                                      "gnu_statement_expr", "gnu_label_address", "gnu_computed_goto",
                                      "gnu_zero_length_array", "gnu_case_range",
                                      "gnu_range_designator", "gnu_elvis", "gnu_dollar_ident",
                                      "gnu_func_name", "ext_nonint_bitfield", "sdcc_ext")]
    gnu_soft = [k for k in f if k in ("gnu_attr_harmless", "gnu_soft_kw", "gnu_builtin_shim",
                                      "gnu_asm_barrier", "gnu_predef_macro")]
    int32 = [k for k in f if k in ("int32_dg", "int32_sizeof_int_4", "int32_sizeof_int_eq_long",
                                   "int32_bitfield_gt24", "int32_big_const_int_ctx",
                                   "int32_shift_ge24", "int16_sizeof_int_2")]
    flt = [k for k in f if k.startswith("float_")]
    env = [k for k in f if k in ("mos_unsupported_call", "posix_header", "needs_stdin",
                                 "mem_gt_100k", "uses_locale")]
    return c99, gnu_hard, gnu_soft, int32, flt, env


def main():
    args = sys.argv[1:]
    sdcc = "--sdcc" in args or "--sdcc-nohost" in args
    nohost = "--sdcc-nohost" in args
    args = [a for a in args if not a.startswith("--sdcc")]
    out_prefix, suite, root = args[0], args[1], args[2]
    pats = args[3:]
    files = []
    for p in pats:
        files += glob.glob(os.path.join(root, p))
    files = sorted(set(x for x in files if not x.endswith("-lib.c")))
    predef = dict(PREDEF)
    if sdcc:
        if not nohost:
            predef.update(SDCC_PREDEF)
    rows = []
    allkeys = set()
    for path in files:
        f, info = classify_file(path, predef, sdcc)
        c99, gh, gs, i32, fl, env = categories(f)
        row = {"file": os.path.relpath(path, root).replace("\\", "/"), "lines": info["lines"]}
        row.update({k: v for k, v in f.items()})
        row["C99"] = int(bool(c99))
        row["GNU_hard"] = int(bool(gh))
        row["GNU_soft"] = int(bool(gs))
        row["INT32"] = int(bool(i32))
        row["FLOAT"] = int(bool(fl))
        row["ENV"] = int(bool(env))
        row["includes"] = " ".join(info["includes"])
        row["lib_funcs"] = " ".join(info["lib_funcs"])
        row["lib_headers"] = " ".join(sorted(info["lib_headers"]))
        row["max_static_bytes"] = info["max_static_bytes"]
        row["notes"] = " ".join("%s=%s" % (k, ",".join(sorted(v)) if isinstance(v, (set, list)) else v)
                                for k, v in info.items() if k in ("attrs", "builtins", "c99_libfuncs", "mos_bad"))
        if sdcc:
            row["sdcc_test_funcs"] = info.get("sdcc_test_funcs", 0)
        rows.append(row)
        allkeys |= set(row)
    first = ["file", "lines", "C99", "GNU_hard", "GNU_soft", "INT32", "FLOAT", "ENV"]
    keys = first + sorted(k for k in allkeys if k not in first)
    with open(out_prefix + ".csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, keys)
        w.writeheader()
        for r in rows:
            w.writerow(r)
    # summary
    n = len(rows)
    s = {"suite": suite, "tests": n, "lines": sum(r["lines"] for r in rows)}
    c89 = [r for r in rows if not r["C99"]]
    c89_nognu = [r for r in c89 if not r["GNU_hard"] and not r["GNU_soft"]]
    c89_noghard = [r for r in c89 if not r["GNU_hard"]]
    c89_nognu_i = [r for r in c89_nognu if not r["INT32"]]
    c89_noghard_i = [r for r in c89_noghard if not r["INT32"]]
    s["c89_clean"] = len(c89)
    s["c89_no_gnu_strict"] = len(c89_nognu)
    s["c89_no_gnu_hard"] = len(c89_noghard)
    s["c89_no_gnu_strict_no_int32"] = len(c89_nognu_i)
    s["c89_no_gnu_hard_no_int32"] = len(c89_noghard_i)
    s["...no_float(strict)"] = len([r for r in c89_nognu_i if not r["FLOAT"]])
    s["...no_float(shim)"] = len([r for r in c89_noghard_i if not r["FLOAT"]])
    s["...no_float_env_ok(shim)"] = len([r for r in c89_noghard_i if not r["FLOAT"] and not r["ENV"]])
    s["...float_env_ok(shim)"] = len([r for r in c89_noghard_i if not r["ENV"]])
    s["line_comments_in_c89_clean"] = len([r for r in c89 if r.get("line_comments")])
    s["pp_uncertain"] = len([r for r in rows if r.get("pp_uncertain")])
    flagcount = Counter()
    for r in rows:
        for k, v in r.items():
            if k not in first and k not in ("includes", "lib_funcs", "lib_headers", "notes",
                                            "max_static_bytes", "sdcc_test_funcs") and v:
                flagcount[k] += 1
    s["flag_files"] = dict(sorted(flagcount.items()))
    hdr = Counter()
    hdr_c89sub = Counter()
    fn = Counter()
    inc = Counter()
    for r in rows:
        for h in r["lib_headers"].split():
            hdr[h] += 1
            if not r["C99"] and not r["GNU_hard"] and not r["INT32"]:
                hdr_c89sub[h] += 1
        for x in r["lib_funcs"].split():
            fn[x] += 1
        for x in r["includes"].split():
            inc[x] += 1
    s["lib_header_use_files"] = dict(hdr.most_common())
    s["lib_header_use_files_in_usable_subset"] = dict(hdr_c89sub.most_common())
    s["lib_func_use_files"] = dict(fn.most_common())
    s["includes"] = dict(inc.most_common(60))
    s["float_files"] = len([r for r in rows if r["FLOAT"]])
    s["int32_files"] = len([r for r in rows if r["INT32"]])
    s["big_const_files"] = len([r for r in rows if r.get("int32_big_const")])
    attrs = Counter()
    blt = Counter()
    for r in rows:
        for part in r["notes"].split():
            k, _, v = part.partition("=")
            if k == "attrs":
                attrs.update(v.split(","))
            if k == "builtins":
                blt.update(v.split(","))
    s["semantic_attrs"] = dict(attrs.most_common(25))
    s["hard_builtins"] = dict(blt.most_common(25))
    with open(out_prefix + ".json", "w") as fh:
        json.dump(s, fh, indent=1)
    print(json.dumps({k: v for k, v in s.items() if not isinstance(v, dict)}, indent=1))


if __name__ == "__main__":
    sys.setrecursionlimit(20000)
    main()
