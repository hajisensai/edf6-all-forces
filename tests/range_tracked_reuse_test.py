"""The test range takes the installer's tracked vehicles when its build cache says they are current, else builds them.

2026-11-10 (docs/feedback-2026-11-10-installer-en.md): every install rebuilt every tracked vehicle and walker here, one
after the other, after the installer had built (or reused) the same files. Stand-in builders; temporary Mods only.
"""
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'testrange')]
import buildcache
import gen
import ledger

NAME = 'edf6tr_vulcan_mission'
GROUP = 'vulcan'


class Builder:
    def __init__(self, files: dict[str, bytes]) -> None:
        self.files, self.calls = files, 0

    def build(self, root: str) -> dict[str, bytes]:
        self.calls += 1
        return dict(self.files)


class RangeTrackedReuse(unittest.TestCase):
    def run_range(self, root: str, current: bool, builder: Builder) -> None:
        cache = types.SimpleNamespace(current=lambda group, settings=None: current and group == GROUP)
        with patch.object(gen, 'ground_tool', lambda name: builder), \
                patch.object(gen, 'vehicle_sgo', lambda game, name, model=None: b'range sgo'), \
                patch.object(buildcache, 'Cache', lambda game: cache):
            gen._write_derived(root, types.SimpleNamespace(root=root), {NAME})

    def test_current_installer_files_are_needed_not_built(self) -> None:
        vehicle = gen.TRACKED_MISSION[NAME]
        expected = gen.range_files(NAME)[1:]
        made = {f'OBJECT/{vehicle.sgo}.SGO': b'call-in', **{rel: rel.encode() for rel in expected}}
        with tempfile.TemporaryDirectory() as root:
            led = ledger.Ledger(root)
            for rel, data in made.items():   # the installer's group, as make_tracked_combat.install leaves it
                led.put(GROUP, rel, data)
            builder = Builder({rel: b'rebuilt' for rel in made})
            self.run_range(root, True, builder)
            self.assertEqual(builder.calls, 0)
            led = ledger.Ledger(root)
            for rel in expected:
                self.assertEqual(set(led.owners(rel)), {GROUP, gen.OWNER}, rel)
                self.assertEqual(Path(led.disk(rel)).read_bytes(), rel.encode(), 'the installer\'s bytes, untouched')
            self.assertNotIn(gen.OWNER, led.owners(f'OBJECT/{vehicle.sgo}.SGO'), 'the call-in SGO is not the range\'s')
            # the installer's cache no longer current (a changed generator): built here, as before
            self.run_range(root, False, builder)
            self.assertEqual(builder.calls, 1)
            self.assertEqual(Path(ledger.Ledger(root).disk(expected[0])).read_bytes(), b'rebuilt')

    def test_current_cache_without_the_files_builds(self) -> None:
        vehicle = gen.TRACKED_MISSION[NAME]
        expected = gen.range_files(NAME)[1:]
        made = {f'OBJECT/{vehicle.sgo}.SGO': b'call-in', **{rel: b'built' for rel in expected}}
        with tempfile.TemporaryDirectory() as root:
            builder = Builder(made)
            self.run_range(root, True, builder)   # a standalone range: nothing of the group's installed
            self.assertEqual(builder.calls, 1)
            led = ledger.Ledger(root)
            for rel in expected:
                self.assertEqual(led.owners(rel), [gen.OWNER], rel)


if __name__ == '__main__':
    unittest.main()
