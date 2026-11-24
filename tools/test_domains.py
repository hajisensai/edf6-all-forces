"""Test domains: which ctest and selftest tests a change can affect, so tools/check.py runs only those (2026-11-24,
the maintainer: 「类似的测试优化都要做。太慢了，性能浪费很严重」; a src-only branch built every model for 10+ minutes).

  python tools/test_domains.py [--write | --check] [--build DIR]
  python tools/test_domains.py --explain NAME      a test's dependencies and the domains they fall in
  python tools/test_domains.py --select FILE...    the domains and tests these changed files select

How it works:
- DOMAINS splits the repository's files into domains (first matching pattern wins); FULL lists the files a change to
  which can affect any test (the build, the shared C++ library, this table, check.py, pylib's shared base, CI): any of
  them changed runs everything.
- Each test's dependencies are found from the code itself, not by hand: a Python test's script and everything it
  imports (function-local and importlib imports too), transitively, and every string in them naming a repository path
  ('src/crew.h', os.path.join(ROOT, 'edf5port'), f'tests/data/{x}', a sibling 'data' folder) or a built artifact
  ('EDF6VehicleCrew.dll', a CMake target's exe); a C++ test: everything a C++ test can compile or read (src/,
  autoturret/, third_party/, the C++ under tests/ and tools/, and every path a C++ test names); a selftest test:
  the code reachable from its function (the functions, modules and values it names, followed through selftest.py),
  with the same rules. A test's domains are the domains of its dependencies: a change to a file of domain D runs
  every test labelled D. When in doubt it takes more: a bare 'src' string is all of src/, a string equal to a
  module's name is that module.
- The result is tests/test_labels.json (check.py reads it) and cmake/test_labels.cmake (CMake gives every ctest test
  its LABELS from it: ctest -L <domain>). --check (check.py's gen stage, CI) fails when either is stale, when a ctest
  or selftest test has no entry or depends on nothing the table knows, or when a configured ctest test's LABELS are
  not its entry's. check.py also runs a test the labels do not know yet (it passes the tests to skip, not to run).
- A change to tools/selftest.py selects the selftest tests whose function reaches one of the top-level functions,
  classes or names the change falls in (each entry's 'uses'); a change elsewhere in it (an import, the loop making
  the shards) or to a name no test reaches runs every selftest test.

Test scripts (tests/*.py, tools/selftest.py, the tools/*_check.py ctest runs) are in the domain 'test-scripts', which
labels no test: a change to one selects the tests that run or import that file, by file, instead of a whole domain.
"""
from __future__ import annotations

import ast
import dis
import fnmatch
import json
import os
import re
import subprocess
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABELS_JSON = 'tests/test_labels.json'
LABELS_CMAKE = 'cmake/test_labels.cmake'
SCRIPTS = 'test-scripts'

# A change to any of these can affect every test: run everything.
FULL = (
    'CMakeLists.txt', 'cmake/', 'common/', 'build.cmd', '.github/', '.gitattributes', '.gitignore',
    'tools/check.py', 'tools/test_domains.py', 'tools/msvc-x64-env.cmd', LABELS_JSON,
    # pylib's shared base: imported (or used to find the game) by nearly every Python test
    'pylib/checklock.py', 'pylib/testshard.py', 'pylib/gamedir.py', 'pylib/i18n.py', 'pylib/modfiles.py',
    'pylib/rootcpk.py', 'pylib/cpk.py', 'pylib/gamelease.py',
)

