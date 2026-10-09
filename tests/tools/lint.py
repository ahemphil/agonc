"""Check what a C compiler cannot: rules the compiler's and library's
sources keep, and that the bootstrap's response files list exactly the
sources there are.

    lint.py         (make lint)

Checked: src/ and tools/, for what a host compiler cannot be told about:
floating point, which the compiler never uses (the bootstrap compiler,
AgDev, has a 32-bit double; softfp.c's integer arithmetic stands in), and
// comments, which are not C89; for fputs (AgDev 3.1.0's appends a
newline; use out_str), for fgetc, getc and fgets (AgDev's take the end of
a file from the byte MOS returns, which on a real Agon can be anything;
use fread, as rd.c and args.c do), for lines over 250 characters, and for printf
formats our C library does not implement. In lib/ as well: no #asm/#endasm
(our own sources use asm("...") where assembly is unavoidable; #asm is for
users' programs), except in the assembly branch of an `#if NAME_ASM` group
(FP_ASM, I64_ASM, STR_ASM) that has an `#else` with the same code in C:
assembly only beside a working C version (softfp.c's and int64.c's
kernels, string.c's busiest functions). Then
bootstrap/*.rsp against the source folders. Exit status 0 if nothing was
found.
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import REPO, PASSES  # noqa: E402

# The compiler uses no floating point: the bootstrap compiler's double is
# 32 bits, so constants are folded by softfp.c's integer arithmetic.
FORBIDDEN_KEYWORDS = ["double", "float"]
KW_RE = re.compile(r"\b(" + "|".join(FORBIDDEN_KEYWORDS) + r")\b")
HASHASM_RE = re.compile(r"^\s*#\s*(asm|endasm)\b")
COND_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b(.*)")

# A printf-family format our library (spec section 8) implements: flags
# - 0 + space, a width and precision (digits or *), h or l, and d i u x X o c s p %.
# Anything else prints wrongly under our libc while AgDev's and the host's
# get it right, so it would show up only once the compiler compiles itself.
FORMAT_CALL_RE = re.compile(r"\b[sf]?printf\s*\((?:[^\"();]*,)?\s*\"((?:[^\"\\\n]|\\.)*)\"")
CONVERSION_RE = re.compile(r"%(?:[-0+ ]*(?:\*|\d+)?(?:\.(?:\*|\d+))?[hl]?[diuxXocsp]|%)")


def strip_comments_and_strings(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + " " * (min(j, n) - i - 1) + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def lint_formats(path, raw):
    problems = []
    for m in FORMAT_CALL_RE.finditer(raw):
        if "%" in CONVERSION_RE.sub("", m.group(1)):
            lineno = raw.count("\n", 0, m.start()) + 1
            problems.append(f"{os.path.relpath(path, REPO)}:{lineno}: format \"{m.group(1)}\" uses a "
                            "conversion our printf does not implement")
    return problems


def lint_file(path):
    with open(path, encoding="ascii") as f:
        raw = f.read()
    problems = lint_formats(path, raw)
    code = strip_comments_and_strings(raw)
    for lineno, (line, rawline) in enumerate(zip(code.split("\n"), raw.split("\n")), 1):
        where = f"{os.path.relpath(path, REPO)}:{lineno}"
        m = KW_RE.search(line)
        if m:
            problems.append(f"{where}: '{m.group(1)}': the compiler uses no floating point")
        if re.search(r"\bfputs\s*\(", line):
            problems.append(f"{where}: fputs appends a newline under AgDev 3.1.0; use out_str (common/io.h)")
        m = re.search(r"\b(fgetc|getc|fgets)\s*\(", line)
        if m:
            problems.append(f"{where}: {m.group(1)} misses the end of a file under AgDev on a real Agon; "
                            "use fread (common/rd.h)")
        if "//" in line:
            problems.append(f"{where}: '//' comment (not C89)")
        if len(rawline) > 250:
            problems.append(f"{where}: line longer than 250 characters")
    return problems + lint_hashasm(path, code)


def lint_hashasm(path, code):
    """#asm only in an `#if NAME_ASM` group's first branch, and only if that
    group has an #else (the C)."""
    problems = []
    rel = os.path.relpath(path, REPO)
    stack = []                  # per open #if: [is NAME_ASM, in its first branch, had #else, line]
    for n, line in enumerate(code.split("\n"), 1):
        m = COND_RE.match(line)
        if m:
            d, rest = m.group(1), m.group(2).strip()
            if d in ("if", "ifdef", "ifndef"):
                stack.append([d == "if" and re.fullmatch(r"[A-Z0-9]+_ASM", rest) is not None, True, False, n])
            elif d in ("else", "elif") and stack:
                stack[-1][1] = False
                stack[-1][2] = stack[-1][2] or d == "else"
            elif d == "endif" and stack:
                group = stack.pop()
                if group[0] and not group[2]:
                    problems.append(f"{rel}:{group[3]}: #if ..._ASM without an #else (the C version)")
        elif HASHASM_RE.search(line) and not any(g[0] and g[1] for g in stack):
            problems.append(f"{rel}:{n}: #asm/#endasm in our own source; use asm(\"...\") "
                            f"(or an #if NAME_ASM group with its C under #else)")
    return problems


def c_files(d):
    return [f"{d}/{f}" for f in sorted(os.listdir(os.path.join(REPO, *d.split("/")))) if f.endswith(".c")]


def lint_responses():
    """Each bootstrap/<prog>.rsp must list its program's .c files, then (for a
    pass) src/common's, each folder in name order - the order build output
    depends on."""
    problems = []
    for prog in PASSES + ["agonc"]:
        want = c_files("src/" + prog) + (c_files("src/common") if prog in PASSES else [])
        got = [w for w in open(os.path.join(REPO, "bootstrap", prog + ".rsp")).read().split() if w.endswith(".c")]
        if got != want:
            problems.append(f"bootstrap/{prog}.rsp lists {got}, but the sources are {want}")
    return problems


def main():
    problems = []
    for top in ("src", "tools"):
        for dirpath, _, names in os.walk(os.path.join(REPO, top)):
            for name in sorted(names):
                if name.endswith((".c", ".h")):
                    problems += lint_file(os.path.join(dirpath, name))
    for dirpath, _, names in os.walk(os.path.join(REPO, "lib")):
        for name in sorted(names):
            if name.endswith((".c", ".h")):
                path = os.path.join(dirpath, name)
                with open(path, encoding="ascii") as f:
                    problems += lint_hashasm(path, strip_comments_and_strings(f.read()))
    problems += lint_responses()
    for p in problems:
        print(p)
    print(f"lint: {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
