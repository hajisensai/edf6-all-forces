"""Self-test of the install chain, without the game (CI runs it; python tools/selftest.py, exit 1 on a failure):
  - tools/calls.py is consistent and src/calls.inc is what tools/gen_calls.py writes from it;
  - the call order rules: every released order is a prefix of CALLS, and installs of every order ever shipped
    (withdrawn ones too), with other mods' rows after ours and with uninstall placeholders, keep every row
    where it is (call_weapons.plan_rows / tail_start);
  - the copies kept by hand elsewhere agree with tools/calls.py and pylib/vcobjects.py (the player jets' marks
    in src/playerjet.cpp, the jets' marks and files in src/jet* and tools/make_jets.py,
    the enum names in src/crew.h, the counts in README.md);
  - the weapon table transaction (call_weapons.commit / recover) writes all or nothing, and rolls back a run
    that died half way;
  - the ownership ledger (pylib/ledger.py) deletes a file only when nobody needs it, and never one someone
    else changed;
  - the installer's ini merge (installer.merge_ini) only adds settings and changes nothing of the player's;
  - the pack: every plugin CMake builds is shipped by tools/build_release.py and in installer.PLUGINS, every writer
    installer.install calls has its remover in uninstall, and a stand-in game's install -> upgrade -> uninstall of
    both plugins and EDF6AutoTurret's data leaves Mods as other mods left it;
  - interrupted or refused runs: autoturret/tools/build.py install killed half way still reinstalls and
    uninstalls cleanly, a call_weapons.install that rolled back records no first backup, and the installer's
    uninstall over a misaligned table offers repair or skipping the table instead of failing;
  - the model importer (pylib/obj_model.py, pylib/texfile.py) on synthetic data: OBJ reading (negative indices,
    n-gons, winding, uv origin, texture lookup), holes found and closed, skins, meshes split below 65536 vertices
    with tangents, PNG decoding, DDS .lod slicing and DXT1.
"""
from __future__ import annotations

import contextlib
import io
import json
import os
import re
import shutil
import sys
import tempfile
import traceback
from typing import Callable, Iterator

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'autoturret', 'tools'))
import ballistics  # noqa: E402
import build as at_build  # noqa: E402
import call_weapons as cw  # noqa: E402
import calls  # noqa: E402
import dsgo  # noqa: E402
import gen_calls  # noqa: E402
import gen_stores  # noqa: E402
import installer  # noqa: E402
import ledger  # noqa: E402
import make_artillery  # noqa: E402
import make_edf5_campaign as e5c  # noqa: E402
import make_jets  # noqa: E402
import make_katyusha  # noqa: E402
import modfiles  # noqa: E402
import vcobjects as vc  # noqa: E402

TESTS: list[Callable[[], None]] = []


def test(fn: Callable[[], None]) -> Callable[[], None]:
    TESTS.append(fn)
    return fn


def src(rel: str) -> str:
    with open(os.path.join(ROOT, *rel.split('/')), encoding='utf-8') as f:
        return f.read()


def disabled_return_offset(hook: str) -> int:
    """The early return may first restore native state, such as the vehicle's zoom."""
    branch = re.search(r'if\(!Cfg\(\)\.enabled\)\s*(?:return;|\{[^}]*\breturn;[^}]*\})', hook)
    assert branch, 'InputHook keeps a plugin-disabled return (with optional native-state cleanup)'
    return branch.start()


# ---------------------------------------------------------------- the code


def in_team_field(rel: str, text: str, at: int) -> bool:
    """The one allowed raw write: crew.cpp WithTeamField, the team field changed for a stock seat check inside a team
    walk's visitor and put back right after (a SetTeam there would change the set the walk stands in)."""
    if rel != 'src/crew.cpp':
        return False
    start = text.find('auto WithTeamField(')
    return start >= 0 and start < at < text.find('\n}\n', start)


@test
def team_changes_go_through_set_team() -> None:
    """No plugin writes an object's team (+0x314, kTeam) itself: the game's team manager finds the object's set by
    it, and a raw write leaves the object in its old team's set after it is freed (the crash at the next
    mission's start, crew.h SetObjectTeam). Every change goes through SetObjectTeam (the game's SetTeam)."""
    raw = re.compile(r'Put\s*<[^>]*>\s*\([^;]*\bkTeam\s*[,)]')
    found = []
    for top in ('src', 'autoturret', 'common'):
        for folder, _, files in os.walk(os.path.join(ROOT, top)):
            for name in files:
                if name.endswith(('.cpp', '.h', '.inc')):
                    rel = os.path.relpath(os.path.join(folder, name), ROOT).replace(os.sep, '/')
                    text = src(rel)
                    found += [f'{rel}: {m.group(0)}' for m in raw.finditer(text) if not in_team_field(rel, text, m.start())]
    assert not found, 'raw team writes (use SetObjectTeam):\n' + '\n'.join(found)
    assert raw.search('Put<std::int32_t>(v,kTeam,own);'), 'the pattern no longer sees a raw write'


@test
def range_writes_every_generated_sgo_its_script_creates() -> None:
    """Each generated object (edf6tr_*) the range's script names is one install writes (testrange/gen.py spawned):
    the grand battle's script created the enemy fighter, never written, and the game stopped (dump EDF6.exe.79680)."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    import rmpa
    # A map of its own (the selftest runs without the game, on CI too): the player start and a point every 25 m out
    # to 1 km round it, enough for the vehicle spots, the enemy ring and the ships' far points.
    points = [rmpa.Point('プレイヤー', (0.0, 0.0, 0.0), (0.0, 0.0, 1.0))]
    points += [rmpa.Point(f'p{x}_{z}', (x * 25.0, 0.0, z * 25.0), (0.0, 0.0, 1.0))
               for x in range(-40, 41) for z in range(-40, 41) if (x, z) != (0, 0) and x * x + z * z <= 1600]
    air = gen.Plan()
    air.air.enabled = True
    for plan in (gen.grand_battle(gen.Plan()), gen.target_range(gen.Plan()), air, gen.Plan()):
        lay = gen.layout(points, gen.small_count(plan), gen.far_reserved(plan))
        named = set(re.findall(r'app:/object/(edf6tr_[a-z0-9_]+)\.sgo', gen.script(plan, lay)))
        missing = named - {x for x in gen.spawned(plan) if x in gen.DERIVED}
        assert not missing, f'{plan.scenario or ("air" if plan.air.enabled else "waves")}: never written {sorted(missing)}'
        assert named or plan.scenario != gen.GRAND, 'the grand battle names no generated object: the check sees nothing'


@test
def exhausts_come_from_the_models() -> None:
    """The flames and the arrival smoke are placed only from the models (the user, 2026-10-10: 「尾烟应该跟着模型生成，而不是
    用两段可能不同步的代码维护」): src/booster.cpp keeps no nozzle table of its own (the jets' by mark, the carrier's locators,
    the stock bombers') and takes both from ExhaustOf; the plugin's names for the bones are pylib/jet_models.py's; the stock
    models' table (src/nozzles_gen.h) is what tools/gen_nozzles.py makes (re-measured with the game, else checked against its
    own record); every plugin model with an exhaust makes nozzle bones (with the game: on its exits / the V508's boosters)."""
    import gen_nozzles
    import jet_models
    import rootcpk
    booster = src('src/booster.cpp')
    for gone in ('kJetNozzles', 'kBomberNozzles', 'kNozzleAt', 'NozzlesOf', 'ExhaustBasis', 'CarrierFlames'):
        assert gone not in booster, f'src/booster.cpp: {gone} (a nozzle table or path of its own) is back'
    assert not hasattr(jet_models, 'NOZZLES'), 'pylib/jet_models.py: a hand-copied NOZZLES table is back'
    flames = booster.split('void JetFrame(', 1)[1].split('\n}\n', 1)[0]
    smoke = booster.split('void SmokeFrame(', 1)[1].split('\n}\n', 1)[0]
    assert 'ExhaustOf(v,ms)' in flames and 'e->world[i]' in flames, 'the flames do not take ExhaustOf\'s nozzles'
    assert 'ExhaustOf(v,ms)' in smoke and 'e->world[i]' in smoke, 'the smoke does not take ExhaustOf\'s nozzles'
    header = src('src/exhaust_nozzles.h')
    prefix = re.search(r'kNozzlePrefix\[\]=L"([^"]+)"', header)
    most = re.search(r'constexpr int kMaxNozzles=(\d+);', header)
    assert prefix and prefix.group(1) == jet_models.NOZZLE_BONE, f'{prefix and prefix.group(1)} vs {jet_models.NOZZLE_BONE}'
    assert most and int(most.group(1)) == jet_models.MAX_NOZZLES, f'{most and most.group(1)} vs {jet_models.MAX_NOZZLES}'
    assert 'JetFlames(v,' in src('src/jet_flight.cpp').split('void Thrusters(', 1)[1].split('\n}\n', 1)[0], \
        'the carrier lights its pods\' flames through JetFlames'
    have = os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk'))
    why = gen_nozzles.check(rootcpk.Game(rootcpk.DEFAULT_GAME) if have else None)
    assert why is None, f'src/nozzles_gen.h is stale ({why}): python tools/gen_nozzles.py'
    exhaust = [f for f in jet_models.MODELS if f == jet_models.CARRIER_FILE or f in jet_models.NOZZLE_EXITS]
    assert sorted(exhaust) == sorted(jet_models.MODELS), f'a plugin model without nozzles: {set(jet_models.MODELS) - set(exhaust)}'
    if have:
        game = rootcpk.Game(rootcpk.DEFAULT_GAME)
        for f in [None, *jet_models.MODELS]:
            md = jet_models._model_of(game, f)
            want = jet_models.nozzles_for(game, jet_models.strip_nozzles(md), f) if f is not None else \
                jet_models.exit_nozzles(jet_models.strip_nozzles(md), None)
            jet_models.check_nozzle_bones(md, want)
            assert len(want) == (4 if f == jet_models.CARRIER_FILE else 1 if f == 'EDF6VC_DRONE.MRAB' else 2), f'{f}: {len(want)}'


def _nozzle_model() -> 'object':
    """A stand-in jet model: mdl; body (skin) with a pod (skin) and a tail (skin) under it, then the object's bone; one
    skinned triangle on each skin bone (its blend index that bone), and on the body a hexagonal exhaust rim 0.5 m across
    the corners round (2, 1) at z -5."""
    import struct
    from mdb import Bone, MatParam, MatTex, Material, Mdb, Mesh, Object, VElem
    import mdb_jet
    ident = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]

    def at(x: float, y: float, z: float) -> list[float]:
        return ident[:12] + [x, y, z, 1.0]
    binds = [ident, ident, at(4.0, 0.0, 1.0), at(0.0, 2.0, -6.0), ident]
    parents = [-1, 0, 1, 1, 0]
    kinds = [0, 3, 3, 3, 2]
    bones = [Bone(i, parents[i], -1, -1, i, 0, kinds[i], 0, int(kinds[i] == 3), 0, 0, list(binds[i]), list(binds[i]),
                  [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]) for i in range(5)]
    for i in (2, 3):   # inverse binds: their binds are translations
        bones[i].inv_bind = ident[:12] + [-binds[i][12], -binds[i][13], -binds[i][14], 1.0]
    mdb_jet.link(bones)
    elems = [VElem(4, 0, 0, 'position'), VElem(1, 12, 0, 'BLENDWEIGHT'), VElem(21, 28, 0, 'BLENDINDICES')]
    import math
    pts = [((2.0 + 0.25 * math.cos(k * math.pi / 3), 1.0 + 0.25 * math.sin(k * math.pi / 3), -5.0), 1) for k in range(6)]
    pts += [((4.0, 0.0, 1.0), 2), ((4.5, 0.0, 1.0), 2), ((4.0, 0.5, 1.0), 2), ((0.0, 2.0, -6.0), 3), ((0.0, 2.5, -6.0), 3),
            ((0.5, 2.0, -6.0), 3)]
    rows = b''.join(struct.pack('<3f4f4B', *p, 1.0, 0.0, 0.0, 0.0, bone, 0, 0, 0) for p, bone in pts)
    tris = struct.pack('<12H', 0, 1, 2, 0, 2, 3, 6, 7, 8, 9, 10, 11)
    me = Mesh(bytes((0, 1, 1, 0)), 0, 0, 32, elems, 0, rows, tris)
    mat = Material(0, 0, 0, 3, 'snd_BRDF_Common_Basic', [MatParam([0.5, 0.5, 0.5, 0.0], (0, 0), 'diffuse', 0x402)],
                   [MatTex(0, 'albedo', (0,) * 5)], 3)
    return Mdb(0x20, ['mdl', 'body', 'pod', 'tail', 'body_mesh', 'Material'], bones, [Object(4, 4, [me])], [mat], [])


@test
def nozzle_bones_in_a_made_model() -> None:
    """pylib/jet_models.py with_nozzles on a stand-in model (no game): a nozzle measured on the body's exit rim (its centre,
    its diameter, the flame FLAME_LENGTH_PER_DIAMETER of it, leaving along -z) and one on the pod (the carrier's kind) are
    bones nozzle_0 / nozzle_1, transform only and unbounded, right after their parents' subtrees, at those binds with those
    flames (check_nozzle_bones, also on the model written and read back); the skinned vertices on the bones the insertion
    moved (the tail's) still on the tail; strip_nozzles gives the model back byte for byte."""
    import math
    import jet_models
    from mdb import bind_world, mdb_read, mdb_write
    md = _nozzle_model()
    jet_models.NOZZLE_EXITS['standin'] = (((1.6, 2.4), (0.6, 1.4), (-5.1, -4.9)), False)
    try:
        body = jet_models.exit_nozzles(md, 'standin')
    finally:
        del jet_models.NOZZLE_EXITS['standin']
    assert len(body) == 1 and body[0].parent == 'body', body
    (c, d), = [((body[0].bind[12], body[0].bind[13], body[0].bind[14]), body[0].width)]
    area = 3 * 3 ** 0.5 / 2 * 0.25 ** 2
    assert max(abs(a - b) for a, b in zip(c, (2.0, 1.0, -5.0))) < 1e-6 and abs(d - 2 * (area / math.pi) ** 0.5) < 1e-4, (c, d)
    assert abs(body[0].length - jet_models.FLAME_LENGTH_PER_DIAMETER * d) < 1e-3 and body[0].bind[8:11] == (0.0, 0.0, -1.0), body[0]
    pod = jet_models.NozzleBone('pod', jet_models.turned_at((4.0, -0.1, -2.0)), 40.0, 12.0)
    want = [body[0], pod]
    out = jet_models.with_nozzles(md, want)
    names = [out.name_of(b.name) for b in out.bones]
    assert names == ['mdl', 'body', 'pod', 'nozzle_1', 'tail', 'nozzle_0', 'body_mesh'], names
    jet_models.check_nozzle_bones(out, want)
    back = mdb_read(mdb_write(out))
    jet_models.check_nozzle_bones(back, want)
    assert [x[0] for x in jet_models._skin_names(back)] == ['body'] * 6 + ['pod'] * 3 + ['tail'] * 3, jet_models._skin_names(back)
    assert back.objects[0].bone == back.bone_index('body_mesh')
    w = bind_world(back)
    assert max(abs(a - b) for a, b in zip(w[back.bone_index('nozzle_1')][12:15], (4.0, -0.1, -2.0))) < 1e-5
    assert mdb_write(jet_models.strip_nozzles(out)) == mdb_write(md), 'strip_nozzles does not give the model back'


@test
def gear_legs_as_the_models_fold_them() -> None:
    """src/gear.cpp's legs (kLegNames, kLegUp) are pylib/jet_gear.py's (LEGS, LEG_UP): the plugin folds each leg by the
    angle its model was measured to fold level at; every fixed-wing model recipe names a gear spec, hover craft none
    (the drone a skid spec instead)."""
    import jet_gear
    import jet_models
    text = src('src/gear.cpp')
    names = re.search(r'kLegNames\[kGearLegs\]=\{([^}]*)\}', text)
    ups = re.search(r'kLegUp\[kGearLegs\]=\{([^}]*)\}', text)
    assert names and ups, 'kLegNames / kLegUp not found in src/gear.cpp'
    got_names = re.findall(r'L"([^"]+)"', names.group(1))
    got_ups = [float(x.strip().rstrip('f')) for x in ups.group(1).split(',')]
    assert tuple(got_names) == jet_gear.LEGS, f'{got_names} vs {jet_gear.LEGS}'
    assert all(abs(a - jet_gear.LEG_UP[n]) < 1e-4 for a, n in zip(got_ups, got_names)), f'{got_ups} vs {jet_gear.LEG_UP}'
    assert jet_models.ELEVON_GEAR in jet_gear.SPECS
    import jet_skids
    for file, r in jet_models.MODELS.items():
        hover = file in ('EDF6VC_CARRIER.MRAB', 'EDF6VC_DRONE.MRAB')
        assert (r.gear is None) == hover and (r.gear is None or r.gear in jet_gear.SPECS), f'{file}: gear {r.gear}'
        # the drone stands on fixed skids (jet_skids: its gun pod was its lowest point), the carrier on its pods
        assert (r.skids is not None) == (file == 'EDF6VC_DRONE.MRAB') and (r.skids is None or r.skids in jet_skids.SPECS), file


@test
def gear_mount_reaches_the_skin() -> None:
    """pylib/jet_gear.py mounts on synthetic data: a slanted cylinder (the donor's shock absorber: its hub end at the
    origin, 0.65 forward and 0.42 up to its upper end, 0.07 of that inside its donor body) placed x 1.5 under a level
    skin 0.5 over where its end would be: reach() raises the end until it goes 0.07 x 1.5 into the skin, _shear keeps
    the hub end, lifts the upper end by exactly that, and leaves every normal perpendicular to the sheared surface."""
    import math
    import jet_gear as jg
    ring = 8
    axis_d = (0.0, 0.42, 0.65)
    ln = math.hypot(axis_d[1], axis_d[2])
    ax = (0.0, axis_d[1] / ln, axis_d[2] / ln)
    u = (1.0, 0.0, 0.0)
    w = (0.0, ax[2], -ax[1])     # u x axis: perpendicular to both
    r = 0.04
    rows, P = [], []
    for end in (0.0, 1.0):
        for k in range(ring):
            a = 2 * math.pi * k / ring
            nrm = tuple(math.cos(a) * u[c] + math.sin(a) * w[c] for c in range(3))
            p = tuple(end * axis_d[c] + r * nrm[c] for c in range(3))
            P.append(p)
            rows.append([p, nrm, ax])
    m = jg.mount_of(0, 0, P, set(range(2 * ring)), 0.07)
    assert abs(m.length - ln) < 1e-9 and all(abs(m.axis[c] - ax[c]) < 1e-9 for c in range(3)), (m.axis, m.length)
    s, off = 1.5, (0.0, 0.0, 0.0)
    skin_y = m.top[1] * s + 0.5
    rise = jg.reach(m, s, off, lambda _x, _z: skin_y)
    top, short = jg.mount_gap(m, s, off, lambda _x, _z: skin_y, rise)
    assert abs(short) < 1e-9 and abs(top[1] - (skin_y + 0.07 * s)) < 1e-9, (top, short, rise)
    assert jg.reach(m, s, (0.0, 1.0, 0.0), lambda _x, _z: skin_y) == 0.0, 'a mount already deep enough is not moved'
    keys = ['position:0', 'normal:0', 'tangent:0']
    out = [jg._shear(m, rise, row, keys) for row in rows]
    for k in range(ring):
        lo, hi = out[k], out[ring + k]
        assert max(abs(lo[0][c] - rows[k][0][c]) for c in range(3)) < 1e-9, 'the hub end moved'
        assert abs(hi[0][1] - rows[ring + k][0][1] - rise * m.t(rows[ring + k][0])) < 1e-9
        side = [hi[0][c] - lo[0][c] for c in range(3)]               # along the sheared surface
        an = 2 * math.pi * k / ring
        around = [-math.sin(an) * u[c] + math.cos(an) * w[c] for c in range(3)]   # the hub ring's tangent there
        for e in (side, around):
            dot = sum(lo[1][c] * e[c] for c in range(3)) / math.sqrt(sum(x * x for x in e))
            assert abs(dot) < 1e-9, f'normal {lo[1]} not across the sheared surface ({dot:.3g})'
        nl = math.sqrt(sum(x * x for x in lo[1]))
        assert abs(nl - 1.0) < 1e-9


@test
def chute_canopy_geometry() -> None:
    """pylib/chute_model.py canopy(): a dome 2 x RADIUS across and HEIGHT over its rim, open underneath (nothing under
    the rim but the lines, which stay inside it and end at the riser point), the outside shell facing out and the
    inside in, every triangle wound as the stock models' (its cross product along its normals)."""
    import chute_model as cm
    verts, tris = cm.canopy()
    assert len(verts) < 0x10000 and tris, (len(verts), len(tris))
    assert all(max(t) < len(verts) for t in tris)
    p = [v[0] for v in verts]
    assert abs(max(x for x, _y, _z in p) - min(x for x, _y, _z in p) - 2 * cm.RADIUS) < 1e-3
    assert abs(max(y for _x, y, _z in p) - cm.RIM_Y - cm.HEIGHT) < 1e-3
    assert abs(min(y for _x, y, _z in p) - cm.RISER_Y) < cm.LINE_WIDTH   # a strip's corners, half its width round the point
    for x, y, z in p:
        r = (x * x + z * z) ** 0.5
        assert r <= cm.RADIUS + cm.LINE_WIDTH, (x, y, z)   # the lines' strips: half their width off the rim
        if y < cm.RIM_Y - 1e-3:   # a line: on the cone from the rim to the riser point
            assert r <= cm.RADIUS * (y - cm.RISER_Y) / (cm.RIM_Y - cm.RISER_Y) + cm.LINE_WIDTH, (x, y, z)
    bad = [t for t in tris if cm.facing(verts, t) <= 0]
    assert not bad, f'{len(bad)} triangles wound against their normals'
    shell = (cm.SEGMENTS + 1) * (cm.RINGS + 1)
    for k, (q, n, *_rest) in enumerate(verts[:2 * shell]):
        out = (q[0] * n[0] + (q[1] - cm.RIM_Y) * n[1] + q[2] * n[2]) > 0
        assert out == (k < shell), f'vertex {k}: normal {n} at {q}'


def _chute_game() -> object:
    """A stand-in for rootcpk.Game with what pylib/chute_model.py reads: the Grape's archive cut down to one fabric
    material, its seat layout (one triangle) and its textures (an 8 x 8 DXT1 occlusion map, bright in one block), and
    a far-off plant SGO."""
    import struct
    import chute_model as cm
    from mdb import Bone, MatParam, MatTex, Material, Mdb, Mesh, Object, Rab, RabFile, Texture, VElem, mdb_write, rab_write
    ident = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
    bones = [Bone(i, -1 if i == 0 else 0, -1, -1, i, 0, kind, 0, int(kind == 3), 0, 0, list(ident), list(ident),
                  [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]) for i, kind in enumerate((0, 2, 3))]
    elems = [VElem(7, 0, 0, 'position'), VElem(7, 8, 0, 'normal'), VElem(7, 16, 0, 'binormal'), VElem(7, 24, 0, 'tangent'),
             VElem(12, 32, 0, 'texcoord'), VElem(12, 40, 1, 'texcoord'), VElem(1, 48, 0, 'BLENDWEIGHT'),
             VElem(21, 64, 0, 'BLENDINDICES')]
    seat = Mesh(bytes((0, 1, 1, 0)), 0, 0, 68, elems, 0, bytes(68 * 3), struct.pack('<3H', 0, 1, 2))
    mat = Material(0, 0, 0, 3, 'snd_BRDF_Common_SeparateOcc', [MatParam([0.5, 0.5, 0.5, 0.0], (0, 0), 'diffuse', 0x402)],
                   [MatTex(0, 'albedo', (0,) * 5), MatTex(1, 'param_occ', (0,) * 5)], 3)
    donor = Mdb(0x20, ['mdl', 'grape', 'mainBody', 'Material'], bones, [Object(1, 1, [seat])], [mat],
                [Texture(0, 'sheet_DDS', cm.FABRIC, 0), Texture(1, 'occ_DDS', 'occ.DDS', 0)])
    occ = bytearray(128)
    occ[0:4] = b'DDS '
    struct.pack_into('<II', occ, 12, 8, 8)
    occ[84:88] = b'DXT1'
    for k in range(4):   # 2 x 2 blocks: the last one white
        occ += struct.pack('<HHI', 0xFFFF if k == 3 else 0x4208, 0xFFFF if k == 3 else 0x2104, 0)
    stem = cm.FABRIC.rsplit('.', 1)[0]
    files = [RabFile(f'{stem}.lod.DDS', 0, 0, b'lod'), RabFile('occ.lod.DDS', 0, 0, bytes(occ)),
             RabFile(cm.DONOR_MDB, 1, 0, mdb_write(donor)), RabFile(cm.FABRIC, 2, 1, b'hd'), RabFile('occ.DDS', 2, 1, bytes(occ))]
    arc = rab_write(Rab(0x110, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], files))
    plant = dsgo.Document(dsgo.Node(['FarEventObject', dsgo.Node([dsgo.Node([1.0, 1.0, 1.0]), 'default'], {0: 'scale', 1: 'default_animation'}),
                                     dsgo.Node([dsgo.Node(['app:/Object/ev601_plant.mrab', 'ev601_plant.mdb']), 'app:/object/ev601_plant.cas',
                                                dsgo.Blob(b'anim')])],
                                    {0: 'xgs_scene_object_class', 1: 'setting', 2: 'animation_model'}), [])
    stock = {('OBJECT', cm.DONOR_ARC): arc, ('OBJECT', cm.STOCK_SGO): dsgo.write(plant)}

    class Game:
        def read(self, folder: str, name: str) -> bytes:
            return stock[(folder, name)]
    return Game()


@test
def chute_builds_without_the_game() -> None:
    """pylib/chute_model.py build / check / sgo on a stand-in Root.cpk: the archive holds the model and the fabric's
    textures, the model's occlusion coordinates sit on the brightest block, and the SGO is a FarEventObject showing
    this model with a unit scale only (no animation, no ragdoll)."""
    import chute_model as cm
    from mdb import mdb_read, rab_read, read_elem
    game = _chute_game()
    arc = cm.build(game)
    cm.check(arc)
    rab = rab_read(arc)
    names = [f.name for f in rab.files]
    stem = cm.FABRIC.rsplit('.', 1)[0]
    assert names == ['occ.lod.DDS', f'{stem}.lod.DDS', cm.OUT_MDB, 'occ.DDS', cm.FABRIC], names   # folder, then name
    md = mdb_read(next(f for f in rab.files if f.name == cm.OUT_MDB).data)
    occ = {tuple(v) for v in read_elem(md.objects[0].meshes[0], 'texcoord', 1)}
    assert occ == {(0.75, 0.75)}, occ
    v = dsgo.to_py(dsgo.parse(cm.sgo(game)).root)
    assert v == {'xgs_scene_object_class': 'FarEventObject', 'setting': {'scale': [1.0, 1.0, 1.0]},
                 'animation_model': [[f'app:/Object/{cm.OUT_ARC.lower()}', cm.OUT_MDB], 0.0, 0.0]}, v


@test
def chute_wired_through() -> None:
    """The canopy's copies agree: src/playerjet.cpp's SGO path, file and height with pylib/chute_model.py, and
    tools/make_chute.py is installed, removed, bundled and a ledger owner."""
    import chute_model as cm
    import make_chute
    text = src('src/playerjet.cpp')
    assert f'kChuteSgo=L"app:/object/{cm.SGO_FILE.lower()}"' in text
    assert f'kChuteFile=L"{cm.SGO_FILE}"' in text
    up = re.search(r'kChuteUp=([\d.]+)f', text)
    assert up and float(up.group(1)) == cm.CANOPY_UP, up
    assert make_chute.OWNER in ledger.OWNERS
    inst = src('tools/installer.py')
    assert 'make_chute.install(game, chute)' in inst and 'make_chute.remove' in inst
    rel = src('tools/build_release.py')
    assert "'make_chute'" in rel and "'chute_model'" in rel


@test
def release_imports() -> None:
    """Every project module tools/installer.py imports inside a function (tools/, pylib/, testrange/) is one of
    tools/build_release.py's PyInstaller hidden imports: a generator left out installs in a dev checkout and is missing
    from the released exe."""
    inst, rel = src('tools/installer.py'), src('tools/build_release.py')
    hidden = set(re.findall(r"'(\w+)'", rel.split("for mod in ('call_weapons'", 1)[1].split('):', 1)[0])) | {'call_weapons'}
    local = {os.path.splitext(f)[0] for d in ('tools', 'pylib', 'testrange', 'autoturret/tools')
             for f in os.listdir(os.path.join(ROOT, d)) if f.endswith('.py')}
    lazy = set(re.findall(r'^[ \t]+import (\w+)', inst, re.M)) & local
    assert {'make_emc', 'build'} <= lazy, 'release_imports: the scan reads installer.py'
    # Procedural models use importlib, so PyInstaller cannot infer these from the import graph.
    import jet_models
    lazy.update(jet_models.GENERATED.values())
    missing = sorted(lazy - hidden)
    assert not missing, f'tools/build_release.py: hidden imports missing {missing}'
    excluded = rel.split("cmd += ['--exclude-module', mod]", 1)[0].rsplit('for mod in ', 1)[1]
    assert "'PIL'" not in excluded, 'procedural model textures require Pillow in the released installer'
    # and installed where the exe is built: build_release refuses without it, CI installs it
    bundled = rel.split('BUNDLED = ', 1)[1].split('\n', 1)[0]
    assert "'PIL'" in bundled and "'numpy'" in bundled, 'build_release.BUNDLED: numpy and Pillow'
    ci = src('.github/workflows/build.yml')
    assert re.search(r'pip install [^\n]*\bnumpy pillow\b', ci), 'CI installs numpy and pillow before the installer is built'


@test
def play_edge_margin_is_the_big_maps() -> None:
    """src/crew.h kBigWorldMargin (the big map's ground edge = BigWorld less it) is tools/make_bigmap.py WORLD_MARGIN."""
    import make_bigmap
    m = re.search(r'kBigWorldMargin=([\d.]+)f', src('src/crew.h'))
    assert m and float(m.group(1)) == make_bigmap.WORLD_MARGIN, (m and m.group(1), make_bigmap.WORLD_MARGIN)


@test
def seam_field_makes_the_block_periodic() -> None:
    """pylib/seams.py on a made-up uneven block: opposite edges and the four corners end up equal, the middle
    ground (|x|, |z| <= 1250) does not move, and nothing moves by more than the edge difference it fixes."""
    import numpy as np
    import seams
    rng = np.random.default_rng(7)
    g = np.arange(-seams.HALF, seams.HALF + 1, 25.0)
    x, z = np.meshgrid(g, g)
    y = (20 * np.sin(x / 310 + 1.3) * np.cos(z / 470) + 0.01 * x + rng.normal(0, 0.5, x.shape))
    pts = np.stack([x.ravel(), y.ravel(), z.ravel()], 1)
    f = seams.Field(pts)
    ny = f.heights(pts).reshape(x.shape)
    assert np.abs(ny[:, 0] - ny[:, -1]).max() < 1e-9 and np.abs(ny[0, :] - ny[-1, :]).max() < 1e-9
    assert np.ptp([ny[0, 0], ny[0, -1], ny[-1, 0], ny[-1, -1]]) < 1e-9
    middle = (np.abs(x) <= seams.INNER) & (np.abs(z) <= seams.INNER)
    assert np.abs(ny - y)[middle].max() == 0.0
    edge_gap = max(np.abs(y[:, 0] - y[:, -1]).max(), np.abs(y[0, :] - y[-1, :]).max())
    assert np.abs(ny - y).max() <= edge_gap


@test
def collision_box_codec_holds_its_child() -> None:
    """pylib/hkcms.py encode_box: the decoded code of any box inside a parent contains the box and stays inside
    the parent (the game culls with the decoded box: a smaller one would let things fall through)."""
    import numpy as np
    import hkcms
    rng = np.random.default_rng(3)
    for _ in range(2000):
        a, b = rng.uniform(-2000, 2000, 3), rng.uniform(-2000, 2000, 3)
        parent = np.array([np.minimum(a, b), np.maximum(a, b)])
        c, d = rng.uniform(parent[0], parent[1]), rng.uniform(parent[0], parent[1])
        child = np.array([np.minimum(c, d), np.maximum(c, d)])
        got = hkcms.decode_box(parent, hkcms.encode_box(parent, child))
        assert (got[0] <= child[0]).all() and (got[1] >= child[1]).all(), (parent, child, got)
        assert (got[0] >= parent[0] - 1e-6).all() and (got[1] <= parent[1] + 1e-6).all(), (parent, child, got)


# ---------------------------------------------------------------- the data


@test
def calls_table_consistent() -> None:
    ids = [c.id for c in calls.CALLS]
    assert len(set(ids)) == len(ids), 'duplicate ids'
    assert all(i.startswith(calls.ID_PREFIX) for i in ids)
    marks = [c.mark for c in calls.CALLS if not c.ground]   # a ground vehicle's request has no mark (0)
    assert len(set(marks)) == len(marks), 'duplicate marks'
    assert all(c.mark == 0 for c in calls.CALLS if c.ground), 'a ground vehicle request with a mark'
    crew_h = src('src/crew.h')
    roles = set(re.search(r'enum class JetRole \{([^}]*)\}', crew_h).group(1).replace(' ', '').split(','))
    bodies = set(re.search(r'enum class HeliBody \{([^}]*)\}', crew_h).group(1).replace(' ', '').split(','))
    drones = set(re.search(r'enum class ThrownDrone \{([^}]*)\}', crew_h).group(1).replace(' ', '').split(','))
    for c in calls.CALLS:
        assert c.kind in calls.KINDS, c.id
        assert c.brings in ('jets', 'helis', 'sub', 'vehicle', 'throw', 'gun'), c.id
        thrown = c.brings == 'throw'
        assert bool(c.drone) == thrown and (not c.drone or c.drone in drones), c.id
        # A thrown drone's marker: the bits of 1.0 with its code in the low ones, never 1.0 itself (calls.throw_mark).
        bits = calls.mark_bits(c)
        assert not thrown or (bits & ~0xFFF == calls.THROW_MARK_BASE and bits != calls.THROW_MARK_BASE), c.id
        assert bool(c.gun) == (c.brings == 'gun') and (not c.gun or c.reload == 0), c.id
        assert bool(c.role) == (c.brings == 'jets') and (not c.role or c.role in roles), c.id
        assert bool(c.body) == (c.brings == 'helis') and (not c.body or c.body in bodies), c.id
        assert bool(c.vehicle) == (bool(c.jet) or bool(c.ground)) == (c.brings == 'vehicle'), c.id
        assert not (c.jet and c.ground) and (not c.ground or c.ground in vc.GROUND_VEHICLES), c.id
        assert not c.ground or vc.GROUND_VEHICLES[c.ground].sgo == c.vehicle, c.id
        assert (c.count > 0) == (c.flown or thrown) and bool(c.log) == (c.flown or thrown), c.id
        assert not thrown or not c.flown, c.id   # not in the in-mission pick (kCalls)
        for lang in cw.LANGS:
            assert calls.call_name(c, lang) and calls.call_description(c, lang)
            assert calls.retired_name(c, lang) != calls.call_name(c, lang)
        assert calls.slot_of(c.id) == c.id and calls.slot_of(calls.retired_id(c.id)) == c.id
        assert not calls.retired_id(c.id).startswith(calls.ID_PREFIX)   # GrantCalls never owns a placeholder
    names = [calls.call_name(c, 'SC') for c in calls.FLOWN]
    assert len(set(names)) == len(names), 'two calls with one banner label'


@test
def boarding_tag_in_plugin() -> None:
    """The boarding gun's tag: src/boarding.cpp compares the bits call_weapons.gun_tag writes (1 + mark ulps)."""
    guns = [c for c in calls.CALLS if c.brings == 'gun']
    assert len(guns) == 4 and len({c.gun for c in guns}) == 4, 'one native gun per class'
    assert {c.mark for c in guns} == {7301, 7302, 7303, 7304}, 'one reserved collision tag per class'
    assert 'bits>=kTagBits && bits<=kTagBits+3u' in src('src/boarding.cpp')
    bits = re.search(r'kTagBits=0x3F800000u\+(\d+)u;', src('src/boarding.cpp'))
    assert bits and int(bits.group(1)) == int(guns[0].mark) == guns[0].mark, "src/boarding.cpp kTagBits is not the gun's mark"
    assert cw.gun_tag_bits(guns[0].mark) == 0x3F800000 + int(guns[0].mark)
    assert [c.gun for c in guns] == ['aWeapon081', 'pWeapon127', 'eWeapon120', 'hCannon01']
    assert '#include' in src('src/boarding.cpp') and 'src/boarding.cpp' in src('CMakeLists.txt')


@test
def boarding_debug_gun_parameters_and_text() -> None:
    """The debug gun changes the intended fields and every locale's numeric rows, keeping star curves and tag."""
    gun = next(c for c in calls.CALLS if c.brings == 'gun')
    root = dsgo.Node([])
    old = {'AmmoSpeed': 25.0, 'FireAccuracy': 0.125, 'AmmoCount': 8.0,
           'ReloadTime': 360.0, 'FireInterval': 90.0}
    curves = {}
    for i, (key, base) in enumerate(old.items()):
        curves[key] = [base, float(i + 10), 0.5, 2.0, 1.0]
        root.set(key, dsgo.Node(curves[key].copy()))
    root.set('AmmoAlive', 40.0)
    for key in cw.GUN_SCALARS:
        if key not in root.names.values():
            root.set(key, 0.0)
    root.set('FireRecoil', 3.0)
    root.set('AmmoColor', dsgo.Node([0.25, 0.5, 0.75, 1.0]))
    template = dsgo.write(dsgo.Document(root, []))
    actual = dsgo.parse(cw.gun_sgo(template, gun)).root
    for key, base in cw.GUN_CURVES.items():
        assert actual.get(key).items == [base, *curves[key][1:]], key
    for key, value in cw.GUN_SCALARS.items():
        assert actual.get(key) == value, key
    import struct
    alpha = actual.get(cw.GUN_TAG).items[3]
    assert struct.unpack('<I', struct.pack('<f', alpha))[0] == cw.gun_tag_bits(gun.mark)
    units = {'AmmoCount': 1.0, 'FireInterval': 1.0, 'ReloadTime': 1.0 / cw.FPS,
             'AmmoSpeed': cw.FPS, 'FireAccuracy': 1.0}
    lines = [dsgo.Node([f'label-{i}', '', dsgo.Node([curves[key][0] * unit, *curves[key][1:-1], -1.0])])
             for i, (key, unit) in enumerate(units.items())]
    lines.append(dsgo.Node(['range', '', dsgo.Node([1000.0, *curves['AmmoSpeed'][1:-1], -1.0])]))
    damage = dsgo.Node(['damage', '', dsgo.Node([242.0, 999.0, 0.5, 2.0, -1.0])])
    lines.append(damage)
    row = dsgo.Node(['title', 'description', dsgo.Node(lines)])
    expected = [cw.GUN_CURVES[key] * unit for key, unit in units.items()] + [1500.0, 242.0]
    for lang in cw.LANGS:
        changed = cw._text_row(row, gun, lang, gun=cw.gun_stats(template))
        assert [st.items[2].items[0] for st in changed.items[2].items] == expected, lang
        assert changed.items[2].items[-1] == damage, 'damage and its star parameters stay unchanged'


@test
def boarding_all_classes_stock_resources() -> None:
    """When Root.cpk is available, generate actual class-native weapons and every locale's menu rows."""
    import rootcpk
    if not os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.DEFAULT_GAME
    rows = cw._rows(dsgo.parse(cw.stock(game, cw.TABLE)), cw.TABLE)
    ids = [r.items[0] for r in rows]
    expected = [('aWeapon081', 2, 'Weapon_BasicShoot'),
                ('pWeapon127', 104, 'Weapon_PreChargeShoot'),
                ('eWeapon120', 303, 'Weapon_BasicShoot'),
                ('hCannon01', 204, 'Weapon_HeavyShoot')]
    guns = [c for c in calls.CALLS if c.brings == 'gun']
    for call, (name, category, weapon_class) in zip(guns, expected):
        template = cw.stock(game, f'WEAPON/{name.upper()}.SGO')
        original = dsgo.parse(template).root
        weapon = dsgo.parse(cw.gun_sgo(template, call)).root
        index = ids.index(name)
        row = cw._table_row(rows[index], call)
        assert row.items[2] == category and row.items[0] == call.id
        assert weapon.get('xgs_scene_object_class') == weapon_class
        for key in ('animation_model', 'ModelConstraint', 'Sight_animation_model'):
            assert dsgo.to_py(weapon.get(key)) == dsgo.to_py(original.get(key)), (call.id, key)
        assert weapon.get('AmmoClass') == 'SolidBullet01'
        assert weapon.get('AmmoExplosion') == weapon.get('AmmoGravityFactor') == 0
        assert weapon.get('SecondaryFire_Type') == 1
        assert weapon.get('Ammo_CustomParameter').items == []
        for lang, rel in zip(cw.LANGS, cw.TEXTS):
            texts = cw._rows(dsgo.parse(cw.stock(game, rel)), rel)
            cw._text_row(texts[index], call, lang, gun=cw.gun_stats(template))


@test
def boarding_only_after_collision() -> None:
    """Broadphase candidates include misses and objects behind walls. Only the native hit's damage call may
    request boarding; keep the executable production-hook test in CI, with original damage on all other paths."""
    board = src('src/boarding.cpp')
    assert 'BoardingCandidate' not in src('src/jet_hooks.cpp')
    assert 'BoardingHit' not in src('src/jet_hooks.cpp')
    assert 'kHitDamageCall=0x230EA6,kHitDamage=0x541FF0' in board
    assert 'RedirectCall(image+kHitDamageCall,image+kHitDamage' in board
    assert 'if(!board)nextHitDamage(damage,target,info);' in board
    assert 'At<const float*>(core,kHitRecords)' in board
    assert 'add_test(NAME boarding_hit COMMAND boarding_hit_test)' in src('CMakeLists.txt')
    assert 'ctest --test-dir build --output-on-failure' in src('.github/workflows/build.yml')


@test
def calls_inc_current() -> None:
    with open(gen_calls.OUT, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_calls.render(), 'src/calls.inc is stale: python tools/gen_calls.py'
    assert '#include "calls.inc"' in src('src/airstrike.cpp')


@test
def thrown_drones_marked_and_wired() -> None:
    """The thrown drones' weapons (tools/calls.py brings 'throw'): the SGO tools/call_weapons.py writes holds the
    marker as a double that is that float exactly (the game reads a double and keeps a float, 0x68D8FB: any rounding
    and the plugin's bit compare misses), with the magazine and reload asked for; its text row keeps only the count
    and reload stats; the plugin's kThrows has every one with those bits; the ini key is read, shipped, documented."""
    import struct
    template = dsgo.write(dsgo.Document(dsgo.Node(
        [1.0, dsgo.Node([8.0, 0.0, 0.0, 7.0, 0.5, 0.5, 0.0]), dsgo.Node([480.0, 21.0, 2.0, 8.0, 1.0, 0.5, 0.0]), 'Patroller'],
        {0: 'AmmoHitSizeAdjust', 1: 'AmmoCount', 2: 'ReloadTime', 3: 'name.en'}), []))
    stat = [['Number', '$0', [8.0, 0.0, 0.0, 7.0, 0.5, 0.5, 0.0]], ['Damage', '$0', [18.0]], ['Search', '$0m', [30.0]],
            ['Reload', '$0 sec', [8.0, 21.0, 2.0, 8.0, 1.0, 0.5, 1.0]], ['Spread', '75.0m']]
    def node(v):   # a plain list as a dsgo list node
        return dsgo.Node([node(x) for x in v]) if isinstance(v, list) else v
    text = node(['Patroller', 'stock text', stat])
    thrown = [c for c in calls.CALLS if c.brings == 'throw']
    assert thrown and cw.template_of(thrown[0]) == cw.THROW_TEMPLATE == 'eWeapon217'
    inc = src('src/calls.inc')
    for c in thrown:
        r = dsgo.parse(cw.weapon_sgo(template, c)).root
        mark = r.get('AmmoHitSizeAdjust')
        assert struct.unpack('<f', struct.pack('<f', mark))[0] == mark, f'{c.id}: the marker is no float'
        assert struct.unpack('<I', struct.pack('<f', mark))[0] == calls.mark_bits(c), c.id
        assert r.get('AmmoCount').items[0] == c.count and r.get('ReloadTime').items[0] == c.reload, c.id
        assert r.get('name.en') == calls.call_name(c, 'EN'), c.id
        row = dsgo.to_py(cw._text_row(text, c, 'EN'))
        assert [s[0] for s in row[2]] == ['Number', 'Reload'], row[2]
        assert row[2][0][2][0] == c.count and abs(row[2][1][2][0] - c.reload / 60.0) < 1e-9, row[2]
        assert f'{{0x{calls.mark_bits(c):08X}u,ThrownDrone::{c.drone},{c.fuel_sec},' in inc, f'src/calls.inc kThrows: {c.id}'
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert 'L"ThrowDrones"' in plugin and re.search(r'^ThrowDrones=1', ini, re.M) and 'ThrowDrones' in readme
    assert 'kThrows' in src('src/airstrike.cpp') and 'JetLaunchThrown' in src('src/jet_spawn.cpp')


@test
def stores_inc_current() -> None:
    with open(gen_stores.OUT, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_stores.render(), 'src/stores.inc is stale: python tools/gen_stores.py'
    assert '#include "stores.inc"' in src('src/stores.cpp')
    # Every jet: as many weapons as holders, four at least (src/stores.cpp: without the plugin the 506 builds four),
    # and every store it names is made.
    for name, jet in vc.JETS.items():
        assert len(jet.weapons) + 1 >= 4, name
        for w in jet.weapons:
            got = vc.store_of(w)
            assert got is None or w.split('/')[-1].upper() in vc.STORE_FILES, (name, w)
    # The weapons the submarine carrier's launch requires (src/subcarrier.cpp kSubFiles) are the ones it is built with.
    import re
    sub = src('src/subcarrier.cpp')
    listed = re.findall(r'WEAPON\\+([A-Z0-9_]+\.SGO)', sub[sub.index('kSubFiles[]'):sub.index('};', sub.index('kSubFiles[]'))])
    built = {w.split('/')[-1].upper() for w in vc.JETS['edf6tr_sub_carrier_mission'].weapons}
    assert listed and set(listed) <= built, (listed, built)
    assert f'kSgoHull={vc.JETS["edf6tr_sub_carrier_mission"].durability:.1f}f' in sub, 'subcarrier.cpp kSgoHull'

@test
def store_looks() -> None:
    """Every store's round flies a model of its own at its weapon's real length (pylib/vcobjects.py STORE_MODELS, Look; the
    user, 2026-10-07: 「不同挂载要有不同模型，射出去的时候也应该是对应模型」): each look's model is one of STORE_MODELS and its
    AmmoSize gives the real length within STORE_LENGTH_SLACK; every model listed is used; the rounds one control cycles
    never share a model (each jet's stores; a stock vehicle's stores on one holder, make_stock_stores.LOADOUTS, and with
    the game the stock weapon of the holder they hang beside). With the game: each model measures as STORE_MODELS says on
    Root.cpk (and _look refuses one that does not); each store SGO jet_guns writes fires its look's model at its size,
    preloads exactly that model, keeps the template's contact sphere and every other value of the build without the look;
    the Sazabi's shield missile (its look the template's) is that build byte for byte."""
    import make_stock_stores as mss
    looks = {kind: s.weapon.look for kind, s in vc.STORES.items() if not isinstance(s.weapon, vc.Shell)}   # a gun round flies its template's
    every = [*looks.values(), vc.SAZABI_MISSILE.weapon.look]
    for look in every:
        assert look.model in vc.STORE_MODELS and look.path == f'app:/WEAPON/{look.model}.rab', look
        assert abs(look.size * vc.STORE_MODELS[look.model] - look.length) <= vc.STORE_LENGTH_SLACK, look
    assert {look.model for look in every} == set(vc.STORE_MODELS), 'a model in STORE_MODELS no weapon flies'
    assert make_katyusha.ROCKET_MODEL_LEN == vc.STORE_MODELS['bullet_rocket'], 'the Katyusha measures the same model'

    def kind(weapon: str) -> str | None:
        got = vc.store_of(weapon if weapon.startswith('app:/') else 'app:/weapon/' + weapon.lower())
        return got[0] if got else None

    def apart(where: object, models: list[str]) -> None:
        assert len(models) == len(set(models)), f'{where}: rounds cycled on one control look alike: {models}'

    for name, jet in vc.JETS.items():
        apart(name, [looks[k].model for k in sorted({kind(w) for w in jet.weapons} - {None})])
    beside: dict[tuple[str, int], list[str]] = {}
    for stem, mounts in mss.LOADOUTS.items():
        for m in mounts:
            if kind(m.weapon) in looks:
                beside.setdefault((stem, m.like), []).append(looks[kind(m.weapon)].model)
    for where, models in beside.items():
        apart(where, models)
    import rootcpk
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.default()
    for model, length in vc.STORE_MODELS.items():
        assert abs(vc.model_length(game.read('WEAPON', f'{model}.rab')) - length) <= vc.STORE_MODEL_SLACK, model
    built = vc.jet_guns(game)
    with patched(vc, _look=lambda *_: None):
        plain, plain_sazabi = vc.jet_guns(game), vc.sazabi_weapons(game)
    assert vc.sazabi_weapons(game)[vc.SAZABI_MISSILE_FILE] == plain_sazabi[vc.SAZABI_MISSILE_FILE], 'the Sazabi missile changed'
    shape = ('AmmoModel', 'AmmoSize', 'AmmoHitSizeAdjust', 'resource')
    for name in vc.STORE_FILES:
        look = looks[kind(name)]
        got, was = (dsgo.to_py(dsgo.parse(files[name]).root) for files in (built, plain))
        assert list(got) == list(was) and all(got[k] == was[k] for k in got if k not in shape), name
        assert was['resource'] == [was['AmmoModel']], f'{name}: the template preloads its own model alone'
        assert (got['AmmoModel'], got['resource'], got['AmmoSize']) == (look.path, [look.path], look.size), name
        contact, stock = got['AmmoSize'] * got['AmmoHitSizeAdjust'], was['AmmoSize'] * was['AmmoHitSizeAdjust']
        assert abs(contact - stock) < 1e-9, f'{name}: contact sphere {contact}, the template {stock}'
    # The stock weapon on the holder a stock vehicle's stores hang beside: another model than theirs.
    rows = {stem: mss.stock_rows(game, stem) for stem in mss.LOADOUTS}
    for request in mss.requests(game):
        entry, stem = mss._brought(dsgo.parse(game.read('WEAPON', request)))
        stock = mss._request_list(entry, rows[stem], request)
        for like in {m.like for m in mss.LOADOUTS[stem]}:
            w = stock.items[like]
            path = str(w.items[0] if isinstance(w, dsgo.Node) else w)
            try:
                own = dsgo.to_py(dsgo.parse(game.read('WEAPON', path.split('/')[-1])).root).get('AmmoModel')
            except (KeyError, ValueError):
                own = None   # no stock file of that name, or not a DSGO weapon: no round model to tell apart
            mine = [vc.Look(m, 1.0).path.lower() for m in beside.get((stem, like), [])]
            assert not isinstance(own, str) or own.lower() not in mine, (request, like, own)
    with patched(vc, STORE_MODELS={**vc.STORE_MODELS, 'bullet_rpg': 1.2}):
        try:
            vc.jet_guns(game)
        except ValueError:
            pass
        else:
            raise AssertionError('_look took a model of another length than STORE_MODELS says')


@test
def order_append_only() -> None:
    for name, order in calls.RELEASED.items():
        assert calls.IDS[:len(order)] == order, f'{name} is no longer a prefix of CALLS: calls only go at the end'
    last = max(len(o) for o in calls.RELEASED.values())
    assert last == len(calls.IDS), ('CALLS has calls no RELEASED order lists: add the order this release '
                                    'installs to tools/calls.py RELEASED')
    for name, order in calls.WITHDRAWN.items():
        assert set(order) <= set(calls.IDS), f'{name}: a call was removed (retire it instead)'


# ---------------------------------------------------------------- where rows go


STOCK = [f'eWeapon{i:03d}' for i in range(400)]
OTHER = ['OTHERMOD_A', 'OTHERMOD_B']


def _installs() -> dict[str, list[str]]:
    shipped = {**calls.RELEASED, **calls.WITHDRAWN}
    return {name: STOCK + list(order) for name, order in shipped.items()}


@test
def install_keeps_rows() -> None:
    for name, ids in _installs().items():
        for after in ([], OTHER):
            table = ids + after
            plan = cw.plan_rows(table)
            for i, x in enumerate(table):
                if calls.slot_of(x):
                    assert plan.at[x] == i, f'{name}: {x} moved'
            import ported_weapons as pw   # every EDF5 weapon's row follows the calls' (edf5_weapons_rows)
            assert plan.appended == [c for c in calls.IDS if c not in table] + list(pw.IDS), name
            assert sorted(plan.at[c] for c in plan.appended) == list(range(len(table), len(table) + len(plan.appended)))
    fresh = cw.plan_rows(STOCK)
    assert [fresh.at[c] for c in calls.IDS] == list(range(len(STOCK), len(STOCK) + len(calls.IDS)))


@test
def placeholders_keep_rows() -> None:
    table = STOCK + [calls.retired_id(x) for x in calls.IDS[:19]] + OTHER
    plan = cw.plan_rows(table)
    for i, x in enumerate(calls.IDS[:19]):
        assert plan.at[x] == len(STOCK) + i
    assert cw.tail_start(table) == len(table)                      # another mod's rows end it: nothing to delete
    assert cw.tail_start(STOCK + list(calls.IDS)) == len(STOCK)    # ours end it: all of them can go
    mixed = STOCK + list(calls.IDS[:5]) + OTHER + list(calls.IDS[5:])
    assert cw.tail_start(mixed) == len(STOCK) + 5 + len(OTHER)
    try:
        cw.plan_rows(STOCK + [calls.IDS[0], calls.retired_id(calls.IDS[0])])
    except ValueError:
        pass
    else:
        raise AssertionError('two rows for one call not refused')


@test
def jet_masses_cover_every_jet() -> None:
    """Every aircraft kind has a clean mass and a durability (src/stores.inc kJetMasses): any of them the player flies
    rams with its own mass (src/playerjet.cpp RamDamage), scaled by its HP over that durability."""
    marks = {j.mark for j in vc.JETS.values() if j.mark != 7101.0 and not j.creature} | {make_jets.GUNSHIP_MARK}   # the sub and enemy creatures have no player stores or ram mass
    missing = sorted(marks - set(vc.JET_MASSES))
    assert not missing, f'pylib/vcobjects.py JET_MASSES: no mass for marks {missing}'
    durability = gen_stores.durabilities()
    assert all(durability.get(m, 0.0) > 0.0 for m in vc.JET_MASSES), 'a JET_MASSES mark with no durability'
    assert all(m > 0.0 for m in vc.JET_MASSES.values())


# ---------------------------------------------------------------- copies kept by hand


@test
def sazabi_bones_agree() -> None:
    """src/sazabi_pose.h kBones is pylib/sazabi_model.py SKELETON's sz_ bones, in its order, each with its parent; the
    Sazabi's request mark is src/body506.cpp's Sazabi range and src/sazabi.cpp's."""
    import sazabi_model as sz
    rows = re.findall(r'\{L"(sz_\w+)",(-1|k\w+)\}', src('src/sazabi_pose.h').split('kBones[kBoneCount]={', 1)[1].split('};', 1)[0])
    names = [n for n, _ in sz.SKELETON if n.startswith('sz_')]
    assert [n for n, _ in rows] == names, f'src/sazabi_pose.h kBones: {[n for n, _ in rows]} != {names}'
    enum = re.findall(r'\b(k[A-Z]\w*)', src('src/sazabi_pose.h').split('enum Bone {', 1)[1].split('kBoneCount', 1)[0])
    parent = dict(sz.SKELETON)
    for (name, par), _ in zip(rows, enum):
        want = parent[name]
        assert (par == '-1') == (want == 'body') and (par == '-1' or names[enum.index(par)] == want), f'{name}: parent {par}'
    assert f'{{{vc.SAZABI_MARK:.1f}f,' in src('src/body506.cpp').replace(' ', ''), 'src/body506.cpp kMarks lacks the Sazabi'
    assert f'kSazabiMark={vc.SAZABI_MARK:.1f}f' in src('src/sazabi.cpp'), 'src/sazabi.cpp kSazabiMark'



@test
def sazabi_rifle_files() -> None:
    """The model folder's rifle (tools/prep_sazabi_rifle.py, pylib/sazabi_arms.py): neither file -> the rifle of boxes and
    RIFLE_MUZZLE; both -> the OBJ on the right hand and sz_muzzle from the JSON; one without the other -> an error."""
    import sazabi_arms
    at = {'sz_hand_r': (1.0, 2.0, 3.0), 'sz_forearm_l': (4.0, 15.0, 0.0), 'sz_hand_l': (4.0, 11.0, 2.0)}
    d = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        assert sazabi_arms.rifle_files(d) is None and sazabi_arms.muzzle(None) == sazabi_arms.RIFLE_MUZZLE
        assert sazabi_arms.joints(at, d)['sz_muzzle'] == (1.0, 2.25, 13.6)
        with open(os.path.join(d, sazabi_arms.RIFLE_FILE), 'w', encoding='utf-8') as h:
            h.write('\n'.join(['o sz_rifle', 'usemtl 07___Default', 'v 0 0 0', 'v 1 0 0', 'v 0 1 0', 'vn 0 0 1',
                               'f 1//1 2//1 3//1']) + '\n')
        try:
            sazabi_arms.rifle(at, d)
            raise AssertionError('an OBJ without its JSON is no error')
        except FileNotFoundError:
            pass
        with open(os.path.join(d, sazabi_arms.RIFLE_INFO), 'w', encoding='utf-8') as h:
            json.dump({'muzzle': [0.5, -2.0, 10.0]}, h)
        assert sazabi_arms.joints(at, d)['sz_muzzle'] == (1.5, 0.0, 13.0)
        parts = sazabi_arms.rifle(at, d)
        assert [p.name for p in parts] == ['sz_rifle'] and parts[0].verts[1].pos == (2.0, 2.0, 3.0), parts
    finally:
        shutil.rmtree(d, ignore_errors=True)


@test
def hand_copies_agree() -> None:
    # kKinds rows: {"name",mark,...}, the mark an integer or a float literal.
    pjet = dict(re.findall(r'\{"(\w+)",\s*(\d+)(?:\.0f)?\s*,', src('src/playerjet.cpp').split('kKinds[]={', 1)[1].split('};', 1)[0]))
    for c in calls.CALLS:
        if c.brings != 'vehicle' or c.ground:
            continue
        jet = vc.JETS[c.jet]
        if c.jet == vc.SAZABI_JET:   # the Sazabi: tools/make_sazabi.py writes it, src/sazabi.cpp knows its mark
            import make_sazabi
            assert jet.mark == c.mark == vc.SAZABI_MARK and make_sazabi.SGO_FILE == f'{c.vehicle}.SGO', c.id
            continue
        assert jet.mark == c.mark and make_jets.FILES[f'{c.vehicle}.SGO'] == c.jet, c.id
        if jet.player:   # a player jet: its mark src/playerjet.cpp kKinds'
            assert float(pjet[c.kind.removeprefix('pjet_')]) == c.mark, f'src/playerjet.cpp kKinds disagrees on {c.id}'
        else:            # one of the plugin's other aircraft: a requested twin (every_boardable_aircraft_requested)
            assert jet.requested and jet.parked and c.jet in {vc.request_name(k) for k in vc.REQUEST_KINDS}, c.id
    # The jets' marks and files, wherever src/jet*.cpp / *.h keep their table (kKinds, kCarrierMarks, kJetFile /
    # kJetSgo, or one body table).
    jet_src = ''.join(src(f'src/{n}') for n in sorted(os.listdir(os.path.join(ROOT, 'src'))) if n.startswith('jet'))
    marks = {float(m) for m in re.findall(r'\b(70\d\d)\.0f', jet_src)}
    npc = {name: j.mark for name, j in vc.JETS.items() if not j.player and 7001 <= j.mark <= 7099}
    for name, mark in npc.items():
        assert mark in marks, f'{name}: mark {mark} is in no src/jet* table'
    files = set(re.findall(r'L"(EDF6VC_(?!CALL_)[A-Z0-9_]+\.SGO)"', jet_src))
    sgos = set(re.findall(r'L"app:/object/(edf6vc_[a-z0-9_]+\.sgo)"', jet_src))
    assert files and {f.lower() for f in files} == sgos, 'src/jet*: the file names and the app:/object paths disagree'
    # the drill's: tools/make_drill.py; the EMC's: tools/make_emc.py; the Sazabi's beams: tools/make_sazabi.py; the
    # Proteus's shield: tools/make_proteus.py
    import make_proteus
    written = ({n.split('/', 1)[1] for n in make_jets.names()} | {vc.DRILL_CHARGE_FILE} | set(vc.EMC_FILES)
               | set(vc.SAZABI_ROUND_FILES) | {make_proteus.SHIELD_FILE.split('/', 1)[1]})
    assert files <= written, f'src/jet* loads files tools/make_jets.py does not write: {sorted(files - written)}'


@test
def ground_mission_builders_take_the_game_alone() -> None:
    """testrange/gen.py GROUND_MISSION calls each tool's vehicle_sgo(game): every other parameter has a default (the
    howitzer's grew a required own_model, and every range placing it would have failed at install)."""
    import importlib
    import inspect
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    for name, tool in gen.GROUND_MISSION.items():
        params = list(inspect.signature(importlib.import_module(tool).vehicle_sgo).parameters.values())[1:]
        assert all(p.default is not inspect.Parameter.empty for p in params), f'{tool}.vehicle_sgo for {name}: {params}'


@test
def drill_copies_agree() -> None:
    """src/drill.cpp's drill (marker and spin bone names, length, base radius, base, rotational repeat) is pylib/drill_model.py's, src/jet_bay.cpp's drill charge
    is the one tools/make_drill.py writes (pylib/vcobjects.py DRILL_CHARGE_FILE), its blast breaks buildings (>= 3 m),
    the drill tank's request is a ground vehicle request of tools/make_drill.py's vehicle, and the model turns the OBJ
    without mirroring it."""
    import drill_model
    import make_drill
    d = src('src/drill.cpp')
    assert f'kDrillBone[]=L"{drill_model.DRILL_BONE}"' in d, 'src/drill.cpp kDrillBone'
    assert f'kSpinBone[]=L"{drill_model.SPIN_BONE}"' in d, 'src/drill.cpp kSpinBone'
    m = re.search(r'kBoxHalfX=([\d.]+)f', d)
    assert m and 2 * float(m.group(1)) <= make_drill.HULL_WIDTH + 1e-6, 'the contact box is no wider than the hull'
    m = re.search(r'kHullFront=([\d.]+)f,kChargeFrom=([\d.]+)f', d)
    assert m and float(m.group(1)) < drill_model.DRILL_BASE[2] + drill_model.DRILL_LENGTH and float(m.group(2)) >= 3.4, m and m.groups()
    m = re.search(r'kDrillLength=([\d.]+)f,kDrillRadius=([\d.]+)f', d)
    assert m and (float(m.group(1)), float(m.group(2))) == (drill_model.DRILL_LENGTH, drill_model.DRILL_RADIUS), m and m.groups()
    m = re.search(r'kDrillBaseY=([\d.]+)f,kDrillBaseZ=([\d.]+)f', d)
    assert m and (0.0, float(m.group(1)), float(m.group(2))) == drill_model.DRILL_BASE, m and m.groups()
    m = re.search(r'kSpinRepeat=2\.0f\*kPi/([\d.]+)f', d)
    assert m and float(m.group(1)) == drill_model.DRILL_FOLDS, m and m.groups()
    bay = src('src/jet_bay.cpp')
    assert f'kDrillChargeFile[]=L"{vc.DRILL_CHARGE_FILE}"' in bay, 'src/jet_bay.cpp kDrillChargeFile'
    assert f'kDrillChargeSgo[]=L"app:/object/{vc.DRILL_CHARGE_FILE.lower()}"' in bay, 'src/jet_bay.cpp kDrillChargeSgo'
    assert vc.DRILL_CHARGE_RADIUS >= 3.0, 'a drill charge under 3 m breaks no building (docs/drill-re.md §3)'
    drills = [c for c in calls.CALLS if c.ground == 'drill']
    assert len(drills) == 1 and drills[0].vehicle == make_drill.VEHICLE.sgo and drills[0].mark == 0
    assert make_drill.OWNER in ledger.OWNERS and make_drill.VEHICLE.tool == 'make_drill'
    assert not drill_model.CONVERSION.mirrors() and drill_model.CONVERSION.point((1.0, 0.0, 0.0))[2] > 0, 'OBJ +X is forward'


@test
def drill_settings_documented() -> None:
    """Every Drill* key src/plugin.cpp reads is in the shipped ini and the README; the retired DrillHeatSec (its 12 s
    default overheated too soon; an installed ini keeps the old value, so the longer default came as the new key
    DrillOverheatSec) is read by nobody, shipped by no ini and named in IgnoreRetired."""
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    keys = set(re.findall(r'Read\w+\(L"(Drill\w*)"', plugin))
    assert {'DrillOverheatSec', 'DrillKillCool', 'DrillLaunch', 'DrillLaunchKey', 'DrillLaunchButton'} <= keys, keys
    for key in keys:
        assert re.search(rf'^{key}=', ini, re.M), f'{key} not in EDF6VehicleCrew.ini'
        assert key == 'Drill' or key in readme, f'{key} not in README.md'
    assert 'DrillHeatSec' not in keys and not re.search(r'^DrillHeatSec=', ini, re.M)
    assert '{L"DrillHeatSec",' in plugin.split('void IgnoreRetired', 1)[1].split('\n}', 1)[0]


@test
def emc_copies_agree() -> None:
    """The EMC's charged beam (src/emc.cpp, src/emc_plan.h, pylib/vcobjects.py EMC_*, tools/make_emc.py): src/jet_bay.cpp
    preloads and fires exactly the files tools/make_emc.py writes, in EmcRound's order; the beam's reach is the SGO's
    (speed x life) and src/emc_plan.h's; the C++ copies of the glow's first thickness, the blast SGO's radius and the
    charges' flights agree with the SGOs (a break charge reaches past the face it is aimed through); the trigger is taken
    before the stock input and the frame runs after it; the tool is the installer's and the ledger's; its ini keys are
    read, shipped and documented; the offline check is a target. With the game here: the SGOs build and pass check_emc."""
    import make_emc
    bay, plan, emc = src('src/jet_bay.cpp'), src('src/emc_plan.h'), src('src/emc.cpp')
    files = re.findall(r'\{L"app:/object/(edf6vc_emc_[a-z_]+\.sgo)",L"(EDF6VC_EMC_[A-Z_]+\.SGO)"', bay)
    assert [f for _, f in files] == list(vc.EMC_FILES) and all(s == f.lower() for s, f in files), files
    assert re.search(r'enum class EmcRound \{ beam, sight, breakCharge, blast, szMega, szCharge, szFunnel, proteusShield \};',
                     src('src/crew.h')), 'src/crew.h EmcRound'
    # after the EMC's, the Sazabi's beams (src/sazabi_arms.inc), in EmcRound's order: the files tools/make_sazabi.py writes
    sz_files = re.findall(r'\{L"app:/object/(edf6vc_sz_[a-z_]+\.sgo)",L"(EDF6VC_SZ_[A-Z_]+\.SGO)"', bay)
    assert [f for _, f in sz_files] == list(vc.SAZABI_ROUND_FILES) and all(s == f.lower() for s, f in sz_files), sz_files
    assert bay.index('EDF6VC_EMC_BLAST.SGO') < bay.index(vc.SAZABI_ROUND_FILES[0]), "kEmcFiles: the EMC's first"
    assert 'vc.sazabi_rounds(game)' in src('tools/make_sazabi.py'), "tools/make_sazabi.py writes the Sazabi's beams"
    assert make_emc.names() == [f'OBJECT/{n}' for n in vc.EMC_FILES] and make_emc.OWNER in ledger.OWNERS
    m = re.search(r'kBeamRange=([\d.]+)f', plan)
    assert m and float(m.group(1)) == vc.EMC_BEAM_RANGE == vc.EMC_BEAM_SPEED * vc.EMC_BEAM_LIFE, m and m.group(1)
    m = re.search(r'kSightThin=([\d.]+)f,kSightThick=([\d.]+)f', emc)
    assert m and float(m.group(1)) == vc.EMC_SIGHT_SIZE and float(m.group(2)) > vc.EMC_SIGHT_SIZE, m and m.groups()
    m = re.search(r'kSgoBlastRadius=([\d.]+)f', emc)
    assert m and float(m.group(1)) == vc.EMC_BLAST_RADIUS, m and m.group(1)
    m = re.search(r'kAhead=([\d.]+)f,kLead=([\d.]+)f,kInto=([\d.]+)f,kBlastLead=([\d.]+)f', emc)
    assert m, 'src/emc.cpp kAhead / kLead / kInto / kBlastLead'
    lead, into, blast_lead = (float(m.group(i)) for i in (2, 3, 4))
    assert lead + into < vc.EMC_BREAK_SPEED * vc.EMC_BREAK_LIFE, 'a break charge must fly past the face it is aimed through'
    assert blast_lead + into < vc.EMC_BLAST_SPEED * vc.EMC_BLAST_LIFE, 'the blast charge must fly past the end it is aimed at'
    assert vc.EMC_BREAK_RADIUS >= 3.0 and vc.EMC_BLAST_RADIUS >= 3.0, 'a blast under 3 m breaks no building (docs/drill-re.md §3)'
    m = re.search(r'kBreakSpacing=([\d.]+)f', plan)
    assert m and float(m.group(1)) + into <= vc.EMC_BREAK_RADIUS, 'two buildings closer than the spacing are both in one blast'
    crew = src('src/crew.cpp')
    hook = crew.split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    i_in, i_stock, i_frame = hook.find('&EmcInput,'), hook.find('nextInput[I]('), hook.find('&EmcFrame,')
    assert 0 <= i_in < i_stock < i_frame, 'src/crew.cpp InputHook: EmcInput before the stock input, EmcFrame after'
    # The plugin off mid-charge: the frame and the tick still run (the charge let go, a gone EMC's loop stopped).
    off = disabled_return_offset(hook)
    assert i_frame < off and 0 <= hook.find('&EmcTick)') < off, 'EmcFrame / EmcTick run with the plugin off'
    assert 'Cfg().enabled && Cfg().emcBeam' in emc, 'emc.cpp Ready: off with the plugin'
    # The HUD's EMC line is the EMC's own vehicle's (its position), as the Proteus readout is.
    assert 'float pos[3]; };' in src('src/crew.h').split('struct EmcCue', 1)[1].split('\n', 1)[0]
    assert 'std::memcpy(c.pos,v+kPosition,12);' in emc and 'x.emc && vec::Dist(x.emc->pos,r.pos)<2.0f' in src('src/hud.cpp')
    assert 'ResetEmc();' in src('src/mission.cpp') and 'InstallEmc();' in src('src/plugin.cpp')
    inst = src('tools/installer.py')
    assert 'build_asset(cache, make_emc,' in inst and 'make_emc.install(game, emc)' in inst and 'make_emc.remove' in inst
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('EmcBeam', 'EmcChargeSec', 'EmcBeamSec', 'EmcBlastRadius', 'EmcBlastShare', 'EmcBreak'):
        assert re.search(rf'^{key}=', ini, re.M) and f'L"{key}"' in plugin and key in readme, key
    cmake = src('CMakeLists.txt')
    assert 'src/emc.cpp' in cmake and 'add_executable(emc_check EXCLUDE_FROM_ALL tools/emc_check.cpp)' in cmake
    import rootcpk
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        make_emc.build(rootcpk.DEFAULT_GAME)


@test
def sidecar_copies_agree() -> None:
    """The sidecar motorcycle (src/sidecar.cpp, pylib/sidecar_model.py, tools/make_sidecar.py): the C++ copies of the
    marker bone, the gunner's point (on the tub's floor) and the tub's outer side are the model's; its request is a ground vehicle
    request of the Freed bike's class and request (a Ranger's vehicle), its notes do not ask for EDF6AutoTurret and
    it needs no stock weapon installed; the plugin is wired through (the input step, the board button before the
    stock seat search, no NPC driver while the player is in the sidecar, the mission reset, the setAngVel redirect,
    the build); its ini keys are read, shipped and documented. With the game: the whole build passes its checks (the
    model against the stock bones, the ragdoll against the stock one byte for byte outside the edit)."""
    import make_sidecar
    import sidecar_model as sm
    c = src('src/sidecar.cpp')
    assert f'kMarkerBone[]=L"{sm.MARKER_BONE}"' in c, 'src/sidecar.cpp kMarkerBone'
    m = re.search(r'kGunnerX=(-?[\d.]+)f,kGunnerY=(-?[\d.]+)f,kGunnerZ=(-?[\d.]+)f', c)
    assert m and tuple(float(x) for x in m.groups()) == sm.GUNNER_POINT, m and m.groups()
    m = re.search(r'kTubOut=(-?[\d.]+)f', c)
    assert m and float(m.group(1)) == sm.TUB_OUT == sm.TUB[0][0] == sm.FLOOR[0][0], m and m.groups()
    assert sm.TUB_OUT == sm.TUB_X - sm.TUB_HALF_WIDTH and sm.TUB_IN == sm.TUB_X + sm.TUB_HALF_WIDTH
    assert sm.FLOOR[0][1] < sm.GUNNER_POINT[1] == sm.FLOOR[1][1] == sm.FLOOR_Y, 'the gunner stands on the floor slab'
    assert all(sm.FLOOR[0][k] < sm.GUNNER_POINT[k] < sm.FLOOR[1][k] for k in (0, 2)), 'the gunner over the floor'
    assert sm.SEAT_FRONT < sm.GUNNER_POINT[2] < sm.DECK_Z, 'the gunner between the seat and the deck'
    rows = [x for x in calls.CALLS if x.ground == 'sidecar']
    assert len(rows) == 1 and rows[0].vehicle == make_sidecar.VEHICLE.sgo and rows[0].mark == 0 and rows[0].brings == 'vehicle'
    assert make_sidecar.VEHICLE.stock == 'V503_BIKE' and make_sidecar.VEHICLE.request == 'AWEAPON338'
    assert make_sidecar.OWNER in ledger.OWNERS and make_sidecar.VEHICLE.tool == 'make_sidecar'
    assert cw.vehicle_needs(rows[0]) == [f"OBJECT/{make_sidecar.SGO_FILE}"], cw.vehicle_needs(rows[0])
    for lang in ('SC', 'CN', 'JA', 'EN', 'KR'):
        text = calls.call_description(rows[0], lang)
        assert 'EDF6AutoTurret' not in text and 'EDF6VehicleCrew' in text, (lang, text)
    crew = src('src/crew.cpp')
    hook = crew.split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    assert '&SidecarFrame,' in hook.split('nextInput[I](', 1)[1], 'SidecarFrame runs after the stock input'
    board = crew.split('unsigned char* __fastcall FindSeatHook(', 1)[1].split('\n}\n', 1)[0]
    assert 0 <= board.find('SidecarBoard(') < board.find('originalFindSeat(vehicle,human)'), 'the sidecar before the stock seat'
    assert 'SidecarHoldsPlayer(vehicle)' in crew.split('void Crew(', 1)[1].split('\n}\n', 1)[0], 'no NPC driver'
    assert 'ResetSidecars();' in src('src/mission.cpp')
    phys = src('src/physics.cpp')
    assert 'SidecarLevel(body,spin)' in phys and '&ChassisSetAngVel' in phys and '&FinalAngProbe' not in phys
    assert 'src/sidecar.cpp' in src('CMakeLists.txt') and 'InstallSidecar();' in src('src/plugin.cpp')
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    # The passengers' rounds pass their own bike by the bullets' hook (jet_hooks.cpp InstallBulletPass): installed on
    # its own, ahead of the heli profile's jets and the sidecar, never from InstallJets (a heli mismatch took it away),
    # and no part of the sidecar's switch.
    load = plugin.split('EML6_Load(', 1)[1]
    assert load.index('InstallBulletPass();') < load.index('if(heli)') < load.index('InstallSidecar();'), 'InstallBulletPass'
    jets = src('src/jet_hooks.cpp').split('bool InstallJets() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'kAddBodySlot' not in jets, 'the bullets hook installed from InstallJets'
    switch = src('src/sidecar.cpp').split('ok=ok && moveOk', 1)[1].split(';', 1)[0]
    assert 'Hooked' not in switch and 'blastOk' not in switch, 'a sidecar sub-channel in its master switch'
    for key in ('Sidecar', 'SidecarNpcGunner', 'SidecarNpcRange'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    assert 'Fix("SidecarNpcRange"' in plugin, 'SidecarNpcRange is not range-checked'
    import rootcpk
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        game = rootcpk.default()
        shkt, _info = sm.build_collision(game)
        files = {f'OBJECT/{make_sidecar.MODEL_FILE}': sm.build(game), f'OBJECT/{make_sidecar.RAGDOLL_FILE}': shkt,
                 f'OBJECT/{make_sidecar.SGO_FILE}': make_sidecar.vehicle_sgo(game)}
        make_sidecar.check(files, game)


@test
def sidecar_tub_holds_the_gunner() -> None:
    """The sidecar's tub (pylib/sidecar_model.py sidecar_parts, no game needed) is a tub the gunner stands IN: a floor
    face at FLOOR_Y under GUNNER_POINT (not floating over it, not sunk in it), walled all round from the floor up to
    the lowest rim with the walls at least SOLDIER_REACH off (their legs and hips do not go through), the sides
    beside them up to RIM_Y (their hips), every face drawn the way it is seen; the point is the floor slab's top (the
    collision the plugin stands them on) and inside the tub's plan."""
    import sidecar_model as sm
    got = sm.check_tub(sm.parts_triangles(list(sm.sidecar_parts(2).values())), sm.SOLDIER_REACH)
    assert got['floor y'] == sm.FLOOR_Y == sm.FLOOR[1][1] == sm.GUNNER_POINT[1], got
    assert got['rays walled'] == 144 and got['faces seen from behind'] == 0, got
    assert 0.60 <= sm.RIM_Y - sm.FLOOR_Y <= 0.70, "low side wall preserves leg space without enclosing the standing gunner"
    assert abs(sm.half_width(sm.GUNNER_POINT[2]) - sm.TUB_HALF_WIDTH) < 0.05, "the gunner at the tub's widest"


@test
def weapon_marks_agree() -> None:
    """The mod's LockonTargetType marks: one copy in C++ (common/edf/weapon.h), the data tools' copies equal to it."""
    marks = dict(re.findall(r'(kMark\w+)=(\d+)', src('common/edf/weapon.h')))
    assert marks == {'kMarkAir': '7301', 'kMarkGround': '7302', 'kMarkLofted': '7303'}, marks
    assert at_build.MARK_AIR == 7301.0 and at_build.MARK_GROUND == 7302.0
    assert make_artillery.MARK_GROUND == 7302.0 and make_katyusha.MARK_LOFTED == 7303.0
    assert make_katyusha.ROCKETS['LockonTargetType'] == make_katyusha.MARK_LOFTED


@test
def sight_zoom_wired() -> None:
    """The vehicle sight's magnification (src/sightzoom.cpp): its ini keys are read, range-checked, shipped and
    documented; it hooks the camera step docs/zoom-re.md names (CharacterGhostCamera slot 4, the field of view's
    write checked) by chaining (map.cpp hooks the same slot); the gunship gunner and every stock vehicle's input
    step it; the turret camera's rate is slowed by it; a new mission puts it back to 1x."""
    code, ini, readme = src('src/sightzoom.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    plugin, doc = src('src/plugin.cpp'), src('docs/zoom-re.md')
    for key in ('SightZoom', 'SightZoomKey', 'SightZoomButton'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in ('SightZoomKey', 'SightZoomButton'):
        assert f'FixInt("{key}"' in plugin, f'{key} is not range-checked'
    assert 'kCamVtable=0x1768C10' in code and 'kCamStep=0xF86A0' in code and 'kFovWrite=0xF8906' in code
    assert '0xF86A0' in doc and '0x1768C30' in doc and 'cam+0x24' in doc
    assert 'ChainVtableSlot' in code and 'InstallSightZoom();' in plugin
    assert 'SightZoomFrame(v,kGunnerSeat,true)' in src('src/playerjet_crew.inc')
    assert '&SightZoomStock,v' in src('src/crew.cpp')
    assert 'sightzoom::Rate(' in src('src/turretcam.cpp')
    assert 'ResetSightZoom();' in src('src/mission.cpp')
    assert 'src/sightzoom.cpp' in src('CMakeLists.txt')
    # The magnified sight's picture (src/scopeview.h): drawn first in HudDraw (every mark over it) whenever the sight is
    # magnified, the gunship gunner's a sensor rectangle; its offline check under CTest; the heli gun's ladder.
    hud = src('src/hud.cpp')
    draw = hud.split('void HudDraw(const float* viewProj,', 1)[1]
    assert draw.index('ScopeShade(') < draw.index('CarrierBars(')
    assert 'sightzoom::MaskOf(sightKind)' in draw and 'SightZoomView()' in draw
    assert 'SeatCapability(v,seat)' in code and 'HighCamOn(v) || TurretCamHighTransition(v)' in code
    assert 'LadderTicks(drawer,ctx,text,vp,width,height,s,h.ladder,' in hud and 'r.ladder=gunsight::Of(' in src('src/helisight.cpp')
    cmake = src('CMakeLists.txt')
    assert 'EXCLUDE_FROM_ALL tools/scopeview_check.cpp' in cmake and 'scopeview_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    assert 'scopeview.h' in readme


BOHR_STOCK_AMMO_ALIVE = 100.0   # V603_FLAK_GLGUN01_DLC_{L,R}.SGO AmmoAlive in the stock Root.cpk


@test
def high_cam_wired() -> None:
    """The high camera (src/highcam.cpp's toggle, src/turretcam.cpp's placement): its ini keys are read, shipped (with a
    range said) and documented; the plugin takes the Katyusha's and the howitzer's weapons for indirect fire (rounds
    living kIndirectLife frames or more) and not the other ground-marked guns (EDF6AutoTurret's Bohr grenades, stock
    life); the raised own cameras of both look down onto the ground ahead; the toggle writes no camera block any more
    (the riding camera never reads game_object_camera_setting: docs/camera-re.md §3b)."""
    import rootcpk
    code, ini, readme, doc = src('src/highcam.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/camera-re.md')
    plugin = src('src/plugin.cpp')
    for key in ('HighCam', 'HighCamKey', 'HighCamButton', 'HighCamHeight', 'HighCamBack', 'HighCamPitch', 'HighCamClass'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in ('HighCamKey', 'HighCamButton', 'HighCamHeight', 'HighCamBack', 'HighCamPitch', 'HighCamClass'):
        assert f'FixInt("{key}"' in plugin or f'Fix("{key}"' in plugin, f'{key} is not range-checked'
    life = int(re.search(r'kIndirectLife=(\d+)', code).group(1))
    assert make_katyusha.ROCKETS['AmmoAlive'] >= life and make_artillery.SHELLS['AmmoAlive'] >= life
    # The Bohr's grenades keep the stock life (autoturret/tools/build.py leaves AmmoAlive alone). The stock value is
    # pinned here so CI (no game) still checks it against kIndirectLife; with the game present it is re-read.
    assert BOHR_STOCK_AMMO_ALIVE < life, (BOHR_STOCK_AMMO_ALIVE, life)
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        for side in 'LR':
            bohr = dsgo.to_py(dsgo.parse(rootcpk.default().read('WEAPON', at_build.BOHR_GUN.format(side=side))).root)
            assert bohr['AmmoAlive'] == BOHR_STOCK_AMMO_ALIVE, (side, bohr['AmmoAlive'])
    assert not re.search(r'=0x1[78]0\b', code), 'highcam.cpp writes the object camera block again'
    for rva in ('0x54DDF0', '0xF86A0', '0xFAF20', '0xFB9F0', '0xFC01B', '+0x170', '+0x180', '+0x190'):
        assert rva in doc, rva
    vc.check_artillery_camera(make_katyusha.CAMERA)
    vc.check_artillery_camera(make_artillery.CAMERA)


@test
def turret_cam_wired() -> None:
    """The turret camera (src/turretcam.cpp, README 功能 12): its ini keys are read, range-checked, shipped and documented;
    the hooks it patches are the ones docs/camera-re.md gives (the look-at fetch's call site, the seat aim's step and its
    vtable), with their signature checked; the camera offsets it writes agree with the doc; the offline check of its math
    (tools/turret_cam_check.cpp) is a CMake target and uses the header the plugin uses; it runs from every vehicle's input
    and is reset with the mission."""
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/camera-re.md')
    code, cmake, crew, mission = src('src/turretcam.cpp'), src('CMakeLists.txt'), src('src/crew.cpp'), src('src/mission.cpp')
    for key in ('DecoupledTurretCam', 'TurretCamRate', 'FreeLookKey', 'FreeLookButton'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in ('TurretCamRate', 'FreeLookKey', 'FreeLookButton'):
        assert f'FixInt("{key}"' in plugin or f'Fix("{key}"' in plugin, f'{key} is not range-checked'
    for name, rva in (('kLookSite', '0xFC013'), ('kLookCall', '0xFC01B'), ('kPoint', '0x6BB5A0'), ('kAimVtable', '0x17D8A90'),
                      ('kAimStep', '0x5FCD80')):
        assert re.search(rf'{name}={rva}\b', code), (name, rva)
        assert rva in doc, rva
    for name, off in (('kCamEye', '0x630'), ('kCamLook', '0x640'), ('kSeatCamType', '0x200'), ('kSeatCamEye', '0x208'),
                      ('kSeatCamLook', '0x218'), ('kAimParams', '0x90'), ('kObjCamLook', '0x170'), ('kObjCamEye', '0x180')):
        assert re.search(rf'{name}={off}\b', code), (name, off)
        assert f'+{off}' in doc or f'{off}' in doc, off
    assert 'Matches(kLookSite,kLookSiteCode' in code and 'Matches(kAimStep,kAimStepCode' in code
    assert 'src/turretcam.cpp' in cmake and 'tools/turret_cam_check.cpp' in cmake and 'EXCLUDE_FROM_ALL tools/turret_cam_check.cpp' in cmake
    assert '#include "../src/turretcam.h"' in src('tools/turret_cam_check.cpp') and '#include "turretcam.h"' in code
    assert '&TurretCamFrame,v' in crew and 'ResetTurretCam();' in mission and 'InstallTurretCam();' in plugin


@test
def gun_stabilizer_wired() -> None:
    """The gun stabilizer (src/stab.cpp, README 功能 14, docs/camera-re.md §7): GunStabilizer is read, shipped on and
    documented; the EDF.dll code it relies on is the doc's and checked by signature (the plain aim step it chains, the axis
    step's end and the bone map it calls again, the angle / rate writes); the AddSe step reaches it through the turret
    camera's hook (StabStep in place of the next step), the plain one through its own chain; its seats are registered from
    every vehicle's input and reset with the mission; the turret camera and EDF6AutoTurret (both its paths) steer from the
    held axes with the hull's part taken out (StabHeld, aimlink V3); the HUD shows its state; the offline check
    (tools/stab_check.cpp) is a CMake target on the header the plugin uses; the class table names only vtables crew.cpp
    hooks (their input is where the seats are registered), and excludes the artillery, the drill and the mechs' pilots."""
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/camera-re.md')
    code, cmake, crew, mission = src('src/stab.cpp'), src('CMakeLists.txt'), src('src/crew.cpp'), src('src/mission.cpp')
    assert 'L"GunStabilizer"' in plugin and re.search(r'^GunStabilizer=1', ini, re.M) and 'GunStabilizer' in readme
    for name, rva in (('kPlainAimVtable', '0x17D8A68'), ('kPlainAimStep', '0x5FBDA0'), ('kAxisStepEnd', '0x5FBD78'),
                      ('kAxisMap', '0x5FC280'), ('kAxisAngleWrite', '0x5FBD12'), ('kAxisRateWrite', '0x5FBCE8')):
        assert re.search(rf'{name}={rva}\b', code), (name, rva)
        assert rva in doc, rva
    for sig in ('kPlainAimStepCode', 'kAxisStepEndCode', 'kAxisMapCode', 'kAxisAngleWriteCode', 'kAxisRateWriteCode'):
        assert f'Matches(' in code and f',{sig},sizeof({sig}))' in code, sig
    assert 'StabStep(aim,cmd,nextAim);' in src('src/turretcam.cpp'), 'the AddSe step runs the stabilizer'
    assert 'stab::Remap(' in code, 'the bones include the complete corrected displacement during remapping'
    assert '&StabFrame,v' in crew and 'ResetStabilizer();' in mission and 'InstallStabilizer();' in plugin
    assert 'src/stab.cpp' in cmake and 'EXCLUDE_FROM_ALL tools/stab_check.cpp' in cmake
    assert '#include "../src/stab.h"' in src('tools/stab_check.cpp') and '#include "stab.h"' in code
    tc = src('src/turretcam.cpp')
    steering = tc.split('void SteeringOf(', 1)[1].split('\n}\n', 1)[0]
    assert 'StabHeld(seat+kSeatAim,s->held,s->hull,s->frame)' in steering
    steer = tc.split('bool Steer(', 1)[1].split('\n}\n', 1)[0]
    assert 'tcam::SteerAxes(game.steer,want,s.held,s.hull,' in steer
    axes = src('src/turretcam.h').split('inline bool SteerAxes(', 1)[1].split('\n}\n', 1)[0]
    assert 'target-held[i]' in axes and '-hull[i];' in axes
    # The wants are seen in the frame the held axes are (stab.h HeldIn), from the bore's point at the turret's pivot
    # (turretcam.h AimOrigin): the Grape's barrel twitched left and right without either (tools/grape_turret_check.cpp).
    wants = tc.split('bool Wants(', 1)[1].split('\n}\n', 1)[0]
    assert 'Wants(seat,st.frame,target,ballistic,want)' in tc and 'tcam::AimOrigin(' in wants and 'tcam::LocalTo(frame,' in wants
    assert 'stab::HeldIn(' in code and 'EXCLUDE_FROM_ALL tools/grape_turret_check.cpp' in cmake
    assert 'grape_turret_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0]
    gtc = src('tools/grape_turret_check.cpp')
    assert all(f in gtc for f in ('tcam::AimOrigin(', 'stab::HeldIn(', 'tcam::SteerAxes(', 'stab::Step('))
    flak = src('autoturret/src/plugin.cpp')
    assert 'Stabilized(vehicle,0,stock,held,hull);' in flak and '-hull;' in flak.split('float AxisInput(', 1)[1].split('\n}\n', 1)[0]
    assert 'Stabilized(vehicle,s,aim.angle,held,hull);' in src('autoturret/src/gunner.cpp')
    assert 'r.stab=StabState(v,r.seat);' in src('src/vhud.cpp') and 'Tx::stab' in src('src/hud.cpp')
    assert 'HUDTEXT(stab,L"STAB",' in src('src/hudtext.inc')
    hooked = set(re.findall(r'\{(0x[0-9A-F]{7}),0x[0-9A-F]+,"', crew))
    table = code.split('const Class kClasses[]={', 1)[1].split('};', 1)[0]
    vts = re.findall(r'\{(0x[0-9A-F]{7}),"', table)
    assert vts and set(vts) <= hooked, set(vts) - hooked
    assert '0x17D8B50' not in vts, 'the rocket artillery (402) fires from a halt: no stabilizer'
    assert 'IsDrillTank(v)' in code and 'IndirectFireSeat(seat)' in code
    assert all(m in table for m in ('"504 Begaruta",{0.0f,0.0f}', '"Begaruta",{0.0f,0.0f}', '"612 Nix",{0.0f,0.0f}')), 'mech pilots: none'


@test
def lofted_arc_solver() -> None:
    """pylib/ballistics.py (the model common/weapon.cpp BallisticArc mirrors): the high and the low root both hit
    their point under the game's per-frame step (v += drop, p += v) within 5 cm, the high one above 45 deg and the low
    one under; out of reach is None; the Katyusha's envelope is what README.md says."""
    import math
    speed, drop = make_katyusha.ROCKETS['AmmoSpeed'], ballistics.drop_per_frame()
    for x, y in ((150.0, 0.0), (500.0, 0.0), (800.0, 30.0), (400.0, -50.0), (900.0, 0.0), (300.0, 60.0)):
        for high in (False, True):
            r = ballistics.arc(x, y, speed, drop, high)
            assert r is not None, (x, y, high)
            e, n = r
            assert (e > math.radians(45.0)) == high, (x, y, high, math.degrees(e))
            path = ballistics.fly(speed, e, drop, int(n) + 2)
            k = int(x / (speed * math.cos(e)))
            (x0, y0), (x1, y1) = path[k - 1], path[k]
            miss = y0 + (y1 - y0) * (x - x0) / (x1 - x0) - y
            assert abs(miss) < 0.05, (x, y, high, miss)
    assert ballistics.arc(1200.0, 0.0, speed, drop, True) is None
    env = ballistics.envelope(speed, drop, math.radians(make_katyusha.PITCH_STOP_DEG))
    readme = src('README.md')
    for key, unit in (('max_range', '米'), ('high_min_range', '米'), ('max_range_time', '秒'), ('high_min_time', '秒')):
        said = f'{round(env[key])} {unit}' if unit == '米' else f'{env[key]:.1f} {unit}'
        assert said in readme, f'README.md: the Katyusha section should say {said} ({key}, pylib/ballistics.py)'


@test
def katyusha_bm13_launcher() -> None:
    """The Katyusha's launcher is a BM-13 rail pack (pylib/katyusha_model.py launcher_parts): the weapon's 16 muzzles
    (MUZZLES, written into the weapon's MAB by tools/make_katyusha.py set_muzzles) one at each rocket's tail on its axis,
    01..16 in the order the rockets are fired (rocket_axes; ROCKET_BONES alike), symmetric about the launcher's middle; a
    salvo is the 16 (FireBurstCount), a load whole salvos; the parts' faces are wound outward as the stock ones are
    ((b - a) x (c - a) along the outward normal). With the game: the built files pass make_katyusha.check (the rails, each
    rocket on its own bone, the muzzles at their tails in the model and in the weapon, the launcher over the bed from 0
    to the stop with its rockets on their stops or slid back to the breech), and the archive lost exactly the textures
    only undrawn materials used and the triangles of zero area (kept_whole_bar_the_prune: nothing else changed)."""
    import math
    import katyusha_model as km
    import procmesh as pm
    xs, ys = km.rail_xs(), km.rocket_ys()
    n = km.ROCKET_COUNT
    assert len(xs) == km.RAILS == 8 and n == 16 and len(km.MUZZLES) == n == len(km.ROCKET_BONES)
    assert [m for m, _ in km.MUZZLES] == [f'{i:02d}' for i in range(1, n + 1)]
    assert [b[-2:] for b in km.ROCKET_BONES] == [m for m, _ in km.MUZZLES] and all(len(b) < 16 for b in km.ROCKET_BONES)
    for (name, (x, y, z)), (ax, ay) in zip(km.MUZZLES, km.rocket_axes()):
        assert (x, y) == (ax, ay) and z == km.ROCKET_TAIL and any(abs(x - r) < 1e-9 for r in xs), name
        assert any(abs(y - h) < 1e-9 for h in ys), name
    assert abs(sum(p[0] for _, p in km.MUZZLES)) < 1e-9 and len({p for _, p in km.MUZZLES}) == n
    assert {(x, y) for x, y in km.rocket_axes()} == {(x, y) for x in xs for y in ys}
    assert make_katyusha.ROCKETS['FireBurstCount'] == n and make_katyusha.ROCKETS['AmmoCount'] % n == 0
    assert abs(km.LOAD - (km.ROCKET_TAIL - km.RAIL_BACK)) < 1e-12 and km.LOAD > 0
    assert ys[0] - km.ROCKET_R > km.RAIL_Y + km.RAIL_H / 2 and ys[1] + km.ROCKET_R < km.RAIL_Y - km.RAIL_H / 2
    # Winding: a box turned off the axes and a tube along x, every face pointing away from the solid's middle.
    part = pm.Part(0)
    a = math.radians(30)
    km._box(part, (1.0, 2.0, 3.0), ((math.cos(a), math.sin(a), 0.0), (-math.sin(a), math.cos(a), 0.0), (0.0, 0.0, 1.0)),
            (0.3, 0.2, 0.5), [(0, 1.0)])
    boxes = len(part.tris)
    km._tube(part, (0.0, 0.0, 0.0), km.X_AXES, [(0.0, 0.1), (1.0, 0.1), (1.2, 0.03)], [(0, 1.0)])
    for k, (i, j, n) in enumerate(part.tris):
        p, q, r = (part.pos[v] for v in (i, j, n))
        normal = [((q - p)[(c + 1) % 3] * (r - p)[(c + 2) % 3] - (q - p)[(c + 2) % 3] * (r - p)[(c + 1) % 3]) for c in range(3)]
        mid = (p + q + r) / 3
        centre = (1.0, 2.0, 3.0) if k < boxes else (0.6, 0.0, 0.0)   # both convex: inside, their middles
        assert sum(normal[c] * (mid[c] - centre[c]) for c in range(3)) > 0, f'triangle {k} wound inward'
    import rootcpk
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        files = make_katyusha.build(rootcpk.DEFAULT_GAME)   # katyusha_model.check + make_katyusha.check
        make_katyusha.check(files)
        katyusha_kept_whole_bar_the_prune(rootcpk.DEFAULT_GAME, files[f'OBJECT/{make_katyusha.MODEL_FILE}'])


def katyusha_kept_whole_bar_the_prune(game_dir: str, arc: bytes) -> None:
    """The Katyusha's archive against the same build without its clean-up (katyusha_model.drop_zero_area /
    prune_textures left out): it is smaller; every member it has is the unpruned build's, byte for byte, but the model;
    the members gone are exactly the textures (and their .lod) the clean-up dropped, none of them in the texture table
    or bound by a material; the materials are the same ones, those that draw keep their textures; every mesh keeps all
    its triangles but the ones of exactly zero area (which draw nothing), each kept one's vertices byte for byte, and
    only the stock materials that no mesh draws had their texture slots pointed elsewhere."""
    from collections import Counter
    from unittest.mock import patch
    import graft_pure as g
    import katyusha_model as km
    import rootcpk
    from mdb import mdb_read, rab_read
    game = rootcpk.Game(game_dir)
    with patch.object(km, 'drop_zero_area', lambda md: (md, 0)), patch.object(km, 'prune_textures', lambda md: (md, [])):
        raw = km.build(game)
    old, new = rab_read(raw), rab_read(arc)
    assert len(arc) < len(raw), (len(arc), len(raw))
    o, n = {f.name: f for f in old.files}, {f.name: f for f in new.files}
    assert n.keys() <= o.keys() and all(n[k].stored == o[k].stored for k in n if k != km.HOST_MDB)
    md_old, md_new = mdb_read(o[km.HOST_MDB].data), mdb_read(n[km.HOST_MDB].data)
    gone = {t.filename for t in md_old.textures} - {t.filename for t in md_new.textures}
    want = {f.name for fn in gone for f in g.texture_members(old, fn)}
    assert set(o) - set(n) == want and len(want) == 2 * len(gone) > 0, (sorted(set(o) - set(n)), sorted(gone))
    drawn = {me.material for ob in md_old.objects for me in ob.meshes}
    assert [m.name for m in md_old.materials] == [m.name for m in md_new.materials]
    for i, (a, b) in enumerate(zip(md_old.materials, md_new.materials)):
        files = [[md.textures[x.texture].filename for x in m.textures] for md, m in ((md_old, a), (md_new, b))]
        assert i not in drawn or files[0] == files[1], f'{md_old.name_of(a.name)} draws and its textures changed'
        assert not set(files[1]) & gone, f'{md_old.name_of(a.name)} binds a texture that left the archive'
    flat = 0
    for ob_old, ob_new in zip(md_old.objects, md_new.objects):
        assert len(ob_old.meshes) == len(ob_new.meshes)
        for a, b in zip(ob_old.meshes, ob_new.meshes):
            def faces(me) -> Counter:  # noqa: ANN001 - mdb.Mesh
                return Counter(tuple(me.vdata[v * me.vsize:(v + 1) * me.vsize] for v in t) for t in g.triangles(me))
            lost, extra = faces(a) - faces(b), faces(b) - faces(a)
            assert not extra, 'the clean-up added triangles'
            pos = g.mesh_positions(a)
            for t in g.triangles(a):
                key = tuple(a.vdata[v * a.vsize:(v + 1) * a.vsize] for v in t)
                if lost[key]:
                    assert km._zero_area(pos[t[0]], pos[t[1]], pos[t[2]]), 'a triangle with area dropped'
                    flat += 1
                    lost[key] -= 1
    assert flat == 4, f'{flat} zero-area triangles dropped (the V607 cab has 4; the 4 on its bed rails go with the rails)'


@test
def mab_round_trips() -> None:
    """pylib/mab.py: a MAB block read and written again is the same bytes (a synthetic one here; with the game every
    stock block of the shape it reads, the Naegling launcher's among them); locators added get records, vec4s and
    strings of their own laid out as the game's (vec4s by their bytes, strings in UTF-16 order, each once), and
    vcobjects.mab_muzzles (the game's reading) finds them in order; a block of another shape is refused."""
    import struct
    from dataclasses import replace
    import mab
    sgo_block = b'SGO\0' + struct.pack('<7I', 0x102, 0, 0x20, 0, 0x20, 0, 0x20)
    base = mab.Locator('01', 'v_Null', 2, (0.0, 0.4, 4.4, 1.0), (0.05, 0.05, 0.25, 1.0), (0.0, 0.0, 0.0, 1.0), 0, sgo_block)
    m = mab.Mab((0xF, 0x83, 0), [0, 1, 2], [base, replace(base, name='02', pos=(-0.35, 0.8, 4.4, 1.0))])
    raw = mab.mab_write(m)
    back = mab.mab_read(raw)   # the same block again (its floats come back as float32 has them)
    assert mab.mab_write(back) == raw and [(x.name, x.node, x.sgo) for x in back.locators] == [(x.name, x.node, x.sgo) for x in m.locators]
    m.locators += [replace(base, name=f'{k:02d}', pos=(0.1 * k, 0.2, 0.55, 1.0)) for k in range(3, 17)]
    raw = mab.mab_write(m)
    got = vc.mab_muzzles(raw)
    assert [n for n, _node, _at in got] == [f'{k:02d}' for k in range(1, 17)] and {nd for _n, nd, _at in got} == {'v_Null'}
    assert struct.unpack_from('<3f', raw, got[15][2]) == struct.unpack('<3f', struct.pack('<3f', 1.6, 0.2, 0.55))
    head = struct.unpack_from('<HHHH', raw, 0x0C)
    assert head == (3, 0, len({mab._vec_key(v) for x in m.locators for v in (x.pos, x.b, x.c)}), 2 * (16 * 3 + len('v_Null') + 1))
    bad = bytearray(raw)
    struct.pack_into('<H', bad, 0x0E, 2)
    try:
        mab.mab_read(bytes(bad))
    except ValueError:
        pass
    else:
        raise AssertionError('a block with the 0x0E table read')
    import rootcpk
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        game = vc.Game(rootcpk.DEFAULT_GAME)
        seen = 0
        for name in game.names('WEAPON'):
            if not name.upper().endswith('.SGO'):
                continue
            with contextlib.suppress(Exception):
                block = dsgo.parse(game.read('WEAPON', name)).root.get('animation_model').items[2].data
                assert block[:4] == b'MAB\0'
                try:
                    parsed = mab.mab_read(block)
                except ValueError:
                    continue
                if mab.mab_write(parsed) != block:
                    raise AssertionError(f'{name}: the MAB block does not round-trip')
                seen += 1
        assert seen >= 100, f'{seen} stock blocks round-tripped'
        block = dsgo.parse(game.read('WEAPON', make_katyusha.STOCK_WEAPON)).root.get('animation_model').items[2].data
        assert mab.mab_write(mab.mab_read(block)) == block


@test
def katyusha_pose_agrees() -> None:
    """The Katyusha's pose (src/katyusha.cpp) is its model's (pylib/katyusha_model.py): the rod bone's name and one
    elevation stop (tools/make_katyusha.py takes the model's). EDF6AutoTurret leaves a lofted launcher the player rides
    alone before it does anything else in Steer (steering it turns the player's camera); the arc solve is one copy
    (common/weapon.cpp BallisticArc) that both plugins call. The ram's offline pose: the eye on the launcher, the
    rod turned with the cylinder, at the stroke's ends."""
    import math
    import katyusha_model as km
    assert f'kRod[]=L"{km.RAM_ROD}"' in src('src/katyusha.cpp'), 'src/katyusha.cpp kRod'
    assert make_katyusha.PITCH_STOP_DEG == km.PITCH_STOP_DEG
    # The rack (src/katyusha_rack.h, src/katyusha.cpp Rockets): its rockets, their bones' names and how far one slides
    # being loaded are the model's.
    rack = src('src/katyusha_rack.h')
    assert f'kRockets={km.ROCKET_COUNT};' in rack and f'kLoad={km.LOAD:g}f;' in rack, 'src/katyusha_rack.h kRockets / kLoad'
    prefix = km.ROCKET_BONES[0][:-2]
    assert all(b == f'{prefix}{k + 1:02d}' for k, b in enumerate(km.ROCKET_BONES))
    assert f'kRocketBone[]=L"{prefix}";' in src('src/katyusha.cpp') and 'L"%ls%02d"' in src('src/katyusha.cpp')
    at = src('autoturret/src/plugin.cpp')
    steer = at.split('float Steer(', 1)[1].split('\n}\n', 1)[0]
    assert steer.split('\n')[1].strip().startswith('if(PlayerLofted(seat))'), 'autoturret Steer: PlayerLofted first'
    assert 'bool Root(' not in at and 'bool BallisticArc(' in src('common/weapon.cpp')
    assert 'edf::BallisticArc(' in at and 'edf::BallisticArc(' in src('src/launcher.cpp')
    P, E, M = (0.0, 2.25, -2.05), (0.0, 2.56, -3.51), (0.0, 2.28, -3.55)   # the built model's, rounded
    d0, e0, l0 = km.ram_pose(P, E, M, 0.0)
    assert abs(d0) < 1e-12 and max(abs(a - b) for a, b in zip(e0, E)) < 1e-12
    d, e, length = km.ram_pose(P, E, M, math.radians(km.PITCH_STOP_DEG))
    q = km.ram_turn(E, P, d)   # the bind eye turned with the cylinder lies on the line to the posed eye
    cross = (q[1] - P[1]) * (e[2] - P[2]) - (q[2] - P[2]) * (e[1] - P[1])
    assert abs(cross) < 1e-9 and length > l0 and km.stroke(P, E, M) > 0.2, (cross, length, l0)


@test
def turret_aim_wired() -> None:
    """The player's turret (autoturret/src/designate.cpp): its ini keys are read, shipped and documented in both READMEs;
    the ownership rule (aimlink.h PlayerGunRule: the lead circle and, with the turret camera, AUTO without a lock) returns
    before the turn input is written (the flak's Steer, the gunners' SteerSeat), and each write is reported for the V2
    Steers; the two plugins' link (common/edf/aimlink.h) exports exactly the names each looks up; EDF6VehicleCrew's two
    keys are read, shipped and documented; the jets' pick scores by the view only with PlayerJetLockByView."""
    at, ini = src('autoturret/src/plugin.cpp'), src('autoturret/EDF6AutoTurret.ini')
    zh, en = src('autoturret/README.zh-CN.md'), src('autoturret/README.md')
    for key in ('AimMode', 'AimModeKey', 'AimModeButton', 'LockKey', 'LockButton', 'LockCone', 'LockRange', 'LockClearMs'):
        assert f'L"{key}"' in at and re.search(rf'^{key}=', ini, re.M) and key in zh and key in en, key
    unread = [k for k in re.findall(r'^([A-Za-z]\w*)=', ini, re.M) if f'L"{k}"' not in at]
    assert not unread, f'EDF6AutoTurret.ini keys autoturret/src/plugin.cpp never reads: {unread}'
    steer = at.split('float Steer(', 1)[1].split('\n}\n', 1)[0]
    put = steer.find('Put<float>(vehicle,kTurn')
    assert 0 <= steer.find('if(!rule.steer)return flight;') < put < steer.find('track->steered=Frame();'), 'Steer: the rule gates the turn'
    assert 'PlayerControlRule(vehicle,0,LeadCircle(),only!=nullptr)' in steer
    gunner = src('autoturret/src/gunner.cpp')
    seat = gunner.split('void SteerSeat(', 1)[1].split('\n}\n', 1)[0]
    put = seat.find('Put<float>(vehicle,kTurn')
    assert 0 <= seat.find('if(!rule.steer)return;') < put < seat.find('track.steered=autoturret::Frame();'), 'SteerSeat: the rule gates the turn'
    assert 'PlayerControlRule(vehicle,s,LeadCircle(),only!=nullptr)' in seat
    link = src('common/edf/aimlink.h')
    names = dict(re.findall(r'constexpr char (k\w+)\[\]="(\w+)";', link))
    assert set(names) == {'kViewRay', 'kMapRay', 'kTurretReadout', 'kCameraTurret', 'kSteers', 'kStabilizer', 'kStabilizerAware', 'kPriorityZone',
                          'kInputHeld', 'kSightBinding', 'kTurretObserver', 'kModeBinding', 'kPlayerAim', 'kAimsTurret'}, names   # kPriorityZone: proteus_wired; kInputHeld: map_wired
    assert names['kCameraTurret'].endswith('V2') and names['kSteers'].endswith('V2'), names
    # V4, the one player turret aim (2026-10-09): EDF6AutoTurret answers it, EDF6VehicleCrew's turret camera asks it every
    # frame and says it steers; the flak's own Steer and the tank driver's frame then leave the turret to the camera.
    assert names['kPlayerAim'].endswith('V4') and names['kAimsTurret'].endswith('V4'), names
    assert f'bool __cdecl {names["kPlayerAim"]}(' in src('autoturret/src/designate.cpp')
    assert f'bool __cdecl {names["kAimsTurret"]}(' in src('src/turretaim.cpp')
    assert 'PlayerTurretLead(v,0,TurretGun(v,seat),lead)' in src('src/turretcam.cpp'), 'turretcam.cpp: the camera asks V4'
    assert 'if(pilot && CrewAims(vehicle,0))rule=edf::aimlink::PlayerGun{false,false};' in steer, 'Steer: the camera is the hand'
    assert 'if(CrewAims(vehicle,0))return;' in gunner.split('void DriverFrame(', 1)[1].split('\n}\n', 1)[0], 'DriverFrame: V4 publishes'
    assert names['kStabilizer'].endswith('V3') and names['kStabilizerAware'].endswith('V3'), names
    assert f'bool __cdecl {names["kStabilizer"]}(' in src('src/stab.cpp') and f'bool __cdecl {names["kStabilizerAware"]}(' in at
    # The rule itself: with the camera, only a lock in AUTO steers and the stick never drags; without, V1.
    rule = link.split('inline PlayerGun PlayerGunRule(', 1)[1].split('\n}', 1)[0]
    assert 'if(!cameraTurret)return PlayerGun{!lead,!lead};' in rule and 'return PlayerGun{!lead && locked,false};' in rule
    assert f'bool __cdecl {names["kTurretReadout"]}(' in src('autoturret/src/designate.cpp')
    assert f'bool __cdecl {names["kSteers"]}(' in at
    crew = src('src/turretaim.cpp')
    assert f'bool __cdecl {names["kViewRay"]}(' in crew and f'float __cdecl {names["kMapRay"]}(' in crew
    assert f'bool __cdecl {names["kCameraTurret"]}(' in crew
    cam = src('src/turretcam.cpp')
    assert 'tcam::Foreign(AutoTurretSteers(s.v,0),in,stick,kForeign)' in cam and 'tcam::BallisticAim(' in cam
    assert '!TurretCamSteers(v)' in src('src/nix.cpp'), 'nix.cpp: the hold stands aside for the turret camera'
    plugin, vini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('PlayerJetLockByView', 'PlayerLockByView', 'TurretAimHud'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=1', vini, re.M) and key in readme, key
    # The lock order is lockon.cpp's, every weapon's: the jets' stores by PlayerJetLockByView, the stock weapons the
    # player holds by PlayerLockByView; "the player holds it" is the soldier's own weapon or a seat they ride.
    lockon = src('src/lockon.cpp')
    score = lockon.split('bool Score(', 1)[1].split('\n}\n', 1)[0]
    assert 'Cfg().playerJetLockByView' in score and 'Cfg().playerLockByView' in score and 'PlayerHolds(w)' in score
    holds = lockon.split('bool PlayerHolds(', 1)[1].split('\n}\n', 1)[0]
    assert 'Rider::player' in holds and 'IsPlayer(owner)' in holds and 'SeatHolds(seat,w)' in holds


@test
def every_npc_aircraft_boardable() -> None:
    # Every jet body of our side (jet_internal.h kBodies: a mark, not hostile) has its row in src/playerjet_kinds.h
    # kBoardable, so an aircraft added later is flown by the player too (or is left out on purpose here, saying why);
    # the enemy's are not; the ini keys of the feature are read and documented.
    table = src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{Body::(\w+),L"[^"]*",L"[^"]*",(\d+)\.0f,[^}]*?"(\w+)"(,true)?\}', re.sub(r'\s+', ' ', table))
    assert len(rows) >= 15, f'src/jet_internal.h kBodies: read {len(rows)} rows'
    boardable = set(re.findall(r'\{Body::(\w+),Airframe::', src('src/playerjet_kinds.h')))
    # A body that is another row's airframe under the same mark is boarded as that row: playerjet_kinds.inc BoardRowOf
    # finds the row by the mark, a strike-mark jet then by its model's bones, so a row of its own could never be found.
    # The paratroop plane is the bomber401 strike jet with passenger seats (tools/make_jets.py TRANSPORT_PLANE_FILE).
    boarded_as = {'transportPlane': 'bomber401'}
    marks = {body: mark for body, mark, _name, _hostile in rows}
    for body, twin in boarded_as.items():
        assert body not in boardable and twin in boardable and marks.get(body) == marks.get(twin), \
            f'{body} is boarded as {twin}: the same mark, no row of its own'
    assert "bomber_sgo(game, 'EDF6VC_BOMBER401.SGO'), 0, TRANSPORT_PLANE_PASSENGERS" in src('tools/make_jets.py'), \
        'the paratroop plane is the bomber401 airframe'
    board_row = src('src/playerjet_kinds.inc').split('const pjet::Boardable* BoardRowOf(', 1)[1].split('\n}\n', 1)[0]
    assert 'BomberBody(v+kModelInst506)' in board_row and 'model==JetBody::bomber401 ? jet::Body::bomber401' in board_row
    for body, mark, _name, hostile in rows:
        if int(mark) == 0:
            continue   # a heli: the stock heli flight
        if hostile:
            assert body not in boardable, f'{body} is the enemy\'s: not boardable'
        else:
            assert body in boardable or boarded_as.get(body) in boardable, \
                f'{body} (mark {mark}): no row in src/playerjet_kinds.h kBoardable'
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('PlayerJetAll', 'PlayerJetHailKey'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key

@test
def boardable_seats_take_every_class() -> None:
    """Every aircraft the player boards (src/playerjet_kinds.h kBoardable) takes every class into its seat, whatever SGO
    brings it (the user, 2026-10-07: the carrier an NPC called took Rangers and Air Raiders only): pylib/vcobjects.py
    BOARDABLE_MARKS is kBoardable's bodies' marks (src/jet_internal.h kBodies), every parked or requested twin is one of
    them; with the game, seat 0 of every NPC and twin SGO jet_sgo makes, and of the bombers and the gunship make_jets makes
    from the strike jet, is PLAYER_SEAT_POSE / PLAYER_SEAT_CLASSES (15) exactly when its mark is one of them, the 506's own
    seat (506_HELI_DRIVER / 9) otherwise. The player jets' are tested by their own builds (always every class)."""
    import sgo
    table = src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1].split('};', 1)[0]
    marks = {body: float(mark) for body, mark in
             re.findall(r'\{Body::(\w+),L"[^"]*",L"[^"]*",(\d+)\.0f,', re.sub(r'\s+', ' ', table))}
    boardable = set(re.findall(r'\{Body::(\w+),Airframe::', src('src/playerjet_kinds.h')))
    assert len(boardable) >= 13 and boardable <= set(marks), sorted(boardable - set(marks))
    assert {marks[b] for b in boardable} == vc.BOARDABLE_MARKS, sorted({marks[b] for b in boardable} ^ vc.BOARDABLE_MARKS)
    assert vc.PLAYER_SEAT_CLASSES == 15, 'R 1 | WD 2 | F 4 | AR 8'
    for name, jet in vc.JETS.items():
        assert not (jet.parked or jet.requested) or jet.mark in vc.BOARDABLE_MARKS, f'{name}: a twin the player cannot board'
    import rootcpk
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.default()

    def seat(data: bytes) -> tuple[str, int]:
        s = sgo.read(data)[1]['vehicle_riding_position'][0]
        return s[3], s[4]

    every = (vc.PLAYER_SEAT_POSE, vc.PLAYER_SEAT_CLASSES)
    for name, jet in vc.JETS.items():
        if not jet.player:
            want = every if jet.mark in vc.BOARDABLE_MARKS else ('506_HELI_DRIVER', 9)
            assert seat(vc.jet_sgo(game, name, make_jets.MODEL)) == want, name
    for name in make_jets.BOMBERS:
        assert seat(make_jets.bomber_sgo(game, name)) == every, name
    gunship = make_jets.with_mark(make_jets.bomber_sgo(game, 'EDF6VC_BOMBER401.SGO'), make_jets.GUNSHIP_MARK)
    assert seat(gunship) == every and make_jets.GUNSHIP_MARK in vc.BOARDABLE_MARKS, 'the gunship'


@test
def every_boardable_aircraft_requested() -> None:
    """The Air Raider requests every aircraft the player flies (src/playerjet_kinds.h kBoardable) empty, as a vehicle (the
    user, 2026-10-06: 「补上空袭的召唤飞机，空母载具」), but those LEFT_OUT says why not: each request of one of them
    (tools/calls.py EDF6VC_CALL_FLY_*) brings its kind's requested twin (pylib/vcobjects.py REQUEST_KINDS: its mark, model
    and arms, parked: the whole plane's box, every class; requested: vehicle_setup, which the stock request's vehicle is
    built from) under tools/make_jets.py's EDF6VC_FLY_<KIND>.SGO; the rows are vehicle requests of the N9 Eros (category
    308, appended after the thrown drones), one a kind, and their names and texts say "fly it"."""
    left_out = {
        'strike': 'the player strike jet request (EDF6VC_CALL_PJET_STRIKE) brings the same airframe, model and stores',
        'bomber401': 'the airstrike bombers the strike jets take over (stock bombers, not ours to bring)',
        'bomber501_2': 'the airstrike bombers the strike jets take over (stock bombers, not ours to bring)',
        'blast': 'its one weapon is the charge that destroys it: the thrown blast drone brings that charge',
        'doll': 'its one weapon is the charge that destroys it: the thrown doll drone brings that charge',
    }
    table = src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1].split('};', 1)[0]
    marks = {body: float(mark) for body, mark in
             re.findall(r'\{Body::(\w+),L"[^"]*",L"[^"]*",(\d+)\.0f,', re.sub(r'\s+', ' ', table))}
    frames = dict(re.findall(r'\{Body::(\w+),Airframe::(\w+),', src('src/playerjet_kinds.h')))
    assert len(frames) >= 13 and set(left_out) <= set(frames), sorted(frames)
    fly = [c for c in calls.CALLS if c.brings == 'vehicle' and c.jet and not vc.JETS[c.jet].player]
    by_mark = {c.mark: c for c in fly}
    assert len(by_mark) == len(fly), 'two requests of one kind'
    for body, frame in frames.items():
        mark = marks[body]
        assert (mark in by_mark) != (body in left_out), f'{body} (mark {mark:.0f}): requested and left out, or neither'
    want = {vc.request_name(k) for k in vc.REQUEST_KINDS}
    assert {c.jet for c in fly} == want, sorted(want ^ {c.jet for c in fly})
    for kind in vc.REQUEST_KINDS:
        a, b = vc.JETS[kind], vc.JETS[vc.request_name(kind)]
        assert b.parked and b.requested and not a.parked and not a.requested and not b.player, kind
        assert (a.mark, a.model, a.file, a.weapons, a.durability, a.box_model) == (b.mark, b.model, b.file, b.weapons,
                                                                                    b.durability, b.box_model), kind
    # The gunship has no model file of its own: measured on the stock bomber401 it flies; its NPC SGO is make_jets'.
    gun = vc.JETS[vc.GUNSHIP_JET]
    assert gun.mark == make_jets.GUNSHIP_MARK == marks['gunship'] and gun.box_model in ('bomber401',), gun
    assert gun.weapons == vc.JETS['edf6tr_jet_strike_mission'].weapons and gun.model[1] == 'bomber401.mdb'
    first = calls.IDS.index(fly[0].id)
    assert calls.IDS[first:first + len(fly)] == tuple(c.id for c in fly), 'the requests are one appended run'
    assert all(c.brings == 'throw' for c in calls.CALLS[first - 3:first]), 'appended after the thrown drones'
    for c in fly:
        assert c.id == calls.ID_PREFIX + 'FLY_' + c.vehicle.removeprefix('EDF6VC_FLY_'), c.id
        assert make_jets.request_file(next(k for k in vc.REQUEST_KINDS if vc.request_name(k) == c.jet)) == f'{c.vehicle}.SGO'
        assert cw.template_of(c) == cw.VEHICLE_TEMPLATE == 'eWeapon394' and c.count == 0 and not c.follow, c.id
        assert 0.0 < c.level <= 4.0 and c.reload >= 3000, c.id
        assert '(Fly It)' in calls.call_name(c, 'EN') and '（自操縦）' in calls.call_name(c, 'JA'), c.id
        assert '自驾' in calls.call_name(c, 'SC') and '自駕' in calls.call_name(c, 'CN'), c.id
        assert 'EDF6VC_FLY_' in calls.call_description(c, 'SC'), c.id
    # The air carriers say how big they are (the stock container makes the vehicle where it lands: docs/player-jet-re.md).
    for c in fly:
        rotor = frames[next(b for b, m in marks.items() if m == c.mark and b in frames)] == 'rotor'
        assert ('59 x 77 m' in calls.call_description(c, 'EN')) == rotor, c.id


@test
def every_boardable_aircraft_caught() -> None:
    """Boardable asset/catch-kind metadata stays valid, but rescue now selects an existing real-piloted aircraft.
    The legacy catch asset table is retained for installed-resource compatibility; no airframe is spawned in the air."""
    head = src('src/playerjet_kinds.h')
    order = re.search(r'enum CatchWith : int \{(.*?)\};', head, re.S).group(1).replace(' ', '').replace('\n', '').split(',')
    assert order[0] == 'kCatchNone=-1', order
    order = order[1:]
    table = head.split('kCatchFiles[]={', 1)[1].split('};', 1)[0]
    files = re.findall(r'\{"([\w-]+)",(\d+)\.0f,L"([^"]+)",L"([^"]+)",(true|false)\}', table)
    assert len(files) == len(order) >= 3, (len(files), order)
    marks = {}
    by_file = {}
    for (name, mark, sgo_path, file, player), enum in zip(files, order):
        assert sgo_path == 'app:/object/' + file.lower(), file
        assert file in make_jets.FILES, f'{file}: tools/make_jets.py does not write it (the installer would not install it)'
        jet = vc.JETS[make_jets.FILES[file]]
        assert jet.mark == float(mark), f'{file}: mark {jet.mark}, kCatchFiles says {mark}'
        assert (player == 'true') == jet.player and (jet.player or (jet.parked and jet.requested)), file
        marks[enum] = float(mark)
        by_file[enum] = (file, jet)
    body_marks = {body: float(mark) for body, mark in re.findall(
        r'\{Body::(\w+),L"[^"]*",L"[^"]*",(\d+)\.0f,', re.sub(r'\s+', ' ', src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1]))}
    rows = re.findall(r'\{Body::(\w+),Airframe::(\w+),Arm::\w+,(?:Wing|Rotor)\(.*?\),(\w+),(\w+)\},', head)
    frames = dict(re.findall(r'\{Body::(\w+),Airframe::(\w+),', head))
    assert len(rows) == len(frames) >= 13, (len(rows), len(frames))
    twins = {vc.JETS[k].mark: k for k in vc.REQUEST_KINDS}
    wing_marks = {body_marks[b] for b, f in frames.items() if f == 'wing'}
    for body, frame, catch, why in rows:
        assert catch in by_file, f'{body}: catchWith {catch} is no kCatchFiles row'
        file, jet = by_file[catch]
        mark = body_marks[body]
        if frame == 'wing' and mark in twins:   # its own kind's twin first
            assert file == make_jets.request_file(twins[mark]) and why == 'kOwnTwin', f'{body}: caught by {file}, not its own twin'
        else:
            assert jet.player and why != 'kOwnTwin', f'{body} ({frame}): no twin of its own to fly in: a player jet, not {file}'
        assert jet.player or jet.mark in wing_marks, f'{body}: its catch {file} is no wing'
    jets = src('src/playerjet.cpp')
    preload = jets[jets.index('void PreloadPlayerJets()'):]
    preload = preload[:preload.index('\n}\n')]
    assert 'i<pjet::kCatchFileCount' in preload and 'kPreloadFn)(mgr,f.sgo' in preload, 'PreloadPlayerJets: not every catch SGO'
    assert 'SpawnCatchJet(' not in jets and 'CatchChoice(p)' in jets, 'catch must select an existing crewed aircraft'
    assert '!CrewedPilot(v)' in jets, 'catch/AutoFly must stop when its real pilot is lost'
    assert 'kPlayerJetFiles' not in jets and 'bail.mark' not in jets, 'a second catch table / the old mark'
    board = src('src/playerjet_board.inc')
    left = board[board.index('void Left('):]
    left = left[:left.index('\n}\n')]
    assert not re.search(r'bail\.(catchWith|mark)\s*=(?!=)', left), 'Left takes the catch away from the jet left'
    assert 'bail.catchWith=CatchOf(j,&bail.catchWhy);' in jets, 'EjectStart: the catch of the jet left'


@test
def range_parks_every_boardable_aircraft_apart() -> None:
    """The grand battle parks one of each of our aircraft the player boards that has a range SGO (testrange/gen.py
    BOARDABLE_PARKED: every mark of src/playerjet_kinds.h kBoardable among pylib/vcobjects.py JETS) empty, none of them
    NPC-flown (the air carrier, an NPC friend there, "flew straight off", 2026-10-05); and on a map of its own every
    placement's footprint is clear of the others' and the player start's (the carrier is 59 x 77 m: 33 m from a jet it
    began 11 m over its spot)."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    import rmpa
    from vcobjects import JETS
    table = src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1].split('};', 1)[0]
    marks = {body: int(mark) for body, mark in
             re.findall(r'\{Body::(\w+),L"[^"]*",L"[^"]*",(\d+)\.0f,', re.sub(r'\s+', ' ', table))}
    boardable = {marks[b] for b in re.findall(r'\{Body::(\w+),Airframe::', src('src/playerjet_kinds.h'))}
    assert len(boardable) >= 10, f'read {len(boardable)} boardable marks'
    kinds = {name for name, jet in JETS.items()
             if int(jet.mark) in boardable and name.endswith('_mission') and not jet.parked and not jet.player}
    want = {vc.parked_name(k) for k in kinds}
    assert set(gen.BOARDABLE_PARKED) == want, f'BOARDABLE_PARKED {sorted(gen.BOARDABLE_PARKED)}, boardable {sorted(want)}'
    # Each the parked twin of its kind: the same mark, model and arms, parked (the whole plane's box, every class).
    for k in kinds:
        a, b = JETS[k], JETS[vc.parked_name(k)]
        assert b.parked and not a.parked and (a.mark, a.model, a.file, a.weapons) == (b.mark, b.model, b.file, b.weapons), k
        assert vc.parked_name(k) in gen.DERIVED and vc.parked_name(k) in {s for s, _ in gen.VEHICLES}, k
    plan = gen.grand_battle(gen.Plan())
    empty = {s for s, npc in gen.placements(plan) if not npc}
    flown = {s for s, npc in gen.placements(plan) if npc}
    carrier = vc.parked_name('edf6tr_jet_carrier_mission')
    carriers = {s for s in want if gen.footprint(s) == gen.footprint(carrier)}
    # NPC-flown fighters fight the battle; a carrier is there for the player only.
    assert want <= empty and len(carriers) == 3 and not carriers & flown, (sorted(want - empty), sorted(carriers & flown))
    assert 2 * gen.footprint(carrier) >= (59.0 ** 2 + 77.0 ** 2) ** 0.5, 'the carrier footprint is under its size'
    # A map of its own: the player start and a point every 25 m out to 1 km (the selftest runs without the game).
    points = [rmpa.Point('プレイヤー', (0.0, 0.0, 0.0), (0.0, 0.0, 1.0))]
    points += [rmpa.Point(f'p{x}_{z}', (x * 25.0, 0.0, z * 25.0), (0.0, 0.0, 1.0))
               for x in range(-40, 41) for z in range(-40, 41) if (x, z) != (0, 0) and x * x + z * z <= 1600]
    lay = gen.layout(points, gen.small_count(plan), gen.far_reserved(plan))
    taken = [(lay.player, gen.SPOT)]
    for sgo, _npc, p in gen.spots_for(plan, lay):
        r = gen.footprint(sgo)
        assert gen.overlap(p, r, taken) == 0.0, f'{sgo} at {p.name} reaches {gen.overlap(p, r, taken):.1f} m into another'
        taken.append((p, r))
    # The fallback is the least overlap, never a silent pile-up: a crowd that cannot fit still spreads out.
    crowd = gen.spaced([(carrier, False)] * 3, points[1:5], lay.player)
    assert len(set(p.name for p in crowd)) == 3, crowd


@test
def grand_battle_fits_the_real_plain() -> None:
    """The grand battle laid out on the game's own M045 points (48 of them): every placement gets a spot and the ships
    and the sky's spawns their far points (grand_points). The 1 km grid above never runs short; the real plain did once a
    second sidecar came in (38 placements left 5 far points of 6 and the install stopped at the last step, 2026-10-06)."""
    import rootcpk
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    import rmpa
    plan = gen.grand_battle(gen.Plan())
    points = rmpa.points(gen.Game(rootcpk.DEFAULT_GAME).read(f'MISSION/EDF6/{plan.site}', 'MISSION.RMPA'))
    reserve = gen.far_reserved(plan)
    lay = gen.layout(points, gen.small_count(plan), reserve)
    assert len(gen.spots_for(plan, lay)) == len(gen.placements(plan))
    assert len(gen.grand_points(lay)) == len(gen.GRAND_SHIPS)
    gen.script(plan, gen.layout(points, gen.small_count(plan), reserve))


def _range_grid() -> list:
    """A map of its own (the selftest runs without the game): the player start and a point every 25 m out to 1 km."""
    import rmpa
    points = [rmpa.Point('プレイヤー', (0.0, 0.0, 0.0), (0.0, 0.0, 1.0))]
    return points + [rmpa.Point(f'p{x}_{z}', (x * 25.0, 0.0, z * 25.0), (0.0, 0.0, 1.0))
                     for x in range(-40, 41) for z in range(-40, 41) if (x, z) != (0, 0) and x * x + z * z <= 1600]


def _range_checked(gen, plan, points: list) -> tuple[list, list]:  # noqa: ANN001 - the testrange module, a gen.Plan
    """Lays `plan` out on `points` and checks it: every placement on a spot of its own, no footprint reaching
    OVERLAP_SLACK into another's or the player's start, and (a range of targets) every target spot's group clear of them
    all. Returns (placed, target spots)."""
    lay = gen.layout(points, gen.small_count(plan), gen.far_reserved(plan))
    placed = gen.spots_for(plan, lay)
    assert len(placed) == len(gen.placements(plan)) == len({p.name for _, _, p in placed}), 'a placement without a spot'
    taken = [(lay.player, gen.SPOT)]
    for sgo, _npc, p in placed:
        r = gen.footprint(sgo)
        assert gen.overlap(p, r, taken) == 0.0, f'{sgo} at {p.name} reaches {gen.overlap(p, r, taken):.1f} m into another'
        taken.append((p, r))
    targets = gen.target_spots(lay) if gen.target_ranged(plan) else []
    for t in targets:
        assert gen.overlap(t, gen.TARGET_SPREAD, taken) == 0.0, f'targets at {t.name} come out on a vehicle'
    return placed, targets


@test
def installer_range_has_targets_and_no_enemy() -> None:
    """The range the installer writes (testrange/gen.py target_range; the user, 2026-10-06: 「测试场的怪给我去掉吧，留下靶子。
    然后载具再补充一下我们新加的」): the installer writes it (not the grand battle); its script makes targets and nothing
    hostile (no ship, no thread, no other enemy, no Primer, no enemy jet); every vehicle we added or rework that the player
    drives is placed empty; and on a map of its own every vehicle has room and no target group comes out on one."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    assert re.search(r'gen\.install\(game, gen\.target_range\(gen\.Plan\(\)\)\)', src('tools/installer.py')), \
        'the installer writes another range'
    plan = gen.target_range(gen.Plan())
    assert gen.target_ranged(plan) and plan.scenario == '' and gen.far_reserved(plan) == gen.TARGET_SPOTS
    placed, targets = _range_checked(gen, plan, _range_grid())
    assert len(targets) >= gen.TARGET_SPOTS, f'{len(targets)} target spots'
    text = gen.script(plan, gen.layout(_range_grid(), gen.small_count(plan), gen.far_reserved(plan)))
    made = re.findall(r'(Create\w+)\(([^;]*)\);', text)
    hostile = [(f, a) for f, a in made if f.startswith('CreateEnemy') and gen.TARGET + '.sgo' not in a]
    assert not hostile, hostile
    assert any(f == 'CreateEnemyGroup' for f, _ in made), 'no target in the script'
    assert not re.search(r'internal_CreateThread\("Grand', text) and 'CreateFriendSquad' not in text
    friends = {re.search(r'object/(\w+)\.sgo', a).group(1) for f, a in made if f == 'CreateFriend'}
    assert friends == set(gen.RANGE_FRIENDS), f'NPC-placed {sorted(friends)}'
    hostile_jets = {s for s, j in gen.JETS.items() if j.mark in (7020.0, 7030.0)} | {s for s, _, _ in gen.ENEMIES}
    assert not {s for s, _ in gen.placements(plan)} & hostile_jets
    # Ours, the player's to drive (README: the player's jets, every boardable kind parked, the ground vehicles our tools
    # make, the helicopters the range makes placeable, the Depth Crawler, the tanks and the flak EDF6AutoTurret arms,
    # and the stock vehicles the plugin reworks: EMC, Nix, Proteus).
    ours = ({'edf6tr_pjet_fighter_mission', 'edf6tr_pjet_strike_mission'} | set(gen.BOARDABLE_PARKED) | set(gen.GROUND_MISSION)
            | {'edf6tr_v506_heli_mission', 'edf6tr_vehicle409_heli_mission', 'edf6tr_vehicle410_heli_mission',
               'edf6tr_v602_heli_mission', 'edf6tr_vehicle502_groundrobo_mission', 'vehicle403_tank_mission',
               'vehicle404_bigtank', 'v603_flak_mission', 'v510_maser_mission', 'v612_nix_g_mission',
               'v614_proteus_mk2_mission'})
    empty = {s for s, npc, _ in placed if not npc}
    assert ours <= empty, f'not placed for the player: {sorted(ours - empty)}'
    assert ours <= {s for s, _ in gen.VEHICLES}


@test
def target_range_fits_the_real_plain() -> None:
    """The installer's range laid out on the game's own M045 points (48, 36 of them flat vehicle spots): every
    placement gets a spot of its own, no footprint reaches into another (the grand battle's 37 and the three reworked
    ones overlapped by up to 6.2 m there), and the targets have their spots, clear of every vehicle."""
    import rootcpk
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    import rmpa
    plan = gen.target_range(gen.Plan())
    points = rmpa.points(gen.Game(rootcpk.DEFAULT_GAME).read(f'MISSION/EDF6/{plan.site}', 'MISSION.RMPA'))
    _placed, targets = _range_checked(gen, plan, points)
    assert len(targets) >= gen.TARGET_SPOTS, f'{len(targets)} target spots on the plain'
    gen.script(plan, gen.layout(points, gen.small_count(plan), gen.far_reserved(plan)))


@test
def footprint_covers_the_stock_models() -> None:
    """testrange/gen.py footprint for each stock-model vehicle the installer's range places: at least how far its model
    reaches from its origin across the ground (the bind pose's vertices, Root.cpk), OVERLAP_SLACK aside (the EMC's
    barrel reaches 21.0 m: as a SPOT, 7.5 m, it stood over its neighbours)."""
    import rootcpk
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    import jet_models
    import mdb
    import sgo
    game = gen.Game(rootcpk.DEFAULT_GAME)
    seen = 0
    for name in sorted({s for s, _ in gen.placements(gen.target_range(gen.Plan()))} - gen.JETS.keys()):
        model = sgo.load(data=gen.vehicle_sgo(game, name)).get('animation_model')
        path, member = model[0][0], model[0][1]
        folder, file = path.split('app:/')[1].rsplit('/', 1)
        try:
            archive = mdb.rab_read(game.read(folder.upper(), file.upper()))
        except KeyError:   # a model our own tools make (the Katyusha's, ...): not in Root.cpk
            continue
        data = next(f for f in archive.files if f.name.lower() == member.lower()).data
        with contextlib.suppress(Exception):
            data = mdb.cmpl_decompress(data)
        reach = max((x * x + z * z) ** 0.5 for x, _y, z in jet_models.bind_positions(mdb.mdb_read(data)))
        assert gen.footprint(name) + gen.OVERLAP_SLACK >= reach, f'{name}: footprint {gen.footprint(name)}, model {reach:.1f} m'
        seen += 1
    assert seen >= 8, f'measured {seen} models'


@test
def carrier_camera_and_ragdoll_on_its_box_centre() -> None:
    """pylib/vcobjects.py seat_camera / sight_problem / _jet_ragdoll (docs/player-jet-re.md §14): the V506's seat camera
    and ragdoll hang on the vehicle's position, the collision box's centre (`mdl`), and on the mesh bone, the model's
    origin. The carrier (2026-10-06, 「空母的视角在空母底下，包括碰撞体积也是」): the stock rig put its eye inside the hull
    and the heli's proxies sat under its belly. Now its rig is the stock one scaled with the model and sees it from
    outside, every other jet's (its eye already outside) is the stock one, and the proxies sit on the box's centre with
    the two bindings each other's inverse. Numbers: EDF6VC_CARRIER / the player fighter as made, the V506 model."""
    import sgo
    heli = ((-5.274, -0.06, -8.31), (5.274, 4.45, 5.274))
    heli_centre = (0.0, 1.45, 0.65)
    eye, look = (0.0, 5.4, -14.45), (0.0, 2.75, 1.1)
    carrier = ((-29.703, 0.0, -41.531), (29.703, 17.031, 35.312))
    c = (0.0, 8.516, -3.109)
    was = [c[i] + eye[i] for i in range(3)], [c[i] + look[i] for i in range(3)]
    assert vc.sight_problem(*was, carrier), 'the stock rig on the carrier: its eye is in the hull'
    fit = vc.seat_camera(eye, look, c, carrier, heli, heli_centre)
    assert fit is not None, 'the carrier keeps the stock rig'
    now = [[c[i] + p[i] for i in range(3)] for p in fit]
    assert vc.sight_problem(now[0], now[1], carrier) is None, now
    assert now[0][2] < carrier[0][2] - 20.0 and now[0][1] > carrier[1][1] + 10.0, now   # behind and over it
    fighter = ((-8.047, 0.0, -8.234), (8.047, 2.762, 11.609))
    assert vc.seat_camera(eye, look, (0.0, 1.381, 1.688), fighter, heli, heli_centre) is None, 'a fighter keeps the stock rig'
    assert vc.sight_problem((0.0, 6.0, -50.0), (0.0, 8.0, 0.0), carrier), 'a line of sight under the carrier top at its tail'
    blob = sgo.write(1, {
        'ragdoll_from_animation': [[['body', 'RagDollProxys.body'], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                                   [['rotor', 'RagDollProxys.rotor'], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]]],
        'animation_from_ragdoll': [[['RagDollProxys.body', 'body'], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                                   [['RagDollProxys.rotor', 'rotor'], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
                                   [['RagDollProxys.body', 'globalSRT'], [0.0, -1.637, 0.0], [0.0, 0.0, 0.0, 1.0]]]})
    _, inner = sgo.read(vc._jet_ragdoll(blob, 'body', c))
    value = vc._value
    for e in inner['ragdoll_from_animation']:
        assert e[0][0] == 'body' and [round(value(x), 3) for x in e[1]] == list(c), e
    assert [e[0][0] for e in inner['animation_from_ragdoll']] == ['RagDollProxys.rotor', 'RagDollProxys.body']
    for e in inner['animation_from_ragdoll']:
        assert e[0][1] == 'body' and [round(value(x), 3) for x in e[1]] == [-x for x in c], e


@test
def jet_door_on_the_ground_beside_its_box() -> None:
    """pylib/vcobjects.py move_door / check_door (docs/player-jet-re.md §12): a jet's boarding point (the V506's door
    locator, read out of a MAB block by mab_locator) goes on the ground DOOR_OUT m outside its collision box's right side,
    with a radius that reaches a human DOOR_STEP m off it on the ground under its box (the door's parent `mdl` is the
    box's centre, so the ground is -hy on it: the DOOR log of 2026-10-06 read the carrier's y-0 door 8.52 m over the
    ground, boarded only through the plugin's own hook) (the parked carrier's door was under its middle, 4.9 m in from its
    box's side: no prompt anywhere, 2026-10-05); check_door refuses a door inside the box, off the ground or out of
    reach. On a block of its own (the selftest runs without the game)."""
    import struct
    import sgo
    door, seat = '搭乗口１', '操縦席１'
    nul = chr(0)
    names = (door + nul + 'mdl' + nul + seat + nul).encode('utf-16le')
    vecs, strings, table = 0x84, 0xA4, 0x24
    mab = bytearray(strings) + names
    mab[0:4] = b'MAB' + bytes(1)
    struct.pack_into('<4I', mab, 0x14, table, table + 0x60, vecs, strings)
    at_door, at_mdl, at_seat = strings, strings + 2 * (len(door) + 1), strings + 2 * (len(door) + len('mdl') + 2)
    for r, name, vec, radius in ((0x44, at_door, vecs, 1.8), (0x64, at_seat, vecs + 16, 0.5)):
        struct.pack_into('<iiiif', mab, r, name - r, at_mdl - r, 0, vec - r, radius)
    struct.pack_into('<8f', mab, vecs, 2.15, 0.0, 1.8, 1.0, 0.0, 1.45, 1.1, 1.0)
    mab = bytes(mab)
    assert vc.mab_locator(mab, door) == (vecs, 0x44 + 0x10) and vc.mab_locator(mab, seat) == (vecs + 16, 0x64 + 0x10)
    for bad in ('カメラ１', 'mdl'):
        try:
            vc.mab_locator(mab, bad)
        except ValueError:
            continue
        raise AssertionError(f'{bad}: not a locator, found')
    carrier = [[0.0, 8.516, -3.109], [29.703, 8.516, 38.422]]   # EDF6VC_CARRIER's whole-model box (jet_models.model_box)

    def jet(box: list[list[float]], door_mab: bytes) -> bytes:
        return sgo.write(1, {'animation_model': [['a', 'b'], 'c', door_mab], 'heli_rigid_body': [box[0], box[1], 0.3],
                             'vehicle_riding_position': [[door, seat, ['カメラ１', 0.0, 0.0], '505_TANK_DRIVER', 15, 10.0, 5]]})

    def refused(data: bytes, why: str) -> None:
        try:
            vc.check_door(data)
        except vc.DoorError:
            return
        raise AssertionError(f'check_door passed {why}')

    refused(jet(carrier, mab), 'the stock door under the middle of the carrier')
    _, m = sgo.read(jet(carrier, mab))
    vc.move_door(m, carrier)
    moved = sgo.write(1, m)
    vc.check_door(moved)
    _, low = sgo.read(moved)
    low['heli_rigid_body'] = [[0.0, 13.516, -3.109], [29.703, 13.516, 38.422], 0.3]   # its bottom 5 m under the door
    refused(sgo.write(1, low), 'a door 5 m over the ground its box stands on')
    _, old = sgo.read(moved)   # §12's door: y 0 on `mdl`, the box's centre, 8.516 m up
    ob = bytearray(old['animation_model'][2])
    struct.pack_into('<f', ob, vc.mab_locator(bytes(ob), door)[0] + 4, 0.0)
    old['animation_model'][2] = bytes(ob)
    refused(sgo.write(1, old), "the door at the box's centre height (8.516 m over the ground)")
    block = m['animation_model'][2]
    vec, rad = vc.mab_locator(block, door)
    x, y, z = struct.unpack_from('<3f', block, vec)
    radius = struct.unpack_from('<f', block, rad)[0]
    assert abs(x - (29.703 + vc.DOOR_OUT)) < 1e-3 and abs(y + 8.516) < 1e-4 and abs(z - 1.8) < 1e-6, (x, y, z)
    assert abs(radius - 1.8) < 1e-6, radius   # on the ground: the stock radius reaches
    assert vc.mab_locator(block, seat) == (vecs + 16, 0x64 + 0x10)
    assert struct.unpack_from('<4f', block, vecs + 16) == struct.unpack_from('<4f', mab, vecs + 16), 'the seat moved'
    # A small jet keeps the stock radius (1.8) when that reaches; the door stays within the box's length.
    at, r = vc.door_point([[0.0, 1.381, 1.688], [8.047, 1.381, 9.922]], (2.15, 0.0, 1.8), 1.8)
    assert at == [8.647, -1.381, 1.8] and r == 1.8, (at, r)
    at, r = vc.door_point([[0.0, 1.255, 0.0], [12.969, 1.255, 1.0]], (2.15, 0.0, 1.8), 1.8)
    assert at[2] == 1.0, at
    # A box reaching under its model's origin (a stock bomber's): the door at the box's bottom all the same, the ground
    # it lands on; the stock radius reaches.
    at, r = vc.door_point([[0.0, 0.339, 2.723], [1.983, 1.624, 15.137]], (2.15, 0.0, 1.8), 1.8)
    assert at == [2.583, -1.624, 1.8] and r == 1.8, (at, r)
    # The Sazabi's `mdl` lands at its model's origin, its soles, not at its box's centre 12.8 m over them (its frames
    # log, 2026-10-07: written from the centre, the door was 12.8 m underground): the same point, from the origin.
    assert vc.JETS[vc.SAZABI_JET].locators_on_origin and vc.mdl_at(vc.JETS[vc.SAZABI_JET]) == (0.0, 0.0, 0.0)
    assert all(vc.mdl_at(j) == (0.0, 0.0, 0.0) for j in vc.JETS.values()), 'native model update uses the model origin'
    at, r = vc.door_point(carrier, (2.15, 0.0, 1.8), 1.8, (0.0, 0.0, 0.0))
    assert at == [30.303, 0.0, -1.309] and r == 1.8, 'a full-span carrier door is not buried by its half-height'
    at, r = vc.door_point([[0.0, 12.805, -1.395], [10.805, 12.805, 13.715]], (2.15, 0.0, 1.8), 1.8, (0.0, 0.0, 0.0))
    assert at == [11.405, 0.0, 0.405], at


@test
def boarding_an_empty_aircraft_makes_its_entry() -> None:
    """One of our aircraft a mission placed empty has no src/jet.cpp entry (JetFrame makes it on an NPC pilot's first
    frame): boarding it makes one (playerjet_board.inc Boarded: jet::Adopt), else a rotor craft never lifts (HoverStep
    flies off the entry), a carrier has no drones and a jet left in the air goes back to an NPC in takeoff mode."""
    board = src('src/playerjet_board.inc')
    boarded = board.split('void Boarded(', 1)[1].split('\n}\n', 1)[0]
    assert 'jet::Adopt(v)' in boarded and 'jet::FindJet' not in boarded, 'Boarded does not make the entry'
    adopt = src('src/jet.cpp').split('jet::Jet* jet::Adopt(', 1)[1].split('\n}\n', 1)[0]
    assert 'FindJet(v)' in adopt and 'CrewPlaced(' in adopt and 'jetPilot' in adopt, adopt
    hover = board.split('void HoverStep(', 1)[1].split('\n}\n', 1)[0]
    assert 'if(!e){j.active=false;return;}' in hover, 'HoverStep no longer needs the entry: revisit jet::Adopt'


# The configuration modules src/plugin.cpp's LoadConfig hands EDF6VehicleCrew.ini to, and the call that does it.
INI_MODULES = {'src/support_config.cpp': 'LoadSupportConfig(iniPath);'}


@test
def heli_sight_after_aim_lines() -> None:
    """The stock heli's gun sight (src/helisight.cpp) draws for the guns whose aim line AimLines hid this frame
    (HiddenAimGuns), so the input hook runs it after AimLines; AimLines hides the player's line for it
    (PlayerHeliOwnSight); its ini key is read, shipped and documented; every key the ini ships is read.
    "Read" means: by src/plugin.cpp (L"Key"), or by a configuration module plugin.cpp's LoadConfig hands the same ini
    to (INI_MODULES: src/support_config.cpp, LoadSupportConfig(iniPath)). A module's suffixed keys (Prefix_<suffix>, read
    as L"Prefix_%ls") count when the module reads that prefix and the suffix is one of its keys (tools/support_config.py
    UNIT_KEYS for SupportAircraftCount_); every key a module reads is shipped in the ini, the suffixed ones as an example."""
    crew = src('src/crew.cpp')
    hook = crew.split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    lines, sight = hook.find('&AimLines,'), hook.find('&HeliSightFrame,')
    assert 0 <= lines < sight, 'src/crew.cpp InputHook: AimLines must run before HeliSightFrame'
    aim = crew.split('void AimLines(', 1)[1].split('\n}\n', 1)[0]
    assert 'PlayerHeliOwnSight(vehicle)' in aim, 'src/crew.cpp AimLines: the heli sight hides the player\'s line'
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert re.search(r'^PlayerHeliGunSight=1', ini, re.M) and 'PlayerHeliGunSight' in readme
    modules = {path: src(path) for path, call in INI_MODULES.items()}
    for path, call in INI_MODULES.items():
        assert call in plugin.split('void LoadConfig() noexcept {', 1)[1].split('\n}\n', 1)[0], f'LoadConfig does not hand the ini to {path}'
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import support_config
    import support_loadout
    suffixes = {'SupportAircraftCount_': support_config.UNIT_KEYS, 'SupportPreset_': tuple(support_loadout.SEATS),
                'SupportVehicle_': tuple(support_loadout.VEHICLE_KEYS)}
    example = {'SupportAircraftCount_': r'\d', 'SupportPreset_': r'[a-z]', 'SupportVehicle_': r'[A-Z]'}   # a count; a kind; a gun

    def read(key: str) -> bool:
        if f'L"{key}"' in plugin or any(f'L"{key}"' in m for m in modules.values()):
            return True
        for prefix, known in suffixes.items():
            if key.startswith(prefix) and key[len(prefix):] in known:
                return any(f'L"{prefix}%ls"' in m for m in modules.values())
        return False
    unread = [k for k in re.findall(r'^([A-Za-z]\w*)=', ini, re.M) if not read(k)]
    assert not unread, f'EDF6VehicleCrew.ini keys neither src/plugin.cpp nor {list(INI_MODULES)} read: {unread}'
    for path, text in modules.items():
        for key in re.findall(r'L"(Support\w+?)"', text):
            assert re.search(rf'^{key}=', ini, re.M), f'{path} reads {key}, the shipped ini lacks it'
        for prefix in re.findall(r'L"(\w+_)%ls"', text):
            assert prefix in suffixes and re.search(rf'{prefix}[A-Z_]+={example[prefix]}', ini), f'{path} reads {prefix}<key>: no shipped example'



@test
def heli_mouse_aim_wired() -> None:
    """The helicopters' mouse-aim flight (src/heliaim.h) and HUD: their ini keys are read, shipped and documented, the
    lever they replaced (HeliMousePitch) is shipped no more and an old ini's is said ignored; the stock heli the player
    flies and the NPC pilot write the horizontal input through the same stick law (heli.cpp Steer and AimFly, StockStick);
    since 2026-10-09 the player's heli flies the War Thunder instructor (aim::Instructor: W / S the collective, the nose
    pitch the cyclic) with its own collective (aim::CollectiveThrottle: the stock vertical law inverted, on the rotor
    PlayerMouseTune quickens) while the NPC keeps StockThrottle; the rotor craft still fly aim::Fly (HoverAim)."""
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('HeliMouseAim', 'HeliFlightHud'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=1', ini, re.M) and key in readme, key
    assert not re.search(r'^HeliMousePitch=', ini, re.M), 'HeliMousePitch is retired (HeliMouseAim)'
    assert 'L"HeliMousePitch"' in plugin.split('void IgnoreRetired(', 1)[1].split('\n}\n', 1)[0]
    heli, board = src('src/heli.cpp'), src('src/playerjet_board.inc')
    steer = heli.split('Control Steer(', 1)[1].split('\n}\n', 1)[0]
    fly = heli.split('void AimFly(', 1)[1].split('\n}\n', 1)[0]
    assert 'aim::StockStick(' in steer and 'aim::StockStick(' in fly, 'aim::StockStick('
    assert 'aim::StockThrottle(' in steer and 'aim::CollectiveThrottle(' in fly and 'aim::StockThrottle(' not in fly
    assert 'aim::Instructor(' in fly and 'aim::Fly(' not in fly, 'heli.cpp AimFly: the instructor, not the rotor craft law'
    # The yaw is the one law apart (2026-10-06, the user: the mouse did not turn the heli): the NPC damps its turn
    # (StockYaw), the player compensates the native angle-state lag/spring (PlayerYawInput).
    assert 'aim::StockYaw(' in steer and 'aim::PlayerYawInput(' in fly and 'aim::StockYaw(' not in fly
    assert 'aim::MoveOnScreen(' in fly, 'heli.cpp AimFly: the mouse kept on the screen axis by axis'
    player = heli.split('void PlayerHeli(', 1)[1].split('\n}\n', 1)[0]
    off = heli.split('void AssistOff(', 1)[1].split('\n}\n', 1)[0]
    assert 'PlayerMouseTune(v,' in player and 'kMaxYaw,a.yaw' in off
    assert 'kRotorUp,a.rotorUp' in off and 'kRotorDown,a.rotorDown' in off and 'kTiltSmooth,a.tilt' in off, 'AssistOff: the rotor / tilt back'
    attitude = heli.split('void __fastcall PlayerAttitudeHook(', 1)[1].split('\n}\n', 1)[0]
    assert 'own[0]=roll;own[2]=pitch;' in attitude, 'PlayerAttitudeHook: the instructor\'s pitch and coordinated roll'
    # The hover rotor from the lift as it is in memory (heliaim.h HoverRotor), not the old 70 the 602 clamped to 1.0 on.
    assert 'kStockLift' not in heli and heli.count('aim::HoverRotor(') >= 3
    hud = src('src/hud.cpp').split('void HeliStrip(', 1)[1].split('\n}\n', 1)[0]
    assert 'Tx::heliKeysAir' in hud and 'KeyName(Cfg().playerJetBrakeKey' in hud, 'hud.cpp HeliStrip: the keys spelt out'
    # The stock heli's keys (f.collective) are the instructor's: W / S up and down, the nose down forward; the rotor
    # craft's stay the speed setpoint's.
    assert 'f.collective ? Tx::heliKeysInstructor : Tx::heliKeysAir' in hud and 'Tx::heliKeysInstructorLanded' in hud
    texts = src('src/hudtext.inc')
    assert 'SPACE: up' in texts.split('HUDTEXT(heliKeysAir,', 1)[1].split('\n', 1)[0]
    instructor = texts.split('HUDTEXT(heliKeysInstructor,', 1)[1].split('\n', 1)[0]
    assert 'W / S: up / down' in instructor and 'down: forward' in instructor, instructor
    assert 'HOLD W / SPACE' in texts.split('HUDTEXT(heliKeysInstructorLanded,', 1)[1].split('\n', 1)[0]
    assert 'f.collective=p.flying' in heli, 'heli.cpp PublishHud: the stock heli says its keys are the instructor\'s'
    assert 'aim::Fly(' in board.split('void HoverAim(', 1)[1].split('\n}\n', 1)[0]


@test
def heli_store_flies_its_own_arc() -> None:
    """The NPC heli's unguided store (heli.cpp kStoreHolder: the 506 napalm gun and drop pod) is aimed and gated on its
    own round's arc (src/roundaim.h), not on the gun's lead with the homing missile's cone: Arms takes holder 2 apart
    (IsStore), StoreSense solves and gates with roundaim, the run's nose goes onto the solution while the store is
    ready, Fire's store branch fires only on the gate; tools/heli_fire_check.cpp runs the same roundaim calls and is
    an EXCLUDE_FROM_ALL target."""
    heli = src('src/heli.cpp')
    arms = heli.split('Loadout Arms(', 1)[1].split('\n}\n', 1)[0]
    assert 'IsStore(h.type,i,homing)' in arms and 'l.store=weapon' in arms, 'heli.cpp Arms: holder 2 is the store'
    sense = heli.split('void StoreSense(', 1)[1].split('\n}\n', 1)[0]
    for call in ('roundaim::Fire(', 'roundaim::Worth(', 'roundaim::Solve('):
        assert call in sense, f'heli.cpp StoreSense: {call}'
    frame = heli.split('bool SenseFrame(', 1)[1].split('\n}\n', 1)[0]
    assert 'StoreSense(h,s);' in frame and 'std::memcpy(s.lead,s.storeLead,12)' in frame, 'heli.cpp SenseFrame: the nose onto the store'
    fire = heli.split('Shot Fire(', 1)[1].split('\n}\n', 1)[0]
    branch = fire.split('} else if(s.arms.store) {', 1)[1].split('} else {', 1)[0]
    assert 's.storeWorth' in branch and 'kMissileCone' not in branch, 'heli.cpp Fire: the store fires on its own gate'
    check = src('tools/heli_fire_check.cpp')
    assert '#include "../src/roundaim.h"' in check and 'roundaim::Worth(' in check and 'roundaim::Solve(' in check
    assert re.search(r'add_executable\(heli_fire_check EXCLUDE_FROM_ALL tools/heli_fire_check\.cpp\)', src('CMakeLists.txt'))

@test
def nix_torso_wired() -> None:
    """The Nix's torso twist (src/nix.cpp, src/nix_twist.h): its ini key is read, shipped on and documented; it chains the
    Nix's own class (the vtable crew.cpp knows as 612_nix) and is built (its own target_sources line) with its offline
    check target; every EDF.dll address it checks is in docs/nix-re.md; the open stops are 120 deg; with the game
    present, V612_NIX.SGO is that class and its pilot class says the +-70 deg README.md gives as the stock twist limit."""
    import math
    import rootcpk
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/nix-re.md')
    code, twist, crew, cmake = src('src/nix.cpp'), src('src/nix_twist.h'), src('src/crew.cpp'), src('CMakeLists.txt')
    assert 'L"NixTorsoTwist"' in plugin and re.search(r'^NixTorsoTwist=1', ini, re.M) and 'NixTorsoTwist' in readme
    vt = re.search(r'kVtNix=(0x[0-9A-F]+)', code).group(1)
    assert re.search(rf'\{{{vt},0x644350,"612_nix",kFindSeat,4\}}', crew), vt   # the family's per-frame update (slot 4)
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/nix.cpp)' in cmake
    assert 'add_executable(nix_twist_check EXCLUDE_FROM_ALL tools/nix_twist_check.cpp)' in cmake
    sigs = code.split('const Sig kSigs[]={', 1)[1].split('};', 1)[0]
    rvas = re.findall(r'\{(0x[0-9A-F]+),\{', sigs)
    assert len(rvas) >= 8, rvas + [re.search(r'kUpdate=(0x[0-9A-F]+)', code).group(1), vt]
    for rva in rvas:
        assert rva.upper().replace('0X', '0x') in doc, f'docs/nix-re.md does not mention {rva}'
    assert re.search(r'kHeading=0x1720\+0x2B4', code) and '0x2B4' in doc
    open_twist = float(re.search(r'kOpenTwist=([0-9.]+)f', twist).group(1))
    assert abs(math.degrees(open_twist) - 120.0) < 1e-4 and '120' in readme
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        import sgo
        nix = sgo.load(data=rootcpk.default().read('OBJECT', 'V612_NIX.SGO'))
        assert nix['xgs_scene_object_class'] == 'Vehicle612_nix'
        assert [-70.0, 70.0] == nix['begaruta_pilot_class'][0][1][2:], nix['begaruta_pilot_class']
        assert '±70°' in readme


@test
def embedded_seat_aim_wired() -> None:
    """The live-layout fixture is used by Nix and Proteus; a Proteus mount another seat borrows turns to that seat's aim
    before the stock slot 5 poses the model (so the muzzles the slot copies into the weapon are the borrowed aim's)."""
    nix, weapons = src('src/nix.cpp'), src('src/proteus_weapons.inc')
    player_aim = nix.split('unsigned char* PlayerAim(', 1)[1].split('\n}\n', 1)[0]
    assert 'seataim::Object(seat)' in player_aim and 'At<unsigned char*>(seat,kSeatAim)' not in player_aim
    aim = weapons.split('void AimBorrowed(', 1)[1].split('\n}\n', 1)[0]
    assert 'seataim::Follow(seataim::Object(SeatAt(v,static_cast<unsigned>(op))),seataim::Object(SeatAt(v,seat))' in aim
    assert '(axis,true)' in aim, 'each changed axis is applied to its bones as the stock axis step does'
    post = weapons.split('void __fastcall ProteusWeaponPost(', 1)[1].split('\n}\n', 1)[0]
    assert post.index('AimBorrowed(') < post.index('nextWeaponPost)(object,step)'), 'aim before the stock pose and muzzle copy'
    assert 'add_executable(seat_aim_check' in src('CMakeLists.txt')


@test
def proteus_wired() -> None:
    """The Proteus rework (src/proteus.cpp and its .inc files, src/proteus_logic.h, README 普罗透斯, docs/proteus-re.md):
    every Proteus* key the ini ships is read, range-checked (all but the two switches), and documented in README.md; the
    class crew.cpp chains for it (VehicleBigBegaruta, its native player update slot 4) is the one proteus.cpp reworks; it
    is built (its own target_sources line) with its offline check, which includes the rules' header alone; every EDF.dll
    address it checks is in docs/proteus-re.md; the mission's reset, the per-frame step (before the plugin-off return: it
    gives the stock numbers back), the install, the turret camera's lift, the shield's SGO (the EMC table's last round,
    preloaded with the others) and the EDF6AutoTurret link (one export name, both turret pickers weighed) are wired; the
    weapons are the stock mounts only (no custom rounds, no damage hook); the HUD's layout check covers it. With the game
    present: every VehicleBigBegaruta SGO has the 5 m foot radius the code takes as a constant and four seats, and the
    50 deg walkable slope whose 1.2 m step README.md quotes."""
    import math
    import rootcpk
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/proteus-re.md')
    code, crew, cmake, check = src('src/proteus.cpp'), src('src/crew.cpp'), src('CMakeLists.txt'), src('tools/proteus_check.cpp')
    weapons, shield = src('src/proteus_weapons.inc'), src('src/proteus_shield.inc')
    keys = re.findall(r'^(Proteus\w+)=', ini, re.M)
    assert len(keys) >= 30 and 'ProteusRework' in keys, keys
    for gone in ('ProteusMarkKey', 'ProteusDriverGun', 'ProteusSalvoCount', 'ProteusShieldArc', 'ProteusShieldBlock'):
        assert gone not in keys and f'L"{gone}"' not in plugin, f'{gone} was retired with the custom rounds / panel shield'
    for key in keys:
        assert f'L"{key}"' in plugin and key in readme, key
        if key not in ('ProteusRework', 'ProteusTwoSeats'):
            assert f'Fix("{key}"' in plugin or f'FixInt("{key}"' in plugin, f'{key} is not range-checked'
    vt = re.search(r'kVtBig=(0x[0-9A-F]+)', code).group(1)
    assert re.search(rf'\{{{vt},0x644350,"BigBegaruta",kFindSeat,4\}}', crew), 'crew.cpp must chain the Proteus player update'
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/proteus.cpp)' in cmake
    assert 'add_executable(proteus_check EXCLUDE_FROM_ALL tools/proteus_check.cpp)' in cmake
    assert re.findall(r'#include "([^"]+)"', check) == ['../src/proteus_logic.h'], 'proteus_check takes the rules alone'
    rvas = set()
    for block in re.findall(r'const Sig k\w+\[\]=\{(.*?)\n\};', code, re.S):
        rvas.update(re.findall(r'\{(0x[0-9A-F]+),\{', block))
    rvas.update(re.findall(r'Matches\((0x[0-9A-F]+),', shield + weapons))
    rvas.update(re.findall(r'k\w+=(0x[0-9A-F]{6,7})', shield + weapons))
    assert len(rvas) >= 25, rvas
    for rva in sorted(rvas):
        assert rva.upper().replace('0X', '0x') in doc or rva in doc, f'docs/proteus-re.md does not mention {rva}'
    assert 'ResetProteus();' in src('src/mission.cpp') and 'InstallProteus();' in plugin
    frame = crew.split('void __fastcall InputHook', 1)[1]
    assert frame.index('&ProteusFrame') < disabled_return_offset(frame), 'the Proteus step must run with the plugin off'
    assert frame.index('&ProteusFrame') < frame.index('&SeatSwitchFrame'), 'the seats it closes are closed before the seat switch asks'
    step = code.split('void Frame(unsigned char* v)', 1)[1].split('\n}', 1)[0]
    assert step.index('Seats(*u,v,c.proteusTwoSeats,true);') < step.index('Guns(*u,c);') < step.index('BarrierFrame(*u,v);')
    gunner = code.split('bool LocalGunner(', 1)[1].split('\n}\n', 1)[0]
    assert 'LivingSoldierInSeat(image,seat)' in gunner and 'IsOnlineAuthority(' in gunner, 'only a real local gunner fires here'
    give = code.split('void GiveBack(Unit& u', 1)[1].split('\n}', 1)[0]
    assert 'DropBarrier(u,why);' in give and 'OpenSeats(u,v);' in give and 'GunsBack(u);' in give
    assert 'Put<float>(a.weapon,kRate,a.rate);Put<float>(a.weapon,kSpread,a.spread);' in weapons, 'what is given back is what was taken'
    lookup = code.split('Unit* UnitOf(', 1)[1].split('\n}\n', 1)[0]
    active = code.split('Unit* ActiveOf(', 1)[1].split('\n}\n', 1)[0]
    assert 'if(u.ref.Is(v))return &u;' in lookup and 'if(u.ref.Is(v))' in active, \
        'lookup and active consumers must validate the complete ObjRef; address reuse only selects a free slot'
    assert 'ControlFresh(u,v,GameMs())' in active and 'u.active && !u.net.remote' in active, \
        'replicas require fresh control; the local active path cannot consume stale remote state'
    # A Proteus no player has ridden is not crewed (the helicopters' rule, crew.cpp Crew).
    assert 'if(!st.playerAt)return;' in crew, 'every unused parked vehicle waits for its first player driver'
    # The stock weapons only: no custom rounds, no damage-call redirect, the stock damage path untouched.
    for gone in ('ProteusGunRound', 'ProteusSalvoRound', 'ProteusRoundsReady', 'ProteusDamageThunk', 'kProteusHoldCountdown'):
        for rel in ('src/crew.h', 'src/jet_bay.cpp', 'src/proteus.cpp', 'src/proteus.h', 'src/subcarrier.cpp', 'src/vehsound.cpp'):
            assert gone not in src(rel), f'{gone} in {rel}'
    assert 'return to==image+kDamageTarget;' in src('src/subcarrier.cpp')
    assert '0x54A586' not in code and 'RedirectCall' not in code
    # The shield: the stock barrier round raised through the EMC table (preloaded whenever its file is installed).
    bay = src('src/jet_bay.cpp')
    assert 'L"app:/object/edf6vc_proteus_shield.sgo",L"EDF6VC_PROTEUS_SHIELD.SGO"' in bay
    assert 'static_assert(kEmcCount==static_cast<int>(EmcRound::proteusShield)+1' in bay
    assert 'PreloadShells(mgr,Preloaded(Body::gunship));' in src('src/jet_spawn.cpp')
    assert 'EmcFire(EmcRound::proteusShield,v,p,at,1.0f)' in shield
    assert "SHIELD_FILE = 'OBJECT/EDF6VC_PROTEUS_SHIELD.SGO'" in src('tools/make_proteus.py')
    assert 'ProteusViewLift(' in src('src/turretcam.cpp')
    assert 'ProteusBorrowedWeapons(v,r.seat,' in src('src/vhud.cpp')
    link = src('common/edf/aimlink.h')
    name = re.search(r'kPriorityZone\[\]="(\w+)"', link).group(1)
    assert f'extern "C" __declspec(dllexport) bool __cdecl {name}(' in code, name
    assert 'link::kPriorityZone' in src('autoturret/src/designate.cpp')
    assert 'distance*PriorityWeight(*e)' in src('autoturret/src/plugin.cpp') and 'distance*PriorityWeight(e)' in src('autoturret/src/gunner.cpp')
    assert 'StockLayoutApart(1920,&sceneProteus)' in src('tools/hud_view.cpp')
    foot = float(re.search(r'kFootRadius=([0-9.]+)f', code).group(1))
    assert abs(foot * (1.0 - math.sin(math.radians(50.0))) - 1.17) < 0.01 and '1.2 米' in readme
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        import sgo
        game = rootcpk.default()
        seen = 0
        for name in game.names('OBJECT'):
            if not name.upper().endswith('.SGO') or not ('PROTEUS' in name.upper() or 'BIGBEGARUTA' in name.upper()):
                continue
            v = sgo.load(data=game.read('OBJECT', name))
            if v.get('xgs_scene_object_class') != 'VehicleBigBegaruta':
                continue
            seen += 1
            assert v['begaruta_rigid_body'][1] == foot and v['begaruta_rigid_body'][3] == 50.0, (name, v['begaruta_rigid_body'])
            assert len(v['vehicle_riding_position']) == 4, name
        assert seen >= 8, seen


@test
def cockpit_warnings_wired() -> None:
    """The cockpit's warnings (src/warn.cpp, hud.cpp, jetaudio.cpp): their ini keys are read, shipped and documented; one
    owner sounds the threats (warn.cpp, not playerjet.cpp any more); every warning has its annunciator text and every
    callout its file name, text and repeat; WarnTick runs before the HUD's publish carries what it decides; the offline
    checks are build targets."""
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('WarnAudio', 'WarnVoice', 'WarnVolume'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    assert 'Fix("WarnVolume"' in plugin
    assert 'ThreatTone' not in src('src/playerjet.cpp') and 'ThreatTone' not in src('src/jetaudio.h')
    warn_h, audio_h, audio, hud = src('src/warn.h'), src('src/jetaudio.h'), src('src/jetaudio.cpp'), src('src/hud.cpp')
    warns = re.search(r'enum Warn : int \{([^}]*)\}', warn_h).group(1)
    n_warn = len([w for w in warns.split(',') if w.strip() and 'kWarnCount' not in w])
    texts = hud.split('kWarnText[kWarnCount]={', 1)[1].split('};', 1)[0]   # the texts' keys (src/hudtext.inc)
    keys = re.findall(r'Tx::(\w+)', texts)
    assert len(keys) == n_warn, (texts, n_warn)
    assert all(f'HUDTEXT({k},' in src('src/hudtext.inc') for k in keys), keys
    calls = re.search(r'enum Callout : int \{([^}]*)\}', audio_h).group(1)
    n_call = len([c for c in calls.split(',') if c.strip() and 'kCallCount' not in c])
    for table in ('kCallName[kCallCount]={', 'kCallText[kCallCount]={'):
        assert len(re.findall(r'(?<!\w)L"', audio.split(table, 1)[1].split('};', 1)[0])) == n_call, table
    repeats = audio.split('kCallRepeat[kCallCount]={', 1)[1].split('};', 1)[0]
    assert len(repeats.split(',')) == n_call, repeats
    tick = src('src/crew.cpp').split('void FrameTick()', 1)[1].split('\n}', 1)[0]
    assert 0 <= tick.find('&WarnTick') < tick.find('&HudPublish'), 'WarnTick must run before HudPublish'
    cmake = src('CMakeLists.txt')
    assert 'src/warn.cpp' in cmake.split('add_library(EDF6VehicleCrew', 1)[1].split(')', 1)[0]
    for target in ('warn_check', 'hud_view'):
        assert f'add_executable({target} EXCLUDE_FROM_ALL' in cmake, target



@test
def hud_scale_one_source() -> None:
    """The plugin HUD's size (src/hudscale.h, docs/hud-re.md §0.1): HudDraw takes its scale from hudscale::Of over the
    game's screen (the read signature-checked) and the ini's HudScale, not from the viewport's own height; every line's
    font scale is times it (Measure and Draw); HudScale is read, range-checked on hudscale's limits, shipped and
    documented; the offline check (tools/hud_view.cpp) runs the scale cases and is a CTest."""
    hud, plugin, ini, readme = src('src/hud.cpp'), src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert '#include "hudscale.h"' in hud
    assert '/1080' not in hud.replace(' ', ''), 'a size from the viewport height alone: hudscale::Of'
    draw = hud.split('void HudDraw(', 1)[1].split('\n}', 1)[0]
    assert 's=HudScaleOf(w,h)' in draw and 'text.s=s' in draw
    assert 'hudscale::Of(uiW,uiH,w,h,Cfg().hudScale)' in hud
    assert 's=hudscale::FitMap(s,width,height);' in hud and 'if(text)text->s=s;' in hud, 'map panel fitting must share geometry and font scale'
    for fn in ('void Measure(Text& t,Line& l)', 'void Draw(Text& t,const Line& l)'):
        assert 'Font(t,hudscale::Font(l.scale,t.s))' in hud.split(fn, 1)[1].split('\n}', 1)[0], fn
    assert 'for(const auto& u:kUiSigs)' in hud and '0x94E24F' in hud
    assert 'L"HudScale"' in plugin and 'Fix("HudScale",n.hudScale,hudscale::kUserMin,hudscale::kUserMax)' in plugin
    assert re.search(r'^HudScale=1\.0', ini, re.M) and '`HudScale`' in readme
    view = src('tools/hud_view.cpp')
    assert 'ScaleOfChecks()+ScaleDrawnChecks()' in view and 'add_test(NAME hud_layout COMMAND hud_view' in src('CMakeLists.txt')

@test
def vehicle_sound_wired() -> None:
    """The ground vehicles' sounds (src/vehsound.cpp, vsynth.h, vehmix.h; README 功能 16, docs/sound-re.md §9): their ini
    keys are read, range-checked, shipped and documented; every clip jetaudio.cpp makes has a WAV name, a loop flag and a
    peak, and README names every WAV a player may put next to the DLL; the stock presets silenced are the ones the doc
    gives (car_base_se_table's idle / drive / turn and the turret's move / stop, the engine's loop handles, FireSe's
    preset) and every EDF.dll address the file reads is signature-checked; the step runs from every vehicle's input
    before the plugin's Enabled test (it gives the stock sounds back when off), is installed and reset with the mission;
    the offline check is a CMake target built from the headers the plugin uses, and the plugin builds the file."""
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/sound-re.md')
    code, audio, audio_h, crew, mission, cmake = (src('src/vehsound.cpp'), src('src/jetaudio.cpp'), src('src/jetaudio.h'),
                                                  src('src/crew.cpp'), src('src/mission.cpp'), src('CMakeLists.txt'))
    keys = ('VehicleSound', 'VehicleEngineVolume', 'VehicleTurretVolume', 'VehicleReloadVolume', 'VehicleGunVolume', 'VehicleMgVolume',
            'VehicleMissileVolume')
    for key in keys:
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in keys[1:]:
        assert re.search(rf'Fix\("{key}",n\.\w+,0\.0f,4\.0f\)', plugin), f'{key} is not range-checked 0..4'
    clips = re.search(r'enum Clip : int \{([^}]*)\}', audio_h).group(1)
    n_clip = len([c for c in clips.split(',') if c.strip() and 'kClipCount' not in c])
    names = re.findall(r'L"([a-z_]+)"', audio.split('kClipName[kClipCount]={', 1)[1].split('};', 1)[0])
    assert len(names) == n_clip, (names, n_clip)
    for table in ('kClipLoops[kClipCount]={', 'kClipPeak[kClipCount]={'):
        assert len(audio.split(table, 1)[1].split('};', 1)[0].split(',')) == n_clip, table
    for name in names:
        assert f'`{name}`' in readme, f'README.md: the WAV name {name}'
    assert 'EDF6VehicleCrew_veh_<名字>.wav' in readme and '_veh_%ls.wav' in audio
    assert 'kEnginePresets[3]={0,1,2}' in code and 'kTurretPresets[4]={13,14,15,16}' in code
    assert 'kEngineHandles[3]={0x1A40,0x1A50,0x1A60}' in code and 'kFirePreset=0x380' in code and 'kEngineLoad=0x1A80' in code
    assert 'kAimTableVtable=0x17DDEA8' in code and 'kFireLoop=0xE28' in code and 'kAimEntry=0xB0' in code
    for rva in ('0x676370', '0x1A20', '0x1A40', '0x1A80', '0x632BD0', '0x380', '0x36C', '0xE0C', '0xE68', '0x7A8CD0', '0x1A38',
                '0x17DDEA8', '0x5F33F0', '0xE28', '0x698005'):
        assert rva in doc, f'docs/sound-re.md: {rva}'
    # Every RVA the file names (a code address: 0x5..../0x6..../0x7....) is in its signature table or checked by name.
    sigs = code.split('const Sig kSigs[]={', 1)[1].split('};', 1)[0]
    for rva in set(re.findall(r'\b0x[5-7][0-9A-F]{5}\b', re.sub(r'//[^\n]*', '', code))):
        named = re.search(rf'(\w+)={rva}\b', code)
        assert rva in sigs or (named and (named.group(1) in sigs or f'Matches({named.group(1)},' in code)), f'{rva} is not checked'
    hook = crew.split('void __fastcall InputHook(', 1)[1]
    assert 0 <= hook.find('&VehicleSound,v') < disabled_return_offset(hook), 'VehicleSound before the Enabled test'
    assert 'ResetVehicleSound();' in mission and 'InstallVehicleSound();' in plugin
    # The listener is the camera's for the vehicles too: placed whenever VehicleSound is on (not only once a jet sounded),
    # and a stock sound is held only while ours can be heard (the clips made and the listener placed).
    tick = src('src/jetsound.cpp').split('void JetSoundTick()', 1)[1].split('\n}', 1)[0]
    assert 'if((jets && started) || (Cfg().enabled && Cfg().vehicleSound))Tick();' in tick, 'JetSoundTick: the vehicles need the listener'
    on = code.split('void VehicleSound(unsigned char* v)', 1)[1].split('\n}', 1)[0]
    assert 'audio::ClipsReady() && SoundListening()' in on, 'VehicleSound: no stock sound held while nothing of ours can sound'
    assert 'src/vehsound.cpp' in cmake.split('add_library(EDF6VehicleCrew', 1)[1].split(')', 1)[0]
    assert 'add_executable(vsound_check EXCLUDE_FROM_ALL tools/vsound_check.cpp)' in cmake
    check = src('tools/vsound_check.cpp')
    assert '#include "../src/jetaudio.cpp"' in check and '#include "../src/vehmix.h"' in check
    assert '#include "vsynth.h"' in audio and '#include "vehmix.h"' in code and 'vsound_check' in readme


@test
def vehicle_gun_calibres_wired() -> None:
    """The vehicle guns heard by calibre (src/vehmix.h ProfileOf / kProfiles, src/vehsound.cpp; the user, 2026-10-07:
    "closer to the real thing, by calibre and round: the loading, the shot, the case landing"; docs/sound-re.md §9.5): the
    weapon's facts are read from the fields the doc gives (FireBurstCount +0x370, AmmoDamage +0x89C, AmmoExplosion +0x8B0,
    ShellCase's factory +0x4E0, the shot count sent +0x1544 and fired +0xBD0) at signature-checked stores; the rail gun is
    told apart by its round's class, not its HUD label; a weapon with a physical case of its own gets no case sound of
    ours, and a case's sound is the profile's; every report, round, case and step clip is one jetaudio.cpp makes; the
    howitzer's own case lands with the stock game's biggest case's sound (tools/make_artillery.py CASE_SE, not the rifle
    case's it inherited), checked against the built SGO when the game is there; the offline check runs the calibres."""
    import rootcpk
    code, mix, doc, check, rounds = (src('src/vehsound.cpp'), src('src/vehmix.h'), src('docs/sound-re.md'), src('tools/vsound_check.cpp'),
                                     src('src/rounds.cpp'))
    assert 'kFireBurst=0x370,kWeaponDamage=0x89C,kWeaponBlast=0x8B0,kCaseFactory=0x4E0,kWeaponShots=0x1544,kWeaponFired=0xBD0' in code
    sigs = code.split('const Sig kSigs[]={', 1)[1].split('};', 1)[0]
    for rva in ('0x68CE85', '0x68D6EC', '0x68D82F', '0x68DC6D', '0x68DCA2', '0x690540', '0x690505', '0x690586', '0x6947AD'):
        assert '{' + rva + ',' in sigs, f'src/vehsound.cpp kSigs: {rva}'
        assert rva in doc, f'docs/sound-re.md: {rva}'
    for field in ('+0x370', '+0x89C', '+0x8B0', '+0x4E0', '+0x1544', '+0xBD0'):
        assert field in doc, f'docs/sound-re.md: {field}'
    assert '".?AVFactory@SolidBullet01Rail@@"' in code and '.?AVFactory@SolidBullet01Rail@@' in rounds and 'm.rtti=c ? c->rtti' in rounds
    case_of = code.split('void CaseOf(', 1)[1].split('\n}', 1)[0]
    assert 'g.stockCase' in case_of and 'pf.casing.clip' in case_of, 'CaseOf: no case of ours over a physical one'
    rapid = code.split('void Rapid(', 1)[1].split('\n}', 1)[0]
    assert '!g.stockCase' in rapid and 'pf.casing.clip' in rapid, 'Rapid: the brass loop not over a physical case'
    assert 'g.stockCase=At<const void*>(w,kCaseFactory)!=nullptr' in code
    assert 'vmix::RemoteShot(g->shots,sent,At<std::int32_t>(w,kWeaponFired))' in code
    # Every clip the table names is a Clip of jetaudio.h; the calibres the doc lists are the enum's.
    clips = set(re.findall(r'\b(kClip\w+)\b', src('src/jetaudio.h').split('enum Clip : int {', 1)[1].split('}', 1)[0]))
    table = mix.split('constexpr Profile kProfiles[', 1)[1].split('};', 1)[0]
    assert set(re.findall(r'audio::(kClip\w+)', table)) <= clips
    bores = re.search(r'enum class Bore : int \{([^}]*)\}', mix).group(1)
    names = [b.strip() for b in bores.split(',') if b.strip() and b.strip() != 'count']
    assert len(re.findall(r'^    \{Bore::(\w+),', table, re.M)) == len(names), 'kProfiles: one row a calibre'
    assert re.findall(r'^    \{Bore::(\w+),', table, re.M) == names, 'kProfiles: rows in the enum\'s order'
    for name in ('Calibres();', 'CalibreScenario(out);', 'scenario_calibres.wav'):
        assert name in check, f'tools/vsound_check.cpp: {name}'
    # The howitzer's case: the big case's landing sound, the rifle case's refused as the expected stock one.
    assert make_artillery.CASE_SE[1] == 'weapon_Common_shell_huge' and make_artillery.STOCK_CASE_SE[1] == 'weapon_Common_shell_srifle'
    assert make_artillery.same_se([0.0, 'x', 0.2000000029802, 0.8, 1.0, 5.0], [0.0, 'x', 0.2, 0.8, 1.0, 5.0])
    assert not make_artillery.same_se([0.0, 'y', 0.2, 0.8, 1.0, 5.0], [0.0, 'x', 0.2, 0.8, 1.0, 5.0])
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        game = vc.Game(rootcpk.DEFAULT_GAME)
        for stock in make_artillery.STOCK_GUNS:
            g = dsgo.to_py(dsgo.parse(make_artillery.howitzer_sgo(game, stock)).root)
            assert make_artillery.same_se(g['ShellCase'][2], make_artillery.CASE_SE), (stock, g['ShellCase'])


@test
def sazabi_sound_wired() -> None:
    """The Sazabi's sounds (src/sazabi_sound.cpp; docs/sound-re.md §10): its tick runs once a frame from every vehicle's
    input before the plugin's Enabled test (it stops its loops when off) and is reset with the mission; its clips are
    jetaudio.cpp's (one table, kSazabiSfxClip / kSazabiLoopClip, used by the plugin and the offline check alike, sized
    against sazabi_sound.h's enums); it hears through the vehicles' switch and their group volumes; the offline check runs
    its clips, rules and scenario; README and the doc describe it."""
    crew, mission, code, audio_h, check = (src('src/crew.cpp'), src('src/mission.cpp'), src('src/sazabi_sound.cpp'),
                                           src('src/jetaudio.h'), src('tools/vsound_check.cpp'))
    hook = crew.split('void __fastcall InputHook(', 1)[1]
    assert 0 <= hook.find('&SazabiSoundTick);') < disabled_return_offset(hook), 'SazabiSoundTick before the Enabled test'
    assert 'ResetSazabiSound();' in mission and '#include "sazabi_sound.h"' in mission
    assert 'constexpr int kSazabiSfxClip[]=' in audio_h and 'constexpr int kSazabiLoopClip[]=' in audio_h
    assert 'static_assert(sizeof(kSazabiSfxClip)' in code and 'kSfxClip[' not in code.replace('kSazabiSfxClip[', '')
    assert 'Cfg().vehicleSound' in code and 'SoundListening()' in code and 'vmix::kSzSfx[' in code and 'vmix::kFarAt' in code
    assert 'kSazabiSfxClip[' in check and 'SazabiRules();' in check and 'SazabiScenario(out);' in check
    assert 'sazabi_sound.cpp' in src('README.md') and '## 10. 沙扎比的声音' in src('docs/sound-re.md')


@test
def view_distance_keeps_far_pass_start() -> None:
    """ViewDistance / MapViewDistance raise only the near pass's end (and the far pass's end): the far pass's start
    (env +0x1A4, camera +0x30) is the mission's, because the far-only scenery (the horizon's mountain ring, the
    simulator's sky dome) is drawn by nothing else. 2026-10-06: moved out to ViewDistance-500 it cut NW_HENDEN's
    mountains nearer than 2500 m and the rest hung in the sky. The rule is src/view_clip.h, run by
    tools/view_clip_check.cpp; view.cpp writes only the far clip and the far pass's end."""
    code, rule, cmake = src('src/view.cpp'), src('src/view_clip.h'), src('CMakeLists.txt')
    assert '#include "view_clip.h"' in code and 'viewclip::Raise(' in code
    assert 'kOverlap' not in code and 'kCamDistantNear' not in code, 'view.cpp: the far pass start must not be moved'
    raise_fn = code.split('bool Raise(', 1)[1].split('\n}', 1)[0]
    puts = re.findall(r'Put<float>\(at,([^,]+),', raise_fn)
    assert puts == ['farClip', 'farClip+8'], f'view.cpp Raise writes {puts}: only the far clip and the far pass end'
    body = rule.split('inline bool Raise(', 1)[1].split('\n}', 1)[0]
    assert 'distantNear' not in body, 'view_clip.h Raise: the far pass start is the mission\'s'
    assert 'add_executable(view_clip_check EXCLUDE_FROM_ALL tools/view_clip_check.cpp)' in cmake
    assert 'view_clip_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0], 'view_clip_check runs in CTest'
    assert '#include "../src/view_clip.h"' in src('tools/view_clip_check.cpp')


@test
def gunship_gunner_seat() -> None:
    """tools/make_jets.py with_gunner_seat / check_gunner_seat on synthetic SGOs (no game needed): the gunship gets a second
    seat with the pilot's locators and the stock door gunner's pose, class mask and key row, and nothing else changes; a
    gunship that has it already, a stock gunner seat not as expected, or a weapon put on the gunner seat are refused. The
    C++ side's seat number (src/crew.h kGunnerSeat) is the generator's, the crew's ini keys are read and documented."""
    import sgo
    pilot = ['door1', 'seat1', ['loc1', 0.0, 0.0], '506_HELI_DRIVER', 9, 10.0, make_jets.PILOT_KEYS]
    setup = [[1.0, 1.0], [make_jets.GUNSHIP_MARK, 0.0003], [999900.0, 1.666],
             [['app:/weapon/a.sgo', [0.0001, 0.1]], ['app:/weapon/b.sgo', [0.0001, 0.1]], ['app:/weapon/v_fuel01.sgo']]]
    weapons = [['mdl', 0], ['mdl', 0], ['mdl', -1]]

    def gunship(seats: list, settings: list) -> bytes:
        return sgo.write(0x102, {'mission_setup': setup, 'vehicle_riding_position': seats, 'vehicle_weapon_setting': settings,
                                 'game_object_durability': 1500.0})

    def stock(pose: str, keys: int) -> bytes:
        door = ['door2', 'seat2', ['loc2', 0.0, 0.0], pose, 15, 0, keys]
        return sgo.write(0x102, {'vehicle_riding_position': [['d', 's', ['l', 0.0, 0.0], '410_HELI_DRIVER', 9, 15.0, 5], door]})

    good = stock(make_jets.GUNNER_POSE, make_jets.GUNNER_KEYS)
    before = gunship([pilot], weapons)
    after = make_jets.with_gunner_seat(before, good)
    make_jets.check_gunner_seat(after)
    old, new = sgo.read(before)[1], sgo.read(after)[1]
    assert [k for k in old if old[k] != new[k]] == ['vehicle_riding_position'] and set(new) == set(old)
    seats = new['vehicle_riding_position']
    assert len(seats) == make_jets.GUNNER_SEAT + 1 and seats[0] == sgo.read(before)[1]['vehicle_riding_position'][0]
    gunner = seats[make_jets.GUNNER_SEAT]
    assert gunner[:3] == seats[0][:3] and gunner[3] == make_jets.GUNNER_POSE and gunner[4] == 15 and gunner[6] == make_jets.GUNNER_KEYS
    for bad, why in ((lambda: make_jets.with_gunner_seat(after, good), 'a second gunner seat'),
                     (lambda: make_jets.with_gunner_seat(before, stock('410_HELI_DRIVER', 5)), 'the stock seat not a gunner seat')):
        try:
            bad()
        except ValueError:
            continue
        raise AssertionError(f'with_gunner_seat took {why}')
    armed = gunship(seats, [['mdl', 0], ['mdl', 1], ['mdl', -1]])
    try:
        make_jets.check_gunner_seat(armed)
    except make_jets.GunnerSeatError:
        pass
    else:
        raise AssertionError('check_gunner_seat let a weapon sit on the gunner seat')
    try:
        make_jets.check_gunner_seat(before)
    except make_jets.GunnerSeatError:
        pass
    else:
        raise AssertionError('check_gunner_seat took a gunship with one seat')
    m = re.search(r'constexpr unsigned kGunnerSeat=(\d+);', src('src/crew.h'))
    assert m and int(m.group(1)) == make_jets.GUNNER_SEAT, 'src/crew.h kGunnerSeat'
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('GunshipBoardGunner', 'GunshipGunnerKey'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    assert 'FixInt("GunshipGunnerKey"' in plugin, 'GunshipGunnerKey is range-checked'


@test
def gunship_cannon_round() -> None:
    """tools/make_jets.py gun_round / check_gun_round on a synthetic stock round (no game needed), for each side gun
    (the long-range cannon and the 25 mm gatling): its round is the stock gunship's solid round with the gun's numbers,
    nothing else changed, and the check refuses a round that falls short of the reach or penetrates. The C++ side fires
    the files the tool writes, with the tool's reach and speed (src/jet_bay.cpp kCannon* / kGatling*), as stores of the
    gunship's (src/playerjet_board.inc CANNON, GATLING); the installer writes them (make_jets.names) and the README tells
    of them. The three guns carry the same damage a second (shells 300 / 2.5 s, cannon 60 x 2, gatling 12 x 10)."""
    import sgo
    stock = {'xgs_scene_object_class': 'DemoIndirectFire', 'indirect_fire_damage': 2000.0,
             'indirect_fire_param': [[1.2, 0.0], [800.0, 0.0], 1, 0, 'SolidBullet01', 20.0, 0.0, 10.0, 2.0, 0.0, 600, 1,
                                     [0.4, 0.4, 6.0, 1.0], [], 0, 60, 0, [0, 'weapon_KUBAKU_Cannon_shot', 0.5, 1.0, 1.0, 500.0],
                                     [0, 'common_damages_kuubaku_Cannon21', 0.9, 1.0, 3.0, 200.0]]}

    class Game:
        def read(self, folder: str, name: str) -> bytes:
            assert (folder, name) == ('OBJECT', make_jets.IMPACT_STOCK), (folder, name)
            return sgo.write(0x102, stock)

    bay, board, readme = src('src/jet_bay.cpp'), src('src/playerjet_board.inc'), src('README.md')
    for gun, cpp, name in ((make_jets.CANNON, 'Cannon', 'CANNON'), (make_jets.GATLING, 'Gatling', 'GATLING')):
        data = make_jets.gun_round(Game(), gun)
        make_jets.check_gun_round(data, gun)
        old, new = sgo.read(Game().read('OBJECT', make_jets.IMPACT_STOCK))[1], sgo.read(data)[1]
        assert set(old) == set(new) and [k for k in old if old[k] != new[k]] == ['indirect_fire_damage', 'indirect_fire_param']
        changed = [i for i, (a, b) in enumerate(zip(old['indirect_fire_param'], new['indirect_fire_param'])) if a != b]
        assert changed == [0, 5, 7, 9, 10, 11, 12, 15], (name, changed)   # scatter, speed, size, blast, life, penetration, colour, wait
        assert new['indirect_fire_param'][17] == old['indirect_fire_param'][17], 'the stock cannon fire sound kept'
        for i, value, why in ((10, 50, 'a life short of the reach'), (11, 1, 'a penetrating round'), (9, 25.0, 'a whale-sized blast')):
            version, bad = sgo.read(data)
            bad['indirect_fire_param'][i] = value
            try:
                make_jets.check_gun_round(sgo.write(version, bad), gun)
            except make_jets.GunRoundError:
                continue
            raise AssertionError(f'check_gun_round ({name}) took {why}')
        assert gun.speed * gun.life >= gun.reach, name
        assert f'k{cpp}File[]=L"{gun.file}"' in bay, f'src/jet_bay.cpp k{cpp}File'
        assert f'k{cpp}Sgo[]=L"app:/object/{gun.file.lower()}"' in bay, f'src/jet_bay.cpp k{cpp}Sgo'
        m = re.search(rf'k{cpp}Reach=([\d.]+)f', bay)
        assert m and float(m.group(1)) == gun.reach, f'src/jet_bay.cpp k{cpp}Reach'
        m = re.search(rf'k{cpp}Speed=([\d.]+)f', bay)
        assert m and float(m.group(1)) == gun.speed * 60.0, f'src/jet_bay.cpp k{cpp}Speed (m/s) is the round\'s speed a frame'
        assert f'OBJECT/{gun.file}' in make_jets.names(), gun.file
        assert '{L"","' + name + '",StoreRole::gun' in board, f'src/playerjet_board.inc kSpecials {name}'
        assert gun.file in readme, f'README.md: {gun.file}'
    assert make_jets.SIDE_GUNS == (make_jets.CANNON, make_jets.GATLING), 'src/jet_bay.cpp kSideGuns\' order'
    assert re.search(r'kSideGuns\[\]=\{\s*\{kCannonSgo,kCannonFile,[^}]*\},\s*\{kGatlingSgo,kGatlingFile,', bay), 'kSideGuns in that order'
    assert 3.0 <= make_jets.CANNON_RADIUS <= 6.0, 'a few metres of blast (and >= 3 m: as the drill charge, docs/drill-re.md §3)'
    assert make_jets.GATLING_RADIUS < 3.0, 'the 25 mm round does not break buildings (under the drill charge\'s 3 m)'
    assert make_jets.GATLING_REACH < make_jets.CANNON_REACH and make_jets.GATLING_SIZE < make_jets.CANNON_SIZE
    num = r'([\d.]+)f'
    rate = {}
    for cpp in ('Cannon', 'Gatling'):
        gap = re.search(rf'constexpr ULONGLONG k{cpp}GapMs=(\d+);', bay)
        dmg = re.search(rf'constexpr float k{cpp}Damage=' + num, bay)
        assert gap and dmg, cpp
        rate[cpp] = float(dmg.group(1)) * 1000.0 / float(gap.group(1))
    gap = re.search(r'constexpr ULONGLONG kGunshipGapMs=(\d+);', bay)
    dmg = re.search(r'constexpr float kGunshipDamage=' + num, bay)
    assert gap and dmg
    rate['Shells'] = float(dmg.group(1)) * 1000.0 / float(gap.group(1))
    assert max(rate.values()) - min(rate.values()) < 1e-6, f'the three guns carry the same damage a second: {rate}'
    assert '炮舰机的机炮' in readme, 'README.md: the gunship cannon'


def _recoil_game(mission_weapon: str, mission_recoil: list, call_weapon: str, call_recoil: list,
                 classic: bool, vehicle_weapon: str | None = None) -> object:
    """A stand-in Root.cpk for autoturret/tools/npc_recoil.py: one mission object (one gun and an empty
    mount, the object's own vehicle_setup naming the player gun) and its call (a vehicle setup under
    Ammo_CustomParameter)."""
    import struct
    import sgo
    def setup(weapon: str, recoil: list) -> list:
        return [[1.0, 1.0], [0.1, 10.0], [[weapon, recoil, [20.0, 0.01, 0.1]], [0]]]
    mission = {'game_object_durability': 100.0, 'mission_setup': setup(mission_weapon, mission_recoil),
               'vehicle_setup': setup(vehicle_weapon or call_weapon, [0.0, 2.0]), 'resource': [mission_weapon]}
    def n(v: object) -> object:   # DSGO numbers are all doubles (the empty mount is [0.0])
        return dsgo.Node([n(c) for c in v]) if isinstance(v, list) else float(v) if isinstance(v, int) else v
    if classic:
        def f(v: object) -> object:
            if isinstance(v, list):
                return [f(c) for c in v]
            return sgo.Float(struct.pack('<f', v)) if isinstance(v, float) else v
        obj = sgo.write_depth_first(0x102, {k: f(v) for k, v in mission.items()})
    else:
        obj = dsgo.write(dsgo.Document(dsgo.Node([n(v) for v in mission.values()], dict(enumerate(mission))), []))
    call_setup = n([[1.0, 1.0], [0.1, 10.0], [[call_weapon, call_recoil, [20.0, 0.01, 0.1]]]])
    call = dsgo.write(dsgo.Document(dsgo.Node([n([1.0, 'app:/object/x.sgo', call_setup])],
                                             {0: 'Ammo_CustomParameter'}), []))
    files = {('OBJECT', 'X_AI.SGO'): obj, ('WEAPON', 'CALL.SGO'): call}

    class Game:
        def read(self, folder: str, name: str) -> bytes:
            return files[(folder, name)]
    return Game()


@test
def recoil_call_formats_agree() -> None:
    """Both call formats expose the same mounts, including classic SGO's lossless Float wrapper."""
    import recoil
    import sgo
    game = _recoil_game('app:/weapon/v_9tank_ai_cannon01.sgo', [0.0, 2.0],
                        'app:/weapon/v_9tank_cannon01.sgo', [0.25, 0.5], True)
    modern = game.read('WEAPON', 'CALL.SGO')
    custom = recoil.plain(dsgo.parse(modern).root.get('Ammo_CustomParameter'))
    classic = sgo.write_depth_first(258, {'Ammo_CustomParameter': recoil._as_sgo(custom)})
    expected = [('v_9tank_cannon01.sgo', [0.25, 0.5])]
    assert recoil.mounts_of(modern, 'DSGO call') == expected
    assert recoil.mounts_of(classic, 'classic SGO call') == expected


@test
def npc_recoil_takes_the_player_call() -> None:
    """autoturret/tools/npc_recoil.py: a mission mount takes the recoil of the same gun in the player's call, in
    either file format; the AI copy of a gun (`_ai` part) and the object's own vehicle_setup gun count as the
    same gun, any other gun is refused; build.py writes every file the table names, the NPC Titan included."""
    import npc_recoil
    import sgo
    import titan_ai
    for classic in (True, False):
        for weapon in ('app:/weapon/v_9tank_ai_cannon01.sgo', 'app:/weapon/v_9tank_cannon01.sgo'):
            game = _recoil_game(weapon, [0.0, 2.0], 'app:/weapon/v_9tank_cannon01.sgo', [0.25, 0.5], classic)
            with patched(npc_recoil, PLAYER_CALL={'X_AI.SGO': 'CALL.SGO'}):
                out = npc_recoil.build('X_AI.SGO', game=game)
            v = sgo.load(data=out)
            assert v['mission_setup'][2][0][1] == [0.25, 0.5], v['mission_setup']
            assert v['mission_setup'][2][1] == [0] and v['vehicle_setup'][2][0][1] == [0.0, 2.0], v
            assert out[:4] == (b'SGO\0' if classic else b'DSGO')
        # The object's vehicle_setup names the player gun when the AI gun's name does not carry it.
        game = _recoil_game('app:/weapon/v_9_tank_ai_cannon01.sgo', [0.0, 2.0], 'app:/weapon/v_9tank_cannon01.sgo',
                            [0.25, 0.5], classic)
        with patched(npc_recoil, PLAYER_CALL={'X_AI.SGO': 'CALL.SGO'}):
            assert sgo.load(data=npc_recoil.build('X_AI.SGO', game=game))['mission_setup'][2][0][1] == [0.25, 0.5]
        # The AI copy's name alone (`_ai` part) is enough when the object's vehicle_setup names another gun.
        game = _recoil_game('app:/weapon/v_9tank_ai_cannon01.sgo', [0.0, 2.0], 'app:/weapon/v_9tank_cannon01.sgo',
                            [0.25, 0.5], classic, vehicle_weapon='app:/weapon/v_7other.sgo')
        with patched(npc_recoil, PLAYER_CALL={'X_AI.SGO': 'CALL.SGO'}):
            assert sgo.load(data=npc_recoil.build('X_AI.SGO', game=game))['mission_setup'][2][0][1] == [0.25, 0.5]
        # A tagged spec (the Epsilon's ['BodyRecoil', [push, kick]]) is taken whole.
        game = _recoil_game('app:/weapon/v_9tank_ai_cannon01.sgo', ['BodyRecoil', [0.0, 0.05]],
                            'app:/weapon/v_9tank_cannon01.sgo', ['BodyRecoil', [0.1, 0.05]], classic)
        with patched(npc_recoil, PLAYER_CALL={'X_AI.SGO': 'CALL.SGO'}):
            got = sgo.load(data=npc_recoil.build('X_AI.SGO', game=game))['mission_setup'][2][0][1]
        assert got[0] == 'BodyRecoil' and [round(x, 6) for x in got[1]] == [0.1, 0.05], got   # float32 in both formats
        # Another gun in the call's slot (and in the object's own vehicle_setup): refused, not overwritten.
        game = _recoil_game('app:/weapon/v_8gun.sgo', [0.0, 2.0], 'app:/weapon/v_9tank_cannon01.sgo', [0.25, 0.5], classic,
                            vehicle_weapon='app:/weapon/v_8gun.sgo')
        with patched(npc_recoil, PLAYER_CALL={'X_AI.SGO': 'CALL.SGO'}):
            try:
                npc_recoil.build('X_AI.SGO', game=game)
            except SystemExit:
                pass
            else:
                raise AssertionError('a mount of another gun took the call recoil')
    # The range's own placeable vehicles follow the same rule (testrange/gen.py vehicle_sgo).
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    assert set(gen.PLAYER_CALLS) <= set(gen.DERIVED) - set(gen.JETS), 'gen.PLAYER_CALLS names a vehicle the range does not derive'
    body = src('testrange/gen.py').split('def vehicle_sgo(', 1)[1].split('\ndef ', 1)[0]
    assert body.count('_with_player_recoil(game, sgo_name,') == 2, 'gen.vehicle_sgo: GROUND_MISSION and DERIVED both take the player recoil'
    assert titan_ai.NAME in npc_recoil.PLAYER_CALL, 'build.py writes the NPC Titan through npc_recoil.PLAYER_CALL'
    code = src('autoturret/tools/build.py')
    assert 'for name in npc_recoil.PLAYER_CALL:' in code and 'titan_ai.build() if name == titan_ai.NAME' in code


@test
def gunship_muzzle_wired() -> None:
    """The gunship's rounds leave off its airframe, now (the user, 2026-10-06: 「炮舰机的机炮会打到自己身上」「炮舰机的轰炸炮弹，
    感觉在飞机后面出现的」): src/gunmuzzle.h's airframe and hit radii are tools/make_jets.py's (GUNSHIP_AIRFRAME, held to the
    bomber401 model and the stock shell by check_gunship_muzzle when the files are made; CANNON_SIZE x CANNON_HIT; SHELL_HIT);
    every gunship round in src/jet_bay.cpp leaves from GunshipMuzzle, none from the vehicle's origin; ShellMake zeroes the IFC's
    first-round wait (+0x2D8, param #15: the stock shell's 60 frames left it where the gunship had been a second before)
    behind its signatures; the offline check (tools/gunship_muzzle_check.cpp) is one of the offline checks CTest runs."""
    head = src('src/gunmuzzle.h')
    num = r'(-?[\d.]+)f'
    m = re.search(r'kGunship\{\{' + ','.join([num] * 3) + r'\},\{' + ','.join([num] * 3) + r'\}\}', head)
    assert m, 'src/gunmuzzle.h kGunship'
    got = [float(m.group(k)) for k in range(1, 7)]
    want = [v for part in make_jets.GUNSHIP_AIRFRAME for v in part]
    assert got == want, f'src/gunmuzzle.h kGunship {got}, tools/make_jets.py GUNSHIP_AIRFRAME {want}'
    m = re.search(r'kCannonHit=' + num, head)
    assert m and abs(float(m.group(1)) - make_jets.CANNON_SIZE * make_jets.CANNON_HIT) < 1e-6, 'src/gunmuzzle.h kCannonHit'
    m = re.search(r'kGatlingHit=' + num, head)
    assert m and abs(float(m.group(1)) - make_jets.GATLING_SIZE * make_jets.GATLING_HIT) < 1e-6, 'src/gunmuzzle.h kGatlingHit'
    m = re.search(r'kShellHit=' + num, head)
    assert m and float(m.group(1)) == make_jets.SHELL_HIT, 'src/gunmuzzle.h kShellHit'
    assert 'check_gunship_muzzle(game)' in src('tools/make_jets.py'), 'tools/make_jets.py build checks the muzzle numbers'
    bay = src('src/jet_bay.cpp')
    assert f'kGunshipSgo[]=L"app:/object/{make_jets.SHELL_STOCK.lower()}"' in bay, 'src/jet_bay.cpp kGunshipSgo is SHELL_STOCK'
    fired = re.findall(r'Shell\((kGunshipSgo|gun\.sgo),(?:gunshipReady|gun\.ready),v,(\w+),', bay)
    assert len(fired) == 3 and all(f == 'muzzle' for _sgo, f in fired), f'the gunship fires from its muzzle: {fired}'
    assert len(re.findall(r'GunshipMuzzle\(v,', bay)) == 4, 'GunshipMuzzle for the side guns, their sight line and both shells'
    assert re.search(r'MapRay\(muzzle,at,hit\)', bay), 'the NPC cannon looks along the line its round flies'
    make = bay.split('unsigned char* ShellMake(', 1)[1].split('\n}\n', 1)[0]
    assert 'if(ifcWaitOk)Put<std::int32_t>(ifc,kIfcWait,0);' in make, 'ShellMake zeroes the first-round wait'
    assert 'constexpr std::size_t kIfcWait=0x2D8;' in bay
    assert 'for(const auto& b:kIfcWaitSigs)ifcWaitOk=ifcWaitOk && Matches(' in bay, 'the wait behind its signatures'
    cmake = src('CMakeLists.txt')
    assert 'add_executable(gunship_muzzle_check EXCLUDE_FROM_ALL tools/gunship_muzzle_check.cpp)' in cmake
    checks = cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0]
    assert 'gunship_muzzle_check' in checks.split(), 'CTest runs gunship_muzzle_check'
    assert '炮舰机的炮口' in src('README.md'), 'README.md: the gunship muzzle'


@test
def readme_counts() -> None:
    readme = src('README.md')
    assert f'{len(calls.FLOWN)} 种呼叫' in readme, f'README.md: say {len(calls.FLOWN)} 种呼叫 (tools/calls.py FLOWN)'
    assert f'共 {len(calls.CALLS)} 行' in readme, f'README.md: say 共 {len(calls.CALLS)} 行 (tools/calls.py CALLS)'


@test
def stock_vehicle_hud_wired() -> None:
    """The stock vehicles' HUD (src/vhud.cpp, src/rounds.cpp, hud.cpp StockVehicleHud): its ini key is read, shipped and
    documented; the input hook gathers it after AimLines, which hides the player's line for it; the HUD publishes and
    draws it; the helis' rockets are flown by the round model, not along a straight line (the user, 2026-10-06); every
    round class rounds.cpp names is the one docs/hud-re.md §7 lists; the stock rockets tools/rounds_check.cpp flies are
    Root.cpk's (re-read when the game is there)."""
    import rootcpk
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert 'L"StockVehicleHud"' in plugin and re.search(r'^StockVehicleHud=1', ini, re.M) and 'StockVehicleHud' in readme
    crew = src('src/crew.cpp')
    hook = crew.split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    assert 0 <= hook.find('&AimLines,') < hook.find('&StockHudFrame,'), 'src/crew.cpp InputHook: AimLines must run before StockHudFrame'
    aim = crew.split('void AimLines(', 1)[1].split('\n}\n', 1)[0]
    assert 'PlayerStockOwnSight(vehicle)' in aim, 'src/crew.cpp AimLines: the stock HUD hides the player\'s line'
    hud = src('src/hud.cpp')
    assert 'PlayerStockHud(&s.stockHud)' in hud.split('void HudPublish(', 1)[1].split('\n}\n', 1)[0]
    assert 'StockDockHud(drawer' in hud.split('void HudDraw(', 1)[1].split('\n}\n', 1)[0]
    dock = hud.split('void StockDockHud(', 1)[1].split('\n}\n', 1)[0]
    assert 'StockVehicleHud(drawer' in dock and 'LoadoutDockOf(' in dock and 'LoadoutStrip(' in dock
    sight = src('src/helisight.cpp').split('bool SolveArm(', 1)[1].split('\n}\n', 1)[0]
    assert 'RoundLands(' in sight and 'kRocketStep' not in sight, 'src/helisight.cpp: the rockets flown as the game flies them'
    rounds, doc = src('src/rounds.cpp'), src('docs/hud-re.md')
    for vt, rtti in re.findall(r'\{(0x[0-9A-F]+),"\.\?AVFactory@(\w+)@@"', rounds):
        assert f'`{vt}`' in doc and rtti.split('_')[0] in doc, (vt, rtti)
    assert 'src/rounds.cpp' in src('CMakeLists.txt') and 'tools/rounds_check.cpp' in src('CMakeLists.txt')
    check = src('tools/rounds_check.cpp')
    rows = re.findall(r'\{"(V_\w+) \([^)]*\)",([\d.]+)f,([\d.]+)f,([\d.]+)f,([\d.]+)f,([\d.]+)f,(\d+),(\d+)\}', check)
    assert len(rows) == 3, rows
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        for name, speed, factor, accel, top, keep, ignite, alive in rows:
            w = dsgo.to_py(dsgo.parse(rootcpk.default().read('WEAPON', name + '.SGO')).root)
            cp = w['Ammo_CustomParameter']
            got = (w['AmmoSpeed'], w['AmmoGravityFactor'], cp[4], cp[6], cp[7][1], cp[7][0], w['AmmoAlive'])
            want = (speed, factor, accel, top, keep, ignite, alive)
            assert all(abs(float(g) - float(x)) < 1e-4 for g, x in zip(got, want)), (name, got, want)
            assert w['AmmoClass'] == 'MissileBullet01' and cp[0] == 0, (name, w['AmmoClass'], cp[0])


@test
def stock_gun_sight_ranged() -> None:
    """Articulated guns retain target lead guides, independently of actual muzzle/terrain prediction.
    Fixed/partial mounts use physical paths only. Runtime fixtures cover this contract; this guard checks wiring."""
    import rootcpk
    vhud, hud, check = src('src/vhud.cpp'), src('src/hud.cpp'), src('tools/rounds_check.cpp')
    arm = vhud.split('void Arm(', 1)[1].split('\n}\n', 1)[0]
    assert 'GunMarkOf(w,m,pos,dir,a)' in arm, 'src/vhud.cpp Arm: an arc gun\'s marks are GunMarkOf\'s'
    mark = vhud.split('void GunMarkOf(', 1)[1].split('\n}\n', 1)[0]
    assert 'roundaim::GunSight(' in mark and 'target.ok ? target.at : nullptr' in mark
    assert 'RangeTarget(v,r,eye,ms);' in vhud.split('void StockHudFrame(', 1)[1].split('\n}\n', 1)[0]
    stock = hud.split('void StockMark(', 1)[1].split('\n}\n', 1)[0]
    assert 'a.ranged && !a.physicalOnly' in stock and 'LeadMark(' in stock
    assert 'PhysicalPaths(' in stock and 'Pipper(' not in stock, 'physical endpoint is distinct from target lead cue'
    assert 'a.physicalOnly ||' in mark and 'a.targetRange=' in mark
    assert 'memcpy(a.at' not in mark and 'a.hit=' not in mark, 'lead selection cannot overwrite physical terrain result'
    assert 'SkySight();' in check.split('int main()', 1)[1]
    row = re.search(r'kE551Gun=\{"(V_\w+) \([^)]*\)",([\d.]+)f,([\d.]+)f,([\d.]+)f,(\d+)\}', check)
    assert row, 'tools/rounds_check.cpp: kE551Gun'
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        name, speed, factor, owner, alive = row.groups()
        w = dsgo.to_py(dsgo.parse(rootcpk.default().read('WEAPON', name + '.SGO')).root)
        got = (w['AmmoSpeed'], w['AmmoGravityFactor'], w['AmmoOwnerMove'], w['AmmoAlive'])
        assert all(abs(float(g) - float(x)) < 1e-4 for g, x in zip(got, (speed, factor, owner, alive))), (name, got)
        assert w['AmmoClass'] == 'RocketBullet01', (name, w['AmmoClass'])


@test
def stock_gauges_wired() -> None:
    """The stock weapon gauges (src/stockgauge.cpp; the user, 2026-10-06: "删掉原版挂载和油料显示"): its ini key is read,
    shipped and documented; it is installed at load and only through the gauges' update slots (the weapon gauge's and
    the armor gauge's slot 1; no draw call skipped); the HUD's publish says what it covers; every EDF.dll address it checks is in docs/hud-re.md §9; the fuel tank is no
    weapon in the stock HUD's arms and is read where the stock FUEL panel was, LOW FUEL its warning."""
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert 'L"HideStockGauges"' in plugin and re.search(r'^HideStockGauges=1', ini, re.M) and 'HideStockGauges' in readme
    assert 'InstallStockGauges();' in plugin.split('EML6_Load(', 1)[1]
    cmake = src('CMakeLists.txt')
    assert 'src/stockgauge.cpp' in cmake.split('add_library(EDF6VehicleCrew', 1)[1].split(')', 1)[0]
    gauge = src('src/stockgauge.cpp')
    # Only update slots (vtable slot 1) are chained, never a draw call skipped: the weapon gauge's (HUiHudWeapon) and,
    # 2026-10-09 ("上了载具以后，可以把原版的左上角的血条hud隐藏吧"), the armor gauge's (HUiHudPowerGuage).
    patched = re.findall(r'PatchVtableSlot\(reinterpret_cast<void\*\*>\(image\+(\w+)\)', gauge)
    assert gauge.count('PatchVtableSlot(') == 2 and sorted(patched) == ['kArmorUpdateSlot', 'kUpdateSlot'] \
        and 'RedirectCall' not in gauge, f'stockgauge.cpp: the two gauges\' update slots only ({patched})'
    assert 'kUpdateSlot=kGaugeVtable+1*8' in gauge and 'kArmorUpdateSlot=kArmorVtable+1*8' in gauge, 'stockgauge.cpp: slot 1 (update)'
    doc = src('docs/hud-re.md').split('## 9.', 1)[1]
    rvas = set(re.findall(r'\b0x[0-9A-F]{6,7}\b', gauge))
    missing = sorted(r for r in rvas if f'`{r}`' not in doc and f'`{r} ' not in doc and r not in doc)
    assert not missing, f'docs/hud-re.md §9 does not name {missing}'
    hud = src('src/hud.cpp')
    assert 'SetStockGaugeCover(textOk' in hud.split('void HudPublish(', 1)[1].split('\n}\n', 1)[0]
    vhud = src('src/vhud.cpp').split('void StockHudFrame(', 1)[1].split('\n}\n', 1)[0]
    assert 'IsFuelTank(w)' in vhud and 'FuelGauge(v,&r.fuel)' in vhud
    assert 'FuelGauge(v,&r.fuel)' in src('src/playerjet.cpp') and 'FuelGauge(v,&r.fuel)' in src('src/heli.cpp')
    assert 'kWarnFuel' in src('src/warn.h') and 'kWarnFuel' in src('src/warn.cpp') and 'Tx::lowFuel' in hud
    assert 'HUDTEXT(lowFuel,L"LOW FUEL",' in src('src/hudtext.inc')
    assert 'jet_lowfuel' in src('tools/hud_view.cpp')


# ---------------------------------------------------------------- the transaction


def _mods(game: str, rel: str) -> str:
    return os.path.join(game, 'Mods', *rel.split('/'))


def _read(path: str) -> bytes | None:
    if not os.path.isfile(path):
        return None
    with open(path, 'rb') as f:
        return f.read()


@test
def commit_all_or_nothing() -> None:
    game = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        before = {'WEAPON/A.SGO': b'a0', 'WEAPON/B.SGO': b'b0', 'WEAPON/D.SGO': b'd0'}
        for rel, data in before.items():
            modfiles.atomic_write(_mods(game, rel), data)
        changes: dict[str, bytes | None] = {'WEAPON/A.SGO': b'a1', 'WEAPON/B.SGO': b'b1', 'WEAPON/C.SGO': b'c1',
                                            'WEAPON/D.SGO': None}
        real = modfiles.atomic_write
        calls_made = []

        def failing(path: str, data: bytes) -> None:
            calls_made.append(path)
            if path.endswith('C.SGO'):
                raise OSError('disk full (test)')
            real(path, data)

        modfiles.atomic_write = failing
        try:
            cw.commit(game, changes)
        except OSError:
            pass
        else:
            raise AssertionError('the failing write did not fail')
        finally:
            modfiles.atomic_write = real
        for rel, data in before.items():
            assert _read(_mods(game, rel)) == data, f'{rel} not rolled back'
        assert _read(_mods(game, 'WEAPON/C.SGO')) is None
        assert not os.path.exists(_mods(game, cw.JOURNAL))
        # A run killed half way: the journal and its copies are left; the next run rolls it back.
        cw.commit(game, {'WEAPON/A.SGO': b'a2'})
        assert _read(_mods(game, 'WEAPON/A.SGO')) == b'a2'
        os.makedirs(_mods(game, f'{cw.BACKUP}/txn/WEAPON'))
        shutil.copy2(_mods(game, 'WEAPON/A.SGO'), _mods(game, f'{cw.BACKUP}/txn/WEAPON/A.SGO'))
        modfiles.atomic_write(_mods(game, cw.JOURNAL), b'{"WEAPON/A.SGO": true, "WEAPON/E.SGO": false}')
        modfiles.atomic_write(_mods(game, 'WEAPON/A.SGO'), b'half')
        modfiles.atomic_write(_mods(game, 'WEAPON/E.SGO'), b'half')
        assert cw.recover(game)
        assert _read(_mods(game, 'WEAPON/A.SGO')) == b'a2' and _read(_mods(game, 'WEAPON/E.SGO')) is None
        assert not cw.recover(game)
    finally:
        shutil.rmtree(game, ignore_errors=True)


# ---------------------------------------------------------------- the ledger


@test
def ledger_refcounts() -> None:
    game = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        led = ledger.Ledger(game)
        led.put('jets', 'WEAPON/GUN.SGO', b'gun')
        led.put('sub', 'WEAPON/GUN.SGO', b'gun')
        led.put('jets', 'OBJECT/MODEL.MRAB', b'model')
        led.need('testrange', 'OBJECT/MODEL.MRAB')
        led.put('jets', 'OBJECT/MINE.SGO', b'mine')
        deleted, kept = ledger.Ledger(game).release('jets')
        assert [os.path.basename(p) for p in deleted] == ['MINE.SGO'], deleted
        assert os.path.isfile(_mods(game, 'WEAPON/GUN.SGO')) and os.path.isfile(_mods(game, 'OBJECT/MODEL.MRAB'))
        with open(_mods(game, 'OBJECT/MODEL.MRAB'), 'wb') as f:
            f.write(b'changed by someone')
        deleted, kept = ledger.Ledger(game).release('testrange')
        assert not deleted and [os.path.basename(p) for p in kept] == ['MODEL.MRAB']
        deleted, _ = ledger.Ledger(game).release('sub')
        assert [os.path.basename(p) for p in deleted] == ['GUN.SGO']
        assert not os.path.exists(os.path.join(game, 'Mods', ledger.MANIFEST)), 'empty ledger left behind'
        modfiles.atomic_write(_mods(game, 'OBJECT/OLD.SGO'), b'from before the ledger')
        led = ledger.Ledger(game)
        led.need('calls', 'OBJECT/OLD.SGO')
        deleted, _ = led.release('calls', ['OBJECT/OLD.SGO'])
        assert not deleted, 'a file from before the ledger deleted by a tool that only needed it'
        deleted, _ = ledger.Ledger(game).release('jets', ['OBJECT/OLD.SGO'], writer=True)
        assert deleted, 'a file from before the ledger is the one of its writer'
        modfiles.atomic_write(_mods(game, 'OBJECT/OLD2.SGO'), b'from before the ledger')
        deleted, _ = ledger.Ledger(game).release('testrange', ['OBJECT/OLD2.SGO'])
        assert not deleted, 'a file the ledger does not know deleted by a tool that does not write it'
        deleted, _ = ledger.Ledger(game).release('jets', ['OBJECT/OLD2.SGO'], writer=True)
        assert deleted
    finally:
        shutil.rmtree(game, ignore_errors=True)


# ---------------------------------------------------------------- the ini


@test
def ini_merge_only_adds() -> None:
    shipped = src('EDF6VehicleCrew.ini')
    user = '; mine\r\n[VehicleCrew]\r\nEnabled=0\r\n; my note\r\nHeliHeight=40\r\nHeliStandoff=60\r\n\r\n[Other]\r\nX=1\r\n'
    text, added, gone = installer.merge_ini(user, shipped)
    assert 'enabled' not in [a.lower() for a in added] and 'HeliHeight' not in added
    assert gone == ['HeliStandoff']
    assert text.startswith('; mine\r\n[VehicleCrew]\r\nEnabled=0\r\n; my note\r\nHeliHeight=40\r\nHeliStandoff=60\r\n')
    assert text.endswith('\r\n[Other]\r\nX=1\r\n') and '\r\n' in text and '\n' not in text.replace('\r\n', '')
    have = installer._keys(text.splitlines())
    assert set(installer._keys(shipped.splitlines())) <= set(have)
    assert installer.merge_ini(text, shipped)[1] == [], 'merge is not idempotent'
    fresh, added, _ = installer.merge_ini('', shipped)
    assert set(a.lower() for a in added) == set(installer._keys(shipped.splitlines()))


@test
def ini_sections_match_windows() -> None:
    import make_stock_stores
    shipped = '[VehicleCrew]\nEnabled=1\nStockHeliStores=0\nNewOption=2\n'
    for section in ('vehiclecrew', 'VEHICLECREW', 'vEhIcLeCrEw'):
        user = f'[{section}]\nEnabled=0\nStockHeliStores=1\n[Other]\nNewOption=99\n'
        merged, added, gone = installer.merge_ini(user, shipped)
        assert added == ['NewOption'] and not gone, (added, gone)
        assert merged.count('[') == 2 and merged.startswith(user.split('[Other]')[0])
        assert installer.merge_ini(merged, shipped) == (merged, [], [])
        assert make_stock_stores.wanted(merged), 'the installer disabled stores that the plugin enables'
        assert not make_stock_stores.wanted(f'[{section}]\nStockHeliStores=0\n')
        if os.name == 'nt':
            import ctypes
            read_int = ctypes.WinDLL('kernel32', use_last_error=True).GetPrivateProfileIntW
            read_int.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_int, ctypes.c_wchar_p]
            read_int.restype = ctypes.c_uint
            with tempfile.TemporaryDirectory(prefix='edf6vc-ini-') as temp:
                path = os.path.join(temp, 'plugin.ini')
                modfiles.atomic_write(path, merged.encode('utf-8'))
                assert read_int('VehicleCrew', 'Enabled', 99, path) == 0
                assert read_int('VehicleCrew', 'NewOption', 99, path) == 2


# ---------------------------------------------------------------- interrupted runs


@contextlib.contextmanager
def patched(obj: object, **attrs: object) -> Iterator[None]:
    """Sets attributes of `obj` for the duration of the block, then puts the originals back."""
    saved = {name: getattr(obj, name) for name in attrs}
    for name, value in attrs.items():
        setattr(obj, name, value)
    try:
        yield
    finally:
        for name, value in saved.items():
            setattr(obj, name, value)


def _failing_write(suffix: str) -> Callable[[str, bytes], None]:
    """modfiles.atomic_write that runs out of disk on the file whose path ends with `suffix`."""
    real = modfiles.atomic_write

    def write(path: str, data: bytes) -> None:
        if path.replace(os.sep, '/').endswith(suffix):
            raise OSError('disk full (test)')
        real(path, data)
    return write


AT_FILES = {'WEAPON/AT_A.SGO': b'a', 'WEAPON/AT_B.SGO': b'b', 'WEAPON/AT_C.SGO': b'c'}


def _at_build_files(legacy: bool = False) -> dict[str, bytes]:
    return {rel: b'legacy ' + data for rel, data in AT_FILES.items()} if legacy else dict(AT_FILES)


@test
def autoturret_interrupted_install() -> None:
    """build.py install dying after some writes: the manifest still agrees with Mods, so a rerun goes on
    without --force and uninstall removes every file it wrote (the one in flight too)."""
    mods = tempfile.mkdtemp(prefix='edf6at-selftest-')
    try:
        with patched(at_build, build_files=_at_build_files, _refuse_while_running=lambda mods: None):
            with patched(modfiles, atomic_write=_failing_write('AT_B.SGO')):
                try:
                    at_build.install(mods, text=False, force=False)
                except OSError:
                    pass
                else:
                    raise AssertionError('the failing write did not fail')
            # Killed right after the write of AT_B landed, before its sha was saved.
            # Emulate the older manifest too: it recorded only sha=None, without write-ahead fingerprints.
            manifest = at_build._load_manifest(mods)
            for entry in manifest['files'].values():
                entry.pop('pending_sha', None)
            modfiles.save_json(at_build._manifest_path(mods), manifest)
            modfiles.atomic_write(os.path.join(mods, 'WEAPON', 'AT_B.SGO'), b'b')
            at_build.install(mods, text=False, force=False)   # refused before: "changed since this tool wrote it"
            for rel, data in AT_FILES.items():
                assert _read(os.path.join(mods, *rel.split('/'))) == data, rel
            # The same death, then uninstall instead of a rerun.
            shutil.rmtree(mods)
            with patched(modfiles, atomic_write=_failing_write('AT_B.SGO')):
                try:
                    at_build.install(mods, text=False, force=False)
                except OSError:
                    pass
            manifest = at_build._load_manifest(mods)
            for entry in manifest['files'].values():
                entry.pop('pending_sha', None)
            modfiles.save_json(at_build._manifest_path(mods), manifest)
            modfiles.atomic_write(os.path.join(mods, 'WEAPON', 'AT_B.SGO'), b'b')
            at_build.uninstall(mods, force=False)
            left = [rel for rel in AT_FILES if os.path.exists(os.path.join(mods, *rel.split('/')))]
            assert not left, f'uninstall left our own files in Mods: {left}'
            assert not os.path.exists(os.path.join(mods, at_build.MANIFEST))
    finally:
        shutil.rmtree(mods, ignore_errors=True)


@test
def autoturret_interrupted_upgrade() -> None:
    """Every whole-file upgrade cut point remains owned, even under a later builder version."""
    upgraded = {rel: b'new ' + data for rel, data in AT_FILES.items()}
    later = {rel: b'later ' + data for rel, data in AT_FILES.items()}
    for cut in ('before_file', 'after_file', 'after_manifest'):
        for rel in AT_FILES:
            for action in ('retry', 'uninstall'):
                with tempfile.TemporaryDirectory(prefix='edf6at-upgrade-') as mods:
                    path = os.path.join(mods, *rel.split('/'))
                    # This file belongs to another mod before the first, explicitly forced install.
                    modfiles.atomic_write(path, b'original mod')
                    with patched(at_build, build_files=lambda legacy=False: dict(AT_FILES),
                                 _refuse_while_running=lambda mods: None), \
                         patched(at_build.describe, table_ids=lambda mods: []):
                        at_build.install(mods, text=False, force=True)
                        write = modfiles.atomic_write
                        save = modfiles.save_json
                        fired = False

                        def fail_file(dst: str, data: bytes) -> None:
                            nonlocal fired
                            if dst == path and cut != 'after_manifest':
                                fired = True
                                if cut == 'after_file':
                                    write(dst, data)
                                raise OSError('interrupted upgrade (test)')
                            write(dst, data)

                        def fail_manifest(dst: str, value: dict) -> None:
                            nonlocal fired
                            save(dst, value)
                            if cut == 'after_manifest' and value['files'][rel]['sha'] == modfiles.sha256(upgraded[rel]):
                                fired = True
                                raise OSError('interrupted after manifest (test)')

                        with patched(at_build, build_files=lambda legacy=False: dict(upgraded)):
                            with patched(modfiles, atomic_write=fail_file, save_json=fail_manifest):
                                try:
                                    at_build.install(mods, text=False, force=False)
                                except OSError:
                                    pass
                                else:
                                    raise AssertionError('the interrupted upgrade did not fail')
                        assert fired, (cut, rel)
                        # A newer package must not need the failed version's generator to recover ownership.
                        with patched(at_build, build_files=lambda legacy=False: dict(later)):
                            if action == 'retry':
                                at_build.install(mods, text=False, force=False)
                                for name, data in later.items():
                                    assert _read(os.path.join(mods, *name.split('/'))) == data
                                assert at_build.check(mods), 'completed retry still reported as pending'
                            at_build.uninstall(mods, force=False)
                        assert _read(path) == b'original mod', 'the first backup was not restored'
                        assert not os.path.exists(os.path.join(mods, at_build.MANIFEST))
                        for name in AT_FILES:
                            if name != rel:
                                assert not os.path.exists(os.path.join(mods, *name.split('/')))


def _sgo_table(key: str, ids: list[str]) -> bytes:
    """A minimal weapon table (key 'table') or text table (key 'text_table'): one row per id."""
    return dsgo.compact(dsgo.Document(dsgo.Node([dsgo.Node([dsgo.Node([i]) for i in ids])], {0: key}), []))


@test
def autoturret_interrupted_text_upgrade() -> None:
    """An unwritten text upgrade still owns its old rows; unrelated and subsequently edited rows survive."""
    from types import SimpleNamespace
    rel = 'WEAPON/WEAPONTEXT.EN.SGO'
    stock = _sgo_table('text_table', ['stock', 'other stock'])
    for cut in ('before_file', 'after_file'):
        for foreign in (False, True):
            with tempfile.TemporaryDirectory(prefix='edf6at-text-') as mods:
                path = os.path.join(mods, *rel.split('/'))
                original = _sgo_table('text_table', ['original mod', 'unrelated mod'])
                modfiles.atomic_write(path, original)

                def texts(version: str) -> at_build.describe.Texts:
                    doc = dsgo.parse(modfiles.read(path))
                    rows = doc.root.get('text_table').items
                    before = rows[0]
                    rows[0] = dsgo.Node([version])
                    return at_build.describe.Texts({rel: dsgo.compact(doc)}, {rel: {'A': (before, rows[0])}})

                with patched(at_build, build_files=lambda legacy=False: dict(AT_FILES),
                             build_texts=lambda files, mods: texts('old'), _refuse_while_running=lambda mods: None), \
                     patched(at_build.describe, table_ids=lambda mods: ['A', 'B']), \
                     patched(at_build.rootcpk, default=lambda: SimpleNamespace(read=lambda folder, name: stock)):
                    at_build.install(mods, text=True, force=False)
                    write = modfiles.atomic_write
                    fired = False

                    def fail(dst: str, data: bytes) -> None:
                        nonlocal fired
                        if dst == path:
                            fired = True
                            if cut == 'after_file':
                                write(dst, data)
                            raise OSError('interrupted text update (test)')
                        write(dst, data)

                    with patched(at_build, build_texts=lambda files, mods: texts('new')), \
                         patched(modfiles, atomic_write=fail):
                        try:
                            at_build.install(mods, text=True, force=False)
                        except OSError:
                            pass
                        else:
                            raise AssertionError('the text update did not fail')
                    assert fired
                    if foreign:
                        modfiles.atomic_write(path, _sgo_table('text_table', ['later mod', 'unrelated mod']))
                    at_build.uninstall(mods, force=False)
                    rows = dsgo.parse(modfiles.read(path)).root.get('text_table').items
                    assert dsgo.dump(rows[0]) == dsgo.dump(dsgo.Node(['later mod' if foreign else 'original mod']))
                    assert dsgo.dump(rows[1]) == dsgo.dump(dsgo.Node(['unrelated mod']))
                    assert os.path.exists(os.path.join(mods, at_build.MANIFEST)) == foreign


@test
def autoturret_pending_files_protect_foreign_edits() -> None:
    with tempfile.TemporaryDirectory(prefix='edf6at-pending-') as mods:
        with patched(at_build, build_files=_at_build_files, _refuse_while_running=lambda mods: None), \
             patched(at_build.describe, table_ids=lambda mods: []):
            with patched(modfiles, atomic_write=_failing_write('AT_A.SGO')):
                try:
                    at_build.install(mods, text=False, force=False)
                except OSError:
                    pass
                else:
                    raise AssertionError('expected a failed first write')
            assert not at_build.check(mods), 'missing pending files were reported as installed'
            path = os.path.join(mods, 'WEAPON', 'AT_A.SGO')
            modfiles.atomic_write(path, b'other mod after interruption')
            try:
                at_build.install(mods, text=False, force=False)
            except SystemExit:
                pass
            else:
                raise AssertionError('a foreign edit was overwritten')
            at_build.uninstall(mods, force=False)
            assert modfiles.read(path) == b'other mod after interruption'
            assert os.path.isfile(os.path.join(mods, at_build.MANIFEST))


def _sazabi_request_template() -> bytes:
    """A stock Eros request with distinguishable setup/resource values for the fallback contract."""
    n = dsgo.Node
    weapons = n([n([f'app:/weapon/v_506heli_gatling01_{side}.sgo', n([0.01, 0.1])]) for side in ('l', 'r')]
                + [n(['app:/weapon/v_506heli_missile01.sgo', n([0.01, 0.1])]), n(['app:/weapon/v_fuel01.sgo'])])
    setup = n([n([1.3, 1.4]), n([0.002, 0.0003]), n([100.0, 1.0]), weapons])
    vehicle = 'app:/object/v506_heli.sgo'
    root = n([n([5.0]), n([0.0, 0.0, 0.0, 0.0, n(['transport', 'box', vehicle, setup, 'voice'])]),
              n([vehicle] + [w.items[0] for w in weapons.items]), 'Eros'],
             {0: 'ReloadTime', 1: 'Ammo_CustomParameter', 2: 'resource', 3: 'name.en'})
    return dsgo.write(dsgo.Document(root, []))


def _call_files(game: str, table_ids: list[str]) -> dict[str, bytes]:
    """What call_weapons.stack would give (shape only), and the jets the vehicle requests need."""
    for c in calls.CALLS:
        if c.vehicle:
            for rel in cw.vehicle_needs(c):
                modfiles.atomic_write(_mods(game, rel), b'jet')
    files = {cw.TABLE: _sgo_table('table', table_ids)}
    files.update({rel: _sgo_table('text_table', table_ids) for rel in cw.TEXTS})
    files.update({cw.sgo_file(c): c.id.encode() for c in calls.CALLS})
    for c in calls.CALLS:
        if c.jet == vc.SAZABI_JET:
            files[cw.sgo_file(c)] = cw.vehicle_sgo(_sazabi_request_template(), c, (2.0, 3.0))
    return files


@test
def sazabi_fallback_install_upgrade() -> None:
    """Clean no-model install, then model install/removal, retain the request row and actual dependencies."""
    import make_sazabi
    import sazabi_model
    import sgo
    call = next(c for c in calls.CALLS if c.jet == vc.SAZABI_JET)
    template = _sazabi_request_template()
    original = dsgo.parse(template).root.get('Ammo_CustomParameter').items[4].items[3]
    stock_object = sgo.write(0x102, {'animation_model': [['app:/object/v506_heli.mrab', 'v506_heli.mdb']],
                                  'game_object_durability': 1000.0})
    class Game:
        def read(self, folder: str, name: str) -> bytes:
            assert (folder, name) == ('OBJECT', 'V506_HELI.SGO')
            return stock_object

    with tempfile.TemporaryDirectory(prefix='edf6vc-sazabi-') as game, \
            patched(modfiles, game_running=lambda process=modfiles.PROCESS: False), \
            patched(sazabi_model, model_dir=lambda: None), patched(vc, Game=lambda root: Game()):
        import ported_weapons as pw   # every install has a row per EDF5 weapon: here they wait for EDF5
        files = _call_files(game, STOCK + list(calls.IDS) + [pw.retired_id(x) for x in pw.IDS])
        arms = {f'WEAPON/{w.split("/")[-1].upper()}' for w in vc.SAZABI_WEAPONS}
        for rel in arms:
            os.remove(_mods(game, rel))  # clean CI package: no Sazabi weapons have ever been installed
        fallback = make_sazabi.build(game)
        assert fallback == {cw.vehicle_file(call): stock_object}
        make_sazabi.install(game, fallback)
        request = cw.vehicle_sgo(template, call, (2.0, 3.0), fallback=True)
        r = dsgo.parse(request).root
        assert dsgo.dump(r.get('Ammo_CustomParameter').items[4].items[3]) == dsgo.dump(original)
        assert r.get('Ammo_CustomParameter').items[4].items[2] == cw._object_path(call)
        assert not any('edf6vc_sz_' in p for p in r.get('resource').items)
        assert cw.vehicle_needs(call, request) == [cw.vehicle_file(call)]
        files[cw.sgo_file(call)] = request
        cw.install(game, files)
        row = cw.load_manifest(game)['rows'][call.id]
        assert cw.check(game)
        # The installed request is authoritative even if today's model folder differs.
        with patched(sazabi_model, model_dir=lambda: 'new model folder'):
            assert cw.check(game)
        generated = {cw.vehicle_file(call): b'model vehicle', **{rel: b'weapon' for rel in arms}}
        make_sazabi.install(game, generated)
        files[cw.sgo_file(call)] = cw.vehicle_sgo(template, call, (2.0, 3.0))
        cw.install(game, files)
        assert cw.load_manifest(game)['rows'][call.id] == row
        assert {ledger.key(p) for p in arms} <= set(ledger.Ledger(game).owned_by(cw.OWNER))
        make_sazabi.install(game, fallback)
        files[cw.sgo_file(call)] = request
        cw.install(game, files)
        assert cw.load_manifest(game)['rows'][call.id] == row
        assert not any(os.path.exists(_mods(game, p)) for p in arms)
        assert cw.check(game)


@test
def sazabi_range_shared_assets() -> None:
    """Standalone Sazabi ranges own every dependency; either writer can go while the other still uses it."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    import make_sazabi
    import sazabi_model
    weapons = {vc.SAZABI_RIFLE_FILE: b'rifle', vc.SAZABI_MISSILE_FILE: b'missile'}
    rounds = {f: f.encode() for f in vc.SAZABI_ROUND_FILES}
    shared = {f'WEAPON/{n}': d for n, d in weapons.items()} | {f'OBJECT/{n}': d for n, d in rounds.items()}
    shared[f'OBJECT/{sazabi_model.OUT_ARC}'] = b'model'
    mission = f'OBJECT/{vc.SAZABI_JET.upper()}.SGO'
    install_files = {**shared, f'OBJECT/{make_sazabi.SGO_FILE}': b'requested object'}
    with tempfile.TemporaryDirectory(prefix='edf6vc-sazabi-range-') as game, \
            patched(gen, jet_guns=lambda game: {}, vehicle_sgo=lambda *args: b'mission object'), \
            patched(vc, sazabi_weapons=lambda game: weapons, sazabi_rounds=lambda game: rounds), \
            patched(sazabi_model, model_dir=lambda: 'model folder', build_archive=lambda *args: (b'model', {})):
        gen._write_derived(game, object(), {vc.SAZABI_JET})
        assert set(ledger.Ledger(game).owned_by(gen.OWNER)) == {ledger.key(n) for n in [*shared, mission]}
        for n, data in shared.items():
            assert _read(_mods(game, n)) == data
        make_sazabi.install(game, install_files)
        make_sazabi.remove(game)
        assert all(_read(_mods(game, n)) == data for n, data in shared.items())
        make_sazabi.install(game, install_files)
        gen._write_derived(game, object(), set())
        assert not os.path.exists(_mods(game, mission))
        assert all(_read(_mods(game, n)) == data for n, data in shared.items())
        make_sazabi.remove(game)
        assert not any(os.path.exists(_mods(game, n)) for n in shared)


@test
def calls_failed_install_records_nothing() -> None:
    """A call_weapons.install whose transaction rolled back must not keep its first-backup records: a later
    install over another mod's table backs that table up, and repair puts it back instead of deleting it."""
    game = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        with patched(modfiles, game_running=lambda process=modfiles.PROCESS: False):
            files = _call_files(game, STOCK + list(calls.IDS))
            with patched(modfiles, atomic_write=_failing_write(cw.sgo_file(calls.CALLS[-1]))):
                try:
                    cw.install(game, files)
                except OSError:
                    pass
                else:
                    raise AssertionError('the failing write did not fail')
            assert _read(_mods(game, cw.TABLE)) is None, 'not rolled back'
            assert cw.TABLE not in cw.load_manifest(game)['created'], 'a rolled back install recorded created'
            other = _sgo_table('table', STOCK + OTHER)
            modfiles.atomic_write(_mods(game, cw.TABLE), other)   # another mod's table, installed since
            cw.install(game, _call_files(game, STOCK + OTHER + list(calls.IDS)))
            cw.repair(game)
            assert _read(_mods(game, cw.TABLE)) == other, "repair deleted the other mod's table"
    finally:
        shutil.rmtree(game, ignore_errors=True)


@test
def uninstall_misaligned_skips_table() -> None:
    """installer.uninstall over a table whose texts do not line up: no traceback; the player can skip the
    table and still get the plugin removed."""
    game = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        modfiles.atomic_write(_mods(game, cw.TABLE), _sgo_table('table', STOCK + list(calls.IDS)))
        for rel in cw.TEXTS:
            modfiles.atomic_write(_mods(game, rel), _sgo_table('text_table', STOCK))
        dll = _mods(game, f'Plugins/{installer.PLUGIN}.dll')
        modfiles.atomic_write(dll, b'dll')
        answers = iter(['1', 's'])
        with patched(modfiles, game_running=lambda process=modfiles.PROCESS: False), \
                patched(installer, ask=lambda prompt: next(answers)):
            installer.uninstall(game)
        assert not os.path.exists(dll), 'the plugin was not removed'
        assert _read(_mods(game, cw.TABLE)) is not None, 'skipping the table changed it'
        # Installed by us, then misaligned: the repair install offers is offered here too.
        shutil.rmtree(game)
        answers = iter(['1', 'y'])
        with patched(modfiles, game_running=lambda process=modfiles.PROCESS: False), \
                patched(installer, ask=lambda prompt: next(answers)):
            cw.install(game, _call_files(game, STOCK + list(calls.IDS)))
            for rel in cw.TEXTS:
                modfiles.atomic_write(_mods(game, rel), _sgo_table('text_table', STOCK))
            modfiles.atomic_write(dll, b'dll')
            installer.uninstall(game)
        assert not os.path.exists(dll), 'the plugin was not removed'
        assert all(_read(_mods(game, rel)) is None for rel in cw.SHARED), 'repair did not put back the stock tables'
        assert not os.path.exists(_mods(game, cw.MANIFEST))
    finally:
        shutil.rmtree(game, ignore_errors=True)


# ---------------------------------------------------------------- the pack: everything built is shipped, installed, removed


def _cmake_plugins() -> dict[str, str]:
    """Every plugin DLL a CMakeLists.txt builds (add_library SHARED) -> the ini it copies beside it into
    build/Mods/Plugins (configure_file), as a path in the repository."""
    out: dict[str, str] = {}
    for top, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in ('build', 'release', '.git', 'models', 'node_modules')]
        if 'CMakeLists.txt' not in files:
            continue
        text = src(os.path.relpath(os.path.join(top, 'CMakeLists.txt'), ROOT).replace(os.sep, '/'))
        for name in re.findall(r'add_library\(\s*(\w+)\s+SHARED\b', text):
            ini = re.search(r'configure_file\(\s*(\S+\.ini)\s+"\$\{CMAKE_BINARY_DIR\}/Mods/Plugins/' + name + r'\.ini"', text)
            assert ini, f'{name}: CMake copies no {name}.ini into build/Mods/Plugins'
            out[name] = os.path.join(top, ini.group(1))
    return out


@test
def pack_ships_every_plugin() -> None:
    """Every plugin CMake builds is in installer.PLUGINS (install writes it, uninstall removes it, check compares it)
    with its ini's own section, and tools/build_release.py bundles its dll and ini. 0.8.0 shipped EDF6VehicleCrew
    alone: the player's EDF6AutoTurret stayed the old build, which fought the turret camera for the Kepler."""
    import build_release
    built = _cmake_plugins()
    assert {'EDF6VehicleCrew', 'EDF6AutoTurret'} <= set(built), f'the scan reads every CMakeLists.txt: {built}'
    sections = dict(installer.PLUGINS)
    missing = sorted(set(built) - set(sections))
    assert not missing, f'installer.PLUGINS lacks {missing}: built by CMake but never shipped or installed'
    for name, ini in built.items():
        with open(ini, encoding='utf-8-sig') as f:
            first = re.search(r'^\s*\[([^\]]+)\]', f.read(), re.M)
        assert first and first.group(1).lower() == sections[name].lower(), \
            f'{name}: installer.PLUGINS section {sections[name]}, the ini says {first and first.group(1)}'
    unbundled = sorted({n + e for n in built for e in installer.PLUGIN_FILES} - set(build_release.plugin_data()))
    assert not unbundled, f'tools/build_release.py does not bundle {unbundled}'


def _function(text: str, name: str) -> str:
    return text.split(f'\ndef {name}(', 1)[1].split('\ndef ', 1)[0]


@test
def installer_removes_what_it_writes() -> None:
    """Every writer tools/installer.py install() calls has its remover in uninstall(): X.install( -> X.remove / X.uninstall,
    install_Y( -> remove_Y(; the call weapons go through retire_weapons (placeholders keep their rows)."""
    inst = src('tools/installer.py')
    body, undo = _function(inst, 'install'), _function(inst, 'uninstall')
    modules = set(re.findall(r'\b(\w+)\.install(?:_\w+)?\(', body))
    helpers = set(re.findall(r'(?<![\w.])install_(\w+)\(', body))
    assert {'make_jets', 'gen', 'call_weapons'} <= modules and {'plugin', 'autoturret'} <= helpers, (modules, helpers)
    lacking = sorted(m for m in modules - {'call_weapons'} if f'{m}.remove' not in undo and f'{m}.uninstall(' not in undo)
    lacking += sorted(f'install_{h}' for h in helpers if f'remove_{h}(' not in undo)
    lacking += [] if 'retire_weapons(' in undo else ['call_weapons']
    assert not lacking, f'installer.uninstall never undoes {lacking}'


def _pack_bundle(folder: str, version: bytes, extra: dict[str, str] | None = None) -> None:
    """A stand-in build/Mods/Plugins: each plugin's DLL (`version` bytes) and its real shipped ini (+ `extra` lines)."""
    for name, ini in _cmake_plugins().items():
        modfiles.atomic_write(os.path.join(folder, name + '.dll'), version + name.encode())
        with open(ini, 'rb') as f:
            text = f.read()
        modfiles.atomic_write(os.path.join(folder, name + '.ini'), text + (extra or {}).get(name, '').encode())


def _tree(game: str) -> dict[str, bytes]:
    out = {}
    for top, _, files in os.walk(os.path.join(game, 'Mods')):
        for f in files:
            path = os.path.join(top, f)
            out[os.path.relpath(path, game).replace(os.sep, '/')] = _read(path)
    return out


@test
def pack_install_upgrade_uninstall() -> None:
    """The whole pack round trip on a stand-in game (generators stubbed; their install / remove, the ledger, the
    AutoTurret manifest and backups, the plugins and ini merge real): an old EDF6AutoTurret (DLL, ini with the
    player's settings, data from build.py) is upgraded to the pack's, another mod's file it replaces is backed up
    after asking, check sees a stale DLL, a second install keeps the player's settings and adds only new keys, and
    uninstall 1 leaves Mods exactly as other mods left it."""
    import buildcache
    import call_weapons
    import describe
    import gen
    import importlib
    import rootcpk
    game = tempfile.mkdtemp(prefix='edf6vc-pack-')
    bundle = os.path.join(game, 'bundle')
    try:
        for name, data in (('Root.cpk', b'root'), ('Chunk02.cpk', b'map'), ('EDF6.exe', b'exe')):
            modfiles.atomic_write(os.path.join(game, name), data)
        foreign = {'Mods/Plugins/EDF6ClearLoot.dll': b'another mod', 'Mods/WEAPON/OTHER.SGO': b'another mod',
                   'Mods/WEAPON/AT_C.SGO': b'another mod at a path AutoTurret writes'}
        for rel, data in foreign.items():
            modfiles.atomic_write(os.path.join(game, *rel.split('/')), data)
        at_ini = _cmake_plugins()['EDF6AutoTurret']
        with open(at_ini, encoding='utf-8') as f:
            shipped_at = f.read()
        assert 'Gain=3.0\n' in shipped_at and 'BurstVisualScale=' in shipped_at
        old_ini = shipped_at.replace('Gain=3.0\n', 'Gain=7.5\n').replace('BurstVisualScale=2.5\n', '')
        plugins = os.path.join(game, 'Mods', 'Plugins')
        modfiles.atomic_write(os.path.join(plugins, 'EDF6AutoTurret.dll'), b'old autoturret, EML6_Load only')
        modfiles.atomic_write(os.path.join(plugins, 'EDF6AutoTurret.ini'), old_ini.encode())
        modfiles.atomic_write(os.path.join(plugins, 'EDF6VehicleCrew.log.1'), b'rotated log')
        mods = os.path.join(game, 'Mods')
        old_data = {rel: b'old ' + data for rel, data in AT_FILES.items() if rel != 'WEAPON/AT_C.SGO'}
        texts = describe.Texts({}, {})
        text_modes: list[bool] = []

        def build_texts_stub(files, mods, *, proteus: bool = False):
            text_modes.append(proteus)
            return texts

        answers: list[str] = []
        with contextlib.ExitStack() as stack:
            enter = stack.enter_context
            enter(patched(at_build, build_files=lambda legacy=False: old_data, _refuse_while_running=lambda mods: None,
                          build_texts=build_texts_stub))
            with contextlib.redirect_stdout(io.StringIO()):
                at_build.install(mods, text=True, force=False)   # the player's earlier build.py install
            enter(patched(at_build, build_files=_at_build_files))
            enter(patched(describe, table_ids=lambda mods: []))
            enter(patched(installer, bundle_dir=lambda: bundle, check_loader=lambda game: None,
                          stack_weapons=lambda game: {}, retire_weapons=lambda game: True,
                          ask=lambda prompt: answers.pop(0)))
            enter(patched(call_weapons, recover=lambda game: False, install=lambda game, files: {},
                          check=lambda game: True))
            enter(patched(buildcache, recipes=lambda: {g: 'recipe' for g in buildcache.GROUPS}))
            enter(patched(rootcpk, use=lambda root: None))   # its readers are stubbed; DEFAULT_GAME stays

            def mission(game: str, plan: object) -> list[str]:
                out = gen.mission_dir(game, gen.RANGE_MISSION)
                modfiles.atomic_write(os.path.join(out, gen.MARKER), b'range')
                modfiles.atomic_write(os.path.join(out, 'MISSION.AC'), b'script')
                modfiles.atomic_write(os.path.join(out, 'MISSION.RMPA'), b'points')
                modfiles.atomic_write(os.path.join(out, 'MISSION.JSON'), b'{}')
                ledger.Ledger(game).put(gen.OWNER, 'OBJECT/EDF6TR_FAKE.SGO', b'range object')
                return []
            enter(patched(gen, install=mission, grand_battle=lambda plan: plan))
            enter(patched(e5c, build=lambda game, campaign=True, test_range=True: _e5c_stub()))   # its own install / remove / check run for real
            enter(patched(e5c.modfiles, refuse_while_running=lambda *a, **k: None))   # the real game may be open
            for group in buildcache.GROUPS:
                made = (b'mac', {'FAKE_PIECE.MAC': b'piece'}) if group == 'bigmap' else \
                    {f'OBJECT/EDF6VC_FAKE_{group.upper()}.SGO': group.encode()}
                enter(patched(importlib.import_module('make_' + group), build=lambda game, made=made: made))
            # Geometry/SGO parsing are generator boundaries; scoped optic journals/install/remove stay real.
            import make_optics
            enter(patched(make_optics, build_models=lambda game: {'OBJECT/EDF6VC_OPTIC_FAKE.MRAB': b'optic'},
                          build_stock_redirects=lambda game: {}, redirect=lambda data: (data, ())))
            # the stock vehicles' stores (on by default): a vehicle and a request, gone again with the uninstall
            stores = {'OBJECT/EDF6VC_FAKE_STORES.SGO': b'stores', 'WEAPON/FAKE_STORES_REQUEST.SGO': b'request'}
            enter(patched(importlib.import_module('make_stock_stores'),
                          build=lambda game, overlay=None: (dict(stores), [], {}),
                          store_files=lambda: []))   # the fake make_jets writes no store weapons to need
            # the out-of-game loadouts' files (tools/support_loadout.py): written after the plugin's ini, gone with uninstall 1
            loadout = {'OBJECT/EDF6VC_LO_TANK_4000000000000C81.SGO': b'loaded tank'}
            built_from: list[str] = []
            enter(patched(importlib.import_module('support_loadout'),
                          build=lambda game, text, pending=(), errors=None: (built_from.append(text), dict(loadout))[1]))
            with contextlib.redirect_stdout(io.StringIO()):
                _pack_bundle(bundle, b'v1 ')
                answers[:] = ['y']   # AT_C: back up the other mod's file and replace it
                installer.install(game, campaign_requested=True)
                assert e5c.enabled(game), 'explicit campaign opt-in was not installed'
                assert not answers, 'install did not ask before replacing the other mod\'s AT_C'
                for name in _cmake_plugins():
                    assert _read(os.path.join(plugins, name + '.dll')) == b'v1 ' + name.encode(), f'{name}.dll not installed'
                for rel, data in AT_FILES.items():
                    assert _read(os.path.join(mods, *rel.split('/'))) == data, f'AutoTurret data {rel} not installed'
                at_text = _read(os.path.join(plugins, 'EDF6AutoTurret.ini')).decode()
                assert 'Gain=7.5' in at_text and 'Gain=3.0' not in at_text, 'the player\'s AutoTurret setting lost'
                assert 'BurstVisualScale=2.5' in at_text and at_text.count('[AutoTurret]') == 1, 'missing key not added'
                assert installer.check(game), 'check fails right after install'
                for rel, data in stores.items():
                    assert _read(os.path.join(mods, *rel.split('/'))) == data, f'stores file {rel} not installed'
                for rel, data in loadout.items():
                    assert _read(os.path.join(mods, *rel.split('/'))) == data, f'loadout file {rel} not installed'
                assert built_from and '[VehicleCrew]' in built_from[-1], 'the loadouts are built from the installed ini'
                modfiles.atomic_write(os.path.join(plugins, 'EDF6AutoTurret.dll'), b'old autoturret, EML6_Load only')
                assert not installer.check(game), 'check passes an old EDF6AutoTurret.dll'
                # The upgrade: new DLLs, a setting the new ini adds.
                _pack_bundle(bundle, b'v2 ', {'EDF6AutoTurret': '\r\n; new in v2\r\nNewTurretKey=5\r\n'})
                installer.install(game)
                assert e5c.enabled(game), 'ordinary update lost the campaign opt-in'
                assert _read(os.path.join(plugins, 'EDF6AutoTurret.dll')) == b'v2 EDF6AutoTurret'
                at_text = _read(os.path.join(plugins, 'EDF6AutoTurret.ini')).decode()
                assert 'Gain=7.5' in at_text and 'NewTurretKey=5' in at_text and installer.ADDED_HEADER in at_text
                assert at_text.count('BurstVisualScale=') == 1 and at_text.count('[AutoTurret]') == 1
                assert installer.check(game)
                for name in ('.log', '.log.1'):
                    modfiles.atomic_write(os.path.join(plugins, 'EDF6AutoTurret' + name), b'log')
                answers[:] = ['1']
                installer.uninstall(game)
        assert text_modes and not text_modes[0] and any(text_modes[1:]), \
            'the old standalone install has no Proteus text; pack installs must request it explicitly'
        left = _tree(game)
        assert left == foreign, f'uninstall left {sorted(set(left) - set(foreign))}, changed ' \
            f'{sorted(r for r in foreign if left.get(r) != foreign[r])}'
    finally:
        shutil.rmtree(game, ignore_errors=True)


# ---------------------------------------------------------------- model import (pylib/obj_model.py, pylib/texfile.py)

def _cube_part(skip_face: int | None = None) -> 'object':
    """A unit cube centred on x = 0 as an obj_model.Part, outward faces (counter-clockwise), face 0 the +x one."""
    import obj_model as om
    c = [(x, y, z) for x in (-0.5, 0.5) for y in (0.0, 1.0) for z in (0.0, 1.0)]
    quads = [(4, 6, 7, 5), (0, 1, 3, 2), (2, 3, 7, 6), (0, 4, 5, 1), (1, 5, 7, 3), (0, 2, 6, 4)]
    verts, tris = [], []
    for k, q in enumerate(quads):
        if k == skip_face:
            continue
        n = om.norm(om.cross(om.sub(c[q[1]], c[q[0]]), om.sub(c[q[2]], c[q[0]])))
        base = len(verts)
        verts += [om.Vertex(c[i], n, (j % 2 * 1.0, j // 2 * 1.0)) for j, i in enumerate(q)]
        tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return om.Part('cube', 'm', verts, tris)


@test
def obj_reader_triangulates_and_converts() -> None:
    import obj_model as om
    d = tempfile.mkdtemp()
    try:
        with open(os.path.join(d, 'm.mtl'), 'w', encoding='utf-8') as h:
            h.write('newmtl a\nmap_Kd C:/elsewhere/Tex.PNG\n')
        with open(os.path.join(d, 'tex.png'), 'wb') as h:
            h.write(b'')
        with open(os.path.join(d, 'm.obj'), 'w', encoding='utf-8') as h:
            h.write('mtllib m.mtl\no box\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0.5 1.5 0\nvt 0 0\nvt 1 0\nvt 1 1\n'
                    'vn 0 0 1\nusemtl a\nf -5/1/1 -4/2/1 -3/3/1 -1/3/1 -2/3/1\nf 1//1 2//1 3//1\n')
        obj = om.read_obj(os.path.join(d, 'm.obj'))
        assert [o.name for o in obj.objects] == ['box'] and len(obj.objects[0].faces) == 2
        assert om.texture_path(obj, 'a') == os.path.join(d, 'tex.png'), 'map_Kd found by its base name'
        (p,) = om.obj_parts(obj)
        assert len(p.tris) == 3 + 1, p.tris                              # a pentagon: 3 triangles, + 1
        for t in p.tris:     # winding kept: every triangle faces the normal
            a, b, c = (p.verts[i].pos for i in t)
            assert om.dot(om.cross(om.sub(b, a), om.sub(c, a)), (0.0, 0.0, 1.0)) > 0
        assert p.verts[0].uv == (0.0, 1.0), 'v flipped to the top-left origin'
        (m,) = om.obj_parts(obj, om.Conversion(axes=((-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))))
        for t in m.tris:     # a mirroring conversion reverses the winding: still facing its (mirrored) normal
            a, b, c = (m.verts[i].pos for i in t)
            assert om.dot(om.cross(om.sub(b, a), om.sub(c, a)), m.verts[t[0]].normal) > 0
    finally:
        shutil.rmtree(d, ignore_errors=True)


@test
def obj_holes_found_and_closed() -> None:
    import obj_model as om
    assert om.boundary_loops(_cube_part()) == [], 'a closed cube has no hole'
    part = _cube_part(skip_face=0)
    loops = om.boundary_loops(part)
    assert len(loops) == 1 and len(loops[0]) == 4 and om.mirror_unmatched(part, loops) == loops
    fill = om.fill_loop(part, loops[0])
    assert len(fill.tris) == 2 and om.boundary_loops(part) == []
    part = _cube_part(skip_face=0)
    full = _cube_part()
    ref = [tuple(om.RefCorner(full.verts[i].pos, full.verts[i].normal, ((0, 1.0),)) for i in t) for t in full.tris]
    fills = om.restore_from_reference(part, ref)  # type: ignore[arg-type]
    assert len(fills) == 1 and len(fills[0].tris) == 2 and om.boundary_loops(part) == [], fills
    skins = om.skins_by_reference(part, ref, lambda b: b + 7)  # type: ignore[arg-type]
    assert all(s == ((7, 1.0),) for s in skins)
    halves = om.split_part(full, lambda t: max(p[0] for p in t) > 0)
    assert len(halves[True].tris) == 2 * 5 and len(halves[False].tris) == 2
    assert len(om.components(om.merge([full, _cube_part()]))) == 1, 'coincident copies weld into one piece'


@test
def obj_meshes_split_below_65536() -> None:
    import struct as st
    import obj_model as om
    from mdb import Mesh, VElem, read_elem
    layout = [VElem(7, 0, 0, 'BINORMAL'), VElem(7, 8, 0, 'TANGENT'), VElem(7, 16, 0, 'NORMAL'), VElem(7, 24, 0, 'POSITION'),
              VElem(12, 32, 0, 'TEXCOORD'), VElem(1, 40, 0, 'BLENDWEIGHT'), VElem(21, 56, 0, 'BLENDINDICES')]
    template = Mesh(bytes(4), 0, 0, 60, layout, 0, b'', b'')
    cube = _cube_part()
    # 67200 vertices, the cubes 2 m apart in a block (in a row they reach 5.6 km, where half floats step 4 m: the far
    # ones drawn as points, which build_meshes leaves out)
    big = om.merge([om.Part('c', 'm', [om.Vertex(om.add(v.pos, (k % 14 * 2.0, k // 14 % 14 * 2.0, k // 196 * 2.0)), v.normal, v.uv)
                                       for v in cube.verts], cube.tris) for k in range(2800)])
    meshes = om.build_meshes(template, [(big, om.rigid(big, 3))], material=2)
    assert len(meshes) == 2 and all(me.nverts < 0x10000 and me.material == 2 for me in meshes)
    assert sum(len(me.indices) // 6 for me in meshes) == len(big.tris)
    me = meshes[0]
    assert me.flags == bytes((0, 1, 1, 0)) and {int(r[0]) for r in read_elem(me, 'BLENDINDICES')} == {3}
    for n, t, b in zip(read_elem(me, 'NORMAL'), read_elem(me, 'TANGENT'), read_elem(me, 'BINORMAL')):
        assert abs(om.dot(n[:3], t[:3])) < 2e-3 and abs(om.dot(n[:3], b[:3])) < 2e-3, (n, t, b)
    assert max(st.unpack(f'<{len(me.indices) // 2}H', me.indices)) < me.nverts


@test
def obj_meshes_skip_what_draws_nothing() -> None:
    """obj_model.build_meshes leaves out only triangles that draw nothing: one with two corners at one point (no area),
    and one whose packed vertices repeat a written one's (any rotation: the same winding); the same triangle wound the
    other way stays. Every written vertex is byte for byte what it was (its tangent frame still over every triangle of
    its piece), and the written triangles cover every point the drawn input did (the silhouette is the same)."""
    import struct as st
    import obj_model as om
    from mdb import Mesh, VElem
    from mdb_jet import pack_vertex
    layout = [VElem(7, 0, 0, 'BINORMAL'), VElem(7, 8, 0, 'TANGENT'), VElem(7, 16, 0, 'NORMAL'), VElem(7, 24, 0, 'POSITION'),
              VElem(12, 32, 0, 'TEXCOORD'), VElem(1, 40, 0, 'BLENDWEIGHT'), VElem(21, 56, 0, 'BLENDINDICES')]
    template = Mesh(bytes(4), 0, 0, 60, layout, 0, b'', b'')
    cube = _cube_part()
    verts = list(cube.verts)
    a0 = verts[0]
    verts.append(om.Vertex(a0.pos, a0.normal, (0.3, 0.7)))                  # 24: at vertex 0's point, another uv
    verts += [om.Vertex(v.pos, v.normal, v.uv) for v in cube.verts[4:8]]   # 25..28: copies of face 1's 4..7
    clean = list(cube.tris)
    extra = [(0, 24, 1),          # no area: two corners at one point
             (1, 2, 0),           # (0, 1, 2) again, rotated
             (25, 26, 27), (25, 27, 28),   # face 1 again, from copied vertices
             (2, 1, 0)]           # (0, 1, 2) wound the other way: the back face, kept
    part = om.Part('cube', 'm', verts, clean + extra)
    skins = om.rigid(part, 3)
    (me,) = om.build_meshes(template, [(part, skins)], material=0)
    frames = om.tangent_frames(part)
    rows = [pack_vertex(layout, 60, om._row(layout, v, f, s)) for v, f, s in zip(verts, frames, skins)]
    out = [me.vdata[k * 60:(k + 1) * 60] for k in range(me.nverts)]
    tris = [tuple(out[i] for i in t) for t in st.iter_unpack('<3H', me.indices)]

    def canon(r: tuple) -> tuple:
        return min(r, r[1:] + r[:1], r[2:] + r[:2])
    want = [canon(tuple(rows[i] for i in t)) for t in clean + [(2, 1, 0)]]
    assert sorted(canon(t) for t in tris) == sorted(want), f'{len(tris)} triangles written, want {len(want)}'
    assert set(out) <= set(rows), 'a written vertex is not what its piece makes of it'
    drawn = {verts[i].pos for t in part.tris for i in t if len({verts[j].pos for j in t}) == 3}
    pos = {st.unpack_from('<3e', r, 24) for r in out}
    assert pos == {tuple(st.unpack('<3e', st.pack('<3e', *p))) for p in drawn}, 'the silhouette changed'


@test
def procmesh_poles_leave_no_empty_triangles() -> None:
    """procmesh.grid writes no triangle with two corners at one point (an ellipsoid's poles), and the smooth normals and
    tangents it gives (frames) are bit for bit those of the grid with them: they had no area to add."""
    import numpy as np
    import procmesh as pm
    part = pm.Part(0)
    pm.ellipsoid(part, (0.0, 0.0, 0.0), (1.0, 0.6, 2.0), [(0, 1.0)], rings=6, segs=8)
    pos = np.array(part.pos)
    for t in part.tris:
        assert len({tuple(pos[i]) for i in t}) == 3, f'empty triangle {t}'
    full = []
    rows = [list(range(k * 8, k * 8 + 8)) for k in range(7)]
    for a, b in zip(rows, rows[1:]):
        for i in range(8):
            j = (i + 1) % 8
            full += [(a[i], b[i], b[j]), (a[i], b[j], a[j])]
    assert len(full) - len(part.tris) == 2 * 8, (len(full), len(part.tris))   # a ring of each pole's
    assert {i for t in part.tris for i in t} == set(range(len(pos))), 'a vertex lost its triangles'
    for x, y in zip(pm.frames(pos, np.array(full)), pm.frames(pos, np.array(part.tris))):
        assert np.array_equal(x, y)


@test
def prune_members_keeps_what_the_model_uses() -> None:
    """graft_pure.prune_members takes out the other models and the textures (HD and .lod) only they name, and nothing a
    kept model's texture table names; everything else keeps its order."""
    import graft_pure as g
    from mdb import Mdb, Rab, RabFile, Texture, mdb_write

    def model(tex: list[str]) -> bytes:
        return mdb_write(Mdb(0x20, ['mdl'], [], [], [], [Texture(k, f'{t}_DDS', t, 0) for k, t in enumerate(tex)]))
    files = [RabFile('a.lod.DDS', 0, 0, b'1'), RabFile('b.lod.dds', 0, 0, b'2'), RabFile('c.lod.DDS', 0, 0, b'3'),
             RabFile('keep.mdb', 1, 0, model(['a.DDS', 'c.DDS'])), RabFile('keep-lod1.mdb', 1, 0, model(['a.DDS', 'b.dds'])),
             RabFile('debris.mdb', 1, 0, model(['b.dds'])), RabFile('a.DDS', 2, 1, b'4'), RabFile('b.dds', 2, 1, b'5'),
             RabFile('c.DDS', 2, 1, b'6'), RabFile('other.bin', 2, 0, b'7')]
    rab = Rab(0x110, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], list(files))
    gone = g.prune_members(rab, ['KEEP.mdb'])
    assert gone == ['b.lod.dds', 'keep-lod1.mdb', 'debris.mdb', 'b.dds'], gone
    assert [f.name for f in rab.files] == ['a.lod.DDS', 'c.lod.DDS', 'keep.mdb', 'a.DDS', 'c.DDS', 'other.bin']


@test
def skid_tubes_face_out_and_rails_rest_level() -> None:
    """pylib/jet_skids.py's tubes: every triangle wound along its corners' normals (the skids are lit from outside), the
    ring normals unit length across the tube, a rail's level stretch has a vertex straight under its axis over its whole
    length (its contact line is exact) and its turned-up ends rise half the upturn radius (a 60 deg arc)."""
    import math
    import jet_skids as js
    spec = js.SPECS['pd607']
    shape = js.Shape([], [])
    path = js.rail_path(spec, 0.4, 0.0)
    js.tube(shape, path, spec.rail_r)
    js.tube(shape, [(0.4, 0.0, 0.3), (0.33, 0.35, 0.3)], spec.strut_r)
    for a, b, c in shape.tris:
        pa, pb, pc = (shape.verts[i][0] for i in (a, b, c))
        n = tuple(sum(shape.verts[i][1][k] for i in (a, b, c)) for k in range(3))
        assert js._dot(js._cross(js._sub(pb, pa), js._sub(pc, pa)), n) > 0, (a, b, c)  # type: ignore[arg-type]
    for _p, n, t in shape.verts:
        assert abs(math.sqrt(js._dot(n, n)) - 1.0) < 1e-9 and abs(js._dot(n, t)) < 1e-9
    low = min(p[1] for p, _n, _t in shape.verts)
    assert abs(low + spec.rail_r) < 1e-6, low      # its level stretch's end rings lean a hair toward the arcs
    flat = [p for p, _n, _t in shape.verts if abs(p[1] - low) < 1e-12]
    assert min(p[2] for p in flat) <= spec.rail_z[0] + 1e-9 and max(p[2] for p in flat) >= spec.rail_z[1] - 1e-9
    assert all(abs(e[1] - spec.upturn / 2) < 1e-9 for e in (path[0], path[-1])), (path[0], path[-1])


@test
def texture_files_decode_and_slice() -> None:
    import struct as st
    import zlib
    import texfile
    w, h = 5, 3
    rows = [bytes(sum(([x * 40, y * 80, (x + y) * 20] for x in range(w)), [])) for y in range(h)]
    raw = bytearray()
    prev = bytes(3 * w)
    for y, row in enumerate(rows):     # each row with another filter (0 none, 1 sub, 2 up, 3 average, 4 paeth)
        ft = y % 5
        out = bytearray(row)
        for i in range(len(row)):
            left = row[i - 3] if i >= 3 else 0
            ul = prev[i - 3] if i >= 3 else 0
            pred = (0, left, prev[i], (left + prev[i]) // 2, texfile._paeth(left, prev[i], ul))[ft]
            out[i] = (row[i] - pred) & 0xFF
        raw += bytes([ft]) + out
        prev = row
    chunk = lambda k, b: st.pack('>I', len(b)) + k + b + st.pack('>I', zlib.crc32(k + b))  # noqa: E731
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', st.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b'')
    img = texfile.decode_png(png)
    assert (img.width, img.height) == (w, h)
    assert bytes(img.rgba[0::4]) + bytes(img.rgba[1::4]) == bytes(b for r in rows for b in r[0::3]) + bytes(b for r in rows for b in r[1::3])
    dds = texfile.solid_dxt1((200, 100, 50), 64)
    info = texfile.dds_info(dds)
    assert (info.width, info.mips, info.fourcc) == (64, 7, b'DXT1')
    hd, lod = texfile.texture_pair(dds)
    li = texfile.dds_info(lod)
    assert hd == dds and (li.width, li.height, li.mips) == (16, 16, 4), li    # 64 -> 16 (never below 16), 16 .. 2 (stock)
    assert len(lod) == 128 + 8 * (16 + 4 + 1 + 1) and texfile.pair_problem(hd, lod) is None
    assert texfile.pair_problem(hd, texfile.dds_tail(dds, 2)) == '.lod has 5 mip levels, the stock layout 4'
    assert [texfile.lod_mips(w, h) for w, h in ((128, 128), (64, 32), (16, 16), (32, 16))] == [7, 6, 4, 5]
    assert texfile.lod_level(2048, 2048) == 4 and texfile.lod_level(1024, 512) == 4 and texfile.lod_level(64, 64) == 2
    c0, c1, idx = st.unpack_from('<HHI', lod, 128)
    assert c0 == c1 == ((200 * 31 + 127) // 255) << 11 | ((100 * 63 + 127) // 255) << 5 | ((50 * 31 + 127) // 255) and idx == 0



@test
def textures_go_before_the_model() -> None:
    """The game binds a model's textures when the model member loads, from the TEXTURE members read before it (mdb.
    insert_member): add_texture / copy_texture_members into an archive that holds only its model (the twin tank's
    case, which rendered black) put each .lod before the model, the HD after; texture_problems refuses the old order."""
    import graft_pure as g
    import obj_model as om
    import texfile
    from mdb import Mdb, Rab, RabFile, Texture, rab_read, rab_write
    md = Mdb(0, [], [], [], [], [Texture(0, 'a_DDS', 'a.dds', 0), Texture(1, 'b_DDS', 'b.DDS', 0)])
    rab = Rab(0x200, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [RabFile('m.mdb', 1, 0, b'model')])
    for fn in ('a.dds',):
        om.add_texture(rab, fn, texfile.solid_dxt1((10, 20, 30), 64))
    donor = Rab(0x200, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    om.add_texture(donor, 'b.DDS', texfile.solid_dxt1((40, 50, 60), 32))
    g.copy_texture_members(rab, donor, ['b.DDS'])
    assert [(rab.folders[f.folder], f.name) for f in rab.files] == [
        ('TEXTURE', 'a.lod.dds'), ('TEXTURE', 'b.lod.DDS'), ('MODEL', 'm.mdb'), ('HD-TEXTURE', 'a.dds'),
        ('HD-TEXTURE', 'b.DDS')], rab.files
    assert om.texture_problems(rab, md, 'm.mdb') == []
    old = Rab(rab.version, rab.folders, [rab.files[k] for k in (2, 3, 4, 0, 1)])     # the order the old code wrote
    bad = om.texture_problems(old, md, 'm.mdb')
    assert bad[0].startswith('archive members not in folder-table order') and len(bad) == 3, bad
    assert 'a.lod.dds after the model m.mdb' in bad[1] and 'b.lod.DDS after the model' in bad[2], bad
    assert rab_read(rab_write(rab)).files[0].name == 'a.lod.dds'

@test
def artillery_chassis_is_stock() -> None:
    """pylib/artillery_model.py takes the chassis from the stock E551 as it is: stock_meshes keeps the triangles on the
    OBJ's points with every vertex byte copied but the blend indices (mapped to the Kepler's bones); stock_materials /
    material_problems: the E551 materials unchanged (tracks under the Kepler's names, which the SGO scrolls), their
    stored texture members, the turret the only other material; an edited parameter, a re-encoded texture member or
    an unused member is refused."""
    import struct
    import artillery_model as am
    import graft_pure as g
    import obj_model as om
    import texfile
    from dataclasses import replace
    from mdb import MatParam, MatTex, Material, Mdb, Mesh, Object, Rab, RabFile, Texture, VElem
    elems = [VElem(7, 0, 0, 'position'), VElem(12, 8, 0, 'texcoord'), VElem(1, 16, 0, 'BLENDWEIGHT'),
             VElem(21, 32, 0, 'BLENDINDICES')]
    P = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (1.0, 1.0, 0.0)]
    rows = [struct.pack('<4e2f4f4B', *p, 1.0, 0.0, 0.25 * k, 0.75, 0.25, 0.0, 0.0, 3, 4, 9, 9) for k, p in enumerate(P)]
    me = Mesh(bytes((0, 1, 2, 0)), 0, 0, 36, elems, 0, b''.join(rows), struct.pack('<6H', 0, 1, 2, 1, 3, 2))
    names = ['mdl'] + list(am.HULL_MATERIALS)
    mats = [Material(k, 0, 0, k + 1, 'snd_BRDF_Mech_Catapillar' if n.startswith('Caterpi') else 'snd_BRDF_Common_Basic',
                     [MatParam([0.5, 0.5, 0.5, 0.0], (0, 0), 'scroll_texture' if n.startswith('Caterpi') else 'diffuse', 513)],
                     [MatTex(k, 'albedo', (0,) * 5)], 3) for k, n in enumerate(am.HULL_MATERIALS)]
    texs = [Texture(k, f't{k}_DDS', f't{k}.DDS', 0) for k in range(len(mats))]
    ref = Mdb(0x20, names, [], [Object(0, 0, [me])], mats, texs)
    out, kept = am.stock_meshes(ref, {om.wkey(p) for p in P[:3]}, {3: 7, 4: 8})
    assert len(out) == 1 and kept == {frozenset(om.wkey(p) for p in P[:3])}, kept
    new = out[0][1]
    assert g.triangles(new) == [(0, 1, 2)] and new.nverts == 3, g.triangles(new)
    for k in range(3):
        a, b = new.vdata[36 * k:36 * k + 36], rows[k]
        assert a[:32] == b[:32] and a[32:] == bytes((7, 8, 0, 0)), (a, b)
    try:
        am.stock_meshes(ref, {om.wkey(p) for p in P[:3]}, {3: 7})
        raise AssertionError('an unmapped weighted bone was accepted')
    except am.ArtilleryCheckError:
        pass
    ref_rab = Rab(0x200, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    for t in texs:
        om.add_texture(ref_rab, t.filename, texfile.solid_dxt1((10 * t.index, 20, 30), 32))
    fp = am.make_stock_fingerprints(ref, ref_rab)

    def made(edit: bool = False) -> tuple[Rab, Mdb]:
        md, _map = am.stock_materials(Mdb(0x20, ['mdl'], [], [], [], []), ref, set(range(len(mats))))
        md, _t = om.add_material(md, ref, 'v505_tank', am.TURRET_MATERIAL[0], {'albedo': 'own.dds'})
        if edit:
            md.materials[0] = replace(md.materials[0], params=[replace(md.materials[0].params[0], value=[1.0, 0, 0, 0])])
        rab = Rab(0x200, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [RabFile(am.HOST_MDB, 1, 0, b'model')])
        g.copy_texture_members(rab, ref_rab, [t.filename for t in texs])
        om.add_texture(rab, 'own.dds', texfile.solid_dxt1((1, 2, 3), 32))
        return rab, md
    with patched(am, STOCK_MATERIALS=fp[0], STOCK_TEXTURES=fp[1]):
        rab, md = made()
        assert {md.name_of(m.name) for m in md.materials} == set(am.HULL_MATERIALS.values()) | {am.TURRET_MATERIAL[0]}
        assert am.material_problems(rab, md) == [], am.material_problems(rab, md)
        assert any('is not the stock E551' in x for x in am.material_problems(*made(edit=True)))
        om.add_texture(rab, 'dead.dds', texfile.solid_dxt1((1, 2, 3), 32))
        assert sorted(am.material_problems(rab, md)) == ['archive member dead.dds used by no material',
                                                         'archive member dead.lod.dds used by no material']
        rab, md = made()
        om.add_texture(rab, 't0.DDS', texfile.solid_dxt1((99, 2, 3), 32))      # a re-encoded stock texture
        assert any('t0.DDS: member' in x for x in am.material_problems(rab, md)), am.material_problems(rab, md)


@test
def artillery_ragdoll_is_the_models() -> None:
    """The twin tank's physics skeleton is its model's (pylib/ragdoll_fit.py, tools/make_artillery.py ragdoll; the
    turret could not turn while the Kepler's ragdoll kept its hinge 0.68 m from the moved turret bone). With Root.cpk:
    every stock vehicle's ragdoll agrees with its own model (the rule problems() checks holds in the game's data);
    refitting the Kepler's to its own model changes no byte; with the twin tank's model folder: its skeleton against
    the stock ragdoll is refused (turret, guns, wheels), the refitted one agrees, its turret hinge is on the turret
    bone and its gun hinges on the gun bones, and vehicle_sgo names the refitted ragdoll with the refitted binding."""
    import artillery_model as am
    import ragdoll_fit as rf
    import rootcpk
    from mdb import mdb_read, rab_read
    assert 'import ragdoll_fit' in src('tools/make_artillery.py') and "'ragdoll_fit'" in src('tools/build_release.py')
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.default()

    def stock(sgo_name: str):  # noqa: ANN202 - (model, shkt, blob)
        raw = game.read('OBJECT', sgo_name)
        if raw[:4] == b'DSGO':
            r = dsgo.parse(raw).root
            arc, mdb = r.get('animation_model').items[0].items
            path, blob = r.get('ragdoll').items[0], r.get('ragdoll').items[1].data
        else:
            import sgo
            _v, m = sgo.read(raw)
            (arc, mdb), (path, blob) = m['animation_model'][0], m['ragdoll']
        md = mdb_read(next(f for f in rab_read(game.read('OBJECT', str(arc).rsplit('/', 1)[1].upper())).files
                           if f.name.lower() == str(mdb).lower()).data)
        return md, game.read('OBJECT', str(path).rsplit('/', 1)[1].upper()), bytes(blob)
    for name in ('V603_FLAK.SGO', 'V505_TANK.SGO', 'VEHICLE402_ROCKET.SGO', 'V503_BIKE.SGO', 'V506_HELI.SGO'):
        md, shkt, blob = stock(name)
        assert rf.problems(md, shkt, blob) == [], (name, rf.problems(md, shkt, blob)[:3])
    kepler, shkt, blob = stock('V603_FLAK.SGO')
    # The Kepler is a ragdoll with constraints. Reading a constraint's type must not make it look like a plain
    # physics system and silently skip the reference pose in both the checker and the fitter.
    import copy
    import struct
    parsed = rf.Shkt(shkt)
    assert parsed.joints and len(parsed.ref) == len(parsed.bodies) == 22, 'the Kepler reference pose was not loaded'
    corrupted = bytearray(shkt)
    struct.pack_into('<f', corrupted, parsed.ref[0], parsed.ref_t(0)[0] + 1.0)
    assert any(x.startswith('reference pose of ') for x in rf.problems(kepler, bytes(corrupted), blob)), \
        'a corrupt reference pose escaped validation'
    # Move a real stock turret without requiring an external model folder: fitting must rewrite the serialized
    # reference pose as well as body/joint transforms. Restoring the old pose must make the checker fail again.
    moved = copy.deepcopy(kepler)
    next(b for b in moved.bones if moved.name_of(b.name) == 'cannon_main').local[12] += 2.0
    moved_shkt, moved_blob = rf.fit(moved, shkt, blob, kepler)
    fitted = rf.Shkt(moved_shkt)
    changed = [k for k in range(len(parsed.ref)) if rf.dist(parsed.ref_t(k), fitted.ref_t(k)) > 1e-3]
    assert changed, 'the moved turret retained the stock reference pose'
    assert rf.problems(moved, moved_shkt, moved_blob) == []
    stale = bytearray(moved_shkt)
    for k in changed:
        struct.pack_into('<3f', stale, fitted.ref[k], *parsed.ref_t(k))
    assert any(x.startswith('reference pose of ') for x in rf.problems(moved, bytes(stale), moved_blob)), \
        'refitting stopped checking the serialized reference pose'
    assert make_artillery.ragdoll(game, kepler) == (shkt, blob), 'refitting the Kepler to itself changed it'
    folder = am.model_dir()
    if folder is None:
        return
    sk = am.skeleton(game, folder)
    before = rf.problems(sk, shkt, blob)
    for bone in ('cannon_main', 'cannon_l', 'cannon_r', 'tire_moveB_l', 'catapi_body'):
        assert any(x.startswith(f'bone {bone} ') for x in before), (bone, before[:3])
    new_shkt, new_blob = make_artillery.ragdoll(game, sk)
    assert rf.problems(sk, new_shkt, new_blob) == []
    s, bones = rf.Shkt(new_shkt), rf.bone_frames(sk)
    hinge = {s.bodies[j.child].name: s.joint_world(j)[1] for j in s.joints}
    for proxy, bone in (('ragdoll_cannon_main', 'cannon_main'), ('ragdoll_cannon_l', 'cannon_l'),
                        ('ragdoll_cannon_r', 'cannon_r')):
        assert rf.dist(hinge[proxy], bones[bone][0]) < 1e-3, (proxy, hinge[proxy], bones[bone][0])
    r = dsgo.parse(make_artillery.vehicle_sgo(game, True, new_blob)).root.get('ragdoll')
    assert r.items[0] == make_artillery.RAGDOLL and r.items[1].data == new_blob, r.items[0]
    assert make_artillery.RAGDOLL == f'app:/object/{make_artillery.RAGDOLL_FILE.lower()}'


@test
def artillery_turret_fixed_guns_mounted() -> None:
    """The self-propelled howitzer's turret is fixed (the user, 2026-10-06): its car_base_constraint_data turret entry
    gets TURRET_LIMITS (the hinge and, through 0x669BA0, the seat's yaw stops), a stock entry already limited is
    refused, and the turret camera still serves its fixed indirect-fire gun (src/turretcam.cpp Turret). The guns'
    MAB points (muzzle, casing) are found once each in the stock Kepler guns. With the twin tank's model folder: the
    built guns put the shell on each barrel's mouth (the stock offsets put it 2.9 m in front of it) and the casing
    just ahead of the trunnion outside the turret (make_artillery.gun_problems); the stock MAB on that model fails."""
    import artillery_model as am
    import rootcpk
    tc = src('src/turretcam.cpp')
    assert '(yaw[1]-yaw[0]>kMinTraverse || IndirectFireSeat(seat))' in tc, 'src/turretcam.cpp Turret: a fixed howitzer'
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = vc.Game(rootcpk.DEFAULT_GAME)
    for own in (False, True):
        if own and am.model_dir() is None:
            continue
        r = dsgo.parse(make_artillery.vehicle_sgo(game, own)).root
        assert dsgo.to_py(make_artillery.turret_constraint(r).items[1].items[1]) == make_artillery.TURRET_LIMITS
        try:
            make_artillery.lock_turret(r)
            raise AssertionError('a turret entry with stops was locked again')
        except ValueError:
            pass
    for stock in make_artillery.STOCK_GUNS:
        mab = dsgo.parse(game.read('WEAPON', stock)).root.get('animation_model').items[2].data
        muzzle, eject = make_artillery.gun_points(mab)
        for got, want in ((muzzle, make_artillery.STOCK_MUZZLE), (eject, make_artillery.STOCK_EJECT)):
            assert max(abs(abs(a) - abs(b)) for a, b in zip(got, want)) < 1e-3, (stock, got, want)
    folder = am.model_dir()
    if folder is None:
        return
    files = make_artillery.build(rootcpk.DEFAULT_GAME)
    _arc, md, _info = am.build_with_info(game, folder)
    assert make_artillery.gun_problems(files, game, md) == []
    stock = dict(files)
    for s, path in zip(make_artillery.STOCK_GUNS, make_artillery.VEHICLE.weapons):
        stock[f'WEAPON/{path.split("/")[-1].upper()}'] = make_artillery.howitzer_sgo(game, s)
    bad = make_artillery.gun_problems(stock, game, md)
    assert len(bad) == 4 and sum('the shell leaves at' in x for x in bad) == 2, bad


@test
def drill_spin_bone_free_of_the_ragdoll() -> None:
    """The drill tank's spin bone (drill_model.SPIN_BONE, the Blacker's catapi_body) is drawn from its local matrix,
    not from the hull's physics proxy: the Blacker's ragdoll binding draws catapi_body from RagDollProxys.body every
    frame (0x6EDCA0), which put the rehomed drill 2.5 m low inside the hull and kept it from turning. With Root.cpk:
    the stock binding against the Blacker skeleton with the spin bone rehomed is refused (ragdoll_fit.problems names
    catapi_body), make_drill.free_spin_bone's is accepted with only that row gone, and the drill SGO carries it."""
    import drill_model
    import make_drill
    import ragdoll_fit as rf
    import rootcpk
    import sgo
    from mdb import mdb_read, rab_read
    assert 'free_spin_bone(' in src('tools/make_drill.py') and 'ragdoll_fit.problems' in src('tools/make_drill.py')
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.default()
    _v, m = sgo.read(game.read('OBJECT', 'V505_TANK.SGO'))
    blob, shkt = bytes(m['ragdoll'][1]), game.read('OBJECT', str(m['ragdoll'][0]).rsplit('/', 1)[1].upper())
    stock = mdb_read(next(f for f in rab_read(game.read('OBJECT', drill_model.HOST_ARC)).files
                          if f.name.lower() == drill_model.HOST_MDB).data)
    assert rf.problems(stock, shkt, blob) == []
    moved, _i = drill_model.rehome_spin_bone(stock, drill_model.SPIN_BONE, drill_model.DRILL_PARENT, drill_model.DRILL_BASE)
    before = rf.problems(moved, shkt, blob)
    assert len(before) == 1 and before[0].startswith(f'bone {drill_model.SPIN_BONE} '), before
    free = make_drill.free_spin_bone(blob)
    assert rf.problems(moved, shkt, free) == [], rf.problems(moved, shkt, free)
    rows = lambda b, k: [tuple(map(str, e[0])) for e in sgo.read(b)[1][k]]  # noqa: E731
    gone = set(rows(blob, 'animation_from_ragdoll')) - set(rows(free, 'animation_from_ragdoll'))
    assert gone == {('RagDollProxys.body', drill_model.SPIN_BONE)}, gone
    assert rows(blob, 'ragdoll_from_animation') == rows(free, 'ragdoll_from_animation')
    built = sgo.read(make_drill.vehicle_sgo(game, [f'app:/Object/{make_drill.MODEL_FILE.lower()}', make_drill.MODEL_MDB]))[1]
    assert bytes(built['ragdoll'][1]) == free
    assert bytes(sgo.read(make_drill.vehicle_sgo(game, make_drill.STOCK_MODEL))[1]['ragdoll'][1]) == blob

@test
def stock_payload_and_seats_wired() -> None:
    """The stock vehicles' payload readout and store switch (src/payload.cpp) and the seat switch (src/seatswitch.cpp):
    their ini keys are read, shipped and documented, the keys range-checked; the input hook moves the player before the
    steps that read who sits where and picks the store before the heli sight marks it; the HUD's struct is the header's;
    playerjet.cpp tells a move between the gunship's seats from getting out; the installer step (tools/make_stock_stores.py)
    is opt-in (off in the shipped ini), installed after the jets' store weapons, removed, bundled and a ledger owner, and
    its stores are store weapons src/stores.inc knows, each written by make_jets (the jets carry it) or by itself."""
    import make_stock_stores as mss
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    keys = ('StockVehicleStores', 'SeatSwitch', 'SeatNextKey', 'SeatNumberKeys', 'SeatButton', 'SeatPilot', 'SeatSwitchOnline', 'SeatList')
    for key in keys:
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in ('SeatNextKey', 'SeatButton'):
        assert f'FixInt("{key}"' in plugin, f'{key} is not range-checked'
    assert re.search(r'^StockVehicleStores=1', ini, re.M) and mss.wanted(ini), 'StockVehicleStores ships on'
    assert not mss.wanted('[VehicleCrew]\nStockVehicleStores=0\n'), 'the player can turn it off'
    assert 'bool stockStores=true;' in src('src/crew.h'), 'the plugin defaults it on as the ini does'
    assert mss.wanted('[VehicleCrew]\nStockVehicleStores=1 ; on\n') and not mss.wanted('[Other]\nStockVehicleStores=1\n')
    # the older key (the helicopters alone) still turns it on, in the plugin and the installer alike
    assert 'L"StockHeliStores"' in plugin and mss.wanted('[VehicleCrew]\nStockVehicleStores=0\nStockHeliStores=1\n')
    # every stock vehicle's fire goes through the holder pull: payload.cpp takes it over (checked bytes), and the stock
    # classes build their extra holders (stores.cpp kBuilds: one class each of the installer's BUILT_CLASSES but the 506)
    payload, stores = src('src/payload.cpp'), src('src/stores.cpp')
    assert 'kPull=0x62C000' in payload and 'bool InstallPayload()' in payload and 'InstallPayload();' in plugin
    assert 'PullHook' in payload and 'kFireSecondary' not in payload, 'payload.cpp: the pull lands on the store picked'
    builds = re.findall(r'\{0x[0-9A-F]+,0x[0-9A-F]+,"([0-9A-Za-z_]+)"\}', stores.split('const BuildClass kBuilds[]={', 1)[1].split('};', 1)[0])
    want = sorted(c.replace('Vehicle', '', 1).lstrip('_') if c != 'VehicleHelicopter409' else 'Helicopter409'
                  for c in mss.BUILT_CLASSES if c != 'Vehicle506_Helicopter')
    assert sorted(builds) == want, (builds, want)
    assert 'if(IsStoreWeapon(w))return PayloadFire::store;' in payload and 'return StoreOf(w)!=nullptr;' in stores
    hook = src('src/crew.cpp').split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    # The disabled branch also calls AimLines to restore native lines. Check the
    # enabled pipeline's ordering, not that earlier ownership-cleanup call.
    enabled_hook = hook[hook.index('FrameTick();'):]
    order = [enabled_hook.find(f'&{f},') for f in ('CrewStep<I>', 'SeatSwitchFrame', 'AimLines', 'PlayerJetFrame', 'PayloadFrame', 'HeliSightFrame')]
    assert all(x >= 0 for x in order) and order == sorted(order), f'src/crew.cpp InputHook step order: {order}'
    assert 'PayloadPicked(v)' in src('src/helisight.cpp')
    assert 'PlayerSeatPrompt(&s.seatPrompt)' in src('src/hud.cpp') and 'void SeatLine(' in src('src/hud.cpp')
    header = src('src/payload.h')
    for name in ('struct PayloadReadout', 'struct PayloadEntry', 'bool PlayerPayload(', 'struct SeatPrompt'):
        assert name in header, name
    pj = src('src/playerjet.cpp')
    assert 'if(j.driven && AboardElsewhere(v,0))Moved(j,v);' in pj, 'playerjet.cpp: a move to the gun is no ejection'
    assert 'AboardElsewhere(v,kGunnerSeat)' in src('src/playerjet_crew.inc'), 'playerjet_crew.inc: a move to the stick is no bail-out'
    for f in ('src/payload.cpp', 'src/seatswitch.cpp'):
        assert f in src('CMakeLists.txt'), f
    assert mss.OWNER in ledger.OWNERS
    inst = src('tools/installer.py')
    assert inst.index('make_jets.install(game, jets)') < inst.index('make_stock_stores.install(game, files)'), 'stores after the jets'
    assert 'make_stock_stores.remove' in inst and "'make_stock_stores'" in src('tools/build_release.py')
    stores_inc = src('src/stores.inc')
    assert set(mss.jet_store_files()) <= set(vc.STORE_FILES) and not set(mss.own_store_files()) & set(vc.STORE_FILES)
    assert set(mss.jet_store_files()) | set(mss.own_store_files()) == set(mss.store_files())
    for f in mss.store_files():
        kind = vc.store_of('app:/weapon/' + f.lower())[0]
        assert f'L"EDF6VC_{kind}_"' in stores_inc, f'{f}: src/stores.inc does not know its kind'
    # The seat switch's offsets agree with the RE notes.
    doc = src('docs/stock-payload-re.md')
    for rva in ('0x5763E0', '0x551C30', '0x56C9F0', '0x633FE0', '0x634940', '0x6346FC', '0x56D7CC', '0x572734'):
        assert rva in doc, rva
    seat = src('src/seatswitch.cpp')
    for c in ('kAnnounce=0x5763E0', 'kSetAction=0x551C30', 'kRideAction=0x56C9F0', 'kReserve=0x633FE0', 'kClear=0x634940'):
        assert c in seat, c
    # The seats line is a reading, not a move (the user 2026-10-07): SeatList lists them the whole ride, with SeatSwitch
    # off too, and the keys ride along only in the prompt's moment.
    assert re.search(r'^SeatList=1', ini, re.M), 'SeatList ships on'
    frame = seat.split('void SeatSwitchFrame(', 1)[1].split('\n}\n', 1)[0]
    assert '!(ok && Cfg().seatSwitch) && !Cfg().seatList' in frame, 'SeatSwitchFrame: the list runs without the switch'
    assert 'Cfg().seatList ? ms+kFreshMs' in seat, 'Publish: SeatList keeps the line up the whole ride'
    assert 'const bool may=ok && Cfg().seatSwitch;' in seat and 'if(p.hints)SeatKeys(l,p);' in src('src/hud.cpp')
    # The AI riders in gunner seats work their guns (the user 2026-10-07): RideAi's dummy riders a bump or a seat swap
    # moved there too, and the 410's door seats under a player pilot, on the gun's own rounds.
    npc = src('src/npcai.cpp')
    assert 'if(who==Rider::dummy)' in npc and 'if(!AiGunner(v,seat))continue;' in npc
    heli = src('src/heli.cpp')
    assert 'DoorGun(c->doors[i],ObjRef{},false,v,i,false,dt,ms)' in heli, 'the player-piloted 410: no refill, no hold'
    assert heli.index('CrewDoorGuns(vehicle);', heli.index('void HeliFrame(')) < heli.index('Replica(vehicle)', heli.index('void HeliFrame(')), 'NPC gunner authority is independent of the local player pilot'
    assert re.search(r'^NpcGunners=1', ini, re.M) and 'L"NpcGunners"' in plugin and 'NpcGunners' in readme
    # Out of a ground vehicle's driver seat with the stock driving AI an NPC driver takes it (the user 2026-10-07: the map
    # sends it off with the player aboard); Crew() never does while a player rides, so Pilot must.
    pilot = seat.split('void Pilot(unsigned char* v) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(!heli && !NpcDrivable(v))return;' in pilot and 'if(!SeatNpcRider(v,false))' in pilot
    assert 'bool NpcDrivable(const unsigned char* v) noexcept { return FamilyOf(v)!=Family::none; }' in src('src/npcpost.cpp')
    # Every vehicle an NPC can drive (the user 2026-10-07: "所有载具都要支持ai"): the unarmed trucks of the Grape's class
    # too, and the CarBase classes whose slot 49 is a preferred-seat wrapper (the trucks 607 / 60X, the rescue 507).
    crew_src = src('src/crew.cpp')
    assert 'armedOnly' not in crew_src, 'an unarmed vehicle gets an NPC driver too'
    for entry in ('{0x17DCAB0,0x65A390,"607_RoboTruck",0x65B910}', '{0x17DCFB8,0x65A390,"60X_Truck",0x65B910}',
                  '{0x17DB590,0x61BFD0,"507_Rescuetank",0x61D310}'):
        assert entry in crew_src, entry
    assert 'if(moved && at==0)Pilot(v);' in seat


@test
def new_defaults_on_once() -> None:
    """The settings whose default became on (the user, 2026-10-07: "还有什么默认是关的，都打开"): the shipped ini and the
    plugin's own defaults have them on; an existing ini still holding the old default gets the new one, once (its mark
    keeps a later install from undoing the player's own 0), a value the player set is kept; the install reads the ini as
    it will be (player_ini_text), so the stores are built on the first install; the uninstall that keeps the call
    weapons still takes the stores back, EDF6AutoTurret's flak requests rewritten first."""
    import installer
    ini, crew = src('EDF6VehicleCrew.ini'), src('src/crew.h')
    for key, (old, new) in installer.NEW_DEFAULTS[installer.SECTION].items():
        assert re.search(rf'^{key}={new}\b', ini, re.M), f'{key} ships {new}'
    for field in ('DWORD heliLandMs=5000;', 'bool rescueAutoBoard=true;', 'bool stockStores=true;', 'bool seatSwitchOnline=true;'):
        assert field in crew, field
    user = '[VehicleCrew]\nEnabled=1\nHeliLandMs=0\nRescueAutoBoard=0 ; mine\nSeatSwitchOnline=1\nStockHeliStores=0\n'
    text, flipped = installer.apply_new_defaults(user)
    assert sorted(flipped) == ['HeliLandMs', 'RescueAutoBoard'], flipped
    assert 'HeliLandMs=5000' in text and 'RescueAutoBoard=1 ; mine' in text and installer.DEFAULTS_MARK in text
    again = text.replace('RescueAutoBoard=1', 'RescueAutoBoard=0')
    assert installer.apply_new_defaults(again) == (again, []), 'a later install undid the player\'s own 0'
    planned = installer.planned_ini(user, ini)
    assert 'StockVehicleStores' in planned[1] and planned[3] and make_stock_stores_wanted(planned[0])
    inst = src('tools/installer.py')
    assert 'return planned_ini(' in inst.split('def player_ini_text(', 1)[1].split('\ndef ', 1)[0], 'the install reads the ini as it will be'
    un = inst.split('def uninstall(game: str)', 1)[1].split('\ndef ', 1)[0]
    assert "if choice == '2':" in un and 'uninstall_stock_stores(game)' in un, 'the plugin-only uninstall keeps the stores'
    body = inst.split('def uninstall_stock_stores(', 1)[1].split('\ndef ', 1)[0]
    assert body.index('install_autoturret(') < body.index('make_stock_stores.remove('), 'its requests rewritten before the vehicles go'


def make_stock_stores_wanted(text: str) -> bool:
    import make_stock_stores
    return make_stock_stores.wanted(text)


@test
def stock_stores_build() -> None:
    """With the game here (CI has none): every stock request that brings a vehicle of LOADOUTS gets a vehicle whose
    holders are its own and one a store (each a copy of the one it hangs beside) and lists a weapon a holder, the stores
    last (make_stock_stores.check); only the vehicle, the weapon list and the preload list change; every vehicle's class
    is one the plugin builds the extra holders of; EDF6AutoTurret's requests handed in come back with the stores."""
    import rootcpk
    import make_stock_stores as mss
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.default()
    names = mss.requests(game)
    assert names, 'no stock request brings a vehicle of LOADOUTS'
    rows = {stem: mss.stock_rows(game, stem) for stem in mss.LOADOUTS}
    files: dict[str, bytes] = {}
    stems = set()
    for name in names:
        data, stem = mss.request_sgo(game.read('WEAPON', name), name, rows)
        files[f'WEAPON/{name}'] = data
        stems.add(stem)
        before, after = dsgo.to_py(dsgo.parse(game.read('WEAPON', name)).root), dsgo.to_py(dsgo.parse(data).root)
        assert set(before) == set(after), name
        for k in before:
            if k not in ('Ammo_CustomParameter', 'resource'):
                assert before[k] == after[k], (name, k)
    assert stems == set(mss.LOADOUTS), set(mss.LOADOUTS) - stems
    for stem in stems:
        assert mss.vehicle_class(game, stem) in mss.BUILT_CLASSES, stem
        files[f'OBJECT/{mss.derived_name(stem)}'] = mss.derived_vehicle(game, stem)
    mss.check(files, rows)
    flak = 'WEAPON/AWEAPON346.SGO'
    assert flak in files, 'the Kepler is one of them'
    import build as at_build
    turret = at_build.build_files()
    assert flak in turret, 'EDF6AutoTurret writes the Kepler request'
    handed = mss.build(rootcpk.DEFAULT_GAME,
                       overlay={k: v for k, v in turret.items() if k.startswith('WEAPON/')})[2]
    got = dsgo.to_py(dsgo.parse(handed[flak]).root)
    mine = dsgo.to_py(dsgo.parse(turret[flak]).root)
    assert [k for k in got if k not in ('Ammo_CustomParameter', 'resource')] == [k for k in mine if k not in ('Ammo_CustomParameter', 'resource')]
    assert 'edf6vc_v603_flak_stores.sgo' in str(got['Ammo_CustomParameter']) and 'edf6vc_aam_s_2.sgo' in str(got['resource'])


@test
def impact_charges_agree() -> None:
    """src/jet_bay.cpp's impact charges are tools/make_jets.py's IMPACT_FILES in its order (the 2 / 4 / 12 m ones appended
    after the four first shipped), each file named by its radius; the pick is the nearest in ratio (vehicleram.h
    NearestCharge), not the first at least as wide; the jets' ram radius is half the model's size and README says the
    numbers the code gives."""
    import make_jets
    bay = src('src/jet_bay.cpp')
    rows = re.findall(r'\{L"app:/object/(edf6vc_impact_\d+\.sgo)",L"(EDF6VC_IMPACT_\d+\.SGO)",([\d.]+)f\}', bay)
    assert [(f, float(r)) for _, f, r in rows] == list(make_jets.IMPACT_FILES.items()), rows
    assert all(sgo == f.lower() for sgo, f, _ in rows), 'kCharges sgo paths are the files lowercased'
    assert list(make_jets.IMPACT_FILES)[:4] == [f'EDF6VC_IMPACT_{n:02d}.SGO' for n in (8, 16, 32, 64)], 'the first four stay first'
    assert all(f == f'EDF6VC_IMPACT_{int(r):02d}.SGO' for f, r in make_jets.IMPACT_FILES.items()), 'a file named by its radius'
    assert {2.0, 4.0, 12.0} <= set(make_jets.IMPACT_FILES.values())
    for name in make_jets.IMPACT_FILES:
        assert f'OBJECT/{name}' in make_jets.names(), f'the installer writes {name}'
    pick = bay.split('int ChargeFor(', 1)[1].split('\n}\n', 1)[0]
    assert 'ram::NearestCharge(' in pick, 'src/jet_bay.cpp ChargeFor: the nearest charge'
    pj = src('src/playerjet.cpp')
    assert 'ram::Damage(' in pj and 'kRamJoulesPerDamage' not in pj, 'the jets take the one ram formula (vehicleram.h)'
    assert re.search(r'\{"fighter",7201,.*, 8\.0f\}', pj) and re.search(r'\{"strike", 7202,.*, 12\.0f\}', pj), 'half of 16 m / 25 m'
    readme = src('README.md')
    assert '战斗机约 8 米、攻击机约 12 米' in readme and '约 10 米' not in readme.split('**撞击伤害**', 1)[1].split('\n', 1)[0]


@test
def vehicle_ram_wired() -> None:
    """The ground vehicles' ram (src/vehicleram.cpp, src/vehicleram.h): every class it knows is one crew.cpp names (the same
    vtable and name), the hooked ones get it from the input hook (the Proteus too, since proteus.cpp) and the one crew.cpp
    leaves alone (the Barga) from its own update hook; it is reset with the mission, built (its own target_sources line) with its offline
    check an EXCLUDE_FROM_ALL target; its ini keys are read, range-checked, shipped and documented."""
    h, c, crew = src('src/vehicleram.h'), src('src/vehicleram.cpp'), src('src/crew.cpp')
    known = dict((name, int(vt, 16)) for vt, name in re.findall(r'\{(0x[0-9A-Fa-f]+|kVt\w+),[^{}]*?"(\w+)"', crew)
                 if vt.startswith('0x'))
    known['502_GroundRobo'] = int(re.search(r'kVt502=(0x[0-9A-Fa-f]+)', src('src/layout.h')).group(1), 16)
    profiles = re.findall(r'(?:Hull|Walker)\((0x[0-9A-Fa-f]+),"(\w+)"|Profile\{(0x[0-9A-Fa-f]+),"(\w+)"', h)
    profiles = [(a or c2, b or d) for a, b, c2, d in profiles]
    assert len(profiles) >= 16, profiles
    for vt, name in profiles:
        assert known.get(name) == int(vt, 16), f'src/vehicleram.h {name} {vt}: not as crew.cpp kClasses has it'
    # The Barga is crew.cpp's now (its slot 4): no own update hook here, or the ram step would run twice a frame.
    assert '{0x17D98C8,0x60AEC0,"501_FortressRobo",kFindSeat,4}' in crew
    assert 'kExtras' not in c and 'ChainVtableSlot' not in c, 'src/vehicleram.cpp: an own update hook besides crew.cpp'
    hook = crew.split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    assert '&VehicleRamFrame,' in hook, 'src/crew.cpp InputHook: the ram step'
    assert 'ResetVehicleRams();' in src('src/mission.cpp') and 'InstallVehicleRam();' in src('src/plugin.cpp')
    cm = src('CMakeLists.txt')
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/vehicleram.cpp)' in cm
    assert re.search(r'add_executable\(vehicle_ram_check EXCLUDE_FROM_ALL tools/vehicle_ram_check\.cpp\)', cm)
    assert '#include "../src/vehicleram.h"' in src('tools/vehicle_ram_check.cpp')
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key, default in (('VehicleRam', '1'), ('VehicleRamDamage', '20')):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M) and key in readme, key
    assert 'Fix("VehicleRamDamage"' in plugin, 'VehicleRamDamage is range-checked'


@test
def map_commands_wired() -> None:
    """The map's NPC commands (src/mapcmd.cpp, README 地图 指挥 NPC): its keys are read only while the map is open (map.cpp
    Frame calls it after its open test, its one key reader is ReadKeys), it is off online, it is reset with the map; each AI
    module takes the command where it picks what it works round (the jets' anchor, the helis' post as HeliCalled writes it,
    the crawlers' leader); the box (Ctrl + left drag) never pans the map; its offline check (tools/map_cmd_check.cpp) is
    built and run by CTest; the README says the keys."""
    code, mapc, cmake, readme = src('src/mapcmd.cpp'), src('src/map.cpp'), src('CMakeLists.txt'), src('README.md')
    assert code.count('GetAsyncKeyState') == 1 and 'Down(VK_TAB)' in code.split('Keys ReadKeys(', 1)[1].split('\n}', 1)[0]
    frame = mapc.split('bool Frame(unsigned char* human)', 1)[1].split('\n}\n', 1)[0]
    assert frame.index('if(!game.open) {') < frame.index('MapCommandFrame(in,onto)'), 'the commands read keys only with the map open'
    # The box (Ctrl + left drag) never pans: the map's left drag gives way to it (the user, 2026-10-06: "操作 需要一个框选吧").
    assert 'if(Down(VK_LBUTTON) && !Down(VK_CONTROL) && !MapCommandBoxing() && !MapCommandPointerCaptured()){mapcam::Drag(v,dx,dy);' in mapc
    assert frame.index('MapCommandEats(front)') < frame.index('MapCommandFrame(in,onto)') < frame.index('Steer(human,dt,front') < frame.index('PumpPayloadUi(human)'), 'UI capture and mark edges precede camera drag; map requests pump while native inputs are held'
    assert 'Down(VK_RBUTTON) && !MapCommandPointerCaptured()' in mapc
    assert 'MapCommandView(vp,width,height);' in src('src/hud.cpp')
    assert 'ResetMapCommands();' in mapc.split('void ResetMap()', 1)[1].split('\n}', 1)[0]
    assert 'const bool allowed=Cfg().enabled;' in code, 'online requests must reach per-unit authority checks, not a global offline veto'
    assert 'src/mapcmd.cpp' in cmake and 'EXCLUDE_FROM_ALL tools/map_cmd_check.cpp' in cmake
    assert re.search(r'EDF6_OFFLINE_CHECKS[^)]*\bmap_cmd_check\b', cmake), 'map_cmd_check is not run by CTest'
    jet, heli, ground = src('src/jet.cpp'), src('src/heli.cpp'), src('src/ground.cpp')
    assert 'CommandAnchor(*j,follow,follow && !j->launched ? player.pos : j->anchor)' in jet
    assert 'const float* leader=r.cmd.order==Order::guard ? r.cmd.at : hasLeader ? player.pos : nullptr;' in ground
    cmd = heli.split('bool HeliCommand(const void* vehicle,const Command& c,const ObjRef& focus)', 1)[1].split('\n}\n', 1)[0]
    assert 'h->guard=true;' in cmd and 'h->orbitSet=false;' in cmd and 'h->guard=h->ownGuard;' in cmd
    for key in ('Ctrl', 'Shift', 'Tab', 'G', 'V', 'X', '联机指令', '框选'):
        assert key in readme, key
    assert '指挥 NPC' in readme


@test
def play_area_wired() -> None:
    """The player-flown aircraft keep inside the map's ground (src/playarea.h, docs/player-jet-re.md §3): the walls are the
    measured play area's, not the physics square's (crew.h PlayEdge, km out over the void on a stock map); the rotor craft
    are kept in too; it is measured every mission; a void within the walls is floored; the cockpit shows AREA; the offline
    check runs in CTest."""
    pj, board = src('src/playerjet.cpp'), src('src/playerjet_board.inc')
    wall = pj.split('int WallTurn(const float* pos,float* dir) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'MapPlayArea()' in wall and 'area::EdgeTurn' in wall and 'PlayEdge' not in wall, 'WallTurn takes the measured walls'
    assert 'j.area=WallTurn(pos,next);' in pj, 'a wing\'s path is bent off the walls (Air)'
    assert 'j.area=WallTurnVelocity(pos,want);' in board, 'a rotor craft\'s velocity is bent off the walls (HoverStep)'
    clear = pj.split('float Clear(const float* p,bool* water) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'area::FloorClear(MapPlayArea()' in clear, 'a void within the walls is floored'
    assert 'GuardedTick(kStepUnderground,&PlayAreaTick);' in src('src/crew.cpp'), 'measured once a mission'
    assert 'ResetPlayArea();' in src('src/mission.cpp'), 'measured again each mission'
    assert 'kWarnArea' in src('src/warn.h') and 'kWarnArea' in src('src/warn.cpp') and 'Tx::warnArea' in src('src/hud.cpp')
    cm = src('CMakeLists.txt')
    assert 'src/playarea.cpp' in cm and 'add_test(NAME play_area_check COMMAND play_area_check)' in cm


@test
def map_wired() -> None:
    """The map view (src/map.cpp, README 功能 17, docs/camera-re.md §8): its ini keys are read, range-checked, shipped and
    documented; the EDF.dll addresses it patches are the doc's, and with the game present its code signatures are the
    bytes EDF.dll has there; the shim the soldier pre-update jumps to branches where it means to (its jumps land on the
    no-pad jump, its absolute targets are where the code writes them); every key the plugin reads gives way to it; it
    is installed, reset with the mission, built, and its offline checks (tools/map_cam_check.cpp, hud_view's scenes)
    are wired."""
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/camera-re.md')
    code, cmake, mission, hud = src('src/map.cpp'), src('CMakeLists.txt'), src('src/mission.cpp'), src('src/hud.cpp')
    for key in ('Map', 'MapKey', 'MapButton', 'MapViewDistance'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in ('MapKey', 'MapButton', 'MapViewDistance'):
        assert f'FixInt("{key}"' in plugin or f'Fix("{key}"' in plugin, f'{key} is not range-checked'
    for name, rva in (('kHoldAt', '0x572F0C'), ('kHoldResume', '0x572F1C'), ('kNoPad', '0x573A4D'), ('kCamVtable', '0x1768C10'),
                      ('kCamStep', '0xF86A0'), ('kLookTo', '0x4E220'), ('kTeamWalk', '0x5E11D0'), ('kMarkerVtable', '0x17D4378'),
                      ('kMarkerDtor', '0x5B0410'), ('kMarkerUpdate', '0x5B2750'), ('kHostileWalk', '0x5E0F20'),
                      ('kOneTeamWalk', '0x5E0D60')):
        assert re.search(rf'\b{name}={rva}\b', code), (name, rva)
        assert rva in doc, rva
    for name, off in (('kCamTargetRef', '0x350'), ('kCamTarget', '0x360'), ('kCamMatrix', '0x220'), ('kHumanRecord', '0xD40'),
                      ('kRecordStride', '0xA80'), ('kMouseX', '0x66C'), ('kMouseY', '0x684'), ('kMarkerAt', '0x1F0')):
        assert re.search(rf'\b{name}={off}\b', code), (name, off)
        assert off in doc or off.lower() in doc, off

    # The shim: parse its bytes, put the three 8-byte values where the code copies them, follow its branches.
    body = re.search(r'unsigned char shim\[\]=\{(.*?)\};', code, re.S).group(1)
    shim = [int(b, 16) if b.startswith('0x') else int(b) for b in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', body)]
    copies = [int(o) for o in re.findall(r'std::memcpy\(shim\+(\d+),&(?:fn|resume|noPad),8\)', code)]
    jumps = [i for i in range(len(shim) - 1) if shim[i] == 0xFF and shim[i + 1] == 0x25]
    assert len(jumps) == 2 and copies == [9, jumps[0] + 6, jumps[1] + 6], (jumps, copies)
    assert shim[7:9] == [0x48, 0xB8] and shim[17:19] == [0xFF, 0xD0], 'mov rax,imm64; call rax'
    assert len(shim) == jumps[1] + 14
    branches = [i for i in range(len(shim) - 1) if shim[i] in (0x74, 0x75) and i in (32, 37)]
    assert branches == [32, 37], branches
    for at in branches:
        assert at + 2 + shim[at + 1] == jumps[1], ('a hold branch misses the no-pad jump', at, shim[at + 1])
    assert shim[23:30] == [0x48, 0x8B, 0x9E, 0x40, 0x03, 0x00, 0x00] and shim[0:3] == [0x48, 0x89, 0xF1]
    try:
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        listing = [(i.address, i.mnemonic, i.op_str) for i in md.disasm(bytes(shim[:jumps[0] + 6]), 0)]
        assert [m for _, m, _ in listing] == ['mov', 'sub', 'movabs', 'call', 'add', 'mov', 'test', 'jne', 'test', 'je', 'jmp'], listing
        assert all(int(o, 16) == jumps[1] for _, m, o in listing if m in ('jne', 'je')), listing
        assert listing[-1][2] == 'qword ptr [rip]', listing[-1]
    except ImportError:
        pass

    # With the game: the signatures are what EDF.dll has.
    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if os.path.exists(dll):
        import edfre
        consts = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'\b(k\w+)=(0x[0-9A-F]+)\b', code)}
        for arr, at in (('kHoldCode', consts['kHoldAt']), ('kNoPadCode', consts['kNoPad']), ('kCamStepCode', consts['kCamStep']),
                        ('kLookToCode', consts['kLookTo']), ('kLookToUse', 0xFC0D3), ('kTeamWalkCode', consts['kTeamWalk']),
                        ('kMarkerUpdateCode', consts['kMarkerUpdate']), ('kHostileWalkCode', consts['kHostileWalk']),
                        ('kRadarCall', 0x82B8C3), ('kOneTeamWalkCode', consts['kOneTeamWalk']),
                        ('kBoardTeam5Code', consts['kBoardTeam5Call'])):
            want = bytes(int(b, 16) for b in re.findall(r'0x[0-9A-F]+', re.search(rf'{arr}\[\]=\{{(.*?)\}};', code, re.S).group(1)))
            assert edfre.img[at:at + len(want)] == want, (arr, hex(at))

    # Closed by a key: the hold stays until every closing key is let go (the closing B is no seat switch, no stock B action).
    frame = code.split('bool Frame(unsigned char* human)', 1)[1].split('\n}\n', 1)[0]
    shut = frame.index('Close(close ? "Esc / B" : "the map key");')
    assert shut < frame.index('game.draining=true;holds.store(true);') < frame.index(
        'if(k.map || k.esc || k.padMap || k.padClose)return true;') < frame.index('if(!game.open) {'), 'map: the closing key drains'
    assert 'if(game.draining){game.draining=false;holds.store(false);}' in code.split('void Close(const char* why)', 1)[1].split('\n}', 1)[0]
    # Every key the plugin reads gives way to the map.
    for rel in ('src/heli.cpp', 'src/highcam.cpp', 'src/payload.cpp', 'src/playerjet.cpp', 'src/seatswitch.cpp', 'src/turretcam.cpp',
                'src/proteus.cpp', 'src/drill.cpp', 'src/sazabi.cpp'):
        assert 'if(vk<=0 || MapHoldsKeys())return false;' in src(rel), rel
    # NPC marking moved from a soldier's KeyHeld/MarkTick into the local player's frame. Track the held key while
    # hidden, but dispatch neither behind the map nor while its close/TV input hold is active.
    mark_frame = src('src/npcai.cpp').split('void NpcMarkFrame(unsigned char* human,bool mapOpen)', 1)[1].split('\n}\n', 1)[0]
    assert '!mapOpen && !MapHoldsKeys()' in mark_frame and 'mark.held=down;' in mark_frame
    assert 'NpcMarkFrame(human,open && game.open);' in code
    close = code.split('void Close(const char* why)', 1)[1].split('\n}', 1)[0]
    assert 'SuspendMapCommands();' in close, 'closing invalidates the hover/view without waiting for the stale timer'
    assert '!MapHoldsKeys() && GameInFront' in src('src/overlay.cpp')
    # ...and EDF6AutoTurret's keys too (its LockKey Q is the map's turn): through the link's export.
    held = re.search(r'kInputHeld\[\]="(\w+)"', src('common/edf/aimlink.h')).group(1)
    assert held.endswith('V1') and f'extern "C" __declspec(dllexport) bool __cdecl {held}() {{ return crew::MapHoldsKeys(); }}' in code
    design = src('autoturret/src/designate.cpp')
    assert 'link::Resolve(link::kCrewDll,link::kInputHeld,inputHeld,heldTried) && inputHeld()' in design
    assert 'if(vk<=0 || vk>0xFE || MapHolds())return false;' in design.split('bool KeyHeld(int vk)', 1)[1].split('\n}', 1)[0]
    at_readers = [f for f in os.listdir(os.path.join(ROOT, 'autoturret', 'src')) if 'GetAsyncKeyState' in src(f'autoturret/src/{f}')]
    assert at_readers == ['designate.cpp'], f'a new EDF6AutoTurret key reader: make it give way to the map ({at_readers})'
    readers = [f for f in os.listdir(os.path.join(ROOT, 'src')) if f.endswith('.cpp') and 'GetAsyncKeyState' in src(f'src/{f}')]
    assert sorted(readers) == sorted(['heli.cpp', 'highcam.cpp', 'payload.cpp', 'playerjet.cpp', 'seatswitch.cpp', 'turretcam.cpp',
                                      'overlay.cpp', 'map.cpp', 'mapcmd.cpp', 'proteus.cpp', 'npcai.cpp', 'drill.cpp', 'sazabi.cpp', 'sightzoom.cpp',
                                      'debug_spawn.cpp']), f'a new key reader: make it give way to the map ({readers})'
    assert 'InFront() && !MapHoldsKeys()' in src('src/debug_spawn.cpp'), 'the debug spawn keys give way to the map'

    assert 'InstallMap();' in plugin and 'ResetMap();' in mission and 'src/map.cpp' in cmake
    assert 'EXCLUDE_FROM_ALL tools/map_cam_check.cpp' in cmake and '#include "../src/map_cam.h"' in src('tools/map_cam_check.cpp')
    assert 'MapScreen(drawer,ctx,t,viewProj' in hud and '!MapOwnsView())KeepViewProj' in hud
    assert 'MapScene(dir,L"map_mid"' in src('tools/hud_view.cpp')
    # The friendly marks walk team 5 (nobody's vehicles: the parked aircraft, every empty vehicle) besides the friends'
    # walk, which never visits it (2026-10-06: aircraft missing from the map); classified by map_marks.h (checked offline).
    gather = code.split('void Gather(Game& g,const unsigned char* human)', 1)[1].split('\n}\n', 1)[0]
    assert 'reinterpret_cast<WalkFn>(image+kOneTeamWalk)(manager,mapmarks::kNobodysTeam,&w);' in gather, 'map: team 5 not walked'
    assert 'mapmarks::WalksFor(team)' in gather and 'mapmarks::FriendlyMark(seen,&kind,&flags)' in code
    assert 'EXCLUDE_FROM_ALL tools/map_marks_check.cpp' in cmake and 'map_marks_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    assert '0x5E0D60' in doc and 'kMapEmpty' in hud and 'MapAircraft(' in hud
    assert 'ViewMapClip(true,' in code and 'ViewMapClip(false,' in code
    # The enemies: every one the radar's hostile walk finds; the large ones pins by HP (kMapLargeEnemies), the small ones
    # dots by distance (kMapDots), the caps the README says; the
    # pin's height from map_cam.h (checked offline), drawn for every kind.
    mh = src('src/map.h')
    assert '0x82B8C3' in doc and 'HUiHudRader' in doc
    for name, value in (('kMapLargeEnemies', '64'), ('kMapDots', '1024')):
        assert re.search(rf'\b{name}={value}\b', mh) and name in readme and f'**{value}**' in readme and value in doc, name
    assert 'a.hpMax>b.hpMax' in code and 'a.d2<b.d2' in code and 'std::partition(foes' in code
    assert 'MapDot1(' in hud and 'kMapFlying' in hud and 'Dot(' in src('tools/hud_view.cpp')
    assert 'mapcam::PinHeight(' in hud and 'PinHeight(' in src('tools/map_cam_check.cpp')
    kinds = re.search(r'enum class MapKind : std::uint8_t \{(.*?)\};', src('src/map.h')).group(1).replace(' ', '').split(',')
    icon = hud[hud.index('void MapIcon('):hud.index('struct Pin {')]
    for kind in kinds:
        assert f'case MapKind::{kind}:' in icon, f'MapIcon draws no {kind}'


@test
def map_hides_stock_hud() -> None:
    """The map hides the stock HUD (docs/hud-re.md §11): through the game's own switch (the camera's +0x200, the mission
    scripts' SetPlayerHudShow), every EDF.dll byte it stands on checked at load (and, with the game present, the bytes
    EDF.dll has there); the switch is driven from the camera hook after its own step and fault handler, so a fault puts it
    back; the scripts' writes go through the record; the followers' bars (no reader of the switch) give way in the gauge
    hook; the plugin's HUD draws nothing while the map's view eases back; the offline checks are built and in CI."""
    code, h, hud, sub = src('src/map.cpp'), src('src/map_stock_hud.h'), src('src/hud.cpp'), src('src/subcarrier.cpp')
    doc, cmake = src('docs/hud-re.md'), src('CMakeLists.txt')
    for name, value in (('kHudShow', '0x118DF30'), ('kHudShowCall', '0x1BA811'), ('kCamHudShown', '0x200')):
        assert re.search(rf'\b{name}={value}\b', code), name
        assert value in doc, value
    sigs = re.search(r'const HudSig kHudSigs\[\]=\{(.*?)\};', code, re.S).group(1)
    arrays = re.findall(r'\{(k\w+|0x[0-9A-F]+),(k\w+),sizeof\(\2\)\}', sigs)
    assert len(arrays) == 7, arrays
    for at, _ in arrays:
        assert at == 'kHudShow' or at in doc, at
    install = code.split('bool InstallHudSwitch() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert install.index('Matches(s.rva,s.bytes,s.size)') < install.index('RedirectCall(image+kHudShowCall,image+kHudShow,')
    assert 'hudOk=holdOk && InstallHudSwitch();' in code and 'if(!hudOk)return;' in code
    step = code.split('void __fastcall CamStepHook(void* cam,void* step) {', 1)[1].split('\n}\n', 1)[0]
    assert step.index('__except(EXCEPTION_EXECUTE_HANDLER){camSide=CamSide{};') < step.index('StockHud(static_cast<unsigned char*>(cam),generation);')
    assert 'maphud::Step(hudRecord,cam,generation,hide,cam+kCamHudShown)' in code
    assert 'maphud::GameSet(hudRecord,cam,cameraSession.Generation(),show)' in code
    assert 'if(write)reinterpret_cast<HudShowFn>(image+kHudShow)(cam,show);' in code
    assert 'cameraSession.Reset();' in code.split('void ResetMap() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'maphud::Hides(hudRecord,camera,cameraSession.Generation())' in code
    assert 'MapHidesStockHud(At<const void*>(hud,0x18))' in sub
    assert 'if(!hide)draw(hud,viewProj,owner,r9,fifth);' in sub
    draw = hud.split('void HudDraw(const float* viewProj', 1)[1]
    assert draw.index('if(MapScreen(drawer,ctx,t,viewProj') < draw.index('if(MapOwnsView()){FreeText(text);return;}') < draw.index('CarrierBars(')
    assert 'inline bool Step(' in h and 'inline bool GameSet(' in h
    assert 'EXCLUDE_FROM_ALL tools/map_hud_check.cpp' in cmake and 'map_hud_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    assert '#include "../src/map_stock_hud.h"' in src('tools/map_hud_check.cpp')
    assert 'failed+=!MapDrawsMapAlone(' in src('tools/hud_view.cpp')

    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if os.path.exists(dll):
        import edfre
        consts = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'\b(k\w+)=(0x[0-9A-F]+)\b', code)}
        for at, arr in arrays:
            want = bytes(int(b, 16) for b in re.findall(r'0x[0-9A-F]+', re.search(rf'{arr}\[\]=\{{(.*?)\}};', code, re.S).group(1)))
            rva = consts[at] if at.startswith('k') else int(at, 16)
            assert edfre.img[rva:rva + len(want)] == want, (arr, hex(rva))
        call = edfre.img[consts['kHudShowCall']:consts['kHudShowCall'] + 5]
        assert call[0] == 0xE8 and consts['kHudShowCall'] + 5 + int.from_bytes(call[1:], 'little', signed=True) == consts['kHudShow']


@test
def game_clock_and_hud_stop_with_the_pause() -> None:
    """The pause menu (docs/hud-re.md §10): the game clock stops while the game's own pause flag says paused (the camera
    step still reads it every frame of the pause), and the HUD draws nothing then. GameMs runs game_clock.h, the rule
    tools/pause_clock_check.cpp checks; the pause flag is read from the System the pause menu sets, its code checked at
    load and named in the doc."""
    crew, hud, plugin, clock, doc = (src('src/crew.cpp'), src('src/hud.cpp'), src('src/plugin.cpp'), src('src/game_clock.h'),
                                     src('docs/hud-re.md'))
    assert '#include "game_clock.h"' in crew
    assert re.search(r'ULONGLONG GameMs\(\) noexcept \{ return gameclock::Read\(clock,GetTickCount64\(\),GamePaused\(\)\); \}', crew)
    assert 'if(c.wall && !paused)' in clock, 'game_clock.h: a paused read must not move the clock'
    assert 'CheckPauseFlag();' in plugin
    draw = hud[hud.index('void HudDraw('):]
    assert draw.index('if(GamePaused())return;') < draw.index('MapScreen('), 'HudDraw must stop before it draws anything'
    for rva in ('0x20B2958', '0xCD8', '0xCDC', '0x934A46', '0x934ED3', '0x1196FC0', '0x11990C', '0x119953B'):
        assert rva in doc, f'docs/hud-re.md §10 does not mention {rva}'
    cmake = src('CMakeLists.txt')
    assert 'add_executable(pause_clock_check EXCLUDE_FROM_ALL tools/pause_clock_check.cpp)' in cmake
    assert '#include "../src/game_clock.h"' in src('tools/pause_clock_check.cpp')


@test
def hud_switch_cues_wired() -> None:
    """The loadout strip (every store's picture, name and rounds; the picked one large for a moment after a switch) and
    EDF6AutoTurret's aim mode said as on / off with a banner on a flip (the user, 2026-10-06) are drawn where the stores
    and the mode line were, and their offline checks run (tools/hud_cue_check.cpp, hud_view's TurretLayoutApart)."""
    hud, cmake, view = src('src/hud.cpp'), src('CMakeLists.txt'), src('tools/hud_view.cpp')
    for call in ('CockpitStrip(drawer,ctx,t,width,height,s,snap.jet,storeSwitched,', 'JetCells(snap.jet,cells)',
                 'StockCells(snap.stockHud,cells)', 'snap.turretAim,aimFlipped,lines,&at,'):
        assert call in hud, call
    assert 'StoresText(stores,_countof(stores),j,false);' in hud and 'Tx::autoAimOn' in hud and 'Tx::autoAimOffCircle' in hud
    table = src('src/hudtext.inc')
    assert 'HUDTEXT(autoAimOn,L"AUTO-AIM ON",' in table and 'HUDTEXT(autoAimOffCircle,L"AUTO-AIM OFF' in table
    assert 'hudcue::StoreIconOf(j.storeName[i],j.storeRole[i])' in hud
    assert 'r.storeRole[i]=j.storeRole[i];' in src('src/playerjet.cpp')
    assert 'EXCLUDE_FROM_ALL tools/hud_cue_check.cpp' in cmake and 'hud_cue_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    assert 'failed+=!TurretLayoutApart(1920);' in view and 'Scene(dir,L"jet_switch"' in view


@test
def split_missile_wired() -> None:
    """MissileBullet02's split test (0x26CF00) is reached only through the flight state's call (0x26ED9C): the plugin
    redirects that call, shows the stock test the surface distance (split_fuse.h), defaults on, and CTest runs the
    simulated flight (docs/split-missile-re.md)."""
    sm = src('src/splitmissile.cpp')
    assert 'kSplitTest=0x26CF00,kSplitCall=0x26ED9C' in sm
    assert 'RedirectCall(image+kSplitCall,image+kSplitTest' in sm
    assert 'if(!moved)return nextSplit(round,frames);' in sm
    assert 'targetVtbl[kAddHitSlot]=reinterpret_cast<void*>(&TargetAddHit);' in sm
    assert 'InstallSplitMissiles();' in src('src/plugin.cpp')
    assert 'bool splitMissileSurface=true;' in src('src/crew.h')
    assert 'SplitMissileSurface=1' in src('EDF6VehicleCrew.ini')
    assert 'src/splitmissile.cpp' in src('CMakeLists.txt')
    assert 'add_test(NAME split_fuse COMMAND split_fuse_test)' in src('CMakeLists.txt')
    assert 'docs/split-missile-re.md' in sm and os.path.exists(os.path.join(ROOT, 'docs', 'split-missile-re.md'))


@test
def stock_guidance_wired() -> None:
    """Every stock homing round steers by PN at its own stock strength (guidance.cpp: the six steering calls of
    MissileBullet01/02 and HomingLaserBullet01 redirected, MissileBullet02's gate result kept); the lock code is one
    for every weapon (lockon.cpp, stores.cpp has none left); the Tempest's TV shares the map's one camera hook and one
    soldier hold; the settings default on and are documented; CTest flies the law (docs/guidance-re.md)."""
    g = src('src/guidance.cpp')
    for site, target in (('0x26AA85', '0x269AF0'), ('0x26AA76', '0x269EE0'), ('0x26ECCA', '0x26D4B0'),
                         ('0x26ECBE', '0x26D8D0'), ('0x250918', '0x24FA60'), ('0x25090B', '0x24FE80')):
        assert site in g and target in g, (site, target)
    assert 'RedirectCall(image+kind.site[t],image+kind.target[t],kHooks[k][t],changed)' in g
    assert 'return t>=At<std::uint32_t>(static_cast<unsigned char*>(b),kKinds[1].delay);' in g, 'MB02 keeps its gate result'
    assert 'own0*turn' in g, 'type 1: the stock turn at its speed'
    assert 'pn::Lateral(pos,vel,aim,tv,nav,accel,a);' in g, 'type 2: the stock thrust'
    assert 'At<std::uint32_t>(b,k.delay)==kNoStockHoming' in g, "the plugin's own missiles left to missile.cpp"
    assert 'pn::Lateral(' in src('src/missile.cpp'), 'one PN law'
    stores = src('src/stores.cpp')
    assert '0x691310' not in stores and '0x68FF60' not in stores, "the lock code is lockon.cpp's"
    assert 'InstallLockon();' in src('src/plugin.cpp') and 'InstallGuidance();' in src('src/plugin.cpp')
    m = src('src/map.cpp')
    assert 'return TvFrame(human,open && game.open,TvRead(human)) || open;' in m, 'one hold shim'
    assert 'TvView(&tvHuman,tvEye,tvLook)' in m, 'the TV through the map camera hook'
    assert 'kCamStep' not in src('src/tvguide.cpp'), 'one camera hook: the map\'s'
    assert 'holds.load(std::memory_order_relaxed) || TvHoldsKeys()' in m
    assert 'if(!TvSteer(static_cast<unsigned char*>(b)) && Cfg().enabled)Guide(' in src('src/missile.cpp')
    crew, vini, readme = src('src/crew.h'), src('EDF6VehicleCrew.ini'), src('README.md')
    for field, key in (('stockMissilePN', 'StockMissilePN'), ('playerLockByView', 'PlayerLockByView'),
                       ('tempestTv', 'TempestTv')):
        assert f'bool {field}=true;' in crew and re.search(rf'^{key}=1', vini, re.M) and key in readme, key
    tvg = src('src/tvguide.cpp')
    assert 'else if(in.fire && !tv.boost){tv.boost=true;' in tvg and 'DetonateRound' not in tvg, 'fire boosts, once'
    for key in ('StockMissileNav', 'TempestTvMouseSpeed', 'TempestTvBoost'):
        assert re.search(rf'^{key}=', vini, re.M) and key in readme, key
    cm = src('CMakeLists.txt')
    for f in ('src/guidance.cpp', 'src/lockon.cpp', 'src/tvguide.cpp'):
        assert f in cm, f
    assert 'add_test(NAME pn COMMAND pn_test)' in cm
    for doc in ('guidance-re.md', 'lockon-re.md', 'tvguide-re.md'):
        assert os.path.exists(os.path.join(ROOT, 'docs', doc)), doc


@test
def npc_pickup_wired() -> None:
    """The squad's box sweep (src/pickup.h, npcai.cpp SweepFrame / PickUp, docs/itembox-re.md): the run to a box
    after the lane move and before the combat spot; weapon / armour through the stock per-box Notify and Apply with the player as
    the one who picks, health boxes only offline, allowed and hurt; the code it calls checked at
    load; its ini keys read, range-checked, shipped and documented; pickup_check under CTest."""
    code, plugin, ini = src('src/npcai.cpp'), src('src/plugin.cpp'), src('EDF6VehicleCrew.ini')
    readme, doc, cmake = src('README.md'), src('docs/npc-ai-design.md'), src('CMakeLists.txt')
    drive = code.split('Plan Drive(Soldier& s,', 1)[1].split('\n}\n', 1)[0]
    run = drive.index('PickUp(s,h,pos)')
    assert drive.index('npc::LaneEscape(') < run < drive.index('Spot(s,pos,t.e->aim')
    pick = code.split('bool PickUp(Soldier& s,unsigned char* h,const float* pos) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'reinterpret_cast<NotifyBoxFn>' in pick and 'reinterpret_cast<ApplyBoxFn>' in pick
    assert pick.index('image+kNotifyBox') < pick.index('image+kApplyBox')
    assert 'npc::Dist(pos,at)>npc::pickup::kReach' in pick
    assert '!PickupHealth() || InSession() || !(At<float>(h,kHumanHp)<hpMax)' in pick
    assert 'healthPick<0 ? Cfg().npcPickupHealth' in code, 'the ini is the default until the map flips it'
    # The map: every command as a button (map_buttons.h), clicks tested against the rectangles drawn; Y and O keys.
    mapcmd, hud = src('src/mapcmd.cpp'), src('src/hud.cpp')
    assert 'mapbtn::Hit(v.button,v.buttons,x,y)' in mapcmd and 'MapCommandButtons(rects,ids,placed);' in hud
    assert 'SameUi(g.uiPress,UiAt(' in mapcmd and 'MapCommandUiPanels(mapUiPanels,mapUiPanelCount);' in hud
    assert 'PlayerSelectablePayload(&r)' in hud and 'MapCommandPayloadButtons(rects,r.selectionToken,r.seat,entries,hits);' in hud
    assert "k.sweep=Down('Y');k.health=Down('O');" in mapcmd and 'if(sweep)Sweep(g);' in mapcmd and 'if(health)Health(g);' in mapcmd
    assert 'EXCLUDE_FROM_ALL tools/map_buttons_check.cpp' in cmake and 'map_buttons_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    assert 'kNotifyBoxSig' in code and 'kApplyBoxSig' in code and 'InstallBoxes();' in code
    for key, default in (('NpcPickupKey', '89'), ('NpcPickupRange', '80'), ('NpcPickupSec', '90'), ('NpcPickupHealth', '0')):
        assert f'L"{key}"' in plugin, key
        assert re.search(rf'^{key}={default}\s*$', ini, re.M), key
        assert key in readme and key in doc, key
    for key in ('NpcPickupKey', 'NpcPickupRange', 'NpcPickupSec'):
        assert f'FixInt("{key}"' in plugin or f'Fix("{key}"' in plugin, key
    assert '0x2C8AC0' in src('docs/itembox-re.md')
    assert 'EXCLUDE_FROM_ALL tools/pickup_check.cpp' in cmake and 'pickup_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]


@test
def npc_formation_wired() -> None:
    """The squads' formations (src/formation.h, npcai.cpp FormationMove, docs/npc-ai-design.md §6.4): the formation move
    only when the soldier has nothing to fight (after the evade / board / fall-back / lane moves), its ini keys read,
    range-checked, shipped and documented, the map's T and the on-foot key wired, the HUD names every shape, and
    formation_check runs under CTest."""
    code, plugin, ini = src('src/npcai.cpp'), src('src/plugin.cpp'), src('EDF6VehicleCrew.ini')
    readme, doc, cmake = src('README.md'), src('docs/npc-ai-design.md'), src('CMakeLists.txt')
    drive = code.split('Plan Drive(Soldier& s,', 1)[1].split('\n}\n', 1)[0]
    form = drive.index('if(!t.e && FormationMove(')
    for before in ('Evade(s,h,c,pos,ms,&p.move)', 'Board(s,h,pos,ms)', 'FallBack(s,h,pos,served,ms)', 'npc::LaneEscape('):
        assert drive.index(before) < form, before
    for key, default in (('NpcFormation', '0'), ('NpcFormationKey', '84'), ('NpcFormationSpacing', '5'), ('NpcGuardFormation', '0')):
        assert f'L"{key}"' in plugin and (f'FixInt("{key}"' in plugin or f'Fix("{key}"' in plugin), key
        assert re.search(rf'^{key}={default}\s*$', ini, re.M), key
        assert key in readme and key in doc, key
    assert 'FormationTick();' in code and 'k.formation=Down(\'T\')' in src('src/mapcmd.cpp')
    table = src('src/hudtext.inc')
    shapes = re.findall(r'^\s*(\w+),\s*//', src('src/formation.h').split('enum class Shape', 1)[1].split('};', 1)[0], re.M)
    assert len(shapes) == 11, shapes
    for s in shapes:
        assert f'HUDTEXT(form{s[0].upper()}{s[1:]},' in table, s
    assert 'EXCLUDE_FROM_ALL tools/formation_check.cpp' in cmake and 'formation_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]


@test
def npc_ai_wired() -> None:
    """The friendly soldiers' own AI (src/npcai.cpp, docs/npc-ai-design.md): its Think hook runs the stock Think first and
    rewrites the intent block after it (§3.2), is installed with the inputs (after every plugin) and reset per mission;
    a script's unit (§4.3) keeps its stock moves (the scripted branch writes no move); only this machine's soldiers are
    driven; its ini keys are read, range-checked, shipped and documented; its offline check runs under CTest."""
    code, crew, mission, cmake = src('src/npcai.cpp'), src('src/crew.cpp'), src('src/mission.cpp'), src('CMakeLists.txt')
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/npc-ai-design.md')
    hook = code.split('void __fastcall ThinkHook(void* human,const float* dt)', 1)[1].split('\n}', 1)[0]
    assert hook.index('nextThink[I](human,dt);') < hook.index('Think(static_cast<unsigned char*>(human),I)'), 'stock Think first'
    assert '!Cfg().customNpcAi' in hook, 'CustomNpcAi=0 must leave every soldier stock'
    scripted = code.split('Plan Scripted(Soldier& s,', 1)[1].split('\n}\n', 1)[0]
    for write in ('Move(', 'MoveTo(', 'Look(', 'Stand(', 'kMoveX', 'kJumpPress'):
        assert write not in scripted, f"a scripted unit's moves are the stock AI's ({write})"
    veto = code.split('void Veto(unsigned char* h,const Enemy* t,const float* eye,const Arms& a) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'Veto(h,' in scripted and '=1' not in scripted and '=1' not in veto and 'h[kTrigger+k]=0' in veto, \
        'a scripted unit: the trigger (both hands) only taken off'
    assert 'k<kHands' in veto and 'a.held[k]' in veto and 'LargestBlast(a)' in veto, \
        "each hand vetoed with its own WeaponSet's weapon; the largest blast only for a hand whose weapon is unknown"
    assert 'LargestBlast(a),LongestReach(a)' not in code.replace(veto, ''), 'no caller vetoes both hands with the largest blast'
    think = code.split('void Think(unsigned char* h,int cls) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert '!IsOnlineAuthority(h)' in think and 'IsAnyPlayer(h)' in think, "only this machine's NPC soldiers (online_authority.h)"
    assert 'npc::Scripted(control) ? Scripted(' in think
    ensure = crew.split('void EnsureInputs() noexcept {', 1)[1].split('\n}', 1)[0]
    assert ensure.index('InstallInputs();') < ensure.index('InstallNpcAi();')
    assert 'ResetNpcAi();' in mission and 'src/npcai.cpp' in cmake
    assert 'EXCLUDE_FROM_ALL tools/npc_ai_check.cpp' in cmake and 'npc_ai_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    for key, default in (('CustomNpcAi', '1'), ('NpcFireLane', '1'), ('NpcLaneWidth', '2.5'), ('NpcLaneLength', '150'),
                         ('NpcFlankDeg', '45'), ('NpcWeaponSwitch', '1'), ('NpcEngageShare', '0.85'), ('NpcEvade', '1'),
                         ('NpcDangerRange', '15'), ('NpcGrabRange', '4'), ('NpcCrowd', '1.5'), ('NpcRollSec', '2.5'),
                         ('NpcRetreatHp', '0.3'), ('NpcLeash', '40')):
        assert f'L"{key}"' in plugin, key
        assert re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M), key
        assert key in readme and key in doc, key
    for key in ('NpcLaneWidth', 'NpcLaneLength', 'NpcFlankDeg', 'NpcEngageShare', 'NpcDangerRange', 'NpcGrabRange', 'NpcCrowd',
                'NpcRollSec', 'NpcRetreatHp', 'NpcLeash', 'TankPostHold', 'TankReverseMax'):
        assert f'Fix("{key}"' in plugin, f'{key} is range-checked'
    # The tanks' post (§8): seat 0's stick written before stock input; script routes relinquish our post, and only
    # the real driver's authority writes it (that may be a client). Keys stay shipped and documented.
    post = src('src/npcpost.cpp')
    hook = crew.split('template<int I> void __fastcall InputHook(', 1)[1].split('\n}', 1)[0]
    assert hook.index('Guarded(kStepNpcPost,&NpcPostInput,') < hook.index('nextInput[I](vehicle,hasInput,a3,a4);')
    body = post.split('void NpcPostInput(unsigned char* v) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(At<const void*>(v,kRoute)){DropPending(v);Relinquish(v);return;}' in body, \
        'a script route must discard the queued waypoint and relinquish the plugin post'
    assert 'if(!IsOnlineAuthority(v))return;' in body, 'the real driver owner, not a hard-coded host, controls the post'
    assert body.index('StockDriving(v,f,*p)') < body.index('Write(v,f,*p,c0,c1);')
    move_to = code.split('void MoveTo(', 1)[1].split('\n}\n', 1)[0]
    # Routed (2026-10-09): in kRouteHorizon legs, from the floor under the soldier (RouteStart), still stood while not moving.
    assert 'GroundNavigate(soldier->navigation,from,goal,stop,ms,waypoint,SoldierRoute())!=npc::navigation::Result::moving' in move_to
    assert move_to.index('RouteStart(pos,from);') < move_to.index('GroundNavigate('), 'the route starts from the floor'
    assert move_to.index('RouteStart(to,goal);') < move_to.index('GroundNavigate('), '...and ends on the floor under its goal'
    assert 'p.horizon=kRouteHorizon;' in code.split('npc::navigation::Profile SoldierRoute()', 1)[1].split('\n', 1)[0]
    assert move_to.index('{Stand(h);return;}') < move_to.index('Move(h,dir,'), 'blocked/pending routes wait instead of walking through walls'
    # Every family (2026-10-07): the mechs' turn-on-spot constant, the Barga by its stock walk, and the plugin's own
    # last write never read as the stock AI driving, taken back when the drive ends.
    assert 'f==Family::mech ? kMechTurnOnSpot : kTurnOnSpot' in body and '(image+kBargaWalk)(v,block,point,1.0f,' in body
    assert 'if(!s.active){TakeBack(v,f,*p);return;}' in body
    stock = post.split('bool StockDriving(unsigned char* v,Family f,const Post& p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'p.wrote && c0==p.last[0] && c1==p.last[1]' in stock
    assert 'kBargaWalkSig[]={0x40,0x53,' in post, 'the Barga walk prologue (push rbx with REX)'
    assert 'ResetNpcPosts();' in mission and 'src/npcpost.cpp' in cmake
    # The leader's death (§5.3): before the stock Think (whose code splits the squad), host only, through the stock
    # SetFollow and its replication slot.
    hook = code.split('void __fastcall ThinkHook(void* human,const float* dt)', 1)[1].split('\n}', 1)[0]
    assert hook.index('PreThink(static_cast<unsigned char*>(human))') < hook.index('nextThink[I](human,dt);')
    pre = code.split('void PreThink(unsigned char* h) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(!OnlineHostOnly())return;' in pre and 'kAutoResurrect' in pre
    follow = code.split('void Follow(unsigned char* h,unsigned char* leader) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'image+kSetFollow' in follow and 'vt[kSlotNetFollow]==image+kNetFollow' in follow
    for key, default in (('NpcSquadSuccession', '1'), ('NpcSquadMin', '2'), ('NpcSquadMax', '8'), ('NpcSquadJoinRange', '150')):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M) and key in readme and key in doc, key
    # The squads on the map (§5.4, §6): a script's squad takes no order; a dismissal clears +0x540 (or the stock takes the
    # squad back at once) and starts the cooldown, whose end puts +0x540 back; vehicles take only their three orders.
    cmd = code.split('NpcCommandResult NpcSquadCommandForRequester(', 1)[1].split('\n}\n', 1)[0]
    assert '!IsOnlineAuthority(top)' in cmd and 'root!=caller' in cmd and 'CommandActor(requester,true)' in cmd
    assert 'npc::Scripted(control)' in cmd.split('switch', 1)[0], "a script's squad takes no order"
    dismiss = cmd.split('case Order::dismiss:', 1)[1].split('break;', 1)[0]
    assert dismiss.index('top[kAutoFollow]=0;') < dismiss.index('Follow(top,nullptr);') < dismiss.index('cooldowns.Start(')
    see = code.split('Squad* SeeSquad(', 1)[1].split('\n}\n', 1)[0]
    assert 'cooldowns.Ready(SquadKey(top),ms)' in see and 'top[kAutoFollow]=q->autoFollow;' in see
    mapc = src('src/mapcmd.cpp')
    takes = mapc.split('bool Takes(const Entry& e,Order o) noexcept {', 1)[1].split('\n}', 1)[0]
    assert 'if(e.u.locked)return false;' in takes and 'mapcmd::VehicleOrder(o)' in takes
    assert 'if(!Takes(e,cmd.order))' in mapc and 'NpcCommandReason::unsupported' in mapc
    for key, default in (('NpcGuardRadius', '15'), ('NpcFreeRange', '120'), ('NpcRecruitCooldownSec', '60')):
        assert f'L"{key}"' in plugin and f'Fix("{key}"' in plugin and re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M), key
        assert key in readme and key in doc, key
    # The mark (§6.3): since 2026-10-10 it is the custom Q that replaces the stock spot (src/qmark.cpp, README「自制 Q 标记
    # 取代原版 Q」): read on foot and riding alike, with or without the custom NPC AI (npcmark::MarkOn: the plugin on and the
    # key set; the NPCs' priority on it stays the AI's, npcmark::Enabled), drawn by the HUD; the focus order needs it.
    tick = code.split('void NpcMarkFrame(unsigned char* human,bool mapOpen)', 1)[1].split('\n}\n', 1)[0]
    assert 'down && !mark.held && !mapOpen && !MapHoldsKeys() && npcmark::MarkOn()' in tick
    assert 'HumanOnFoot' not in tick and 'customNpcAi' not in tick, 'the custom Q marks riding too, without the NPC AI'
    assert 'KeepMark();' in tick and 'QMarkFrame();' in tick
    # A teammate's point mark is named (slot and name tag) as its enemy mark is: its marker is looked up for either.
    qframe = src('src/qmark.cpp').split('void QMarkFrame() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'inbox.Point(p,now)' in qframe and 't.slot<0' in qframe, "a teammate's point mark goes unnamed"
    assert re.search(r'inline bool MarkOn\(\) noexcept \{ return Cfg\(\)\.enabled && Cfg\(\)\.npcMarkKey>0; \}', src('src/npc_mark.h'))
    assert 'QMarkHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);' in src('src/hud.cpp')
    assert 'mapcmd::Decide(g.sel.n,p,allowed,point,pointOk,NpcMarked())' in mapc
    for key, default in (('NpcMarkKey', '81'), ('NpcMarkCone', '8')):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M) and key in readme and key in doc, key
    # Boarding (§7): real soldiers may drive seat 0 only through the verified native driver path; one strong reference
    # is taken before RideVehicle (callee-consumed). Existing riders/reservations and ownership stay protected.
    board = code.split('bool BoardSquad(', 1)[1].split('\n}\n', 1)[0]
    assert 'AssignBoard(v,m[i],ms)' in board, 'squad boarding uses the shared real-seat allocator'
    assign = code.split('bool AssignBoard(', 1)[1].split('\n}\n', 1)[0]
    assert 'for(unsigned k=0;' in assign and 'Reserved(v,k,ms,h)' in assign and '!SeatTakes(v,k,h)' in assign
    assert '!IsOnlineAuthority(h)' in assign and '!OnlineMaySeatNpc(v)' in assign
    takes_seat = code.split('bool SeatTakes(', 1)[1].split('\n}\n', 1)[0]
    assert 'SeatRider(seat)!=Rider::none' in takes_seat and '(i==0 && !RealDriverNativeReady())' in takes_seat
    ride = code.split('bool Board(Soldier& s,unsigned char* h,const float* pos,ULONGLONG ms) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert ride.index('s.boardSeat==0 && !PrepareNpcVehicle(v,false)') < ride.index('(image+kRideVehicle)(h,&ref,s.boardSeat)')
    assert ride.index('_InterlockedIncrement(') < ride.index('(image+kRideVehicle)(h,&ref,s.boardSeat)')
    off = code.split('bool DismountSquad(', 1)[1].split('\n}\n', 1)[0]
    assert 'At<const void*>(seat,kSeatRider)!=m[i]' in off and 'kSeatKick' in off
    gun = code.split('void NpcGunnersInput(unsigned char* v) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'vt[kSlotSeatFire]!=image+kSeatFire' in gun and 'for(unsigned i=1;' in gun and 'if(!AiGunner(v,seat))continue;' in gun
    who = code.split('bool AiGunner(const unsigned char* vehicle,const unsigned char* seat) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'IsSoldierClass(rider)' in who and '!IsAnyPlayer(rider)' in who and 'IsOnlineAuthority(rider)' in who, 'AiGunner: only local NPC soldiers'
    assert 'OnlineHostOnly()' in who and 'IsOnlineAuthority(vehicle)' in who, 'Dummy ownership follows registered host or copy owner'
    assert '|| InSession())return' not in who and '|| InSession() ||' not in gun, 'online NPC gunners are enabled'
    assert gun.index('ReleaseGunnerInputs(v)') < gun.index('if(!ok'), 'disable/ownership changes release our previous inputs'
    assert 'Cfg().customNpcAi' in who and 'Cfg().npcBoarding' in who, 'AiGunner: the soldiers still under NpcBoarding'
    inputs = crew.split('template<int I> void __fastcall InputHook(', 1)[1].split('\n}', 1)[0]
    assert inputs.index('Guarded(kStepNpcGunners,&NpcGunnersInput,') < inputs.index('nextInput[I](vehicle,hasInput,a3,a4);')
    assert f'L"NpcBoarding"' in plugin and re.search(r'^NpcBoarding=1\s*$', ini, re.M) and 'NpcBoarding' in readme and 'NpcBoarding' in doc
    # A script's squad let go (§4.4): released once by npc::Step after the settle time, recruitable only with
    # ScriptNpcRecruit and never while a dismissal's cooldown keeps +0x540 clear.
    assert 'npc::Step(q->script,held,ms,' in see and 'const bool held=Routed(top)' in see, 'released when its route / fixed position ends (not a direction order)'
    assert 'Cfg().scriptNpcRecruit && !q->dismissed && !top[kAutoFollow]' in see
    for key, default in (('ScriptNpcRecruit', '1'), ('ScriptNpcSettleSec', '5')):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M) and key in readme and key in doc, key
    for key, default in (('TankReturnToPost', '1'), ('TankPostHold', '6'), ('TankReverseMax', '30')):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M) and key in readme and key in doc, key


@test
def incremental_install_regressions() -> None:
    from test_installer_incremental import run_checks
    run_checks()


# The HUD's sources whose text the player reads: every word comes from src/hudtext.inc (hudtext.h Tr / Word).
HUD_TEXT_SOURCES = ('src/hud.cpp', 'src/mapcmd.cpp')
# What may stay in a wide literal of those: printf conversions, digits, punctuation, single letters (pad buttons A B X
# Y, L3 / R3, the RWR's J / M symbols, the g symbol G), the pad's two-letter buttons and the units.
HUD_LITERAL_WORDS = {'LB', 'RB', 'LT', 'RT', 'km'}
HUD_SPEC = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hs|ls|l?[dufxXsc]|%)')


def hud_literal_words(text: str) -> list[tuple[int, str]]:
    """The words (two letters or more, or any non-ASCII character) in a source's wide literals, with their lines."""
    found = []
    # `L"` opens a wide literal only as a token of its own: the L closing a narrow one ("DISMOUNT ALL") is no prefix.
    for m in re.finditer(r'(?<!\w)L"((?:[^"\\]|\\.)*)"', text):
        line = text.count('\n', 0, m.start()) + 1
        core = HUD_SPEC.sub('', m.group(1))
        found += [(line, w) for w in re.findall(r'[A-Za-z]{2,}', core) if w not in HUD_LITERAL_WORDS]
        found += [(line, ch) for ch in core if ord(ch) > 0x7E]
    return found


def hudtext_entries() -> list[tuple[str, list[str]]]:
    """src/hudtext.inc's texts: (key, [en, zh-CN, zh-TW, ja])."""
    table = re.sub(r'//[^\n]*', '', src('src/hudtext.inc'))
    out = []
    for m in re.finditer(r'HUDTEXT\((\w+),(.*?)\)\s*(?=HUDTEXT\(|\Z)', table, re.S):
        texts = [t.encode('utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')
                 for t in re.findall(r'L"((?:[^"\\]|\\.)*)"', m.group(2))]
        out.append((m.group(1), texts))
    return out


@test
def hud_text_localized() -> None:
    """The HUD's words in English, Simplified and Traditional Chinese and Japanese (src/hudtext.h, docs/hud-re.md §11):
    no English or CJK literal left in the HUD's sources (every text a key of the table), every key four texts, each
    language's characters its own script's (zh-CN in GB2312, zh-TW in Big5, ja in Shift JIS: a Traditional character in
    the Simplified text, or a Simplified one in the Traditional, is caught), the run-time identifiers the HUD shows (a
    round's class label, a jet's role, a carrier part) each a word of the table; the language follows the game's
    Option_Language (read signature-checked) and the ini's HudLanguage, which is read, shipped and documented; the
    offline checks (tools/hudtext_check.cpp, hud_view in every language) are CTests. With the game here, every character
    is in one of the game's four fonts (the font chain the game draws with: Root.cpk UI/*.TTF)."""
    for path in HUD_TEXT_SOURCES:
        left = hud_literal_words(src(path))
        assert not left, f'{path}: words outside src/hudtext.inc: {left[:12]}'
    entries = hudtext_entries()
    keys = [k for k, _ in entries]
    assert len(keys) == len(set(keys)) and len(keys) > 200, len(keys)
    for key, texts in entries:
        assert len(texts) == 4 and all(texts), (key, texts)
        en, zh_cn, zh_tw, ja = texts
        assert all(ord(c) < 0x7F for c in en), (key, en)
        for text, codec in ((zh_cn, 'gb2312'), (zh_tw, 'big5'), (ja, 'cp932')):
            for ch in text:
                if ord(ch) >= 0x2E80:
                    try:
                        ch.encode(codec)
                    except UnicodeEncodeError:
                        raise AssertionError(f'{key}: {ch!r} is not {codec} in {text!r}') from None
    words = set(re.findall(r'\{"([^"]+)",Tx::(\w+)\}', src('src/hudtext.h').split('kWords[]={', 1)[1].split('};', 1)[0]))
    ids = {w for w, _ in words}
    assert all(k in keys for _, k in words), words
    shown = set(re.findall(r'\{0x[0-9A-F]+,"[^"]+","(\w+)",Cls::', src('src/rounds.cpp')))
    shown |= set(re.findall(r'm\.label="(\w+)"', src('src/rounds.cpp') + src('src/vhud.cpp')))
    shown |= set(re.findall(r'strncpy_s\(a\.label,(?:m\.label \? m\.label : )?"(\w+)"', src('src/vhud.cpp')))
    shown |= set(re.findall(r'\{Role::\w+,"(\w+)"', src('src/jet_internal.h')))
    shown |= set(re.findall(r'\{kVt\w+,"(\w+)"', src('src/heli.cpp'))) - {'506', '409', '410'}
    shown |= set(re.findall(r'^\s+\{"(\w+)",\{', src('src/subcarrier.cpp'), re.M))
    shown |= set(re.findall(r'Kind\(d,"(\w+)"\)', src('src/hud.cpp')))
    shown |= set(re.findall(r'ReadCommandUnit\(\w+\.ref,"(\w+)"', src('src/ground.cpp')))
    assert {'GUN', 'WPN', 'ROCKETS', 'RKT', 'fighter', 'turretA', 'heli', 'CRAWLER', 'base'} <= shown, shown
    assert shown <= ids, f'shown on the HUD without a word: {sorted(shown - ids)}'
    hud, plugin, ini, readme = src('src/hud.cpp'), src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert 'kLangValue=0x20B2B30' in hud and 'CallsTo(0x963724,kLangGet) && CallsTo(0x96372E,kFontLoad)' in hud
    assert 'hudtext::Use(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));' in hud
    assert 'GetPrivateProfileStringW(L"VehicleCrew",L"HudLanguage"' in plugin and 'n.hudLanguage=ReadLanguage(' in plugin
    assert re.search(r'^HudLanguage=auto$', ini, re.M) and 'HudLanguage' in readme and '§11' in src('src/hudtext.h')
    cmake = src('CMakeLists.txt')
    assert 'EXCLUDE_FROM_ALL tools/hudtext_check.cpp' in cmake and 'hudtext_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1]
    assert 'failed+=Scenes(at);' in src('tools/hud_view.cpp')
    try:
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import hud_view
        fonts = hud_view.game_fonts()
    except Exception:  # noqa: BLE001 - no game here (CI): the fonts are not checked
        fonts = {}
    if fonts:
        from fontTools.ttLib import TTFont
        import io
        cmaps = [set(TTFont(io.BytesIO(data), lazy=True).getBestCmap()) for data in fonts.values()]
        missing = sorted({ch for _, texts in entries for t in texts for ch in t if not any(ord(ch) in c for c in cmaps)})
        assert not missing, f'characters in none of the game\'s fonts: {missing}'


@test
def soft_edge_wired() -> None:
    """The flyers' soft edge (src/airbound.h, the user 2026-10-06): the NPC jets' Guard turns them in by it (not the old
    world walls), their rotor goals and anchors are put inside it, their targets past it let be, the helis' wanted
    velocity is cut by it; its ini keys are read, range-checked, shipped and documented; its two offline tests are CTest
    tests; a blocked jet logs what it hit (src/impact.cpp, built)."""
    flight, jet, combat, heli = src('src/jet_flight.cpp'), src('src/jet.cpp'), src('src/jet_combat.cpp'), src('src/heli.cpp')
    guard = flight[flight.index('void Guard(Jet& j,'):flight.index('void ResetWalls()')]
    assert 'SoftEdge(j,pos,want);' in guard and 'WorldWalls' not in flight, 'Guard: the soft edge, not the world walls'
    # KeepIn every frame (its state follows the jet); only a gun dive at a ground point inside the soft box keeps its
    # line (KeepIn on a copy, 2026-10-09: bent by the edge the dive never came onto the lead).
    soft_edge = flight.split('void SoftEdge(Jet& j,const float* pos,float* want) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'airbound::KeepIn(soft,pos,j.m.vel,r,react,dive ? kept : want,&j.m.edgeBack,&j.m.edgeTurn);' in soft_edge
    assert 'const bool dive=j.mode==Mode::dive && j.t.target && !j.t.flyer && airbound::Depth(soft,j.t.aim)>=0.0f;' in soft_edge, \
        'only a dive at a ground point inside the soft box is spared the edge'
    assert 'airbound::CapClimb(' in flight
    hover = flight[flight.index('void Hover(Jet& j,'):]
    assert 'airbound::ClampIn(JetSoftBox(j),inside,0.0f);' in hover[:1500], 'Hover: the goal inside the soft edge'
    assert 'anchor=SoftAnchor(*j,anchor,anchorIn);' in jet
    # Targets past it let be; the map's focus target (2026-10-09) only out past the play area's walls, where no jet goes.
    visit = combat.split('void VisitTarget(void* ctx,const void* object,const float* p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(k.focus || PastEdge(*k.j,p))return;' in visit
    focus = visit.split('if(k.j->focus.Is(object)) {', 1)[1].split('\n    }\n', 1)[0]
    assert 0 <= focus.find('if(!airbound::Inside(PlayBox(),p))return;') < focus.find('k.focus=true;'), \
        'a focus target out past the play area is waited for, not chased'
    fly = heli[heli.index('void Fly(Heli& h,unsigned char* v,bool playerAboard)'):]
    assert 'SoftEdge(h,s,mode,w);' in fly[:600] and 'airbound::LimitOut(' in heli
    assert 'LogImpact("JET",v,pos,was);' in jet and 'LogImpact("PJET",v,pos,j.sent);' in src('src/playerjet.cpp')
    cm = src('CMakeLists.txt')
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/impact.cpp)' in cm
    assert 'add_test(NAME jet_edge_suite COMMAND jet_obstacle_sim --edge-suite' in cm and 'airbound_check)' in cm
    assert '#include "../src/airbound.h"' in src('tools/airbound_check.cpp')
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key, default in (('AirSoftEdge', '600'), ('AirSoftTurns', '1'), ('AirSoftCeil', '150'), ('HeliSoftEdge', '150')):
        assert f'L"{key}"' in plugin and f'Fix("{key}"' in plugin, key
        assert re.search(rf'^{key}={re.escape(default)}\s*$', ini, re.M) and key in readme, key

@test
def installer_recovery_regressions() -> None:
    from test_installer_recovery import run_checks
    run_checks()


# ---------------------------------------------------------------- the EDF5 campaign (tools/make_edf5_campaign.py)


def _e5c_stub() -> tuple[dict[str, bytes], object, dict]:
    """make_edf5_campaign.build on a stand-in game (no Root.cpk to read): a file each, the EDF5 packs' content from 3,
    the test range's after them."""
    return {rel: f'stub {rel}'.encode() for rel in e5c.FILES}, e5c.Contents(3, 6), {'rows': [], 'skipped': []}


def _e5c_have_game() -> bool:
    import rootcpk
    return os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk'))


def _e5c_real() -> tuple[object, set[str]]:
    import rootcpk
    game = rootcpk.Game(rootcpk.DEFAULT_GAME)
    return game, e5c.map_names(rootcpk.DEFAULT_GAME, game)


@contextlib.contextmanager
def _e5c_game() -> Iterator[tuple[str, object]]:
    """A stand-in game directory whose Root.cpk reads are the real game's (and the game counted as closed)."""
    game, maps = _e5c_real()
    with tempfile.TemporaryDirectory(prefix='edf6vc-e5c-') as root, \
            patched(e5c, map_names=lambda _root, _game: maps), \
            patched(e5c.rootcpk, Game=lambda _root: game), \
            patched(e5c.modfiles, refuse_while_running=lambda *a, **k: None):
        yield root, game


def _e5c_refuses(root: str, why: str) -> None:
    try:
        e5c.build(root)
    except e5c.Refused:
        return
    raise AssertionError(f'installed over {why}')


@test
def edf5_campaign_build() -> None:
    """The three packs from the real Root.cpk: the mode table gets three offline modes after the stock six (those
    untouched), each with a content id of its own after every stock one, its own save file and its own list / five
    texts / thumbnails, the texts one row per list row, every row 11 members with the 11th named flags (EDF.dll reads it
    by name), successors chained and in range; every language's text table names the three modes. EDF6's own offline
    list is not written. The test range's pack after them: edf5_campaign_range_pack."""
    import mdb
    import rootcpk
    import sgo
    if not _e5c_have_game():   # the real data: a developer's machine (CI has no game)
        return
    files, content, p = e5c.build(rootcpk.DEFAULT_GAME)
    assert not set(files) & set(e5c.LEGACY), 'EDF6\'s own offline list written'
    assert set(files) == set(e5c.FILES)
    stock = sgo.plain(sgo.read(rootcpk.default().read('DEFAULTPACKAGE', 'CONFIG.SGO'))[1]['ModeList'])
    modes = sgo.plain(sgo.read(files[e5c.CONFIG])[1]['ModeList'])
    assert modes[:len(stock)] == stock and len(modes) == len(stock) + 3 * 2 + 2, 'the stock modes changed'
    assert content == e5c.Contents(3, 6) and content.campaign == max(m[e5c.M_CONTENT] for m in stock) + 1
    packs = modes[len(stock):len(stock) + 6]   # each pack's offline mode, then its online one (more than one player)
    assert [m[e5c.M_CONTENT] for m in packs] == [3, 3, 4, 4, 5, 5]
    assert [(m[e5c.M_ONLINE], m[e5c.M_TYPE]) for m in packs] == [(0, 0), (1, 1)] * 3, 'not an offline and an online mode'
    assert len({m[e5c.M_MST].upper() for m in modes}) == len(stock) // 2 + 3 + 1, 'a pack\'s two modes on two saves'
    assert len({(m[e5c.M_TYPE], m[e5c.M_CONTENT]) for m in modes}) == len(modes), 'GetModeNo(type, content) ambiguous'
    for (off, on), pack in zip(zip(packs[0::2], packs[1::2]), e5c.PACKS):
        assert off[e5c.M_MST] == on[e5c.M_MST] == pack.mst
        for m, kind in ((off, e5c.OFFLINE), (on, e5c.ONLINE)):
            like = next(s for s in stock if s[e5c.M_CONTENT] == pack.like and s[e5c.M_TYPE] == m[e5c.M_TYPE])
            assert m[7] == like[7] and m[11:] == like[11:], 'the difficulty ranges are not the matching stock mode\'s'
            assert m[e5c.M_FILES] == pack.paths(kind) and (m[e5c.M_NAME], m[e5c.M_DESC]) == pack.keys(kind)
    for lang, rel in e5c.TEXTS.items():
        base = sgo.read(rootcpk.default().read('ETC', f'TEXTTABLE_STEAM.{lang}.TXT_SGO'))[1]
        text = sgo.read(files[rel])[1]
        assert {k: v for k, v in text.items() if k in base} == base, (rel, 'stock text changed')
        for pack in e5c.PACKS:
            for kind in pack.kinds:
                name_key, desc_key = pack.keys(kind)
                assert text[name_key] == pack.name[lang] and text[desc_key] == pack.desc[lang], rel
    counts = {pack.group: 0 for pack in e5c.PACKS}
    for pack in e5c.PACKS:
        assert pack.kinds == (e5c.OFFLINE, e5c.ONLINE)
        lists = [pack.kind_files(kind) for kind in pack.kinds]
        for listed, image, txt in lists:
            rows = dsgo.parse(files[listed]).root.get('table').items
            counts[pack.group] = len(rows)
            assert 0 < len(rows) <= 512
            for i, r in enumerate(rows):
                assert len(r.items) == 11 and r.names == {10: 'flags'} and r.items[0] == float(i), (pack.tag, i)
                assert r.items[3].items == ([float(i + 1)] if i + 1 < len(rows) else []), (pack.tag, i)
                assert r.items[1].startswith('app:/Mission/EDF5_OLD_SCRIPT/') and r.items[10] == 8.0
                rootcpk.default().read(r.items[1].split('app:/', 1)[1], 'MISSION.BVM')
            for rel in txt.values():
                assert len(sgo.read(files[rel])[1]['table']) == len(rows), rel
            names = [f.name for f in mdb.rab_read(files[image]).files]
            assert names == [e5c.thumb_name(r.items[2]) for r in rows], pack.tag
            assert dsgo.compact(dsgo.parse(files[listed])) == files[listed]
    assert counts == {'main': 110, 'dlc1': 11, 'dlc2': 14}, counts
    assert {x[1] for x in p['skipped']} == {'DLC/DM015', 'DLC/DM018', 'DLC/DM019', 'DLC/DM020'}, p['skipped']


@test
def edf5_campaign_install_remove() -> None:
    """Over another mod's mode table: install keeps what it replaced, a second install adds the packs once, removal
    puts the other mod's files back byte for byte (and deletes the ones that were not there), the ini content id
    follows; a file changed by someone since is left alone, and its manifest entry (what it replaced) with it."""
    import copy
    import sgo
    if not _e5c_have_game():
        return
    with _e5c_game() as (root, game):
        other = game.read('DEFAULTPACKAGE', 'CONFIG.SGO')
        ver, members = sgo.read(other)
        members['ModeList'].append(copy.deepcopy(members['ModeList'][2]))   # another mod's mode, content 1 again
        members['ModeList'][-1][e5c.M_CONTENT] = 7
        members['ModeList'][-1][e5c.M_MST] = 'MOD1.MST'
        other = sgo.write_depth_first(ver, members)
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), other)
        ini = os.path.join(root, 'Mods', e5c.INI)
        modfiles.atomic_write(ini, b'[VehicleCrew]\nEnabled=1\n')
        first = e5c.build(root)
        assert first[1] == e5c.Contents(8, 11), 'the packs\' content ids collide with the other mod\'s'
        e5c.install(root, first)
        assert 'EDF5CampaignContent=8' in open(ini, encoding='utf-8').read()
        assert 'TestRangeContent=11' in open(ini, encoding='utf-8').read()
        assert all('also' not in e for e in e5c.load_manifest(root)['files'].values()), 'the final manifest'
        assert e5c.check(root)
        again = e5c.build(root)
        assert again[0] == first[0], 'a second install added the packs again'
        e5c.install(root, again)
        done, kept = e5c.remove(root)
        assert not kept and len(done) == len(e5c.FILES)
        assert modfiles.read(e5c.rel_path(root, e5c.CONFIG)) == other, "the other mod's mode table not put back"
        assert all(modfiles.read(e5c.rel_path(root, rel)) is None for rel in e5c.FILES if rel != e5c.CONFIG)
        assert 'EDF5CampaignContent=0' in open(ini, encoding='utf-8').read()
        assert 'TestRangeContent=0' in open(ini, encoding='utf-8').read()
        assert not e5c.installed(root) and e5c.check(root), 'not installed is a valid state'
        e5c.install(root, e5c.build(root))
        image = e5c.PACKS[0].image
        modfiles.atomic_write(e5c.rel_path(root, image), b'someone else')
        assert not e5c.check(root)
        done, kept = e5c.remove(root)
        assert kept == [e5c.rel_path(root, image)] and modfiles.read(kept[0]) == b'someone else'
        assert list(e5c.load_manifest(root)['files']) == [image], 'the changed file\'s record dropped'
        os.remove(e5c.rel_path(root, image))
        os.remove(os.path.join(root, 'Mods', e5c.MANIFEST))
        # the mode table itself changed by someone: everything stays, and so does the plugin's content id
        e5c.install(root, e5c.build(root))
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), b'someone else')
        e5c.remove(root)
        assert 'EDF5CampaignContent=8' in open(ini, encoding='utf-8').read(), 'content id zeroed under a kept table'
        assert all(os.path.isfile(e5c.rel_path(root, rel)) for rel in e5c.FILES)


@test
def edf5_campaign_range_pack() -> None:
    """The test range's pack from the real Root.cpk (2026-10-09: the range over RM015 never ends, so the story stopped
    there): an offline and an online mode after the EDF5 packs, one content id and one save file for both (as the stock
    packs), each a copy of the stock story mode of its kind; one row each, row 0 (open from the start), no successor,
    naming the range's folder with RM015's row values (flags 8: the Air Raider's requests arrive; the ruined world's
    rows do not); texts and thumbnails per kind, the mode names in every text table. Without the campaign it is the
    only pack, at the same id (a room's mode is its content id); and the stock story's lists are never written."""
    import mdb
    import rootcpk
    import sgo
    if not _e5c_have_game():
        return
    game = rootcpk.default()
    stock = sgo.plain(sgo.read(game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))[1]['ModeList'])
    rm015 = next(r for r in dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
                 if r.items[2] == 'EDF6/RM015')
    for campaign, want in ((True, e5c.Contents(3, 6)), (False, e5c.Contents(0, 6))):
        files, content, _ = e5c.build(rootcpk.DEFAULT_GAME, campaign=campaign)
        assert content == want, (campaign, content)
        assert set(files) == {e5c.CONFIG, *e5c.TEXTS.values(),
                              *(f for pk in (*(e5c.PACKS if campaign else ()), e5c.RANGE) for f in pk.files())}
        modes = sgo.plain(sgo.read(files[e5c.CONFIG])[1]['ModeList'])
        assert modes[:len(stock)] == stock
        ours = modes[-2:]
        assert [(m[e5c.M_ONLINE], m[e5c.M_TYPE]) for m in ours] == [(0, 0), (1, 1)]
        assert {m[e5c.M_CONTENT] for m in ours} == {content.range} and {m[e5c.M_MST] for m in ours} == {e5c.RANGE.mst}
        assert len({(m[e5c.M_TYPE], m[e5c.M_CONTENT]) for m in modes}) == len(modes), 'GetModeNo(type, content) ambiguous'
        for m, kind in zip(ours, e5c.RANGE.kinds):
            like = next(x for x in stock if x[e5c.M_CONTENT] == 0 and x[e5c.M_TYPE] == m[e5c.M_TYPE])
            assert m[7] == like[7] and m[11:] == like[11:], 'not the stock story mode of its kind'
            assert m[e5c.M_FILES] == e5c.RANGE.paths(kind) and (m[e5c.M_NAME], m[e5c.M_DESC]) == e5c.RANGE.keys(kind)
            listed, image, txt = e5c.RANGE.kind_files(kind)
            rows = dsgo.parse(files[listed]).root.get('table').items
            assert len(rows) == 1 and rows[0].names == {10: 'flags'} and rows[0].items[3].items == []
            assert rows[0].items[:3] == [0.0, f'app:/Mission/EDF6/{e5c.RANGE_MISSION}', f'EDF6/{e5c.RANGE_MISSION}']
            assert rows[0].items[5:] == rm015.items[5:], 'not RM015\'s row values'
            for lang, rel in txt.items():
                assert sgo.read(files[rel])[1]['table'] == [[e5c.RANGE_ROW['title'][lang], e5c.RANGE_ROW['brief'][lang]]]
            assert [f.name for f in mdb.rab_read(files[image]).files] == [e5c.thumb_name(rows[0].items[2])]
        for lang, rel in e5c.TEXTS.items():
            text = sgo.read(files[rel])[1]
            for kind in e5c.RANGE.kinds:
                name_key, desc_key = e5c.RANGE.keys(kind)
                assert text[name_key] == e5c.RANGE.name[lang] and text[desc_key] == e5c.RANGE.desc[lang], rel
        assert not set(files) & set(e5c.LEGACY)
    try:
        e5c.build(rootcpk.DEFAULT_GAME, campaign=False, test_range=False)
    except ValueError:
        pass
    else:
        raise AssertionError('a build with no pack')


@test
def edf5_campaign_interrupted_reinstall() -> None:
    """An update whose files differ from the installed ones, cut short after the manifest: the next install neither
    refuses nor loses the other mod's mode table it replaced, and removal still puts that back (review of f8be221:
    the manifest held only the new hashes, so the old files read as someone else's)."""
    if not _e5c_have_game():
        return
    with _e5c_game() as (root, game):
        other = game.read('DEFAULTPACKAGE', 'CONFIG.SGO')
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), other)
        e5c.install(root, e5c.build(root))
        real_plan = e5c.plan
        shorter = lambda r: {**real_plan(r), 'rows': real_plan(r)['rows'][:-1]}   # an update that writes other bytes
        with patched(e5c, plan=shorter):
            update = e5c.build(root)
        with patched(e5c.modfiles, atomic_write=_failing_write(e5c.PACKS[0].txt['CN'])):
            try:
                e5c.install(root, update)
            except OSError:
                pass
            else:
                raise AssertionError('the failing write did not fail')
        files, content, _ = e5c.build(root)   # neither refused nor added to the half-written table
        assert content == e5c.Contents(3, 6) and files[e5c.CONFIG] == e5c.build(root)[0][e5c.CONFIG]
        done, kept = e5c.remove(root)
        assert not kept, kept
        assert modfiles.read(e5c.rel_path(root, e5c.CONFIG)) == other, "the other mod's mode table lost"


@test
def edf5_campaign_refusals() -> None:
    """A mode table that is not SGO, one that already has EDF5 pack modes this tool did not leave, one whose save files
    collide, and the older version's appended list changed by someone since all refuse (nothing written) instead of
    stopping the installer; an empty pack is not installed."""
    import base64
    import sgo
    if not _e5c_have_game():
        return
    with _e5c_game() as (root, game):
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), b'not a table')
        _e5c_refuses(root, 'a mode table that is not SGO')
        with tempfile.TemporaryDirectory(prefix='edf6vc-e5c-other-') as other:
            left = e5c.build(other)[0][e5c.CONFIG]   # as another tool might have left it, without a manifest
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), left)
        _e5c_refuses(root, 'EDF5 pack modes with no manifest')
        ver, members = sgo.read(game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))
        members['ModeList'][0][e5c.M_MST] = e5c.PACKS[1].mst
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), sgo.write_depth_first(ver, members))
        _e5c_refuses(root, 'a save file a pack would share')
        os.remove(e5c.rel_path(root, e5c.CONFIG))
        list_rel = e5c.LEGACY_LIST
        modfiles.atomic_write(e5c.rel_path(root, list_rel), b'appended')
        modfiles.save_json(os.path.join(root, 'Mods', e5c.MANIFEST), {'version': 1, 'rows': 147, 'files': {
            list_rel: {'sha': modfiles.sha256(b'appended'), 'original': base64.b64encode(b'stock').decode()}}})
        modfiles.atomic_write(e5c.rel_path(root, list_rel), b'changed since')
        _e5c_refuses(root, 'the older version\'s list changed by someone')
        os.remove(e5c.rel_path(root, list_rel))
        os.remove(os.path.join(root, 'Mods', e5c.MANIFEST))
        real_plan = e5c.plan
        with patched(e5c, plan=lambda r: {**real_plan(r), 'rows': [x for x in real_plan(r)['rows'] if x['group'] != 'dlc2']}):
            _e5c_refuses(root, 'an empty pack')
        assert not e5c.installed(root), 'a refusal wrote a manifest'


# Every call in EDF.dll to the owned-content lookup (0xD92B0) goes through the plugin (src/edf5campaign.cpp): one
# missed, a pack enabled in the dialog could be refused where that call asks.
_E5C_OWNED_SITES = [0x8BEF48, 0x8B5188, 0x8EC659, 0x8F64A1, 0x8FD963, 0x90069B, 0x91A655]


@test
def edf5_campaign_plugin_sites() -> None:
    """The plugin's call sites are each still a stock rel32 call to the owned-content lookup in EDF.dll, and they are
    every call to it."""
    import rootcpk
    code = src('src/edf5campaign.cpp')
    body = re.sub(r'//[^\n]*', '', code.split('kSites[]=', 1)[1].split('}', 1)[0])
    sites = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)', body)]
    assert sorted(sites) == sorted(_E5C_OWNED_SITES), [hex(x) for x in sites]
    if not _e5c_have_game():
        return
    import pefile
    pe = pefile.PE(os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll'), fast_load=True)
    img = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
    lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize

    def callers(target: int) -> set[int]:
        out = set()
        at = img.find(b'\xe8', lo)
        while 0 <= at < hi - 5:
            if at + 5 + int.from_bytes(img[at + 1:at + 5], 'little', signed=True) == target:
                out.add(at)
            at = img.find(b'\xe8', at + 1)
        return out
    assert callers(0xD92B0) == set(sites), sorted(hex(x) for x in callers(0xD92B0) ^ set(sites))


@test
def edf5_campaign_shipped() -> None:
    """Both the frozen installer and source-tools archive must carry the campaign's required text."""
    rel = src('tools/build_release.py')
    assert '"edf5campaign", "missions.json")}{seps}edf5campaign' in rel
    assert "os.path.join(sys._MEIPASS, 'edf5campaign', 'missions.json')" in src('tools/make_edf5_campaign.py')
    assert os.path.isfile(os.path.join(ROOT, 'edf5campaign', 'missions.json'))
    workflow = src('.github/workflows/build.yml')
    assert re.search(r'foreach \(\$f in git [^\n]*ls-files [^\n)]*\bedf5campaign\b', workflow), \
        'source-tools archive omits the campaign text required by make_edf5_campaign.TEXT'



def _utf(name: str, columns: list[tuple[str, str]], rows: list[dict]) -> bytes:
    """A minimal unmasked @UTF table: every column per-row (storage 0x50), kinds 'str' / 'I' / 'Q'."""
    import struct
    codes = {'I': 4, 'Q': 6, 'str': 0xA}
    strings = bytearray(b'<NULL>\0')
    at: dict[str, int] = {}

    def text(s: str) -> int:
        if s not in at:
            at[s] = len(strings)
            strings.extend(s.encode() + b'\0')
        return at[s]

    described = b''.join(struct.pack('>BI', 0x50 | codes[k], text(c)) for c, k in columns)
    packed = bytearray()
    for row in rows:
        for c, k in columns:
            packed += struct.pack('>I', text(row[c])) if k == 'str' else struct.pack('>' + k, row[c])
    width = len(packed) // max(len(rows), 1)
    rows_at = 0x18 + len(described)
    strings_at = rows_at + len(packed)
    name_at = text(name)
    body = struct.pack('>IIIIHHI', rows_at, strings_at, strings_at + len(strings), name_at, len(columns), width,
                       len(rows)) + described + bytes(packed) + bytes(strings)
    return b'@UTF' + struct.pack('>I', len(body)) + body


@test
def cpk_offsets_count_from_the_header_sector() -> None:
    """EDF5 / EDF4.1 (and EDF6's DX11.cpk) keep the table of contents at the end of the archive: file offsets still
    count from the end of the 0x800-byte header sector, never from TocOffset."""
    import struct
    import tempfile
    import cpk
    payload = b'SSA\0 fixture file'
    toc_at = cpk.HEADER_SECTOR + 0x40 + len(payload)
    header = _utf('CpkHeader', [('TocOffset', 'Q'), ('ContentOffset', 'Q')],
                  [{'TocOffset': toc_at, 'ContentOffset': cpk.HEADER_SECTOR + 0x40}])
    toc = _utf('CpkTocInfo', [('DirName', 'str'), ('FileName', 'str'), ('FileOffset', 'Q'), ('FileSize', 'I'),
                              ('ExtractSize', 'I')],
               [{'DirName': 'WEAPON', 'FileName': 'X.RAB', 'FileOffset': 0x40, 'FileSize': len(payload),
                 'ExtractSize': len(payload)}])
    blob = bytearray(b'@CPK' + bytes(4) + struct.pack('<Q', len(header)) + header)
    blob += bytes(cpk.HEADER_SECTOR + 0x40 - len(blob)) + payload
    assert len(blob) == toc_at
    blob += b'TOC ' + bytes(4) + struct.pack('<Q', len(toc)) + toc
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'Root.cpk')
        with open(path, 'wb') as f:
            f.write(blob)
        assert cpk.Cpk(path).read('WEAPON', 'X.RAB') == payload


@test
def cpk_reads_compressed_entries_of_every_installed_archive() -> None:
    """Real data where present: a compressed entry of each EDF6 / EDF5 / EDF4.1 archive decompresses. EDF6's DX11.cpk
    and every EDF5 / EDF4.1 archive failed with 'not CRILAYLA data' while the base was TocOffset."""
    import rootcpk
    steam = os.path.dirname(rootcpk.DEFAULT_GAME)
    games = [rootcpk.DEFAULT_GAME] + [os.path.join(steam, n) for n in ('EARTH DEFENSE FORCE 5', 'Earth Defense Force 4.1')]
    for root in games:
        for archive in ('Root.cpk', 'DX11.cpk'):
            if not os.path.isfile(os.path.join(root, archive)):
                continue
            game = rootcpk.Game(root, archive)
            entry = next(e for e in game.cpk.entries if int(e['ExtractSize']) != int(e['FileSize']))
            data = game.read(entry['DirName'], entry['FileName'])
            assert len(data) == int(entry['ExtractSize']), f'{root}/{archive} {entry["FileName"]}'


# ---------------------------------------------------------------- earlier games' weapons (tools/ported_weapons.py, pylib/edf5port.py)


# The categories EDF6's slots take (DEFAULTPACKAGE/CONFIG.SGO SoldierInit, docs/loadout-re.md §1.2).
EDF6_SLOT_CATEGORIES = ({*range(0, 11), 20, 21, 22, 23} | {*range(100, 107), *range(110, 117), 120}
                        | {*range(200, 210)} | {302, 303, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 320,
                                                330, 331, 332, 333, 334})


def _e5w_curves(v: object) -> list[list]:
    """The 7-element numeric lists in a text row (its stat curves)."""
    if isinstance(v, list):
        if len(v) == 7 and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v):
            return [v]
        return [c for x in v for c in _e5w_curves(x)]
    return []


@test
def edf5_weapons_registry() -> None:
    """edf5port/weapons.json: unique ids under EDF6VC_E5_ whose placeholders no call shares, every released order still
    a prefix (a removed id would leave its row nobody's), five languages with EDF6's 7-element curves, and only
    categories an EDF6 slot takes (EDF5's 304 / 107 / 108 have none)."""
    import hashlib
    import ported_weapons as pw
    assert len(set(pw.IDS)) == len(pw.IDS) and all(p.id.startswith(pw.BY_GAME[p.game].prefix) for p in pw.PORTS)
    calls_rows = set(calls.IDS) | {calls.retired_id(c) for c in calls.IDS}
    assert not (set(pw.IDS) | {pw.retired_id(x) for x in pw.IDS}) & calls_rows
    for name, (n, digest) in pw.RELEASED.items():
        assert len(pw.IDS) >= n and hashlib.sha256('\n'.join(pw.IDS[:n]).encode()).hexdigest() == digest, \
            f'{name}: the registry no longer starts with the ids that release installed (EDF5 weapons only go at the end)'
    assert max(n for n, _ in pw.RELEASED.values()) == len(pw.IDS), \
        'edf5port/weapons.json has weapons no RELEASED order lists: add this release to tools/ported_weapons.py RELEASED'
    for p in pw.PORTS:
        assert p.category in EDF6_SLOT_CATEGORIES, f'{p.id}: category {p.category} has no EDF6 slot'
        assert p.source in (p.game, 'edf6') and set(p.text) == set(cw.LANGS), p.id
        for lang, t in p.text.items():
            assert isinstance(t[0], str) and t[0] and isinstance(t[1], str), f'{p.id} {lang}'
            assert all(c[6] in (0.0, 1.0) for c in _e5w_curves(t[2])), f'{p.id} {lang}: a curve without its flag'


def _f32(x: float) -> object:
    import struct
    import sgo
    return sgo.Float(struct.pack('<f', x))


@test
def edf5_weapon_conversion() -> None:
    """pylib/edf5port.py on a hand-made EDF5 weapon: a curve gets a 7th element, 1.0 when its base was stored as a float
    and 0.0 when as an int; a 6-element list that is no curve (ShellCase's) stays 6 long; the curve inside
    EnergyChargeRequire is found; an empty SecondaryFire_Parameter becomes [0.0]; name.<lang> are the registry's; a raw
    block other than a MAB is refused. to_sub: Weapon_Sub with the template's named custom_parameter, EDF5's animation
    unless the template plays 'vehicle_call', EDF5's speed."""
    import edf5port
    members = {
        'xgs_scene_object_class': 'Weapon_BasicShoot',
        'AmmoCount': [15, 0, 0, 6, _f32(0.5), _f32(0.5)],
        'AmmoDamage': [_f32(150.0), 8, 1, 8, _f32(0.5), _f32(0.5)],
        'ShellCase': ['ShellCase_RB', 'app:/Weapon/ShellCase401.rab', [0, 'shell', _f32(0.22), _f32(1.15), _f32(1.0), _f32(5.0)]],
        'EnergyChargeRequire': [[_f32(100.0), 8, 1, 8, _f32(0.5), _f32(0.5)], 3],
        'SecondaryFire_Parameter': [],
        'custom_parameter': ['assault_recoil1', 0, 0, _f32(1.5)],
        'name.ja': 'old',
    }
    doc = edf5port.weapon(members, {'ja': 'JA', 'sc': 'SC'})
    r = doc.root
    assert dsgo.to_py(r.get('AmmoCount')) == [15.0, 0.0, 0.0, 6.0, 0.5, 0.5, 0.0]
    assert dsgo.to_py(r.get('AmmoDamage'))[6] == 1.0 and len(r.get('AmmoDamage').items) == 7
    assert len(r.get('ShellCase').items[2].items) == 6
    assert len(r.get('EnergyChargeRequire').items[0].items) == 7 and r.get('EnergyChargeRequire').items[1] == 3.0
    assert dsgo.to_py(r.get('SecondaryFire_Parameter')) == [0.0]
    assert (r.get('name.ja'), r.get('name.sc')) == ('JA', 'SC')
    named = lambda animation: dsgo.Node([dsgo.Node([animation, 1.0, 1.0, 1.0], {0: 'animation', 1: 'whole_body',   # noqa: E731
                                                                                2: 'animation_speed', 3: 'is_manual_reload'})],
                                        {0: 'custom_parameter'})
    call = edf5port.weapon(members, {})
    edf5port.to_sub(call, named('vehicle_call'))
    custom = dsgo.to_py(call.root.get('custom_parameter'))
    assert call.root.get('xgs_scene_object_class') == 'Weapon_Sub'
    assert custom == {'animation': 'vehicle_call', 'whole_body': 1.0, 'animation_speed': 1.5, 'is_manual_reload': 1.0}
    unit = edf5port.weapon(members, {})
    edf5port.to_sub(unit, named('mine_whole_recoil1'))
    assert dsgo.to_py(unit.root.get('custom_parameter'))['animation'] == 'assault_recoil1'
    try:
        edf5port.weapon({'animation_model': [['a', 'b'], 'c', b'XXXX' + bytes(12)]}, {})
    except edf5port.Unsupported:
        pass
    else:
        raise AssertionError('a raw block other than a MAB was carried over')
    stat = edf5port.text_value([['ROF', '$0', [60, 25, 4, 8, _f32(0.5), _f32(0.5)]], ['Damage', '$0', [_f32(2175.0), 8, 0, 8, _f32(0.5), _f32(0.5)]]])
    assert stat[0][2][6] == 0.0 and stat[1][2][6] == 1.0
    assert stat[0][2][4] == 0.5 and edf5port.text_value(_f32(0.05)) == float(_f32(0.05).value) != 0.05
    other = edf5port.weapon({'Ammo_CustomParameter': [[1, 2, 3, 4, 5, 6], 'x']}, {}).root.get('Ammo_CustomParameter')
    assert len(other.items[0].items) == 6, 'a 6-list outside CURVES got a flag'
    try:
        edf5port.weapon({'animation_model': [['a', 'b'], 'c', b'MAB\0' + bytes(28)]}, {})
    except edf5port.Unsupported:
        pass
    else:
        raise AssertionError('a MAB mab_legacy refuses was not Unsupported')


@test
def edf5_weapons_rows() -> None:
    """call_weapons with EDF5 weapons in the table: a fresh install appends every one of them (built or not: the rows
    are the same on every machine) after the calls, in the registry's order; a reinstall keeps each row or placeholder
    where it is; tail_start counts them as ours; retired_id picks each kind's."""
    import ported_weapons as pw
    fresh = cw.plan_rows(STOCK)
    assert fresh.appended == list(calls.IDS) + list(pw.IDS)
    assert [fresh.at[x] for x in fresh.appended] == list(range(len(STOCK), len(STOCK) + len(fresh.appended)))
    installed = STOCK + list(calls.IDS) + list(pw.IDS) + OTHER
    again = cw.plan_rows(installed)
    assert all(again.at[x] == installed.index(x) for x in pw.IDS) and again.appended == []
    pending = STOCK + list(calls.IDS) + [pw.retired_id(x) for x in pw.IDS] + OTHER
    assert all(cw.plan_rows(pending).at[x] == pending.index(pw.retired_id(x)) for x in pw.IDS)
    ports = list(pw.IDS[:3])
    assert cw.tail_start(STOCK + ports) == len(STOCK) and cw.tail_start(STOCK + ports + OTHER) == len(STOCK) + 3 + 2
    assert cw.retired_id(ports[0]) == pw.retired_id(ports[0]) != calls.retired_id(ports[0])
    assert cw.retired_id(calls.IDS[0]) == calls.retired_id(calls.IDS[0])


@test
def edf5_found_by_gamedir() -> None:
    """gamedir.find_other: $EDF5_DIR first, then the folder next to EDF6 (the same Steam library); a folder counts only
    with EDF5.exe and Root.cpk."""
    import gamedir
    folder, exe, var = gamedir.EDF5
    tmp = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    old = os.environ.get(var)
    try:
        edf6 = os.path.join(tmp, 'lib', 'EARTH DEFENSE FORCE 6')
        near = os.path.join(tmp, 'lib', folder)
        own = os.path.join(tmp, 'elsewhere')
        for d, files in ((edf6, ()), (near, (exe, 'Root.cpk')), (own, (exe, 'Root.cpk'))):
            os.makedirs(d)
            for n in files:
                open(os.path.join(d, n), 'wb').close()
        os.environ[var] = own
        assert gamedir.find_other(gamedir.EDF5, near=edf6) == os.path.normpath(own)
        os.environ[var] = os.path.join(tmp, 'missing')
        assert gamedir.find_other(gamedir.EDF5, near=edf6) == os.path.normpath(near)
        os.remove(os.path.join(near, 'Root.cpk'))
        found = gamedir.find_other(gamedir.EDF5, near=edf6)
        assert found != os.path.normpath(near), 'a folder without Root.cpk counted'
    finally:
        if old is None:
            os.environ.pop(var, None)
        else:
            os.environ[var] = old
        shutil.rmtree(tmp, ignore_errors=True)


@test
def weapon_table_fits_the_save() -> None:
    """stack refuses, before building anything, a table its rows would take past the 0x800 weapons a save keeps (the
    game writes past the save's block for every row beyond); a table exactly full is fine."""
    import types
    import ported_weapons as pw
    ours = len(cw.plan_rows([]).appended)
    full = [f'OTHER{i}' for i in range(cw.SAVE_WEAPONS - ours + 1)]

    def built(*_a: object) -> None:
        raise AssertionError('stack built the weapons before refusing the table')

    with patched(cw, load_shared=lambda game_root: types.SimpleNamespace(ids=full)), patched(pw, build=built):
        try:
            cw.stack('nowhere')
        except SystemExit as e:
            assert str(cw.SAVE_WEAPONS + 1) in str(e), e
        else:
            raise AssertionError('a table past the save was stacked')
    cw.check_room(cw.SAVE_WEAPONS)


def _e5w_games(test: str) -> tuple[str, str] | None:
    """(EDF6, EDF5) when both are installed (the real-data tests), else None, saying the test is skipped."""
    import gamedir
    import rootcpk
    edf6 = rootcpk.DEFAULT_GAME
    edf5 = gamedir.find_other(gamedir.EDF5, near=edf6)
    if edf5 and os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        return edf6, edf5
    print(f'skip  {test}: needs EDF6 and EDF5 installed')
    return None


@contextlib.contextmanager
def _stock_only_mods() -> Iterator[None]:
    """call_weapons reading the real game's archives but an empty Mods: the real-data tests start from the stock table
    whatever this machine has installed (an install of ours or another tool's would change what stack keeps)."""
    with tempfile.TemporaryDirectory(prefix='edf6vc-stock-mods-') as clean, \
            patched(cw, _mods=lambda game_root, *rel: os.path.join(clean, *[x for r in rel for x in r.split('/')])):
        yield


# The 'edf6' weapons' fields the developers changed beyond converting them (measured 2026-10-10): balance, and the MAB
# of the gunship requests. The two thrown Wing Diver weapons became Weapon_Subs with new physics, which edf5port does
# not do (Unsupported).
E5W_REBALANCED: dict[str, set[str] | None] = {
    'DLC_hSupport_Actuator.sgo': {'custom_parameter'},
    'eRequestAirStrike01D.sgo': {'ReloadTime', 'animation_model'},
    'eRequestAirStrike01.sgo': {'ReloadTime', 'animation_model'},
    'eWeapon016.sgo': {'ReloadTime', 'animation_model'},
    'DLC_Vehicle_Begaruta01.sgo': {'ReloadInit'},
    'DLC_Vehicle_Begaruta02.sgo': {'ReloadInit'},
    'DLC_pWeapon01.sgo': None,
    'DLC_pWeapon02.sgo': None,
}


@test
def edf5_weapons_match_developers() -> None:
    """Real data, where both games are installed: EDF5's copy of every weapon the developers converted themselves (the
    'edf6' weapons) run through pylib/edf5port.py equals EDF6's file field for field, MAB bytes included, but for the
    fields they rebalanced (E5W_REBALANCED); the Light Truck's whole Weapon_Sub conversion among them."""
    import dataclasses
    import edf5port
    import ported_weapons as pw
    import rootcpk
    games = _e5w_games('edf5_weapons_match_developers')
    if games is None:
        return
    g6 = rootcpk.Game(games[0])
    stock = lambda rel: g6.read(*rel.split('/'))   # noqa: E731
    checked = 0
    for p in pw.PORTS:
        if p.source != 'edf6':
            continue
        want = E5W_REBALANCED.get(p.sgo, set())
        try:
            ours = dsgo.parse(pw.build_sgo(dataclasses.replace(p, source='edf5'), stock, games[1])).root
        except edf5port.Unsupported:
            assert want is None, f'{p.sgo}: not converted'
            continue
        assert want is not None, f'{p.sgo} converted now: drop it from E5W_REBALANCED'
        theirs = dsgo.parse(stock(f'WEAPON/{p.sgo.upper()}')).root
        a = {ours.names[i]: dsgo.dump(v) for i, v in enumerate(ours.items) if not ours.names[i].startswith('name.')}
        b = {theirs.names[i]: dsgo.dump(v) for i, v in enumerate(theirs.items) if not theirs.names[i].startswith('name.')}
        differ = {k for k in set(a) | set(b) if a.get(k) != b.get(k)}
        assert differ == want, f'{p.sgo}: differs in {sorted(differ)}, expected {sorted(want)}'
        checked += 1
    assert checked >= 10


@test
def edf5_weapons_stack_real() -> None:
    """Real data: call_weapons.stack builds every EDF5 weapon; each row names its own SGO, with the registry's category,
    level and star caps, acquire 0, no EDF6 pack and its template's other columns; each SGO is a DSGO of the registry's
    class with no star curve left 6 long. Without EDF5 the table has the same rows at the same indices: the 'edf6'
    weapons built, the 'edf5' ones placeholders waiting for it, or, on a table an install with EDF5 wrote (its SGOs in
    Mods), those rows kept. retire on that table leaves every EDF5 weapon a placeholder."""
    import edf5port
    import ported_weapons as pw
    games = _e5w_games('edf5_weapons_stack_real')
    if games is None:
        return
    with _stock_only_mods():
        out = cw.stack(games[0])
    rows = {r.items[0]: r for r in dsgo.parse(out[cw.TABLE]).root.get('table').items}
    for p in pw.PORTS:
        row = rows[p.id]
        tpl = rows[p.template]
        assert row.items[1].lower() == f'app:/weapon/{p.id}.sgo'.lower(), p.id
        assert (row.items[2], row.items[3], row.items[4], row.items[5], row.items[8]) == \
               (float(p.category), 1.0, p.level, 0.0, 0.0), p.id
        assert dsgo.to_py(row.items[6]) == [float(x) for x in p.stars] and row.items[7] == tpl.items[7], p.id
        w = dsgo.parse(out[pw.sgo_file(p)]).root
        assert w.get('xgs_scene_object_class') == p.cls, p.id
        for k, at in edf5port.CURVES.items():
            v = w.get(k) if k in w.names.values() else None
            if at is not None and isinstance(v, dsgo.Node) and v.items:
                v = v.items[at]
            if isinstance(v, dsgo.Node) and all(isinstance(x, float) for x in v.items):
                assert len(v.items) != 6, f'{p.id} {k}: a star curve still 6 long'
    with_edf5 = cw.row_ids(out[cw.TABLE])
    with _stock_only_mods(), patched(pw, game_root=lambda game, edf6_root: None):   # no EDF5 / EDF4.1 here
        no_game = cw.stack(games[0])
    no_ids = cw.row_ids(no_game[cw.TABLE])
    assert len(no_ids) == len(with_edf5)
    for p in pw.PORTS:   # EDF6's own and the registry's converted (EDF5's) need no game; EDF4.1's wait
        i = with_edf5.index(p.id)
        alone = p.source == 'edf6' or p.weapon is not None
        assert no_ids[i] == (p.id if alone else pw.retired_id(p.id)), p.id
        assert not alone or no_game[pw.sgo_file(p)] == out[pw.sgo_file(p)], f'{p.id}: built without its game differs'
    # A weapon this machine cannot build (its game missing): the same rows, a placeholder for it.
    real_build = pw.build

    def edf6_only(game_root: str, stock):  # noqa: ANN001, ANN202
        built, why = real_build(game_root, stock)
        cut = {k for k in built if pw.BY_ID[k].source != 'edf6'}
        return {k: v for k, v in built.items() if k not in cut}, {**why, **{k: 'Unavailable: test' for k in cut}}

    with _stock_only_mods(), patched(pw, build=edf6_only):
        bare = cw.stack(games[0])
    without = cw.row_ids(bare[cw.TABLE])
    assert len(without) == len(with_edf5)
    for p in pw.PORTS:
        i = with_edf5.index(p.id)
        assert without[i] == (p.id if p.source == 'edf6' else pw.retired_id(p.id)), p.id
        assert (pw.sgo_file(p) in bare) == (p.source == 'edf6'), p.id
        assert dsgo.parse(bare[cw.TABLE]).root.get('table').items[i].items[5] == pw.ACQUIRE, \
            f'{p.id}: a placeholder obtained otherwise than the weapon (its template a starting or DLC weapon)'
    pending = next(p for p in pw.PORTS if p.source == 'edf5')
    text = dsgo.parse(bare['WEAPON/WEAPONTEXT.EN.SGO']).root.get('text_table').items[with_edf5.index(pending.id)]
    assert text.items[0] == pending.text['EN'][0] + pw.PENDING_NOTE['EN'][0].format(game=pw.BY_GAME[pending.game].name)
    orig_base, orig_mods = cw.base, cw._mods
    with tempfile.TemporaryDirectory(prefix='edf6vc-e5w-') as mods:
        for p in pw.PORTS:
            modfiles.atomic_write(os.path.join(mods, *pw.sgo_file(p).split('/')), out[pw.sgo_file(p)])
        installed = lambda game_root, rel: out[rel] if rel in cw.SHARED else orig_base(game_root, rel)   # noqa: E731
        ours = lambda game_root, *rel: os.path.join(mods, *[x for r in rel for x in r.split('/')])   # noqa: E731
        with patched(cw, base=installed, _mods=ours), patched(pw, build=edf6_only):
            kept = cw.stack(games[0])
            retired, _ = cw.retire(games[0], False)
    assert cw.row_ids(kept[cw.TABLE]) == with_edf5, 'a built row an earlier install wrote was not kept'
    assert not any(pw.sgo_file(p) in kept for p in pw.PORTS if p.source == 'edf5'), 'a kept SGO was rewritten'
    have = cw.row_ids(retired[cw.TABLE])
    assert all(have[with_edf5.index(p.id)] == pw.retired_id(p.id) for p in pw.PORTS)


@test
def edf5_weapons_retire_and_uninstall() -> None:
    """On a stand-in game whose Mods table holds the calls and three EDF5 weapons: retire turns each EDF5 weapon's row
    into its template's stock row under the placeholder id (text: its name marked uninstalled), deleting only the run
    of ours ending the table when asked; uninstall removes the EDF5 weapons' SGOs with the calls'."""
    import ported_weapons as pw
    ports = list(pw.PORTS[:3])
    templates = sorted({*cw.templates(), *(p.template for p in ports)})
    ids = STOCK + [t for t in templates if t not in STOCK] + list(calls.IDS) + [p.id for p in ports]

    def table(key: str, rows: list[str]) -> bytes:
        def row(i: str) -> dsgo.Node:
            # the 9 columns; every template a new save's starting weapon (acquire 1), as AssultRifle01 is
            acquire = 1.0 if i in templates else 0.0
            return dsgo.Node([i, f'app:/weapon/{i}.sgo', 0.0, 1.0, 0.0, acquire, dsgo.Node([]), 1.0, 0.0]
                             if key == 'table' else [f'name {i}', f'about {i}'])
        return dsgo.compact(dsgo.Document(dsgo.Node([dsgo.Node([row(i) for i in rows])], {0: key}), []))

    with tempfile.TemporaryDirectory(prefix='edf6vc-e5w-') as game, \
            patched(modfiles, game_running=lambda process=modfiles.PROCESS: False):
        modfiles.atomic_write(_mods(game, cw.TABLE), table('table', ids))
        for rel in cw.TEXTS:
            modfiles.atomic_write(_mods(game, rel), table('text_table', ids))
        for rel in [cw.sgo_file(c) for c in calls.CALLS] + [pw.sgo_file(p) for p in pw.PORTS]:
            modfiles.atomic_write(_mods(game, rel), b'ours')
        out, deleted = cw.retire(game, False)
        assert deleted == []
        rows = dsgo.parse(out[cw.TABLE]).root.get('table').items
        texts = dsgo.parse(out['WEAPON/WEAPONTEXT.EN.SGO']).root.get('text_table').items
        for p in ports:
            i = ids.index(p.id)
            assert rows[i].items[:2] == [pw.retired_id(p.id), f'app:/weapon/{p.template}.sgo'], p.id
            assert rows[i].items[5] == pw.ACQUIRE, f'{p.id}: the placeholder took its template\'s acquire'
            assert texts[i].items[0] == p.text['EN'][0] + calls.RETIRED_NOTE['EN'][0], p.id
        assert [r.items[0] for r in rows[:len(STOCK)]] == STOCK
        _, deleted = cw.retire(game, True)
        assert deleted == list(calls.IDS) + [p.id for p in ports], 'the run of ours ending the table'
        cw.uninstall(game)
        assert not any(os.path.isfile(_mods(game, pw.sgo_file(p))) for p in pw.PORTS)
        assert not any(os.path.isfile(_mods(game, cw.sgo_file(c))) for c in calls.CALLS)
        left = [r.items[0] for r in dsgo.parse(modfiles.read(_mods(game, cw.TABLE))).root.get('table').items]
        assert all(pw.retired_id(p.id) in left for p in ports) and len(left) == len(ids)
        # Installing again where these cannot be built (no EDF5): their placeholders stay, recorded (once a KeyError
        # after the commit, the manifest knowing only live rows).
        files = _call_files(game, left)
        cw.install(game, files)
        recorded = cw.load_manifest(game)['rows']
        assert all(recorded[p.id] == left.index(pw.retired_id(p.id)) for p in ports)


@test
def edf41_weapon_conversion() -> None:
    """pylib/edf5port.py weapon41 on a hand-made EDF4.1 weapon: 'name' and 'Range' gone, name.<lang> the registry's,
    AmmoDamageReduce [1, 1] and ExtPrams [1] added (kept when the weapon has its own), plain numbers stay plain (4.1
    has no star curves), an empty SecondaryFire_Parameter becomes [0.0]."""
    import edf5port
    members = {'xgs_scene_object_class': 'Weapon_BasicShoot', 'name': ['ＡＦ', 'AF', 'ＡＦ'], 'Range': _f32(120.0),
               'AmmoDamage': _f32(6.0), 'AmmoCount': 200, 'ReloadTime': 150, 'SecondaryFire_Parameter': [],
               'ExtPrams': [_f32(2.0)]}
    r = edf5port.weapon41(members, {'ja': 'JA', 'en': 'EN'}).root
    names = set(r.names.values())
    assert 'name' not in names and 'Range' not in names
    assert (r.get('name.ja'), r.get('name.en')) == ('JA', 'EN')
    assert dsgo.to_py(r.get('AmmoDamageReduce')) == [1.0, 1.0] and dsgo.to_py(r.get('ExtPrams')) == [2.0]
    assert (r.get('AmmoDamage'), r.get('AmmoCount'), r.get('ReloadTime')) == (6.0, 200.0, 150.0)
    assert dsgo.to_py(r.get('SecondaryFire_Parameter')) == [0.0]
    assert dsgo.to_py(edf5port.weapon41({'xgs_scene_object_class': 'x'}, {}).root.get('ExtPrams')) == [1.0]
    energy = edf5port.weapon41({'EnergyChargeRequire': 25}, {}).root.get('EnergyChargeRequire')
    assert dsgo.to_py(energy) == [25.0, 25.0], 'EDF6 holds EnergyChargeRequire as [curve, value]'


@test
def edf41_stat_lines() -> None:
    """tools/make_edf41_weapons.py: a 4.1 stat line through EDF6's forms (its numbers put into each language's format),
    two stats on one line, a part without a colon going on the value before it, a language-neutral value, a value put
    together from EDF6's pieces, LABELS41's label, and None for a label nobody shows."""
    import make_edf41_weapons as m41
    form = lambda label, values: {L: (label[L], values[L]) for L in m41.LANGS}   # noqa: E731
    rof = form({'JA': '連射速度', 'EN': 'ROF', 'CN': '連射速度', 'SC': '连射速度', 'KR': '연사 속도'},
               {'JA': '$0発／秒', 'EN': '$0/sec', 'CN': '$0發/秒', 'SC': '$0发/秒', 'KR': '$0발/초'})
    dmg = form({'JA': 'ダメージ', 'EN': 'Damage', 'CN': '傷害', 'SC': '伤害', 'KR': '대미지'},
               {'JA': '$0×$1', 'EN': '$0×$1', 'CN': '$0×$1', 'SC': '$0×$1', 'KR': '$0×$1'})
    acc = form({'JA': '精度', 'EN': 'Accuracy', 'CN': '準度', 'SC': '准度', 'KR': '정확도'},
               {L: '$0' for L in m41.LANGS})
    formats = {'連射速度': [rof], 'ダメージ': [dmg], '精度': [acc]}
    pieces = m41.fragments(formats)
    one = m41.stat_lines('連射速度：12.0発／秒', formats, pieces)
    assert one == [{'JA': ['連射速度', '12.0発／秒'], 'EN': ['ROF', '12.0/sec'], 'CN': ['連射速度', '12.0發/秒'],
                    'SC': ['连射速度', '12.0发/秒'], 'KR': ['연사 속도', '12.0발/초']}], one
    two = m41.stat_lines('ダメージ：60.0    ×30', formats, pieces)
    assert two and two[0]['EN'] == ['Damage', '60.0×30'], two
    pair = m41.stat_lines('精度：S+     連射速度：60.0発／秒×900', formats, pieces)
    assert pair and pair[0]['CN'] == ['準度', 'S+'] and pair[1]['KR'] == ['연사 속도', '60.0발/초×900'], pair
    supply = m41.stat_lines('供給量：0.05', formats, pieces)
    assert supply and supply[0]['EN'] == ['Supply Rate', '0.05'], supply
    assert m41.stat_lines('謎の値：12', formats, pieces) is None
    generic = m41._generalize({'JA': ('効果時間', '90.0秒'), 'EN': ('Effective Time', '90.0 sec'),
                               'CN': ('效果時間', '90.0秒'), 'SC': ('效果时间', '90.0秒'), 'KR': ('효과 시간', '90.0초')})
    assert generic['EN'] == ('Effective Time', '$0 sec') and generic['JA'] == ('効果時間', '$0秒'), generic


@test
def edf41_weapons_convert_real() -> None:
    """Real data, where EDF4.1 and EDF6 are installed: every EDF4.1 weapon of the registry converts, of its class, with
    every field nearly every EDF6 weapon has (EDF6 reads those; 4.1 lacks AmmoDamageReduce / ExtPrams / name.<lang>),
    its swapped cues all in EDF6's banks and no cue left that EDF6's banks lack."""
    import acb
    import gamedir
    import ported_weapons as pw
    import rootcpk
    edf6 = rootcpk.DEFAULT_GAME
    edf41 = gamedir.find_other(gamedir.EDF41, near=edf6)
    if not edf41 or not os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        print('skip  edf41_weapons_convert_real: needs EDF6 and EDF4.1 installed')
        return
    g6 = rootcpk.Game(edf6)
    stock = lambda rel: g6.read(*rel.split('/'))   # noqa: E731
    rows = dsgo.parse(stock('WEAPON/WEAPONTABLE.SGO')).root.get('table').items
    count: dict[str, int] = {}
    for r in rows[::7]:
        for k in dsgo.parse(stock(f'WEAPON/{r.items[0].upper()}.SGO')).root.names.values():
            count[k] = count.get(k, 0) + 1
    common = {k for k, n in count.items() if n >= 0.95 * len(rows[::7])}
    cues6 = acb.game_cues(edf6)
    cues41 = acb.game_cues(edf41)
    ports = [p for p in pw.PORTS if p.game == 'edf41']
    assert ports
    for p in ports:
        r = dsgo.parse(pw.build_sgo(p, stock, edf41)).root
        assert r.get('xgs_scene_object_class') == p.cls, p.id
        lacking = common - set(r.names.values())
        assert not lacking, f'{p.id}: lacks {sorted(lacking)}'
        assert all(c in cues6 for c in p.cues.values()), p.id
        strings = [x for x in _dsgo_strings(r) if x in cues41]
        assert all(x in cues6 for x in strings), f'{p.id}: {sorted(set(strings) - cues6)} play nothing in EDF6'


def _dsgo_strings(v: dsgo.Value) -> list[str]:
    if isinstance(v, str):
        return [v]
    if isinstance(v, dsgo.Node):
        return [x for c in v.items for x in _dsgo_strings(c)]
    return []


@test
def utf_table_version_field() -> None:
    """pylib/cpk.py parse_utf: an @UTF table starts u16 version, u16 rows offset. A CPK's are version 0 (so reading the
    two as one u32 worked); an ACB's (pylib/acb.py, the sound banks) are version 1."""
    import cpk
    table = bytearray(_utf('Bank', [('CueName', 'str')], [{'CueName': 'weapon_fire'}]))
    table[8:10] = (1).to_bytes(2, 'big')
    _name, _cols, count, rows = cpk.parse_utf(bytes(table))
    assert count == 1 and next(rows())['CueName'] == 'weapon_fire'


# EDF4.1 weapons EDF6 ships converted by its developers (never in its table): 4.1 file -> EDF6 file.
E41_DEVELOPER_COPIES = {'hHellStorm01.sgo': 'HHELLSTORM01.SGO', 'Weapon457.sgo': 'HWEAPON112.SGO',
                        'Weapon462.sgo': 'HWEAPON118.SGO', 'Weapon466.sgo': 'HWEAPON123.SGO',
                        'Weapon470.sgo': 'HWEAPON127.SGO'}


@test
def edf41_weapons_match_developers() -> None:
    """Real data, where EDF4.1 and EDF6 are installed: pylib/edf5port.py weapon41 on the 4.1 weapons the developers
    converted themselves gives their files' fields exactly (name / Range gone, AmmoDamageReduce / ExtPrams added), and
    each field's shape theirs where they kept 4.1's numbers plain (EnergyChargeRequire [v, v], not 4.1's one number);
    the rest they turned into star curves or rebalanced."""
    import edf5port
    import gamedir
    import rootcpk
    import sgo
    edf6 = rootcpk.DEFAULT_GAME
    edf41 = gamedir.find_other(gamedir.EDF41, near=edf6)
    if not edf41 or not os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        print('skip  edf41_weapons_match_developers: needs EDF6 and EDF4.1 installed')
        return
    g4, g6 = rootcpk.Game(edf41), rootcpk.Game(edf6)
    for mine, theirs in E41_DEVELOPER_COPIES.items():
        a = edf5port.weapon41(sgo.read(g4.read('WEAPON', mine))[1], {}).root
        b = dsgo.parse(g6.read('WEAPON', theirs)).root
        names = lambda r: {k for k in r.names.values() if not k.startswith('name.')}   # noqa: E731
        assert names(a) == names(b), f'{mine}: {sorted(names(a) ^ names(b))}'
        for k in names(a):
            va, vb = a.get(k), b.get(k)
            plain = not isinstance(vb, dsgo.Node) or not any(isinstance(x, dsgo.Node) and len(x.items) == 7 for x in vb.items)
            if isinstance(vb, dsgo.Node) and len(vb.items) == 7 and all(isinstance(x, float) for x in vb.items):
                continue   # a star curve they added where 4.1 has a number
            if plain:
                assert isinstance(va, dsgo.Node) == isinstance(vb, dsgo.Node), f'{mine} {k}: {dsgo.to_py(va)} / {dsgo.to_py(vb)}'
        assert dsgo.to_py(a.get('EnergyChargeRequire')) == dsgo.to_py(b.get('EnergyChargeRequire')) == [-1.0, -1.0]

def main() -> int:
    import rootcpk
    game = rootcpk.DEFAULT_GAME
    failed = 0
    for fn in TESTS:
        try:
            fn()
            print(f'ok    {fn.__name__}')
        except (Exception, SystemExit):   # the tools report refusals with SystemExit
            failed += 1
            print(f'FAIL  {fn.__name__}')
            traceback.print_exc()
        finally:   # installer.install / uninstall point it at their stand-in game: the real-data tests after need the real one
            rootcpk.use(game)
    print(f'{len(TESTS) - failed}/{len(TESTS)} passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
