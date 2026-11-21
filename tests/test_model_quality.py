"""The model conversion chain's quality fixes (docs/model-quality-1110.md): the DXT1 encoder's candidates never lose to
the range fit and recover a colour line off the box's axes; auto-smoothed normals are welded and edge-connected (a
cube keeps its hard edges across a UV seam, a cylinder's side is smooth across it); a glTF material's metallic-
roughness, specular-glossiness and occlusion maps become the game's parameter-map bytes; an OBJ's `vn` normals are
kept; a model past obj_model.PRECISE_EXTENT gets float positions."""
from __future__ import annotations

import io
import os
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))

import gltf  # noqa: E402
import mesh_normals  # noqa: E402
import obj_model as om  # noqa: E402
import pbr_maps  # noqa: E402
import texfile  # noqa: E402
import texfile_accel  # noqa: E402
from mdb import Mesh, VElem  # noqa: E402


def _png(rgba: np.ndarray) -> bytes:
    from PIL import Image
    b = io.BytesIO()
    Image.fromarray(rgba.astype(np.uint8), 'RGBA').save(b, 'PNG')
    return b.getvalue()


def _range_fit_error(px: list[tuple[int, int, int]]) -> int:
    lo = [min(p[c] for p in px) for c in range(3)]
    hi = [max(p[c] for p in px) for c in range(3)]
    ax = max(range(3), key=lambda c: hi[c] - lo[c])
    return texfile._dxt1_fit(px, texfile._rgb565(*max(px, key=lambda p: p[ax])),
                             texfile._rgb565(*min(px, key=lambda p: p[ax])))[0]


def _block_error(block: bytes, px: list[tuple[int, int, int]]) -> int:
    import struct
    c0, c1, idx = struct.unpack('<HHI', block)
    e0, e1 = texfile._rgb888(c0), texfile._rgb888(c1)
    err = 0
    for i, p in enumerate(px):
        w = texfile._DXT1_WEIGHT[(idx >> (2 * i)) & 3] if c0 != c1 else 3
        err += sum((3 * p[c] - w * e0[c] - (3 - w) * e1[c]) ** 2 for c in range(3))
    return err


class Dxt1(unittest.TestCase):
    def test_never_worse_than_the_range_fit(self) -> None:
        rng = np.random.default_rng(5)
        for _ in range(300):
            base = rng.integers(0, 256, 3)
            px = [tuple(int(x) for x in np.clip(base + rng.normal(0, rng.uniform(1, 60), 3), 0, 255))
                  for _ in range(16)]
            self.assertLessEqual(_block_error(texfile._dxt1_block(px), px), _range_fit_error(px))

    def test_a_diagonal_colour_line(self) -> None:
        """Pixels along (1, -1, 0.5) with noise: the principal axis and the refits beat the box's axis by far."""
        rng = np.random.default_rng(9)
        t = rng.uniform(-1, 1, 16)
        px = [tuple(int(x) for x in np.clip(np.array((128, 128, 128)) + 70 * k * np.array((1, -1, 0.5))
                                            + rng.normal(0, 3, 3), 0, 255)) for k in t]
        new, old = _block_error(texfile._dxt1_block(px), px), _range_fit_error(px)
        self.assertLess(new, old)

    def test_numpy_is_byte_identical(self) -> None:
        rng = np.random.default_rng(3)
        img = (np.clip(rng.normal(120, 40, (24, 20, 4)), 0, 255)).astype(np.uint8)
        image = texfile.Image(20, 24, bytearray(img.tobytes()))
        self.assertEqual(texfile_accel.dxt1_dds(image), texfile.dxt1_dds(image))


def _cube(seam: bool) -> tuple[np.ndarray, np.ndarray]:
    """A unit cube's 12 triangles (outward); `seam`: each face's own four vertices (a UV / buffer split)."""
    corners = np.array([[x, y, z] for x in (0, 1) for y in (0, 1) for z in (0, 1)], dtype=np.float64)
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    P, T = [], []
    for q in quads:
        idx = []
        for c in q:
            if seam:
                P.append(corners[c])
                idx.append(len(P) - 1)
            else:
                idx.append(c)
        T += [(idx[0], idx[1], idx[2]), (idx[0], idx[2], idx[3])]
    return (np.asarray(P) if seam else corners), np.asarray(T, dtype=np.int64)


