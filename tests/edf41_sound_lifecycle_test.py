"""Authored EDF4.1 sound ownership: ACB cue metadata, source MAB channels, shipped weapon modes."""
from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import acb
import awe
import edf41_sound
import edf41_sounds
import cpk
import gamedir
import make_edf41_objects as objects
import make_edf41_weapons as weapons
import rootcpk
import sgo


def utf(columns: list[tuple[str, str]], rows: list[dict]) -> bytes:
    """Tiny independent row-storage @UTF fixture writer (not used by production)."""
    strings = bytearray()
    indices = {}

    def text(value: str) -> int:
        if value not in indices:
            indices[value] = len(strings)
            strings.extend(value.encode('utf-8') + b'\0')
        return indices[value]

    title = text('Fixture')
    desc = bytearray()
    for name, kind in columns:
        desc.extend(struct.pack('>BI', 0x50 | {'str': 10, 'data': 11, 'H': 2, 'I': 4}[kind], text(name)))
    body = bytearray()
    data = bytearray()
    for row in rows:
        for name, kind in columns:
            value = row[name]
            if kind == 'str':
                body.extend(struct.pack('>I', text(value)))
            elif kind == 'data':
                body.extend(struct.pack('>II', len(data), len(value)))
                data.extend(value)
            else:
                body.extend(struct.pack('>' + kind, value))
    row_at = 24 + len(desc)
    strings_at = row_at + len(body)
    data_at = strings_at + len(strings)
    width = sum({'str': 4, 'data': 8, 'H': 2, 'I': 4}[kind] for _, kind in columns)
    result = struct.pack('>HHIIIHHI', 1, row_at, strings_at, data_at, title, len(columns), width, len(rows))
    result += desc + body + strings + data
    return b'@UTF' + struct.pack('>I', len(result)) + result


def bank(names: bytes, cues: bytes) -> bytes:
    return utf([('CueNameTable', 'data'), ('CueTable', 'data')], [{'CueNameTable': names, 'CueTable': cues}])


