"""X-Morph: Defense's units (docs/xmorph-port.md): the Ogre reader (pylib/ogre_mesh.py) and EXOR's text format
(pylib/xm_pack.py) on synthetic data, the frame change and the tracked rig rules (pylib/xm_model.py,
tools/prep_xmorph.py) on a synthetic unit; with the game installed (the developer's machine: CI has no X-Morph), one
unit read from the packs and prepared into a temporary folder, and with EDF6's Root.cpk too, that folder built and
checked through pylib/wt_tracked.py and pylib/tracked_collision.py.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]

import numpy as np

import ogre_mesh as om
import prep_xmorph as px
import xm_model as xm
import xm_pack as xp


# ------------------------------------------------------------------------------------------ synthetic Ogre files

def chunk(cid: int, payload: bytes, length: int | None = None) -> bytes:
    """An Ogre chunk; `length` overrides the stored length (Ogre's containers do not count what follows them)."""
    return struct.pack('<HI', cid, 6 + len(payload) if length is None else length) + payload


def mesh_bytes() -> bytes:
    """One submesh: a quad (two triangles) with positions and UVs, its own geometry, two bones (the second vertex
    pair on bone 1), a skeleton link, bounds and a name table. The mesh and submesh chunks carry lengths that are
    not their contents' (6): the reader must not trust them."""
    pos = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
    vdata = b''.join(struct.pack('<5f', *p, *t) for p, t in zip(pos, uv))
    elements = chunk(om.M_GEOMETRY_VERTEX_ELEMENT, struct.pack('<5H', 0, 2, om.SEM_POSITION, 0, 0)) + \
        chunk(om.M_GEOMETRY_VERTEX_ELEMENT, struct.pack('<5H', 0, 1, om.SEM_TEXCOORD, 12, 0))
    geometry = chunk(om.M_GEOMETRY, struct.pack('<I', 4) +
                     chunk(om.M_GEOMETRY_VERTEX_DECLARATION, elements) +
                     chunk(om.M_GEOMETRY_VERTEX_BUFFER, struct.pack('<HH', 0, 20) +
                           chunk(om.M_GEOMETRY_VERTEX_BUFFER_DATA, vdata)), length=6)
    idx = (0, 1, 2, 0, 2, 3)
    sub = 'units/quad\n'.encode() + struct.pack('<?I?', False, len(idx), False) + struct.pack('<6H', *idx)
    sub += geometry + chunk(om.M_SUBMESH_OPERATION, struct.pack('<H', 4))
    for v, b in ((0, 0), (1, 1), (2, 1), (3, 0)):
        sub += chunk(om.M_SUBMESH_BONE_ASSIGNMENT, struct.pack('<IHf', v, b, 1.0))
    body = struct.pack('<?', True) + chunk(om.M_SUBMESH, sub, length=6)
    body += chunk(om.M_MESH_SKELETON_LINK, b'meshes/quad.skeleton\n')
    body += chunk(om.M_MESH_BOUNDS, struct.pack('<7f', 0, 0, 0, 1, 1, 0, 1.5))
    body += chunk(om.M_SUBMESH_NAME_TABLE, chunk(om.M_SUBMESH_NAME_TABLE_ELEMENT, struct.pack('<H', 0) + b'quad\n'))
    return struct.pack('<H', om.M_HEADER) + b'[ExorSerializer_v1.41]\n' + chunk(om.M_MESH, body, length=6)


def skeleton_bytes() -> bytes:
    """body (at z 2, turned 90 degrees about z) and its child turret (1 m along the parent's x), then an animation."""
    s = 2 ** -0.5
    out = struct.pack('<H', om.M_HEADER) + b'[Serializer_v1.10]\n'
    out += chunk(om.SKELETON_BONE, b'body\n' + struct.pack('<H3f4f', 0, 0, 0, 2, 0, 0, s, s), length=6)
    out += chunk(om.SKELETON_BONE, b'turret\n' + struct.pack('<H3f4f3f', 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1), length=6)
    out += chunk(om.SKELETON_BONE_PARENT, struct.pack('<HH', 1, 0))
    out += chunk(om.SKELETON_ANIMATION, b'idle\n' + struct.pack('<f', 1.0) + b'\xff' * 12)
    return out


class OgreReaderTests(unittest.TestCase):
    def test_mesh(self) -> None:
        m = om.read_mesh(mesh_bytes())
        self.assertEqual(m.skeleton, 'meshes/quad.skeleton')
        (sub,) = m.submeshes
        self.assertEqual((sub.material, sub.name), ('units/quad', 'quad'))
        np.testing.assert_array_equal(sub.indices, [[0, 1, 2], [0, 2, 3]])
        np.testing.assert_allclose(m.positions(sub)[2], (1, 1, 0))
        np.testing.assert_allclose(m.uvs(sub)[3], (0, 1))
        np.testing.assert_array_equal(m.rigid_bones(sub), [0, 1, 1, 0])
        self.assertEqual(m.bounds[2], 1.5)

    def test_skeleton(self) -> None:
        sk = om.read_skeleton(skeleton_bytes())
        self.assertEqual([b.name for b in sk.bones], ['body', 'turret'])
        self.assertEqual(sk.bones[1].parent, 0)
        self.assertEqual(sk.bones[1].scale, (1.0, 1.0, 1.0))
        self.assertEqual(sk.animations, ['idle'])
        w = sk.world()
        np.testing.assert_allclose(w[1][:3, 3], (0, 1, 2), atol=1e-6)    # 1 m along the parent's turned x

    def test_rejects_other_files(self) -> None:
        with self.assertRaises(om.OgreError):
            om.read_mesh(struct.pack('<H', 0x1234) + b'x\n')


class TextFormatTests(unittest.TestCase):
    def test_blocks_values_and_material_scripts(self) -> None:
        root = xp.parse('''
            EntityBlueprint            // a comment
            {
                name "units/a"
                TurretComp { Turrets { Turret {
                    name "t"
                    yaw "turret_1"
                    pitch "barrel_1"
                } } }
                aim_volume "SphericalAimVolume"
                {
                    range_max "8"
                }
                offset "1 2 3"
                offset "4 5 6"
            }
            material units_ground/a : base/default
            {
                set_texture_alias diffuse materials/textures/a.dds
                set_texture_alias normal materials/textures/a_normal.dds
            }''')
        bp = root.child('EntityBlueprint')
        self.assertEqual(bp.get('name'), 'units/a')
        (t,) = bp.find('Turret')
        self.assertEqual((t.get('yaw'), t.get('pitch')), ('turret_1', 'barrel_1'))
        self.assertEqual(bp.child('aim_volume').get('type'), 'SphericalAimVolume')
        self.assertEqual(bp.values['offset'], ['1 2 3', '4 5 6'])
        self.assertEqual(bp.floats('offset'), (4.0, 5.0, 6.0))
        mat = root.child('material')
        self.assertEqual(mat.get('type'), 'units_ground/a : base/default')
        self.assertEqual(mat.values['set_texture_alias'][1], 'normal materials/textures/a_normal.dds')

    def test_unbalanced(self) -> None:
        with self.assertRaises(xp.PackError):
            xp.parse('A { B { }')


# ------------------------------------------------------------------------------------------ frame, rig

class FrameTests(unittest.TestCase):
    def test_frame_change_is_a_rotation(self) -> None:
        R = xm.to_edf(np.eye(3)).T          # columns: where X-Morph's axes go
        self.assertAlmostEqual(float(np.linalg.det(R)), 1.0)
        np.testing.assert_allclose(xm.to_edf((1, 0, 0)), (0, 0, 1))   # forward
        np.testing.assert_allclose(xm.to_edf((0, 1, 0)), (1, 0, 0))   # the left side: EDF +x
        np.testing.assert_allclose(xm.to_edf((0, 0, 1)), (0, 1, 0))   # up

    def test_normal_map_rebuilt_from_alpha_and_green(self) -> None:
        from PIL import Image
        a = np.zeros((2, 2, 4), dtype=np.uint8)
        a[..., :3] = 128            # y ~ 0 (R = G = B)
        a[..., 3] = 255             # x = +1
        a[1, 1] = (128, 128, 128, 128)          # flat
        a[0, 1] = (255, 255, 255, 128)          # y = +1, OpenGL: the texture's up
        rgb = np.asarray(xm.normal_rgb(Image.fromarray(a, 'RGBA')), dtype=int)
        self.assertGreater(rgb[0, 0, 0], 250)
        self.assertLess(rgb[0, 1, 1], 5)        # DirectX green (the stock maps'): up is -Y
        self.assertLess(abs(rgb[1, 1, 0] - 128), 2)
        self.assertGreater(rgb[1, 1, 2], 250)   # flat: z = 1


def _box(c, h) -> tuple[np.ndarray, np.ndarray]:  # noqa: ANN001
    """A closed box (EDF frame) wound outward."""
    c, h = np.asarray(c, float), np.asarray(h, float)
    P = np.asarray([c + h * (x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)])
    T = np.asarray([(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1),
                    (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3)])
    return P, T


def synthetic_unit() -> xm.UnitModel:
    """A tank in EDF's frame: a hull, two belts on the ground (x +-1.5), a turret on a ring, its gun (a cradle and a
    recoiling barrel), a mast standing on the turret's ring but parented to the hull, a hull launcher off the ring."""
    pieces = [('body', (0, 1.0, 0), (1.2, 0.5, 3.0)), ('body', (1.5, 0.5, 0), (0.3, 0.5, 2.5)),
              ('body', (-1.5, 0.5, 0), (0.3, 0.5, 2.5)), ('turret_1', (0, 2.0, 0), (1.0, 0.5, 1.0)),
              ('barrel_1', (0, 2.0, 1.2), (0.3, 0.3, 0.2)), ('recoil', (0, 2.0, 2.4), (0.1, 0.1, 1.0)),
              ('be_mast', (0, 3.0, -0.2), (0.1, 0.5, 0.1)), ('launcher', (0.8, 1.8, -2.5), (0.3, 0.3, 0.3))]
    P, T, B = [], [], []
    for bone, c, h in pieces:
        p, t = _box(c, h)
        T.append(t + sum(len(x) for x in P))
        P.append(p)
        B += [bone] * len(t)
    bind = {'body': (0, 0, 0), 'turret_1': (0, 1.5, 0), 'barrel_1': (0, 2.0, 1.0), 'recoil': (0, 2.0, 1.5),
            'muzzle_1': (0, 2.0, 3.3), 'be_mast': (0, 2.6, -0.2), 'launcher': (0.8, 1.6, -2.5)}
    parent = {'body': None, 'turret_1': 'body', 'barrel_1': 'turret_1', 'recoil': 'barrel_1', 'muzzle_1': 'barrel_1',
              'be_mast': 'body', 'launcher': 'body'}
    # one key a line: a line's head word takes the rest of its line (xm_pack.parse)
    bp = xp.parse('''EntityBlueprint {
        TurretComp { Turrets { Turret {
            name "main_turret"
            yaw "turret_1"
            pitch "barrel_1"
        } } }
        WeaponComp { Weapons { Weapon {
            name "main_weapon"
            weapon_controller "TurretWeaponController" {
                turret "main_turret"
            }
            Muzzles { Muzzle {
                attachment "muzzle_1"
                recoil_bone "recoil"
            } }
        } } } }''').child('EntityBlueprint')
    frames = {}
    for b, p in bind.items():
        f = np.eye(4)
        f[:3, 3] = np.asarray(p)[[2, 0, 1]]           # X-Morph frame
        frames[b] = f
    allp = np.vstack(P)
    return xm.UnitModel('ground/synthetic', bp, 'meshes/synthetic.mesh', list(bind), parent,
                        {b: np.asarray(p, float) for b, p in bind.items()}, frames, {}, allp,
                        np.zeros((len(allp), 2)), np.vstack(T), np.asarray(B, dtype=object), np.zeros(len(B), int),
                        ['units/synthetic'])


class RigTests(unittest.TestCase):
    def test_bones_follow_the_blueprint(self) -> None:
        um = synthetic_unit()
        r = px.rig(um, 'xm_synthetic')
        self.assertEqual((r['yaw'], r['pitch'], r['recoils']), ('turret_1', 'barrel_1', ['recoil']))
        b = r['bone']
        self.assertEqual(b['turret_1'], 'cannon_main')
        self.assertEqual(b['barrel_1'], 'cannon_l')
        self.assertEqual(b['recoil'], 'cannon_slide_l')
        self.assertEqual(b['be_mast'], 'cannon_main')       # on the ring, over the axis: carried by the turret
        self.assertEqual(b['launcher'], 'body')             # off the ring: stays on the hull
        np.testing.assert_allclose(r['mouths']['left'], (0, 2.0, 3.3))
        np.testing.assert_allclose(r['mouths']['right'], (0, 2.0, 3.3))

    def test_running_gear_inside_the_belts(self) -> None:
        um = synthetic_unit()
        gear = px.running_gear(um, px.rig(um, 'xm_synthetic')['bone'])
        for side, sign in (('l', 1), ('r', -1)):
            g = gear[side]
            np.testing.assert_allclose(g['band'], (1.2, 1.8))
            self.assertEqual(len(g['wheels']), 8)
            zs = [c[2] for c, _r, _h in g['wheels']]
            self.assertEqual(zs, sorted(zs, reverse=True))  # A in front
            for (x, y, z), radius, half in g['wheels']:
                self.assertAlmostEqual(y, radius)            # on the ground
                self.assertTrue(1.2 < sign * x - half and sign * x + half < 1.8)
                self.assertTrue(-2.5 < z - radius and z + radius < 2.5)
                self.assertLess(2 * radius, g['top'])
            self.assertTrue(g['belt'].any())

    def test_wheel_mesh_is_closed_and_outward(self) -> None:
        P, T = px.wheel_mesh((1.0, 0.3, 0.0), 0.3, 0.1)
        c = P.mean(axis=0)
        tri = P[T]
        vol = np.einsum('ij,ij->i', tri[:, 0] - c, np.cross(tri[:, 1] - c, tri[:, 2] - c)).sum() / 6
        self.assertGreater(vol, 0)
        np.testing.assert_allclose(P.min(axis=0)[1:], (0.0, -0.3), atol=1e-9)

    def test_runs_merge(self) -> None:
        self.assertEqual(px._merge([(3, 4), (0, 1), (1.2, 2)], tol=0.3), [(0, 2), (3, 4)])


# ------------------------------------------------------------------------------------------ the game's own unit

def _xmorph() -> bool:
    return os.path.isdir(os.path.join(xp.STEAM_DIR, 'packs'))


@unittest.skipUnless(_xmorph(), 'X-Morph: Defense is not installed (the developer machine only)')
class XmorphAssetTests(unittest.TestCase):
    UNIT = 'xm_medium_tank_regular'

    @classmethod
    def setUpClass(cls) -> None:
        import xm_units
        cls.packs = xp.Packs()
        cls.unit = xm_units.UNITS[cls.UNIT]
        cls.tmp = tempfile.mkdtemp(prefix='test_xmorph_')
        cls.info = px.prepare_tracked(cls.packs, cls.unit, cls.tmp)
        cls.folder = os.path.join(cls.tmp, cls.UNIT)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.packs.close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_unit_reads(self) -> None:
        um = xm.load(self.packs, self.unit.entity)
        self.assertEqual(um.bones, ['body', 'turret_1', 'barrel_1', 'muzzle_1', 'recoil'])
        self.assertEqual(set(um.tri_bone), {'body', 'turret_1', 'barrel_1', 'recoil'})
        P = um.positions
        self.assertAlmostEqual(float(P[:, 1].min()), 0.0, places=2)     # on the ground
        self.assertGreater(float(P[:, 2].max()), 8.0)                    # the barrel points +z
        self.assertIn('normal', xm.texture_images(self.packs, um.materials[0]))

    def test_every_unit_reads(self) -> None:
        # Every row of the table, ported or not: an inherited blueprint (the warcar's escort helicopter), an unskinned
        # mesh (the bunkers: one body bone), a skin naming a bone the skeleton lacks (large_gunship_aa: the root).
        import xm_units
        for key, unit in xm_units.UNITS.items():
            with self.subTest(unit=key):
                um = xm.load(self.packs, unit.entity)
                self.assertGreater(len(um.triangles), 0)
                self.assertTrue(set(um.tri_bone) <= set(um.bones))
        self.assertEqual(xm.load(self.packs, 'fort/bunker_barracks').bones, [xm.STATIC_BONE])
        escort = xm.blueprint(self.packs, 'entities/units/bosses/warcar_escort_helicopter.ent')
        self.assertEqual(escort.child('RenderableDescComp').get('mesh'), 'meshes/units/air/large_helicopter_core.mesh')

    def test_folder(self) -> None:
        info = self.info
        self.assertEqual((info['format'], info['kind']), (1, self.UNIT))
        self.assertFalse(info['winding_flipped'])
        names = {p['name'] for p in info['parts']}
        self.assertTrue({f'wheel_{k}_{s}' for k in 'ABCDEFGH' for s in 'lr'} <= names)
        self.assertTrue({'v603_kyata_L', 'v603_kyata_R'} <= {m['name'] for m in info['materials']})
        self.assertEqual(info['muzzles']['left'][0]['point'], info['muzzles']['right'][0]['point'])
        for f in ('model.json', 'model.npz', 'LICENSE.txt'):
            self.assertTrue(os.path.isfile(os.path.join(self.folder, f)), f)
        for m in info['materials']:
            self.assertTrue(os.path.isfile(os.path.join(self.folder, m['albedo'])))

    def test_builds_on_the_v603(self) -> None:
        import rootcpk
        if not (Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file():
            self.skipTest('EDF6 Root.cpk unavailable')
        import obj_model
        import tracked_collision as tc
        import tracked_combat_model as t
        import wt_tracked as wt
        game = rootcpk.default()
        old = (os.environ.get('EDF6VC_MODELS'), dict(wt.FOLDERS), tc.KINDS)
        os.environ['EDF6VC_MODELS'] = self.tmp            # obj_model.model_roots: this folder first
        wt.FOLDERS[self.UNIT] = self.UNIT
        tc.KINDS = tuple(set(tc.KINDS) | {self.UNIT})
        try:
            self.assertEqual(obj_model.model_dir(self.UNIT), self.folder)
            arc, md, info = wt.build_with_info(game, self.UNIT)
            self.assertEqual(info['stock_meshes_retained'], 0)
            a = t.Assembly(md)
            wt.parts(a, self.UNIT)
            _shkt, _binding, overrides, _report = tc.build(game, self.UNIT, md, a.parts,
                                                           layout=wt.collision_layout(self.UNIT))
            self.assertEqual(len(overrides['car_base_wheel']), 12)
        finally:
            if old[0] is None:
                os.environ.pop('EDF6VC_MODELS', None)
            else:
                os.environ['EDF6VC_MODELS'] = old[0]
            wt.FOLDERS.clear()
            wt.FOLDERS.update(old[1])
            tc.KINDS = old[2]



# ------------------------------------------------------------------------------------------ the walkers and the infantry

class RetargetTests(unittest.TestCase):
    """pylib/xm_rig.py's pieces that need no model folder or game (CI), and the walkers' weapon lists."""

    def test_turn_takes_one_direction_onto_another(self) -> None:
        import xm_rig
        rng = np.random.default_rng(3)
        for _ in range(20):
            a, b = rng.normal(size=3), rng.normal(size=3)
            R = xm_rig._turn(a, b)
            self.assertTrue(np.allclose(R @ R.T, np.eye(3), atol=1e-9))
            self.assertAlmostEqual(float(np.linalg.det(R)), 1.0, places=9)
            self.assertTrue(np.allclose(R @ (a / np.linalg.norm(a)), b / np.linalg.norm(b), atol=1e-9))
        R = xm_rig._turn(np.array([0., 1., 0.]), np.array([0., -1., 0.]))   # opposite: half a turn
        self.assertTrue(np.allclose(R @ np.array([0., 1., 0.]), [0., -1., 0.]))

    def test_far_weight_runs_along_the_segment(self) -> None:
        import xm_rig
        h0, h1 = np.array([0., 2., 0.]), np.array([0., 0., 0.])
        pl = xm_rig.Placement(1.0, {'shin': (np.eye(3), np.zeros(3))}, {'shin': 'sune'}, {'shin': ('ashi', h0, h1)})
        w = pl.far_weight('shin', np.array([[0., 3., 0.], [0., 2., 0.], [0.3, 1., 0.], [0., 0., 0.], [0., -1., 0.]]))
        self.assertTrue(np.allclose(w, [0., 0., 0.5, 1., 1.]))
        self.assertTrue(np.allclose(pl.far_weight('other', np.zeros((2, 3))), 0.))

    def test_trim_keeps_the_bits_near_the_joint(self) -> None:
        import xm_rig
        near, _ = _box((0.1, 0., 0.), 0.1)
        far, _ = _box((1.5, 0., 0.), 0.1)
        tris = np.array([[0, 1, 2], [0, 2, 3], [4, 5, 6], [4, 6, 7]], dtype=np.int32)
        pos = np.vstack((near[:4], far[:4]))
        info = {'kind': 'k', 'parts': [{'bone': 'hand'}], 'bones': [{'name': 'hand', 'point': [0., 0., 0.]}],
                'attachments': {}}
        arrays = {'p0_pos': pos, 'p0_nrm': np.zeros_like(pos), 'p0_uv': np.zeros((8, 2)), 'p0_tri': tris}
        spec = xm_rig.Spec('', info, arrays)
        rig = xm_rig.Rig('S', 'A', 'm', ('hand', 'b'), {}, trim={'hand': 0.5})
        p, _n, _uv, t = xm_rig.trimmed(spec, rig, 0)
        self.assertEqual(len(p), 4)
        self.assertEqual(len(t), 2)
        self.assertTrue(np.allclose(p, near[:4]))
        self.assertEqual(int(t.max()), 3)
        whole = xm_rig.trimmed(spec, xm_rig.Rig('S', 'A', 'm', ('hand', 'b'), {}), 0)
        self.assertEqual(len(whole[3]), 4)

    def test_walker_weapon_lists(self) -> None:
        import call_weapons as cw
        import dsgo
        import xm_walker
        for key, w in xm_walker.WALKERS.items():
            with self.subTest(walker=key):
                paths = xm_walker.weapons(key)
                self.assertEqual(len(paths), len(xm_walker.HOST_HOLDERS[w.stock]))
                self.assertEqual(sum(1 for p in paths if p), len(w.holders))
        # the Nix's eight holders: its hands carry the unit's guns, the stock entries' params kept, the rest empty
        stock = [['app:/weapon/a.sgo', [0.5, 0.25]], [0.0], [0.0], ['app:/weapon/b.sgo', [0.1, 0.2]]]
        made = xm_walker._setup_weapons(stock, ('app:/weapon/x.sgo', 'app:/weapon/y.sgo', '', ''))
        self.assertEqual(made, [['app:/weapon/x.sgo', [0.5, 0.25]], ['app:/weapon/y.sgo', [0.5, 0.25]], [0.0], [0.0]])
        nodes = [dsgo.Node(['app:/weapon/a.sgo', dsgo.Node([0.5, 0.25])]), dsgo.Node([0.0])]
        out = cw._walker_weapons(nodes, ('', 'app:/weapon/y.sgo'))
        self.assertEqual(dsgo.to_py(dsgo.Node(out)), [[0.0], ['app:/weapon/y.sgo', [0.5, 0.25]]])
        with self.assertRaises(ValueError):
            cw._walker_weapons(nodes, ('app:/weapon/y.sgo',))


def _walkers_here() -> bool:
    import rootcpk
    import xm_rig
    return (Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file() and all(
        xm_rig.folder(k) is not None for k in ('xm_small_mech', 'xm_large_mech', 'xm_small_infantry'))


@unittest.skipUnless(_walkers_here(), 'the model pack (xm_small_mech, xm_large_mech, xm_small_infantry) or EDF6 '
                     'Root.cpk unavailable (the developer machine only)')
class WalkerAssetTests(unittest.TestCase):
    """The three folders built on their hosts and posed through the hosts' clips (docs/xmorph-port.md 「步行单位」:
    the measured gaps; a rig change that opens a joint or sinks a foot fails here)."""
    # the worst joint gap (m) and the floor range (m) over the rig's clips, as measured 2026-10-30, with a margin
    LIMITS = {'xm_small_mech': (0.4, -0.45), 'xm_large_mech': (0.4, -0.2), 'xm_small_infantry': (0.12, -0.15)}

    @classmethod
    def setUpClass(cls) -> None:
        import rootcpk
        cls.game = rootcpk.default()

    def test_poses(self) -> None:
        import xm_rig
        for key, (joint, floor) in self.LIMITS.items():
            for clip in xm_rig.RIGS[key].clips:
                with self.subTest(unit=key, clip=clip):
                    r = xm_rig.gaps(self.game, key, clips=(clip,))
                    self.assertLessEqual(max(r['joints'].values()), joint, r['joints'])
                    if clip != 'jump_start':   # the hosts' own crouch: their root goes 1.4-2.0 m down in it
                        self.assertGreaterEqual(r['floor'][0], floor, r['floor'])

    def test_walkers_build(self) -> None:
        import sgo
        import xm_walker
        for key in xm_walker.WALKERS:
            with self.subTest(unit=key):
                files = xm_walker.build(self.game, key)       # checks itself (xm_rig.check, xm_walker.check)
                self.assertEqual(len(files), 2 + len(xm_walker.WALKERS[key].holders))
                v = sgo.load(data=files[f'OBJECT/{xm_walker.vehicle_file(key)}'])
                self.assertEqual(v['animation_model'][1], sgo.load(
                    data=self.game.read('OBJECT', xm_walker.WALKERS[key].stock + '.SGO'))['animation_model'][1])

    def test_infantry_builds(self) -> None:
        import xm_infantry
        files = xm_infantry.build(self.game, 'xm_small_infantry')   # checks itself
        self.assertEqual(set(files), {f'OBJECT/{xm_infantry.model_file("xm_small_infantry")}',
                                      *(f'OBJECT/{b}' for b in xm_infantry.body_files('xm_small_infantry'))})


if __name__ == '__main__':
    unittest.main()
