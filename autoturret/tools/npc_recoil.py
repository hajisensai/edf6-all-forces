"""Give the stock NPC / mission ground vehicles the player's body recoil (docs/recoil-re.md).

Every gun mount in a vehicle setup is [weapon SGO, recoil, turret parameters, ...]; `recoil` is the
BodyRecoil (or, tagged, AimRecoil) spec [push, kick]. On a shot the vehicle adds FireRecoil x push to the
chassis' linear velocity along the gun's own axis (backwards) and FireRecoil x kick to its spin
(EDF+5F9FB0, from each class's slot 48 for every weapon that fired this frame; nothing there asks who
fires). The player's vehicles get their setup from the call (WEAPON/*: Ammo_CustomParameter). The
missions' NPC and boardable vehicles get theirs from OBJECT/*.SGO mission_setup, and the stock files
zero the push there (the NPC Titan's main cannon: [0, 2] where the call has [1, 3]), so an NPC tank
does not rock back when it fires.

The rule, one for every vehicle: each mission_setup mount takes the recoil of the same gun in the
player's call. PLAYER_CALL names that call per object: the first-tier call of the vehicle, whose guns
are the ones the mission files mount (or their AI copies). Which gun is "the same" is checked, never
assumed: the mount's weapon, that weapon without its `_ai` part, or the player gun the object's own
vehicle_setup puts in that slot must be the call's gun in that slot.

  python autoturret/tools/npc_recoil.py audit   scan Root.cpk: every stock mission_setup mount against
                                                every call that mounts the same gun; exit 1 when a file
                                                differs that PLAYER_CALL does not cover, or the table
                                                disagrees with the scan (slow: reads every call)
"""
from __future__ import annotations

import os
import re
import struct
import sys
from typing import Callable, Iterator

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'pylib'))
import dsgo  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
import vehicle_setup  # noqa: E402

# OBJECT file -> the player call whose mounts it takes. The files are the ones `audit` finds; the Titan's
# NPC file is titan_ai.py's output (its side cannons copied in) before this rule runs on it.
PLAYER_CALL: dict[str, str] = {
    'VEHICLE402_ROCKET_AI.SGO': 'EWEAPON401.SGO',     # Naegling missile launcher: [0, 0] -> [0.025, 0.1]
    'VEHICLE403_TANK_AI.SGO': 'AWEAPON347.SGO',       # Epsilon railgun: BodyRecoil [0, 0.05] -> [0.1, 0.05]
    'VEHICLE404_BIGTANK.SGO': 'EWEAPON420.SGO',       # Titan (mission, boardable): side cannons [0.5, 1.5] -> [0.05, 0.5]
    'VEHICLE404_BIGTANK_AI.SGO': 'EWEAPON420.SGO',    # Titan (NPC): main [0, 2] -> [1, 3], sides as above
    'V505_TANK_AI.SGO': 'EWEAPON418.SGO',             # Blacker: [0, 2] -> [0.1, 1]
    'V505_TANK_MISSION.SGO': 'EWEAPON418.SGO',
    'V601_TANK_AI.SGO': 'AWEAPON351.SGO',             # Varius: [0, 2] -> [0.25, 0.5]
}
AI_PART = re.compile(r'_ai(?=[_.])', re.IGNORECASE)


def _base(path: str) -> str:
    return path.replace('\\', '/').split('/')[-1].lower()


def _plain(v: object) -> object:
    """A spec value as plain Python, from either file format (floats as float32, as both store them)."""
    if isinstance(v, dsgo.Node):
        return [_plain(c) for c in v.items]
    if isinstance(v, list):
        return [_plain(c) for c in v]
    if isinstance(v, sgo.Float):
        return v.value
    if isinstance(v, float):
        return struct.unpack('<f', struct.pack('<f', v))[0]
    return v


def _as_sgo(v: object) -> sgo.Value:
    if isinstance(v, list):
        return [_as_sgo(c) for c in v]
    if isinstance(v, float):
        return sgo.Float(struct.pack('<f', v))
    return v


