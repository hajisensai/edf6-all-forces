"""pylib/shkt_check.py. The first class needs no game files; the second reads Root.cpk (skipped without the game)
and checks that the stock files are clean and that each kind of damage to a stock SHKT is reported."""
from __future__ import annotations

import math
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'pylib'))
sys.path.insert(1, os.path.join(os.path.dirname(__file__), '..', 'tools'))
import gamedir  # noqa: E402
import hkfile  # noqa: E402
import hkshape  # noqa: E402
import sgo  # noqa: E402
import shkt_check as sc  # noqa: E402

CUBE = [(x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)]
CUBE_PLANES = [(1.0, 0.0, 0.0, -1.0), (-1.0, 0.0, 0.0, -1.0), (0.0, 1.0, 0.0, -1.0),
               (0.0, -1.0, 0.0, -1.0), (0.0, 0.0, 1.0, -1.0), (0.0, 0.0, -1.0, -1.0)]


def mapping_sgo(rows_afr: list[tuple[str, str]], rows_rfa: list[tuple[str, str]]) -> bytes:
    """An embedded mapping SGO: rows [[names], position, rotation, scale]."""
    def row(names: tuple[str, str]) -> list:
        return [list(names), [0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0, 1.0, 1.0]]
    return sgo.write(0x102, {'animation_from_ragdoll': [row(r) for r in rows_afr],
                             'ragdoll_from_animation': [row(r) for r in rows_rfa]})


def _hk_int(v: int) -> bytes:
    """pylib/hk2014.py's variable-length int."""
    neg, v = v < 0, abs(v)
    out = [(v & 0x3F) << 1 | neg]
    v >>= 6
    while v:
        out[-1] |= 0x80
        out.append(v & 0x7F)
        v >>= 7
    return bytes(out)


def _hk_str(s: str) -> bytes:
    return _hk_int(len(s)) + s.encode('latin-1')


def legacy_ragdoll(names: list[str]) -> bytes:
    """A minimal Havok 2014 binary tagfile (pylib/hk2014.py's format): one hkpRigidBody object (a name only) per name."""
    import hk2014
    out = bytearray(hk2014.MAGIC)
    out += _hk_int(1) + _hk_int(5) + _hk_str('hk_2014.1.0-r1') + hk2014.PREAMBLE
    out += _hk_int(2) + _hk_str('hkpRigidBody') + _hk_int(0) + _hk_int(0) + _hk_int(1) + _hk_str('name') +         _hk_int(hk2014.T_CSTRING)
    for n in names:
        out += _hk_int(4) + _hk_int(1) + bytes([1]) + _hk_str(n)
    return bytes(out + _hk_int(7))


