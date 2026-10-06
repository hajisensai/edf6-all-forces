"""Aircraft collision regressions. No installation or process interaction.

python tools/aircraft_collision_check.py           synthetic geometry / BVH
python tools/aircraft_collision_check.py --game    read Root.cpk, verify written SHKTs/SGOs
"""
from __future__ import annotations

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'pylib'))
import aircraft_collision as ac
import jet_models
from hktag import Tag
from hkcms import bodies


def contains(box, p, eps=0.0002):
    return all(box[0][k]-eps <= p[k] <= box[1][k]+eps for k in range(3))


def read_shape(blob):
    """Read serialized native-consumer hulls, not the generator's cell list."""
    t = Tag(blob)
    _, comp, _ = t.item(bodies(t)['RagDollProxys.body'])
    assert struct.unpack_from('<Q', blob, comp+t.offset('hknpCompoundShape', 'userData'))[0] == ac.MAGIC
    assert t.u32(comp+t.offset('hknpCompoundShape', 'properties')) == 0
    _, instances, count = t.item(t.u32(comp+t.offset('hknpCompoundShape', 'instances')))
    result = []
    for i in range(count):
        inst = instances+i*t.size('hknpShapeInstance')
        assert struct.unpack_from('<7f', blob, inst) == (0, 0, 0, 1, 0, 0, 0)
        assert struct.unpack_from('<3f', blob, inst+32) == (1, 1, 1)
        typ, shape, _ = t.item(t.u32(inst+t.offset('hknpShapeInstance', 'shape')))
        assert typ == 'hknpConvexShape'
        assert struct.unpack_from('<f', blob, shape+t.offset(typ, 'convexRadius'))[0] == 0
        assert t.u32(shape+t.offset(typ, 'properties')) == 0
        h = shape+t.offset(typ, 'hull')
        _, vs, vn = t.item(t.u32(h))
        _, ps, pn = t.item(t.u32(h+4))
        assert (vn, pn) == (8, 6)
        vertices = [struct.unpack_from('<3f', blob, vs+12*j) for j in range(vn)]
        planes = [struct.unpack_from('<4f', blob, ps+16*j) for j in range(pn)]
        box = jet_models.bbox(vertices)
        assert all(box[1][k] > box[0][k] for k in range(3))
        assert len(set(vertices)) == 8
        assert all(sum(n[k]*p[k] for k in range(3))+n[3] <= 0.0001 for n in planes for p in vertices)
        # Face topology retained from the stock box must still address valid vertices.
        _, indices, ni = t.item(t.u32(h+12))
        assert all(b < vn for b in blob[indices:indices+ni])
        result.append(box)
    tree = comp+t.offset('hknpCompoundShape', 'boundingVolumeData')+t.offset('hknpCompoundShapeData', 'simdTree')
    _, at, n = t.item(t.u32(tree+t.offset('hkcdSimdTree', 'nodes')))
    seen, ids = set(), []
    def visit(i):
        assert 0 < i < n and i not in seen
        seen.add(i)
        f = struct.unpack_from('<24f4I?', blob, at+128*i)
        all_boxes = []
        for lane in range(4):
            box = ([f[8*k+lane] for k in range(3)], [f[8*k+4+lane] for k in range(3)])
            if box[0][0] > box[1][0]:
                continue
            key = f[24+lane]
            if f[28]:
                assert key < count
                ids.append(key)
                children = [result[key]]
            else:
                children = visit(key)
            for child in children:
                assert contains(box, child[0]) and contains(box, child[1])
            all_boxes.extend(children)
        return all_boxes
    visit(1)
    assert sorted(ids) == list(range(count))
    assert len(seen) == n-1
    bound = struct.unpack_from('<8f', blob, comp+t.offset('hknpCompoundShape', 'aabb'))
    cb = (bound[:3], bound[4:7])
    assert all(contains(cb, b[0]) and contains(cb, b[1]) for b in result)
    return result


