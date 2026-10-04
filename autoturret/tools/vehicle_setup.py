"""The vehicle setup inside a vehicle call SGO (Weapon_Ranger_Vehicle*, under Ammo_CustomParameter) and an
OBJECT SGO (mission_setup): found by its shape, not by its position in the list.

  setup = [multipliers, _, guns, ...]
  multipliers = [durability multiplier, ...]
  guns = [[weapon SGO name, _, turret parameters, ...], ...]   (an unused mount may be a bare [0])

The call SGOs are plain lists, so the setup has no name to look it up by; it is the one node of that shape.
"""
from __future__ import annotations

from typing import Iterator

import dsgo
from dsgo import Node


def _gun(n: object) -> bool:
    return (isinstance(n, Node) and len(n.items) >= 3 and isinstance(n.items[0], str)
            and n.items[0].lower().endswith('.sgo') and isinstance(n.items[2], Node))


def is_setup(n: object) -> bool:
    if not isinstance(n, Node) or len(n.items) < 3:
        return False
    mul, guns = n.items[0], n.items[2]
    return (isinstance(mul, Node) and bool(mul.items) and isinstance(mul.items[0], float)
            and isinstance(guns, Node) and bool(guns.items) and _gun(guns.items[0]))


def _walk(n: dsgo.Value) -> Iterator[Node]:
    if isinstance(n, Node):
        yield n
        for c in n.items:
            yield from _walk(c)


def find(tree: dsgo.Value, where: str) -> Node:
    """The one vehicle setup in `tree`; SystemExit (naming `where`) when there is none or more than one."""
    found = [n for n in _walk(tree) if is_setup(n)]
    if len(found) != 1:
        raise SystemExit(f'{where}: {len(found)} vehicle setups (expected one): not the layout these tools know')
    return found[0]


def check(setup: dsgo.Value, where: str) -> Node:
    if not is_setup(setup):
        raise SystemExit(f'{where}: not a vehicle setup: not the layout these tools know')
    return setup


def of_call(root: Node, where: str) -> Node:
    return find(root.get('Ammo_CustomParameter'), where)


def durability(setup: Node) -> float:
    return setup.items[0].items[0]


def guns(setup: Node) -> list:
    return setup.items[2].items


def first_turret(setup: Node) -> Node:
    """The first gun's turret parameters ([rate, ...], the gun-L turret the calls and objects share)."""
    return guns(setup)[0].items[2]
