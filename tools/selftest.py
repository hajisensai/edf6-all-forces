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
        want = jet_models.NOZZLES[jet.file or jet.box_model]   # the gunship: the stock bomber401 it flies
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
def release_imports() -> None:
    """Every project module tools/installer.py imports inside a function (tools/, pylib/, testrange/) is one of
    tools/build_release.py's PyInstaller hidden imports: a generator left out installs in a dev checkout and is missing
    from the released exe."""
    inst, rel = src('tools/installer.py'), src('tools/build_release.py')
    hidden = set(re.findall(r"'(\w+)'", rel.split("for mod in ('call_weapons'", 1)[1].split('):', 1)[0])) | {'call_weapons'}
    local = {os.path.splitext(f)[0] for d in ('tools', 'pylib', 'testrange') for f in os.listdir(os.path.join(ROOT, d)) if f.endswith('.py')}
    lazy = set(re.findall(r'^[ \t]+import (\w+)', inst, re.M)) & local
    assert 'make_emc' in lazy, 'release_imports: the scan reads installer.py'
    missing = sorted(lazy - hidden)
    assert not missing, f'tools/build_release.py: hidden imports missing {missing}'


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
        assert c.brings in ('jets', 'helis', 'sub', 'vehicle', 'throw'), c.id
        thrown = c.brings == 'throw'
        assert bool(c.drone) == thrown and (not c.drone or c.drone in drones), c.id
        # A thrown drone's marker: the bits of 1.0 with its code in the low ones, never 1.0 itself (calls.throw_mark).
        bits = calls.mark_bits(c)
        assert not thrown or (bits & ~0xFFF == calls.THROW_MARK_BASE and bits != calls.THROW_MARK_BASE), c.id
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
    written = {n.split('/', 1)[1] for n in make_jets.names()} | {vc.DRILL_CHARGE_FILE} | set(vc.EMC_FILES)   # the drill's: tools/make_drill.py; the EMC's: tools/make_emc.py
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
    assert m and 2 * float(m.group(1)) <= 3.8 + 1e-6, 'the contact box is no wider than the 3.8 m hull'
    m = re.search(r'kHullFront=([\d.]+)f,kChargeFrom=([\d.]+)f', d)
    assert m and float(m.group(1)) < drill_model.DRILL_BASE[2] and float(m.group(2)) >= 3.4, m and m.groups()
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
    assert re.search(r'enum class EmcRound \{ beam, sight, breakCharge, blast \};', src('src/crew.h')), 'src/crew.h EmcRound'
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
    off = hook.find('if(!Cfg().enabled)return;')
    assert i_frame < off and 0 <= hook.find('&EmcTick)') < off, 'EmcFrame / EmcTick run with the plugin off'
    assert 'Cfg().enabled && Cfg().emcBeam' in emc, 'emc.cpp Ready: off with the plugin'
    # The HUD's EMC line is the EMC's own vehicle's (its position), as the Proteus readout is.
    assert 'float pos[3]; };' in src('src/crew.h').split('struct EmcCue', 1)[1].split('\n', 1)[0]
    assert 'std::memcpy(c.pos,v+kPosition,12);' in emc and 'x.emc && vec::Dist(x.emc->pos,r.pos)<2.0f' in src('src/hud.cpp')
    assert 'ResetEmc();' in src('src/mission.cpp') and 'InstallEmc();' in src('src/plugin.cpp')
    inst = src('tools/installer.py')
    assert 'make_emc.build(game)' in inst and 'make_emc.install(game, emc)' in inst and 'make_emc.remove' in inst
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
    marker bone, the gunner's point and the platform's outer side are the model's; its request is a ground vehicle
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
    m = re.search(r'kPlatformOut=(-?[\d.]+)f', c)
    assert m and float(m.group(1)) == sm.PLATFORM[0][0], m and m.groups()
    assert sm.PLATFORM[0][1] < sm.GUNNER_POINT[1] == sm.PLATFORM[1][1], 'the gunner stands on the platform top'
    assert all(sm.PLATFORM[0][k] < sm.GUNNER_POINT[k] < sm.PLATFORM[1][k] for k in (0, 2)), 'the gunner over the platform'
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
def weapon_marks_agree() -> None:
    """The mod's LockonTargetType marks: one copy in C++ (common/edf/weapon.h), the data tools' copies equal to it."""
    marks = dict(re.findall(r'(kMark\w+)=(\d+)', src('common/edf/weapon.h')))
    assert marks == {'kMarkAir': '7301', 'kMarkGround': '7302', 'kMarkLofted': '7303'}, marks
    assert at_build.MARK_AIR == 7301.0 and at_build.MARK_GROUND == 7302.0
    assert make_artillery.MARK_GROUND == 7302.0 and make_katyusha.MARK_LOFTED == 7303.0
    assert make_katyusha.ROCKETS['LockonTargetType'] == make_katyusha.MARK_LOFTED


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
    assert 'axisMap(axis,true);' in code, 'the bones take the held angle'
    assert '&StabFrame,v' in crew and 'ResetStabilizer();' in mission and 'InstallStabilizer();' in plugin
    assert 'src/stab.cpp' in cmake and 'EXCLUDE_FROM_ALL tools/stab_check.cpp' in cmake
    assert '#include "../src/stab.h"' in src('tools/stab_check.cpp') and '#include "stab.h"' in code
    steer = src('src/turretcam.cpp').split('bool Steer(', 1)[1].split('\n}\n', 1)[0]
    assert 'StabHeld(seat+kSeatAim,held,hull);' in steer and 'target-held[i]' in steer and '-hull[i];' in steer
    flak = src('autoturret/src/plugin.cpp')
    assert 'Stabilized(vehicle,0,stock,held,hull);' in flak and '-hull;' in flak.split('float AxisInput(', 1)[1].split('\n}\n', 1)[0]
    assert 'Stabilized(vehicle,s,aim.angle,held,hull);' in src('autoturret/src/gunner.cpp')
    assert 'r.stab=StabState(v,r.seat);' in src('src/vhud.cpp') and 'L"    STAB"' in src('src/hud.cpp')
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
    at = src('autoturret/src/plugin.cpp')
    steer = at.split('float Steer(', 1)[1].split('\n}\n', 1)[0]
    assert steer.split('\n')[1].strip().startswith('if(PlayerLofted(seat))'), 'autoturret Steer: PlayerLofted first'
    assert 'bool Root(' not in at and 'bool BallisticArc(' in src('common/weapon.cpp')
    assert 'edf::BallisticArc(' in at and 'edf::BallisticArc(' in src('src/launcher.cpp')
    P, E, M = (0.0, 2.2, -2.06), (0.0, 2.51, -3.52), (0.0, 2.23, -3.56)   # the built model's, rounded
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
    assert 'PlayerGunRule(CameraTurret(vehicle,0),LeadCircle(),only!=nullptr)' in steer
    gunner = src('autoturret/src/gunner.cpp')
    seat = gunner.split('void SteerSeat(', 1)[1].split('\n}\n', 1)[0]
    put = seat.find('Put<float>(vehicle,kTurn')
    assert 0 <= seat.find('if(!rule.steer)return;') < put < seat.find('track.steered=autoturret::Frame();'), 'SteerSeat: the rule gates the turn'
    assert 'PlayerGunRule(CameraTurret(vehicle,s),LeadCircle(),only!=nullptr)' in seat
    link = src('common/edf/aimlink.h')
    names = dict(re.findall(r'constexpr char (k\w+)\[\]="(\w+)";', link))
    assert set(names) == {'kViewRay', 'kMapRay', 'kTurretReadout', 'kCameraTurret', 'kSteers', 'kStabilizer', 'kStabilizerAware', 'kPriorityZone',
                          'kInputHeld'}, names   # kPriorityZone: proteus_wired; kInputHeld: map_wired
    assert names['kCameraTurret'].endswith('V2') and names['kSteers'].endswith('V2'), names
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
    for key in ('PlayerJetLockByView', 'TurretAimHud'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=1', vini, re.M) and key in readme, key
    pick = src('src/stores.cpp').split('float ViewAngle(', 1)[1].split('\n}\n', 1)[0]
    assert 'Cfg().playerJetLockByView' in pick and 'Rider::player' in pick


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
    lay = gen.layout(points, gen.small_count(plan))
    taken = [(lay.player, gen.SPOT)]
    for sgo, _npc, p in gen.spots_for(plan, lay):
        r = gen.footprint(sgo)
        assert gen.overlap(p, r, taken) == 0.0, f'{sgo} at {p.name} reaches {gen.overlap(p, r, taken):.1f} m into another'
        taken.append((p, r))
    # The fallback is the least overlap, never a silent pile-up: a crowd that cannot fit still spreads out.
    crowd = gen.spaced([(carrier, False)] * 3, points[1:5], lay.player)
    assert len(set(p.name for p in crowd)) == 3, crowd


@test
def jet_door_on_the_ground_beside_its_box() -> None:
    """pylib/vcobjects.py move_door / check_door (docs/player-jet-re.md §12): a jet's boarding point (the V506's door
    locator, read out of a MAB block by mab_locator) goes on the ground DOOR_OUT m outside its collision box's right side,
    with a radius that reaches a human DOOR_STEP m off it on the ground under its box (the door is at the box frame's
    origin: on the ground for a box on its origin, over it for a stock bomber's box reaching under it) (the parked
    carrier's door was under its middle, 4.9 m in from its box's side: no prompt anywhere, 2026-10-05); check_door
    refuses a door inside the box or out of reach. On a block of its own (the selftest runs without the game)."""
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
    low['heli_rigid_body'] = [[0.0, 3.516, -3.109], [29.703, 8.516, 38.422], 0.3]   # its bottom 5 m under the door
    refused(sgo.write(1, low), 'a door out of reach (5 m over the ground its box stands on)')
    block = m['animation_model'][2]
    vec, rad = vc.mab_locator(block, door)
    x, y, z = struct.unpack_from('<3f', block, vec)
    radius = struct.unpack_from('<f', block, rad)[0]
    assert abs(x - (29.703 + vc.DOOR_OUT)) < 1e-3 and y == 0.0 and abs(z - 1.8) < 1e-6, (x, y, z)
    assert abs(radius - 1.8) < 1e-6, radius   # on the ground: the stock radius reaches
    assert vc.mab_locator(block, seat) == (vecs + 16, 0x64 + 0x10)
    assert struct.unpack_from('<4f', block, vecs + 16) == struct.unpack_from('<4f', mab, vecs + 16), 'the seat moved'
    # A small jet keeps the stock radius (1.8) when that reaches; the door stays within the box's length.
    at, r = vc.door_point([[0.0, 1.381, 1.688], [8.047, 1.381, 9.922]], (2.15, 0.0, 1.8), 1.8)
    assert at == [8.647, 0.0, 1.8] and r == 1.8, (at, r)
    at, r = vc.door_point([[0.0, 1.255, 0.0], [12.969, 1.255, 1.0]], (2.15, 0.0, 1.8), 1.8)
    assert at[2] == 1.0, at
    # A box reaching under its origin (a stock bomber's) puts the door over the ground: a radius that reaches it.
    assert abs(vc.door_height([[0.0, 0.339, 2.723], [1.983, 1.624, 15.137]]) - 1.285) < 1e-9
    at, r = vc.door_point([[0.0, -1.0, 0.0], [2.0, 2.0, 5.0]], (2.15, 0.0, 1.8), 1.8)
    assert r == round((3.0 ** 2 + vc.DOOR_STEP ** 2) ** 0.5 - vc.DOOR_SLACK + vc.DOOR_MARGIN, 3), r


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
def heli_mouse_aim_wired() -> None:
    """The helicopters' mouse-aim flight (src/heliaim.h) and HUD: their ini keys are read, shipped and documented, the
    lever they replaced (HeliMousePitch) is shipped no more and an old ini's is said ignored; the stock heli the player
    flies and the NPC pilot write the input block through the same stick, throttle and yaw law (heli.cpp Steer and
    AimFly), and the rotor craft fly the same aim::Fly (playerjet_board.inc HoverAim)."""
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    for key in ('HeliMouseAim', 'HeliFlightHud'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=1', ini, re.M) and key in readme, key
    assert not re.search(r'^HeliMousePitch=', ini, re.M), 'HeliMousePitch is retired (HeliMouseAim)'
    assert 'L"HeliMousePitch"' in plugin.split('void IgnoreRetired(', 1)[1].split('\n}\n', 1)[0]
    heli, board = src('src/heli.cpp'), src('src/playerjet_board.inc')
    steer = heli.split('Control Steer(', 1)[1].split('\n}\n', 1)[0]
    fly = heli.split('void AimFly(', 1)[1].split('\n}\n', 1)[0]
    for law in ('aim::StockStick(', 'aim::StockThrottle(', 'aim::StockYaw('):
        assert law in steer and law in fly, law
    assert 'aim::Fly(' in fly and 'aim::Fly(' in board.split('void HoverAim(', 1)[1].split('\n}\n', 1)[0]


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
    assert re.search(rf'\{{{vt},0x[0-9A-F]+,"612_nix"\}}', crew), vt
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
def proteus_wired() -> None:
    """The Proteus rework (src/proteus.cpp, src/proteus_logic.h, README 普罗透斯, docs/proteus-re.md): every Proteus* key the
    ini ships is read, range-checked (all but the three switches), and documented in README.md; the class crew.cpp chains
    for it (VehicleBigBegaruta, its own slot 55 now, 0 before) is the one proteus.cpp reworks; it is built (its own
    target_sources line) with its offline check, which includes the rules' header alone; every EDF.dll address it checks is
    in docs/proteus-re.md; the mission's reset, the per-frame step (before the plugin-off return: it gives the stock numbers
    back), the install, the turret camera's lift, the shells' preload and the EDF6AutoTurret link (one export name, both
    turret pickers weighed) are wired; the HUD's layout check covers it. With the game present: every VehicleBigBegaruta SGO
    has the 5 m foot radius and the 7500 durability the code takes as constants, four seats, and the 50 deg walkable slope
    whose 1.2 m step README.md quotes."""
    import math
    import rootcpk
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/proteus-re.md')
    code, crew, cmake, check = src('src/proteus.cpp'), src('src/crew.cpp'), src('CMakeLists.txt'), src('tools/proteus_check.cpp')
    keys = re.findall(r'^(Proteus\w+)=', ini, re.M)
    assert len(keys) >= 40 and 'ProteusRework' in keys, keys
    for key in keys:
        assert f'L"{key}"' in plugin and key in readme, key
        if key not in ('ProteusRework', 'ProteusTwoSeats', 'ProteusDriverGun'):
            assert f'Fix("{key}"' in plugin or f'FixInt("{key}"' in plugin, f'{key} is not range-checked'
    vt = re.search(r'kVtBig=(0x[0-9A-F]+)', code).group(1)
    assert re.search(rf'\{{{vt},0x648F70,"BigBegaruta"\}}', crew), 'crew.cpp does not chain the Proteus input'
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/proteus.cpp)' in cmake
    assert 'add_executable(proteus_check EXCLUDE_FROM_ALL tools/proteus_check.cpp)' in cmake
    assert re.findall(r'#include "([^"]+)"', check) == ['../src/proteus_logic.h'], 'proteus_check takes the rules alone'
    rvas = set()
    for block in re.findall(r'const Sig k\w+\[\]=\{(.*?)\n\};', code, re.S):
        rvas.update(re.findall(r'\{(0x[0-9A-F]+),\{', block))
    assert len(rvas) >= 20, rvas
    for rva in sorted(rvas):
        assert rva in doc, f'docs/proteus-re.md does not mention {rva}'
    assert 'ResetProteus();' in src('src/mission.cpp') and 'InstallProteus();' in plugin
    frame = crew.split('void __fastcall InputHook', 1)[1]
    assert frame.index('&ProteusFrame') < frame.index('if(!Cfg().enabled)return;'), 'the Proteus step must run with the plugin off'
    assert frame.index('&ProteusFrame') < frame.index('&SeatSwitchFrame'), 'the seats it closes are closed before the seat switch asks'
    # The stock launcher is the salvo's only while the salvo can be fired and its seat is closed, decided each frame after
    # the seats; what is given back is what was taken.
    step = code.split('void Frame(unsigned char* v)', 1)[1].split('\n}', 1)[0]
    assert step.index('TwoSeats(*u,v);') < step.index('Guns(*u,salvoReady,c);'), 'Guns after the seats'
    assert 'const bool hold=salvo && u.closed;' in code and 'Put<float>(m,kRate,u.rate[kLauncherSeat]);' in code
    give = code.split('void GiveBack(Unit& u', 1)[1].split('\n}', 1)[0]
    assert 'Put<float>(w,kRate,u.rate[s]);Put<float>(w,kSpread,u.spread[s]);' in give and '1.0f' not in give
    assert 'u.active && u.ref.Is(v)' in code and 'u.ref.obj==' not in code, 'a Proteus unit by its live object, not its address'
    # A Proteus no player has ridden is not crewed (the helicopters' rule, crew.cpp Crew).
    assert '(IsHelicopter(vehicle) || IsProteus(vehicle)) && !st.playerAt' in crew
    assert 'kProteusHoldCountdown*0.5f' in src('src/vehsound.cpp') and 'kHoldCountdown=kProteusHoldCountdown' in code
    # The damage call both read: the carrier's check takes the Proteus's redirect as intact (no install order between them).
    sub = src('src/subcarrier.cpp')
    sigs = sub.split('const Sig kDamageSigs[]={', 1)[1].split('};', 1)[0]
    assert '{0x54A586,' not in sigs and 'return to==image+kDamageTarget || ProteusDamageThunk(to);' in sub
    assert 'damageOk=Body506MessageOk() && DamageCallReaches();' in sub and 'damageThunk=image+kDamageCall+5+rel;' in code
    assert 'ProteusViewLift(' in src('src/turretcam.cpp')
    assert 'PreloadShells(mgr,Preloaded(Body::gunship),ProteusReady());' in src('src/jet_spawn.cpp') and 'gunship || proteus' in src('src/jet_bay.cpp')
    link = src('common/edf/aimlink.h')
    name = re.search(r'kPriorityZone\[\]="(\w+)"', link).group(1)
    assert f'extern "C" __declspec(dllexport) bool __cdecl {name}(' in code, name
    assert 'link::kPriorityZone' in src('autoturret/src/designate.cpp')
    assert 'distance*PriorityWeight(*e)' in src('autoturret/src/plugin.cpp') and 'distance*PriorityWeight(e)' in src('autoturret/src/gunner.cpp')
    assert 'StockLayoutApart(1920,&sceneProteus)' in src('tools/hud_view.cpp')
    foot = float(re.search(r'kFootRadius=([0-9.]+)f', code).group(1))
    durability = float(re.search(r'kDurability=([0-9.]+)f', code).group(1))
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
            assert v['game_object_durability'] == durability, name
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
    texts = hud.split('kWarnText[kWarnCount]={', 1)[1].split('};', 1)[0]
    assert len(re.findall(r'(?<!\w)L"', texts)) == n_warn, (texts, n_warn)
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
    assert 0 <= hook.find('&VehicleSound,v') < hook.find('if(!Cfg().enabled)return;'), 'VehicleSound before the Enabled test'
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
    """tools/make_jets.py cannon_round / check_cannon_round on a synthetic stock round (no game needed): the gunship
    cannon's round is the stock gunship's solid round with the cannon's numbers, nothing else changed, and the check
    refuses a round that falls short of the reach or penetrates. The C++ side fires the file the tool writes, with the
    tool's reach and speed (src/jet_bay.cpp kCannon*), as a store of the gunship's (src/playerjet_board.inc CANNON); the
    installer writes it (make_jets.names) and the README tells of it."""
    import sgo
    stock = {'xgs_scene_object_class': 'DemoIndirectFire', 'indirect_fire_damage': 2000.0,
             'indirect_fire_param': [[1.2, 0.0], [800.0, 0.0], 1, 0, 'SolidBullet01', 20.0, 0.0, 10.0, 2.0, 0.0, 600, 1,
                                     [0.4, 0.4, 6.0, 1.0], [], 0, 60, 0, [0, 'weapon_KUBAKU_Cannon_shot', 0.5, 1.0, 1.0, 500.0],
                                     [0, 'common_damages_kuubaku_Cannon21', 0.9, 1.0, 3.0, 200.0]]}

    class Game:
        def read(self, folder: str, name: str) -> bytes:
            assert (folder, name) == ('OBJECT', make_jets.IMPACT_STOCK), (folder, name)
            return sgo.write(0x102, stock)

    data = make_jets.cannon_round(Game())
    make_jets.check_cannon_round(data)
    old, new = sgo.read(Game().read('OBJECT', make_jets.IMPACT_STOCK))[1], sgo.read(data)[1]
    assert set(old) == set(new) and [k for k in old if old[k] != new[k]] == ['indirect_fire_damage', 'indirect_fire_param']
    changed = [i for i, (a, b) in enumerate(zip(old['indirect_fire_param'], new['indirect_fire_param'])) if a != b]
    assert changed == [0, 5, 7, 9, 10, 11, 12, 15], changed   # scatter, speed, size, blast, life, penetration, colour, wait
    assert new['indirect_fire_param'][17] == old['indirect_fire_param'][17], 'the stock cannon fire sound kept'
    for i, value, why in ((10, 100, 'a life short of the reach'), (11, 1, 'a penetrating round'), (9, 25.0, 'a whale-sized blast')):
        version, bad = sgo.read(data)
        bad['indirect_fire_param'][i] = value
        try:
            make_jets.check_cannon_round(sgo.write(version, bad))
        except make_jets.CannonRoundError:
            continue
        raise AssertionError(f'check_cannon_round took {why}')
    assert 3.0 <= make_jets.CANNON_RADIUS <= 6.0, 'a few metres of blast (and >= 3 m: as the drill charge, docs/drill-re.md §3)'
    assert make_jets.CANNON_SPEED * make_jets.CANNON_LIFE >= make_jets.CANNON_REACH
    bay = src('src/jet_bay.cpp')
    assert f'kCannonFile[]=L"{make_jets.CANNON_FILE}"' in bay, 'src/jet_bay.cpp kCannonFile'
    assert f'kCannonSgo[]=L"app:/object/{make_jets.CANNON_FILE.lower()}"' in bay, 'src/jet_bay.cpp kCannonSgo'
    m = re.search(r'kCannonReach=([\d.]+)f', bay)
    assert m and float(m.group(1)) == make_jets.CANNON_REACH, 'src/jet_bay.cpp kCannonReach'
    m = re.search(r'kCannonSpeed=([\d.]+)f', bay)
    assert m and float(m.group(1)) == make_jets.CANNON_SPEED * 60.0, 'src/jet_bay.cpp kCannonSpeed (m/s) is CANNON_SPEED a frame'
    assert f'OBJECT/{make_jets.CANNON_FILE}' in make_jets.names()
    assert '{L"","CANNON",StoreRole::bomb' in src('src/playerjet_board.inc'), 'src/playerjet_board.inc kSpecials CANNON'
    readme = src('README.md')
    assert make_jets.CANNON_FILE in readme and '炮舰机的机炮' in readme, 'README.md: the gunship cannon'


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
    assert 'StockVehicleHud(drawer' in hud.split('void HudDraw(', 1)[1].split('\n}\n', 1)[0]
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
def stock_gauges_wired() -> None:
    """The stock weapon gauges (src/stockgauge.cpp; the user, 2026-10-06: "删掉原版挂载和油料显示"): its ini key is read,
    shipped and documented; it is installed at load and only through the gauge's update slot (no draw call skipped); the
    HUD's publish says what it covers; every EDF.dll address it checks is in docs/hud-re.md §9; the fuel tank is no
    weapon in the stock HUD's arms and is read where the stock FUEL panel was, LOW FUEL its warning."""
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    assert 'L"HideStockGauges"' in plugin and re.search(r'^HideStockGauges=1', ini, re.M) and 'HideStockGauges' in readme
    assert 'InstallStockGauges();' in plugin.split('EML6_Load(', 1)[1]
    cmake = src('CMakeLists.txt')
    assert 'src/stockgauge.cpp' in cmake.split('add_library(EDF6VehicleCrew', 1)[1].split(')', 1)[0]
    gauge = src('src/stockgauge.cpp')
    assert gauge.count('PatchVtableSlot(') == 1 and 'RedirectCall' not in gauge, 'stockgauge.cpp: the update slot only'
    doc = src('docs/hud-re.md').split('## 9.', 1)[1]
    rvas = set(re.findall(r'\b0x[0-9A-F]{6,7}\b', gauge))
    missing = sorted(r for r in rvas if f'`{r}`' not in doc and f'`{r} ' not in doc and r not in doc)
    assert not missing, f'docs/hud-re.md §9 does not name {missing}'
    hud = src('src/hud.cpp')
    assert 'SetStockGaugeCover(textOk' in hud.split('void HudPublish(', 1)[1].split('\n}\n', 1)[0]
    vhud = src('src/vhud.cpp').split('void StockHudFrame(', 1)[1].split('\n}\n', 1)[0]
    assert 'IsFuelTank(w)' in vhud and 'FuelGauge(v,&r.fuel)' in vhud
    assert 'FuelGauge(v,&r.fuel)' in src('src/playerjet.cpp') and 'FuelGauge(v,&r.fuel)' in src('src/heli.cpp')
    assert 'kWarnFuel' in src('src/warn.h') and 'kWarnFuel' in src('src/warn.cpp') and 'L"LOW FUEL"' in hud
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
def stock_payload_and_seats_wired() -> None:
    """The stock vehicles' payload readout and store switch (src/payload.cpp) and the seat switch (src/seatswitch.cpp):
    their ini keys are read, shipped and documented, the keys range-checked; the input hook moves the player before the
    steps that read who sits where and picks the store before the heli sight marks it; the HUD's struct is the header's;
    playerjet.cpp tells a move between the gunship's seats from getting out; the installer step (tools/make_stock_stores.py)
    is opt-in (off in the shipped ini), installed after the jets' store weapons, removed, bundled and a ledger owner, and
    its stores are store weapons make_jets writes and src/stores.inc knows."""
    import make_stock_stores as mss
    plugin, ini, readme = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md')
    keys = ('StockHeliStores', 'SeatSwitch', 'SeatNextKey', 'SeatNumberKeys', 'SeatButton', 'SeatPilot', 'SeatSwitchOnline')
    for key in keys:
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    for key in ('SeatNextKey', 'SeatButton'):
        assert f'FixInt("{key}"' in plugin, f'{key} is not range-checked'
    assert re.search(r'^StockHeliStores=0', ini, re.M) and not mss.wanted(ini), 'StockHeliStores ships off'
    assert mss.wanted('[VehicleCrew]\nStockHeliStores=1 ; on\n') and not mss.wanted('[Other]\nStockHeliStores=1\n')
    hook = src('src/crew.cpp').split('void __fastcall InputHook(', 1)[1].split('\n}\n', 1)[0]
    order = [hook.find(f'&{f},') for f in ('CrewStep<I>', 'SeatSwitchFrame', 'AimLines', 'PlayerJetFrame', 'PayloadFrame', 'HeliSightFrame')]
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
    for f in mss.store_files():
        assert f in vc.STORE_FILES, f'{f}: make_jets does not write it'
        kind = vc.store_of('app:/weapon/' + f.lower())[0]
        assert f'L"EDF6VC_{kind}_"' in stores_inc, f'{f}: src/stores.inc does not know its kind'
    # The seat switch's offsets agree with the RE notes.
    doc = src('docs/stock-payload-re.md')
    for rva in ('0x5763E0', '0x551C30', '0x56C9F0', '0x633FE0', '0x634940', '0x6346FC', '0x56D7CC', '0x572734'):
        assert rva in doc, rva
    seat = src('src/seatswitch.cpp')
    for c in ('kAnnounce=0x5763E0', 'kSetAction=0x551C30', 'kRideAction=0x56C9F0', 'kReserve=0x633FE0', 'kClear=0x634940'):
        assert c in seat, c


@test
def stock_stores_build() -> None:
    """With the game here (CI has none): every stock request that brings a 506-class helicopter of LOADOUTS gets a vehicle
    whose holders and weapon list agree, the fuel tank fourth, the stores after it (make_stock_stores.check); only the
    vehicle, the weapon list and the preload list change."""
    import rootcpk
    import make_stock_stores as mss
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.default()
    names = mss.requests(game)
    assert names, 'no stock request brings a helicopter of LOADOUTS'
    files: dict[str, bytes] = {}
    stems = set()
    for name in names:
        data, stem = mss.request_sgo(game.read('WEAPON', name), name)
        files[f'WEAPON/{name}'] = data
        stems.add(stem)
        before, after = dsgo.to_py(dsgo.parse(game.read('WEAPON', name)).root), dsgo.to_py(dsgo.parse(data).root)
        assert set(before) == set(after), name
        for k in before:
            if k not in ('Ammo_CustomParameter', 'resource'):
                assert before[k] == after[k], (name, k)
    assert stems == set(mss.LOADOUTS), stems
    for stem in stems:
        files[f'OBJECT/{mss.derived_name(stem)}'] = mss.derived_vehicle(game, stem)
    mss.check(files)


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
    unhooked = dict(re.findall(r'\{(0x[0-9A-Fa-f]+),0,"(\w+)"\}', crew))
    extras = re.findall(r'\{(0x[0-9A-Fa-f]+),4,0x[0-9A-Fa-f]+,\{[^}]*\},"(\w+)"\}', c)
    assert sorted(n for _, n in extras) == ['501_FortressRobo'], extras
    for vt, name in extras:
        assert unhooked.get(vt) == name, f'{name}: crew.cpp hooks it (no own hook needed) or names it otherwise'
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
                      ('kMarkerDtor', '0x5B0410'), ('kMarkerUpdate', '0x5B2750'), ('kHostileWalk', '0x5E0F20')):
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
                        ('kRadarCall', 0x82B8C3)):
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
                'src/proteus.cpp'):
        assert 'if(vk<=0 || MapHoldsKeys())return false;' in src(rel), rel
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
                                      'overlay.cpp', 'map.cpp', 'proteus.cpp']), f'a new key reader: make it give way to the map ({readers})'

    assert 'InstallMap();' in plugin and 'ResetMap();' in mission and 'src/map.cpp' in cmake
    assert 'EXCLUDE_FROM_ALL tools/map_cam_check.cpp' in cmake and '#include "../src/map_cam.h"' in src('tools/map_cam_check.cpp')
    assert 'MapScreen(drawer,ctx,t,viewProj' in hud and '!MapOwnsView())KeepViewProj' in hud
    assert 'MapScene(dir,L"map_mid"' in src('tools/hud_view.cpp')
    assert 'ViewMapClip(true,' in code and 'ViewMapClip(false,' in code
    # The pins: every enemy the radar's hostile walk finds (the nearest kMapEnemies drawn, the cap the README says), the
    # pin's height from map_cam.h (checked offline), drawn for every kind.
    assert '0x82B8C3' in doc and 'HUiHudRader' in doc and '256' in readme and 'kMapEnemies' in readme
    assert re.search(r'kMapEnemies=256\b', src('src/map.h')) and 'std::partial_sort(foes' in code
    assert 'mapcam::PinHeight(' in hud and 'PinHeight(' in src('tools/map_cam_check.cpp')
    kinds = re.search(r'enum class MapKind : std::uint8_t \{(.*?)\};', src('src/map.h')).group(1).replace(' ', '').split(',')
    icon = hud[hud.index('void MapIcon('):hud.index('struct Pin {')]
    for kind in kinds:
        assert f'case MapKind::{kind}:' in icon, f'MapIcon draws no {kind}'


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
