"""Authored M163/BMPT: matching native model / weapon / request contracts.

The native data cases read only Root.cpk. They generate in memory and never write
or start the game. Missing game data skips these cases but keeps table tests.
"""
from pathlib import Path
import struct
import copy

import numpy as np
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import call_weapons
import calls
import dsgo
import make_tracked_combat as combat
import rootcpk
import vcobjects as vc
import graft_pure as graft
import mdb
import tracked_combat_model as tracked
import vulcan_model


class CatalogContractTests(unittest.TestCase):
    def test_new_requests_append_without_reordering_shipped_ids(self) -> None:
        self.assertEqual(calls.IDS[-2:], ('EDF6VC_CALL_VULCAN', 'EDF6VC_CALL_BMPT'))
        for release in calls.RELEASED.values():
            self.assertEqual(calls.IDS[:len(release)], release)

    def test_native_weapon_choices_use_same_catalog_as_runtime(self) -> None:
        vehicle = vc.GROUND_VEHICLES['vulcan']
        self.assertEqual(len(vehicle.weapons), 2)
        for kind, path in zip(('VULCAN_HE', 'VULCAN_AP'), vehicle.weapons):
            self.assertEqual(vc.store_of(path), (kind, 2000))
            self.assertTrue(vc.STORES[kind].native_control)
        missile = vc.GROUND_VEHICLES['bmpt'].weapons[2]
        self.assertEqual(vc.store_of(missile), ('BMPT_ATGM', 4))
        self.assertEqual(vc.STORES['BMPT_ATGM'].role, 'ground')