# (domain, what it is, the files it owns). First match wins; a pattern ending in '/' is a folder, others fnmatch ('*'
# crosses folders).
DOMAINS: list[tuple[str, str, tuple[str, ...]]] = [
    (SCRIPTS, 'the tests themselves: a change selects the tests that run or import the file',
     ('tests/*.py', 'tools/selftest.py', 'tools/*_check.py', 'tools/test_*.py', 'tools/primer_ballistics_test.py',
      'tools/append_log_test.py')),
    ('test-data', 'data files the tests read', ('tests/data/', 'tests/*.json', 'tests/*.inc')),
    ('plugin-native', 'the game plugins\' C++ and what the C++ tests compile',
     ('src/', 'autoturret/src/', 'autoturret/CMakeLists.txt', 'autoturret/*.ini', 'third_party/', 'tests/*.cpp',
      'tests/*.h', 'tests/*.hpp', 'tools/*.cpp', 'tools/*.h', 'EDF6VehicleCrew.ini', 'tests/autopilot/')),
    ('native-codec', 'native/: the installer\'s C++ codecs (edf6cmpl) pylib loads by ctypes', ('native/',)),
    ('autoturret-tools', 'EDF6AutoTurret\'s Python install tools', ('autoturret/',)),
    ('installer', 'the install chain: installer, release, weapon tables, ownership ledger, test hub client',
     ('tools/installer.py', 'tools/build_release.py', 'tools/call_weapons.py', 'tools/calls.py', 'tools/gen_calls.py',
      'tools/gen_stores.py', 'tools/testhub.py', 'tools/save_backup.py', 'tools/support_config.py',
      'tools/support_loadout.py', 'tools/loadout_generator.py', 'tools/strs.py', 'tools/sound_pack.py',
      'pylib/ledger.py', 'pylib/buildcache.py', 'pylib/modelpack.py', 'pylib/vcobjects.py')),
    ('edf5', 'EDF5 ports: weapons, campaign, DLC', ('edf5port/', 'edf5campaign/', 'pylib/edf5*', 'tools/*edf5*')),
    ('edf41', 'EDF4.1 ports: objects, maps, sounds, outfits', ('edf41port/', 'pylib/edf41*', 'tools/*edf41*')),
    ('cockpit', 'first-person cockpits: their parts, fits, kits and the cut interiors',
     ('pylib/cockpit*', 'models/cockpits/', 'tools/*cockpit*', 'tools/instrument_preview.py')),
    ('worldmap', 'the world map', ('worldmap/', 'tools/*worldmap*', 'pylib/worldmap*')),
    ('testrange', 'the test range launcher and its catalog', ('testrange/', '"testrange/')),
    ('testhub', 'the test site (Cloudflare Worker)', ('testhub/',)),
    ('pylib-model', 'model and asset generation: pylib\'s formats and model builders, the make_* generators',
     ('pylib/', 'models/', 'tools/make_*', 'tools/prep_*', 'tools/gen_*')),
    ('tools-misc', 'the other tools (viewers, reverse-engineering helpers, perf tooling)', ('tools/',)),
    ('docs-guard', 'documentation and agent configuration', ('docs/', '*.md', '.claude/', '.impeccable.md')),
    ('misc', 'anything else tracked', ('*',)),
]
NAMES = [d for d, _, _ in DOMAINS]
# The folders Python modules are imported from (sys.path entries): named as a whole, only their data files count.
MODULE_DIRS = {'pylib', 'tools', 'testrange', 'tests', 'tests/autopilot', 'tools/claude_hooks', 'autoturret/tools'}
SOURCE_EXT = ('.py', '.pyw', '.cpp', '.h', '.hpp', '.c', '.cmd')
# What a file imports in a way the analysis cannot see (a module name built from a variable and imported elsewhere).
EXTRA = {
    'pylib/buildcache.py': ('tools/make_*.py',),          # generator(group): 'make_' + group, imported by tool()
    'tools/cockpit_preview.py': ('pylib/cockpit*.py',),   # import_module(generator) from its command line
}
# A built artifact a test names -> the domains its sources are in.
ARTIFACTS = {'edf6cmpl': ('native-codec',)}
NATIVE = ('plugin-native',)
# Where the tools/gen_*.py generators write their C++.
GEN_OUT = ('src/', 'common/', 'autoturret/src/')


def _match(pattern: str, path: str) -> bool:
    if pattern.endswith('/'):
        return path.startswith(pattern)
    return fnmatch.fnmatchcase(path, pattern)


def is_full(path: str) -> bool:
    return any(_match(p, path) for p in FULL)


def owner(path: str) -> str:
    for name, _, patterns in DOMAINS:
        if any(_match(p, path) for p in patterns):
            return name
    return 'misc'


def tracked() -> list[str]:
    """The repository's files: tracked, and new ones not ignored yet (a test added before its git add), on disk."""
    out = subprocess.run(['git', '-c', 'core.quotepath=off', 'ls-files', '--cached', '--others', '--exclude-standard'],
                         cwd=ROOT, capture_output=True, text=True, encoding='utf-8', check=True).stdout
    return sorted({p for p in out.splitlines() if p and os.path.isfile(os.path.join(ROOT, p))})


