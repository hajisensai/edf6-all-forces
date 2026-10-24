"""Gold coats (pylib/goldcoat.py): EDF6's metal-gold material for every gold-painted vehicle.

Offline (no game): the coat's hue band, the re-toning keeps each texel's light and dark, the legacy metal / roughness
follow EDF6's old shader, DXT1 decoding, the material / archive rewrite (shader, slots, params, texture members,
idempotent, another mod's texture left alone), and every gold call's vehicle is registered.
With EDF6 (EDF6_GAME_DIR / the default install; skipped without it): the constants are EDF6's own gold coat
(V504_BEGARUTA_GOLD), and the real Depth Crawler gold (and the EDF4.1 BM03 gold, with EDF4.1) convert to a gold,
detailed, metallic, glossy coat whose archive the game can bind.
"""
from __future__ import annotations

import json
import os
import sys
import unittest
from dataclasses import replace

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))

import numpy as np  # noqa: E402

import goldcoat  # noqa: E402
import mdb  # noqa: E402
import texfile  # noqa: E402
import texfile_accel  # noqa: E402
from mdb import MatParam, MatTex, Material, Mdb, Rab, RabFile, Texture  # noqa: E402

GAME6 = os.environ.get('EDF6_GAME_DIR') or r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'
GAME41 = os.environ.get('EDF41_DIR') or r'D:\steam\steamapps\common\Earth Defense Force 4.1'
SOURCE_GOLD = (68, 46, 0)        # the sources' coat panels (both measured: 68,46,0 and 66,48,0)


def dds(rgb: np.ndarray) -> bytes:
    h, w = rgb.shape[:2]
    rgba = np.concatenate([rgb, np.full((h, w, 1), 255, np.uint8)], -1)
    return texfile_accel.dxt1_dds(texfile.Image(w, h, bytearray(np.ascontiguousarray(rgba).tobytes())))


def coat_median(rgb: np.ndarray, weight: np.ndarray) -> np.ndarray:
    return np.median(rgb[weight > 0.9].astype(float), 0)


class Pixels(unittest.TestCase):
    def test_gold_weight_band(self) -> None:
        px = np.array([[SOURCE_GOLD, (66, 48, 0), goldcoat.GOLD_ALBEDO, (128, 128, 128), (40, 60, 160),
                        (200, 30, 30), (6, 4, 0), (20, 14, 0)]], np.uint8)
        w = goldcoat.gold_weight(px)[0]
        self.assertTrue(np.all(w[[0, 1, 2, 7]] == 1.0), w)          # coat, reference, a dark panel line
        self.assertTrue(np.all(w[[3, 4, 5, 6]] == 0.0), w)          # grey, blue, red, near black

    def test_tone_keeps_detail(self) -> None:
        img = np.zeros((8, 8, 3), np.uint8)
        img[:] = SOURCE_GOLD
        img[2, :] = (34, 23, 0)                                     # a panel line, half as bright
        img[5, :] = (120, 120, 120)                                 # a grey part
        out, y_coat = goldcoat.tone(img, goldcoat.gold_weight(img))
        self.assertEqual(tuple(out[0, 0]), goldcoat.GOLD_ALBEDO)
        ratio = goldcoat.luminance(out[2, 0]) / goldcoat.luminance(out[0, 0])
        self.assertAlmostEqual(ratio, goldcoat.luminance(img[2, 0]) / y_coat, delta=0.02)
        self.assertEqual(tuple(out[5, 0]), (120, 120, 120))
        flat = np.zeros((4, 4, 3), np.uint8) + 128
        with self.assertRaises(ValueError):
            goldcoat.tone(flat, goldcoat.gold_weight(flat))

    def test_legacy_metal_rough_is_edf6s_old_shader(self) -> None:
        reflect = np.array([[[255, 255, 0, 255], [64, 128, 0, 255], [0, 0, 0, 255]]], np.uint8)
        params = {'specular_pow': [10.0], 'fall_off_scale': [2.0], 'fall_off_offset': [0.5]}
        metal, rough = goldcoat.legacy_metal_rough(reflect, params, spec_alpha=False)
        f = reflect[0, :, 0] / 255.0
        spec = reflect[0, :, 1] / 255.0
        want = np.clip((1 - spec * 10 / 150) ** (2 * f + 3), 0.1, 1)
        np.testing.assert_allclose(metal[0], np.sqrt(f))
        np.testing.assert_allclose(rough[0], want)
        self.assertGreater(rough[0, 0], 0.7)            # specular_pow 10: the plastic look the BM03 had

    def test_decode_dxt1(self) -> None:
        img = np.zeros((8, 8, 3), np.uint8)
        img[:4] = (255, 0, 0)
        img[4:] = (0, 0, 255)
        out = goldcoat.decode_dxt1(dds(img))
        self.assertEqual(out.shape, (8, 8, 4))
        self.assertEqual(tuple(out[0, 0]), (255, 0, 0, 255))
        self.assertEqual(tuple(out[7, 7]), (0, 0, 255, 255))


