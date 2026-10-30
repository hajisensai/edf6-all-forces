"""The X-Morph walkers (pylib/xm_units.py family 'mech') as EDF6 vehicles: each on a stock walker (its host: class, CAS,
ragdoll, seats, the request it is requested like), the X-Morph model retargeted onto the host's skeleton (pylib/xm_rig.py),
its weapons the host's holders' (vehicle_weapon_setting) carrying weapon files of its own: a stock vehicle weapon with
no model of its own (v_null) as the template, the game's numbers (xm_units Unit.guns / Arm.numbers), its muzzles moved
onto the X-Morph gun mouths in the holder bone's frame.

    WALKERS[key]: Walker             the host and the holders
    weapons(key) -> (path or '', ...)  the request's weapon list, one entry a host holder ('' an empty one)
    build(game, key) -> {Mods path: bytes}   MRAB, vehicle SGO, weapon SGOs (model_lod.finish gives the LODs)
    check(files, key)

Light at import (vcobjects reads WALKERS and weapons()): the model code is imported when building.
"""
from __future__ import annotations

import copy
import struct
from dataclasses import dataclass

import xm_units


@dataclass(frozen=True)
class Holder:
    """One of the host's weapon holders carrying a weapon of the unit's: the holder bone (the host's
    vehicle_weapon_setting), the stock weapon file it is made from and that file's AmmoClass (checked), the X-Morph
    attachments its muzzles go to (in turn: the template's muzzles each take the next), and the unit's arm whose
    numbers and names it takes ('' the main gun: Unit.guns / Unit.gun)."""
    bone: str
    template: str
    ammo_class: str
    muzzles: tuple[str, ...]
    arm: str = ''


@dataclass(frozen=True)
class Walker:
    """The host's vehicle SGO, its request family (the first the template: tools/call_weapons.py vehicle_sgo; all of
    them set the multipliers by level), the setup entry holding the weapon list (Begaruta / Proteus requests:
    [multipliers, params, params, [seat params], weapons, [x]]) and the holders the unit uses."""
    stock: str
    family: tuple[str, ...]
    weapons_at: int
    holders: tuple[Holder, ...]


MG = ('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01')       # a tank's coaxial gun: v_null, one muzzle
CANNON = ('V_407BIGBEGARUTA_CANNON.SGO', 'RocketBullet01')  # the Proteus's gun: v_null, one muzzle
MISSILE = ('V_407BIGBEGARUTA_MISSILE.SGO', 'MissileBullet01')  # the Proteus's missiles: v_null, a muzzle per tube

WALKERS: dict[str, Walker] = {
    # The light mech on the Nix (the EWEAPON356 family's v504 Begaruta, the Air Raider's): its two hand guns on the
    # Nix's hand holders, where xm_rig puts the X-Morph guns' mouths (att_muzzle_1 the left gun's).
    'xm_small_mech': Walker('V504_BEGARUTA_BLUE', ('EWEAPON356', 'EWEAPON357', 'EWEAPON358', 'EWEAPON359', 'EWEAPON360',
                                                   'EWEAPON362', 'EWEAPON363', 'EWEAPON364', 'EWEAPON365'), 4,
                            (Holder('weapon_hand_r', *MG, ('att_muzzle_2',)),
                             Holder('weapon_hand_l', *MG, ('att_muzzle_1',)))),
    # The heavy mech on the Proteus Mk2 (the EWEAPON387 family): its gun pods on the Proteus's barrels' holders, its
    # missile racks on the launcher's.
    'xm_large_mech': Walker('V614_PROTEUS_MK2_CALL', ('EWEAPON387', 'EWEAPON388', 'EWEAPON390', 'EWEAPON392'), 4,
                            (Holder('gun_mount_barrel_l', *CANNON, ('att_main_gun_left',)),
                             Holder('gun_mount_barrel_r', *CANNON, ('att_main_gun_right',)),
                             Holder('missile_launcher', *MISSILE, ('att_missiles_left', 'att_missiles_right'),
                                    'missiles'))),
}


# The host model whose cockpit fits (pylib/cockpit_fits_mech.py FITS) the walker's model takes (cockpit_fits_plugin):
# the same skeleton, the same seats.
COCKPITS = {'xm_small_mech': 'V504_BEGARUTA_BLUE', 'xm_large_mech': 'V614_PROTEUS_MK2'}


def model_file(key: str) -> str:
    return f'EDF6VC_{key.upper()}.MRAB'


def vehicle_file(key: str) -> str:
    return f'EDF6VC_{key.upper()}.SGO'


def weapon_file(key: str, k: int) -> str:
    return f'EDF6VC_{key.upper()}_W{k + 1}.SGO'