class Repo:
    """The tracked files, the local modules by name, the CMake targets; each file's dependencies, cached."""

    def __init__(self) -> None:
        self.files = tracked()
        self.fileset = set(self.files)
        self.dirs: set[str] = set()
        for f in self.files:
            parts = f.split('/')
            for i in range(1, len(parts)):
                self.dirs.add('/'.join(parts[:i]))
        self.top = {f.split('/')[0] for f in self.files}
        self.modules: dict[str, list[str]] = {}
        for f in self.files:
            if f.endswith('.py'):
                self.modules.setdefault(os.path.basename(f)[:-3], []).append(f)
        self.targets: set[str] = set()
        for cm in ('CMakeLists.txt', 'autoturret/CMakeLists.txt'):
            with open(os.path.join(ROOT, cm), encoding='utf-8') as f:
                self.targets.update(re.findall(r'add_(?:executable|library)\(\s*([A-Za-z_][\w-]*)', f.read()))
        self.basenames: dict[str, list[str]] = {}
        for f in self.files:
            if not f.endswith(SOURCE_EXT):
                self.basenames.setdefault(os.path.basename(f).lower(), []).append(f)
        self._dir_owners: dict[str, set[str]] = {}
        self._direct: dict[str, set[str]] = {}
        self._closure: dict[str, set[str]] = {}
        self._cpp: dict[str, list[str]] | None = None
        self._cpp_file: dict[str, tuple[list[str], set[str]]] = {}
        self._strs: dict[tuple[str, str], frozenset[str]] = {}

    # --- a string -> the repository paths (or '@domain' markers) it may name
    def string_deps(self, s: str, base: str) -> set[str]:
        """`s` found in a file in folder `base` (repo-relative): the paths it names. A folder is 'dir/'."""
        key = (s, base)
        if key not in self._strs:
            self._strs[key] = frozenset(self._string_deps(s, base))
        return set(self._strs[key])

    def _string_deps(self, s: str, base: str) -> set[str]:
        out: set[str] = set()
        if not s or len(s) > 300 or re.search(r'\s', s) or not re.search(r'[A-Za-z0-9]', s):   # not prose
            return out
        if s in self.modules and '_' in s:   # a module named by a string: importlib, a generator table, a subprocess
            out.update(self.modules[s])      # (not 'check' / 'build' / 'calls': plain words far more often)
        low = s.lower()
        for t in self.targets:
            tl = t.lower()
            if low == tl or tl + '.exe' in low or tl + '.dll' in low:
                out.update('@' + d for d in ARTIFACTS.get(t, NATIVE))
        if low.endswith('.exe') and not low.startswith('edf'):
            out.update('@' + d for d in NATIVE)
        p = s.replace('\\', '/')
        if os.path.isabs(p):
            r = os.path.relpath(p, ROOT).replace('\\', '/') if p.lower().startswith(ROOT.replace('\\', '/').lower()) else None
            if r is None:
                return out
            p = r
        if p.endswith('.py') and os.path.basename(p)[:-3] in self.modules:
            out.update(self.modules[os.path.basename(p)[:-3]])
        if '/' not in p and '.' in p.strip('.'):   # a bare file name, joined to its folder elsewhere: every such file
            out.update(self.basenames.get(p.lower(), ()))
        cands = [p]
        if base:
            cands.append(os.path.normpath(os.path.join(base, p)).replace('\\', '/'))
        stripped = p
        while stripped.startswith(('../', './', '/')):
            stripped = stripped.split('/', 1)[1] if '/' in stripped else ''
        cands.append(stripped)
        for c in cands:
            c = c.strip('/')
            if not c or c.startswith('..'):
                continue
            if c in self.fileset:
                out.add(c)
            elif c in self.dirs:
                out.add(c + '/')
            elif '/' in c and c.split('/')[0] in self.top:
                # a path built further at run time (f'src/{x}.cpp', 'edf5port/' + name): the deepest folder it names
                head = c.rsplit('/', 1)[0]
                while head and head not in self.dirs:
                    head = head.rsplit('/', 1)[0] if '/' in head else ''
                if head:
                    out.add(head + '/')
        return out

    # --- Python
    @staticmethod
    def _strings(tree: ast.AST) -> list[str]:
        """The strings in `tree`; the constant pieces of a path join (os.path.join(HERE, '..', 'testhub', 'src'),
        ROOT / 'tests' / 'data' / name) as one path ('../testhub/src', 'tests/data'), not as loose words: a bare
        'src' alone is all of src/, inside a join it is the folder it is in."""
        used: set[int] = set()
        out: list[str] = []

        def run(parts: list[ast.AST]) -> None:
            seg: list[str] = []
            for x in parts + [None]:
                if isinstance(x, ast.Constant) and isinstance(x.value, str):
                    seg.append(x.value.replace('\\', '/').strip('/') or '.')
                    used.add(id(x))
                else:
                    if seg:
                        out.append('/'.join(seg))
                    seg = []

        for n in ast.walk(tree):
            if isinstance(n, ast.Call):
                f = n.func
                name = f.attr if isinstance(f, ast.Attribute) else f.id if isinstance(f, ast.Name) else ''
                if name in ('join', 'Path', 'PurePath', 'PureWindowsPath', 'joinpath'):
                    run(list(n.args))
            elif isinstance(n, ast.BinOp) and isinstance(n.op, ast.Div) and id(n) not in used:
                chain: list[ast.AST] = []
                x: ast.AST = n
                while isinstance(x, ast.BinOp) and isinstance(x.op, ast.Div):
                    used.add(id(x))
                    chain.insert(0, x.right)
                    x = x.left
                chain.insert(0, x)
                run(chain)
        out += [n.value for n in ast.walk(tree) if isinstance(n, ast.Constant) and isinstance(n.value, str)
                and id(n) not in used]
        return out

    def py_direct(self, path: str) -> set[str]:
        """A Python file's own dependencies: the local modules it imports and the paths its strings name."""
        if path in self._direct:
            return self._direct[path]
        out: set[str] = set()
        self._direct[path] = out
        try:
            with open(os.path.join(ROOT, path), 'rb') as f:
                tree = ast.parse(f.read())
        except (OSError, SyntaxError, ValueError):
            return out
        base = os.path.dirname(path)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for a in node.names:
                    out.update(self.modules.get(a.name.split('.')[0], ()))
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                out.update(self.modules.get(node.module.split('.')[0], ()))
                for a in node.names:   # from package import module
                    out.update(self.modules.get(a.name, ()))
            elif isinstance(node, ast.Call) and node.args and (
                    isinstance(node.func, ast.Attribute) and node.func.attr == 'import_module'
                    or isinstance(node.func, ast.Name) and node.func.id in ('import_module', '__import__')):
                arg = node.args[0]
                head = None
                if isinstance(arg, ast.BinOp) and isinstance(arg.left, ast.Constant):
                    head = arg.left.value
                elif isinstance(arg, ast.JoinedStr) and arg.values and isinstance(arg.values[0], ast.Constant):
                    head = arg.values[0].value
                if isinstance(head, str):   # importlib.import_module('cockpit_fits_' + family): every such module
                    for m, files in self.modules.items():
                        if m.startswith(head):
                            out.update(files)
        for pattern in EXTRA.get(path, ()):
            out.update(f for f in self.files if fnmatch.fnmatchcase(f, pattern))
        for s in self._strings(tree):
            out |= self.string_deps(s, base)
        if os.path.basename(path).startswith('gen_'):
            # a generator writes the C++ it names (src/calls.inc) from Python data, it does not read it; check.py's gen
            # stage, which always runs, compares the two
            out = {d for d in out if not d.startswith(GEN_OUT)}
            self._direct[path] = out
        out.discard(path)
        return out

    def closure(self, paths: set[str]) -> set[str]:
        """`paths` and, through every Python file among them, what they depend on, transitively."""
        seen: set[str] = set()
        todo = list(paths)
        while todo:
            p = todo.pop()
            if p in seen:
                continue
            seen.add(p)
            if p.endswith('.py') and p in self.fileset:
                todo.extend(self.py_direct(p) - seen)
        return seen

    # --- C++
    def cpp_deps(self, target: str) -> set[str]:
        """A C++ test exe's dependencies: the plugin sources (any of which it may compile, through the shared headers),
        and every path its own sources under tests/ and tools/ (and the local headers they include) name. An exe
        CMake does not list: every path any C++ test names."""
        if self._cpp is None:
            self._cpp = {}
            for cm in ('CMakeLists.txt', 'autoturret/CMakeLists.txt'):
                with open(os.path.join(ROOT, cm), encoding='utf-8') as f:
                    text = f.read()
                for m in re.finditer(r'add_executable\(\s*([A-Za-z_][\w-]*)([^)]*)\)', text):
                    srcs = [s.replace('${CMAKE_SOURCE_DIR}/', '').replace('${CMAKE_CURRENT_SOURCE_DIR}/',
                                                                          os.path.dirname(cm) + '/' if '/' in cm else '')
                            for s in m.group(2).split() if s.endswith(('.cpp', '.c', '.h', '.hpp'))]
                    self._cpp[m.group(1).lower()] = srcs
        own = self._cpp.get(target.lower())
        if own is None:   # foreach(check IN ITEMS ...) add_executable(${check} tests/${check}.cpp)
            own = [f for f in (f'tests/{target}.cpp', f'tools/{target}.cpp') if f in self.fileset] or None
        if own is None:
            own = [f for f in self.files if f.startswith(('tests/', 'tools/')) and f.endswith(('.cpp', '.h', '.hpp'))]
        out = {'@' + d for d in NATIVE}
        lit = re.compile(r'"((?:[^"\\\n]|\\.){1,300})"')
        inc = re.compile(r'#\s*include\s*"([^"]+)"')
        seen: set[str] = set()
        todo = list(own)
        while todo:
            f = todo.pop()
            if f in seen or f not in self.fileset:
                continue
            seen.add(f)
            if not f.startswith(('tests/', 'tools/')):
                continue   # the plugin's own sources: plugin-native already
            if f not in self._cpp_file:
                with open(os.path.join(ROOT, f), encoding='utf-8', errors='replace') as h:
                    text = h.read()
                incs = [os.path.normpath(os.path.join(os.path.dirname(f), s)).replace('\\', '/')
                        for s in inc.findall(text)]
                names: set[str] = set()
                for s in set(lit.findall(text)):
                    if '/' in s or s in self.top or os.path.isdir(os.path.join(ROOT, os.path.dirname(f), s)):
                        names |= {d for d in self.string_deps(s, os.path.dirname(f)) if not d.endswith('.py')}
                self._cpp_file[f] = (incs, names)
            incs, names = self._cpp_file[f]
            todo.extend(incs)
            out |= names
        return out

    # --- dependencies -> domains
    def dir_owners(self, d: str) -> set[str]:
        """The domains of the files in folder `d`. A module folder (pylib/, tools/, ...) named as a whole is a sys.path
        entry or a folder a file is looked up in: its modules count through the imports, its C++ through the C++ tests,
        so only its other files (data) directly in it count."""
        if d not in self._dir_owners:
            if d.rstrip('/') in MODULE_DIRS:
                inside = [f for f in self.files if f.startswith(d) and '/' not in f[len(d):] and not f.endswith(SOURCE_EXT)]
            else:
                inside = [f for f in self.files if f.startswith(d)]
            self._dir_owners[d] = {owner(f) for f in inside if not is_full(f)}
        return self._dir_owners[d]

    def domains_of(self, deps: set[str]) -> set[str]:
        out: set[str] = set()
        for d in deps:
            if d.startswith('@'):
                out.add(d[1:])
            elif d.endswith('/'):
                out |= self.dir_owners(d)
            elif not is_full(d):
                out.add(owner(d))
        out.discard(SCRIPTS)
        return out

    def scripts_of(self, deps: set[str]) -> list[str]:
        return sorted(d for d in deps if not d.startswith('@') and not d.endswith('/') and owner(d) == SCRIPTS)


