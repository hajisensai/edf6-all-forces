"""Shield skinning/CANM regressions; real assets are read-only when Root.cpk exists."""
from __future__ import annotations

import math
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import cas_pose
import graft_pure as graft
import make_proteus
import mdb
import proteus_model as pm
import rootcpk
import sgo


def fixture() -> bytes:
    data = bytearray(240)
    data[:8] = b'CAS\0\x04\x02\0\0'
    struct.pack_into('<I', data, 8, 32)
    data[32:40] = b'CANM\0\x03\0\0'
    struct.pack_into('<6I', data, 40, 1, 32, 1, 80, 1, 128)
    struct.pack_into('<IiffIII', data, 64, 0, 136, 1., 1., 1, 1, 104)
    struct.pack_into('<8f4i', data, 112, 1., 2., 3., 0., 0., 0., 0., 0., 0, 0, 1, 0)
    struct.pack_into('<i', data, 160, 24)
    struct.pack_into('<Hhhh', data, 168, 0, 0, -1, -1)
    data[184:194] = 'base\0'.encode('utf-16-le')
    data[200:212] = 'stand\0'.encode('utf-16-le')
    return bytes(data)


class AnimationTests(unittest.TestCase):
    def check_extension(self, source: bytes, target: bytes) -> None:
        old, new = cas_pose.CasPose(source), cas_pose.CasPose(target)
        self.assertEqual(new.names, old.names + pm.SHIELD_BONES)
        self.assertEqual(new.channel_count, old.channel_count + 1)
        self.assertEqual(new.points % 16, 0, 'native CANM SIMD channel rows need 16-byte alignment')
        for i in range(old.channel_count):
            a, b = old.points + i * 48, new.points + i * 48
            self.assertEqual(source[a:a + 32], target[b:b + 32])
            self.assertEqual(source[a + 36:a + 48], target[b + 36:b + 48])
            off_a, off_b = struct.unpack_from('<i', source, a + 32)[0], struct.unpack_from('<i', target, b + 32)[0]
            self.assertEqual(a + off_a if off_a else 0, b + off_b if off_b else 0)
        for before, after in zip(old.clips, new.clips):
            self.assertEqual(before.name, after.name)
            self.assertEqual(source[before.at:before.at+20], target[after.at:after.at+20],
                             'loop flag, duration, step and frame count remain native values')
            self.assertEqual(len(after.tracks), len(before.tracks) + 36)
            for a, b in zip(before.tracks, after.tracks):
                self.assertEqual(source[a.at:a.at + 8], target[b.at:b.at + 8])
            added = after.tracks[len(before.tracks):]
            self.assertEqual(tuple(t.name for t in added), pm.SHIELD_BONES)
            for t in added:
                self.assertEqual((t.translation, t.rotation, t.scale), (-1, -1, old.channel_count))
                self.assertEqual(new.translation(t.scale), (0., 0., 0.))

    def test_extension_preserves_stock_animation_and_hides_uncontrolled_panels(self) -> None:
        source = fixture()
        target = pm.animation(source)
        self.check_extension(source, target)
        self.assertEqual(cas_pose.CasPose(target).translation(0), (1., 2., 3.))
        with self.assertRaisesRegex(ValueError, 'already bound'):
            pm.animation(target)

    def test_panel_geometry_has_boundaries_and_nonzero_faces(self) -> None:
        template = mdb.Mesh(b'\0\1\1\0', 0, 0, 32,
                            [mdb.VElem(1, 0, 0, 'position'), mdb.VElem(1, 16, 0, 'normal')], 0,
                            struct.pack('<8f', 0., 0., 0., 1., 0., 0., 1., 1.), b'')
        mesh = pm.shield_mesh(template, 0)
        points = mdb.read_elem(mesh, 'position')
        self.assertEqual(mesh.nverts, 12)
        self.assertEqual(len(mesh.indices) // 6, 8)
        for p in points:
            self.assertAlmostEqual(math.hypot(p[0], p[2]), 11., places=5)
            self.assertLessEqual(abs(math.degrees(math.atan2(p[0], p[2]))), 5.00001)
        for a, b, c in struct.iter_unpack('<3H', mesh.indices):
            ab = [points[b][k] - points[a][k] for k in range(3)]
            ac = [points[c][k] - points[a][k] for k in range(3)]
            cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
            self.assertGreater(sum(v*v for v in cross), 1.)


@unittest.skipUnless(Path(rootcpk.DEFAULT_GAME, 'Root.cpk').is_file(), 'read-only Root.cpk unavailable')
class StockAssetsTests(AnimationTests):
    @classmethod
    def setUpClass(cls) -> None:
        cls.game = rootcpk.Game(rootcpk.DEFAULT_GAME)
        cls.files = make_proteus.build(rootcpk.DEFAULT_GAME)

    def test_real_model_skinning_shader_and_original_parts(self) -> None:
        for host in pm.HOSTS:
            with self.subTest(host=host):
                old_arc = mdb.rab_read(self.game.read('OBJECT', host + '.MRAB'))
                new_arc = mdb.rab_read(self.files[f'OBJECT/EDF6VC_{host}.MRAB'])
                name = host.lower() + '.mdb'
                old = mdb.mdb_read(next(f.data for f in old_arc.files if f.name.lower() == name))
                new = mdb.mdb_read(next(f.data for f in new_arc.files if f.name.lower() == name))
                self.assertEqual([new.name_of(b.name) for b in new.bones[:len(old.bones)]],
                                 [old.name_of(b.name) for b in old.bones])
                for a, b in zip(old.bones, new.bones):
                    self.assertEqual((a.parent, a.local, a.inv_bind), (b.parent, b.local, b.inv_bind))
                for a, b in zip(old.objects, new.objects):
                    self.assertEqual(a.meshes, b.meshes[:len(a.meshes)])
                points = graft.skinned_points(new)
                for bone_name in pm.SHIELD_BONES:
                    bone = new.bone_index(bone_name)
                    self.assertGreaterEqual(bone, len(old.bones))
                    self.assertEqual(len(points[bone]), 12)
                    self.assertEqual(new.bones[bone].local, mdb.ident())
                shield = [me for ob in new.objects for me in ob.meshes
                          if new.materials[me.material].shader == 'snd_Chara_FencerEnergyShield']
                self.assertEqual(len(shield), 36)
                self.check_extension(self.game.read('OBJECT', host + '.CAS'), self.files[f'OBJECT/EDF6VC_{host}.CAS'])

    def test_every_vehicle_variant_only_references_private_model_and_animation(self) -> None:
        count = 0
        for path, data in self.files.items():
            if not path.endswith('.SGO'):
                continue
            name = path.split('/')[1]
            before = sgo.load(data=self.game.read('OBJECT', name))
            after = sgo.load(data=data)
            expected = before['animation_model']
            actual = after.pop('animation_model')
            before.pop('animation_model')
            self.assertEqual(before, after)
            self.assertEqual(len(after['vehicle_riding_position']), 4)
            self.assertEqual(actual[0][1], expected[0][1])
            self.assertEqual(actual[2], expected[2])
            for ref in (actual[0][0], actual[1]):
                self.assertIn(ref.removeprefix('app:/').upper(), self.files)
            count += 1
        self.assertEqual(count, 10)

    def test_range_redirect_uses_identical_generated_resource_schema(self) -> None:
        import sys
        sys.path.insert(0, str(ROOT / 'testrange'))
        import gen
        name = 'v614_proteus_mk2_mission'
        stock = gen.vehicle_sgo(self.game, name)
        made, needs = make_proteus.redirect(stock)
        self.assertEqual(made, self.files['OBJECT/' + name.upper() + '.SGO'])
        self.assertEqual(set(needs), {'OBJECT/EDF6VC_V614_PROTEUS_MK2.MRAB',
                                     'OBJECT/EDF6VC_V614_PROTEUS_MK2.CAS'})
        self.assertTrue(all(rel in self.files for rel in needs))


if __name__ == '__main__':
    unittest.main()
