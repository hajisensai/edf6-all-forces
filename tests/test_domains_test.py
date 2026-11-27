"""tools/test_domains.py and tools/check.py's selection: what a change selects, what makes a run whole, the selftest
names a change to tools/selftest.py falls in, and one check per worktree (pylib/checklock.py worktree_once)."""
from __future__ import annotations

import contextlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import checklock  # noqa: E402
import test_domains as td  # noqa: E402


BUILD = os.path.join(ROOT, 'build')


def write(folder: str, name: str, text: str) -> str:
    path = os.path.join(folder, name)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    return path


class Select(unittest.TestCase):
    """select_files: by file, on synthetic dependencies (no build needed)."""

    DEPS = {
        'ctest': {
            'a_cpp_test': {'#a_cpp_test', 'tests/data/a.json'},
            'unbuilt_cpp': {'#unbuilt_cpp'},
            'py_test': {'tests/py_test.py', 'pylib/mdb.py', 'pylib/', '~pylib/vcobjects.py', '?gone_mod'},
            'reads_all': {'@' + d for d in td.NAMES if d != td.SCRIPTS},
        },
        'selftest': {
            'uses_x': ({'tools/selftest.py', 'src/heli.cpp'}, {'uses_x', 'helper_x'}),
            'uses_y': ({'tools/selftest.py', 'pylib/mdb.py'}, {'uses_y'}),
        },
    }
    CPP = ({'a_cpp_test': {'tests/a_cpp_test.cpp', 'src/heli.h', 'common/edf/memory.h'}},
           {'a_cpp_test': {'a_cpp_test', 'edf6common'}, 'unbuilt_cpp': {'unbuilt_cpp'}})

    def sel(self, files, deleted=frozenset(), names=None, cmake=(set(), None)):
        return td.select_files(files, self.DEPS, self.CPP, set(deleted), names, cmake)

    def test_a_cpp_test_runs_for_what_its_exe_compiled(self) -> None:
        r = self.sel(['src/heli.h'])
        self.assertIsNone(r['full'])
        self.assertIn('a_cpp_test', r['ctest'])
        self.assertIn('unbuilt_cpp', r['ctest'], 'a target the build has no records of: its domain')
        self.assertNotIn('py_test', r['ctest'])
        r = self.sel(['src/other.cpp'])
        self.assertNotIn('a_cpp_test', r['ctest'], 'a plugin source its exe does not compile')
        self.assertIn('unbuilt_cpp', r['ctest'])
        self.assertIn('a_cpp_test', self.sel(['common/edf/memory.h'])['ctest'], 'common/: the tests compiling it')

    def test_a_file_read_as_text_is_only_itself(self) -> None:
        self.assertIn('py_test', self.sel(['pylib/vcobjects.py'])['ctest'])
        self.assertIn('py_test', self.sel(['pylib/mdb.py'])['ctest'])
        self.assertEqual(self.sel(['pylib/mdb.py'])['selftest'], ['uses_y'])
        self.assertNotIn('py_test', self.sel(['pylib/xm_walker.py'])['ctest'])

    def test_a_module_folder_named_whole_is_its_data(self) -> None:
        self.assertIn('py_test', self.sel(['pylib/LICENSE.x'])['ctest'])
        self.assertNotIn('py_test', self.sel(['pylib/other.py'])['ctest'])
        self.assertIn('py_test', self.sel(['pylib/other.py'], deleted={'pylib/other.py'})['ctest'])

    def test_a_deleted_module_selects_its_importers(self) -> None:
        self.assertIn('py_test', self.sel(['tools/gone_mod.py'], deleted={'tools/gone_mod.py'})['ctest'])

    def test_shared_files_run_everything(self) -> None:
        for f in ('cmake/build_id.cmake', 'tools/check.py', 'tools/test_domains.py', '.github/workflows/build.yml',
                  'pylib/checklock.py', 'pylib/testshard.py', 'build.cmd'):
            r = self.sel([f, 'src/heli.cpp'])
            self.assertTrue(r['full'], f)
            self.assertEqual(len(r['ctest']), len(self.DEPS['ctest']), f)
        for f in ('CMakeLists.txt', 'common/edf/memory.h', td.LABELS_JSON, td.LABELS_CMAKE, 'pylib/rootcpk.py'):
            self.assertFalse(td.is_full(f), f)
        self.assertTrue(self.sel(['src/heli.cpp'], cmake=(set(), 'a CMake if() changed'))['full'])

    def test_every_domain_test_follows_any_change(self) -> None:
        for f in ('docs/agent-rules-detail.md', 'pylib/mdb.py', 'testhub/src/a.ts', 'misc.txt'):
            self.assertIn('reads_all', self.sel([f])['ctest'], f)

    def test_cmake_names_select_their_tests(self) -> None:
        r = self.sel(['CMakeLists.txt'], cmake=({'edf6common'}, None))
        self.assertIn('a_cpp_test', r['ctest'], 'a target its exe links')
        r = self.sel(['CMakeLists.txt'], cmake=({'py_test'}, None))
        self.assertIn('py_test', r['ctest'])
        self.assertNotIn('a_cpp_test', r['ctest'])

    def test_selftest_change_selects_the_tests_using_it(self) -> None:
        self.assertEqual(self.sel([td.SELFTEST], names={'helper_x'})['selftest'], ['uses_x'])
        self.assertEqual(self.sel([td.SELFTEST], names=None)['selftest'], ['uses_x', 'uses_y'])
        self.assertEqual(self.sel([td.SELFTEST], names={'nobody'})['selftest'], ['uses_x', 'uses_y'])

    def test_docs_only(self) -> None:
        r = self.sel(['docs/agent-rules-detail.md'])
        self.assertEqual(r['ctest'], ['reads_all'])
        self.assertEqual(r['selftest'], [])

    def test_changed_names(self) -> None:
        src = ('"""doc."""\nimport os\n\nX = 1\n\n\n@deco\ndef a():\n    return X\n\n\nclass B:\n    pass\n\n'
               'for i in range(2):\n    pass\n')
        self.assertEqual(td.changed_names(src, {1}), set())
        self.assertEqual(td.changed_names(src, {4, 7, 9}), {'X', 'a'})
        self.assertEqual(td.changed_names(src, {13, 5}), {'B'})
        self.assertIsNone(td.changed_names(src, {2}))
        self.assertIsNone(td.changed_names(src, {16}))
        diff = '@@ -3,0 +4,2 @@\n+x\n+y\n@@ -10 +12 @@\n-a\n+b\n@@ -20,2 +21,0 @@\n-c\n-d\n'
        self.assertEqual(td.diff_lines(diff), {4, 5, 12, 21, 22})
        self.assertEqual(td.diff_sides(diff), ({3, 4, 10, 20, 21}, {4, 5, 12, 21, 22}))


