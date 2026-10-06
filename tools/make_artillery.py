"""Writes the self-propelled artillery (pylib/vcobjects.py GROUND_VEHICLES['artillery']) into <game>/Mods:

  Mods/OBJECT/EDF6VC_ARTILLERY.MRAB    the user's twin-gun tank model (models/twin_tank/twin_tank.obj: an E551 hull,
                                       a Balam turret and barrels with its texture) on the KG6 Kepler's skeleton
                                       (pylib/artillery_model.py; the chassis is the stock E551's own meshes,
                                       materials and textures from the player's own Root.cpk). Without the model
                                       folder it is not made, and the vehicle keeps the Kepler's own model.
  Mods/OBJECT/EDF6VC_ARTILLERY_RAGDOLL.SHKT
                                       the Kepler's ragdoll (its physics: turret, guns and wheels are Havok bodies on
                                       joints) refitted to that model's bones (pylib/ragdoll_fit.py). With the stock one
                                       the turret's hinge stayed 0.68 m from the moved turret bone and the turret could
                                       not turn (docs/artillery-re.md). Made with the model.
  Mods/OBJECT/EDF6VC_ARTILLERY.SGO     the Kepler's vehicle (Vehicle603_Flak: its turret, its twin guns) with that model,
                                       that ragdoll (its binding refitted too) and the guns below
  Mods/WEAPON/EDF6VC_HOWITZER_L/R.SGO  the Kepler's guns made howitzers: a large high-explosive shell each on a ballistic
                                       arc (GrenadeBullet01, impact fuse), both firing together (two shells a salvo),
                                       a slow reload; LockonTargetType kMarkGround (EDF6AutoTurret aims them at ground
                                       targets on the shells' arc). Shell and ejected casing sized to the model's
                                       bore (BORE).

The request is tools/calls.py EDF6VC_CALL_ARTILLERY (tools/call_weapons.py, the Kepler's request as its template).
Built in memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_artillery.py [game dir]            write / refresh
  python tools/make_artillery.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import artillery_model  # noqa: E402
import dsgo  # noqa: E402
import ledger  # noqa: E402
import ragdoll_fit  # noqa: E402
import vcobjects as vc  # noqa: E402
import graft_pure  # noqa: E402
from mdb import Mdb, bind_world, mdb_read, rab_read  # noqa: E402

OWNER = 'artillery'   # pylib/ledger.py
VEHICLE = vc.GROUND_VEHICLES['artillery']
SGO_FILE = f'{VEHICLE.sgo}.SGO'
MODEL_FILE = f'{VEHICLE.sgo}.MRAB'
MODEL_MDB = 'v603_flak.mdb'   # the host's own model file name, inside the archive
RAGDOLL_FILE = f'{VEHICLE.sgo}_RAGDOLL.SHKT'
RAGDOLL = f'app:/object/{RAGDOLL_FILE.lower()}'
STOCK_RAGDOLL = 'app:/object/Ragdoll_v603_flak.shkt'
STOCK_GUNS = ('V603_FLAK_GUN01_L.SGO', 'V603_FLAK_GUN01_R.SGO')
MARK_GROUND = 7302.0   # EDF6AutoTurret's ground-attack mark (autoturret/src/turret.h kMarkGround)
SHELL_MODEL = 'app:/WEAPON/bullet_grenade.rab'
# The camera (game_object_camera_setting [0] look-at / [1] eye, pylib/vcobjects.py camera_view): the Kepler's level
# (0, 4, 0) / (0, 4, -15.5) raised over the turret, looking down onto the ground ahead.
CAMERA = ([0.0, 4.5, 4.0], [0.0, 9.0, -18.0])
# The calibre: the twin tank's muzzle mouth, inscribed diameter (artillery_model.Barrel.bore, 0.350 m; build() checks
# the model still has it). The shell (bullet_grenade.rab: 0.063 m across, 0.139 m long at AmmoSize 1, scaled
# uniformly; the stock Barga / E551 DLC cannons use it at 5) is 90 % of the bore across, and its contact sphere
# (AmmoSize x AmmoHitSizeAdjust) stays the 1.2 m it was. The casing the gun throws out (the Kepler gun's ShellCase:
# ShellCase401l.rab, 0.070 m across, 0.206 m long, a Havok body) is scaled by ShellCase_CustomParameter {scale}
# (stock: the Galleon cannons' 2.0) to the same 90 % of the bore.
BORE = 0.35
SHELL_SIZE = round(0.9 * BORE / 0.063, 2)          # 5.0
CASE_SCALE = round(0.9 * BORE / 0.070, 2)          # 4.5
HIT_SPHERE = 1.2
CASE_MODEL = 'app:/Weapon/ShellCase401l.rab'
# A 155 mm-class shell cut to the game's world: ~240 m/s off the muzzle on a ballistic arc (about 4 km at 45 deg in the
# game's gravity: the world's whole width), a 25 m blast; one a gun every 5 s, both guns together. Base values the
# request's tier multiplies.
SHELLS = {
    'AmmoClass': 'GrenadeBullet01', 'AmmoModel': SHELL_MODEL, 'AmmoSpeed': 4.0, 'AmmoGravityFactor': 1.0,
    'AmmoAlive': 1200.0, 'AmmoOwnerMove': 0.0, 'AmmoDamage': 2500.0, 'AmmoExplosion': 25.0, 'AmmoHitImpulseAdjust': 0.5,
    'AmmoSize': SHELL_SIZE, 'AmmoHitSizeAdjust': round(HIT_SPHERE / SHELL_SIZE, 4), 'AmmoIsPenetration': 0.0,
    'AmmoCount': 40.0,
    'FireCount': 1.0, 'FireBurstCount': 1.0, 'FireInterval': 300.0, 'FireAccuracy': 0.01, 'FireRecoil': 2.0,
    'AmmoColor': [3.0, 2.0, 1.0, 1.0],
    # GrenadeBullet01: [0] 0 = burst on impact, [3] 0 = no bounce, [4] / [5] its trail (param, frames).
    'Ammo_CustomParameter': [0.0, -0.004, 1.0, 0.0, 0.05, 20.0],
    'AmmoHitSe': [0.0, 'common_damages_explode_L', 1.0, 1.0, 1.0, 400.0],
    'FireSe': [0.0, 'weapon_VHC_tank403_cannon', 1.0, 1.0, 1.0, 120.0],
    # EDF6AutoTurret's gun lock-on (autoturret/tools/build.py gun_lockon).
    'LockonType': 0.0, 'LockonTargetType': MARK_GROUND, 'LockonRange': 0.0, 'Lockon_DistributionType': 0.0,
}
NAMES = {'ja': '榴弾砲', 'en': 'Howitzer', 'cn': '榴彈砲', 'kr': 'Howitzer', 'sc': '榴弹炮'}
# The turret is fixed (the user, 2026-10-06: "the twin tank's turret should not turn"): the guns elevate, the hull is
# turned to lay them. car_base_constraint_data's turret entry ['cannon_main', 'body', 2, [axis, [_, limits, motor]]]:
# axis 1 = yaw; limits [0] = none (the Kepler's: a full circle) or [1, min, max] degrees, as the stock RoboTruck's spine
# [1, -170, 170] and every gun's elevation [1, -60, 5]. The same entry is where the seat's yaw axis takes its stops
# (EDF.dll 0x6212B0 / 0x621280 -> 0x669BA0: aim axis 0 from constraint 0 via 0x5EC4A0, +-pi when unlimited; 0x5FBC00
# clamps the axis to them), so [1, 0, 0] holds both the hinge and the aim at straight ahead: no input, plugin or AI turns it.
TURRET_CONSTRAINT = ('cannon_main', 'body')
TURRET_LIMITS = [1.0, 0.0, 0.0]
# The guns' locators (the Kepler gun SGO's animation_model MAB, the vec4s of its locator block, offsets from the gun's
# bone cannon_slide_l / _r): the muzzle (0, 0, 2.894): the slide bone sits at the Kepler's tube root (z 1.007), its mouth
# 2.9 m ahead (the E551's cannon the same way: (0, 0, 2.8) from its tube root 2.777 to the mouth 5.60); the casing's
# ejection (+-0.185, 0.322, -1.451): 0.32 m over the trunnion. The twin tank's slide bone is on the mouth plane, so as
# stock the shell left 2.9 m in front of the barrel and the casing jumped out of the tube's middle: both are rewritten
# from the model (gun_mount).
STOCK_MUZZLE = (0.0, 0.0, 2.894)
STOCK_EJECT = (0.185, 0.322, -1.451)       # x: + on the left gun, - on the right
EJECT_AHEAD = 0.05                         # m ahead of the turret's front face: the breech behind it is inside the turret


def _node(v: object) -> object:
    """A python value as dsgo writes it: lists as Nodes, ints as floats."""
    if isinstance(v, (list, tuple)):
        return dsgo.Node([_node(x) for x in v])
    if isinstance(v, int) and not isinstance(v, bool):
        return float(v)
    return v


Vec3 = tuple[float, float, float]


def mab_point(mab: bytes, near: Vec3) -> int:
    """Offset of the one locator point (x, y, z, 1) within 1e-3 of `near` (x by magnitude: the guns mirror it) in the
    MAB block's locator area (u32 at 0x1C .. u32 at 0x20, pylib/vcobjects.py mab_locator). ValueError unless exactly one."""
    if mab[:4] != b'MAB\0':
        raise ValueError('不是 MAB 块')
    lo, hi = struct.unpack_from('<2I', mab, 0x1C)
    hits = [at for at in range(lo, min(hi, len(mab)) - 15, 4)
            if struct.unpack_from('<f', mab, at + 12)[0] == 1.0
            and all(abs(abs(v) - abs(w)) < 1e-3 if c == 0 else abs(v - w) < 1e-3
                    for c, (v, w) in enumerate(zip(struct.unpack_from('<3f', mab, at), near)))]
    if len(hits) != 1:
        raise ValueError(f'炮的 MAB 里坐标 {near} 的定位点有 {len(hits)} 个')
    return hits[0]


