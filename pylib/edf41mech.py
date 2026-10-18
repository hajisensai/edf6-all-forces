"""EDF4.1's real 406 Begaruta, using EDF6's retained VehicleBegaruta class.

The 504 remake has another skeleton and eight weapon mounts; 406 has six. Keep
406's model, animation, MAB locators, Havok bodies and weapon mount names together.
EDF.dll 0x678CCB46 retains the old factory (634D70 -> 6355C0), old boost parser
(636DCE -> 640340), and old destruction effects (636E3B). Its new foot lookup
(635F29..636282) requires begaruta_foot_node, absent in 4.1. The pilot uses EDF6's
504 driver animation, while the three source seat/sight locators stay on 406.

Weapon archives with names shared by EDF6 are privately relocated, so their own
MAB muzzles never attach to an EDF6 remake model. No game files are written here.
"""
from __future__ import annotations

import copy
import struct

import cas_legacy
import cas_pose
import dsgo
import edf5port
import hk2014
import legacy_assets
import legacy_sgo
import mab_legacy
import mdb
import ragdoll_fit
import sgo

ROOTS = tuple('VEHICLE406_BEGARUTA' + suffix + '.SGO' for suffix in ('', '_RED', '_GOLD', '_GREEN'))
WEAPONS = tuple('WEAPON/V_406BEGARUTA_' + suffix + '.SGO' for suffix in (
    'CANNON01', 'CANNON02SOLID', 'FLAMETHROWER01', 'GATLING01', 'GATLING02',
    'LONGCANNON015_L', 'LONGCANNON015_R', 'LONGCANNON01_L', 'LONGCANNON01_R',
    'LONGCANNON02_L', 'LONGCANNON02_R', 'LONGSPREADCANNON01_L', 'LONGSPREADCANNON01_R',
    'MISSILE01_L', 'MISSILE01_R', 'ROCKET01'))
PRIVATE = 'WEAPON/EDF41MECH/'
# The three-element 4.1 name field is removed by weapon41; emit current name.*
# fields so the source guns also retain readable names on EDF6's vehicle HUD.
_LABELS = {
    'Rocket Cannon': ('火箭炮', '火箭砲'),
    'Heavy Revolver Cannon': ('重型转轮机炮', '重型轉輪機砲'),
    'Combat Burner': ('战斗喷火器', '戰鬥噴火器'),
    'Revolver Cannon': ('转轮机炮', '轉輪機砲'),
    'New Revolver Cannon': ('新型转轮机炮', '新型轉輪機砲'),
    'Shoulder Howitzer S': ('肩部榴弹炮 S', '肩部榴彈砲 S'),
    'Shoulder Howitzer': ('肩部榴弹炮', '肩部榴彈砲'),
    'Dispersal Howitzer': ('扩散榴弹炮', '擴散榴彈砲'),
    'Canister Shot': ('散弹炮', '散彈砲'),
    'Missile Pod': ('导弹巢', '導彈莢艙'),
    'Revolver Rocket Cannon': ('转轮火箭炮', '轉輪火箭砲'),
}


def rel(ref: str) -> str:
    return ref.split(':/', 1)[1].replace('\\', '/').upper()


def model(data: bytes, name: str) -> mdb.Mdb:
    found = [f for f in mdb.rab_read(data).files if f.name.lower() == name.lower()]
    if len(found) != 1:
        raise ValueError(f'406 model archive does not hold exactly one {name}')
    return mdb.mdb_read(found[0].data)


def vehicle(members: dict, driver: list) -> dict:
    """Upgrade only the runtime fields introduced after 4.1; keep source geometry."""
    out = copy.deepcopy(members)
    if out.get('xgs_scene_object_class') != 'VehicleBegaruta':
        raise ValueError('406 must use the retained VehicleBegaruta class')
    if len(out.get('vehicle_weapon_setting', [])) != 6 or len(out['vehicle_setup'][4]) != 6:
        raise ValueError('406 must retain its six weapon mounts')
    seats = out.get('vehicle_riding_position')
    if not isinstance(seats, list) or len(seats) != 1 or len(driver) != 7:
        raise ValueError('406 needs one seat and EDF6 driver metadata')
    # The old pilot tag is not an EDF6 soldier animation. Preserve the source
    # mount, entry and camera locator names; only current soldier metadata changes.
    seats[0] = [*seats[0][:3], *copy.deepcopy(driver[3:])]
    out['begaruta_foot_node'] = ['ashi_l', 'ashi_r']
    return out