class Normals(unittest.TestCase):
    def test_cube_keeps_its_edges(self) -> None:
        for seam in (False, True):
            P, T = _cube(seam)
            n, of = mesh_normals.auto_smooth(P, T)
            fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
            fn /= np.linalg.norm(fn, axis=1, keepdims=True)
            # every corner's normal is its own face's: flat across the 90 degree edges
            self.assertTrue(np.allclose(n[of], fn[:, None, :]), f'seam {seam}')

    def test_cylinder_smooth_across_a_seam(self) -> None:
        k = 24
        a = np.linspace(0, 2 * np.pi, k + 1)         # the last column repeats the first: a UV seam
        ring = np.c_[np.cos(a), np.sin(a)]
        P = np.vstack([np.c_[ring, np.zeros(k + 1)], np.c_[ring, np.ones(k + 1)]])
        T = np.array([t for i in range(k) for t in ((i, i + 1, k + 2 + i), (i, k + 2 + i, k + 1 + i))])
        n, of = mesh_normals.auto_smooth(P, T)
        radial = P[T][..., :2] / np.linalg.norm(P[T][..., :2], axis=-1, keepdims=True)
        got = n[of]
        self.assertTrue(np.allclose(got[..., 2], 0.0, atol=1e-9))
        self.assertGreater(float(np.abs((got[..., :2] * radial).sum(-1)).min()), 0.999)

    def test_obj_vn_is_kept(self) -> None:
        import prep_wt_tracked as pw
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'm.obj')
            with open(path, 'w', encoding='utf-8') as f:
                f.write('o vehicle#x_root_1_0\nusemtl a\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n'
                        'vn 0 0 2\nvn 0 1 1\nvn 1 0 1\nf 1/1/2 2/2/3 3/3/1\n')
            src = pw.read_source(path)
            self.assertEqual(src.tri_n.tolist(), [[1, 2, 0]])
            self.assertTrue(np.allclose(np.linalg.norm(src.normals, axis=1), 1.0))
            self.assertTrue(np.allclose(src.normals[0], (0, 0, 1)))