# The host's holders in vehicle_weapon_setting order (the request's weapon list has one entry each).
HOST_HOLDERS = {
    'V504_BEGARUTA_BLUE': ('weapon_hand_r', 'weapon_hand_l', 'weapon_lowerArm_r', 'weapon_lowerArm_l',
                           'weapon_shoulder_r', 'weapon_shoulder_l', 'weapon_upperArm_r', 'weapon_upperArm_l'),
    'V614_PROTEUS_MK2_CALL': ('gun_mount_barrel_l', 'gun_mount_barrel_r', 'missile_launcher'),
}


def weapons(key: str) -> tuple[str, ...]:
    """The request's weapon list (vcobjects GroundVehicle.weapons): per host holder, in its order, the app: path of the
    unit's weapon on it, '' where the unit has none (an empty entry: the holder carries nothing)."""
    w = WALKERS[key]
    on = {h.bone: k for k, h in enumerate(w.holders)}
    return tuple(f'app:/weapon/{weapon_file(key, on[b]).lower()}' if b in on else '' for b in HOST_HOLDERS[w.stock])


# ------------------------------------------------------------------------------------------ building

def _numbers(u: xm_units.Unit, h: Holder) -> tuple[dict, tuple[str, ...]]:
    """(SGO numbers, names EN / JA / CN / SC / KR) of the holder's weapon."""
    if not h.arm:
        en, sc, ja, cn = u.gun
        return u.guns, (en, ja, cn, sc, en)
    arm = next(a for a in u.arms if a.key == h.arm)
    return arm.numbers, arm.names


def _move_muzzles(r, template: str, points: list[tuple[float, float, float]]) -> None:  # noqa: ANN001 - dsgo.Node
    """Every muzzle of weapon root `r` (vcobjects.mab_muzzles) to points[k % len(points)]: ValueError if a muzzle's
    vec4 is shared with any other record of the block (moving it would move that too)."""
    import dsgo
    import vcobjects as vc
    model = r.get('animation_model')
    block = bytearray(model.items[2].data)
    muzzles = vc.mab_muzzles(bytes(block))
    if not muzzles:
        raise ValueError(f'{template} 没有炮口')
    heads, end = struct.unpack_from('<2I', block, 0x14)
    first = heads + 8 * struct.unpack_from('<H', block, 0x0C)[0]
    for k, (_name, _node, vec) in enumerate(muzzles):
        users = [(rec, off) for rec in range(first, end, 0x20) for off in (0xC, 0x10, 0x14)
                 if rec + struct.unpack_from('<i', block, rec + off)[0] == vec]
        if len(users) != 1:
            raise ValueError(f'{template} 的炮口坐标被 {len(users)} 处共用')
        struct.pack_into('<3f', block, vec, *points[k % len(points)])
    model.items[2] = dsgo.Blob(bytes(block), model.items[2].kind)


def weapon_sgo(game, u: xm_units.Unit, h: Holder, points: list[tuple[float, float, float]]) -> bytes:  # noqa: ANN001
    """The holder's weapon: its template with the unit's numbers and names, its muzzles on `points` (holder frame)."""
    import dsgo
    doc = dsgo.parse(game.read('WEAPON', h.template))
    r = doc.root
    if r.get('AmmoClass') != h.ammo_class:
        raise ValueError(f'{h.template} 的弹类是 {r.get("AmmoClass")}，不是 {h.ammo_class}')
    if r.get('animation_model').items[0].items[0].lower() != 'app:/weapon/v_null.rab':
        raise ValueError(f'{h.template} 有自己的模型（应为 v_null）')
    numbers, names = _numbers(u, h)
    for name, value in numbers.items():
        if r.get(name) is None:
            raise ValueError(f'{h.template} 缺少 {name}')
        r.set(name, float(value))
    _move_muzzles(r, h.template, points)
    local = dict(zip(('en', 'ja', 'cn', 'sc', 'kr'), names))
    for name in list(r.names.values()):   # every name.* row (vcobjects._named): its language's, else the English
        if name.startswith('name.'):
            r.set(name, local.get(name[5:].lower(), names[0]))
    return dsgo.write(doc)


def holder_points(md, info: dict, h: Holder) -> list[tuple[float, float, float]]:  # noqa: ANN001 - mdb.Mdb
    """The holder's muzzles (xm_rig.build info['points']: model space, bind pose) in its bone's frame (row vectors:
    local = [p, 1] @ inverse(bind world))."""
    import numpy as np
    from mdb import bind_world
    i = md.bone_index(h.bone)
    if i < 0:
        raise ValueError(f'宿主没有挂载骨骼 {h.bone}')
    inv = np.linalg.inv(np.asarray(bind_world(md)[i], dtype=np.float64).reshape(4, 4))
    out = []
    for att in h.muzzles:
        if att not in info['points']:
            raise ValueError(f'模型没有挂点 {att}')
        p = np.append(np.asarray(info['points'][att][1], dtype=np.float64), 1.0) @ inv
        out.append(tuple(round(float(c), 3) for c in p[:3]))
    return out


