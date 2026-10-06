"""Give the stock NPC / mission ground vehicles the player's body recoil (docs/recoil-re.md).

Every gun mount in a vehicle setup is [weapon SGO, recoil, turret parameters, ...]; `recoil` is the
BodyRecoil (or, tagged, AimRecoil) spec [push, kick]. On a shot the vehicle adds FireRecoil x push to the
chassis' linear velocity along the gun's own axis (backwards) and FireRecoil x kick to its spin
(EDF+5F9FB0, from each class's slot 48 for every weapon that fired this frame; nothing there asks who
fires). The player's vehicles get their setup from the call (WEAPON/*: Ammo_CustomParameter). The
missions' NPC and boardable vehicles get theirs from OBJECT/*.SGO mission_setup, and the stock files
zero the push there (the NPC Titan's main cannon: [0, 2] where the call has [1, 3]), so an NPC tank
does not rock back when it fires.

The rule, one for every vehicle (pylib/recoil.py): each mission_setup mount takes the recoil of the same
gun in the player's call. PLAYER_CALL names that call per object: the first-tier call of the vehicle,
whose guns are the ones the mission files mount (or their AI copies). The test range's own placeable
vehicles follow the same rule in testrange/gen.py (vehicle_sgo, player_mounts).

  python autoturret/tools/npc_recoil.py audit   scan Root.cpk: every stock mission_setup mount against
                                                every call that mounts the same gun, and every vehicle
                                                the test range makes against its player call; exit 1
                                                when a file differs that the tables do not cover, or a
                                                table disagrees with the scan (slow: reads every call)
"""
from __future__ import annotations

import ast
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import recoil  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

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
    # Freed bikes: the mission ones' machine guns BodyRecoil [0.02, 0.02], every call's [0, 0]. The DLC calls mount
    # lasers, so the bikes that keep the machine guns take the first call that mounts them.
    'V503_BIKE.SGO': 'AWEAPON338.SGO',
    'V503_BIKE_EDF6BENEFITS.SGO': 'AWEAPON338.SGO',
    'V503_BIKE_OMEGAZ.SGO': 'AWEAPON338.SGO',
    'V613_BIKE.SGO': 'AWEAPON339.SGO',
}


def call_mounts(game: rootcpk.Game, call: str) -> list[recoil.Mount | None]:
    """recoil.mounts_of the stock call WEAPON/`call`."""
    return recoil.mounts_of(game.read('WEAPON', call), call)


def build(name: str, data: bytes | None = None, game: rootcpk.Game | None = None) -> bytes:
    """OBJECT/`name` (the stock file, or `data`) with the player's recoil on every mount, in its own format."""
    game = game or rootcpk.default()
    out, n = recoil.align_data(data if data is not None else game.read('OBJECT', name),
                               call_mounts(game, PLAYER_CALL[name]), name)
    if not n:
        raise SystemExit(f'{name}: already has the player recoil (the stock data changed? see audit)')
    return out


# ---------------------------------------------------------------- audit (reads every call: slow)


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
        try:
            mounts = recoil.mounts_of(data, name)
        except SystemExit:
            continue   # not a vehicle call (no setup, or more than one)
        for m in mounts:
            if m:
                out.setdefault(m[0], {}).setdefault(repr(m[1]), []).append(name.upper())
    return out


def _mission_guns(data: bytes, where: str) -> list[tuple[int, list, list | None]]:
    """(slot, mission mount, the vehicle_setup mount in that slot or None) of every gun in a mission file."""
    values = sgo.load(data=data)
    if not isinstance(values, dict) or 'mission_setup' not in values:
        return []
    try:
        guns = recoil.guns_of(values['mission_setup'], where)
    except SystemExit:
        return []
    vehicle = values.get('vehicle_setup')
    try:
        player = recoil.guns_of(vehicle, where) if isinstance(vehicle, list) else []
    except SystemExit:
        player = []
    return [(i, g, recoil.gun_of(player[i]) if i < len(player) else None)
            for i, g in ((i, recoil.gun_of(m)) for i, m in enumerate(guns)) if g]


def _stock_audit(game: rootcpk.Game, recoils: dict[str, dict[str, list[str]]]) -> list[str]:
    problems: list[str] = []
    found: set[str] = set()
    for folder, name in sorted(game.cpk.index):
        if folder.upper() != 'OBJECT' or not name.upper().endswith('.SGO'):
            continue
        for i, g, p in _mission_guns(game.read(folder, name), name):
            hit = next((c for c in recoil.candidates(recoil.base(g[0]), recoil.base(p[0]) if p else None)
                        if c in recoils), None)
            if hit is None:
                continue   # no player call mounts this gun (V605, V607, V610, V611): nothing to match
            wanted = recoils[hit]
            if repr(recoil.plain(g[1])) in wanted:
                continue
            found.add(name.upper())
            print(f'{name} mount {i}: {recoil.base(g[0])} {recoil.plain(g[1])}, player {hit} {sorted(wanted)}')
            if len(wanted) > 1:
                problems.append(f'{name} mount {i}: the calls mounting {hit} disagree ({sorted(wanted)})')
    for name in sorted(found - set(PLAYER_CALL)):
        problems.append(f'{name}: differs from the player call and PLAYER_CALL does not cover it')
    for name in sorted(set(PLAYER_CALL) - found):
        problems.append(f'{name}: in PLAYER_CALL but already as the player call')
    for name, call in PLAYER_CALL.items():
        for i, mount in enumerate(call_mounts(game, call)):
            if mount and (repr(mount[1]) not in recoils.get(mount[0], {}) or len(recoils[mount[0]]) > 1):
                problems.append(f'{call} mount {i}: {mount[0]} {mount[1]} is not the one recoil every call gives it')
    return problems


def _range_audit(game: rootcpk.Game, recoils: dict[str, dict[str, list[str]]]) -> list[str]:
    """The test range's ground vehicles (testrange/gen.py DERIVED, GROUND_MISSION; the jets are the plugin's, with
    no player call), as gen.vehicle_sgo writes them: every gun mount the player call's."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    problems: list[str] = []
    for name in sorted((set(gen.DERIVED) | set(gen.GROUND_MISSION)) - set(gen.JETS)):
        mounts = gen.player_mounts(game, name)
        for i, g, p in _mission_guns(gen.vehicle_sgo(game, name), name):
            have = recoil.plain(g[1])
            if mounts is not None:
                want = [mounts[i][1]] if i < len(mounts) and mounts[i] else []
                source = 'its player call'
            else:
                hit = next((c for c in recoil.candidates(recoil.base(g[0]), recoil.base(p[0]) if p else None)
                            if c in recoils), None)
                if hit is None:
                    continue
                want, source = [ast.literal_eval(r) for r in recoils[hit]], f'the calls mounting {hit} (gen.PLAYER_CALLS has none)'
            print(f'{name} mount {i}: {recoil.base(g[0])} {have}, {source} {want}')
            if have not in want:
                problems.append(f'{name} mount {i}: {have}, {source} {want}')
    return problems


def audit(game: rootcpk.Game) -> list[str]:
    """What is wrong with PLAYER_CALL and gen.PLAYER_CALLS against the game's data (empty: nothing)."""
    recoils = player_recoils(game)
    return _stock_audit(game, recoils) + _range_audit(game, recoils)


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