class CMakeEffects(unittest.TestCase):
    OLD = ('cmake_minimum_required(VERSION 3.20)\n'
           'add_compile_options(/W4)\n'
           'set(CHECKS a_check b_check)\n'
           'add_executable(a_check EXCLUDE_FROM_ALL tools/a_check.cpp)\n'
           'target_include_directories(a_check PRIVATE src)  # a comment\n'
           'foreach(check IN LISTS CHECKS)\n'
           '  add_test(NAME ${check} COMMAND ${check})\n'
           'endforeach()\n'
           'add_test(NAME py_test COMMAND python tests/py_test.py)\n'
           'set_tests_properties(py_test PROPERTIES TIMEOUT 300)\n')
    NAMES = {'a_check', 'b_check', 'c_check', 'py_test'}

    def effects(self, new: str):
        old_lines, new_lines = set(), set()
        a, b = self.OLD.splitlines(), new.splitlines()
        import difflib
        for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b).get_opcodes():
            if tag != 'equal':
                old_lines |= set(range(i1 + 1, i2 + 1)) or {i1, i1 + 1}
                new_lines |= set(range(j1 + 1, j2 + 1)) or {j1, j1 + 1}
        return td.cmake_effects(self.OLD, new, old_lines, new_lines, self.NAMES)

    def test_a_test_added_to_a_list_selects_it(self) -> None:
        new = self.OLD.replace('set(CHECKS a_check b_check)', 'set(CHECKS a_check b_check c_check)')
        self.assertEqual(self.effects(new), ({'c_check'}, None))

    def test_a_test_s_own_command_selects_it(self) -> None:
        self.assertEqual(self.effects(self.OLD.replace('TIMEOUT 300', 'TIMEOUT 600')), ({'py_test'}, None))
        new = self.OLD.replace('PRIVATE src)', 'PRIVATE src common)')
        self.assertEqual(self.effects(new), ({'a_check'}, None))
        new = self.OLD + 'add_executable(c_check tools/c_check.cpp)\nadd_test(NAME c_check COMMAND c_check)\n'
        self.assertEqual(self.effects(new), ({'c_check'}, None))

    def test_comments_and_spacing_select_nothing(self) -> None:
        self.assertEqual(self.effects(self.OLD.replace('# a comment', '# another')), (set(), None))
        self.assertEqual(self.effects(self.OLD.replace('TIMEOUT 300', 'TIMEOUT   300')), (set(), None))

    def test_anything_else_runs_everything(self) -> None:
        for new in (self.OLD.replace('/W4', '/W3'),                            # every target's flags
                    self.OLD.replace('COMMAND ${check})', 'COMMAND ${check} --x)'),   # a foreach body, no name
                    self.OLD.replace('set(CHECKS a_check b_check)', 'set(CHECKS a_check b_check)\nset(FLAG 1)'),
                    self.OLD.replace('endforeach()', 'endforeach()\nif(WIN32)\nendif()')):
            got, why = self.effects(new)
            self.assertTrue(why, new)

    def test_a_call_of_the_file_s_own_function_is_placed(self) -> None:
        """A new edf6_feedback_fixture(<target> <test> <source>) line selects its names, not every test (2026-11-26);
        a change inside the function selects what every call of it names."""
        nl = chr(10)
        fn = nl.join(('function(fixture target test_name source)',
                      '  add_executable(${target} ${source})',
                      '  add_test(NAME ${test_name} COMMAND ${target})',
                      'endfunction()',
                      'fixture(a_check py_test tests/a.cpp)', ''))
        self.OLD = CMakeEffects.OLD + fn   # this test's own
        self.assertEqual(self.effects(self.OLD + 'fixture(c_check b_check tests/c.cpp)' + nl),
                         ({'c_check', 'b_check'}, None))
        self.assertEqual(self.effects(self.OLD.replace('COMMAND ${target})', 'COMMAND ${target} --x)')),
                         ({'a_check', 'py_test'}, None))
        got, why = self.effects(self.OLD + 'fixture(nothing_known x y)' + nl)
        self.assertTrue(why)

    def test_commands_span_lines_and_quotes(self) -> None:
        cmds = td.cmake_commands('set(A\n  x "y z" # c\n  w)\nadd_test(NAME n COMMAND "a(b)")\n')
        self.assertEqual(cmds, [(1, 3, 'set', ['A', 'x', 'y z', 'w']), (4, 4, 'add_test', ['NAME', 'n', 'COMMAND', 'a(b)'])])


