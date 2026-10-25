"""Generate src/pvp_units.inc: the debug-spawn catalogue's enemy rows a PvP player can be, and the faction side of each.

  python -B tools/gen_pvp_units.py            write src/pvp_units.inc
  python -B tools/gen_pvp_units.py --check    fail (exit 1) when the committed file is stale; writes nothing

Inputs: src/spawn_catalog.inc (Category::enemy / Category::enemy41 rows, in catalogue order) and the class each row
becomes:
  * enemy rows are EDF5/EDF6 stock enemies: the original Root.cpk OBJECT SGO's xgs_scene_object_class (the game directory,
    read only, resolved with rootcpk/sgo as tools/gen_enemy_control_pvp_spawn_profiles.py does);
  * enemy41 rows are EDF4.1 ports: the shipped binary SGO in edf41port/objects/OBJECT (in the repository, no game needed).
A row is playable when its class is one enemy control can take over (docs/enemy-control-runtime.md): the 34 native
profiles of src/enemy_control_native_profiles.inc, or the plugin-owned EDF4.1 controllers (UfoRobo, UfoMother301,
UfoMother401; src/enemy_control_legacy*.cpp). Structures (generator, anchor, nest, anthill, fortress, egg, target ...) and
the structural roots (MovingFortress, UfoMother511, EDF6_GrandMother, EDF6_Jormungand, Transporter_Formation) are left out.

Without the game directory (CI) --check re-derives the file from the classes recorded in the committed file's own comments
(`// <class>` per playable row, `// excluded: <id> (<class>) ...` per left-out row), so a changed catalogue, side table or
controllable set is still caught; only a changed Root.cpk class cannot be, and that needs the game.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'pylib'))
import gen_enemy_control_pvp_spawn_profiles as native  # noqa: E402  (reuse: object_name, profile_vtables' pattern)
import sgo  # noqa: E402

CATALOG = ROOT / 'src/spawn_catalog.inc'
PROFILES = ROOT / 'src/enemy_control_native_profiles.inc'
PORTED_OBJECTS = ROOT / 'edf41port/objects/OBJECT'
DEFAULT_OUTPUT = ROOT / 'src/pvp_units.inc'

# Plugin-owned EDF4.1 controllers (the enemy-control legacy adapters), not in the native profile table.
LEGACY_CONTROLLABLE = frozenset(('UfoRobo', 'UfoMother301', 'UfoMother401'))

# The sides (the user: monsters are one faction, whatever the game; the Primers are EDF5/EDF6's aliens and their
# machines; the Foreigners EDF4.1's alien machines). By the class the row becomes.
MONSTER = frozenset((
    'GiantAnt', 'GiantAntEX', 'GiantBee', 'GiantBeeEX', 'GiantSpider', 'GiantSpiderEX', 'GiantDango',
    'EDF6_GiantDangoLarge', 'DragonSmall', 'Deiroi', 'Nephila', 'Monster501', 'Monster504',
    'EDF6_Merman', 'EDF6_FlyingDyno', 'EDF6_ShellFish', 'EDF6_SquidSmall'))
PRIMER = frozenset((
    'Humanoid_Basic', 'Humanoid_BigGreyBoss', 'EDF6_Martian', 'EDF6_Venus', 'EDF6_Berserker_Large',
    'EDF6_Berserker_Middle', 'EDF6_Berserker_TypeA', 'EDF6_Berserker_TypeBommer', 'EDF6_Berserker_TypeGrapple',
    'EDF6_Berserker_TypeShooter', 'AlienTrailer', 'EDF6_SpinnerUfo', 'UfoSmall507', 'UfoSmall515', 'UfoCarrier508',
    'EDF6_TimeShip', 'DropBoat512'))
# EDF4.1 rows by class: the machines are the Foreigners'; the creatures (ants, bees, spiders, dragons, Deiroi) the monsters'.
FOREIGNER = frozenset((
    'EDF6_Berserker_Large', 'AlienTrailer', 'UfoSmall507', 'UfoSmall515', 'UfoCarrier508', 'UfoRobo', 'UfoMother301',
    'UfoMother401'))

ROW = re.compile(r'\{Category::(enemy41|enemy),How::\w+,"([^"]*)",L"[^"]*",L"app:/object/([^"]*)\.sgo"')
KEPT = re.compile(r'^\{"([^"]+)",Side::(\w+)\},\s*// (\S+)')
LEFT = re.compile(r'^// excluded: (\S+) \((\S+)\)')


def catalogue() -> list[tuple[str, str, str]]:
    """(category, row id, sgo path under app:/object/) of every enemy / enemy41 row, in catalogue order."""
    rows = ROW.findall(CATALOG.read_text(encoding='utf-8'))
    if not rows:
        raise ValueError('no enemy rows in spawn_catalog.inc')
    return [(cat, rid, path) for cat, rid, path in rows]


def controllable_native() -> frozenset[str]:
    names = re.findall(r'\{"([A-Za-z0-9_]+)",0x[0-9A-Fa-f]+,', PROFILES.read_text(encoding='utf-8-sig'))
    if len(names) != 34 or len(set(names)) != 34:
        raise ValueError(f'enemy_control_native_profiles.inc: expected 34 distinct profiles, found {len(names)}')
    return frozenset(names)


def ported_class(path: str) -> str:
    """The configured class of an EDF4.1 ported object (the shipped SGO)."""
    name = native.object_name(path.split('/')[-1] + '.SGO')
    folder = PORTED_OBJECTS / '/'.join(path.split('/')[:-1])
    for candidate in folder.iterdir():
        if candidate.name.upper() == name:
            values = sgo.load(data=candidate.read_bytes())
            cls = values.get('xgs_scene_object_class') if isinstance(values, dict) else None
            if isinstance(cls, str) and cls:
                return cls
            raise ValueError(f'{path}: SGO has no class')
    raise ValueError(f'{path}: not in edf41port/objects/OBJECT')


def stock_classes(rows: list[tuple[str, str, str]], game_dir: Path) -> dict[str, str]:
    """Row id -> the class of its original Root.cpk OBJECT SGO ('-' for a DLC row Root.cpk lacks)."""
    import rootcpk
    game = rootcpk.Game(str(game_dir))
    stock = {actual.upper(): actual for actual in game.names('OBJECT')}
    out: dict[str, str] = {}
    for cat, rid, path in rows:
        if cat != 'enemy':
            continue
        name = native.object_name(path + '.SGO')
        if name not in stock:
            out[rid] = '-'
            continue
        values = sgo.load(data=game.read('OBJECT', stock[name]))
        cls = values.get('xgs_scene_object_class') if isinstance(values, dict) else None
        if not isinstance(cls, str) or not cls:
            raise ValueError(f'{rid}: SGO has no class')
        out[rid] = cls
    return out


def recorded_classes(text: str) -> dict[str, str]:
    """The stock rows' classes the committed file records (for a run without the game)."""
    out: dict[str, str] = {}
    for line in text.splitlines():
        m = KEPT.match(line.strip())
        if m:
            out[m.group(1)] = m.group(3)
            continue
        m = LEFT.match(line.strip())
        if m:
            out[m.group(1)] = m.group(2)
    return out