def synthetic(light: bool) -> tuple[Rab, Mdb, goldcoat.Coat]:
    """A one-material archive on the legacy shader, its textures as the stock ones are stored."""
    alb = np.zeros((16, 16, 3), np.uint8)
    alb[:] = SOURCE_GOLD
    alb[:, 12:] = (110, 110, 110)
    refl = np.zeros((8, 8, 3), np.uint8)
    refl[:] = (160, 200, 0)
    refl[0:2, 0:2, 2] = 255                                         # a lamp
    files = {'a.dds': dds(alb), 'r.dds': dds(refl), 'n.dds': dds(np.full((4, 4, 3), 128, np.uint8)),
             'o.dds': dds(np.full((4, 4, 3), 200, np.uint8))}
    rab = Rab(272, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    for name, data in files.items():
        hd, lod = texfile.texture_pair(data)
        stem = name[:-4]
        mdb.insert_member(rab, RabFile(stem + '.lod.dds', 0, 0, mdb.cmpl_compress(lod)))
        mdb.insert_member(rab, RabFile(name, 2, 1, mdb.cmpl_compress(hd)))
    mdb.insert_member(rab, RabFile('m.mdb', 1, 0, b''))
    textures = [Texture(i, n[:-4] + '_dds', n, 0) for i, n in enumerate(files)]
    shader = 'snd_Mech_Light' if light else 'snd_Mech'
    params = [MatParam([1.0, 0.698, 0.0, 0.0], (0, 0), 'diffuse', 0x402),
              MatParam([1.0, 1.0, 0.0, 0.0], (0, 0), 'normal_amount', 0x201),
              MatParam([10.0, 0, 0, 0], (0, 0), 'specular_pow', 0x100),
              MatParam([0.0, 0.976, 1.0, 0.0], (0, 0), 'light_color', 0x402),
              MatParam([50.0, 0, 0, 0], (0, 0), 'fall_off_amount', 0x100),
              MatParam([2.0, 0, 0, 0], (0, 0), 'fall_off_scale', 0x100),
              MatParam([0.5, 0, 0, 0], (0, 0), 'fall_off_offset', 0x100)]
    slots = [MatTex(0, 'albedo', (0,) * 5), MatTex(2, 'normal', (0,) * 5),
             MatTex(1, 'param_reflect_spec_lightmask_hlightmask', (0,) * 5), MatTex(3, 'param_occlusion', (0,) * 5)]
    md = Mdb(0x20, ['coat'], [], [], [Material(0, 0, 0, 0, shader, params, slots, 3)], textures)
    import hashlib
    coat = goldcoat.Coat('m.mdb', 'coat', shader, 'a.dds', hashlib.sha256(files['a.dds']).hexdigest(),
                         'r.dds', hashlib.sha256(files['r.dds']).hexdigest(), 'test')
    return rab, md, coat


class Material_(unittest.TestCase):
    def setUp(self) -> None:
        self.saved = goldcoat.COATS

    def tearDown(self) -> None:
        goldcoat.COATS = self.saved

    def convert(self, light: bool) -> tuple[Rab, Mdb]:
        rab, md, coat = synthetic(light)
        goldcoat.COATS = (coat,)
        out, notes = goldcoat.apply(rab, md, 'm.mdb')
        self.assertEqual(notes, [])
        return rab, out

    def test_light_coat(self) -> None:
        rab, md = self.convert(True)
        mat = md.materials[0]
        self.assertEqual(mat.shader, goldcoat.SHADER_LIGHT)
        self.assertEqual([x.kind for x in mat.textures], ['albedo', 'normal', 'param_r_m_light', 'param_occ'])
        self.assertEqual([md.textures[x.texture].filename for x in mat.textures],
                         ['a_goldcoat.dds', 'n.dds', 'r_goldcoat.dds', 'o.dds'])
        params = {p.name: p for p in mat.params}
        self.assertEqual([p.name for p in mat.params], ['diffuse', 'specular_color', 'specular_diffuse_blend',
                                                        'light_color', 'normal_amount', 'metallic', 'roughness'])
        self.assertAlmostEqual(params['light_color'].value[1], 0.976, places=5)
        self.assertEqual((params['metallic'].type, params['roughness'].type, params['diffuse'].type), (0x100, 0x100, 0x402))
        names = [f.name for f in rab.files]
        self.assertNotIn('a.dds', names)
        self.assertNotIn('r.lod.dds', names)
        for n in ('a_goldcoat.dds', 'a_goldcoat.lod.dds', 'r_goldcoat.dds', 'r_goldcoat.lod.dds', 'o.dds', 'n.lod.dds'):
            self.assertIn(n, names)
        self.assertTrue(mdb.folder_order_ok(rab))
        self.assertLess(names.index('a_goldcoat.lod.dds'), names.index('m.mdb'))
        get = {f.name: f.data for f in rab.files}
        albedo = goldcoat.decode_dxt1(get['a_goldcoat.dds'])
        param = goldcoat.decode_dxt1(get['r_goldcoat.dds'])
        # the coat at the reference colour, metallic 0.9, roughness 0.1 (DXT1 565: within a step)
        self.assertTrue(np.all(np.abs(albedo[8, 4, :3].astype(int) - goldcoat.GOLD_ALBEDO) <= 8), albedo[8, 4])
        self.assertLessEqual(abs(int(param[8, 4, 1]) - 230), 4)
        self.assertLessEqual(abs(int(param[8, 4, 0]) - 26), 8)
        self.assertGreater(int(param[0, 0, 2]), 200)                # the lamp's mask
        self.assertGreater(int(albedo[0, 0, 0]), 240)               # its albedo white: emission = light_color
        self.assertLess(abs(int(albedo[8, 14, 0]) - int(albedo[8, 14, 2])), 9)   # grey stays grey
        self.assertGreater(int(param[8, 14, 0]), 150)               # and keeps its old (rough) roughness
        # idempotent: a converted material is left alone
        again, notes = goldcoat.apply(rab, md, 'm.mdb')
        self.assertEqual((again.materials, notes), (md.materials, []))

    def test_basic_coat(self) -> None:
        _rab, md = self.convert(False)
        mat = md.materials[0]
        self.assertEqual(mat.shader, goldcoat.SHADER_BASIC)
        self.assertEqual([x.kind for x in mat.textures], ['albedo', 'normal', 'param_r_m_xxx', 'param_occ'])
        self.assertNotIn('light_color', [p.name for p in mat.params])

    def test_foreign_texture_left_alone(self) -> None:
        rab, md, coat = synthetic(True)
        goldcoat.COATS = (replace(coat, albedo_sha256='0' * 64),)
        before = [f.name for f in rab.files]
        out, notes = goldcoat.apply(rab, md, 'm.mdb')
        self.assertEqual(len(notes), 1)
        self.assertEqual(out.materials[0].shader, 'snd_Mech_Light')
        self.assertEqual([f.name for f in rab.files], before)


def call_vehicles() -> dict[str, str]:
    """id -> vehicle SGO of every gold-named call the plugin ships."""
    templates = {'DLC_Vehicle_v504_begaruta_gold': 'v504_begaruta_gold.sgo'}
    out = {}
    for rel in ('edf41port/calls.json', 'edf5port/weapons.json'):
        with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
            rows = json.load(f)['weapons']
        for row in rows:
            text = json.dumps(row.get('text'), ensure_ascii=False)
            if not any(k in text for k in ('金色', 'ゴールド', 'Gold')):
                continue
            call = row.get('call') or {}
            vehicle = call.get('vehicle') or templates.get(row.get('template'))
            if call.get('kind', 'vehicle') == 'vehicle' and (vehicle or 'Vehicle' in row.get('template', '')):
                out[row['id']] = (vehicle or '').lower()
    return out


class Coverage(unittest.TestCase):
    def test_every_gold_call_is_registered(self) -> None:
        calls = call_vehicles()
        self.assertGreaterEqual(len(calls), 3, calls)
        for ident, vehicle in calls.items():
            self.assertIn(vehicle, goldcoat.GOLD_VEHICLES, f'{ident}: gold vehicle {vehicle!r} not in goldcoat')
        for model in filter(None, goldcoat.GOLD_VEHICLES.values()):
            self.assertTrue(goldcoat.coats_of(model), model)


def game6():
    import gamedir
    if not gamedir.is_game(GAME6):
        raise unittest.SkipTest('EDF6 not installed')
    import rootcpk
    return rootcpk.Game(GAME6)


def read41(rel: str) -> bytes:
    import rootcpk
    if not os.path.isfile(os.path.join(GAME41, 'Root.cpk')):
        raise unittest.SkipTest('EDF4.1 not installed')
    folder, name = rel.split('/')
    for archive in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'Chunk03.cpk'):
        if os.path.isfile(os.path.join(GAME41, archive)):
            try:
                return rootcpk.Game(GAME41, archive).read(folder, name)
            except KeyError:
                pass
    raise unittest.SkipTest(f'{rel} not in EDF4.1')


