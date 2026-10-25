"""Writes the Dreadnought (src/dreadnought.cpp, docs/dreadnought-plan.md, pylib/vcobjects.py JETS[DREADNOUGHT_JET]) and its
hangar's fighters into <game>/Mods:

  Mods/OBJECT/EDF6VC_DN_<MODEL>.MRAB            the dreadnought and the four fighters: the model folder
                                                (tools/prep_dreadnought.py) on the V506 frame (pylib/dreadnought_model.py)
  Mods/OBJECT/EDF6VC_DN_<MODEL>_AIRFRAME.SHKT   each one's collision, measured off its model (pylib/aircraft_collision.py)

The dreadnought carries the hangar modules the plugin's ini fits (pylib/dreadnought_model.py HANGAR_KEYS, the installer
reads them from the ini it leaves: tools/installer.py; the plugin runs the hangars its model has): a module not fitted is
in neither its model nor its collision. Changing them takes a run of the installer.
  Mods/OBJECT/EDF6VC_DREADNOUGHT.SGO            the V506 body (vcobjects.jet_sgo) with the dreadnought's model, mark,
                                                durability and arms (the submarine carrier's: tools/make_jets.py writes
                                                them), its door beside it
  Mods/OBJECT/EDF6VC_DN_<FIGHTER>.SGO           the fighters, NPC jets (src/jet_internal.h kBodies Body::dn*)
  Mods/OBJECT/EDF6VC_DN_ROUND_<GUN>.SGO         its turrets' rounds, their emplacements' (TURRET_ROUNDS, src/jet_bay.cpp
                                                kDnRounds)

Without the model folder (pylib/obj_model.py model_dir: $EDF6VC_MODELS, `models` next to the installer, the developer's
folder) none of it can be built: the dreadnought's SGO is then the stock V506_HELI.SGO as it is, so its request
(tools/calls.py EDF6VC_CALL_DREADNOUGHT, always installed: the weapon table's rows never move) brings a plain N9 Eros
heli, which the plugin (no Dreadnought mark) leaves alone; no fighter SGO is written (the plugin's hangar then launches
none). Said once when it happens.
Built in memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_dreadnought.py [game dir]                  write / refresh
  python tools/make_dreadnought.py [game dir] --out DIR        build and check only, the files written under DIR
  python tools/make_dreadnought.py [game dir] --remove         release them
  ... --hangars 1010                                           the modules fitted (left fore, left aft, right fore, right
                                                               aft), else as <game>/Mods/Plugins/EDF6VehicleCrew.ini has
                                                               them (all, without one)
"""
from __future__ import annotations

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import aircraft_collision  # noqa: E402
import jet_models  # noqa: E402
import make_jets  # noqa: E402
import dreadnought_model as dm  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

OWNER = 'dreadnought'   # pylib/ledger.py
JET = vc.JETS[vc.DREADNOUGHT_JET]
SGO_FILE = 'EDF6VC_DREADNOUGHT.SGO'
STOCK_SGO = 'V506_HELI.SGO'
FIGHTER_SGOS = {k: vc.dn_file(k, 'SGO') for k in vc.DREADNOUGHT_FIGHTERS}
# Its turrets' rounds (src/jet_bay.cpp kDnRounds, in tools/make_tracked_combat.py DREADNOUGHT_GUNS' order; vcobjects
# DREADNOUGHT_TURRET_GUNS): each gun's emplacement's round (GUNS['emp_<kind>']: its speed, life, blast, thickness) in its
# shell's tracer colour (EMPLACEMENT_SHELL's AmmoColor), made as the gunship cannon's round is (make_jets.gun_round: one
# SolidBullet01, fired straight, no wait; the plugin writes the damage, the emplacement's AmmoDamage).
TURRET_ROUNDS = {kind: f'EDF6VC_DN_ROUND_{kind.removeprefix("emp_").upper()}.SGO' for kind in ('emp_mid', 'emp_short', 'emp_long')}
BARREL_FIT = 0.05   # m: vcobjects.DREADNOUGHT_BARRELS against the model's guns


