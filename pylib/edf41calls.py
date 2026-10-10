"""EDF4.1's vehicle requests and bomber calls as EDF6 weapons (plan P5).

    vehicle_call(members, template, vehicle) -> dsgo.Document   a 4.1 vehicle request (4.1 categories 36-39)
    bomber_call(members, template) -> dsgo.Document              a 4.1 bomber call (category 31, mode 2)
    VEHICLES: 4.1 vehicle SGO (lower case) -> the EDF6 vehicle it becomes; WEAPONS: 4.1 vehicle weapon -> EDF6's

A 4.1 vehicle request is a Weapon_Throw whose Ammo_CustomParameter is laid out as EDF6's own vehicle calls (Weapon_Sub)
lay theirs out: [0.05, 180, 120, 1, [transport, container, vehicle, settings], voices] (EDF6 eWeapon418); 4.1 drops
with Transporter401 (a class EDF6 no longer has), EDF6 and EDF5 with v508_transport / v509_transportbox. So the call
is the EDF6 template's (an EDF6 call of the same vehicle and category: its class, custom_parameter, transport and
voices; the 4.1 vehicle-named voices are not in EDF6's banks) with 4.1's own: its vehicle (VEHICLES), its settings
(the vehicle's armour and weapons, the weapons renamed by WEAPONS, a weapon EDF6 split into left / right twice) when
laid out as the template's (Begaruta 6 weapon slots in 4.1, 8 in EDF6; the bike's another shape: the template's then),
and its use (AmmoCount, ReloadTime, ReloadType, EnergyChargeRequire).
A 4.1 bomber call (Weapon_Throw smoke candle, mode 2) holds the bomber run in ACP[4] as EDF6's Weapon_RadioContact calls
hold theirs in ACP[2] ([Bomber401.sgo, count, interval, height, speed, ...], docs/mission-airstrike-re.md): the
template's call with 4.1's run and use.
Research: jobs/4bf89026/tmp/vehicles41/REPORT.md (2026-10-10).
"""
from __future__ import annotations

import copy

import dsgo
import edf5port
import sgo
from dsgo import Node

# 4.1 vehicle (lower case, as the calls name it) -> the EDF6 vehicle in its place.
VEHICLES: dict[str, str] = {
    'vehicle301_tank.sgo': 'v505_tank.sgo',
    'vehicle301_tank_bulletgirls.sgo': 'v505_tank.sgo', 'vehicle301_tank_dczero.sgo': 'v505_tank.sgo',
    'vehicle301_tank_dc55.sgo': 'v505_tank.sgo', 'vehicle301_tank_edf2pv2.sgo': 'v505_tank.sgo',
    'vehicle301_tank_natsuiro.sgo': 'v505_tank.sgo',
    'vehicle406_begaruta.sgo': 'v504_begaruta.sgo', 'vehicle406_begaruta_red.sgo': 'v504_begaruta_red.sgo',
    'vehicle406_begaruta_gold.sgo': 'v504_begaruta_gold.sgo', 'vehicle406_begaruta_green.sgo': 'v504_begaruta.sgo',
    'vehicle302_heli.sgo': 'v506_heli.sgo',
    'vehicle408_motorbike.sgo': 'v503_bike.sgo',
    'vehicle501_fortressrobo.sgo': 'v515_retrobalam.sgo',
    'vehicle403_tank.sgo': 'vehicle403_tank.sgo', 'vehicle402_rocket.sgo': 'vehicle402_rocket.sgo',
    'vehicle404_bigtank.sgo': 'vehicle404_bigtank.sgo', 'vehicle502_groundrobogold.sgo': 'vehicle502_groundrobogold.sgo',
}
# 4.1 vehicle weapon -> EDF6's (two: EDF6 split it into a left and a right one); [] none: the template's settings.
WEAPONS: dict[str, list[str]] = {
    'v_301tank_cannon01.sgo': ['v_505tank_cannon01'], 'v_301tank_cannon02.sgo': ['v_505tank_cannon02'],
    'v_301tank_cannon02l.sgo': ['v_505tank_cannon02L'], 'v_301tank_cannon03.sgo': ['v_505tank_cannon03'],
    'v_301tank_cannon03l.sgo': ['v_505tank_cannon03L'], 'v_301tank_cannon04.sgo': ['v_505tank_cannon04'],
    'v_301tank_cannon04l.sgo': ['v_505tank_cannon04L'], 'v_301tank_drean_cannon.sgo': [],
    'v_406begaruta_cannon01.sgo': ['v_504begaruta_cannon01_l', 'v_504begaruta_cannon01_r'],
    'v_406begaruta_cannon02solid.sgo': ['v_504begaruta_cannon02Solid_l', 'v_504begaruta_cannon02Solid_r'],
    'v_406begaruta_flamethrower01.sgo': ['v_504begaruta_flamethrower01'],
    'v_406begaruta_gatling01.sgo': ['v_504begaruta_gatling01_l', 'v_504begaruta_gatling01_r'],
    'v_406begaruta_gatling02.sgo': ['v_504begaruta_gatling02_l', 'v_504begaruta_gatling02_r'],
    'v_406begaruta_longcannon015_r.sgo': ['v_504begaruta_longCannon015_r'],
    'v_406begaruta_longcannon01_l.sgo': ['v_504begaruta_longCannon01_l'],
    'v_406begaruta_longcannon01_r.sgo': ['v_504begaruta_longCannon01_r'],
    'v_406begaruta_longcannon02_l.sgo': ['v_504begaruta_longCannon02_l'],
    'v_406begaruta_longcannon02_r.sgo': ['v_504begaruta_longCannon02_r'],
    'v_406begaruta_longspreadcannon01_l.sgo': ['v_504begaruta_longSpreadCannon01_l'],
    'v_406begaruta_longspreadcannon01_r.sgo': ['v_504begaruta_longSpreadCannon01_r'],
    'v_406begaruta_missile01_l.sgo': ['v_504begaruta_missile01_l'],
    'v_406begaruta_missile01_r.sgo': ['v_504begaruta_missile01_r'],
    'v_406begaruta_rocket01.sgo': ['v_504begaruta_rocket01_l', 'v_504begaruta_rocket01_r'],
    'v_302heli_lasercannon01.sgo': ['v_506heli_LaserCannon01_l', 'v_506heli_LaserCannon01_r'],
    'v_302heli_gatling01.sgo': ['v_506heli_gatling01_l', 'v_506heli_gatling01_r'],
    'v_302heli_missile01.sgo': ['v_506heli_missile01'], 'v_302heli_napalm01.sgo': ['v_506heli_napalm01'],
    'v_408motorbike_gun.sgo': ['v_503_bike_gun_l', 'v_503_bike_gun_r'],
}
USE = ('AmmoCount', 'ReloadTime', 'ReloadType', 'EnergyChargeRequire')