# --- ctest
def ctest_tests(build: str) -> list[dict]:
    """The tests CMake configured: {'name', 'command': [...], 'properties': {name: value}}, read from the build's
    CTestTestfile.cmake (ctest --show-only leaves out the command of an exe not built yet)."""
    out: list[dict] = []
    by_name: dict[str, dict] = {}

    def args(text: str) -> list[str]:
        return [a.replace('\\"', '"') for a in re.findall(r'"((?:[^"\\]|\\.)*)"', text)]

    def read(folder: str) -> None:
        path = os.path.join(folder, 'CTestTestfile.cmake')
        try:
            with open(path, encoding='utf-8') as f:
                lines = f.read().splitlines()
        except OSError:
            if folder == build:
                raise SystemExit(f'test_domains.py: no {path}: configure the build first (build.cmd)')
            return
        for line in lines:
            m = re.match(r'add_test\(\[=\[(.+?)\]=\] (.*)\)$', line)
            if m:
                t = {'name': m.group(1), 'command': args(m.group(2)), 'properties': {}}
                out.append(t)
                by_name[t['name']] = t
                continue
            m = re.match(r'set_tests_properties\(\[=\[(.+?)\]=\] PROPERTIES (.*)\)$', line)
            if m and m.group(1) in by_name:
                for key, value in re.findall(r'(\w+) ("(?:[^"\\]|\\.)*")', m.group(2)):
                    if key != '_BACKTRACE_TRIPLES':
                        by_name[m.group(1)]['properties'][key] = args(value)[0]
                continue
            m = re.match(r'subdirs\("?(.*?)"?\)$', line)
            if m:
                read(os.path.join(folder, m.group(1)))
    read(build)
    return out


