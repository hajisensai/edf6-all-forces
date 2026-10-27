"""The Dreadnought's hangar modules fitted per socket (pylib/dreadnought_model.py; the user, 2026-10-24: 「模块能装啥，
肯定是装了模块才能有对应的功能啊，比如两个机库模块，就应该两个机库」): python tests/dreadnought_fitting_test.py, exit 1 on
a failure. Offline, on a small stand-in of the prep tool's model (no model folder, no game):
  - a module not fitted is in neither the model's skeleton (its hangar, its bays' lifts, doors, fighters and mouths) nor
    its geometry (fitted_parts), so not in the collision measured off it (aircraft_collision.mesh_cells): where it was
    the cells are the hull's alone; a fitted one's are there;
  - the foredeck's old ready markers are never in it;
  - the installer reads the fitting from the plugin's ini as the plugin does (hangars_wanted), and keys its cache by it.
"""
from __future__ import annotations

import os
import sys
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import aircraft_collision  # noqa: E402
import dreadnought_model as dm  # noqa: E402

failures: list[str] = []


def check(ok: bool, what: str) -> None:
    if not ok:
        failures.append(what)


@dataclass
class Part:   # an obj_model part as fitted_parts reads it: its object's name (a bone), its triangles here
    name: str
    tris: list


def box(lo, hi) -> list:   # noqa: ANN001 - the 12 triangles of an axis-aligned box
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    c = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    faces = ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3))
    return [t for a, b, cc, d in faces for t in ((c[a], c[b], c[cc]), (c[a], c[cc], c[d]))]


# A stand-in ship: its hull 40 m wide (x -20 .. 20, y 0 .. 10) and 200 m long; on each broadside two modules standing
# out of it (x 20 .. 30, y 2 .. 14: higher than the hull's deck), module m at z -50 / -150 as the real ones go
# (0 left fore, 1 left aft at +x; 2 right fore, 3 right aft at -x), each with its two bays' parts.
MODULE_Z = (-50.0, -150.0, -50.0, -150.0)
MODULE_X = (25.0, 25.0, -25.0, -25.0)


def ship() -> list[Part]:
    parts = [Part('dreadnought', box((-20.0, 0.0, -200.0), (20.0, 10.0, 0.0)))]
    for m in range(dm.HANGARS):
        x, z = MODULE_X[m], MODULE_Z[m]
        parts.append(Part(f'hangar_{m}', box((x - 5.0, 2.0, z - 40.0), (x + 5.0, 14.0, z + 40.0))))
        for k, i in enumerate((2 * m, 2 * m + 1)):
            dz = z - 20.0 + 40.0 * k
            for name in (f'lift_{i}', f'door_u_{i}', f'door_l_{i}', f'bay_{i}'):
                parts.append(Part(name, box((x - 3.0, 3.0, dz - 5.0), (x + 3.0, 6.0, dz + 5.0))))
    return parts


def manifest() -> dict:
    bones = {'dreadnought': {'parent': '', 'pivot': [0.0, 0.0, 0.0]}}
    for m in range(dm.HANGARS):
        bones[f'hangar_{m}'] = {'parent': 'dreadnought', 'pivot': [MODULE_X[m], 14.0, MODULE_Z[m]]}
        for i in (2 * m, 2 * m + 1):
            for p in ('lift', 'door_u', 'door_l'):
                bones[f'{p}_{i}'] = {'parent': f'hangar_{m}', 'pivot': [MODULE_X[m], 3.0, MODULE_Z[m]]}
    for i in range(dm.BAYS):
        bones[f'bay_{i}'] = {'parent': 'dreadnought', 'pivot': [0.0, 0.0, 0.0]}
        bones[f'bay_{i}_out'] = {'parent': 'dreadnought', 'pivot': [0.0, 0.0, 0.0]}
    bones['bridge'] = {'parent': 'dreadnought', 'pivot': [0.0, 12.0, -100.0]}
    return {'models': {'dreadnought': {'bones': bones}, 'fighter': {'bones': {'fighter': {'parent': '', 'pivot': [0, 0, 0]}}}}}