class Surface(unittest.TestCase):
    def test_gltf_material_maps_read(self) -> None:
        """gltf._materials hands each map to pbr_maps (metallic-roughness, occlusion and its strength); a map on
        another UV set than TEXCOORD_0 is refused, not laid on the wrong texels."""
        import base64
        uri = 'data:image/png;base64,' + base64.b64encode(_png(np.full((2, 2, 4), 255))).decode()
        g = {'images': [{'uri': uri}], 'textures': [{'source': 0}],
             'materials': [{'name': 'a', 'pbrMetallicRoughness': {'metallicRoughnessTexture': {'index': 0},
                                                                   'metallicFactor': 0.0},
                            'occlusionTexture': {'index': 0, 'strength': 0.3}}]}
        (m,) = gltf._materials(g, [])
        self.assertIsNotNone(m.metal_rough)
        self.assertIsNotNone(m.occlusion)
        self.assertEqual((m.workflow, m.metallic, m.occlusion_strength), ('metal', 0.0, 0.3))
        g['materials'][0]['occlusionTexture']['texCoord'] = 1
        with self.assertRaises(gltf.GltfError):
            gltf._materials(g, [])

    def test_emissive_strength_and_texture_read(self) -> None:
        """emissiveFactor comes times KHR_materials_emissive_strength (the laser turret's emitter: (1, 0, 0) x 10);
        an emissiveTexture's image is kept (prep_glb_model refuses it: docs/xmorph-redo-1121.md)."""
        import base64
        uri = 'data:image/png;base64,' + base64.b64encode(_png(np.full((2, 2, 4), 255))).decode()
        g = {'images': [{'uri': uri}], 'textures': [{'source': 0}],
             'materials': [{'name': 'emitter', 'emissiveFactor': [1.0, 0.0, 0.0],
                            'extensions': {'KHR_materials_emissive_strength': {'emissiveStrength': 10.0}}},
                           {'name': 'textured', 'emissiveFactor': [1.0, 1.0, 1.0], 'emissiveTexture': {'index': 0}}]}
        a, b = gltf._materials(g, [])
        self.assertEqual((a.emissive, a.emissive_texture), ((10.0, 0.0, 0.0), None))
        self.assertEqual(b.emissive, (1.0, 1.0, 1.0))
        self.assertIsNotNone(b.emissive_texture)

    def test_glowing_material_on_the_light_template(self) -> None:
        """prep_glb_model._emissive: a glowing material takes the light template (wt_tracked LIGHT), its whole surface
        the mask, its glow per unit of albedo the factor over the albedo's linear mean; a glow the albedo cannot carry
        (a channel it has none of, an emissiveTexture's own colours) is refused."""
        from PIL import Image
        import prep_glb_model as glb
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'textures'))
            red = gltf.Material('emitter', (1.0, 0.0, 0.0, 1.0), (10.0, 0.0, 0.0))
            albedo = glb._plain(red, d, 'e')
            got = glb._emissive(red, d, 'e', albedo)
            self.assertEqual(got, {'template': 'light', 'emissive': 'textures/e_emissive.png', 'light': [10.0, 0.0, 0.0]})
            with Image.open(os.path.join(d, got['emissive'])) as im:
                self.assertEqual(im.getextrema(), (255, 255))
            self.assertEqual(glb._emissive(gltf.Material('dark', (1, 1, 1, 1), (0.0, 0.0, 0.0)), d, 'e', albedo), {})
            with self.assertRaises(ValueError):
                glb._emissive(gltf.Material('green glow', (1, 0, 0, 1), (0.0, 1.0, 0.0)), d, 'e', albedo)
            with self.assertRaises(ValueError):
                glb._emissive(gltf.Material('tex', (1, 0, 0, 1), (1.0, 0.0, 0.0), emissive_texture=b'png'), d, 'e', albedo)

    def test_metallic_roughness_map_and_factors(self) -> None:
        mr = np.zeros((4, 4, 4), dtype=np.uint8)
        mr[..., 1], mr[..., 2], mr[..., 3] = 200, 100, 255
        m = gltf.Material('m', (1, 1, 1, 1), (0, 0, 0), metallic=0.5, roughness=1.0, metal_rough=_png(mr))
        rm = pbr_maps.roughness_metal(m)
        self.assertEqual((int(rm[0, 0, 0]), int(rm[0, 0, 1])), (200, 50))
        flat = pbr_maps.roughness_metal(gltf.Material('f', (1, 1, 1, 1), (0, 0, 0), metallic=0.0, roughness=0.6))
        self.assertEqual((int(flat[0, 0, 0]), int(flat[0, 0, 1])), (153, 0))

    def test_specular_glossiness(self) -> None:
        sg = np.zeros((2, 2, 4), dtype=np.uint8)
        sg[..., 3] = 255
        m = gltf.Material('s', (1, 1, 1, 1), (0, 0, 0), workflow='specular', spec_gloss=_png(sg), glossiness=0.4)
        rm = pbr_maps.roughness_metal(m)
        self.assertEqual((int(rm[0, 0, 0]), int(rm[0, 0, 1])), (153, 0))

    def test_occlusion_strength_and_floor(self) -> None:
        occ = np.zeros((2, 2, 4), dtype=np.uint8)
        occ[..., 3] = 255
        m = gltf.Material('o', (1, 1, 1, 1), (0, 0, 0), occlusion=_png(occ), occlusion_strength=0.5)
        self.assertEqual(int(pbr_maps.occlusion(m, floor=False)[0, 0]), 128)
        self.assertEqual(int(pbr_maps.occlusion(m)[0, 0]), 182)     # 110 + 127.5 x 145 / 255


