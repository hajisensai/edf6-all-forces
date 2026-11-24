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


class Select(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.labels = td.load()

    def test_every_test_has_a_known_domain_or_script(self) -> None:
        known = set(td.NAMES)
        for part in ('ctest', 'selftest'):
            self.assertTrue(self.labels[part], part)
            for name, e in self.labels[part].items():
                self.assertTrue(e['labels'] or e['scripts'], f'{part} {name} depends on nothing')
                self.assertLessEqual(set(e['labels']), known, name)
                self.assertNotIn(td.SCRIPTS, e['labels'], name)
        for d in ('plugin-native', 'pylib-model', 'installer', 'testrange', 'docs-guard'):
            self.assertIn(d, known)

    def test_plugin_source_selects_the_cpp_tests_not_the_model_builds(self) -> None:
        r = td.select(['src/heli.cpp'], self.labels)
        self.assertIsNone(r['full'])
        self.assertEqual(list(r['domains']), ['plugin-native'])
        self.assertIn('bullet_pass_test', r['ctest'])
        for heavy in ('cockpit_build_0', 'wt_tracked_0'):
            self.assertNotIn(heavy, r['ctest'])
        self.assertNotIn('spawn_catalog_archives_recorded_0', r['selftest'])
        self.assertIn('spawn_catalog_archives_counted', r['selftest'])   # the shards' C++ side, split off for this

    def test_shared_files_run_everything(self) -> None:
        for f in ('CMakeLists.txt', 'common/edf/memory.h', 'tools/check.py', 'tools/test_domains.py', td.LABELS_JSON,
                  '.github/workflows/build.yml', 'pylib/checklock.py', 'build.cmd'):
            r = td.select([f, 'src/heli.cpp'], self.labels)
            self.assertTrue(r['full'], f)
            self.assertEqual(len(r['ctest']), len(self.labels['ctest']), f)
            self.assertEqual(len(r['selftest']), len(self.labels['selftest']), f)

    def test_a_test_script_selects_its_tests(self) -> None:
        r = td.select(['tests/cockpit_build_test.py'], self.labels)
        self.assertIn('cockpit_build_0', r['ctest'])
        self.assertNotIn('bullet_pass_test', r['ctest'])
        self.assertEqual(r['selftest'], [])

    def test_docs_only(self) -> None:
        r = td.select(['docs/agent-rules-detail.md'], self.labels)
        self.assertEqual(list(r['domains']), ['docs-guard'])
        self.assertLess(len(r['ctest']) + len(r['selftest']), 100)

    def test_selftest_change_selects_the_tests_using_it(self) -> None:
        r = td.select([td.SELFTEST], self.labels, {'boarding_tag_in_plugin'})
        self.assertEqual(r['selftest'], ['boarding_tag_in_plugin'])
        everything = [n for n, e in self.labels['selftest'].items() if td.SELFTEST in e['scripts']]
        self.assertEqual(td.select([td.SELFTEST], self.labels, None)['selftest'], sorted(everything))
        # a name no test reaches (its @test decorator): every selftest test
        self.assertEqual(td.select([td.SELFTEST], self.labels, {'test'})['selftest'], sorted(everything))

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

    def test_conditional_tests_are_known(self) -> None:
        cond = td.conditional_tests()
        self.assertIn('real_driver_native_dll', cond)
        self.assertLessEqual(cond, set(self.labels['ctest']))


    def test_tests_reading_the_whole_repo_follow_every_source(self) -> None:
        # gamedir_literal_guard reads every tracked .py / .cmd (git ls-files); cab_floor_katyusha_only lists tools/,
        # pylib/ and autoturret/tools/: any of those sources changing must select them
        for f in ('tools/installer.py', 'testrange/gen.py', 'tools/edfre.py', 'worldmap/missions.json',
                  'tools/make_edf5_weapons.py'):
            self.assertIn('gamedir_literal_guard', td.select([f], self.labels)['ctest'], f)
        for f in ('tools/make_edf5_weapons.py', 'pylib/mdb.py', 'autoturret/tools/build.py'):
            self.assertIn('cab_floor_katyusha_only', td.select([f], self.labels)['selftest'], f)
        self.assertEqual(set(self.labels['ctest']['gamedir_literal_guard']['labels']), set(td.NAMES) - {td.SCRIPTS})

    def test_a_listed_module_folder_is_read_whole(self) -> None:
        repo = td.Repo()
        with tempfile.TemporaryDirectory() as d:
            scan = os.path.join(d, 'scan.py')
            with open(scan, 'w', encoding='utf-8') as f:
                f.write("import os\nnames = os.listdir(os.path.join(ROOT, 'pylib'))\n")
            deps = repo.py_direct(scan)
        self.assertIn('pylib/*', deps)
        self.assertIn('pylib-model', repo.domains_of({'pylib/*'}))


class Slot(unittest.TestCase):
    def test_tests_are_chosen_after_the_check_slot_is_held(self) -> None:
        import contextlib
        import check
        order: list[str] = []

        @contextlib.contextmanager
        def held(_who: str):
            order.append('held')
            yield

        saved = check.checklock.held, check.plan
        check.checklock.held = held
        check.plan = lambda argv: order.append('plan') or {'argv': argv}
        try:
            with tempfile.TemporaryDirectory() as root:
                with checklock.worktree_once(root):   # the worktree's own slot chooses nothing
                    self.assertEqual(order, [])
            with check.slot(['--x']) as (sel, waited):
                self.assertEqual(order, ['held', 'plan'])
                self.assertEqual(sel, {'argv': ['--x']})
                self.assertGreaterEqual(waited, 0)
        finally:
            check.checklock.held, check.plan = saved


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
'''

    def test_listings_naming_no_folder_are_found(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            scan = os.path.join(d, 'scan.py')
            with open(scan, 'w', encoding='utf-8') as f:
                f.write(self.SOURCE)
            found = {fn for fn, _line, _text in td.blind_scans(scan, {'eo'})}
        self.assertEqual(found, {'whole_root', 'a_module_constant', 'through_a_name', 'git_files', 'a_nested_scope'})

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