def tops(parts: list[Part]) -> dict:
    """The collision's cells (aircraft_collision.mesh_cells, the grid laid on the whole ship's extent so cells line up
    between fittings): {(x, z) cell centre rounded: its top}."""
    tris = [t for p in parts for t in p.tris]
    # pin the grid to the whole ship's extent: two specks at its corners (off every module's place)
    lo, hi = (-30.0, 0.0, -200.0), (30.0, 14.0, 0.0)
    pins = [(lo, lo, lo), (hi, hi, hi)]
    cells = aircraft_collision.mesh_cells(tris + pins)
    return {(round((a[0] + b[0]) / 2.0, 1), round((a[2] + b[2]) / 2.0, 1)): b[1] for a, b in cells}


def module_of(x: float, z: float) -> int:
    for m in range(dm.HANGARS):
        if abs(x - MODULE_X[m]) < 5.0 and abs(z - MODULE_Z[m]) < 40.0 and abs(x) > 20.0:
            return m
    return -1


def main() -> int:
    every = ship()
    for fitting in ((True, True, True, True), (True, False, True, False), (False, True, False, True),
                    (True, False, False, False), (False, False, False, False)):
        label = dm.hangars_text(fitting)
        kept = {p.name for p in dm.fitted_parts(dm.SHIP, every, fitting)}
        names = {n for n, _ in dm.skeleton(dm.SHIP, manifest(), fitting)}
        for m in range(dm.HANGARS):
            mine = dm.hangar_parts(m)
            geometry = mine - {f'bay_{i}_out' for i in range(dm.BAYS)}   # the mouths are markers: no geometry
            ours = {dm._ours(dm.SHIP, b) for b in mine}
            if fitting[m]:
                check(geometry <= kept, f'{label}: module {m} fitted, its parts {sorted(geometry - kept)} left out')
                check(ours <= names, f'{label}: module {m} fitted, its bones {sorted(ours - names)} not in the skeleton')
            else:
                check(not geometry & kept, f'{label}: module {m} not fitted, its parts {sorted(geometry & kept)} built')
                check(not ours & names, f'{label}: module {m} not fitted, its bones {sorted(ours & names)} in the skeleton')
        check('dreadnought' in kept, f'{label}: the hull left out')
        check({'dn_bridge', 'dn_hull'} <= names, f'{label}: the hull or its bridge marker missing')
        bays = {i for i in range(dm.BAYS) if f'dn_bay_{i}' in names}
        check(bays == {i for i in range(dm.BAYS) if fitting[i // 2]}, f'{label}: bays {sorted(bays)}')
        # the collision measured off what is built: a module not fitted leaves only the hull's cells (none outboard of
        # the hull's side, x beyond 20 m) where it stood; a fitted one stands there (its top 14 m)
        cells = tops(dm.fitted_parts(dm.SHIP, every, fitting))
        for m in range(dm.HANGARS):
            here = [top for (x, z), top in cells.items() if module_of(x, z) == m]
            if fitting[m]:
                check(bool(here) and max(here) >= 13.9, f'{label}: module {m} fitted, no cell of it in the collision')
            else:
                check(not here or max(here) < 1.0, f'{label}: module {m} not fitted, cells of it in the collision: {here[:4]}')
    # a fighter is built whole whatever the fitting
    fighter = [Part('fighter', box((0, 0, 0), (1, 1, 1)))]
    check(dm.fitted_parts('fighter', fighter, (False,) * 4) == fighter, 'a fighter cut by the fitting')
    check({n for n, _ in dm.skeleton('fighter', manifest(), (False,) * 4)} >= {'dn_hull'}, 'a fighter\'s skeleton cut')
    # the installer's reading of the plugin's ini (what it builds by) and its cache key
    shipped = open(os.path.join(HERE, '..', 'EDF6VehicleCrew.ini'), encoding='utf-8-sig').read()
    check(dm.hangars_wanted(shipped) == dm.ALL_HANGARS, 'the shipped ini does not fit all four (the original\'s look)')
    two = shipped.replace('DreadnoughtHangarLeftAft=1', 'DreadnoughtHangarLeftAft=0').replace(
        'DreadnoughtHangarRightAft=1', 'DreadnoughtHangarRightAft=0')
    check(dm.hangars_wanted(two) == (True, False, True, False), 'left fore + right fore read wrong')
    check(dm.hangars_text((True, False, True, False)) == '1010', 'the cache key of a fitting')
    try:
        dm.left_out((True, True))
        check(False, 'a fitting of two sockets taken')
    except dm.DreadnoughtModelError:
        pass
    for f in failures:
        print('FAIL', f)
    print('dreadnought fitting:', 'FAILED' if failures else 'passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
