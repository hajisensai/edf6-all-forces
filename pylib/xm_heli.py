"""An X-Morph: Defense helicopter's vehicle and weapons (pylib/xm_units.py family 'heli', docs/xmorph-port.md 「直升机」):
the stock V506_HELI.SGO in its own model (pylib/xm_air_model.py: the archive EDF6VC_XM_<KEY>.MRAB and its animation
EDF6VC_XM_<KEY>.CAS), so it flies as the game's helis do (the player's own flight; an NPC pilot's: src/heli.cpp flies any
506), with the unit's weapons on the V506's four holders (Unit.air) and its durability.

    files(game, unit) -> {Mods-relative path: bytes}      the vehicle SGO, its model, animation and weapons
    vehicle_sgo(game, unit, info) -> bytes                 the SGO alone (info: the model folder's model.json)

What the V506's SGO keeps: its class, heli flight (vehicle_setup's heli params: the request keeps them, tools/call_weapons.py),
rotor sounds, ragdoll and dead effect (the model keeps every bone they name), its seat. What changes: the model and its
animation; the collision box (the model's, cut at its origin: vcobjects.on_origin); the MAB's locators measured from the
model's origin (their parents renamed mdl, as every jet's: vcobjects.JET_MAB_BONES) with its door beside the box and the
seat camera over the airframe (Unit.eye); the third-person camera scaled to the airframe; the weapons and their holders
(xm_gun_l / xm_gun_r / xm_missile, bones of the model at the blueprint's muzzles; the fuel tank on body); the durability.
"""
from __future__ import annotations

import copy

import dsgo
import sgo
import vcobjects as vc
import xm_air_model as xa
import xm_units

HOLDERS = ('xm_gun_l', 'xm_gun_r', 'xm_missile')
STOCK_SGO = 'V506_HELI.SGO'
STOCK_GUNS = ('V_506HELI_GATLING01_L.SGO', 'V_506HELI_GATLING01_R.SGO')
STOCK_MISSILE = 'V_506HELI_MISSILE01.SGO'
STOCK_LENGTH = 12.0          # m: the V506's airframe, whose third-person camera (game_object_camera_setting) is scaled
LANGS = ('en', 'ja', 'cn', 'sc', 'kr')   # HeliArm.names' order (vcobjects._local)


def sgo_name(key: str) -> str:
    return f'EDF6VC_{key.upper()}'


def weapon_name(key: str, slot: str) -> str:
    """WEAPON/<this>.SGO: the unit's weapon on a holder (slot: gun_l, gun_r, missile)."""
    return f'EDF6VC_{key.upper()}_{slot.upper()}'


def weapon_paths(unit: xm_units.Unit) -> tuple[str, str, str]:
    """The SGO paths of its three weapons, holder order (the transport keeps the stock 506's: Unit.air without arms)."""
    if 'guns' not in unit.air:
        return tuple('app:/weapon/' + f.lower() for f in (*STOCK_GUNS, STOCK_MISSILE))   # type: ignore[return-value]
    return tuple(f'app:/weapon/{weapon_name(unit.key, s).lower()}.sgo' for s in ('gun_l', 'gun_r', 'missile'))  # type: ignore


def holders(unit: xm_units.Unit, info: dict) -> dict[str, tuple[float, float, float]]:
    """Each holder's bone point (model frame): the guns' at their muzzles (left, right), the missile's the middle of its
    muzzles; an unarmed unit's (the transport) where the V506's are on its airframe: under its nose, either side."""
    pts = info['attachments']
    if 'guns' not in unit.air:
        (x0, y0, z0), (x1, _y1, z1) = info['bounds']
        return {'xm_gun_l': (x1 * 0.3, y0 + 1.0, z1 * 0.8), 'xm_gun_r': (x0 * 0.3, y0 + 1.0, z1 * 0.8),
                'xm_missile': (0.0, y0 + 1.0, z1 * 0.5)}
    left, right = (tuple(pts[m]) for m in unit.air['guns'].muzzles)
    ms = [pts[m] for m in unit.air['missile'].muzzles]
    mid = tuple(sum(p[k] for p in ms) / len(ms) for k in range(3))
    return {'xm_gun_l': left, 'xm_gun_r': right, 'xm_missile': mid}   # type: ignore[dict-item]


