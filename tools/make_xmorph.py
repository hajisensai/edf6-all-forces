"""The X-Morph: Defense units' generator (pylib/xm_units.py): one module for every ported unit, where each War Thunder
vehicle has a tools/make_<kind>.py. A unit on the V603 (a tracked or wheeled hull, an emplacement) is a
tools/make_tracked_combat.py kind (its `like`'s gun, its model folder models/xm_<key>, wt_tracked.Model); a helicopter is
pylib/xm_heli.py's (the stock V506 in its own model, its weapons); a jet is pylib/xm_jet.py's (one of the plugin's
aircraft in its own model, its NPC body and its requested twin); a car (the 'car' family) is pylib/car_model.py's on the
V512, its seat's cockpit grafted and its LODs added as the V603 kinds'. Each unit is its own ledger owner and build group
(its key), so one unit's model changing rebuilds only that unit, and one unit's folder gone takes only that unit's files
away.

A ported unit whose model folder this machine has not (the folders are the model pack's, never the repository's) is not
deployed: build() says so in one line and returns no files, so the installer's install() releases whatever an earlier
install wrote for it (CLAUDE.md: no stand-in vehicle); its request row stays a placeholder (tools/call_weapons.py
undeployed) and the test range leaves it out (testrange/gen.py without_missing).

    python tools/make_xmorph.py --kind KEY [GAME] --out DIR     generate and check into DIR (never installs)
    python tools/make_xmorph.py --kind KEY [GAME] --check       generate and check in memory
    python tools/make_xmorph.py --kind KEY [GAME] [--remove]    install into (remove from) GAME's Mods
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent / 'pylib'), str(HERE)]

import ledger  # noqa: E402
import make_tracked_combat as tracked  # noqa: E402
import vcobjects as vc  # noqa: E402
import xm_heli  # noqa: E402
import xm_jet  # noqa: E402
import xm_units  # noqa: E402

KINDS = tuple(u.key for u in xm_units.v603()) + tuple(u.key for u in xm_units.ported('heli')) + \
    tuple(u.key for u in xm_units.ported('jet')) + tuple(u.key for u in xm_units.v512()) + \
    tuple(u.key for u in xm_units.ported('mech')) + tuple(u.key for u in xm_units.ported('infantry'))
HELIS = frozenset(u.key for u in xm_units.ported('heli'))
JETS = frozenset(u.key for u in xm_units.ported('jet'))
# The cars (pylib/car_model.py) and what the installer calls each; the V603 units are make_tracked_combat.LABELS'.
CARS = {u.key: f'{xm_units.label(u)}（X-Morph: Defense 模型）' for u in xm_units.v512()}
WALKERS = tuple(u.key for u in xm_units.ported('mech'))
INFANTRY = tuple(u.key for u in xm_units.ported('infantry'))
OWN = WALKERS + INFANTRY   # built here (pylib/xm_walker.py, xm_infantry.py), not by make_tracked_combat
# what the installer says it builds
LABELS = {**{u.key: tracked.LABELS[u.key] for u in xm_units.v603()}, **CARS,
          **{kind: f'{xm_units.label(xm_units.UNITS[kind])}（X-Morph: Defense 模型）' for kind in OWN}}


def deployable(kind: str) -> bool:
    """Its model folder is here (xm_units.deployed: models/xm_<key>, or EDF6VC_MODELS / the model pack's)."""
    return xm_units.deployed(kind)


def build(root: str, kind: str) -> dict[str, bytes]:
    if not deployable(kind):
        print(f'未找到 {xm_units.label(xm_units.UNITS[kind])} 的模型文件夹 {xm_units.folder(kind)}（模型包）：不部署这个单位。',
              flush=True)
        return {}
    if kind in HELIS:
        files = xm_heli.files(vc.Game(root), xm_units.UNITS[kind])
        check(files, kind)
        return files
    if kind in JETS:
        return xm_jet.files(vc.Game(root), xm_units.UNITS[kind])   # checked (xm_jet.check)
    if kind in CARS:
        import car_model
        import cockpits
        import model_lod
        game = vc.Game(root)
        files = model_lod.finish(cockpits.graft_files(game, car_model.build(game, kind)))
        car_model.check(files, kind)
        return files
    if kind in WALKERS:
        import xm_walker
        return xm_walker.build(vc.Game(root), kind)
    if kind in INFANTRY:
        import xm_infantry
        return xm_infantry.build(vc.Game(root), kind)
    return tracked.build(root, kind)


def install(root: str, files: dict[str, bytes], kind: str) -> list[str]:
    if kind in OWN:   # the unit's files, the ledger's (owner: its key); what an earlier install wrote and this not, released
        led = ledger.Ledger(root)
        before = set(led.owned_by(kind))
        paths = [led.put(kind, rel, data) for rel, data in files.items()]
        led.release(kind, sorted(before - {ledger.key(rel) for rel in files}))
        return paths
    return tracked.install(root, files, kind)


def remove(root: str, kind: str) -> tuple[list[str], list[str]]:
    if kind in OWN:
        led = ledger.Ledger(root)
        return led.release(kind, sorted(set(led.owned_by(kind))), writer=True)
    return tracked.remove(root, kind)


def vehicle_sgo(game, kind: str) -> bytes:  # noqa: ANN001 - vcobjects.Game
    if kind in HELIS:
        import obj_model as om
        import xm_air_model as xa
        unit = xm_units.UNITS[kind]
        return xm_heli.vehicle_sgo(game, unit, xa.read_info(om.model_dir(xm_units.folder(kind))))
    if kind in JETS:
        return vc.jet_sgo(game, xm_units.jet_request(kind))   # what its request brings
    if kind in CARS:
        import car_model
        return car_model.build(game, kind)[f'OBJECT/{car_model.out_names(kind)[0]}.SGO']
    if kind in WALKERS:
        import xm_walker
        return xm_walker.vehicle_sgo(game, kind)
    if kind in INFANTRY:
        raise ValueError(f'{kind} 是支援士兵，不是载具')
    return tracked.vehicle_sgo(game, kind)


def check(files: dict[str, bytes], kind: str, game=None) -> None:  # noqa: ANN001
    if kind in HELIS:
        import obj_model as om
        import xm_air_model as xa
        unit = xm_units.UNITS[kind]
        want = {f'OBJECT/{xm_heli.sgo_name(kind)}.SGO', f'OBJECT/{xa.archive_name(kind)}', f'OBJECT/{xa.animation_name(kind)}'}
        want |= {f'WEAPON/{p.split("/")[-1].upper()}' for p in xm_heli.weapon_paths(unit) if 'edf6vc_' in p}
        if set(files) != want:
            raise ValueError(f'{kind}: files {sorted(set(files) ^ want)}')
        xm_heli.check(files[f'OBJECT/{xm_heli.sgo_name(kind)}.SGO'], unit, xa.read_info(om.model_dir(xm_units.folder(kind))))
        return
    if kind in JETS:
        xm_jet.check(files, xm_units.UNITS[kind])
        return
    if kind in WALKERS:
        import xm_walker
        xm_walker.check(files, kind)
        return
    if kind in INFANTRY:
        import xm_infantry
        xm_infantry.check(files, kind)
        return
    if kind in CARS:
        import car_model
        car_model.check(files, kind)
    else:
        tracked.check(files, kind, game)


class Builder:
    """One unit's generator in the shape of a tools/make_<kind>.py module (OWNER, build(root), install, remove,
    vehicle_sgo(game), check): what pylib/buildcache.py tool() hands the installer and the test range."""

    def __init__(self, kind: str) -> None:
        self.OWNER = kind
        self.__name__ = f'make_xmorph[{kind}]'

    def build(self, root: str) -> dict[str, bytes]:
        return build(root, self.OWNER)

    def install(self, root: str, files: dict[str, bytes]) -> list[str]:
        return install(root, files, self.OWNER)

    def remove(self, root: str) -> tuple[list[str], list[str]]:
        return remove(root, self.OWNER)

    def vehicle_sgo(self, game) -> bytes:  # noqa: ANN001
        return vehicle_sgo(game, self.OWNER)

    def check(self, files: dict[str, bytes], game=None) -> None:  # noqa: ANN001
        check(files, self.OWNER, game)


BUILDERS: dict[str, Builder] = {kind: Builder(kind) for kind in KINDS}


def builder(kind: str) -> Builder:
    """`kind`'s generator, the same object every call (a test patches its build / install there)."""
    return BUILDERS[kind]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--kind', choices=KINDS, required=True)
    args, rest = parser.parse_known_args(argv)
    if not deployable(args.kind) and '--remove' not in rest:
        build('', args.kind)
        return 1
    if args.kind in HELIS or args.kind in JETS or args.kind in CARS or args.kind in OWN:
        return _own_main(rest, args.kind)
    return tracked.main(rest, args.kind)


def _own_main(argv: list[str], kind: str) -> int:
    """make_tracked_combat.main's options for a helicopter, a jet, a car, a walker or the infantry (its build is this module's)."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', nargs='?', default=vc.DEFAULT_GAME)
    parser.add_argument('--out', type=Path, help='write generated files under this output root; never installs')
    parser.add_argument('--check', action='store_true', help='generate and validate in memory only')
    parser.add_argument('--remove', action='store_true')
    args = parser.parse_args(argv)
    if args.remove:
        deleted, kept = remove(args.game, kind)
        for path in deleted:
            print('deleted', path)
        for path in kept:
            print('kept modified file', path)
        return 0
    files = build(args.game, kind)
    if args.out:
        for rel, data in files.items():
            path = args.out / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f'{kind}: generated and checked {len(files)} files in {args.out}')
    elif args.check:
        print(f'{kind}: generated and checked {len(files)} files in memory')
    else:
        for path in install(args.game, files, kind):
            print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