def _texts(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for child in value:
            yield from _texts(child)


def check(members: dict, md: mdb.Mdb, physics: bytes, cas: bytes) -> None:
    """Refuse mismatched runtime bones, physical bindings or animation events."""
    if members.get('xgs_scene_object_class') != 'VehicleBegaruta':
        raise ValueError('406 must keep VehicleBegaruta, not the 504 remake')
    names = {md.name_of(b.name) for b in md.bones}
    required = set(members['begaruta_foot_node'])
    required.update(row[0] for row in members['vehicle_weapon_setting'])
    required.update(members['begaruta_boost'][0])
    required.update(n for row in members['begaruta_pilot_class'] for n in row[2][1:])
    required.update(_texts(members['vehicle_dead_effect']))
    locators = mab_legacy.mab_locator_nodes(members['animation_model'][2])
    required.update(node for _, node in locators)
    if required - names:
        raise ValueError(f'406 runtime bone contract: {sorted(required - names)}')
    if len(members['vehicle_weapon_setting']) != 6 or len(members['vehicle_setup'][4]) != 6:
        raise ValueError('406 weapon mount count changed')
    loc_names = {name for name, _ in locators}
    seat = members['vehicle_riding_position'][0]
    if len(seat) != 7 or not {seat[0], seat[1], seat[2][0]} <= loc_names:
        raise ValueError('406 seat metadata does not resolve its source locators')
    bodies = {o['name']: o['motion']['motionState']['transform'] for o in hk2014.read(physics).objects
              if o.type.name == 'hkpRigidBody'}
    frames = ragdoll_fit.bone_frames(md)
    binding = sgo.read(members['ragdoll'][1])[1]
    for key, reverse in (('ragdoll_from_animation', False), ('animation_from_ragdoll', True)):
        for link in ragdoll_fit.links(binding, key):
            bone, proxy = (link.dst, link.src) if reverse else (link.src, link.dst)
            if bone not in names or proxy not in bodies:
                raise ValueError(f'406 {key}: unresolved {bone} / {proxy}')
            # The three forward aim targets are authored 300 m ahead in the
            # animation rig, then move to the head when the corpse ragdolls.
            # They are transform-only sight helpers, not collision bind frames.
            if reverse and bone in {'aim_target', 'aim_target_shoulder_l', 'aim_target_shoulder_r'}:
                if md.bones[md.bone_index(bone)].kind != 0 or proxy != 'RagDollProxys.head':
                    raise ValueError('406 ragdoll aim target is not a head sight helper')
                continue
            physical = bodies[proxy]
            pf = tuple(physical[12:15]), [tuple(physical[4 * i:4 * i + 3]) for i in range(3)]
            start, expected = (pf, frames[bone]) if reverse else (frames[bone], pf)
            origin, basis = start
            position = tuple(origin[j] + sum(link.off.t[i] * basis[i][j] for i in range(3)) for j in range(3))
            rotation = [tuple(sum(axis[i] * basis[i][j] for i in range(3)) for j in range(3))
                        for axis in ragdoll_fit.axes(link.off.q)]
            delta = max(abs(a - b) for a, b in zip(position, expected[0]))
            delta = max(delta, *(abs(rotation[i][j] - expected[1][i][j]) for i in range(3) for j in range(3)))
            if delta > 1e-3:
                raise ValueError(f'406 {key}: physics bind frames disagree ({delta:.6f})')
    lay = cas_legacy.cas_layout(cas)
    groups = {cas_legacy._text(cas, at + struct.unpack_from('<i', cas, at)[0]) for at, _ in lay.tree.anmgroups}
    if not {'main', 'AddAim'} <= groups:
        raise ValueError('406 CAS lacks its main / AddAim animation players')
    clips = {c.name for c in cas_pose.CasPose(cas).clips}
    if set(mab_legacy.mab_events(members['animation_model'][2])) - clips:
        raise ValueError('406 MAB event tracks do not resolve to its CAS clips')



def _mab_presets(blob: bytes, changes: dict[str, str]) -> bytes:
    """Relocate changed embedded event SGOs; preserve every locator and event time."""
    layout = mab_legacy.mab_layout(blob)
    if layout.flag != mab_legacy.RELATIVE:
        raise ValueError('406 sound upgrade requires a converted MAB')

    def rename(value):
        if isinstance(value, str):
            return changes.get(value, value)
        return [rename(x) for x in value] if isinstance(value, list) else value

    added, moved = bytearray(), {}
    for target in sorted({target for _, _, target in layout.offsets}):
        if blob[target:target + 4] != b'SGO\0':
            continue
        version, original = sgo.read(blob[target:])
        members = {key: rename(value) for key, value in original.items()}
        if members == original:
            continue
        added += bytes((-len(added)) % 16)
        moved[target] = layout.strings_at + len(added)
        added += sgo.write_depth_first(version, members)
    if not added:
        return blob
    added += bytes((-len(added)) % 16)
    out = bytearray(blob[:layout.strings_at]) + added + blob[layout.strings_at:]
    struct.pack_into('<I', out, 0x20, layout.strings_at + len(added))
    for at, base, target in layout.offsets:
        new = moved.get(target, target + len(added) if target >= layout.strings_at else target)
        struct.pack_into('<i', out, at, new - base)
    result = bytes(out)
    mab_legacy.mab_layout(result)
    return result


def sounds(games, members: dict) -> None:
    """Use registered EDF6 presets for the same cues, including MAB step events."""
    source = sgo.load(data=games.read4('SOUND/SEPRESET.SGO'))
    stock = sgo.load(data=games.read6('SOUND/SEPRESET.SGO'))
    prefix = '\u30d9\u30ac\u30eb\u30bf'
    changes = {}
    for name, preset in source.items():
        if name in stock or not name.startswith(prefix):
            continue
        target = prefix + '504' + name[len(prefix):]
        if target in stock and preset[0] == stock[target][0]:
            changes[name] = target

    def rename(value):
        if isinstance(value, str):
            return changes.get(value, value)
        return [rename(x) for x in value] if isinstance(value, list) else value

    for key in ('begaruta_se_table', 'begaruta_aim_se_table', 'ragdoll_contact'):
        members[key] = rename(members[key])
    am = members['animation_model']
    am[2] = _mab_presets(am[2], changes)
    names = {name for key in ('begaruta_se_table', 'begaruta_aim_se_table') for name in _texts(members[key])}
    names.add(members['ragdoll_contact'][0])
    for events in mab_legacy.mab_event_params(am[2]).values():
        names.update(params['name'] for key, _time, params in events if key == 'se')
    if names - stock.keys():
        raise ValueError(f'406 sound presets are unregistered: {sorted(names - stock.keys())}')


def _weapon_assets(games) -> dict[str, bytes]:
    out = {}
    for path in WEAPONS:
        _, original = sgo.read(games.read4(path))
        original = copy.deepcopy(original)
        am = original['animation_model']
        # Use the complete original weapon models and animations, including the
        # missile launchers' moving lids. EDF6 has remade files at the old paths.
        refs = [am[0][0], am[1]]
        for ref in refs:
            if not isinstance(ref, str) or not ref.lower().startswith('app:/'):
                continue
            source = rel(ref)
            target = PRIVATE + source.rsplit('/', 1)[-1]
            data = legacy_assets.convert(source, games.read4(source))
            if target in out and out[target] != data:
                raise ValueError(f'406 weapon archive clash: {target}')
            out[target] = data
            if am[0][0] == ref:
                am[0][0] = 'app:/' + target.lower()
            if am[1] == ref:
                am[1] = 'app:/' + target.lower()
        md = model(out[rel(am[0][0])], am[0][1])
        names = {md.name_of(b.name) for b in md.bones}
        missing = {node for _, node in mab_legacy.mab_locator_nodes(am[2])} - names
        if missing:
            raise ValueError(f'{path}: unresolved source weapon muzzle bones {sorted(missing)}')
        ja, en, _ = original['name']
        label, side = (en.rsplit(' (', 1)[0], en.rsplit(' (', 1)[1][:-1]) if en.endswith(')') else (en, '')
        sc, cn = _LABELS[label]
        suffix = {'Left': '（左）', 'Right': '（右）', '': ''}[side]
        converted = edf5port.weapon41(original, {'ja': ja, 'en': en, 'sc': sc + suffix,
                                                'cn': cn + suffix, 'kr': en})
        out[path] = dsgo.write(converted)
    return out


def build(games, root: str) -> dict[str, bytes]:
    if root not in ROOTS:
        raise ValueError(f'not a 406 Begaruta: {root}')
    path = 'OBJECT/' + root
    version, members = sgo.read(legacy_sgo.convert(games.read4(path)))
    driver = sgo.load(data=games.read6('OBJECT/V504_BEGARUTA.SGO'))['vehicle_riding_position'][0]
    members = vehicle(members, driver)
    sounds(games, members)
    am = members['animation_model']
    # Source *_light.rab files are absent from 4.1 too; this is the optional
    # secondary light model, not a second visible body.
    archive = rel(am[0][0][0])
    animation, physics = rel(am[1]), rel(members['ragdoll'][0])
    out = {archive: legacy_assets.convert(archive, games.read4(archive)),
           animation: legacy_assets.convert(animation, games.read4(animation)),
           physics: games.read4(physics)}
    check(members, model(out[archive], am[0][0][1]), out[physics], out[animation])
    out[path] = sgo.write_depth_first(version, members)
    out.update(_weapon_assets(games))
    return out
