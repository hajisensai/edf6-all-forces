"""The fixed emplacements (pylib/emplacement_model.py, tools/make_emp_*.py; src/emplacement.cpp).

The registry cases run anywhere; the asset cases need the installed EDF6 Root.cpk (read only) and the Dreadnought model
folder (pylib/dreadnought_model.py MODEL_SUBDIR).
"""
from __future__ import annotations

from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]

import numpy as np

import calls
import cockpit_fits
import emplacement_model as em
import rootcpk
import vcobjects as vc
import xm_units


class RegistryTests(unittest.TestCase):
    def test_every_emplacement_is_a_tracked_kind_with_a_call_and_its_tool(self) -> None:
        for kind in em.KINDS:
            with self.subTest(kind=kind):
                self.assertIn(kind, vc.TRACKED)
                vehicle = vc.GROUND_VEHICLES[kind]
                self.assertEqual(vehicle.stock, 'V603_FLAK')
                # two guns in turn, the GDF-005 (and an X-Morph emplacement like it: xm_units.Unit.like) both together
                like = xm_units.UNITS[kind].like if kind in xm_units.UNITS else kind
                self.assertEqual(vehicle.alternate, like != 'gdf005', 'two guns in turn, the GDF-005 both together')
                # the Patriot's PAC-3 from its tubes as well (a missile holder, vcobjects PATRIOT_SAM)
                self.assertEqual(vehicle.slots, ('left', 'right') + (('missiles',) if kind == 'patriot' else ()))
                self.assertTrue((ROOT / 'tools' / f'{vehicle.tool}.py').is_file())
                self.assertEqual(sum(c.ground == kind for c in calls.CALLS), 1)
                self.assertIn(f'EDF6VC_{kind.upper()}', cockpit_fits.FITS)

    def test_the_plugin_knows_the_marker(self) -> None:
        source = (ROOT / 'src' / 'emplacement.cpp').read_text(encoding='utf-8')
        self.assertIn(f'kMarker[]=L"{em.MARKER}"', source)
        # cleared last before the stock input reads the seat (crew.cpp InputHook)
        hook = (ROOT / 'src' / 'crew.cpp').read_text(encoding='utf-8')
        body = hook[hook.index('void __fastcall InputHook'):]
        body = body[:body.index('nextInput[I](vehicle,hasInput,a3,a4);')]
        self.assertTrue(body.rstrip().endswith('Guarded(kStepEmplacement,&EmplacementInput,static_cast<unsigned char*>(vehicle));'))
        self.assertIsNotNone(re.search(r'Put<float>\(driver,kSeatSteer,0\.0f\);\s*Put<float>\(driver,kSeatSteer\+4,0\.0f\);', source))


