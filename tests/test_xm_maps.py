"""X-Morph: Defense's maps (docs/xmorph-maps.md): pylib/map_build.py on a synthetic scene (mapb / mapo cross references,
the RAB's members and models, the trial mission), pylib/xm_map.py's water grid; with EDF6's archives (the developer's
machine) the whole MAC / MAD from the stock template and donor, and with X-Morph too one map read into a scene.
"""
from __future__ import annotations

import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]

import numpy as np
from PIL import Image

import bigmap
import map_build
import mdb
import sgo
import xm_map


def scene() -> xm_map.Scene:
    """A ground quad (100 m, opaque texture, collision) and a small clip-textured prop placed twice."""
    sc = xm_map.Scene('XM_TEST', xm_map.MAPS['XM_STORY_JAPAN'], 'maps/test', np.zeros(3))
    sc.textures['g'] = xm_map.Tex('xmjp_t0000', Image.new('RGBA', (64, 64), (200, 100, 50, 255)), 'opaque')
    sc.textures['c'] = xm_map.Tex('xmjp_t0001', Image.new('RGBA', (32, 32), (20, 200, 50, 0)), 'clip')
    pos = np.array([[-50, 0, -50], [50, 0, -50], [50, 0, 50], [-50, 0, 50]], float)
    nrm = np.tile([0.0, 1.0, 0.0], (4, 1))
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], float)
    tris = np.array([[0, 2, 1], [0, 3, 2]])
    sc.models['xmjp_g0'] = xm_map.Model('xmjp_g0', [xm_map.Part('g', pos, nrm, uv, tris)], pos[tris], True, 'test')
    box = pos * 0.05 + [0, 2, 0]
    sc.models['xmjp_p0000'] = xm_map.Model('xmjp_p0000', [xm_map.Part('c', box, nrm, uv, tris)], box[tris], False, 'test')
    sc.placements = [xm_map.Placement('xmjp_g0', (0.0, 0.0, 0.0), 0.0),
                     xm_map.Placement('xmjp_p0000', (5.0, 0.0, 5.0), 30.0),
                     xm_map.Placement('xmjp_p0000', (-5.0, 0.0, 5.0), 0.0)]
    sc.bounds = (pos.min(0), pos.max(0))
    return sc


TEMPLATE_SGO = {'Durability': 0, 'Disappear': 1000, 'Type': '地形パーツ', 'filename': 'ig_transparentfld01.mdx',
                'xgs_scene_object_class': 'MapObject_***'}


class MapB(unittest.TestCase):
    def test_records_hit_their_defs(self) -> None:
        sc = scene()
        mb = bigmap.MapB(map_build.mapb(sc, TEMPLATE_SGO))
        self.assertEqual((mb.def_n, mb.rec_n), (2, 3))
        self.assertEqual([mb.def_name(mb.def_index(i)) for i in range(3)],
                         ['xmjp_g0.mdx', 'xmjp_p0000.mdx', 'xmjp_p0000.mdx'])
        self.assertEqual([mb.def_type(k) for k in range(2)], [map_build.GROUND_TYPE, map_build.OBJECT_TYPE])
        self.assertEqual(mb.pos(1), (5.0, 0.0, 5.0))
        self.assertEqual(struct.unpack_from('<3f', mb.record(1), 0x10), (0.0, 30.0, 0.0))
        self.assertEqual(len({mb.guid(i) for i in range(3)}), 3)
        for k, ground in ((0, True), (1, False)):
            o = mb.def_off + k * bigmap.DEF
            rel = struct.unpack_from('<i', mb.raw, o + 4)[0]
            m = sgo.plain(sgo.read(mb.raw[o + 4 + rel:])[1])
            self.assertEqual(m['filename'], mb.def_name(k))
            self.assertEqual(m['Type'], '地形パーツ' if ground else 'ビル')

    def test_empty_point_blocks_and_wire_name(self) -> None:
        b = map_build.mapb(scene(), TEMPLATE_SGO)
        h = struct.unpack_from('<32i', b, 0)
        b78, b7c = h[30], h[31]
        self.assertEqual(struct.unpack_from('<4i', b, b78), (0, 0, 0, 0))
        at = b78 + 16 + struct.unpack_from('<i', b, b78 + 16)[0]
        self.assertEqual(bigmap._wstr(b, at), map_build.WIRE_NAME)
        self.assertEqual(struct.unpack_from('<6i', b, b7c), (0, 0, 0, 0, -4, 1))
        self.assertEqual(b[h[28]:h[28] + 4], b'SGO\0')
        self.assertEqual(h[2] + 2 * h[3], len(b))


