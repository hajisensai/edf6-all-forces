"""pylib/derivedcache.py: the test processes' shared store of game-derived builds, and the tracked vehicles' builds
(tools/make_tracked_combat.py _assets) through it.

The store must be invisible: off without $EDF6_DERIVED_CACHE (the installer), a hit only for a whole entry of the same
key, a torn or foreign entry built again, and a stored build the same as a fresh one.
"""
from __future__ import annotations

import os
import pickle
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]

import derivedcache  # noqa: E402


class StoreTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        env = mock.patch.dict(os.environ, {derivedcache.ENV: self.tmp.name})
        env.start()
        self.addCleanup(env.stop)
        self.builds = 0

    def build(self, value=('model', b'\x00\x01', {'a': 1.5})):
        def run():
            self.builds += 1
            return value
        return run

    def test_off_without_the_variable_builds_every_time(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(derivedcache.folder())
            for _ in range(2):
                derivedcache.fetch('ns', lambda: self.fail('a key made with the store off'), self.build())
        self.assertEqual(self.builds, 2)
        self.assertEqual(list(Path(self.tmp.name).rglob('*')), [])

    def test_second_fetch_of_a_key_reads_the_stored_build(self) -> None:
        first = derivedcache.fetch('ns', lambda: {'k': 1}, self.build())
        second = derivedcache.fetch('ns', lambda: {'k': 1}, self.build())
        self.assertEqual(self.builds, 1)
        self.assertEqual(first, second)
        self.assertIsNot(first, second)   # a stored build is a copy of its own: no reader sees another's changes

    def test_another_key_or_namespace_is_another_build(self) -> None:
        derivedcache.fetch('ns', lambda: {'k': 1}, self.build())
        derivedcache.fetch('ns', lambda: {'k': 2}, self.build())
        derivedcache.fetch('other', lambda: {'k': 1}, self.build())
        self.assertEqual(self.builds, 3)

    def test_the_environment_is_part_of_the_key(self) -> None:
        derivedcache.fetch('ns', lambda: {'k': 1}, self.build())
        with mock.patch.object(derivedcache, 'environment', return_value={'numpy': 'another'}):
            derivedcache.fetch('ns', lambda: {'k': 1}, self.build())
        self.assertEqual(self.builds, 2)

    def test_a_torn_or_foreign_entry_is_built_again_and_replaced(self) -> None:
        derivedcache.fetch('ns', lambda: {'k': 1}, self.build())
        path, key_text = derivedcache.entry_path('ns', {'k': 1})
        whole = path.read_bytes()
        payload = pickle.dumps('another value', protocol=pickle.HIGHEST_PROTOCOL)
        for broken in (whole[:-1], whole[:10], b'', whole[:len(derivedcache.MAGIC)] + b'\x00' * 32 + whole[len(derivedcache.MAGIC) + 32:],
                       derivedcache._encode(b'another key', payload),
                       whole[:-1] + bytes([whole[-1] ^ 1])):
            with self.subTest(size=len(broken)):
                path.write_bytes(broken)
                before = self.builds
                self.assertEqual(derivedcache.fetch('ns', lambda: {'k': 1}, self.build()), self.build()())
                self.assertEqual(self.builds, before + 2)   # built again (and the comparison's own call)
                self.assertEqual(derivedcache._decode(path.read_bytes(), key_text),
                                 pickle.dumps(self.build()(), protocol=pickle.HIGHEST_PROTOCOL))

    def test_the_first_stored_entry_of_a_key_is_kept(self) -> None:
        # Two processes building the same key at once: the second's rename finds the first's entry and leaves it.
        path, key_text = derivedcache.entry_path('ns', {'k': 1})
        first = derivedcache._encode(key_text, pickle.dumps('first', protocol=pickle.HIGHEST_PROTOCOL))
        derivedcache._store(path, first)
        derivedcache._store(path, derivedcache._encode(key_text, pickle.dumps('second')))
        self.assertEqual(path.read_bytes(), first)
        self.assertEqual([p.name for p in path.parent.iterdir()], [path.name])   # no temporary file left


class TrackedAssetsTests(unittest.TestCase):
    """make_tracked_combat._assets through the store: the stored build is the fresh one, and a model folder's change
    is another key."""

    KIND = 'patriot'   # the cheapest kind to build (about 2 s)

    @classmethod
    def setUpClass(cls) -> None:
        import rootcpk
        if not (Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file():
            raise unittest.SkipTest('EDF6 Root.cpk unavailable')
        import make_tracked_combat
        if make_tracked_combat.model_missing(cls.KIND):
            raise unittest.SkipTest(f'{cls.KIND}: its model folder is missing')

    def setUp(self) -> None:
        import make_tracked_combat
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        env = mock.patch.dict(os.environ, {derivedcache.ENV: self.tmp.name})
        env.start()
        self.addCleanup(env.stop)
        memo = mock.patch.dict(make_tracked_combat._ASSETS, clear=True)
        memo.start()
        self.addCleanup(memo.stop)

    @staticmethod
    def flat(assets: tuple) -> tuple:
        import mdb
        arc, md, shkt, binding, overrides, report = assets
        return arc, mdb.mdb_write(md), shkt, binding, pickle.dumps(overrides), pickle.dumps(report)

    def test_a_stored_build_is_the_fresh_build(self) -> None:
        import make_tracked_combat as combat
        import vcobjects as vc
        import rootcpk
        game = vc.Game(rootcpk.DEFAULT_GAME)
        fresh = combat._build_assets(game, self.KIND)
        stored = combat._assets(game, self.KIND)       # built and stored
        combat._ASSETS.clear()                          # another process: only the store
        with mock.patch.object(combat, '_build_assets', side_effect=AssertionError('built again')):
            read = combat._assets(game, self.KIND)
        self.assertEqual(self.flat(fresh), self.flat(stored))
        self.assertEqual(self.flat(fresh), self.flat(read))
        # What vehicle_sgo and authored make from it: the same bytes as from the fresh build.
        with mock.patch.object(combat, '_assets', return_value=fresh):
            want = combat.vehicle_sgo(game, self.KIND)
        self.assertEqual(combat.vehicle_sgo(game, self.KIND), want)

    def test_the_model_folder_and_the_switches_are_the_key(self) -> None:
        import buildcache
        import make_tracked_combat as combat
        import vcobjects as vc
        import rootcpk
        import wt_tracked
        game = vc.Game(rootcpk.DEFAULT_GAME)
        key = combat._assets_key(game, self.KIND)
        self.assertEqual(key['inputs'], buildcache.inputs(game.root, self.KIND))
        self.assertEqual(key['recipe'], buildcache.source_recipes(str(ROOT))[self.KIND])
        with mock.patch.object(wt_tracked, 'ENABLED', not wt_tracked.ENABLED):
            self.assertNotEqual(combat._assets_key(game, self.KIND), key)
        with mock.patch.object(buildcache, 'inputs', return_value={**key['inputs'], 'model': None}):
            self.assertNotEqual(combat._assets_key(game, self.KIND), key)


if __name__ == '__main__':
    unittest.main()
