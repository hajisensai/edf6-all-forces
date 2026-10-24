"""Gold coats (pylib/goldcoat.py): EDF6's metal-gold material for every gold-painted model.

Offline (no game): the coat's hue band, the re-toning keeps each texel's light and dark (a darker paint stays darker),
the legacy metal / roughness follow EDF6's old shaders, DXT1 / DXT5 decoding, the material / archive rewrite for each
legacy family (shader, slots, params, texture members, two coats on one texture, idempotent, another mod's texture
left alone), the stock gold weapons' private archives and SGO redirects (new files, the stock path restorable), the
gold models built without cockpit assets (no eye marker: the seats keep the stock camera, fp_vehicle.cpp), and every
gold call's vehicle is registered.
With EDF6 (EDF6_GAME_DIR / the default install; skipped without it): the constants are EDF6's own gold coat
(V504_BEGARUTA_GOLD) and paint; discover() over the game (and EDF4.1's ported gold archives, with EDF4.1) finds
exactly COATS; every coat converts to a gold, detailed, metallic, glossy coat whose archive the game can bind.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
import sys
import unittest
from dataclasses import replace
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import numpy as np  # noqa: E402

import goldcoat  # noqa: E402
import mdb  # noqa: E402
import texfile  # noqa: E402
import texfile_accel  # noqa: E402
from mdb import MatParam, MatTex, Material, Mdb, Rab, RabFile, Texture  # noqa: E402

GAME6 = os.environ.get('EDF6_GAME_DIR') or r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'
GAME41 = os.environ.get('EDF41_DIR') or r'D:\steam\steamapps\common\Earth Defense Force 4.1'
SOURCE_GOLD = (68, 46, 0)        # the sources' coat panels (measured: 68,46,0 and 66,48,0)
LIGHT, MECH, BASIC = (goldcoat.LEGACY[k] for k in ('SND_MECH_LIGHT', 'SND_MECH', 'SND_COMMON_BASIC'))


def dds(rgb: np.ndarray) -> bytes:
    h, w = rgb.shape[:2]
    rgba = np.concatenate([rgb, np.full((h, w, 1), 255, np.uint8)], -1)
    return texfile_accel.dxt1_dds(texfile.Image(w, h, bytearray(np.ascontiguousarray(rgba).tobytes())))


def coat_median(rgb: np.ndarray, weight: np.ndarray) -> np.ndarray:
    return np.median(rgb[weight > 0.9].astype(float), 0)


class Pixels(unittest.TestCase):
    def test_gold_weight_band(self) -> None:
        px = np.array([[SOURCE_GOLD, (66, 48, 0), goldcoat.GOLD_ALBEDO, (74, 67, 35), (128, 128, 128), (40, 60, 160),
                        (200, 30, 30), (6, 4, 0), (20, 14, 0)]], np.uint8)
        w = goldcoat.gold_weight(px)[0]
        self.assertTrue(np.all(w[[0, 1, 2, 3, 8]] == 1.0), w)       # coats, the reference, the Balam's, a panel line
        self.assertTrue(np.all(w[[4, 5, 6, 7]] == 0.0), w)          # grey, blue, red, near black

    def test_tone_keeps_detail(self) -> None:
        img = np.zeros((8, 8, 3), np.uint8)
        img[:] = SOURCE_GOLD
        img[2, :] = (34, 23, 0)                                     # a panel line, half as bright
        img[5, :] = (120, 120, 120)                                 # a grey part
        out, y_coat = goldcoat.tone(img, goldcoat.gold_weight(img))
        self.assertTrue(np.all(np.abs(out[0, 0].astype(int) - goldcoat.GOLD_ALBEDO) <= 4), out[0, 0])   # 68,46,0: a shade under the paint
        ratio = goldcoat.luminance(out[2, 0]) / goldcoat.luminance(out[0, 0])
        self.assertAlmostEqual(ratio, goldcoat.luminance(img[2, 0]) / goldcoat.luminance(img[0, 0]), delta=0.03)
        self.assertEqual(tuple(out[5, 0]), (120, 120, 120))
        flat = np.zeros((4, 4, 3), np.uint8) + 128
        with self.assertRaises(ValueError):
            goldcoat.tone(flat, goldcoat.gold_weight(flat))

    def test_darker_paint_stays_darker(self) -> None:
        dark = np.zeros((4, 4, 3), np.uint8) + np.array((33, 24, 0), np.uint8)   # the gatling barrels' paint
        out, y_coat = goldcoat.tone(dark, goldcoat.gold_weight(dark))
        self.assertEqual(y_coat, goldcoat.PAINT_LUMINANCE)
        self.assertAlmostEqual(goldcoat.luminance(out[0, 0]) / goldcoat.luminance(goldcoat.GOLD_ALBEDO), 0.5, delta=0.04)

    def test_legacy_metal_rough_is_edf6s_old_shader(self) -> None:
        reflect = np.array([[[255, 255, 0, 255], [64, 128, 0, 255], [0, 0, 0, 255]]], np.uint8)
        params = {'specular_pow': [10.0], 'fall_off_scale': [2.0], 'fall_off_offset': [0.5]}
        metal, rough = goldcoat.legacy_metal_rough(reflect, params, spec_alpha=False)
        f = reflect[0, :, 0] / 255.0
        spec = reflect[0, :, 1] / 255.0
        np.testing.assert_allclose(metal[0], np.sqrt(f))
        np.testing.assert_allclose(rough[0], np.clip((1 - spec * 10 / 150) ** (2 * f + 3), 0.1, 1))
        self.assertGreater(rough[0, 0], 0.7)            # specular_pow 10: the plastic look the BM03 had

    def test_decode_dxt1_dxt5(self) -> None:
        from PIL import Image
        rng = np.random.default_rng(7)
        img = rng.integers(0, 255, (16, 16, 3), dtype=np.uint8)
        data = dds(img)
        np.testing.assert_array_equal(goldcoat.decode_dds(data), np.asarray(Image.open(io.BytesIO(data)).convert('RGBA')))
        # DXT5: one block, colour c0 > c1 forced 4-colour, alpha 8-step table
        head = bytearray(texfile._header(4, 4, 1, b'DXT5', 0, (0, 0, 0, 0), 16))
        alpha = bytes([200, 40]) + (0b001_000 | (0b111 << 6)).to_bytes(6, 'little')
        colour = (0xF800).to_bytes(2, 'little') + (0x001F).to_bytes(2, 'little') + (0b11_10_01_00).to_bytes(4, 'little')
        five = bytes(head) + alpha + colour
        mine = goldcoat.decode_dds(five)
        pil = np.asarray(Image.open(io.BytesIO(five)).convert('RGBA'))
        np.testing.assert_array_equal(mine, pil)


def synthetic(legacy: goldcoat.Legacy, materials: int = 1) -> tuple[Rab, Mdb, list[goldcoat.Coat]]:
    """An archive on a legacy shader (one or two materials on the same textures), stored as the stock ones are."""
    alb = np.zeros((16, 16, 3), np.uint8)
    alb[:] = SOURCE_GOLD
    alb[:, 12:] = (110, 110, 110)
    refl = np.zeros((8, 8, 3), np.uint8)
    refl[:] = (160, 200, 90 if legacy.occ_in_param else 0)
    refl[0:2, 0:2, 2] = 255                                         # a lamp (or the occlusion's light texels)
    files = {'a.dds': dds(alb), 'r.dds': dds(refl), 'n.dds': dds(np.full((4, 4, 3), 128, np.uint8)),
             'o.dds': dds(np.full((4, 4, 3), 200, np.uint8))}
    rab = Rab(272, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    for name, data in files.items():
        hd, lod = texfile.texture_pair(data)
        mdb.insert_member(rab, RabFile(name[:-4] + '.lod.dds', 0, 0, mdb.cmpl_compress(lod)))
        mdb.insert_member(rab, RabFile(name, 2, 1, mdb.cmpl_compress(hd)))
    mdb.insert_member(rab, RabFile('m.mdb', 1, 0, b''))
    textures = [Texture(i, n[:-4] + '_dds', n, 0) for i, n in enumerate(files)]
    shader = next(k for k, v in goldcoat.LEGACY.items() if v == legacy).lower()
    params = [MatParam([1.0, 0.698, 0.0, 0.0], (0, 0), 'diffuse', 0x402),
              MatParam([1.0, 1.0, 0.0, 0.0], (0, 0), 'normal_amount', 0x201),
              MatParam([10.0, 0, 0, 0], (0, 0), 'specular_pow', 0x100),
              MatParam([0.0, 0.976, 1.0, 0.0], (0, 0), 'light_color', 0x402),
              MatParam([50.0, 0, 0, 0], (0, 0), 'fall_off_amount', 0x100),
              MatParam([2.0, 0, 0, 0], (0, 0), 'fall_off_scale', 0x100),
              MatParam([0.5, 0, 0, 0], (0, 0), 'fall_off_offset', 0x100)]
    slots = [MatTex(0, 'albedo', (0,) * 5), MatTex(2, 'normal', (0,) * 5),
             MatTex(1, 'param_reflect_spec_occ' if legacy.occ_in_param else 'param_reflect_spec_lightmask_hlightmask',
                    (0,) * 5)]
    if not legacy.occ_in_param:
        slots.append(MatTex(3, 'param_occlusion', (0,) * 5))
    names = [f'coat{i}' for i in range(materials)]
    mats = [Material(i, 0, 0, i, shader, list(params), list(slots), 3) for i in range(materials)]
    md = Mdb(0x20, names, [], [], mats, textures)
    coats = [goldcoat.Coat('m.mdb', n, shader, 'a.dds', hashlib.sha256(files['a.dds']).hexdigest(),
                           'r.dds', hashlib.sha256(files['r.dds']).hexdigest(), 'test') for n in names]
    return rab, md, coats


class Material_(unittest.TestCase):
    def setUp(self) -> None:
        self.saved = goldcoat.COATS

    def tearDown(self) -> None:
        goldcoat.COATS = self.saved

    def convert(self, legacy: goldcoat.Legacy, materials: int = 1) -> tuple[Rab, Mdb]:
        rab, md, coats = synthetic(legacy, materials)
        goldcoat.COATS = tuple(coats)
        out, notes = goldcoat.apply(rab, md, 'm.mdb')
        self.assertEqual(notes, [])
        return rab, out

    def maps(self, rab: Rab, md: Mdb, k: int = 0) -> tuple[np.ndarray, np.ndarray]:
        get = {f.name: f.data for f in rab.files}
        mat = md.materials[k]
        files = [md.textures[x.texture].filename for x in mat.textures]
        return goldcoat.decode_dds(get[files[0]]), goldcoat.decode_dds(get[files[2]])

    def check_coat(self, albedo: np.ndarray, param: np.ndarray) -> None:
        # the coat at the reference colour, metallic 0.9, roughness 0.1 (DXT1 565: within a step)
        self.assertTrue(np.all(np.abs(albedo[8, 4, :3].astype(int) - goldcoat.GOLD_ALBEDO) <= 8), albedo[8, 4])
        self.assertLessEqual(abs(int(param[8, 4, 1]) - 230), 4)
        self.assertLessEqual(abs(int(param[8, 4, 0]) - 26), 8)
        self.assertLess(abs(int(albedo[8, 14, 0]) - int(albedo[8, 14, 2])), 9)   # grey stays grey
        self.assertGreater(int(param[8, 14, 0]), 150)               # and keeps its old (rough) roughness

    def test_light_coat(self) -> None:
        rab, md = self.convert(LIGHT)
        mat = md.materials[0]
        self.assertEqual(mat.shader, LIGHT.target)
        self.assertEqual([x.kind for x in mat.textures], ['albedo', 'normal', 'param_r_m_light', 'param_occ'])
        self.assertEqual([md.textures[x.texture].filename for x in mat.textures],
                         ['a_goldcoat.dds', 'n.dds', 'r_goldcoat.dds', 'o.dds'])
        self.assertEqual([p.name for p in mat.params], ['diffuse', 'specular_color', 'specular_diffuse_blend',
                                                        'light_color', 'normal_amount', 'metallic', 'roughness'])
        params = {p.name: p for p in mat.params}
        self.assertAlmostEqual(params['light_color'].value[1], 0.976, places=5)
        self.assertEqual((params['metallic'].type, params['roughness'].type, params['diffuse'].type), (0x100, 0x100, 0x402))
        names = [f.name for f in rab.files]
        for gone in ('a.dds', 'a.lod.dds', 'r.dds', 'r.lod.dds'):
            self.assertNotIn(gone, names)
        for n in ('a_goldcoat.dds', 'a_goldcoat.lod.dds', 'r_goldcoat.dds', 'r_goldcoat.lod.dds', 'o.dds', 'n.lod.dds'):
            self.assertIn(n, names)
        self.assertEqual([t.index for t in md.textures], list(range(len(md.textures))))
        self.assertTrue(mdb.folder_order_ok(rab))
        self.assertLess(names.index('a_goldcoat.lod.dds'), names.index('m.mdb'))
        albedo, param = self.maps(rab, md)
        self.check_coat(albedo, param)
        self.assertGreater(int(param[0, 0, 2]), 200)                # the lamp's mask
        self.assertGreater(int(albedo[0, 0, 0]), 240)               # its albedo white: emission = light_color
        again, notes = goldcoat.apply(rab, md, 'm.mdb')             # idempotent: a converted material is left alone
        self.assertEqual((again.materials, notes), (md.materials, []))

    def test_mech_coat(self) -> None:
        rab, md = self.convert(MECH)
        mat = md.materials[0]
        self.assertEqual(mat.shader, MECH.target)
        self.assertEqual([x.kind for x in mat.textures], ['albedo', 'normal', 'param_r_m_xxx', 'param_occ'])
        self.assertNotIn('light_color', [p.name for p in mat.params])
        self.check_coat(*self.maps(rab, md))

    def test_common_basic_coat_keeps_occlusion_in_param(self) -> None:
        rab, md = self.convert(BASIC)
        mat = md.materials[0]
        self.assertEqual(mat.shader, 'snd_BRDF_Common_Basic')
        self.assertEqual([x.kind for x in mat.textures], ['albedo', 'normal', 'param_r_m_occ_hr'])
        albedo, param = self.maps(rab, md)
        self.check_coat(albedo, param)
        self.assertLessEqual(abs(int(param[8, 4, 2]) - 90), 4)      # the old occlusion (reflect B)
        self.assertEqual(int(param[8, 4, 3]), 255)                  # hr: roughness scale 1
        self.assertGreater(int(param[0, 0, 2]), 245)

    def test_two_coats_share_one_texture(self) -> None:
        rab, md = self.convert(MECH, materials=2)
        a, b = md.materials
        self.assertEqual([x.texture for x in a.textures], [x.texture for x in b.textures])
        files = sorted(t.filename for t in md.textures)
        self.assertEqual(files, ['a_goldcoat.dds', 'n.dds', 'o.dds', 'r_goldcoat.dds'])
        self.assertEqual(len([f for f in rab.files if 'goldcoat' in f.name]), 4)

    def test_foreign_texture_left_alone(self) -> None:
        rab, md, coats = synthetic(LIGHT)
        goldcoat.COATS = (replace(coats[0], albedo_sha256='0' * 64),)
        before = [f.name for f in rab.files]
        out, notes = goldcoat.apply(rab, md, 'm.mdb')
        self.assertEqual(len(notes), 1)
        self.assertEqual(out.materials[0].shader, 'snd_mech_light')
        self.assertEqual([f.name for f in rab.files], before)


class StockModels(unittest.TestCase):
    """The stock gold archives go through vehicle_optics / make_optics: private outputs, restorable SGO redirects."""
    def test_every_stock_coat_has_a_builder(self) -> None:
        import vehicle_optics as optics
        stems = {s.stem.lower(): s for s in optics.MODELS}
        for coat in goldcoat.COATS:
            if coat.where.startswith('edf41:'):
                continue
            folder, name = coat.where.split('/')
            spec = stems.get(name.rsplit('.', 1)[0].lower())
            self.assertIsNotNone(spec, coat.where)
            self.assertEqual((spec.folder, spec.stem + spec.ext), (folder, name))
            self.assertTrue(optics.ready(spec))                      # gold: built with or without cockpit assets
            self.assertTrue(optics.output(spec).startswith('EDF6VC_'))
            self.assertNotEqual(optics.output(spec).upper(), name.upper())   # a new file, never the stock one

    def test_weapon_sgo_redirect_and_restore(self) -> None:
        import vehicle_optics as optics
        stock = 'app:/Weapon/Vehicle502_groundrobogold_Cannon_l.rab'
        import sgo
        data = sgo.write(1, {'animation_model': [[stock, 'Vehicle502_groundrobogold_Cannon_l.mdb'], 0.0, b'MAB'],
                             'custom': 5})
        made, needs = optics.redirect(data)
        self.assertEqual(needs, ('WEAPON/EDF6VC_GOLD_VEHICLE502_GROUNDROBOGOLD_CANNON_L.RAB',))
        self.assertEqual(optics.model_path(made), 'app:/weapon/edf6vc_gold_vehicle502_groundrobogold_cannon_l.rab')
        self.assertEqual(sgo.read(made)[1]['custom'], 5)
        self.assertEqual(optics.model_path(optics.restore_path(made, stock)), stock)
        self.assertEqual(optics.redirect(made), (made, needs))       # idempotent

    def test_stock_weapon_sgos_are_found_and_install_restores(self) -> None:
        import tempfile
        from pathlib import Path
        import ledger
        import make_optics
        import sgo
        import vehicle_optics as optics
        weapon = sgo.write(1, {'animation_model': [['app:/Weapon/Vehicle502_groundrobogold_gatling.rab',
                                                     'Vehicle502_groundrobogold_gatling.mdb'], 'x.cas', b'MAB']})
        other = sgo.write(1, {'animation_model': [['app:/Weapon/other.rab', 'other.mdb'], 'x.cas', b'MAB']})
        payloads = {'WEAPON': {'V_502_GROUNDROBOGOLD_GATLING.SGO': weapon, 'V_OTHER.SGO': other}, 'OBJECT': {}}

        class Source:
            def __init__(self, root): self.root = root
            def names(self, folder): return list(payloads[folder])
            def read(self, folder, name): return payloads[folder][name]
        with tempfile.TemporaryDirectory(prefix='edf-gold-test-') as root:
            Path(root, 'Mods', 'WEAPON').mkdir(parents=True)
            with mock.patch.object(make_optics.rootcpk, 'Game', Source):
                made = make_optics.build_stock_redirects(root)
            self.assertEqual(set(made), {'WEAPON/V_502_GROUNDROBOGOLD_GATLING.SGO'})
            model = 'WEAPON/EDF6VC_GOLD_VEHICLE502_GROUNDROBOGOLD_GATLING.RAB'
            make_optics.install(root, {**made, model: b'gold model'})
            sgo_path = Path(root, 'Mods', 'WEAPON', 'V_502_GROUNDROBOGOLD_GATLING.SGO')
            self.assertEqual(optics.model_path(sgo_path.read_bytes()), 'app:/weapon/edf6vc_gold_vehicle502_groundrobogold_gatling.rab')
            self.assertIn('optics', ledger.Ledger(root).owners(model))
            make_optics.remove(root)
            self.assertFalse(sgo_path.exists())                     # no stock file was there: none is left
            self.assertFalse(Path(root, 'Mods', *model.split('/')).exists())

    def test_seats_keep_stock_camera_without_cockpit_marker(self) -> None:
        """Without cockpit assets a gold model is still built and redirected; it has no vc_eye_ marker, so the
        plugin's VehicleEye finds none and the seat keeps the stock camera (src/fp_vehicle.cpp)."""
        with open(os.path.join(ROOT, 'src', 'fp_vehicle.cpp'), encoding='utf-8') as f:
            src = f.read()
        self.assertRegex(src, r'swprintf\(marker,16,L"vc_eye_%d",out->seat\);\s*const unsigned char\* rec=Bone\('
                              r'v\+kVehicleModel,marker\);\s*if\(!rec\)return false;')
        with open(os.path.join(ROOT, 'src', 'firstperson.cpp'), encoding='utf-8') as f:
            fp = f.read()
        self.assertIn('if(!VehicleEye(h,eye,&ride))return "no cockpit in this seat";', fp)


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

    def test_registry_is_consistent(self) -> None:
        for coat in goldcoat.COATS:
            self.assertIn(goldcoat._legacy_name(coat.shader), goldcoat.LEGACY, coat)
            self.assertRegex(coat.albedo_sha256 + coat.reflect_sha256, r'^[0-9a-f]{128}$')
        for (folder, stem, ext), digest in goldcoat.STOCK_WEAPONS.items():
            self.assertTrue(any(c.where == f'{folder}/{stem}{ext}' for c in goldcoat.COATS), stem)
            self.assertRegex(digest, r'^[0-9a-f]{64}$')


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


