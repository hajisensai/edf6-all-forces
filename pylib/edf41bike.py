"""EDF4.1's real V408 sidecar: three CarBase wheels and a separate gunner.

The old VehicleBike factory is gone.  V512's Vehicle_Car is a real native empty
aim chassis; the paired src/edf41bike.cpp adapts the gunner's original single
two-axis gun.  It is intentionally not the existing standing/free-hand sidecar.
"""
from __future__ import annotations

import copy
import math
import struct

import cas_graft
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

ROOTS = ('VEHICLE408_MOTORBIKE.SGO',)
MARKER = 'edf41_motorbike'
WEAPONS = {'v_408motorbike_gun.sgo': ['edf41_v_408motorbike_gun']}
CAR_TEMPLATE = 'OBJECT/V512_KEITRUCK.SGO'
# vehicle_setup[1] of the Car chassis, as the object carries it (CAR_TEMPLATE's own; build checks it) and as every
# call must hand it to the vehicle. Vehicle_Car's setup slot 46 (EDF+0x65A910) reads exactly seven numbers from it:
# [0] (or [0][0..4] when a list) scales car_base_brake_and_friction (0x66EEC0 / 0x66EF20), [1] / [2] scale the engine
# gear table (0x66E1A0), [3] / [4] are the steering rates (0x66B730 -> +0x1B44 / +0x1B40), [5] / [6] the traction
# curve (0x66EE60 -> +0x17F4..+0x17FC); a missing index reads as 0. 4.1's VehicleBike block is another class's
# (four numbers, [2.0, 0.9, 30, 100] in the first request): given to the Car the gear table was scaled by 0.9 / 30
# instead of 30 / 0.0075, the steering rates became 100 / 0 and the traction factors 0 (2026-10-19 static review).
CAR_DRIVE = [0.8, 30.0, 0.0075, 0.7, 0.075, 0.3, 0.6]
POSE_STATE = 'vehicle_striker_rear'  # all four native classes have this visible seated pose
RIDER_CLASSES = (('ARMYSOLDIER', 'EDF6ARMYSOLDIER', 'P505_RANGER'),
                 ('ENGINEER', 'EDF6ENGINEER', 'P508_AIRRADER'),
                 ('PALEWING', 'EDF6_WINGDIVER', 'P506_WINGDIVER'),
                 ('HEAVYARMOR', 'EDF6HEAVYARMOR', 'P507_FENCER'))
WHEELS = ('front_tire', 'rear_tire', 'side_tire')
PROXIES = frozenset(('body', 'handle', 'front_sus', 'front_tire', 'rear_sus', 'rear_tire', 'side_tire', 'gun'))


def rel(ref: str) -> str:
    if not isinstance(ref, str) or not ref.lower().startswith('app:/'):
        raise ValueError(f'408 resource is not an app path: {ref!r}')
    return ref[5:].replace('\\', '/').upper()


def model(data: bytes, name: str) -> mdb.Mdb:
    entries = [f for f in mdb.rab_read(data).files if f.name.lower() == name.lower()]
    if len(entries) != 1:
        raise ValueError(f'408 archive has {len(entries)} copies of {name}')
    return mdb.mdb_read(entries[0].data)


def _state_root(cas: bytes, state: str) -> tuple[float, float, float]:
    clips = cas_graft.state_clips(cas).get('main', {}).get(state, [])
    if len(clips) != 1:
        raise ValueError(f'408 rider state {state} does not resolve to one clip')
    pose = cas_pose.CasPose(cas)
    clip = next(c for c in pose.clips if c.name == clips[0])
    track = next((t for t in clip.tracks if t.name == 'koshi'), None)
    if track is None or track.translation < 0:
        raise ValueError(f'408 rider clip {clips[0]} has no pelvis translation')
    _duration, _step, frames = cas_pose.clip_info(pose, clip)
    keys = [pose.translation(track.translation, k) for k in range(max(frames, 1))]
    if max(abs(a - b) for p in keys for a, b in zip(p, keys[0])) > 1e-4:
        raise ValueError(f'408 seated rider {state} has moving root keys')
    return keys[0]


