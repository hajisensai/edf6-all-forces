"""pylib/mesh_simplify.py and pylib/model_lod.py on synthetic meshes (no game files): the collapses keep rows, borders,
seams and bones; they are deterministic; meshes merge and split at the u16 limit without losing a triangle; a LOD
keeps the base model's tables and box (docs/model-lod.md)."""
from __future__ import annotations

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'pylib'))
import mesh_simplify as ms  # noqa: E402
import model_lod  # noqa: E402
from mdb import Material, Mdb, Mesh, Object, VElem  # noqa: E402

ELEMS = [VElem(4, 0, 0, 'position'), VElem(4, 12, 0, 'normal'), VElem(12, 24, 0, 'texcoord'),
         VElem(21, 32, 0, 'BLENDINDICES'), VElem(1, 36, 0, 'BLENDWEIGHT')]
VSIZE = 52


def sheet(n: int = 40, curve: float = 0.05):
    """A bent n x n sheet over [0, 1]^2: a UV seam down x = 0.5 (rows split, other UVs on the right), the left half on
    bone 1 and the right on bone 2 (so the seam is also where the bones meet). (points, normals, uv, bone, tris)."""
    xs = np.linspace(0, 1, n + 1)
    pts, uv, bone, idx = [], [], [], {}
    for side in (0, 1):
        for j, y in enumerate(xs):
            for i, x in enumerate(xs):
                if (side == 0 and x > 0.5 + 1e-9) or (side == 1 and x < 0.5 - 1e-9):
                    continue
                idx[side, i, j] = len(pts)
                pts.append((x, y, curve * np.sin(3 * x) * np.cos(2 * y)))
                uv.append((x, y) if side == 0 else (x + 3.0, y))
                bone.append(1 + side)
    tris = []
    for j in range(n):
        for i in range(n):
            side = 0 if xs[i] < 0.5 - 1e-9 else 1
            a, b, c, d = idx[side, i, j], idx[side, i + 1, j], idx[side, i + 1, j + 1], idx[side, i, j + 1]
            tris += [(a, b, c), (a, c, d)]
    pts = np.array(pts, dtype=np.float64)
    nrm = np.tile((0.0, 0.0, 1.0), (len(pts), 1))
    return pts, nrm, np.array(uv), np.array(bone), np.array(tris, dtype=np.int64)


def rows_of(pts, nrm, uv, bone) -> np.ndarray:
    out = np.zeros((len(pts), VSIZE), dtype=np.uint8)
    out[:, 0:12] = pts.astype('<f4').view(np.uint8).reshape(len(pts), 12)
    out[:, 12:24] = nrm.astype('<f4').view(np.uint8).reshape(len(pts), 12)
    out[:, 24:32] = uv.astype('<f4').view(np.uint8).reshape(len(pts), 8)
    out[:, 32] = bone
    out[:, 36:52] = np.tile(np.array((1, 0, 0, 0), '<f4').view(np.uint8), (len(pts), 1))
    return out


def mesh(rows: np.ndarray, tris: np.ndarray, material: int = 0) -> Mesh:
    return Mesh(bytes((0, 1, 1, 0)), material, 0, VSIZE, list(ELEMS), 0, rows.tobytes(),
                tris.astype('<u2').tobytes())


def model(meshes: list[Mesh], materials: int = 1) -> Mdb:
    names = ['obj'] + [f'mat{k}' for k in range(materials)]
    mats = [Material(k, 0, 0, 1 + k, 'snd_x', [], [], 3) for k in range(materials)]
    return Mdb(0, names, [], [Object(0, 0, meshes)], mats, [])


def sphere(n: int = 60) -> tuple[np.ndarray, np.ndarray]:
    th, ph = np.linspace(0.05, np.pi - 0.05, n), np.linspace(0, 2 * np.pi, 2 * n, endpoint=False)
    T, P = np.meshgrid(th, ph, indexing='ij')
    pts = np.stack([np.sin(T) * np.cos(P), np.cos(T), np.sin(T) * np.sin(P)], -1).reshape(-1, 3)
    idx = np.arange(n * 2 * n).reshape(n, 2 * n)
    a, b = idx[:-1], idx[1:]
    c, d = np.roll(b, -1, axis=1), np.roll(a, -1, axis=1)
    tris = np.concatenate([np.stack([a, b, c], -1).reshape(-1, 3), np.stack([a, c, d], -1).reshape(-1, 3)])
    return pts, tris


