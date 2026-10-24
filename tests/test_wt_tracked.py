"""The M163 / BMPT built from their War Thunder model folders (pylib/wt_tracked.py, tools/prep_wt_tracked.py).

The asset cases need the installed EDF6 Root.cpk (read only) and the tracked model folders; the prep tool's rules
and hull maths run anywhere.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]

import numpy as np

import prep_wt_tracked as prep
import rootcpk
import wt_tracked as wt


class PrepRuleTests(unittest.TestCase):
    def test_node_names_and_bone_rules(self) -> None:
        self.assertEqual(prep.node('vehicle#wheel_l_01_72'), 'wheel_l_01')
        self.assertEqual(prep.node('9m120_rocket_135'), '9m120_rocket')
        m, b = prep.M163, prep.BMPT
        self.assertEqual(prep.bone_of(m, 'wheel_l_01'), 'tire_moveB_l')
        self.assertEqual(prep.bone_of(m, 'wheel_r_05'), 'tire_moveF_r')
        self.assertEqual(prep.bone_of(m, 'wheel_r_drive'), 'tire_moveA_r')
        self.assertEqual(prep.bone_of(m, 'gun_barrel'), 'edf6vc_m61_rotor')
        self.assertEqual(prep.bone_of(m, 'x_root'), 'body')
        self.assertEqual(prep.bone_of(b, 'wheel_l_06'), 'tire_moveG_l')
        self.assertEqual(prep.bone_of(b, 'wheel_l_front'), 'tire_moveA_l')    # T-72: idler in front
        self.assertEqual(prep.bone_of(b, 'wheel_r_drive'), 'tire_moveH_r')    # sprocket at the back
        self.assertEqual(prep.bone_of(b, 'wheel_l_top_02'), 'body')           # return rollers do not turn
        self.assertEqual(prep.bone_of(b, 'gun_barrel_02'), 'cannon_slide_l')  # +x is the vehicle's left
        self.assertEqual(prep.bone_of(b, 'bone_turret_03'), 'body')           # the hull's grenade launchers
        self.assertEqual(prep.bone_of(b, '9m120_rocket'), 'cannon_main')

    def test_collision_hulls_are_outward_convex_pieces(self) -> None:
        cube = np.asarray([(x, y, z) for x in (0, 1) for y in (0, 1) for z in (0, 1)], dtype=float)
        tris = np.asarray([(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1),
                           (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3)])
        bolt = cube * 0.01 + 5.0                      # under MIN_HULL: left out
        P = np.vstack((cube, bolt))
        T = np.vstack((tris, tris + 8))
        hp, ht = prep.collision_hulls(P, T)
        self.assertTrue(np.all(hp < 2))
        c = hp.mean(axis=0)
        p = hp[ht]
        n = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
        self.assertTrue(np.all(np.einsum('ij,ij->i', n, p.mean(axis=1) - c) > 0))


def _ready() -> bool:
    return (Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file() and wt.available('vulcan') and wt.available('bmpt')


@unittest.skipUnless(_ready(), 'EDF6 Root.cpk or the War Thunder model folders are unavailable')
class WtAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import make_tracked_combat as combat
        cls.combat = combat
        cls.game = rootcpk.default()
        # The authored files (make_tracked_combat.authored): the model these tests check is the War Thunder geometry on
        # the native skeleton, without the seats' cockpits build() grafts onto it (its extra bones and materials:
        # test_grafted_eyes_sit_on_the_war_thunder_geometry).
        cls.files = {kind: combat.authored(cls.game.root, kind) for kind in combat.KINDS}

    def model(self, kind: str):  # noqa: ANN201
        import mdb
        import tracked_combat_model as t
        module = self.combat.model_module(kind)
        rab = mdb.rab_read(self.files[kind][f'OBJECT/{module.OUT_ARC}'])
        return mdb.mdb_read(t.member(rab, t.HOST_MDB).data), rab

    def test_generated_sets_pass_every_generator_check(self) -> None:
        for kind, files in self.files.items():
            with self.subTest(kind=kind):
                self.combat.check(files, kind, self.game)       # model, SHKT, ragdoll, CAS, muzzle contracts
                info = self.combat.model_module(kind).check(files[f'OBJECT/{self.combat.model_module(kind).OUT_ARC}'])
                self.assertEqual(info['stock_meshes_retained'], 0)

    def test_skeleton_is_native_with_moved_origins(self) -> None:
        import tracked_combat_model as t
        host, _rab = t.stock(self.game)
        for kind in self.files:
            with self.subTest(kind=kind):
                md, _rab = self.model(kind)
                spec = wt.load(kind)
                wt.check_skeleton(md, host, spec)                # names, links, rotations; origins per model.json
                extra = len(spec.info['visual_bones'])
                self.assertEqual(len(md.bones), len(host.bones) + extra)
        md, _rab = self.model('vulcan')
        rotor = md.bone_index('edf6vc_m61_rotor')
        self.assertEqual(md.name_of(md.bones[md.bones[rotor].parent].name), 'cannon_slide_l')

    def test_muzzles_sit_on_the_barrel_mouths(self) -> None:
        import dsgo
        import graft_pure as g
        import mdb
        import vcobjects as vc
        for kind in self.files:
            md, _rab = self.model(kind)
            spec = wt.load(kind)
            world = mdb.bind_world(md)
            vehicle = vc.GROUND_VEHICLES[kind]
            for index, path in enumerate(vehicle.weapons):
                side = self.combat.slot(kind, index)
                gun = dsgo.parse(self.files[kind][f'WEAPON/{path.rsplit("/", 1)[1].upper()}']).root
                block = gun.get('animation_model').items[2].data
                got = [struct.unpack_from('<3f', block, off) for _, _, off in vc.mab_muzzles(block)]
                want = spec.info['muzzles'][side]
                self.assertEqual(len(got), len(want))
                for local, row in zip(got, want):
                    with self.subTest(kind=kind, weapon=path):
                        placed = g.xform(local, world[md.bone_index(row['bone'])])
                        np.testing.assert_allclose(placed, row['point'], atol=2e-5)
        # the mouths are the barrels' front ends
        md, _rab = self.model('bmpt')
        spec = wt.load('bmpt')
        for side in ('left', 'right'):
            k = next(i for i, p in enumerate(spec.info['parts'])
                     if p['bone'] == f'cannon_slide_{side[0]}' and 'twingun' in p['name'])
            P = spec.part(k)[0]
            self.assertAlmostEqual(spec.info['muzzles'][side][0]['point'][2], float(P[:, 2].max()), places=5)
        self.assertEqual(len(spec.info['muzzles']['missiles']), 4)

    def test_grafted_eyes_sit_on_the_war_thunder_geometry(self) -> None:
        # build() is authored() with the seats' cockpits grafted (cockpits.graft_files, the fits by the model's stem):
        # the first-person eye (vc_eye_<seat>, src/fp_vehicle.cpp VehicleEye) is where pylib/cockpit_fits_plugin.py
        # puts it, on its fit's bone, and that place is the one measured on this model: clear of its meshes, at the
        # BMPT's gunner sight window (ahead of its hood), over the M163's gun receiver in the turret's well.
        import cockpit_fits
        import cockpits
        import mdb
        import vcobjects as vc
        if not cockpits.assets_present():
            self.skipTest('the cockpit assets (models/cockpits) are unavailable')
        eyes = {}
        for kind in self.files:
            with self.subTest(kind=kind):
                module = self.combat.model_module(kind)
                rel = f'OBJECT/{module.OUT_ARC}'
                built = cockpits.graft_files(vc.Game(self.game.root), self.files[kind])
                self.assertEqual({k: v for k, v in built.items() if k != rel},
                                 {k: v for k, v in self.files[kind].items() if k != rel})
                arc = mdb.rab_read(built[rel])
                md = mdb.mdb_read(next(f for f in arc.files if f.name.lower() == module.HOST_MDB).data)
                world = mdb.bind_world(md)
                (fit,) = cockpit_fits.FITS[module.OUT_ARC.rsplit('.', 1)[0]]
                eye = md.bone_index(f'vc_eye_{fit.seat}')
                self.assertGreaterEqual(eye, 0, f'{kind}: seat {fit.seat} has no eye')
                self.assertEqual(md.name_of(md.bones[md.bones[eye].parent].name), fit.bone)
                np.testing.assert_allclose(world[eye][12:15], fit.eye, atol=1e-4)
                spec = wt.load(kind)
                P = np.vstack([spec.part(k)[0] for k in range(len(spec.info['parts']))])
                self.assertGreater(float(np.linalg.norm(P - np.asarray(fit.eye), axis=1).min()), 0.05,
                                   f'{kind}: the eye is inside the model')
                eyes[kind] = (np.asarray(fit.eye), spec)

        def bounds(spec, name: str):  # noqa: ANN001, ANN202
            k = next(i for i, p in enumerate(spec.info['parts']) if p['name'].startswith(name))
            P = spec.part(k)[0]
            return P.min(axis=0), P.max(axis=0)
        eye, spec = eyes['bmpt']
        lo, hi = bounds(spec, 'cannon_main.wt_optic_glass_c')
        self.assertTrue(lo[0] < eye[0] < hi[0] and lo[1] < eye[1] < hi[1] and eye[2] > hi[2],
                        'BMPT: the eye looks out of the gunner sight window')
        eye, spec = eyes['vulcan']
        lo, hi = bounds(spec, 'cannon_main.wt_m163_vulcan_gun')
        radar_lo, radar_hi = bounds(spec, 'doppler_radar.')
        self.assertTrue(eye[1] > hi[1] and eye[2] < hi[2] and radar_hi[0] < eye[0] < hi[0],
                        'M163: the eye is behind and above the M61, left of the radar')

    def test_cas_retargets_absolute_clip_and_drops_radar_tracks(self) -> None:
        import cas_pose
        source = self.game.read('OBJECT', 'V603_FLAK.CAS')
        old = cas_pose.CasPose(source)
        fire = next(x for x in old.clips if x.name == 'fire_loop')
        for kind in self.files:
            with self.subTest(kind=kind):
                fixed = self.files[kind][self.combat.animation_path(kind)]
                pose = cas_pose.CasPose(fixed)
                self.assertEqual(len(fixed), len(source))
                self.assertEqual(source[fire.at:fire.at + 28], fixed[fire.at:fire.at + 28])
                radar = next(x for x in pose.clips if x.name == 'doppler_radar_loop')
                for track in radar.tracks:
                    if track.name in ('doppler_radar', 'tracking_radar'):
                        self.assertEqual((track.translation, track.rotation), (-1, -1))

    def test_ragdoll_follows_the_moved_bones_and_wheel_radii_are_the_model_s(self) -> None:
        import dsgo
        import ragdoll_fit as rf
        import vcobjects as vc
        for kind, files in self.files.items():
            with self.subTest(kind=kind):
                md, _rab = self.model(kind)
                r = dsgo.parse(files[f'OBJECT/{vc.GROUND_VEHICLES[kind].sgo}.SGO']).root
                shkt = files[self.combat.collision_path(kind)]
                self.assertEqual(rf.problems(md, shkt, r.get('ragdoll').items[1].data), [])
                radii = {row[0]: row[8] for row in dsgo.to_py(r.get('car_base_wheel'))}
                wheels = wt.load(kind).info['wheels']
                self.assertEqual(len(radii), 10 if kind == 'vulcan' else 12)
                for bone, radius in radii.items():
                    self.assertAlmostEqual(radius, wheels[bone]['radius'], places=5)

    def test_textures_are_bound_and_tracks_keep_the_scrolled_names(self) -> None:
        import obj_model as om
        import tracked_combat_model as t
        for kind in self.files:
            with self.subTest(kind=kind):
                md, rab = self.model(kind)
                self.assertEqual(om.texture_problems(rab, md, t.HOST_MDB), [])
                names = {md.name_of(m.name) for m in md.materials}
                self.assertTrue({'v603_kyata_L', 'v603_kyata_R'} <= names)

    def test_model_json_matches_its_arrays(self) -> None:
        for kind in self.files:
            spec = wt.load(kind)
            with open(os.path.join(spec.folder, 'model.json'), encoding='utf-8') as f:
                info = json.load(f)
            for k, p in enumerate(info['parts']):
                pos, nrm, uv, tris = spec.part(k)
                self.assertEqual((len(pos), len(tris)), (p['vertices'], p['triangles']))
                self.assertEqual(len(nrm), len(pos))
                self.assertEqual(len(uv), len(pos))
                self.assertTrue(np.allclose(np.linalg.norm(nrm, axis=1), 1, atol=1e-3))
                self.assertLess(int(tris.max()), len(pos))


if __name__ == '__main__':
    unittest.main()
