"""Caption keys follow the actual native suffix lookup; generated rows preserve authored text.

No game directory is written. Synthetic cases always run. Installed games, when present,
add a read-only EDF.dll instruction contract and complete JA/EN voice-list/table coverage.
"""
from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import awe
import dsgo
import edf41_sounds as sounds
import gamedir
import rootcpk

GAME = gamedir.find()
SOURCE = gamedir.find_other(gamedir.EDF41, GAME)


def native_key(cue: str) -> str:
    # Independent translation of the native loop: test the final suffix only,
    # shorten once and leave the loop; embedded suffix bytes remain untouched.
    for suffix in ('_E', '_S'):
        if len(cue) >= len(suffix) and cue[len(cue) - len(suffix):] == suffix:
            return cue[:len(cue) - len(suffix)]
    return cue


class CaptionContract(unittest.TestCase):
    def test_suffix_is_terminal_and_removed_once(self):
        cases = {
            'A001AA': 'A001AA', 'A001AA_E': 'A001AA', 'A001AA_S': 'A001AA',
            'A_E_S': 'A_E', 'A_S_E': 'A_S', 'E115_001_E_B': 'E115_001_E_B',
            'A_E_middle': 'A_E_middle', 'A_S_middle': 'A_S_middle',
            '_E': '', '_S': '', 'A_e': 'A_e', 'A_s': 'A_s', '': '',
        }
        for cue, expected in cases.items():
            with self.subTest(cue=cue):
                self.assertEqual(native_key(cue), expected)
                self.assertEqual(sounds.cut_key(cue), expected)

    def test_embedded_key_gets_authored_row_without_overwriting_stock(self):
        row = dsgo.Node(['speaker', 'stock'], {0: 'name', 1: 'caption'})
        rows = dsgo.Node([row], {0: 'E115_001'})
        original = dsgo.write(dsgo.Document(dsgo.Node([rows], {0: 'table'}), []))
        cue = 'E115_001_E_B'
        actual = dsgo.parse(sounds.add_rows(original, [sounds.cut_key(cue)], {cue: 'authored line'})).root.items[0]
        by = {actual.names[i]: actual.items[i].items for i in actual.names}
        self.assertEqual(by['E115_001'], ['speaker', 'stock'])
        self.assertEqual(by[cue], ['', 'authored line'])
        self.assertEqual(by['MusenBegin'], ['', sounds.NO_SUBTITLE])
        self.assertEqual(by['MusenEnd'], ['', sounds.NO_SUBTITLE])
        self.assertGreaterEqual(sounds.VERSIONS['table'], 4)

    @unittest.skipUnless(GAME and (Path(GAME) / 'EDF.dll').is_file(), 'supported EDF.dll unavailable')
    def test_native_lookup_checks_tail_and_exits_after_one_removal(self):
        import pefile
        pe = pefile.PE(str(Path(GAME) / 'EDF.dll'), fast_load=True)
        self.addCleanup(pe.close)
        self.assertEqual(pe.FILE_HEADER.TimeDateStamp, 0x678CCB46)
        img = pe.get_memory_mapped_image()
        sites = {
            # r14=key length; rbx=suffix length; rcx=last possible match start.
            0x7B1900: '488b5e10498bce482bcb',
            0x7B1952: '498d0c4f',
            # Match shortens by the suffix's length, then jumps out, not back to the loop.
            0x7B19F5: '498bce482bcb4c3bf10f82',
            0x7B1A11: '48894c24584533e46644892448eb21',
            0x7B1A2C: '4883c620493bf17409',
            # The exact resulting key is passed to the table row lookup.
            0x7B1AC5: '488d54244848837d8708480f43542448',
        }
        for address, expected in sites.items():
            with self.subTest(address=hex(address)):
                data = bytes.fromhex(expected)
                self.assertEqual(img[address:address + len(data)], data)
        self.assertEqual(img[0x17D6E50:0x17D6E56], '_E\0'.encode('utf-16le'))
        self.assertEqual(img[0x17F0B38:0x17F0B3E], '_S\0'.encode('utf-16le'))
        self.assertEqual(0x7B1AD8 + 5 + struct.unpack_from('<i', img, 0x7B1AD9)[0], 0x55A30)

    @unittest.skipUnless(GAME and SOURCE, 'EDF6 and EDF4.1 source unavailable')
    def test_every_source_cue_has_native_key_in_all_caption_tables(self):
        cues = []
        for _awb, name in sounds.VOICES.values():
            cues.extend(cue.name for cue in awe.read_awe(Path(sounds.sound_path(SOURCE, name)).read_bytes()))
        expected = {native_key(cue) for cue in cues}
        self.assertIn('E115_001_E_B', expected)
        self.assertEqual(set(sounds.cue_keys(SOURCE)), expected)
        generated = sounds.voice_tables(GAME, SOURCE)
        game = rootcpk.Game(GAME)
        for lang, text_file in sounds.CAPTIONS.items():
            with self.subTest(language=lang):
                stock = dsgo.parse(game.read('MISSION', f'EDF6_VOICETABLE.{lang}.SGO')).root.items[0]
                got = dsgo.parse(generated[sounds.table_rel(lang)]).root.items[0]
                rows = {got.names[i]: got.items[i] for i in got.names}
                self.assertFalse(expected - rows.keys())
                for index, key in stock.names.items():
                    self.assertEqual(rows[key].items, stock.items[index].items)
                text = {**sounds.read_captions(sounds.plugin_path(SOURCE, sounds.CAPTIONS['EN'])),
                        **sounds.read_captions(sounds.plugin_path(SOURCE, text_file))}
                self.assertEqual(rows['E115_001_E_B'].items, ['', text['E115_001_E_B']])


if __name__ == '__main__':
    unittest.main()