def turret_round(game: vc.Game, kind: str) -> make_jets.GunRound:
    """Gun `kind`'s round as its emplacement fires it (TURRET_ROUNDS)."""
    import make_tracked_combat as tc
    g = tc.GUNS[kind]
    color = sgo.load(data=game.read('WEAPON', tc.EMPLACEMENT_SHELL))['AmmoColor']
    life = int(g['AmmoAlive'])
    return make_jets.GunRound(TURRET_ROUNDS[kind], float(g['AmmoExplosion']), float(g['AmmoSpeed']), life,
                              float(g['AmmoSpeed']) * life, float(g['AmmoSize']), float(g['AmmoHitSizeAdjust']),
                              tuple(float(c) for c in color))


def check_barrels() -> None:
    """vcobjects.DREADNOUGHT_BARRELS against the model folder's guns: each turret's two barrel mouths (pylib/
    emplacement_model.py _muzzles, the emplacements' own) BARREL_FIT m of its gun's half spacing either side of its muzzle
    bone, across its guns (x), level with it."""
    import json
    import numpy as np
    import emplacement_model as em
    folder = dm.model_dir()
    with open(os.path.join(folder, 'dreadnought.json'), encoding='utf-8') as h:
        bones = json.load(h)['models']['dreadnought']['bones']
    guns = em._read(os.path.join(folder, em.SOURCE), {f'turret_{n}_gun' for n in range(vc.DREADNOUGHT_TURRETS)})
    for n, kind in enumerate(vc.DREADNOUGHT_TURRET_GUNS):
        muzzle = np.asarray(bones[f'turret_{n}_muzzle']['pivot'], dtype=np.float64)
        mouths = em._muzzles(guns[f'turret_{n}_gun'][0], muzzle)
        half = vc.DREADNOUGHT_BARRELS[kind]
        for side, sign in (('left', 1.0), ('right', -1.0)):
            off = mouths[side] - muzzle
            assert abs(off[0] - sign * half) < BARREL_FIT and abs(off[1]) < BARREL_FIT and abs(off[2]) < BARREL_FIT, \
                f'turret {n} ({kind}) {side} barrel {off.round(3)}: vcobjects.DREADNOUGHT_BARRELS says {sign * half}'


def build(root: str, hangars: tuple[bool, ...] = dm.ALL_HANGARS) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read) and the
    Dreadnought's model folder; the dreadnought with the hangar modules `hangars` fits (module m: hangars[m])."""
    game = vc.Game(root)
    folder = dm.model_dir()
    if folder is None:
        print(f'未找到无畏舰模型（{dm.MODEL_SUBDIR}/{dm.MANIFEST}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
              '无畏舰请求先叫来普通的 N9 Eros 直升机，机库没有舰载机；放好模型后重新安装即可。')
        files = {f'OBJECT/{SGO_FILE}': game.read('OBJECT', STOCK_SGO)}
    else:
        files = {}
        for model in dm.MODELS:
            arc, _info, md = dm.build_archive(game, folder, model, hangars)
            name = dm.archive_name(model)
            files[f'OBJECT/{name}'] = arc
            # its collision measured off it as built (the dreadnought: its fitted modules only), about its whole model's
            # box centre, the vehicle's position as its SGO has it (vcobjects.jet_sgo)
            files[f'OBJECT/{aircraft_collision.DN_FILES[name]}'] = aircraft_collision.airframe(
                game, md, jet_models.model_box(game, name)[0], grid=grid_of(model))
        # its turrets' gunner seats after the pilot's (vcobjects.DREADNOUGHT_TURRETS): the 410 door gunner's pose
        files[f'OBJECT/{SGO_FILE}'] = make_jets.with_passenger_seats(
            vc.jet_sgo(game, vc.DREADNOUGHT_JET), 0, vc.DREADNOUGHT_TURRETS, game.read('OBJECT', make_jets.GUNNER_STOCK))
        for kind, (name, *_rest) in vc.DREADNOUGHT_FIGHTERS.items():
            files[f'OBJECT/{FIGHTER_SGOS[kind]}'] = vc.jet_sgo(game, name)
        for kind in TURRET_ROUNDS:
            gun = turret_round(game, kind)
            files[f'OBJECT/{gun.file}'] = make_jets.gun_round(game, gun)
            make_jets.check_gun_round(files[f'OBJECT/{gun.file}'], gun)
        check_barrels()
    check(files, hangars)
    return files