def weapon_sgo(game, unit: xm_units.Unit, slot: str) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The stock weapon (HeliArm.template; a gun pair's {side} its side's) with the game's numbers and the unit's names."""
    arm = unit.air['missile' if slot == 'missile' else 'guns']
    template = arm.template.format(side='L' if slot == 'gun_l' else 'R')
    doc = dsgo.parse(game.read('WEAPON', template))
    r = doc.root
    for k, v in arm.numbers.items():
        if k not in r.names.values():
            raise ValueError(f'{template}: no {k}')
        r.set(k, float(v))
    side = {'gun_l': (' (L)', '（左）'), 'gun_r': (' (R)', '（右）')}.get(slot, ('', ''))
    local = dict(zip(LANGS, arm.names))
    for key in list(r.names.values()):   # every name.* row (vcobjects._named): its language's, else the English
        if key.startswith('name.'):
            lang = key[5:].lower()
            r.set(key, local.get(lang, arm.names[0]) + (side[0] if lang in ('en', 'kr') or lang not in local else side[1]))
    return dsgo.write(doc)


def _box(info: dict) -> list[list[float]]:
    (x0, y0, z0), (x1, y1, z1) = info['bounds']
    return vc.on_origin([[(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], [(x1 - x0) / 2, (y1 - y0) / 2, (z1 - z0) / 2]])


def _camera(unit: xm_units.Unit, info: dict) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    """The seat camera (eye, look; model frame): Unit.eye over the airframe, looking at its middle ahead."""
    (x0, y0, z0), (x1, y1, z1) = info['bounds']
    look = (0.0, (y0 + y1) / 2, z1 * 0.5)
    return tuple(float(c) for c in unit.eye), look   # type: ignore[return-value]


def vehicle_sgo(game, unit: xm_units.Unit, info: dict) -> bytes:  # noqa: ANN001 - rootcpk.Game
    key = unit.key
    version, m = sgo.read(game.read('OBJECT', STOCK_SGO))
    if m.get('xgs_scene_object_class') != 'Vehicle506_Helicopter' or 'vehicle_setup' not in m:
        raise ValueError(f'{STOCK_SGO}: not the stock 506 heli')
    model = m['animation_model']
    mab = model[2]
    for at, old in vc.JET_MAB_BONES:
        mab = sgo.replace_utf16(mab, at, old, vc.JET_ROOT_BONE)
    m['animation_model'] = [[f'app:/object/{xa.archive_name(key).lower()}', xa.mdb_name(key)],
                            f'app:/object/{xa.animation_name(key).lower()}', mab]
    setup = m['vehicle_setup']
    stock = {w[0]: w for w in setup[3]}
    fuel = stock['app:/weapon/v_fuel01.sgo']
    guns = [w for w in setup[3] if w is not fuel]
    if len(guns) != 3:
        raise ValueError(f'{STOCK_SGO}: {len(guns)} weapons besides its fuel tank, not 3')
    setup[3] = vc.with_fuel([[path, copy.deepcopy(w[1])] for path, w in zip(weapon_paths(unit), guns)], fuel)
    m['vehicle_weapon_setting'] = vc.with_fuel([[h, 0] for h in HOLDERS], ['body', -1])
    if len(m['vehicle_weapon_setting']) != len(setup[3]):
        raise ValueError(f'{key}: {len(setup[3])} weapons for {len(m["vehicle_weapon_setting"])} holders')
    m['game_object_durability'] = float(unit.durability)
    box = _box(info)
    rb = m['heli_rigid_body']
    m['heli_rigid_body'] = [box[0], box[1], rb[2]]
    origin = (0.0, 0.0, 0.0)
    vc.move_door(m, box, origin)
    vc.place_seat_camera(m, box, _camera(unit, info), origin)
    size = max(info['size'][0], info['size'][2])
    k = max(1.0, size / STOCK_LENGTH)
    look, eye = m['game_object_camera_setting']
    m['game_object_camera_setting'] = [[float(vc._value(c)) * k for c in look], [float(vc._value(c)) * k for c in eye]]
    out = sgo.write(version, m)
    seats = unit.air.get('seats', 0)
    if seats:
        import make_jets
        out = make_jets.with_passenger_seats(out, 0, seats, game.read('OBJECT', make_jets.GUNNER_STOCK))
        make_jets.check_passenger_seats(out, 1 + seats, sgo_name(key), 1)
    check(out, unit, info)
    return out


def check(data: bytes, unit: xm_units.Unit, info: dict) -> None:
    """Re-read the SGO: still the 506 class with its heli params; our model, animation and weapons; a holder a weapon;
    the door on the ground beside its box; the seat camera over the airframe."""
    _, m = sgo.read(data)
    key = unit.key
    if m.get('xgs_scene_object_class') != 'Vehicle506_Helicopter':
        raise ValueError(f'{key}: not a 506')
    if m['animation_model'][0] != [f'app:/object/{xa.archive_name(key).lower()}', xa.mdb_name(key)]:
        raise ValueError(f'{key}: model {m["animation_model"][0]}')
    weapons = [w[0] for w in m['vehicle_setup'][3]]
    if [w for w in weapons if 'fuel' not in w] != list(weapon_paths(unit)):
        raise ValueError(f'{key}: weapons {weapons}')
    if [h[0] for h in m['vehicle_weapon_setting'] if h[1] != -1] != list(HOLDERS):
        raise ValueError(f'{key}: holders {m["vehicle_weapon_setting"]}')
    if float(vc._value(m['game_object_durability'])) != unit.durability:
        raise ValueError(f'{key}: durability')
    origin = (0.0, 0.0, 0.0)
    vc.check_door(data, origin)
    vc.check_seat_camera(data, tuple(tuple(b) for b in info['bounds']), _camera(unit, info), origin)


def files(game, unit: xm_units.Unit) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    """Everything the unit installs (Mods-relative): its SGO, model and animation in OBJECT, its weapons in WEAPON. Its
    model folder must be here (xm_units.deployed): the caller says so and builds nothing where it is not."""
    import obj_model as om
    folder = om.model_dir(xm_units.folder(unit.key))
    if folder is None:
        raise FileNotFoundError(f'{unit.key}: no model folder {xm_units.folder(unit.key)}')
    info = xa.read_info(folder)
    arc, anim, _info = xa.build_archive(game, folder, unit.key, holders(unit, info))
    out = {f'OBJECT/{xa.archive_name(unit.key)}': arc, f'OBJECT/{xa.animation_name(unit.key)}': anim,
           f'OBJECT/{sgo_name(unit.key)}.SGO': vehicle_sgo(game, unit, info)}
    if 'guns' in unit.air:
        for slot in ('gun_l', 'gun_r', 'missile'):
            out[f'WEAPON/{weapon_name(unit.key, slot)}.SGO'] = weapon_sgo(game, unit, slot)
    return out


def model_inputs(key: str) -> list[str]:
    """The model folder's files the build reads (pylib/buildcache.py hashes them)."""
    import obj_model as om
    folder = om.model_dir(xm_units.folder(key))
    return [] if folder is None else xa.model_files(folder)


