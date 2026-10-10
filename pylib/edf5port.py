"""Earlier games' weapon data in EDF6's form: an EDF5 (weapon) or EDF4.1 (weapon41) weapon SGO (SGO v0x102, typed
values) -> an EDF6 DSGO weapon, and an EDF5 WEAPONTEXT row -> an EDF6 one. Read only; tools/make_edf5_weapons.py and
tools/make_edf41_weapons.py pick the weapons, tools/ported_weapons.py installs them.

What changes between the two games' copies of the same weapon (measured on every weapon both ship under the same SGO
name and the same parameters, docs/edf5-weapons-plan.md):
  - every star curve, a 6-element list [base, ...], gains a 7th element: 1.0 when EDF5 stored the base as a float,
    0.0 when it stored an int (765 of 765 SGO curves, 847 of 847 text curves). DSGO keeps every number as a double,
    so EDF6 records there whether the value was whole. Only the fields in CURVES are curves: other 6-element lists
    (ShellCase's parameters, a few Ammo_CustomParameter entries) stay 6 long in EDF6.
  - the MAB block in animation_model[2] is laid out with offsets relative to their records (header 0x08 = 0x83, every
    one of EDF6's 2487 blocks) instead of EDF5's absolute ones (0x03): pylib/mab_legacy.py.
  - an empty SecondaryFire_Parameter becomes [0.0] (none of EDF6's 1833 weapon files has an empty one, 929 of EDF5's
    do).
  - EDF6 adds name.sc (simplified Chinese) next to EDF5's name.ja / en / cn / kr.
Weapon_Sub (the class EDF6 uses for everything in a support or vehicle slot; EDF5 used Weapon_BasicShoot there): the
class name and custom_parameter from a list to EDF6's named form, which comes from an EDF6 weapon of the same category
(to_sub).
Checked against the developers' own conversions (the EDF5 weapons EDF6 ships but never lists, tools/ported_weapons.py
'edf6'): converting EDF5's copy gives EDF6's field for field (the Light Truck's Weapon_Sub all 70), but for the fields
they rebalanced.
"""
from __future__ import annotations

import copy
import struct

import dsgo
import mab_legacy
import sgo
from dsgo import Blob, Node

# Star curves, by top-level key; the int is the index of the curve inside the field when the field holds it as its
# first element (EnergyChargeRequire = [curve, ...]).
CURVES: dict[str, int | None] = {
    'AmmoCount': None, 'AmmoDamage': None, 'AmmoExplosion': None, 'AmmoSpeed': None, 'FireAccuracy': None,
    'FireBurstInterval': None, 'FireCount': None, 'FireInterval': None, 'LockonRange': None, 'LockonTime': None,
    'ReloadTime': None, 'EnergyChargeRequire': 0, 'ExtPrams': 0,
}
SUB = 'Weapon_Sub'


class Unsupported(ValueError):
    """Something in the EDF5 file this conversion does not know how to carry over: the weapon is not built (its row
    waits as a placeholder, tools/ported_weapons.py)."""


def _number(v: object) -> bool:
    return isinstance(v, (int, sgo.Float)) and not isinstance(v, bool) or isinstance(v, float)


def is_curve(v: object) -> bool:
    return isinstance(v, list) and len(v) == 6 and all(_number(x) for x in v)


def flag(base: object) -> float:
    """The 7th element EDF6 gives a curve: whether EDF5 stored its base as a float."""
    return 1.0 if isinstance(base, (sgo.Float, float)) else 0.0


def _plain(v: object) -> object:
    """A typed scalar as plain JSON; a float its 4 stored bytes widened exactly (as to_value does, and as EDF6 stores
    them: 0.05 is 0.05000000074505806 there), not sgo.plain's rounding."""
    if isinstance(v, sgo.Float):
        return float(v.value)
    return sgo.plain(v) if not isinstance(v, (list, dict)) else v


def to_value(v: object) -> dsgo.Value:
    """A typed SGO value as a DSGO one: numbers become doubles (the float's 4 bytes widened exactly), lists Nodes, raw
    blocks Blobs (a MAB converted; any other block is Unsupported)."""
    if isinstance(v, bool):
        raise Unsupported('bool in an SGO')
    if isinstance(v, int):
        return float(v)
    if isinstance(v, sgo.Float):
        return float(v.value)
    if isinstance(v, float):
        return v
    if isinstance(v, str):
        return v
    if isinstance(v, bytes):
        if v[:4] == b'MAB\0':
            try:
                return Blob(mab_legacy.mab_from_edf5(v), 2)
            except (ValueError, struct.error) as e:   # a layout mab_legacy refuses: this weapon stays out
                raise Unsupported(f'MAB block: {e}') from e
        raise Unsupported(f'raw block {v[:4]!r} (only MAB blocks are converted)')
    if isinstance(v, list):
        return Node([to_value(x) for x in v])
    raise Unsupported(f'{type(v).__name__} in an SGO')