def grid_of(model: str) -> tuple[int, int]:
    """Model `model`'s collision grid: the dreadnought's own (aircraft_collision.DN_GRID), its fighters' the jets'."""
    return aircraft_collision.DN_GRID if model == dm.SHIP else (aircraft_collision.NX, aircraft_collision.NZ)


def made_files() -> list[str]:
    """The paths under Mods `build` writes with the model folder, its own SGO aside (the test range has its own):
    every model's archive and airframe, the fighters' SGOs."""
    out = []
    for model in dm.MODELS:
        name = dm.archive_name(model)
        out += [f'OBJECT/{name}', f'OBJECT/{aircraft_collision.DN_FILES[name]}']
    return out + [f'OBJECT/{f}' for f in (*FIGHTER_SGOS.values(), *TURRET_ROUNDS.values())]


def check(files: dict[str, bytes], hangars: tuple[bool, ...] = dm.ALL_HANGARS) -> None:
    """With the model: every SGO names its model (and the V506's CAS) and its airframe, carries its mark, durability and
    arms, every weapon on a bone its model has; every archive passes dreadnought_model.check_archive. Without: the
    dreadnought's SGO is the stock heli's, and nothing else is written."""
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    if f'OBJECT/{dm.archive_name(dm.SHIP)}' not in files:
        assert v['animation_model'][0] == ['app:/object/v506_heli.mrab', 'v506_heli.mdb'], v['animation_model'][0]
        assert set(files) == {f'OBJECT/{SGO_FILE}'}, sorted(files)
        return
    assert JET.file == dm.archive_name(dm.SHIP) and JET.model == (f'app:/object/{JET.file.lower()}', dm.mdb_name(dm.SHIP))
    jets = {SGO_FILE: (vc.DREADNOUGHT_JET, dm.SHIP)}
    jets.update({FIGHTER_SGOS[k]: (name, k) for k, (name, *_r) in vc.DREADNOUGHT_FIGHTERS.items()})
    for file, (name, model) in jets.items():
        jet = vc.JETS[name]
        v = sgo.load(data=files[f'OBJECT/{file}'])
        assert jet.file == dm.archive_name(model) and jet.cas is None, (file, jet.file, jet.cas)
        assert v['xgs_scene_object_class'] == 'Vehicle506_Helicopter', (file, v['xgs_scene_object_class'])
        assert v['animation_model'][0] == list(jet.model) and v['animation_model'][1] == 'app:/object/v506_heli.cas', \
            (file, v['animation_model'][:2])
        setup = v['vehicle_setup'] if jet.player else v['mission_setup']
        assert setup[1][0] == jet.mark and v['game_object_durability'] == jet.durability, file
        held = [w[0] for w in setup[3] if w[0] != 'app:/weapon/v_fuel01.sgo']   # vcobjects.with_fuel puts the tank in
        assert held == list(jet.weapons), (file, setup[3])
        assert v['ragdoll'][0] == 'app:/object/' + aircraft_collision.DN_FILES[jet.file].lower(), (file, v['ragdoll'][0])
        assert f'OBJECT/{aircraft_collision.DN_FILES[jet.file]}' in files, file
        arc = files[f'OBJECT/{jet.file}']
        dm.check_archive(arc, model)
        md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == dm.mdb_name(model)).data)
        names = {md.name_of(b.name) for b in md.bones}
        missing = {b for b, _ in v['vehicle_weapon_setting']} - names
        assert not missing, f'{file}: model lacks weapon bones {missing}'
        if file == SGO_FILE:   # its box's centre the plugin's keel; a gunner seat a turret, every turret's bones in the model
            centre = float(v['heli_rigid_body'][0][1])
            assert abs(centre - vc.DREADNOUGHT_KEEL) < 0.01, f'{file}: box centre {centre} over the keel, vcobjects says {vc.DREADNOUGHT_KEEL}'
            make_jets.check_passenger_seats(files[f'OBJECT/{file}'], 1 + vc.DREADNOUGHT_TURRETS, file, 1)
            turrets = {f'dn_turret_{n}{s}' for n in range(vc.DREADNOUGHT_TURRETS) for s in ('', '_gun', '_muzzle')}
            assert turrets <= names and f'dn_turret_{vc.DREADNOUGHT_TURRETS}' not in names, sorted(turrets - names)
            check_markers(md, {k: files[f'OBJECT/{dm.archive_name(k)}'] for k in dm.BAY_KINDS}, hangars)
            check_fitting(md, hangars)


