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
# The SDL1 models (2026-10-25: 「没做完的根本性做完」). In 4.1 every SDL1 request drops the same VEHICLE408_MOTORBIKE.SGO
# and the model is only the request's own setup: [0] armour, [1] VehicleBike's four numbers, [2] the gun. VehicleBike
# (vtable 0xAA0670 slot 29 0x2F55D0 -> 0x2D06E0 -> 0x2D07E0, EDF41.exe) is Havok's vehicle kit: [1][0] -> every gripping
# wheel's friction (hkpVehicleData wheel +0x0C, maxFriction +0x14 = 2x; a 0 wheel, the sidecar's, is left 0), [1][1]
# -> its slip angle (+0x18), [1][2] -> the top speed (0x2D19F0: +0x1190, and the primary transmission ratio from it,
# 0x660640 = maxRPM / (speed x k / radius) / top gear: wheel torque ~ 1 / speed), [1][3] -> the engine's max torque
# (engine +0x1C). The models, by their 4.1 request file (armour, block; checked against 4.1 by tests/test_edf41bike.py):
SDL1_MODELS: dict[str, tuple[str, list[float], list[float]]] = {
    'WEAPON688.SGO': ('SDL1', [1.5, 1.5], [2.0, 0.9, 30.0, 100.0]),
    'WEAPON700.SGO': ('SDL1 Bad Custom', [1.8, 1.8], [0.25, 0.9, 30.0, 100.0]),
    'WEAPON721.SGO': ('SDL1 Type R', [5.0, 5.0], [2.0, 1.25, 30.0, 225.0]),
    'WEAPON739.SGO': ('SDL1 Type Z', [10.5, 10.5], [2.0, 1.2, 50.0, 250.0]),
    'WEAPON753.SGO': ('SDL1 Burst Turbo', [22.0, 22.0], [2.0, 1.2, 60.0, 400.0]),
}
BASE41 = SDL1_MODELS['WEAPON688.SGO'][2]   # the plain SDL1: the model CAR_DRIVE stands for


def car_drive(block41: object) -> list[float]:
    """The Car drive block (CAR_DRIVE's seven numbers) of a 4.1 SDL1 request's VehicleBike block, relative to the plain
    SDL1 (BASE41 -> CAR_DRIVE, the block every 408 drove with until now): [0] grip scales EDF6's friction ([0] scales
    car_base_brake_and_friction [2..4], 0x66EEC0 over +0x17B0..+0x17B8; the brakes [0..1] stay), the top speed
    scales [1] (the gear table's speeds, 0x66E1A0), and the wheel force [2] (the gears' drive, independent of [1] in
    EDF6) by torque / top speed as 4.1's transmission makes it. The slip angle [1][1] has no Car counterpart that was
    found and is not mapped; steering and traction stay the chassis'."""
    b = sgo.plain(block41)
    if (not isinstance(b, list) or len(b) != 4 or not all(isinstance(x, (int, float)) for x in b)
            or min(b[0], b[2], b[3]) <= 0):
        raise ValueError(f'not a 4.1 VehicleBike drive block: {b!r}')
    grip, _slip, speed, torque = (float(x) for x in b)
    out = list(CAR_DRIVE)
    out[0] = CAR_DRIVE[0] * grip / BASE41[0]
    out[1] = CAR_DRIVE[1] * speed / BASE41[2]
    out[2] = CAR_DRIVE[2] * (torque / BASE41[3]) * (BASE41[2] / speed)
    return [round(x, 6) for x in out]


# The rider's pose per seat: the seat locator's MAB Animation, played by name on the rider's own CAS (HumanBase
# 0x57AE8E -> 0x116C2D0, docs/all-classes-ride-re.md §3). The driver straddles the bike as on EDF6's own motorbike
# (V503 Freed's seat: vehicle503_bike_driver, in the Ranger's and the Air Raider's CAS, the classes 4.1's driver seat
# admits, mask 9); until 2026-10-25 it sat on the bench pose vehicle_striker_rear, legs forward through the tank and no
# hands on the bars (feedback 「4.1摩托SDL1会出现驾驶姿势问题」). A class without the bike pose (Wing Diver, Fencer:
# only through AllClassesRide) gets allclass.cpp RideState's bench fallback, as on the stock bikes. The sidecar
# gunner sits on the bench pose, which all four classes have.
SEAT_STATES = ('vehicle503_bike_driver', 'vehicle_striker_rear')
SOURCE_STATES = ('vehicle_bike_driver', 'vehicle_bike_gunner')   # 4.1's own two seated states
RIDER_CLASSES = (('ARMYSOLDIER', 'EDF6ARMYSOLDIER', 'P505_RANGER', 1),
                 ('ENGINEER', 'EDF6ENGINEER', 'P508_AIRRADER', 8),
                 ('PALEWING', 'EDF6_WINGDIVER', 'P506_WINGDIVER', 2),
                 ('HEAVYARMOR', 'EDF6HEAVYARMOR', 'P507_FENCER', 4))   # (4.1 CAS, EDF6 CAS, EDF6 model, class bit)
