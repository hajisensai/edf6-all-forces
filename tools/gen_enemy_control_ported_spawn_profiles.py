"""Generate separate spawn proofs for audited EDF4.1 ported resources.

  python -B tools/gen_enemy_control_ported_spawn_profiles.py
  python -B tools/gen_enemy_control_ported_spawn_profiles.py --check

Inputs are the tracked edf41port/objects.json registry and the shipped binary
SGO files in edf41port/objects/. No Root.cpk, installed Mods, game DLL loading or
ported/native path substitution is involved. Each admitted resource must match
its registry SHA256 and exact xgs_scene_object_class. Unsupported class/weapon
budgets are reported explicitly, never replaced with guessed one-slot bounds.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'pylib'))
import sgo  # noqa: E402

REGISTRY = ROOT / 'edf41port/objects.json'
ASSETS = ROOT / 'edf41port/objects'
OUTPUT = ROOT / 'src/enemy_control_ported_spawn_profiles.inc'
FIXED_BODY_EMITTERS = {'DragonSmall': 2, 'GiantAnt': 2, 'GiantBee': 2, 'AlienTrailer': 1, 'Nephila': 1, 'UfoSmall507': 1}
TARGET_CLASSES = frozenset(('UfoRobo', 'EDF6_Berserker_Large', 'AssultSoldier', 'PaleWing', 'HeavyArmor', 'UfoSmall515', *FIXED_BODY_EMITTERS))
# Native missing key is exactly an empty PartsShootList: D750F/19 clears the
# missing child handle; 342631/648 calls 346F30(root+17E8,0,...) to clear count
# (3470DC/EB); 342696..69C skips all entries for the missing handle tree.
MISSING_PARTS_IS_EMPTY = True
# The current NPC provider contract is deliberately narrower than all Soldier
# resources. Every BasicShoot weapon can retain at most FireCount bullets: the
# 69749F..69759D live-list path skips the new-shot factory until they expire.
NPC_WEAPONS = {
    'WEAPON/EDF41_NPC/AISOLDIERRIFLE01.SGO': ('Weapon_BasicShoot', 'SolidBullet01'),
    'WEAPON/EDF41_NPC/AISOLDIERFLAMETHROWER01.SGO': ('Weapon_BasicShoot', 'FlameBullet02'),
    'WEAPON/EDF41_NPC/AISOLDIERROCKETLAUNCHER01.SGO': ('Weapon_BasicShoot', 'RocketBullet01'),
    'WEAPON/EDF41_NPC/AISOLDIERSHOTGUN01.SGO': ('Weapon_BasicShoot', 'SolidBullet01'),
    'WEAPON/EDF41_NPC/AISOLDIERSNIPERRIFLE02.SGO': ('Weapon_BasicShoot', 'SolidBullet01'),
    'WEAPON/EDF41_NPC/AISOLDIERLASERRIFLE01.SGO': ('Weapon_BasicShoot', 'LaserBullet02'),
    'WEAPON/EDF41_NPC/AISOLDIERLASERRIFLE02.SGO': ('Weapon_BasicShoot', 'LaserBullet02'),
    'WEAPON/EDF41_NPC/AIIZNR01.SGO': ('Weapon_BasicShoot', 'LightningBullet01'),
    'WEAPON/EDF41_NPC/AIIZNR02.SGO': ('Weapon_BasicShoot', 'LightningBullet01'),
    'WEAPON/EDF41_NPC/AILASERLANCE01.SGO': ('Weapon_BasicShoot', 'LaserBullet01'),
    'WEAPON/EDF41_NPC/AILAZR01.SGO': ('Weapon_BasicShoot', 'LaserBullet01'),
    'WEAPON/EDF41_NPC/AILAZR02.SGO': ('Weapon_BasicShoot', 'LaserBullet01'),
    'WEAPON/EDF41_NPC/AIMNS01.SGO': ('Weapon_BasicShoot', 'LaserBullet01'),
    'WEAPON/EDF41_NPC/AITB01.SGO': ('Weapon_BasicShoot', 'LightningBullet01'),
    'WEAPON/EDF41_NPC/AITB02.SGO': ('Weapon_BasicShoot', 'LightningBullet01'),
    'WEAPON/EDF41_NPC/AIHEABYMIDDLECANNON01.SGO': ('Weapon_HeavyShoot', 'SolidBullet01'),
    'WEAPON/EDF41_NPC/AIHEAVYCANNON01.SGO': ('Weapon_HeavyShoot', 'SolidBullet01'),
    'WEAPON/EDF41_NPC/AIHEAVYPILEBANKER01.SGO': ('Weapon_Swing', 'PileBunkerBullet01'),
    'WEAPON/EDF41_NPC/AIHEAVYSHIELD01.SGO': ('Weapon_Shield', 'ShieldBashBullet01'),
}
NPC_SUPPORTED_CLASSES = frozenset(('AssultSoldier', 'PaleWing', 'HeavyArmor'))
# Match the typed provider's finite immutable Context table. Swing's combo
# FireOptions+18 is reset to zero; Shield's EF0/E98/EA0 has a separate typed
# ownership path, but at most two retained cache fields per held object.
NPC_WEAPON_LIMIT = 2
NPC_HELD_LIMIT = 15


class UnsupportedBudget(ValueError):
    """An explicit unsupported path, published as an omitted manifest row."""


@dataclass(frozen=True)
class Row:
    path: str
    configured_class: str
    members: int
    emitters: int
    source_sha256: str
    dependencies: tuple[tuple[str, str], ...] = ()
    max_weapons: int = 0
    max_held: int = 0


@dataclass(frozen=True)
class Omitted:
    path: str
    reason: str


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def asset_path(value: object, folder: str | None = None) -> str:
    if not isinstance(value, str) or not 1 <= len(value) <= 248:
        raise ValueError(f'invalid registered asset path: {value!r}')
    parts = value.split('/')
    if len(parts) < 2 or parts[0].upper() not in ('OBJECT', 'WEAPON'):
        raise ValueError(f'expected OBJECT/ or WEAPON/ registered path: {value!r}')
    if folder and parts[0].upper() != folder:
        raise ValueError(f'expected {folder}/ resource: {value!r}')
    if any(not re.fullmatch(r'[A-Za-z0-9_-][A-Za-z0-9_.-]*', part) or '..' in part for part in parts):
        raise ValueError(f'unsafe registered asset path: {value!r}')
    if not parts[-1].upper().endswith('.SGO'):
        raise ValueError(f'expected a binary SGO resource: {value!r}')
    return value.upper()


def weapon_path(value: object) -> str:
    if not isinstance(value, str) or not value.lower().startswith('app:/weapon/'):
        raise ValueError(f'expected explicit app:/weapon/ reference: {value!r}')
    return asset_path(value[5:], 'WEAPON')


def configured_path(rel: str) -> str:
    return 'app:/object/' + rel.removeprefix('OBJECT/')[:-4] + '.sgo'


def checked_read(registry: dict, rel: str, read: Callable[[str], bytes]) -> bytes:
    expected = registry.get('files', {}).get(rel)
    if not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected):
        raise ValueError(f'{rel}: no exact registry SHA256')
    data = read(rel)
    if sha256(data) != expected:
        raise ValueError(f'{rel}: binary bytes do not match the registry SHA256')
    return data


def hector_budget(values: dict) -> tuple[int, int]:
    settings = values.get('BerserkerSettings')
    if not isinstance(settings, dict):
        raise ValueError('Hector: missing BerserkerSettings')
    if 'PartsShootList' not in settings:
        if not MISSING_PARTS_IS_EMPTY:
            raise UnsupportedBudget('missing PartsShootList has no enabled native empty-table proof')
    elif settings['PartsShootList'] != []:
        raise UnsupportedBudget('nonempty or unknown PartsShootList needs independent child-member bounds')
    # The ordinary root contributes two derived emitters. Each configured GB2
    # contributes one owner emitter plus two fields for each live kept muzzle
    # bullet. A single shape dictionary is exactly one native muzzle; unknown
    # multi-muzzle representations are not accepted by this ported contract.
    guns = values.get('gun_control')
    if not isinstance(guns, list) or len(guns) > 256:
        raise ValueError('Hector: missing or unbounded gun_control')
    emitters = 2
    for gun in guns:
        if not isinstance(gun, dict) or not isinstance(gun.get('gun_battery'), dict):
            raise ValueError('Hector: unknown gun_control entry')
        muzzle = gun['gun_battery'].get('muzzle')
        if not isinstance(muzzle, dict) or set(muzzle) != {'shape'} or not isinstance(muzzle['shape'], str) or not muzzle['shape']:
            raise UnsupportedBudget('Hector: unknown GB2 muzzle count')
        emitters += 1 + 2
    if emitters > 512:
        raise UnsupportedBudget('Hector: typed emitter bound exceeds lease capacity')
    return 1, emitters


def small_battery_budget(values: dict) -> tuple[int, int]:
    rows = values.get('battery_config')
    if not isinstance(rows, list) or not 1 <= len(rows) <= 512:
        raise UnsupportedBudget('UfoSmall515: missing or unbounded small-battery configuration')
    names: set[str] = set()
    for row in rows:
        if not isinstance(row, list) or len(row) != 3:
            raise UnsupportedBudget('UfoSmall515: unknown small-battery row')
        muzzle, params, phase = row
        if not isinstance(muzzle, str) or not muzzle or muzzle in names:
            raise UnsupportedBudget('UfoSmall515: invalid or duplicated muzzle')
        names.add(muzzle)
        if not isinstance(params, list) or len(params) != 9 or params[0] != 'bullet_object' or 'bullet_object' not in values:
            raise UnsupportedBudget('UfoSmall515: unknown small-battery resource parameters')
        if type(params[1]) is not int or type(phase) is not int:
            raise UnsupportedBudget('UfoSmall515: invalid small-battery resource/phase')
    return 1, len(rows)


def held_bound(value: object) -> int:
    if type(value) not in (int, float) or not 0 < value <= NPC_HELD_LIMIT or not float(value).is_integer():
        raise UnsupportedBudget('NPC FireCount has no positive integral held-bullet bound')
    return int(value)


def npc_budget(values: dict, registry: dict, read: Callable[[str], bytes]) -> tuple[int, int, tuple[tuple[str, str], ...], int, int]:
    if values['xgs_scene_object_class'] not in NPC_SUPPORTED_CLASSES:
        raise UnsupportedBudget('NPC class has no completed typed collector contract')
    loaded = values.get('soldier_load_weapon')
    slots = values.get('soldier_weapon_slot')
    if not isinstance(loaded, list) or not isinstance(slots, list) or not 1 <= len(loaded) <= NPC_WEAPON_LIMIT or len(slots) != len(loaded):
        raise UnsupportedBudget('NPC requires a separately proved roster/slot bound')
    if values.get('soldier_sub_weapon_slot') != 0:
        raise UnsupportedBudget('NPC secondary weapon slots are not covered')
    for index, slot in enumerate(slots):
        if not isinstance(slot, list) or len(slot) != 4 or slot[0] != [float(index)]:
            raise UnsupportedBudget('NPC weapon slot mapping does not match the configured roster')
    dependencies = []
    maximum_held = 0
    for reference in loaded:
        rel = weapon_path(reference)
        expected = NPC_WEAPONS.get(rel)
        if expected is None:
            raise UnsupportedBudget(f'NPC weapon has no completed emission contract: {rel}')
        raw = checked_read(registry, rel, read)
        weapon = sgo.load(data=raw)
        if not isinstance(weapon, dict) or (weapon.get('xgs_scene_object_class'), weapon.get('AmmoClass')) != expected:
            raise ValueError(f'{rel}: weapon class/AmmoClass changed')
        if weapon.get('FireType') != 0 or weapon.get('FireBurstCount') != 1:
            raise UnsupportedBudget(f'{rel}: unsupported retained/continuous FireType or burst count')
        maximum_held = max(maximum_held, held_bound(weapon.get('FireCount')))
        dependencies.append((rel, sha256(raw)))
    count = len(loaded)
    emitters = count * (1 + 2 * maximum_held)
    if emitters > 512:
        raise UnsupportedBudget('NPC held-bullet emitter bound exceeds lease capacity')
    return 1, emitters, tuple(dependencies), count, maximum_held


def gather(registry: dict, read: Callable[[str], bytes]) -> tuple[list[Row], list[Omitted]]:
    if not isinstance(registry, dict) or not isinstance(registry.get('objects'), list) or not isinstance(registry.get('files'), dict):
        raise ValueError('invalid ported object registry')
    rows: list[Row] = []
    omitted: list[Omitted] = []
    seen: set[str] = set()
    for entry in registry['objects']:
        if not isinstance(entry, dict):
            raise ValueError('invalid object registry entry')
        rel = asset_path(entry.get('object'), 'OBJECT')
        if rel in seen:
            raise ValueError(f'{rel}: duplicate case-insensitive object path')
        seen.add(rel)
        declared = entry.get('class')
        if declared not in TARGET_CLASSES:
            continue
        if not isinstance(entry.get('files'), list) or rel not in entry['files']:
            raise ValueError(f'{rel}: own SGO is missing from the registry dependency closure')
        raw = checked_read(registry, rel, read)
        values = sgo.load(data=raw)
        if not isinstance(values, dict) or values.get('xgs_scene_object_class') != declared:
            raise ValueError(f'{rel}: decoded class does not match the registered configured class')
        dependencies: tuple[tuple[str, str], ...] = ()
        max_weapons = max_held = 0
        try:
            if declared == 'UfoRobo':
                if rel != 'OBJECT/UFOROBO401.SGO':
                    raise UnsupportedBudget('unreviewed Robo resource has no setup/emitter closure')
                members, emitters = 1, 0
            elif declared in FIXED_BODY_EMITTERS:
                # Native IP + common contact. The ported Breathe path derives its
                # per-shot team from the owner and has no retained team cache.
                members, emitters = 1, FIXED_BODY_EMITTERS[declared]
            elif declared == 'UfoSmall515':
                members, emitters = small_battery_budget(values)
            elif declared == 'EDF6_Berserker_Large':
                members, emitters = hector_budget(values)
            else:
                members, emitters, dependencies, max_weapons, max_held = npc_budget(values, registry, read)
                if any(dependency not in entry['files'] for dependency, _hash in dependencies):
                    raise ValueError(f'{rel}: weapon SGO is outside its registered dependency closure')
        except UnsupportedBudget as error:
            omitted.append(Omitted(configured_path(rel), str(error)))
            continue
        rows.append(Row(configured_path(rel), declared, members, emitters, sha256(raw), dependencies, max_weapons, max_held))
    return sorted(rows, key=lambda row: row.path), sorted(omitted, key=lambda row: row.path)


def render(rows: list[Row], omitted: list[Omitted]) -> str:
    canonical = [{'path': row.path, 'class': row.configured_class, 'members': row.members,
                  'emitters': row.emitters, 'max_weapons': row.max_weapons, 'max_held': row.max_held,
                  'sha256': row.source_sha256, 'dependencies': row.dependencies} for row in rows]
    canonical += [{'omitted': row.path, 'reason': row.reason} for row in omitted]
    selection = sha256(json.dumps(canonical, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('utf-8'))
    lines = [
        '// Generated by tools/gen_enemy_control_ported_spawn_profiles.py. Do not edit.',
        '// Separate EDF4.1 registry + shipped binary SGOs; never substituted from Root.cpk.',
        '// {configured SGO, exact configured class, maxMembers, maxEmitters[, maxWeapons, maxHeld]}',
        f'// Canonical source/configuration selection SHA256: {selection}',
        '// Robo: one configured GOB, no retained emitter cache; setup runs only at adoption.',
        '// DragonSmall: one GOB, native IP + contact; ported Breathe has per-shot owner derivation.',
        '// Hector: no PartsShootList members; two root emitters + sum(1 + 2*muzzleCount) over GB2.',
        '// NPC: maxWeapons*(1+2*maxHeld); FireType=0, FireBurstCount=1, maxHeld=max(FireCount).',
        '// Heavy: real two-weapon roster; Swing combo override=0; Shield has its distinct typed EF0 cache.',
        '// Non-NPC rows omit the final two zero-initialized NPC-only bounds.',
        f'// {len(rows)} proved resources; {len(omitted)} explicitly unsupported candidate resources.',
    ]
    for row in omitted:
        lines.append(f'// Omitted {row.path}: {row.reason}.')
    for row in rows:
        npc = f',{row.max_weapons},{row.max_held}' if row.max_weapons else ''
        lines.append(f'    {{L"{row.path}","{row.configured_class}",{row.members},{row.emitters}{npc}}}, // SHA256 {row.source_sha256}')
        for dependency, dependency_hash in row.dependencies:
            lines.append(f'    // Weapon {dependency}; SHA256 {dependency_hash}')
    return '\n'.join(lines) + '\n'


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registry', type=Path, default=REGISTRY)
    parser.add_argument('--assets', type=Path, default=ASSETS)
    parser.add_argument('--output', type=Path, default=OUTPUT)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args(argv)
    try:
        registry = json.loads(args.registry.read_text(encoding='utf-8-sig'))
        def read(rel: str) -> bytes:
            return args.assets.joinpath(*rel.split('/')).read_bytes()
        rows, omitted = gather(registry, read)
        text = render(rows, omitted).encode('utf-8')
        if args.check:
            if not args.output.is_file() or args.output.read_bytes() != text:
                print(f'STALE: {args.output}', file=sys.stderr)
                return 1
            print(f'OK: {len(rows)} ported spawn profiles; {len(omitted)} explicit unsupported resources')
            return 0
        args.output.write_bytes(text)
        print(f'wrote {args.output}: {len(rows)} profiles; {len(omitted)} explicit unsupported resources')
        return 0
    except (OSError, ValueError, KeyError, TypeError, IndexError, RecursionError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
