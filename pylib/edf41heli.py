"""EDF4.1's Nereid model, animations and physics on EDF6's 506 helicopter.

Only the runtime contract comes from 506.  The 302 MDB, CAS rest pose, Havok
ragdoll and gun mount frames stay together; mixing a 506 CAS or binding blob
with that MDB moves its body and leaves missing tail-rotor / wheel bones.
"""
from __future__ import annotations

import copy
import struct

import cas_graft
import cas_legacy
import dsgo
import legacy_assets
import mab_legacy
import mdb
import sgo

ROOTS = ('VEHICLE302_HELI.SGO',)
WEAPONS = {
    'v_302heli_gatling01.sgo': ['edf41_v_302heli_gatling01_l', 'edf41_v_302heli_gatling01_r'],
    'v_302heli_lasercannon01.sgo': ['edf41_v_302heli_lasercannon01_l', 'edf41_v_302heli_lasercannon01_r'],
    'v_302heli_missile01.sgo': ['edf41_v_302heli_missile01'],
    'v_302heli_napalm01.sgo': ['edf41_v_302heli_napalm01'],
}
_HOST = {
    'gatling01': ('V_506HELI_GATLING01_L', 'V_506HELI_GATLING01_R'),
    'lasercannon01': ('V_506HELI_LASERCANNON01_L', 'V_506HELI_LASERCANNON01_R'),
    'missile01': ('V_506HELI_MISSILE01',),
    'napalm01': ('V_506HELI_NAPALM01',),
}
_SOURCE_KEYS = ('game_object_durability', 'heli_rigid_body', 'vehicle_dead_effect')


def _rel(ref: str) -> str:
    if not isinstance(ref, str) or not ref.lower().startswith('app:/'):
        raise ValueError(f'302 resource is not an app path: {ref!r}')
    return ref[5:].replace('\\', '/').upper()


def _bones(archive: bytes, name: str) -> set[str]:
    files = [f for f in mdb.rab_read(archive).files if f.name.lower() == name.lower()]
    if len(files) != 1:
        raise ValueError(f'302 archive has {len(files)} models named {name}')
    model = mdb.mdb_read(files[0].data)
    return {model.name_of(b.name) for b in model.bones}


def _mab(blob: bytes) -> bytes:
    return mab_legacy.mab_from_edf5(blob)


def _vehicle_mab(source: bytes, template: bytes) -> bytes:
    """Keep 302 locator frames, with the 506 damage-locator names."""
    blob = _mab(source)
    old = [loc for group, loc in mab_legacy.mab_locators(blob) if group == 2]
    new = [loc for group, loc in mab_legacy.mab_locators(template) if group == 2]
    if len(old) != len(new):
        raise ValueError('302 and 506 have different damage locator counts')
    rename = {a.name: b.name for a, b in zip(old, new)}
    changes = {}
    layout = mab_legacy.mab_layout(blob)
    for at, _base, target in layout.offsets:
        if target < layout.strings_at:
            continue
        end = target
        while struct.unpack_from('<H', blob, end)[0]:
            end += 2
        text = blob[target:end].decode('utf-16le')
        if text in rename:
            changes[at] = rename[text]
    return mab_legacy.mab_set_strings(blob, changes)


def _check_members(members: dict, bones: set[str], template: dict, cas: bytes, host_cas: bytes) -> None:
    """Reject unresolved model / seat / weapon / physics names before emitting files."""
    model = members['animation_model']
    locators = mab_legacy.mab_locators(model[2])
    missing = sorted({loc.node for _lst, loc in locators} - bones)
    if missing:
        raise ValueError(f'302 MAB names missing model bones: {missing}')
    names = {loc.name for _lst, loc in locators}
    expected = {loc.name for _lst, loc in mab_legacy.mab_locators(template['animation_model'][2])}
    if not expected <= names:
        raise ValueError(f'302 lacks 506 locator names: {sorted(expected - names)}')
    for row in members['vehicle_riding_position']:
        if len(row) != 7 or any(n not in names for n in (row[0], row[1], row[2][0])):
            raise ValueError('302 seat does not meet the 506 locator / seven-field contract')
    for row in members['vehicle_weapon_setting']:
        if row[0] not in bones:
            raise ValueError(f'302 weapon mount names missing model bone: {row[0]}')
        if row[1] < -1 or row[1] >= len(members['vehicle_riding_position']):
            raise ValueError('302 weapon holder names a missing seat')
    def effect_bones(value):
        if isinstance(value, str):
            yield value
        elif isinstance(value, list):
            for item in value:
                yield from effect_bones(item)
    missing = set(effect_bones(members['vehicle_dead_effect'])) - bones
    if missing:
        raise ValueError(f'302 death effects name missing model bones: {sorted(missing)}')
    if len(members['vehicle_weapon_setting']) != len(members['vehicle_setup'][3]):
        raise ValueError('302 weapon holders and default setup disagree')
    required = set(template.get('animation_model_bone_mapping', ['mdl', 'rotor']))
    if not required <= bones:
        raise ValueError(f'302 lacks helicopter runtime bones: {sorted(required - bones)}')
    bindings = sgo.read(members['ragdoll'][1])[1]
    bound = {row[0][1] for row in bindings['animation_from_ragdoll']}
    bound |= {row[0][0] for row in bindings['ragdoll_from_animation']}
    if not bound <= bones:
        raise ValueError(f'302 ragdoll names missing model bones: {sorted(bound - bones)}')
    # EDF+0x650560 uses these players/states and variables without fallbacks.
    for player, states in cas_graft.states(host_cas).items():
        if not states <= cas_graft.states(cas).get(player, set()):
            raise ValueError(f'302 CAS does not satisfy helicopter player {player}')
    if not set(cas_graft.variable_names(host_cas)) <= set(cas_graft.variable_names(cas)):
        raise ValueError('302 CAS lacks a helicopter control variable')