class Portable(unittest.TestCase):
    def test_rotation(self) -> None:
        q = (0.0, 0.0, math.sin(math.pi / 4), math.cos(math.pi / 4))     # 90 degrees about z
        r = sc.rot(q, (1.0, 0.0, 0.0))
        self.assertAlmostEqual(r[0], 0.0, places=6)
        self.assertAlmostEqual(r[1], 1.0, places=6)
        self.assertEqual(sc.place((1.0, 2.0, 3.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0)), (2.0, 3.0, 4.0))

    def test_hull_and_boxes(self) -> None:
        self.assertAlmostEqual(sc.hull_outside(CUBE, CUBE_PLANES), 0.0)
        self.assertAlmostEqual(sc.hull_outside(CUBE + [(1.5, 0.0, 0.0)], CUBE_PLANES), 0.5)
        box = sc.box_of(CUBE, 0.1)
        self.assertEqual(box, ((-1.1, -1.1, -1.1), (1.1, 1.1, 1.1)))
        self.assertLessEqual(sc.box_misses(box, sc.box_of(CUBE)), 0.0)
        self.assertAlmostEqual(sc.box_misses(sc.box_of(CUBE), box, tol=0.0), 0.1)

    def test_shkt_ref(self) -> None:
        self.assertEqual(sc.shkt_ref('app:/object/Ragdoll_v603_flak.shkt'), 'OBJECT/RAGDOLL_V603_FLAK.SHKT')
        self.assertEqual(sc.shkt_ref('app:/WEAPON/ShellCase.shkt'), 'WEAPON/SHELLCASE.SHKT')
        self.assertEqual(sc.shkt_ref('x.SHKT'), 'OBJECT/X.SHKT')
        self.assertEqual(sc.shkt_ref('OBJECT/X.SHKT'), 'OBJECT/X.SHKT')

    def test_bound_names_and_missing_bodies(self) -> None:
        inner = mapping_sgo([('RagDollProxys.body', 'body'), ('RagDollProxys.gone', 'turret')],
                            [('body', 'RagDollProxys.body')])
        sgo_bytes = sgo.write(0x102, {
            'ragdoll': ['app:/object/Test_ragdoll.shkt', inner],
            'model': [['x.mrab', [1.0, 2.0], 'app:/object/Test_acc.shkt', inner]],
            'accessory_ragdoll': [1.0, [['RagDollProxys.hair', 0.5], ['RagDollProxys.tail', 0.5]],
                                  'app:/object/Test_acc.shkt', inner]})
        doc = sc.load_plain(sgo_bytes)
        assert doc is not None
        found = sc.bound_names(doc)
        ragdoll = sorted(b for s, _, b in found if s == 'OBJECT/TEST_RAGDOLL.SHKT')
        self.assertEqual(ragdoll, ['RagDollProxys.body', 'RagDollProxys.body', 'RagDollProxys.gone'])
        acc = {b for s, _, b in found if s == 'OBJECT/TEST_ACC.SHKT'}
        self.assertEqual(acc, {'RagDollProxys.body', 'RagDollProxys.gone', 'RagDollProxys.hair', 'RagDollProxys.tail'})
        # the bone names (turret, body as a bone) are never taken for bodies
        self.assertNotIn('turret', {b for _, _, b in found})
        missing = sc.sgo_problems(sgo_bytes, 'app:/object/Test_ragdoll.shkt', {'RagDollProxys.body'})
        self.assertEqual([p.what for p in missing], ["body 'RagDollProxys.gone' is not in OBJECT/TEST_RAGDOLL.SHKT"])
        self.assertTrue(all(p.severity == 'error' for p in missing))
        self.assertEqual(sc.sgo_problems(sgo_bytes, 'app:/object/Test_ragdoll.shkt',
                                         {'RagDollProxys.body', 'RagDollProxys.gone'}), [])

    def test_legacy_tagfile_bodies(self) -> None:
        """A 4.1 ragdoll (Havok 2014 tagfile) is not skipped: its body names are what its SGO is checked against."""
        data = legacy_ragdoll(['RagDollProxys.body', 'RagDollProxys.turret'])
        self.assertEqual(sc.body_names(data), {'RagDollProxys.body', 'RagDollProxys.turret'})
        self.assertEqual(sc.problems(data), [])
        inner = mapping_sgo([('RagDollProxys.body', 'body'), ('RagDollProxys.turret', 'turret')], [])
        sgo_bytes = sgo.write(0x102, {'ragdoll': ['app:/object/Ragdoll_x401.shkt', inner]})
        self.assertEqual(sc.sgo_problems(sgo_bytes, 'app:/object/Ragdoll_x401.shkt', sc.body_names(data)), [])
        renamed = legacy_ragdoll(['RagDollProxys.body', 'RagDollProxys.turreX'])
        missing = sc.sgo_problems(sgo_bytes, 'app:/object/Ragdoll_x401.shkt', sc.body_names(renamed))
        self.assertEqual([p.what for p in missing], ["body 'RagDollProxys.turret' is not in OBJECT/RAGDOLL_X401.SHKT"])
        twice = legacy_ragdoll(['RagDollProxys.body', 'RagDollProxys.body'])
        self.assertEqual([p.severity for p in sc.problems(twice)], ['error'])

    def test_ragdoll_rules(self) -> None:
        """The filter rule, GJK's distance and the stale massDistribution rule on synthetic data."""
        group = 3 << 15
        parent, child = group | 1 << 5, group | 2 << 5 | 1 << 10          # the child's no-collide = the parent's subsystem
        self.assertTrue(sc.collision_disabled(parent, child))
        self.assertFalse(sc.collision_disabled(parent, group | 2 << 5))
        self.assertFalse(sc.collision_disabled(1 << 5, 2 << 5 | 1 << 10))   # system group 0: decided by layers
        cube = [(x, y, z) for x in (0.0, 1.0) for y in (0.0, 1.0) for z in (0.0, 1.0)]
        moved = [(x + 3.0, y + 0.5, z) for x, y, z in cube]
        self.assertAlmostEqual(sc.gjk_distance(cube, moved), 2.0, places=9)
        self.assertEqual(sc.gjk_distance(cube, [(x + 0.5, y, z) for x, y, z in cube]), 0.0)
        import frozen_collision_probe
        import hkfile
        import hkshape
        raw, _ = frozen_collision_probe._sample()
        tf = hkfile.read(raw)
        bodies = hkfile.Physics(tf).bodies()
        dist = tf.new('hkUint32')                       # stands in for a massDistribution object
        for b in bodies:
            b.value['massDistribution'].set_target(dist)
        donor = tf.write()
        cube_shape = bodies[0].shape
        bodies[0].set_shape(hkshape.affine_convex(tf, cube_shape, [[2, 0, 0], [0, 1, 0], [0, 0, 1]], (0, 0, 0)))
        stale = sc.stale_mass_distributions(tf.write(), donor)
        self.assertEqual([p.where for p in stale], ["body 'ragdoll_doppler_radar'"])
        bodies[0].value['massDistribution'].set_target(None)
        self.assertEqual(sc.stale_mass_distributions(tf.write(), donor), [])

    def test_not_an_sgo(self) -> None:
        self.assertIsNone(sc.load_plain(b'CAS\0' + bytes(12)))
        with self.assertRaises(ValueError):
            sc.sgo_problems(b'CAS\0' + bytes(12), 'x.shkt', set())