def _as_dsgo(v: object) -> dsgo.Value:
    return dsgo.Node([_as_dsgo(c) for c in v]) if isinstance(v, list) else v


def _items(v: object) -> list | None:
    if isinstance(v, dsgo.Node):
        return v.items
    return v if isinstance(v, list) else None


def _guns(setup: object, where: str) -> list:
    items = _items(setup)
    guns = _items(items[2]) if items and len(items) > 2 else None
    if guns is None:
        raise SystemExit(f'{where}: not the vehicle setup layout this tool knows')
    return guns


def _gun(mount: object) -> list | None:
    """The mount's [weapon, recoil, ...] items, or None for an empty mount ([0])."""
    items = _items(mount)
    return items if items and len(items) > 1 and isinstance(items[0], str) else None


def call_mounts(game: rootcpk.Game, call: str) -> list[tuple[str, object] | None]:
    """(weapon, recoil) of each slot of the call's vehicle setup, None for an empty slot."""
    setup = vehicle_setup.of_call(dsgo.parse(game.read('WEAPON', call)).root, call)
    return [(_base(g[0]), _plain(g[1])) if g else None for g in (_gun(m) for m in vehicle_setup.guns(setup))]


def _candidates(weapon: str, player_slot: str | None) -> list[str]:
    """The player gun a mission mount's weapon stands for: itself, its AI copy's original (the stock names
    the AI copy with an `_ai` part), the player gun the object's own vehicle_setup has in that slot."""
    out = [weapon, AI_PART.sub('', weapon)]
    return out + [player_slot] if player_slot else out


def align(mission: list, vehicle: list | None, mounts: list[tuple[str, object] | None], where: str,
          convert: Callable[[object], object]) -> int:
    """Sets each mission mount's recoil to the call's for the same gun; the number of mounts changed."""
    changed = 0
    for i, mount in enumerate(mission):
        gun = _gun(mount)
        if gun is None:
            continue
        if i >= len(mounts) or mounts[i] is None:
            raise SystemExit(f'{where}: mount {i} has no gun in the player call')
        player = _gun(vehicle[i]) if vehicle and i < len(vehicle) else None
        weapon, recoil = mounts[i]
        if weapon not in _candidates(_base(gun[0]), _base(player[0]) if player else None):
            raise SystemExit(f'{where}: mount {i} is {_base(gun[0])}, the call mounts {weapon} there')
        if _plain(gun[1]) != recoil:
            gun[1] = convert(recoil)
            changed += 1
    return changed


def build(name: str, data: bytes | None = None, game: rootcpk.Game | None = None) -> bytes:
    """OBJECT/`name` (the stock file, or `data`) with the player's recoil on every mount, in its own format."""
    game = game or rootcpk.default()
    data = data if data is not None else game.read('OBJECT', name)
    mounts = call_mounts(game, PLAYER_CALL[name])
    if data[:4] == b'DSGO':
        doc = dsgo.parse(data)
        vehicle = doc.root.get('vehicle_setup') if 'vehicle_setup' in doc.root.names.values() else None
        n = align(_guns(doc.root.get('mission_setup'), name), _guns(vehicle, name) if vehicle else None,
                  mounts, name, _as_dsgo)
        out = dsgo.write(doc)
    else:
        version, members = sgo.read(data)
        if sgo.read(sgo.write_depth_first(version, members)) != (version, members):
            raise SystemExit(f'{name} does not round-trip')
        vehicle = members.get('vehicle_setup')
        n = align(_guns(members['mission_setup'], name), _guns(vehicle, name) if vehicle else None,
                  mounts, name, _as_sgo)
        out = sgo.write_depth_first(version, members)
    if not n:
        raise SystemExit(f'{name}: already has the player recoil (the stock data changed? see audit)')
    return out


# ---------------------------------------------------------------- audit (reads every call: slow)


