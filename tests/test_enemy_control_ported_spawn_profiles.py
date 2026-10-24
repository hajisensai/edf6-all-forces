"""Independent resource/negative tests for the ported SpawnProof generator.

Run: python -B tests/test_enemy_control_ported_spawn_profiles.py
No installed game or native DLL is needed.
"""
from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('ported_spawn_profiles', ROOT / 'tools/gen_enemy_control_ported_spawn_profiles.py')
assert spec and spec.loader
gen = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = gen
spec.loader.exec_module(gen)


class PortedSpawnProfilesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.registry = {'objects': [], 'files': {}}
        self.assets: dict[str, bytes] = {}

    def add(self, path: str, configured_class: str, values: dict | None = None) -> None:
        data = gen.sgo.write(0x102, {'xgs_scene_object_class': configured_class, **(values or {})})
        self.assets[path] = data
        self.registry['files'][path] = gen.sha256(data)
        if path.startswith('OBJECT/'):
            self.registry['objects'].append({'object': path, 'class': configured_class, 'files': [path]})

    def gather(self):
        return gen.gather(self.registry, self.assets.__getitem__)

    def ranger(self, weapon: str = 'WEAPON/EDF41_NPC/AISOLDIERRIFLE01.SGO') -> None:
        self.add('OBJECT/EDF41/AIARMYSOLDIER_AF.SGO', 'AssultSoldier', {
            'soldier_load_weapon': ['app:/' + weapon.lower()],
            'soldier_weapon_slot': [[[0.0], 'weapon', 0.0, 0.0]],
            'soldier_sub_weapon_slot': 0.0,
        })
        self.add(weapon, 'Weapon_BasicShoot', {'AmmoClass': 'SolidBullet01', 'FireType': 0.0, 'FireCount': 1.0, 'FireBurstCount': 1.0})
        self.registry['objects'][0]['files'].append(weapon)

    def test_robo_and_dragon_use_distinct_proved_bounds(self) -> None:
        self.add('OBJECT/UFOROBO401.SGO', 'UfoRobo')
        self.add('OBJECT/EDF41/DRAGONSMALL401.SGO', 'DragonSmall')
        rows, omitted = self.gather()
        self.assertEqual(omitted, [])
        self.assertEqual({row.configured_class: (row.members, row.emitters) for row in rows},
                         {'UfoRobo': (1, 0), 'DragonSmall': (1, 2)})

    def test_same_supported_class_does_not_authorize_another_robo_resource(self) -> None:
        self.add('OBJECT/UNREVIEWED_ROBO.SGO', 'UfoRobo')
        rows, omitted = self.gather()
        self.assertEqual(rows, [])
        self.assertEqual(len(omitted), 1)
        self.assertIn('unreviewed Robo', omitted[0].reason)

    def test_binary_hash_is_required(self) -> None:
        self.add('OBJECT/UFOROBO401.SGO', 'UfoRobo')
        self.assets['OBJECT/UFOROBO401.SGO'] += b'changed'
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            self.gather()

    def test_decoded_class_must_match_even_with_valid_hash(self) -> None:
        self.add('OBJECT/UFOROBO401.SGO', 'DragonSmall')
        self.registry['objects'][0]['class'] = 'UfoRobo'
        with self.assertRaisesRegex(ValueError, 'decoded class'):
            self.gather()

    def test_duplicate_and_traversal_paths_are_rejected(self) -> None:
        self.add('OBJECT/UFOROBO401.SGO', 'UfoRobo')
        duplicate = copy.deepcopy(self.registry['objects'][0])
        duplicate['object'] = duplicate['object'].lower()
        self.registry['objects'].append(duplicate)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.gather()
        for path in ['OBJECT/../X.SGO', 'app:/object/X.SGO', 'OBJECT/X..SGO', 'OBJECT//X.SGO', 'OBJECT/X.MRAB']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                gen.asset_path(path, 'OBJECT')

    def test_dependency_closure_includes_the_selected_sgo(self) -> None:
        self.add('OBJECT/UFOROBO401.SGO', 'UfoRobo')
        self.registry['objects'][0]['files'] = []
        with self.assertRaisesRegex(ValueError, 'dependency closure'):
            self.gather()

    def test_hector_counts_guns_and_all_possible_retained_muzzle_fields(self) -> None:
        values = {'BerserkerSettings': {'PartsShootList': []},
                  'gun_control': [{'gun_battery': {'muzzle': {'shape': 'left'}}},
                                  {'gun_battery': {'muzzle': {'shape': 'right'}}}]}
        self.assertEqual(gen.hector_budget(values), (1, 8))
        values['gun_control'].append({'gun_battery': {'muzzle': {'shape': 'extra'}}})
        self.assertEqual(gen.hector_budget(values), (1, 11))

    def test_nonempty_parts_or_unproved_muzzle_schema_never_default_to_one(self) -> None:
        values = {'BerserkerSettings': {'PartsShootList': ['dynamic child']}, 'gun_control': []}
        with self.assertRaisesRegex(gen.UnsupportedBudget, 'PartsShootList'):
            gen.hector_budget(values)
        values = {'BerserkerSettings': {'PartsShootList': []},
                  'gun_control': [{'gun_battery': {'muzzle': [{'shape': 'unknown-list'}]}}]}
        with self.assertRaisesRegex(gen.UnsupportedBudget, 'muzzle'):
            gen.hector_budget(values)

    def test_missing_parts_follows_the_explicit_native_proof_gate(self) -> None:
        values = {'BerserkerSettings': {}, 'gun_control': []}
        if gen.MISSING_PARTS_IS_EMPTY:
            self.assertEqual(gen.hector_budget(values), (1, 2))
        else:
            with self.assertRaises(gen.UnsupportedBudget):
                gen.hector_budget(values)

    def test_npc_known_weapon_includes_both_retained_bullet_cache_fields(self) -> None:
        self.ranger()
        rows, omitted = self.gather()
        self.assertEqual(omitted, [])
        self.assertEqual((rows[0].members, rows[0].emitters, rows[0].max_weapons, rows[0].max_held), (1, 3, 1, 1))
        self.assertEqual(rows[0].dependencies[0][0], 'WEAPON/EDF41_NPC/AISOLDIERRIFLE01.SGO')

    def test_npc_held_bound_uses_fire_count_not_magazine_size(self) -> None:
        self.ranger()
        weapon = 'WEAPON/EDF41_NPC/AISOLDIERRIFLE01.SGO'
        self.add(weapon, 'Weapon_BasicShoot', {'AmmoClass': 'SolidBullet01', 'FireType': 0.0,
                                            'FireCount': 10.0, 'FireBurstCount': 1.0, 'AmmoCount': 999.0})
        rows, omitted = self.gather()
        self.assertEqual(omitted, [])
        self.assertEqual((rows[0].emitters, rows[0].max_held), (21, 10))

    def test_npc_wrong_burst_and_slot_mapping_have_no_implicit_bound(self) -> None:
        self.ranger()
        weapon = 'WEAPON/EDF41_NPC/AISOLDIERRIFLE01.SGO'
        self.add(weapon, 'Weapon_BasicShoot', {'AmmoClass': 'SolidBullet01', 'FireType': 0.0,
                                            'FireCount': 1.0, 'FireBurstCount': 2.0})
        rows, omitted = self.gather()
        self.assertEqual(rows, [])
        self.assertIn('burst', omitted[0].reason)
        values = {'xgs_scene_object_class': 'AssultSoldier',
                  'soldier_load_weapon': ['app:/weapon/edf41_npc/aisoldierrifle01.sgo'],
                  'soldier_weapon_slot': [[[1.0], 'weapon', 0.0, 0.0]], 'soldier_sub_weapon_slot': 0.0}
        with self.assertRaisesRegex(gen.UnsupportedBudget, 'mapping'):
            gen.npc_budget(values, self.registry, self.assets.__getitem__)

    def test_small_battery_bound_comes_from_each_actual_ported_muzzle(self) -> None:
        values = {'bullet_object': ['factory'],
                  'battery_config': [[f'muzzle{n}', ['bullet_object', 4, 0.0, 5.5, 0, 0, 3, 1, 0.0], 0] for n in range(6)]}
        self.assertEqual(gen.small_battery_budget(values), (1, 6))
        values['battery_config'][1][0] = 'muzzle0'
        with self.assertRaisesRegex(gen.UnsupportedBudget, 'duplicated'):
            gen.small_battery_budget(values)

    def test_heavy_actual_two_weapon_slots_include_swing_and_distinct_shield_cache(self) -> None:
        object_path = 'OBJECT/EDF41/AIHEAVYARMOR_SP.SGO'
        weapons = ['WEAPON/EDF41_NPC/AIHEAVYPILEBANKER01.SGO', 'WEAPON/EDF41_NPC/AIHEAVYSHIELD01.SGO']
        self.add(object_path, 'HeavyArmor', {
            'soldier_load_weapon': ['app:/' + weapon.lower() for weapon in weapons],
            'soldier_weapon_slot': [[[0.0], 'weaponL', 0.0, 0.0], [[1.0], 'weaponR', 0.0, 0.0]],
            'soldier_sub_weapon_slot': 0.0,
        })
        for weapon in weapons:
            cls, ammo = gen.NPC_WEAPONS[weapon]
            self.add(weapon, cls, {'AmmoClass': ammo, 'FireType': 0.0, 'FireCount': 1.0, 'FireBurstCount': 1.0})
        self.registry['objects'][0]['files'] += weapons
        rows, omitted = self.gather()
        self.assertEqual(omitted, [])
        self.assertEqual((rows[0].members, rows[0].emitters, rows[0].max_weapons, rows[0].max_held), (1, 6, 2, 1))
        self.assertEqual(len(rows[0].dependencies), 2)

    def test_npc_provider_context_limits_are_not_exceeded(self) -> None:
        for value in [0, -1, 1.5, 16, float('inf'), float('nan')]:
            with self.subTest(value=value), self.assertRaises(gen.UnsupportedBudget):
                gen.held_bound(value)
        self.ranger()
        values = {'xgs_scene_object_class': 'HeavyArmor',
                  'soldier_load_weapon': ['app:/weapon/edf41_npc/aisoldierrifle01.sgo'] * 3,
                  'soldier_weapon_slot': [[[float(index)], 'weapon', 0.0, 0.0] for index in range(3)],
                  'soldier_sub_weapon_slot': 0.0}
        with self.assertRaisesRegex(gen.UnsupportedBudget, 'roster/slot'):
            gen.npc_budget(values, self.registry, self.assets.__getitem__)

    def test_npc_unknown_weapon_has_no_invented_emitter_budget(self) -> None:
        self.ranger('WEAPON/EDF41_NPC/UNPROVED.SGO')
        rows, omitted = self.gather()
        self.assertEqual(rows, [])
        self.assertEqual(len(omitted), 1)

    def test_render_is_independent_of_registry_object_order(self) -> None:
        self.add('OBJECT/UFOROBO401.SGO', 'UfoRobo')
        self.add('OBJECT/EDF41/DRAGONSMALL401.SGO', 'DragonSmall')
        first = gen.render(*self.gather())
        self.registry['objects'].reverse()
        self.assertEqual(first, gen.render(*self.gather()))
        self.assertTrue(first.endswith('\n'))

    def test_real_shipped_binary_registry_and_manifest(self) -> None:
        registry = json.loads(gen.REGISTRY.read_text(encoding='utf-8'))
        read = lambda rel: gen.ASSETS.joinpath(*rel.split('/')).read_bytes()
        rows, omitted = gen.gather(registry, read)
        counts: dict[str, int] = {}
        for row in rows:
            counts[row.configured_class] = counts.get(row.configured_class, 0) + 1
            self.assertEqual(row.members, 1)
            self.assertLessEqual(row.emitters, 512)
        self.assertEqual(counts['UfoRobo'], 1)
        self.assertEqual(counts['DragonSmall'], 7)
        if gen.MISSING_PARTS_IS_EMPTY:
            self.assertEqual(counts['EDF6_Berserker_Large'], 21)
            self.assertTrue(all(row.emitters == 8 for row in rows if row.configured_class == 'EDF6_Berserker_Large'))
        self.assertEqual(counts['AssultSoldier'], 12)
        self.assertEqual(counts['PaleWing'], 10)
        self.assertEqual(len(rows), 75)
        self.assertEqual(omitted, [])
        self.assertEqual(counts['HeavyArmor'], 6)
        self.assertTrue(all((row.members, row.emitters, row.max_weapons, row.max_held) == (1, 6, 2, 1)
                            for row in rows if row.configured_class == 'HeavyArmor'))
        self.assertEqual({row.max_held for row in rows if row.configured_class == 'AssultSoldier'}, {1, 10})
        self.assertEqual({row.max_held for row in rows if row.configured_class == 'PaleWing'}, {1, 15})
        self.assertEqual({row.emitters for row in rows if row.configured_class == 'UfoSmall515'}, {6})
        self.assertEqual(gen.OUTPUT.read_bytes(), gen.render(rows, omitted).encode('utf-8'))


if __name__ == '__main__':
    unittest.main()