def gun_points(mab: bytes) -> tuple[Vec3, Vec3]:
    """(muzzle, casing ejection) locators of a Kepler gun's MAB, from its slide bone."""
    return tuple(struct.unpack_from('<3f', mab, mab_point(mab, p)) for p in (STOCK_MUZZLE, STOCK_EJECT))  # type: ignore[return-value]


def gun_mount(md: Mdb, barrel: artillery_model.Barrel) -> tuple[Vec3, Vec3]:
    """(muzzle, casing ejection) of the model's gun `barrel` from its slide bone: the muzzle the barrel's mouth; the
    casing STOCK_EJECT's height over the gun's axis, on the axis, EJECT_AHEAD in front of the trunnion (the turret's
    front face: the breech behind it is inside the turret, where a casing would be spawned inside its body)."""
    w = bind_world(md)
    slide = w[md.bone_index(f'cannon_slide_{barrel.side}')][12:15]
    eject = (barrel.pivot[0], barrel.pivot[1] + STOCK_EJECT[1], barrel.pivot[2] + EJECT_AHEAD)
    return (tuple(barrel.muzzle[c] - slide[c] for c in range(3)),  # type: ignore[return-value]
            tuple(eject[c] - slide[c] for c in range(3)))


def howitzer_sgo(game: vc.Game, stock: str, mount: tuple[Vec3, Vec3] | None = None) -> bytes:
    """The Kepler gun `stock` made a howitzer (SHELLS, the casing to BORE); `mount`: its (muzzle, casing ejection)
    from its slide bone on the twin tank's model (gun_mount; None: the Kepler's own, for the Kepler's own model)."""
    doc = dsgo.parse(game.read('WEAPON', stock))
    r = doc.root
    if mount is not None:
        model = r.get('animation_model')
        mab = bytearray(model.items[2].data)
        for near, point in zip((STOCK_MUZZLE, STOCK_EJECT), mount):
            struct.pack_into('<3f', mab, mab_point(bytes(mab), near), *point)
        model.items[2] = dsgo.Blob(bytes(mab), model.items[2].kind)
    if r.get('xgs_scene_object_class') != 'Weapon_VehicleShoot':
        raise ValueError(f'{stock} 不是预期的 Kepler 炮')
    for key, value in SHELLS.items():
        if r.get(key) is None:
            raise ValueError(f'{stock} 缺少 {key}')
        r.set(key, _node(value))
    if dsgo.to_py(r.get('ShellCase'))[1] != CASE_MODEL:
        raise ValueError(f'{stock} 的弹壳不是 {CASE_MODEL}')
    r.set('ShellCase_CustomParameter', dsgo.Node([CASE_SCALE], {0: 'scale'}))
    res = r.get('resource')
    if SHELL_MODEL not in res.items:
        res.items.append(SHELL_MODEL)
    for lang, name in NAMES.items():
        if r.get(f'name.{lang}') is not None:
            r.set(f'name.{lang}', name)
    return dsgo.write(doc)