class Dependencies(unittest.TestCase):
    """What Repo makes of a Python file: imports followed, a file read as text only itself, a module named by a string
    only by code that can import or run it."""

    def deps_of(self, text: str) -> set[str]:
        repo = td.Repo()
        with tempfile.TemporaryDirectory() as d:
            return repo.closure(repo.py_direct(write(d, 'probe.py', text)))

    def test_a_file_read_as_text_is_not_followed(self) -> None:
        deps = self.deps_of("import os\ntext = open(os.path.join('pylib', 'vcobjects.py')).read()\n")
        self.assertIn('~pylib/vcobjects.py', deps)
        self.assertNotIn('pylib/xm_walker.py', deps)

    def test_a_file_run_is_followed(self) -> None:
        deps = self.deps_of("import subprocess, sys\nsubprocess.run([sys.executable, 'pylib/vcobjects.py'])\n")
        self.assertIn('pylib/vcobjects.py', deps)
        self.assertIn('pylib/xm_walker.py', deps)

    def test_an_import_is_followed(self) -> None:
        deps = self.deps_of('import vcobjects\n')
        self.assertIn('pylib/xm_walker.py', deps)

    def test_a_module_name_in_a_table_is_a_word(self) -> None:
        self.assertNotIn('tools/menu_preview.py', self.deps_of("OWNERS = ('jets', 'menu_preview')\n"))
        deps = self.deps_of("import importlib\nfor m in ('menu_preview',):\n    importlib.import_module(m)\n")
        self.assertIn('tools/menu_preview.py', deps)

    def test_an_import_of_no_module_is_kept(self) -> None:
        deps = self.deps_of('import os\nimport json\nimport gone_mod_xyz\nfrom gone_pkg.sub import y\n')
        self.assertIn('?gone_mod_xyz', deps)
        self.assertIn('?gone_pkg', deps)
        self.assertNotIn('?os', deps)
        self.assertNotIn('?json', deps)

    def test_a_listed_module_folder_is_read_whole(self) -> None:
        repo = td.Repo()
        with tempfile.TemporaryDirectory() as d:
            deps = repo.py_direct(write(d, 'scan.py', "import os\nnames = os.listdir(os.path.join(ROOT, 'pylib'))\n"))
        self.assertIn('pylib/*', deps)
        self.assertIn('pylib-model', repo.domains_of({'pylib/*'}))

    def test_a_generator_depends_on_the_cpp_it_writes(self) -> None:
        deps = td.generator_deps(td.Repo(), 'tools/gen_calls.py')
        self.assertIn('src/calls.inc', deps)
        self.assertIn('tools/calls.py', deps)