def curve(v: list) -> Node:
    """A 6-element EDF5 curve as EDF6's 7-element one."""
    out = to_value(v)
    assert isinstance(out, Node)
    out.items.append(flag(v[0]))
    return out


def weapon(members: dict[str, object], names: dict[str, str]) -> dsgo.Document:
    """An EDF5 weapon SGO's members (sgo.read(...)[1]) as an EDF6 DSGO document; `names` maps 'ja' / 'en' / 'cn' / 'kr'
    / 'sc' to the weapon's name (name.<lang> is rewritten from it, name.sc added)."""
    root = Node([])
    for key, value in members.items():
        if key in CURVES:
            root.set(key, _curve_field(value, CURVES[key]))
        elif key == 'SecondaryFire_Parameter' and value == []:
            root.set(key, Node([0.0]))
        else:
            root.set(key, to_value(value))
    for lang, name in names.items():
        root.set(f'name.{lang}', name)
    return dsgo.Document(root, [])


# What every EDF6 weapon has and an EDF4.1 weapon lacks: the value most EDF6 weapons have (1350 of 1564
# AmmoDamageReduce [1, 1]: no fall-off with range, which 4.1 has none of; 1486 of 1564 ExtPrams [1]).
EDF41_ADDED: dict[str, list[float]] = {'AmmoDamageReduce': [1.0, 1.0], 'ExtPrams': [1.0]}
# EDF4.1's fields EDF6 has no use for: 'name' (one list [ja, en, ja]; EDF6 has name.<lang>) and 'Range' (AmmoSpeed x
# AmmoAlive in every 4.1 weapon, which is how EDF5 and EDF6 get a weapon's range).
EDF41_DROPPED = ('name', 'Range')


def weapon41(members: dict[str, object], names: dict[str, str]) -> dsgo.Document:
    """An EDF4.1 weapon SGO's members as an EDF6 DSGO document: weapon()'s conversion (4.1 has no star curves, its
    numbers stay plain), without EDF41_DROPPED and with EDF41_ADDED. EnergyChargeRequire, one number in 4.1, is
    [curve, value] in every EDF6 weapon: [v, v] (the developers' own conversions of 4.1 weapons EDF6 ships turn -1
    into [-1, -1]; a Wing Diver weapon's 4.1 number is EDF5's curve base and value, Rapier 25 / [[25, ...], 25]; a
    plain number where a curve goes is a curve that does not grow, as EDF6's own plain AmmoCount etc.)."""
    members = dict(members)
    energy = members.get('EnergyChargeRequire')
    if isinstance(energy, (int, float, sgo.Float)) and not isinstance(energy, bool):
        members['EnergyChargeRequire'] = [energy, energy]
    doc = weapon({k: v for k, v in members.items() if k not in EDF41_DROPPED}, names)
    for key, value in EDF41_ADDED.items():
        if key not in doc.root.names.values():
            doc.root.set(key, Node(list(value)))
    return doc