# The markers' checks (check_markers): the bridge marker stands on the collision (its cell's top within STAND m under
# it: aircraft_collision's height field is what the player stands on), its roof drawn within ROOF m of that top; the
# bridge eye sees CLEAR m ahead (level and VIEW_DOWN degrees down, VIEW_SIDE degrees either side) past every triangle.
# The bays: dn_bay_<i> a mesh bone carrying its kind's fighter (its box the kind's model's within FIT m, its origin the
# box's centre), nose outboard (port for bays 0-3, starboard 4-7); dn_bay_<i>_out the same frame straight outboard,
# its fighter's box BAY_MARGIN m larger overlapping no collision cell. Only the fitted modules' (`hangars`) bays.
STAND, ROOF, CLEAR, VIEW_DOWN, VIEW_SIDE = 0.5, 0.75, 30.0, 10.0, 30.0
FIT, BAY_MARGIN = 0.05, 1.0


def on(y: float) -> float:
    """How near a point at height `y` m is to stand on a collision top: the model's half-float vertices' step there (0.0625 m
    at 64-128 m: a 500 m ship's deck), 0.02 m at least."""
    return max(0.02, 2.0 ** (math.floor(math.log2(max(abs(y), 1e-3))) - 10))




def check_fitting(md, hangars: tuple[bool, ...]) -> None:  # noqa: ANN001 - mdb.Mdb
    """The dreadnought `md` has the hangar modules `hangars` fits and nothing of the others (the plugin reads which it has
    off its bones: src/dreadnought_hangar.inc RigHangar): a fitted module's bone, its bays', their lifts' and doors' and
    mouths'; no mesh of a module not fitted (every vertex skinned to a bone it has: dreadnought_model.check_archive), so
    none of it in the collision measured off the model. Each fitted module's mesh within the board reach the plugin uses
    (dreadnought_model.HANGAR_REACH, src/dreadnought_deck.h OverHangar), its roof within its height."""
    import numpy as np
    from mdb import bind_world
    names = {md.name_of(b.name) for b in md.bones}
    for m, fitted in enumerate(hangars):
        mine = {dm._ours(dm.SHIP, b) for b in dm.hangar_parts(m)}
        if fitted:
            assert mine <= names, f'hangar {m} fitted: its model lacks {sorted(mine - names)}'
        else:
            assert not mine & names, f'hangar {m} not fitted: its model has {sorted(mine & names)}'
    assert not {dm._ours(dm.SHIP, b) for b in dm.LEGACY_MARKERS} & names, 'the foredeck ready markers are left out'
    world = bind_world(md)
    index = {md.name_of(b.name): b.index for b in md.bones}
    reach = dm.HANGAR_REACH
    for m, fitted in enumerate(hangars):
        if not fitted:
            continue
        origin = np.array(world[index[f'dn_hangar_{m}']][12:15])
        pts = np.array(jet_models.bind_positions(md, {index[f'dn_hangar_{m}']})) - origin
        lo, hi = pts.min(0), pts.max(0)
        assert max(abs(lo[0]), abs(hi[0])) <= reach['across'] and max(abs(lo[2]), abs(hi[2])) <= reach['along'], \
            f'hangar {m}: its mesh {lo.round(1)} .. {hi.round(1)} off its origin, past the board reach {reach}'
        assert -reach['below'] <= hi[1] <= reach['above'], f'hangar {m}: its roof {hi[1]:.1f} m over its origin, out of {reach}'