def pose_fit(games) -> tuple[list[list[float]], list[list]]:
    """Fit shared visible seat locators to all four original pelvis positions.

    A 503 driver pose is absent for wing diver/fencer; its automatic rear-seat
    fallback would put their pelvis 0.6 m too low.  Both seats therefore use the
    native rear-seat pose.  A least-squares shared translation keeps every class
    within 8 cm of its own 4.1 pelvis, without replacing global soldier CAS files.
    """
    cached = getattr(games, '_edf41bike_pose_fit', None)
    if cached is not None:
        return copy.deepcopy(cached)
    rows = []
    for source, native, _model in RIDER_CLASSES:
        old = cas_legacy.cas_from_legacy(games.read4(f'OBJECT/{source}.CAS'))
        new = games.read6(f'OBJECT/{native}.CAS')
        driver, gunner = (_state_root(old, s) for s in ('vehicle_bike_driver', 'vehicle_bike_gunner'))
        seated = _state_root(new, POSE_STATE)
        rows.append([source, list(driver), list(gunner), list(seated)])
    offsets = [[sum(row[seat + 1][c] - row[3][c] for row in rows) / len(rows) for c in range(3)]
               for seat in range(2)]
    for row in rows:
        errors = [math.dist([row[3][c] + offsets[seat][c] for c in range(3)], row[seat + 1]) for seat in range(2)]
        if max(errors) > 0.08:
            raise ValueError(f'408 {row[0]} shared seat pose differs by {max(errors):.3f} m')
        row.append(errors)
    result = offsets, rows
    games._edf41bike_pose_fit = copy.deepcopy(result)
    return result


def seat_mab(block: bytes, offsets: list[list[float]]) -> bytes:
    """Append two independent seat parameter SGOs and rebase outer MAB offsets.

    The 4.1 MAB's old seat parameters name removed rider states.  Do not overwrite
    longer UTF-16 names in place or change parameters shared by another locator.
    """
    layout = mab_legacy.mab_layout(block)
    if layout.flag != mab_legacy.RELATIVE:
        raise ValueError('408 seat MAB must first be converted to EDF6 offsets')
    _layout, records = mab_legacy._records(block)
    records_by_name = {}
    for group, at in records:
        if group == 0:
            name_at = at + struct.unpack_from('<i', block, at)[0]
            records_by_name[mab_legacy._text_at(block, name_at)] = at
    wanted = ['座席１', '座席２']
    if any(n not in records_by_name for n in wanted):
        raise ValueError('408 MAB lacks its two real seat locators')
    extra = bytearray()
    params_at = []
    nvec = struct.unpack_from('<H', block, 0x10)[0]
    vecs = struct.unpack_from('<I', block, 0x1C)[0]
    vec_end = vecs + nvec * 16
    grow_vecs = 32
    for _name in wanted:
        params_at.append(layout.strings_at + grow_vecs + len(extra))
        extra += sgo.write_depth_first(0x102, {'Animation': POSE_STATE, 'DispCharacter': 1})
    grow = len(extra)
    new_vecs = b''.join(struct.pack('<4f', *p, 1.0) for p in offsets)
    out = bytearray(block[:vec_end]) + new_vecs + block[vec_end:layout.strings_at] + extra + block[layout.strings_at:]
    struct.pack_into('<H', out, 0x10, nvec + 2)
    struct.pack_into('<I', out, 0x20, layout.strings_at + grow_vecs + grow)
    def moved(at):
        return at + (grow_vecs if at >= vec_end else 0) + (grow if at >= layout.strings_at else 0)
    for at, base, target in layout.offsets:
        struct.pack_into('<i', out, moved(at), moved(target) - moved(base))
    for i, name in enumerate(wanted):
        at = records_by_name[name]
        struct.pack_into('<i', out, at + 0x1C, params_at[i] - at)
        vec = at + struct.unpack_from('<i', block, at + 0xC)[0]
        if max(abs(v) for v in struct.unpack_from('<3f', block, vec)) > 1e-6:
            raise ValueError('408 seat locator is not the audited source origin')
        # The source origin vec4 is also shared by camera rotations. Each moved
        # seat needs its own vector; editing that shared value tilts the camera.
        struct.pack_into('<i', out, at + 0xC, vec_end + i * 16 - at)
    mab_legacy.mab_layout(bytes(out))
    return bytes(out)


def native_gun_rows(rows: object) -> list:
    """Adapt the old AimRecoil holder to Vehicle_Car's BodyRecoil payload.

    Car's fired callback requests variant 0 and dereferences its result. A named
    AimRecoil is variant 1 and returns null there. Hull impulse is zero for this
    small mounted gun; its weapon's real FireRecoil remains unchanged.
    """
    if (not isinstance(rows, list) or len(rows) != 1 or not isinstance(rows[0], list)
            or len(rows[0]) != 3 or not isinstance(rows[0][0], str)
            or not isinstance(rows[0][2], list) or len(rows[0][2]) != 3):
        raise ValueError('408 requires its one three-column gun holder')
    out = copy.deepcopy(rows)
    out[0][1] = [0.0, 0.0]
    return out


