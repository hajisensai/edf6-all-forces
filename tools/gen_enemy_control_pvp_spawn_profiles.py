"""Generate the audited native controlled-enemy spawn reservation profiles.

  python -B tools/gen_enemy_control_pvp_spawn_profiles.py
  python -B tools/gen_enemy_control_pvp_spawn_profiles.py --check

The catalog only selects candidates. Every output path must exist directly in
Root.cpk/OBJECT and its original SGO must name the exact audited native class.
Mods, ported resource registries and Chunk archives are never consulted. The
native DLL is hashed/read as data; it is never loaded or executed. --check does
not write files. The only generation output is the requested .inc file.

maxMembers=1 is an audited constructor/lifecycle property, not a default inferred
from a class name or an SGO. The fixed body families below do not create child
GameObjects: their contact allocations are ordinary components (new 0x130 + 0x10),
small batteries/GB2 are components, and emitted bullets are not lease members.
Nephila's global team-6 SpiderNet is shared environment, not a child GameObject.
The separate TimeShip/transport/BigGreyBoss child paths are deliberately absent.
Emitter bounds mirror the typed provider contracts, including common contact GDI.
The exact candidate snapshot is embedded for clean-checkout reproducibility when
the temporary source catalog is absent; --catalog accepts a refreshed catalog.
Adding a class requires proving both bounds before extending CONTRACTS.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'pylib'))
import rootcpk  # noqa: E402
import sgo  # noqa: E402

TIMESTAMP = 0x678CCB46
DLL_SHA256 = '0d5092393a1b9d8f1b75b0c93d8a8f65615a36a92c46e40349c2b66c733d0916'
DEFAULT_CATALOG = ROOT / 'tmp/enemy-family-catalog-20261019.json'
DEFAULT_OUTPUT = ROOT / 'src/enemy_control_pvp_spawn_profiles.inc'
PROFILES = ROOT / 'src/enemy_control_native_profiles.inc'


@dataclass(frozen=True)
class Contract:
    vtable: int
    emitters: int | None
    evidence: str


# Fixed, separately audited single-GameObject providers. None means a verified
# resource-dependent bound is required; it never falls back to one emitter.
CONTRACTS = {
    'AlienTrailer': Contract(0x17A9650, 1, 'inline Barrier 0x10D0; model/barrier components only'),
    'DragonSmall': Contract(0x17ABA40, 2, 'IP 0x15B0 + common contact GDI 0xF30'),
    'GiantAnt': Contract(0x17BB798, 2, 'IP 0x1680 + common contact GDI'),
    'GiantAntEX': Contract(0x17BC648, 3, 'Ant emitters + Contact pointer/control 0x2140/0x2148'),
    'GiantBee': Contract(0x17BCEE0, 2, 'IP 0x16D0 + common contact GDI'),
    'GiantBeeEX': Contract(0x17BD638, 3, 'Bee emitters + Contact pointer/control 0x2240/0x2248'),
    'GiantDango': Contract(0x17BDAC0, 2, 'common contact GDI + root rolling damage method'),
    'GiantSpider': Contract(0x17BE120, 2, 'IP 0x1AA0 + common contact GDI'),
    'Monster501': Contract(0x17C5628, 1, 'inline IP 0x1B30; no independent meteor child'),
    'Monster504': Contract(0x17C5A60, 1, 'inline IP 0x1A70; no independent meteor child'),
    'Nephila': Contract(0x17C84C8, 1, 'inline IP 0x10D0; SpiderNet is shared global environment'),
    'UfoSmall507': Contract(0x17CC2A8, 1, 'inline IP 0x700; owner team refreshed each volley'),
    'UfoSmall515': Contract(0x17CC838, None, 'battery_config count; inline small batteries, stride 0x1B0'),
}


# Exact candidate snapshot from tmp/enemy-family-catalog-20261019.json. This
# tracked copy makes a clean checkout reproducible when the temporary audit
# catalog is absent. An explicit --catalog still validates a refreshed input.
CANDIDATE_SNAPSHOT = {'AlienTrailer': ('E513_SHIELDBEARER.SGO',
                  'E513_SHIELDBEARER_FIXED.SGO',
                  'E513_SHIELDBEARER_FIXEDL.SGO',
                  'E513_SHIELDBEARER_FIXEDXL.SGO',
                  'E513_SHIELDBEARER_L.SGO',
                  'E513_SHIELDBEARER_XL.SGO'),
 'DragonSmall': ('DRAGONSMALL401.SGO',
                 'DRAGONSMALL401_HS.SGO',
                 'REDSMALLDRAGON.SGO',
                 'REDSMALLDRAGON_HS.SGO',
                 'YERROWSMALLDRAGON.SGO',
                 'YERROWSMALLDRAGON_HS.SGO'),
 'GiantAnt': ('E650_GIANTANT01.SGO',
              'E650_GIANTANT01_NETLIGHTSYNCMODE.SGO',
              'E650_GIANTANT02B.SGO',
              'E650_GIANTANT03B.SGO',
              'E650_GIANTANT_GOLD.SGO',
              'E650_GIANTANT_QUEEN.SGO',
              'E650_GIANTANT_QUEEN_GOLD.SGO',
              'GIANTANT01.SGO',
              'GIANTANT01B.SGO',
              'GIANTANT01NETLIGHTSYNCMODE.SGO',
              'GIANTANT02B.SGO',
              'GIANTANT03B.SGO',
              'GIANTANT04.SGO',
              'GIANTANT04SB.SGO',
              'GIANTANTQUEEN.SGO',
              'GOLDANT.SGO',
              'GOLDANTQUEEN.SGO'),
 'GiantAntEX': ('E662_GIANTANT_QUEEN_RED.SGO', 'E665_GIANTANT_MEDIUM.SGO'),
 'GiantBee': ('GIANTBEE01.SGO',
              'GIANTBEE01CHARGE.SGO',
              'GIANTBEE02CHARGE.SGO',
              'GIANTBEEQUEEN.SGO',
              'GIANTBEEQUEEN02.SGO'),
 'GiantBeeEX': ('E668_GIANTBEE_MEDIUM.SGO', 'E668_GIANTBEE_QUEEN.SGO'),
 'GiantDango': ('E514_DANGO.SGO',),
 'GiantSpider': ('E651_SPIDER01.SGO',
                 'E651_SPIDER01_LIGHT.SGO',
                 'E651_SPIDER01_NETLIGHTSYNCMODE.SGO',
                 'E651_SPIDER01_SILVER.SGO',
                 'E651_SPIDER_QUEEN.SGO',
                 'GIANTSPIDER01.SGO',
                 'GIANTSPIDER01NETLIGHTSYNCMODE.SGO',
                 'GIANTSPIDERQUEEN.SGO',
                 'SILVERSPIDER.SGO',
                 'SILVERSPIDERQUEEN.SGO'),
 'Monster501': ('MONSTER501.SGO', 'MONSTER501_KING.SGO'),
 'Monster504': ('E504_MONSTERA.SGO', 'E504_MONSTERA_M034_3.SGO'),
 'Nephila': ('NEPHILA.SGO',),
 'UfoSmall507': ('E507_GOLDUFO.SGO',
                 'E507_GOLDUFO_ACE.SGO',
                 'E507_GOLDUFO_BIG.SGO',
                 'E507_GOLDUFO_CHARGE.SGO',
                 'E507_GOLDUFO_HI.SGO'),
 'UfoSmall515': ('E515_IMPERIALUFO.SGO', 'E515_IMPERIALUFO_ACE.SGO', 'E515_IMPERIALUFO_PROTOTYPE.SGO')}

@dataclass(frozen=True)
class Row:
    path: str
    native_class: str
    vtable: int
    members: int
    emitters: int
    digest: str


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def native_dll(game: Path) -> None:
    """Check the exact supported native image without loading it."""
    data = (game / 'EDF.dll').read_bytes()
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise ValueError('EDF.dll is not a PE image')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if pe > len(data) - 24 or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('EDF.dll has no valid PE header')
    timestamp = struct.unpack_from('<I', data, pe + 8)[0]
    if timestamp != TIMESTAMP or digest(data) != DLL_SHA256:
        raise ValueError('EDF.dll does not match the audited 678CCB46 native contracts')


def profile_vtables(data: bytes) -> dict[str, int]:
    rows = re.findall(r'\{"([A-Za-z0-9_]+)",(0x[0-9A-Fa-f]+),', data.decode('utf-8-sig'))
    out: dict[str, int] = {}
    for name, address in rows:
        if name in out:
            raise ValueError(f'duplicate native profile {name}')
        out[name] = int(address, 16)
    for name, contract in CONTRACTS.items():
        if out.get(name) != contract.vtable:
            raise ValueError(f'{name}: native profile vtable changed or is missing')
    return out


def object_name(value: object) -> str:
    """Only a direct stock OBJECT filename, never a ported/relative path."""
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-][A-Za-z0-9_.-]*\.SGO', value, re.I):
        raise ValueError(f'invalid direct OBJECT SGO filename: {value!r}')
    if '..' in value or len(value) > 128:
        raise ValueError(f'invalid OBJECT SGO filename: {value!r}')
    return value.upper()


def emitter_bound(native_class: str, values: dict[str, object], contract: Contract) -> int:
    if contract.emitters is not None:
        return contract.emitters
    if native_class != 'UfoSmall515':
        raise ValueError(f'{native_class}: no audited emitter bound')
    # The native constructor allocates one stride-0x1B0 small GunBattery for
    # each battery_config row; its current data/count live at +0x1298/+0x12A8.
    batteries = values.get('battery_config')
    if not isinstance(batteries, list) or not 1 <= len(batteries) <= 512:
        raise ValueError('UfoSmall515: missing or unbounded battery_config')
    muzzles: set[str] = set()
    for row in batteries:
        if not isinstance(row, list) or len(row) != 3:
            raise ValueError('UfoSmall515: unknown battery_config row layout')
        muzzle, params, phase = row
        if not isinstance(muzzle, str) or not muzzle or not muzzle.isascii() or muzzle in muzzles:
            raise ValueError('UfoSmall515: invalid or duplicated muzzle')
        muzzles.add(muzzle)
        if not isinstance(params, list) or len(params) != 9 or params[0] != 'bullet_object':
            raise ValueError('UfoSmall515: unknown small-battery parameters')
        if type(params[1]) is not int or type(phase) is not int or 'bullet_object' not in values:
            raise ValueError('UfoSmall515: invalid small-battery resource or phase')
    return len(batteries)


def load_catalog(path: Path | None = None) -> bytes:
    if path is not None:
        return path.read_bytes()
    if DEFAULT_CATALOG.is_file():
        return DEFAULT_CATALOG.read_bytes()
    snapshot = {'game_timestamp': f'0x{TIMESTAMP:08X}',
                'classes': {name: [{'object': value} for value in values]
                            for name, values in CANDIDATE_SNAPSHOT.items()}}
    return json.dumps(snapshot, sort_keys=True, separators=(',', ':')).encode('utf-8')


def selection_digest(catalog_data: bytes) -> str:
    """Hash the canonical selected candidates, independent of JSON formatting."""
    catalog = json.loads(catalog_data.decode('utf-8-sig'))
    selected = {name: sorted(object_name(row['object']) for row in catalog['classes'][name])
                for name in sorted(CONTRACTS)}
    snapshot = {'game_timestamp': catalog['game_timestamp'], 'classes': selected}
    return digest(json.dumps(snapshot, sort_keys=True, separators=(',', ':')).encode('utf-8'))


def gather(catalog_data: bytes, game: rootcpk.Game, profiles: bytes) -> list[Row]:
    profile_vtables(profiles)
    catalog = json.loads(catalog_data.decode('utf-8-sig'))
    if not isinstance(catalog, dict) or catalog.get('game_timestamp') != f'0x{TIMESTAMP:08X}':
        raise ValueError('catalog does not identify the supported native image')
    classes = catalog.get('classes')
    if not isinstance(classes, dict):
        raise ValueError('catalog has no classes mapping')
    stock: dict[str, str] = {}
    for actual in game.names('OBJECT'):
        key = actual.upper()
        if key in stock:
            raise ValueError(f'case-ambiguous Root.cpk OBJECT member {actual}')
        stock[key] = actual
    rows: dict[str, Row] = {}
    for native_class, contract in sorted(CONTRACTS.items()):
        entries = classes.get(native_class)
        if not isinstance(entries, list) or not entries:
            raise ValueError(f'{native_class}: catalog has no audited resource candidates')
        for entry in entries:
            if not isinstance(entry, dict):
                raise ValueError(f'{native_class}: invalid catalog resource entry')
            name = object_name(entry.get('object'))
            if name not in stock:
                raise ValueError(f'{name}: not a direct Root.cpk/OBJECT member')
            data = game.read('OBJECT', stock[name])
            values = sgo.load(data=data)
            if not isinstance(values, dict) or values.get('xgs_scene_object_class') != native_class:
                raise ValueError(f'{name}: original SGO class does not exactly match {native_class}')
            count = emitter_bound(native_class, values, contract)
            path = 'app:/object/' + name[:-4] + '.sgo'
            if path in rows:
                raise ValueError(f'{name}: duplicate catalog resource binding')
            rows[path] = Row(path, native_class, contract.vtable, 1, count, digest(data))
    return [rows[path] for path in sorted(rows)]


def render(rows: list[Row], catalog_data: bytes, profiles: bytes) -> str:
    lines = [
        '// Generated by tools/gen_enemy_control_pvp_spawn_profiles.py. Do not edit.',
        '// Original Root.cpk/OBJECT SGOs only; Mods and ported registries are never read.',
        '// {configured SGO, exact native class vtable RVA, maxMembers, maxEmitters}',
        f'// EDF.dll timestamp: 0x{TIMESTAMP:08X}; SHA256: {DLL_SHA256}',
        f'// Canonical candidate selection SHA256: {selection_digest(catalog_data)}',
        f'// enemy_control_native_profiles.inc SHA256: {digest(profiles)}',
        '// Bounds are audited single-GameObject contracts; bullet objects are not lease members.',
    ]
    for name, contract in sorted(CONTRACTS.items()):
        lines.append(f'// {name}: maxMembers=1; {contract.evidence}.')
    lines.append(f'// {len(rows)} exact resource bindings. Per-row SHA256 hashes the original decompressed SGO bytes.')
    for row in rows:
        lines.append(f'    {{L"{row.path}",0x{row.vtable:X},{row.members},{row.emitters}}}, '
                     f'// {row.native_class}; SHA256 {row.digest}')
    return '\n'.join(lines) + '\n'


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path(rootcpk.DEFAULT_GAME), help='read-only native game directory')
    parser.add_argument('--catalog', type=Path, help='source audit catalog; default: local temporary catalog or embedded snapshot')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--check', action='store_true', help='compare output without writing')
    args = parser.parse_args(argv)
    try:
        native_dll(args.game)
        catalog_data = load_catalog(args.catalog)
        profiles = PROFILES.read_bytes()
        rows = gather(catalog_data, rootcpk.Game(str(args.game)), profiles)
        text = render(rows, catalog_data, profiles)
        if args.check:
            if not args.output.is_file() or args.output.read_bytes() != text.encode('utf-8'):
                print(f'STALE: {args.output}', file=sys.stderr)
                return 1
            print(f'OK: {len(rows)} native spawn profiles match original Root.cpk SGOs')
            return 0
        args.output.write_bytes(text.encode('utf-8'))
        print(f'wrote {args.output}: {len(rows)} native spawn profiles')
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error, RecursionError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
