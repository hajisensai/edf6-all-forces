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
  ('src/config.h', os.path.join(ROOT, 'edf5port'), f'tests/data/{x}', a sibling 'data' folder) or a built artifact
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

# A change to any of these can affect every test: run everything. The CMakeLists.txt files are read by what they say
# (cmake_effects: a test's or target's own lines select that test or target's tests; any other line runs everything),
# the generated labels only the tests that read them (they are labels, no test's behaviour; --check in the gen stage
# keeps them current), common/ the tests that compile it (selection reads the build's own header dependencies).
FULL = (
    'cmake/', 'build.cmd', '.github/', '.gitattributes', '.gitignore',
    'tools/check.py', 'tools/test_domains.py', 'tools/msvc-x64-env.cmd',
    # the test runners and how they find the game: every test runs through them
    'pylib/checklock.py', 'pylib/testshard.py', 'pylib/gamedir.py', 'pylib/gamelease.py', 'pylib/testpool.py',
)
# The CMake files cmake_effects reads; outside FULL (a line it cannot place runs everything).
CMAKE_FILES = ('CMakeLists.txt', 'autoturret/CMakeLists.txt', 'common/CMakeLists.txt')
# Generated from the tests (--write): their labels, no test's behaviour (cmake/ is FULL but for this one).
GENERATED_LABELS = (LABELS_JSON, LABELS_CMAKE)

