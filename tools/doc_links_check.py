"""Docs and code point at each other (eng-12, docs/reviews/2026-11-24-optimization-audit.md): the docs/*.md a source
file names must exist, and the repository paths a doc names (src/, common/, tools/, tests/, pylib/, autoturret/,
testrange/, native/) must too. Run by tools/check.py's gen step and ctest (doc_links); exit 1 lists the dead links.

A doc's path counts when it is a tracked file or directory, the stem of one (src/gravity: src/gravity_*.cpp; the
"src/x.h/.cpp" pair form), or a module's attribute (pylib/mdb.encode: pylib/mdb.py). Not checked: the archives of
the maintainer's own words and of reviews (docs/feedback-*, docs/reviews/), whose text is kept as it was written,
and the plans (docs/plan-*, docs/*-plan.md), which name files still to be written; "native/<word>" with no extension
is prose (native/script checks), not a path. A path of another repository is written with its name in front
(edf-coop-stable:docs/x.md, multislot/src/x.cpp), a deleted file as git names it (c9e917dc^:tests/x.cpp): the
patterns skip a path after ':' or '/'.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE_DIRS = ('src/', 'common/', 'tools/', 'tests/', 'pylib/', 'autoturret/', 'testrange/', 'native/')
CODE_EXT = ('.h', '.hpp', '.cpp', '.inc', '.py', '.cmake', '.cmd', '.txt')
NOT_CHECKED = ('docs/feedback-', 'docs/reviews/', 'docs/plan-')
DOC_REF = re.compile(r'(?<![\w/.:-])docs/[\w./-]+?\.md')
PATH_REF = re.compile(r'(?<![\w/.:\-])((?:%s)[\w./-]*\w)' % '|'.join(d.rstrip('/') + '/' for d in CODE_DIRS))


def tracked() -> list[str]:
    return subprocess.run(['git', '-c', 'core.quotepath=off', 'ls-files'], cwd=ROOT, capture_output=True, text=True,
                          encoding='utf-8', check=True).stdout.splitlines()


def resolves(path: str, files: set[str], dirs: set[str], stems: list[str]) -> bool:
    path = path.rstrip('.')
    if path in files or path in dirs:
        return True
    if '/.' in path:   # src/x.h/.cpp: the pair, either half there
        return resolves(path.split('/.')[0], files, dirs, stems)
    head, _, tail = path.rpartition('.')
    if head and (head + '.py' in files or head + '.cpp' in files or head + '.h' in files):
        return True   # pylib/mdb.encode, tests/x_audit.disassemble: a module's name
    return any(s.startswith(path) for s in stems)   # a stem: src/gravity -> src/gravity_net.cpp


def dead_links() -> list[str]:
    files = tracked()
    fileset = set(files)
    dirs = {f.rsplit('/', i)[0] for f in files for i in range(1, f.count('/') + 1)}
    bad = []
    for f in files:
        if f == 'CMakeLists.txt' or (f.startswith(CODE_DIRS) and f.endswith(CODE_EXT)):
            if f == 'tools/doc_links_check.py':
                continue
            with open(os.path.join(ROOT, f), encoding='utf-8', errors='replace') as h:
                for n, line in enumerate(h, 1):
                    bad += [f'{f}:{n}: {m}' for m in DOC_REF.findall(line) if m not in fileset]
        elif f.startswith('docs/') and f.endswith('.md') and not f.startswith(NOT_CHECKED) and '-plan' not in f:
            with open(os.path.join(ROOT, f), encoding='utf-8', errors='replace') as h:
                for n, line in enumerate(h, 1):
                    bad += [f'{f}:{n}: {m}' for m in PATH_REF.findall(line)
                            if '*' not in m and not (m.startswith('native/') and '.' not in m)
                            and not resolves(m, fileset, dirs, files)]
    return bad


def main() -> int:
    bad = dead_links()
    for b in bad:
        print(b)
    if bad:
        print(f'doc_links_check: {len(bad)} dead link(s): fix the path, or name the other repository in front of it')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