# The driver's hand IK targets (2026-10-26, feedback 「人手臂伸出去好远」: on the 408 the driver's arms ran as thin rods
# from the shoulders far past the bars). vehicle503_bike_driver reaches each hand for its seat's grip matrix: HumanBase's
# update (0x575275 / 0x575329) sets the rider's v_vehicle_grip_l / _r_ik_target (indices +0x1440 / +0x1444, looked up at
# 0x56E495 / 0x56E4AE) from seat +0x40 / +0x80 whenever the rider is in a seat. Only Vehicle503_Bike fills those two
# matrices: its constructor finds te_ik_l / te_ik_r (0x61757D / 0x6175FE -> +0x29A0 / +0x29A4) and its update copies
# the two bones' world matrices into seat 0 every frame (0x617A30). Vehicle_Car never writes them, so they stay the
# seat constructor's identity (0x6291EB / 0x629225): a target at the map's origin. The 408 now carries the same two
# bones (under its steering `handle`) and src/edf41bike.cpp copies them as 0x617A30 does. V503's te_ik_l bind is
# exactly the Ranger's te_l in vehicle503_bike_driver (position and rotation, the pose's own hands); the 408's bones are
# 4.1's own driver's hands in vehicle_bike_driver on this bike (4.1's seat at the origin, whose pelvis pose_fit
# matches), turned onto EDF6's te_l axes by the two skeletons' binds.
GRIP_BONES = (('te_ik_l', 'te_l'), ('te_ik_r', 'te_r'))   # (vehicle bone, the rider's hand bone)
GRIP_PARENT = 'handle'
GRIP_RIDER41 = ('OBJECT/ARMYANDENGINEER.MRAB', 'ArmySoldier.mdb', 'OBJECT/ARMYSOLDIER.CAS')
GRIP_RIDER6 = ('OBJECT/P505_RANGER.MRAB', 'p505_ranger.mdb')
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


def pose_fit(games, masks: tuple[int, int]) -> tuple[list[list[float]], list[list]]:
    """Fit each seat locator so its native pose (SEAT_STATES) puts the pelvis where 4.1's own seated state had it.

    `masks`: the two seats' class masks as 4.1 authored them (vehicle_riding_position [4]: the driver 9, Ranger and
    Air Raider; the gunner 15). Every class a seat admits must have that seat's native state and land within 8 cm of
    its own 4.1 pelvis under one shared translation, without replacing global soldier CAS files. A class the seat does
    not admit and whose CAS lacks the state (Wing Diver / Fencer on the driver's seat, reachable only by
    AllClassesRide) rides on allclass.cpp's bench fallback as on EDF6's own bikes: no fit, error None.
    Rows: [4.1 class, [4.1 driver, gunner pelvis], [native pelvis per seat or None], [error per seat or None]].
    """
    cached = getattr(games, '_edf41bike_pose_fit', None)
    if cached is not None and cached[0] == list(masks):
        return copy.deepcopy(cached[1])
    rows = []
    for source, native, _model, bit in RIDER_CLASSES:
        old = cas_legacy.cas_from_legacy(games.read4(f'OBJECT/{source}.CAS'))
        new = games.read6(f'OBJECT/{native}.CAS')
        states = cas_graft.state_clips(new).get('main', {})
        posed = []
        for seat, state in enumerate(SEAT_STATES):
            if state in states:
                posed.append(list(_state_root(new, state)))
            elif masks[seat] & bit:
                raise ValueError(f'408 seat {seat} admits {source} but its CAS lacks {state}')
            else:
                posed.append(None)
        rows.append([source, [list(_state_root(old, s)) for s in SOURCE_STATES], posed])
    offsets = []
    for seat in range(2):
        fitted = [row for row in rows if row[2][seat] is not None]
        offsets.append([sum(row[1][seat][c] - row[2][seat][c] for row in fitted) / len(fitted) for c in range(3)])
    for row in rows:
        errors = [None if row[2][seat] is None else
                  math.dist([row[2][seat][c] + offsets[seat][c] for c in range(3)], row[1][seat]) for seat in range(2)]
        worst = max(e for e in errors if e is not None)
        if worst > 0.08:
            raise ValueError(f'408 {row[0]} shared seat pose differs by {worst:.3f} m')
        row.append(errors)
    result = offsets, rows
    games._edf41bike_pose_fit = copy.deepcopy([list(masks), result])
    return result