def side_of(cat: str, cls: str) -> str | None:
    if cat == 'enemy41':
        if cls in FOREIGNER:
            return 'foreigner'
        return 'monster' if cls in MONSTER else None
    if cls in MONSTER:
        return 'monster'
    return 'primer' if cls in PRIMER else None


def render(rows: list[tuple[str, str, str]], classes: dict[str, str], native_ok: frozenset[str]) -> str:
    lines = [
        '// Generated by tools/gen_pvp_units.py. Do not edit.',
        '// {debug spawn row id, faction side}: the enemy rows (src/spawn_catalog.inc) a PvP player can be, in catalogue order.',
        '// Playable = the row\'s class is one enemy control takes over (34 native profiles + UfoRobo / UfoMother301 / UfoMother401).',
        '// Each line records the class it was made from; `// excluded:` lines are the rows left out. See docs/plan-2026-10-25-pvp-factions.md.',
    ]
    for cat, rid, path in rows:
        cls = classes[rid]
        side = side_of(cat, cls) if (cls in native_ok or cls in LEGACY_CONTROLLABLE) else None
        if side is None:
            why = 'not in Root.cpk' if cls == '-' else 'not a controllable class'
            lines.append(f'// excluded: {rid} ({cls}) {why}')
        else:
            lines.append(f'{{"{rid}",Side::{side}}}, // {cls}')
    return '\n'.join(lines) + '\n'


def build(output: Path, game_dir: Path | None) -> tuple[str, str]:
    rows = catalogue()
    classes: dict[str, str] = {}
    source = 'game'
    if game_dir is not None and (game_dir / 'Root.cpk').exists():
        classes.update(stock_classes(rows, game_dir))
    else:
        if not output.is_file():
            raise ValueError('no game directory and no committed file to re-derive from')
        recorded = recorded_classes(output.read_text(encoding='utf-8'))
        source = 'committed classes (no game directory)'
        for cat, rid, _ in rows:
            if cat == 'enemy':
                if rid not in recorded:
                    raise ValueError(f'{rid}: new stock row and no game directory to resolve its class: run with the game')
                classes[rid] = recorded[rid]
    for cat, rid, path in rows:
        if cat == 'enemy41':
            classes[rid] = ported_class(path)
    return render(rows, classes, controllable_native()), source


def main(argv: list[str] | None = None) -> int:
    import rootcpk
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, default=Path(rootcpk.DEFAULT_GAME), help='read-only game directory')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--check', action='store_true', help='compare with the committed file, write nothing')
    args = parser.parse_args(argv)
    try:
        text, source = build(args.output, args.game)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 2
    kept = sum(1 for line in text.splitlines() if line.startswith('{'))
    if args.check:
        if not args.output.is_file() or args.output.read_bytes() != text.encode('utf-8'):
            print(f'STALE: {args.output}: run python tools/gen_pvp_units.py', file=sys.stderr)
            return 1
        print(f'OK: {kept} PvP units ({source})')
        return 0
    args.output.write_bytes(text.encode('utf-8'))
    print(f'wrote {args.output}: {kept} PvP units ({source})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