def leaf(path: str) -> str:
    return path.replace('\\', '/').rsplit('/', 1)[-1].lower()


def shape(v: object) -> object:
    """A value's layout: lists by length and element layout, a string / number as its kind."""
    if isinstance(v, Node):
        v = v.items
    if isinstance(v, list):
        return ('list', tuple(shape(x) for x in v))
    return 'str' if isinstance(v, str) else 'num'


def _is_row(v: object) -> bool:
    return isinstance(v, list) and bool(v) and isinstance(v[0], str) and leaf(v[0]).startswith('v_')


def _weapons(settings: object) -> object | None:
    """4.1's settings (plain values) with every list of vehicle-weapon rows ([weapon, ...]) renamed to EDF6's weapons
    (a weapon EDF6 split into left / right: a row each); None when one has no EDF6 counterpart."""
    if isinstance(settings, list) and settings and all(_is_row(x) for x in settings):
        rows = []
        for row in settings:
            names = WEAPONS.get(leaf(row[0]))
            if not names:
                return None
            rows += [[f'app:/weapon/{n}.sgo', *copy.deepcopy(row[1:])] for n in names]
        return rows
    if isinstance(settings, list):
        out = []
        for x in settings:
            y = _weapons(x)
            if y is None and x is not None and _contains_row(x):
                return None
            out.append(y if y is not None else copy.deepcopy(x))
        return out
    return settings


def _contains_row(v: object) -> bool:
    return _is_row(v) or (isinstance(v, list) and any(_contains_row(x) for x in v))


def _use(r: Node, members: dict) -> None:
    for k in USE:
        if k in members:
            v = members[k]
            if k == 'EnergyChargeRequire' and not isinstance(v, list):
                v = [v, v]   # edf5port.weapon41: one number in 4.1, [curve, value] in every EDF6 weapon
            r.set(k, edf5port.to_value(v))


def vehicle_call(members: dict, template: Node, vehicle: str) -> dsgo.Document:
    """`members`: the 4.1 request's SGO members (sgo.read); `template`: the EDF6 call (DSGO root); `vehicle`: the
    EDF6 vehicle SGO (VEHICLES). edf5port.Unsupported when the 4.1 call is not a vehicle request laid out so."""
    acp = members.get('Ammo_CustomParameter')
    if not (isinstance(acp, list) and len(acp) > 4 and isinstance(acp[4], list) and len(acp[4]) > 3):
        raise edf5port.Unsupported('not a vehicle request')
    r = copy.deepcopy(template)
    tacp = r.get('Ammo_CustomParameter')
    drop = tacp.items[4]
    drop.items[2] = f'app:/Object/{vehicle}'
    mine = _weapons(sgo.plain(acp[4][3])) if isinstance(acp[4][3], list) else None
    if mine is not None:
        converted = edf5port.to_value(mine)
        if shape(converted) == shape(drop.items[3]):
            drop.items[3] = converted
    _use(r, members)
    return dsgo.Document(r, [])


def bomber_call(members: dict, template: Node) -> dsgo.Document:
    """`members`: a 4.1 bomber call's SGO members; `template`: an EDF6 Weapon_RadioContact bomber call."""
    acp = members.get('Ammo_CustomParameter')
    if not (isinstance(acp, list) and len(acp) > 4 and isinstance(acp[4], list)):
        raise edf5port.Unsupported('not a bomber call')
    r = copy.deepcopy(template)
    tacp = r.get('Ammo_CustomParameter')
    run = edf5port.to_value(sgo.plain(acp[4]))
    if shape(run) != shape(tacp.items[2]):
        raise edf5port.Unsupported("the bomber run is not laid out as EDF6's")
    tacp.items[2] = run
    _use(r, members)
    return dsgo.Document(r, [])