def fit_locators(doc: dsgo.Document, bones: list[tuple[str, int]]) -> None:
    """Hang every locator of the weapon's MAB block (animation_model[2]: the muzzles of list 0, the grips and the rest
    of the other lists) on a bone its EDF6 model has; `bones` is that model's (name, parent) list.
    EDF.dll skips a muzzle whose node the model lacks and then takes every shot's muzzle as (shots % muzzles found):
    none found, the first shot divides by zero (0x69AA20, Weapon_BasicShoot's family; the crash 2026-10-10 on firing a
    4.1 Wing Diver weapon). EDF6 re-exported many models EDF4.1 / EDF5 share with it, the root bone 'mdl' renamed
    after the model (p_lazer_LAZR01: 4.1 ['mdl', 'p_lazer_LAZR01'], EDF6 ['p_lazer_LAZR01', 'polymesh']); the
    developers' own ports of those weapons hang every locator that was on it on the new root and change nothing else
    in the block (tools/selftest.py ported_weapon_locators_fit). So does this: a missing node becomes the model's one
    root. Every locator of every EDF6 weapon names a bone of its model, and each model has one root (2026-10-10, every
    weapon file of Root.cpk). Unsupported when the block has no muzzle, or a node is missing and the model has no single
    root to move it to."""
    if 'animation_model' not in doc.root.names.values():
        return
    am = doc.root.get('animation_model')
    block = am.items[2] if isinstance(am, Node) and len(am.items) > 2 else None
    if not isinstance(block, Blob) or block.data[:4] != b'MAB\0':
        raise Unsupported('animation_model has no MAB block')
    b = block.data
    nlists = struct.unpack_from('<H', b, 0x0C)[0]
    head, records_end = struct.unpack_from('<II', b, 0x14)
    if not nlists or not struct.unpack_from('<H', b, head + 2)[0]:
        raise Unsupported('the MAB block has no muzzle')
    names = {name for name, _parent in bones}
    nodes = {r + 4: mab_legacy._text_at(b, r + struct.unpack_from('<i', b, r + 4)[0])
             for r in range(head + 8 * nlists, records_end, mab_legacy.RECORD)}
    missing = [at for at, node in nodes.items() if node not in names]
    if not missing:
        return
    roots = [name for name, parent in bones if parent == -1]
    if len(roots) != 1:
        raise Unsupported(f'locator node(s) {sorted({nodes[at] for at in missing})} not in the model, '
                          f'and it has {len(roots)} root bones to hang them on')
    try:
        am.items[2] = Blob(mab_legacy.mab_set_strings(b, {at: roots[0] for at in missing}), block.kind)
    except (ValueError, struct.error) as e:   # a layout mab_legacy refuses: this weapon stays out
        raise Unsupported(f'MAB block: {e}') from e


def _curve_field(value: object, at: int | None) -> dsgo.Value:
    """A CURVES field: its curve 7 long (the field itself, or its element `at`); a scalar or another shape as it is
    (EDF6 holds many of these fields as plain numbers too)."""
    if at is None:
        return curve(value) if is_curve(value) else to_value(value)   # type: ignore[arg-type]
    node = to_value(value)
    if isinstance(node, Node) and isinstance(value, list) and len(value) > at and is_curve(value[at]):
        node.items[at] = curve(value[at])
    return node


def to_sub(doc: dsgo.Document, template: Node) -> None:
    """Weapon_BasicShoot -> Weapon_Sub in place, the way EDF6 converted EDF5's vehicle calls and support units: the
    class and custom_parameter in EDF6's named form, the template's (an EDF6 Weapon_Sub
    of the same category) with EDF5's own animation (custom_parameter[0], unless the template plays the category's
    'vehicle_call') and animation speed (custom_parameter[3])."""
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Weapon_BasicShoot':
        raise Unsupported(f"{r.get('xgs_scene_object_class')} -> {SUB}")
    old = r.get('custom_parameter')
    custom = copy.deepcopy(template.get('custom_parameter'))
    if not (isinstance(old, Node) and len(old.items) == 4 and isinstance(custom, Node) and custom.names):
        raise Unsupported('custom_parameter is not the [animation, ?, ?, speed] list EDF6 converted')
    if custom.get('animation') != 'vehicle_call':
        custom.set('animation', old.items[0])
    custom.set('animation_speed', float(old.items[3]))
    r.set('xgs_scene_object_class', SUB)
    r.set('custom_parameter', custom)


def text_value(v: object) -> object:
    """A WEAPONTEXT row's value (sgo.read typed) as plain JSON, its curves 7 long (the same rule as the SGO's)."""
    if is_curve(v):
        return [_plain(x) for x in v] + [flag(v[0])]   # type: ignore[index]
    if isinstance(v, list):
        return [text_value(x) for x in v]
    return _plain(v)


def json_node(v: object) -> dsgo.Value:
    """Plain JSON (text_value's) as a DSGO value."""
    if isinstance(v, list):
        return Node([json_node(x) for x in v])
    if isinstance(v, bool):
        raise Unsupported('bool')
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        return v
    raise Unsupported(f'{type(v).__name__} in a text row')


def app_paths(v: dsgo.Value) -> list[str]:
    """Every 'app:/...' resource path a converted weapon names."""
    if isinstance(v, str):
        return [v] if v.lower().startswith('app:/') else []
    if isinstance(v, Node):
        return [p for c in v.items for p in app_paths(c)]
    return []
