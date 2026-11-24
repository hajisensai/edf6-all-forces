"""tools/test_domains.py and tools/check.py's selection: what a change selects, what makes a run whole, the selftest
names a change to tools/selftest.py falls in, and one check per worktree (pylib/checklock.py worktree_once)."""
from __future__ import annotations

import os
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


if __name__ == '__main__':
    unittest.main()
