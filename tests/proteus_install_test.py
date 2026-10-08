"""Proteus file transaction and range consumers; all writes use temporary games."""
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
from test_proteus_assets import fixture as cas_fixture

HOST = 'V614_PROTEUS_MK2'
MODEL, CAS = f'OBJECT/EDF6VC_{HOST}.MRAB', f'OBJECT/EDF6VC_{HOST}.CAS'
VEHICLE = f'OBJECT/{HOST}_MISSION.SGO'

def stock(hp=7500):
    return sgo.write(0x102, {'xgs_scene_object_class': 'VehicleBigBegaruta',
        'game_object_durability': hp, 'mission_setup': [1, 2],
        'animation_model': [[f'app:/object/{HOST.lower()}.mrab', HOST.lower()+'.mdb'],
                            f'app:/object/{HOST.lower()}.cas', []]})

class Transaction(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = self.tmp.name
        # Root integration registers the owner; keep the isolated branch testable.
        self.owner = patch.object(ledger, 'OWNERS', tuple(set(ledger.OWNERS) | {mp.OWNER}))
        self.owner.start(); self.addCleanup(self.owner.stop)
        self.original = stock(12345)
        self.files = {VEHICLE: mp.redirect(stock())[0], MODEL: b'new model', CAS: mp.proteus_model.animation(cas_fixture())}
        self.write(VEHICLE, self.original)

    def path(self, rel): return Path(self.root, 'Mods', *rel.split('/'))
    def write(self, rel, data): modfiles.atomic_write(str(self.path(rel)), data)
    def test_existing_mod_fields_reinstall_and_exact_restore(self):
        self.write(MODEL, b'previous model')
        mp.install(self.root, self.files)
        self.assertEqual(sgo.load(data=self.path(VEHICLE).read_bytes())['game_object_durability'], 12345)
        first = self.path(VEHICLE).read_bytes()
        mp.install(self.root, self.files)
        self.assertEqual(self.path(VEHICLE).read_bytes(), first)
        mp.remove(self.root)
        self.assertEqual(self.path(VEHICLE).read_bytes(), self.original)
        self.assertEqual(self.path(MODEL).read_bytes(), b'previous model')
        self.assertFalse(self.path(CAS).exists())
        self.assertFalse(self.path(mp.MANIFEST).exists())

    def test_changed_consumer_keeps_pair_and_is_not_overwritten_on_reinstall(self):
        mp.install(self.root, self.files)
        altered = mp.redirect(stock(99999))[0]; self.write(VEHICLE, altered)
        mp.install(self.root, self.files)
        self.assertEqual(self.path(VEHICLE).read_bytes(), altered)
        _, kept = mp.remove(self.root)
        self.assertEqual(self.path(VEHICLE).read_bytes(), altered)
        self.assertTrue(self.path(MODEL).exists() and self.path(CAS).exists())
        self.assertEqual(len(kept), 3)
        self.assertTrue(self.path(mp.MANIFEST).exists())

    def test_external_range_reference_survives_uninstall(self):
        mp.install(self.root, self.files)
        self.write('OBJECT/EDF6TR_CUSTOM.SGO', mp.redirect(stock())[0])
        mp.remove(self.root)
        self.assertEqual(self.path(VEHICLE).read_bytes(), self.original)
        self.assertTrue(self.path(MODEL).exists() and self.path(CAS).exists())
        self.path('OBJECT/EDF6TR_CUSTOM.SGO').unlink()
        mp.remove(self.root)
        self.assertFalse(self.path(MODEL).exists() or self.path(CAS).exists())

    def test_range_uses_same_redirect_and_holds_both_actual_dependencies(self):
        mp.install(self.root, self.files)
        led = ledger.Ledger(self.root)
        made, needs = mp.range_vehicle(led, None, stock())
        self.assertEqual(needs, (MODEL, CAS))
        led.put('testrange', 'OBJECT/EDF6TR_PROTEUS.SGO', made)
        mp.remove(self.root)
        current = ledger.Ledger(self.root)
        for rel in needs:
            self.assertIn('testrange', current.owners(rel))
            self.assertTrue(self.path(rel).exists())

    def test_standalone_range_builds_missing_pair_before_publishing_consumer(self):
        led = ledger.Ledger(self.root)
        with patch.object(mp.proteus_model, 'build_model', return_value=(b'range model', b'range CAS')) as build:
            made, needs = mp.range_vehicle(led, object(), stock())
        build.assert_called_once()
        self.assertEqual(self.path(MODEL).read_bytes(), b'range model')
        self.assertEqual(self.path(CAS).read_bytes(), b'range CAS')
        led.put('testrange', 'OBJECT/EDF6TR_PROTEUS.SGO', made)
        for rel in needs:
            self.assertIn('testrange', ledger.Ledger(self.root).owners(rel))

    def test_third_party_model_is_not_replaced(self):
        values = sgo.load(data=self.original)
        values['animation_model'][0][0] = 'app:/object/some_other_mod.mrab'
        custom = sgo.write(0x102, values); self.write(VEHICLE, custom)
        mp.install(self.root, self.files); mp.remove(self.root)
        self.assertEqual(self.path(VEHICLE).read_bytes(), custom)

    def test_interruptions_after_every_write_recover_originals(self):
        atomic = modfiles.atomic_write
        # Exercise backup, pending journal, file commit, ledger commit, and final
        # journal writes. An exception after commit simulates a killed installer.
        for stop in range(1, 19):
            with self.subTest(stop=stop), tempfile.TemporaryDirectory() as root:
                old = Path(root, 'Mods', VEHICLE); atomic(str(old), self.original)
                calls = [0]
                def interrupted(path, data):
                    atomic(path, data); calls[0] += 1
                    if calls[0] == stop: raise OSError('interrupted after commit')
                with patch.object(modfiles, 'atomic_write', interrupted), patch.object(ledger, 'atomic_write', interrupted):
                    try: mp.install(root, self.files)
                    except OSError: pass
                mp.install(root, self.files)
                mp.remove(root)
                self.assertEqual(old.read_bytes(), self.original)
                self.assertFalse(Path(root, 'Mods', MODEL).exists())
                self.assertFalse(Path(root, 'Mods', CAS).exists())

    def test_interrupted_restores_are_idempotent(self):
        atomic = modfiles.atomic_write
        for stop in range(1, 13):
            with self.subTest(stop=stop), tempfile.TemporaryDirectory() as root:
                old = Path(root, 'Mods', VEHICLE); atomic(str(old), self.original)
                mp.install(root, self.files); calls = [0]
                def interrupted(path, data):
                    atomic(path, data); calls[0] += 1
                    if calls[0] == stop: raise OSError('interrupted restore')
                with patch.object(modfiles, 'atomic_write', interrupted), patch.object(ledger, 'atomic_write', interrupted):
                    try: mp.remove(root)
                    except OSError: pass
                mp.remove(root)
                self.assertEqual(old.read_bytes(), self.original)
                self.assertFalse(Path(root, 'Mods', MODEL).exists())
                self.assertFalse(Path(root, 'Mods', CAS).exists())

if __name__ == '__main__': unittest.main()
