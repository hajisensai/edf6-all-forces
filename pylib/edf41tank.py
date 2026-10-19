"""The real EDF4.1 V301 tank, including its promotional paint variants, on EDF6 CarBase.

VehicleTank301 was removed. Reusing V505's behavior requires translating its physical
and rendering contracts, not replacing animation_model alone: V301 has seven wheel
pairs and no V505 antenna or mudguard bones. Keep the source model, textures, Havok
collision and bindings. EDF6's ragdoll loader migrates hkpPhysicsData to hknp itself
(src/legacymap.cpp); the map loader's separate limitation does not apply here.
"""
from __future__ import annotations

import copy

import cas_pose
import edf41calls
import hk2014
import legacy_assets
import legacy_sgo
import mab_legacy
import mdb
import ragdoll_fit
import sgo

ROOTS = tuple('VEHICLE301_TANK' + suffix + '.SGO' for suffix in
              ('', '_AI', '_BULLETGIRLS', '_DCZERO', '_DC55', '_EDF2PV2', '_NATSUIRO'))


def rel(ref: str) -> str:
    return ref.split(':/', 1)[1].replace('\\', '/').upper()


def model(data: bytes, name: str) -> mdb.Mdb:
    members = [f for f in mdb.rab_read(data).files if f.name.lower() == name.lower()]
    if len(members) != 1:
        raise ValueError(f'model archive does not hold exactly one {name}')
    return mdb.mdb_read(members[0].data)


def check(members: dict, md: mdb.Mdb, physics: bytes, cas: bytes) -> None:
    """Refuse unresolved bones, Havok bodies, locators or animation ownership conflicts."""
    names = {md.name_of(b.name) for b in md.bones}
    proxies = {o['name']: o['motion']['motionState']['transform'] for o in hk2014.read(physics).objects
               if o.type.name == 'hkpRigidBody'}
    bodies = set(proxies)
    frames = ragdoll_fit.bone_frames(md)
    inner = sgo.read(members['ragdoll'][1])[1]
    for key, reverse in (('ragdoll_from_animation', False), ('animation_from_ragdoll', True)):
        for row in inner[key]:
            bone, proxy = row[0][::-1] if reverse else row[0]
            if bone not in names or proxy not in bodies:
                raise ValueError(f'{key}: unresolved {bone} / {proxy}')
        for link in ragdoll_fit.links(inner, key):
            p = proxies[link.src if reverse else link.dst]
            physical = (tuple(p[12:15]), [tuple(p[4 * i:4 * i + 3]) for i in range(3)])
            start, expected = (physical, frames[link.dst]) if reverse else (frames[link.src], physical)
            origin, basis = start
            position = tuple(origin[j] + sum(link.off.t[i] * basis[i][j] for i in range(3)) for j in range(3))
            rotation = [tuple(sum(axis[i] * basis[i][j] for i in range(3)) for j in range(3))
                        for axis in ragdoll_fit.axes(link.off.q)]
            delta = max(abs(a - b) for a, b in zip(position, expected[0]))
            delta = max(delta, *(abs(rotation[i][j] - expected[1][i][j]) for i in range(3) for j in range(3)))
            if delta > 1e-3:
                raise ValueError(f'{key}: {link.src} / {link.dst} physics bind frames disagree ({delta:.6f})')
    required = {members['car_base_body_name']}
    for key in ('car_base_rigid_body', 'car_base_simulation_node', 'car_base_wheel', 'vehicle_weapon_setting'):
        required.update(row[0] for row in members[key])
    for row in members['car_base_constraint_data']:
        required.update(row[:2])
    required.update(members['car_base_disable_contact_lisner'])
    required.update(loc.node for _, loc in mab_legacy.mab_locators(members['animation_model'][2]))
    missing = required - names
    if missing:
        raise ValueError(f'V301 runtime bone contract: {sorted(missing)}')
    missing = {'RagDollProxys.' + row[0] for row in members['car_base_rigid_body']} - bodies
    if missing:
        raise ValueError(f'V301 runtime body contract: {sorted(missing)}')
    wheels = members['car_base_wheel']
    for row in members['car_base_draw_wheel_info']:
        index, bone = row[0]
        if index >= len(wheels) or bone not in names or row[1][0] not in names:
            raise ValueError('V301 wheel drawing contract is not its wheel array')
        if index >= 0 and wheels[index][0] != bone:
            raise ValueError('V301 wheel drawing index points at another wheel')
    materials = {md.name_of(mt.name) for mt in md.materials}
    if any(row[0] not in materials for row in members['tank_caterpillar_animation']):
        raise ValueError('V301 caterpillar animation names another model material')
    pose = cas_pose.CasPose(cas)
    if {c.name for c in pose.clips} != {'default', 'fire'}:
        raise ValueError('V301 recoil CAS no longer has the audited two clips')
    tracks = {t.name for c in pose.clips for t in c.tracks}
    driven = {row[0][1] for row in inner['animation_from_ragdoll']}
    if tracks != {'cannon_slide'} or tracks & driven:
        raise ValueError('V301 recoil animation conflicts with physics ownership')
    target = md.bones[mdb.bone_named(md, 'cannon_slide')].local[12:15]
    for clip in pose.clips:
        start = pose.translation(clip.tracks[0].translation)
        if max(abs(a - b) for a, b in zip(start, target)) > 1e-4:
            raise ValueError('V301 recoil CAS is not rebased to its own barrel')