class Simplify(unittest.TestCase):
    def test_rows_borders_seams_bones(self) -> None:
        pts, nrm, uv, bone, tris = sheet()
        out = ms.simplify(pts, tris, bone, len(tris) // 4, 0.01)
        self.assertLess(len(out), len(tris) // 2)
        self.assertTrue(set(np.unique(out)) <= set(range(len(pts))))          # only rows that were there
        self.assertTrue((bone[out] == bone[out][:, :1]).all())                 # no triangle across two bones
        left = uv[out][:, :, 0] < 2
        self.assertTrue((left == left[:, :1]).all())                           # no triangle across the UV seam
        used = pts[np.unique(out)]
        np.testing.assert_allclose(used.min(0)[:2], (0, 0))                    # the outline: the corners stay
        np.testing.assert_allclose(used.max(0)[:2], (1, 1))
        # the seam's two sides keep the same positions (no crack)
        seam = lambda s: {tuple(p) for p in np.round(pts[np.unique(out[bone[out] == s])], 9) if abs(p[0] - 0.5) < 1e-9}  # noqa: E731
        self.assertEqual(seam(1), seam(2))

    def test_deterministic_and_bounded(self) -> None:
        pts, tris = sphere()
        a = ms.simplify(pts, tris, np.zeros(len(pts)), len(tris) // 3, 0.05)
        b = ms.simplify(pts, tris, np.zeros(len(pts)), len(tris) // 3, 0.05)
        np.testing.assert_array_equal(a, b)
        self.assertLessEqual(len(a), len(tris) // 3 + 2)
        tight = ms.simplify(pts, tris, np.zeros(len(pts)), 0, 1e-6)          # an error bound it cannot pass
        self.assertGreater(len(tight), len(tris) * 0.9)

    def test_creases_collapse(self) -> None:
        """Rows that differ only in their normal (attrs equal) are a crease the collapses may cross: a sheet of
        faceted (per-face normal) quads still simplifies, and every row keeps a normal of its own facet's side."""
        pts, _n, uv, _b, tris = sheet(curve=0.0)
        # every triangle its own rows (fully faceted): three rows per face, one normal each, same UV and bone
        P = pts[tris].reshape(-1, 3)
        t = np.arange(len(P)).reshape(-1, 3)
        attrs = np.unique(np.round(P, 9), axis=0, return_inverse=True)[1].reshape(-1)
        nrm = np.tile((0.0, 0.0, 1.0), (len(P), 1))
        out_strict = ms.simplify(P, t, np.zeros(len(P)), len(t) // 4, 0.01)
        out = ms.simplify(P, t, np.zeros(len(P)), len(t) // 4, 0.01, nrm, attrs)
        self.assertGreater(len(out_strict), 0.99 * len(t))                   # each row a seam: (almost) nothing moves
        self.assertLess(len(out), len(t) // 2)

    def test_drop_small(self) -> None:
        pts, tris = sphere(10)
        small = pts * 0.01 + 5
        both = np.vstack((pts, small))
        t = np.vstack((tris, tris + len(pts)))
        kept = ms.drop_small(both, t, 0.1)
        self.assertEqual(len(kept), len(tris))
        self.assertTrue((kept < len(pts)).all())


class Pack(unittest.TestCase):
    def test_split_at_u16(self) -> None:
        n = 70000                                    # loose triangles: 210000 vertices, at least four meshes
        pts = np.random.default_rng(1).random((3 * n, 3))
        rows = rows_of(pts, np.tile((0.0, 0.0, 1.0), (3 * n, 1)), np.zeros((3 * n, 2)), np.ones(3 * n, dtype=int))
        tris = np.arange(3 * n).reshape(-1, 3)
        meshes = model_lod.pack_rows(mesh(rows[:3], tris[:1]), rows, tris)
        self.assertEqual(len(meshes), 4)
        self.assertEqual(sum(len(m.indices) // 6 for m in meshes), n)
        for m in meshes:
            ix = np.frombuffer(m.indices, '<u2')
            self.assertLessEqual(m.nverts, model_lod.MAX_VERTS)
            self.assertLess(int(ix.max()), m.nverts)

    def test_merge_keeps_triangles(self) -> None:
        pts, nrm, uv, bone, tris = sheet(10)
        rows = rows_of(pts, nrm, uv, bone)
        half = len(tris) // 2
        md = model([mesh(rows, tris[:half]), mesh(rows, tris[half:]), mesh(rows, tris[:5], 1)], materials=2)
        merged = model_lod.merge_meshes(md)
        self.assertEqual(len(merged.objects[0].meshes), 2)

        def faces(m: Mdb) -> list:
            out = []
            for me in m.objects[0].meshes:
                r = np.frombuffer(me.vdata, np.uint8).reshape(-1, VSIZE)
                for t in np.frombuffer(me.indices, '<u2').reshape(-1, 3):
                    out.append((me.material, b''.join(r[i].tobytes() for i in t)))
            return sorted(out)
        self.assertEqual(faces(md), faces(merged))


class Lods(unittest.TestCase):
    def test_lods_keep_tables_and_box(self) -> None:
        pts, tris = sphere(80)
        nrm = pts / np.linalg.norm(pts, axis=1, keepdims=True)
        uv = np.stack([np.arctan2(pts[:, 2], pts[:, 0]), pts[:, 1]], -1)
        rows = rows_of(pts, nrm, uv, np.ones(len(pts), dtype=int))
        md = model(model_lod.pack_rows(mesh(rows[:3], tris[:1]), rows, tris))
        levels = model_lod.lods(md)
        self.assertEqual(len(levels), 2)
        self.assertEqual(model_lod.check_lods(md, levels), [])
        counts = [model_lod.triangles(m) for m, _d in levels]
        self.assertLess(counts[0], 0.5 * len(tris))
        self.assertLess(counts[1], counts[0])
        r = model_lod.radius(md)
        self.assertEqual([round(d, 6) for _m, d in levels], [round(lv.reach * r, 6) for lv in model_lod.LEVELS])
        # a LOD that drops below the base model's box past the tolerance is caught
        squashed = [(model(model_lod.pack_rows(mesh(rows[:3], tris[:1]), rows, tris[: len(tris) // 3])), 10.0)]
        self.assertTrue(any('shrank' in b for b in model_lod.check_lods(md, squashed)))


if __name__ == '__main__':
    unittest.main()