class Positions(unittest.TestCase):
    def test_layout_widens_past_the_extent(self) -> None:
        elems = [VElem(7, 0, 0, 'position'), VElem(7, 8, 0, 'normal'), VElem(12, 16, 0, 'texcoord')]
        mesh = Mesh(bytes(4), 0, 0, 24, elems, 0, b'', b'')
        self.assertIs(om.position_layout(mesh, om.PRECISE_EXTENT), mesh)
        wide = om.position_layout(mesh, om.PRECISE_EXTENT + 1)
        self.assertEqual((wide.vsize, [(e.fmt, e.offset) for e in wide.elems]), (32, [(1, 0), (7, 16), (12, 24)]))

    def test_one_model_one_position_format(self) -> None:
        """A model built a material at a time: a material near the origin still stores floats when the model reaches
        past the extent, so a vertex it shares with a far one is the same point in both (half: 3.9 mm off at 12 m)."""
        import struct
        layout = [VElem(7, 0, 0, 'BINORMAL'), VElem(7, 8, 0, 'TANGENT'), VElem(7, 16, 0, 'NORMAL'),
                  VElem(7, 24, 0, 'POSITION'), VElem(12, 32, 0, 'TEXCOORD'), VElem(1, 40, 0, 'BLENDWEIGHT'),
                  VElem(21, 56, 0, 'BLENDINDICES')]
        template = Mesh(bytes(4), 0, 0, 60, layout, 0, b'', b'')
        shared = (12.0013, 1.0, 0.0)

        def tri(name: str, far: float) -> om.Part:
            vs = [om.Vertex(p, (0.0, 0.0, 1.0), (0.0, 0.0)) for p in (shared, (far, 1.0, 0.0), (far, 2.0, 0.0))]
            return om.Part(name, name, vs, [(0, 1, 2)])

        near, wide = tri('glass', 11.0), tri('hull', 20.0)
        extent = om.reach([near, wide])
        meshes = [om.build_meshes(template, [(p, om.rigid(p, 3))], material=k, extent=extent)[0]
                  for k, p in enumerate((near, wide))]
        firsts = []
        for me in meshes:
            pos = next(e for e in me.elems if e.name.lower() == 'position')
            self.assertEqual(pos.fmt, 1)
            firsts.append(struct.unpack_from('<3f', me.vdata, pos.offset))
        self.assertEqual(firsts[0], firsts[1])
        # the near material alone (no model extent) would have stored halves
        alone = om.build_meshes(template, [(near, om.rigid(near, 3))], material=0, extent=0.0)[0]
        self.assertEqual(next(e.fmt for e in alone.elems if e.name.lower() == 'position'), 7)


def _net(size: int = 64) -> np.ndarray:
    """An RGBA net: strands (A 255) every 8 texels, 2 wide, holes (A 0) between, a colour gradient under both."""
    y, x = np.mgrid[:size, :size]
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = x * 4 % 256, y * 4 % 256, 90
    rgba[..., 3] = np.where((x % 8 < 2) | (y % 8 < 2), 255, 0)
    return rgba