def check_markers(md, kinds: dict[str, bytes], hangars: tuple[bool, ...] = dm.ALL_HANGARS) -> None:  # noqa: ANN001 - mdb.Mdb
    """Every marker bone the plugin reads on its parent (dreadnought_model.BRIDGE, EYE, the fitted hangars' bays'), and
    standing / seeing / clear as STAND .. BAY_MARGIN say. `kinds`: each fighter kind's archive."""
    import math
    import numpy as np
    from mdb import bind_world
    bones = {md.name_of(b.name): b for b in md.bones}
    world = bind_world(md)
    level = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)

    def rows(name: str) -> tuple[float, ...]:
        m = world[bones[name].index]
        return tuple(m[k] for k in (0, 1, 2, 4, 5, 6, 8, 9, 10))
    want = {dm.BRIDGE: (dm.HULL, level, 0), dm.EYE.format(0): (dm.HULL, level, 0)}
    want.update({dm.EYE.format(1 + n): (f'dn_turret_{n}_gun', level, 0) for n in range(vc.DREADNOUGHT_TURRETS)})
    bays = [i for i in range(dm.BAYS) if hangars[i // 2]]
    for i in bays:
        side = 1.0 if i < dm.BAYS // 2 else -1.0   # outboard: port (+x), starboard
        out = (0.0, 0.0, -side, 0.0, 1.0, 0.0, side, 0.0, 0.0)   # left, up, ahead
        want[f'dn_bay_{i}'] = (dm.HULL, out, 3)
        want[f'dn_bay_{i}_out'] = (dm.HULL, out, 0)
    want.update({f'dn_hangar_{m}': (dm.HULL, level, 3) for m in range(dm.HANGARS) if hangars[m]})
    for name, (parent, frame, kind) in want.items():
        b = bones.get(name)
        assert b is not None, f'the dreadnought model has no {name}'
        assert md.name_of(md.bones[b.parent].name) == parent, (name, md.name_of(md.bones[b.parent].name), parent)
        assert b.kind == kind, f'{name}: kind {b.kind}, not {kind}'
        assert max(abs(a - f) for a, f in zip(rows(name), frame)) < 1e-5, f'{name}: frame {rows(name)}, not {frame}'
    for i in bays:
        assert {f'dn_lift_{i}', f'dn_door_u_{i}', f'dn_door_l_{i}'} <= set(bones), f'bay {i}: its lift or doors'
    assert dm.EYE.format(1 + vc.DREADNOUGHT_TURRETS) not in bones and f'dn_bay_{dm.BAYS}' not in bones
    tris = np.array(list(aircraft_collision.triangles(md)), float)
    cells = aircraft_collision.mesh_cells([tuple(map(tuple, t)) for t in tris], *aircraft_collision.DN_GRID)
    c0, c1 = np.array([c[0] for c in cells]), np.array([c[1] for c in cells])

    def at(name: str) -> np.ndarray:
        return np.array(world[bones[name].index][12:15])

    def under(lo, hi) -> np.ndarray:   # noqa: ANN001 - the tops of the cells a box overlaps
        return c1[np.all((c0 < np.asarray(hi) - 1e-6) & (c1 > np.asarray(lo) + 1e-6), axis=1), 1]
    sizes = {}
    for kind, arc in kinds.items():
        km = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == dm.mdb_name(kind)).data)
        pts = np.array(list(aircraft_collision.triangles(km)), float).reshape(-1, 3)
        sizes[kind] = pts.max(0) - pts.min(0)   # its model frame: span (x), height, length (z)
    for i in bays:
        b, size = bones[f'dn_bay_{i}'], sizes[dm.BAY_KINDS[i % len(dm.BAY_KINDS)]]
        assert all(abs(2 * b.half[k] - size[k]) < FIT for k in range(3)), (i, b.half, size)
        assert all(abs(b.centre[k]) < FIT for k in range(3)), f'bay {i}: its origin off its box\'s centre {b.centre}'
        p, q = at(f'dn_bay_{i}'), at(f'dn_bay_{i}_out')
        ahead = np.array(rows(f'dn_bay_{i}')[6:9])
        assert abs(p[1] - q[1]) < 1e-4 and abs(p[2] - q[2]) < 1e-4 and (q - p) @ ahead > 0, (i, p, q)
        half = np.abs(np.array([size[2], size[1], size[0]])) / 2.0 + BAY_MARGIN   # across the ship it is its length
        assert not under(q - half, q + half).size, f'dn_bay_{i}_out: its fighter in the collision'
    stand = at(dm.BRIDGE)
    top = float(under(stand - [0.01, 1e4, 0.01], stand + [0.01, 1e4, 0.01]).max())
    assert 0.0 <= stand[1] - top <= STAND, f'{dm.BRIDGE} at y {stand[1]:.2f}, its collision top {top:.2f}'

    def ray(o, d):   # the nearest triangle along unit d from o, or inf
        v0 = tris[:, 0]
        e1, e2 = tris[:, 1] - v0, tris[:, 2] - v0
        h = np.cross(d, e2)
        a = (e1 * h).sum(1)
        ok = np.abs(a) > 1e-12
        f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
        s = o - v0
        u, q = f * (s * h).sum(1), np.cross(s, e1)
        v, t = f * (q * d).sum(1), f * (e2 * q).sum(1)
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-6)
        return float(t[hit].min()) if hit.any() else math.inf
    roof = stand[1] - ray(stand, np.array([0.0, -1.0, 0.0]))
    assert abs(top - roof) <= ROOF, f'{dm.BRIDGE}: its roof drawn at {roof:.2f}, its collision at {top:.2f}'
    eye = np.array(world[bones[dm.EYE.format(0)].index][12:15])
    for yaw in (-VIEW_SIDE, 0.0, VIEW_SIDE):
        for pitch in (0.0, -VIEW_DOWN):
            y, p = math.radians(yaw), math.radians(pitch)
            d = np.array([math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)])
            t = ray(eye, d)
            assert t > CLEAR, f'the bridge eye {eye} sees the hull {t:.1f} m off (yaw {yaw}, pitch {pitch})'


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def write_out(files: dict[str, bytes], out: str) -> list[str]:
    """The files under `out` (a scratch folder, never the game's): for an offline build and check."""
    paths = []
    for rel, data in files.items():
        p = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as h:
            h.write(data)
        paths.append(p)
    return paths