GAME = gamedir.find()


@unittest.skipUnless(GAME, 'needs the game (Root.cpk)')
class StockMutations(unittest.TestCase):
    """Each check fires on a damaged copy of a stock file that is clean as shipped."""

    SHKT = 'RAGDOLL_V506_HELI.SHKT'
    SGO = 'V506_HELI.SGO'

    @classmethod
    def setUpClass(cls) -> None:
        import rootcpk
        g = rootcpk.Game(GAME)
        cls.data = g.read('OBJECT', cls.SHKT)
        cls.sgo = g.read('OBJECT', cls.SGO)

    def errors(self, data: bytes) -> list[str]:
        return [p.what for p in sc.problems(data) if p.severity == 'error']

    def test_every_stock_file_keeps_the_ragdoll_rules(self) -> None:
        """No stock SHKT overlaps parts that may collide by 5 cm or leaves a body out of its ragdoll (warnings); the
        jointed pairs the filter lets collide are 112 in 5 files (info)."""
        import rootcpk
        g = rootcpk.Game(GAME)
        info = 0
        for name in g.names('OBJECT'):
            if name.upper().endswith('.SHKT'):
                d = g.read('OBJECT', name)
                if d[4:8] == b'TAG0':
                    found = sc.ragdoll_problems(sc.Shkt(d))
                    self.assertEqual([str(p) for p in found if p.severity != 'info'], [], name)
                    info += len(found)
        self.assertEqual(info, 112)

    def test_stock_is_clean(self) -> None:
        self.assertEqual(sc.problems(self.data), [])
        self.assertEqual(sc.sgo_problems(self.sgo, 'app:/OBJECT/Ragdoll_v506_heli.shkt', sc.body_names(self.data)),
                         [])

    def test_renamed_body_breaks_the_sgo(self) -> None:
        old, new = b'RagDollProxys.rotor\0', b'RagDollProxys.rotoX\0'
        self.assertEqual(self.data.count(old), 1)
        bad = self.data.replace(old, new)
        found = sc.sgo_problems(self.sgo, 'app:/OBJECT/Ragdoll_v506_heli.shkt', sc.body_names(bad))
        self.assertTrue(found)
        self.assertTrue(all("'RagDollProxys.rotor'" in p.what for p in found), [str(p) for p in found])

    def _first(self, kind: str) -> tuple[sc.Shkt, hkfile.Block]:
        s = sc.Shkt(self.data)
        for blk in s.tf.blocks():
            if blk.type.full_name == kind:
                return s, blk
        raise AssertionError(f'no {kind}')

    def test_moved_hull_vertex_is_stale(self) -> None:
        s, shape = self._first('hknpConvexShape')
        verts = shape.value['hull']['vertices'].target
        x = struct.unpack_from('<f', verts.data, 0)[0]
        struct.pack_into('<f', verts.data, 0, x + 5.0 * (1 if x >= 0 else -1))
        self.assertTrue(any('stale planes' in e for e in self.errors(s.tf.write())))

    def test_shrunk_tree_leaf_is_stale(self) -> None:
        s, comp = self._first('hknpCompoundShape')
        _, simd = hkshape.compound_bounds(comp)
        assert simd is not None
        nodes = hkshape.simd_lanes(simd)
        k = next(i for i, (leaf, lanes) in enumerate(nodes) if leaf and lanes)
        lo, hi = nodes[k][1][0][0], nodes[k][1][0][1]
        struct.pack_into('<f', simd.data, k * simd.size + 4 * 4, lo[0] + 0.25 * (hi[0] - lo[0]))     # hx of lane 0
        self.assertTrue(any('stale tree' in e for e in self.errors(s.tf.write())))

    def test_moved_body_splits_its_joint(self) -> None:
        s = sc.Shkt(self.data)
        system = next(x for x in s.systems if x.joints)
        body = system.bodies[system.joints[0].body_a].body
        assert body is not None
        p = body.position
        body.set_position((p[0] + 0.5, p[1], p[2]))
        self.assertTrue(any('put the pivot' in e for e in self.errors(s.tf.write())))


if __name__ == '__main__':
    unittest.main()