class Includes(unittest.TestCase):
    """static_includes: a source's #include closure, the headers of an object ninja has no records for."""

    def test_the_closure_takes_every_branch_and_folder(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            for sub in ('src', 'common/edf', 'tests'):
                os.makedirs(os.path.join(d, sub))
            write(d, 'src/a.h', '#pragma once\n#include "b.h"\n')
            write(d, 'src/b.h', '#if 0\n#include "c.inc"\n#endif\n')
            write(d, 'src/c.inc', '')
            write(d, 'common/edf/m.h', '#include "near.h"\n#include "from_src.h"\n')   # the last: the includer's folder
            write(d, 'common/edf/near.h', '')
            write(d, 'src/from_src.h', '')
            write(d, 'tests/t.cpp', '#include "a.h"\n#include <edf/m.h>\n#include <vector>\n#include "../src/c.inc"\n')
            got = td.static_includes(os.path.join(d, 'tests', 't.cpp'), [os.path.join(d, 'src'), os.path.join(d, 'common')])
            self.assertIsNotNone(got)
            names = {os.path.relpath(p, os.path.normcase(d)).replace(os.sep, '/') for p in got}
            self.assertEqual(names, {'tests/t.cpp', 'src/a.h', 'src/b.h', 'src/c.inc', 'common/edf/m.h',
                                     'common/edf/near.h', 'src/from_src.h'})

    def test_an_include_of_a_macro_is_not_known(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            write(d, 'h.h', '#define H "x.h"\n')
            write(d, 't.cpp', '#include "h.h"\n#include H\n')
            self.assertIsNone(td.static_includes(os.path.join(d, 't.cpp'), []))


@unittest.skipUnless(os.path.isfile(os.path.join(BUILD, 'CTestTestfile.cmake')), 'no configured build')
class OnTheBuild(unittest.TestCase):
    """select_files on this repository's own tests and build."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = td.Repo()
        cls.deps = td.test_deps(BUILD, cls.repo)
        cls.cpp = td.cpp_files(BUILD)
        cls.labels = td.load()

    def test_every_test_has_a_known_domain_or_script(self) -> None:
        known = set(td.NAMES)
        for part in ('ctest', 'selftest'):
            self.assertTrue(self.labels[part], part)
            for name, e in self.labels[part].items():
                self.assertTrue(e['labels'] or e['scripts'], f'{part} {name} depends on nothing')
                self.assertLessEqual(set(e['labels']), known, name)
                self.assertNotIn(td.SCRIPTS, e['labels'], name)

    def test_the_build_s_records_cover_its_tests(self) -> None:
        files, links = self.cpp
        if not links:
            self.skipTest('not a ninja build')
        self.assertIn('src/heli.cpp', files['edf6vehiclecrew'])
        self.assertIn('common/edf/memory.h', files['edf6vehiclecrew'])
        exes = [os.path.splitext(os.path.basename(t['command'][0]))[0].lower() for t in td.ctest_tests(BUILD)
                if t['command'] and os.path.isfile(t['command'][0])]
        built = [e for e in exes if e in self.repo.target_names]
        known = sum(1 for e in built if e in files)
        self.assertGreater(known, len(built) * 0.9, 'most built C++ tests have their sources known')

    def test_the_include_closure_holds_what_ninja_recorded(self) -> None:
        """The #include closure (an object ninja has no records for) takes every file the compiler took, object by
        object of the targets the build has records of: never fewer tests than the records would select."""
        files, links = self.cpp
        if not links:
            self.skipTest('not a ninja build')
        closure, _ = td.cpp_files(BUILD, static=True)
        self.assertIn('src/heli.cpp', closure['edf6vehiclecrew'])
        for t, fs in files.items():
            self.assertIn(t, closure)
            self.assertLessEqual(fs, closure[t], t)

    def test_a_cpp_test_is_labelled_by_what_it_compiles(self) -> None:
        if not self.repo.compiles:
            self.skipTest('not a ninja build')
        self.assertIn('plugin-air', self.repo.domains_of({'#edf6vehiclecrew'}))
        self.assertNotIn('plugin-air', self.repo.domains_of({'#edf6autoturret'}))

    def test_plugin_source_selects_the_cpp_tests_compiling_it_not_the_model_builds(self) -> None:
        r = td.select_files(['src/heli.cpp'], self.deps, self.cpp)
        self.assertIsNone(r['full'])
        for heavy in ('cockpit_build_0', 'wt_tracked_0'):
            self.assertNotIn(heavy, r['ctest'])
        self.assertNotIn('spawn_catalog_archives_recorded_0', r['selftest'])
        self.assertLess(len(r['ctest']), len(self.deps['ctest']) // 2)

    def test_a_test_script_selects_its_tests(self) -> None:
        r = td.select_files(['tests/cockpit_build_test.py'], self.deps, self.cpp)
        self.assertIn('cockpit_build_0', r['ctest'])
        for name in r['selftest']:   # only the selftests listing tests/ itself
            self.assertTrue({'tests/', 'tests/*'} & self.deps['selftest'][name][0], name)

    def test_tests_reading_the_whole_repo_follow_every_source(self) -> None:
        # gamedir_literal_guard reads every tracked .py / .cmd (git ls-files); cab_floor_katyusha_only lists tools/,
        # pylib/ and autoturret/tools/: any of those sources changing must select them
        for f in ('tools/installer.py', 'testrange/gen.py', 'tools/edfre.py', 'worldmap/missions.json',
                  'tools/make_edf5_weapons.py'):
            self.assertIn('gamedir_literal_guard', td.select_files([f], self.deps, self.cpp)['ctest'], f)
        for f in ('tools/make_edf5_weapons.py', 'pylib/mdb.py', 'autoturret/tools/build.py'):
            self.assertIn('cab_floor_katyusha_only', td.select_files([f], self.deps, self.cpp)['selftest'], f)

    def test_conditional_tests_are_known(self) -> None:
        cond = td.conditional_tests()
        self.assertIn('real_driver_native_dll', cond)
        self.assertLessEqual(cond, set(self.labels['ctest']))


class Slot(unittest.TestCase):
    def test_the_build_holds_a_build_slot_and_the_tests_are_chosen_after_it(self) -> None:
        """2026-11-26 「特别是并发」: the build in one of the machine's build slots, not a slot the tests wait on too; the
        tests chosen once the build is done (the code and the build they run), and run in the shared core slots."""
        with open(os.path.join(ROOT, 'tools', 'check.py'), encoding='utf-8') as f:
            body = f.read().split('def _main(', 1)[1]
        self.assertLess(body.index('with checklock.build_slot('), body.index("results['build'] = run("))
        self.assertLess(body.index("results['build'] = run("), body.index('sel = plan(argv)'))
        self.assertLess(body.index('sel = plan(argv)'), body.index('with checklock.running('))
        self.assertNotIn('checklock.held', body)


class MachineSlots(unittest.TestCase):
    """pylib/checklock.py Slots: the machine's build and core slots, across processes."""

    HOLD = ('import sys, time\n'
            f'sys.path.insert(0, {os.path.join(ROOT, "pylib")!r})\n'
            'import checklock\n'
            's = checklock.Slots(sys.argv[1], int(sys.argv[2]))\n'
            'i = s.take()\n'
            'print(i, flush=True)\n'
            'sys.stdin.readline()\n'   # held till told
            'if sys.argv[3] == "give":\n'
            '    s.give(i)\n'
            '    print("given", flush=True)\n'
            '    sys.stdin.readline()\n')

    def holder(self, name: str, n: int, end: str = 'give') -> subprocess.Popen:
        p = subprocess.Popen([sys.executable, '-c', self.HOLD, name, str(n), end], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True)
        self.addCleanup(lambda: (p.poll() is None and p.kill(), p.wait(), p.stdin.close(), p.stdout.close()))
        return p

    def test_n_slots_hold_n_and_a_dead_holders_slot_is_free_again(self) -> None:
        name = f'test-{os.getpid()}'
        a, b = self.holder(name, 2), self.holder(name, 2, 'die')
        self.assertEqual({a.stdout.readline().strip(), b.stdout.readline().strip()}, {'0', '1'})
        s = checklock.Slots(name, 2)
        self.assertIsNone(s.take(0.2), 'a third took one of two held slots')
        self.assertEqual(s.free(), 0)
        b.kill()   # killed holding it: the system lets go of it
        b.wait()
        got = s.take(10)
        self.assertIsNotNone(got)
        a.stdin.write('\n')
        a.stdin.flush()
        self.assertEqual(a.stdout.readline().strip(), 'given')
        import threading
        seen: list[int] = []
        t = threading.Thread(target=lambda: seen.append(checklock.Slots(name, 2).free()))   # another thread: ours is held
        t.start()
        t.join()
        self.assertEqual(seen, [1])   # a's given back
        s.give(got)
        self.assertEqual(s.free(), 2)

    def test_a_waiter_wakes_when_a_slot_is_let_go(self) -> None:
        import time
        name = f'test-wake-{os.getpid()}'
        a = self.holder(name, 1)
        self.assertEqual(a.stdout.readline().strip(), '0')
        s = checklock.Slots(name, 1)
        import threading
        took: list[float] = []

        def wait() -> None:
            i = s.take(30)
            took.append(time.monotonic())
            s.give(i)
        t = threading.Thread(target=wait)
        t.start()
        time.sleep(0.3)
        self.assertEqual(took, [])
        let_go = time.monotonic()
        a.stdin.write('\n')
        a.stdin.flush()
        t.join(30)
        self.assertEqual(len(took), 1)
        self.assertLess(took[0] - let_go, 1.5, 'the waiter did not wake when the slot was let go')

    def test_started_by_a_run_waits_for_no_slot(self) -> None:
        saved = os.environ.get(checklock.HELD)
        try:
            os.environ[checklock.HELD] = str(os.getpid())   # this process's own run: it waits like anyone
            self.assertFalse(checklock._inside())
            os.environ[checklock.HELD] = str(os.getpid() + 1)   # a run that started this process
            self.assertTrue(checklock._inside())
            with checklock.build_slot('test') as waited:
                self.assertEqual(waited, 0.0)
        finally:
            if saved is None:
                os.environ.pop(checklock.HELD, None)
            else:
                os.environ[checklock.HELD] = saved


class PoolLog(unittest.TestCase):
    def test_the_pools_lines_read_as_ctests(self) -> None:
        """pylib/testpool.py writes check-ctest.log in ctest's words: check.py and heartbeat read it as before."""
        import check
        import heartbeat
        import testpool
        j = testpool.Job('cockpit_build_0', 'ctest', 1, lambda: ('', ''), index=12)
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, 'check-ctest.log')
            with open(log, 'w', encoding='utf-8') as f:
                f.write(testpool.ctest_line(1, 3, j, testpool.Result(testpool.PASSED, 1.5, '')))
                f.write(testpool.ctest_line(2, 3, testpool.Job('b', 'ctest', 1, lambda: ('', ''), index=2),
                                            testpool.Result(testpool.SKIPPED, 0.1, '')))
            self.assertEqual(check.ctest_outcomes(log), {'cockpit_build_0': 'Passed', 'b': 'Skipped'})
            self.assertEqual(heartbeat.parts_progress({}, {'ctest': log}), 'ctest 2/3')


class WorktreeOnce(unittest.TestCase):
    def test_second_check_of_a_worktree_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            with checklock.worktree_once(root):
                with self.assertRaises(checklock.Busy) as e:
                    with checklock.worktree_once(root):
                        pass
                self.assertEqual(e.exception.args[0].get('pid'), os.getpid())
                with checklock.worktree_once(os.path.join(root, 'other')):   # another worktree: its own slot
                    pass
            with checklock.worktree_once(root):   # released
                pass


class Tracked(unittest.TestCase):
    def test_untracked_files_are_not_the_repositorys(self) -> None:
        with tempfile.TemporaryDirectory() as repo:   # a repository of its own: nothing written into the real one
            def git(*args: str) -> None:
                subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', *args], cwd=repo, check=True,
                               capture_output=True)
            git('init', '-q')
            os.makedirs(os.path.join(repo, 'tests'))
            for name, text in (('tests/kept.py', 'import os\n'), ('tests/new.py', 'import os\n'),
                               ('.gitignore', 'ignored.py\n'), ('tests/ignored.py', '')):
                with open(os.path.join(repo, name), 'w', encoding='utf-8') as f:
                    f.write(text)
            git('add', 'tests/kept.py', '.gitignore')
            git('commit', '-q', '-m', 'kept')
            self.assertEqual(td.tracked(repo), ['.gitignore', 'tests/kept.py'])   # not new.py: untracked, not ignored
            git('add', 'tests/new.py')
            self.assertEqual(td.tracked(repo), ['.gitignore', 'tests/kept.py', 'tests/new.py'])   # added: it is


