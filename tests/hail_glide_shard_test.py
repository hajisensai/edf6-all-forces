"""ctest's hail_glide_shard_test: tests/hail_glide_shard.py and the unit list of tools/hail_glide_check.cpp.

  python tests/hail_glide_shard_test.py HAIL_GLIDE_CHECK_EXE

- a unit that fails says why: what it wrote to stderr is in the shard's output, with which unit failed;
- `--list` gives every wing's every part: the sweep and the two walled maps for the same wings, none cut short.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SHARD = os.path.join(HERE, 'hail_glide_shard.py')
EXE = ''


@unittest.skipUnless(os.name == 'nt', 'the stand-in check is a .cmd')
class Shard(unittest.TestCase):
    def test_a_failing_unit_shows_its_stderr(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            fake = os.path.join(d, 'fake_check.cmd')
            with open(fake, 'w', encoding='ascii') as f:
                f.write('@echo off\r\n'
                        'if "%1"=="--list" (echo good& echo bad& exit /b 0)\r\n'
                        'if "%1"=="good" (echo good ok& exit /b 0)\r\n'
                        'echo bad: the reason it died 1>&2\r\n'
                        'exit /b 3\r\n')
            r = subprocess.run([sys.executable, SHARD, fake], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn('bad: the reason it died', r.stdout)
        self.assertIn('FAIL bad: exit 3', r.stdout)
        self.assertIn('1 FAILED: bad', r.stdout)

    def test_a_failing_list_shows_why(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            fake = os.path.join(d, 'fake_check.cmd')
            with open(fake, 'w', encoding='ascii') as f:
                f.write('@echo off\r\necho no table 1>&2\r\nexit /b 2\r\n')
            r = subprocess.run([sys.executable, SHARD, fake], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn('no table', r.stdout)
        self.assertIn('FAIL hail_glide_check --list: exit 2', r.stdout)


class List(unittest.TestCase):
    def test_every_wing_has_every_part(self) -> None:
        if not EXE:
            self.skipTest('no hail_glide_check given')
        r = subprocess.run([EXE, '--list'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)
        units = r.stdout.split()
        self.assertEqual(len(units), len(set(units)), 'a unit listed twice')
        wings = {part: [u.split(':', 1)[1] for u in units if u.startswith(part + ':')]
                 for part in ('sweep', 'map1600', 'map4000')}
        self.assertTrue(wings['sweep'], units)
        self.assertEqual(wings['map1600'], wings['sweep'])
        self.assertEqual(wings['map4000'], wings['sweep'])
        self.assertEqual(units, ['logged'] + [f'sweep:{w}' for w in wings['sweep']] + ['walls']
                         + [f'map1600:{w}' for w in wings['sweep']] + [f'map4000:{w}' for w in wings['sweep']])


if __name__ == '__main__':
    if len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
        EXE = os.path.abspath(sys.argv.pop(1))
    unittest.main()