def joints(physics: bytes) -> list[tuple[str, str]]:
    """The SHKT's proxy constraints as (child, parent) bare proxy names, in file order (the order CarBase's ragdoll keeps:
    the 2026-10-21 run logged `constraint 0: proxy 1 (gun) - proxy 0 (body)` for the first instance, gun <-> body)."""
    tf = hk2014.read(physics)
    def name(ref: int) -> str:
        return str(tf.objects[ref - 1]['name']).removeprefix('RagDollProxys.')
    return [(name(o['entities'][0]), name(o['entities'][1])) for o in tf.objects if o.type.name == 'hkpConstraintInstance']


def pinned(members: dict, physics: bytes) -> list[tuple[str, str]]:
    """The constraints that join a keyframed proxy to a dynamic one once CarBase has set the modes: what held the 408 in
    the air (docs/edf41-bike-re.md 「悬空」). CarBase resets every proxy to dynamic mode 4 (0x6E40B0) and adds every
    constraint between two bodies in the world (0x6E4A70), then walks car_base_simulation_node: a row value 1 makes that
    proxy keyframed (0x66428F -> 0x6ED480 mode 2: motion type 1, kept in the world) and leaves its constraint; a row value
    0 (0x6642A2 -> 0x6E94E0 -> 0x6E96C0, recursive) removes the constraint to its parent (0x6E9725 -> 0x6EC840) and takes
    the proxy and its children out of the world (constructor default mode 1, +0xC8). A keyframed body has infinite mass:
    a dynamic chassis joined to it can only move where the keyframed body is moved, and its target is the chassis' own
    pose of the frame before, so it stays where it was made (the run: gun mode 2 / motion 1 / constraint added, body never
    fell)."""
    links = joints(physics)
    parent = dict(links)
    children: dict[str, list[str]] = {}
    for child, up in links:
        children.setdefault(up, []).append(child)
    mode = {n: 4 for n in PROXIES}
    removed: set[tuple[str, str]] = set()
    def default(node: str) -> None:
        if node in parent:
            removed.add((node, parent[node]))
        mode[node] = 1
        for child in children.get(node, ()):
            default(child)
    for name, value in members['car_base_simulation_node']:
        if value == 0:
            default(name)
        elif value == 1:
            mode[name] = 2
    return [(a, b) for a, b in links if (a, b) not in removed and {mode[a], mode[b]} == {2, 4}]


def check(members: dict, md: mdb.Mdb, physics: bytes) -> None:
    names = {md.name_of(b.name) for b in md.bones}
    if MARKER not in names or members.get('car_aim_node') != []:
        raise ValueError('408 must have its own marker and a real empty native car aim')
    bodies = {o['name']: o['motion']['motionState']['transform'] for o in hk2014.read(physics).objects
              if o.type.name == 'hkpRigidBody'}
    if set(bodies) != {'RagDollProxys.' + n for n in PROXIES}:
        raise ValueError('408 physics no longer holds its eight original bodies')
    bound = sgo.read(members['ragdoll'][1])[1]
    frames = ragdoll_fit.bone_frames(md)
    for key, reverse in (('animation_from_ragdoll', True), ('ragdoll_from_animation', False)):
        for link in ragdoll_fit.links(bound, key):
            bone, proxy = (link.dst, link.src) if reverse else (link.src, link.dst)
            if bone not in names or proxy not in bodies:
                raise ValueError(f'408 {key} unresolved bone / body: {bone} / {proxy}')
            p = bodies[proxy]
            physical = (tuple(p[12:15]), [tuple(p[4 * i:4 * i + 3]) for i in range(3)])
            source, expected = (physical, frames[bone]) if reverse else (frames[bone], physical)
            origin, basis = source
            position = tuple(origin[j] + sum(link.off.t[i] * basis[i][j] for i in range(3)) for j in range(3))
            rotation = [tuple(sum(axis[i] * basis[i][j] for i in range(3)) for j in range(3))
                        for axis in ragdoll_fit.axes(link.off.q)]
            delta = max(abs(a - b) for a, b in zip(position, expected[0]))
            delta = max(delta, *(abs(rotation[i][j] - expected[1][i][j]) for i in range(3) for j in range(3)))
            if delta > 1e-3:
                raise ValueError(f'408 {bone} physics and model bind frames disagree: {delta:.6f}')
    if {row[0] for row in members['car_base_wheel']} != set(WHEELS) or len(members['car_base_wheel']) != 3:
        raise ValueError('408 does not have three real native wheels')
    required = {members['car_base_body_name']}
    for key in ('car_base_rigid_body', 'car_base_simulation_node', 'car_base_wheel', 'vehicle_weapon_setting'):
        required.update(row[0] for row in members[key])
    required.update(row[0] for row in members['car_base_breaking_parts'][1])
    if any(row[0] == 'gun' for row in members['car_base_rigid_body']):
        raise ValueError('408 gun must remain animation-driven, not a dynamic CarBase body')
    if [row for row in members['car_base_simulation_node'] if row[0] == 'gun'] != [['gun', 0]]:
        raise ValueError('408 gun needs its one animation-driven simulation row 0, like a stock car moving part')
    held = pinned(members, physics)
    if held:
        raise ValueError(f'408 keyframed proxies pin the dynamic chassis through their constraints: {held}')
    seats = len(members['vehicle_riding_position'])
    fall = members['car_base_fall_out'][4]
    if not isinstance(fall, list) or any(not isinstance(i, int) or not 0 <= i < seats for i in fall):
        raise ValueError(f'408 car_base_fall_out names seats it does not have: {fall} of {seats}')
    for key in ('vehicle_setup', 'mission_setup'):
        if sgo.plain(members[key][2]) != sgo.plain(native_gun_rows(members[key][2])):
            raise ValueError('408 Car gun needs a native BodyRecoil pair')
    locators = mab_legacy.mab_locators(members['animation_model'][2])
    required.update(loc.node for _, loc in locators)
    if required - names:
        raise ValueError(f'408 runtime names missing model bones: {sorted(required - names)}')
    loc_names = {loc.name for _, loc in locators}
    for row in members['vehicle_riding_position']:
        if len(row) != 7 or any(n not in loc_names for n in (row[0], row[1], row[2][0])):
            raise ValueError('408 seat has unresolved locators or is not the native seven-field row')
    if len(members['vehicle_riding_position']) != 2 or members['vehicle_weapon_setting'] != [['gun', 1]]:
        raise ValueError('408 must have two real seats and its one gun on the gunner')
    for i, row in enumerate(members['car_base_draw_wheel_info']):
        if row[0] != [i, WHEELS[i]]:
            raise ValueError('408 wheel drawing index no longer matches the physical wheel')


