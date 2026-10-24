"""pylib/hkshape.py. The first class needs no game files; the second checks the rules against every stock shape of the
player's Root.cpk (skipped without the game): the packed vectors bit for bit, the 82 compressed meshes of convex
shapes written again object for object, their computed data and trees, the compounds' derived data, the hulls rebuilt
from their vertices and their mass properties."""
from __future__ import annotations

import math
import os
import random
import statistics
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import gamedir  # noqa: E402
import hkfile  # noqa: E402
import hkshape as hs  # noqa: E402

CUBE = [(x, y, z) for x in (-1.0, 1.0) for y in (-2.0, 2.0) for z in (-0.5, 0.5)]


class Portable(unittest.TestCase):
    def test_box_hull(self) -> None:
        h = hs.build_hull(CUBE + [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)])     # inner and face points are not vertices
        self.assertEqual(sorted(h.vertices), sorted(CUBE))
        self.assertEqual(len(h.faces), 6)
        self.assertEqual(hs.hull_problems(h), [])
        self.assertAlmostEqual(h.min_width(), 1.0)
        self.assertTrue(all(f[2] == 127 for f in h.faces), h.faces)       # 90 degrees: half 45 -> 127
        mp = hs.from_hull(h, 0.0)
        self.assertAlmostEqual(mp.volume, 8.0, places=9)
        self.assertAlmostEqual(mp.mass, 8000.0, places=6)
        want = (mp.mass / 12 * (16 + 1), mp.mass / 12 * (4 + 1), mp.mass / 12 * (4 + 16))
        for a, b in zip(sorted(mp.moments), sorted(want)):
            self.assertAlmostEqual(a, b, places=6)
        r = hs.from_hull(h, 0.1)
        self.assertAlmostEqual(r.volume, 8 + 28 * 0.1 + 7 * math.pi * 0.01 + 4 / 3 * math.pi * 0.001, places=6)

    def test_random_hulls(self) -> None:
        rng = random.Random(7)
        for n in (4, 9, 40, 300):
            pts = [(rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)) for _ in range(n)]
            h = hs.build_hull(pts)
            self.assertEqual(hs.hull_problems(h), [], n)
            for p in pts:
                self.assertTrue(all(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] <= 1e-5 for pl in h.planes))
        ball = [(math.cos(a) * math.sin(b), math.sin(a) * math.sin(b), math.cos(b))
                for a in [k * 0.1 for k in range(63)] for b in [k * 0.1 + 0.05 for k in range(31)]]
        with self.assertRaises(hs.HullError):
            hs.build_hull(ball)                   # > 252 vertices
        self.assertGreater(len(hs.hull_vertices(ball)), hs.MAX_VERTICES)
        with self.assertRaises(hs.HullError):
            hs.build_hull([(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)])    # flat

    def test_packing(self) -> None:
        self.assertEqual(hs.pack_vec3((0.0, 0.0, 0.0)), (0, 0, 0, 0x1980))
        for v in ((1.0, -2.5, 3.25), (1e-3, 2e-3, -7e-4), (12345.0, -0.5, 9.0)):
            got = hs.unpack_vec3(hs.pack_vec3(v))
            m = max(abs(c) for c in v)
            self.assertTrue(all(abs(a - b) <= m * 2 ** -14 for a, b in zip(got, v)), (v, got))
        self.assertEqual(hs.pack_quat(hs.CMS_AXES), (0x8000, 0x8000, 0x8000, 0xF52F))
        self.assertEqual(hs.pack_quat((0.0, 0.0, 0.0, 1.0)), (0x8000, 0x8000, 0x8000, 0xF530))
        mp = hs.box_mass((0.0, 0.0, 0.0), (2.0, 4.0, 6.0), 100.0)
        self.assertEqual(mp.volume, 48.0)
        self.assertEqual(mp.com, (1.0, 2.0, 3.0))
        self.assertAlmostEqual(mp.moments[0], 1.5 * 100 / 12 * (16 + 36))
        b = hs.mass_bytes(mp)
        self.assertEqual(len(b), 32)
        self.assertEqual(struct.unpack_from('<2f', b, 24), (100.0, 48.0))

    def test_codecs(self) -> None:
        for lo, hi, mn, mx in ((0.0, 10.0, 2.0, 7.0), (-3.0, 3.0, -3.0, 3.0), (1.0, 2.0, 1.5, 1.5)):
            b = hs.aabb4_encode(lo, hi, mn, mx)
            dlo, dhi = hs.aabb4_decode(lo, hi, b)
            self.assertLessEqual(dlo, mn)
            self.assertGreaterEqual(dhi, mx)
        lo, hi = hs.f32_out((0.1, -0.1, 1.0), (0.3, 0.7, 2.0))
        self.assertTrue(lo[0] <= 0.1 and lo[1] <= -0.1 and hi[0] >= 0.3 and hi[1] >= 0.7 and lo[2] == 1.0)

    def test_eigen(self) -> None:
        q = (0.2, -0.3, 0.1, math.sqrt(1 - 0.14))
        r = hs.quat_matrix(q)
        d = [[3.0, 0, 0], [0, 5.0, 0], [0, 0, 7.0]]
        t = hs._mm(hs._mm(r, d), hs._tr(r))
        mp = hs.MassProperties.from_tensor(1.0, 1.0, (0, 0, 0), t)
        back = mp.tensor()
        self.assertTrue(all(abs(back[i][j] - t[i][j]) < 1e-9 for i in range(3) for j in range(3)))
        self.assertEqual(sorted(round(x, 9) for x in mp.moments), [3.0, 5.0, 7.0])

    def _file(self):  # noqa: ANN202 - (tagfile, cube shape)
        import frozen_collision_probe
        raw, _ = frozen_collision_probe._sample()
        tf = hkfile.read(raw)
        return tf, hkfile.Physics(tf).bodies()[0].shape

    def test_affine_convex(self) -> None:
        tf, cube = self._file()
        new = hs.affine_convex(tf, cube, [[0, -2, 0], [3, 0, 0], [0, 0, 1]], (1.0, 2.0, 3.0))
        radius, verts, planes = hs.convex_hull(new)
        self.assertAlmostEqual(radius, 0.05, places=6)                      # x the smallest singular value (1)
        self.assertEqual(sorted(verts), sorted((-2 * y + 1, 3 * x + 2, z + 3) for x, y, z in hs.convex_hull(cube)[1]))
        self.assertEqual(hs.hull_problems(hs.read_hull(new)), [])
        mp = hs.read_mass(hs.mass_properties(new))
        self.assertAlmostEqual(mp.volume, hs.from_hull(hs.read_hull(new), radius).volume, delta=1e-3)
        with self.assertRaises(hs.HullError):
            hs.affine_convex(tf, cube, [[-1, 0, 0], [0, 1, 0], [0, 0, 1]], (0, 0, 0))     # a mirror

    def test_compound_and_mesh(self) -> None:
        import shkt_check as sc
        tf, cube = self._file()
        a = hs.affine_convex(tf, cube, [[1, 0, 0], [0, 1, 0], [0, 0, 1]], (5.0, 0.0, 0.0))
        q = (0.0, math.sin(0.3), 0.0, math.cos(0.3))
        comp = hs.write_compound(tf, [((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0), cube), (q, (0.0, 3.0, 0.0), a)])
        boxes = [hs.instance_box(i) for i in hs.instances(comp)]
        (lo, hi), nodes = hs.compound_bounds(comp)
        self.assertTrue(all(lo[k] <= b[0][k] and hi[k] >= b[1][k] for b in boxes for k in range(3)))
        self.assertEqual(comp.value['numShapeKeyBits'].int(), 2)
        self.assertEqual([e['flags'].int() for e in hs.instance_array(comp)], [0x40, 0x46])
        mesh = hs.write_convex_mesh(tf, [cube, a], 50.0)
        mp = hs.read_mass(hs.mass_properties(mesh))
        self.assertEqual(struct.unpack_from('<4H', hs.mass_properties(mesh).data, 16), (0x8000, 0x8000, 0x8000, 0xF52F))
        self.assertAlmostEqual(mp.mass, 50.0)
        body = hkfile.Physics(tf).bodies()
        body[0].set_shape(comp)
        body[1].set_shape(mesh)
        out = tf.write()
        self.assertEqual([p for p in sc.problems(out) if p.severity == 'error'], [])