def build(games, root: str) -> dict[str, bytes]:
    """Return complete OBJECT/WEAPON files; read4/read6 never write a game directory."""
    root = root.upper().removeprefix('OBJECT/')
    if root not in ROOTS:
        raise ValueError(f'not a 302 helicopter: {root}')
    _version, mine = sgo.read(games.read4('OBJECT/' + root))
    host_version, host = sgo.read(games.read6('OBJECT/V506_HELI.SGO'))
    if mine['xgs_scene_object_class'] != 'VehicleHelicopter302':
        raise ValueError('302 source has another vehicle class')
    if host['xgs_scene_object_class'] != 'Vehicle506_Helicopter':
        raise ValueError('506 template has another vehicle class')
    source_model = mine['animation_model']
    entry = source_model[0][0]
    archive_rel, cas_rel = _rel(entry[0]), _rel(source_model[1])
    archive = legacy_assets.convert(archive_rel, games.read4(archive_rel))
    bones = _bones(archive, entry[1])
    cas = cas_legacy.cas_from_legacy(games.read4(cas_rel))
    files = {archive_rel: archive, cas_rel: cas}
    # EDF4.1 lists an unavailable _light archive as a second LOD.  Its real first
    # MDB is kept, without adding a reference the installed 4.1 copy cannot feed.
    members = copy.deepcopy(host)
    members['animation_model'] = [copy.deepcopy(entry), source_model[1],
                                  _vehicle_mab(source_model[2], host['animation_model'][2])]
    members['ragdoll'] = copy.deepcopy(mine['ragdoll'])
    ragdoll_rel = _rel(members['ragdoll'][0])
    files[ragdoll_rel] = games.read4(ragdoll_rel)
    for key in _SOURCE_KEYS:
        members[key] = copy.deepcopy(mine[key])
    # Preserve the 302 boarding/camera points, but use EDF6's registered rider
    # animation, interaction radius and input table (302_HELI_DRIVER is absent).
    seat = copy.deepcopy(mine['vehicle_riding_position'][0])
    members['vehicle_riding_position'] = [[*seat[:3], *copy.deepcopy(host['vehicle_riding_position'][0][3:])]]
    members['vehicle_weapon_setting'] = [['gatling_l', 0], ['gatling_r', 0], ['body', 0], ['body', -1]]
    # Keep EDF6's fuel block and fuel weapon.  Calls supply the per-model armour,
    # gun and flight settings just as they do for every stock 506 helicopter.
    members['vehicle_setup'][0:2] = copy.deepcopy(mine['vehicle_setup'][0:2])
    source_setup = mine['vehicle_setup'][2]
    gun = WEAPONS['v_302heli_gatling01.sgo']
    missile = WEAPONS['v_302heli_missile01.sgo'][0]
    members['vehicle_setup'][3] = [
        [f'app:/weapon/{name}.sgo', *copy.deepcopy(source_setup[0][1:])] for name in gun]
    members['vehicle_setup'][3].append([f'app:/weapon/{missile}.sgo', *copy.deepcopy(source_setup[1][1:])])
    members['vehicle_setup'][3].append(copy.deepcopy(host['vehicle_setup'][3][-1]))
    for kind, hosts in _HOST.items():
        source_name = f'V_302HELI_{kind.upper()}.SGO'
        source_weapon = sgo.read(games.read4('WEAPON/' + source_name))[1]
        # The old gatling/laser muzzle is local to its dedicated left/right MDB
        # mount.  Missile/napalm locators already contain both body-local pods.
        muzzle = _mab(source_weapon['animation_model'][2])
        for host_name, own_name in zip(hosts, WEAPONS[source_name.lower()]):
            weapon = dsgo.parse(games.read6('WEAPON/' + host_name + '.SGO'))
            weapon_model = weapon.root.get('animation_model')
            weapon_entry = weapon_model.items[0].items
            weapon_bones = _bones(games.read6(_rel(weapon_entry[0])), weapon_entry[1])
            missing = {loc.node for _lst, loc in mab_legacy.mab_locators(muzzle)} - weapon_bones
            if missing:
                raise ValueError(f'302 weapon MAB names missing model bones: {sorted(missing)}')
            weapon_model.items[2] = dsgo.Blob(muzzle)
            files['WEAPON/' + own_name.upper() + '.SGO'] = dsgo.write(weapon)
    _check_members(members, bones, host, cas, games.read6(_rel(host['animation_model'][1])))
    files['OBJECT/' + root] = sgo.write_depth_first(host_version, members)
    return files