def conditional_tests() -> set[str]:
    """The tests CMakeLists.txt adds inside an if(EXISTS ...): configured only where that file is."""
    with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
        text = f.read()
    out: set[str] = set()
    depth = 0
    for line in text.splitlines():
        s = line.strip()
        if re.match(r'if\s*\(.*\bEXISTS\b', s) or depth and re.match(r'if\s*\(', s):
            depth += 1
        elif depth and re.match(r'endif\s*\(', s):
            depth -= 1
        elif depth:
            m = re.match(r'add_test\(\s*NAME\s+([\w.-]+)', s)
            if m:
                out.add(m.group(1))
    return out


def _rel(arg: str, build: str) -> str | None:
    a = os.path.normcase(os.path.normpath(arg))
    for base, tag in ((build, '@build/'), (ROOT, '')):
        b = os.path.normcase(os.path.normpath(base))
        if a == b or a.startswith(b + os.sep):
            return tag + os.path.relpath(arg, base).replace('\\', '/')
    return None


def ctest_deps(repo: Repo, test: dict, build: str) -> set[str]:
    cmd = test.get('command') or []
    if not cmd:
        return set()
    props = [x for k, v in test.get('properties', {}).items() if k != 'LABELS' for x in v.split(';')]
    deps: set[str] = set()
    exe = os.path.basename(cmd[0]).lower()
    args = cmd[1:]
    if exe.startswith('python'):
        while args and args[0].startswith('-'):   # python -B / -m unittest ...
            if args[0] == '-m' and len(args) > 1:
                deps.update(repo.modules.get(args[1].split('.')[0], ()))
                args = args[2:]
                break
            args = args[1:]
    elif _rel(cmd[0], build) and _rel(cmd[0], build).startswith('@build/'):
        deps |= repo.cpp_deps(os.path.splitext(os.path.basename(cmd[0]))[0])
    else:   # cmake -P, a tool we do not know: everything it could touch
        deps |= {'@' + d for d in NAMES if d != SCRIPTS}
    for a in args + props:
        for piece in re.split(r'[;=]', a):
            r = _rel(piece, build) if os.path.isabs(piece) else None
            if r is None:
                continue
            if r.startswith('@build/'):
                if r != '@build/' and not r.endswith('/build'):
                    name = os.path.basename(r).lower()
                    if name.endswith(('.exe', '.dll')) or 'mods' in r.lower():
                        deps |= {'@' + d for d in ARTIFACTS.get(os.path.splitext(name)[0], NATIVE)}
                continue
            deps |= repo.string_deps(r, '')
    return repo.closure(deps)