def registered() -> set[tuple[str, str, str]]:
    return {(c.where.split(':')[-1], c.model.lower(), c.material) for c in goldcoat.COATS}


class Game(unittest.TestCase):
    def test_constants_are_edf6s_gold_coat_and_paint(self) -> None:
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
        rgb = goldcoat.decode_dds(next(f for f in rab.files if f.name == goldcoat.REFERENCE['albedo']).data)[..., :3]
        self.assertEqual(tuple(coat_median(rgb, goldcoat.gold_weight(rgb))), goldcoat.GOLD_ALBEDO)
        crawler = mdb.rab_read(game.read('OBJECT', 'VEHICLE502_GROUNDROBOGOLD.MRAB'))
        paint = goldcoat.decode_dds(next(f for f in crawler.files if f.name == 'Vehicle502_groundrobogold_df.dds').data)
        y = goldcoat.luminance(coat_median(paint[..., :3], goldcoat.gold_weight(paint[..., :3])))
        self.assertAlmostEqual(y, goldcoat.PAINT_LUMINANCE, delta=0.5)
        import sgo
        for vehicle, model in goldcoat.GOLD_VEHICLES.items():
            if model is None:     # already EDF6's metal gold: its model is the reference
                text = json.dumps(sgo.load(data=game.read('OBJECT', vehicle.upper())), default=str).lower()
                self.assertIn(goldcoat.REFERENCE['model'].lower(), text)

    def test_discovery_finds_exactly_the_registered_coats(self) -> None:
        """Any gold model on a whitening shader the game ships (and the EDF4.1 port brings) must be in COATS."""
        found = {(w, m.lower(), mat) for w, m, mat, _shader, _share in goldcoat.discover(game6())}
        stock = {r for r in registered() if not r[0].startswith('OBJECT/VEHICLE406')}
        self.assertEqual(found, stock)

    def test_edf41_port_discovery(self) -> None:
        import legacy_assets
        with open(os.path.join(ROOT, 'edf41port', 'objects.json'), encoding='utf-8') as f:
            files = json.load(f)['files']
        found = set()
        for rel in files:
            if 'GOLD' in rel.upper() and rel.upper().endswith(('.RAB', '.MRAB')):
                data = legacy_assets.convert(rel, read41(rel))
                found |= {(w, m.lower(), mat) for w, m, mat, _s, _c in goldcoat.discover_archive(rel.upper(), data)}
        self.assertEqual(found, {r for r in registered() if r[0].startswith('OBJECT/VEHICLE406')})

    def check_converted(self, data: bytes, model: str) -> tuple:
        import obj_model
        before = mdb.rab_read(data)
        out, notes = goldcoat.convert_archive(data)
        self.assertEqual(notes, [])
        self.assertEqual(goldcoat.convert_archive(out)[0], out)
        rab = mdb.rab_read(out)
        self.assertEqual(mdb.rab_write(rab), out)
        md = model_of(rab, model)
        was = set(obj_model.texture_problems(before, model_of(before, model), model))
        self.assertEqual(set(obj_model.texture_problems(rab, md, model)) - was, set())
        get = {f.name.lower(): f.data for f in rab.files}
        params = []
        for coat in goldcoat.coats_of(model):
            mat = next(m for m in md.materials if md.name_of(m.name) == coat.material)
            legacy = goldcoat.LEGACY[goldcoat._legacy_name(coat.shader)]
            self.assertEqual(mat.shader, legacy.target)
            files = {x.kind: md.textures[x.texture].filename.lower() for x in mat.textures}
            src = goldcoat.decode_dds(next(f.data for f in before.files if f.name == coat.albedo))[..., :3]
            new = goldcoat.decode_dds(get[files['albedo']])[..., :3]
            param = goldcoat.decode_dds(get[files[legacy.param_kind]])
            weight = goldcoat.gold_weight(src)
            coat_px = weight > 0.9
            self.assertGreater(coat_px.mean(), goldcoat.COAT_SHARE)
            unlit = coat_px & ((param[..., 2] < 8) if legacy.light else True)   # a lamp's texel goes white on purpose
            y_src, y_new = goldcoat.luminance(src)[unlit], goldcoat.luminance(new)[unlit]
            # gold: the coat's colour is the reference's (scaled by its paint's brightness), not flat yellow / white
            scale = max(float(np.median(y_src)), goldcoat.PAINT_LUMINANCE)
            want = np.asarray(goldcoat.GOLD_ALBEDO) * float(np.median(y_src)) / scale
            self.assertTrue(np.all(np.abs(coat_median(new, weight) - want) <= 6), (coat_median(new, weight), want))
            # not flat: the coat's light and dark follow the source's, texel for texel
            self.assertGreater(np.corrcoef(y_src, y_new)[0, 1], 0.95)
            self.assertGreater(y_new.std(), 1.0)
            # metal-gold where the coat is: metallic 0.9, roughness 0.1
            self.assertLessEqual(abs(np.median(param[..., 1][coat_px]) - 230), 4)
            self.assertLessEqual(abs(np.median(param[..., 0][coat_px]) - 26), 8)
            params.append(param)
        return rab, md, params

    def test_stock_coats(self) -> None:
        game = game6()
        for where in sorted({c.where for c in goldcoat.COATS if not c.where.startswith('edf41:')}):
            folder, name = where.split('/')
            with self.subTest(where):
                rab = mdb.rab_read(game.read(folder, name))
                model = next(f.name for f in rab.files if f.name.lower().endswith('.mdb') and goldcoat.coats_of(f.name))
                self.check_converted(game.read(folder, name), model)

    def test_stock_models_build_without_cockpit_assets(self) -> None:
        """The gold vehicle built with no cockpit assets: gold, and no vc_eye_ marker (the seats' stock camera)."""
        game = game6()
        import cockpits
        import vehicle_optics as optics
        spec = next(s for s in optics.MODELS if s.stem == 'VEHICLE502_GROUNDROBOGOLD')
        missing = cockpits.CockpitAssetsMissing('test: no cockpit assets')
        with mock.patch.object(cockpits, 'asset_root', side_effect=missing), \
                mock.patch.object(cockpits, 'graft', side_effect=missing):
            self.assertTrue(optics.ready(spec))
            data = optics.build_model(game, spec)
        rab = mdb.rab_read(data)
        md = model_of(rab, 'Vehicle502_groundrobogold.mdb')
        names = [md.name_of(b.name) for b in md.bones]
        self.assertFalse([n for n in names if n.startswith(('vc_eye_', 'vc_optic_'))])
        stock = model_of(mdb.rab_read(game.read('OBJECT', 'VEHICLE502_GROUNDROBOGOLD.MRAB')), 'Vehicle502_groundrobogold.mdb')
        self.assertEqual(sorted(names), sorted(stock.name_of(b.name) for b in stock.bones))
        mat = next(m for m in md.materials if md.name_of(m.name) == 'groundrobo')
        self.assertEqual(mat.shader, goldcoat.LEGACY['SND_MECH'].target)
        weapon = next(s for s in optics.MODELS if s.stem == 'VEHICLE502_GROUNDROBOGOLD_CANNON_L')
        built = model_of(mdb.rab_read(optics.build_model(game, weapon)), 'Vehicle502_groundrobogold_Cannon_l.mdb')
        self.assertEqual(built.materials[0].shader, 'snd_BRDF_Common_Basic')

    def test_bm03_vegalta_gold(self) -> None:
        import legacy_assets
        rel = 'OBJECT/VEHICLE406_BEGARUTAGOLD.RAB'
        data = legacy_assets.convert(rel, read41(rel))
        _rab, _md, params = self.check_converted(data, 'Vehicle406_begarutagold.mdb')
        self.assertGreater(int(params[0][..., 2].max()), 200)      # its lamps' mask kept
        import edf41materials                                       # the EDF4.1 install path: the gold coat first
        out = edf41materials.transform_models(rel, data)
        md = model_of(mdb.rab_read(out), 'Vehicle406_begarutagold.mdb')
        self.assertEqual([m.shader for m in md.materials], [goldcoat.LEGACY['SND_MECH_LIGHT'].target])
        self.assertEqual(edf41materials.transform_models(rel, out), out)


if __name__ == '__main__':
    unittest.main()
