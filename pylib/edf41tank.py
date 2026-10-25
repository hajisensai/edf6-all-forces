"""The real EDF4.1 V301 tank, including its promotional paint variants, on EDF6 CarBase.

VehicleTank301 was removed. The native Vehicle601_Tank keeps the single-gun tracked
behavior without Vehicle505_Tank's hard-coded antenna and mudguard bodies. V301
keeps its seven wheel pairs and real model/physics contracts. The source model,
textures, Havok collision and bindings stay together. EDF6's ragdoll loader migrates hkpPhysicsData to hknp itself
(src/legacymap.cpp); the map loader's separate limitation does not apply here.
"""
from __future__ import annotations

import copy

import cas_pose
import dsgo
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
RECOIL_BIND_BONE = 'edf6vc_cannon_slide_physics'
RUNTIME_CLASS = 'Vehicle601_Tank'
# The bones Vehicle601_Tank's pose (slot 45, 0x620830) turns by the seat-0 aim IN PLACE every frame: 0x661810 reads the
# bone's local (its record's +0x70), rotates it by the aim's absolute pitch (0x296F90) and writes it back (0x661BCC).
# The class's CAS (V601_TANK.CAS: a static rest-pose track for every bone) rewrites that local each frame first, so the
# pitch is applied once. A bone without a track keeps last frame's turned local and turns by the pitch again every frame:
# the barrel spins while the gun is laid off level, float error grows its matrix past 1e20 in about a second, and
# Umbra, handed that box (0x11B30C0 -> Object::setObjectToCellMatrix), culls the whole scene down to the sky (the user,
# 2026-10-25: 4.1 mission 1 turning into a grey map; docs/feedback-2026-10-25-greymap.md). Each needs its own rest track.
CLASS_POSED = frozenset({'cannon'})


def rel(ref: str) -> str:
    return ref.split(':/', 1)[1].replace('\\', '/').upper()


def model(data: bytes, name: str) -> mdb.Mdb:
    members = [f for f in mdb.rab_read(data).files if f.name.lower() == name.lower()]
    if len(members) != 1:
        raise ValueError(f'model archive does not hold exactly one {name}')
    return mdb.mdb_read(members[0].data)


def recoil_model_archive(data: bytes, name: str) -> bytes:
    """Add a meshless physics readback leaf with the barrel's exact rest frame.

    Every native ragdoll body needs an animation_from_ragdoll target even when
    CAS owns its visible bone. A sibling leaf receives that writeback while the
    real cannon_slide keeps animating and poses its body through the other stage.
    """
    archive = mdb.rab_read(data)
    members = [member for member in archive.files if member.name.lower() == name.lower()]
    if len(members) != 1:
        raise ValueError(f'model archive does not hold exactly one {name}')
    member = members[0]
    md = mdb.mdb_read(member.data)
    if md.bone_index(RECOIL_BIND_BONE) >= 0:
        raise ValueError('V301 reserved recoil binding bone already exists')
    source = copy.deepcopy(md.bones[mdb.bone_named(md, 'cannon_slide')])
    if source.parent < 0:
        raise ValueError('V301 recoil bone has no parent')
    parent = md.name_of(md.bones[source.parent].name)
    at = mdb.insert_bone(md, RECOIL_BIND_BONE, parent)
    md.bones[at].local = list(source.local)
    md.bones[at].inv_bind = list(source.inv_bind)
    raw = mdb.mdb_write(md)
    member.stored = mdb.cmpl_compress(raw) if member.stored[:4] == b'CMPL' else raw
    return mdb.rab_write(archive)


def _template(data: bytes) -> tuple[int, dict]:
    """Read the actual 601 DSGO template without turning its raw blobs into text.

    V301 uses SGO 0x102. This template has a fully named root, unnamed nested
    arrays and ordinary raw blobs; refuse unrepresentable named/typed content.
    """
    if data[:4] != b'DSGO':
        return sgo.read(data)
    root = dsgo.parse(data).root
    if len(root.names) != len(root.items):
        raise ValueError('V301 template root has unnamed fields')
    def value(node):
        if isinstance(node, dsgo.Node):
            if node.names:
                raise ValueError('V301 template has a named nested container')
            return [value(item) for item in node.items]
        if isinstance(node, dsgo.Blob):
            if node.kind != 2:
                raise ValueError('V301 template has a typed expression blob')
            return node.data
        return node
    return 0x102, {name: value(root.items[index]) for index, name in root.names.items()}