class Geometry(unittest.TestCase):
    def test_long_triangle_has_interior_collision(self):
        tri = ((0., 1., 0.), (10., 1., 0.), (0., 1., 10.))
        cells = ac.mesh_cells([tri], 10, 10)
        self.assertTrue(any(contains(b, (3, 1, 3)) for b in cells))
        self.assertFalse(any(contains(b, (9, 1, 9)) for b in cells))

    def test_wing_contacts_do_not_fill_air_underneath(self):
        tris = [((-10., 5., -2.), (10., 5., -2.), (0., 5., 3.)),
                ((-1., 0., -1.), (1., 0., -1.), (0., 1., 1.))]
        cells = ac.mesh_cells(tris)
        self.assertTrue(any(contains(b, (-8, 5, -1.5)) for b in cells))
        self.assertFalse(any(contains(b, (-8, 1, -1.5)) for b in cells))
        self.assertAlmostEqual(min(b[0][1] for b in cells), 0.)

    def test_bvh_four_way_root_and_sparse_lanes(self):
        boxes = [((i*2., 0., 0.), (i*2.+1, 1., 1.)) for i in range(19)]
        data, n = ac._tree(boxes)
        self.assertEqual(len(data), n*128)
        f = struct.unpack_from('<24f4I?', data, 128)
        self.assertFalse(f[28])
        self.assertTrue(all(0 < i < n for i in f[24:28]))


class GameAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from rootcpk import default
        cls.game = default()

    def test_all_written_shapes_match_model_surfaces(self):
        for key in ac.FILES:
            with self.subTest(model=key):
                data = ac.build(self.game, key)
                boxes = read_shape(data)
                centre = jet_models.model_box(self.game, key)[0]
                tris = list(ac.triangles(jet_models._model_of(self.game, key)))
                points = set(p for tri in tris for p in tri)
                points.update(tuple(sum(p[k] for p in tri)/3 for k in range(3)) for tri in tris)
                for point in points:
                    p = tuple(point[k]-centre[k] for k in range(3))
                    self.assertTrue(any(contains(b, p) for b in boxes), (key, point))
                # Both outer wings must be represented in the serialized compound.
                low, high = jet_models.bbox(list(points))
                for side in (low[0], high[0]):
                    tip = min(points, key=lambda p: abs(p[0]-side))
                    p = tuple(tip[k]-centre[k] for k in range(3))
                    self.assertTrue(any(contains(b, p) for b in boxes))
                print(f'{ac.FILES[key]}: {len(boxes)} hulls, {len(points)} surface samples, {len(data)} bytes')

    def test_generated_sgos_use_same_shapes_for_every_owner(self):
        import make_jets
        import vcobjects as vc
        import sgo
        out = make_jets.build(self.game.root)
        self.assertEqual(set(out), set(make_jets.names()))
        keys = {}
        for path, name in make_jets.FILES.items():
            jet = vc.JETS[name]
            key = ac.model_key(jet, make_jets.MODEL)
            m = sgo.read(out['OBJECT/'+path])[1]
            if key is False:
                self.assertEqual(m['ragdoll'][0].lower(), 'app:/object/ragdoll_v506_heli.shkt')
                continue
            self.assertEqual(m['ragdoll'][0].lower(), 'app:/object/'+ac.FILES[key].lower())
            self.assertIn('OBJECT/'+ac.FILES[key], out)
            # Geometry/frame must not depend on whether a human or the AI owns it.
            values = [[vc._value(v) for v in row] for row in m['heli_rigid_body'][:2]]
            if key in keys:
                self.assertEqual(keys[key], values)
            keys[key] = values
        for file in make_jets.BOMBERS:
            m = sgo.read(out['OBJECT/'+file])[1]
            self.assertIn('OBJECT/'+m['ragdoll'][0].split('/')[-1].upper(), out)


if __name__ == '__main__':
    game = '--game' in sys.argv
    suite = unittest.TestLoader().loadTestsFromTestCase(Geometry)
    if game:
        suite.addTests(unittest.TestLoader().loadTestsFromTestCase(GameAssets))
    sys.exit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