def hand_pose(cas: bytes, md: mdb.Mdb, state: str) -> dict[str, list[float]]:
    """Model-space matrices of the rider's two hand bones (GRIP_BONES' te_l / te_r) in the seated `state` (one clip,
    every key the same), the skeleton's root at the seat."""
    clips = cas_graft.state_clips(cas).get('main', {}).get(state, [])
    if len(clips) != 1:
        raise ValueError(f'408 rider state {state} does not resolve to one clip')
    pose = cas_pose.CasPose(cas)
    clip = next(c for c in pose.clips if c.name == clips[0])
    names = [md.name_of(b.name) for b in md.bones]
    _duration, _step, frames = cas_pose.clip_info(pose, clip)
    keys = [cas_pose.pose_world(pose, clip, md, k) for k in range(max(frames, 1))]
    out = {}
    for _vehicle, hand in GRIP_BONES:
        rows = [w[names.index(hand)] for w in keys]
        if max(abs(a - b) for m in rows for a, b in zip(m, rows[0])) > 1e-4:
            raise ValueError(f'408 seated rider {state} moves its {hand}')
        out[hand] = rows[0]
    return out


def _rotation(m: list[float]) -> list[float]:
    """`m` with its translation dropped; refused unless its 3x3 part is a rotation (orthonormal, right-handed)."""
    r = [m[i * 4:i * 4 + 3] for i in range(3)]
    for i in range(3):
        for j in range(3):
            if abs(sum(r[i][k] * r[j][k] for k in range(3)) - (1.0 if i == j else 0.0)) > 1e-3:
                raise ValueError('408 grip frame is not a rotation')
    det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
           + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
    if det < 0.999:
        raise ValueError('408 grip frame is not right-handed')
    return [*r[0], 0.0, *r[1], 0.0, *r[2], 0.0, 0.0, 0.0, 0.0, 1.0]


def grip_targets(games) -> list[tuple[str, list[float]]]:
    """The 408's driver grip IK bones (GRIP_BONES' te_ik_l / te_ik_r) as model-space matrices: where 4.1's own Ranger
    held this bike's bars. Position: 4.1's te_l / te_r in vehicle_bike_driver (SOURCE_STATES[0]) with the skeleton's
    root at 4.1's seat (the model origin: seat_mab refuses any other source locator; pose_fit puts EDF6's pelvis on 4.1's).
    Rotation: the same hand in EDF6's te_l axes, D x R41 with D = bind6 x bind41^-1 (both skeletons bind their hands
    in the same place, so D only relabels the bone's axes). The EDF6 pose then bends its arms to 4.1's grip instead of
    reaching for the identity matrix at the map's origin."""
    archive41, model41, cas41 = GRIP_RIDER41
    md41 = model(games.read4(archive41), model41)
    md6 = model(games.read6(GRIP_RIDER6[0]), GRIP_RIDER6[1])
    held = hand_pose(cas_legacy.cas_from_legacy(games.read4(cas41)), md41, SOURCE_STATES[0])
    bind41 = {md41.name_of(b.name): m for b, m in zip(md41.bones, mdb.bind_world(md41))}
    bind6 = {md6.name_of(b.name): m for b, m in zip(md6.bones, mdb.bind_world(md6))}
    out = []
    for bone, hand in GRIP_BONES:
        if max(abs(a - b) for a, b in zip(bind41[hand][12:15], bind6[hand][12:15])) > 1e-3:
            raise ValueError(f'408 grip: 4.1 and EDF6 bind {hand} in different places')
        relabel = mdb.mmul(_rotation(bind6[hand]), mdb.inverse_affine(_rotation(bind41[hand])))
        target = _rotation(mdb.mmul(relabel, _rotation(held[hand])))
        target[12:15] = held[hand][12:15]
        out.append((bone, [round(x, 6) for x in target]))
    return out


def add_grips(md: mdb.Mdb, grips: list[tuple[str, list[float]]]) -> None:
    """GRIP_BONES' vehicle bones into `md` under GRIP_PARENT (kind 0, transform only, as V503's), at `grips`' model-space
    matrices: local = target x parent bind^-1 so they turn with the bars, inverse bind = target^-1."""
    for bone, target in grips:
        at = mdb.insert_bone(md, bone, GRIP_PARENT)
        parent = mdb.bind_world(md)[md.bones[at].parent]
        md.bones[at].local = [round(x, 6) for x in mdb.mmul(target, mdb.inverse_affine(parent))]
        md.bones[at].inv_bind = [round(x, 6) for x in mdb.inverse_affine(target)]


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
    for state in SEAT_STATES:
        params_at.append(layout.strings_at + grow_vecs + len(extra))
        extra += sgo.write_depth_first(0x102, {'Animation': state, 'DispCharacter': 1})
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


