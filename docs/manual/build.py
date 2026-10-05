"""Build the manual's PDF: the chapters (docs/manual/0*.md, GitHub Markdown)
through pandoc into Typst, then Typst's compiler with manual.typ, the print
layout, into build/manual/agonc-manual.pdf.

    build.py PANDOC TYPST [VERSION]     (make manual)

VERSION is printed on the cover; by default it is the driver's own
(src/agonc/agonc.c's VERSION).

The chapters link to each other GitHub's way, `file.md#anchor`. In one PDF
every heading needs a unique label, so this script gives each heading an
explicit id (GitHub's anchor for it, with the chapter's number in front
when two chapters share one) and rewrites every link to match; a link that
leads nowhere stops the build, so it also checks the chapters' links for
GitHub. A link to another file in the repository becomes its text followed
by the file's path, since the PDF has no repository to open.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "build", "manual")

HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
LINK = re.compile(r"(?<!\!)\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)\)")


def github_anchor(text):
    """The anchor GitHub gives a heading: its text without markup, lower
    case, punctuation dropped (but not - or _), spaces as hyphens."""
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = text.lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def driver_version():
    with open(os.path.join(REPO, "src", "agonc", "agonc.c"), encoding="utf-8") as f:
        m = re.search(r'#define VERSION "agonc (\S+)', f.read())
    return m.group(1) if m else "dev"


def split_code(lines):
    """Yield (line, in_code) for each line, fenced code blocks marked."""
    fence = None
    for line in lines:
        m = re.match(r"^\s*(```+|~~~+)", line)
        if m:
            if fence is None:
                fence = m.group(1)[:3]
                yield line, True
                continue
            if line.strip().startswith(fence):
                fence = None
                yield line, True
                continue
        yield line, fence is not None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    # a relative path to a tool, made absolute (Windows does not search one)
    pandoc, typst = [os.path.abspath(t) if os.path.exists(t) else t for t in sys.argv[1:3]]
    version = sys.argv[3] if len(sys.argv) > 3 else driver_version()
    names = sorted(f for f in os.listdir(HERE) if re.match(r"\d\d-.*\.md$", f))
    chapters = []
    for name in names:
        with open(os.path.join(HERE, name), encoding="utf-8") as f:
            chapters.append((name, f.read().split("\n")))

    # every heading's GitHub anchor (deduplicated within its file, as GitHub
    # does), and the label it gets in the PDF (unique in the whole manual)
    labels = {}             # (file, anchor) -> label
    first = {}              # file -> its chapter heading's label
    used = set()
    for n, (name, lines) in enumerate(chapters, 1):
        seen = {}
        for line, code in split_code(lines):
            m = HEADING.match(line)
            if code or not m:
                continue
            a = github_anchor(m.group(2))
            k = seen.get(a, 0)
            seen[a] = k + 1
            if k:
                a = "%s-%d" % (a, k)
            label = a if a not in used else "c%d-%s" % (n, a)
            used.add(label)
            labels[(name, a)] = label
            first.setdefault(name, label)

    problems = []
    body = []
    for name, lines in chapters:
        seen = {}

        def relink(m):
            text, target = m.group(1), m.group(2)
            if re.match(r"[a-z]+:", target):
                return m.group(0)
            path, _, anchor = target.partition("#")
            if path == "" or path in first:
                file = path or name
                if anchor == "":
                    return "[%s](#%s)" % (text, first[file])
                if (file, anchor) not in labels:
                    problems.append("%s: link to %s#%s, which has no such heading" % (name, file, anchor))
                    return text
                return "[%s](#%s)" % (text, labels[(file, anchor)])
            full = os.path.normpath(os.path.join(HERE, path))
            if not os.path.exists(full):
                problems.append("%s: link to %s, which does not exist" % (name, path))
            rel = os.path.relpath(full, REPO).replace(os.sep, "/")
            if text.strip("`") == rel or text.strip("`") == os.path.basename(rel):
                return "`%s`" % rel
            return "%s (`%s`)" % (text, rel)

        for line, code in split_code(lines):
            if code:
                body.append(line)
                continue
            m = HEADING.match(line)
            if m:
                a = github_anchor(m.group(2))
                k = seen.get(a, 0)
                seen[a] = k + 1
                if k:
                    a = "%s-%d" % (a, k)
                line = "%s %s {#%s}" % (m.group(1), LINK.sub(relink, m.group(2)), labels[(name, a)])
            else:
                line = LINK.sub(relink, line)
            body.append(line)
        body.append("")
    if problems:
        for p in problems:
            print("manual: " + p)
        return 1

    os.makedirs(OUT, exist_ok=True)
    md = os.path.join(OUT, "manual.md")
    with open(md, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(body))
    typ = os.path.join(OUT, "body.typ")
    r = subprocess.run([pandoc, "-f", "gfm+attributes", "-t", "typst", "--syntax-highlighting=none",
                        "--wrap=preserve", "-o", typ, md])
    if r.returncode:
        return 1
    with open(typ, encoding="utf-8") as f:
        t = f.read()
    # tables left-aligned: pandoc centres them
    t = t.replace("align(center)[#table(", "[#table(")
    with open(typ, "w", encoding="utf-8", newline="\n") as f:
        f.write(t)
    with open(os.path.join(HERE, "manual.typ"), encoding="utf-8") as f:
        layout = f.read()
    with open(os.path.join(OUT, "manual.typ"), "w", encoding="utf-8", newline="\n") as f:
        f.write(layout)
    pdf = os.path.join(OUT, "agonc-manual.pdf")
    r = subprocess.run([typst, "compile", "--ignore-system-fonts", "--input", "version=" + version,
                        os.path.join(OUT, "manual.typ"), pdf])
    if r.returncode:
        return 1
    print("manual: " + os.path.relpath(pdf, REPO))
    return 0


sys.exit(main())