def _ready() -> bool:
    return (Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file() and em.folder() is not None


@unittest.skipUnless(_ready(), 'EDF6 Root.cpk or the Dreadnought model folder is unavailable')
class AssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import make_tracked_combat as combat
        cls.combat = combat
        cls.game = rootcpk.default()
        cls.files = {kind: combat.authored(cls.game.root, kind) for kind in em.KINDS}

    def test_generated_sets_pass_every_generator_check(self) -> None:
        for kind, files in self.files.items():
            with self.subTest(kind=kind):
                self.combat.check(files, kind, self.game)

    def test_turret_on_the_base_guns_on_their_trunnions_and_the_marker(self) -> None:
        import mdb
        import tracked_combat_model as t
        for kind in em.KINDS:
            with self.subTest(kind=kind):
                model = em.MODELS[kind]
                rab = mdb.rab_read(self.files[kind][f'OBJECT/{model.OUT_ARC}'])
                md = mdb.mdb_read(t.member(rab, t.HOST_MDB).data)
                world = mdb.bind_world(md)
                spec = em.spec(kind)
                marker = md.bone_index(em.MARKER)
                self.assertGreaterEqual(marker, 0)
                self.assertEqual(md.name_of(md.bones[md.bones[marker].parent].name), 'body')
                ring = world[md.bone_index('cannon_main')][12:15]
                on = lambda *bones: np.vstack([spec.arrays[f'p{k}_pos'] for k, p in enumerate(spec.info['parts'])  # noqa: E731
                                               if p['bone'] in bones])
                turret, guns = on('cannon_main'), on('cannon_slide_l', em.ROTOR)
                # the source stands on the base's top (nothing under it), the turret turning about an upright through
                # its own box (a Dreadnought turret's ring: the vehicle's centre)
                self.assertAlmostEqual(float(on('cannon_main', 'cannon_slide_l', em.ROTOR)[:, 1].min()), em.BASE_TOP,
                                       delta=1e-4 if kind in self.combat.DREADNOUGHT_GUNS else 5.0)
                self.assertGreaterEqual(float(turret[:, 1].min()), em.BASE_TOP - 1e-3)
                self.assertGreaterEqual(ring[1], em.BASE_TOP - 1e-3)   # the laser turns on its foot, at the base's top
                self.assertTrue(turret[:, 0].min() < ring[0] < turret[:, 0].max() and turret[:, 2].min() < ring[2] < turret[:, 2].max())
                if kind in self.combat.DREADNOUGHT_GUNS:
                    self.assertEqual((ring[0], ring[2]), (0.0, 0.0))
                for side in ('left', 'right'):
                    point = np.asarray(spec.info['muzzles'][side][0]['point'])
                    self.assertGreaterEqual(point[2], guns[:, 2].max() - 1.0, 'the mouth at the guns\' front')
                if kind in self.combat.DREADNOUGHT_GUNS:
                    self.assertNotEqual(spec.info['muzzles']['left'][0]['point'][0],
                                        spec.info['muzzles']['right'][0]['point'][0], 'two barrels, two mouths')

    def test_the_phalanx_barrels_spin(self) -> None:
        # Its six barrels on the M163's rotor bone (src/vulcan_rotor.cpp spins every one under the slide when one of
        # its M61 holders shoots: the weapon files' prefix is one the plugin reads).
        import mdb
        import tracked_combat_model as t
        model = em.MODELS['ciws_phalanx']
        md = mdb.mdb_read(t.member(mdb.rab_read(self.files['ciws_phalanx'][f'OBJECT/{model.OUT_ARC}']), t.HOST_MDB).data)
        rotor = md.bone_index(em.ROTOR)
        self.assertGreaterEqual(rotor, 0)
        self.assertEqual(md.name_of(md.bones[md.bones[rotor].parent].name), 'cannon_slide_l')
        header = (ROOT / 'src' / 'vulcan_rotor.h').read_text(encoding='utf-8')
        prefixes = re.findall(r'L"(EDF6VC_[A-Z_]+_)"', header[header.index('kGunPrefixes'):])
        for path in vc.GROUND_VEHICLES['ciws_phalanx'].weapons:
            self.assertTrue(any(path.rsplit('/', 1)[1].upper().startswith(p) for p in prefixes), path)

    def test_first_person_eye_is_the_spec_s(self) -> None:
        for kind in em.KINDS:
            with self.subTest(kind=kind):
                (fit,) = cockpit_fits.FITS[f'EDF6VC_{kind.upper()}']
                self.assertEqual(fit.bone, 'cannon_main')
                np.testing.assert_allclose(fit.eye, em.spec(kind).info['eye'], atol=1e-3)

    def test_weapons_fire_the_cannon_shell_in_turn(self) -> None:
        import dsgo
        for kind in self.combat.DREADNOUGHT_GUNS:
            vehicle = vc.GROUND_VEHICLES[kind]
            for path in vehicle.weapons:
                with self.subTest(kind=kind, weapon=path):
                    gun = dsgo.parse(self.files[kind][f'WEAPON/{path.rsplit("/", 1)[1].upper()}']).root
                    self.assertEqual(gun.get('AmmoClass'), 'RocketBullet01')
                    self.assertGreater(gun.get('AmmoExplosion'), 3.0)
                    self.assertEqual(gun.get('FireInterval'), self.combat.GUNS[kind]['FireInterval'])
            names = {name for name, _pair in vc.twin_barrels()}
            self.assertTrue({w.rsplit('/', 1)[1].upper() for w in vehicle.weapons} <= names)

    def test_the_patriot_fires_missiles_from_its_tubes(self) -> None:
        # The native pair the Titan's side missile (its round and lock-on); the PAC-3 from the pack's four tubes.
        import dsgo
        vehicle = vc.GROUND_VEHICLES['patriot']
        for path in vehicle.weapons[:2]:
            gun = dsgo.parse(self.files['patriot'][f'WEAPON/{path.rsplit("/", 1)[1].upper()}']).root
            self.assertEqual(gun.get('AmmoClass'), 'MissileBullet01')
            self.assertEqual(gun.get('LockonType'), 1.0)
            self.assertEqual(gun.get('LockonTargetType'), 7301.0)
        self.assertEqual(vc.store_of(vehicle.weapons[2]), ('PATRIOT_SAM', 8))
        tubes = em.spec('patriot').info['muzzles']['missiles']
        self.assertEqual(len(tubes), 4)
        self.assertEqual({t['bone'] for t in tubes}, {'cannon_main'})


if __name__ == '__main__':
    unittest.main()