@unittest.skipUnless((Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file(), 'EDF6 Root.cpk is unavailable')
class NativeAssetContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.game = rootcpk.default()
        cls.files = {kind: combat.build(cls.game.root, kind) for kind in combat.KINDS}

    def test_authored_geometry_native_skeleton_and_weapon_muzzles(self) -> None:
        for kind, files in self.files.items():
            with self.subTest(kind=kind):
                combat.check(files, kind, self.game)
                model = combat.model_module(kind)
                info = model.check(files[f'OBJECT/{model.OUT_ARC}'])
                self.assertIsNotNone(info)

    def test_private_optics_animation_is_a_required_generated_dependency(self) -> None:
        for kind, files in self.files.items():
            with self.subTest(kind=kind):
                path = combat.animation_path(kind)
                self.assertIn(path, files)
                doc = dsgo.parse(files[f'OBJECT/{vc.GROUND_VEHICLES[kind].sgo}.SGO'])
                self.assertEqual(doc.root.get('animation_model').items[1], 'app:/' + path.lower())
                missing = dict(files);del missing[path]
                with self.assertRaisesRegex(ValueError, 'incomplete generated asset set'):
                    combat.check(missing, kind, self.game)
                bad = dict(files);bad[path] = self.game.read('OBJECT', combat.model_module(kind).HOST_CAS)
                with self.assertRaisesRegex(ValueError, 'private optics animation payload differs'):
                    combat.check(bad, kind, self.game)

    def test_m163_rotor_is_a_declared_preorder_leaf_with_all_native_world_binds(self) -> None:
        model = vulcan_model
        native, _rab = tracked.stock(self.game)
        arc = mdb.rab_read(self.files['vulcan'][f'OBJECT/{model.OUT_ARC}'])
        md = mdb.mdb_read(tracked.member(arc,model.HOST_MDB).data)
        self.assertEqual(len(md.bones),len(native.bones)+1)
        rotor = md.bone_index(model.ROTOR_BONE)
        self.assertEqual(rotor,6)
        self.assertEqual(md.bones[rotor].parent,md.bone_index(model.ROTOR_PARENT))
        self.assertEqual([md.name_of(b.name) for b in md.bones if md.name_of(b.name)!=model.ROTOR_BONE],
                         [native.name_of(b.name) for b in native.bones])
        old,new = mdb.bind_world(native),mdb.bind_world(md)
        for bone in native.bones:
            np.testing.assert_allclose(new[md.bone_index(native.name_of(bone.name))],old[bone.index],atol=2e-6)
        self.assertEqual(model.animation(self.game),self.files['vulcan'][combat.animation_path('vulcan')])
        # A naive end-of-file child produces a backwards native subtree range.
        broken = copy.deepcopy(md)
        leaf = broken.bones.pop(rotor)
        broken.bones.append(leaf)
        for index,bone in enumerate(broken.bones):
            bone.index=index
        with self.assertRaises(tracked.TrackedCombatCheckError):
            tracked.check_skeleton(broken,model.VISUAL_BONES)

    def test_rotor_pose_moves_six_barrels_and_clamps_without_aim_or_case_drift(self) -> None:
        model = vulcan_model
        rab = mdb.rab_read(self.files['vulcan'][f'OBJECT/{model.OUT_ARC}'])
        md = mdb.mdb_read(tracked.member(rab,model.HOST_MDB).data)
        rotor=md.bone_index(model.ROTOR_BONE)
        assembly=tracked.Assembly(md);model.parts(assembly)
        meshes=dict(zip(assembly.parts,md.objects[0].meshes))
        moving={'six_m61_barrels','m61_barrel_clamps'}
        self.assertEqual({name for name,part in assembly.parts.items() if part._rigid_bone==rotor},moving)
        bind=np.asarray(md.bones[rotor].local).reshape(4,4)
        parent=np.asarray(mdb.bind_world(md)[md.bones[rotor].parent]).reshape(4,4)
        inverse=np.asarray(md.bones[rotor].inv_bind).reshape(4,4)
        centre=np.asarray((*model.MUZZLES['left'][0][1],1.0))
        original=np.asarray(graft.mesh_positions(meshes['six_m61_barrels']))
        vertices=np.column_stack((original,np.ones(len(original))))
        for phase in (.2,.7,1.3,2.2):
            c,s=np.cos(phase),np.sin(phase)
            rz=np.asarray(((c,s,0,0),(-s,c,0,0),(0,0,1,0),(0,0,0,1)))
            world=rz@bind@parent
            posed=vertices@inverse@world
            self.assertGreater(np.max(np.abs(posed[:,:3]-original)),.01)
            np.testing.assert_allclose(centre@inverse@world,centre,atol=2e-6)
            # Receiver and case port retain the static slide; they cannot orbit
            # with the rotor or make the existing native aim/muzzle drift.
            self.assertEqual(assembly.parts['m61_receiver']._rigid_bone,md.bone_index('cannon_r'))
            self.assertTrue(all(bone=='cannon_slide_l' for bone,_ in model.MUZZLES['left']+model.MUZZLES['right']))
        bad=copy.deepcopy(md);bad.bones[rotor].kind=0
        with self.assertRaises(tracked.TrackedCombatCheckError):
            tracked.check_skeleton(bad,model.VISUAL_BONES)

    def test_request_default_setup_has_every_real_holder(self) -> None:
        for kind in combat.KINDS:
            with self.subTest(kind=kind):
                call = next(c for c in calls.CALLS if c.ground == kind)
                template = self.game.read('WEAPON', call_weapons.template_of(call) + '.SGO')
                request = call_weapons.vehicle_sgo(template, call, (1.0, 1.0))
                root = dsgo.parse(request).root
                setup = dsgo.to_py(root.get('Ammo_CustomParameter'))[4][3]
                weapons = tuple(entry[0] for entry in setup[-1])
                self.assertEqual(weapons, vc.GROUND_VEHICLES[kind].weapons)
                self.assertEqual(root.get('use_underground'), 0.0)
                model = dsgo.parse(self.files[kind][f'OBJECT/{vc.GROUND_VEHICLES[kind].sgo}.SGO']).root
                self.assertEqual(len(setup[-1]), len(model.get('vehicle_weapon_setting').items))
                if kind == 'bmpt':
                    self.assertEqual(setup[-1][2][1], [0.0, 0.0])
                    self.assertEqual(len(setup[-1][2]), 2, 'missile store has no turret-motor parameters')
                    self.assertEqual(len(dsgo.to_py(model.get('vehicle_setup'))[-1][2]), 2)

    def test_m163_alternate_belts_share_the_visible_moving_barrel(self) -> None:
        vehicle = vc.GROUND_VEHICLES['vulcan']
        r = dsgo.parse(self.files['vulcan'][f'OBJECT/{vehicle.sgo}.SGO']).root
        settings = dsgo.to_py(r.get('vehicle_weapon_setting'))
        self.assertEqual(settings[0][0], 'cannon_slide_l')
        self.assertEqual(settings[1][0], settings[0][0])
        for path in vehicle.weapons:
            gun = dsgo.parse(self.files['vulcan'][f'WEAPON/{path.rsplit("/",1)[1].upper()}']).root
            self.assertEqual(gun.get('FireInterval'), 1.0)
            self.assertGreater(gun.get('AmmoSpeed'), 15.0)
        for kind in ('VULCAN_HE', 'VULCAN_AP'):
            weapon = dsgo.parse(self.files['vulcan'][f'WEAPON/{vc.store_file(kind,2000)}']).root
            self.assertEqual(weapon.get('AmmoIsPenetration'), 1.0 if kind.endswith('AP') else 0.0)
            self.assertEqual(weapon.get('AmmoExplosion'), 0.0 if kind.endswith('AP') else 0.6)

    def test_cases_leave_authored_receiver_with_cartridge_scale(self) -> None:
        for kind in combat.KINDS:
            model = combat.model_module(kind)
            expected = model.ejection_local(self.game)
            for side, path in zip(('left', 'right'), vc.GROUND_VEHICLES[kind].weapons[:2]):
                gun = dsgo.parse(self.files[kind][f'WEAPON/{path.rsplit("/",1)[1].upper()}']).root
                self.assertEqual(gun.get('ShellCase_CustomParameter').get('scale'), 0.4 if kind == 'vulcan' else 0.64)
                block = gun.get('animation_model').items[2].data
                heads = struct.unpack_from('<I', block, 0x14)[0]
                header = heads + 16
                record = header + struct.unpack_from('<i', block, header+4)[0]
                offset = record + struct.unpack_from('<i', block, record+0xC)[0]
                for got, want in zip(struct.unpack_from('<3f', block, offset), expected[side]):
                    self.assertAlmostEqual(got, want, places=5)

    def test_muzzle_patch_preserves_native_casing_and_effect_lists(self) -> None:
        native = dsgo.parse(self.game.read('WEAPON', combat.STOCK_GUNS[0])).root.get('animation_model').items[2].data
        point = ('cannon_slide_l', (0.125, 0.25, 1.5))
        changed = combat.set_muzzles(native, [point])
        locator = vc.mab_muzzles(native)[0][2]
        expected = bytearray(native)
        struct.pack_into('<3f', expected, locator, *point[1])
        self.assertEqual(changed, bytes(expected))
        with self.assertRaises(ValueError):
            combat.set_muzzles(native, [point, point])


if __name__ == '__main__':
    unittest.main()