def build(games, root: str) -> dict[str, bytes]:
    if root not in ROOTS:
        raise ValueError(f'not a V301 tank: {root}')
    obj = 'OBJECT/' + root
    original = sgo.read(legacy_sgo.convert(games.read4(obj)))[1]
    template = 'OBJECT/V505_TANK' + ('_AI' if root.endswith('_AI.SGO') else '') + '.SGO'
    version, members = sgo.read(games.read6(template))
    members = copy.deepcopy(members)
    am = original['animation_model']
    entry = am[0] if isinstance(am[0][0], str) else am[0][0]
    archive = rel(entry[0])
    # The absent *_light.rab is a virtual secondary model, not part of the source closure.
    out = {archive: legacy_assets.convert(archive, games.read4(archive))}
    md = model(out[archive], entry[1])
    donor_am = members['animation_model']
    donor_md = model(games.read6(rel(donor_am[0][0])), donor_am[0][1])
    cas_data = games.read6(rel(donor_am[1]))
    pose = cas_pose.CasPose(cas_data)
    # Only the recoil is portable. Every other donor track encodes V505's absolute
    # rest pose and would move the original wheels/turret back onto V505's skeleton.
    free = {t.name for c in pose.clips for t in c.tracks} - {'cannon_slide'}
    cas_data = cas_pose.retarget(cas_data, donor_md, md, {'default', 'fire'}, free)
    cas_rel = 'OBJECT/EDF41_V301_RECOIL.CAS'
    out[cas_rel] = cas_data
    mab = am[2]
    own_names = {loc.name for _, loc in mab_legacy.mab_locators(mab)}
    # AI overlap/dust locators were introduced after 4.1; the four original seat,
    # muzzle and sight locators keep the original tank's positions.
    added = [loc for _, loc in mab_legacy.mab_locators(donor_am[2]) if loc.name not in own_names]
    mab = mab_legacy.mab_add_locators(mab, added)
    members['animation_model'] = [entry, 'app:/' + cas_rel.lower(), mab]
    members['ragdoll'] = copy.deepcopy(original['ragdoll'])
    shkt = rel(members['ragdoll'][0])
    out[shkt] = games.read4(shkt)
    bind_version, binding = sgo.read(members['ragdoll'][1])
    # A recoil track owns the barrel's slide; its collision body follows that track.
    # Never let animation_from_ragdoll overwrite the recoil in the next update.
    binding['animation_from_ragdoll'] = [r for r in binding['animation_from_ragdoll'] if r[0][1] != 'cannon_slide']
    members['ragdoll'][1] = sgo.write_depth_first(bind_version, binding)
    bodies = {o.get('name', '').removeprefix('RagDollProxys.') for o in hk2014.read(out[shkt]).objects
              if o.type.name == 'hkpRigidBody'}
    members['car_base_rigid_body'] = [r for r in members['car_base_rigid_body'] if r[0] in bodies]
    configured = {r[0] for r in members['car_base_rigid_body']}
    members['car_base_rigid_body'] += [[n, -1.0, -1.0, -1.0, -1.0, [], []] for n in sorted(bodies - configured)]
    members['car_base_constraint_data'] = [r for r in members['car_base_constraint_data'] if set(r[:2]) <= bodies]
    members['car_base_disable_contact_lisner'] = [n for n in members['car_base_disable_contact_lisner'] if n in bodies]
    wheel_names = [row[0] for row in original['vehicle_tire']]
    wheel = members['car_base_wheel'][0]
    members['car_base_wheel'] = [[n, *copy.deepcopy(wheel[1:])] for n in wheel_names]
    members['car_base_simulation_node'] = [[n, 0] for n in wheel_names]
    members['car_base_cast_mode'] = [1] * len(wheel_names)
    members['car_base_draw_wheel_info'] = [[[i, n], [n.replace('tire_move', 'catapi'), -1, -1, 0.0, 0]]
                                           for i, n in enumerate(wheel_names)]
    members['car_base_wheel_effect'] = [[0], [1], [len(wheel_names) - 2], [len(wheel_names) - 1]]
    materials = [md.name_of(mt.name) for mt in md.materials if 'Catapillar' in mt.shader]
    if len(materials) != 2:
        raise ValueError('V301 source must have left and right caterpillar materials')
    members['tank_caterpillar_animation'] = [[n, 'scroll_texture'] for n in materials]
    basic = original['vehicle_basic']
    members['car_base_all_rigid_body'] = [*copy.deepcopy(basic[1][:3]), copy.deepcopy(basic[2]), copy.deepcopy(basic[3])]
    members['car_base_submerge_height'] = original['submerge_height']
    members['game_object_ai_route_update_distance'] = original['game_object_ai_route_update_distance']
    for key in ('game_object_durability', 'game_object_camera_setting', 'vehicle_damage_effect', 'vehicle_weapon_setting'):
        members[key] = copy.deepcopy(original[key])
    # Keep EDF6's driver animation/row layout, but the source MAB supplies the seat.
    setup = copy.deepcopy(original['vehicle_setup'])
    for row in setup[2]:
        mapped = edf41calls.WEAPONS.get(edf41calls.leaf(row[0]))
        if not mapped or len(mapped) != 1:
            raise ValueError('V301 weapon has no single-barrel EDF6 counterpart')
        row[0] = 'app:/weapon/' + mapped[0] + '.sgo'
    members['vehicle_setup'] = setup
    check(members, md, out[shkt], cas_data)
    out[obj] = sgo.write_depth_first(version, members)
    return out