# (domain, what it is, the files it owns). First match wins; a pattern ending in '/' is a folder, others fnmatch ('*'
# crosses folders).
DOMAINS: list[tuple[str, str, tuple[str, ...]]] = [
    (SCRIPTS, 'the tests themselves: a change selects the tests that run or import the file',
     ('tests/*.py', 'tools/selftest.py', 'tools/*_check.py', 'tools/test_*.py', 'tools/primer_ballistics_test.py',
      'tools/append_log_test.py')),
    ('test-data', 'data files the tests read', ('tests/data/', 'tests/*.json', 'tests/*.inc')),
    ('plugin-native', 'the game plugins\' C++ and what the C++ tests compile',
     ('src/', 'common/', 'CMakeLists.txt', 'autoturret/src/', 'autoturret/CMakeLists.txt', 'autoturret/*.ini',
      'third_party/', 'tests/*.cpp',
      'tests/*.h', 'tests/*.hpp', 'tools/*.cpp', 'tools/*.h', 'EDF6VehicleCrew.ini', 'tests/autopilot/')),
    ('native-codec', 'native/: the installer\'s C++ codecs (edf6cmpl) pylib loads by ctypes', ('native/',)),
    ('autoturret-tools', 'EDF6AutoTurret\'s Python install tools', ('autoturret/',)),
    ('installer', 'the install chain: installer, release, weapon tables, ownership ledger, test hub client',
     ('tools/installer.py', 'tools/build_release.py', 'tools/call_weapons.py', 'tools/calls.py', 'tools/gen_calls.py',
      'tools/gen_stores.py', 'tools/testhub.py', 'tools/save_backup.py', 'tools/support_config.py',
      'tools/support_loadout.py', 'tools/loadout_generator.py', 'tools/strs.py', 'tools/sound_pack.py',
      'tools/wt_sound.py', 'assets/sound/',   # the shipped War Thunder recordings (their SOURCE.md is data, not docs)
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
# The tests that read the whole repository (git ls-files, every source of every folder) without naming what they read:
# every domain. tools/test_domains.py cannot see such a scan; a scan of a module folder it does see (SCANS).
EVERYTHING = {
    'ctest': {'gamedir_literal_guard',           # git ls-files: every .py / .cmd / CMakeLists.txt
              'test_domains'},                   # every test's script and tools/selftest.py (blind_scans), git ls-files
    'selftest': {'cab_floor_katyusha_only'},     # os.listdir of tools/, pylib/, autoturret/tools/: every .py
}
# A listing of a folder built at run time from the repository (ROOT, __file__, a module's constant) with no folder name
# written along the way (os.walk(ROOT), os.walk(eo.FILES)): what it reads, declared, as 'file::top-level function' (or
# '::<module>') -> the files it reads (fnmatch, '*' crosses folders; () = none in the repository: a temporary or the
# game's folder). tools/test_domains.py --check finds every such listing in the tests' scripts and tools/selftest.py
# (blind_scans): one neither declared here nor run only by EVERYTHING's tests fails it.
SCANNED: dict[str, tuple[str, ...]] = {
    'tests/perfrun_test.py::scenario_files': (),                # a temporary game folder's mission
    'tests/save_backup_test.py::<module>': (),                  # a temporary save folder
    'tests/test_worldmap_pack.py::DeveloperFolder': (),         # the developer's model folder (not tracked)
    'tools/selftest.py::_cmake_plugins': ('CMakeLists.txt', '*/CMakeLists.txt'),
    'tools/selftest.py::edf41_outfits_shipped': ('edf41port/outfits/*',),   # edf41_outfits.FILES
    'tools/selftest.py::edf5_dlc_shipped': ('edf5port/dlc/*',),             # edf5_dlc.DLC
}
# The calls that list a folder: a file (or a selftest function) that makes one reads the module folders it names whole,
# the Python files directly in them too ('pylib/*'), not only their data files.
SCANS = {'listdir', 'walk', 'glob', 'iglob', 'rglob', 'iterdir', 'scandir'}
# A built artifact a test names -> the domains its sources are in.
ARTIFACTS = {'edf6cmpl': ('native-codec',)}
NATIVE = ('plugin-native',)
# Where the tools/gen_*.py generators write their C++.
GEN_OUT = ('src/', 'common/', 'autoturret/src/')
# A Python file named by a string ('pylib/vcobjects.py') is read as text unless the code naming it can run one: with
# any of these names in it (or in a local module it imports) the file named is followed through its imports as if
# imported. Read as text, it is a dependency of its own ('~pylib/vcobjects.py'), not what it imports: a selftest
# reading pylib/vcobjects.py's source pulled in 169 files, every model builder.
RUNS = frozenset({
    'subprocess', 'Popen', 'run', 'call', 'check_call', 'check_output', 'system', 'popen', 'startfile', 'runpy',
    'run_path', 'run_module', 'exec', 'eval', 'exec_module', 'spec_from_file_location', 'SourceFileLoader',
    'load_module', 'import_module', '__import__', 'executable', 'execv', 'execve', 'execl', 'spawnv', 'spawnl',
    'create_subprocess_exec', 'create_subprocess_shell', 'module_from_spec', 'reload'})
TEXT = '~'     # '~path': the file's own content only (read as text)
UNRESOLVED = '?'   # '?name': an import of a module the repository does not have (a deleted file's importers)
CPP = '#'      # '#target': the sources a CMake target compiles (the build's dependencies; else its domains)
BYNAME = '='   # '=path': a module a string names by its name ('menu_preview'): imported only by code that can (RUNS)


def _plain(dep: str) -> str:
    return dep[1:] if dep.startswith(TEXT) else dep


def _runs(tree: ast.AST) -> bool:
    return any(isinstance(n, ast.Name) and n.id in RUNS or isinstance(n, ast.Attribute) and n.attr in RUNS
               or isinstance(n, ast.alias) and n.name.split('.')[-1] in RUNS for n in ast.walk(tree))


def _match(pattern: str, path: str) -> bool:
    if pattern.endswith('/'):
        return path.startswith(pattern)
    return fnmatch.fnmatchcase(path, pattern)


def is_full(path: str) -> bool:
    return path not in GENERATED_LABELS and any(_match(p, path) for p in FULL)


def owner(path: str) -> str:
    for name, _, patterns in DOMAINS:
        if any(_match(p, path) for p in patterns):
            return name
    return 'misc'


def tracked(root: str = ROOT) -> list[str]:
    """The repository's (`root`'s) files: the ones git tracks, on disk. Not the untracked ones: a scratch file in a folder a test
    lists would widen (or a stray module named like a real one would bend) the labels of whoever ran --write, and the
    labels would differ between checkouts; a new test is git added before --write."""
    out = subprocess.run(['git', '-c', 'core.quotepath=off', 'ls-files', '--cached'],
                         cwd=root, capture_output=True, text=True, encoding='utf-8', check=True).stdout
    return sorted({p for p in out.splitlines() if p and os.path.isfile(os.path.join(root, p))})


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
        for cm in CMAKE_FILES:
            with open(os.path.join(ROOT, cm), encoding='utf-8') as f:
                self.targets.update(re.findall(r'add_(?:executable|library)\(\s*([A-Za-z_][\w-]*)', f.read()))
        self.target_names = {t.lower() for t in self.targets}
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
        self._runs: dict[str, bool] = {}

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
            out.update(BYNAME + m for m in self.modules[s])   # (not 'check' / 'build': plain words far more often)
        low = s.lower()
        for t in self.targets:
            tl = t.lower()
            if low == tl or tl + '.exe' in low or tl + '.dll' in low:
                out.add(CPP + tl)
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
                    out.update(self.modules.get(a.name.split('.')[0], ()) or self.unresolved(a.name))
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                out.update(self.modules.get(node.module.split('.')[0], ()) or self.unresolved(node.module))
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
        if path != 'tools/selftest.py':   # its listings apply to the selftest tests that reach them (selftest_deps)
            out |= self.declared(path)
        imported = set(out)
        named: set[str] = set()
        for s in self._strings(tree):
            named |= self.string_deps(s, base)
        out |= self.resolve(named, imported, self.runs_file(path),
                            self.runs_file(path) or any(self.runs_file(m) for m in imported if m.endswith('.py')))
        if any(isinstance(n, ast.Attribute) and n.attr in SCANS or isinstance(n, ast.Name) and n.id in SCANS
               for n in ast.walk(tree)):
            out |= whole(out)
        if os.path.basename(path).startswith('gen_'):
            # a generator writes the C++ it names (src/calls.inc) from Python data, it does not read it; check.py's gen
            # stage, which always runs, compares the two
            out = {d for d in out if not d.startswith(GEN_OUT)}
            self._direct[path] = out
        out.discard(path)
        out.discard(TEXT + path)
        return out

    def closure(self, paths: set[str]) -> set[str]:
        """`paths` and, through every Python file among them, what they depend on, transitively (not through a file
        read as text, '~path')."""
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

    @staticmethod
    def unresolved(module: str) -> set[str]:
        """An import of a module no tracked file is: itself, as '?name' (a deleted module's importers), unless the
        standard library has it."""
        top = module.split('.')[0]
        return set() if top in sys.stdlib_module_names or top == '__future__' else {UNRESOLVED + top}

    @staticmethod
    def as_text(deps: set[str]) -> set[str]:
        return {TEXT + d if d.endswith(('.py', '.pyw')) else d for d in deps}

    def resolve(self, named: set[str], imported: set[str], by_name: bool, follows: bool) -> set[str]:
        """What the paths a file's (or a selftest test's) strings name are: the modules named by their name imported
        when its code can import or run by name (`by_name`), else none; the Python files named by path followed through
        their imports when it can run one (`follows`), else read as text."""
        mods = {d[1:] for d in named if d.startswith(BYNAME)} if by_name else set()
        paths = {d for d in named if not d.startswith(BYNAME)}
        return mods | (paths if follows else self.as_text(paths - imported - mods))

    def runs_file(self, path: str) -> bool:
        """Whether a Python file has a name that can run another one (RUNS)."""
        if path not in self._runs:
            try:
                with open(os.path.join(ROOT, path), 'rb') as f:
                    self._runs[path] = _runs(ast.parse(f.read()))
            except (OSError, SyntaxError, ValueError):
                self._runs[path] = True
        return self._runs[path]

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
        out = {CPP + target.lower()}
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
            d = _plain(d)
            if d.startswith('@'):
                out.add(d[1:])
            elif d.startswith(CPP):
                out.update(ARTIFACTS.get(d[1:], NATIVE))
            elif d.startswith(UNRESOLVED):
                continue
            elif d.endswith('/*'):   # a module folder listed: its Python files too (its data files: 'dir/')
                out |= {owner(f) for f in self.files if f.startswith(d[:-1]) and '/' not in f[len(d) - 1:]
                        and f.endswith('.py') and not is_full(f)}
            elif d.endswith('/'):
                out |= self.dir_owners(d)
            elif not is_full(d):
                out.add(owner(d))
        out.discard(SCRIPTS)
        return out

    def declared(self, path: str, functions: set[str] | None = None) -> set[str]:
        """The files SCANNED says `path`'s listings read (only those in `functions`, when given)."""
        out: set[str] = set()
        for key, patterns in SCANNED.items():
            f, fn = key.split('::', 1)
            if f == path and (functions is None or fn in functions):
                for pattern in patterns:
                    out.update(x for x in self.files if fnmatch.fnmatchcase(x, pattern))
        return out

    def scripts_of(self, deps: set[str]) -> list[str]:
        return sorted({_plain(d) for d in deps if not d.startswith(('@', CPP, UNRESOLVED)) and not d.endswith(('/', '/*'))
                       and owner(_plain(d)) == SCRIPTS})


def whole(deps: set[str]) -> set[str]:
    """The module folders among `deps` as listed whole ('pylib/' -> 'pylib/*'): a file that lists a folder it names
    may read every Python file in it."""
    return {d + '*' for d in deps if d.endswith('/') and d.rstrip('/') in MODULE_DIRS}


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
                    stem = os.path.splitext(os.path.basename(r))[0]
                    if name.endswith('.exe') and stem in repo.targets:   # a check a script runs (hail_glide_<I>)
                        deps |= repo.cpp_deps(stem)
                    elif name.endswith(('.exe', '.dll')) and stem.lower() in repo.target_names:
                        deps.add(CPP + stem.lower())
                    elif name.endswith(('.exe', '.dll')) or 'mods' in r.lower():
                        deps |= {'@' + d for d in ARTIFACTS.get(os.path.splitext(name)[0], NATIVE)}
                continue
            deps |= repo.resolve(repo.string_deps(r, ''), set(), True, True)   # a test's command runs what it names
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
        scans = bool(SCANS & set(code.co_names))
        if RUNS & set(code.co_names):
            state['runs'] = True
        for c in code.co_consts:
            if isinstance(c, str):
                named = repo.string_deps(c, tools_dir)
                state['named'].update(named)
                if scans:
                    out.update(whole(named))
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
            state['named'].update(repo.string_deps(v, tools_dir))
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
    # the paths the test's strings name, and whether its code can run a file it names (RUNS): else those it names
    # as Python files are read as text
    state: dict = {}
    for fn in selftest.TESTS:
        deps: set[str] = set()
        uses = {fn.__name__, fn.__code__.co_name}
        state = {'named': set(), 'runs': False}
        value_deps(fn, fn.__globals__, set(), deps, 0)
        imported = set(deps)
        deps |= repo.resolve(state['named'], imported, state['runs'], state['runs'] or any(
            repo.runs_file(m) for m in imported if m.endswith('.py') and m != SELFTEST))
        deps.discard('tools/selftest.py')   # its own module: what this test reaches of it is followed above
        deps.discard(TEXT + 'tools/selftest.py')
        mine = {u for u in uses if u in vars(selftest)}
        deps |= repo.declared('tools/selftest.py', mine)
        out[fn.__name__] = (repo.closure(deps) | {'tools/selftest.py'}, mine)
    return out


# --- folder listings the analysis cannot see
_LISTERS = {('os', 'listdir'), ('os', 'walk'), ('os', 'scandir'), ('glob', 'glob'), ('glob', 'iglob')}
_SHELLS = {('os', 'popen'), ('os', 'system')}
# a command that lists files: git's (in one string, or as a list's items) or the shell's
_GIT_LISTS = re.compile(r'^\s*git(\s+-\S+(\s+[^-\s]\S*)?)*\s+(ls-files|ls-tree|grep)\b')   # git -c k=v ls-files
_SHELL_LISTS = re.compile(r'^\s*(dir|ls|find|tree|where|findstr)\b', re.IGNORECASE)


def _names_folder(text: str) -> bool:
    """Whether a string written in a path names a folder or file: a part of it (between slashes) that is not dots and
    not a pattern ('**', '*.cpp', '?'): those match whatever is there."""
    return any(part.strip('.') and not any(w in part for w in '*?[') for part in re.split(r'[\\/]', text))


def _imported_listers(tree: ast.AST) -> dict[str, tuple[str, str]]:
    """The names a file binds to os / glob (`import os as o`: 'o' -> ('os', '')) and to their listing and shell
    functions (`from os import walk`: 'walk' -> ('os', 'walk'))."""
    out: dict[str, tuple[str, str]] = {}
    for n in ast.walk(tree):
        if isinstance(n, ast.Import):
            for a in n.names:
                if a.name in ('os', 'glob'):
                    out[a.asname or a.name] = (a.name, '')
        elif isinstance(n, ast.ImportFrom) and n.module in ('os', 'glob'):
            for a in n.names:
                if (n.module, a.name) in _LISTERS | _SHELLS:
                    out[a.asname or a.name] = (n.module, a.name)
    return out


def _call_of(call: ast.Call, names: dict[str, tuple[str, str]]) -> tuple[str, str] | None:
    """(module, function) of a call to os / glob, by attribute (os.walk, o.walk) or by its imported name (walk)."""
    f = call.func
    if isinstance(f, ast.Name) and names.get(f.id, ('', ''))[1]:
        return names[f.id]
    if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and names.get(f.value.id, ('', 'x'))[1] == '':
        return names[f.value.id][0], f.attr
    return None


def _listed(call: ast.Call, names: dict[str, tuple[str, str]] | None = None) -> ast.AST | None:
    """The folder a call lists, if it lists one: os.listdir / walk / scandir, glob.glob / iglob (their argument; by
    any name they are imported as: `names`, _imported_listers), p.iterdir() / p.rglob() / p.glob() (the path p)."""
    f = call.func
    if names is None:
        names = {'os': ('os', ''), 'glob': ('glob', '')}
    which = _call_of(call, names)
    if which in _LISTERS:
        if call.args:
            return call.args[0]
        for k in call.keywords:
            if k.arg in ('path', 'top', 'pathname'):
                return k.value
        return ast.Constant('.')
    if isinstance(f, ast.Attribute) and which is None and f.attr in ('iterdir', 'rglob', 'glob'):
        return f.value
    return None


def _bound(target: ast.AST) -> list[str]:
    """The names an assignment binds; not a name inside a subscript or an attribute (os.environ['X'] = ...)."""
    if isinstance(target, ast.Name):
        return [target.id]
    if isinstance(target, (ast.Tuple, ast.List)):
        return [n for e in target.elts for n in _bound(e)]
    if isinstance(target, ast.Starred):
        return _bound(target.value)
    return []


def _binds(scope: ast.AST) -> dict[str, list[ast.AST]]:
    """Every name bound in `scope` -> the expressions bound to it (assignments, loop targets, parameter defaults); not
    the names of a function or class inside it (their own scope), only its parameters' defaults."""
    out: dict[str, list[ast.AST]] = {}

    def own(node: ast.AST):
        yield node
        for c in ast.iter_child_nodes(node):
            if isinstance(c, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.ClassDef)):
                if not isinstance(c, ast.ClassDef):
                    yield c.args
            else:
                yield from own(c)
    for n in own(scope):
        if isinstance(n, (ast.Assign, ast.AnnAssign)) and n.value is not None:
            for t in (n.targets if isinstance(n, ast.Assign) else [n.target]):
                for x in _bound(t):
                    out.setdefault(x, []).append(n.value)
        elif isinstance(n, (ast.For, ast.AsyncFor, ast.comprehension)):
            for x in _bound(n.target):
                out.setdefault(x, []).append(n.iter)
        elif isinstance(n, ast.NamedExpr):
            out.setdefault(n.target.id, []).append(n.value)
        elif isinstance(n, ast.arguments):
            pos = n.posonlyargs + n.args
            for a, d in zip(pos[len(pos) - len(n.defaults):], n.defaults):
                out.setdefault(a.arg, []).append(d)
            for a, d in zip(n.kwonlyargs, n.kw_defaults):
                if d is not None:
                    out.setdefault(a.arg, []).append(d)
    return out


def blind_scans(path: str, local_modules: set[str]) -> list[tuple[str, int, str]]:
    """The folder listings in a Python file whose folder names no folder: (its top-level function or class,
    '<module>' outside one; the line; the call). A listing of a folder built from the repository (ROOT, __file__, a
    local module's constant) with no folder name written anywhere along the way (os.walk(ROOT), os.walk(eo.FILES))
    reads files this analysis cannot see; so does a git command listing or searching files (git ls-files, ls-tree,
    grep: in one string or as a list's items) and a shell listing command (os.popen / os.system of dir, ls, find,
    ...). A pattern ('**', '*.cpp') names no folder. Names are followed through the assignments, loop
    targets and parameter defaults of their function and of the module (any of them, not the one that runs)."""
    with open(path, 'rb') as f:
        tree = ast.parse(f.read())
    imported: set[str] = set()   # the names bound to the repository's own modules
    for n in ast.walk(tree):
        if isinstance(n, ast.Import):
            imported.update(a.asname or a.name.split('.')[0] for a in n.names if a.name.split('.')[0] in local_modules)
        elif isinstance(n, ast.ImportFrom) and n.module:
            imported.update(a.asname or a.name for a in n.names if a.name in local_modules)
    module = _binds(tree)
    names = _imported_listers(tree)

    def judge(expr: ast.AST, env: dict, memo: dict) -> tuple[bool, bool]:
        """(a folder name is written in it, it is built from the repository)"""
        named = anchored = False
        called = {id(c.func) for c in ast.walk(expr) if isinstance(c, ast.Call)}
        for x in ast.walk(expr):
            if isinstance(x, ast.Constant) and isinstance(x.value, str) and _names_folder(x.value):   # not '..', '**'
                named = True
            elif isinstance(x, ast.Name):
                if x.id == '__file__':
                    anchored = True
                    continue
                if x.id not in memo:
                    memo[x.id] = (False, False)   # bound through itself: nothing more
                    n2 = a2 = False
                    for v in env.get(x.id, []) + ([] if env is module else module.get(x.id, [])):
                        n3, a3 = judge(v, env, memo)
                        n2, a2 = n2 or n3, a2 or a3
                    memo[x.id] = (n2, a2)
                named = named or memo[x.id][0]
                anchored = anchored or memo[x.id][1]
            elif isinstance(x, ast.Attribute) and isinstance(x.value, ast.Name) and x.value.id in imported \
                    and id(x) not in called:   # a module's constant (eo.FILES); its function builds from its arguments
                anchored = True
        return named, anchored

    out: list[tuple[str, int, str]] = []
    scopes = (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.ClassDef)

    def visit(node: ast.AST, name: str, env: dict, memo: dict) -> None:
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and (
                node.value in ('ls-files', 'ls-tree') or _GIT_LISTS.search(node.value)):
            out.append((name, node.lineno, f'git: {node.value[:80]}'))
        elif isinstance(node, (ast.List, ast.Tuple)) and any(
                isinstance(e, ast.Constant) and e.value == 'git' for e in node.elts) and any(
                isinstance(e, ast.Constant) and e.value == 'grep' for e in node.elts):
            out.append((name, node.lineno, ast.unparse(node)[:100]))   # ['git', 'grep', ...]: what it searches
        elif isinstance(node, ast.Call) and _call_of(node, names) in _SHELLS and node.args and any(
                isinstance(c, ast.Constant) and isinstance(c.value, str) and _SHELL_LISTS.search(c.value)
                for c in ast.walk(node.args[0])):
            out.append((name, node.lineno, ast.unparse(node)[:100]))   # os.popen('dir /s /b ' + ...): a whole tree
        elif isinstance(node, ast.Call) and (target := _listed(node, names)) is not None:
            named, anchored = judge(target, env, memo)
            if anchored and not named:
                out.append((name, node.lineno, ast.unparse(node)[:100]))
        for c in ast.iter_child_nodes(node):
            if isinstance(c, scopes):   # its own names, then the ones of the scopes around it
                inner = {k: v + env.get(k, []) for k, v in _binds(c).items()}
                visit(c, name, {**env, **inner}, {})
            else:
                visit(c, name, env, memo)

    for top in tree.body:
        name = top.name if isinstance(top, scopes) else '<module>'
        if isinstance(top, scopes):
            visit(top, name, {k: v[:] for k, v in _binds(top).items()}, {})
        else:
            visit(top, name, module, {})
    return out


def blind_scan_problems(repo: Repo, labels: dict) -> list[str]:
    """Each listing blind_scans finds in a test's script or in tools/selftest.py that neither SCANNED declares nor
    only EVERYTHING's tests run; and each SCANNED entry no listing needs any more."""
    runs: dict[str, set[str]] = {}   # a ctest script -> the ctest tests that run or import it
    for name, e in labels['ctest'].items():
        for f in e['scripts']:
            runs.setdefault(f, set()).add(name)
    reach: dict[str, set[str]] = {}  # a tools/selftest.py global -> the selftest tests that reach it
    for name, e in labels['selftest'].items():
        for u in e.get('uses', ()):
            reach.setdefault(u, set()).add(name)
    files = sorted({f for f in runs if f.endswith('.py') and f != 'tools/selftest.py'} | {'tools/selftest.py'})
    problems, seen = [], set()
    for f in files:
        for fn, line, text in blind_scans(os.path.join(ROOT, f), set(repo.modules)):
            key = f'{f}::{fn}'
            seen.add(key)
            if key in SCANNED:
                continue
            tests, every = (reach.get(fn, set()), EVERYTHING['selftest']) if f == 'tools/selftest.py' else \
                (runs.get(f, set()), EVERYTHING['ctest'])
            if tests and tests <= every:
                continue
            problems.append(f'{f}:{line} {text}: lists a folder this analysis cannot see; put the tests running it '
                            f'({", ".join(sorted(tests)[:4]) or "none"}) in EVERYTHING, or declare what it reads: '
                            f'SCANNED[{key!r}]')
    problems += [f'SCANNED[{k!r}]: no such listing any more' for k in sorted(SCANNED.keys() - seen)]
    return problems


# --- the labels
def compute(build: str, deps: dict | None = None, repo: Repo | None = None) -> dict:
    """The labels: each test's domains and scripts, from `deps` (test_deps; computed when not given)."""
    repo = repo or Repo()
    deps = deps or test_deps(build, repo)
    ctest = {name: {'labels': sorted(repo.domains_of(d)), 'scripts': repo.scripts_of(d)}
             for name, d in deps['ctest'].items()}
    # A test CMake adds only on some machines (the native checks against the game's DLL, configured when it is
    # installed): this build has no command to read, so keep what a build that had it wrote.
    try:
        old = load().get('ctest', {})
    except (OSError, ValueError):
        old = {}
    for name in conditional_tests():
        if name not in ctest and name in old:
            ctest[name] = old[name]
    selftest = {name: {'labels': sorted(repo.domains_of(d)), 'scripts': repo.scripts_of(d), 'uses': sorted(uses)}
                for name, (d, uses) in deps['selftest'].items()}
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


def diff_sides(diff: str) -> tuple[set[int], set[int]]:
    """The old-side and new-side line numbers a `git diff -U0` changes (a pure insertion or deletion: the lines on
    either side of it on the other side)."""
    old: set[int] = set()
    new: set[int] = set()
    for m in re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', diff, re.M):
        for first, count, out in ((int(m.group(1)), int(m.group(2) or 1), old), (int(m.group(3)), int(m.group(4) or 1), new)):
            out.update(range(first, first + count) if count else (first, first + 1))
    return old, new


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


def test_deps(build: str, repo: Repo | None = None) -> dict:
    """Every configured test's dependencies, from the code as it is now: {'ctest': {name: deps}, 'selftest': {name:
    (deps, uses)}} (deps: Repo's atoms; EVERYTHING's tests given every domain)."""
    repo = repo or Repo()
    every = {'@' + d for d in NAMES if d != SCRIPTS}
    ctest = {t['name']: ctest_deps(repo, t, build) | (every if t['name'] in EVERYTHING['ctest'] else set())
             for t in ctest_tests(build)}
    selftest = {n: (deps | (every if n in EVERYTHING['selftest'] else set()), uses)
                for n, (deps, uses) in selftest_deps(repo).items()}
    return {'ctest': ctest, 'selftest': selftest}


def _ninja_split(text: str) -> list[str]:
    return [p.replace('$:', ':').replace('$ ', ' ').replace('$$', '$') for p in re.split(r'(?<!\$) ', text.strip()) if p]


def cpp_files(build: str) -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    """What the build compiled, from its own records (build.ninja and ninja's header dependencies): {target: the
    repository files its objects, and those of the libraries it links, were compiled from}, {target: the targets it
    links, itself included}. A target with an object ninja has no valid dependencies for is left out of the first (it
    falls back to its domains). Both empty without a ninja build."""
    try:
        with open(os.path.join(build, 'build.ninja'), encoding='utf-8', errors='replace') as f:
            ninja_text = f.read()
        with open(os.path.join(build, 'CMakeCache.txt'), encoding='utf-8', errors='replace') as f:
            cache = f.read()
        program = re.search(r'^CMAKE_MAKE_PROGRAM:\w+=(.*)$', cache, re.M)
        home = re.search(r'^CMAKE_HOME_DIRECTORY:\w+=(.*)$', cache, re.M)
        source = os.path.normcase(os.path.normpath(home.group(1).strip() if home else ROOT))
        listing = subprocess.run([program.group(1).strip() if program else 'ninja', '-C', build, '-t', 'deps'],
                                 capture_output=True, text=True, encoding='utf-8', errors='replace')
    except (OSError, ValueError):
        return {}, {}
    if listing.returncode:
        return {}, {}

    def key(path: str) -> str:
        return os.path.normcase(os.path.normpath(os.path.join(build, path)))

    def rel(path: str) -> str | None:   # a repository file (the build's sources), not a generated one
        k = key(path)
        if k.startswith(os.path.normcase(os.path.normpath(build)) + os.sep) or not k.startswith(source + os.sep):
            return None
        return os.path.relpath(k, source).replace(os.sep, '/')

    headers: dict[str, set[str] | None] = {}
    obj = None
    for line in listing.stdout.splitlines():
        m = re.match(r'^(\S.*?): #deps \d+, deps mtime \d+ \((\w+)\)', line)
        if m:
            obj = key(m.group(1))
            headers[obj] = set() if m.group(2) == 'VALID' else None
        elif line.startswith('    ') and obj is not None and headers.get(obj) is not None:
            r = rel(line.strip())
            if r:
                headers[obj].add(r)
    objs: dict[str, set[str] | None] = {}   # object -> its source and headers (None: not known)
    linked: dict[str, tuple[str, list[str], list[str]]] = {}   # output -> (target, objects, libraries)
    for m in re.finditer(r'^build (.+?): (?:CXX|C)_(COMPILER|\w+_LINKER)__(\w+?)_(?:unscanned_)?[A-Za-z]+ (.*)$',
                         ninja_text, re.M):
        outs, kind, tgt, rest = m.group(1), m.group(2), m.group(3).lower(), m.group(4)
        explicit, _, implicit = rest.split(' || ')[0].partition(' | ')
        ins = _ninja_split(explicit)
        if kind == 'COMPILER':
            h = headers.get(key(_ninja_split(outs)[0]))
            src = rel(ins[0]) if ins else None
            objs[key(_ninja_split(outs)[0])] = None if h is None or src is None else h | {src}
        else:
            entry = (tgt, [key(i) for i in ins if i.endswith('.obj')],
                     [key(i) for i in ins + _ninja_split(implicit) if i.endswith('.lib')])
            for out_path in _ninja_split(outs):
                linked[key(out_path)] = entry
    by_target = {tgt: (ob, libs) for tgt, ob, libs in linked.values()}

    def walk(t: str, seen: set[str]) -> tuple[set[str] | None, set[str]]:
        ob, libs = by_target[t]
        got: set[str] | None = set()
        names = {t}
        for o in ob:
            h = objs.get(o)
            got = None if h is None or got is None else got | h
        for lib in libs:
            sub = linked.get(lib)
            if sub is None or sub[0] == t or sub[0] in seen:
                continue
            f, n = walk(sub[0], seen | {t})
            names |= n
            got = None if got is None or f is None else got | f
        return got, names

    files: dict[str, set[str]] = {}
    links: dict[str, set[str]] = {}
    for t in by_target:
        f, n = walk(t, {t})
        links[t] = n
        if f is not None:
            files[t] = f
    return files, links


# --- CMake: what a change to a CMakeLists.txt does
# The commands whose effect is the test or target they name (their first argument, or a name their change adds or
# removes); any other command changed (a flag for every target, an if(), project(), a find_package, a foreach whose
# body names no test) runs everything.
CMAKE_NAMED = {
    'add_test', 'add_executable', 'add_library', 'add_dependencies', 'add_custom_target', 'set_target_properties',
    'set_tests_properties', 'target_include_directories', 'target_compile_options', 'target_compile_definitions',
    'target_link_libraries', 'target_link_options', 'target_sources', 'target_compile_features', 'set', 'list',
}


def cmake_commands(text: str) -> list[tuple[int, int, str, list[str]]]:
    """The commands of a CMake file: (first line, last line, lower-case name, argument tokens); comments left out."""
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
        elif c == '#':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c.isalpha() or c == '_':
            m = re.match(r'[A-Za-z_]\w*', text[i:i + 200])
            name = m.group(0)
            j = i + len(name)
            while j < n and text[j] in ' \t':
                j += 1
            if j >= n or text[j] != '(':
                i = j
                continue
            first, depth, k, args, tok = line, 0, j, [], ''
            while k < n:
                ch = text[k]
                if ch == '"':
                    e = k + 1
                    while e < n and text[e] != '"':
                        e += 2 if text[e] == '\\' else 1
                    line += text.count('\n', k, e)
                    tok += text[k + 1:e]
                    k = e + 1
                    continue
                if ch == '#':
                    e = text.find('\n', k)
                    k = n if e < 0 else e
                    continue
                if ch in '() \t\r\n':
                    if tok:
                        args.append(tok)
                        tok = ''
                    if ch == '\n':
                        line += 1
                    elif ch == '(':
                        depth += 1
                    elif ch == ')':
                        depth -= 1
                        if depth == 0:
                            break
                else:
                    tok += ch
                k += 1
            out.append((first, line, name.lower(), args))
            i = k + 1
        else:
            i += 1
    return out


def cmake_effects(old: str, new: str, old_lines: set[int], new_lines: set[int],
                  names: set[str]) -> tuple[set[str], str | None]:
    """What a change to a CMake file selects: (the tests and targets it names, lower-case; None), or (set(), why) when a
    command changed is one whose effect it cannot place: run everything. `old_lines` / `new_lines`: the lines changed
    on each side (diff_lines); `names`: the tests and targets there are (lower-case). A command changed is matched with
    its old self by its name and first argument; what it selects is that argument when it is a test or target, and the
    tests and targets the change adds to or takes from its arguments (a name in a list of tests).
    A call of a function() or macro() the file defines is placed as a named command is (2026-11-26: a new
    edf6_feedback_fixture(<target> <test> ...) line ran every test); a change inside its definition selects the tests
    and targets every call of it names, on either side."""
    sides = []
    hit: set[tuple[str, str]] = set()
    own: set[str] = set()                          # the functions and macros the file defines (either side)
    bodies: set[str] = set()                       # those whose definition the diff touches
    calls: dict[str, set[str]] = {}                # a defined function -> every argument its calls pass (either side)
    for text, lines in ((old, old_lines), (new, new_lines)):
        every: dict[tuple[str, str], list[str]] = {}
        inside = None                              # the definition the commands are in
        for first, last, name, args in cmake_commands(text):
            touched = any(first <= x <= last for x in lines)
            if name in ('function', 'macro') and args:
                inside = args[0].lower()
                own.add(inside)
            if inside is not None:
                if touched:
                    bodies.add(inside)
                if name in ('endfunction', 'endmacro'):
                    inside = None
                continue
            calls.setdefault(name, set()).update(a.lower() for a in args)
            entity = args[1] if name == 'add_test' and len(args) > 1 and args[0] == 'NAME' else (args or [''])[0]
            every.setdefault((name, entity), []).extend(args)
            if touched:
                hit.add((name, entity))
        sides.append(every)
    out: set[str] = set()
    for fn in sorted(bodies):
        got = calls.get(fn, set()) & names
        if not got:
            return set(), f'a CMake {fn}() definition changed whose calls name no test or target'
        out |= got
    for name, entity in sorted(hit):   # a command the diff touches, against all of its name and argument on each side
        a, b = sides[0].get((name, entity), []), sides[1].get((name, entity), [])
        if sorted(a) == sorted(b):
            continue   # moved, or only a comment or the spacing changed
        if name not in CMAKE_NAMED and name not in own:
            return set(), f'a CMake {name}() changed'
        changed = {t.lower() for t in set(a + b) if a.count(t) != b.count(t)}
        got = ({entity.lower()} | changed) & names
        if not got:
            return set(), f'a CMake {name}({entity}) changed that names no test or target'
        out |= got
    return out, None


# --- selection (check.py)
def generator_deps(repo: Repo, script: str) -> set[str]:
    """What a tools/gen_*.py --check depends on: its code and inputs (the closure of its script) and the C++ it writes
    and compares (the GEN_OUT paths its strings name, which py_direct leaves out of its dependencies)."""
    with open(os.path.join(ROOT, script), 'rb') as f:
        tree = ast.parse(f.read())
    outs = {d for s in repo._strings(tree) for d in repo.string_deps(s, os.path.dirname(script))
            if d.startswith(GEN_OUT)}
    return repo.closure({script}) | outs


def file_keys(f: str, deleted: bool = False) -> set[str]:
    """The dependency atoms a change to file `f` hits (the C++ targets' aside: select_files)."""
    keys = {f, TEXT + f, '@' + owner(f)}
    parts = f.split('/')
    for i in range(1, len(parts)):
        d = '/'.join(parts[:i])
        # a module folder named whole: its data files directly in it (its modules count through the imports)
        if d not in MODULE_DIRS or deleted or i == len(parts) - 1 and not f.endswith(SOURCE_EXT):
            keys.add(d + '/')
    if f.endswith('.py'):
        keys.add(os.path.dirname(f) + '/*')
        keys.add(UNRESOLVED + os.path.basename(f)[:-3])
    return keys


def select_files(changed: list[str], deps: dict, cpp: tuple[dict, dict] = ({}, {}), deleted: set[str] = frozenset(),
                 selftest_names: set[str] | None = None,
                 cmake: tuple[set[str], str | None] = (set(), None)) -> dict:
    """The tests `changed` affects, by file: a test runs when a changed file is one of its dependencies (`deps`:
    test_deps), a C++ test when a file its exe was compiled from changed (`cpp`: cpp_files; a target the build has no
    records of: a change in its domains), or when a CMake change names it or a target it runs (`cmake`:
    cmake_effects). `deleted`: the changed files that are gone. `selftest_names`: the globals of tools/selftest.py its
    change falls in (changed_names): a selftest test runs when it uses one; None: every test depending on selftest.py.
    {'full': why or None, 'domains': {domain: [files]}, 'ctest': [names], 'selftest': [names]}."""
    files, links = cpp
    full = [f for f in changed if is_full(f)]
    why = f'{full[0]} changed' + (f' (and {len(full) - 1} more)' if len(full) > 1 else '') if full else cmake[1]
    hit: dict[str, list[str]] = {}
    for f in changed:
        if not is_full(f):
            hit.setdefault(owner(f), []).append(f)
    res: dict = {'full': why, 'domains': hit}
    names: set[str] = set()
    by_file = [f for f in changed if not is_full(f)]
    if SELFTEST in by_file and selftest_names is not None:
        used = {u for _, uses in deps['selftest'].values() for u in uses}
        if selftest_names <= used:   # else: a change to its runner or a helper no test reaches: all of it
            by_file.remove(SELFTEST)
            names = selftest_names
            res['selftest_names'] = sorted(names)
    keys: set[str] = set()
    for f in by_file:
        keys |= file_keys(f, f in deleted)
        keys |= {CPP + t for t, fs in files.items() if f in fs}
    chosen = {n.lower() for n in cmake[0]}
    keys |= {CPP + t for t, linked in links.items() if linked & chosen}   # a target the CMake change names, or links

    def hits(d: set[str]) -> bool:
        if d & keys:
            return True
        # a target the build has no records of (not built yet, or none of the build's): its domains
        return any(a[0] == CPP and a[1:] not in files and set(ARTIFACTS.get(a[1:], NATIVE)) & hit.keys() for a in d)

    res['hits'] = hits   # check.py's gen stage: which generators the change reaches
    res['ctest'] = sorted(n for n, d in deps['ctest'].items() if why or n.lower() in chosen or hits(d))
    res['selftest'] = sorted(n for n, (d, uses) in deps['selftest'].items() if why or set(uses) & names or hits(d))
    return res


def check_report(build: str, deps: dict | None = None, repo: Repo | None = None) -> tuple[int, str]:
    """--check: (exit code, what it says). `deps` / `repo`: test_deps's, when already computed (check.py)."""
    repo = repo or Repo()
    want = compute(build, deps, repo)
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
    problems += blind_scan_problems(repo, want)
    for n, e in {**want['ctest'], **want['selftest']}.items():
        if not e['labels'] and not e['scripts']:
            problems.append(f'test {n} depends on nothing this table knows: no change but a full run would select it')
    if problems:
        return 1, ('test_domains.py --check: the test labels are stale; run python tools/test_domains.py --write and '
                   'reconfigure (build.cmd)\n  ' + '\n  '.join(problems[:80]))
    return 0, f'test_domains.py: {len(want["ctest"])} ctest and {len(want["selftest"])} selftest tests, every one in a domain'


def _check(build: str) -> int:
    code, text = check_report(build)
    print(text)
    return code


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
        r = select_files(files, test_deps(build), cpp_files(build))
        print(json.dumps({k: v for k, v in r.items() if k != 'hits'}, ensure_ascii=False, indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