def ragdoll(game: vc.Game, skeleton: Mdb) -> tuple[bytes, bytes]:
    """(the ragdoll tagfile, the SGO's binding blob): the Kepler's refitted to `skeleton` (the model's bones:
    artillery_model.skeleton or the built model), every body where its bone now is and every joint with its body
    (pylib/ragdoll_fit.py). The Kepler's ragdoll is checked against the Kepler's own model first."""
    rag = dsgo.parse(game.read('OBJECT', f'{VEHICLE.stock}.SGO')).root.get('ragdoll')
    if str(rag.items[0]).lower() != STOCK_RAGDOLL.lower():
        raise ValueError(f'{VEHICLE.stock}.SGO 的 ragdoll 不是预期的 {STOCK_RAGDOLL}')
    stock = mdb_read(next(f for f in rab_read(game.read('OBJECT', 'V603_FLAK.MRAB')).files
                          if f.name.lower() == MODEL_MDB).data)
    shkt = game.read('OBJECT', STOCK_RAGDOLL.rsplit('/', 1)[1].upper())
    return ragdoll_fit.fit(skeleton, shkt, rag.items[1].data, stock)


def vehicle_sgo(game: vc.Game, own_model: bool | None = None, blob: bytes | None = None) -> bytes:
    """The vehicle: the Kepler's, with this tool's model and ragdoll when `own_model` (else the Kepler's own), and
    its guns. `blob`: the refitted ragdoll's binding (ragdoll(); None: worked out from the model's skeleton). None
    `own_model`: as install writes it (the model with the twin tank's folder there): what the test range places
    (testrange/gen.py GROUND_MISSION calls this with the game alone)."""
    folder = artillery_model.model_dir()
    if own_model is None:
        own_model = folder is not None
    doc = dsgo.parse(game.read('OBJECT', f'{VEHICLE.stock}.SGO'))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError(f'{VEHICLE.stock}.SGO 不是 Vehicle603_Flak')
    if own_model:
        model = r.get('animation_model')
        model.items[0].items[0] = f'app:/Object/{MODEL_FILE.lower()}'
        model.items[0].items[1] = MODEL_MDB
        model.items[1] = f'app:/object/{artillery_model.OUT_CAS.lower()}'
        if blob is None:
            if folder is None:
                raise ValueError('没有双管坦克模型，无法生成它的物理骨架')
            blob = ragdoll(game, artillery_model.skeleton(game, folder))[1]
        rag = r.get('ragdoll')
        rag.items[0] = RAGDOLL
        rag.items[1] = dsgo.Blob(blob, rag.items[1].kind)
    setup = r.get('vehicle_setup')
    guns = setup.items[2].items
    if len(guns) != len(VEHICLE.weapons):
        raise ValueError(f'{VEHICLE.stock}.SGO: {len(guns)} guns, the artillery has {len(VEHICLE.weapons)}')
    for entry, path in zip(guns, VEHICLE.weapons):
        entry.items[0] = path
    cam = r.get('game_object_camera_setting')
    cam.items[0], cam.items[1] = _node(CAMERA[0]), _node(CAMERA[1])
    r.set('game_object_durability', VEHICLE.durability)
    lock_turret(r)
    return dsgo.write(doc)