def check(members: dict, md: mdb.Mdb, physics: bytes, cas: bytes) -> None:
    """Refuse unresolved bones, Havok bodies, locators or animation ownership conflicts."""
    if members.get('xgs_scene_object_class') != RUNTIME_CLASS:
        raise ValueError('V301 requires the audited Vehicle601_Tank class contract')
    names = {md.name_of(b.name) for b in md.bones}
    proxies = {o['name']: o['motion']['motionState']['transform'] for o in hk2014.read(physics).objects
               if o.type.name == 'hkpRigidBody'}
    bodies = set(proxies)
    frames = ragdoll_fit.bone_frames(md)
    inner = sgo.read(members['ragdoll'][1])[1]
    # BindDependency initializes each body's +0x60 through this stage before
    # the first animation update. An absent row crashes at native 0x6E8284.
    missing = bodies - {row[0][0] for row in inner['animation_from_ragdoll']}
    if missing:
        raise ValueError(f'V301 unbound animation_from_ragdoll bodies: {sorted(missing)}')
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
    required = {members['car_base_body_name'], 'body', 'cannon_main', 'cannon'}
    for key in ('car_base_rigid_body', 'car_base_simulation_node', 'car_base_wheel', 'vehicle_weapon_setting'):
        required.update(row[0] for row in members[key])
    for row in members['car_base_constraint_data']:
        required.update(row[:2])
    required.update(members['car_base_disable_contact_lisner'])
    required.update(loc.node for _, loc in mab_legacy.mab_locators(members['animation_model'][2]))
    missing = required - names
    if missing:
        raise ValueError(f'V301 runtime bone contract: {sorted(missing)}')
    required_bodies = {row[0] for row in members['car_base_rigid_body']} | {'body', 'cannon_main', 'cannon'}
    missing = {'RagDollProxys.' + name for name in required_bodies} - bodies
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
    slide = md.bones[mdb.bone_named(md, 'cannon_slide')]
    sink = md.bones[mdb.bone_named(md, RECOIL_BIND_BONE)]
    if (sink.kind != 0 or sink.bounded != 0 or sink.parent != slide.parent or
            sink.local != slide.local or sink.inv_bind != slide.inv_bind or
            any(obj.bone == sink.index for obj in md.objects)):
        raise ValueError('V301 recoil binding bone must be a meshless sibling with the exact barrel bind frame')
    reverse = [row[0] for row in inner['animation_from_ragdoll'] if row[0][0] == 'RagDollProxys.cannon_slide']
    if reverse != [['RagDollProxys.cannon_slide', RECOIL_BIND_BONE]]:
        raise ValueError('V301 recoil readback must target only its hidden binding bone')
    forward = [row[0] for row in inner['ragdoll_from_animation'] if row[0][1] == 'RagDollProxys.cannon_slide']
    if forward != [['cannon_slide', 'RagDollProxys.cannon_slide']]:
        raise ValueError('V301 recoil body must still follow the animated barrel')
    pose = cas_pose.CasPose(cas)
    if {c.name for c in pose.clips} != {'default', 'fire'}:
        raise ValueError('V301 recoil CAS no longer has the audited two clips')
    tracks = {t.name for c in pose.clips for t in c.tracks}
    driven = {row[0][1] for row in inner['animation_from_ragdoll']}
    # Physics owns every other bound bone; the class's in-place pose (CLASS_POSED) needs its track as V601's has one.
    if (tracks & driven) - CLASS_POSED:
        raise ValueError('V301 recoil animation conflicts with physics ownership')
    if tracks != {'cannon_slide'} | CLASS_POSED:
        raise ValueError(f'V301 CAS must hold the recoil and the rest pose of {sorted(CLASS_POSED)} '
                         f'(the 601 pose turns them in place every frame), holds {sorted(tracks)}')
    for clip in pose.clips:
        if {t.name for t in clip.tracks} != tracks:
            raise ValueError(f'V301 CAS clip {clip.name} lacks a track another clip has')
        for track in clip.tracks:
            local = md.bones[mdb.bone_named(md, track.name)].local
            start = pose.translation(track.translation)
            if max(abs(a - b) for a, b in zip(start, local[12:15])) > 1e-4:
                raise ValueError(f'V301 CAS track {track.name} is not rebased to its own bone')
            if track.name in CLASS_POSED:
                # A static rest track: the rotation (the bind's rows) and the translation of its first key, every key.
                rows = ragdoll_fit.axes(cas_pose.rotation(pose, track.rotation))
                if max(abs(rows[i][j] - local[4 * i + j]) for i in range(3) for j in range(3)) > 1e-4:
                    raise ValueError(f'V301 CAS track {track.name} is not the rest rotation of its bone')
                for frame in range(1, cas_pose.clip_info(pose, clip)[2]):
                    if max(abs(a - b) for a, b in zip(pose.translation(track.translation, frame), start)) > 1e-6:
                        raise ValueError(f'V301 CAS track {track.name} moves: the 601 pose owns its motion')


