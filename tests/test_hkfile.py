"""pylib/hkfile.py: the tagfile graph and Havok's writer rules. The first class needs no game files (a synthetic file
from tools/frozen_collision_probe.py's type table); the second reads Root.cpk and IG_HEIGEN601.MAD (skipped without
the game) and writes every stock SHKT and the map collision back byte for byte."""
from __future__ import annotations

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import gamedir  # noqa: E402
import hkfile  # noqa: E402


class Portable(unittest.TestCase):
    def test_packed_ints(self) -> None:
        for tna in (False, True):
            for v in (0, 1, 0x7F, 0x80, 0x3FFF, 0x4000, 0x1FFFFF, 0x200000, 0x7FFFFFF, *([0x1FFFFFFF] if not tna else
                                                                                           [0x8000000, 0xFFFFFFFF])):
                o = bytearray()
                hkfile.write_packed(o, v, tna)
                self.assertEqual(hkfile.read_packed(bytes(o) + b'\xAA', 0, tna), (v, len(o)), (v, tna))
        with self.assertRaises(ValueError):
            hkfile.write_packed(bytearray(), 0x20000000, False)

    def sample(self) -> bytes:
        import frozen_collision_probe
        return frozen_collision_probe._sample()[0]

    def test_synthetic_round_trip(self) -> None:
        raw = self.sample()
        tf = hkfile.read(raw)
        self.assertEqual(tf.write(), raw)
        p = hkfile.Physics(tf)
        self.assertEqual([b.name for b in p.bodies()], ['ragdoll_doppler_radar', 'ragdoll_tracking_radar',
                                                         'ragdoll_cannon_main'])
        shapes = {id(b.shape) for b in p.bodies()}
        self.assertEqual(len(shapes), 1, 'one shared hull stays one block')

    def test_writer_rules(self) -> None:
        """Strings are shared by content, empty arrays and unreachable blocks are not written, an object comes
        before the arrays only it reaches."""
        tf = hkfile.read(self.sample())
        bodies = hkfile.Physics(tf).bodies()
        for b in bodies:                                    # three equal names: one string item
            b.value['name'].set_target(tf.string('same'))
        shape = bodies[0].shape
        old_planes = shape.value['hull']['planes'].target
        shape.value['hull']['planes'].set_target(tf.new(old_planes.type, 0))     # empty: written as null
        out = tf.write()
        again = hkfile.read(out)
        self.assertEqual(again.write(), out)
        names = [b for b in again.items[1:] if b is not None and b.type.full_name == 'char' and b.text() == 'same']
        self.assertEqual(len(names), 1)
        reread = hkfile.Physics(again).bodies()[0].shape
        self.assertIsNone(reread.value['hull']['planes'].target)
        self.assertNotIn(old_planes.type.full_name + '[6]', [repr(b)[1:-1] for b in again.items[1:]])
        # the root container first; its NamedVariant array right after it (its closure)
        self.assertEqual(again.items[1].type.full_name, 'hkRootLevelContainer')
        self.assertEqual(again.items[2].type.full_name, 'hkRootLevelContainer::NamedVariant')
        for k, b in enumerate(again.items[1:], 1):
            data0 = struct.unpack_from('>I', out, 8)[0] & 0x3FFFFFFF      # SDKV's size: DATA follows it
            at = struct.unpack_from('<I', out, len(out) - 12 * (len(again.items) - k) + 4)[0]
            self.assertEqual(at % b.type.eff_align, 0, (k, b))
            self.assertGreater(data0, 0)

    def test_content_equal_and_changes(self) -> None:
        a = hkfile.read(self.sample())
        b = hkfile.read(self.sample())
        self.assertTrue(hkfile.content_equal(a.root, b.root))
        self.assertEqual(hkfile.changed_blocks(a.root, b.root), [])
        body = hkfile.Physics(b).bodies()[1]
        body.set_position((1.0, 2.0, 3.0))
        self.assertFalse(hkfile.content_equal(a.root, b.root))
        changed = hkfile.changed_blocks(a.root, b.root)
        self.assertEqual(len(changed), 1)
        self.assertEqual(changed[0][0].type.full_name, 'hknpPhysicsSystemData::bodyCinfoWithAttachment')
        copy = hkfile.clone(a.root)
        self.assertTrue(hkfile.content_equal(a.root, copy))
        self.assertFalse(any(x is y for x in a.blocks() for y in hkfile.Tagfile(a.types, copy).blocks()))


GAME = gamedir.find()


@unittest.skipUnless(GAME, 'needs the game (Root.cpk)')
class StockRoundTrip(unittest.TestCase):
    def test_every_stock_shkt(self) -> None:
        import rootcpk
        g = rootcpk.Game(GAME)
        n = 0
        for folder in ('OBJECT', 'WEAPON'):
            for name in g.names(folder):
                if not name.upper().endswith('.SHKT'):
                    continue
                d = g.read(folder, name)
                if d[4:8] != b'TAG0':
                    continue
                tf = hkfile.read(d)
                self.assertEqual(tf.write(), d, name)
                hkfile.Physics(tf)
                n += 1
        self.assertEqual(n, 316)

    def test_map_collision(self) -> None:
        import check_hkcms_build
        d = check_hkcms_build.donor(GAME)
        self.assertEqual(hkfile.read(d).write(), d)


if __name__ == '__main__':
    unittest.main()