def wheel_flags(tire: list) -> list:
    """car_base_wheel [10:13] of one 4.1 vehicle_tire row: [driven, steering slot or -1, steering factor].

    EDF6's Car reads them at 0x679CFC..0x67A004: [10] != 0 -> wheel +0x64 (driven: the stock bikes V503 / V613 drive
    the rear wheel only, V512 its rear axle), [11] != -1 -> +0x65 (steered) and then +0x70[[11]] = [12]. 4.1's
    VehicleBike reads its tire row at EDF41.exe 0x2CC7AD..0x2CC803 (record defaults at 0x2CBDA7..0x2CBDFC): [8] ->
    +0x64 (preset to 1 for the first two tires, a car's front axle, at 0x2CBF2A: steered), [9] -> +0xA8 (default 1)
    and [10] -> +0x68 (default 0.25), read here as the drive flag and the wheel's share of the drive (every tank road
    wheel 1 / 0.1, the Striker's rear axles 1, the 408's rear wheel alone 1 / 1.0). The 408's own rows: front steered,
    share 0; rear driven; the sidecar wheel neither (flag 0, share 0, and no grip: [2..4] 0), i.e. rear-wheel drive
    as EDF6's own bikes. The flags were set by position until 2026-10-25, which also drove the sidecar wheel: a pushed
    wheel 1 m out on the left side of a 300 kg bike."""
    tire = sgo.plain(tire)
    if not isinstance(tire, list) or len(tire) < 11:
        raise ValueError(f'408 tire row lacks its steer / drive columns: {tire!r}')
    steer, drive = bool(tire[8]), bool(tire[9]) and float(tire[10]) > 0.0
    return [1 if drive else 0, 1 if steer else -1, 1 if steer else 0]


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
    for bone, _hand in GRIP_BONES:
        if bone not in names or md.name_of(md.bones[md.bones[mdb.bone_named(md, bone)].parent].name) != GRIP_PARENT:
            raise ValueError(f'408 driver grip {bone} must be a bone under {GRIP_PARENT} (src/edf41bike.cpp copies it)')
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
    add_grips(md, grip_targets(games))
    member.stored = mdb.cmpl_compress(mdb.mdb_write(md)) if member.stored[:4] == b'CMPL' else mdb.mdb_write(md)
    files = {archive_rel: mdb.rab_write(rab)}
    seats41 = source['vehicle_riding_position']
    masks = tuple(int(sgo.plain(seat[4])) for seat in seats41)
    if len(masks) != 2:
        raise ValueError('408 source does not have its two seats')
    offsets, pose_rows = pose_fit(games, masks)
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
        row[10:13] = wheel_flags(old)
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
    # [4] is the seat's class mask (docs/all-classes-ride-re.md §1): 4.1's own, as EDF6's bikes (V503 / V613: 9) —
    # the driver Ranger and Air Raider, whose CAS have the bike pose; AllClassesRide widens it at run time.
    members['vehicle_riding_position'] = [
        [*copy.deepcopy(seats41[0][:3]), '512_KEITRUCK_DRIVER', masks[0], 5.0, 0],
        [*copy.deepcopy(seats41[1][:3]), '401_STRIKER_SHEET1', masks[1], 0.0, 6]]
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
    # A record of the fit (nothing reads it at run time); a seat a class has no pose for (None) is written as [].
    members['edf41_bike_pose'] = [offsets, [[name, roots, [p if p is not None else [] for p in posed],
                                             [e if e is not None else [] for e in errors]]
                                            for name, roots, posed, errors in pose_rows]]
    # This particular 4.1 vehicle gun is a big-endian SGO; read() handles both
    # byte orders, while legacy_sgo.convert deliberately accepts only LE files.
    old_gun = sgo.read(games.read4('WEAPON/V_408MOTORBIKE_GUN.SGO'))[1]
    weapon = edf5port.weapon41(old_gun, {'ja': '機関銃', 'en': 'Machine Gun', 'cn': '機關槍', 'kr': '기관총', 'sc': '机枪'})
    files['WEAPON/' + gun_name.upper() + '.SGO'] = dsgo.write(weapon)
    check(members, md, files[shkt_rel])
    files['OBJECT/' + root] = sgo.write_depth_first(version, members)
    return files