class Alpha(unittest.TestCase):
    """docs/model-quality-1121-alpha.md: model.json 'alpha', DXT5, the Clip / Alpha hosts."""

    def test_classify(self) -> None:
        import alpha_maps as am
        net = _net()[..., 3]
        glass = np.full((16, 16), 133, np.uint8)
        self.assertEqual(am.classify(net), {'mode': 'mask', 'cutoff': 0.5})
        self.assertEqual(am.classify(glass), {'mode': 'blend', 'factor': 1.0})
        self.assertEqual(am.classify(glass, 0.15), {'mode': 'blend', 'factor': 0.15})     # War Thunder's glass, d 0.15
        self.assertEqual(am.classify(None, 0.4), {'mode': 'blend', 'factor': 0.4})
        self.assertIsNone(am.classify(np.full((8, 8), 255, np.uint8)))      # a map_d that hides nothing (mg_pkt)
        self.assertIsNone(am.classify(None))
        self.assertIsNone(am.classify(net, declared='OPAQUE'))               # glTF: alpha ignored
        self.assertEqual(am.classify(glass, declared='MASK', cutoff=0.3), {'mode': 'mask', 'cutoff': 0.3})
        self.assertEqual(am.classify(net, declared='BLEND'), {'mode': 'mask', 'cutoff': 0.5})   # Sketchfab's BLEND net
        with self.assertRaises(am.AlphaError):
            am.classify(net, declared='CUTOUT')

    def test_entry_contract(self) -> None:
        import alpha_maps as am
        self.assertEqual(am.entry(None)['mode'], 'opaque')
        self.assertEqual(am.entry({'mode': 'mask'}), {'mode': 'mask', 'cutoff': 0.5, 'factor': 1.0, 'map': None})
        self.assertEqual(am.entry({'mode': 'blend', 'factor': 0.2, 'map': 'textures/a.png'})['map'], 'textures/a.png')
        for bad in ({'mode': 'clip'}, {'mode': 'mask', 'factor': 0.5}, {'mode': 'blend', 'cutoff': 0.5},
                    {'mode': 'mask', 'cutoff': 2}, {'mode': 'opaque', 'map': 'x.png'}, {'mode': 'mask', 'x': 1}):
            with self.subTest(bad=bad), self.assertRaises(am.AlphaError):
                am.entry(bad)

    def test_gltf_alpha(self) -> None:
        import alpha_maps as am
        tex = _png(_net())
        self.assertEqual(am.of_gltf(gltf.Material('n', (1, 1, 1, 1), (0, 0, 0), texture=tex, alpha_mode='BLEND')),
                         {'mode': 'mask', 'cutoff': 0.5})
        self.assertEqual(am.of_gltf(gltf.Material('m', (1, 1, 1, 1), (0, 0, 0), texture=tex, alpha_mode='MASK',
                                                  alpha_cutoff=0.25)), {'mode': 'mask', 'cutoff': 0.25})
        self.assertEqual(am.of_gltf(gltf.Material('g', (1, 1, 1, 0.3), (0, 0, 0), alpha_mode='BLEND')),
                         {'mode': 'blend', 'factor': 0.3})
        self.assertIsNone(am.of_gltf(gltf.Material('o', (1, 1, 1, 0.3), (0, 0, 0), texture=tex)))   # OPAQUE

    def test_dxt5_blocks_and_layout(self) -> None:
        rgba = _net(64)
        img = texfile.Image(64, 64, bytearray(rgba.tobytes()))
        dds = texfile_accel.dxt5_dds(img)
        info = texfile.dds_info(dds)
        self.assertEqual((info.fourcc, info.width, info.mips), (b'DXT5', 64, 7))
        hd, lod = texfile.texture_pair(dds)
        self.assertIsNone(texfile.pair_problem(hd, lod))
        top = dds[128:128 + 16 * 16 * 16]
        dec = texfile_accel.decode_dxt5(top, 64, 64)
        self.assertTrue(np.array_equal(dec[..., 3], rgba[..., 3]))          # a two-value block's BC4 is exact
        # the colour half is the opaque path's DXT1 block of the same texels
        colour = np.frombuffer(top, np.uint8).reshape(-1, 16)[:, 8:].tobytes()
        self.assertEqual(colour, texfile_accel._level(rgba))
        # colour0 >= colour1 everywhere: the four-colour palette a DXT5 decoder always uses
        c = np.frombuffer(colour, np.dtype([('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')]))
        self.assertTrue(((c['c0'] > c['c1']) | ((c['c0'] == c['c1']) & (c['i'] == 0))).all())

    def test_mask_mips_keep_their_coverage(self) -> None:
        # an irregular cut-out (a camouflage net's leaves: random blobs a few texels wide, 45 % kept); a regular grid's
        # mips have only a few coverage steps to choose from
        rng = np.random.default_rng(3)
        field = np.kron(rng.random((64, 64)), np.ones((4, 4)))
        field = sum(np.roll(np.roll(field, dy, 0), dx, 1) for dy in range(-2, 3) for dx in range(-2, 3))
        rgba = _net(256)
        rgba[..., 3] = np.where(field > np.quantile(field, 0.55), 255, 0)
        img = texfile.Image(256, 256, bytearray(rgba.tobytes()))
        top = texfile_accel.alpha_coverage(rgba[..., 3], 0.5)

        def coverages(dds: bytes) -> list[float]:
            out, at, w = [], 128, 256
            while w >= 16:
                n = (w // 4) ** 2 * 16
                out.append(texfile_accel.alpha_coverage(texfile_accel.decode_dxt5(dds[at:at + n], w, w)[..., 3], 0.5))
                at, w = at + n, w // 2
            return out
        kept = coverages(texfile_accel.dxt5_dds(img, 0.5))
        boxed = coverages(texfile_accel.dxt5_dds(img))
        self.assertTrue(all(abs(k - top) < 0.05 for k in kept), kept)
        self.assertLess(min(boxed), top - 0.05)      # the plain box filter thins the cut-out away (why the scale)

    def test_hosts_by_alpha_mode(self) -> None:
        import wt_tracked as wt
        body = {'name': 'b', 'template': 'v603_body', 'albedo': 'a.png'}
        self.assertEqual((wt.template(body), wt.layout_template(body)), ('v603_body', 'v603_body'))
        net = {**body, 'alpha': {'mode': 'mask'}}
        glass = {**body, 'alpha': {'mode': 'blend', 'factor': 0.15}}
        track = {**body, 'template': 'v603_kyata_L', 'alpha': {'mode': 'mask', 'cutoff': 0.5}}
        self.assertEqual((wt.template(net), wt.layout_template(net)), (wt.CLIP, 'v603_body'))
        self.assertEqual((wt.template(glass), wt.layout_template(glass)), (wt.ALPHA, 'v603_body'))
        self.assertEqual((wt.template(track), wt.layout_template(track)), ('v603_kyata_L', 'v603_kyata_L'))
        for bad in ({**track, 'alpha': {'mode': 'blend'}}, {**net, 'template': wt.LIGHT}):
            with self.subTest(bad=bad), self.assertRaises(wt.WtModelError):
                wt.template(bad)

    def test_albedo_dds_carries_the_alpha_map_and_swaps_track_colours(self) -> None:
        import wt_tracked as wt
        from PIL import Image
        rgba = _net(32)
        with tempfile.TemporaryDirectory() as d:
            Image.fromarray(rgba[..., :3], 'RGB').save(os.path.join(d, 'c.png'))
            Image.fromarray(rgba[..., 3], 'L').save(os.path.join(d, 'a.png'))
            m = {'name': 'n', 'template': 'v603_body', 'albedo': 'c.png'}
            self.assertEqual(texfile.dds_info(wt.albedo_dds(d, m)).fourcc, b'DXT1')     # opaque: as before
            dds = wt.albedo_dds(d, {**m, 'alpha': {'mode': 'mask', 'map': 'a.png'}})
            dec = texfile_accel.decode_dxt5(dds[128:], 32, 32)
            self.assertTrue(np.array_equal(dec[..., 3], rgba[..., 3]))
            swapped = texfile_accel.decode_dxt1(wt.albedo_dds(d, m, swap_gb=True)[128:], 32, 32)
            plain = texfile_accel.decode_dxt1(wt.albedo_dds(d, m)[128:], 32, 32)
            self.assertLess(np.abs(swapped[..., 1] - rgba[..., 2]).mean(), np.abs(plain[..., 1] - rgba[..., 2]).mean())

    def test_wt_obj_map_d(self) -> None:
        import prep_wt_tracked as prep
        from PIL import Image
        with tempfile.TemporaryDirectory() as d:
            Image.fromarray(_net(16)[..., 3], 'L').save(os.path.join(d, 'net_a.png'))
            Image.fromarray(np.full((16, 16), 133, np.uint8), 'L').save(os.path.join(d, 'glass_a.png'))
            src = prep.Source(os.path.join(d, 'x.obj'), np.zeros((0, 3)), np.zeros((0, 2)), np.zeros((0, 3), int),
                              np.zeros((0, 3), int), np.zeros(0, int), np.zeros(0, int), [], ['net', 'glass', 'body'],
                              {'net': {'map_d': 'net_a.png'}, 'glass': {'d': '0.15', 'map_d': 'glass_a.png'},
                               'body': {'map_Kd': 'c.jpg'}})
            self.assertEqual(prep.alpha_of(src, 'net'), {'mode': 'mask', 'cutoff': 0.5})
            self.assertEqual(prep.alpha_of(src, 'glass'), {'mode': 'blend', 'factor': 0.15})
            self.assertIsNone(prep.alpha_of(src, 'body'))


if __name__ == '__main__':
    unittest.main()