GAME = gamedir.find()


@unittest.skipUnless(GAME, 'needs the game (Root.cpk)')
class Stock(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import rootcpk
        g = rootcpk.Game(GAME)
        cls.files = []
        for name in g.names('OBJECT'):
            if name.upper().endswith('.SHKT'):
                d = g.read('OBJECT', name)
                if d[4:8] == b'TAG0':
                    cls.files.append((name, hkfile.read(d)))

    def blocks(self, kind: str):  # noqa: ANN201
        for name, tf in self.files:
            for b in tf.blocks():
                if b.type.full_name == kind:
                    yield name, tf, b

    def test_packed_vectors_bit_exact(self) -> None:
        n = 0
        for name, _, b in self.blocks('hknpShapeMassProperties'):
            for at in (0, 8):
                w = struct.unpack_from('<4H', b.data, at)
                self.assertEqual(hs.pack_vec3(hs.unpack_vec3(w)), w, name)
                n += 1
            q = struct.unpack_from('<4H', b.data, 16)
            self.assertEqual(hs.pack_quat(hs.unpack_quat(q)), q)
        self.assertGreater(n, 20000)

    def test_convex_meshes(self) -> None:
        """All 82: written again from what they store, content equal; computed from their externs: the domain to
        rounding, the key bits, the box mass rule (centre to a word, axes exact, inertia 2e-3, volume 1e-5); the
        stored section trees re-encode axis by axis with the single precision codec."""
        n = axes = 0
        for name, tf, b in self.blocks('hknpCompressedMeshShape'):
            if b.value['numTriangles'].int() or not b.value['numConvexShapes'].int():
                continue
            n += 1
            ext = [i[3] for i in hs.instances(b)]
            stored = hs.read_mass(hs.mass_properties(b))
            lay = hs.convex_mesh_layout(b)
            again = hs.write_convex_mesh(tf, ext, stored.mass, keep=b, layout=lay)
            self.assertTrue(hkfile.content_equal(b, again), name)
            mine = hs.write_convex_mesh(tf, ext, stored.mass, keep=b, placement=lay['placement'])
            tree, tree0 = mine.value['data'].target.value['meshTree'], b.value['data'].target.value['meshTree']
            d, d0 = tree['domain']['max'].vec(3), tree0['domain']['max'].vec(3)
            e, e0 = tree['domain']['min'].vec(3), tree0['domain']['min'].vec(3)
            tol = 1e-6 * max(1.0, max(abs(x) for x in d0 + e0))
            self.assertTrue(all(abs(x - y) <= tol for x, y in zip(d + e, d0 + e0)), (name, d, d0, e, e0))
            self.assertEqual(mine.value['numShapeKeyBits'].int(), b.value['numShapeKeyBits'].int())
            mw = hs.mass_properties(mine).data
            sw = hs.mass_properties(b).data
            self.assertTrue(all(abs(x - y) <= 1 for x, y in zip(struct.unpack_from('<3h', mw, 0),
                                                                   struct.unpack_from('<3h', sw, 0))), name)
            self.assertEqual(mw[16:24], sw[16:24], name)
            mp = hs.read_mass(hs.mass_properties(mine))
            self.assertLess(max(abs(x - y) for x, y in zip(mp.moments, stored.moments)), 2e-3 * max(stored.moments))
            self.assertLess(abs(mp.volume - stored.volume), 1e-5 * stored.volume)
            axes += self._reencode(b, ext, lay)
        self.assertEqual(n, 82)
        self.assertGreater(axes, 2000)

    def _reencode(self, b: hkfile.Block, ext: list, lay: dict) -> int:
        boxes = [hs.instance_box((q, t, (1.0, 1.0, 1.0), s)) for (q, t), s in zip(lay['placement'], ext)]
        prim = [boxes[lay['shapes'][j]] for j in lay['blocks']]
        prim = [(tuple(hs.f32(c) for c in lo), tuple(hs.f32(c) for c in hi)) for lo, hi in prim]
        code = lay['section']
        count = 0

        def box(i: int):  # noqa: ANN202
            if not code[4 * i + 3] & 1:
                return prim[code[4 * i + 3] >> 1]
            a, c = box(i + 1), box(i + (code[4 * i + 3] & ~1))
            return tuple(min(x, y) for x, y in zip(a[0], c[0])), tuple(max(x, y) for x, y in zip(a[1], c[1]))

        def walk(i: int, lo, hi) -> None:  # noqa: ANN001
            nonlocal count
            mn, mx = box(i)
            nlo, nhi = [], []
            for a in range(3):
                count += 1
                self.assertEqual(hs.aabb4_encode(lo[a], hi[a], mn[a], mx[a]), code[4 * i + a])
                x, y = hs.aabb4_decode(lo[a], hi[a], code[4 * i + a])
                nlo.append(x)
                nhi.append(y)
            if code[4 * i + 3] & 1:
                walk(i + 1, nlo, nhi)
                walk(i + (code[4 * i + 3] & ~1), nlo, nhi)
        walk(0, lay['domain'][0][:3], lay['domain'][1][:3])
        return count

    def test_compounds(self) -> None:
        """refresh_compound on a copy of each stock compound of convex shapes: the aabb to two float32 steps (all),
        the bounding radius never smaller, the key bits and key estimate equal."""
        n = 0
        for name, tf, b in self.blocks('hknpCompoundShape'):
            if any(hs.instance_box(i) is None for i in hs.instances(b)):
                continue
            n += 1
            c = hkfile.clone(b)
            (lo0, hi0), _ = hs.compound_bounds(b)
            lo, hi = hs.refresh_compound(tf, c)
            for k in range(3):
                self.assertLessEqual(abs(lo[k] - lo0[k]), max(4e-5, 3e-7 * abs(lo0[k])), name)
                self.assertLessEqual(abs(hi[k] - hi0[k]), max(4e-5, 3e-7 * abs(hi0[k])), name)
            self.assertGreaterEqual(c.value['boundingRadius'].f32(), b.value['boundingRadius'].f32() * (1 - 1e-5))
            for m in ('numShapeKeyBits', 'estimatedNumShapeKeys'):
                self.assertEqual(c.value[m].int(), b.value[m].int(), (name, m))
        self.assertGreater(n, 1800)

    def test_hulls_and_their_mass(self) -> None:
        """Every stock hull with faces rebuilt from its vertices is consistent (hull_problems) and keeps its vertices
        (> 95 %; the rest: Havok's simplified planes drop near-coplanar ones; 3 flat ones are refused); from_hull
        agrees with the stored mass properties to the documented accuracy (volume median 5e-4)."""
        n = same = flat = 0
        vol = []
        for kind in ('hknpConvexShape', 'hknpBoxShape', 'hknpCylinderShape'):
            for name, _, b in self.blocks(kind):
                h = hs.read_hull(b)
                if not h.faces:
                    continue
                n += 1
                try:
                    again = hs.build_hull(h.vertices)
                except hs.HullError:
                    flat += 1
                    continue
                self.assertEqual(hs.hull_problems(again), [], name)
                same += sorted(again.vertices) == sorted(h.vertices)
                m = hs.mass_properties(b)
                if kind == 'hknpConvexShape' and m is not None:
                    s = hs.read_mass(m)
                    if abs(s.mass / s.volume - hs.DENSITY) < 1:
                        vol.append(abs(hs.from_hull(h, b.value['convexRadius'].f32()).volume - s.volume) / s.volume)
        self.assertGreater(same, 0.95 * n)
        self.assertLessEqual(flat, 3)
        self.assertLess(statistics.median(vol), 1e-3)


if __name__ == '__main__':
    unittest.main()
