"""tools/sound_pack.py without the real archive (another game's sounds, never in this public repository): a stand-in
zip with every recording MAPPING names, made here in the formats such archives use (16-bit stereo, 24-bit, float, other
rates), goes through build and check. The plugin's clip table is read from src/jetaudio.cpp as the tool reads it."""
from __future__ import annotations

import os
import struct
import sys
import tempfile
import unittest
import zipfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import sound_pack as sp  # noqa: E402


def wav_bytes(x: np.ndarray, rate: int, kind: str) -> bytes:
    """`x` (mono, -1..1) as a WAV of `kind`: pcm16 / pcm16stereo / pcm24 / float32."""
    if kind == 'pcm16stereo':
        data = np.repeat(np.round(x * 32000).astype('<i2'), 2).tobytes()
        code, ch, bits = 1, 2, 16
    elif kind == 'pcm24':
        v = np.round(x * 8000000).astype(np.int32)
        data = b''.join(int(s).to_bytes(3, 'little', signed=True) for s in v)
        code, ch, bits = 1, 1, 24
    elif kind == 'float32':
        data = x.astype('<f4').tobytes()
        code, ch, bits = 3, 1, 32
    else:
        data = np.round(x * 32000).astype('<i2').tobytes()
        code, ch, bits = 1, 1, 16
    block = ch * bits // 8
    head = struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + len(data), b'WAVE', b'fmt ', 16, code, ch, rate, rate * block,
                       block, bits, b'data', len(data))
    return head + data


def stand_in(seconds: float, rate: int, seed: int) -> np.ndarray:
    """A shot-like sound: a click, a noise burst decaying under a low thump, and a steady hum and hiss after it
    (enough to be cut, looped and levelled), with a DC offset as some recordings have."""
    rng = np.random.default_rng(seed)
    t = np.arange(int(seconds * rate)) / rate
    burst = rng.standard_normal(len(t)) * np.exp(-t * 6.0) * 0.6
    thump = np.sin(2 * np.pi * 70 * t) * np.exp(-t * 3.0) * 0.5
    steady = 0.15 * np.sin(2 * np.pi * 110 * t) + 0.05 * rng.standard_normal(len(t))
    return np.clip(burst + thump + steady + 0.03, -0.99, 0.99)


class SoundPackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.zip = os.path.join(cls.tmp.name, 'stand_in.zip')
        kinds = ('pcm16', 'pcm16stereo', 'pcm24', 'float32')
        rates = (48000, 44100, 32000, 22050)
        sources = sorted({layer.src for r in sp.MAPPING for layer in r.layers})
        with zipfile.ZipFile(cls.zip, 'w') as z:
            for i, src in enumerate(sources):
                z.writestr(sp.SAMPLES + src, wav_bytes(stand_in(17.0 if 'fa-18' in src else 7.5, rates[i % 4], i), rates[i % 4],
                                                     kinds[i % 4]))
        # The plugin's own clips as vsound_check writes them: a reference to level against.
        cls.ref = os.path.join(cls.tmp.name, 'ref')
        os.makedirs(cls.ref)
        for r in sp.MAPPING:
            with open(os.path.join(cls.ref, r.clip + '.wav'), 'wb') as f:
                f.write(sp.encode_wav(0.3 * stand_in(2.0, sp.RATE, 99)))
        cls.out = os.path.join(cls.tmp.name, 'out')
        cls.infos = sp.build(cls.zip, cls.out, cls.ref, set())

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def test_mapping_names_the_plugins_clips(self) -> None:
        table = sp.clip_table()
        self.assertIn('gun_near', table)
        self.assertTrue(table['engine_heavy_idle'][0])
        self.assertFalse(table['gun_near'][0])
        sp.validate_mapping(table)

    def test_every_clip_written_and_read_by_the_plugin(self) -> None:
        self.assertEqual([i['clip'] for i in self.infos], [r.clip for r in sp.MAPPING])
        self.assertEqual(sp.check(self.out), [])
        for r in sp.MAPPING:
            with open(os.path.join(self.out, sp.file_name(r.clip)), 'rb') as f:
                self.assertIsNotNone(sp.plugin_reads(f.read()), r.clip)

    def test_loops_seamless_and_shots_levelled(self) -> None:
        table = sp.clip_table()
        for info in self.infos:
            with open(os.path.join(self.out, info['file']), 'rb') as f:
                x, rate = sp.decode_wav(f.read())
            self.assertEqual(rate, sp.RATE)
            is_loop, peak = table[info['clip']]
            self.assertLessEqual(float(np.max(np.abs(x))), peak + 0.01, info['clip'])
            self.assertLess(abs(float(np.mean(x))), 0.01, info['clip'])   # the recordings' DC taken out
            if is_loop:
                step = float(np.percentile(np.abs(np.diff(x)), 99.9))
                self.assertLessEqual(abs(float(x[0] - x[-1])), step, info['clip'])
            # never louder than the plugin's own clip (headroom may leave it quieter, never over)
            self.assertLessEqual(info['vs_plugin_db'], 0.05, info['clip'])

    def test_manifest_records_sources(self) -> None:
        for info in self.infos:
            for s in info['sources']:
                self.assertRegex(s['sha256'], r'^[0-9a-f]{64}$')
            self.assertTrue(info['why'])

    def test_skip_leaves_a_clip_to_the_plugin(self) -> None:
        out = os.path.join(self.tmp.name, 'skipped')
        infos = sp.build(self.zip, out, None, {'gun_near'})
        self.assertNotIn('gun_near', [i['clip'] for i in infos])
        self.assertFalse(os.path.exists(os.path.join(out, sp.file_name('gun_near'))))
        self.assertEqual(sp.check(out), [])
        with self.assertRaises(SystemExit):
            sp.build(self.zip, out, None, {'no_such_clip'})

    def test_wrong_archive_refused(self) -> None:
        empty = os.path.join(self.tmp.name, 'empty.zip')
        with zipfile.ZipFile(empty, 'w') as z:
            z.writestr('readme.txt', 'x')
        with self.assertRaises(SystemExit):
            sp.build(empty, os.path.join(self.tmp.name, 'none'), None, set())

    def test_plugin_refuses_what_readwav_refuses(self) -> None:
        x = stand_in(1.0, 48000, 1)
        self.assertIsNotNone(sp.plugin_reads(wav_bytes(x, 48000, 'pcm16')))
        self.assertIsNotNone(sp.plugin_reads(wav_bytes(x, 48000, 'pcm16stereo')))
        self.assertIsNone(sp.plugin_reads(wav_bytes(x, 48000, 'float32')))
        self.assertIsNone(sp.plugin_reads(wav_bytes(x, 48000, 'pcm24')))
        self.assertIsNone(sp.plugin_reads(wav_bytes(x[:4000], 48000, 'pcm16')))   # under 0.1 s

    def test_check_catches_a_bad_loop_and_a_stray_name(self) -> None:
        bad = os.path.join(self.tmp.name, 'bad')
        os.makedirs(bad)
        t = np.arange(48000) / 48000
        with open(os.path.join(bad, sp.file_name('tracks')), 'wb') as f:
            f.write(sp.encode_wav(0.5 * np.sin(2 * np.pi * 3.3 * t)))       # ends far from where it starts
        with open(os.path.join(bad, 'EDF6VehicleCrew_veh_nothing.wav'), 'wb') as f:
            f.write(sp.encode_wav(0.1 * np.sin(2 * np.pi * 200 * t)))
        errors = sp.check(bad)
        self.assertTrue(any('seam' in e for e in errors), errors)
        self.assertTrue(any('no file of this name' in e for e in errors), errors)


if __name__ == '__main__':
    unittest.main()
