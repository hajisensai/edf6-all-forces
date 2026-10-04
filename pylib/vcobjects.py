"""The plugin's generated objects, made on this machine from the player's own Root.cpk: the jets (src/jet.cpp,
src/playerjet.cpp, src/subcarrier.cpp) as V506 heli SGOs with their own models, their guns and charges, the
portal laser (src/carrierlaser.cpp) and script-placeable call-in vehicles. Shared by tools/make_jets.py,
tools/make_sub.py, tools/call_weapons.py and the test range (testrange/gen.py); builders only: what gets
written where, and who owns it, is pylib/ledger.py.
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass

import dsgo
import sgo
from rootcpk import DEFAULT_GAME, Game  # noqa: F401  (re-exported: the tools take both from here)

@dataclass(frozen=True)
class Jet:
    mark: float        # mission_setup[1][0], the speed gain k: how EDF6VehicleCrew (src/jet.cpp) tells a jet
    durability: float
    weapons: tuple[str, ...]
    # Its own model (pylib/jet_models.py writes the archive, `file`, into Mods/OBJECT), or None: the bomber
    # (JET_MODEL / JET_ELEVON_MODEL). `body`: the mesh bone; `anchor`: the bone the V506 locators, weapons
    # and dead effect hang on (it replaces their names in place, so it is at most 4 characters: `body`).
    # The model's root bone is always JET_ROOT_BONE (see JET_MAB_BONES).
    model: tuple[str, str] | None = None
    file: str | None = None
    body: str = 'bomber501'
    anchor: str = 'mdl'
    rigid: tuple[tuple[float, float, float], tuple[float, float, float]] | None = None
    # The bone each weapon hangs on (vehicle_weapon_setting), in `weapons` order; empty: all on `anchor`.
    weapon_bones: tuple[str, ...] = ()
    # A player jet (src/playerjet.cpp): the player flies it. Its SGO keeps `vehicle_setup` beside
    # `mission_setup` (the Air Raider's call weapon brings it like a stock heli, tools/call_weapons.py), and
    # `camera` replaces game_object_camera_setting's offset (the stock heli's (0, 5.5, -11.5) is inside a jet).
    player: bool = False
    camera: tuple[float, float, float] | None = None
    # The stock heli SGO it is made from (its body, rigid body, crash and weapons): every jet is a V506.
    stock: str = 'V506_HELI'


# Jets (src/jet.cpp, docs/jet-model-re.md): the V506 heli body (rigid body, HP, weapons, crash) with the
# BOMBER501 model, flown by the plugin. The 506 fires 0x2020 -> weapons 0 and 1, 0x2021 -> weapon 2.
# The guns are the 506's gatlings with a jet's reach (jet_guns): stock they fly 4 m a frame for 40 frames,
# 160 m, inside every role's gun pass (src/jet.cpp kKinds gunOpen 350-500 m, Fire takes the nearer of the
# two): a jet diving at 160 m/s had 0.3 s between their reach and its pull-out, and 7 of a drone's 130 gun
# chances fired on 2026-10-03 (the rest held, the nose not yet on the lead); the strike, interceptor and
# multirole jets fired none. Faster and longer lived they reach JET_GUN_REACH; damage and rate stay stock.
JET_GUN_FILES = {'EDF6VC_JET_GUN_L.SGO': 'V_506HELI_GATLING01_L.SGO', 'EDF6VC_JET_GUN_R.SGO': 'V_506HELI_GATLING01_R.SGO'}
JET_GUN_SPEED, JET_GUN_ALIVE = 10.0, 60.0   # m a frame, frames: 600 m/s, 600 m
JET_GUN_REACH = JET_GUN_SPEED * JET_GUN_ALIVE
_GUNS = tuple('app:/weapon/' + f.lower() for f in JET_GUN_FILES)
_MISSILE = 'app:/weapon/v_506heli_missile01.sgo'
_ARMS = _GUNS + (_MISSILE,)
# The blast drones' charge (src/jet.cpp Detonate: weapon 2, fired by 0x2021 once next to the enemy): the
# 409's unguided bomb (GrenadeBullet01) made a point charge (docs/decoy-blast-re.md 1.4): CP#0 = 1 bursts
# when its life runs out (0x26543E), CP#3 = 0 no bounce, CP#5 = 0 no random life; it barely moves, lives
# JET_BLAST_ALIVE frames, so it goes off where the drone is. One round, one shot. (damage, radius m).
JET_BLAST_STOCK = 'V_409HELI_BOMB01.SGO'
JET_BLAST_FILES: dict[str, tuple[float, float]] = {
    'EDF6VC_BLAST_CHARGE.SGO': (1200.0, 15.0),   # the blast drone: fast, many
    'EDF6VC_DOLL_CHARGE.SGO': (3000.0, 25.0),    # the doll drone: slow, draws the enemy in first
}
JET_BLAST_ALIVE = 2.0
_BLAST = tuple('app:/weapon/' + f.lower() for f in JET_BLAST_FILES)
JET_WEAPON_FILES = (*JET_GUN_FILES, *JET_BLAST_FILES)
# Model sizes and boxes: pylib/jet_models.py (bind-pose vertices after scaling).
JETS: dict[str, Jet] = {
    'edf6tr_jet_strike_mission': Jet(7001.0, 1500.0, _ARMS),
    'edf6tr_jet_fighter_mission': Jet(7002.0, 1000.0, _ARMS),
    # bomber501_2 (dark paint) with elevons, x 0.65: 16 m across
    'edf6tr_jet_interceptor_mission': Jet(7003.0, 900.0, _ARMS, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                          'EDF6VC_INTERCEPTOR.MRAB', 'bomber501', rigid=((0.0, 0.22, 1.69), (1.3, 1.04, 8.45))),
    # bomber401 x 0.5: 26 m across
    'edf6tr_jet_multirole_mission': Jet(7004.0, 1300.0, _ARMS, ('app:/object/edf6vc_multirole.mrab', 'bomber401.mdb'),
                                        'EDF6VC_MULTIROLE.MRAB', 'bomber401', rigid=((0.0, 1.07, 0.0), (1.25, 1.0, 4.0))),
    # the EDF transport x 1.6: 59 x 77 m; it never fires (its drones do)
    'edf6tr_jet_carrier_mission': Jet(7005.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                      'EDF6VC_CARRIER.MRAB', 'body', rigid=((0.0, 6.75, -3.11), (7.09, 6.77, 38.42))),
    # the same carrier sending blast / doll drones (src/jet.cpp kCarrierMarks)
    'edf6tr_jet_blast_carrier_mission': Jet(7009.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                            'EDF6VC_CARRIER.MRAB', 'body', rigid=((0.0, 6.75, -3.11), (7.09, 6.77, 38.42))),
    'edf6tr_jet_doll_carrier_mission': Jet(7010.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                           'EDF6VC_CARRIER.MRAB', 'body', rigid=((0.0, 6.75, -3.11), (7.09, 6.77, 38.42))),
    # the airstrike drone x 3: 5.7 m long; only carriers launch it (tools/make_jets.py EDF6VC_JET_DRONE.SGO)
    'edf6tr_jet_drone': Jet(7006.0, 300.0, _ARMS, ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                            'EDF6VC_DRONE.MRAB', 'body', 'body',
                            rigid=((0.0, -0.47, 1.08), (1.75, 1.04, 2.83))),
    # Blast and doll drones (src/jet.cpp Role::blast / doll): the drone with a charge for its missile; only
    # the blast and doll carriers launch them (their guns never fire).
    'edf6tr_jet_blast': Jet(7007.0, 250.0, _GUNS + (_BLAST[0],), ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                            'EDF6VC_DRONE.MRAB', 'body', 'body', rigid=((0.0, -0.47, 1.08), (1.75, 1.04, 2.83))),
    'edf6tr_jet_doll': Jet(7008.0, 800.0, _GUNS + (_BLAST[1],), ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                           'EDF6VC_DRONE.MRAB', 'body', 'body', rigid=((0.0, -0.47, 1.08), (1.75, 1.04, 2.83))),
    # the submarine carrier (src/subcarrier.cpp, tools/make_sub.py, docs/subcarrier-re.md): the mission
    # object EV603_MARINE's model at its own size, 1664 m long; the box is the 30 m of hull under its main
    # deck (y 163.08..193.08 over the origin; the tower above is not solid). Not the whole hull: afloat its
    # keel is 340 m down and EDF's seas are some 30 m deep (M082, 2026-10-04): a hull box stuck in the seabed,
    # was pushed 215 m off its point and fought the ground every frame. Guns on its forward turrets' (left)
    # barrels, the missile on its missile bay.
    'edf6tr_sub_carrier_mission': Jet(7101.0, 30000.0, _ARMS, ('app:/object/edf6vc_sub.mrab', 'ev603_marine.mdb'),
                                      'EDF6VC_SUB.MRAB', 'body', 'body', rigid=((0.0, 178.08, -7.58), (121.0, 15.0, 832.0)),
                                      weapon_bones=('gunA_tilt_l', 'gunB_tilt_l', 'missle_l')),
    # Player jets (src/playerjet.cpp kKinds): the fighter in the interceptor's dark bomber501_2 (16 m across),
    # the strike jet in the elevon bomber (25 m across); empty until the player boards them.
    'edf6tr_pjet_fighter_mission': Jet(7201.0, 1400.0, _ARMS, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                       'EDF6VC_INTERCEPTOR.MRAB', 'bomber501', rigid=((0.0, 0.22, 1.69), (1.3, 1.04, 8.45)),
                                       player=True, camera=(0.0, 6.0, -24.0)),
    'edf6tr_pjet_strike_mission': Jet(7202.0, 2200.0, _ARMS, player=True, camera=(0.0, 8.0, -32.0)),
}
JET_MODEL = ['app:/object/bomber501.mrab', 'bomber501.mdb']
# The bomber with elevon bones (tools/make_jets.py writes it): the jets use it when it is installed.
JET_ELEVON_FILE = 'EDF6VC_JET.MRAB'
JET_ELEVON_MODEL = ['app:/object/edf6vc_jet.mrab', 'bomber501.mdb']
JET_ROOT_BONE = 'mdl'
# The V506 MAB block's locator parent names (UTF-16, block offsets), shortened in place to JET_ROOT_BONE:
# the bomber has only `mdl` and `bomber501` (docs/jet-model-re.md §1, §3.3).
JET_MAB_BONES = ((0x360, 'body'), (0x372, 'rotor'), (0x37E, 'tailRotor'))
# The fourth parent name, the root, stays: (0x36A, 'mdl') has no room for a longer name, so every jet model's
# root bone is JET_ROOT_BONE (pylib/jet_models.py renames the drone's `pd607_Drone_airstrike`). A model
# without it leaves the riding-position locators (vehicle_riding_position) without a parent: the vehicle
# init (0x62B430, from 0x629450) then reads a null locator (EDF+0x62B619), CreateObject comes back with a
# half-made vehicle, and its first crash step reads a dead effect never set up (EDF+0x5F866C; 2026-10-03,
# a carrier's drone).
JET_MAB_ROOT = (0x36A, JET_ROOT_BONE)
# Fuselage only (half extents; the 25 m wingspan left out so low passes do not scrape), centre as the model.
JET_RIGID_BODY = [[0.0, 0.34, 2.6], [2.0, 1.6, 13.0]]


def _rebone(v, names: set[str], to: str = JET_ROOT_BONE):
    """`v` with every string in `names` replaced by `to` (deep)."""
    if isinstance(v, list):
        return [_rebone(c, names, to) for c in v]
    return to if isinstance(v, str) and v in names else v


JET_BODY_BONE = 'bomber501'


def _jet_ragdoll(blob: bytes, body: str = JET_BODY_BONE) -> bytes:
    """The ragdoll's embedded binding SGO with every model-side bone one the bomber has.
    RagdollController::BindDependency (0x6E6A50): each animation_from_ragdoll entry looks its model bone
    up (0x6E7B98); found, the proxy's record gets the bone (+0x60, first entry wins) and the bone gets the
    proxy (+8, last entry wins). Every proxy must end up with a bone: the loop at 0x6E8280 reads each
    record's +0x60 unchecked (crashed 2026-10-03 with the V506 bone names, then with only the body proxy
    bound). So every proxy is bound to the fuselage bone, the body proxy last so it is what drives it
    (the rotor proxies spin); ragdoll_from_animation has them all follow it."""
    version, inner = sgo.read(blob)
    inner['ragdoll_from_animation'] = [[[body, e[0][1]]] + e[1:] for e in inner['ragdoll_from_animation']]
    drive: dict[str, list] = {}
    for e in inner['animation_from_ragdoll']:
        drive.setdefault(e[0][0], [[e[0][0], body]] + e[1:])   # globalSRT: a second body entry, dropped
    body = drive.pop('RagDollProxys.body')
    inner['animation_from_ragdoll'] = list(drive.values()) + [body]
    return sgo.write(version, inner)


def jet_sgo(game: Game, name: str, model: list[str] | None = None, body: str = JET_BODY_BONE,
            rigid: list[list[float]] | None = None) -> bytes:
    """`model`: the model archive and file (default JET_MODEL, the stock bomber); `body`: its mesh bone, which
    the root and the ragdoll drive; `rigid`: the collision box [centre, half extents] (default JET_RIGID_BODY).
    A jet with its own model (Jet.model) always flies it: these three come from the Jet then."""
    jet = JETS[name]
    root = anchor = JET_ROOT_BONE   # root: see JET_MAB_ROOT
    if jet.model is not None:
        model, body, rigid = list(jet.model), jet.body, [list(x) for x in jet.rigid] if jet.rigid else None
        anchor = jet.anchor
    version, m = sgo.read(game.read('OBJECT', jet.stock + '.SGO'))
    at, want = JET_MAB_ROOT
    if m['animation_model'][2][at:at + 2 * len(want) + 2] != want.encode('utf-16le') + b'\0\0':
        raise ValueError(f'V506 MAB 的根骨骼名不在 {at:#x}')
    if 'vehicle_setup' not in m or 'mission_setup' in m:
        raise ValueError('V506_HELI 没有 vehicle_setup')
    setup = m.pop('vehicle_setup')
    setup[1][0] = jet.mark
    stock = {w[0]: w for w in setup[3]}   # each weapon keeps its stock per-weapon parameters
    setup[3] = [stock.get(w, [w, [0.0001, 0.1]]) for w in jet.weapons] + [stock['app:/weapon/v_fuel01.sgo']]
    m['mission_setup'] = setup
    if jet.player:
        import copy
        m['vehicle_setup'] = copy.deepcopy(setup)
        cam = m['game_object_camera_setting']
        if jet.camera is not None:
            m['game_object_camera_setting'] = [cam[0], [float(x) for x in jet.camera]]
    m['game_object_durability'] = jet.durability
    model_ref = model
    model = m['animation_model']
    mab = model[2]
    for at, old in JET_MAB_BONES:
        mab = sgo.replace_utf16(mab, at, old, anchor)
    m['animation_model'] = [list(JET_MODEL if model_ref is None else model_ref), model[1], mab]
    m['animation_model_bone_mapping'] = [root, body]
    bones = {'body', 'rotor', 'tailRotor'}
    m['vehicle_weapon_setting'] = [[b, 0] for b in (jet.weapon_bones or (anchor,) * len(jet.weapons))] + [[anchor, -1]]
    m['vehicle_dead_effect'] = _rebone(m['vehicle_dead_effect'], bones, anchor)
    m['roter_contact_damage_scale'] = 0.0
    m['heli_contact_damage_scale'] = 0.0005
    rb = m['heli_rigid_body']
    box = JET_RIGID_BODY if rigid is None else rigid
    m['heli_rigid_body'] = [box[0], box[1], rb[2]]
    rag = m['ragdoll']
    m['ragdoll'] = [rag[0], _jet_ragdoll(rag[1], body)]
    return sgo.write(version, m)


def as_mission_sgo(data: bytes) -> bytes:
    """A call-in vehicle SGO turned into a script-placeable one: its `vehicle_setup` name becomes
    `mission_setup` (same length, same value layout) and the name table is re-sorted. Little-endian SGO:
    header {count, data offset, name count, name table offset} at 8, names {string offset from the entry,
    member index}. DSGO: see _sort_dsgo_names."""
    old, new = 'vehicle_setup'.encode('utf-16le') + b'\0\0', 'mission_setup'.encode('utf-16le') + b'\0\0'
    if data[:4] not in (b'SGO\0', b'DSGO'):
        raise ValueError('不是小端 SGO / DSGO')
    if data.count(old) != 1 or new in data:
        raise ValueError('vehicle_setup 不唯一或已有 mission_setup')
    buf = bytearray(data.replace(old, new))
    if data[:4] == b'DSGO':
        _sort_dsgo_names(buf)
        return bytes(buf)
    _, _, name_count, name_off = struct.unpack_from('<4I', buf, 8)
    entries = []
    for i in range(name_count):
        p = name_off + i * 8
        rel, idx = struct.unpack_from('<iI', buf, p)
        entries.append((_utf16_at(buf, p + rel), p + rel, idx))
    for i, (_, at, idx) in enumerate(sorted(entries)):
        p = name_off + i * 8
        struct.pack_into('<iI', buf, p, at - p, idx)
    return _without_ai_obstacle(bytes(buf))


def _without_ai_obstacle(data: bytes) -> bytes:
    """`data` without its `ai_obstacle` member. A script-placed vehicle is AI-driven, and only then does the
    game (EDF+62C990, under EDF+6747F2's flag test) look each ai_obstacle name up in the vehicle's collision
    bodies (+0xE40) and read the result without a null check. The call-in Grape 401 lists one the placed
    vehicle does not have (2026-10-04: EXCEPTION at EDF+62CB9C, rbx=5 entries, r15=4); the stock game never
    places a 401 from a script, so it never hit it. Without the member the loop is skipped (as for the 502,
    which has none). SGOs without it come back unchanged."""
    version, m = sgo.read(data)
    if 'ai_obstacle' not in m:
        return data
    del m['ai_obstacle']
    return sgo.write(version, m)


def _sort_dsgo_names(buf: bytearray) -> None:
    """Re-sort the top-level dictionary's name table of a DSGO (see pylib/dsgo.py): node 0 at the node
    table is that dictionary, {name table offset, name count, ...} at node + value; names are {string
    offset from the entry, member position}, kept sorted like the stock files."""
    table = struct.unpack_from('<I', buf, 4)[0]
    raw, typ = struct.unpack_from('<QI', buf, table)
    if typ != 3:
        raise ValueError('DSGO 顶层不是字典')
    d = table + raw
    name_off, name_count = struct.unpack_from('<II', buf, d)
    entries = []
    for k in range(name_count):
        e = d + name_off + k * 8
        so, member = struct.unpack_from('<II', buf, e)
        entries.append((_utf16_at(buf, e + so), e + so, member))
    for k, (_, at, member) in enumerate(sorted(entries)):
        e = d + name_off + k * 8
        struct.pack_into('<II', buf, e, at - e, member)


def _utf16_at(buf: bytes, off: int) -> str:
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16le')


def object_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'OBJECT')


def weapon_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'WEAPON')



def jet_guns(game: Game) -> dict[str, bytes]:
    """The jets' guns (JET_GUN_FILES): the stock gatling with JET_GUN_SPEED and JET_GUN_ALIVE; and the
    blast drones' charges (JET_BLAST_FILES)."""
    out = {}
    for name, stock in JET_GUN_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', stock))
        r = doc.root
        if r.get('AmmoClass') != 'SolidBullet01' or r.get('AmmoSpeed') * r.get('AmmoAlive') >= JET_GUN_REACH:
            raise ValueError(f'{stock} 不是预期的直升机机炮')
        r.set('AmmoSpeed', JET_GUN_SPEED)
        r.set('AmmoAlive', JET_GUN_ALIVE)
        out[name] = dsgo.write(doc)
    for name, (damage, radius) in JET_BLAST_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', JET_BLAST_STOCK))
        r = doc.root
        cp = r.get('Ammo_CustomParameter')
        if r.get('AmmoClass') != 'GrenadeBullet01' or len(cp.items) != 6:
            raise ValueError(f'{JET_BLAST_STOCK} 不是预期的直升机炸弹')
        cp.items[0], cp.items[3], cp.items[5] = 1.0, 0.0, 0.0
        for key, value in (('AmmoCount', 1.0), ('FireCount', 1.0), ('FireBurstCount', 1.0), ('FireInterval', 1.0),
                           ('AmmoSpeed', 0.01), ('AmmoGravityFactor', 0.0), ('AmmoAlive', JET_BLAST_ALIVE),
                           ('AmmoDamage', damage), ('AmmoExplosion', radius)):
            r.set(key, value)
        out[name] = dsgo.write(doc)
    return out


# The teleportation ships' portal laser (src/carrierlaser.cpp): two DemoIndirectFire objects (the class of
# the missions' DEMOSATELLITELASER*: an IndirectFireControl at +0x170 that fires indirect_fire_param's
# rounds at its own position, docs/carrier-laser-re.md), made from DEMOSATELLITELASER18.SGO. The plugin
# fires them from the ship's hatch (IFC +0x2F9 / +0x300) at its target and sets their damage itself.
# indirect_fire_param (index: meaning, from the IFC's parser 0x2B5F40): 2 rounds, 3 frames between rounds,
# 4 bullet class, 5 speed (m a frame), 7 beam size, 9 hit impulse, 10 life (frames), 11 penetrates,
# 12 colour, 14 explosion, 15 frames before the first round, 16 fire sound looped, 17 fire sound, 18 hit sound.
PORTAL_LASER_STOCK = 'DEMOSATELLITELASER18.SGO'
# name -> (rounds, gap, size, life, colour, fire sound once)
PORTAL_LASER_FILES: dict[str, tuple[int, int, float, int, tuple[float, float, float, float], bool]] = {
    # The aim light: a thin red beam, a round every frame living 6 (so it follows the aim), 12.5 s of rounds at
    # most (the charge is 12 s, src/carrierlaser.cpp kChargeMs; the plugin ends it sooner), no damage (the
    # plugin sets 0).
    'EDF6VC_PORTAL_SIGHT.SGO': (750, 0, 1.5, 6, (3.0, 0.15, 0.1, 1.0), True),
    # The main shot: one wide violet beam living 45 frames (0.75 s); its damage is CarrierLaserDamage.
    'EDF6VC_PORTAL_LASER.SGO': (1, 0, 8.0, 45, (2.5, 0.4, 3.0, 1.0), False),
}


def portal_lasers(game: Game) -> dict[str, bytes]:
    """The portal laser's two DemoIndirectFire SGOs (PORTAL_LASER_FILES)."""
    out = {}
    for name, (rounds, gap, size, life, colour, once) in PORTAL_LASER_FILES.items():
        version, m = sgo.read(game.read('OBJECT', PORTAL_LASER_STOCK))
        p = m['indirect_fire_param']
        if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
                or p[4] != 'LaserBullet02'):
            raise ValueError(f'{PORTAL_LASER_STOCK} 不是预期的卫星激光')
        p[2], p[3], p[7], p[9], p[10] = rounds, gap, size, 0.0, life
        p[12] = list(colour)
        p[14], p[15], p[16] = 0, 0, 0
        if isinstance(p[17], list) and p[17]:
            p[17][0] = 1.0 if once else 0   # 1: the fire sound once for all rounds (the player's satellite)
        m['indirect_fire_damage'] = 0.0     # the plugin sets the damage (IFC +0xDC)
        out[name] = sgo.write(version, m)
    return out
