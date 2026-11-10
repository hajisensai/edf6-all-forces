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


if __name__ == '__main__':
    unittest.main()