class MapO(unittest.TestCase):
    def test_per_record_entries(self) -> None:
        sc = scene()
        o = map_build.mapo(sc)
        mo = bigmap.MapO(o)
        self.assertEqual((mo.ent_n, mo.obj_n), (3, 3))
        for i, p in enumerate(sc.placements):
            self.assertEqual(mo.obj_name(i), str(i))
            e = mo.ent_off + i * bigmap.ENT
            ia = e + struct.unpack_from('<i', o, e + 4)[0]
            self.assertEqual(struct.unpack_from('<i', o, e + 8)[0], 1)
            s = ia + struct.unpack_from('<i', o, ia)[0]
            f = struct.unpack_from('<9i', o, s)
            self.assertEqual(f[0], 4)
            self.assertEqual(bigmap._wstr(o, s + f[1]), f'{p.model}.hkt')
            self.assertEqual(struct.unpack_from('<3f', o, s + f[3]), (0.0, 0.0, 0.0))
            self.assertEqual(bigmap._wstr(o, s + f[5]), f'{p.model}_hk.mdb')
            self.assertEqual(bigmap._wstr(o, s + f[8]), f'{p.model}.mdb')
        self.assertEqual(mo.pool_off + 2 * mo.pool_n, len(o))


class Rab(unittest.TestCase):
    def test_members_and_models(self) -> None:
        sc = scene()
        rab = mdb.rab_read(map_build.rab(sc, log=lambda _m: None))
        self.assertTrue(mdb.folder_order_ok(rab))
        names = {f.name for f in rab.files}
        for t in ('xmjp_t0000', 'xmjp_t0001', 'xmjp_flat_normal', 'xmjp_flat_rmo'):
            self.assertIn(f'{t}.dds', names)
            self.assertIn(f'{t}.lod.dds', names)
        self.assertIn(map_build.WIRE_TEXTURE, names)
        clip = next(f for f in rab.files if f.name == 'xmjp_t0001.dds').data
        self.assertEqual(clip[84:88], b'DXT5')
        md = mdb.mdb_read(next(f for f in rab.files if f.name == 'xmjp_p0000.mdb').data)
        self.assertEqual(len(md.bones), 1)
        self.assertEqual((md.bones[0].kind, md.bones[0].depth_delta, md.bones[0].bounded), (2, 1, 1))
        self.assertEqual(md.materials[0].shader, 'snd_BRDF_Map_Common_Clip')
        me = md.objects[0].meshes[0]
        self.assertEqual((me.vsize, me.nverts, len(me.indices) // 6), (64, 4, 2))
        self.assertEqual({t.filename for t in md.textures}, {'xmjp_t0001.dds', 'xmjp_flat_normal.dds', 'xmjp_flat_rmo.dds'})
        self.assertAlmostEqual(md.bones[0].centre[1], 2.0, places=5)

    def test_big_parts_split_at_u16(self) -> None:
        n = map_build.MAX_VERTS // 3 * 2 + 5
        tris = np.arange(n * 3).reshape(n, 3)
        runs = map_build._chunks(tris)
        self.assertEqual(len(runs), 3)
        self.assertTrue(all(len(used) <= map_build.MAX_VERTS for used, _ in runs))
        self.assertTrue(np.array_equal(np.concatenate([used[local] for used, local in runs]), tris))


class Water(unittest.TestCase):
    def test_grid_covers_the_plane_in_runs(self) -> None:
        g = np.stack(np.meshgrid(np.arange(0, 21.0), np.arange(0, 11.0)), -1).reshape(-1, 2)
        pos = np.hstack((g, np.full((len(g), 1), -3.0)))
        w = 21
        tris = np.array([[j * w + i, j * w + i + 1, (j + 1) * w + i + 1] for j in range(10) for i in range(20)])
        verts, quads = xm_map.water_grid(pos, tris)
        self.assertTrue(np.all(verts[:, 2] == -3.0))
        self.assertLessEqual(len(quads), 2 * 11)       # one run (two triangles) per row of cells
        self.assertEqual(verts[:, 0].min(), 0.0)
        self.assertGreaterEqual(verts[:, 0].max(), 20.0)


MATERIALS = '''
material base/default
{
    technique 1light
    {
        pass 0
        {
            depth_write on
        }
    }
}
material base/default_alpha_test : base/default
{
    technique 1light
    {
        pass 0
        {
            alpha_rejection greater_equal 128
            scene_blend one zero
        }
    }
}
material base/default_alpha_blend : base/default
{
    technique 1light
    {
        pass 0
        {
            scene_blend alpha_blend
        }
    }
}
material base/decal_static
{
    technique 1light
    {
        pass 0
        {
            separate_scene_blend src_alpha one_minus_src_alpha zero one
        }
    }
}
material base/unlit_additive : base/default
{
    technique 1light
    {
        pass 0
        {
            scene_blend add
        }
    }
}
material t/box : base/default
{
    set_texture_alias diffuse t/holes.png
}
material t/leaf : base/default_alpha_test
{
    set_texture_alias diffuse t/holes.png
}
material t/leaf_solid : base/default_alpha_test
{
    set_texture_alias diffuse t/solid.png
}
material t/road : base/default_alpha_blend
{
    set_texture_alias diffuse t/holes.png
}
material t/crack : base/decal_static
{
    set_texture_alias diffuse t/holes.png
}
material t/glow : base/unlit_additive
{
    set_texture_alias diffuse t/holes.png
}
'''


class FakePacks:
    """A packs tree of one material script and two textures: holes.png half see-through, solid.png not."""

    def __init__(self) -> None:
        import io
        holes = np.full((8, 8, 4), 200, np.uint8)
        holes[:4, :, 3] = 0
        self.files = {'materials/scripts/test.material': MATERIALS.encode()}
        for name, rgba in (('t/holes.png', holes), ('t/solid.png', np.full((8, 8, 4), 200, np.uint8))):
            b = io.BytesIO()
            Image.fromarray(rgba, 'RGBA').save(b, 'PNG')
            self.files[name] = b.getvalue()

    def paths(self, prefix: str = '') -> list[str]:
        return sorted(k for k in self.files if k.startswith(prefix))

    def text(self, path: str) -> str:
        return self.files[path].decode()

    def read(self, path: str) -> bytes:
        return self.files[path]

    def exists(self, path: str) -> bool:
        return path in self.files


class AlphaModes(unittest.TestCase):
    """The see-through mode follows the material's inheritance chain (alpha_rejection / scene_blend), never the
    texture's own alpha: base/default with a diffuse that has alpha holes (electric_box_01, billboard_large_01) stays
    opaque."""

    def setUp(self) -> None:
        packs = FakePacks()
        r = object.__new__(xm_map.Reader)
        r.packs = packs
        r.xm = xm_map.MAPS['XM_STORY_JAPAN']
        r.mats = xm_map.Materials(packs, None)
        r.images = xm_map.Images(packs)
        r.scene = xm_map.Scene('XM_TEST', r.xm, 'maps/test', np.zeros(3))
        r.tex_ids = {}
        self.r = r

    def mode(self, material: str) -> str:
        return self.r.scene.textures[self.r.material_tex(material)].mode

    def test_material_chain_decides(self) -> None:
        m = self.r.mats
        self.assertEqual(m.alpha('t/box'), ('opaque', 0))
        self.assertEqual(m.alpha('t/leaf'), ('clip', 128))
        self.assertEqual(m.alpha('t/road'), ('blend', 0))
        self.assertEqual(m.alpha('t/crack'), ('blend', 0))
        self.assertEqual(m.alpha('t/glow'), ('opaque', 0))

    def test_scene_modes(self) -> None:
        self.assertEqual(self.mode('t/box'), 'opaque')          # holes in the texture, opaque material
        self.assertEqual(self.mode('t/leaf'), 'clip')
        self.assertEqual(self.mode('t/road'), 'clip')           # alpha blend on a map surface: alpha-tested
        self.assertEqual(self.mode('t/crack'), 'clip')
        self.assertEqual(self.mode('t/glow'), 'opaque')
        self.assertEqual(self.mode('t/leaf_solid'), 'opaque')   # a test that rejects nothing: stored as DXT1

    def test_one_image_two_modes(self) -> None:
        box, leaf = self.r.material_tex('t/box'), self.r.material_tex('t/leaf')
        self.assertNotEqual(box, leaf)
        self.assertEqual(self.r.material_tex('t/road'), self.r.material_tex('t/crack'))


class Install(unittest.TestCase):
    """make_xm_maps.install with no usable model pack index releases what it installed before."""

    def test_no_index_releases_old_maps(self) -> None:
        import tempfile
        from unittest import mock
        import ledger
        import make_xm_maps
        with tempfile.TemporaryDirectory() as game:
            led = ledger.Ledger(game)
            old = led.put(make_xm_maps.OWNER, 'MAP/XM_STORY_JAPAN.MAC', b'old map')
            other = led.put('edf41maps', 'MAP/OTHER.MAC', b'not ours')
            for folder in (None, os.path.join(game, 'empty')):        # no model pack / no xm_maps.json in it
                with mock.patch.object(make_xm_maps.obj_model, 'model_dir', return_value=folder):
                    written, skipped = make_xm_maps.install(game, say=lambda _s: None)
                self.assertEqual(written, [])
                self.assertEqual(len(skipped), 1)
                self.assertFalse(os.path.exists(old))
                self.assertTrue(os.path.exists(other))
                self.assertEqual(ledger.Ledger(game).owned_by(make_xm_maps.OWNER), [])

    def test_unreadable_archives_release_old_maps(self) -> None:
        import tempfile
        from unittest import mock
        import ledger
        import make_xm_maps
        with tempfile.TemporaryDirectory() as game:
            pack = os.path.join(game, 'pack')
            os.makedirs(pack)
            with open(os.path.join(pack, make_xm_maps.INDEX), 'w', encoding='utf-8') as f:
                f.write('{"version": %d, "maps": {"XM_TEST": {"donor": "X", "weathers": [], "label": "", "files": {}}}}'
                        % make_xm_maps.VERSION)
            old = ledger.Ledger(game).put(make_xm_maps.OWNER, 'MAP/XM_TEST.MAC', b'old map')
            with mock.patch.object(make_xm_maps.obj_model, 'model_dir', return_value=pack):
                written, skipped = make_xm_maps.install(game, say=lambda _s: None)   # no EDF6 archives in `game`
            self.assertEqual((written, [n for n, _why in skipped]), ([], ['XM_TEST']))
            self.assertFalse(os.path.exists(old))


class Trial(unittest.TestCase):
    @unittest.skipUnless(os.path.isdir(os.environ.get('EDF6_GAME_DIR', r'd:\steam\steamapps\common\EARTH DEFENSE FORCE 6')),
                         'EDF6 not installed')
    def test_mission_moves_the_player(self) -> None:
        import make_xm_maps
        import rmpa
        game = os.environ.get('EDF6_GAME_DIR', r'd:\steam\steamapps\common\EARTH DEFENSE FORCE 6')
        entry = {'weathers': ['FINE'], 'bounds': [[0, -20, 0], [0, 10, 0]]}
        files = make_xm_maps.trial_mission('XM_TEST', entry, game)
        text = files['MISSION.AC'].decode('utf-8-sig')
        self.assertIn('Map("app:/Map/XM_TEST.mac", "fine");', text)
        p = next(p for p in rmpa.points(files['MISSION.RMPA']) if p.name == 'プレイヤー')
        self.assertEqual(p.pos, (0.0, 10 + make_xm_maps.TRIAL_DROP, 0.0))


if __name__ == '__main__':
    unittest.main()
