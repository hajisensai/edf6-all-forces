"""Script selection, source analysis and malformed-container regressions, without game resources."""
from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import bvm41 as b
import mission_script as script


def fixture(body: list[b.Ins] | None = None) -> bytes:
    function = b.Function('Main', b'', [b.Ins(b.OP_PUSHL), b.Ins(b.OP_FPADD), b.Ins(b.OP_RET),
                                      *(body or [b.Ins(b.OP_NAT0, 22, 1), b.Ins(b.OP_END)])])
    return b.build(b.Program('Mission', [], [b.Ins(b.OP_END)], [function]))


class SelectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.files: dict[str, script.File] = {}

    def read(self, key: str) -> script.File | None:
        return self.files.get(key.upper())

    def put(self, key: str, data: bytes, origin: str = 'Root') -> None:
        self.files[key.upper()] = script.File(data, origin)

    def test_root_ac_beats_a_mod_bvm(self) -> None:
        self.put('MISSION/X/MISSION.BVM', fixture(), 'Mods')
        self.put('MISSION/X/MISSION.AC', b'bad AC source', 'Root')
        selected = script.select(self.read, 'X')
        self.assertEqual((selected['format'], selected['source'], selected['shadowed_bvm']), ('AC', 'Root', 'Mods'))
        self.assertFalse(selected['bvm_fallback'])

    def test_empty_ac_still_blocks_bvm_fallback(self) -> None:
        self.put('MISSION/X/MISSION.BVM', fixture())
        self.put('MISSION/X/MISSION.AC', b'')
        self.assertEqual(script.select(self.read, 'X')['format'], 'AC')

    def test_bvm_when_ac_is_absent(self) -> None:
        self.put('MISSION/X/MISSION.BVM', fixture())
        self.assertEqual(script.select(self.read, 'app:/Mission/X')['format'], 'BVM')

    def test_before_ac_needs_the_bvm_existence_gate(self) -> None:
        self.put('MISSION/X/BEFORE.AC', b'void Main() {}')
        self.assertEqual(script.select(self.read, 'X', 'BEFORE')['state'], 'blocked_before_gate')
        self.put('MISSION/X/BEFORE.BVM', fixture())
        self.assertEqual(script.select(self.read, 'X', 'BEFORE')['format'], 'AC')

    def test_missing_before_is_optional_but_missing_main_is_not(self) -> None:
        self.assertEqual(script.select(self.read, 'X', 'BEFORE')['state'], 'absent')
        self.assertEqual(script.select(self.read, 'X')['state'], 'missing')

    def test_non_utf8_ac_reports_error_without_selecting_bvm(self) -> None:
        self.put('MISSION/X/MISSION.AC', b'\xff')
        self.put('MISSION/X/MISSION.BVM', fixture())
        result = script.audit(self.read, 'X')['scripts']['MISSION']
        self.assertEqual(result['format'], 'AC')
        self.assertIn('UnicodeDecodeError', result['parse_error'])

    def test_keys_cannot_escape_mission_directory(self) -> None:
        for key in ('../EDF.dll', 'X/../Y', 'C:/X', ''):
            with self.subTest(key=key), self.assertRaises(ValueError):
                script.mission_key(key)