def build(games, root: str) -> dict[str, bytes]:
    if root not in ROOTS:
        raise ValueError(f'not a V301 tank: {root}')
    obj = 'OBJECT/' + root
    original = sgo.read(legacy_sgo.convert(games.read4(obj)))[1]
    template = 'OBJECT/V601_TANK' + ('_AI' if root.endswith('_AI.SGO') else '') + '.SGO'
    version, members = _template(games.read6(template))
    if members.get('xgs_scene_object_class') != RUNTIME_CLASS:
        raise ValueError('V301 native 601 template has another runtime class')
    members = copy.deepcopy(members)
    am = original['animation_model']
    entry = am[0] if isinstance(am[0][0], str) else am[0][0]
    archive = rel(entry[0])
    # The absent *_light.rab is a virtual secondary model, not part of the source closure.
    out = {archive: recoil_model_archive(legacy_assets.convert(archive, games.read4(archive)), entry[1])}
    md = model(out[archive], entry[1])
    donor_am = members['animation_model']
    donor_md = model(games.read6(rel(donor_am[0][0])), donor_am[0][1])
    cas_data = games.read6(rel(donor_am[1]))
    pose = cas_pose.CasPose(cas_data)
    # The recoil and the rest pose of the bones the class turns in place (CLASS_POSED), both rebased onto V301's own
    # skeleton (retarget). Every other donor track encodes V601's absolute rest pose and would move the original
    # wheels / turret back onto V601's skeleton.
    free = {t.name for c in pose.clips for t in c.tracks} - {'cannon_slide'} - CLASS_POSED
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
    # Preserve the original row/offset so native construction binds this body.
    # Only its destination changes: physics writes the hidden same-frame leaf,
    # while CAS owns cannon_slide and ragdoll_from_animation follows its recoil.
    recoil_rows = [row for row in binding['animation_from_ragdoll'] if row[0][1] == 'cannon_slide']
    if len(recoil_rows) != 1 or recoil_rows[0][0][0] != 'RagDollProxys.cannon_slide':
        raise ValueError('V301 source recoil binding is not its canonical single row')
    recoil_rows[0][0][1] = RECOIL_BIND_BONE
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
    # The AI file has an authored mission loadout too. Inheriting 601's one
    # would silently replace the original cannon with the donor's weapon/rate.
    for field in ('vehicle_setup', 'mission_setup'):
        if field not in original:
            members.pop(field, None)
            continue
        setup = copy.deepcopy(original[field])
        for row in setup[2]:
            mapped = edf41calls.WEAPONS.get(edf41calls.leaf(row[0]))
            if not mapped or len(mapped) != 1:
                raise ValueError('V301 weapon has no single-barrel EDF6 counterpart')
            row[0] = 'app:/weapon/' + mapped[0] + '.sgo'
        members[field] = setup
    check(members, md, out[shkt], cas_data)
    out[obj] = sgo.write_depth_first(version, members)
    return out