class Edf41SoundLifecycleTests(unittest.TestCase):
    def test_cue_lengths_follow_indices_not_name_order_or_wave_loop_flags(self) -> None:
        names = utf([('CueName', 'str'), ('CueIndex', 'H')], [
            {'CueName': 'one_shot', 'CueIndex': 1}, {'CueName': 'sustained', 'CueIndex': 0}])
        cues = utf([('Length', 'I')], [{'Length': 0xffffffff}, {'Length': 250}])
        self.assertEqual(acb.cue_lengths(bank(names, cues)), {'one_shot': 250, 'sustained': 0xffffffff})
        self.assertEqual(acb.cue_lengths(cpk._unmask(bank(names, cues))), {'one_shot': 250, 'sustained': 0xffffffff})

    def test_installed_fallback_prevents_finite_to_infinite_template_substitution(self) -> None:
        finite_name = utf([('CueName', 'str'), ('CueIndex', 'H')], [{'CueName': 'old_finite', 'CueIndex': 0}])
        infinite_name = utf([('CueName', 'str'), ('CueIndex', 'H')], [{'CueName': 'template_loop', 'CueIndex': 0}])
        finite = bank(finite_name, utf([('Length', 'I')], [{'Length': 1000}]))
        infinite = bank(infinite_name, utf([('Length', 'I')], [{'Length': 0xffffffff}]))
        with tempfile.TemporaryDirectory() as tmp:
            paths = [Path(tmp, game, 'SOUND', 'PC') for game in ('six', 'old')]
            for path in paths:
                path.mkdir(parents=True)
            (paths[0] / 'TIKYUUX_SE.ACB').write_bytes(infinite)
            (paths[1] / 'TIKYUUX_SE.ACB').write_bytes(finite)
            own = {'FireSe': [0, 'old_finite', 1, 1, 1, 25]}
            template = {'FireSe': [1, 'template_loop', 1, 1, 1, 25]}
            stock = acb.game_cues(str(Path(tmp, 'six')))
            self.assertEqual(weapons.cue_swaps(own, template, stock, {'old_finite'}),
                             {'old_finite': 'template_loop'})  # the old P5 rule reproduces the leak
            installed = weapons.available_weapon_cues(str(Path(tmp, 'six')), str(Path(tmp, 'old')))
            self.assertEqual(weapons.cue_swaps(own, template, installed, {'old_finite'}), {})
            self.assertEqual(weapons.missing_cues(own, {}, installed, {'old_finite'}), [])

    def test_invalid_cue_index_is_refused(self) -> None:
        names = utf([('CueName', 'str'), ('CueIndex', 'H')], [{'CueName': 'bad', 'CueIndex': 3}])
        cues = utf([('Length', 'I')], [{'Length': 10}])
        with self.assertRaisesRegex(ValueError, 'invalid index'):
            acb.cue_lengths(bank(names, cues))

    def test_shipped_robo_events_keep_absent_channel_distinct_from_channel_zero(self) -> None:
        folder = ROOT / 'edf41port' / 'objects' / 'OBJECT'
        _, obj = sgo.read((folder / 'UFOROBO401.SGO').read_bytes())
        regenerated = objects.robo_events((folder / 'UFOROBO401.CAS').read_bytes(), obj['animation_model'][2])
        actual = sgo.plain(obj['edf41_robo_events'])
        # SGO float32 storage, including the event time, is compared at the format's precision.
        written = sgo.read(sgo.write(0x102, {'events': regenerated}))[1]['events']
        self.assertEqual(actual, sgo.plain(written))
        sound_events = [event for _, _, events in actual for event in events if event[1] in (0, 1)]
        owned = [event for event in sound_events if event[3] >= 0]
        self.assertEqual([(e[1], e[3]) for e in owned], [(0, 0.0), (1, 0.0)])
        self.assertTrue(all(event[3] == -1 for event in sound_events if event not in owned))

    def test_mother_cannon_tracks_preserve_exact_stop_keys_and_bone_tracing(self) -> None:
        folder = ROOT / 'edf41port' / 'objects' / 'OBJECT'
        _, hull = sgo.read((folder / 'UFOMOTHER301.SGO').read_bytes())
        presets = {entry[0] for entry in hull['edf41_se']}
        tables = {}
        rebuild = {}
        for name in ('UFOMOTHER301_BIGCANNON', 'UFOMOTHER301_CANNON'):
            _, obj = sgo.read((folder / (name + '.SGO')).read_bytes())
            cas = (folder / (name + '.CAS')).read_bytes()
            expected = edf41_sound.animation_events(cas, obj['animation_model'][2])
            stripped = dict(obj)
            stripped.pop('edf41_sound_events')
            rebuild['OBJECT/' + name + '.SGO'] = sgo.write(0x102, stripped)
            rebuild['OBJECT/' + name + '.CAS'] = cas
            normalized = sgo.read(sgo.write(0x102, {'events': expected}))[1]['events']
            self.assertEqual(sgo.plain(obj['edf41_sound_events']), sgo.plain(normalized))
            tables[name] = {state: keys for state, _, keys in sgo.plain(obj['edf41_sound_events'])}
            for state, _, events in expected:
                for time, kind, name, channel, node, trace in events:
                    if kind == 0:
                        self.assertIn(name, presets, (state, name))
        objects.mother_parts_fix(rebuild)
        for name in ('UFOMOTHER301_BIGCANNON', 'UFOMOTHER301_CANNON'):
            regenerated = sgo.read(rebuild['OBJECT/' + name + '.SGO'])[1]
            self.assertIn('edf41_sound_events', regenerated, 'the production generator must emit the event table')
            self.assertEqual({state: keys for state, _, keys in sgo.plain(regenerated['edf41_sound_events'])}, tables[name])
        main = tables['UFOMOTHER301_BIGCANNON']
        cannon = tables['UFOMOTHER301_CANNON']['appear']
        self.assertEqual([(e[0], e[3]) for e in main['open'] if e[1] == 1], [(440.0, 0), (976.0, 1)])
        self.assertEqual([(e[0], e[3]) for e in main['close'] if e[1] == 1], [(470.0, 1), (910.0, 0)])
        self.assertEqual([(e[0], e[3]) for e in cannon if e[1] == 1], [(189.0, 0), (400.0, 1), (560.0, 2)])
        self.assertEqual((cannon[0][4], cannon[0][5]), ('cannon', 1))

    def test_real_authored_robo_loops_and_ported_fire_modes(self) -> None:
        game6 = Path(rootcpk.DEFAULT_GAME)
        game41 = gamedir.find_other(gamedir.EDF41, str(game6))
        resident = game6 / 'SOUND' / 'PC' / 'TIKYUUX_SE.ACB'
        if not game41 or not resident.is_file():
            self.skipTest('real games unavailable; fixture and shipped-source tests still run')
        with (game6 / 'SOUND' / 'PC' / 'TIKYUU4_EN_UFOROBO.ACB').open('rb') as stream:
            robo = acb.cue_lengths(stream.read())
        self.assertEqual(robo['en_Robo_fire360_shot'], 0xffffffff)
        self.assertEqual(robo['en_Robo_fireSmall_shot'], 0xffffffff)
        self.assertNotEqual(robo['en_Robo_fireBuster_shot'], 0xffffffff)
        # Source MAB ownership is independently backed by actual HCA loops in both appearance cues.
        mother = (game6 / 'SOUND' / 'PC' / 'TIKYUU4_EN_MOTHERSHIP.ACB').read_bytes()
        _, data_at, fields = edf41_sounds.utf_fields(mother)
        offset, size = fields['AwbFile']
        awb = mother[data_at + offset:data_at + offset + size]
        waves = awe.afs2_waves(awb)
        for wave_id, expected in ((21, (35, 110)), (32, (23, 58))):
            begin, end = waves[wave_id]
            header = awb[begin:min(end, begin + 0x100)]
            loop = bytes(byte & 0x7f for byte in header).find(b'loop')
            self.assertGreaterEqual(loop, 8)
            self.assertEqual(struct.unpack_from('>II', header, loop + 4), expected)

        # Actual resolution is stock first, with the 4.1-only bank as fallback. Never change all wave LoopFlags.
        source_lengths = acb.cue_lengths((Path(game41) / 'SOUND' / 'PC' / 'TIKYUUX_SE.ACB').read_bytes())
        stock_lengths = acb.cue_lengths(resident.read_bytes())
        lengths = source_lengths | stock_lengths
        self.assertFalse([cue for cue in source_lengths.keys() & stock_lengths.keys()
                          if source_lengths[cue] != 0xffffffff and stock_lengths[cue] == 0xffffffff])
        registry = json.loads((ROOT / 'edf41port' / 'weapons.json').read_text(encoding='utf-8'))
        checked = 0
        restored = 0
        repaired_ids = {'EDF6VC_E41_' + suffix for suffix in (
            'WEAPON006', 'WEAPON015', 'WEAPON019', 'WEAPON025', 'WEAPON028', 'WEAPON165', 'LASERLANCE01',
            'WEAPON185', 'WEAPON187', 'WEAPON188', 'WEAPON189', 'WEAPON191', 'WEAPON193', 'WEAPON194',
            'WEAPON196', 'WEAPON199', 'WEAPON202', 'WEAPON203', 'WEAPON206', 'WEAPON207', 'WEAPON210',
            'WEAPON211', 'WEAPON257', 'WEAPON259', 'WEAPON261', 'WEAPON264', 'WEAPON267')}
        repaired = set()
        source_game = rootcpk.Game(game41)
        for weapon in registry['weapons']:
            root = weapon['weapon']
            fields = {name: root['items'][int(index)] for index, name in root['names'].items()}
            fire = fields.get('FireSe', {}).get('items', [])
            if len(fire) < 2:
                continue
            cue = weapon.get('cues', {}).get(fire[1], fire[1])
            _, authored = sgo.read(source_game.read('WEAPON', weapon['sgo']))
            source_fire = authored.get('FireSe', [])
            self.assertEqual(fire[0], sgo.plain(source_fire[0]), (weapon['id'], 'source playback mode changed'))
            if fire[1] in source_lengths and fire[1] not in stock_lengths:
                restored += 1
                self.assertNotIn(fire[1], weapon.get('cues', {}), (weapon['id'], 'resident cue must not be replaced'))
            if weapon['id'] in repaired_ids:
                repaired.add(weapon['id'])
                self.assertEqual(fire[0], 0.0)
                self.assertNotEqual(lengths[cue], 0xffffffff)
            if lengths.get(cue) == 0xffffffff:
                checked += 1
                self.assertEqual(fire[0], 1.0, (weapon['id'], cue, 'must have the native automatic-stop owner'))
        self.assertGreater(checked, 20)
        self.assertEqual(repaired, repaired_ids)
        self.assertEqual(restored, 217)


if __name__ == '__main__':
    unittest.main()