def fitting_text(hangars: tuple[bool, ...]) -> str:
    """The fitted modules in the player's words (the installer's line)."""
    names = [n for n, fitted in zip(('左舷前', '左舷后', '右舷前', '右舷后'), hangars) if fitted]
    return '、'.join(names) + f'，共 {len(names)} 个' if names else '一个也不装'


def hangars_of(root: str, argv: list[str]) -> tuple[bool, ...]:
    """The modules to fit: --hangars (four 0 / 1, module order), else the game's plugin ini's (all without one)."""
    if '--hangars' in argv:
        text = argv[argv.index('--hangars') + 1]
        assert len(text) == dm.HANGARS and set(text) <= {'0', '1'}, f'--hangars {text}: four 0 / 1 (left fore, left aft, right fore, right aft)'
        return tuple(c == '1' for c in text)
    ini = os.path.join(root, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini')
    if not os.path.isfile(ini):
        return dm.ALL_HANGARS
    with open(ini, encoding='utf-8-sig', errors='replace') as h:
        return dm.hangars_wanted(h.read())


def main(argv: list[str]) -> int:
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    args = [a for i, a in enumerate(argv) if not a.startswith('--') and (i == 0 or argv[i - 1] not in ('--out', '--hangars'))]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    files = build(root, hangars_of(root, argv))
    for path in write_out(files, out) if out else install(root, files):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
