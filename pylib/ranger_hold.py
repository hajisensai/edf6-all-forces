"""The Ranger's shotguns held as its rifles are (user 2026-11-10, test site #32: "突击兵握持霰弹枪的姿势很怪 几乎是左
手臂90度弯曲握持 建议改成和突击步枪差不多的姿势"; maintainer: "32 改，应该改的都改"; docs/feedback-2026-11-10-shotgun-grip.md).

How a Ranger holds a weapon is five names in its weapon SGO (edf5port.HOLD_NAMES): BaseAnimation (the stance the arms
keep), AimAnimation, ChangeAnimation, ReloadAnimation and the recoil clip (custom_parameter[0] of a Weapon_BasicShoot
or Weapon_BasicSemiAuto); where the left hand grips is the weapon's MAB block (animation_model[2], its 'IK' locator),
not these. Every EDF6 Ranger shotgun (WEAPONTABLE category 1) is held 'waist' (hip level, the left forearm bent up to the
fore-end) but three: Destruction Blazer, Destruction Blazer G and Burst Blazer are held as the rifles (category 0, all
'assault'), on the set ('assault', 'aim', 'assault_change1', 'assault_reload2', 'waist_recoil1') -- the developers'
own way of holding a shotgun as a rifle. That set (derived from the table at build time, rifle_like_set) is what every
shotgun takes here; nothing else of the weapon changes (its numbers, its hand, its MAB block, the rest of
custom_parameter).
"""
from __future__ import annotations

import collections
from typing import Callable, Iterable

import dsgo
from dsgo import Node

RIFLE = 0          # WEAPONTABLE category (row[2]) of the Ranger's assault rifles
SHOTGUN = 1        # ... of its shotguns
# The classes a Ranger gun is (the SGN shotguns are semi-automatic); custom_parameter[0] is the recoil clip in both.
CLASSES = ('Weapon_BasicShoot', 'Weapon_BasicSemiAuto')
# The fields of a hold set, in order; the last is custom_parameter[0] (the recoil clip).
FIELDS = ('BaseAnimation', 'AimAnimation', 'ChangeAnimation', 'ReloadAnimation', 'custom_parameter')
HoldSet = tuple[str, str, str, str, str]


class NoDonor(Exception):
    """The table has no shotgun held as its rifles: nothing to copy (an EDF6 that is not the one this was made on)."""


def hold_set(root: Node) -> HoldSet | None:
    """The weapon's five hold names, None when it has not all of them in their plain form (a Weapon_Sub's named
    custom_parameter, a weapon without a recoil clip)."""
    out = []
    for f in FIELDS:
        if f not in root.names.values():
            return None
        v = root.get(f)
        if f == 'custom_parameter':
            if not isinstance(v, Node) or v.names or not v.items or not isinstance(v.items[0], str):
                return None
            v = v.items[0]
        if not isinstance(v, str):
            return None
        out.append(v)
    return tuple(out)  # type: ignore[return-value]


def rows_of(table: dsgo.Document) -> list[Node]:
    """WEAPONTABLE's rows (its 'table': [id, 'app:/weapon/<file>.sgo', category, ...] each)."""
    return list(table.root.get('table').items)


def sgo_name(row: Node) -> str:
    """The row's SGO file name in WEAPON (upper case)."""
    return str(row.items[1]).rsplit('/', 1)[-1].upper()


def category_sets(rows: Iterable[Node], read: Callable[[str], bytes], category: int) -> list[tuple[str, HoldSet]]:
    """[(SGO file, its hold set)] of the table's rows of `category` that are a gun (CLASSES) with a full hold set;
    `read(name)` the file's bytes (KeyError: not there, skipped)."""
    out = []
    for row in rows:
        if not isinstance(row, Node) or len(row.items) < 3 or row.items[2] != float(category):
            continue
        name = sgo_name(row)
        try:
            root = dsgo.parse(read(name)).root
        except (KeyError, ValueError):
            continue
        if 'xgs_scene_object_class' not in root.names.values() or root.get('xgs_scene_object_class') not in CLASSES:
            continue
        held = hold_set(root)
        if held is not None:
            out.append((name, held))
    return out


def rifle_like_set(rows: list[Node], read: Callable[[str], bytes]) -> HoldSet:
    """The hold set EDF6's own shotguns held as its rifles use: the stance most rifles have (BaseAnimation), then the
    set most shotguns in that stance have. NoDonor when no shotgun is held so."""
    rifles = collections.Counter(s[0] for _n, s in category_sets(rows, read, RIFLE))
    if not rifles:
        raise NoDonor('the weapon table has no Ranger rifle')
    stance = rifles.most_common(1)[0][0]
    alike = collections.Counter(s for _n, s in category_sets(rows, read, SHOTGUN) if s[0] == stance)
    if not alike:
        raise NoDonor(f'no Ranger shotgun of the weapon table is held {stance!r} as the rifles are')
    return alike.most_common(1)[0][0]


def hold_like(root: Node, donor: HoldSet) -> bool:
    """Gives the gun `root` the hold set `donor` (only the names: of custom_parameter element 0 alone).
    False, and nothing changed, when it already has it or has no full plain hold set."""
    held = hold_set(root)
    if held is None or held == donor:
        return False
    for f, name in zip(FIELDS, donor):
        if f == 'custom_parameter':
            root.get(f).items[0] = name
        else:
            root.set(f, name)
    return True
