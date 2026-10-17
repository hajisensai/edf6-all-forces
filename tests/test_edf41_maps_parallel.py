"""tools/edf41_maps.py install with the maps converted in processes of their own (convert_job, model_workers.ordered_jobs)
writes what converting them here one by one writes: every file, the ledger and the stamp byte for byte, the same
messages. Needs EDF6 and an EDF4.1 source (EDF4.1 or an asset pack, pylib/edf41src.py) and file symbolic links (the
stand-in EDF6 links to the real archives; nothing is written to the game): skipped otherwise, as on CI."""
from pathlib import Path
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import edf41_maps
import edf41src
import gamedir

NAMES = ('IG_HORNETCAVE01', 'IG_CAVE01', 'SK_HEIGEN02')   # two small ones and an SK map (terrain, Havok)


def stand_in(game: str, at: str) -> bool:
    """`at` as an EDF6 root whose archives link to `game`'s; False when links cannot be made here."""
    os.makedirs(os.path.join(at, 'Mods'))
    for name in edf41_maps.ARCHIVES:
        if os.path.isfile(os.path.join(game, name)):
            try:
                os.symlink(os.path.join(game, name), os.path.join(at, name))
            except OSError:
                return False
    return True


def tree(root: str) -> dict[str, bytes]:
    mods = os.path.join(root, 'Mods')
    out = {}
    for folder, _, files in os.walk(mods):
        for f in files:
            with open(os.path.join(folder, f), 'rb') as h:
                out[os.path.relpath(os.path.join(folder, f), mods)] = h.read()
    return out


class ParallelMapsTests(unittest.TestCase):
    def test_parallel_install_writes_what_one_by_one_writes(self) -> None:
        game, root41 = gamedir.find(), edf41src.find()
        if not game or not os.path.isfile(os.path.join(game, 'Root.cpk')) or not root41:
            self.skipTest('EDF6 and EDF4.1 (or its asset pack) needed')
        runs = {}
        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(edf41_maps, 'MAPS', {n: edf41_maps.MAPS[n] for n in NAMES}):
            for workers in (1, 2):
                at = os.path.join(tmp, str(workers))
                if not stand_in(game, at):
                    self.skipTest('no symbolic links here')
                said: list[str] = []
                with patch.object(edf41_maps, 'workers', return_value=workers):
                    written, skipped = edf41_maps.install(at, root41, said.append)
                runs[workers] = ([os.path.relpath(p, at) for p in written], skipped, said, tree(at))
        one, two = runs[1], runs[2]
        self.assertFalse(one[1], one[1])
        self.assertEqual(one[:3], two[:3])
        self.assertEqual(sorted(one[3]), sorted(two[3]))
        for rel in one[3]:
            self.assertEqual(one[3][rel], two[3][rel], rel)
        self.assertIn(os.path.join('MAP', 'SK_HEIGEN02.MAD'), one[3])


if __name__ == '__main__':
    unittest.main()
