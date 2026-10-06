"""Portable CAS retarget tests; no game files or installation are used."""
import os
import struct
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'pylib'))
from cas_pose import CasPose, retarget


def model(offset):
    local = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., *offset, 1.]
    return SimpleNamespace(bones=[SimpleNamespace(name='gun', local=local)], name_of=lambda n: n)


def fixture(shared=False):
    """One absolute animated channel and an additive recoil channel, two clips."""
    d = bytearray(512)
    d[:8] = b'CAS\0\x04\x02\0\0'
    struct.pack_into('<I', d, 8, 32)
    d[32:40] = b'CANM\0\x03\0\0'
    struct.pack_into('<6I', d, 40, 2, 128, 2, 32, 1, 240)
    for at, base in [(64, (0., 2., 3., 1.)), (112, (0., 0., -.2, 1.))]:
        struct.pack_into('<8f4i', d, at, *base, 0., 0., .00001, 0., 320-at, 1, 2, 0)
    for at, name_at, tracks_at, channel in [(160, 350, 240, 0), (188, 380, 248, 0 if shared else 1)]:
        struct.pack_into('<IiffIII', d, at, 1, name_at-at, 1., 1., 2, 1, tracks_at-at)
        struct.pack_into('<Hhhh', d, tracks_at, 0, channel, -1, -1)
    struct.pack_into('<i', d, 272, 400-272)
    for at, text in [(350, 'base'), (380, 'recoil'), (400, 'gun')]:
        raw = (text+'\0').encode('utf-16le'); d[at:at+len(raw)] = raw
    struct.pack_into('<6H', d, 320, 0, 0, 0, 0, 0, 60000)
    return bytes(d)


class RetargetTests(unittest.TestCase):
    def test_absolute_base_moves_but_motion_and_additive_clip_do_not(self):
        raw = fixture(); a = CasPose(raw)
        fixed = retarget(raw, model((0, 2, 3)), model((0, 4, 8)), {'base'})
        b = CasPose(fixed)
        for frame in (0, 1):
            self.assertAlmostEqual(b.translation(0, frame)[2]-a.translation(0, frame)[2], 5)
            self.assertEqual(b.translation(1, frame), a.translation(1, frame))
        self.assertEqual(raw[76:], fixed[76:])   # only xyz of the one absolute channel changed

    def test_free_tracks_keep_name_table_and_stop_pose_writes(self):
        raw = fixture(); fixed = retarget(raw, model((0,2,3)), model((0,4,8)), {'base'}, {'gun'})
        p = CasPose(fixed)
        self.assertEqual(p.names, ('gun',))
        self.assertTrue(all(not c.tracks for c in p.clips))
        self.assertEqual(len(raw), len(fixed))

    def test_shared_absolute_and_additive_channel_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'shared'):
            retarget(fixture(True), model((0,2,3)), model((0,4,8)), {'base'})

    def test_invalid_offsets_are_refused(self):
        bad = bytearray(fixture()); struct.pack_into('<I', bad, 8, 0xffffffff)
        with self.assertRaises(ValueError):
            CasPose(bytes(bad))


if __name__ == '__main__':
    unittest.main()