# --- selftest
def selftest_deps(repo: Repo) -> dict[str, tuple[set[str], set[str]]]:
    """Each selftest test's dependencies (tools/selftest.py, and what is reachable from its function) and the names of
    selftest.py's own globals it reaches (its function, the helpers and tables it uses): a change to tools/selftest.py
    inside one of them selects the tests that reach it, not every selftest test."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import selftest   # noqa: E402  (sets its own sys.path)
    tools_dir = 'tools'
    mod_files = {}
    for name, m in list(sys.modules.items()):
        f = getattr(m, '__file__', None)
        r = _rel(f, os.path.join(ROOT, 'build')) if f else None
        if r and not r.startswith('@'):
            mod_files[name] = r

    def code_deps(code: types.CodeType, glb: dict, seen: set, out: set) -> None:
        if id(code) in seen:
            return
        seen.add(id(code))
        for c in code.co_consts:
            if isinstance(c, str):
                out.update(repo.string_deps(c, tools_dir))
            elif isinstance(c, types.CodeType):
                code_deps(c, glb, seen, out)
            elif isinstance(c, (tuple, frozenset)):
                value_deps(c, glb, seen, out, 0)
        for ins in dis.get_instructions(code):
            if ins.opname == 'IMPORT_NAME' and isinstance(ins.argval, str):
                out.update(repo.modules.get(ins.argval.split('.')[0], ()))
            elif ins.opname in ('LOAD_GLOBAL', 'LOAD_NAME', 'STORE_GLOBAL') and glb is selftest.__dict__:
                uses.add(str(ins.argval).replace(' + NULL', '').replace('NULL + ', ''))
        for n in code.co_names:   # globals and attribute names alike: more than it needs, never less
            if n in glb:
                value_deps(glb[n], glb, seen, out, 0)

    def value_deps(v, glb: dict, seen: set, out: set, depth: int) -> None:
        if depth > 6:
            return
        if isinstance(v, str):
            out.update(repo.string_deps(v, tools_dir))
            return
        if isinstance(v, (int, float, bool, bytes, type(None))):
            return
        if id(v) in seen:
            return
        seen.add(id(v))
        if isinstance(v, types.ModuleType):
            if v.__name__ in mod_files:
                out.add(mod_files[v.__name__])
            return
        if isinstance(v, (list, tuple, set, frozenset)):
            for x in list(v)[:2000]:
                value_deps(x, glb, seen, out, depth + 1)
            return
        if isinstance(v, dict):
            for k, x in list(v.items())[:2000]:
                value_deps(k, glb, seen, out, depth + 1)
                value_deps(x, glb, seen, out, depth + 1)
            return
        fn = getattr(v, '__func__', v)
        if isinstance(fn, types.FunctionType):
            if fn.__module__ == selftest.__name__:
                code_deps(fn.__code__, fn.__globals__, seen, out)
                for d in fn.__defaults__ or ():
                    value_deps(d, glb, seen, out, depth + 1)
                for cell in fn.__closure__ or ():
                    try:
                        value_deps(cell.cell_contents, glb, seen, out, depth + 1)
                    except ValueError:
                        pass
            elif fn.__module__ in mod_files:
                out.add(mod_files[fn.__module__])
            return
        if isinstance(v, type):
            if v.__module__ == selftest.__name__:
                for x in vars(v).values():
                    value_deps(x, glb, seen, out, depth + 1)
            elif v.__module__ in mod_files:
                out.add(mod_files[v.__module__])
            return
        m = getattr(type(v), '__module__', None)
        if m in mod_files:
            out.add(mod_files[m])
        elif m == selftest.__name__:
            value_deps(type(v), glb, seen, out, depth + 1)
            for x in getattr(v, '__dict__', {}).values():
                value_deps(x, glb, seen, out, depth + 1)

    out: dict[str, tuple[set[str], set[str]]] = {}
    uses: set[str] = set()
    for fn in selftest.TESTS:
        deps: set[str] = set()
        uses = {fn.__name__, fn.__code__.co_name}
        value_deps(fn, fn.__globals__, set(), deps, 0)
        deps.discard('tools/selftest.py')   # its own module: what this test reaches of it is followed above
        out[fn.__name__] = (repo.closure(deps) | {'tools/selftest.py'}, {u for u in uses if u in vars(selftest)})
    return out


# --- the labels
def compute(build: str) -> dict:
    repo = Repo()
    ctest = {}
    for t in ctest_tests(build):
        deps = ctest_deps(repo, t, build)
        ctest[t['name']] = {'labels': sorted(repo.domains_of(deps)), 'scripts': repo.scripts_of(deps)}
    # A test CMake adds only on some machines (the native checks against the game's DLL, configured when it is
    # installed): this build has no command to read, so keep what a build that had it wrote.
    try:
        old = load().get('ctest', {})
    except (OSError, ValueError):
        old = {}
    for name in conditional_tests():
        if name not in ctest and name in old:
            ctest[name] = old[name]
    selftest = {}
    for name, (deps, uses) in selftest_deps(repo).items():
        selftest[name] = {'labels': sorted(repo.domains_of(deps)), 'scripts': repo.scripts_of(deps),
                          'uses': sorted(uses)}
    return {
        'about': 'generated by tools/test_domains.py --write: each test\'s domains (the domains of the files it depends '
                 'on) and the test scripts it runs or imports. check.py runs a test when a changed file is in one of its '
                 'domains or is one of its scripts.',
        'domains': {d: what for d, what, _ in DOMAINS},
        'ctest': dict(sorted(ctest.items())),
        'selftest': dict(sorted(selftest.items())),
    }


def cmake_text(labels: dict) -> str:
    lines = ['# Generated by tools/test_domains.py --write from tests/test_labels.json: every ctest test\'s domains as its',
             '# LABELS (ctest -L <domain>). Do not edit; a test missing here fails tools/test_domains.py --check.', '']
    for name, entry in labels['ctest'].items():
        lab = ';'.join(entry['labels']) or 'none'
        lines.append(f'if(TEST {name})\n  set_tests_properties({name} PROPERTIES LABELS "{lab}")\nendif()')
    return '\n'.join(lines) + '\n'


def dump(labels: dict) -> str:
    return json.dumps(labels, ensure_ascii=False, indent=1) + '\n'


def load() -> dict:
    with open(os.path.join(ROOT, LABELS_JSON), encoding='utf-8') as f:
        return json.load(f)


# --- selection (check.py)
SELFTEST = 'tools/selftest.py'


def diff_lines(diff: str) -> set[int]:
    """The new-side line numbers a `git diff -U0` touches (a deletion: the line after it)."""
    out: set[int] = set()
    for m in re.finditer(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', diff, re.M):
        first, count = int(m.group(1)), int(m.group(2) or 1)
        out.update(range(first, first + count) if count else (first, first + 1))
    return out


def changed_names(source: str, lines: set[int]) -> set[str] | None:
    """The top-level functions, classes and assigned names of `source` the changed `lines` fall in; None when one falls
    in other module-level code (an import, a loop that makes tests): any test may depend on that."""
    tree = ast.parse(source)
    out: set[str] = set()
    spans = []
    for node in tree.body:
        first = min([node.lineno] + [d.lineno for d in getattr(node, 'decorator_list', [])])
        spans.append((first, node.end_lineno or node.lineno, node))
    for line in lines:
        node = next((n for a, b, n in spans if a <= line <= b), None)
        if node is None:
            continue   # a blank line or a comment between the statements
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            out.add(node.name)
        elif isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            names = [t.id for t in targets if isinstance(t, ast.Name)]
            if len(names) != len(targets):
                return None
            out.update(names)
        elif isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant):
            continue   # the module's docstring
        else:
            return None
    return out


def select(changed: list[str], labels: dict, selftest_names: set[str] | None = None) -> dict:
    """The tests `changed` affects: {'full': reason or None, 'domains': {domain: [files]}, 'scripts': [files],
    'ctest': [names], 'selftest': [names]}. `selftest_names`: the globals of tools/selftest.py its change falls in
    (changed_names); a selftest test runs when it uses one. None: every test that runs selftest.py."""
    full = [f for f in changed if is_full(f)]
    hit: dict[str, list[str]] = {}
    scripts: list[str] = []
    for f in changed:
        if is_full(f):
            continue
        d = owner(f)
        if d == SCRIPTS:
            scripts.append(f)
        else:
            hit.setdefault(d, []).append(f)
    res = {'full': f'{full[0]} changed' + (f' (and {len(full) - 1} more)' if len(full) > 1 else '') if full else None,
           'domains': hit, 'scripts': scripts}
    by_file = set(scripts)
    names: set[str] = set()
    if SELFTEST in by_file and selftest_names is not None:
        used = {u for e in labels.get('selftest', {}).values() for u in e.get('uses', ())}
        if selftest_names <= used:   # else: a change to its runner or a helper no test reaches: all of it
            by_file.discard(SELFTEST)
            names = selftest_names
            res['selftest_names'] = sorted(names)
    for part in ('ctest', 'selftest'):
        res[part] = sorted(n for n, e in labels.get(part, {}).items()
                           if res['full'] or set(e['labels']) & hit.keys() or set(e['scripts']) & by_file
                           or set(e.get('uses', ())) & names)
    return res


def _check(build: str) -> int:
    want = compute(build)
    problems = []
    try:
        have = load()
    except (OSError, ValueError) as e:
        have = {}
        problems.append(f'{LABELS_JSON}: {e}')
    if have != want:
        for part in ('ctest', 'selftest'):
            a, b = have.get(part, {}), want[part]
            for n in sorted(b.keys() - a.keys()):
                problems.append(f'{part} test {n} has no domain in {LABELS_JSON}')
            for n in sorted(a.keys() - b.keys()):
                problems.append(f'{part} test {n} in {LABELS_JSON} no longer exists')
            for n in sorted(a.keys() & b.keys()):
                if a[n] != b[n]:
                    problems.append(f'{part} test {n}: {a[n]} -> {b[n]}')
        if have.get('domains') != want['domains']:
            problems.append('the domain table changed')
    try:
        with open(os.path.join(ROOT, LABELS_CMAKE), encoding='utf-8') as f:
            if f.read() != cmake_text(want):
                problems.append(f'{LABELS_CMAKE} is not what {LABELS_JSON} says')
    except OSError as e:
        problems.append(f'{LABELS_CMAKE}: {e}')
    # the labels CMake configured: every ctest test has one of ours
    for t in ctest_tests(build):
        labs = t['properties'].get('LABELS')
        labs = labs.split(';') if labs else None
        if not labs:
            problems.append(f'ctest test {t["name"]} has no LABELS (reconfigure after --write: build.cmd)')
        elif labs != (want['ctest'].get(t['name'], {}).get('labels') or ['none']):
            problems.append(f'ctest test {t["name"]} LABELS {labs} != {want["ctest"][t["name"]]["labels"]} (reconfigure)')
    for n, e in {**want['ctest'], **want['selftest']}.items():
        if not e['labels'] and not e['scripts']:
            problems.append(f'test {n} depends on nothing this table knows: no change but a full run would select it')
    if problems:
        print('test_domains.py --check: the test labels are stale; run python tools/test_domains.py --write and '
              'reconfigure (build.cmd)\n  ' + '\n  '.join(problems[:80]))
        return 1
    print(f'test_domains.py: {len(want["ctest"])} ctest and {len(want["selftest"])} selftest tests, every one in a domain')
    return 0


def _explain(name: str, build: str) -> int:
    repo = Repo()
    deps = None
    for t in ctest_tests(build):
        if t['name'] == name:
            deps = ctest_deps(repo, t, build)
    if deps is None:
        deps = selftest_deps(repo).get(name, (None,))[0]
    if deps is None:
        print(f'no test {name}')
        return 1
    for d in sorted(deps):
        print(f'  {d:70} {d[1:] if d.startswith("@") else ",".join(sorted(repo.domains_of({d}))) or "(full)"}')
    print('domains:', ', '.join(sorted(repo.domains_of(deps))))
    return 0


def main(argv: list[str]) -> int:
    build = os.path.join(ROOT, 'build')
    if '--build' in argv:
        build = os.path.abspath(argv[argv.index('--build') + 1])
    if '--write' in argv:
        labels = compute(build)
        with open(os.path.join(ROOT, LABELS_JSON), 'w', encoding='utf-8', newline='\n') as f:
            f.write(dump(labels))
        with open(os.path.join(ROOT, LABELS_CMAKE), 'w', encoding='utf-8', newline='\n') as f:
            f.write(cmake_text(labels))
        print(f'wrote {LABELS_JSON} and {LABELS_CMAKE}: {len(labels["ctest"])} ctest, {len(labels["selftest"])} selftest')
        return 0
    if '--check' in argv:
        return _check(build)
    if '--explain' in argv:
        return _explain(argv[argv.index('--explain') + 1], build)
    if '--select' in argv:
        files = argv[argv.index('--select') + 1:]
        r = select(files, load())
        print(json.dumps({k: v for k, v in r.items()}, ensure_ascii=False, indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
