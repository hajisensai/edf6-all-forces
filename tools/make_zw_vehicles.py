"""Writes the Vehicler's vehicles (pylib/zw_vehicles.py, docs/newclass-re/vehicler-plan.md §1.2) into <game>/Mods:
  Mods/OBJECT/EDF6VC_ZW_<V>.SGO     each vehicle: its stock hull's SGO with its durability, speed, drive force and guns,
                                    and mission_setup the same as vehicle_setup (the plugin spawns it by
                                    ApplyMissionSetup, which reads mission_setup: a vehicle_setup-only hull comes unarmed)
  Mods/WEAPON/EDF6VC_ZW_<V>_GUN*.SGO  its guns: the stock hull's own weapons with the vehicle's numbers and names
Models are the stock hulls' (zw_vehicles state 'placeholder'). Built from the player's own Root.cpk (only read), written
atomically and recorded in the ledger as this tool's (pylib/ledger.py 'zwvehicle'); --remove releases them.
  python tools/make_zw_vehicles.py [game dir]            write / refresh
  python tools/make_zw_vehicles.py [game dir] --remove   release them
"""
from __future__ import annotations

import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
import zw_vehicles as zv  # noqa: E402

OWNER = 'zwvehicle'   # pylib/ledger.py


def _need(ok: bool, what: str) -> None:
    if not ok:
        raise ValueError(what)


def gun_sgo(g: zv.Gun, stock: bytes) -> bytes:
    """A gun from its stock vehicle weapon (a DSGO): its values and names over the stock's."""
    _need(stock[:4] == b'DSGO', f'{g.stock}: not a DSGO')
    doc = dsgo.parse(stock)
    _need(g.cls in zv.GUN_CLASSES and doc.root.get('xgs_scene_object_class') == g.cls,
          f'{g.stock}: not a {g.cls} (a native vehicle gun)')
    for key, value in g.values.items():
        doc.root.get(key)   # KeyError: not the stock gun this was written for
        doc.root.set(key, float(value))
    for lang, key in zv.NAME_KEYS.items():
        doc.root.get(key)
        doc.root.set(key, g.names[lang])
    return dsgo.compact(doc)


def _kept(stock_gun: list) -> bool:
    return any(str(stock_gun[0]).lower() == f'app:/weapon/{w}.sgo' for w in zv.KEPT_WEAPONS)


def _setup_plain(setup: list, v: zv.ZwVehicle) -> list:
    """The vehicle's setup from its stock hull's vehicle_setup (plain values): multipliers, speed and force (a ground
    hull: [multipliers, params, guns]; a flying one, [multipliers, params, flight, guns], keeps its params and flight),
    guns (the stock KEPT_WEAPONS past them kept)."""
    parts = 4 if v.flying else 3
    _need(isinstance(setup, list) and len(setup) == parts,
          f'{v.stock}: vehicle_setup is not {parts} parts ([multipliers, params, {"flight, " if v.flying else ""}guns])')
    multipliers, params, *flight, guns = setup
    _need(len(multipliers) == 2 and len(params) >= 3, f'{v.stock}: vehicle_setup multipliers / params')
    kept = [x for x in guns if _kept(x)]
    guns = [x for x in guns if not _kept(x)]
    _need(len(guns) == len(v.guns), f'{v.stock}: {len(guns)} weapons in its setup, {len(v.guns)} guns registered')
    for stock_gun, g in zip(guns, v.guns):
        _need(str(stock_gun[0]).lower() == f'app:/weapon/{g.stock.lower()}.sgo',
              f'{v.stock}: setup weapon {stock_gun[0]}, want {g.stock} (changed by another mod?)')
    params = list(params) if v.flying else [params[0], v.speed, v.force, *params[3:]]
    return [list(zv.MULTIPLIERS), params, *flight,
            [*([g.path, *stock_gun[1:]] for stock_gun, g in zip(guns, v.guns)), *kept]]


def _as_sgo(value):  # noqa: ANN001, ANN202 - a plain value back into the old SGO's (floats stay floats)
    if isinstance(value, list):
        return [_as_sgo(x) for x in value]
    if isinstance(value, sgo.Float):
        return value.value
    return value


def _node(value):  # noqa: ANN001, ANN202 - a plain value as a DSGO node
    if isinstance(value, list):
        return dsgo.Node([_node(x) for x in value])
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else value


def vehicle_sgo(v: zv.ZwVehicle, stock: bytes) -> bytes:
    """A vehicle from its stock hull's SGO (the old SGO or a DSGO)."""
    if stock[:4] == b'DSGO':
        doc = dsgo.parse(stock)
        root = doc.root
        _need(root.get('xgs_scene_object_class') == v.cls, f'{v.stock}: class {root.get("xgs_scene_object_class")}')
        _need(len(root.get('vehicle_riding_position').items) == v.seats, f'{v.stock}: seats')
        root.get('game_object_durability')
        root.set('game_object_durability', v.durability)
        setup = _node(_setup_plain(dsgo.to_py(root.get('vehicle_setup')), v))
        root.set('vehicle_setup', setup)
        root.set('mission_setup', copy.deepcopy(setup))
        return dsgo.compact(doc)
    version, members = sgo.read(stock)
    _need(members.get('xgs_scene_object_class') == v.cls, f'{v.stock}: class {members.get("xgs_scene_object_class")}')
    _need(len(members.get('vehicle_riding_position') or []) == v.seats, f'{v.stock}: seats')
    _need('game_object_durability' in members, f'{v.stock}: no durability')
    members['game_object_durability'] = v.durability
    setup = _setup_plain(_as_sgo(members['vehicle_setup']), v)
    members['vehicle_setup'] = setup
    members['mission_setup'] = copy.deepcopy(setup)
    return sgo.write_depth_first(version, members)


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    out: dict[str, bytes] = {}
    for v in zv.VEHICLES:
        out[v.rel] = vehicle_sgo(v, game.read('OBJECT', f'{v.stock.upper()}.SGO'))
        for g in v.guns:
            if g.rel not in out:
                out[g.rel] = gun_sgo(g, game.read('WEAPON', f'{g.stock.upper()}.SGO'))
    if list(out) != zv.files():
        raise AssertionError('build and zw_vehicles.files disagree')
    return out


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's, each one only when it is not already exactly that (every install rebuilds
    them: an unchanged file is not rewritten, as make_zw_classes.install); what it wrote before and does not now is
    released. Returns the paths written."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = []
    for rel, data in files.items():
        if sha256_file(led.disk(rel)) == sha256(data) and (ledger.key(rel) in before or led.owners(rel)):
            if ledger.key(rel) not in before:
                led.need(OWNER, rel)
            continue
        paths.append(led.put(OWNER, rel, data))
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    for path in install(root, build(root)):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