def model_of(rab: Rab, name: str) -> Mdb:
    return mdb.mdb_read(next(f for f in rab.files if f.name.lower() == name.lower()).data)


class Game(unittest.TestCase):
    def test_constants_are_edf6s_gold_coat(self) -> None:
        game = game6()
        rab = mdb.rab_read(game.read('OBJECT', goldcoat.REFERENCE['archive']))
        md = model_of(rab, goldcoat.REFERENCE['model'])
        mat = next(m for m in md.materials if md.name_of(m.name) == goldcoat.REFERENCE['material'])
        self.assertEqual(mat.shader, goldcoat.REFERENCE['shader'])
        p = {x.name: x.value for x in mat.params}
        self.assertAlmostEqual(p['metallic'][0], goldcoat.GOLD_METALLIC, places=5)
        self.assertAlmostEqual(p['roughness'][0], goldcoat.GOLD_ROUGHNESS, places=5)
        self.assertAlmostEqual(p['specular_diffuse_blend'][0], goldcoat.SPECULAR_DIFFUSE_BLEND, places=5)
        np.testing.assert_allclose(p['specular_color'][:3], goldcoat.SPECULAR_COLOR, atol=1e-6)
        albedo = next(f for f in rab.files if f.name == goldcoat.REFERENCE['albedo']).data
        rgb = goldcoat.decode_dxt1(albedo)[..., :3]
        self.assertEqual(tuple(coat_median(rgb, goldcoat.gold_weight(rgb))), goldcoat.GOLD_ALBEDO)
        for vehicle, model in goldcoat.GOLD_VEHICLES.items():
            import sgo
            text = json.dumps(sgo.read(game.read('OBJECT', vehicle.upper()))[1].get('animation_model'),
                              default=str) if model is None else ''
            if model is None:     # already EDF6's metal gold: its model's coat is the reference material
                self.assertIn(goldcoat.REFERENCE['model'].lower(), text.lower())

    def check_converted(self, data: bytes, coat: goldcoat.Coat) -> tuple:
        import obj_model
        before = mdb.rab_read(data)
        out, notes = goldcoat.convert_archive(data)
        self.assertEqual(notes, [])
        self.assertEqual(goldcoat.convert_archive(out)[0], out)
        rab = mdb.rab_read(out)
        self.assertEqual(mdb.rab_write(rab), out)
        md = model_of(rab, coat.model)
        # nothing new keeps a texture from binding (EDF4.1's own .lod layouts differ from EDF6's already)
        was = set(obj_model.texture_problems(before, model_of(before, coat.model), coat.model))
        self.assertEqual(set(obj_model.texture_problems(rab, md, coat.model)) - was, set())
        mat = next(m for m in md.materials if md.name_of(m.name) == coat.material)
        self.assertIn(mat.shader, (goldcoat.SHADER_BASIC, goldcoat.SHADER_LIGHT))
        get = {f.name.lower(): f.data for f in rab.files}
        files = {x.kind: md.textures[x.texture].filename.lower() for x in mat.textures}
        src = goldcoat.decode_dxt1(next(f.data for f in before.files if f.name == coat.albedo))[..., :3]
        new = goldcoat.decode_dxt1(get[files['albedo']])[..., :3]
        param = goldcoat.decode_dxt1(get[[v for k, v in files.items() if k.startswith('param_r_m')][0]])
        weight = goldcoat.gold_weight(src)
        coat_px = weight > 0.9
        self.assertGreater(coat_px.mean(), 0.2)
        # gold: the coat's median colour is the reference's (DXT1: a step), not the old flat yellow / white
        self.assertTrue(np.all(np.abs(coat_median(new, weight) - goldcoat.GOLD_ALBEDO) <= 6), coat_median(new, weight))
        # not flat: the coat's light and dark follow the source's, texel for texel
        unlit = coat_px & (param[..., 2] < 8)          # a lamp's texel goes white on purpose (its emission)
        ys, yn = goldcoat.luminance(src)[unlit], goldcoat.luminance(new)[unlit]
        self.assertGreater(np.corrcoef(ys, yn)[0, 1], 0.95)
        self.assertGreater(yn.std(), 1.0)
        # metal-gold where the coat is: metallic 0.9, roughness 0.1
        self.assertLessEqual(abs(np.median(param[..., 1][coat_px]) - 230), 4)
        self.assertLessEqual(abs(np.median(param[..., 0][coat_px]) - 26), 8)
        return rab, md, param

    def test_depth_crawler_gold(self) -> None:
        game = game6()
        coat = goldcoat.coats_of('Vehicle502_groundrobogold.mdb')[0]
        self.check_converted(game.read('OBJECT', 'VEHICLE502_GROUNDROBOGOLD.MRAB'), coat)

    def test_bm03_vegalta_gold(self) -> None:
        import legacy_assets
        rel = 'OBJECT/VEHICLE406_BEGARUTAGOLD.RAB'
        data = legacy_assets.convert(rel, read41(rel))
        coat = goldcoat.coats_of('Vehicle406_begarutagold.mdb')[0]
        _rab, _md, param = self.check_converted(data, coat)
        self.assertGreater(int(param[..., 2].max()), 200)          # its lamps' mask kept
        # the EDF4.1 install path: the gold coat first, the alias only for the other materials
        import edf41materials
        out = edf41materials.transform_models(rel, data)
        md = model_of(mdb.rab_read(out), coat.model)
        self.assertEqual([m.shader for m in md.materials], [goldcoat.SHADER_LIGHT])
        self.assertEqual(edf41materials.transform_models(rel, out), out)


if __name__ == '__main__':
    unittest.main()