class MetadataTests(unittest.TestCase):
    def test_fixed_multiplier_native_has_a_known_empty_signature(self) -> None:
        metadata = script.bvm_metadata(fixture())
        self.assertEqual(metadata['missing_signatures'], [])
        self.assertEqual(metadata['native_calls'][0]['signature'], ['', 0])
        self.assertEqual(metadata['native_calls'][0]['stack_effect'], 0)
        self.assertEqual(len(metadata['fixed_multiplier_calls']), 1)
        self.assertEqual(metadata['type_issues'], [])

    def test_initializer_is_in_native_inventory(self) -> None:
        program = b.parse(fixture())
        program.init.insert(0, b.Ins(b.OP_NAT0, 99999, 4))
        calls = script.bvm_metadata(b.build(program))['native_calls']
        self.assertEqual((calls[0]['function'], calls[0]['instruction']), ('<init>', 0))
        self.assertIsNone(calls[0]['signature'])

    def test_ac_ignores_comments_and_string_fake_calls(self) -> None:
        source = b'// SetMissionLevel(9); "app:/fake"\nSetMissionLevel(1); /* "app:/also_fake" */\n' \
                 b'Print("SetMissionLevelFix()"); Load("app:/Object/test.sgo");'
        metadata = script.ac_metadata(source)
        self.assertEqual(metadata['api_call_sites'], {'SetMissionLevel': 1})
        self.assertEqual(metadata['direct_references'], ['app:/Object/test.sgo'])
        self.assertEqual(metadata['compile_status'], 'not_verified')

    def test_native20_never_becomes_an_automatic_same_name_mapping(self) -> None:
        old = script.File(fixture([b.Ins(b.OP_PUSH, 0, 4), b.Ins(b.OP_NAT0, 20, 1), b.Ins(b.OP_END)]), 'old')
        result = script.compare(old, script.File(b'SetMissionLevel(1);', 'new'))
        self.assertEqual(result['level_semantics'], 'requires_team_scope_review')
        self.assertFalse(result['automatic_switch_allowed'])


    def test_added_ac_level_api_also_requires_scope_review(self) -> None:
        old = script.File(fixture([b.Ins(b.OP_END)]), 'old')
        result = script.compare(old, script.File(b'SetMissionLevel(1);', 'new'))
        self.assertEqual(result['bvm_level_calls'], [])
        self.assertEqual(result['ac_level_call_sites'], 1)
        self.assertEqual(result['level_semantics'], 'requires_team_scope_review')

    def test_added_fixed_level_api_is_visible_in_comparison(self) -> None:
        old = script.File(fixture([b.Ins(b.OP_END)]), 'old')
        result = script.compare(old, script.File(b'SetMissionLevelFix();', 'new'))
        self.assertEqual(result['bvm_fixed_level_calls'], [])
        self.assertEqual(result['ac_fixed_level_call_sites'], 1)
        self.assertEqual(result['fixed_level_review'], 'different_call_site_count')

    def test_equal_fixed_call_counts_are_not_behavior_proof(self) -> None:
        result = script.compare(script.File(fixture(), 'old'), script.File(b'SetMissionLevelFix();', 'new'))
        self.assertEqual(result['fixed_level_review'], 'same_call_site_count_only')
        self.assertFalse(result['automatic_switch_allowed'])


class FriendlyDependencyTests(unittest.TestCase):
    def test_models_from_both_predecessors_are_required(self) -> None:
        import edf41friends
        data = fixture([
            b.Ins(b.OP_PUSHS, 'spawn_point'), b.Ins(b.OP_PUSH1), b.Ins(b.OP_JZ, 8),
            b.Ins(b.OP_PUSHS, 'app:/Object/first.sgo'), b.Ins(b.OP_JMP, 9),
            b.Ins(b.OP_PUSHS, 'app:/Object/second.sgo'), b.Ins(b.OP_PUSH, 0, 4), b.Ins(b.OP_PUSH, 1, 1),
            b.Ins(b.OP_NAT0, 1010, 2), b.Ins(b.OP_END),
        ])
        spawned = edf41friends.spawns(data)
        self.assertEqual(len(spawned), 1)
        self.assertEqual(spawned[0].point, 'spawn_point')
        self.assertEqual(spawned[0].sgos, ('OBJECT/FIRST.SGO', 'OBJECT/SECOND.SGO'))


class ContainerTests(unittest.TestCase):
    def test_rebuild_is_unchanged(self) -> None:
        data = fixture()
        self.assertEqual(b.build(b.parse(data)), data)

    def test_string_cannot_run_past_eof(self) -> None:
        with self.assertRaisesRegex(ValueError, 'unterminated string'):
            b._wstr(b'no terminator', 0)
        with self.assertRaisesRegex(ValueError, 'outside file'):
            b._wstr(b'', 0)

    def test_instruction_must_fit_inside_code_region(self) -> None:
        with self.assertRaisesRegex(ValueError, 'truncated instruction'):
            b._decode(bytes([b.OP_NAT0 | 0xc0, 1]), 0, 2)

    def test_oversized_function_table_is_rejected(self) -> None:
        data = bytearray(fixture())
        struct.pack_into('<I', data, 0x20, 0xffffffff)
        with self.assertRaisesRegex(ValueError, 'tables or code'):
            b.parse(bytes(data))

    def test_zero_parameter_pointer_may_be_zero(self) -> None:
        data = bytearray(fixture())
        table = struct.unpack_from('<I', data, 0x24)[0]
        struct.pack_into('<I', data, table + 8, 0)
        self.assertEqual(b.parse(bytes(data)).funcs[0].types, b'')

    def test_nonzero_parameter_range_must_fit_file(self) -> None:
        data = bytearray(fixture())
        table = struct.unpack_from('<I', data, 0x24)[0]
        struct.pack_into('<II', data, table + 8, len(data) - 1, 8)
        with self.assertRaisesRegex(ValueError, 'parameter types'):
            b.parse(bytes(data))


if __name__ == '__main__':
    unittest.main()