class Busy(unittest.TestCase):
    def test_a_lock_held_with_no_live_record_reads_as_starting(self) -> None:
        import check
        dead = subprocess.Popen([sys.executable, '-c', 'pass'])
        dead.wait()
        with tempfile.TemporaryDirectory() as root:
            path = checklock.worktree_lock(root)
            os.makedirs(checklock.LOCK_DIR, exist_ok=True)
            fd = os.open(path, os.O_RDWR | os.O_CREAT)
            try:
                self.assertTrue(checklock._try_lock(fd))   # taken; its holder has not written itself down yet
                for record in (None, {'root': root, 'pid': dead.pid, 'since': 0}):   # none, or a dead one's
                    if record is not None:
                        with open(path + '.json', 'w', encoding='utf-8') as h:
                            json.dump(record, h)
                    with self.assertRaises(checklock.Busy) as e:
                        with checklock.worktree_once(root):
                            pass
                    self.assertEqual(e.exception.args[0], {'starting': True}, record)
                    text = check.busy_text(e.exception.args[0])
                    self.assertIn('还没写下 pid', text)
                    self.assertNotIn('pid ?', text)
            finally:
                os.close(fd)
                with contextlib.suppress(FileNotFoundError):
                    os.remove(path + '.json')

    def test_the_record_names_the_holder_only_while_it_holds(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            lock = checklock.worktree_lock(root)
            with checklock.worktree_once(root):
                with open(lock + '.json', encoding='utf-8') as h:
                    self.assertEqual(json.load(h)['pid'], os.getpid())
                self.assertEqual(checklock.worktree_holder(lock)['pid'], os.getpid())
            self.assertEqual(checklock.worktree_holder(lock), {'starting': True})
            self.assertEqual(self.leftovers(lock), [])

    @staticmethod
    def leftovers(lock: str) -> list[str]:
        name = os.path.basename(lock)
        return [f for f in os.listdir(checklock.LOCK_DIR) if f.startswith(name) and f not in (name, name + '.json')]

    def test_a_reader_holding_the_record_open_breaks_nothing(self) -> None:
        """A second check.py of the worktree has the record open (reading who holds the lock) just as the holder
        writes it at its start, and again just as it lets go at its end."""
        dead = subprocess.Popen([sys.executable, '-c', 'pass'])
        dead.wait()
        with tempfile.TemporaryDirectory() as root:
            lock = checklock.worktree_lock(root)
            os.makedirs(checklock.LOCK_DIR, exist_ok=True)
            with open(lock + '.json', 'w', encoding='utf-8') as h:   # a killed holder's record
                json.dump({'root': root, 'pid': dead.pid, 'since': 0}, h)
            first = open(lock + '.json', encoding='utf-8')
            second = None
            try:
                with self.assertRaises(SystemExit) as e:
                    with checklock.worktree_once(root):   # the start: the record written while `first` reads it
                        self.assertEqual(checklock.worktree_holder(lock)['pid'], os.getpid())
                        second = open(lock + '.json', encoding='utf-8')
                        raise SystemExit(5)   # the end, while `second` reads it: the check's own exit code stays
                self.assertEqual(e.exception.code, 5)
            finally:
                first.close()
                if second:
                    second.close()
            self.assertEqual(checklock.worktree_holder(lock), {'starting': True})
            self.assertEqual(self.leftovers(lock), [])

    def test_readers_running_alongside(self) -> None:
        """The same, raced: a thread reading the record over and over while a holder comes and goes."""
        import threading
        with tempfile.TemporaryDirectory() as root:
            lock = checklock.worktree_lock(root)
            stop = threading.Event()
            seen: list[dict] = []

            def read() -> None:
                while not stop.is_set():
                    seen.append(checklock.worktree_holder(lock))
            reader = threading.Thread(target=read)
            reader.start()
            try:
                for _ in range(200):
                    with checklock.worktree_once(root):
                        pass
            finally:
                stop.set()
                reader.join()
            self.assertTrue(seen)
            self.assertLessEqual({tuple(sorted(w)) for w in seen}, {('starting',), ('pid', 'root', 'since')})
            self.assertEqual(self.leftovers(lock), [])


@unittest.skipUnless(os.name == 'nt', 'a job object is Windows')
class ChildrenDieWithMe(unittest.TestCase):
    def test_a_killed_parent_takes_its_children(self) -> None:
        import ctypes
        from ctypes import wintypes
        k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.OpenProcess.restype = wintypes.HANDLE
        k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        k.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        k.CloseHandle.argtypes = [wintypes.HANDLE]
        code = ('import subprocess, sys, time\n'
                f'sys.path.insert(0, {os.path.join(ROOT, "pylib")!r})\n'
                'import checklock\n'
                'checklock.children_die_with_me()\n'
                "c = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'])\n"
                'print(c.pid, flush=True)\n'
                'time.sleep(120)\n')
        parent = subprocess.Popen([sys.executable, '-c', code], stdout=subprocess.PIPE, text=True)
        child = None
        try:
            pid = int(parent.stdout.readline())
            child = k.OpenProcess(0x00100001, False, pid)   # SYNCHRONIZE | PROCESS_TERMINATE
            self.assertTrue(child, ctypes.get_last_error())
            self.assertEqual(k.WaitForSingleObject(child, 0), 0x102, 'the child ended on its own')   # WAIT_TIMEOUT
            parent.kill()   # TerminateProcess: none of the parent's finally / atexit runs
            parent.wait()
            self.assertEqual(k.WaitForSingleObject(child, 20000), 0, 'the child outlived its killed parent')
        finally:
            if child:
                k.TerminateProcess(child, 1)
                k.CloseHandle(child)
            if parent.poll() is None:
                parent.kill()
                parent.wait()
            parent.stdout.close()

    def test_its_builds_have_a_pdb_server_of_their_own(self) -> None:
        """A compile in the job starts mspdbsrv in the job (tmp probe, 2026-11-24: IsProcessInJob true, ended with its
        killed parent): it must not be the machine's shared one, other sessions' builds use that."""
        code = ('import os, subprocess, sys\n'
                f'sys.path.insert(0, {os.path.join(ROOT, "pylib")!r})\n'
                'import checklock\n'
                "os.environ['_MSPDBSRV_ENDPOINT_'] = ''\n"
                'checklock.children_die_with_me()\n'
                'print(os.getpid())\n'
                "sys.stdout.flush()\n"
                "subprocess.run([sys.executable, '-c', \"import os; print(os.environ['_MSPDBSRV_ENDPOINT_'])\"])\n")
        r = subprocess.run([sys.executable, '-c', code], stdout=subprocess.PIPE, text=True, check=True)
        pid, endpoint = r.stdout.split()
        self.assertEqual(endpoint, f'edf6-check-{pid}')


class BlindScans(unittest.TestCase):
    SOURCE = '''import os
import eo
import glob
import re
import subprocess
from os import walk
from glob import glob as g
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = os.path.join(ROOT, 'edf41port')
def whole_root():
    return os.walk(ROOT)
def a_module_constant():
    return list(eo.FILES.iterdir())
def through_a_name():
    top = ROOT
    for d, _, _ in os.walk(top):
        pass
def git_files():
    return ['git', 'ls-files']
def named_folder():
    return os.listdir(os.path.join(ROOT, 'pylib')), os.listdir(FILES)
def a_temporary():
    import tempfile
    with tempfile.TemporaryDirectory() as t:
        return os.listdir(t)
def a_parameter(d):
    return os.listdir(d)
def a_module_function(root):
    return os.listdir(eo.mission_dir(root))
def a_nested_scope():
    def inner():
        here = ROOT
        return os.listdir(here)
    return inner
def only_patterns():
    return glob.glob(os.path.join(ROOT, '**', '*.cpp'), recursive=True), g(ROOT + '/**/*.cpp')
def imported_walk():
    return walk(ROOT)
def imported_glob():
    return g(os.path.join(os.path.dirname(__file__), '*'))
def shell_listing():
    return os.popen('dir /s /b ' + ROOT).read()
def git_in_one_string():
    return os.popen('git -c core.quotepath=off ls-files').read()
def git_grep_list():
    return subprocess.run(['git', 'grep', '-l', 'ROOT'], capture_output=True)
def a_pattern_mentioning_git(text):
    return re.search(r'foreach .* in git [^)]*ls-files', text), os.popen('echo ' + ROOT)
def imported_glob_naming_a_folder():
    return g(os.path.join(ROOT, 'pylib', '*.py')), walk(os.path.join(ROOT, 'tests'))
'''

    def test_listings_naming_no_folder_are_found(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            scan = os.path.join(d, 'scan.py')
            with open(scan, 'w', encoding='utf-8') as f:
                f.write(self.SOURCE)
            found = {fn for fn, _line, _text in td.blind_scans(scan, {'eo'})}
        self.assertEqual(found, {'whole_root', 'a_module_constant', 'through_a_name', 'git_files', 'a_nested_scope',
                                'only_patterns', 'imported_walk', 'imported_glob', 'shell_listing',
                                'git_in_one_string', 'git_grep_list'})

    def test_every_blind_listing_is_declared_or_runs_everything(self) -> None:
        repo, labels = td.Repo(), td.load()
        self.assertEqual(td.blind_scan_problems(repo, labels), [])
        for key in list(td.SCANNED):   # each declaration is needed: without it the guard fails
            saved = td.SCANNED.pop(key)
            try:
                problems = td.blind_scan_problems(repo, labels)
            finally:
                td.SCANNED[key] = saved
            self.assertTrue(any(repr(key) in p for p in problems), key)
        td.SCANNED['tests/nothing.py::gone'] = ()
        try:
            self.assertIn("SCANNED['tests/nothing.py::gone']: no such listing any more",
                          td.blind_scan_problems(repo, labels))
        finally:
            del td.SCANNED['tests/nothing.py::gone']

    def test_a_declared_listing_is_a_dependency(self) -> None:
        repo = td.Repo()
        self.assertIn('edf41port/outfits/MENUOBJECT/EDF41_P_RANGER.SGO',
                      repo.declared('tools/selftest.py', {'edf41_outfits_shipped'}))
        self.assertIn('CMakeLists.txt', repo.declared('tools/selftest.py', {'_cmake_plugins'}))
        self.assertEqual(repo.declared('tools/selftest.py', {'something_else'}), set())
        deps, _uses = td.selftest_deps(repo)['edf41_outfits_shipped']
        self.assertIn('edf41port/outfits/MENUOBJECT/EDF41_P_RANGER.SGO', deps)



if __name__ == '__main__':
    unittest.main()
