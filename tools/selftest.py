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
  - interrupted or refused runs: autoturret/tools/build.py install killed half way still reinstalls and
    uninstalls cleanly, a call_weapons.install that rolled back records no first backup, and the installer's
    uninstall over a misaligned table offers repair or skipping the table instead of failing;
  - the model importer (pylib/obj_model.py, pylib/texfile.py) on synthetic data: OBJ reading (negative indices,
    n-gons, winding, uv origin, texture lookup), holes found and closed, skins, meshes split below 65536 vertices
    with tangents, PNG decoding, DDS .lod slicing and DXT1.
"""
from __future__ import annotations

import contextlib
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
    for plan in (gen.grand_battle(gen.Plan()), air, gen.Plan()):
        lay = gen.layout(points, gen.small_count(plan))
        named = set(re.findall(r'app:/object/(edf6tr_[a-z0-9_]+)\.sgo', gen.script(plan, lay)))
        missing = named - {x for x in gen.spawned(plan) if x in gen.DERIVED}
        assert not missing, f'{plan.scenario or ("air" if plan.air.enabled else "waves")}: never written {sorted(missing)}'
        assert named or plan.scenario != gen.GRAND, 'the grand battle names no generated object: the check sees nothing'


@test
def jet_nozzles_on_their_models() -> None:
    """src/booster.cpp kJetNozzles: each mark's nozzles those of its model (pylib/jet_models.py NOZZLES: on the exit's
    centre), its flame as big as that engine (width the exit's diameter, length FLAME_LENGTH_PER_DIAMETER of it), and
    every jet mark has a row."""
    import jet_models
    from vcobjects import JETS
    carrier = 'EDF6VC_CARRIER.MRAB'
    num = r'([-\d.]+)f'
    vec = r'\{' + num + ',' + num + ',' + num + r'\}'
    rows = {}
    for m in re.finditer(r'\{(\d+)\.0f,(\d),\{' + vec + ',' + vec + r'\},\{' + num + ',' + num + r'\}\}',
                         src('src/booster.cpp')):
        count = int(m.group(2))
        at = [tuple(float(m.group(k)) for k in range(3 + 3 * n, 6 + 3 * n)) for n in range(count)]
        rows[float(m.group(1))] = (at, (float(m.group(9)), float(m.group(10))))
    bad = []
    for name, jet in JETS.items():
        if jet.file is not None and (jet.file not in jet_models.MODELS or jet.file == carrier):
            continue   # the carrier's four nozzles are its own (CarrierFlames); the Primers' fighter flaps: no exhaust
        if jet.mark not in rows:
            bad.append(f'{name}: mark {jet.mark} has no nozzle row')
            continue
        want = jet_models.NOZZLES[jet.file]
        got, size = rows[jet.mark]
        d = want[0][1]
        if len(got) != len(want) or any(abs(a - b) >= 0.005 for g, (w, _d) in zip(got, want) for a, b in zip(g, w)):
            bad.append(f'{name}: {got}, the model has {[w for w, _d in want]}')
        if abs(size[1] - d) >= 0.005 or abs(size[0] - jet_models.FLAME_LENGTH_PER_DIAMETER * d) >= 0.01:
            bad.append(f'{name}: flame {size}, its engine {d} m across')
    assert len(rows) >= 10, f'{len(rows)} nozzle rows parsed: the pattern no longer reads the table'
    table = src('src/booster.cpp').split('kBomberNozzles[]={', 1)[1].split('};', 1)[0]
    for name in ('bomber401', 'bomber501_2'):
        m = re.search(r'\{0\.0f,(\d),\{' + vec + ',' + vec + r'\},\{' + num + ',' + num + r'\}\},\s*// JetBody::'
                      + name + r'\n', table)
        assert m, f'src/booster.cpp kBomberNozzles: no {name} row'
        want = jet_models.NOZZLES[name]
        got = [tuple(float(m.group(k)) for k in range(2 + 3 * n, 5 + 3 * n)) for n in range(int(m.group(1)))]
        d = want[0][1]
        if len(got) != len(want) or any(abs(a - b) >= 0.005 for g, (w, _d) in zip(got, want) for a, b in zip(g, w)):
            bad.append(f'{name}: {got}, the model has {[w for w, _d in want]}')
        if abs(float(m.group(9)) - d) >= 0.005 or abs(float(m.group(8)) - jet_models.FLAME_LENGTH_PER_DIAMETER * d) >= 0.01:
            bad.append(f'{name}: flame {m.group(8)} x {m.group(9)}, its engine {d} m across')
    assert not bad, '\n'.join(bad)


@test
def gear_legs_as_the_models_fold_them() -> None:
    """src/gear.cpp's legs (kLegNames, kLegUp) are pylib/jet_gear.py's (LEGS, LEG_UP): the plugin folds each leg by the
    angle its model was measured to fold level at; every fixed-wing model recipe names a gear spec, hover craft none."""
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
    for file, r in jet_models.MODELS.items():
        hover = file in ('EDF6VC_CARRIER.MRAB', 'EDF6VC_DRONE.MRAB')
        assert (r.gear is None) == hover and (r.gear is None or r.gear in jet_gear.SPECS), f'{file}: gear {r.gear}'


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
    for c in calls.CALLS:
        assert c.kind in calls.KINDS, c.id
        assert c.brings in ('jets', 'helis', 'sub', 'vehicle'), c.id
        assert bool(c.role) == (c.brings == 'jets') and (not c.role or c.role in roles), c.id
        assert bool(c.body) == (c.brings == 'helis') and (not c.body or c.body in bodies), c.id
        assert bool(c.vehicle) == (bool(c.jet) or bool(c.ground)) == (c.brings == 'vehicle'), c.id
        assert not (c.jet and c.ground) and (not c.ground or c.ground in vc.GROUND_VEHICLES), c.id
        assert not c.ground or vc.GROUND_VEHICLES[c.ground].sgo == c.vehicle, c.id
        assert (c.count > 0) == c.flown and bool(c.log) == c.flown, c.id
        for lang in cw.LANGS:
            assert calls.call_name(c, lang) and calls.call_description(c, lang)
            assert calls.retired_name(c, lang) != calls.call_name(c, lang)
        assert calls.slot_of(c.id) == c.id and calls.slot_of(calls.retired_id(c.id)) == c.id
        assert not calls.retired_id(c.id).startswith(calls.ID_PREFIX)   # GrantCalls never owns a placeholder
    names = [calls.call_name(c, 'SC') for c in calls.FLOWN]
    assert len(set(names)) == len(names), 'two calls with one banner label'


@test
def calls_inc_current() -> None:
    with open(gen_calls.OUT, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_calls.render(), 'src/calls.inc is stale: python tools/gen_calls.py'
    assert '#include "calls.inc"' in src('src/airstrike.cpp')


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
            assert plan.appended == [c for c in calls.IDS if c not in table], name
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
    marks = {j.mark for j in vc.JETS.values() if j.mark != 7101.0} | {make_jets.GUNSHIP_MARK}   # 7101: the sub, no jet
    missing = sorted(marks - set(vc.JET_MASSES))
    assert not missing, f'pylib/vcobjects.py JET_MASSES: no mass for marks {missing}'
    durability = gen_stores.durabilities()
    assert all(durability.get(m, 0.0) > 0.0 for m in vc.JET_MASSES), 'a JET_MASSES mark with no durability'
    assert all(m > 0.0 for m in vc.JET_MASSES.values())


# ---------------------------------------------------------------- copies kept by hand


@test
def hand_copies_agree() -> None:
    # kKinds rows: {"name",mark,...}, the mark an integer or a float literal.
    pjet = dict(re.findall(r'\{"(\w+)",\s*(\d+)(?:\.0f)?\s*,', src('src/playerjet.cpp').split('kKinds[]={', 1)[1].split('};', 1)[0]))
    for c in calls.CALLS:
        if c.brings != 'vehicle' or c.ground:
            continue
        jet = vc.JETS[c.jet]
        assert jet.player and jet.mark == c.mark, c.id
        assert float(pjet[c.kind.removeprefix('pjet_')]) == c.mark, f'src/playerjet.cpp kKinds disagrees on {c.id}'
        assert make_jets.FILES[f'{c.vehicle}.SGO'] == c.jet, c.id
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
    written = {n.split('/', 1)[1] for n in make_jets.names()} | {vc.DRILL_CHARGE_FILE}   # the drill's: tools/make_drill.py
    assert files <= written, f'src/jet* loads files tools/make_jets.py does not write: {sorted(files - written)}'


@test
def drill_copies_agree() -> None:
    """src/drill.cpp's drill (bone name, length, base radius) is pylib/drill_model.py's, src/jet_bay.cpp's drill charge
    is the one tools/make_drill.py writes (pylib/vcobjects.py DRILL_CHARGE_FILE), its blast breaks buildings (>= 3 m),
    the drill tank's request is a ground vehicle request of tools/make_drill.py's vehicle, and the model turns the OBJ
    without mirroring it."""
    import drill_model
    import make_drill
    d = src('src/drill.cpp')
    assert f'kDrillBone[]=L"{drill_model.DRILL_BONE}"' in d, 'src/drill.cpp kDrillBone'
    m = re.search(r'kDrillLength=([\d.]+)f,kDrillRadius=([\d.]+)f', d)
    assert m and (float(m.group(1)), float(m.group(2))) == (drill_model.DRILL_LENGTH, drill_model.DRILL_RADIUS), m and m.groups()
    bay = src('src/jet_bay.cpp')
    assert f'kDrillChargeFile[]=L"{vc.DRILL_CHARGE_FILE}"' in bay, 'src/jet_bay.cpp kDrillChargeFile'
    assert f'kDrillChargeSgo[]=L"app:/object/{vc.DRILL_CHARGE_FILE.lower()}"' in bay, 'src/jet_bay.cpp kDrillChargeSgo'
    assert vc.DRILL_CHARGE_RADIUS >= 3.0, 'a drill charge under 3 m breaks no building (docs/drill-re.md §3)'
    drills = [c for c in calls.CALLS if c.ground == 'drill']
    assert len(drills) == 1 and drills[0].vehicle == make_drill.VEHICLE.sgo and drills[0].mark == 0
    assert make_drill.OWNER in ledger.OWNERS and make_drill.VEHICLE.tool == 'make_drill'
    assert not drill_model.CONVERSION.mirrors() and drill_model.CONVERSION.point((1.0, 0.0, 0.0))[2] > 0, 'OBJ +X is forward'


@test
def weapon_marks_agree() -> None:
    """The mod's LockonTargetType marks: one copy in C++ (common/edf/weapon.h), the data tools' copies equal to it."""
    marks = dict(re.findall(r'(kMark\w+)=(\d+)', src('common/edf/weapon.h')))
    assert marks == {'kMarkAir': '7301', 'kMarkGround': '7302', 'kMarkLofted': '7303'}, marks
    assert at_build.MARK_AIR == 7301.0 and at_build.MARK_GROUND == 7302.0
    assert make_artillery.MARK_GROUND == 7302.0 and make_katyusha.MARK_LOFTED == 7303.0
    assert make_katyusha.ROCKETS['LockonTargetType'] == make_katyusha.MARK_LOFTED


@test
def lofted_arc_solver() -> None:
    """pylib/ballistics.py (the model autoturret/src/plugin.cpp Ballistic mirrors): the high and the low root both hit
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
def every_npc_aircraft_boardable() -> None:
    # Every jet body of our side (jet_internal.h kBodies: a mark, not hostile) has its row in src/playerjet_kinds.h
    # kBoardable, so an aircraft added later is flown by the player too (or is left out on purpose here, saying why);
    # the enemy's are not; the ini keys of the feature are read and documented.
    table = src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{Body::(\w+),L"[^"]*",L"[^"]*",(\d+)\.0f,[^}]*?"(\w+)"(,true)?\}', re.sub(r'\s+', ' ', table))
    assert len(rows) >= 15, f'src/jet_internal.h kBodies: read {len(rows)} rows'
    boardable = set(re.findall(r'\{Body::(\w+),Airframe::', src('src/playerjet_kinds.h')))
    for body, mark, _name, hostile in rows:
        if int(mark) == 0:
            continue   # a heli: the stock heli flight
        if hostile:
            assert body not in boardable, f'{body} is the enemy\'s: not boardable'
        else:
            assert body in boardable, f'{body} (mark {mark}): no row in src/playerjet_kinds.h kBoardable'
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('PlayerJetAll', 'PlayerJetHailKey'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key


@test
def heli_sight_after_aim_lines() -> None:
    """The stock heli's gun sight (src/helisight.cpp) draws for the guns whose aim line AimLines hid this frame
    (HiddenAimGuns), so the input hook runs it after AimLines; AimLines hides the player's line for it
    (PlayerHeliOwnSight); its ini key is read, shipped and documented; every key the ini ships is read."""
    crew = src('src/crew.cpp')
    hook = crew.split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    lines, sight = hook.find('&AimLines,'), hook.find('&HeliSightFrame,')
    assert 0 <= lines < sight, 'src/crew.cpp InputHook: AimLines must run before HeliSightFrame'
    aim = crew.split('void AimLines(', 1)[1].split('\n}\n', 1)[0]
    assert 'PlayerHeliOwnSight(vehicle)' in aim, 'src/crew.cpp AimLines: the heli sight hides the player\'s line'
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert re.search(r'^PlayerHeliGunSight=1', ini, re.M) and 'PlayerHeliGunSight' in readme
    unread = [k for k in re.findall(r'^([A-Za-z]\w*)=', ini, re.M) if f'L"{k}"' not in plugin]
    assert not unread, f'EDF6VehicleCrew.ini keys src/plugin.cpp never reads: {unread}'


@test
def readme_counts() -> None:
    readme = src('README.md')
    assert f'{len(calls.FLOWN)} 种呼叫' in readme, f'README.md: say {len(calls.FLOWN)} 种呼叫 (tools/calls.py FLOWN)'
    assert f'共 {len(calls.CALLS)} 行' in readme, f'README.md: say 共 {len(calls.CALLS)} 行 (tools/calls.py CALLS)'


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
            modfiles.atomic_write(os.path.join(mods, 'WEAPON', 'AT_B.SGO'), b'b')
            at_build.uninstall(mods, force=False)
            left = [rel for rel in AT_FILES if os.path.exists(os.path.join(mods, *rel.split('/')))]
            assert not left, f'uninstall left our own files in Mods: {left}'
            assert not os.path.exists(os.path.join(mods, at_build.MANIFEST))
    finally:
        shutil.rmtree(mods, ignore_errors=True)


def _sgo_table(key: str, ids: list[str]) -> bytes:
    """A minimal weapon table (key 'table') or text table (key 'text_table'): one row per id."""
    return dsgo.compact(dsgo.Document(dsgo.Node([dsgo.Node([dsgo.Node([i]) for i in ids])], {0: key}), []))


def _call_files(game: str, table_ids: list[str]) -> dict[str, bytes]:
    """What call_weapons.stack would give (shape only), and the jets the vehicle requests need."""
    for c in calls.CALLS:
        if c.vehicle:
            for rel in cw.vehicle_needs(c):
                modfiles.atomic_write(_mods(game, rel), b'jet')
    files = {cw.TABLE: _sgo_table('table', table_ids)}
    files.update({rel: _sgo_table('text_table', table_ids) for rel in cw.TEXTS})
    files.update({cw.sgo_file(c): c.id.encode() for c in calls.CALLS})
    return files


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
    big = om.merge([om.Part('c', 'm', [om.Vertex(om.add(v.pos, (k * 2.0, 0.0, 0.0)), v.normal, v.uv) for v in cube.verts],
                            cube.tris) for k in range(2800)])           # 67200 vertices
    meshes = om.build_meshes(template, [(big, om.rigid(big, 3))], material=2)
    assert len(meshes) == 2 and all(me.nverts < 0x10000 and me.material == 2 for me in meshes)
    assert sum(len(me.indices) // 6 for me in meshes) == len(big.tris)
    me = meshes[0]
    assert me.flags == bytes((0, 1, 1, 0)) and {int(r[0]) for r in read_elem(me, 'BLENDINDICES')} == {3}
    for n, t, b in zip(read_elem(me, 'NORMAL'), read_elem(me, 'TANGENT'), read_elem(me, 'BINORMAL')):
        assert abs(om.dot(n[:3], t[:3])) < 2e-3 and abs(om.dot(n[:3], b[:3])) < 2e-3, (n, t, b)
    assert max(st.unpack(f'<{len(me.indices) // 2}H', me.indices)) < me.nverts


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
    assert hd == dds and (li.width, li.height, li.mips) == (16, 16, 5), li          # 64 -> 16 (never below 16)
    assert texfile.lod_level(2048, 2048) == 4 and texfile.lod_level(1024, 512) == 4 and texfile.lod_level(64, 64) == 2
    c0, c1, idx = st.unpack_from('<HHI', lod, 128)
    assert c0 == c1 == ((200 * 31 + 127) // 255) << 11 | ((100 * 63 + 127) // 255) << 5 | ((50 * 31 + 127) // 255) and idx == 0


def main() -> int:
    failed = 0
    for fn in TESTS:
        try:
            fn()
            print(f'ok    {fn.__name__}')
        except (Exception, SystemExit):   # the tools report refusals with SystemExit
            failed += 1
            print(f'FAIL  {fn.__name__}')
            traceback.print_exc()
    print(f'{len(TESTS) - failed}/{len(TESTS)} passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