def _setup_weapons(stock_list: list, paths: tuple[str, ...]) -> list:
    """The host's weapon list (plain values) with the unit's: each holder an entry [path, params] (the params the
    stock entry's on that holder, else the first stock weapon's), [0.0] where it carries nothing."""
    first = next(e for e in stock_list if isinstance(e, list) and len(e) == 2 and isinstance(e[0], str))
    if len(stock_list) != len(paths):
        raise ValueError(f'宿主有 {len(stock_list)} 个挂载，单位给了 {len(paths)} 个')
    out = []
    for entry, path in zip(stock_list, paths):
        if not path:
            out.append([0.0])
            continue
        base = entry if isinstance(entry, list) and len(entry) == 2 and isinstance(entry[0], str) else first
        out.append([path, copy.deepcopy(base[1])])
    return out


SETUPS = ('vehicle_setup', 'mission_setup')   # the host SGO's setups holding the weapon list (weapons_at)


def vehicle_sgo(game, key: str) -> bytes:  # noqa: ANN001
    """The host's vehicle SGO with the unit's model, durability and weapons (its own setup's list as the request's; its
    mission_setup's too where the host has one: the Proteus's call SGO, placed as it is by the test range)."""
    import sgo
    u = xm_units.UNITS[key]
    w = WALKERS[key]
    data = game.read('OBJECT', w.stock + '.SGO')
    model = [f'app:/object/{model_file(key).lower()}', f'edf6vc_{key}.mdb']
    if data[:4] == b'DSGO':
        import dsgo
        doc = dsgo.parse(data)
        r = doc.root
        ref = r.get('animation_model').items[0]
        ref.items[0], ref.items[1] = model
        r.set('game_object_durability', float(u.durability))

        def node(v):  # noqa: ANN001, ANN202 - a plain list as a DSGO array (numbers as floats)
            return dsgo.Node([node(c) for c in v]) if isinstance(v, list) else (v if isinstance(v, str) else float(v))
        for name in SETUPS:
            if name in r.names.values():
                setup = r.get(name)
                setup.items[w.weapons_at] = node(_setup_weapons(dsgo.to_py(setup.items[w.weapons_at]), weapons(key)))
        return dsgo.write(doc)
    version, members = sgo.read(data)
    members['animation_model'][0][0], members['animation_model'][0][1] = model
    members['game_object_durability'] = float(u.durability)
    for name in SETUPS:
        if name in members:
            setup = members[name]
            setup[w.weapons_at] = _setup_weapons([sgo.plain(e) for e in setup[w.weapons_at]], weapons(key))
    return sgo.write(version, members)


def build(game, key: str) -> dict[str, bytes]:  # noqa: ANN001
    """Every file of the unit, {Mods path: bytes}, checked; its models given their LODs (model_lod.finish)."""
    import model_lod
    import xm_rig
    u = xm_units.UNITS[key]
    arc, md, info = xm_rig.build(game, key)
    files = {f'OBJECT/{model_file(key)}': arc, f'OBJECT/{vehicle_file(key)}': vehicle_sgo(game, key)}
    for k, h in enumerate(WALKERS[key].holders):
        files[f'WEAPON/{weapon_file(key, k)}'] = weapon_sgo(game, u, h, holder_points(md, info, h))
    check(files, key)
    # its seats' first-person cockpits (pylib/cockpits.py, the host's fits), then its model merged and given its LODs
    import cockpits
    files = model_lod.finish(cockpits.graft_files(game, files))
    check(files, key)
    return files


def check(files: dict[str, bytes], key: str) -> None:
    """The vehicle SGO is the host's class on the unit's model, durability and weapon list; every weapon it names is
    written and every holder it uses is the host's."""
    import sgo
    u = xm_units.UNITS[key]
    w = WALKERS[key]
    v = sgo.load(data=files[f'OBJECT/{vehicle_file(key)}'])
    stock_cls = {'V504_BEGARUTA_BLUE': 'Vehicle504_begaruta', 'V614_PROTEUS_MK2_CALL': 'VehicleBigBegaruta'}[w.stock]
    assert v['xgs_scene_object_class'] == stock_cls, v['xgs_scene_object_class']
    assert v['animation_model'][0][0] == f'app:/object/{model_file(key).lower()}', v['animation_model'][0]
    assert v['animation_model'][0][1] == f'edf6vc_{key}.mdb', v['animation_model'][0]
    assert abs(v['game_object_durability'] - u.durability) < 1e-3, v['game_object_durability']
    assert f'OBJECT/{model_file(key)}' in files
    assert [b for b, _ in v['vehicle_weapon_setting']] == list(HOST_HOLDERS[w.stock]), v['vehicle_weapon_setting']
    for name in SETUPS:
        if name in v or name == 'vehicle_setup':
            listed = [e[0] if isinstance(e, list) and e and isinstance(e[0], str) else '' for e in v[name][w.weapons_at]]
            assert tuple(listed) == weapons(key), (name, listed)
    for k in range(len(w.holders)):
        assert f'WEAPON/{weapon_file(key, k)}' in files, weapon_file(key, k)