def build(games, root: str) -> dict[str, bytes]:
    if root not in ROOTS:
        raise ValueError(f'not a 408 sidecar: {root}')
    source = sgo.read(legacy_sgo.convert(games.read4('OBJECT/' + root)))[1]
    if source['xgs_scene_object_class'] != 'VehicleBike':
        raise ValueError('408 source has another runtime class')
    version, members = sgo.read(games.read6(CAR_TEMPLATE))
    members = copy.deepcopy(members)
    if members['xgs_scene_object_class'] != 'Vehicle_Car' or members['car_aim_node']:
        raise ValueError('408 template must be the native car with an empty aim')
    if [round(float(x), 6) for x in sgo.plain(members['vehicle_setup'][1])] != CAR_DRIVE:
        raise ValueError('408 Car drive block differs from CAR_DRIVE, which the requests carry')
    am = source['animation_model']
    entry = copy.deepcopy(am[0][0])
    archive_rel = rel(entry[0])
    archive = legacy_assets.convert(archive_rel, games.read4(archive_rel))
    rab = mdb.rab_read(archive)
    member = next(f for f in rab.files if f.name.lower() == entry[1].lower())
    md = mdb.mdb_read(member.data)
    mdb.insert_bone(md, MARKER, 'body')
    member.stored = mdb.cmpl_compress(mdb.mdb_write(md)) if member.stored[:4] == b'CMPL' else mdb.mdb_write(md)
    files = {archive_rel: mdb.rab_write(rab)}
    offsets, pose_rows = pose_fit(games)
    members['animation_model'] = [entry, 0, seat_mab(am[2], offsets)]
    members['ragdoll'] = copy.deepcopy(source['ragdoll'])
    shkt_rel = rel(members['ragdoll'][0])
    files[shkt_rel] = games.read4(shkt_rel)
    # Keep both directions, including gun. The runtime applies its live gun pose
    # after native physics readback; death returns to the untouched wreck path.
    members['car_base_body_name'] = 'body'
    # Keep all eight source proxies; the gun is not a dynamic CarBase body: its bone is posed from the gunner's Aim
    # (src/edf41bike.cpp Pose408), never by physics.
    members['car_base_rigid_body'] = [[n, -1.0, -1.0, -1.0, -1.0, [], []] for n in sorted(PROXIES - {'gun'})]
    # CarBase first resets ALL proxies to mode 4 (6E40B0); a row value 0 takes the proxy out of the world and removes
    # its constraint to the body (6E94E0 -> 6E96C0), as for the wheels and the stock V603's spinning radars. The gun was
    # row 1 (mode 2, keyframed) until 2026-10-21: still joined to the body by its ragdoll constraint it held the
    # dynamic chassis where it was made, in the air (pinned(), docs/edf41-bike-re.md 「悬空」).
    members['car_base_simulation_node'] = [[n, 0] for n in WHEELS] + [['gun', 0]]
    wheel = members['car_base_wheel'][0]
    members['car_base_wheel'] = []
    for i, old in enumerate(source['vehicle_tire']):
        if old[0] != WHEELS[i]:
            raise ValueError('408 source wheel order changed')
        row = [old[0], *copy.deepcopy(wheel[1:])]
        row[8] = old[5]  # the original three tire radii (0.3 m)
        row[10:13] = [0 if i == 0 else 1, 1 if i == 0 else -1, 0 if i == 2 else 1]
        members['car_base_wheel'].append(row)
    members['car_base_draw_wheel_info'] = [[[i, n], [-1, -1, -1, 0.0, 1 if i == 0 else 0]]
                                           for i, n in enumerate(WHEELS)]
    members['car_base_draw_wheel_limit_info'] = []
    members['car_base_wheel_effect'] = [[0], [1], [2]]
    members['car_base_breaking_parts'][1] = [[n, 1, 2] for n in sorted(PROXIES - {'body'})]
    members['car_base_se_table'] = copy.deepcopy(source['car_se_table']) + [0, 0]
    members['car_base_ragdoll_contact'] = copy.deepcopy(source['ragdoll_contact'])
    members['car_enable_horn'] = 0
    basic = source['vehicle_basic']
    members['car_base_all_rigid_body'] = [*copy.deepcopy(basic[1][:3]), copy.deepcopy(basic[2]), copy.deepcopy(basic[3])]
    members['car_base_submerge_height'] = source['submerge_height']
    members['car_base_head_light'] = [[source['spotlight'][0], source['spotlight'][2], source['spotlight'][1],
                                       copy.deepcopy(source['spotlight'][3])]]
    for key in ('game_object_camera_setting', 'game_object_durability', 'game_sound', 'vehicle_damage_effect'):
        members[key] = copy.deepcopy(source[key])
    members['vehicle_riding_position'] = [
        [*copy.deepcopy(source['vehicle_riding_position'][0][:3]), '512_KEITRUCK_DRIVER', 15, 5.0, 0],
        [*copy.deepcopy(source['vehicle_riding_position'][1][:3]), '401_STRIKER_SHEET1', 15, 0.0, 6]]
    members['vehicle_weapon_setting'] = [['gun', 1]]
    # car_base_fall_out [4]: the seats thrown off at a hard impact. The template's are V512's own [1, 2, 3, 4] (its
    # bed); 0x673F41 indexes the seat array with them unchecked (seat * 0x340 + 0x268), so on the two-seat 408 the
    # first hard knock read past it (2026-10-21 run: C0000005 at EDF.dll+0x673F5A). Every seat but the driver's.
    members['car_base_fall_out'][4] = list(range(1, len(members['vehicle_riding_position'])))
    gun_name = WEAPONS['v_408motorbike_gun.sgo'][0]
    gun_rows = native_gun_rows(source['vehicle_setup'][2])
    if len(gun_rows) != 1:
        raise ValueError('408 source does not have its one machine gun')
    gun_rows[0][0] = 'app:/weapon/' + gun_name + '.sgo'
    members['vehicle_setup'][0] = copy.deepcopy(source['vehicle_setup'][0])
    members['vehicle_setup'][2] = gun_rows
    members['mission_setup'] = copy.deepcopy(members['vehicle_setup'])
    members['edf41_bike'] = [1, ['body', 'handle', 'gun'], copy.deepcopy(source['bike_gun_ctrl']), 1]
    members['edf41_bike_pose'] = [offsets, pose_rows]
    # This particular 4.1 vehicle gun is a big-endian SGO; read() handles both
    # byte orders, while legacy_sgo.convert deliberately accepts only LE files.
    old_gun = sgo.read(games.read4('WEAPON/V_408MOTORBIKE_GUN.SGO'))[1]
    weapon = edf5port.weapon41(old_gun, {'ja': '機関銃', 'en': 'Machine Gun', 'cn': '機關槍', 'kr': '기관총', 'sc': '机枪'})
    files['WEAPON/' + gun_name.upper() + '.SGO'] = dsgo.write(weapon)
    check(members, md, files[shkt_rel])
    files['OBJECT/' + root] = sgo.write_depth_first(version, members)
    return files
