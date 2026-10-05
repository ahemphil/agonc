"""Fetch the three C test suites into third_party/suites/, at the revisions
v1 is measured against, and classify their tests.

    fetch_suites.py [ctest] [gcc] [sdcc]      (default: all three)

The suites are not in git; their licences are in their own files. A
suite already fetched
at the pinned revision is left alone. Needs git and network access. After
fetching, the classifier (classify.py) writes third_party/suites/out/<suite>.csv,
which `run_suite.py --update-skips` turns into the committed skip lists.
"""
import os, re, shutil, subprocess, sys, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SUITES = os.path.join(REPO, "third_party", "suites")
PY = sys.executable

CTEST = ("https://github.com/c-testsuite/c-testsuite", "5c7275656d751de0e68b2d340a95b5681858ed07")
GCC = ("https://github.com/gcc-mirror/gcc", "f0d56aeb912dd5e5ec7f81eaab23a3715e0c50a8",
       ["/gcc/testsuite/gcc.c-torture/execute/", "/gcc/testsuite/gcc.c-torture/*.exp",
        "/gcc/testsuite/README", "/COPYING", "/COPYING3", "/COPYING.LIB"])
SDCC = ("https://svn.code.sf.net/p/sdcc/code/!svn/bc/%s/", "16923", "trunk/sdcc/support/regression/")

# suite key -> (folder under third_party/suites, test folder, globs, classifier flags)
CLASSIFY = {
    "ctest": ("c-testsuite", "c-testsuite/tests/single-exec", ["*.c"], []),
    "gcc": ("gcc", "gcc/gcc/testsuite/gcc.c-torture/execute", ["*.c"], []),
    "sdcc": ("sdcc-regression", "sdcc-regression/tests", ["*.c", "*.c.in"], ["--sdcc"]),
}


def git(args, cwd):
    subprocess.run(["git"] + args, cwd=cwd, check=True)


def pinned(folder, rev):
    stamp = os.path.join(SUITES, folder, ".agonc-revision")
    return os.path.exists(stamp) and open(stamp).read().strip() == rev


def stamp(folder, rev):
    open(os.path.join(SUITES, folder, ".agonc-revision"), "w").write(rev + "\n")


def fetch_git(folder, url, sha, sparse=None):
    dest = os.path.join(SUITES, folder)
    if pinned(folder, sha):
        print(f"{folder}: already at {sha[:12]}")
        return
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    git(["init", "-q"], dest)
    git(["remote", "add", "origin", url], dest)
    if sparse:
        git(["sparse-checkout", "set", "--no-cone"] + sparse, dest)
        git(["fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", sha], dest)
    else:
        git(["fetch", "-q", "--depth", "1", "origin", sha], dest)
    git(["checkout", "-q", "FETCH_HEAD"], dest)
    stamp(folder, sha)
    print(f"{folder}: fetched {sha[:12]}")


def fetch_sdcc():
    base, rev, path = SDCC
    folder = "sdcc-regression"
    if pinned(folder, rev):
        print(f"{folder}: already at r{rev}")
        return
    dest = os.path.join(SUITES, folder)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    root = base % rev

    def get(url):
        err = None
        for _ in range(5):
            try:
                with urllib.request.urlopen(url, timeout=60) as r:
                    return r.read()
            except Exception as e:      # noqa: BLE001 - retried, then re-raised
                err = e
        raise err

    files = []

    def walk(sub, out):
        url = root + path + sub
        for href in re.findall(r'<li><a href="([^"]+)">', get(url).decode("utf-8", "replace")):
            if href == "../":
                continue
            name = urllib.parse.unquote(href)
            if href.endswith("/"):
                walk(sub + href, os.path.join(out, name.rstrip("/")))
            else:
                files.append((url + href, os.path.join(out, name)))

    walk("", dest)

    def download(item):
        url, target = item
        os.makedirs(os.path.dirname(target), exist_ok=True)
        open(target, "wb").write(get(url))

    with ThreadPoolExecutor(8) as ex:
        list(ex.map(download, files))
    open(os.path.join(dest, "_sdcc_COPYING"), "wb").write(get(root + "trunk/sdcc/COPYING"))
    stamp(folder, rev)
    print(f"{folder}: fetched r{rev}, {len(files)} files")


def classify(key):
    folder, tests, globs, flags = CLASSIFY[key]
    os.makedirs(os.path.join(SUITES, "out"), exist_ok=True)
    subprocess.run([PY, os.path.join(HERE, "classify.py"), os.path.join(SUITES, "out", key), key,
                    os.path.join(SUITES, tests)] + globs + flags, check=True, stdout=subprocess.DEVNULL)
    print(f"{key}: classified into third_party/suites/out/{key}.csv")


def main():
    want = sys.argv[1:] or ["ctest", "gcc", "sdcc"]
    os.makedirs(SUITES, exist_ok=True)
    if "ctest" in want:
        fetch_git("c-testsuite", *CTEST)
    if "gcc" in want:
        fetch_git("gcc", GCC[0], GCC[1], GCC[2])
    if "sdcc" in want:
        fetch_sdcc()
    for key in want:
        classify(key)
    return 0


if __name__ == "__main__":
    sys.setrecursionlimit(20000)
    sys.exit(main())