def turret_constraint(r: dsgo.Node) -> dsgo.Node:
    """The [axis, [_, limits, motor]] node of the turret's car_base_constraint_data entry (TURRET_CONSTRAINT)."""
    hits = [e for e in r.get('car_base_constraint_data').items if tuple(e.items[:2]) == TURRET_CONSTRAINT]
    if len(hits) != 1 or hits[0].items[3].items[0] != 1.0:
        raise ValueError(f'{VEHICLE.stock}.SGO: 炮塔约束 {TURRET_CONSTRAINT} 不是一条偏航铰链')
    return hits[0].items[3]


def lock_turret(r: dsgo.Node) -> None:
    """The turret's hinge and the seat's yaw axis both stopped at straight ahead (TURRET_LIMITS); refuses a stock
    entry that is not the Kepler's unlimited one."""
    joint = turret_constraint(r)
    if dsgo.to_py(joint.items[1].items[1]) != [0.0]:
        raise ValueError(f'{VEHICLE.stock}.SGO: 炮塔约束已经有限位 {dsgo.to_py(joint.items[1].items[1])}')
    joint.items[1].items[1] = _node(TURRET_LIMITS)


def check(files: dict[str, bytes], game: vc.Game | None = None) -> None:
    """The SGO names this tool's model and ragdoll when it made them (else the Kepler's) and its guns; its turret is
    fixed (TURRET_LIMITS); the ragdoll and its binding agree with the model's bones (ragdoll_fit.problems: every body
    at its bone, every joint where both its bodies hold it); the model has the guns' bones and the track materials /
    parameter the SGO scrolls; the guns are marked, their shells and casings sized to BORE. With `game` (the stock
    guns to compare with): each gun's MAB is the stock one but for its muzzle and ejection points, which on the twin
    tank's model put the muzzle on its barrel's mouth and the casing outside the turret body by its trunnion
    (gun_problems)."""
    root = dsgo.parse(files[f'OBJECT/{SGO_FILE}']).root
    v = dsgo.to_py(root)
    own = f'OBJECT/{MODEL_FILE}' in files
    if own:
        assert v['animation_model'][1] == f'app:/object/{artillery_model.OUT_CAS.lower()}'
        assert f'OBJECT/{artillery_model.OUT_CAS}' in files, 'the retargeted artillery animation is missing'
    assert dsgo.to_py(turret_constraint(root).items[1].items[1]) == TURRET_LIMITS, 'the turret can still turn'
    md = None
    assert own == (f'OBJECT/{RAGDOLL_FILE}' in files), 'the model and its ragdoll come together'
    want = [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB] if own else ['app:/Object/v603_flak.mrab', MODEL_MDB]
    assert v['animation_model'][0] == want, v['animation_model'][0]
    assert v['ragdoll'][0].lower() == (RAGDOLL if own else STOCK_RAGDOLL).lower(), v['ragdoll'][0]
    assert [w[0] for w in v['vehicle_setup'][2]] == list(VEHICLE.weapons)
    cam = v['game_object_camera_setting']
    assert [cam[0], cam[1]] == [list(CAMERA[0]), list(CAMERA[1])], cam
    vc.check_artillery_camera(CAMERA)
    if own:
        md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{MODEL_FILE}']).files
                           if f.name.lower() == MODEL_MDB.lower()).data)
        bad = ragdoll_fit.problems(md, files[f'OBJECT/{RAGDOLL_FILE}'], root.get('ragdoll').items[1].data)
        assert not bad, "the ragdoll is not the model's: " + '; '.join(bad)
        missing = {b for b, _ in v['vehicle_weapon_setting']} - {md.name_of(b.name) for b in md.bones}
        assert not missing, f'model lacks gun bones {missing}'
        # the tracks scroll: every (material, parameter) the SGO's tank_caterpillar_animation animates is in the model
        params = {(md.name_of(m.name), p.name) for m in md.materials for p in m.params}
        scroll = [tuple(x) for x in v['tank_caterpillar_animation']]
        assert scroll and not set(scroll) - params, f'model lacks the scrolled track materials {set(scroll) - params}'
    if game is not None:
        if own:
            assert files[f'OBJECT/{artillery_model.OUT_CAS}'] == artillery_model.animation(game, md), 'stale artillery animation'
        bad = gun_problems(files, game, md)
        assert not bad, 'gun mounts: ' + '; '.join(bad)
    for w in VEHICLE.weapons:
        g = dsgo.to_py(dsgo.parse(files[f'WEAPON/{w.split("/")[-1].upper()}']).root)
        assert g['LockonTargetType'] == MARK_GROUND and g['LockonType'] == 0.0 and g['AmmoClass'] == 'GrenadeBullet01', g
        assert abs(g['AmmoSize'] * 0.063 - 0.9 * BORE) < 0.01, g['AmmoSize']
        assert abs(g['AmmoSize'] * g['AmmoHitSizeAdjust'] - HIT_SPHERE) < 1e-3, g['AmmoHitSizeAdjust']
        assert g['ShellCase'][1] == CASE_MODEL, g['ShellCase']
        assert abs(g['ShellCase_CustomParameter']['scale'] * 0.070 - 0.9 * BORE) < 0.01, g['ShellCase_CustomParameter']