def _setups(v: object) -> Iterator[list]:
    """Every vehicle setup in a plain call tree (autoturret/tools/vehicle_setup.py's shape)."""
    if isinstance(v, list):
        if (len(v) >= 3 and isinstance(v[0], list) and v[0] and isinstance(v[0][0], float)
                and isinstance(v[2], list) and v[2] and isinstance(v[2][0], list) and v[2][0]
                and isinstance(v[2][0][0], str) and v[2][0][0].lower().endswith('.sgo')):
            yield v
        for c in v:
            yield from _setups(c)


def player_recoils(game: rootcpk.Game) -> dict[str, dict[str, list[str]]]:
    """gun -> recoil (repr) -> the calls mounting it with that recoil, over every call in Root.cpk."""
    key = 'Ammo_CustomParameter'.encode('utf-16le')
    out: dict[str, dict[str, list[str]]] = {}
    for folder, name in sorted(game.cpk.index):
        if folder.upper() != 'WEAPON' or not name.upper().endswith('.SGO'):
            continue
        data = game.read(folder, name)
        if key not in data:
            continue
        values = sgo.load(data=data)
        for setup in _setups(values.get('Ammo_CustomParameter') if isinstance(values, dict) else None):
            for g in setup[2]:
                if isinstance(g, list) and len(g) > 1 and isinstance(g[0], str):
                    out.setdefault(_base(g[0]), {}).setdefault(repr(_plain(g[1])), []).append(name.upper())
    return out


def audit(game: rootcpk.Game) -> list[str]:
    """What is wrong with PLAYER_CALL against the game's data (empty: nothing)."""
    recoils = player_recoils(game)
    problems: list[str] = []
    found: set[str] = set()
    for folder, name in sorted(game.cpk.index):
        if folder.upper() != 'OBJECT' or not name.upper().endswith('.SGO'):
            continue
        values = sgo.load(data=game.read(folder, name))
        if not isinstance(values, dict) or 'mission_setup' not in values:
            continue
        vehicle = values.get('vehicle_setup')
        try:
            guns = _guns(values['mission_setup'], name)
        except SystemExit:
            continue
        for i, gun in enumerate(guns):
            g = _gun(gun)
            if g is None:
                continue
            p = _gun(_guns(vehicle, name)[i]) if isinstance(vehicle, list) and i < len(_guns(vehicle, name)) else None
            hit = next((c for c in _candidates(_base(g[0]), _base(p[0]) if p else None) if c in recoils), None)
            if hit is None:
                continue   # no player call mounts this gun (V605, V607, V610, V611): nothing to match
            wanted = recoils[hit]
            if repr(_plain(g[1])) in wanted:
                continue
            found.add(name.upper())
            print(f'{name} mount {i}: {_base(g[0])} {_plain(g[1])}, player {hit} {sorted(wanted)}')
            if len(wanted) > 1:
                problems.append(f'{name} mount {i}: the calls mounting {hit} disagree ({sorted(wanted)})')
    for name in sorted(found - set(PLAYER_CALL)):
        problems.append(f'{name}: differs from the player call and PLAYER_CALL does not cover it')
    for name in sorted(set(PLAYER_CALL) - found):
        problems.append(f'{name}: in PLAYER_CALL but already as the player call')
    for name, call in PLAYER_CALL.items():
        for i, mount in enumerate(call_mounts(game, call)):
            if mount is None:
                continue
            weapon, recoil = mount
            if repr(recoil) not in recoils.get(weapon, {}) or len(recoils[weapon]) > 1:
                problems.append(f'{call} mount {i}: {weapon} {recoil} is not the one recoil every call gives it')
    return problems


def main(argv: list[str]) -> int:
    if argv != ['audit']:
        print(__doc__)
        return 2
    problems = audit(rootcpk.default())
    for p in problems:
        print('PROBLEM', p)
    print('audit:', 'ok' if not problems else f'{len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
