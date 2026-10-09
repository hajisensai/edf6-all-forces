"""The Proteus resource transaction (tools/make_proteus.py): the shield SGO installed and removed under the write-ahead
journal, and an older install (private models + redirected vehicle SGOs) undone on update. Temporary games only."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import ledger
import modfiles
import make_proteus as mp
import sgo

HOST = 'V614_PROTEUS_MK2'
MODEL, CAS = f'OBJECT/EDF6VC_{HOST}.MRAB', f'OBJECT/EDF6VC_{HOST}.CAS'
VEHICLE = f'OBJECT/{HOST}_MISSION.SGO'
SHIELD = mp.SHIELD_FILE


def vehicle(hp=7500, model=HOST.lower()):
    return sgo.write(0x102, {'xgs_scene_object_class': 'VehicleBigBegaruta',
        'game_object_durability': hp, 'mission_setup': [1, 2],
        'animation_model': [[f'app:/object/{model}.mrab', HOST.lower()+'.mdb'], f'app:/object/{model}.cas', []]})


class Transaction(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = self.tmp.name
        self.owner = patch.object(ledger, 'OWNERS', tuple(set(ledger.OWNERS) | {mp.OWNER}))
        self.owner.start(); self.addCleanup(self.owner.stop)
        self.original = vehicle(12345)
        self.shield = b'shield sgo bytes'
        # What the old install wrote: the private model pair and the vehicle SGO redirected to it (its own original kept).
        self.legacy = {VEHICLE: vehicle(12345, 'edf6vc_' + HOST.lower()), MODEL: b'old model', CAS: b'old cas'}
        self.write(VEHICLE, self.original)

    def path(self, rel): return Path(self.root, 'Mods', *rel.split('/'))
    def write(self, rel, data): modfiles.atomic_write(str(self.path(rel)), data)

    def test_update_undoes_the_old_models_and_writes_the_shield(self):
        mp.install(self.root, self.legacy)
        self.assertNotEqual(self.path(VEHICLE).read_bytes(), self.original)
        mp.install(self.root, {SHIELD: self.shield})
        self.assertEqual(self.path(VEHICLE).read_bytes(), self.original, 'the redirected vehicle SGO gets its original back')
        self.assertFalse(self.path(MODEL).exists() or self.path(CAS).exists(), 'the private model pair is removed')
        self.assertEqual(self.path(SHIELD).read_bytes(), self.shield)
        self.assertEqual(set(ledger.Ledger(self.root).owners(SHIELD)), {mp.OWNER})
        mp.install(self.root, {SHIELD: self.shield})
        self.assertEqual(self.path(SHIELD).read_bytes(), self.shield, 'a reinstall is idempotent')
        mp.remove(self.root)
        self.assertFalse(self.path(SHIELD).exists() or self.path(mp.MANIFEST).exists())
        self.assertEqual(self.path(VEHICLE).read_bytes(), self.original)

    def test_third_party_edit_of_a_redirected_sgo_keeps_it_and_its_models(self):
        mp.install(self.root, self.legacy)
        altered = vehicle(99999, 'edf6vc_' + HOST.lower()); self.write(VEHICLE, altered)
        mp.install(self.root, {SHIELD: self.shield})
        self.assertEqual(self.path(VEHICLE).read_bytes(), altered, 'somebody else edited it: kept')
        self.assertTrue(self.path(MODEL).exists() and self.path(CAS).exists(), 'it still names the old pair: kept')
        _, kept = mp.remove(self.root)
        self.assertEqual(len(kept), 3)
        self.assertTrue(self.path(mp.MANIFEST).exists(), 'the journal keeps what it could not restore')

    def test_a_foreign_file_at_the_shield_path_is_restored(self):
        self.write(SHIELD, b'someone else')
        mp.install(self.root, {SHIELD: self.shield})
        self.assertEqual(self.path(SHIELD).read_bytes(), self.shield)
        mp.remove(self.root)
        self.assertEqual(self.path(SHIELD).read_bytes(), b'someone else')

    def test_shield_changed_after_install_is_not_overwritten(self):
        mp.install(self.root, {SHIELD: self.shield})
        self.write(SHIELD, b'edited later')
        mp.install(self.root, {SHIELD: self.shield})
        self.assertEqual(self.path(SHIELD).read_bytes(), b'edited later')

    def test_range_holds_the_installed_shield_and_builds_it_alone(self):
        mp.install(self.root, {SHIELD: self.shield})
        led = ledger.Ledger(self.root)
        self.assertEqual(mp.range_shield(led, None), SHIELD)
        led._save()
        mp.remove(self.root)
        self.assertIn('testrange', ledger.Ledger(self.root).owners(SHIELD))
        with tempfile.TemporaryDirectory() as alone:
            led = ledger.Ledger(alone)
            with patch.object(mp, 'shield', return_value=b'range shield') as build:
                mp.range_shield(led, object())
            build.assert_called_once()
            self.assertEqual(Path(alone, 'Mods', *SHIELD.split('/')).read_bytes(), b'range shield')
            self.assertIn('testrange', ledger.Ledger(alone).owners(SHIELD))

    def test_interrupted_update_recovers_the_originals(self):
        atomic = modfiles.atomic_write
        for stop in range(1, 25):
            with self.subTest(stop=stop), tempfile.TemporaryDirectory() as root:
                old = Path(root, 'Mods', VEHICLE); atomic(str(old), self.original)
                mp.install(root, self.legacy)
                calls = [0]
                def interrupted(path, data):
                    atomic(path, data); calls[0] += 1
                    if calls[0] == stop: raise OSError('interrupted after commit')
                with patch.object(modfiles, 'atomic_write', interrupted), patch.object(ledger, 'atomic_write', interrupted):
                    try: mp.install(root, {SHIELD: self.shield})
                    except OSError: pass
                mp.install(root, {SHIELD: self.shield})
                self.assertEqual(old.read_bytes(), self.original)
                self.assertFalse(Path(root, 'Mods', MODEL).exists() or Path(root, 'Mods', CAS).exists())
                mp.remove(root)
                self.assertFalse(Path(root, 'Mods', *SHIELD.split('/')).exists())
                self.assertEqual(old.read_bytes(), self.original)

    def test_interrupted_removal_is_idempotent(self):
        atomic = modfiles.atomic_write
        for stop in range(1, 13):
            with self.subTest(stop=stop), tempfile.TemporaryDirectory() as root:
                old = Path(root, 'Mods', VEHICLE); atomic(str(old), self.original)
                mp.install(root, self.legacy); calls = [0]
                def interrupted(path, data):
                    atomic(path, data); calls[0] += 1
                    if calls[0] == stop: raise OSError('interrupted restore')
                with patch.object(modfiles, 'atomic_write', interrupted), patch.object(ledger, 'atomic_write', interrupted):
                    try: mp.remove(root)
                    except OSError: pass
                mp.remove(root)
                self.assertEqual(old.read_bytes(), self.original)
                self.assertFalse(Path(root, 'Mods', MODEL).exists() or Path(root, 'Mods', CAS).exists())


if __name__ == '__main__': unittest.main()