def gun_problems(files: dict[str, bytes], game: vc.Game, md: Mdb | None) -> list[str]:
    """What breaks "each gun's MAB is the stock Kepler gun's but for its two points, which put the shell on the
    mouth of `md`'s barrel and the casing outside its turret body at its trunnion" (`md` None: the Kepler's own model,
    the stock MAB unchanged)."""
    out: list[str] = []
    w = bind_world(md) if md is not None else None
    pts = graft_pure.skinned_points(md) if md is not None else {}
    body = artillery_model.turret_tris(md) if md is not None else []
    for stock, path, side in zip(STOCK_GUNS, VEHICLE.weapons, 'lr'):
        ref = dsgo.parse(game.read('WEAPON', stock)).root.get('animation_model').items[2].data
        mab = dsgo.parse(files[f'WEAPON/{path.split("/")[-1].upper()}']).root.get('animation_model').items[2].data
        at = [mab_point(ref, p) for p in (STOCK_MUZZLE, STOCK_EJECT)]
        spans = {k for a in at for k in range(a, a + 12)}
        if len(mab) != len(ref) or any(mab[k] != ref[k] for k in range(len(ref)) if k not in spans):
            out.append(f'{path}: its MAB differs from {stock} beyond the muzzle and ejection points')
            continue
        muzzle, eject = (struct.unpack_from('<3f', mab, a) for a in at)
        if md is None:
            if mab != ref:
                out.append(f'{path}: the Kepler model keeps the stock gun points')
            continue
        slide = w[md.bone_index(f'cannon_slide_{side}')][12:15]
        pivot = w[md.bone_index(f'cannon_{side}')][12:15]
        lo, hi = artillery_model.box(pts[md.bone_index(f'cannon_slide_{side}')])
        mouth = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2])
        shot = tuple(slide[c] + muzzle[c] for c in range(3))
        case = tuple(slide[c] + eject[c] for c in range(3))
        if max(abs(shot[c] - mouth[c]) for c in range(3)) > 0.01:
            out.append(f"{path}: the shell leaves at {shot}, its barrel's mouth is at {mouth}")
        if artillery_model.inside(body, case) or not pivot[2] < case[2] < pivot[2] + 0.2:
            out.append(f'{path}: the casing is thrown out at {case}, not just ahead of the trunnion {tuple(pivot)} '
                       'outside the turret')
    return out


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes (including the model's private CAS), {path under Mods: bytes}, checked, from the game's Root.cpk (only read) and the
    twin tank's model folder (artillery_model.model_dir(); without it no model is made, with a message)."""
    game = vc.Game(root)
    folder = artillery_model.model_dir()
    files: dict[str, bytes] = {}
    blob = None
    if folder is None:
        print('  没有找到双管坦克模型（models/twin_tank/twin_tank.obj），自行榴弹炮先用 Kepler 原版外形。')
    else:
        arc, md, info = artillery_model.build_with_info(game, folder)
        artillery_model.check(arc)
        for b in info['barrels'].values():
            if abs(b.bore - BORE) > 0.02:
                raise ValueError(f'炮口内径 {b.bore:.3f} m，炮弹按 {BORE} m 设计：请更新 BORE')
        files[f'OBJECT/{MODEL_FILE}'] = arc
        files[f'OBJECT/{artillery_model.OUT_CAS}'] = artillery_model.animation(game, md)
        files[f'OBJECT/{RAGDOLL_FILE}'], blob = ragdoll(game, md)
    files[f'OBJECT/{SGO_FILE}'] = vehicle_sgo(game, folder is not None, blob)
    for stock, path, side in zip(STOCK_GUNS, VEHICLE.weapons, 'lr'):
        mount = None if folder is None else gun_mount(md, info['barrels'][side])
        files[f'WEAPON/{path.split("/")[-1].upper()}'] = howitzer_sgo(game, stock, mount)
    check(files, game)
    return files


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    for path in install(root, build(root)):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
