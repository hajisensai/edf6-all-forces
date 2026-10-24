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
  - the installer's 「回传日志」 (tools/testhub.py collect) sends only a crash dump of this install and of the run the
    logs end with (tests/testhub_dump_source_test.py: synthetic minidumps against the plugin log's loading lines), the
    plugin's own crash dump beside Windows' one, a candidate Windows Report.wer with unconfirmed session identity, and always crash/dumps.txt saying why there is
    no dump; the dump's file name and log lines src/crash_dump.cpp writes are the ones collect() looks for;
  - the model importer (pylib/obj_model.py, pylib/texfile.py) on synthetic data: OBJ reading (negative indices,
    n-gons, winding, uv origin, texture lookup), holes found and closed, skins, meshes split below 65536 vertices
    with tangents, PNG decoding, DDS .lod slicing and DXT1.
"""
from __future__ import annotations

import contextlib
import io
import json
import lzma
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
    for write in ('Put<std::int32_t>(v,kTeam,own);',
                  'Put<int>(const_cast<void*>(ref.obj),edf::kTeam,team);',
                  'Put<int>(object+kTeam,0,team);'):
        assert raw.search(write), f'the pattern no longer sees a raw write: {write}'


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


@test
def arrival_smoke_is_drawn_where_it_is_laid() -> None:
    """The arrival's smoke trail is made with the identity as its object's matrix (the user, 2026-10-21: 「飞机的入场尾烟与实际
    偏移了好多」): the stock LineStripObject draws its world-laid nodes through that matrix (src/smoke_trail.h), so made with the
    nozzle's world matrix the smoke was drawn turned and moved by where the nozzle was when it began, far off its jet. The
    emitter is placed from the nozzle by smoke::Emitter alone, when made and every frame (it was laid forward when made and
    rearward after)."""
    booster = src('src/booster.cpp')
    make = booster.split('void TrailMake(', 1)[1].split('\n}\n', 1)[0]
    frame = booster.split('void SmokeFrame(', 1)[1].split('\n}\n', 1)[0]
    calls = re.findall(r'\(image\+kTrailMake\)\(([^)]*)\)', make)
    assert calls == ['mgr,out,smoke::kObjectFrame,p'], f'TrailMake hands the factory {calls}: its matrix must be smoke::kObjectFrame'
    for body, where in ((make, 'TrailMake'), (frame, 'SmokeFrame')):
        assert 'smoke::Emitter(' in body, f'{where} places the emitter without smoke::Emitter'
        assert not re.search(r'-m\[8\]|m\[8\],m\[9\],m\[10\]', body), f'{where} works the emitter\'s way out by hand again'
    header = src('src/smoke_trail.h')
    rows = re.search(r'kObjectFrame\[16\]=\{([^}]*)\}', header)
    assert rows and [float(x) for x in rows.group(1).split(',')] == [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], \
        f'src/smoke_trail.h: kObjectFrame is not the identity ({rows and rows.group(1)})'


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


def _belly_model(belly: float) -> 'object':
    """A stand-in grounded jet: mdl; body (skin) with gear_nose (z 4) and gear_main_l / _r (z -2, x +-2) under it, then
    the object's bone; a flat belly (two skinned triangles on body) at y `belly` over x -3..3, z -6..6, and on each main
    gear leg a triangle down to the ground (y 0) beside the belly's centre line."""
    import struct
    from mdb import Bone, MatParam, MatTex, Material, Mdb, Mesh, Object, VElem
    import mdb_jet
    ident = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]

    def at(x: float, y: float, z: float) -> list[float]:
        return ident[:12] + [x, y, z, 1.0]
    binds = [ident, ident, at(0.0, 0.8, 4.0), at(2.0, 0.8, -2.0), at(-2.0, 0.8, -2.0), ident]
    parents = [-1, 0, 1, 1, 1, 0]
    kinds = [0, 3, 3, 3, 3, 2]
    bones = [Bone(i, parents[i], -1, -1, i, 0, kinds[i], 0, int(kinds[i] == 3), 0, 0, list(binds[i]),
                  ident[:12] + [-binds[i][12], -binds[i][13], -binds[i][14], 1.0], [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0])
             for i in range(6)]
    mdb_jet.link(bones)
    elems = [VElem(4, 0, 0, 'position'), VElem(1, 12, 0, 'BLENDWEIGHT'), VElem(21, 28, 0, 'BLENDINDICES')]
    pts = [((-3.0, belly, -6.0), 1), ((3.0, belly, -6.0), 1), ((3.0, belly, 6.0), 1), ((-3.0, belly, 6.0), 1),
           ((1.0, 0.0, -2.5), 3), ((1.2, 0.0, -1.5), 3), ((1.1, belly, -2.0), 3),
           ((-1.0, 0.0, -2.5), 4), ((-1.2, 0.0, -1.5), 4), ((-1.1, belly, -2.0), 4)]
    rows = b''.join(struct.pack('<3f4f4B', *p, 1.0, 0.0, 0.0, 0.0, bone, 0, 0, 0) for p, bone in pts)
    tris = struct.pack('<12H', 0, 1, 2, 0, 2, 3, 4, 5, 6, 7, 8, 9)
    me = Mesh(bytes((0, 1, 1, 0)), 0, 0, 32, elems, 0, rows, tris)
    mat = Material(0, 0, 0, 3, 'snd_BRDF_Common_Basic', [MatParam([0.5, 0.5, 0.5, 0.0], (0, 0), 'diffuse', 0x402)],
                   [MatTex(0, 'albedo', (0,) * 5)], 3)
    return Mdb(0x20, ['mdl', 'body', 'gear_nose', 'gear_main_l', 'gear_main_r', 'body_mesh', 'Material'], bones,
               [Object(5, 5, [me])], [mat], [])


# The stock 506 weapons' muzzles from their holder bone (Root.cpk: V_506HELI_GATLING01_L / _R, V_506HELI_MISSILE01's
# pods, V_409HELI_BOMB01's racks; vcobjects.hardpoint_muzzles reads them off the game).
_GUN_MUZZLES = ((-2.5, -0.24, 2.53), (2.5, -0.24, 2.53))
_STORE_MUZZLES = ((-1.96, -0.49, 1.1), (-1.76, -0.67, 0.2), (-1.37, -0.48, 1.5), (-1.05, -0.67, 0.2), (1.05, -0.67, 0.2),
                  (1.37, -0.48, 1.5), (1.76, -0.67, 0.2), (1.96, -0.49, 1.1))


@test
def jet_hardpoints_keep_the_muzzles_out_of_the_ground() -> None:
    """pylib/jet_hardpoints.py on a stand-in grounded jet (no game; the user, 2026-10-19: 「在地面停着的飞机射导弹，会在
    地底生成」): the stock muzzles on the root (the model's origin, the wheels' contact: what every jet hung its weapons
    on) are all under the ground; the hardpoints put the stores' bone over the main gear and the guns' over the nose gear,
    each as high as leaves every muzzle at or under the belly (the gear's triangles, down to the ground, not taken for
    airframe), every muzzle GROUND_CLEAR over the ground; the bones are transform only on the mesh bone, the model written
    and read back passes check_hardpoints, strip_hardpoints gives it back byte for byte; a belly too low for the missile
    pods' 0.67 m drop leaves no place (HardpointError), never a muzzle in the ground."""
    import jet_hardpoints as jh
    from mdb import mdb_read, mdb_write
    assert all(m[1] < 0.0 for m in _GUN_MUZZLES + _STORE_MUZZLES), 'every stock muzzle is under its holder bone'
    md = _belly_model(1.0)
    want = {jh.HP_GUN: _GUN_MUZZLES, jh.HP_STORE: _STORE_MUZZLES}
    hps = jh.hardpoints(md, want)
    at = {h.name: h.at for h in hps}
    assert [h.name for h in hps] == [jh.HP_GUN, jh.HP_STORE], hps
    assert max(abs(a - b) for a, b in zip(at[jh.HP_STORE], (0.0, 1.0 + 0.48, -2.0))) < 1e-6, at   # the racks on the belly
    assert max(abs(a - b) for a, b in zip(at[jh.HP_GUN], (0.0, 1.0 + 0.24, 4.0))) < 1e-6, at
    out = jh.with_hardpoints(md, hps)
    names = [out.name_of(b.name) for b in out.bones]
    assert names == ['mdl', 'body', 'gear_nose', 'gear_main_l', 'gear_main_r', 'hp_gun', 'hp_store', 'body_mesh'], names
    back = mdb_read(mdb_write(out))
    jh.check_hardpoints(back, hps)
    for h in hps:
        pts = jh.muzzle_points(back, h.name, h.muzzles)
        assert min(p[1] for p in pts) >= jh.GROUND_CLEAR and max(p[1] for p in pts) <= 1.0 + 1e-6, (h.name, pts)
        assert all(p[1] < 0.0 for p in jh.muzzle_points(back, 'mdl', h.muzzles)), 'on the root they were in the ground'
    assert back.objects[0].bone == back.bone_index('body_mesh')
    assert mdb_write(jh.strip_hardpoints(out)) == mdb_write(md), 'strip_hardpoints does not give the model back'
    low = _belly_model(0.5)   # 0.5 m: the pods 0.19 m under the racks still clear the ground
    pts = [p for h in jh.hardpoints(low, want) for p in jh.muzzle_points(jh.with_hardpoints(low, jh.hardpoints(low, want)),
                                                                          h.name, h.muzzles)]
    assert min(p[1] for p in pts) >= jh.GROUND_CLEAR, pts
    try:
        jh.hardpoints(_belly_model(0.1), want)
    except jh.HardpointError:
        pass
    else:
        raise AssertionError('a belly 0.1 m over the ground took the stores')


@test
def jet_weapons_hang_on_their_hardpoints() -> None:
    """pylib/vcobjects.py jet_sgo hangs each jet's weapons (jet_weapon_bones): on a model with hardpoints
    (HARDPOINT_FILES, which pylib/jet_models.py gives the bones) its guns on hp_gun, its missiles, rockets and bombs on
    hp_store, anything else (the blast drones' charges) on the anchor; on any other model (the stock bombers the airstrike
    takes over) all on the anchor; Jet.weapon_bones as given. tools/make_jets.py checks every SGO's holder bones against its
    model (check_weapon_bones), and tools/support_loadout.py hangs a loadout's pylons on the jet's store holder. With the
    game: every hardpoint model as made carries the bones, every muzzle GROUND_CLEAR over its ground and under its skin
    (check_hardpoints), where the same muzzles on the root were in the ground."""
    import jet_hardpoints as jh
    import jet_models
    import rootcpk
    assert vc.HARDPOINT_FILES == {vc.JET_ELEVON_FILE, *(f for f, r in jet_models.MODELS.items() if r.grounded),
                                  vc.GUNSHIP_MODEL_FILE}, sorted(vc.HARDPOINT_FILES)
    assert jet_models.ELEVON_FILE == vc.JET_ELEVON_FILE
    seen = 0
    for name, jet in vc.JETS.items():
        model = list(jet.model) if jet.model is not None else vc.JET_ELEVON_MODEL
        bones = vc.jet_weapon_bones(jet, model, jet.anchor)
        if jet.weapon_bones:
            assert bones == list(jet.weapon_bones), name
            continue
        if vc.model_file(model) not in vc.HARDPOINT_FILES:
            assert bones == [jet.anchor] * len(jet.weapons), name
            continue
        seen += 1
        for w, b in zip(jet.weapons, bones):
            kind = vc.store_of(w)
            want = jh.HP_GUN if w in vc._GUNS else jh.HP_STORE if kind and vc.STORES[kind[0]].role in \
                ('air', 'ground', 'bomb', 'rocket') else jet.anchor
            assert b == want, (name, w, b)
        assert vc.jet_weapon_bones(jet, vc.JET_MODEL, jet.anchor) == [jet.anchor] * len(jet.weapons), name
    assert seen >= 20, seen
    jets = src('tools/make_jets.py')
    assert 'check_weapon_bones(out, game)' in jets.split('\ndef build(', 1)[1].split('\ndef ', 1)[0]
    assert "jet_weapon_bones(jet, m['animation_model'][0], anchor)" in src('pylib/vcobjects.py')
    loadout = src('tools/support_loadout.py')
    assert 'store_row = next(' in loadout and '[list(store_row) for _ in l.pylons]' in loadout
    if not os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    game = rootcpk.Game(rootcpk.DEFAULT_GAME)
    muzzles = vc.hardpoint_muzzles(game)
    assert muzzles == {jh.HP_GUN: _GUN_MUZZLES, jh.HP_STORE: _STORE_MUZZLES}, muzzles
    from mdb import mdb_read, rab_read
    models = {f: jet_models._model_of(game, None if f == vc.JET_ELEVON_FILE else f)
              for f in vc.HARDPOINT_FILES if f != vc.GUNSHIP_MODEL_FILE}
    raw = jet_models.stock_copy(game, vc.GUNSHIP_MODEL_FILE)
    models[vc.GUNSHIP_MODEL_FILE] = mdb_read(next(f for f in rab_read(raw).files if f.name.lower() == 'bomber401.mdb').data)
    for f, md in models.items():
        r = jet_models.MODELS.get(f)
        hps = jet_models.hardpoints_for(game, jh.strip_hardpoints(md), f)
        jh.check_hardpoints(md, hps, bool(r and r.skids))
        for h in hps:
            assert min(p[1] for p in jh.muzzle_points(md, h.name, h.muzzles)) >= jh.GROUND_CLEAR, (f, h)
            assert min(p[1] for p in jh.muzzle_points(md, 'mdl', h.muzzles)) < 0.0, (f, h.name, 'was not in the ground')


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
def release_models_whole_folders() -> None:
    """tools/build_release.py ships every RELEASE_MODELS folder whole, its subfolders too: the cockpits' cut interiors
    (models/cockpits/parts/<vehicle>/, textures/, in the repo since #230) are below the folder's top (it once took the top
    files alone: the CI zip had the cockpits' CREDITS and none of the parts)."""
    import build_release
    import cockpits
    shipped = {arc for _src, arc in build_release.release_models()}
    root = os.path.join(ROOT, 'models', 'cockpits')
    want = {f'models/cockpits/{os.path.relpath(os.path.join(d, f), root).replace(os.sep, "/")}'
            for d, _dirs, files in os.walk(root) for f in files}
    assert any('/parts/' in w for w in want) and any('/textures/' in w for w in want), 'the repo holds the cockpit parts'
    assert want <= shipped, sorted(want - shipped)[:5]
    assert cockpits.ASSET_DIR == 'cockpits' and 'cockpits' in build_release.RELEASE_MODELS, cockpits.ASSET_DIR


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
def npc_vehicle_ai_declarations() -> None:
    """Player vehicles retain canonical AI metadata without requiring cockpit models."""
    sys.path.insert(0, os.path.join(ROOT, 'tests'))
    import test_npc_vehicle_ai_sgo
    test_npc_vehicle_ai_sgo.run_checks()


@test
def release_third_party_notices() -> None:
    """The release zip carries THIRD-PARTY-NOTICES.txt with the licence of the EDF6 SHKT Editor code ported to pylib
    (MIT, Copyright (c) 2026 Ktaro) next to edf6-cpk's; every ported module names its source and that licence."""
    import build_release
    text = build_release.notices()
    assert 'Copyright (c) 2026 Ktaro' in text and 'MIT License' in text and 'edf6-cpk' in text, text[:200]
    assert "z.writestr('THIRD-PARTY-NOTICES.txt', notices()" in src('tools/build_release.py')
    for mod in ('hkfile', 'hkshape', 'model_pieces', 'shkt_check', 'vehicle_check'):
        head = src(f'pylib/{mod}.py').split('from __future__', 1)[0]
        assert 'Ktaro' in head and 'LICENSE.edf6-shkt-editor' in head, mod


@test
def frozen_collision_consumer() -> None:
    """The package gate runs the real binding and geometry consumers on portable data."""
    import frozen_collision_probe
    frozen_collision_probe.check()


@test
def release_module_identity() -> None:
    """Recipes and frozen imports must use the same unambiguous runtime modules."""
    sys.path.insert(0, os.path.join(ROOT, 'tests'))
    import test_buildcache_module_identity
    test_buildcache_module_identity.run_checks()


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
        assert c.brings in ('jets', 'helis', 'sub', 'vehicle', 'throw', 'gun', 'ultimate', 'gravity'), c.id
        thrown = c.brings == 'throw'
        assert bool(c.drone) == thrown and (not c.drone or c.drone in drones), c.id
        # A thrown drone's marker: the bits of 1.0 with its code in the low ones, never 1.0 itself (calls.throw_mark).
        bits = calls.mark_bits(c)
        assert not thrown or (bits & ~0xFFF == calls.THROW_MARK_BASE and bits != calls.THROW_MARK_BASE), c.id
        assert bool(c.gun) == (c.brings == 'gun') and (not c.gun or c.reload == 0), c.id
        assert bool(c.weapon) == (c.brings == 'gravity'), c.id
        # The gravity gun's marker is a thrown drone's kind (the bits of 1.0 with a code in the low ones).
        assert c.brings != 'gravity' or (bits & ~0xFFF == calls.THROW_MARK_BASE and bits != calls.THROW_MARK_BASE), c.id
        assert bool(c.role) == (c.brings == 'jets') and (not c.role or c.role in roles), c.id
        assert bool(c.body) == (c.brings == 'helis') and (not c.body or c.body in bodies), c.id
        assert bool(c.vehicle) == (bool(c.jet) or bool(c.ground)) == (c.brings == 'vehicle'), c.id
        assert not (c.jet and c.ground) and (not c.ground or c.ground in vc.GROUND_VEHICLES), c.id
        assert not c.ground or vc.GROUND_VEHICLES[c.ground].sgo == c.vehicle, c.id
        assert (c.count > 0) == (c.flown or thrown) and bool(c.log) == (c.flown or thrown or c.brings == 'ultimate'), c.id
        assert not thrown or not c.flown, c.id   # not in the in-mission pick (kCalls)
        for lang in cw.LANGS:
            assert calls.call_name(c, lang) and calls.call_description(c, lang)
            assert calls.retired_name(c, lang) != calls.call_name(c, lang)
        assert calls.slot_of(c.id) == c.id and calls.slot_of(calls.retired_id(c.id)) == c.id
        assert not calls.retired_id(c.id).startswith(calls.ID_PREFIX)   # GrantCalls never owns a placeholder
    names = [calls.call_name(c, 'SC') for c in calls.FLOWN]
    assert len(set(names)) == len(names), 'two calls with one banner label'


@test
def gravity_gun_mark_in_plugin() -> None:
    """The gravity gun (tools/calls.py brings 'gravity'): src/gravitygun.cpp tells it by its AmmoHitSizeAdjust's bits (the
    call's mark) and its class's vtable (Weapon_LaserMarker: the template call_weapons.gravity_sgo keeps); its SGO is the kit
    with that mark, the reach and the damage scale the plugin reads, and its stat list the range and the zoom."""
    guns = [c for c in calls.CALLS if c.brings == 'gravity']
    assert len(guns) == 1, guns
    g = guns[0]
    plugin = src('src/gravitygun.cpp')
    m = re.search(r'kGravityMarkBits=0x([0-9A-Fa-f]+)', plugin)
    assert m and int(m.group(1), 16) == calls.mark_bits(g), 'gravitygun.cpp kGravityMarkBits vs tools/calls.py'
    assert 'kLaserMarkerVtable=0x17E45E8' in plugin and g.weapon == 'eLockonMarkerL01'
    assert cw.template_of(g) == 'eLockonMarkerL01' and g.reload == 0 and g.count == 0
    assert 'gravity::Pick' in plugin and 'gravity::HoldAt' in plugin and 'gravity::Throw' in plugin, 'the gun is the gravity operations\' user'
    assert 'GravityGunTick' in src('src/crew.cpp'), 'the gun runs from the shared tick'


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
    reach = cw.GUN_CURVES['AmmoSpeed'] * cw.GUN_SCALARS['AmmoAlive']
    assert reach == 90000.0, reach
    expected = [cw.GUN_CURVES[key] * unit for key, unit in units.items()] + [reach, 242.0]
    for lang in cw.LANGS:
        changed = cw._text_row(row, gun, lang, gun=cw.gun_stats(template))
        assert [st.items[2].items[0] for st in changed.items[2].items] == expected, lang
        assert changed.items[2].items[-1] == damage, 'damage and its star parameters stay unchanged'


@test
def boarding_gun_range_unlimited() -> None:
    """The boarding rifle shoots as far as anything can be (the user, 2026-10-11: "登车狙击枪的射击距离应该是无限远"):
    its reach (AmmoSpeed x AmmoAlive, its only range limit) is past the unlimited view distance at the largest
    BigWorld (src/crew.h UnlimitedViewDistance) and the stock far pass, with half again to spare (a fire modifier
    may scale a round's life, docs/boarding-re.md §2); its life stays a direct-fire round's (src/highcam.cpp, src/sightzoom.cpp: 600 frames is indirect fire) and
    flat (no drop). Only the boarding guns get it: GUN_LIFE is read by gun_sgo / gun_stats alone."""
    import ast
    plugin, crew_h = src('src/plugin.cpp'), src('src/crew.h')
    most = re.search(r'Fix\("BigWorld",n\.bigWorld,([\d.]+)f,([\d.]+)f\)', plugin)
    assert most, 'BigWorld clamp not found in src/plugin.cpp'
    diag = re.search(r'UnlimitedViewDistance\(\) noexcept \{ const float d=([\d.]+)f\*WorldHalf\(\)', crew_h)
    far = re.search(r'kStockDistantFar=([\d.]+)f', crew_h)
    assert diag and far, 'src/crew.h UnlimitedViewDistance changed'
    farthest = max(float(diag.group(1)) * float(most.group(2)), float(far.group(1)))
    speed, life = cw.GUN_CURVES['AmmoSpeed'], cw.GUN_SCALARS['AmmoAlive']
    assert life == cw.GUN_LIFE and life == int(life), life
    assert speed * life >= 1.5 * farthest, (speed * life, farthest)
    indirect = int(re.search(r'kIndirectLife=(\d+);', src('src/highcam.cpp')).group(1))
    assert 'round.alive>=600' in src('src/sightzoom.cpp')
    assert life < min(indirect, 600), 'the boarding rifle would count as indirect fire'
    assert cw.GUN_SCALARS['AmmoGravityFactor'] == 0.0 and cw.GUN_SCALARS['AmmoIsPenetration'] == 0.0
    # 10 shots a second, each alive `life` frames: few rounds in flight.
    assert life / cw.GUN_CURVES['FireInterval'] <= 20
    tree = ast.parse(src('tools/call_weapons.py'))
    users = {f.name for f in ast.walk(tree) if isinstance(f, ast.FunctionDef)
             for n in ast.walk(f) if isinstance(n, ast.Name) and n.id in ('GUN_LIFE', 'GUN_SCALARS', 'GUN_CURVES')}
    assert users == {'gun_sgo', 'gun_stats'}, users
    for lang in ('SC', 'CN', 'JA', 'EN'):
        assert f'{speed * life / 1000:.0f} ' in calls.KINDS['boarding_gun'][lang][1], lang
    for c in calls.CALLS:
        if c.brings == 'gun':
            assert calls.call_description(c, 'SC').startswith(calls.KINDS['boarding_gun']['SC'][1])


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
        # The range (the user, 2026-10-11: unlimited): the real weapon's speed x life, no falloff over it.
        assert weapon.get('AmmoAlive') == cw.GUN_LIFE and weapon.get('AmmoSpeed').items[0] == cw.GUN_CURVES['AmmoSpeed']
        assert weapon.get('AmmoSpeed').items[1:] == original.get('AmmoSpeed').items[1:], 'star curve kept'
        assert dsgo.to_py(weapon.get('AmmoDamageReduce')) == [1.0, 1.0], (call.id, 'damage falls off with range')
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


# ---------------------------------------------------------------- every call's SGO and text row
# The kinds a call brings (calls.Call.brings): tools/call_weapons.py weapon_sgo / _text_row branch on each. The 2026-10-20
# merge dropped ultimate_sgo's `return dsgo.write(doc)` and the ultimate text branch's `return row` and nothing offline saw
# it: these run every call through both, on synthetic templates (no game: CI) and on the real ones (with the game).
CALL_KINDS = ('jets', 'helis', 'sub', 'vehicle', 'throw', 'gun', 'ultimate', 'gravity')
_STOCK_PT = [1020.0, 21.0, 1.0, 10.0, 1.0, 0.30000001192092896, 0.0]          # a call's ReloadTime star curve
_SAT_DAMAGE = [670.0, 8.0, 0.0, 10.0, 0.800000011920929, 0.699999988079071, 1.0]   # Spritefall Destroy's AmmoDamage
_SAT_PT = [3200.0, 21.0, 1.0, 10.0, 1.0, 0.30000001192092896, 0.0]                 # ...and its ReloadTime


def _dnode(v):
    """A plain value as DSGO: a list a list node, a dict a named node."""
    if isinstance(v, dict):
        return dsgo.Node([_dnode(x) for x in v.values()], dict(enumerate(v)))
    return dsgo.Node([_dnode(x) for x in v]) if isinstance(v, list) else v


def _synthetic_call_sgo(c: calls.Call) -> bytes:
    """A stand-in for call `c`'s stock template with the fields its kind's builder reads and writes."""
    k = c.brings
    if k in ('jets', 'helis', 'sub'):
        root = {'AmmoHitSizeAdjust': 0.0, 'ReloadTime': list(_STOCK_PT),
                'Ammo_CustomParameter': [0.0, 'bomber', [1.0, 3.0, 0.5], 'app:/object/x.sgo']}
    elif k == 'vehicle':
        entry, fuel = ['app:/weapon/v_506heli_gatling01_l.sgo', [1.0, 0.0]], ['app:/weapon/v_fueltank.sgo', [1.0]]
        if c.ground:
            n = 2 if len(vc.GROUND_VEHICLES[c.ground].slots) > 2 else len(cw.vehicle_weapons(c))
            setup = [[1.0, 1.0], [0.0, 1.0], [[f'app:/weapon/stock_{i}.sgo', [1.0, [0.0, 0.0]]] for i in range(n)]]
        else:
            setup = [[1.0, 1.0], [0.0, 1.0, 2.0], [100.0], [entry, fuel]]
        root = {'use_underground': 0.0, 'ReloadTime': [8000.0, 21.0, 0.0, 8.0, 1.0, 0.5, 0.0],
                'Ammo_CustomParameter': [0.0, 0.0, 0.0, 0.0,
                                         ['transport', 'box', 'app:/object/v506_heli.sgo', setup, ['voice']]],
                'resource': ['app:/object/v506_heli.sgo', 'app:/weapon/v_506heli_gatling01_l.sgo']}
    elif k == 'throw':
        root = {'AmmoHitSizeAdjust': 1.0, 'AmmoCount': [8.0, 0.0, 0.0, 7.0, 0.5, 0.5, 0.0],
                'ReloadTime': [480.0, 21.0, 2.0, 8.0, 1.0, 0.5, 0.0]}
    elif k == 'gun':
        bases = {'AmmoSpeed': 25.0, 'FireAccuracy': 0.125, 'AmmoCount': 8.0, 'ReloadTime': 360.0, 'FireInterval': 90.0}
        root = {key: [base, float(i + 10), 0.5, 2.0, 1.0] for i, (key, base) in enumerate(bases.items())}
        root.update({key: 0.0 for key in cw.GUN_SCALARS})
        root.update({'AmmoAlive': 40.0, 'AmmoColor': [0.25, 0.5, 0.75, 1.0], 'AmmoClass': 'LaserBullet01',
                     'Ammo_CustomParameter': [1.0]})
    elif k == 'ultimate':
        root = {'AmmoHitSizeAdjust': 0.0, 'AmmoDamage': list(_SAT_DAMAGE), 'ReloadTime': list(_SAT_PT),
                'Ammo_CustomParameter': [0.0, 0.0, 0.0, 0.0, [0.0, 0.0, 0.0, 0.0, 'LaserBullet02'],
                                         list(cw.ULTIMATE_VOICES[0])],
                'Ammo_EquipVoice': list(cw.ULTIMATE_VOICES[1])}
    elif k == 'gravity':
        root = {'AmmoHitSizeAdjust': 0.0, 'AmmoSpeed': [30.0, 8.0, 0.0, 10.0, 0.5, 0.5, 1.0], 'AmmoAlive': 1.0,
                'AmmoColor': [1.0, 0.0, 0.0, 1.0], 'AmmoSize': 0.5, 'AmmoDamage': 0.0}
    else:
        raise AssertionError(f'{c.id}: a kind this test does not know: {k}')
    root.update({f'name.{lang.lower()}': f'stock {lang}' for lang in cw.LANGS})
    return dsgo.write(dsgo.Document(_dnode(root), []))


def _synthetic_call_text(c: calls.Call, template: bytes) -> dsgo.Node:
    """A stand-in for call `c`'s template's menu row: [name, description, stat lines] as its stock one is laid out."""
    k = c.brings
    if k in ('jets', 'helis', 'sub'):
        stats = [['Planes', '3'], ['Reload', '$0pt', list(_STOCK_PT)]]
    elif k == 'vehicle':
        stats = [['Re-request', '$0pt', [8000.0, 21.0, 0.0, 8.0, 1.0, 0.5, 0.0]], ['Durability', '1000'],
                 ['Fuel', '100'], ['Fuel cost', '1']]
    elif k == 'throw':
        stats = [['Number', '$0', [8.0, 0.0, 0.0, 7.0, 0.5, 0.5, 0.0]], ['Damage', '$0', [18.0]], ['Search', '$0m', [30.0]],
                 ['Reload', '$0 sec', [8.0, 21.0, 2.0, 8.0, 1.0, 0.5, 1.0]], ['Spread', '75.0m']]
    elif k == 'gun':
        stats = [[f'line {i}', '', [old, *stars, -1.0]] for i, (old, stars, _) in enumerate(cw.gun_stats(template))]
    elif k == 'ultimate':
        stats = [['Damage', '$0', list(_SAT_DAMAGE)], ['Attack Range', 'Radius 70.0m'], ['Reload', '$0pt', list(_SAT_PT)],
                 ['Laser Range', '$0m', [600.0, 16.0, 2.0, 10.0, 0.8, 1.0, 1.0]], ['Shots', '160'], ['Zoom', '4.0x']]
    else:   # gravity
        stats = [['Projection Range', '$0m', [30.0, 8.0, 0.0, 10.0, 0.5, 0.5, 1.0]], ['Lock-on Speed', '1.0x'],
                 ['Lock-on Range', '300m'], ['Zoom', '2.0x']]
    return _dnode(['stock name', 'stock description', stats])


def _check_call_outputs(c: calls.Call, out: object, row: object, lang: str, durability: float | None) -> None:
    """Call `c`'s weapon_sgo result `out` and its `lang` text row `row` (made with `durability`): bytes / a Node, and
    the fields its kind writes written."""
    assert isinstance(out, bytes) and out, f'{c.id}: weapon_sgo gave {type(out).__name__}, not the SGO'
    r = dsgo.parse(out).root
    if 'name.en' in r.names.values():
        assert r.get('name.en') == calls.call_name(c, 'EN'), (c.id, r.get('name.en'))
    k = c.brings
    if k in ('jets', 'helis', 'sub', 'throw', 'ultimate', 'gravity'):
        assert r.get('AmmoHitSizeAdjust') == float(c.mark), (c.id, r.get('AmmoHitSizeAdjust'), c.mark)
    if k in ('jets', 'helis', 'sub'):
        assert r.get('ReloadTime').items[0] == c.reload, c.id
        assert r.get('Ammo_CustomParameter').items[2].items[1] == c.count, c.id
    elif k == 'vehicle':
        assert r.get('ReloadTime').items[0] == c.reload, c.id
        assert r.get('Ammo_CustomParameter').items[4].items[2] == cw._object_path(c), c.id
        assert r.get('use_underground') == (1.0 if c.underground else 0.0), c.id
        assert cw._object_path(c) in r.get('resource').items, c.id
    elif k == 'throw':
        assert r.get('AmmoCount').items[0] == c.count and r.get('ReloadTime').items[0] == c.reload, c.id
    elif k == 'gun':
        for key, value in cw.GUN_CURVES.items():
            field = r.get(key)
            assert (field.items[0] if isinstance(field, dsgo.Node) else field) == value, (c.id, key)
        assert r.get('AmmoClass') == 'SolidBullet01' and r.get('AmmoAlive') == cw.GUN_LIFE, c.id
    elif k == 'ultimate':
        # The damage and the pt the call asks for at every star count (a call is owned at 0 stars: the 2026-10-20 2400).
        for stars in range(11):
            assert cw.star_value(r.get('AmmoDamage'), stars) == cw.ULTIMATE_DAMAGE, \
                (c.id, stars, dsgo.to_py(r.get('AmmoDamage')))
            assert cw.star_value(r.get('ReloadTime'), stars) == c.reload, (c.id, stars, dsgo.to_py(r.get('ReloadTime')))
        voices = (*r.get('Ammo_CustomParameter').items[5].items, *r.get('Ammo_EquipVoice').items)
        assert all(v == '' for v in voices), (c.id, 'its radio lines silenced', voices)
    elif k == 'gravity':
        assert r.get('AmmoSpeed').items[0] == cw.GRAVITY_RANGE and r.get('AmmoDamage') == 1.0, c.id
        assert dsgo.to_py(r.get('AmmoColor')) == list(cw.GRAVITY_COLOR) and r.get('AmmoSize') == cw.GRAVITY_WIDTH, c.id
    assert isinstance(row, dsgo.Node), f'{c.id} {lang}: _text_row gave {type(row).__name__}, not the row'
    assert row.items[0] == calls.call_name(c, lang) and row.items[1] == calls.call_description(c, lang), (c.id, lang)
    stats = dsgo.to_py(row.items[2])
    if k in ('jets', 'helis', 'sub', 'vehicle'):
        pt = [s for s in stats if len(s) == 3 and str(s[1]).startswith('$0pt')]
        assert pt and all(s[2][0] == c.reload for s in pt), (c.id, lang, stats)
    if k == 'vehicle' and durability is not None:
        assert stats[1][1] == f'{durability:.0f}', (c.id, lang, stats[1])
    elif durability is not None:
        assert f"'{durability:.0f}'" not in repr(stats), (c.id, lang, "a vehicle request's durability line written", stats)
    if k == 'throw':
        assert len(stats) == 2 and stats[0][2][0] == c.count and abs(stats[1][2][0] - c.reload / 60.0) < 1e-9, (c.id, stats)
    elif k == 'ultimate':
        assert len(stats) == 5, (c.id, lang, "the stock beam's round count left", stats)
        damage, reload = stats[cw._ULT_DAMAGE_STAT][2], stats[cw._ULT_RELOAD_STAT][2]
        for stars in range(11):
            assert cw.star_value(damage, stars) == cw.ULTIMATE_DAMAGE and cw.star_value(reload, stars) == c.reload, \
                (c.id, lang, stars, damage, reload)
        assert str(cw.ULTIMATE_RADIUS) in stats[cw._ULT_RANGE_STAT][1], (c.id, lang, stats[cw._ULT_RANGE_STAT])
    elif k == 'gravity':
        assert len(stats) == 2 and stats[0][2][0] == cw.GRAVITY_RANGE, (c.id, lang, stats)


@test
def every_call_kind_builds_synthetic() -> None:
    """Every call (calls.CALLS, every kind it brings) through weapon_sgo and _text_row on stand-ins for its template
    (no game needed: CI): the SGO comes back as bytes with its kind's fields written, each language's menu row as a
    Node with its name, description and kind's lines; a vehicle request's durability goes on its durability line and
    on no other kind's (the ultimate's row is not a vehicle request's)."""
    kinds = {c.brings for c in calls.CALLS}
    assert kinds == set(CALL_KINDS), f'a kind of call with no stand-in here: {sorted(kinds - set(CALL_KINDS))}'
    for c in calls.CALLS:
        template = _synthetic_call_sgo(c)
        out = cw.weapon_sgo(template, c, [(0.0, 1.0, 1.0), (5.0, 10.0, 10.0)] if c.brings == 'vehicle' else None)
        text = _synthetic_call_text(c, template)
        stats = cw.gun_stats(template) if c.brings == 'gun' else None
        for lang in cw.LANGS:
            _check_call_outputs(c, out, cw._text_row(text, c, lang, 4321.0, stats), lang, 4321.0)
            if c.brings != 'vehicle':
                _check_call_outputs(c, out, cw._text_row(text, c, lang, None, stats), lang, None)
    # The star curve as EDF.dll 0xD7CE0 reads it: the satellite's damage a fifth of its base at 0 stars, its base at 5,
    # about 1.5 times at 10 (the most); its pt twice its base at 0 (a falling curve), its base at 5.
    near = lambda a, b: abs(a - b) <= 1e-6 * abs(b)   # noqa: E731  (the game's float shares, read back as doubles)
    assert near(cw.star_value([12000.0, *_SAT_DAMAGE[1:]], 0), 2400.0)
    assert near(cw.star_value([12000.0, *_SAT_DAMAGE[1:]], 5), 12000.0)
    assert near(cw.star_value([12000.0, *_SAT_DAMAGE[1:]], 10), 12000.0 * (0.2 + 0.8 * 2 ** 0.7))
    assert near(cw.star_value([15000.0, *_SAT_PT[1:]], 0), 30000.0) and near(cw.star_value([15000.0, *_SAT_PT[1:]], 5), 15000.0)
    assert cw.star_value([15000.0, *_SAT_PT[1:]], 12) == cw.star_value([15000.0, *_SAT_PT[1:]], 10), 'most stars 10'


@test
def every_call_kind_builds_real() -> None:
    """With the game: every call through weapon_sgo and _text_row on its real stock template, SGO and menu rows, as
    call_weapons.stack makes them (the vehicle requests on their families' curves, the durability shown), the same
    checks as every_call_kind_builds_synthetic. The satellite's damage curve is still the one the flat curve was made
    against (a fifth of its base at the 0 stars a call is owned with)."""
    import rootcpk
    game = rootcpk.DEFAULT_GAME
    if not os.path.isfile(os.path.join(game, 'Root.cpk')):
        return
    ids = cw.row_ids(cw.stock(game, cw.TABLE))
    texts = {lang: cw._rows(dsgo.parse(cw.stock(game, rel)), rel) for lang, rel in zip(cw.LANGS, cw.TEXTS)}
    sgo = {t: cw.stock(game, f'WEAPON/{t.upper()}.SGO') for t in cw.templates()}
    stock_damage = dsgo.parse(sgo[cw.ULTIMATE_TEMPLATE]).root.get('AmmoDamage')
    assert abs(cw.star_value([cw.ULTIMATE_DAMAGE, *stock_damage.items[1:]], 0) - cw.ULTIMATE_DAMAGE * 0.2) < 0.01, \
        dsgo.to_py(stock_damage)
    families = {cw.request_family(c) for c in calls.CALLS if c.brings == 'vehicle'}
    curves = {f: cw.request_curve(game, f) for f in families}
    for c in calls.CALLS:
        t = cw.template_of(c)
        out = cw.weapon_sgo(sgo[t], c, curves.get(cw.request_family(c)))
        durability = cw.vehicle_durability(game, c) if c.brings == 'vehicle' else None
        stats = cw.gun_stats(sgo[t]) if c.brings == 'gun' else None
        at = cw._template_index(ids, t)
        for lang in cw.LANGS:
            _check_call_outputs(c, out, cw._text_row(texts[lang][at], c, lang, durability, stats), lang, durability)


@test
def sound_at_takes_a_still_sound() -> None:
    """SoundAt's contract (src/crew.h, src/sound_place.h): `vel` may be nullptr, a still sound; the arithmetic that reads
    it is sound_place.h's, which checks it (2026-10-20 smoke test: the ultimate's recordings passed none, jetsound.cpp's
    Place read through it and the show's step faulted on every cue). jetsound.cpp reads no velocity outside it; the
    offline check runs in CI."""
    place, jet = src('src/sound_place.h'), src('src/jetsound.cpp')
    assert 'if(!v)return 0.0f;' in place and 'Closing(vel,u)' in place and 'Closing(camVel,u)' in place
    assert '*out=soundplace::Place(listener,listenerVel,pos,vel);' in jet
    assert not re.search(r'Dot\(\s*(vel|listenerVel)\s*,', jet), 'jetsound.cpp reads a velocity outside sound_place.h'
    assert '`vel` may be nullptr' in src('src/crew.h')
    cmake = src('CMakeLists.txt')
    assert 'add_executable(sound_place_check EXCLUDE_FROM_ALL tools/sound_place_check.cpp)' in cmake
    assert re.search(r'set\(EDF6_OFFLINE_CHECKS[^)]*\bsound_place_check\b', cmake)


@test
def cockpit_fields_current() -> None:
    """The instruments' field numbers and the round classes are one table (pylib/cockpit_fields.py): the C++ enums are
    generated from it, and the model side imports it, not a copy."""
    import gen_cockpit_fields
    import cockpit_fields as cf
    with open(gen_cockpit_fields.OUT, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_cockpit_fields.render(), \
            'src/cockpit_fields.inc is stale: python tools/gen_cockpit_fields.py'
    assert '#include "cockpit_fields.inc"' in src('src/cockpit_instruments.h')
    assert '#include "cockpit_fields.inc"' in src('src/hud_cue.h')
    assert 'enum Field' not in src('src/cockpit_instruments.h') and 'enum class CockpitRound' not in src('src/hud_cue.h')
    assert 'from cockpit_fields import' in src('pylib/cockpit_instruments.py')
    # The numbers the models already built carry (appended only).
    assert (cf.HP, cf.POWER, cf.ARM, cf.PICK, cf.ROW, cf.ROWROUND, cf.ROLL, cf.COUNT) == (0, 11, 12, 16, 17, 20, 23, 24)
    assert cf.ROUNDS == ('MG', 'GUN', 'AP', 'HE', 'HEAT', 'CAN', 'BEAM', 'RKT', 'MSL', 'AAM', 'BOMB', 'SPC')
    # A stock weapon's round class reads the sight's tag by reticle::Shell, not by bare numbers.
    hud_cue = src('src/hud_cue.h')
    assert 'reticle::Shell shell' in hud_cue and 'case reticle::Shell::canister: return CockpitRound::can;' in hud_cue


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
def weapon_rows_never_past_the_records_the_game_keeps() -> None:
    """The table is never longer than the records the game will keep (2026-10-24 review of #246: past them it writes
    over the game status for every row beyond): install allows the plugin's 0x2000 only with the EDF.dll it relocates
    them on and without edf6_weaponlimit.dll (call_weapons.record_room), else the game's own 0x800; uninstall, when the
    plugin goes, always deletes the rows past 0x800 (ours: they end the table) and refuses when another mod's are there
    (past_room)."""
    with tempfile.TemporaryDirectory() as game:
        assert cw.record_room(game)[0] == cw.NATIVE_WEAPONS, "no EDF.dll: the game's own records only"
        pe = bytearray(0x200)
        pe[0:2] = b'MZ'
        pe[0x3C:0x40] = (0x80).to_bytes(4, 'little')
        pe[0x80:0x84] = b'PE\0\0'
        for stamp, room in ((cw.EDF_STAMP, cw.SAVE_WEAPONS), (cw.EDF_STAMP + 1, cw.NATIVE_WEAPONS)):
            pe[0x88:0x8C] = stamp.to_bytes(4, 'little')
            with open(os.path.join(game, 'EDF.dll'), 'wb') as f:
                f.write(pe)
            assert cw.record_room(game)[0] == room, (hex(stamp), cw.record_room(game))
        pe[0x88:0x8C] = cw.EDF_STAMP.to_bytes(4, 'little')
        with open(os.path.join(game, 'EDF.dll'), 'wb') as f:
            f.write(pe)
        os.makedirs(os.path.join(game, 'Mods', 'Plugins'))
        open(os.path.join(game, 'Mods', 'Plugins', 'EDF6_WeaponLimit.dll'), 'wb').close()
        room = cw.record_room(game)
        assert room[0] == cw.NATIVE_WEAPONS and cw.OTHER_RELOCATION in room[1], room
    for rows, room, refused in ((cw.SAVE_WEAPONS, (cw.SAVE_WEAPONS, None), False),
                                (cw.NATIVE_WEAPONS + 1, (cw.NATIVE_WEAPONS, 'no EDF.dll'), True),
                                (cw.SAVE_WEAPONS + 1, (cw.SAVE_WEAPONS, None), True)):
        try:
            cw.check_room(rows, room)
        except SystemExit:
            assert refused, (rows, room)
        else:
            assert not refused, (rows, room)
    import ported_weapons as pw
    stock_ids = [f'stock{i}' for i in range(cw.NATIVE_WEAPONS - 10)]
    ours = list(calls.IDS) + list(pw.IDS)
    assert len(stock_ids) + len(ours) > cw.NATIVE_WEAPONS
    ids = stock_ids + ours
    assert cw.past_room(ids) == cw.NATIVE_WEAPONS and all(cw.slot_of(x) for x in ids[cw.NATIVE_WEAPONS:])
    assert cw.past_room(ids[:cw.NATIVE_WEAPONS]) == cw.NATIVE_WEAPONS
    foreign = stock_ids + ours[:20] + ['another_mods_row'] + ours[20:]
    try:
        cw.past_room(foreign)
    except SystemExit:
        pass
    else:
        raise AssertionError("another mod's row past 0x800 was left for the game to write past")


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
def dreadnought_agrees() -> None:
    """src/dreadnought.cpp's hand copies of pylib/vcobjects.py: its mark (and body506.cpp's range), its keel, its turrets
    (dreadnought_turrets.inc kTurrets and their bone names), its fighters' first mark; the fighters' bodies in
    src/jet_internal.h kBodies are vcobjects.DREADNOUGHT_FIGHTERS', in order, mark for mark."""
    code = src('src/dreadnought.cpp')
    assert f'kDreadnoughtMark={vc.DREADNOUGHT_MARK:.1f}f' in code
    assert f'kKeel={vc.DREADNOUGHT_KEEL}f' in code, 'src/dreadnought.cpp kKeel != vcobjects.DREADNOUGHT_KEEL'
    turrets = src('src/dreadnought_turrets.inc')
    assert f'constexpr int kTurrets={vc.DREADNOUGHT_TURRETS};' in turrets
    for n in range(vc.DREADNOUGHT_TURRETS):
        assert f'{{L"dn_turret_{n}",L"dn_turret_{n}_gun",L"dn_turret_{n}_muzzle"}}' in turrets, n
    marks = [m for _, m, *_ in vc.DREADNOUGHT_FIGHTERS.values()]
    assert f'kDnFighterMark={marks[0]:.1f}f' in code and marks == [marks[0] + k for k in range(len(marks))], marks
    table = src('src/jet_internal.h').split('kBodies[kBodyCount]={', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{Body::dn(\w+),L"app:/object/(edf6vc_dn_\w+\.sgo)",L"(EDF6VC_DN_\w+\.SGO)",(\d+)\.0f', table)
    assert [(k.lower(), f'edf6vc_dn_{k.lower()}.sgo', vc.dn_file(k, 'SGO'), m) for k, (_, m, *_r) in
            vc.DREADNOUGHT_FIGHTERS.items()] == [(a.lower(), b, c, float(d)) for a, b, c, d in rows], rows
    assert f'{{{vc.DREADNOUGHT_MARK:.1f}f,' in src('src/body506.cpp').replace(' ', ''), 'src/body506.cpp kMarks'
    # its hangar modules (the user, 2026-10-24): the board reach src/dreadnought_deck.h OverHangar uses is
    # dreadnought_model.HANGAR_REACH (tools/make_dreadnought.py holds the model's modules to it); their four ini keys, in
    # module order, are the ones src/plugin.cpp reads into Config::dreadnoughtHangars and the shipped ini carries (all 1)
    import dreadnought_model as dm
    deck = src('src/dreadnought_deck.h')
    for name, key in (('Across', 'across'), ('Along', 'along'), ('Below', 'below'), ('Above', 'above')):
        assert f'constexpr float kHangar{name}={dm.HANGAR_REACH[key]}f;' in deck, f'src/dreadnought_deck.h kHangar{name}'
    assert f'constexpr int kHangars={dm.HANGARS};' in deck and dm.BAYS == 2 * dm.HANGARS
    plugin = src('src/plugin.cpp')
    keys = re.search(r'kHangarKeys\[\]=\{([^}]*)\}', plugin)
    assert keys and re.findall(r'L"(\w+)"', keys.group(1)) == list(dm.HANGAR_KEYS), 'src/plugin.cpp kHangarKeys'
    shipped = src('EDF6VehicleCrew.ini')
    assert dm.hangars_wanted(shipped) == dm.ALL_HANGARS and all(f'\n{k}=1\n' in shipped for k in dm.HANGAR_KEYS)
    for text, want in (('[VehicleCrew]\nDreadnoughtHangarLeftAft=0\ndreadnoughthangarrightfore = 0 ; off\n', (True, False, False, True)),
                       ('[VehicleCrew]\nDreadnoughtHangarLeftFore=0\nDreadnoughtHangarLeftFore=1\n', (False, True, True, True)),
                       ('[Other]\nDreadnoughtHangarLeftFore=0\n', dm.ALL_HANGARS),
                       ('[VehicleCrew]\nDreadnoughtHangarRightAft=no\n', (True, True, True, False))):
        assert dm.hangars_wanted(text) == want, (text, dm.hangars_wanted(text))


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
        if c.jet == vc.DREADNOUGHT_JET:   # the Dreadnought: tools/make_dreadnought.py writes it, src/dreadnought.cpp knows its mark
            import make_dreadnought
            assert jet.mark == c.mark == vc.DREADNOUGHT_MARK and make_dreadnought.SGO_FILE == f'{c.vehicle}.SGO', c.id
            assert f'kDreadnoughtMark={vc.DREADNOUGHT_MARK:.1f}f' in src('src/dreadnought.cpp'), 'src/dreadnought.cpp kDreadnoughtMark'
            assert f'{{{vc.DREADNOUGHT_MARK:.1f}f,' in src('src/body506.cpp').replace(' ', ''), 'src/body506.cpp kMarks lacks the Dreadnought'
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
    # Proteus's shield: tools/make_proteus.py; the ultimate strike's rounds: tools/make_ultimate.py
    import make_proteus
    # ...and the Dreadnought's fighters: tools/make_dreadnought.py
    import make_dreadnought
    written = ({n.split('/', 1)[1] for n in make_jets.names()} | {vc.DRILL_CHARGE_FILE} | set(vc.EMC_FILES)
               | set(vc.SAZABI_ROUND_FILES) | {make_proteus.SHIELD_FILE.split('/', 1)[1]} | set(vc.ULT_FILES)
               | set(make_dreadnought.FIGHTER_SGOS.values()))
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
    assert re.search(r'enum class EmcRound \{ beam, sight, breakCharge, blast, szMega, szCharge, szFunnel, proteusShield,\s+ultSight, ultBolt, ultSpark, ultBeam, ultCore, ultBlast,\s+ultGather \};',
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
def ultimate_copies_agree() -> None:
    """The ultimate strike (src/ultimate.cpp, src/ultimate_plan.h, pylib/vcobjects.py ULT_*, tools/make_ultimate.py,
    tools/call_weapons.py ULTIMATE_*): src/jet_bay.cpp preloads and fires exactly the files tools/make_ultimate.py writes,
    after the EMC's, the Sazabi's and the Proteus's, in EmcRound's order; the C++ copies of the area, a bolt's rounds and a
    ring's blast are the Python ones; the call is one satellite call of ours (its marker unique, its template a fire state
    the hook knows); the hook, the tick, the mission reset and the build are wired; the tool is the installer's and the
    ledger's; the offline check is a target. With the game: the SGOs build and pass check_ult."""
    import make_ultimate
    bay, plan = src('src/jet_bay.cpp'), src('src/ultimate_plan.h')
    files = re.findall(r'\{L"app:/object/(edf6vc_ult_[a-z_]+\.sgo)",L"(EDF6VC_ULT_[A-Z_]+\.SGO)"', bay)
    assert [f for _, f in files] == list(vc.ULT_FILES) and all(s == f.lower() for s, f in files), files
    assert bay.index('EDF6VC_PROTEUS_SHIELD.SGO') < bay.index(vc.ULT_FILES[0]), "kEmcFiles: the ultimate's after the Proteus's"
    import magic_circle
    assert make_ultimate.names() == [f'OBJECT/{n}' for n in (*vc.ULT_FILES, *magic_circle.files())]
    assert make_ultimate.OWNER in ledger.OWNERS
    # the circles: src/ultimate.cpp kCircleFiles in CircleModel's order are pylib/magic_circle.py LAYERS'
    strike_src = src('src/ultimate.cpp')
    circles = re.findall(r'\{L"app:/object/(edf6vc_ult_circle_[a-z]+\.sgo)",L"(EDF6VC_ULT_CIRCLE_[A-Z]+\.SGO)"', strike_src)
    assert [f for _, f in circles] == [magic_circle.sgo_file(k) for k in magic_circle.LAYERS] and \
        all(s == f.lower() for s, f in circles), circles
    model = re.search(r'enum class CircleModel : unsigned char \{ ([a-z, ]+) \};', plan)
    assert model and [m.strip().upper() for m in model.group(1).split(',')] == list(magic_circle.LAYERS), model
    radii = re.search(r'kModelRadius\[kModelCount\]=\{([^}]*)\}', plan)
    assert radii and [float(x.strip().rstrip('f')) for x in radii.group(1).split(',')] ==         [magic_circle.LAYERS[k].radius for k in magic_circle.LAYERS], radii
    assert 'PreloadUltimate(mgr);' in src('src/jet_spawn.cpp')
    for name, value in (('kRadius', cw.ULTIMATE_RADIUS), ('kRingBlast', vc.ULT_BLAST_RADIUS), ('kSparkRadius', vc.ULT_SPARK_RADIUS)):
        m = re.search(rf'\b{name}=([\d.]+)f', plan)
        assert m and float(m.group(1)) == value, (name, m and m.group(1), value)
    m = re.search(r'\bkGatherReach=([\d.]+)f', plan)
    assert m and float(m.group(1)) == vc.ULT_GATHER_REACH, m and m.group(1)
    m = re.search(r'\bkBoltFrames=(\d+);', plan)
    assert m and int(m.group(1)) == vc.ULT_BOLT_ROUNDS, m and m.group(1)
    ults = [c for c in calls.CALLS if c.brings == 'ultimate']
    assert ults and all(cw.template_of(c) == cw.ULTIMATE_TEMPLATE and c.mark >= 1 and float(c.mark).is_integer()
                        for c in ults), ults
    strike, crew, airstrike = src('src/ultimate.cpp'), src('src/crew.cpp'), src('src/airstrike.cpp')
    assert 'InstallUltimate()' in airstrike and 'UltimateStart(owner,aim,' in airstrike
    assert 'GuardedTick(kStepUltimate,&UltimateTick);' in crew and 'ResetUltimate();' in src('src/mission.cpp')
    assert 'EmcRound::ultBeam' in strike and 'kSkyBackSec' in strike
    inst = src('tools/installer.py')
    assert 'build_asset(cache, make_ultimate,' in inst and 'make_ultimate.install(game, ult)' in inst and 'make_ultimate.remove' in inst
    assert "'ultimate'" in src('pylib/buildcache.py')
    cmake = src('CMakeLists.txt')
    assert 'src/ultimate.cpp' in cmake and 'add_executable(ultimate_check EXCLUDE_FROM_ALL tools/ultimate_check.cpp)' in cmake
    import rootcpk
    if os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        make_ultimate.build(rootcpk.DEFAULT_GAME)


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
def shkt_check_on_synthetic_data() -> None:
    """pylib/shkt_check.py without the game (tests/test_shkt_check.py Portable): hull / box geometry, SHKT refs, and
    an SGO whose ragdoll mapping or accessory names a body its SHKT lacks is an error, a 4.1 ragdoll (Havok 2014
    tagfile, pylib/hk2014.py) included; the module's massDistribution note matches pylib/aircraft_collision.py. The
    stock-file mutations of the same test file need the game; tools/check_shkt.py checks an installed Mods folder."""
    import unittest
    sys.path.insert(0, os.path.join(ROOT, 'tests'))
    import test_shkt_check
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(test_shkt_check.Portable)
    result = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(suite)
    assert result.wasSuccessful() and result.testsRun == 8, (result.failures, result.errors)


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
    # One snapshot of the seat's sight decides the field (SightZoomScope, sightzoom.h ScopeField) and whose stock
    # crosshair gives way to it (scope_hold.h PublishScopeDrawn); SightZoomView still tells the overhead view.
    assert 'SightZoomScope(&sight)' in draw and 'sightzoom::MaskOf(sight.kind)' in draw and 'SightZoomView()' in draw
    assert 'PublishScopeDrawn(drawn ? sight.human : nullptr)' in draw and 'HudScopeHoldsCrosshair(soldier)' in src('src/infantryscope.cpp')
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
        # The clean-up's archive against the unpruned one: before the cockpit's graft (cockpits.graft_files, the last
        # step of the build, adds its own textures; tests/cockpit_build_test.py checks that one).
        from unittest.mock import patch
        import cockpits
        with patch.object(cockpits, 'graft_files', lambda game, made: made):
            plain = make_katyusha.build(rootcpk.DEFAULT_GAME)
        katyusha_kept_whole_bar_the_prune(rootcpk.DEFAULT_GAME, plain[f'OBJECT/{make_katyusha.MODEL_FILE}'])


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
    the ownership rule (aimlink.h PlayerGunRule: with the turret camera, no lock) returns
    before the turn input is written (the flak's Steer, the gunners' SteerSeat), and each write is reported for the V2
    Steers; the two plugins' link (common/edf/aimlink.h) exports exactly the names each looks up; EDF6VehicleCrew's two
    keys are read, shipped and documented; the jets' pick scores by the view only with PlayerJetLockByView."""
    at, ini = src('autoturret/src/plugin.cpp'), src('autoturret/EDF6AutoTurret.ini')
    zh, en = src('autoturret/README.zh-CN.md'), src('autoturret/README.md')
    for key in ('LockKey', 'LockButton', 'LockCone', 'LockRange', 'LockClearMs'):
        assert f'L"{key}"' in at and re.search(rf'^{key}=', ini, re.M) and key in zh and key in en, key
    # One player turret aim (2026-10-25, "这个自动瞄准是不是多余了一个版本，删掉一个"): the lead-circle mode, its keys and
    # its HUD lines are gone from both plugins and the ini (the READMEs name them only as removed).
    for key in ('AimMode', 'AimModeKey', 'AimModeButton'):
        assert f'L"{key}"' not in at and not re.search(rf'^{key}=', ini, re.M), key
    unread = [k for k in re.findall(r'^([A-Za-z]\w*)=', ini, re.M) if f'L"{k}"' not in at]
    assert not unread, f'EDF6AutoTurret.ini keys autoturret/src/plugin.cpp never reads: {unread}'
    steer = at.split('float Steer(', 1)[1].split('\n}\n', 1)[0]
    put = steer.find('Put<float>(vehicle,kTurn')
    assert 0 <= steer.find('if(!rule.steer)return flight;') < put < steer.find('track->steered=Frame();'), 'Steer: the rule gates the turn'
    assert 'PlayerControlRule(vehicle,0,only!=nullptr)' in steer
    gunner = src('autoturret/src/gunner.cpp')
    seat = gunner.split('void SteerSeat(', 1)[1].split('\n}\n', 1)[0]
    put = seat.find('Put<float>(vehicle,kTurn')
    assert 0 <= seat.find('if(!rule.steer)return;') < put < seat.find('track.steered=autoturret::Frame();'), 'SteerSeat: the rule gates the turn'
    assert 'PlayerControlRule(vehicle,s,only!=nullptr)' in seat
    link = src('common/edf/aimlink.h')
    names = dict(re.findall(r'constexpr char (k\w+)\[\]="(\w+)";', link))
    assert set(names) == {'kViewRay', 'kMapRay', 'kTurretReadout', 'kCameraTurret', 'kSteers', 'kStabilizer', 'kStabilizerAware', 'kPriorityZone',
                          'kInputHeld', 'kTurretObserver', 'kPlayerAim', 'kAimsTurret', 'kPayloadAim'}, names   # kPriorityZone: proteus_wired; kInputHeld: map_wired
    assert names['kCameraTurret'].endswith('V2') and names['kSteers'].endswith('V2'), names
    # V4, the one player turret aim (2026-10-09): EDF6AutoTurret answers it, EDF6VehicleCrew's turret camera asks it every
    # frame and says it steers; the flak's own Steer and the tank driver's frame then leave the turret to the camera.
    assert names['kPlayerAim'].endswith('V4') and names['kAimsTurret'].endswith('V4'), names
    assert names['kPayloadAim'].endswith('V1'), names
    assert f'bool __cdecl {names["kPayloadAim"]}(' in src('src/turretaim.cpp'), 'optional actual-weapon export must match its shared ABI name'
    assert f'bool __cdecl {names["kPlayerAim"]}(' in src('autoturret/src/designate.cpp')
    assert f'bool __cdecl {names["kAimsTurret"]}(' in src('src/turretaim.cpp')
    assert 'PlayerTurretLead(v,0,TurretGun(v,seat),lead)' in src('src/turretcam.cpp'), 'turretcam.cpp: the camera asks V4'
    assert 'if(pilot && CrewAims(vehicle,0))rule=edf::aimlink::PlayerGun{false,false};' in steer, 'Steer: the camera is the hand'
    assert 'if(CrewAims(vehicle,0))return;' in gunner.split('void DriverFrame(', 1)[1].split('\n}\n', 1)[0], 'DriverFrame: V4 publishes'
    assert names['kStabilizer'].endswith('V3') and names['kStabilizerAware'].endswith('V3'), names
    assert f'bool __cdecl {names["kStabilizer"]}(' in src('src/stab.cpp') and f'bool __cdecl {names["kStabilizerAware"]}(' in at
    # The rule itself: with the camera, only a lock steers and the stick never drags; without, V1.
    rule = link.split('inline PlayerGun PlayerGunRule(', 1)[1].split('\n}', 1)[0]
    assert 'if(!cameraTurret)return PlayerGun{true,true};' in rule and 'return PlayerGun{locked,false};' in rule
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
        **{b: "the Dreadnought's hangar launches it (src/dreadnought_hangar.inc): its model is the user's model folder's"
           for b in ('dnFighter', 'dnDefender', 'dnBomber', 'dnInterceptor')},
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
    # the Dreadnought's fighters come from its hangar alone (their models are the user's model folder's: no range copy)
    hangar = {name for name, *_ in vc.DREADNOUGHT_FIGHTERS.values()}
    kinds = {name for name, jet in JETS.items()
             if int(jet.mark) in boardable and name.endswith('_mission') and not jet.parked and not jet.player
             and name not in hangar}
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
    ours = ({'edf6tr_pjet_fighter_mission', 'edf6tr_pjet_strike_mission'} | set(gen.BOARDABLE_PARKED)
            | (set(gen.GROUND_MISSION) - set(gen.CATALOG_ONLY))
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
def spawn_catalog_generated_is_current() -> None:
    """src/spawn_catalog.inc (the plugin's object rows: the M map's 「摆放」 in the test range, the debug spawn tool) is what
    tools/gen_spawn_catalog.py writes from testrange/catalog.py now: a row added to a registry reaches the plugin."""
    import gen_spawn_catalog
    with open(gen_spawn_catalog.OUT, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_spawn_catalog.render(), \
            'src/spawn_catalog.inc is stale: python tools/gen_spawn_catalog.py'
    with open(gen_spawn_catalog.OUT_ARCHIVES, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_spawn_catalog.render_archives(), \
            'src/spawn_catalog_archives.h is stale: python tools/gen_spawn_catalog.py (--archives with the game)'


@test
def spawn_catalog_rows_check_their_files() -> None:
    """The plugin makes a Mods row of the catalog only when every file the range's own tools need for it is there: one
    list for both sides (the review of #191: the plugin checked the SGO alone, catalog.available every file). Each row of
    src/spawn_catalog.inc carries catalog.files_of (its SGO among them), and debug_spawn.cpp checks each of them."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import catalog
    import gen_spawn_catalog
    inc = src('src/spawn_catalog.inc')
    rows = catalog.rows()
    lines = [l for l in inc.splitlines() if l.startswith('    {Category::')]
    assert len(lines) == len(rows)
    for r, line in zip(rows, lines):
        want = catalog.files_of(r)
        if r.source == 'stock':
            assert not want and line.endswith(',nullptr},'), r.sgo
            continue
        files = re.search(r',(?:true|false),(?:true|false),(L".*)\},$', line).group(1)
        got = [n.replace('\\\\', '/') for n in re.findall(r'L"([^"]*)\\0"', files)]
        assert got == [gen_spawn_catalog.mod_file_name(w) for w in want] and r.rel in want, (r.sgo, got, want)
    assert gen_spawn_catalog.files_literal(rows[0]) == 'nullptr'
    code = src('src/debug_spawn.cpp')
    lacks = code.split('const wchar_t* ModRowLacks(const Entry& e) noexcept {', 1)[1].split('\n}', 1)[0]
    assert 'for(const wchar_t* f=e.files;*f;f+=std::wcslen(f)+1)' in lacks and 'ModRowFileThere(f)' in lacks
    assert 'const auto reference=ParseModFile(file);' in code
    assert gen_spawn_catalog.mod_file_name('OBJECT/EDF41MP01/ANTHILL301UG.SGO') == 'EDF41MP01/ANTHILL301UG.SGO'
    assert gen_spawn_catalog.mod_file_name('WEAPON/EDF6VC_BMPT_ATGM_4.SGO') == 'WEAPON/EDF6VC_BMPT_ATGM_4.SGO'
    for unsafe in ('OBJECT/../x', 'WEAPON/a//b', 'WEAPON/a\\b', 'C:/OBJECT/x', '/OBJECT/x', 'OBJECT/.x', 'OBJECT/x.', 'OBJECT/a..b', 'OBJECT/x:ads'):
        try:
            gen_spawn_catalog.mod_file_name(unsafe)
        except ValueError:
            continue
        raise AssertionError(f'unsafe spawn catalog file accepted: {unsafe}')
    assert 'missing[i]=lacks!=nullptr;' in code and 'ModRowThere' not in code
    assert 'static_assert(FilesAgree(),' in src('src/debug_spawn.h')


@test
def spawn_catalog_archives_recorded() -> None:
    """src/fsbudget.cpp adds one CriFsLoader per model archive the catalog's whole preload can hold open
    (src/spawn_catalog_archives.h, from testrange/catalog_archives.json; docs/cri-fs-loaders-re.md): the header is the
    record's count, the record has every row and no other, and (with the game) the record is what the rows' SGOs name
    now, through every SGO they name."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import catalog
    import gen_spawn_catalog as g
    rec = g.recorded()
    union = g.archive_union(rec)
    assert f'inline constexpr int kCatalogModelArchives={len(union)};' in src('src/spawn_catalog_archives.h')
    assert all(a.endswith(g.MODEL_ARCHIVES) for a in union)   # the objects' models and their weapons' (WEAPON/...)
    assert 'constexpr std::int32_t kAddedLoaders=debugspawn::kCatalogModelArchives;' in src('src/fsbudget.cpp')
    import rootcpk
    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    read = g.game_reader(rootcpk.DEFAULT_GAME)
    stale = []
    for r in catalog.rows():
        try:
            now = g.row_archives(r, read)
        except (FileNotFoundError, KeyError):
            continue   # not installed here (the installer's sedan, EDF5's DLC fortress): not checked
        if now != rec[r.sgo]:
            stale.append(r.sgo)
    assert not stale, f'testrange/catalog_archives.json is stale for {stale[:5]}: python tools/gen_spawn_catalog.py --archives'


@test
def spawn_catalog_covers_every_registry() -> None:
    """The test range's catalog (testrange/catalog.py) misses nothing the registries add (the user, 2026-10-17:
    「测试场的东西该更新了」): every EDF4.1 object (edf41port/objects.json) is a row or named with why it is not
    (catalog.edf41_left_out) and its class is known; EDF5's DLC object groups are rows; every vehicle of ours the player
    drives empty (gen.PLACEABLE_DERIVED: the helicopters, the player jets, every parked twin, the ground vehicles our tools
    make) and the installer's are rows; every vcobjects GROUND_VEHICLES kind is placeable (the range makes it or the
    installer's SGO is a row); every stock row the range's waves and the debug spawn tool name is one; the waves leave
    no ship on the ground; the log ids are unique."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import catalog
    import gen
    import vcobjects as vc
    rows = catalog.rows()
    by = {r.sgo: r for r in rows}
    assert len(by) == len(rows) and len({r.ident for r in rows}) == len(rows), 'a row twice'
    reg = catalog._edf41_registry()
    for o in reg['objects']:
        stem = o['object'].removeprefix('OBJECT/').removesuffix('.SGO').lower()
        why = catalog.edf41_left_out(o)
        assert o['class'] in catalog.EDF41_CLASSES or o['class'] in catalog.EDF41_LEFT_OUT_CLASSES, \
            f'{o["object"]}: class {o["class"]} is new: give it a category in testrange/catalog.py EDF41_CLASSES'
        assert (stem in by) != bool(why), f'{o["object"]}: neither a row nor left out with why ({why})'
    for r in catalog.edf41_rows():
        assert set(catalog.edf41_files(r.sgo)) <= set(reg['files']), r.sgo
    assert {r.sgo for r in catalog.edf5dlc_rows()} <= set(by) and catalog.edf5dlc_rows(), 'EDF5 DLC objects'
    assert set(gen.PLACEABLE_DERIVED) <= set(by), sorted(set(gen.PLACEABLE_DERIVED) - set(by))
    assert all(by[s].category == 'ours' and by[s].source == 'range' for s in gen.PLACEABLE_DERIVED)
    # not the requests' copies, nor the BIG ones (the 500 m Dreadnought: the plan places it at the farthest points)
    want_jets = {s for s, j in vc.JETS.items() if (j.player or j.parked) and s in gen.DERIVED and s not in gen.BIG}
    assert want_jets <= set(gen.PLACEABLE_DERIVED), f'a jet the player flies is not placeable: {sorted(want_jets - set(gen.PLACEABLE_DERIVED))}'
    installer_rows = {s for s, _, _ in catalog.INSTALLER_VEHICLES}
    for kind, v in vc.GROUND_VEHICLES.items():
        placed = gen.DERIVED_PREFIX + kind + '_mission' in gen.GROUND_MISSION or v.sgo.lower() in installer_rows
        assert placed, f'GROUND_VEHICLES {kind} is not placeable in the test range (testrange/gen.py GROUND_MISSION or catalog.INSTALLER_VEHICLES)'
    assert installer_rows <= set(by) and set(gen.GROUND_MISSION) <= set(by)
    for s, _ in catalog.STOCK_VEHICLES:
        assert by[s].category == 'vehicle' and by[s].source == 'stock'
    assert {s for s, _, _ in gen.ENEMIES if s not in gen.PRIMER_KINDS} <= set(by), 'a wave enemy is no row'
    assert all(by[s].lift < gen.WAVE_LIFT_MOST for s, _, _ in gen.ENEMIES if s in by), 'a ship among the waves'
    wanted = {'ufomother301', 'ufomother401', 'uforobo401', 'edf6vc_sedan', 'v507_rescuetank_ai', 'vehicle301_tank',
              'edf6tr_vulcan_mission', 'edf6tr_bmpt_mission', 'edf6tr_quadvulcan_mission'}
    assert wanted <= set(by), sorted(wanted - set(by))
    # The panel's tabs (src/debug_spawn.h Category) are the catalog's, in its order.
    header = src('src/debug_spawn.h')
    tabs = re.search(r'enum class Category : std::uint8_t \{ ([^}]*), count \};', header).group(1).split(', ')
    assert tabs == list(catalog.CATEGORIES), tabs


@test
def range_leaves_out_what_is_not_installed() -> None:
    """The range's vehicles whose files the installer writes (the sedan, EDF4.1's tank) are left out, said so, when this
    game's Mods does not have them (an EDF4.1 install that never ran, a sedan build that failed); the rest of the plan is
    as it was. A waves' 4.1 enemy that is not there is refused with its file named."""
    import tempfile
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import gen
    plan = gen.target_range(gen.Plan())
    assert set(gen.RANGE_NEW) <= set(plan.vehicles), 'the installer\'s range lacks the new vehicles'
    with tempfile.TemporaryDirectory() as root:
        kept, said = gen.without_missing(root, plan)
        gone = {s for s in gen.RANGE_NEW if s in gen.OPTIONAL_VEHICLES}
        assert gone and not gone & set(kept.vehicles) and len(said) == len(gone), (said, kept.vehicles)
        assert {s: n for s, n in plan.vehicles.items() if s not in gone} == kept.vehicles and kept.friends == plan.friends
        assert 'v507_rescuetank_ai' in kept.vehicles, 'a stock vehicle is never left out'
        for rel in gen.catalog.files_of(gen.OPTIONAL_VEHICLES['edf6vc_sedan']):
            path = os.path.join(root, 'Mods', *rel.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, 'wb').close()
        kept, said = gen.without_missing(root, plan)
        assert 'edf6vc_sedan' in kept.vehicles and 'vehicle301_tank' not in kept.vehicles
    row = gen.enemy_row('hector301cc')
    assert row and row.source == 'edf41' and gen.enemy_row('giantant01') is None


@test
def range_online_mode_has_no_weapon_level_cap() -> None:
    """The test range's online mode lifts the online weapon level cap (the user, 2026-10-17: 「测试场不限制武器等级」;
    docs/weapon-level-cap-re.md): EDF.dll 0xE2710 reads the current mode's ModeList member 7 [difficulty][6] and a
    negative value is no cap; the range writes EDF.dll's own [-1.0, -1.0] on every difficulty, the armour cap (element 7)
    and every other value the stock online story's; its offline mode and every other pack keep theirs. (The full
    CONFIG.SGO build is checked with the game in edf5_campaign_range_pack.)"""
    import copy
    import struct
    import sgo
    f = lambda v: sgo.Float(struct.pack('<f', v))  # noqa: E731
    template = ['name', 'desc', 0, 0, 0, 'M.MST', ['a', 'b', 'c'],
                [[f(1.0)] * 5 + [[f(1.2), f(1.5)], [f(0.1), f(1.1)], [358, 2322]] for _ in range(5)], 1, 0, 1]
    online = e5c.mode_entry(copy.deepcopy(template), e5c.RANGE, 6, e5c.ONLINE)
    assert all(sgo.plain(r[e5c.LIMIT_WEAPON]) == [-1.0, -1.0] and r[e5c.LIMIT_ARMOR] == [358, 2322]
               for r in online[e5c.M_LIMITS])
    assert sgo.plain(online[e5c.M_LIMITS][0][:6]) == sgo.plain(template[7][0][:6])
    offline = e5c.mode_entry(copy.deepcopy(template), e5c.RANGE, 6, e5c.OFFLINE)
    assert sgo.plain(offline[e5c.M_LIMITS]) == sgo.plain(template[7]), 'the offline mode changed'
    for pack in e5c.PACKS:
        assert pack.weapon_cap and sgo.plain(e5c.mode_entry(copy.deepcopy(template), pack, 3, e5c.ONLINE)[7]) == sgo.plain(template[7])
    # The game's reader (EDF.dll 0xE2710): a negative element is no cap, and these are the instructions read for it.
    assert not e5c.RANGE.weapon_cap and e5c.M_LIMITS == 7 and e5c.LIMIT_WEAPON == 6


@test
def range_weapon_cap_in_pack_definition() -> None:
    """Each machine computes the online weapon cap from its own config.sgo (docs/weapon-level-cap-re.md), so a guest whose
    install predates the range's lifted cap keeps the stock cap in the host's room. The cap is part of the pack's
    definition (make_edf5_campaign.pack_digest, the AF_PACK digest the guest compares, src/mission_packs.h Admit): the
    same rows with the stock cap make another digest, so that guest is refused (pack-definition-differs) and told to
    reinstall. A pack that keeps the stock cap hashes as before (its digest, and its saves' layout marker, unchanged)."""
    import copy
    import hashlib
    rows = [{'key': f'EDF6/{e5c.RANGE_MISSION}'}]
    capped = copy.copy(e5c.RANGE)
    capped.weapon_cap = True
    assert e5c.pack_digest(e5c.RANGE, rows) != e5c.pack_digest(capped, rows), 'the lifted cap is not in the definition'
    old = lambda pack, rs: hashlib.sha256(  # noqa: E731 - the digest before the cap was in it (2026-10-18, #193)
        (f'AF-PACK-1\n{pack.tag}\n{len(rs)}\n' + ''.join(f'{r["key"]}\n' for r in rs)).encode()).hexdigest()[:16]
    assert e5c.pack_digest(capped, rows) == old(capped, rows)
    for pack in e5c.PACKS:
        some = [{'key': f'X/{i}'} for i in range(3)]
        assert pack.weapon_cap and e5c.pack_digest(pack, some) == old(pack, some), pack.tag
    # The guest's check (mission_packs.h Admit) compares exactly that digest: another one is pack-definition-differs.
    header = src('src/mission_packs.h')
    assert 'if(std::strcmp(a.host.digest,local->digest) || a.host.rows!=local->rows){a.verdict=Verdict::kOtherVersion;return a;}' in header
    assert 'case Verdict::kOtherVersion: return "pack-definition-differs";' in header


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
    # Match gen.install's effective plan: an optional migrated vehicle absent from Mods is not spawned, and is not a
    # stock Root.cpk model this audit can measure. Installed optional vehicles are still included.
    plan, _skipped = gen.without_missing(rootcpk.DEFAULT_GAME, gen.target_range(gen.Plan()))
    for name in sorted({s for s, _ in gen.placements(plan)} - gen.JETS.keys()):
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

    def at(proxy: str) -> list[float]:   # the proxy on the box's centre, in its own place of the V506's rest pose
        return [round(c[k] + vc.V506_PROXY_LAYOUT[proxy][k], 3) for k in range(3)]
    for e in inner['ragdoll_from_animation']:
        assert e[0][0] == 'body' and [round(value(x), 3) for x in e[1]] == at(e[0][1]), e
        assert [value(x) for x in e[2]] == [0.0, 0.0, 0.0, 1.0], e
    assert [e[0][0] for e in inner['animation_from_ragdoll']] == ['RagDollProxys.rotor', 'RagDollProxys.body']
    for e in inner['animation_from_ragdoll']:
        assert e[0][1] == 'body' and [round(value(x), 3) for x in e[1]] == [-x for x in at(e[0][0])], e
        assert [value(x) for x in e[2]] == [0.0, 0.0, 0.0, 1.0], e


@test
def jet_ragdoll_proxies_in_the_v506_rest_pose() -> None:
    """pylib/vcobjects.py V506_PROXY_LAYOUT is RAGDOLL_V506_HELI.SHKT's rest pose: every proxy's position less the body
    proxy's, every orientation the identity, the pose its hinges and slider hold. A jet's proxies all follow one bone; on
    one point (each still turned by its V506 binding) a destroyed jet's dynamic ragdoll was thrown apart by its own
    constraints (docs/feedback-2026-10-12-ragdoll-twitch.md). A proxy the layout does not know is refused. The SHKT half
    needs the game's Root.cpk."""
    import struct
    import rootcpk
    import sgo
    blob = sgo.write(1, {'ragdoll_from_animation': [[['body', 'RagDollProxys.wing'], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]]],
                         'animation_from_ragdoll': []})
    try:
        vc._jet_ragdoll(blob, 'body', (0.0, 1.0, 0.0))
        raise AssertionError('an unknown proxy was placed')
    except ValueError:
        pass
    if not os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        return
    from hktag import Tag
    t = Tag(rootcpk.Game(rootcpk.DEFAULT_GAME).read('OBJECT', 'RAGDOLL_V506_HELI.SHKT'))
    scene = t.root_variant('hknpPhysicsSceneData')
    _, systems, _ = t.item(t.u32(scene + t.offset('hknpPhysicsSceneData', 'systemDatas')))
    _, system, _ = t.item(t.u32(systems))
    B = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
    _, first, n = t.item(t.u32(system + t.offset('hknpPhysicsSystemData', 'bodyCinfos')))
    pose = {}
    for j in range(n):
        p = first + j * t.size(B)
        pose[t.cstr(t.u32(p + t.offset(B, 'name')))] = (struct.unpack_from('<3f', t.b, p + t.offset(B, 'position')),
                                                        struct.unpack_from('<4f', t.b, p + t.offset(B, 'orientation')))
    assert set(pose) == set(vc.V506_PROXY_LAYOUT), sorted(pose)
    hull = pose['RagDollProxys.body'][0]
    for name, (position, turn) in pose.items():
        assert all(abs(position[k] - hull[k] - vc.V506_PROXY_LAYOUT[name][k]) < 1e-3 for k in range(3)), (name, position)
        assert all(abs(a - b) < 1e-6 for a, b in zip(turn, (0.0, 0.0, 0.0, 1.0))), (name, turn)


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


@test
def aim_lines_hide_real_crews() -> None:
    """An NPC's red aim lines are hidden whoever the NPC is (the user, 2026-10-10: "npc载具红线会显示出来"): the
    support's crews are real soldiers (Rider::other), so AimLines reads a seat's holder through npcai.cpp NpcInSeat
    (src/aim_line_want.h, tools/aim_line_want_check.cpp) and "an NPC drives it" through NpcDriver, not through
    Rider::dummy alone."""
    crew, npcai = src('src/crew.cpp'), src('src/npcai.cpp')
    aim = crew.split('void AimLines(', 1)[1].split('\n}\n', 1)[0]
    holder = crew.split('aimline::Holder LineHolder(', 1)[1].split('\n}\n', 1)[0]
    assert 'aimline::Want(LineHolder(seat),ownSight)' in aim, 'AimLines: holder by NpcInSeat'
    want = src('src/aim_line_want.h').split('constexpr LineWant Want(', 1)[1].split('\n}\n', 1)[0]
    assert 'case Holder::empty: return LineWant::hide;' in want, 'an empty seat hides its line whoever drives (2026-10-14)'
    assert 'NpcInSeat(seat)' in holder and 'Rider::dummy' not in aim, 'LineHolder: an NPC is NpcInSeat, not the Dummy alone'
    driver = npcai.split('bool NpcDriver(', 1)[1].split('\n}\n', 1)[0]
    assert 'NpcInSeat(' in driver, 'NpcDriver is NpcInSeat of seat 0 (one test of "an NPC is there")'


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
                'SupportVehicle_': tuple(support_loadout.VEHICLE_KEYS),
                'SupportCrewColour_': (*support_loadout.CREW_KEYS, support_loadout.MISSION)}   # 阶段 5: the crews' colours
    example = {'SupportAircraftCount_': r'\d', 'SupportPreset_': r'[a-z]', 'SupportVehicle_': r'[A-Z]',
               'SupportCrewColour_': r'[0-9A-FX]'}   # a count; a kind; a gun; a colour

    def read(key: str) -> bool:
        if f'L"{key}"' in plugin or any(f'L"{key}"' in m for m in modules.values()):
            return True
        if key == 'Edf41MaterialCompatibility':
            return key.lower() in src('tools/installer.py').lower() and 'material_compat=' in src('tools/installer.py')
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
    assert 'aim::Move(p.aim,mx,my,k)' in fly and 'MoveOnScreen' not in fly, 'stock heli mouse direction must not be tethered to the screen'
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
    # The stock heli's keys (f.collective) separate W/S pitch from Space/brake collective; the rotor
    # craft's stay the speed setpoint's.
    assert 'f.collective ? Tx::heliKeysInstructor : Tx::heliKeysAir' in hud and 'Tx::heliKeysInstructorLanded' in hud
    texts = src('src/hudtext.inc')
    assert 'SPACE: up' in texts.split('HUDTEXT(heliKeysAir,', 1)[1].split('\n', 1)[0]
    instructor = texts.split('HUDTEXT(heliKeysInstructor,', 1)[1].split('\n', 1)[0]
    assert 'W / S: nose down / up' in instructor and 'MOUSE: aim / heading' in instructor, instructor
    assert 'HOLD SPACE' in texts.split('HUDTEXT(heliKeysInstructorLanded,', 1)[1].split('\n', 1)[0]
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
    # Unused parked vehicles wait for their first player driver unless the explicit test-only opt-in is enabled.
    assert 'if(!st.playerAt && !Cfg().npcCrewTest)return;' in crew, \
        'unused parked vehicles require a player driver or the explicit NpcCrewTest opt-in'
    assert 'bool npcCrewTest=false;' in src('src/crew.h'), 'NpcCrewTest must stay off by default'
    assert 'n.npcCrewTest=ReadBool(L"NpcCrewTest",n.npcCrewTest);' in plugin, 'the test opt-in must load from config'
    assert re.search(r'^NpcCrewTest=0\s*$', src('EDF6VehicleCrew.ini'), re.MULTILINE), \
        'the shipped configuration must not auto-crew unused vehicles'
    # The stock weapons only: no custom rounds, no damage-call redirect, the stock damage path untouched.
    for gone in ('ProteusGunRound', 'ProteusSalvoRound', 'ProteusRoundsReady', 'ProteusDamageThunk', 'kProteusHoldCountdown'):
        for rel in ('src/crew.h', 'src/jet_bay.cpp', 'src/proteus.cpp', 'src/proteus.h', 'src/subcarrier.cpp', 'src/vehsound.cpp'):
            assert gone not in src(rel), f'{gone} in {rel}'
    assert 'return to==image+kDamageTarget;' in src('src/subcarrier.cpp')
    assert '0x54A586' not in code and 'RedirectCall' not in code
    # The shield: the stock barrier round raised through the EMC table (preloaded whenever its file is installed).
    bay = src('src/jet_bay.cpp')
    assert 'L"app:/object/edf6vc_proteus_shield.sgo",L"EDF6VC_PROTEUS_SHIELD.SGO"' in bay
    assert 'static_assert(kEmcCount==static_cast<int>(EmcRound::ultGather)+1' in bay   # the Proteus's before the ultimate's
    assert 'PreloadShells(mgr,Preloaded(Body::gunship) || ModFileThere(L"EDF6VC_DN_DREADNOUGHT.MRAB"));' in src('src/jet_spawn.cpp')
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
    crew_source = src('src/crew.cpp')
    assert 'void FrameTick() noexcept { SharedFrameTick(); }' in crew_source, 'the public frame entry must use the shared GameFrame guard'
    tick = crew_source.split('void SharedFrameTick()', 1)[1].split('\n}', 1)[0]
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
    writes = re.findall(r'WriteClip\(at,farClip,(\d),', raise_fn)
    assert writes == ['0', '2'], f'view.cpp Raise writes fields {writes}: only far clip and far pass end'
    write_fn = code.split('void WriteClip(', 1)[1].split('\n}', 1)[0]
    assert 'Put<float>(at,Field(farClip,field),value);' in write_fn
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
    assert len(re.findall(r'GunshipMuzzle\(j,v,', bay)) == 4, 'GunshipMuzzle for the side guns, their sight line and both shells'
    # The round's path against the flying airframe (the user, 2026-10-10: 「炮舰机的机炮有可能会打在自己身上」): every gunship
    # round is held by GunshipClears before it is made, its rounds' speeds and fall are make_jets.py's (the shell's held to
    # the stock file by check_gunship_muzzle), and the side guns' lead speeds are their rounds'.
    for gun, round_ in (('kCannon', (make_jets.CANNON_SPEED, 0.0, 'kCannonHit')), ('kGatling', (make_jets.GATLING_SPEED, 0.0, 'kGatlingHit')),
                        ('kShell', (make_jets.SHELL_SPEED, make_jets.SHELL_FALL, 'kShellHit'))):
        m = re.search(gun + r'\{' + num + ',' + num + r',(\w+)\}', head)
        assert m and float(m.group(1)) == round_[0] and float(m.group(2)) == round_[1] and m.group(3) == round_[2], f'src/gunmuzzle.h {gun}'
    assert len(re.findall(r'if\(!GunshipClears\(j,v,', bay)) == 3, "GunshipClears before the side guns' round and both shells"
    assert 'static_assert(kCannonSpeed==gunmuzzle::kCannon.speed*60.0f' in bay, "the side guns lead by their rounds' speed"
    muzzle_fn = bay.split('bool GunshipMuzzle(', 1)[1].split('\n}\n', 1)[0]
    assert 'gunmuzzle::Launch(' in muzzle_fn, 'GunshipMuzzle is gunmuzzle::Launch'
    assert re.search(r'MapRay\(muzzle,at,hit\)', bay), 'the NPC cannon looks along the line its round flies'
    make = bay.split('unsigned char* ShellMake(', 1)[1].split('\n}\n', 1)[0]
    assert 'if(ifcWaitOk)Put<std::int32_t>(ifc,kIfcWait,0);' in make, 'ShellMake zeroes the first-round wait'
    assert 'constexpr std::size_t kIfcWait=0x2D8;' in bay
    assert 'for(const auto& b:kIfcWaitSigs)ifcWaitOk=ifcWaitOk && Matches(' in bay, 'the wait behind its signatures'
    cmake = src('CMakeLists.txt')
    assert 'add_executable(gunship_muzzle_check EXCLUDE_FROM_ALL tools/gunship_muzzle_check.cpp)' in cmake
    checks = cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0]
    assert 'gunship_muzzle_check' in checks.split(), 'CTest runs gunship_muzzle_check'
    # A plugin jet's own round never on its own airframe (src/ownround.h), whatever the bullet's owner bit: the collector's
    # hook asks Judge with the candidate's body id, Publish gives every jet its own body ids, the offline check runs.
    hooks = src('src/jet_hooks.cpp')
    assert 'ownround::Judge(flights.jet,flights.count,owner,target,body)' in hooks, 'jet_hooks.cpp Passes asks ownround::Judge'
    assert 'Passes(owner,target,body)' in hooks and 'OwnBodies(static_cast<const unsigned char*>(j.ref.obj),c);' in hooks
    assert 'own round kept off its airframe' in hooks, 'the own round is logged'
    assert 'add_executable(own_round_check EXCLUDE_FROM_ALL tools/own_round_check.cpp)' in cmake
    assert 'own_round_check' in checks.split(), 'CTest runs own_round_check'
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
def ledger_committed_writer() -> None:
    """A transaction's writer can register an unchanged legacy file without rewriting it or losing other owners;
    a mismatching committed hash cannot claim a foreign edit."""
    with tempfile.TemporaryDirectory(prefix='edf6vc-record-') as game:
        rel, data = 'OBJECT/MODEL.MRAB', b'committed model'
        path = _mods(game, rel)
        modfiles.atomic_write(path, data)
        led = ledger.Ledger(game)
        led.need('edf41', rel)
        before = os.stat(path).st_mtime_ns
        led.record('calls', rel, modfiles.sha256(data))
        assert set(led.owners(rel)) == {'calls', 'edf41'}
        assert os.stat(path).st_mtime_ns == before
        modfiles.atomic_write(path, b'foreign edit')
        try:
            led.record('optics', rel, modfiles.sha256(data))
        except ValueError:
            pass
        else:
            raise AssertionError('a changed file was registered as the transaction writer\'s')
        assert set(ledger.Ledger(game).owners(rel)) == {'calls', 'edf41'}


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
def ini_merge_adds_room_presync() -> None:
    # An ini from before 阶段 R (RoomLoadout but no RoomPreSync): the install adds RoomPreSync=1 with its comment under the
    # added-settings header, the player's RoomLoadout untouched.
    shipped = src('EDF6VehicleCrew.ini')
    user = '[VehicleCrew]\r\nEnabled=1\r\nRoomLoadout=0\r\nRoomLoadoutKey=0x71\r\n'
    text, added, _ = installer.merge_ini(user, shipped)
    assert 'RoomPreSync' in added, added
    tail = text.split(installer.ADDED_HEADER, 1)[1]
    assert '\r\nRoomPreSync=1\r\n' in tail and tail.index('联机房间预同步') < tail.index('RoomPreSync=1'), 'the comment above it'
    assert text.startswith(user.rstrip('\r\n')) and '\nRoomLoadout=0\r\n' in text and '\nRoomLoadout=1\r\n' not in text
    assert installer.merge_ini(text, shipped)[1] == [], 'merge is not idempotent'


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


def _flak_request_template() -> bytes:
    """A stock flak-tank request (a ground request: [multipliers, vehicle params, weapons]) for an emplacement's
    no-model fallback (call_weapons.MODEL_GROUND)."""
    n = dsgo.Node
    weapons = n([n([f'app:/weapon/v603_flak_gun01_{side}.sgo', n([0.01, 0.1])]) for side in ('l', 'r')])
    setup = n([n([1.3, 1.4]), n([0.5, 0.5]), weapons])
    vehicle = 'app:/object/v603_flak.sgo'
    root = n([n([5.0]), n([0.0, 0.0, 0.0, 0.0, n(['transport', 'box', vehicle, setup, 'voice'])]),
              n([vehicle] + [w.items[0] for w in weapons.items]), 'Flak'],
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
        if c.jet in cw.MODEL_VEHICLES:   # their install reads the request's own setup (call_weapons.vehicle_needs)
            files[cw.sgo_file(c)] = cw.vehicle_sgo(_sazabi_request_template(), c, (2.0, 3.0))
        elif c.ground in cw.MODEL_GROUND:
            files[cw.sgo_file(c)] = cw.vehicle_sgo(_flak_request_template(), c, (2.0, 3.0), fallback=True)
    return files


@test
def emplacement_fallback_without_the_dreadnought() -> None:
    """Without the Dreadnought's model folder its three secondary-turret emplacements are the stock flak tank under
    their names (make_tracked_combat.model_missing), their requests keep the template's stock guns and need only that
    SGO (call_weapons.MODEL_GROUND): the installer no longer stops at them. The emplacements with folders of their own
    are not affected."""
    from unittest.mock import patch
    import make_tracked_combat as mtc
    import obj_model
    with tempfile.TemporaryDirectory() as empty, patch.object(obj_model, 'model_roots', lambda: [empty]):
        for c in calls.CALLS:
            if c.ground in cw.MODEL_GROUND:
                assert cw.model_missing(c) and mtc.model_missing(c.ground), c.id
                request = cw.vehicle_sgo(_flak_request_template(), c, (2.0, 3.0), fallback=True)
                assert cw.vehicle_needs(c, request) == [cw.vehicle_file(c)], (c.id, cw.vehicle_needs(c, request))
                r = dsgo.parse(request).root
                assert r.get('Ammo_CustomParameter').items[4].items[2] == f'app:/object/{c.vehicle.lower()}.sgo'
    import emplacement_model
    assert {k for k, e in emplacement_model.KINDS.items() if not e.folder} == set(cw.MODEL_GROUND), cw.MODEL_GROUND
    try:
        mtc.check({f'OBJECT/{vc.GROUND_VEHICLES["emp_mid"].sgo}.SGO': _flak_request_template()}, 'emp_mid')
    except ValueError:
        pass   # the stand-in is not a V603: refused, as a wrong fallback must be
    else:
        raise AssertionError('a fallback that is not the stock V603 was accepted')


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
                          find_edf41=lambda game: None, material_compat_wanted=lambda text: False,
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
            enter(patched(e5c, build=lambda game, campaign=True, test_range=True, root41=None, skip41=(): _e5c_stub()))   # its own install / remove / check run for real
            enter(patched(e5c.modfiles, refuse_while_running=lambda *a, **k: None))   # the real game may be open
            for group in buildcache.GROUPS:
                made = (b'mac', {'FAKE_PIECE.MAC': b'piece'}) if group == 'bigmap' else \
                    {f'OBJECT/EDF6VC_FAKE_{group.upper()}.SGO': group.encode()}
                enter(patched(importlib.import_module('make_' + group), build=lambda game, *_choices, made=made: made))   # make_dreadnought.build(game, hangars)
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
            # (the stand-in builder's files are what the ini wants: the install's stale-file cleanup keeps them)
            enter(patched(importlib.import_module('support_loadout'),
                          build=lambda game, text, pending=(), errors=None: (built_from.append(text), dict(loadout))[1],
                          wanted_files=lambda text, pending=(): [rel.split('/')[-1] for rel in loadout]))
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
    damaged = rf.Shkt(shkt)
    damaged.set_ref_t(0, (parsed.ref_t(0)[0] + 1.0, *parsed.ref_t(0)[1:]))
    corrupted = damaged.bytes()
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
    for k in changed:
        fitted.set_ref_t(k, parsed.ref_t(k))
    stale = fitted.bytes()
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
    assert 'if(IsStoreWeapon(w) && !(spec && spec->nativeControl))' in payload
    assert 'return playerSeat && FreeSecondary(c) && IsCoax(w) ? PayloadFire::secondary : PayloadFire::store;' in payload and 'return StoreOf(w)!=nullptr;' in stores
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


@test
def edf41_clone_destructor_slot() -> None:
    """The 4.1 classes' destructor wrappers sit on the stock classes' deleting destructor (src/edf41classes.h): slot 1, not 0.
    Slot 0 of EventAnimationObject (0x5B6BB0) and UfoCarrier508 (0x54B010) takes a pointer in rdx and is no destructor; the
    wrappers sat there until 2026-10-17 and never ran (a 401 run's log had no `hull ... destroyed`, 934 records left at the
    exit). The slot the wrappers take must be a function of the deleting destructor's shape (flags from edx kept in ebx, then
    `test bl,1 / je / mov edx,<the class's size>` and operator delete 0x12D85EC), checked at load (kDtors, BuildClone refuses
    anything else) and, with the game present, on EDF.dll's own vtables. A destructor from inside the application destructor
    calls nothing into the game: each class's shutdown callback released its sounds / players / effects first."""
    header, shared = src('src/edf41classes.h'), src('src/edf41classes.cpp')
    assert 'constexpr int kSlotDtor=1,kSlotUpdate=5,kSlotMessage=9;' in header
    dtors = {int(r, 16): (int(t, 16), int(z, 16)) for r, t, z in
             re.findall(r'\{(0x[0-9A-F]+),(0x[0-9A-F]+),(0x[0-9A-F]+)\},\s*// (?:EventAnimationObject|UfoCarrier508)',
                        shared.split('kDtors[]={', 1)[1].split('};', 1)[0])}
    assert dtors == {0x5B64F0: (0x5B651F, 0xC90), 0x4F8A70: (0x4F8AC7, 0x1630)}, dtors
    sizes = {int(v, 16) for m in re.findall(r'kEaoSize=(0x[0-9A-F]+)|kCarrierSize=(0x[0-9A-F]+)', header) for v in m if v}
    assert sizes == {0xC90, 0x1630}, sizes

    def slots(name: str) -> list[int]:
        body = shared.split(f'const std::uint32_t {name}[', 1)[1].split('};', 1)[0].split('{', 1)[1]
        return [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', body)]
    eao, carrier = slots('kEaoSlots'), slots('kCarrierSlots')
    assert eao[1] == 0x5B64F0 and carrier[1] == 0x4F8A70 and eao[0] == 0x5B6BB0 and carrier[0] == 0x54B010
    prints = shared.split('constexpr Print kPrints[]={', 1)[1].split('};', 1)[0]
    for rva in dtors:
        assert '{0x%X,{' % rva in prints, f'the destructor 0x{rva:X} has no fingerprint'
    assert '{0x5B6BB0,{' not in prints, 'slot 0 is fingerprinted as if it were the destructor'
    build = shared.split('bool BuildClone(Clone& c,DtorFn dtor,UpdateFn update,MessageFn message) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'DtorShaped(c.rvas[kSlotDtor])' in build and build.index('DtorShaped(') < build.index('c.block[1+kSlotDtor]=')
    for rel in ('src/edf41mother.cpp', 'src/edf41mother401.cpp', 'src/edf41robo.cpp'):
        code = re.sub(r'//[^\n]*', '', src(rel))
        assert 'orig[kSlotDtor]' in code and 'orig[0]' not in code, rel
        # block[1] is the address of the clone's first virtual entry. Read-only
        # identity comparisons are valid; assigning this slot would corrupt slot 0.
        assert not re.search(r'\.block\s*\[\s*1\s*\]\s*=(?!=)', code), rel
        assert re.search(r'AtGameShutdown\(&\w+\);', code), f'{rel}: no shutdown callback'
    # the shutdown: the callbacks first (the game whole), the flag after; the sound / material calls inert from then on
    hook = shared.split('void __fastcall ShutdownHook(void* self) {', 1)[1].split('\n}', 1)[0]
    assert hook.index('shutdownFns[i]()') < hook.index('shuttingDown=true;') < hook.index('kAppDtor')
    for fn in ('void StopSound(SoundHandle& handle,int fadeFrames) noexcept {', 'void MoveSound(SoundHandle& handle,const float* pos) noexcept {',
               'bool PlayCue(const wchar_t* cue,const float* pos,SoundHandle* handle,const SeParams& params) noexcept {',
               'void MaterialAnimsFree(MaterialAnims& m) noexcept {'):
        assert 'ShuttingDown()' in shared.split(fn, 1)[1].split('\n}', 1)[0], fn
    robo = src('src/edf41robo.cpp')
    assert 'if(!ShuttingDown())At<void(__fastcall*)(void*)>(kMatFree)(l.player+8);' in robo
    assert 'AtGameShutdown(&ShutdownRobos);' in robo and 'Release(kv.second)' in robo.split('void ShutdownRobos() noexcept {', 1)[1].split('\n}', 1)[0]
    m401 = src('src/edf41mother401.cpp').split('void ShutdownGlows() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'StopSound(p.loopA,0); StopSound(p.loopB,0); StopSound(p.ambient,0);' in m401 and 'MaterialAnimsFree(p.mat);' in m401
    m301 = src('src/edf41mother.cpp').split('void ShutdownVents() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'StopSound(kv.second.fly,0);' in m301 and 'StopSound(kv.second.chargeLoop,0);' in m301

    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if not os.path.exists(dll):
        return
    import pefile
    pe = pefile.PE(dll, fast_load=True)
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase

    def slot(vtable: int, i: int) -> int:
        return int.from_bytes(img[vtable + 8 * i:vtable + 8 * i + 8], 'little') - base
    eao_vt = int(re.search(r'kEaoVtable=(0x[0-9A-F]+)', header).group(1), 16)
    carrier_vt = int(re.search(r'kCarrierVtable=(0x[0-9A-F]+)', header).group(1), 16)
    for vtable, table in ((eao_vt, eao), (carrier_vt, carrier)):
        for i, rva in enumerate(table):
            assert slot(vtable, i) == rva, (hex(vtable), i, hex(slot(vtable, i)), hex(rva))
        rva = table[1]
        tail, size = dtors[rva]
        head = img[rva:rva + 0x20]
        assert b'\x8b\xda' in head, f'0x{rva:X}: the flags (edx) are not kept in ebx'
        assert img[tail:tail + 10] == b'\xf6\xc3\x01\x74' + bytes([img[tail + 4]]) + b'\xba' + size.to_bytes(4, 'little'), hex(tail)
        call = tail + 5 + 5 + 3   # je, mov edx / mov rcx,rdi, then the call
        assert img[call] == 0xE8 and call + 5 + int.from_bytes(img[call + 1:call + 5], 'little', signed=True) == 0x12D85EC, hex(call)
        zero = table[0]
        assert img[zero:zero + 0x40].find(b'\xf6\xc3\x01') < 0, f'slot 0 0x{zero:X} looks like a destructor'


@test
def edf41_fortress_object_actions() -> None:
    """4.1's fortress (Alien4LegTank301 on EDF6's MovingFortress) runs the script's ObjectAction (native 3200, message
    0x1000000D) as 4.1 does (src/edf41fortress_action.h): 0..5 one part group on / off, 6 destroyed. EDF6's class numbers
    the same message otherwise, and its 1 spawns rubble at a locator 4.1's model has not: M117 (要塞破壊作戦) sends
    ObjectAction(fortress, 1) about two minutes after the dropped Ufo301 wave is cleared and EDF.dll crashed at 0x4C3A30
    every time (2026-10-18, docs/feedback-2026-10-18-edf41-m16-crash.md). So: every ObjectAction a shipped 4.1 mission
    sends a fortress is a constant 4.1 knows; the hook runs it before anything else and never hands 0x1000000D to the class;
    with the game present the class functions it calls are EDF.dll's, and the class's own action 1 is the rubble spawn whose
    locator lookup is unchecked (the reason none may reach it)."""
    import glob

    import bvm41
    hook = src('src/edf41fortress.cpp')
    head = src('src/edf41fortress_action.h')
    assert 'constexpr int kActions=7;' in head
    body = hook.split('bool __fastcall MessageHook(void* self,std::uint32_t msg,void* data) {', 1)[1].split('\n}\n', 1)[0]
    first = body.index('if(msg==kMsgAction && data && Marked(self))return Edf41Action(self,')
    assert first < body.index('nextMessage('), 'ObjectAction on a 4.1 fortress is answered before the class sees it'
    act = hook.split('bool Edf41Action(void* self,int action) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'nextMessage' not in act and 'return true;' in act, 'no 4.1 action falls through to the class table'
    assert 'constexpr std::uint32_t kMsgAction=0x1000000D;' in hook
    assert 'edf41_fortress_check' in src('CMakeLists.txt').split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0]
    assert "('edf41_fortress', 1)" in src('tools/make_edf41_objects.py'), 'the mark the hook keys on'

    seen: dict[str, list[int]] = {}
    for path in sorted(glob.glob(os.path.join(ROOT, 'edf41port', '**', 'MISSION.BVM'), recursive=True)):
        with open(path, 'rb') as f:
            prog = bvm41.parse(f.read())
        codes = [prog.init] + [g.code for g in prog.funcs]
        # a fortress global: push <address>; pushs <name>; pushs <...Alien4LegTank301.sgo>; ... create; storeind
        forts = set()
        for code in codes:
            for i, x in enumerate(code):
                if x.op == bvm41.OP_PUSHS and isinstance(x.v, str) and x.v.lower().endswith('/alien4legtank301.sgo') \
                        and i >= 2 and code[i - 1].op == bvm41.OP_PUSHS and code[i - 2].op == bvm41.OP_PUSH \
                        and any(y.op == bvm41.OP_STOREIND for y in code[i + 1:i + 5]):
                    forts.add(code[i - 2].v)
        for code in codes:
            for i, x in enumerate(code):
                if x.op != bvm41.OP_NAT0 or x.v != 3200 or i < 2:
                    continue
                h, a = code[i - 2], code[i - 1]
                if h.op != bvm41.OP_PUSHG or h.v not in forts:
                    continue
                assert a.op in (bvm41.OP_PUSH, bvm41.OP_PUSH1), f'{path}: ObjectAction on the fortress with no constant'
                n = 1 if a.op == bvm41.OP_PUSH1 else a.v
                assert 0 <= n < 7, f'{path}: ObjectAction {n} on the fortress is not in 4.1\'s table'
                seen.setdefault(os.path.relpath(path, ROOT).replace(os.sep, '/'), []).append(n)
    assert sorted(seen.get('edf41port/missions/M117/MISSION.BVM', [])) == [0, 1, 2, 3, 4, 5, 6], seen
    assert 1 in seen['edf41port/missions/M117/MISSION.BVM'], 'the action that crashed EDF6\'s own table'
    assert 6 in seen.get('edf41port/mp01/missions/M416/MISSION.BVM', []), seen

    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if not os.path.exists(dll):
        return
    import edfre
    for name in ('kPartsSwitchHead', 'kDieHead'):
        want = bytes(int(b, 16) for b in re.findall(r'0x[0-9A-F]+', re.search(name + r'\[8\]=\{(.*?)\};', hook).group(1)))
        rva = int(re.search(('kPartsSwitch' if name == 'kPartsSwitchHead' else 'kDie') + r'=(0x[0-9A-F]+);', hook).group(1), 16)
        assert edfre.img[rva:rva + 8] == want, (name, hex(rva))
    # the class's table (0x4C6240): action 1 calls the rubble spawn 0x4C37E0, which derefs the locator it looks up unchecked
    assert edfre.img[0x4C6334] == 0xE8 and 0x4C6339 + int.from_bytes(edfre.img[0x4C6335:0x4C6339], 'little', signed=True) == 0x4C37E0
    assert edfre.img[0x4C3A27] == 0xE8 and 0x4C3A2C + int.from_bytes(edfre.img[0x4C3A28:0x4C3A2C], 'little', signed=True) == 0x6BADD0
    assert edfre.img[0x4C3A2C:0x4C3A34] == bytes.fromhex('488b45f04863480c'), 'mov rax,[rbp-10h]; movsxd rcx,[rax+0Ch]'


@test
def edf41_mother_parts_default_migrates() -> None:
    """Edf41MotherParts shipped 15 (no 301 panels) in the nightlies from 2026-10-14 and ships 31 now: merge_ini keeps a
    player's value, so an ini still at the old 15 is moved to 31 once by the installer's second defaults step (its own mark:
    an ini that took the 10-07 step takes it too), a value the player chose is kept, and a later install changes nothing."""
    import installer
    ini, crew = src('EDF6VehicleCrew.ini'), src('src/crew.h')
    assert re.search(r'^Edf41MotherParts=31$', ini, re.M) and 'int edf41MotherParts=31;' in crew
    assert installer.NEW_DEFAULTS_1017[installer.SECTION] == {'Edf41MotherParts': ('15', '31')}
    assert [m for m, _ in installer.DEFAULTS_STEPS] == [installer.DEFAULTS_MARK, installer.DEFAULTS_MARK_1017,
                                                         installer.DEFAULTS_MARK_1024]
    assert installer.DEFAULTS_MARK.split('：', 1)[0] != installer.DEFAULTS_MARK_1017.split('：', 1)[0]
    old = f'[VehicleCrew]\n{installer.DEFAULTS_MARK}\nEnabled=1\nHeliLandMs=0\nEdf41MotherParts=15\n'
    text, flipped = installer.apply_new_defaults(old)
    assert flipped == ['Edf41MotherParts'], flipped
    assert 'Edf41MotherParts=31' in text and 'HeliLandMs=0' in text, 'the 10-07 step was taken already: the player\'s 0 stays'
    assert installer.DEFAULTS_MARK_1017 in text and text.count(installer.DEFAULTS_MARK) == 1
    again = text.replace('Edf41MotherParts=31', 'Edf41MotherParts=15')
    assert installer.apply_new_defaults(again) == (again, []), 'a later install undid the player\'s own 15'
    mine = '[VehicleCrew]\nEnabled=1\nEdf41MotherParts=7 ; mine\n'
    text, flipped = installer.apply_new_defaults(mine)
    assert 'Edf41MotherParts' not in flipped and 'Edf41MotherParts=7 ; mine' in text and installer.DEFAULTS_MARK_1017 in text
    # an ini from before both steps takes both
    both = '[VehicleCrew]\nEnabled=1\nRescueAutoBoard=0\nEdf41MotherParts=15\n'
    text, flipped = installer.apply_new_defaults(both)
    assert sorted(flipped) == ['Edf41MotherParts', 'RescueAutoBoard'], flipped
    assert installer.DEFAULTS_MARK in text and installer.DEFAULTS_MARK_1017 in text
    # the install path goes through it (merge_ini keeps the player's 15, the step moves it)
    planned = installer.planned_ini(old, ini)
    assert re.search(r'^Edf41MotherParts=31$', planned[0], re.M) and 'Edf41MotherParts' in planned[3]


@test
def high_cam_default_migrates() -> None:
    """The high view's eye went from 45 m over / 35 m behind to 90 / 55 (the user, 2026-10-23: "俯瞰视角太小了"; PR #241):
    the shipped ini and the plugin's own defaults agree, and an old ini still at the old 45 / 35 is moved once by the
    installer's 10-24 defaults step (merge_ini alone keeps them: the player would never see the farther view); a value the
    player chose is kept, and a later install changes nothing."""
    import installer
    ini, crew = src('EDF6VehicleCrew.ini'), src('src/crew.h')
    assert re.search(r'^HighCamHeight=90$', ini, re.M) and re.search(r'^HighCamBack=55$', ini, re.M)
    assert 'float highCamHeight=90.0f;' in crew and 'float highCamBack=55.0f;' in crew
    assert installer.NEW_DEFAULTS_1024[installer.SECTION] == {'HighCamHeight': ('45', '90'), 'HighCamBack': ('35', '55')}
    marks = [m.split('：', 1)[0] for m, _ in installer.DEFAULTS_STEPS]
    assert len(set(marks)) == len(marks), marks
    done = (f'[VehicleCrew]\n{installer.DEFAULTS_MARK}\n{installer.DEFAULTS_MARK_1017}\n'
            'Enabled=1\nHighCamHeight=45\nHighCamBack=35\n')
    text, flipped = installer.apply_new_defaults(done)
    assert sorted(flipped) == ['HighCamBack', 'HighCamHeight'], flipped
    assert 'HighCamHeight=90' in text and 'HighCamBack=55' in text and text.count(installer.DEFAULTS_MARK_1024) == 1
    again = text.replace('HighCamHeight=90', 'HighCamHeight=45')
    assert installer.apply_new_defaults(again) == (again, []), 'a later install undid the player\'s own 45'
    mine = (f'[VehicleCrew]\n{installer.DEFAULTS_MARK}\n{installer.DEFAULTS_MARK_1017}\n'
            'HighCamHeight=60 ; mine\nHighCamBack=35\n')
    text, flipped = installer.apply_new_defaults(mine)
    assert flipped == ['HighCamBack'] and 'HighCamHeight=60 ; mine' in text and 'HighCamBack=55' in text, flipped
    # the install path: merge_ini keeps the old 45 / 35, the step moves them
    planned = installer.planned_ini(done, ini)
    assert re.search(r'^HighCamHeight=90$', planned[0], re.M) and re.search(r'^HighCamBack=55$', planned[0], re.M)
    assert 'HighCamHeight' in planned[3] and 'HighCamBack' in planned[3]


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
    # Keys come through the plugin's input source (common/edf/input_source.h), so a background host's keyboard is read.
    assert code.count('edf::AsyncKey(') == 1 and 'GetAsyncKeyState' not in code
    reader = code.split('Keys ReadKeys(', 1)[1].split('\n}', 1)[0]
    assert 'Down(VK_TAB)' in reader
    # The 「摆放」 panel's keys (, . / Enter Backspace) only where the panel works (the test range; enemy placement also works online): on any other map
    # they stay the game's and never switch the input back from the pad.
    assert reader.startswith('const MapCmdInput& in,bool placeKeys)') and 'const bool placeKeys=RangePlaceOn();' in code
    for key in ('VK_OEM_COMMA', 'VK_OEM_PERIOD', 'VK_OEM_2', 'VK_RETURN', 'VK_BACK'):
        assert f'Down({key})' in reader.split('if(placeKeys) {', 1)[1] and f'Down({key})' not in reader.split('if(placeKeys) {', 1)[0], key
    assert 'placeHoldsPad.store(placeKeys && ' in code and '(placeKeys && ((k.padUp && !g.was.padUp)' in code
    frame = mapc.split('bool Frame(unsigned char* human)', 1)[1].split('\n}\n', 1)[0]
    assert frame.index('if(!game.open) {') < frame.index('MapCommandFrame(in,onto)'), 'the commands read keys only with the map open'
    # The box never pans: the map's left drag gives way to it (the user, 2026-10-06: "操作 需要一个框选吧"; RTS since
    # 2026-10-19: a plain left drag boxes whenever there is a unit to box, the middle button's drag pans).
    assert 'if(Down(VK_MBUTTON) || (Down(VK_LBUTTON) && !Down(VK_CONTROL) && !MapCommandBoxing() && !MapCommandPointerCaptured())){mapcam::Drag(v,dx,dy);' in mapc
    assert 'else if(k.ctrl || (boxes && !armed)){g.boxing=true;' in code, 'a plain left drag over the map is the box'
    # The RTS groups, the Shift queue and HOLD (2026-10-19: 「合并分组经常无效，分开也是」): SPLIT / JOIN are gone, the
    # queue goes on with the map shut (map.cpp Frame ticks it before its open test), a group follows a squad's hand-over.
    assert 'SplitSquad' not in code and 'MergeSquads' not in code and 'SplitSquad' not in src('src/npcai.cpp')
    # The Shift queues advance on the scene's tick (2026-10-20): every soldier's pre-update before the player test, and the
    # shared frame tick (vehicles' input) -- not the player's map frame, which stops with the player dead or watching.
    human_frame = mapc.split('bool __fastcall MapHumanFrame(', 1)[1].split('\n}\n', 1)[0]
    assert 'MapCommandQueueTick();' not in frame and \
        human_frame.index('MapCommandQueueTick();') < human_frame.index('if(!human || !human[kHumanPlayer]'), 'the queue advances on the scene tick'
    assert 'GuardedTick(kStepMapQueue,&MapCommandQueueTick);' in src('src/crew.cpp')
    assert 'crew::AuthorityQueue();' in src('tools/map_command_runtime_check.cpp')
    assert 'NpcSquadSuccessor(' in code and 'handovers[handoverNext]=Handover{' in src('src/npcai.cpp')
    assert 'crew::RtsGroups();' in src('tools/map_command_runtime_check.cpp')
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
    for key in ('Ctrl', 'Shift', 'Tab', 'Z', 'H', 'V', 'X', '联机指令', '框选', '编队'):
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
def damage_stats_wired() -> None:
    """The damage statistics (src/damagestats.cpp, README 伤害统计, docs/damage-stats-re.md): its ini keys are read,
    range-checked, shipped and documented; the addresses it hooks are the doc's; it never touches the damage call
    0x54A586 (subcarrier.cpp's DamageCallReaches needs it stock); with the game present, 0x547C30's head is the bytes
    the detour copies and each call it redirects is an E8 to the function it expects; it is installed, reset with the
    mission, built, and its offline checks are wired."""
    plugin, ini, readme, doc = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini'), src('README.md'), src('docs/damage-stats-re.md')
    code, cmake, mission = src('src/damagestats.cpp'), src('CMakeLists.txt'), src('src/mission.cpp')
    for key in ('DamageStats', 'DamageStatsKey'):
        assert f'L"{key}"' in plugin and re.search(rf'^{key}=', ini, re.M) and key in readme, key
    assert 'FixInt("DamageStatsKey"' in plugin, 'DamageStatsKey is not range-checked'
    consts = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'\b(k\w+)=(0x[0-9A-F]+)\b', code)}
    for name in ('kDamage', 'kSpawn', 'kSpawnCall', 'kSettle', 'kArea'):
        assert f'{consts[name]:#X}'.replace('0X', '0x') in doc, name
    settle = [int(v, 16) for v in re.search(r'kSettleCalls\[\]=\{([^}]*)\}', code).group(1).split(',')]
    area = [int(v, 16) for v in re.search(r'kAreaCalls\[\]=\{([^}]*)\}', code).group(1).split(',')]
    assert settle == [0x232702, 0x23426F] and area == [0x23250E, 0x5425D6, 0x5427BF], (settle, area)
    assert '0x54A586' not in re.sub(r'//.*', '', code), 'the damage call 0x54A586 must stay stock (subcarrier.cpp DamageCallReaches)'
    assert 'InstallDamageStats();' in plugin and 'ResetDamageStats();' in mission and 'src/damagestats.cpp' in cmake
    assert 'add_executable(damage_stats_check' in cmake and 'damage_stats_check' in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0]
    assert 'StatsScenes(dir);' in src('tools/hud_view.cpp') and 'crew::StatsPage();' in src('tools/map_command_runtime_check.cpp')
    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if os.path.exists(dll):
        import edfre
        head = bytes(int(b, 16) for b in re.findall(r'0x[0-9A-F]+', re.search(r'kDamageHead\[\]=\{(.*?)\};', code, re.S).group(1)))
        assert edfre.img[consts['kDamage']:consts['kDamage'] + len(head)] == head, 'the damage function head'
        for site, target in [(consts['kSpawnCall'], consts['kSpawn'])] + [(s, consts['kSettle']) for s in settle] + \
                            [(s, consts['kArea']) for s in area]:
            assert edfre.img[site] == 0xE8, hex(site)
            rel = int.from_bytes(edfre.img[site + 1:site + 5], 'little', signed=True)
            assert site + 5 + rel == target, (hex(site), hex(site + 5 + rel), hex(target))


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

    # Closed by a key: the hold stays until every closing key is let go (the closing B is no seat switch, no stock B action;
    # the damage statistics' key, which shuts the map from its page, drains the same way).
    frame = code.split('bool Frame(unsigned char* human)', 1)[1].split('\n}\n', 1)[0]
    shut = frame.index('Close(close ? "Esc / B" : stats ? "the stats key" : "the map key");')
    assert shut < frame.index('game.draining=true;holds.store(true);') < frame.index(
        'if(k.map || k.esc || k.padMap || k.padClose || k.stats)return true;') < frame.index('if(!game.open) {'), 'map: the closing key drains'
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
    # Every key reader goes through the input source (edf::AsyncKey, common/edf/input_source.h): a direct user32 call
    # would read the desktop's keyboard into a background run and would escape this census.
    direct = [f'{d}/{f}' for d in ('src', 'autoturret/src') for f in os.listdir(os.path.join(ROOT, d))
              if f.endswith(('.cpp', '.h')) and re.search(r'GetAsyncKeyState\s*\(', src(f'{d}/{f}'))]
    assert direct == [], f'read keys through edf::AsyncKey, not user32 ({direct})'
    at_readers = [f for f in os.listdir(os.path.join(ROOT, 'autoturret', 'src')) if 'AsyncKey(' in src(f'autoturret/src/{f}')]
    assert at_readers == ['designate.cpp'], f'a new EDF6AutoTurret key reader: make it give way to the map ({at_readers})'
    readers = [f for f in os.listdir(os.path.join(ROOT, 'src')) if f.endswith('.cpp') and 'edf::AsyncKey(' in src(f'src/{f}')]
    assert sorted(readers) == sorted(['heli.cpp', 'highcam.cpp', 'payload.cpp', 'playerjet.cpp', 'seatswitch.cpp', 'turretcam.cpp',
                                      'overlay.cpp', 'map.cpp', 'mapcmd.cpp', 'proteus.cpp', 'npcai.cpp', 'drill.cpp', 'sazabi.cpp', 'dreadnought.cpp', 'sightzoom.cpp',
                                      'debug_spawn.cpp', 'firstperson.cpp', 'room_ui.cpp', 'vehicle_look.cpp', 'thermal.cpp', 'enemy_control.cpp']), f'a new key reader: make it give way to the map ({readers})'
    assert 'InFront() && !MapHoldsKeys()' in src('src/debug_spawn.cpp'), 'the debug spawn keys give way to the map'
    fp = src('src/firstperson.cpp')
    key_input = fp.split('bool KeyInputAllowed(const unsigned char* human) noexcept {', 1)[1].split('\n}', 1)[0]
    assert 'if(MapHoldsHumanKeys(human))return false;' in key_input
    assert '!KeyInputAllowed(human)' in fp and 'key.was=false;key.cancelled=down;return;' in fp, 'map input cancels only its owner camera FP gesture'
    assert 'CancelToggle(human);if(Current().on)Leave();' in fp and 'keyClaimHuman==human && keyClaimControl==key.control' in fp
    assert 'Foreground() && !MapHoldsKeys()' in src('src/room_ui.cpp'), 'the room menu keys give way to the map'
    thermal = src('src/thermal.cpp')
    # Thermal switching (#217: a press chooses the mode on an equipped seat in or out of the sight; it is drawn only
    # through the sight): it gives way to the map and the pause, and only a seat with the actual thermal equipment
    # (VehicleHasThermal) turns a press into a request; elsewhere the press is only answered on the HUD (rejectedAt).
    tick = thermal.split('void TickPlayer(Player& p,unsigned char* human,bool enabled,unsigned toggleKey) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'const bool admitted=!GamePaused() && !MapHoldsKeys();' in tick, 'thermal switching gives way to map/pause'
    assert 'const bool equipped=seated && VehicleHasThermal(vehicle,seat);' in tick, 'thermal switching requires the actual optic'
    assert 'if(admitted && equipped && press)CycleModeOf(p);' in tick and tick.count('CycleModeOf(') == 1, 'thermal: one gated request'
    assert tick.index('if(!equipped) {') < tick.index('CycleModeOf(p)'), 'a seat without the equipment returns before any request'
    assert 'shot.inSight=!MapOwnsView() &&' in tick, 'the image is drawn only through the sight, never under the map'
    assert 'process==GetCurrentProcessId()' in thermal, 'the thermal key reader requires this game window'
    assert 'seatsw::ChordHeld(At<std::uint16_t>(s,edf::kSeatButtons),static_cast<unsigned>(Cfg().thermalButton))' in thermal, \
        'a pad seat toggles thermal by its seat-button chord'
    for rel in ('src/sightzoom.cpp', 'src/highcam.cpp', 'src/turretcam.cpp'):
        assert 'seatsw::ChordOwns(' in src(rel) and 'c.thermalButton' in src(rel), f'{rel}: its button gives way to the thermal chord'
    controlled = src('src/enemy_control.cpp')
    sample = controlled.split('void Sample(Session& session,Input& sample) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(InputAllowed() && !MapHoldsHumanKeys(human) && Readable(record,kRecordBytes) && Readable(pad,kPadPathField+sizeof(int))) {' in sample
    assert 'ReadPadSticks(pad,record,At<int>(pad,kPadPathField)!=0 || PadFlag())' in sample, 'movement and look are read as the soldier reads its controller (enemy_control_pad.h)'
    assert 'const bool firstLocal=FirstLocalPad(At<int>(human,kHumanLocalPad));' in sample and 'if(firstLocal) {' in sample, \
        'keyboard input belongs only to the first local player (pad 0), whatever its class'
    assert 'index==0' not in sample, 'the class record (+0xD40) is not the player'
    allowed = controlled.split('bool InputAllowed() noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(GamePaused())return false;' in allowed and 'pid==GetCurrentProcessId()' in allowed

    opening = code.split('void Open(const unsigned char* human) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'CameraRayOf(human,eye,dir)' in opening and 'CameraRay(eye,dir)' not in opening
    own_hold = code.split('bool MapHoldsHumanKeys(const unsigned char* human) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'const ObjRef owner=inputOwner' in own_hold and 'held && owner.Is(human)' in own_hold
    assert 'AcquireSRWLockShared(&lock)' in own_hold and 'ReleaseSRWLockShared(&lock)' in own_hold
    assert 'TvHoldsHumanKeys(human)' in own_hold
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
    assert draw.index('if(MapScreen(drawer,ctx,t,viewProj') < draw.index('if(MapOwnsView() && MapHidesStockHud(passCamera)){FreeText(text);return;}') < draw.index('CarrierBars(')
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
                 'StockCells(snap.stockHud,cells)', 'snap.turretAim,lines,&at,controlsBottom'):
        assert call in hud, call
    # One auto-aim (2026-10-25, "这个自动瞄准是不是多余了一个版本，删掉一个"): one line naming it, no on / off pair, no
    # lead-circle mode, its banner or its key's text.
    assert 'StoresText(stores,_countof(stores),j,false);' in hud and 'Tx::autoAim' in hud
    table = src('src/hudtext.inc')
    assert 'HUDTEXT(autoAim,L"AUTO-AIM",L"自动瞄准",' in table
    for gone in ('autoAimOn', 'autoAimOffCircle', 'autoAimToggle', 'autoAimOffBanner', 'aimModeKeyUnset', 'aimModeButtonUnset', 'aimSightBinding'):
        assert f'HUDTEXT({gone},' not in table and f'Tx::{gone}' not in hud, gone
    assert 'Mode::leadCircle' not in hud and 'Mode::leadCircle' not in src('src/turretcam.cpp')
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
    human_frame = m.split('bool __fastcall MapHumanFrame(', 1)[1].split('// --- The camera ---', 1)[0]
    tv_hold = 'const bool tv=TvFrame(human,open && game.open,TvRead(human));'
    enemy_hold = 'const bool enemy=EnemyControlSuppressHuman(human);'
    merged_hold = 'return tv || open || enemy;'
    assert tv_hold in human_frame and enemy_hold in human_frame and merged_hold in human_frame, 'one hold shim runs TV and possession each frame'
    assert human_frame.index(tv_hold) < human_frame.index(enemy_hold) < human_frame.index(merged_hold), 'hold queries must execute before OR, without short-circuiting either lifecycle'
    assert human_frame.count('TvFrame(') == 1 and human_frame.count('EnemyControlSuppressHuman(') == 1
    assert 'TvView(&tvHuman,tvEye,tvLook)' in m, 'the TV through the map camera hook'
    assert 'kCamStep' not in src('src/tvguide.cpp'), 'one camera hook: the map\'s'
    assert 'holds.load(std::memory_order_relaxed) || TvHoldsKeys()' in m
    assert 'if(!TvSteer(static_cast<unsigned char*>(b)) && Cfg().enabled)Guide(' in src('src/missile.cpp')
    # 2026-10-10: the view placed after the stock step (it was a frame behind, inside the missile); the right button hands back.
    ms_src = src('src/missile.cpp')
    assert ms_src.index('nextUpdate(b,a2,a3,a4);') < ms_src.index('TvAfter(static_cast<unsigned char*>(b));'), 'TV view after the step'
    assert 'in.back=Down(VK_RBUTTON);' in m, 'the right button hands the TV back'
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
    assert 'if(!IsOnlineAuthority(v)){p->active=false;p->wrote=false;return;}' in body, \
        'clients may publish map rows but must exit before writing vehicle input'
    assert body.index('p->seen=ms;') < body.index('if(!IsOnlineAuthority(v))') < body.index('Write(v,f,*p,c0,c1);')
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
    # The stock walk-up recruit off (VanillaRecruit=0, the user 2026-10-11: "删了原版的率领npc机制"): +0x540 hidden only
    # around the stock Think (whose 0x596BD8 gate it feeds), put back right after; a script's squad the stock would let
    # in takes the map's join alone.
    hook = code.split('void __fastcall ThinkHook(void* human,const float* dt)', 1)[1].split('\n}', 1)[0]
    assert hook.index('HideWalkUp(') < hook.index('nextThink[I](human,dt);') < hook.index('h[kAutoFollow]=walkUp;')
    walk = code.split('bool WalkUpOff() noexcept {', 1)[1].split('\n}\n', 1)[0]
    # VanillaRecruit as played (host_settings.h: the host's online, this machine's ini otherwise; 2026-10-20 review)
    assert 'followOk' in walk and 'Cfg().customNpcAi' in walk and '!VanillaRecruitInEffect()' in walk, \
        'the walk-up stays when the map cannot recruit (AI off, SetFollow not as read) or VanillaRecruit=1'
    assert '!(joinOrder && NativeJoinable(top))' in cmd.split('switch', 1)[0]
    assert 'if(e.u.joinOnly)return (o==Order::recruit || o==Order::follow) && e.u.recruitable;' in takes
    assert 'L"VanillaRecruit"' in plugin and re.search(r'^VanillaRecruit=0\s*$', ini, re.M) and 'VanillaRecruit' in readme and 'VanillaRecruit' in doc


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
    assert 'airbound::KeepIn(soft,pos,j.m.vel,r,react,dive || line ? kept : want,&j.m.edgeBack,&j.m.edgeTurn);' in soft_edge
    # And the paratroop plane on its pass line (ferry_line.h, 2026-10-10): its line's ends stand off the map with room to
    # turn (support_entry.h PassEnd); the soft edge turned it back the moment it was made. Withdrawing, the edge's again.
    assert 'const bool line=j.ferry && j.mode!=Mode::withdraw;' in soft_edge, 'a ferry on its line keeps it'
    assert 'const bool dive=j.mode==Mode::dive && j.t.target && !j.t.flyer && airbound::Depth(soft,j.t.aim)>=0.0f;' in soft_edge, \
        'only a dive at a ground point inside the soft box is spared the edge'
    assert 'airbound::CapClimb(' in flight
    hover = flight[flight.index('void Hover(Jet& j,'):]
    assert 'airbound::ClampIn(JetSoftBox(j),inside,0.0f);' in hover[:1500], 'Hover: the goal inside the soft edge'
    assert 'anchor=SoftAnchor(*j,anchor,anchorIn);' in jet
    # Targets past it let be; the map's focus target (2026-10-09) only out past the play area's walls, where no jet goes.
    visit = combat.split('void VisitTarget(void* ctx,const void* object,const float* p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(k.focus || PastEdge(*k.j,p) || HoldFire(k.j->Vehicle(),object))return;' in visit
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
def support_arrives_from_off_the_map() -> None:
    """The support's arrival (2026-10-10, the user: 「我叫的支援直接凭空出现了」; docs/feedback-2026-10-10-support-arrival.md).
    The game-memory halves the offline tests cannot run: the move-area clamp held off as the hull is made (before the stock
    input's first clamp, which runs before the plugin's frame step) on every machine, and given back to a helicopter once
    inside; far rendering on the vehicle's AnimationModel node (its own vtable 0x17C4030 over umbra::Object's); the air
    entry planned off the map in the physical square past the draw distance; the paratroop plane on its pass line from its
    first frame, its stick by StickStarts, and on to the line's end when it is out; the ground entries the caller cannot
    see first."""
    spawn, heli, crew = src('src/jet_spawn.cpp'), src('src/heli.cpp'), src('src/crew.cpp')
    prep = spawn.split('unsigned char* PrepareSupportAircraft(', 1)[1].split('\n}\n', 1)[0]
    made = prep.index('CreateJet(body,matrix,&param,variant)')
    inset = prep.index('Put<float>(vehicle,kAreaInset,kNoInset);')
    assert made < inset < prep.rindex('return vehicle;'), 'the clamp held off as the hull is made, before it is handed back'
    assert prep.index('At<float>(vehicle,kAreaInset)') < inset, 'its own inset kept to give back'
    # The clamp runs in the stock input, before the plugin's frame step: JetFrame's own write came a frame late.
    step = crew[crew.index('nextInput[I](vehicle,hasInput,a3,a4);'):]
    assert step.index('nextInput[I](vehicle,hasInput,a3,a4);') < step.index('Guarded(kStepHeli,&HeliStep,v);')
    frame = heli.split('void HeliFrame(unsigned char* vehicle) noexcept {', 1)[1]
    assert frame.lstrip().startswith('SupportAircraftFrame(vehicle);'), 'every machine, any pilot: before any branch returns'
    arrival = spawn.split('void SupportAircraftFrame(unsigned char* v) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'Put<float>(v,kAreaInset,a.stockInset);a.held=false;' in arrival and 'MoveAreaBox(lo,hi)' in arrival
    assert 'kAnimationModelVtable=0x17C4030' in spawn and 'vt!=image+kAnimationModelVtable && vt!=image+kRenderNodeVtable' in spawn
    assert 'if(!At<const void*>(node,kNodeUmbra))return Far::later;' in spawn, 'no Umbra object yet: setBitmask would get a null this'
    plan = src('src/airstrike.cpp').split('support::Refusal PlanAirSupport(', 1)[1].split('\n}\n', 1)[0]
    assert 'const support::Reach reach{ArrivalHalf(),NearDrawDistance()};' in plan
    assert 'std::fabs(at[0])>reach.half || std::fabs(at[2])>reach.half' in plan, 'each formation slot inside the physical square'
    assert 'at[0]<area.lo[0]' not in plan, 'no slot is held to the map any more'
    assert 'SupportPassTurn(spec)' in plan
    crewh = src('src/crew.h')
    assert 'inline float ArrivalHalf() noexcept { const float h=HavokHalf(),w=WorldHalf();return (h<w ? h : w)-kArrivalRoom; }' in crewh
    assert 'const float v=ViewDistanceWanted();return v>kStockFarClip ? v : kStockFarClip;' in crewh
    # 2026-10-10 "改成无限": ViewDistance -1 (the default) reaches past the world's diagonal and the stock far pass end.
    assert 'float viewDistance=-1.0f;' in crewh and re.search(r'^ViewDistance=-1', src('EDF6VehicleCrew.ini'), re.M)
    assert 'Cfg().viewDistance<0.0f ? UnlimitedViewDistance()' in crewh
    dispatch = src('src/support_dispatch.cpp')
    board = dispatch.split('bool BoardAirborne(Deployment& deployed,bool networked) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'JetFerry(vehicle,plan.target,plan.units[i].matrix+8)' in board, 'the plane on its line from its first frame'
    assert 'JetFerry(' not in dispatch.split('bool Assign(Deployment& deployed) noexcept {', 1)[1].split('\n}\n', 1)[0]
    # Visibility was explicitly removed from support admission on 2026-10-11.
    # support_entry CTest verifies observer/draw/occlusion cannot affect entry selection.
    transport = src('src/transport.cpp')
    drop = transport.split('void DropFrame(Drop& d,ULONGLONG ms) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'transport::StickStarts(along,across,speed,aboard,kChuteBleed)' in drop and 'kDropRadius' not in transport
    assert 'if(!JetFerryDone(plane))JetWithdrawNow(plane,"paratroopers out");' in drop
    jet = src('src/jet.cpp')
    assert 'if(j->ferryGone)j->reap=true;' in jet
    cm = src('CMakeLists.txt')
    assert 'add_test(NAME ferry_line COMMAND ferry_line_test)' in cm and 'add_test(NAME support_entry COMMAND support_entry_test)' in cm

@test
def air_chase_wired() -> None:
    """The flyers are fought (src/air_chase.h, the user 2026-10-10: "飞行怪在打无人机，但是无人机和一堆npc竟然没有攻击蜜蜂",
    and, correcting the first fix, which had the gunship take no flyer and the charge drones and their carrier none over a
    200 m ceiling, given up and shunned when outrun: they are to attack them). No jet's target pick leaves a flyer out; a
    charge drone (and its carrier) lets go only of a current target out of its range; a charge drone climbs as fast as it
    flies at a flyer (at half that the flyers kept above it to the ceiling) and its run goes through airchase::Step (held
    off its target it goes off within its charge's blast, farther out it keeps after it); the gunship's side guns fire at a
    flyer, its shell only at the ground; the NPC soldiers rank targets in reach first (npc_logic.h PickTarget with their
    longest reach); the log has the target's height; the offline check is a CTest test."""
    combat, jet, npcai, bay = src('src/jet_combat.cpp'), src('src/jet.cpp'), src('src/npcai.cpp'), src('src/jet_bay.cpp')
    visit = combat.split('void VisitTarget(void* ctx,const void* object,const float* p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert "k.j->cmd.order!=Order::none || k.charges) && Dot(d,d)>k.range*k.range" in visit, 'a charge drone: its current target in range'
    assert 'const bool flyer=Flies(object,p,k.ms);' in visit
    for gone in ('Shunned', 'Allowed', 'kChargeCeiling', 'groundOnly', 'UnderCeiling', 'giveUp', 'ShunList'):
        assert gone not in combat + jet + src('src/air_chase.h') + src('src/jet_internal.h'), f'{gone}: a flyer left out again'
    charges = combat.split('bool Charges(const Jet& j) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'k.weapon==Weapon::charge' in charges and 'Preloaded(own.body)' in charges
    assert 'Pick pick{&j,pos,anchor,range,ms,Charges(j),' in combat
    assert 'bottom[3]={root[0],root[1]-kFlyerDepth,root[2]}' in combat, 'the ground under a high flyer is found'
    assert 'ty=%.0f' in combat, 'the target height in the jet log'
    assert 'climb=j.t.target && j.t.flyer ? kind.cruise : kind.cruise*0.5f;' in jet, 'a charge drone at a flyer: its full climb'
    assert 'for(int i=0;i<3;++i)goal[i]=j.t.aim[i]+j.t.tgtVel[i]*kChargeLead;' in jet, 'a charge drone leads its target'
    assert 'constexpr float kChargeLead=2.0f;' in jet and 'wantV[1]=Clamp((goal[1]-pos[1])*0.5f,-climb,climb);' in src('src/jet_flight.cpp'), \
        "the lead is 1 / Hover's vertical gain"
    assert 'airchase::Step(j->t.closing,j->t.target,d,kind.trigger,held,walled,ms)' in jet
    assert 'const float d=Len(to),held=std::fmin(kind.trigger*kTriggerHeld,kind.blast);' in jet
    gun = bay.split('bool GunAtTarget(Jet& j,SideGun g,', 1)[1].split('\n}\n', 1)[0]
    assert 'if(!gun.ready || !j.t.target)return false;' in gun and 'flyer' not in gun, 'the side guns at a flyer too'
    fire = bay.split('void GunshipFire(Jet& j,const unsigned char* v,const float* pos,ULONGLONG ms) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert fire.index('GunsAtTarget(j,v,pos,ms,"its NPC crew");') < fire.index('if(j.t.flyer || ms-j.shells.gunAt<kGunshipGapMs)return;')
    crew = bay.split('bool CrewShell(unsigned char* v,float dt,ULONGLONG ms) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'if(!j->t.target)return false;' in crew and 'const bool shell=!j->t.flyer && ' in crew
    assert 'PickTarget(s,h,eye,anchor,o.leash+engage,LongestReach(a))' in npcai and 'npc::PickTarget(world.enemies,' in npcai
    # Fire the game will not let hurt its target (docs/feedback-2026-10-12-ai-targeting.md): every AI asks WastedFire, the
    # damage hook feeds it whatever the statistics' switch, and the offline check runs in CTest.
    assert '[self](int k) noexcept { return !WastedFire(self,world.enemy[k].object); }' in npcai
    assert 'HoldFire(h,t->object,eye,known ? a.speed[w]' in npcai and 'if(WastedFire(p.self,object))return;' in npcai
    assert 'if(target && HoldFire(v,target))target=nullptr;' in npcai and 'if(p.best && HoldFire(v,p.best))' in npcai
    assert 'HoldFire(h,t.e->object,eye,armed ? a.speed[p.arm]' in npcai and 'p.fire=!hold && ShotOk(' in npcai
    assert 'p.speed==speed && p.gravity==gravity' in src('src/wastedfire.cpp')
    assert 'std::memcmp(p.from,from,12)==0' in src('src/wastedfire.cpp'), 'cached plans belong to the same muzzle and weapon'
    assert 'if(!HoldFire(v,o))f(o,a,0.0f);' in src('src/heli.cpp')
    assert 'if(HoldFire(p.self,object))return;' in src('src/ground.cpp')
    hook = src('src/damagestats.cpp').split('std::uintptr_t __fastcall DamageHook(', 1)[1].split('\n}\n', 1)[0]
    assert 'const Before b=Read(obj);' in hook and 'NoteWasted(obj,gdi,b);' in hook, 'fed whether the statistics are on or not'
    assert 'ResetWastedFire();' in src('src/mission.cpp')
    cm = src('CMakeLists.txt')
    assert 'add_executable(air_chase_check EXCLUDE_FROM_ALL tools/air_chase_check.cpp)' in cm
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/wastedfire.cpp)' in cm
    assert 'wasted_fire_check' in cm.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0].split(), 'wasted_fire_check runs in CTest'
    assert 'hit_predict_check' in cm.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0].split(), 'hit_predict_check runs in CTest'
    assert 'add_test(NAME hit_accept_native COMMAND hit_accept_native_test "${EDF6_NATIVE_DLL}")' in cm
    # The game's overrides the prediction asks are the side-effect-free ones, gated by their bytes (wastedfire.cpp).
    wf = src('src/wastedfire.cpp')
    for rva in ('0x54AA40', '0x4BC8A0', '0x3EE2F0', '0x4F7190', '0x3F6970', '0x401200', '0x4D1790'):
        assert rva in wf, rva
    for rva in ('0x504720', '0x51EFB0', '0x4C6650', '0x30E200', '0x374230'):
        assert rva not in wf, f'{rva} has side effects: never asked'
    assert 'air_chase_check' in cm.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0].split(), 'air_chase_check runs in CTest'
    assert '#include "../src/air_chase.h"' in src('tools/air_chase_check.cpp')
    # The charges' blast radii are their generated charges' (pylib/vcobjects.py jet_guns).
    vc = src('pylib/vcobjects.py')
    blast = float(re.search(r"'EDF6VC_BLAST_CHARGE\.SGO': \([\d.]+, ([\d.]+)\)", vc).group(1))
    doll = float(re.search(r"'EDF6VC_DOLL_CHARGE\.SGO': \([\d.]+, ([\d.]+)\)", vc).group(1))
    kinds = src('src/jet_internal.h')
    assert f'kBlastTrigger,false,Body::blast,{blast:.1f}f}}' in kinds, blast
    assert f'kDollTrigger,true,Body::doll,{doll:.1f}f}}' in kinds, doll

@test
def installer_recovery_regressions() -> None:
    from test_installer_recovery import run_checks
    run_checks()


@test
def installer_menu_8_opens_the_loadouts() -> None:
    """The pre-battle loadouts (squads, each soldier's class and colour, each tank's and jet's pylons) have their own
    main-menu entry (the user, 2026-10-10: “战斗外配置npc和挂载好像没做入口”: it was only menu 7's `l`, and
    menu 7's line named neither): 8 goes straight to support_loadout.edit and saves the ini the way menu 7 does."""
    import tempfile
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import installer
    import modfiles
    import support_config
    import support_loadout
    with tempfile.TemporaryDirectory() as game:
        plugins = os.path.join(game, 'Mods', 'Plugins')
        os.makedirs(plugins)
        ini = os.path.join(plugins, installer.PLUGIN + '.ini')
        with open(os.path.join(ROOT, 'EDF6VehicleCrew.ini'), 'rb') as f:
            original = f.read()
        with open(ini, 'wb') as f:
            f.write(original)
        prompts: list[str] = []
        opened: list[str] = []

        def ask(prompt: str) -> str:
            prompts.append(prompt)
            return '8' if len(prompts) == 1 else ''

        def loadouts(text: str, _ask) -> str:
            opened.append('loadout')
            return text + 'SupportPreset_SQUAD=ranger\n'

        def menu_7(text: str, _ask) -> str:
            opened.append('support')
            return text

        saved = (installer.ask, installer.pick_game, modfiles.game_running, support_loadout.edit, support_config.edit)
        installer.ask, installer.pick_game, modfiles.game_running = ask, lambda: game, lambda: True
        support_loadout.edit, support_config.edit = loadouts, menu_7
        try:
            assert installer.main([]) == 0
        finally:
            installer.ask, installer.pick_game, modfiles.game_running, support_loadout.edit, support_config.edit = saved
        assert '8 战前配置' in prompts[0] and '挂载' in prompts[0], prompts[0]
        assert opened == ['loadout'], opened
        with open(ini, 'rb') as f:
            assert f.read() == original + b'SupportPreset_SQUAD=ranger\n'


# ---------------------------------------------------------------- the EDF5 campaign (tools/make_edf5_campaign.py)


def _e5c_stub() -> tuple[dict[str, bytes], object, dict]:
    """make_edf5_campaign.build on a stand-in game (no Root.cpk to read): a file each, the packs' fixed ids (EDF5's
    65..67, the test range's 68)."""
    return {rel: f'stub {rel}'.encode() for rel in e5c.FILES}, e5c.Contents(65, 68), {'rows': [], 'skipped': []}


def _e5c_have_game() -> bool:
    import rootcpk
    return os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk'))


def _edf41_object(rel: str) -> bytes | None:
    """tools/edf41_objects.read of a shipped file; an archive some members of which are read from the player's EDF6
    (an entry's members_from: the King) from the real game, None without one (CI)."""
    import edf41_objects as eo
    import rootcpk
    if not any(rel in eo.stock_members(o) for o in eo.entries(eo.registry())):
        return eo.read(rel)
    return eo.read(rel, eo.game_source(rootcpk.DEFAULT_GAME)) if _e5c_have_game() else None


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
    assert content == e5c.Contents(65, 68) and content.campaign == e5c.CONTENT['EDF5'], content   # fixed ids
    packs = modes[len(stock):len(stock) + 6]   # each pack's offline mode, then its online one (more than one player)
    assert [m[e5c.M_CONTENT] for m in packs] == [65, 65, 66, 66, 67, 67]
    assert [(m[e5c.M_ONLINE], m[e5c.M_TYPE]) for m in packs] == [(0, 0), (1, 1)] * 3, 'not an offline and an online mode'
    assert len({m[e5c.M_MST].upper() for m in modes}) == len(stock) // 2 + 3 + 1, 'a pack\'s two modes on two saves'
    assert len({(m[e5c.M_TYPE], m[e5c.M_CONTENT]) for m in modes}) == len(modes), 'GetModeNo(type, content) ambiguous'
    for (off, on), pack in zip(zip(packs[0::2], packs[1::2]), e5c.PACKS):
        assert off[e5c.M_MST] == on[e5c.M_MST] == pack.mst
        for m, kind in ((off, e5c.OFFLINE), (on, e5c.ONLINE)):
            like = next(s for s in stock if s[e5c.M_CONTENT] == pack.like and s[e5c.M_TYPE] == m[e5c.M_TYPE])
            assert m[7] == like[7] and m[11:] == like[11:], 'the difficulty ranges are not the matching stock mode\'s'
            assert m[e5c.M_FILES] == pack.paths(kind) and (m[e5c.M_NAME], m[e5c.M_DESC]) == pack.keys(kind)
            assert m[e5c.M_BGM] == pack.bgm == e5c.BGM5, "the mode's HQ music is not EDF5's"
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
                assert r.items[7] == 0.0 and r.items[8] == e5c.BGM5, (pack.tag, i, "not EDF5's HQ background / music")
                rootcpk.default().read(r.items[1].split('app:/', 1)[1], 'MISSION.BVM')
            for rel in txt.values():
                assert len(sgo.read(files[rel])[1]['table']) == len(rows), rel
            thumbs = mdb.rab_read(files[image]).files
            assert [f.name for f in thumbs] == [e5c.thumb_name(r.items[2]) for r in rows], pack.tag
            own = e5c.edf5_thumbs()   # each mission shows EDF5's own thumbnail, not a stand-in on its map
            for r, f in zip(rows, thumbs):
                path = r.items[2].split(f'{e5c.SCRIPTS}/', 1)[1]
                assert f.stored == own[e5c.thumb_name(path).upper()].stored, (pack.tag, path)
            assert dsgo.compact(dsgo.parse(files[listed])) == files[listed]
    assert counts == {'main': 110, 'dlc1': 15, 'dlc2': 14}, counts
    # EDF5 Mission Pack 1's four (maps and fortress pre-generated: tools/edf5_dlc.py) are offered too
    assert not [x for x in p['skipped'] if x[0] in {pk.group for pk in e5c.PACKS}], p['skipped']
    # the plugin's EDF6VehicleCrew.packs.ini: a section per pack written, by its fixed id, nothing missing here
    packs_ini = files[e5c.PACKS_FILE].decode('ascii')
    for pack in (*e5c.PACKS, e5c.RANGE):
        rows = [r for r in p['rows'] if r['group'] == pack.group] or [None]
        section = packs_ini.split(f'[Pack{e5c.CONTENT[pack.tag]}]\r\n', 1)[1].split('\r\n\r\n', 1)[0].split('\r\n')
        assert section[0] == f'Tag={pack.tag}' and section[2] == f'Rows={len(rows)}', (pack.tag, section)
        assert section[3] == 'Missing=' + '0' * ((len(rows) + 3) // 4), (pack.tag, section)


@test
def edf5_campaign_pack_music() -> None:
    """Each pack's HQ / sortie screen music (the user, 2026-10-18: EDF5's packs EDF5's own, EDF4.1's EDF4.1's): every
    row's list member 8 (what EDF.dll 0x8406E0 plays while the row is selected) and its modes' ModeList member 3 (the
    fallback) are the pack's (Pack.bgm), the test range keeps RM015's row and the stock mode's music. The stand-in
    rows and modes need no game (CI)."""
    assert [p.bgm for p in e5c.PACKS] == [e5c.BGM5] * 3 == ['BGM_E5S02_EDFHonbu'] * 3
    assert [p.pack.bgm for p in e5c.PACKS41] == ['BGM_E4S02_EDFHonbu', 'BGM_Sinkou_intermission',
                                                 'BGM_Sinkou_taisenmenu']
    assert e5c.RANGE.bgm is None
    row = e5c.new_row(4, {'key': 'EDF41/M001', 'bgm': e5c.BGM41, 'progress': 0.5, 'flags': e5c.FLAGS}, [5])
    assert row.items[3].items == [5.0] and row.items[7] == 0.0 and row.items[8] == e5c.BGM41 and row.names == {10: 'flags'}
    template = [f'm{k}' for k in range(14)]
    for pack in (*e5c.PACKS, *(p.pack for p in e5c.PACKS41)):
        for kind in pack.kinds:
            entry = e5c.mode_entry(template, pack, 9, kind)
            assert entry[e5c.M_BGM] == pack.bgm and template[e5c.M_BGM] == 'm3', (pack.tag, kind)
            assert [entry[k] for k in (2, 4, 7, 11, 12, 13)] == [template[k] for k in (2, 4, 7, 11, 12, 13)]
    assert e5c.mode_entry(template, e5c.RANGE, 9)[e5c.M_BGM] == 'm3', "the range lost the stock mode's music"


@test
def edf5_campaign_pack_music_real() -> None:
    """The packs' music against the games themselves (a developer's machine: each part skipped without its game):
    EDF6's music bank holds every cue with its sound (TIKYUUX_MUSIC.AWB); EDF5's HQ music is its modes' ModeList member 3 (its lists have no music
    member), as is EDF4.1's (config.sgo, and each DLC's package.sgo); and EDF6's own rows 13..36 (missions 14..37,
    the user's example) play EDF5's."""
    import gamedir
    import rootcpk
    import sgo
    if not _e5c_have_game():
        return
    bank = os.path.join(rootcpk.DEFAULT_GAME, 'SOUND', 'PC', 'TIKYUUX_MUSIC')
    with open(bank + '.AWB', 'rb') as f:
        waves = _afs2_sizes(f.read(0x10000))
    for bgm in (e5c.BGM5, *(p.pack.bgm for p in e5c.PACKS41)):
        ids = _music_cue_waves(bank + '.ACB', bgm)   # a cue a bank only names, its sound stripped, plays nothing
        assert ids and all(waves.get(i, 0) > 1 << 20 for i in ids), (bgm, ids, "no sound in EDF6's music bank")
    table = dsgo.parse(rootcpk.default().read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    assert {r.items[8] for r in table[13:37]} == {e5c.BGM5} and {r.items[7] for r in table[13:37]} == {0.0}
    root5 = gamedir.find_other(gamedir.EDF5, near=rootcpk.DEFAULT_GAME)
    if root5:
        game5 = rootcpk.Game(root5)
        assert {str(m[3]) for m in sgo.read(game5.read('DEFAULTPACKAGE', 'CONFIG.SGO'))[1]['ModeList']} == {e5c.BGM5}
        assert {len(r) for r in sgo.read(game5.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO'))[1]['table']} == {4}
    root41 = _edf41_root()
    if root41:
        import edf41dlc
        modes41 = sgo.read(_cpk_file(os.path.join(root41, 'Root.cpk'), 'DEFAULTPACKAGE', 'CONFIG.SGO'))[1]['ModeList']
        assert {str(m[3]) for m in modes41} == {e5c.BGM41}
        want = {'mp01': e5c.BGM41_MP01, 'mp02': e5c.BGM41_MP02}
        for mount, dlc in edf41dlc.find(root41).items():   # only the DLC this EDF4.1 has
            assert {str(m[3]) for m in sgo.read(dlc.read('package.sgo'))[1]['ModeList']} == {want[mount]}, mount


def _music_cue_waves(acb_path: str, cue: str) -> list[int]:
    """The stream wave ids an ACB's cue plays: cue -> sequence -> its tracks' events' synth references (command
    0x7D0) -> waveforms (reference type 1). [] for a cue the bank does not name."""
    import struct
    import acb
    import cpk

    def table(b: bytes) -> list[dict]:
        b = b if b[:4] == b'@UTF' else cpk._unmask(b)
        body = b[8:8 + struct.unpack_from('>I', b, 4)[0]]
        at = struct.unpack_from('>I', body, 8)[0]
        return [{k: body[at + v[0]:at + v[0] + v[1]] if isinstance(v, tuple) else v for k, v in r.items()}
                for r in cpk.parse_utf(b)[3]()]
    with open(acb_path, 'rb') as f:
        head, _ = acb._row(f.read())
    t = {k: table(head[k]) for k in ('CueNameTable', 'CueTable', 'SequenceTable', 'TrackTable', 'TrackEventTable',
                                     'SynthTable', 'WaveformTable')}
    index = {r['CueName']: r['CueIndex'] for r in t['CueNameTable']}
    if cue not in index:
        return []
    entry = t['CueTable'][index[cue]]
    assert entry['ReferenceType'] == 3, (cue, entry['ReferenceType'])   # a sequence, as every music cue
    seq = t['SequenceTable'][entry['ReferenceIndex']]
    out = []
    for k in range(seq['NumTracks']):
        event = t['TrackEventTable'][t['TrackTable'][struct.unpack_from('>H', seq['TrackIndex'], 2 * k)[0]]['EventIndex']]
        cmd, p = event['Command'], 0
        while p + 3 <= len(cmd):
            code, size = struct.unpack_from('>HB', cmd, p)
            if code == 0x7D0:
                refs = t['SynthTable'][struct.unpack_from('>H', cmd, p + 5)[0]]['ReferenceItems']
                out += [t['WaveformTable'][i]['StreamAwbId'] for kind, i in struct.iter_unpack('>HH', refs) if kind == 1]
            p += 3 + size
    return out


def _afs2_sizes(head: bytes) -> dict[int, int]:
    """An AWB's wave id -> its size, from the file's header (pylib/awe.afs2_waves without the data)."""
    import struct
    assert head[:4] == b'AFS2'
    off_size, id_size = head[5], head[6]
    count = struct.unpack_from('<I', head, 8)[0]
    ids = struct.unpack_from(f'<{count}{"H" if id_size == 2 else "I"}', head, 16)
    offs = struct.unpack_from(f'<{count + 1}{"I" if off_size == 4 else "Q"}', head, 16 + count * id_size)
    return {w: offs[i + 1] - offs[i] for i, w in enumerate(ids)}


def _cpk_file(path: str, folder: str, name: str) -> bytes:
    """A file of a CPK, CRILAYLA-decompressed when it is."""
    import cpk
    import crilayla
    archive = cpk.Cpk(path)
    e = archive.index[(folder, name)]
    with open(path, 'rb') as h:
        h.seek(archive.base + int(e['FileOffset']))
        data = h.read(int(e['FileSize']))
    return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data


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
        assert first[1] == e5c.Contents(65, 68), 'the packs\' content ids moved for the other mod\'s (2026-10-15)'
        e5c.install(root, first)
        assert 'EDF5CampaignContent=65' in open(ini, encoding='utf-8').read()
        assert 'TestRangeContent=68' in open(ini, encoding='utf-8').read()
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
        assert 'EDF5CampaignContent=65' in open(ini, encoding='utf-8').read(), 'content id zeroed under a kept table'
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
    for campaign, want in ((True, e5c.Contents(65, 68)), (False, e5c.Contents(0, 68))):
        files, content, _ = e5c.build(rootcpk.DEFAULT_GAME, campaign=campaign)
        assert content == want, (campaign, content)
        assert set(files) == {e5c.CONFIG, e5c.PACKS_FILE, *e5c.TEXTS.values(), e5c.ACHIEVEMENT,
                              *(f for pk in (*(e5c.PACKS if campaign else ()), e5c.RANGE) for f in pk.files())}
        modes = sgo.plain(sgo.read(files[e5c.CONFIG])[1]['ModeList'])
        assert modes[:len(stock)] == stock
        ours = modes[-2:]
        assert [(m[e5c.M_ONLINE], m[e5c.M_TYPE]) for m in ours] == [(0, 0), (1, 1)]
        assert {m[e5c.M_CONTENT] for m in ours} == {content.range} and {m[e5c.M_MST] for m in ours} == {e5c.RANGE.mst}
        assert len({(m[e5c.M_TYPE], m[e5c.M_CONTENT]) for m in modes}) == len(modes), 'GetModeNo(type, content) ambiguous'
        for m, kind in zip(ours, e5c.RANGE.kinds):
            like = next(x for x in stock if x[e5c.M_CONTENT] == 0 and x[e5c.M_TYPE] == m[e5c.M_TYPE])
            # Online, no weapon level cap (the user, 2026-10-17: 「测试场不限制武器等级」; docs/weapon-level-cap-re.md):
            # every difficulty's element 6 is EDF.dll's own "no cap" [-1.0, -1.0], the rest the stock online story's.
            if kind == e5c.ONLINE:
                assert all(row[e5c.LIMIT_WEAPON] == [-1.0, -1.0] for row in m[e5c.M_LIMITS]), m[e5c.M_LIMITS]
                assert any(row[e5c.LIMIT_WEAPON] != [-1.0, -1.0] for row in like[e5c.M_LIMITS]), 'the stock mode has no cap'
                assert [r[:e5c.LIMIT_WEAPON] + r[e5c.LIMIT_WEAPON + 1:] for r in m[e5c.M_LIMITS]] == \
                       [r[:e5c.LIMIT_WEAPON] + r[e5c.LIMIT_WEAPON + 1:] for r in like[e5c.M_LIMITS]]
            else:
                assert m[e5c.M_LIMITS] == like[e5c.M_LIMITS]
            assert m[11:] == like[11:], 'not the stock story mode of its kind'
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
        shorter = lambda r, root41=None, skip41=(): {**real_plan(r, root41, skip41), 'rows': real_plan(r, root41, skip41)['rows'][:-1]}   # an update that writes other bytes
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
        assert content == e5c.Contents(65, 68) and files[e5c.CONFIG] == e5c.build(root)[0][e5c.CONFIG]
        done, kept = e5c.remove(root)
        assert not kept, kept
        assert modfiles.read(e5c.rel_path(root, e5c.CONFIG)) == other, "the other mod's mode table lost"


@test
def edf5_campaign_refusals() -> None:
    """A mode table that is not SGO, one that already has EDF5 pack modes this tool did not leave, one whose save files
    collide, one on a pack's fixed content id, and the older version's appended list changed by someone since all
    refuse (nothing written) instead of stopping the installer; an empty pack is not installed."""
    import base64
    import copy
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
        ver, members = sgo.read(game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))   # another mod on a pack's fixed id
        members['ModeList'].append(copy.deepcopy(members['ModeList'][2]))
        members['ModeList'][-1][e5c.M_CONTENT] = e5c.CONTENT['EDF41MP01']
        members['ModeList'][-1][e5c.M_MST] = 'MOD7.MST'
        modfiles.atomic_write(e5c.rel_path(root, e5c.CONFIG), sgo.write_depth_first(ver, members))
        _e5c_refuses(root, 'another mod\'s mode on a pack\'s fixed content id')
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
        with patched(e5c, plan=lambda r, root41=None, skip41=(): {**real_plan(r, root41, skip41),
                                                  'rows': [x for x in real_plan(r, root41, skip41)['rows'] if x['group'] != 'dlc2']}):
            _e5c_refuses(root, 'an empty pack')
        assert not e5c.installed(root), 'a refusal wrote a manifest'


@test
def edf5_campaign_two_machines() -> None:
    """A room publishes only its content id (DLC) and row (MISSION_NO) (docs/mod-room-isolation.md, feedback
    2026-10-15 「别人加的时候说未持有此内容」): from the real game, machine A (no other mod, every resource) and
    machine B (another mod's modes at 3 and 4, EDF5 Mission Pack 1's IG_DESERT508 not installed, a 4.1 map not
    converted) give every (online mode's content id, list row) the same mission. Before 2026-10-18 B's ids followed
    the other mod's (5.., the guest did not own the host's pack) and its lists lost the missing missions' rows (the
    rows after them moved: the wrong mission, or a black screen). B's rows it lacks are placeholders, the same
    definition digest, only its Missing mask differs."""
    import copy
    import dsgo
    import edf5_dlc
    import gamedir
    import rootcpk
    import sgo
    if not _e5c_have_game():
        return
    root41 = gamedir.find_other(gamedir.EDF41, near=rootcpk.DEFAULT_GAME)
    reg = edf5_dlc.registry()
    maps = {f'{g}.MAC' for g, x in reg['groups'].items() if x['kind'] == 'map'}
    names = {tuple(rel.split('/', 1)) for x in reg['groups'].values() if x['kind'] == 'object' for rel in x['files']}

    def rooms(files: dict[str, bytes]) -> dict[tuple[int, int], str]:
        out = {}
        for m in sgo.plain(sgo.read(files[e5c.CONFIG])[1]['ModeList']):
            rel = 'MISSION/' + m[e5c.M_FILES][0].split('app:/Mission/', 1)[-1].upper()
            if m[e5c.M_TYPE] == 1 and rel in files:   # our online modes: the room's (DLC, MISSION_NO)
                for i, r in enumerate(dsgo.parse(files[rel]).root.get('table').items):
                    out[(int(m[e5c.M_CONTENT]), i)] = r.items[2]
        return out
    real41 = e5c.plan41   # the 4.1 rows read the real game's archives (a stand-in game has none)
    with _e5c_game() as (root_a, game), patched(e5c, plan41=lambda _r, m, r41, skip=frozenset():
                                                real41(rootcpk.DEFAULT_GAME, m, r41, skip)):
        with patched(edf5_dlc, offered=lambda _root: (maps, names, {})):
            a = e5c.build(root_a, root41=root41)
    with _e5c_game() as (root_b, game), patched(e5c, plan41=lambda _r, m, r41, skip=frozenset():
                                                real41(rootcpk.DEFAULT_GAME, m, r41, skip)):
        ver, members = sgo.read(game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))
        for content, online in ((3, 0), (4, 1)):   # another mod's modes, as DEFP_MOD1.MST
            members['ModeList'].append(copy.deepcopy(members['ModeList'][2 + online]))
            members['ModeList'][-1][e5c.M_CONTENT] = content
            members['ModeList'][-1][e5c.M_MST] = 'MOD1.MST'
        modfiles.atomic_write(e5c.rel_path(root_b, e5c.CONFIG), sgo.write_depth_first(ver, members))
        skip41 = {'NW_KOUSOUBLD01': '转换失败：测试'} if root41 else {}
        with patched(edf5_dlc, offered=lambda _root: (maps - {'IG_DESERT508.MAC'}, names, {'IG_DESERT508': '测试'})):
            b = e5c.build(root_b, root41=root41, skip41=skip41)
    assert a[1] == b[1] == e5c.Contents(65, 68, 69 if root41 else 0), (a[1], b[1])
    room_a, room_b = rooms(a[0]), rooms(b[0])
    assert room_a == room_b and len(room_a) > 140, (len(room_a), sorted(set(room_a.items()) ^ set(room_b.items()))[:6])
    held = {r['key'] for r in b[2]['rows'] if not e5c.available(r)}
    assert {'EDF5_OLD_SCRIPT/DLC/DM018', 'EDF5_OLD_SCRIPT/DLC/DM020'} <= held, held
    assert not [r for r in a[2]['rows'] if not e5c.available(r)], 'machine A lacks nothing'
    ini_a, ini_b = a[0][e5c.PACKS_FILE].decode(), b[0][e5c.PACKS_FILE].decode()
    digests = lambda text: re.findall(r'Digest=(\w+)', text)
    assert digests(ini_a) == digests(ini_b) and ini_a != ini_b, 'same definitions, other placeholders'
    # B's placeholders are never written as missions, and nothing opens them
    for p41 in e5c.PACKS41:
        for r in b[2]['rows']:
            if r['group'] == p41.pack.group and not e5c.available(r):
                assert not any(rel.startswith(f'MISSION/{p41.folder}/{r["path"]}/') for rel in b[0]), r['path']
    dlc1 = dsgo.parse(b[0][e5c.PACKS[1].list]).root.get('table').items
    held_rows = {i for i, r in enumerate(dlc1) if r.items[2] in held}
    assert held_rows and not any(int(n) in held_rows for r in dlc1 for n in r.items[3].items), 'a placeholder is a successor'


@test
def edf5_campaign_legacy_rows() -> None:
    """An install from before 2026-10-18 that left a mission out (its rows after it one higher up) and its saves: the
    next install records that list as the pack's legacy layout and writes Legacy (each old row's row now) into
    EDF6VehicleCrew.packs.ini, so the plugin moves such a save once (src/mission_packs.h MigrateModeSave); an install
    over a list that is already the definition records nothing, and the record survives later installs."""
    if not _e5c_have_game():
        return
    with _e5c_game() as (root, game):
        real_plan = e5c.plan

        def older(r: str, root41: object = None, skip41: object = ()) -> dict:   # the old planner: DM018 left out
            p = real_plan(r, root41, skip41)   # type: ignore[arg-type]
            return {**p, 'rows': [x for x in p['rows'] if x['key'] != 'EDF5_OLD_SCRIPT/DLC/DM018']}
        with patched(e5c, plan=older):
            e5c.install(root, e5c.build(root))
        assert 'legacy' not in e5c.load_manifest(root), 'the first install has no older list'
        files, _, p = e5c.build(root)
        dlc1 = [r for r in p['rows'] if r['group'] == 'dlc1']
        old = p['legacy']['EDF5DLC1']
        assert set(p['legacy']) == {'EDF5DLC1'} and len(old) == len(dlc1) - 1, p['legacy'].keys()
        at = [r['key'] for r in dlc1].index('EDF5_OLD_SCRIPT/DLC/DM018')
        want = list(range(at)) + list(range(at + 1, len(dlc1)))
        assert f'Legacy={len(old)}:' + ','.join(map(str, want)) in files[e5c.PACKS_FILE].decode(), 'the rows after DM018 one down'
        e5c.install(root, (files, _, p))
        assert e5c.load_manifest(root)['legacy'] == p['legacy']
        again = e5c.build(root)   # the list is the definition now: the record is what keeps the older layout
        assert again[2]['legacy'] == p['legacy'] and again[0][e5c.PACKS_FILE] == files[e5c.PACKS_FILE]


@test
def mission_pack_ids_agree() -> None:
    """The plugin's fixed ids and identity format (src/mission_packs.h) are the installer's (make_edf5_campaign
    CONTENT, PACKS_FILE): base, count, the three ini runs, the attribute's name, the 512-row cap; the plugin reads
    the file beside its ini under the name the installer writes."""
    header = src('src/mission_packs.h')
    assert f'constexpr int kBase={e5c.CONTENT_BASE},kCount={len(e5c.SLOTS)};' in header
    assert (f'constexpr int kEdf5={e5c.CONTENT["EDF5"]},kRange={e5c.CONTENT["EDF6TR"]},kEdf41={e5c.CONTENT["EDF41"]};'
            in header)
    assert 'constexpr int kMaxRows=512;' in header and 'constexpr char kAttribute[]="AF_PACK";' in header
    assert e5c.PACKS_FILE == 'Plugins/EDF6VehicleCrew.packs.ini' and 'L".packs.ini"' in src('src/plugin.cpp')
    text = e5c.packs_ini((e5c.RANGE,), {e5c.RANGE.group: [{'key': 'EDF6/X'}, {'key': 'EDF6/Y', 'missing': 'x'}]}).decode()
    assert '[Pack68]\r\nTag=EDF6TR\r\nDigest=' in text and '\r\nRows=2\r\nMissing=2\r\n' in text, text
    assert e5c.missing_mask([{}] * 4 + [{'missing': 'x'}]) == '01' and e5c.missing_mask([{'missing': 'x'}] * 4) == 'f'


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
def edf41_menu_bg_math() -> None:
    """tools/edf41_menu_bg.py's reprojection (no game): 4.1's camera (vertical fov 0.96 rad from the origin) mapped to
    EDF6's screen in model units (+-9.6 x +-5.4): a point on the frustum's edge lands on the screen's edge at any depth,
    BG_Stripe's corner (z = -31.03, the frame's back) just past it; depth() is projective, falls with distance and
    stays in 0 .. 1.5 over the layers; clipping keeps a triangle's inside part with its attributes; the row-vector
    rotation matrix is mdb's (a 52-degree turn about -Y gives Menu_BG01's BGCircle_map3 bind rows); the HUiBG layout
    gets one layout more, first, on the new skin, the stock ones as they were."""
    import math
    import sgo
    import edf41_menu_bg as b
    for z in (-8.84, -17.5, -31.03):
        x, y, _ = b.project((b.TAN_Y * b.ASPECT * -z, b.TAN_Y * -z, z))
        assert abs(x - 9.6) < 1e-4 and abs(y + 5.4) < 1e-4, (z, x, y)   # EDF6's UI is y down
    x, y, _ = b.project((29.02, 16.33, -31.03))
    assert 9.6 < x < 9.8 and -5.5 < y < -5.4, (x, y)
    ds = [b.depth(z) for z in (-8.0, -13.0, -17.5, -25.0, -32.0)]
    assert ds == sorted(ds, reverse=True) and abs(ds[0] - b.DEPTH_RANGE) < 1e-9 and abs(ds[-1]) < 1e-9, ds
    # projective: the depth of a plane's points is affine in their screen position (planes stay planes)
    pts = [(-5.0, 1.0, -10.0), (5.0, 1.0, -30.0), (0.0, 4.0, -20.0)]
    proj = [b.project(p) for p in pts]
    q = b.project(tuple(sum(p[k] for p in pts) / 3 for k in range(3)))   # a point of that plane
    dx, dy = proj[1][0] - proj[0][0], proj[1][1] - proj[0][1]
    ex, ey = proj[2][0] - proj[0][0], proj[2][1] - proj[0][1]
    det = dx * ey - dy * ex
    u = ((q[0] - proj[0][0]) * ey - (q[1] - proj[0][1]) * ex) / det
    v = (dx * (q[1] - proj[0][1]) - dy * (q[0] - proj[0][0])) / det
    assert abs(proj[0][2] + u * (proj[1][2] - proj[0][2]) + v * (proj[2][2] - proj[0][2]) - q[2]) < 1e-9
    assert abs(b.depth_slope(-20.0) - (b.depth(-20.001) - b.depth(-19.999)) / -0.002) < 1e-6
    tri = [([(0.0, 0.0, -10.0, 1.0), (0.0, 0.0)], (0.0, 0.0, -10.0)),
           ([(40.0, 0.0, -10.0, 1.0), (1.0, 0.0)], (40.0, 0.0, -10.0)),
           ([(0.0, 2.0, -10.0, 1.0), (0.0, 1.0)], (0.0, 2.0, -10.0))]
    poly = b.clip_polygon(tri)
    edge = b.SIDE_KEEP * b.TAN_Y * b.ASPECT * 10
    assert len(poly) == 4 and max(p[1][0] for p in poly) <= edge + 1e-6, poly
    for elems, view in poly:   # attributes follow the cut: u = x / 40 on the triangle
        assert abs(elems[1][0] - view[0] / 40) < 1e-9 and abs(elems[0][0] - view[0]) < 1e-9
    m = b.quat_matrix((0.0, -math.sin(math.radians(26)), 0.0, math.cos(math.radians(26))))
    assert abs(m[0] - 0.6157) < 1e-3 and abs(m[2] - 0.7880) < 1e-3 and abs(m[8] + 0.7880) < 1e-3, m[:11]
    stock = {'BG_BASE': ['Layout', 'app:/UI/BG_skin.sgo', [0, 0, 0], [0, 0, 1920, 1080], [], 0, 0, []],
             'BG_BASE_B': ['Layout', 'app:/UI/BG_B_skin.sgo'], 'BG_BASE_C': ['Layout', 'app:/UI/BG_C_skin.sgo'],
             'BG_BASE_D': ['Layout', 'app:/UI/BG_D_skin.sgo'], 'xgs_ui_object_class': 'HUiBG',
             'layout_tree': [['BaseFrame'], [['BG_BASE'], 0, ['BG_BASE_B'], 0, ['BG_BASE_C'], 0, ['BG_BASE_D'], 0]]}
    data = sgo.write_depth_first(258, stock)
    out = sgo.plain(sgo.read(b.lyt(data))[1])
    base = sgo.plain(sgo.read(data)[1])
    assert {k: v for k, v in out.items() if k not in (b.LAYOUT, 'layout_tree')} == \
        {k: v for k, v in base.items() if k != 'layout_tree'}, 'a stock layout changed'
    assert out[b.LAYOUT][1] == f'app:/UI/{b.STEM}_skin.sgo' and out[b.LAYOUT][2:] == base['BG_BASE'][2:]
    assert out['layout_tree'] == [['BaseFrame'], [[b.LAYOUT], 0, *base['layout_tree'][1]]]
    try:
        b.lyt(b.lyt(data))
    except ValueError:
        pass
    else:
        raise AssertionError('the layout added twice')


@test
def edf41_menu_bg_install() -> None:
    """tools/edf41_menu_bg.py install on a stand-in game (conversion stubbed): every file written as edf41menubg's, a
    second install from the same 4.1 files converts nothing, no EDF4.1 writes nothing (and takes an earlier install
    out), another mod's copy of a stock file it extends leaves the whole background out with that file untouched, removal deletes what it
    wrote; the installer and the release bundle carry it, and the asset pack lists its sources."""
    import tempfile
    import edf41_menu_bg as b
    import edf41src
    import export_edf41_assets as exp
    import ledger
    made: list[str] = []

    class Src:
        def __init__(self, _root41: str) -> None:
            pass

        def fingerprint(self, rel: str) -> list[int]:
            return [len(rel), 1]

    def build(_root: str, _root41: str) -> dict[str, bytes]:
        made.append('x')
        return {rel: f'bg {rel}'.encode() for rel in b.FILES}

    assert b.OWNER in ledger.OWNERS and set(b.SOURCES) <= set(exp.wanted()) and 'MENU' in edf41src.TOP
    assert "'edf41_menu_bg'" in src('tools/build_release.py')
    inst = src('tools/installer.py')
    assert 'edf41_menu_bg.install(game, root41)' in inst and 'edf41_menu_bg.remove' in inst
    with tempfile.TemporaryDirectory() as game, patched(b, build=build, Source=Src), \
            patched(b.edf41src, problems=lambda *_a, **_k: []):
        os.makedirs(os.path.join(game, 'Mods'))
        assert b.install(game, None) == ([], [b.NO_EDF41]) and not made
        written, skipped = b.install(game, 'edf41')
        assert not skipped and len(written) == len(b.FILES) and made == ['x']
        led = ledger.Ledger(game)
        assert all(led.owners(rel) == [b.OWNER] for rel in b.FILES)
        assert b.install(game, 'edf41') == ([], []) and made == ['x'], 'converted again from the same files'
        assert b.install(game, None)[0] == [] and not ledger.Ledger(game).owned_by(b.OWNER), 'an old install left in'
        other = os.path.join(game, 'Mods', 'UI', 'UIRESOURCEGROUP_HQ.SGO')
        os.makedirs(os.path.dirname(other), exist_ok=True)
        with open(other, 'wb') as f:
            f.write(b'another mod')
        written, skipped = b.install(game, 'edf41')
        assert not written and 'UIRESOURCEGROUP_HQ.SGO' in skipped[0], skipped
        with open(other, 'rb') as f:
            assert f.read() == b'another mod'
        os.remove(other)
        assert b.install(game, 'edf41')[0]
        b.remove(game)
        assert not ledger.Ledger(game).owned_by(b.OWNER)
        assert not any(os.path.exists(os.path.join(game, 'Mods', *rel.split('/'))) for rel in b.FILES)


@test
def edf41_menu_bg_real() -> None:
    """tools/edf41_menu_bg.py on the real games (a developer's machine; CI has neither): 4.1's Menu_BG01 reprojected
    onto EDF6's screen. Every still layer lands where 4.1's camera shows it (its extent = its projected 4.1 extent;
    the panoramic band clipped to the kept region), the back stripe fills the screen just past its edge, the two discs
    off the axis keep their size in their own plane scaled by their depth and sit at their projected centre (the CAS's
    fixed translations moved to match), depths stay in 0 .. 1.5; the files read back (MDB 0x20, CAS 0x204, SGOs, the
    clip's names); lyt_bg.sgo is EDF6's own plus the one layout. The plugin's hook sites are the code it reads."""
    import math
    import struct
    import cas_legacy
    import mdb
    import mdb_legacy
    import rootcpk
    import sgo
    import edf41_menu_bg as b
    if not _e5c_have_game():
        return
    import pefile
    code = src('src/edf5campaign.cpp')

    def sig(name: str) -> bytes:
        body = code.split(f'const unsigned char {name}[]={{', 1)[1].split('}', 1)[0]
        return bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    pe = pefile.PE(os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll'), fast_load=True)
    img = pe.get_memory_mapped_image()
    for name, rva in (('kBgThemeCallerSig', 0x8BE6B0), ('kBgThemeSig', 0x8BE6E0), ('kChildByNameSig', 0x839FD0)):
        assert img[rva:rva + len(sig(name))] == sig(name), name
    assert 'L"BG_BASE_EDF41"' in code and b.LAYOUT == 'BG_BASE_EDF41'
    root41 = _edf41_root()
    if not root41:
        return
    files = b.build(rootcpk.DEFAULT_GAME, root41)
    assert set(files) == set(b.FILES)
    stock = sgo.plain(sgo.read(rootcpk.default().read('UI', 'LYT_BG.SGO'))[1])
    lyt = sgo.plain(sgo.read(files[b.LYT])[1])
    assert {k: v for k, v in lyt.items() if k not in (b.LAYOUT, 'layout_tree')} == \
        {k: v for k, v in stock.items() if k != 'layout_tree'}
    assert lyt['layout_tree'][1][:2] == [[b.LAYOUT], 0] and lyt['layout_tree'][1][2:] == stock['layout_tree'][1]
    for rel in b.GROUPS:   # every scene that preloads the backgrounds preloads ours too, the rest as it was
        base = sgo.plain(sgo.read(rootcpk.default().read(*rel.split('/')))[1])['resource']
        got = sgo.plain(sgo.read(files[rel])[1])['resource']
        ours = [f'APP:/{r}' for r in (b.SKIN, b.SGO, b.RAB, b.CAS)]
        at = base.index('APP:/UI/BG_D.CAS') + 1
        assert got == base[:at] + ours + base[at:], rel
    groups = [n for n in rootcpk.default().names('UI') if n.startswith('UIRESOURCEGROUP_') and
              'APP:/UI/LYT_BG.SGO' in sgo.plain(sgo.read(rootcpk.default().read('UI', n))[1]).get('resource', [])]
    assert sorted(f'UI/{n}' for n in groups) == sorted(b.GROUPS), groups
    skin = sgo.plain(sgo.read(files[b.SKIN])[1])
    model_sgo = sgo.plain(sgo.read(files[b.SGO])[1])
    assert skin['base'] == f'app:/UI/{b.STEM}.sgo' and skin['object_class'] == 'Model'
    assert model_sgo['animation_model'] == [[[f'app:/UI/{b.STEM}.rab', b.MODEL]], f'app:/UI/{b.STEM}.cas', 0]
    src41 = b.Source(root41)
    cas41 = src41.read(b.SOURCES[2])
    pose = b.Pose(cas41)
    rab41 = mdb.rab_read(src41.read(b.SOURCES[1]))
    old = mdb.mdb_read(mdb_legacy.mdb_from_legacy(mdb.cmpl_decompress(
        next(f for f in rab41.files if f.name == b.MODEL).stored)))
    rab6 = mdb.rab_read(files[b.RAB])
    assert [f.name for f in rab6.files] == [f.name for f in rab41.files]
    new = mdb.mdb_read(mdb.cmpl_decompress(next(f for f in rab6.files if f.name == b.MODEL).stored))
    assert new.version == 0x20 and len(new.objects) == len(old.objects)
    cas6 = cas_legacy.cas_layout(files[b.CAS])
    assert cas6.version == cas_legacy.CAS_6
    names = [old.name_of(x.name) for x in old.bones]

    def extent(md: mdb.Mdb, o: mdb.Object, pose_: b.Pose, project: bool) -> list[float]:
        world = pose_.matrix(md.name_of(md.bones[o.bone].name), md.bones[o.bone].local)
        lo, hi = [math.inf] * 3, [-math.inf] * 3
        for me in o.meshes:
            pi = b._pos_index(me)
            for e in b.decode(me):
                p = b._transform(e[pi], world)
                if project:   # where 4.1 shows it, cut at the kept region's sides (as the converter clips)
                    x, y, z = b.project(p)
                    ex, ey = b.SIDE_KEEP * 9.6, b.SIDE_KEEP * 5.4
                    p = (max(-ex, min(ex, x)), max(-ey, min(ey, y)), z)
                for k in range(3):
                    lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
        return lo + hi
    discs = ('BGCircle_map3', 'BGCircle_map4')
    pose6 = b.Pose(b.patch_cas(cas41, pose, {n: b.project(tuple(pose.translation(n))) for n in discs}))
    for o_old, o_new in zip(old.objects, new.objects):
        name = names[o_old.bone]
        got = extent(new, o_new, pose6, False)
        assert new.materials[o_new.meshes[0].material].b2 == old.materials[o_old.meshes[0].material].b2, name
        if pose.spins(name):   # the tilted discs spread in depth (drawn by material priority b2, as every layer)
            continue
        assert -1e-6 <= got[2] and got[5] <= b.DEPTH_RANGE + 1e-6, (name, got)
        want = extent(old, o_old, pose, True)
        if name in ('WorldMap', 'WorldMapFrame'):   # clipped: inside the kept region, wider than the screen
            assert got[0] < -9.6 and got[3] > 9.6 and max(abs(got[0]), got[3]) <= b.SIDE_KEEP * 9.6 + 1e-3, (name, got)
            continue
        assert max(abs(g - w) for g, w in zip(got, want)) < 0.01, (name, got, want)   # half-float positions: 1 px
    stripe = extent(new, next(o for o in new.objects if new.name_of(new.bones[o.bone].name) == 'BG_Stripe'), pose6, False)
    assert stripe[0] < -9.6 < 9.6 < stripe[3] and stripe[1] < -5.4 < 5.4 < stripe[4], stripe
    for disc in discs:
        t = pose.translation(disc)
        want = tuple(struct.unpack('<3f', struct.pack('<3f', *b.project(tuple(t)))))
        assert pose6.translation(disc) == want, disc
        bind = new.bones[[new.name_of(x.name) for x in new.bones].index(disc)].local[12:15]
        assert max(abs(x - y) for x, y in zip(bind, b.project(tuple(t)))) < 1e-5, (disc, bind)
        o_old = next(o for o in old.objects if names[o.bone] == disc)
        o_new = next(o for o in new.objects if new.name_of(new.bones[o.bone].name) == disc)
        r_old = max(abs(e[b._pos_index(o_old.meshes[0])][0]) for e in b.decode(o_old.meshes[0]))
        r_new = max(abs(e[b._pos_index(o_new.meshes[0])][0]) for e in b.decode(o_new.meshes[0]))
        assert abs(r_new - r_old * b.S / -t[2]) < 0.01, (disc, r_old, r_new)
    l41, l6 = cas_legacy.cas_layout(cas41).canm, cas6.canm
    assert [c.name for c in l41.clips] == [c.name for c in l6.clips] and l41.names == l6.names
    # what the real game needed (2026-10-18): faces clockwise in x / y as EDF6's own BG (else culled: a black screen),
    # texture names spelled as the RAB's members (else a crash), the turning bones' spins mirrored with their models
    members = [f.name for f in rab6.files]
    assert all(t.filename in members for t in new.textures), [t.filename for t in new.textures]
    for o in new.objects:
        for me in o.meshes:
            ps = [e[b._pos_index(me)] for e in b.decode(me)]
            idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
            area = sum((ps[j][0] - ps[i][0]) * (ps[k][1] - ps[i][1]) - (ps[j][1] - ps[i][1]) * (ps[k][0] - ps[i][0])
                       for i, j, k in zip(idx[0::3], idx[1::3], idx[2::3]))
            assert area < 0, (new.name_of(new.bones[o.bone].name), area)
    for name in pose.euler:
        if pose.spins(name):
            for k, k6 in zip(pose.euler[name], pose6.euler[name]):
                assert (k6[0], k6[1], k6[2]) == (-k[0], k[1], -k[2]), name


@test
def ported_content_unlock_keys() -> None:
    """PortedMissionsUnlocked / PortedWeaponsUnlocked (2026-10-17 user: 「给我4.1和5代的任务和装备全部解锁」): shipped 0 (a
    shared package keeps everyone's progress), read by the plugin with false defaults, and added at 0 to an old ini by
    merge_ini alone (no NEW_DEFAULTS migration). The weapon grant runs before the native DLC grant (its "equipped but not
    owned" pass) and only for EDF6VC_E5_* / EDF6VC_E41_* rows, the prefixes of every registry; the mission unlock is the
    mission select's one clear-check call, still a stock call to 0x8A4C00 in the supported EDF.dll."""
    import ported_weapons as pw
    shipped = src('EDF6VehicleCrew.ini')
    for key in ('PortedMissionsUnlocked', 'PortedWeaponsUnlocked'):
        assert re.search(rf'^{key}=0$', shipped, re.M), f'{key} must ship as 0'
        assert key not in installer.NEW_DEFAULTS[installer.SECTION] and key not in installer.NEW_DEFAULTS_1017[installer.SECTION]
        assert key not in installer.NEW_DEFAULTS_1024[installer.SECTION]
        text, added, _ = installer.merge_ini('[VehicleCrew]\r\nEnabled=1\r\nEDF41Content=7\r\n', shipped)
        assert key in added and f'\r\n{key}=0\r\n' in text, (key, added)
        assert installer.merge_ini(text, shipped)[1] == [], 'merge is not idempotent'
        mine = f'[VehicleCrew]\r\nEnabled=1\r\n{key}=1\r\n'
        text, added, _ = installer.merge_ini(mine, shipped)
        assert key not in added and f'\r\n{key}=1\r\n' in text and f'{key}=0' not in text, 'the player\'s 1 is kept'
    crew = src('src/crew.h')
    plugin = src('src/plugin.cpp')
    assert 'bool portedMissionsUnlocked=false;' in crew and 'bool portedWeaponsUnlocked=false;' in crew
    assert 'n.portedMissionsUnlocked=ReadBool(L"PortedMissionsUnlocked",n.portedMissionsUnlocked);' in plugin
    assert 'n.portedWeaponsUnlocked=ReadBool(L"PortedWeaponsUnlocked",n.portedWeaponsUnlocked);' in plugin
    assert 'InstallPortedMissions();' in plugin
    header = src('src/unlock_ported.h')
    prefixes = set(re.findall(r'L"(EDF6VC_E[0-9]+_)"', header))
    assert prefixes == {g.prefix for g in pw.GAMES}, (prefixes, [g.prefix for g in pw.GAMES])
    assert all(any(i.startswith(x) for x in prefixes) for i in pw.IDS) and len(pw.IDS) == 432, len(pw.IDS)
    assert not any(pw.retired_id(i).startswith(tuple(prefixes)) for i in pw.IDS), 'a placeholder would be granted'
    assert not any(c.startswith(tuple(prefixes)) for c in calls.IDS)
    air = src('src/airstrike.cpp')
    give = air.split('void Grant(unsigned char* gs) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'GrantCalls(gs,ported)' in give and 'Cfg().portedWeaponsUnlocked' in give
    for name in ('UnlockDlcHook', 'ScriptUnlockDlcHook'):   # both entries of 0xDC550: the grant before the native step
        hook = air.split(f'void __fastcall {name}(unsigned char* gs) {{', 1)[1].split('\n}\n', 1)[0]
        assert hook.index('Grant(gs);') < hook.index('(image+kUnlockDlc)(gs)'), f'{name}: grant before the native step'
        # the mission pack's save only at the script's entry (after SetMode and the load): the reset's has the old mode
        assert ('MigrateMissionPackSave(gs)' in hook) == (name == 'ScriptUnlockDlcHook'), name
    grant = air.split('Granted GrantCalls(unsigned char* gs,bool ported) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert '&=' not in grant and '~' not in grant, 'the grant never clears a bit'
    camp = src('src/edf5campaign.cpp')
    assert 'constexpr unsigned kListScan=0x8A21C3;' in camp and 'constexpr unsigned kCleared=0x8A4C00;' in camp
    assert 'ported::StoryPack(content,cfg.edf5CampaignContent,cfg.edf41Content)' in camp, 'the six story packs only'
    assert 'cfg.portedMissionsUnlocked' in camp
    if not _e5c_have_game():
        return
    import pefile
    import rootcpk
    pe = pefile.PE(os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll'), fast_load=True)
    img = pe.get_memory_mapped_image()
    at = 0x8A21C3
    assert img[at] == 0xE8 and at + 5 + int.from_bytes(img[at + 1:at + 5], 'little', signed=True) == 0x8A4C00


@test
def edf5_campaign_thumbnails() -> None:
    """Every EDF5 mission in the text has EDF5's own thumbnail in THUMBS (no game needed: the file is committed), each a
    440x220 DXT1 DDS like EDF6's; pick_thumbs gives a mission its own pack's one (EDF5's, or EDF4.1's for the 4.1 story:
    both games have an M007), the range its stock 'thumb', a mission THUMBS lacks the stock one on its map, else
    M046's."""
    import mdb
    import struct
    own = e5c.edf5_thumbs()
    text = json.load(open(e5c.TEXT, encoding='utf-8'))
    paths = [m['path'] for group in ('main', 'dlc1', 'dlc2') for m in text[group]]
    names = {e5c.thumb_name(x).upper() for x in paths}
    assert len(paths) == 139 and set(own) == names, sorted(set(own) ^ names)
    for name, f in own.items():
        d = f.data
        assert d[:4] == b'DDS ' and struct.unpack_from('<II', d, 12) == (220, 440) and d[84:88] == b'DXT1', name

    stock = {n.upper(): mdb.RabFile(n, 0, 0, n.encode()) for n in ('EDF6_M046.dds', 'EDF6_M045.dds', 'EDF6_M010.dds')}
    with open(e5c.EDF41_THUMBS, 'rb') as f:
        own41 = {x.name.upper(): x for x in mdb.rab_read(f.read()).files}
    assert own41['M007.DDS'].stored != own['M007.DDS'].stored   # the two games' M007: why own_thumbs keys by pack
    rows = [{'group': 'main', 'key': 'EDF5_OLD_SCRIPT/M001', 'path': 'M001', 'map': 'X.MAC'},
            {'group': 'dlc1', 'key': 'EDF5_OLD_SCRIPT/DLC/DM011', 'path': 'DLC/DM011', 'map': None},
            {'group': 'main', 'key': 'EDF5_OLD_SCRIPT/M999', 'path': 'M999', 'map': 'X.MAC'},   # not in THUMBS: its map's
            {'group': 'main', 'key': 'EDF5_OLD_SCRIPT/M998', 'path': 'M998', 'map': None},      # nothing: M046's
            {'group': 'range', 'key': 'EDF6/RANGE', 'map': None, 'thumb': 'EDF6/M045'},
            {'group': 'main', 'key': 'EDF5_OLD_SCRIPT/M007', 'path': 'M007', 'map': None},
            {'group': 'edf41', 'key': 'EDF41/M007', 'path': 'M007', 'map': None}]
    out = e5c.pick_thumbs(rows, e5c.own_thumbs(), stock, {'X.MAC': 'EDF6/M010'})
    assert [f.name for f in out] == [e5c.thumb_name(r['key']) for r in rows]
    assert [f.stored for f in out] == [own['M001.DDS'].stored, own['DLC_DM011.DDS'].stored, b'EDF6_M010.dds',
                                       b'EDF6_M046.dds', b'EDF6_M045.dds', own['M007.DDS'].stored,
                                       own41['M007.DDS'].stored]


@test
def edf5_campaign_shipped() -> None:
    """Both the frozen installer and source-tools archive must carry the campaign's required text."""
    rel = src('tools/build_release.py')
    assert '"edf5campaign", "missions.json")}{seps}edf5campaign' in rel
    assert "os.path.join(sys._MEIPASS, 'edf5campaign', 'missions.json')" in src('tools/make_edf5_campaign.py')
    assert os.path.isfile(os.path.join(ROOT, 'edf5campaign', 'missions.json'))
    assert '"edf5campaign", "thumbnails.rab")}{seps}edf5campaign' in rel
    assert e5c.THUMBS == os.path.join(os.path.dirname(e5c.TEXT), 'thumbnails.rab')
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
    release = src('tools/build_release.py')
    assert all(f"'{registry}'" in release for _game, registry in pw.REGISTRIES), \
        'tools/build_release.py does not bundle every ported_weapons.REGISTRIES file: a frozen installer would lack rows'
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
    """stack refuses, before building anything, a table its rows would take past the weapon records the game keeps
    (0x800 natively, the plugin's relocation src/weapon_records_logic.h kSlots: the game writes past them for every row
    beyond); a table exactly full is fine. The installer's bound is the plugin's."""
    import types
    import ported_weapons as pw
    with open(os.path.join(ROOT, 'src', 'weapon_records_logic.h'), encoding='utf-8') as f:
        slots = re.search(r'constexpr std::uint32_t kSlots=(0x[0-9A-Fa-f]+);', f.read())
    assert slots and int(slots.group(1), 16) == cw.SAVE_WEAPONS, (slots and slots.group(1), cw.SAVE_WEAPONS)
    assert cw.SAVE_WEAPONS > 0x800
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
        alone = (p.source == 'edf6' or p.weapon is not None) and all(os.path.isfile(pw.bundled(r)) for r in p.assets)
        assert no_ids[i] == (p.id if alone else pw.retired_id(p.id)), p.id
        assert not alone or no_game[pw.sgo_file(p)] == out[pw.sgo_file(p)], f'{p.id}: built without its game differs'
    # A weapon this machine cannot build (its game missing): the same rows, a placeholder for it.
    real_build = pw.build

    def edf6_only(game_root: str, stock):  # noqa: ANN001, ANN202
        built, why = real_build(game_root, stock)
        cut = {k for k in built if pw.BY_ID[k].source != 'edf6'}
        return {k: v for k, v in built.items() if k not in cut}, {**why, **{k: 'Unavailable: test' for k in cut}}

    def refused(game_root: str, stock):  # noqa: ANN001, ANN202
        built, why = edf6_only(game_root, stock)
        return built, {k: v.replace('Unavailable', 'Unsupported') for k, v in why.items()}

    with _stock_only_mods(), patched(pw, build=edf6_only):
        bare = cw.stack(games[0])
    without = cw.row_ids(bare[cw.TABLE])
    assert len(without) == len(with_edf5)
    for p in pw.PORTS:
        i = with_edf5.index(p.id)
        alone = p.source == 'edf6'   # edf6_only keeps exactly these (their assets are built, EDF5 is here)
        assert without[i] == (p.id if alone else pw.retired_id(p.id)), p.id
        assert (pw.sgo_file(p) in bare) == alone, p.id
        assert dsgo.parse(bare[cw.TABLE]).root.get('table').items[i].items[5] == pw.ACQUIRE, \
            f'{p.id}: a placeholder obtained otherwise than the weapon (its template a starting or DLC weapon)'
    pending = next(p for p in pw.PORTS if p.source == 'edf5')
    import mdb
    for p in pw.PORTS:
        for rel in p.assets:   # a converted model: EDF6's container, its models version 0x20
            assert out[rel][:4] == b'SSA\0' and all(f.data[4:8] == (0x20).to_bytes(4, 'little')
                                                    for f in mdb.rab_read(out[rel]).files if f.name.lower().endswith('.mdb')), rel
    text = dsgo.parse(bare['WEAPON/WEAPONTEXT.EN.SGO']).root.get('text_table').items[with_edf5.index(pending.id)]
    assert text.items[0] == pending.text['EN'][0] + pw.PENDING_NOTE['EN'][0].format(game=pw.BY_GAME[pending.game].name)
    orig_base, orig_mods = cw.base, cw._mods
    with tempfile.TemporaryDirectory(prefix='edf6vc-e5w-') as mods:
        for p in pw.PORTS:
            for rel in pw.files(p):
                modfiles.atomic_write(os.path.join(mods, *rel.split('/')), out[rel])
        installed = lambda game_root, rel: out[rel] if rel in cw.SHARED else orig_base(game_root, rel)   # noqa: E731
        ours = lambda game_root, *rel: os.path.join(mods, *[x for r in rel for x in r.split('/')])   # noqa: E731
        with patched(cw, base=installed, _mods=ours), patched(pw, build=edf6_only):
            kept = cw.stack(games[0])
            retired, _ = cw.retire(games[0], False)
        with patched(cw, base=installed, _mods=ours), patched(pw, build=refused):
            refused_out = cw.stack(games[0])
    assert cw.row_ids(kept[cw.TABLE]) == with_edf5, 'a built row an earlier install wrote was not kept'
    assert not any(rel in kept for p in pw.PORTS if p.source != 'edf6' for rel in pw.files(p)), \
        'a kept file was rewritten'
    # Refused by the conversion, not waiting for a game: the earlier install's SGO (an older conversion's) is not kept.
    after = cw.row_ids(refused_out[cw.TABLE])
    assert all(after[with_edf5.index(p.id)] == (p.id if p.source == 'edf6' else pw.retired_id(p.id)) for p in pw.PORTS), \
        'a weapon the conversion refuses kept the row an earlier install gave it'
    # Retired: a placeholder each, but the rows past the game's own records go (call_weapons.past_room: without the
    # plugin the game would write past its records for them); the table ends at them.
    have = cw.row_ids(retired[cw.TABLE])
    assert len(have) == min(len(with_edf5), cw.NATIVE_WEAPONS), (len(have), len(with_edf5))
    assert all(have[with_edf5.index(p.id)] == pw.retired_id(p.id) for p in pw.PORTS
               if with_edf5.index(p.id) < cw.NATIVE_WEAPONS)


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
        written = [cw.sgo_file(c) for c in calls.CALLS] + [rel for p in pw.PORTS for rel in [pw.sgo_file(p), *p.assets]]
        for rel in written:
            modfiles.atomic_write(_mods(game, rel), b'ours')
        # As an install leaves it: what we wrote, by sha (cw.ours: an asset under a stock name is ours only by that).
        modfiles.atomic_write(_mods(game, cw.MANIFEST), json.dumps(
            {'created': [], 'replaced': [], 'written': {rel: modfiles.sha256(b'ours') for rel in written},
             'rows': {}}).encode())
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
        assert not any(os.path.isfile(_mods(game, rel)) for p in pw.PORTS for rel in [pw.sgo_file(p), *p.assets])
        assert any(p.assets for p in pw.PORTS), 'no port has assets: the asset half of this check is empty'
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


# EDF.dll's weapon update draws a charge's energy from its owner (vtable slot 31, call 0x693E34) unless
# EnergyChargeRequire[1] (+0x234, read at 0x68CAAA) is below this (comiss 0x693E1B); the amount is element 0 (+0x22C,
# 0x68CA3D: a curve at the star, a plain number as it is). docs/stock-payload-re.md section 3.
WD_ENERGY_GATE = 0.0011


@test
def edf41_wing_diver_energy() -> None:
    """edf41port/weapons.json against EDF.dll's energy draw (2026-10-18 feedback: a 4.1 Wing Diver weapon's charge
    takes no flight energy): every Wing Diver weapon (100-107) holds EnergyChargeRequire as [need, gate], need its 4.1
    number; one whose 4.1 text names an energy cost (消費エネルギー X％) needs X and draws (gate at or above
    WD_ENERGY_GATE), one 4.1 gives the no-draw mark 0.001 (its own generator: 0％ / －－－ or no cost line) stays below
    it. Needs no game."""
    import re
    with open(os.path.join(ROOT, 'edf41port', 'weapons.json'), encoding='utf-8') as f:
        weapons = [w for w in json.load(f)['weapons'] if 100 <= w['category'] <= 107]
    assert weapons, 'no Wing Diver weapon in edf41port/weapons.json'
    drawing = 0
    for w in weapons:
        energy = dsgo.to_py(dsgo.load(w['weapon']).get('EnergyChargeRequire'))
        assert isinstance(energy, list) and len(energy) == 2 and energy[0] == energy[1], (w['id'], energy)
        need = energy[0]
        ja = w['text']['JA']
        cost = dict((s[0], s[1]) for s in ja[2]).get('消費エネルギー') if len(ja) > 2 else None
        number = re.match(r'[\d.]+', cost) if cost else None
        if number and float(number.group()) > 0:
            assert abs(need - float(number.group())) <= 1e-4 * max(1.0, need), (w['id'], cost, need)
            assert need >= WD_ENERGY_GATE, (w['id'], need)
            drawing += 1
        else:
            assert abs(need - 0.001) < 1e-6, f'{w["id"]}: no energy cost in its text ({cost!r}) but {need}'
    assert drawing >= 100, drawing


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
    import make_edf41_weapons as m41
    cues6 = m41.available_weapon_cues(edf6, edf41)
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



@test
def ported_weapon_locators_fit() -> None:
    """pylib/edf5port.py fit_locators: a locator whose node the weapon's EDF6 model lacks (4.1's root 'mdl', which
    EDF6 renamed after the model) moves to the model's one root, the block otherwise as the game lays it
    (mab_legacy.mab_set_strings: strings re-laid in UTF-16 order, the same bytes mab.mab_write gives for the moved
    block); nothing changes when every node is a bone; no muzzle, or a missing node and no single root: Unsupported.
    Without this a 4.1 Wing Diver weapon has no muzzle in EDF6 and its first shot divides by zero (EDF.dll 0x69AA20)."""
    import struct
    from dataclasses import replace
    import edf5port
    import mab
    import mab_legacy
    sgo_block = b'SGO\0' + struct.pack('<7I', 0x102, 0, 0x20, 0, 0x20, 0, 0x20)   # every game block's records have one
    loc = mab.Locator('muzzle', 'mdl', 2, (0.0, 0.1, 1.5, 1.0), (0.05, 0.05, 0.25, 1.0), (0.0, 0.0, 0.0, 1.0), 0, sgo_block)
    grip = replace(loc, name='grip', node='barrel', pos=(0.0, -0.1, 0.2, 1.0))
    aim = replace(loc, name='z_aim', pos=(0.0, 0.2, 1.6, 1.0))
    block = mab.mab_write(mab.Mab((0xF, 0x83, 0), [0, 1, 2], [loc, grip, aim]))

    def doc_of(b: bytes) -> dsgo.Document:
        root = dsgo.Node([])
        root.set('animation_model', dsgo.Node([dsgo.Node(['app:/Weapon/w.rab', 'w.mdb']), 'app:/Weapon/w.cas',
                                               dsgo.Blob(b, 2)]))
        return dsgo.Document(root, [])

    def block_of(d: dsgo.Document) -> bytes:
        return d.root.get('animation_model').items[2].data

    d = doc_of(block)
    edf5port.fit_locators(d, [('w_model', -1), ('barrel', 0), ('polymesh', 0)])
    moved = mab.Mab((0xF, 0x83, 0), [0, 1, 2], [replace(loc, node='w_model'), grip, replace(aim, node='w_model')])
    assert block_of(d) == mab.mab_write(moved), 'the moved block is not laid out as the game lays one'
    assert [n for _name, n, _at in vc.mab_muzzles(block_of(d))] == ['w_model', 'barrel', 'w_model']
    assert struct.unpack_from('<H', block_of(d), 0x12)[0] == len(block_of(d)) - struct.unpack_from('<I', block_of(d), 0x20)[0]
    d = doc_of(block)
    edf5port.fit_locators(d, [('mdl', -1), ('barrel', 0)])
    assert block_of(d) == block, 'a block whose nodes are all bones changed'
    d = doc_of(block)
    edf5port.fit_locators(d, [('w_model', -1), ('barrel', 0), ('slide', 1)], {'muzzle': 'slide', 'grip': 'w_model'})
    assert [n for _name, n, _at in vc.mab_muzzles(block_of(d))] == ['slide', 'barrel', 'w_model'], \
        'a preferred bone did not win over the root (or moved a locator whose node is a bone)'
    d = doc_of(block)
    edf5port.fit_locators(d, [('a', -1), ('b', -1), ('barrel', 0)], {'muzzle': 'a', 'z_aim': 'b'})
    assert [n for _name, n, _at in vc.mab_muzzles(block_of(d))] == ['a', 'barrel', 'b'], 'preferred bones, no root'
    for bones, why in (([('a', -1), ('b', -1), ('barrel', 0)], 'two roots'),
                       ([], 'no bones')):
        try:
            edf5port.fit_locators(doc_of(block), bones)
        except edf5port.Unsupported:
            pass
        else:
            raise AssertionError(f'{why}: a missing node was not refused')
    try:
        edf5port.fit_locators(doc_of(mab.mab_write(mab.Mab((0xF, 0x83, 0), [0, 1, 2], []))), [('w', -1)])
    except edf5port.Unsupported:
        pass
    else:
        raise AssertionError('a block without a muzzle was taken (EDF.dll divides by the muzzles found)')
    cut = bytearray(block)   # its records said to run past its end: followed unchecked, a struct.error ends the install
    struct.pack_into('<I', cut, 0x18, len(block) + 0x1000)
    try:
        edf5port.fit_locators(doc_of(bytes(cut)), [('w_model', -1)])
    except edf5port.Unsupported:
        pass
    else:
        raise AssertionError('a block mab_layout refuses was followed')
    try:
        mab_legacy.mab_set_strings(block, {0x24: 'x'})
    except ValueError:
        pass
    else:
        raise AssertionError('mab_set_strings wrote over a field that is not a string offset')



@test
def ported_weapons_held_as_edf6() -> None:
    """pylib/edf5port.py hold_as on hand-made weapons: with EDF6 weapons of its class on its model, the weapon takes the
    hand and holding animations most of them have, of custom_parameter only the animation (element 0; a Weapon_Sub's
    named form, the call it makes, untouched), and the MAB block of one whose locators include all of its own; without
    one of its class, only a holding animation no EDF6 weapon uses takes the template's; its numbers never change."""
    import edf5port
    import struct
    import mab

    def block(*names: str) -> bytes:
        sgo_block = b'SGO\0' + struct.pack('<7I', 0x102, 0, 0x20, 0, 0x20, 0, 0x20)
        return mab.mab_write(mab.Mab((0xF, 0x83, 0), [0, 1, 2], [
            mab.Locator(n, 'body', 2, (0.0, 0.1 * k, 1.0, 1.0), (0.05, 0.05, 0.25, 1.0), (0.0, 0.0, 0.0, 1.0), 0, sgo_block)
            for k, n in enumerate(names)]))

    def weapon(cls: str, hand: str, base: str, recoil: str, mab_block: bytes, damage: float = 10.0) -> dsgo.Node:
        r = dsgo.Node([])
        r.set('xgs_scene_object_class', cls)
        r.set('ModelConstraint', dsgo.Node([hand]))
        r.set('BaseAnimation', base)
        r.set('custom_parameter', dsgo.Node([recoil, 0.0, 0.0, 1.0]))
        r.set('AmmoDamage', damage)
        r.set('animation_model', dsgo.Node([dsgo.Node(['app:/Weapon/w.rab', 'w.mdb']), 'c', dsgo.Blob(mab_block, 2)]))
        return r

    ours_block, theirs_block = block('01'), block('01', 'IK')
    peers = [weapon('Weapon_BasicShoot', 'arms_l', 'assault', 'assault_recoil1', theirs_block, 99.0) for _ in range(2)]
    peers.append(weapon('Weapon_BasicShoot', 'arms_r', 'assault', 'assault_recoil1', theirs_block, 99.0))
    known = {f: {'assault', 'assault_recoil1'} for f in edf5port.HOLD_NAMES}
    doc = dsgo.Document(weapon('Weapon_BasicShoot', 'arms_r', 'assault2', 'assault_recoil3', ours_block), [])
    edf5port.hold_as(doc, peers, peers[0], known)
    r = doc.root
    assert r.get('ModelConstraint').items == ['arms_l'], 'not the hand most of EDF6\'s weapons on the model use'
    assert r.get('BaseAnimation') == 'assault' and r.get('custom_parameter').items == ['assault_recoil1', 0.0, 0.0, 1.0]
    assert r.get('AmmoDamage') == 10.0, 'a number of the weapon\'s own changed'
    assert r.get('animation_model').items[2].data == theirs_block, 'not the developers\' MAB block (a superset of ours)'
    # a block whose locators the developers' do not all carry stays ours
    doc = dsgo.Document(weapon('Weapon_BasicShoot', 'arms_r', 'assault', 'assault_recoil1', block('01', 'extra')), [])
    edf5port.hold_as(doc, peers, peers[0], known)
    assert doc.root.get('animation_model').items[2].data == block('01', 'extra')
    # no peer of its class: only the names no EDF6 weapon uses take the template's
    template = weapon('Weapon_Throw', 'arms_l', 'assault', 'assault_recoil1', theirs_block)
    doc = dsgo.Document(weapon('Weapon_Throw', 'arms_r', 'bag', 'assault_recoil1', ours_block), [])
    edf5port.hold_as(doc, peers, template, known)
    assert doc.root.get('BaseAnimation') == 'assault' and doc.root.get('ModelConstraint').items == ['arms_r']
    # a Weapon_Sub's named custom_parameter (the call it makes) is never taken over
    sub = weapon('Weapon_Sub', 'arms_r', 'assault', 'assault_recoil1', ours_block)
    named = dsgo.Node(['vehicle_call', 1.0], {0: 'motion', 1: 'speed'})
    sub.set('custom_parameter', named)
    peer = weapon('Weapon_Sub', 'arms_r', 'assault', 'assault_recoil1', theirs_block)
    peer.set('custom_parameter', dsgo.Node(['other_call', 2.0], {0: 'motion', 1: 'speed'}))
    doc = dsgo.Document(sub, [])
    edf5port.hold_as(doc, [peer], peer, known)
    assert doc.root.get('custom_parameter').items == ['vehicle_call', 1.0], 'a Weapon_Sub\'s call was taken over'

@test
def ported_weapon_locators_fit_real() -> None:
    """With the games, every ported weapon (EDF5's, EDF4.1's) as installed:
      - is held as EDF6's own weapons on its model (edf5port.hold_as; user 2026-10-10, a 4.1 weapon's hands looked
        wrong): its MAB block is a developer block on its model wherever one carries all its locators, its hand and
        holding animations the set most of EDF6's weapons of its class on its model have, and no holding animation
        name is one no EDF6 weapon uses;
      - hangs every locator of its MAB on a bone of its EDF6 model (EDF.dll finds no muzzle otherwise,
        ported_weapon_locators_fit).
    And fit_locators alone (hold_as off, as for a model no EDF6 weapon uses) moves a missing node where EDF6's own
    weapons on that model hang a locator of that name (the Hercules sniper's on 'body', not the root), the strings then
    the developers' where a developer block is laid out alike."""
    import collections
    import edf5port
    import mab_legacy
    import ported_weapons as pw
    import rootcpk
    edf6 = rootcpk.DEFAULT_GAME
    if not os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        print('skip  ported_weapon_locators_fit_real: needs EDF6 installed')
        return
    g6 = rootcpk.Game(edf6)
    stock = lambda rel: g6.read(*rel.split('/'))   # noqa: E731
    roots = {g.key: pw.game_root(g, edf6) for g in pw.GAMES}

    def strings(b: bytes) -> dict[int, str]:
        lay = mab_legacy.mab_layout(b)
        return {at: mab_legacy._text_at(b, to) for at, _base, to in lay.offsets if to >= lay.strings_at}

    def locs(b: bytes) -> set[str]:
        return {loc for loc, _node in mab_legacy.mab_locator_nodes(b)}

    weapons = tuple(n for d, n in g6.cpk.index if d.upper() == 'WEAPON' and n.upper().endswith('.SGO'))
    models = pw.Models(stock, weapons)
    hold, fit = edf5port.hold_as, edf5port.fit_locators

    def build(p: object, held: bool, fitted: bool) -> dsgo.Node:
        if not held:
            edf5port.hold_as = lambda *a: None   # noqa: E731
        if not fitted:
            edf5port.fit_locators = lambda *a: None   # noqa: E731
        try:
            return dsgo.parse(pw.build_sgo(p, stock, roots[p.game], models)).root
        finally:
            edf5port.hold_as, edf5port.fit_locators = hold, fit

    built = adopted = held_fields = moved = like = preferred = 0
    for p in pw.PORTS:
        try:
            r = build(p, True, True)
        except pw.Unavailable:
            continue
        built += 1
        if 'animation_model' not in r.names.values():
            continue
        doc = dsgo.Document(r, [])
        b = r.get('animation_model').items[2].data
        bones = {n for n, _parent in models.bones(doc)}
        assert {n for _l, n in mab_legacy.mab_locator_nodes(b)} <= bones, f'{p.id}: locator node(s) not in its model'
        for f in edf5port.HOLD_NAMES:
            if f in r.names.values() and edf5port._first_text(r.get(f)) is not None:
                assert edf5port._first_text(r.get(f)) in models.known[f], f'{p.id} {f}: an animation EDF6 has not'
        raw = build(p, False, False)
        raw_block = raw.get('animation_model').items[2].data
        peers = models.peers(doc)
        carriers = [q.get('animation_model').items[2].data for q in peers
                    if locs(raw_block) <= locs(q.get('animation_model').items[2].data)]
        if carriers:
            adopted += 1
            assert b in carriers, f'{p.id}: not the developers\' MAB block for its model'
        alike = [q for q in peers if q.get('xgs_scene_object_class') == r.get('xgs_scene_object_class')]
        if alike:
            held_fields += 1
            def held(q: dsgo.Node) -> tuple:
                return tuple(edf5port._key(q.get(f)) if f in q.names.values() else None for f in edf5port.HOLD)
            best = collections.Counter(held(q) for q in alike).most_common(1)[0][0]
            donor = next(q for q in alike if held(q) == best)
            for f in edf5port.HOLD:
                if f not in donor.names.values():
                    continue
                if f == 'custom_parameter':
                    if edf5port._first_text(r.get(f)) is not None and not r.get(f).names:
                        assert r.get(f).items[0] == donor.get(f).items[0], f'{p.id}: recoil not EDF6\'s'
                    continue
                assert edf5port._key(r.get(f)) == edf5port._key(donor.get(f)), \
                    f'{p.id} {f}: not as most of EDF6\'s weapons on its model hold it'
        # fit_locators alone, hold_as off
        unheld = build(p, False, True).get('animation_model').items[2].data
        if unheld == raw_block:
            continue
        moved += 1
        want = models.preferred(doc)
        for (loc, old), (_loc, new) in zip(mab_legacy.mab_locator_nodes(raw_block), mab_legacy.mab_locator_nodes(unheld)):
            if old not in bones and want.get(loc) in bones:
                preferred += 1
                assert new == want[loc], f'{p.id} {loc}: moved to {new}, EDF6\'s weapons on its model use {want[loc]}'
        same_len = [q.get('animation_model').items[2].data for q in peers
                    if len(q.get('animation_model').items[2].data) == len(unheld)]
        if same_len:
            like += 1
            assert strings(unheld) in [strings(d) for d in same_len], f'{p.id}: strings not the developers\' block\'s'
    if not built:
        print('skip  ported_weapon_locators_fit_real: no ported weapon could be built')
        return
    assert adopted > 150 and held_fields > 100 and moved and like and preferred, \
        (f'{adopted} developer blocks taken, {held_fields} held as EDF6\'s, {moved} fitted, {like} beside a developer '
         f'block, {preferred} placed as the developers do: this check is (nearly) empty')
    print(f'  {built} weapons built: {adopted} on the developers\' MAB block, {held_fields} held as EDF6\'s weapons '
          f'on their model; fit_locators alone: {moved} moved, {like} against the developers\' strings, {preferred} '
          'locators where the developers put them')


@test
def mab_strings_relaid_as_the_game() -> None:
    """pylib/mab_legacy.py mab_set_strings lays the string area as EDF6's exporter does (_string_area: Shift-JIS order,
    each string in its Shift-JIS bytes + 1 UTF-16 units): every weapon MAB of EDF6's Root.cpk, re-laid with nothing
    renamed, comes back byte for byte (so a renamed block is the game's own layout too)."""
    import mab_legacy
    import rootcpk
    edf6 = rootcpk.DEFAULT_GAME
    if not os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        print('skip  mab_strings_relaid_as_the_game: needs EDF6 installed')
        return
    g6 = rootcpk.Game(edf6)
    count = kanji = 0
    for folder, name in g6.cpk.index:
        if folder.upper() != 'WEAPON' or not name.upper().endswith('.SGO'):
            continue
        r = dsgo.parse(g6.read(folder, name)).root
        if 'animation_model' not in r.names.values():
            continue
        b = r.get('animation_model').items[2].data
        assert mab_legacy.mab_set_strings(b, {}) == b, f'{name}: its MAB laid out again differs'
        count += 1
        kanji += any(not loc.isascii() for loc, _node in mab_legacy.mab_locator_nodes(b))
    assert count > 1000 and kanji > 100, f'{count} blocks, {kanji} with a kanji name: this check is empty'




@test
def edf41_calls_convert() -> None:
    """pylib/edf41calls.py on hand-made calls: a vehicle request becomes its EDF6 template's call (class, transport,
    voices) with the EDF6 vehicle in its place, 4.1's settings with the weapons renamed to EDF6's (a weapon EDF6 split
    in two: a row each) when laid out as the template's, else the template's, and 4.1's use (AmmoCount, ReloadTime,
    EnergyChargeRequire as [v, v]); a bomber call takes 4.1's bomber run when laid out as the template's, else refused."""
    import edf41calls as ec
    import edf5port

    def call(acp: list, extra: dict | None = None) -> dsgo.Node:
        r = dsgo.Node([])
        r.set('xgs_scene_object_class', 'Weapon_Sub')
        r.set('Ammo_CustomParameter', edf5port.to_value(acp))
        r.set('AmmoCount', 1.0)
        r.set('ReloadTime', 600.0)
        for k, v in (extra or {}).items():
            r.set(k, v)
        return r

    donor_weapons = [[f'app:/weapon/v_504begaruta_slot{i}.sgo', [1.0, 2.0]] for i in range(8)]
    source_weapons = [[f'app:/weapon/v_406begaruta_slot{i}.sgo', [float(i), 3.0]] for i in range(6)]
    donor_setup = [[0.5, 0.7], [2, 3, 4, 5], [8] * 6, [7], donor_weapons, [9, 10]]
    source_setup = [[0.4, 0.6], [6, 5, 4, 3], [9] * 6, [8], source_weapons, [10, 11]]
    template = call([0.05, 180.0, 120.0, 1.0,
                     ['app:/Object/v508_transport.sgo', 'app:/object/v509_transportbox.sgo',
                      'app:/Object/v504_begaruta.sgo', donor_setup],
                     ['輸送部隊目標確認', '輸送部隊発射', '輸送部隊攻撃後']])
    mine = {'Ammo_CustomParameter': [0.05, 180, 120, 1,
                                    ['app:/Object/Transporter401.sgo', 'app:/object/Transporter401_ContainerM.sgo',
                                     'app:/Object/Vehicle406_Begaruta_red.sgo', source_setup],
                                    ['x', '輸送部隊発射ベガルタ', 'y']],
            'AmmoCount': 3, 'ReloadTime': 900, 'EnergyChargeRequire': -1.0}
    r = ec.vehicle_call(mine, template, ec.VEHICLES['vehicle406_begaruta_red.sgo']).root
    acp = r.get('Ammo_CustomParameter')
    drop = acp.items[4]
    assert drop.items[0] == 'app:/Object/v508_transport.sgo'
    assert drop.items[2] == 'app:/Object/vehicle406_begaruta_red.sgo', 'the real 406 model must be summoned'
    assert dsgo.to_py(drop.items[3]) == source_setup, 'the six original holders and tuning must survive an eight-slot donor'
    assert len(drop.items[3].items[4].items) == 6
    assert dsgo.to_py(acp.items[5]) == ['輸送部隊目標確認', '輸送部隊発射', '輸送部隊攻撃後'], 'not the template\'s voices'
    assert r.get('AmmoCount') == 3 and r.get('ReloadTime') == 900 and dsgo.to_py(r.get('EnergyChargeRequire')) == [-1.0, -1.0]
    assert template.get('Ammo_CustomParameter').items[4].items[2] == 'app:/Object/v504_begaruta.sgo', 'the template changed'
    odd = dict(mine, Ammo_CustomParameter=mine['Ammo_CustomParameter'][:4] + [mine['Ammo_CustomParameter'][4][:3] + [[0.4]]]
               + mine['Ammo_CustomParameter'][5:])
    try:
        ec.vehicle_call(odd, template, ec.VEHICLES['vehicle406_begaruta_red.sgo'])
    except edf5port.Unsupported as e:
        assert 'six-holder' in str(e)
    else:
        raise AssertionError('a malformed 406 setup must not silently fall back to the 504')
    # A generic donor route retains its template when the old settings do not
    # have the donor shape; the dedicated six-holder 406 route above is strict.
    r = ec.vehicle_call(odd, template, 'v504_begaruta_red.sgo').root
    assert dsgo.to_py(r.get('Ammo_CustomParameter').items[4].items[3]) == donor_setup
    bomber = call([30.0, 1.0, ['app:/object/Bomber501.sgo', 2.0, [1.0]], ['a']])
    r = ec.bomber_call({'Ammo_CustomParameter': [0.05, 120, 180, 2, ['app:/object/Bomber401.sgo', 3, [5.0]]]}, bomber).root
    assert dsgo.to_py(r.get('Ammo_CustomParameter').items[2]) == ['app:/object/Bomber401.sgo', 3, [5.0]]
    try:
        ec.bomber_call({'Ammo_CustomParameter': [0.05, 120, 180, 2, ['app:/object/Bomber401.sgo', 3]]}, bomber)
    except edf5port.Unsupported:
        pass
    else:
        raise AssertionError('a bomber run laid out otherwise was taken')


@test
def edf41_calls_real() -> None:
    """The registry's EDF4.1 calls (plan P5): every 4.1 vehicle request is a Weapon_Sub bringing an EDF6 vehicle with
    EDF6's transport, its weapons all EDF6's; every bomber call a Weapon_RadioContact running a Bomber401; the Whale
    gunship's calls in EDF6's gunship category; none left out for want of a template."""
    import collections
    import edf41calls as ec
    import ported_weapons as pw
    import rootcpk
    with open(os.path.join(ROOT, 'edf41port', 'weapons.json'), encoding='utf-8') as f:
        reg = json.load(f)
    # The calls ship in their own registry, released after edf5port/models.json (ported_weapons.REGISTRIES).
    stray = [w['id'] for w in reg['weapons'] if 'call' in w or w['category'] == 311]
    assert not stray, f'edf41port/weapons.json holds P5 calls (they belong in edf41port/calls.json): {stray[:5]}'
    with open(os.path.join(ROOT, 'edf41port', 'calls.json'), encoding='utf-8') as f:
        reg['weapons'] = json.load(f)['weapons']
    kinds = collections.Counter(w['call']['kind'] for w in reg['weapons'] if 'call' in w)
    assert kinds['vehicle'] == 65 and kinds['bomber'] == 11, kinds
    assert not [s for s in reg['skipped'] if '呼叫' in s['reason']], 'a call left out'
    gunships = [w for w in reg['weapons'] if w['category'] == 311]
    assert len(gunships) == 8 and all(w['class'] == 'Weapon_BasicShoot' for w in gunships)
    edf6 = rootcpk.DEFAULT_GAME
    have = None
    if os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        g6 = rootcpk.Game(edf6)
        have = {f'{d}/{n}'.upper() for d, n in g6.cpk.index}
    with open(os.path.join(ROOT, 'edf41port', 'objects.json'), encoding='utf-8') as f:
        ported41 = set(json.load(f)['files'])
    # 4.1 has no Barga: a call bringing EDF6's (v515_retrobalam, its "Retro Balam") is a Balam call that lost the ported
    # Balam (2026-10-12, test site #12: 「巴拉姆模型依然是巴尔加」).
    balam = [w['id'] for w in reg['weapons'] if w.get('call', {}).get('vehicle') == 'v515_retrobalam.sgo']
    assert not balam, f"4.1's Balam calls bring EDF6's Barga (v515_retrobalam), not the ported Balam: {balam}"
    assert sum(w.get('call', {}).get('vehicle') == 'vehicle501_fortressrobo.sgo' for w in reg['weapons']) == 4
    by_id = {p.id: p for p in pw.PORTS}
    for w in reg['weapons']:
        if 'call' not in w:
            continue
        r = dsgo.load(by_id[w['id']].weapon)
        acp = r.get('Ammo_CustomParameter')
        if w['call']['kind'] == 'vehicle':
            assert r.get('xgs_scene_object_class') == 'Weapon_Sub', w['id']
            drop = acp.items[4]
            assert ec.leaf(drop.items[2]) == w['call']['vehicle'], w['id']
            paths = [x for x in _dsgo_strings(drop) if x.lower().startswith('app:/')]
            assert not any('transporter401' in x.lower() for x in paths), f"{w['id']}: still 4.1's transport"
            if have is not None:
                # Or a 4.1 object the install ports into Mods under its own name (edf41port/objects: 4.1's Balam).
                gone = [x for x in paths if x.split(':/', 1)[1].upper() not in have and x.split(':/', 1)[1].upper() not in ported41]
                assert not gone, f"{w['id']}: EDF6 lacks {gone}"
        else:
            assert r.get('xgs_scene_object_class') == 'Weapon_RadioContact', w['id']
            assert ec.leaf(acp.items[2].items[0]) == 'bomber401.sgo', w['id']

@test
def ported_weapons_registry_current() -> None:
    """Every converted weapon the registries keep ('weapon': EDF5's and EDF4.1's, so the install needs neither game)
    is what pylib/edf5port.py makes of that game's file today, where the game is installed: a converter change without
    tools/make_edf5_weapons.py / make_edf41_weapons.py run again fails here. Every port not EDF6's own carries one."""
    import edf5port
    import gamedir
    import ported_weapons as pw
    import rootcpk
    import sgo
    lacking = [p.id for p in pw.PORTS if p.source != 'edf6' and p.weapon is None]
    assert not lacking, f'converted at install time, needing the game: {lacking[:5]}'
    checked = 0
    for g in pw.GAMES:
        root = gamedir.find_other(g.install, near=rootcpk.DEFAULT_GAME)
        if not root:
            print(f'skip  ported_weapons_registry_current ({g.name}): not installed')
            continue
        game = rootcpk.Game(root)
        calls = {}
        for key, registry in pw.REGISTRIES:
            if key != g.key:
                continue
            with open(os.path.join(ROOT, *registry.split('/')), encoding='utf-8') as f:
                calls.update({w['id']: w['call'] for w in json.load(f)['weapons'] if 'call' in w})
        templates = None
        for p in pw.PORTS:
            if p.game != g.key or p.source == 'edf6':
                continue
            members = sgo.read(game.read('WEAPON', p.sgo))[1]
            if p.id in calls:   # an EDF6 call made of a 4.1 vehicle request / bomber call (plan P5)
                import make_edf41_weapons as m41
                templates = templates or m41.CallTemplates(rootcpk.Game(rootcpk.DEFAULT_GAME))
                made = m41.convert(members, pw._names(p), calls[p.id], templates)
            else:
                made = g.convert(members, pw._names(p)).root
            assert dsgo.dump(made) == p.weapon, f'{p.id}: registry out of date'
            checked += 1
    print(f'  {checked} registry weapons equal to a conversion now')

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


# EDF5 files EDF6 ships unchanged in content, so EDF5's converted must be EDF6's byte for byte (pylib/cas_legacy.py,
# pylib/mdb_legacy.py; found 2026-10-10 among the 230 CAS / 3203 MDB pairs of the same name).
LEGACY_CAS_SAME = ('OBJECT/E511_MOTHERSHIP_GENOCIDE_L.CAS', 'OBJECT/E511_MOTHERSHIP_GENOCIDE_S.CAS',
                   'OBJECT/EDFDOOR01.CAS', 'OBJECT/V505_TANK.CAS')
LEGACY_MDB_SAME = (('WEAPON/BEGARUTACANNON.RAB', 'begarutaCannon.mdb'), ('WEAPON/BEGARUTAGATLING.RAB', 'begarutaGatling.mdb'),
                   ('WEAPON/BULLET_BEETLE01.RAB', 'bullet_beetle01.mdb'))


@test
def legacy_assets_match_edf6() -> None:
    """Real data, where EDF5 and EDF6 are installed: EDF5's animations and models that EDF6 ships unchanged convert to
    EDF6's files byte for byte (CAS 0x203 -> 0x204, MDB 0x14 -> 0x20); a whole EDF5 archive converts member by member
    (pylib/legacy_assets.py), its models 0x20 and its textures as they were; and with EDF4.1 there too, a 4.1 animation
    EDF5 kept converts to what EDF5's does."""
    import cas_legacy
    import gamedir
    import legacy_assets
    import mdb
    import mdb_legacy
    import rootcpk
    edf6 = rootcpk.DEFAULT_GAME
    edf5 = gamedir.find_other(gamedir.EDF5, near=edf6)
    if not edf5 or not os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        print('skip  legacy_assets_match_edf6: needs EDF6 and EDF5 installed')
        return
    g5, g6 = rootcpk.Game(edf5), rootcpk.Game(edf6)
    for rel in LEGACY_CAS_SAME:
        assert cas_legacy.cas_from_legacy(g5.read(*rel.split('/'))) == g6.read(*rel.split('/')), rel
    for rel, member in LEGACY_MDB_SAME:
        mine = next(f.data for f in mdb.rab_read(g5.read(*rel.split('/'))).files if f.name == member)
        theirs = next(f.data for f in mdb.rab_read(g6.read(*rel.split('/'))).files if f.name == member)
        assert mdb_legacy.mdb_from_legacy(mine) == theirs, rel
    source = g5.read('WEAPON', 'BEGARUTACANNON.RAB')
    out = mdb.rab_read(legacy_assets.convert('WEAPON/BEGARUTACANNON.RAB', source))
    old = mdb.rab_read(source)
    assert [f.name for f in out.files] == [f.name for f in old.files] and out.folders == old.folders
    for a, b in zip(out.files, old.files):
        if a.name.lower().endswith('.mdb'):
            assert a.data[4:8] == (0x20).to_bytes(4, 'little')
        else:
            assert a.stored == b.stored, a.name
    try:
        legacy_assets.convert('WEAPON/X.SHKT', b'')
    except ValueError:
        pass
    else:
        raise AssertionError('a kind with no converter was passed through')
    edf41 = gamedir.find_other(gamedir.EDF41, near=edf6)
    if edf41:
        g4 = rootcpk.Game(edf41)
        rel = 'MENUOBJECT/ARMYSOLDIER.CAS'
        assert cas_legacy.cas_from_legacy(g4.read(*rel.split('/'))) == cas_legacy.cas_from_legacy(g5.read(*rel.split('/')))
        # 4.1's archives whose members are not grouped by folder convert, every member where it was
        mixed = 0
        for d, n in g4.cpk.index:
            if not n.upper().endswith(('.RAB', '.MRAB')) or d.upper() == 'MAP':
                continue
            source = g4.read(d, n)
            old = mdb.rab_read(source)
            if mdb.folder_order_ok(old):
                continue
            new = mdb.rab_read(legacy_assets.convert(f'{d}/{n}', source))
            assert [(f.name, f.folder) for f in new.files] == [(f.name, f.folder) for f in old.files], n
            assert all(f.data[4:8] == (0x20).to_bytes(4, 'little') for f in new.files if f.name.lower().endswith('.mdb')), n
            mixed += 1
        assert mixed >= 3, f'{mixed} 4.1 archives out of folder order: this check is empty'


@test
def legacy_archive_refusals() -> None:
    """pylib/legacy_assets.py on hand-made archives: textures only -> the same archive back; a member of a kind it has
    no converter for, a cut archive, a SHKT file -> ValueError (nothing half-converted reaches the game)."""
    import legacy_assets
    import mdb
    tex = mdb.RabFile('a.lod.dds', 0, 0, b'DDS ' + bytes(124))
    hd = mdb.RabFile('a.dds', 2, 1, b'DDS ' + bytes(252))
    archive = mdb.rab_write(mdb.Rab(0x110, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [tex, hd]))
    assert legacy_assets.convert('OBJECT/X.RAB', archive) == archive
    mixed = mdb.rab_write(mdb.Rab(0x110, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [hd, tex]))   # folders 2 then 0, as
    assert legacy_assets.convert('OBJECT/X.RAB', mixed) == mixed, 'an archive out of folder order'   # 4.1's DEIROI401
    odd = mdb.rab_write(mdb.Rab(0x110, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [tex, mdb.RabFile('a.txt', 1, 0, b'x')]))
    for rel, data in (('OBJECT/X.RAB', odd), ('OBJECT/X.RAB', archive[:len(archive) - 40]), ('WEAPON/X.SHKT', b'')):
        try:
            legacy_assets.convert(rel, data)
        except ValueError:
            continue
        raise AssertionError(f'{rel} ({len(data)} bytes) was not refused')


@test
def legacy_parsers_refuse_malformed_input() -> None:
    """Cut or corrupt EDF5 / EDF4.1 files are refused with ValueError (what ported_weapons.build_assets and
    make_edf5_weapons.convertible catch: the weapon waits as a placeholder) by the parsers themselves: a UTF-16 string
    with no end looped forever, a cut CMPL stream came back short or raised IndexError, a 4-byte MDB raised struct.error,
    a vertex size of 0 ZeroDivisionError, a CANM offset past the file allocated it and raised IndexError, a channel
    value no float32 holds OverflowError."""
    import struct
    import cas_legacy
    import mdb
    import mdb_legacy
    packed = mdb.cmpl_compress(bytes(range(256)) * 8)
    cases = {
        'string past the end': lambda: mdb._wstr(b'a\0b\0', 0),
        'string offset past the end': lambda: mdb._wstr(b'a\0\0\0', 8),
        'CMPL header cut': lambda: mdb.cmpl_decompress(b'CMPL\0\0'),
        'CMPL stream cut': lambda: mdb.cmpl_decompress(packed[:len(packed) // 2]),
        'CMPL stream cut in a token': lambda: mdb.cmpl_decompress(packed[:-1]),
        'MDB of 4 bytes': lambda: mdb_legacy.mdb_from_legacy(b'MDB0'),
        'vertex size 0': lambda: mdb_legacy._check_mesh(mdb.Mesh(bytes(4), 0, 0, 0, [], 0, b'', b'')),
        'CANM past the file': lambda: cas_legacy.cas_layout(b'CAS\0' + struct.pack('<II', cas_legacy.CAS_5, 0xFFFFFFF0)
                                                            + bytes(0x24)),
        'float32 overflow': lambda: cas_legacy._f32(1e39),
    }
    assert mdb.cmpl_decompress(packed) == bytes(range(256)) * 8
    for name, case in cases.items():
        try:
            case()
        except ValueError:
            continue
        raise AssertionError(f'{name}: not refused')


@test
def mdb_legacy_half2_widened() -> None:
    """EDF4.1 map parts' half2 vertex elements (format 24: NW_HILLYCITY_LIGHT's station models; no EDF6 model has one)
    become float2 (12) with the same values: the vertex grows by 4 per such element, the elements keep their order
    (a half2 before another element moves that one), every other byte is kept, and a mesh without one is untouched."""
    import struct
    import mdb
    import mdb_legacy
    uv = [(0.75, 0.76806640625), (33.25, -1.1064453125)]   # exact halves (so exact in float)
    pos = [(1.0, 2.0, 3.0, 1.0), (-6.6015625, 6.0, -35.0, 1.0)]
    raw = b''.join(struct.pack('<2e', *u) + struct.pack('<4e', *p) for u, p in zip(uv, pos))
    me = mdb.Mesh(bytes(4), 0, 0, 12, [mdb.VElem(24, 0, 0, 'texcoord'), mdb.VElem(7, 4, 0, 'position')], 0, raw, b'')
    mdb_legacy._check_mesh(me)
    mdb_legacy._widen_half2(me)
    assert me.vsize == 16 and [(e.name, e.fmt, e.offset) for e in me.elems] == [('texcoord', 12, 0), ('position', 7, 8)]
    assert me.nverts == 2
    for i in range(2):
        assert struct.unpack_from('<2f', me.vdata, i * 16) == uv[i]
        assert struct.unpack_from('<4e', me.vdata, i * 16 + 8) == pos[i]
    mdb_legacy._check_mesh(me)
    plain = mdb.Mesh(bytes(4), 0, 0, 8, [mdb.VElem(7, 0, 0, 'position')], 0, struct.pack('<4e', *pos[0]), b'')
    mdb_legacy._widen_half2(plain)
    assert plain.vsize == 8 and plain.vdata == struct.pack('<4e', *pos[0])


@test
def ported_assets_of_another_mod_stay() -> None:
    """tools/call_weapons.py on a stand-in game: a model another mod put under the name an EDF5 weapon's asset uses
    (Mods/OBJECT/V505_TANKEDF4.MRAB) survives an install that cannot build that weapon (no EDF5), the uninstall and a
    repair (ours(): a file under a stock name is ours only by the sha we wrote); one we wrote is removed."""
    import ported_weapons as pw
    port = next(p for p in pw.PORTS if p.assets)
    asset = next(iter(port.assets))
    ids = STOCK + sorted({*cw.templates(), *(p.template for p in pw.PORTS)} - set(STOCK)) + list(calls.IDS)
    for ours_written in (False, True):
        with tempfile.TemporaryDirectory(prefix='edf6vc-asset-') as game, \
                patched(modfiles, game_running=lambda process=modfiles.PROCESS: False), \
                patched(cw, stock=lambda game_root, rel: b''):   # no Root.cpk: our tables are never the stock ones
            if not ours_written:
                modfiles.atomic_write(_mods(game, asset), b'another mod')
            table = ids + [p.id if (ours_written and p is port) else pw.retired_id(p.id) for p in pw.PORTS]
            files = _call_files(game, table)
            rows = lambda key: dsgo.compact(dsgo.Document(dsgo.Node([dsgo.Node([   # noqa: E731
                dsgo.Node([i, f'app:/weapon/{i}.sgo', 0.0, 1.0, 0.0, 0.0, dsgo.Node([]), 1.0, 0.0]   # the 9 columns
                          if key == 'table' else [f'name {i}', f'about {i}']) for i in table])],
                {0: key}), []))
            files.update({cw.TABLE: rows('table'), **{rel: rows('text_table') for rel in cw.TEXTS}})
            if ours_written:
                files.update({pw.sgo_file(port): b'ours', asset: b'our model'})
            cw.install(game, files)
            if ours_written:
                assert ledger.Ledger(game).owners(asset) == [cw.OWNER], 'the transaction did not register its model'
            assert modfiles.read(_mods(game, asset)) == (b'our model' if ours_written else b'another mod')
            cw.uninstall(game)
            assert (modfiles.read(_mods(game, asset)) == b'another mod') if not ours_written else \
                not os.path.isfile(_mods(game, asset)), ours_written
            if not ours_written:
                cw.repair(game)
                assert modfiles.read(_mods(game, asset)) == b'another mod'


def _edf41_shared_port_fixture():
    """Synthetic sharing independent of whether current EDF4.1 objects borrow EDF5 art."""
    from dataclasses import replace
    import ported_weapons as pw
    asset = 'OBJECT/EDF41_SHARED_LEDGER_FIXTURE.MRAB'
    port = replace(pw.PORTS[0], assets={asset: 'OBJECT/SYNTHETIC_SOURCE.RAB'})
    ports = tuple(port if p.id == port.id else p for p in pw.PORTS)
    return asset, port, ports


@test
def edf41_shared_weapon_asset_lifecycle() -> None:
    """An explicit synthetic shared asset, through install -> legacy upgrade -> campaign plan -> either uninstall order.
    A weapon-manifest hash is required even for byte-identical unowned files; edited files still block the object."""
    import edf41_objects as eo
    import itertools
    import ported_weapons as pw
    asset, port, ports = _edf41_shared_port_fixture()
    obj, data = 'OBJECT/VEHICLE301_TANK_AI.SGO', b'shared model'
    payloads = {obj: b'converted tank', asset: data}
    reg = {'objects': [{'object': obj, 'files': list(payloads)}],
           'files': {r: modfiles.sha256(d) for r, d in payloads.items()}, 'shared': {asset: 'unused'}}
    ids = STOCK + sorted({*cw.templates(), *(p.template for p in pw.PORTS)} - set(STOCK)) + list(calls.IDS)
    table = ids + [p.id if p.id == port.id else pw.retired_id(p.id) for p in pw.PORTS]
    for legacy in (False, True):
        for removal, original in itertools.product(('calls', 'repair', 'edf41'), (None, b'original mod model')):
            with tempfile.TemporaryDirectory(prefix='edf6vc-shared-') as game, \
                    patched(modfiles, game_running=lambda process=modfiles.PROCESS: False), \
                    patched(cw, stock=lambda game_root, rel: b''), \
                    patched(pw, PORTS=ports, BY_ID={p.id: p for p in ports}), \
                    patched(eo, registry=lambda: reg), patched(eo, read=payloads.__getitem__):
                if original is not None:
                    modfiles.atomic_write(_mods(game, asset), original)
                files = _call_files(game, table)
                def rows(key: str) -> bytes:
                    return dsgo.compact(dsgo.Document(dsgo.Node([dsgo.Node([dsgo.Node(
                        [i, f'app:/weapon/{i}.sgo', 0.0, 1.0, 0.0, 0.0, dsgo.Node([]), 1.0, 0.0]
                        if key == 'table' else [f'name {i}', f'about {i}']) for i in table])], {0: key}), []))
                files.update({cw.TABLE: rows('table'), **{r: rows('text_table') for r in cw.TEXTS},
                              pw.sgo_file(port): b'weapon', asset: data})
                cw.install(game, files)
                if legacy:
                    led = ledger.Ledger(game)
                    del led.files[ledger.key(asset)]   # old releases wrote only the weapon manifest
                    led._save()
                assert not eo.unavailable(game), 'our unmodified weapon model blocks the campaign preflight'
                # The same unowned bytes without the committed manifest cannot be adopted.
                with patched(cw, load_manifest=lambda root: {'written': {}}):
                    if legacy:
                        assert eo.unavailable(game)
                modfiles.atomic_write(_mods(game, asset), b'another mod changed it')
                assert eo.unavailable(game), 'a foreign edit was treated as the bundled model'
                modfiles.atomic_write(_mods(game, asset), data)
                built, skipped = eo.build(game)
                assert not skipped and set(built) == set(payloads)
                eo.install(game, built)
                assert set(ledger.Ledger(game).owners(asset)) == {cw.OWNER, eo.OWNER}
                cw.install(game, files)   # a repeat upgrade keeps both the writer and the consumer
                assert set(ledger.Ledger(game).owners(asset)) == {cw.OWNER, eo.OWNER}
                if removal == 'edf41':
                    eo.remove(game)
                    assert modfiles.read(_mods(game, asset)) == data
                    cw.uninstall(game)
                else:
                    (cw.repair if removal == 'repair' else cw.uninstall)(game)
                    assert modfiles.read(_mods(game, asset)) == data
                    assert ledger.Ledger(game).owners(asset) == [eo.OWNER]
                    eo.remove(game)
                if original is None:
                    assert not os.path.exists(_mods(game, asset)), 'the last consumer left an unneeded model'
                else:
                    assert modfiles.read(_mods(game, asset)) == original, 'the pre-install model was not restored'


@contextlib.contextmanager
def _edf41_shared_game(original: bytes | None) -> Iterator[tuple[str, str, bytes, Callable[[], dict[str, bytes]]]]:
    """A game dir with the synthetic shared EDF4.1 model and a weapon table for its port: (game, asset, our bytes,
    the call weapons' files). `original`: a pre-install file at the asset's path (another mod's)."""
    import edf41_objects as eo
    import ported_weapons as pw
    asset, port, ports = _edf41_shared_port_fixture()
    obj, data = 'OBJECT/VEHICLE301_TANK_AI.SGO', b'shared model'
    payloads = {obj: b'converted tank', asset: data}
    reg = {'objects': [{'object': obj, 'files': list(payloads)}],
           'files': {r: modfiles.sha256(d) for r, d in payloads.items()}, 'shared': {asset: 'unused'}}
    ids = STOCK + sorted({*cw.templates(), *(p.template for p in pw.PORTS)} - set(STOCK)) + list(calls.IDS)
    table = ids + [p.id if p.id == port.id else pw.retired_id(p.id) for p in pw.PORTS]
    with tempfile.TemporaryDirectory(prefix='edf6vc-shared-') as game, \
            patched(modfiles, game_running=lambda process=modfiles.PROCESS: False), \
            patched(cw, stock=lambda game_root, rel: b''), \
            patched(pw, PORTS=ports, BY_ID={p.id: p for p in ports}), \
            patched(eo, registry=lambda: reg), patched(eo, read=payloads.__getitem__):
        if original is not None:
            modfiles.atomic_write(_mods(game, asset), original)

        def call_files() -> dict[str, bytes]:
            files = _call_files(game, table)

            def rows(key: str) -> bytes:
                return dsgo.compact(dsgo.Document(dsgo.Node([dsgo.Node([dsgo.Node(
                    [i, f'app:/weapon/{i}.sgo', 0.0, 1.0, 0.0, 0.0, dsgo.Node([]), 1.0, 0.0]
                    if key == 'table' else [f'name {i}', f'about {i}']) for i in table])], {0: key}), []))
            files.update({cw.TABLE: rows('table'), **{r: rows('text_table') for r in cw.TEXTS},
                          pw.sgo_file(port): b'weapon', asset: data})
            return files
        yield game, asset, data, call_files


def _edf41_install(game: str) -> None:
    import edf41_objects as eo
    built, skipped = eo.build(game)
    assert not skipped, skipped
    eo.install(game, built)


def _edf41_shared_left_clean(game: str, asset: str, original: bytes | None) -> None:
    """After every component is gone: the original back (or no file), nothing of ours left about the model, and a
    next install's preflight takes the EDF4.1 object."""
    import edf41_objects as eo
    led = ledger.Ledger(game)
    assert ledger.key(asset) not in led.files, f'the ledger still lists {asset}: {led.files.get(ledger.key(asset))}'
    assert not os.path.exists(_mods(game, ledger.BACKUP)), 'the ledger left kept originals or empty directories'
    if original is None:
        assert not os.path.exists(_mods(game, asset)), 'an unowned model was left in Mods'
        assert not eo.unavailable(game), eo.unavailable(game)
    else:
        assert modfiles.read(_mods(game, asset)) == original, 'the pre-install model was not restored'


@test
def edf41_shared_asset_reinstall_after_repair() -> None:
    """Repair while the EDF4.1 objects outlive the call weapons, reinstall the weapons, then remove both in either
    order (the installer's option 1 removes the weapons first): the weapons' second first-backup must not take the
    model our tools wrote for a pre-install original, or the last release "restores" our bytes and leaves an unowned
    file that blocks the EDF4.1 tank the next time. Both write orders: the weapons first, or the EDF4.1 objects."""
    import edf41_objects as eo
    import itertools
    for edf41_first, original, weapons_go_first in itertools.product((False, True), (None, b'original mod model'),
                                                                      (True, False)):
        if edf41_first and original is not None:
            continue   # the EDF4.1 install leaves out an object whose file another mod put there
        with _edf41_shared_game(original) as (game, asset, data, call_files):
            files = call_files()
            if edf41_first:
                _edf41_install(game)
                cw.install(game, files)
                assert asset not in cw.load_manifest(game)['replaced'], \
                    "the EDF4.1 objects' model was backed up as a pre-install original"
            else:
                cw.install(game, files)
                _edf41_install(game)
            cw.repair(game)
            assert modfiles.read(_mods(game, asset)) == data
            assert ledger.Ledger(game).owners(asset) == [eo.OWNER]
            cw.install(game, files)
            assert asset not in cw.load_manifest(game)['replaced'], 'our own model was backed up as an original'
            assert set(ledger.Ledger(game).owners(asset)) == {cw.OWNER, eo.OWNER}
            assert not eo.unavailable(game)
            if weapons_go_first:
                cw.uninstall(game)
                assert modfiles.read(_mods(game, asset)) == data, 'removed a model the EDF4.1 objects still use'
                eo.remove(game)
            else:
                eo.remove(game)
                assert modfiles.read(_mods(game, asset)) == data, 'removed a model the call weapons still use'
                cw.uninstall(game)
            _edf41_shared_left_clean(game, asset, original)


@test
def edf41_shared_asset_backed_up_by_older_install() -> None:
    """A weapon manifest from an older install that backed up the EDF4.1 objects' model as its "original" (the
    EDF4.1 objects wrote it first): removal in either order must not put those same bytes back as another mod's."""
    import edf41_objects as eo
    for weapons_go_first in (True, False):
        with _edf41_shared_game(None) as (game, asset, data, call_files):
            files = call_files()
            _edf41_install(game)
            cw.install(game, files)
            manifest = cw.load_manifest(game)
            manifest['created'].remove(asset)
            manifest['replaced'].append(asset)
            modfiles.atomic_write(_mods(game, f'{cw.BACKUP}/{asset}'), data)
            modfiles.atomic_write(_mods(game, cw.MANIFEST), cw._manifest_bytes(manifest))
            if weapons_go_first:
                cw.uninstall(game)
                assert not ledger.Ledger(game).restores(asset), 'our own bytes were kept as the original'
                eo.remove(game)
            else:
                eo.remove(game)
                cw.uninstall(game)
            _edf41_shared_left_clean(game, asset, None)


@test
def ledger_deferred_restore_edges() -> None:
    """The ledger's kept original: put back by the last release and cleaned up with its directories; a file changed
    since keeps the change and drops the copy; a file gone drops it too; a missing or changed copy neither raises
    half way (files released before it stay released, the ledger is saved) nor removes the installed file; bytes
    equal to the registered ones are not kept as an original at all."""
    rel, other, ours, original = 'OBJECT/SUB/MODEL.MRAB', 'OBJECT/OTHER.MRAB', b'our model', b'original'

    def backup(game: str) -> str:
        return _mods(game, f'{ledger.BACKUP}/{rel}')

    for case in ('restored', 'changed', 'gone', 'backup missing', 'backup changed', 'same bytes'):
        with tempfile.TemporaryDirectory(prefix='edf6vc-restore-') as game:
            led = ledger.Ledger(game)
            if case == 'same bytes':
                led.put('calls', rel, ours)
                led.defer_restore(rel, ours)
                assert not led.restores(rel)
                deleted, kept = led.release('calls')
                assert deleted == [led.disk(rel)] and not kept
                assert not os.path.exists(_mods(game, ledger.BACKUP))
                continue
            led.put('calls', other, b'other')
            led.put('calls', rel, ours)
            led.defer_restore(rel, original)
            assert led.restores(rel) and modfiles.read(backup(game)) == original
            if case == 'changed':
                modfiles.atomic_write(led.disk(rel), b'edited by another mod')
            elif case == 'gone':
                os.remove(led.disk(rel))
            elif case == 'backup missing':
                os.remove(backup(game))
            elif case == 'backup changed':
                modfiles.atomic_write(backup(game), b'not what was kept')
            deleted, kept = led.release('calls', [other, rel])
            assert deleted == [led.disk(other)] and not os.path.exists(led.disk(other))
            assert not ledger.Ledger(game).files, f'{case}: the release was not saved'
            if case == 'restored':
                assert kept == [led.disk(rel)] and modfiles.read(led.disk(rel)) == original
            elif case == 'changed':
                assert kept == [led.disk(rel)] and modfiles.read(led.disk(rel)) == b'edited by another mod'
            elif case == 'gone':
                assert not kept and not os.path.exists(led.disk(rel))
            else:
                assert kept == [led.disk(rel)] and modfiles.read(led.disk(rel)) == ours
            if case == 'backup changed':
                assert modfiles.read(backup(game)) == b'not what was kept', 'an unverified copy was thrown away'
            else:
                assert not os.path.exists(_mods(game, ledger.BACKUP)), f'{case}: the kept copy or its directories stay'


@test
def ported_assets_bundled() -> None:
    """The converted models the EDF5 weapons need ship with the tools (tools/ported_weapons.py bundled): every asset of
    a port has its file, and every file under edf5port/assets is some port's asset (a weapon left out leaves none).
    Where EDF5 is installed, each is what converting EDF5's file gives, byte for byte."""
    import gamedir
    import legacy_assets
    import ported_weapons as pw
    import rootcpk
    wanted = {rel for p in pw.PORTS for rel in p.assets}
    assert wanted, 'no port has assets: this check is empty'
    assert all(os.path.isfile(pw.bundled(rel)) for rel in wanted), 'an asset of a port is not shipped'
    root = os.path.join(ROOT, 'edf5port', 'assets')
    shipped = {os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/') for d, _, fs in os.walk(root) for f in fs}
    assert shipped == wanted, f'shipped files no port needs: {sorted(shipped - wanted)}'
    edf5 = gamedir.find_other(gamedir.EDF5, near=rootcpk.DEFAULT_GAME)
    if not edf5:
        print('skip  ported_assets_bundled (against EDF5): EDF5 not installed')
        return
    g5 = rootcpk.Game(edf5)
    for p in pw.PORTS:
        for rel, source in p.assets.items():
            with open(pw.bundled(rel), 'rb') as f:
                assert f.read() == legacy_assets.convert(source, g5.read(*source.split('/'))), rel



@test
def cas_graft_players() -> None:
    """pylib/cas_graft.py: grafting no group gives every EDF6 CAS back byte for byte (the re-emission and the CANM
    edit change nothing of the target's); EDF4.1's carriers (CAS converted) get EDF6's carrier players in_ring /
    out_ring, which UfoCarrier508 resolves unchecked (a crash on spawn without them): the groups and their tcontrols
    after the target's own, renumbered to them, each with a trackless clip of its own after the target's clips, every
    clip of the target's as it was."""
    import cas_graft
    import cas_legacy
    import cas_pose
    import gamedir
    import rootcpk
    import struct
    edf6 = rootcpk.DEFAULT_GAME
    if not os.path.isfile(os.path.join(edf6, 'Root.cpk')):
        print('skip  cas_graft_players: needs EDF6 installed')
        return
    g6 = rootcpk.Game(edf6)
    donor = g6.read('OBJECT', 'E508_CARRIER.CAS')
    count = 0
    for folder, name in g6.cpk.index:
        if name.upper().endswith('.CAS') and folder.upper() == 'OBJECT':
            b = g6.read(folder, name)
            assert cas_graft.graft(b, donor, []) == b, f'{name}: grafting nothing changed it'
            count += 1
    assert count > 100, count
    edf41 = gamedir.find_other(gamedir.EDF41, near=edf6)
    if not edf41:
        print('skip  cas_graft_players (4.1 carriers): EDF4.1 not installed')
        return
    g4 = rootcpk.Game(edf41)

    def name_at(b: bytes, at: int) -> str:
        return cas_legacy._text(b, at + struct.unpack_from('<i', b, at)[0])

    for n in ('UFOCARRIER301.CAS', 'UFOCARRIER401.CAS'):
        target = cas_legacy.cas_from_legacy(g4.read('OBJECT', n))
        out = cas_graft.graft(target, donor, ['in_ring', 'out_ring'])
        lay = cas_legacy.cas_layout(out)
        groups = {name_at(out, at): ms for at, ms in lay.tree.anmgroups}
        assert set(groups) == {'main', 'in_ring', 'out_ring'}, sorted(groups)
        tcs = [name_at(out, at) for at, _w in lay.tree.tcontrols]
        assert tcs[-2:] == ['t_in_rotation_add', 't_out_rotation_add'], tcs
        before, after = cas_pose.CasPose(target), cas_pose.CasPose(out)
        assert [(c.name, [t.name for t in c.tracks]) for c in after.clips[:len(before.clips)]] == \
               [(c.name, [t.name for t in c.tracks]) for c in before.clips], f'{n}: a clip of its own changed'
        new = after.clips[len(before.clips):]
        assert [c.name for c in new] == ['in_rotation_add', 'out_rotation_add'] and not any(c.tracks for c in new)
        for k, g in enumerate(('in_ring', 'out_ring')):
            m = groups[g][0]
            index = len(before.clips) + k
            assert struct.unpack_from('<I', out, m.at + 0x20)[0] == tcs.index(f't_{name_at(out, m.at)}'), g
            words = lay.tree.tcontrols[tcs.index(f't_{name_at(out, m.at)}')][1]
            assert [struct.unpack_from('<I', out, w)[0] for w in words] == [index], g

@test
def edf41_objects_shipped() -> None:
    """edf41port: every object's files are shipped (edf41port/objects, the registry's SHA-256), every shipped file is
    some object's (a dropped object leaves none behind), each SGO's MAB blocks are EDF6's (0x83, pylib/legacy_sgo.py)
    and each model archive's MDBs EDF6's (0x20, pylib/legacy_assets.py). A member read from the player's EDF6 (an
    entry's members_from: the King's Monster501 textures) is not shipped, its manifest row names that very source,
    and the archive is put together (from the real game; skipped without one, CI) only from there."""
    import hashlib
    import edf41_objects as eo
    import legacy_sgo
    import mdb
    import rootcpk
    import sgo
    reg = eo.registry()
    assert reg['objects'], 'no object registered: this check is empty'
    listed = {rel for o in eo.entries(reg) for rel in o['files']}
    assert listed == set(reg['files']), 'the registry lists files no object needs, or the reverse'
    root = os.path.join(ROOT, 'edf41port', 'objects')
    shipped = {os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/') for d, _, fs in os.walk(root) for f in fs}
    import rabpool
    own = {s for rel in listed - set(reg.get('shared', {})) for s in rabpool.stored(rel)}
    from_game = {rel: srcs for o in eo.entries(reg) for rel, srcs in eo.stock_members(o).items()}
    for s in sorted(own):   # every member an archive's manifest names but those read from EDF6, and no member none does
        if s.endswith(rabpool.MANIFEST):
            with open(os.path.join(root, *s.split('/')), encoding='utf-8') as f:
                rows = json.load(f)['files']
            own |= {f'{rabpool.MEMBERS}/{row[5]}' for row in rows if len(row) == 6}
            rel = s[:-len(rabpool.MANIFEST)]
            assert {row[5]: row[6] for row in rows if len(row) > 6} == from_game.get(rel, {}),                 f'{rel}: the manifest and the registry name other members read from EDF6'
    assert shipped == own, f'shipped but unlisted: {sorted(shipped - own)[:5]}; listed, not shipped: {sorted(own - shipped)[:5]}'
    assert all(os.path.isfile(eo.bundled(r)) for r in reg.get('shared', {})), 'a shared file is not shipped where named'
    king = from_game.get('OBJECT/MONSTER501_KING.RAB', {})
    assert sorted(src[3] for src in king.values()) == [
        'Monster501_light0_scroll.dds', 'Monster501_nor_body.dds', 'Monster501_nor_wing.dds',
        'Monster501_param_rslh_body.dds', 'Monster501_param_rslh_wing.dds'] and         {tuple(src[:2]) for src in king.values()} == {('Root.cpk', 'OBJECT/MONSTER501.MRAB')}, king
    source = eo.game_source(rootcpk.DEFAULT_GAME) if _e5c_have_game() else None
    if source is None:
        print(f'      (skipped: {sorted(from_game)} take members from EDF6, which is not here)')
    for rel in listed:
        if rel in from_game and source is None:
            continue
        data = eo.read(rel, source) if rel in from_game else eo.read(rel)
        assert hashlib.sha256(data).hexdigest() == reg['files'][rel], rel
        if rel.endswith('.SGO'):
            assert not legacy_sgo.mab_blocks(data), f'{rel}: a MAB block still in 4.1\'s form'
            assert sgo.load(data=data).get('xgs_scene_object_class'), rel
        elif rel.endswith(('.RAB', '.MRAB')):
            models = [f for f in mdb.rab_read(data).files if f.name.lower().endswith('.mdb')]
            assert models and all(f.data[4:8] == (0x20).to_bytes(4, 'little') for f in models), rel


@test
def edf41_motherships_not_faked() -> None:
    """Both of 4.1's motherships are the plugin's own classes (UfoMother301: src/edf41mother.cpp; UfoMother401, the Earth
    Eater, and its eleven part classes: src/edf41mother401.cpp), not stood in by an EDF6 class (EDF6's UfoMother511 family is a
    ring of units, nothing like either): every mothership registered keeps 4.1's class name, the 401's parts keep theirs, and
    every mission script that names a mothership names a registered file (make_edf5_campaign.plan41 lets them through)."""
    import edf41_objects as eo
    import make_edf5_campaign as mc
    import sgo
    reg = eo.registry()
    assert reg['objects'], 'no object registered: this check is empty'
    mothers = {o['object']: o['class'] for o in reg['objects'] if o['class'].startswith('UfoMother')}
    import make_edf41_objects as m
    want = {'OBJECT/UFOMOTHER301.SGO': 'UfoMother301'}
    want.update({'OBJECT/' + r: 'UfoMother401' for r in (*m.MOTHER401_ROOTS, *m.MOTHER401_DLC_ROOTS)})
    assert mothers == want, sorted(set(mothers.items()) ^ set(want.items()))
    parts = {sgo.load(data=eo.read(rel)).get('xgs_scene_object_class') for rel in reg['files']
             if rel.endswith('.SGO') and 'UFOMOTHER401_' in rel} - {'UfoMother401'}   # the DLC hulls are UFOMOTHER401_DLC_*
    assert parts == {'UfoMother401_' + c for c in ('Panel01', 'WeakPanel', 'Core', 'BigCannon01', 'Generator', 'FinalCannon01',
                                                   'FinalGenerator', 'GunBattery_SmallLaser', 'GunBattery_Sniper',
                                                   'GunBattery_PlasmaCannon', 'GunBattery_RapidCannon')}, parts
    named = {}
    root = os.path.join(ROOT, 'edf41port', 'missions')
    for mid in sorted(os.listdir(root)):
        with open(os.path.join(root, mid, 'MISSION.BVM'), 'rb') as f:
            for ref in mc.asset_refs(f.read()):
                if 'ufomother' in ref.lower():
                    named.setdefault(mid, set()).add(ref)
    assert len(named) == 21, f'the missions naming a mothership changed: {sorted(named)}'
    for mid, refs in named.items():
        for ref in refs:
            rel = ('OBJECT/' + ref.rsplit('/', 1)[-1]).upper()
            assert rel in reg['files'], (mid, ref)
    for root in m.MOTHER401_DLC_ROOTS:
        assert 'OBJECT/' + root in reg['files'], root


@test
def edf41_mother_plugin_contract() -> None:
    """src/edf41mother.cpp is built, installed and agrees with the data: the class name its factory answers with is the one the
    generator's SGO carries (and the registry's), the ini switch Edf41Mother (on by default, shipped 1) gates the install, the
    development folder and environment switch are gone, and every EDF.dll function it calls is checked against its bytes
    (kPrints) before anything is hooked or called."""
    import edf41_objects as eo
    import make_edf41_objects as m
    cpp, plugin, cmake = src('src/edf41mother.cpp'), src('src/plugin.cpp'), src('CMakeLists.txt')
    assert 'src/edf41mother.cpp' in cmake and 'InstallEdf41Mother();' in plugin
    assert f'L"{m.MOTHER301_ALIAS.cls}"' in cpp, 'the factory must answer with the class name the SGO carries'
    assert m.ROOTS[m.MOTHER301_ROOT] and m.ALIASES[m.MOTHER301_ROOT] is m.MOTHER301_ALIAS
    assert any(o['object'] == 'OBJECT/' + m.MOTHER301_ROOT and o['class'] == m.MOTHER301_ALIAS.cls
               for o in eo.registry()['objects'])
    assert 'if(!Cfg().edf41Mother)' in cpp.split('bool InstallEdf41Mother()', 1)[1].split('BuildClone', 1)[0]
    crew, ini = src('src/crew.h'), src('EDF6VehicleCrew.ini')
    assert 'bool edf41Mother=true;' in crew and 'ReadBool(L"Edf41Mother"' in plugin
    assert re.search(r'^Edf41Mother=1$', ini, re.M) and re.search(r'^Edf41MotherParts=31$', ini, re.M)
    assert not hasattr(eo, 'DEV_ENV') and not hasattr(m, 'MOTHER_DEV_OUT')
    assert 'edf41port_dev' not in src('tools/build_release.py') + src('tools/installer.py')
    shared = src('src/edf41classes.cpp')   # the factory registration and the clones (shared with the other 4.1 classes)
    assert 'src/edf41classes.cpp' in cmake and '#include "edf41classes.h"' in cpp
    for rva in ('kRegisterAllSite', 'kRegisterAll', 'kRegisterFactory', 'kNew', 'kCarrierCtor', 'kEaoCtor'):
        assert re.search(rf'\{{{rva},\{{0x', shared), f'{rva} has no fingerprint in kPrints'
    assert (cpp + shared).count('Rd(') >= 8 and 'Readable(image+p.rva,8)' in shared
    # Real runs (2026-10-15): EventAnimationObject's slot 22 lets SetTeam set team 6 only, so the parts' clone must take
    # GameObjectBase's (else no part is ever an enemy or gets a target); the hull's damage filter is 4.1's (slot 16)
    assert 'AllowAnyTeam(partClass);' in cpp and 'SetSlot(c,22,image+kAnyTeam)' in shared
    assert 'SetSlot(hullClass,kSlotAccept,' in cpp and 'kSlotAccept=16' in cpp
    # The DSGO accessors (0xD7480 ...) fault on a plain SGO such as this object's (EDF+0xD74D9, 2026-10-13): never called here.
    code = re.sub(r'//[^\n]*', '', cpp + shared)   # comments may name the accessors; the code must not call them
    assert not re.search(r'0xD7480|kChildByName|0x53060|0x52DE0|0x52F30|0x551E0', code)
    assert m.MOTHER301_ALIAS.template == 'E508_CARRIER.SGO' and 'UfoCarrier508' in cpp
    # The clone is the stock class: built from the file's table, not the live one this plugin patches (carrierlaser.cpp
    # ShipAiHook takes slot 7 of the same vtable; the 2026-10-13 real run turned the prototype off on it).
    stock = [int(x, 16) for x in re.search(r'kCarrierSlots\[49\]=\{([^}]*)\}', shared).group(1).replace('\n', ' ').split(',')]
    laser = src('src/carrierlaser.cpp')
    ai, slot = (int(re.search(rf'{k}=(\w+)', laser).group(1), 0) for k in ('kShipAi', 'kShipAiSlot'))
    assert len(stock) == 49 and stock[slot] == ai, (len(stock), slot)


@test
def edf41_mother301_finish_contract() -> None:
    """The 301's 4.1 hit, death, states and glow (src/edf41mother.cpp, 2026-10-15) agree with EDF.dll and the data: every EDF.dll
    function the finish calls is in kMotherPrints and the replaced slots (18 OnDamaged, 3 draw) are set only once they matched;
    every 4.1 sound name the class plays or gives a bullet as its hit sound is in the hull SGO's edf41_se (else silence); every
    CAS state the hull plays is in the hull's CAS; every model of the hull and its parts has the two material parameters the
    glow binds (light0_color, scroll_light0); the shared Bullet carries its hit sound into InitParam +0x100 only when set; the
    dev switch Edf41MotherForce takes bits 16, 32, 64, 128 and 256 (the bisect and compare switches) and ships 0."""
    import cas_graft
    import edf41_objects as eo
    import mdb
    import sgo
    cpp, shared, header = src('src/edf41mother.cpp'), src('src/edf41classes.cpp'), src('src/edf41classes.h')
    prints = cpp.split('kMotherPrints[]={', 1)[1].split('};', 1)[0]
    for rva in ('0x300C80', '0x2E8510', '0x1120820', '0x301290', '0x302200', '0x302EE0', '0x302D10', '0x12D85B0'):
        assert '{' + rva + ',' in prints, f'particle emitter call {rva} unchecked'
    # the emitters are on by default (bit 2048 = puffs instead); the exit crash once blamed on them was edf41sound's bank reference
    # (fix/edf41-exit-crash, 2026-10-17): a dropped hull's emitters wait in the graveyard, then go through their own deleting
    # destructor (vtable-checked, fingerprinted); what is left goes right before the application destructor
    assert '(Cfg().edf41MotherForce&2048))return;' in cpp and '!(Cfg().edf41MotherForce&2048)' not in cpp
    assert '{0x301460,' in prints and 'kEmitterVtable=0x17A8F70,kEmitterDelete=0x301460' in cpp
    assert 'VentGraveyardTick();' in cpp and 'AtGameShutdown(&ShutdownVents);' in cpp
    for name in ('kBindCtor', 'kBindDtor', 'kBind', 'kBindSet', 'kBindApply', 'kSetState', 'kUnregister'):
        assert re.search(rf'\{{{name},\{{0x', prints), f'{name} has no fingerprint in kMotherPrints'
    for rva in ('0x4F9250', '0x4F9530', '0x5B6D10'):
        assert '{' + rva + ',' in prints, f'replaced stock slot {rva} unchecked'
    install = cpp.split('bool InstallEdf41Mother()', 1)[1]
    gated = install.split('if(MotherPrims()) {', 1)[1].split('}', 1)[0]
    for slot in ('SetSlot(hullClass,kSlotDamaged,', 'SetSlot(hullClass,kSlotDraw,', 'SetSlot(partClass,kSlotDraw,'):
        assert slot in gated, slot
    assert 'kSlotDraw=3,kSlotDamaged=18' in cpp
    base = os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT')
    with open(os.path.join(base, 'UFOMOTHER301.SGO'), 'rb') as f:
        hull = sgo.load(data=f.read())
    table = {e[0] for e in hull['edf41_se']}
    code = re.sub(r'//[^\n]*', '', cpp)
    names = set(re.findall(r'L"(母船UFO[^"]+)"', code))   # every 4.1 preset name the class plays or hands a bullet
    assert len(names) >= 30 and not (names - table), sorted(names - table)
    with open(os.path.join(base, 'UFOMOTHER301.CAS'), 'rb') as f:
        states = cas_graft.states(f.read())['main']
    played = set(re.findall(r'HullState\(self,L"(\w+)"\)', code)) | set(re.findall(r'big \? L"(\w+)" : L"(\w+)"', code)[0])
    assert played == {'open', 'show', 'hide', 'close'} and played <= states, (played, states)
    models = [x for x in mdb.rab_read(eo.read('OBJECT/UFOMOTHER301.MRAB')).files if x.name.lower().endswith('.mdb')]
    assert len(models) == 6
    for x in models:
        md = mdb.mdb_read(x.data)
        params = {p.name for mt in md.materials for p in mt.params}
        assert {'light0_color', 'scroll_light0'} <= params, (x.name, sorted(params))
    assert 'const wchar_t* hitSe=nullptr;' in header and 'bool FillSePreset(unsigned char* preset' in header   # unified with UfoRobo's (one definition, initialises the preset itself)
    assert 'FillSePreset(param+0x100,b.hitSeTable,b.hitSe)' in shared
    plugin, ini = src('src/plugin.cpp'), src('EDF6VehicleCrew.ini')
    assert 'ReadInt(L"Edf41MotherForce",n.edf41MotherForce),0,4095)' in plugin
    assert re.search(r'^Edf41MotherForce=0$', ini, re.M) and '16 =' in ini.split('Edf41MotherForce=0', 1)[0].rsplit('Edf41MotherParts=31', 1)[1]
    # the clamp keeps every bit the ini documents (2048 = the puffs: a cap of 2047 cut it, 2026-10-17 review)
    doc = ini.split('Edf41MotherForce=0', 1)[0].rsplit('Edf41MotherParts=31', 1)[1]
    top = max(int(b) for b in re.findall(r'(\d+) =', doc))
    cap = int(re.search(r'ReadInt\(L"Edf41MotherForce",n\.edf41MotherForce\),0,(\d+)\)', plugin).group(1))
    assert top == 2048 and cap == 2 * top - 1, (top, cap)


@test
def edf41_robo_registered() -> None:
    """4.1's UfoRobo (the ship that turns into the robot アルゴ) is the plugin's own class (src/edf41robo.cpp): its SGO keeps
    4.1's class name and keys, is registered with its closure, and is named by exactly the five missions it unlocks (story M007 /
    M411, DLC pack 1 M415 / M416, pack 2 M010; make_edf5_campaign.plan41 skips a mission whose object file is not registered).
    What robo_fix adds is there and agrees with the files: the CAS has `default` (EventAnimationObject's constructor plays it)
    and every state the plugin plays, the moving clips' root bone has no track left (the plugin moves the object), every
    edf41_robo_events state is a CAS state, every se event and class sound is an edf41_se name with 4.1's cue, and
    material_animation is 4.1's two light blobs as [MTAB blob, slot] pairs (ROBO_MATERIAL_PAIRS; 4.1's bare blobs make EDF6's
    material code fault; src/edf41robo.cpp hands the pairs to EDF6's material animation player)."""
    import cas_graft
    import cas_pose
    import edf41_objects as eo
    import make_edf41_objects as m
    import make_edf5_campaign as mc
    import mab_legacy
    import sgo
    reg = eo.registry()
    rel = 'OBJECT/' + m.ROBO_ROOT
    robo = [o for o in reg['objects'] if o['object'] == rel]
    assert robo and robo[0]['class'] == 'UfoRobo' and m.ROOTS[m.ROBO_ROOT] == 2, robo
    assert m.ROBO_ROOT not in m.ALIASES and m.NATIVE_FIXES[m.ROBO_ROOT] is m.robo_fix
    assert sorted(robo[0]['files']) == sorted([rel, 'OBJECT/UFOROBO401.RAB', 'OBJECT/UFOROBO401.CAS',
                                               'OBJECT/RAGDOLL_UFOROBO401_HAS_HKA.SHKT']), robo[0]['files']
    naming = []
    for base in ('missions', 'mp01/missions', 'mp02/missions'):
        root = os.path.join(ROOT, 'edf41port', *base.split('/'))
        for mid in sorted(os.listdir(root)):
            for script in ('MISSION.BVM', 'BEFORE.BVM'):
                path = os.path.join(root, mid, script)
                if os.path.isfile(path):
                    with open(path, 'rb') as f:
                        if any(r.lower().endswith('uforobo401.sgo') for r in mc.asset_refs(f.read())):
                            naming.append(f'{base.split("/")[0]}/{mid}')
    assert sorted(set(naming)) == ['missions/M007', 'missions/M411', 'mp01/M415', 'mp01/M416', 'mp02/M010'], naming
    top = sgo.load(data=eo.read(rel))
    assert top['xgs_scene_object_class'] == 'UfoRobo'
    ma = top['material_animation']
    assert [e[1] for e in ma] == [slot for _i, slot in m.ROBO_MATERIAL_PAIRS] == [0, 2], ma
    blobs = [bytes.fromhex(e[0][4:]) if isinstance(e[0], str) and e[0].startswith('raw:') else e[0] for e in ma]
    assert all(isinstance(b, bytes) and b[16:20] == b'MTAB' for b in blobs), [type(e[0]) for e in ma]
    for key in ('robo_attack_table', 'robo_small_muzzle_setting', 'robo_middle_muzzle_setting', 'robo_large_muzzle_setting',
                'small_bullet_class', 'cannon_bullet_class', 'genocide_bullet_class', 'se_table', 'game_sound'):
        assert key in top, key
    assert len(top['robo_attack_table']) == 8 and top['game_object_durability'] == 12000.0
    cas = eo.read('OBJECT/UFOROBO401.CAS')
    states = cas_graft.states(cas)['main']
    assert set(m.ROBO_STATES) <= states, sorted(set(m.ROBO_STATES) - states)
    assert cas_graft.state_clips(cas)['main']['default'] == cas_graft.state_clips(cas)['main']['ship']
    # root motion: one table per moving state, its clip's root bone without tracks
    pose = cas_pose.CasPose(cas)
    clips = cas_graft.state_clips(cas)['main']
    motion = {s: (length, keys) for s, length, keys in top['edf41_robo_motion']}
    assert set(motion) == {'walk_f_nl', 'walk_f_lr', 'walk_f_rl', 'walk_f_ln', 'walk_f_rn', 'walk_b_nl', 'walk_b_lr',
                           'walk_b_rl', 'walk_b_ln', 'walk_b_rn', 'side_step_l', 'side_step_r', 'turn_l45', 'turn_r45',
                           'turn_l90', 'turn_r90', 'turn_l180', 'turn_r180'}, sorted(motion)
    by_name = {c.name: c for c in pose.clips}
    for state, (length, keys) in motion.items():
        clip = by_name[clips[state][0]]
        assert all(t.translation == -1 and t.rotation == -1 for t in clip.tracks if t.name == m.ROBO_ROOT_BONE), state
        assert len(keys) % 3 == 0 and len(keys) // 3 <= m.ROBO_MOST_MOTION_KEYS and length > 0, state
        assert keys[:3] == [0.0, 0.0, 0.0], state
    near = lambda a, b: abs(a - b) < 0.01
    assert near(motion['walk_f_nl'][1][-2], 50.0) and near(motion['walk_b_lr'][1][-2], -100.0)
    assert near(motion['side_step_l'][1][-3], 80.0) and near(motion['turn_l90'][1][-1], 1.5708)
    assert near(motion['turn_r180'][1][-1], -3.1416)
    # events: CAS states, known kinds, the MAB's own times
    se = {e[0]: e for e in top['edf41_se']}
    kinds = set(m.ROBO_EVENTS.values())
    for state, clip, keys in top['edf41_robo_events']:
        assert state in states and 0 <= clip < len(clips[state]) and len(keys) <= m.ROBO_MOST_EVENTS, state
        for t, kind, text, a, b, c in keys:
            assert kind in kinds, (state, kind)
            if kind == m.ROBO_EVENTS['se']:
                assert text in se, (state, text)
    events = {(s, n): keys for s, n, keys in top['edf41_robo_events']}
    hits = [k for k in events[('attack_stomp_r', 0)] if k[1] == m.ROBO_EVENTS['攻撃判定']]
    assert hits and hits[0][2] == '右足歩行衝撃' and hits[0][3] == 40.0 and hits[0][4] == 3.0 and hits[0][5] == 2.0, hits
    genocide = [k for k in events[('attack_fire_genocide', 0)] if k[1] == m.ROBO_EVENTS['ジェノサイド砲発射']]
    assert genocide and genocide[0][3] == 346.0, genocide
    mab = mab_legacy.mab_events(bytes.fromhex(top['animation_model'][2][4:]) if isinstance(top['animation_model'][2], str)
                                else top['animation_model'][2])
    assert ('砲撃開始', mab['attack_fire_cross'][2][1]) in mab['attack_fire_cross']
    for name in list(top['se_table']) + list(m.ROBO_CLASS_SE) + [top['ragdoll_contact'][0]]:
        assert name in se, name
    assert se['ロボ駆動音'][1] == 'en_Robo_moveDriving' and se['ロボ飛行形態バスター砲チャージ'][3] == 1.5
    assert all(len(e) == 7 and e[1].startswith(('en_Robo_', 'common_damages_')) for e in top['edf41_se'])
    # CLASS_NAMES holds every shipped robot to the plugin's names
    assert m.CLASS_NAMES['UfoRobo'].states == m.ROBO_STATES and m.CLASS_NAMES['UfoRobo'].locators == m.ROBO_LOCATORS


@test
def edf41_robo_plugin_contract() -> None:
    """src/edf41robo.cpp is built, installed and agrees with the data: the factory answers with the SGO's class name, the
    keys it reads are the ones robo_fix writes, its table limits are robo_fix's, the ini switch Edf41Robo (on, shipped 1)
    gates it and its development switches ship off, it builds on EventAnimationObject's constructor with the HP block of
    that class cleared and its durability sized by level (slot 6), the transform refuses damage, ObjectAction 0 / 1 / 2
    follow 4.1, and every EDF.dll function the shared module calls for it is fingerprinted."""
    import make_edf41_objects as m
    cpp, plugin, cmake, crew = src('src/edf41robo.cpp'), src('src/plugin.cpp'), src('CMakeLists.txt'), src('src/crew.h')
    shared, ini = src('src/edf41classes.cpp'), src('EDF6VehicleCrew.ini')
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/edf41robo.cpp)' in cmake and 'InstallEdf41Robo();' in plugin
    assert 'bool InstallEdf41Robo() noexcept;' in crew and 'bool edf41Robo=true;' in crew and 'ReadBool(L"Edf41Robo"' in plugin
    assert 'if(!Cfg().edf41Robo)' in cpp.split('bool InstallEdf41Robo()', 1)[1].split('BuildClone', 1)[0]
    assert 'AddFactory(L"UfoRobo"' in cpp and 'CloneObject(roboClass,kEaoCtor,kEaoSize,info)' in cpp
    assert re.search(r'^Edf41Robo=1$', ini, re.M)
    for key, off in (('Edf41RoboLevel', '0'), ('Edf41RoboGunEvery', '0'), ('Edf41RoboAttack', '-1'), ('Edf41RoboPlace', '0'),
                     ('Edf41RoboTransformAfter', '0')):
        assert re.search(rf'^{key}={re.escape(off)}$', ini, re.M), key
        assert f'L"{key}"' in plugin, key
    for key in ('edf41_robo_events', 'edf41_robo_motion', 'robo_attack_table', 'robo_small_muzzle_setting',
                'robo_middle_muzzle_setting', 'robo_large_muzzle_setting', 'small_bullet_custom_param',
                'cannon_bullet_custom_param', 'genocide_bullet_custom_param'):
        assert f'L"{key}"' in cpp, key
    for name, value in (('kMostEvents', m.ROBO_MOST_EVENTS), ('kMostMotionKeys', m.ROBO_MOST_MOTION_KEYS)):
        assert re.search(rf'constexpr int {name}={value};', cpp), name
    for kind, value in m.ROBO_EVENTS.items():   # EventKind follows ROBO_EVENTS
        assert re.search(rf'kEv\w+={value}', cpp), kind
    assert 'SetFlag(obj,kNoHpLoss,false)' in cpp and 'SetSlot(roboClass,kSlotOnLevel' in cpp and 'SizeDurability(self)' in cpp
    message = cpp.split('MessageWrap(void* self', 1)[1].split('\n}\n', 1)[0]
    assert 'msg==kMsgDamage' in message and 'it->second.body==kS4)return 0;' in message
    assert 'action==0' in message and 'action==1' in message and 'action==2' in message
    # ObjectAction is never handed to EventAnimationObject's OnMessage (it throws without an `action` table: M411, 2026-10-15)
    action = message.split('if(msg==kMsgAction', 1)[1].split('if(msg==kMsgDamage', 1)[0]
    assert re.search(r'return 1;\s*\}\s*\}\s*$', action), action
    assert 'AllowAnyTeam(roboClass);' in cpp
    # the robot's AI is 4.1's A3 / A4 / A5 (src/edf41robo_ai.h, tests/edf41robo_ai_test.cpp): A5 holds the AI through the
    # attack (it only attacked and never walked, 2026-10-17)
    assert 'edf41robo::RobotAiStep(' in cpp and 'edf41robo::ChooseInStand(' in cpp
    assert 'add_test(NAME edf41robo_ai COMMAND edf41robo_ai_test)' in cmake
    # the ground ray passes the robot's own ragdoll bodies (they lifted it on the M411 run)
    assert 'GroundHit(knee,to,hit,ragdoll) || GroundHit(top,knee,hit,ragdoll)' in cpp
    for name in m.ROBO_LOCATORS[:6] + ('左足歩行衝撃', '右足歩行衝撃', 'ロックオン'):
        assert name in cpp or name.startswith('muzzle_'), name   # the muzzles come from the SGO's settings
    for rva in ('kStateByName', 'kPlayStateIndex', 'kCastRay', 'kHitReset', 'kGroundAdd', 'kAddLockTarget', 'kWind', 'kQuake'):
        assert re.search(rf'\{{{rva},\{{0x', shared), f'{rva} has no fingerprint'
    code = re.sub(r'//[^\n]*', '', cpp)
    assert not re.search(r'0xD7480|0x53060|0x52DE0|0x52F30', code), 'a DSGO accessor on a classic SGO'
    assert 'std::min(' not in code and 'std::max(' not in code   # windows.h macros


@test
def edf41_net_contract() -> None:
    """The EDF4.1 classes online (src/edf41net.h, docs/edf41-classes.md 联机): the layer is built and switched by Edf41Net
    (shipped 1; the development loopback Edf41NetMirror shipped 0); every EDF.dll function it calls is fingerprinted; its tag
    is none the game, coop (13 / 14) or the plugin's vehicles (15) use; it asks online_authority, never the session functions
    itself; each class attaches its objects, gives its parts and spawns the game's child identity, and its guest copies leave
    the decisions to the host (UfoRobo: no AI, no gun timer; 401: no attack start; 301: no cannon charge). The 401 runs on
    every machine (its ObjectAction reached EventAnimationObject's OnMessage on a guest, which throws without `action`)."""
    net, head = src('src/edf41net.cpp'), src('src/edf41net.h')
    cmake, plugin, crew, ini = src('CMakeLists.txt'), src('src/plugin.cpp'), src('src/crew.h'), src('EDF6VehicleCrew.ini')
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/edf41net.cpp)' in cmake and 'add_test(NAME edf41net COMMAND edf41net_test)' in cmake
    assert 'bool edf41Net=true;' in crew and 'bool edf41NetMirror=false;' in crew
    assert 'ReadBool(L"Edf41Net"' in plugin and 'ReadBool(L"Edf41NetMirror"' in plugin
    assert re.search(r'^Edf41Net=1$', ini, re.M) and re.search(r'^Edf41NetMirror=0$', ini, re.M)
    assert 'bool Enabled() noexcept { return Cfg().edf41Net && Install(); }' in net
    assert 'bool Mirror() noexcept { return Cfg().edf41NetMirror && Enabled() && !Online(); }' in net, 'the loopback is offline only'
    tag = int(re.search(r'constexpr std::int8_t kTag=(\d+);', head).group(1))
    assert tag not in (0, 1, 2, 3, 4, 5, 13, 14, 15) and -14 <= tag <= 15
    for other in ('drill_net.h', 'proteus_net.h', 'sazabi_net.h'):
        assert f'kTag={tag};' not in src('src/' + other), other
    rvas = {d.split('=')[0].strip() for line in re.findall(r'^constexpr unsigned (.*);$', net, re.M) for d in line.split(',')}
    # vtables (read, not called: their slots are checked by the native test) and the stock receiver (only called on; EDF6Coop
    # hooks its entry, which turned the layer off on the real game when it was fingerprinted)
    rvas -= {'kEaoNet', 'kCarrierNet', 'kStockReceive'}
    assert len(rvas) >= 12, rvas
    assert '{kStockReceive,{' not in net
    for name in rvas:
        assert re.search(rf'\{{{name},\{{0x', net), f'{name} has no fingerprint'
    code = re.sub(r'//[^\n]*', '', net)
    assert not re.search(r'0x7748F0|0x784210|0x630F90|0x630DF0', code), 'session / host / operator asked outside online_authority'
    assert 'OnlineRunsHere(object)' in code and 'InSession()' in code
    # the classes
    robo, m401, m301 = src('src/edf41robo.cpp'), src('src/edf41mother401.cpp'), src('src/edf41mother.cpp')
    for cpp in (robo, m401, m301):
        assert '#include "edf41net.h"' in cpp and 'edf41net::Attach(obj);' in cpp
    decision = robo.split('if(r.body<kS10 && !NetFollows(r)) {', 1)[1].split('\n    } else {', 1)[0]
    assert 'if(r.player.Active())PlayerTick(r);' in decision and 'else AiTick(r);' in decision
    assert '!NetFollows(r) && --g.autoCount<=0' in robo and 'NetBody(r);' in robo.split('void BodySet(', 1)[1].split('\n}\n', 1)[0]
    assert 'NetMirrorMake(self);' in robo and 'CreateFromPath(m,L"app:/object/UfoRobo401.sgo")' in robo
    assert 'OnlineRunsHere(self)' not in re.sub(r'//[^\n]*', '', m401), 'the 401 runs on every machine'
    for case in ('case sBcIdle:', 'case sFcIdle:', 'case sGbIdle: {'):
        assert 'NetDecides(' in m401.split(case, 1)[1].split('case ', 1)[0], case
    assert 'NetPanel(h,*p);' in m401 and 'NetBattery(p,*b,i);' in m401 and 'NetSpawn(p,o);' in m401 and 'edf41net::Pump(self);' in m401
    assert 'NetEntered(p,netFrom,state);' in m401 and 'NetPartIn(*p);' in m401
    assert 'if((p.manualLease && !p.attack) || !NetDecides(obj))break;' in m301.split('case kCanWait:', 1)[1].split('case ', 1)[0]
    assert 'NetPart(hullObj,part,group,h.parts.size()-1);' in m301 and 'NetSpawn(p,enemy);' in m301 and 'edf41net::Pump(self);' in m301
    for cpp in (robo, m401, m301):   # windows.h macros: the 4.1 class files use (std::min) / (std::max)
        assert 'far' not in re.findall(r'\b(?:float|int|bool|auto)\s+(\w+)', re.sub(r'//[^\n]*', '', cpp))


@test
def edf41_robo_finish() -> None:
    """UfoRobo phase 3 (docs/edf41-uforobo.md 2.2): the lock point is found where the shipped MAB keeps it (ロックオン in locator
    list 1, the lock-on list; the muzzles and the stomp points in list 0) and src/edf41robo.cpp asks that list; the death is
    OnDamaged's (slot 18) with EDF6's ragdoll activation; the stomps are EDF6's ApplyAreaDamage (no grenade stand-in); the
    bullets carry 4.1's hit sounds (FireSe, InitParam +0x100) whose presets edf41_se holds; the foot IK reads ik_control_info,
    which the shipped SGO keeps; every EDF.dll function the file calls itself has a fingerprint."""
    import edf41_objects as eo
    import make_edf41_objects as m
    import mab_legacy
    import sgo
    top = sgo.load(data=eo.read('OBJECT/' + m.ROBO_ROOT))
    mab = top['animation_model'][2]
    mab = bytes.fromhex(mab[4:]) if isinstance(mab, str) and mab.startswith('raw:') else mab
    lists = {loc.name: i for i, loc in mab_legacy.mab_locators(mab)}
    assert lists.get('ロックオン') == 1, lists.get('ロックオン')
    for name in m.ROBO_LOCATORS:
        assert lists.get(name) == (1 if name == 'ロックオン' else 0), name
    ik = top['ik_control_info']
    assert [e[0] for e in ik] == ['toe_l', 'toe_r'] and [e[1] for e in ik] == ['v_ik_target_l', 'v_ik_target_r'], ik
    se = {e[0] for e in top['edf41_se']}
    cpp, shared, header = src('src/edf41robo.cpp'), src('src/edf41classes.cpp'), src('src/edf41classes.h')
    assert 'p6::FindLocatorIn(Model(r.obj),1,L"ロックオン",&r.lockOn)' in cpp
    assert 'SetSlot(roboClass,kSlotOnDamaged,reinterpret_cast<void*>(&OnDamagedWrap));' in cpp and 'kSlotOnDamaged=18' in cpp
    # the transform refuses damage in slot 16 (every damage path asks it; the OnMessage check alone let hits through on M416)
    accept = cpp.split('bool __fastcall AcceptWrap(void* self,void* gdi) {', 1)[1].split('return reinterpret_cast', 1)[0]
    assert 'kSlotAccept=16' in cpp and 'it->second.body==kS4)return false;' in accept
    assert 'SetSlot(roboClass,kSlotAccept,reinterpret_cast<void*>(&AcceptWrap));' in cpp
    assert 'r.ragdoll=p6::RagdollOn(r.obj);' in cpp and 'p6::AreaDamage(r.obj,lifted,radius,damage,' in cpp
    # the ragdoll goes back on the ragdoll layer first (EventAnimationObject's constructor put it on 0x11, which the ground ignores)
    on = cpp.split('bool RagdollOn(void* object) noexcept {', 1)[1].split('return true;', 1)[0]
    assert on.index('(kRagdollLayer)(rag,6)') < on.index('(kRagdollOn)(rag)'), on
    # the stomp's circle: scale x the kind-0 locator's own radius (record +0x10), at the ground under it
    assert 'std::memcpy(&size,rec+0x10,4);' in cpp and 'GroundHit(from,to,c,' in cpp
    code = re.sub(r'//[^\n]*', '', cpp)
    assert 'Blast(' not in code, 'the stomp still lets off the grenade stand-in'
    assert 'bool FireSe(void* owner,const float* matrix,const Bullet& b,const void* seTable,const wchar_t* hitSe) noexcept;' in header
    assert 'c.hitSe=hitSe;' in shared and 'FillRawCue(param+0x100,b.hitCue' in shared and 'FireSe(r.obj,mm,g.bullet,r.obj,g.hitSe)' in cpp
    for name in re.findall(r'hitSe=L"([^"]+)"', cpp) + re.findall(r'chargeSe=L"([^"]+)"', cpp) + re.findall(r'shotSe=L"([^"]+)"', cpp):
        assert name in se, name
    assert len(re.findall(r'hitSe=L"', cpp)) == 4 and 'L"ik_control_info"' in cpp
    # the lights: EDF6's material animation player on the model (+0x160), the loop at birth, the second entry at the death
    assert 'p6::LightsStart(r.lights,r.obj,Key(o,L"material_animation"))' in cpp and 'p6::LightsPlay(r.lights,0,2)' in cpp
    assert 'p6::LightsPlay(r.lights,1,1)' in cpp and 'p6::LightsEnd(r.lights);' in cpp.split('void Release(Robo& r) noexcept {', 1)[1].split('\n}', 1)[0]
    assert 'Release(it->second);' in cpp.split('std::uint64_t __fastcall DtorWrap(', 1)[1].split('\n}', 1)[0]
    prints = cpp.split('constexpr Print kPrints[]={', 1)[1].split('};', 1)[0]
    for rva in re.findall(r'\b(k[A-Z]\w+)=0x[0-9A-F]+', cpp.split('namespace p6 {', 1)[1].split('struct Print', 1)[0]):
        if rva in ('kInitParamBase', 'kEffects', 'kManager', 'kChargeVtable', 'kParticleParamVtable'):   # data, not code
            continue
        assert re.search(rf'\{{{rva},\{{0x', prints), f'{rva} has no fingerprint'


@test
def edf41_mother401_data() -> None:
    """The 401 mothership's shipped data is what src/edf41mother401.cpp reads (tools/make_edf41_objects.py mother401_fix): every
    variant (MOTHER401_ROOTS, MOTHER401_DLC_ROOTS) keeps 4.1's class and keys (action_list, panel_definition, the layout maps its
    action_list names, 19 x 19) and has the model and ragdoll EventAnimationObject's constructor reads unchecked; every panel
    definition names a shipped part SGO; every part SGO has a CAS whose main player has the state default (the constructor asks
    for it), edf41_401_se parallel to se_table (cues of EDF6's own Earth Eater bank, 4.1's preset numbers), its muzzle / spawn
    point / battery mounts where its class needs them, and a weakpoint body its ragdoll has."""
    import cas_graft
    import edf41_objects as eo
    import make_edf41_objects as m
    import sgo
    reg = eo.registry()
    files = set(reg['files'])
    roots = ['OBJECT/' + r for r in (*m.MOTHER401_ROOTS, *m.MOTHER401_DLC_ROOTS)]
    assert len(roots) == 17 and all(r in files for r in roots), [r for r in roots if r not in files]
    parts: set[str] = set()
    for rel in roots:
        top = sgo.load(data=eo.read(rel))
        assert top['xgs_scene_object_class'] == 'UfoMother401', rel
        am, rd = top.get('animation_model'), top.get('ragdoll')
        assert isinstance(am, list) and m.rel_of(am[0][0][0]) in files and m.rel_of(am[1]) in files, rel
        assert isinstance(rd, list) and m.rel_of(rd[0]) in files, rel
        defs = top['panel_definition']
        for d in defs:
            if isinstance(d[0], str):
                assert m.rel_of(d[0]) in files, (rel, d[0])
                parts.add(m.rel_of(d[0]))
        maps = 0
        for a in top['action_list']:
            if a[0] == 0:
                grid = top[a[1]]
                assert len(grid) == 19 and all(len(row) == 19 for row in grid), (rel, a[1])
                assert all(0 <= v < len(defs) for row in grid for v in row), (rel, a[1])
                maps += 1
            assert a[0] in range(8), (rel, a)
        assert maps, rel
        assert [x[0] for x in top['edf41_401_se']] == ['en_EE_ambient'], rel
    for rel in list(parts):   # the batteries Panel01 hangs
        for d in sgo.load(data=eo.read(rel)).get('parts_define', []):
            if isinstance(d[0], str):
                parts.add(m.rel_of(d[0]))
    classes = set()
    for rel in sorted(parts):
        top = sgo.load(data=eo.read(rel))
        cls = top['xgs_scene_object_class']
        classes.add(cls)
        am = top['animation_model']
        cas = m.rel_of(am[1])
        assert cas in files and 'default' in cas_graft.states(eo.read(cas)).get('main', set()), (rel, cas)
        assert m.rel_of(top['ragdoll'][0]) in files, rel
        se, table = top.get('edf41_401_se'), top.get('se_table')
        assert isinstance(se, list) and len(se) == len(table), rel
        for name, entry in zip(table, se):
            assert (entry == 0) == (not isinstance(name, str)), (rel, name, entry)
            assert entry == 0 or entry[0].startswith(('en_EE_', 'common_')), (rel, entry)
        if cls in ('UfoMother401_BigCannon01', 'UfoMother401_FinalCannon01') or cls.startswith('UfoMother401_GunBattery_'):
            assert len(top['edf41_401_muzzle']) == 3, rel
        if cls in ('UfoMother401_Generator', 'UfoMother401_FinalGenerator'):
            assert len(top['edf41_401_spawn']) == 3, rel
        if 'parts_define' in top:
            assert len(top['edf41_401_mounts']) >= 10, rel
        if 'weakpoint' in top:
            assert len(top['edf41_401_weak']) == 3, rel
            assert m.ragdoll_body(eo.read(m.rel_of(top['ragdoll'][0])), top['weakpoint']), (rel, top['weakpoint'])
        # the lock-on point (MAB list 1, ターゲット): every part that can be hurt has one, the Panel01 armour none (4.1's models)
        if cls == 'UfoMother401_Panel01':
            assert 'edf41_401_lock' not in top, rel
        else:
            assert len(top['edf41_401_lock']) == 3, rel
        # the charge glows (MOTHER401_GLOWS): EDF6's ChargeEffect_StdParticle node per colour, 4.1's Particle3_k block
        glows = top.get('edf41_401_charge')
        assert (glows is not None) == (cls in m.MOTHER401_GLOWS), rel
        for node, colour in zip(glows or [], m.MOTHER401_GLOWS.get(cls, ())):
            assert node[1][0] == 'ChargeEffect_StdParticle' and node[1][1][0][-1] == [0, 'Particle3_k.dds'], rel
            assert [round(float(c), 3) for c in node[1][1][0][13][1]] == list(colour), rel
        if glows:
            assert len(glows) == len(m.MOTHER401_GLOWS[cls]), rel
    assert len(classes) == 11, classes


@test
def edf41_mother401_plugin_contract() -> None:
    """src/edf41mother401.cpp is built and installed (its own CMake line, InstallEdf41Mother401 next to InstallEdf41Mother), gated
    by the ini switch Edf41Mother401 (on, shipped 1; its development switches shipped 0), registers a factory for every class the
    401 data carries, replaces the slots the shared clone machinery documents (16 the damage filter, 18 after the damage), and
    every EDF.dll function the shared module calls for it has a fingerprint."""
    import edf41_objects as eo
    import sgo
    cpp, plugin, cmake, crew, ini = (src(r) for r in ('src/edf41mother401.cpp', 'src/plugin.cpp', 'CMakeLists.txt', 'src/crew.h',
                                                       'EDF6VehicleCrew.ini'))
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/edf41mother401.cpp)' in cmake
    assert 'InstallEdf41Mother401();' in plugin and 'bool InstallEdf41Mother401() noexcept;' in crew
    assert plugin.index('InstallEdf41Mother();') < plugin.index('InstallEdf41Mother401();')
    assert 'bool edf41Mother401=true;' in crew and 'ReadBool(L"Edf41Mother401"' in plugin
    assert re.search(r'^Edf41Mother401=1$', ini, re.M) and re.search(r'^Edf41Mother401Dev=0$', ini, re.M)
    assert re.search(r'^Edf41Mother401Place=0$', ini, re.M)
    assert 'if(!Cfg().edf41Mother401)' in cpp.split('bool InstallEdf41Mother401()', 1)[1].split('BuildClone', 1)[0]
    classes = {sgo.load(data=eo.read(rel)).get('xgs_scene_object_class') for rel in eo.registry()['files']
               if rel.endswith('.SGO') and 'UFOMOTHER401' in rel}
    assert len(classes) == 12, classes
    for cls in classes:
        assert f'{{L"{cls}",' in cpp, f'no factory for {cls}'
    assert 'SetSlot(partClone,kSlotAccept,' in cpp and 'SetSlot(partClone,kSlotDamaged,' in cpp and 'SetSlot(hullClone,kSlotAccept,' in cpp
    # EventAnimationObject's slot 22 allows only team 6: without the replacement no SetTeam (CreateEnemy's, the parts') takes
    assert 'AllowAnyTeam(hullClone);' in cpp and 'AllowAnyTeam(partClone);' in cpp
    shared = src('src/edf41classes.cpp')
    for rva in ('kSkeletonFromMatrix', 'kSetFarRender', 'kActivate', 'kSetTeam', 'kSetLevel', 'kGetTarget', 'kBodyById',
                'kBodyByName', 'kCueLookup', 'kPresetPlay', 'kFlame', 'kExplosionBig', 'kDifficulty'):
        assert re.search(rf'\{{{rva},\{{0x', shared), f'{rva} has no fingerprint'
    assert 'kMaxDurabilityAt=0x2F4' in src('src/edf41classes.h')
    code = re.sub(r'//[^\n]*', '', cpp)
    assert not re.search(r'0xD7480|0x53060|0x52DE0|0x52F30|0x551E0', code)   # the DSGO accessors fault on a plain SGO
    # online every machine runs it (edf41net; a guest's ObjectAction must not reach EventAnimationObject's OnMessage): edf41_net_contract
    assert 'OnlineRunsHere(self)' not in code and 'edf41net::Pump(self);' in cpp
    # the finishing pass (2026-10-15, docs/edf41-ufomother401.md): material animations, bullet hit sounds, lock-on points, the
    # weak panel's light flashes, the charge glows and the hull's ragdoll out of the world; every new EDF.dll call fingerprinted
    for rva in ('kMatCtor', 'kMatBind', 'kMatAdd', 'kMatPlay', 'kMatUpdate', 'kMatFree'):
        assert re.search(rf'\{{{rva},\{{0x', shared), f'{rva} has no fingerprint'
    for call in ('FireHit(', 'MaterialAnimsLoad(', 'MaterialAnimsFree(', 'AddLockPoint(', 'StencilLight(', 'SmallBurst(', 'Encount('):
        assert call in cpp, call
    assert 'kChargePrint' in cpp and 'kDisablePrint' in cpp and 'GraveyardTick();' in cpp
    # the real-run pass (2026-10-16): the exit hook, the render job in the sync-render clone slot, the dev shooter at ordinary enemies;
    # the charge glows are on (dev bit 4194304 turns them off): their exit crash was edf41sound's weapon bank reference, which is now
    # given back through the game's BankRef release (0x7AAC80, fingerprinted) right before the application destructor
    assert 'AtGameShutdown(&ShutdownGlows);' in cpp and 'PartSyncRender' in cpp and 'Edf41Mother401DevTick' in cpp
    assert '!(DevBits()&4194304))GlowsMake(p)' in cpp and 'AtGameShutdown(' in src('src/edf41classes.h')
    sound = src('src/edf41sound.cpp')
    assert 'kBankRelease=0x7AAC80' in sound and '{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x51,0x08}' in sound
    assert 'edf41c::AtGameShutdown(&ReleaseWeaponBank);' in sound and 'weaponBankPtr.store(nullptr);' in sound
    assert 'Edf41Mother401DevTick();' in src('src/map.cpp') and 'Edf41Mother401DevTick();' in src('src/crew.cpp')
    assert re.search(r'^Edf41DevShoot=0$', ini, re.M)


@test
def edf41_mother401_parts_take_the_hull_level() -> None:
    """The Earth Eater's parts are as tough, and their guns as strong / accurate / fast, as the hull's level makes them
    (docs/feedback-2026-10-17-eartheater.md): 4.1 gives every panel the hull's effective level (+0x490, which the hull's slot 3
    copies from SetLevel's +0x25c) and Panel01 hands it to its batteries; HP = durability x level / 10 x the difficulty rows,
    the bullet blocks use the same s. The port once took s = 1 on every difficulty (its notes read +0x490 as never written):
    on Inferno a 16000 core had 16000 HP where 4.1 gives 260 000-450 000. The plugin records the hull's EDF6 level (+0x49C) on
    each panel and battery and sizes both from it. With EDF4.1 installed the 4.1 path is checked byte for byte; with EDF6 too,
    the claim the fix rests on: EDF6's story difficulty ranges put the Earth Eater missions' s within 10 % of 4.1's."""
    cpp = re.sub(r'//[^\n]*', '', src('src/edf41mother401.cpp'))
    assert 'float level=10.0f;' in cpp, 'Part has no level'
    make_panel = cpp.split('void* MakePanel(Part& h,Cell& c,', 1)[1].split('\n}\n', 1)[0]
    assert 'p->level=LevelS(h.obj)*10.0f;' in make_panel, 'a panel must take the hull\'s level (4.1 0x25fbb6)'
    batteries = cpp.split('void MakeBatteries(Part& p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'b->level=p.level;' in batteries, 'a battery must take its panel\'s level (4.1 0x26ebb0)'
    init = cpp.split('void InitPart(Part& p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'const float s=p.level/10.0f;' in init and 'Difficulty(base*s)' in init, 'HP must be durability x s x difficulty'
    assert not re.search(r'Difficulty\(base\)', cpp), 'HP without the level is what the players saw as wrong'
    bullets = cpp.split('void SetupBullets(Part& p) noexcept {', 1)[1].split('\n}\n', 1)[0]
    assert 'const float s=p.level/10.0f;' in bullets and 'const float s=1.0f;' not in cpp
    assert 'kLevelAt=0x49C' in src('src/edf41classes.h')

    import rootcpk
    steam = os.path.dirname(rootcpk.DEFAULT_GAME)
    exe = os.path.join(steam, 'Earth Defense Force 4.1', 'EDF41.exe')
    if not os.path.exists(exe):
        print('skip  edf41_mother401_parts_take_the_hull_level (4.1 code, rows): EDF4.1 not installed')
        return
    import pefile
    pe = pefile.PE(exe, fast_load=True)
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase

    def slot(vtable: int, i: int) -> int:
        return int.from_bytes(img[vtable + 8 * i:vtable + 8 * i + 8], 'little') - base

    def at(rva: int, hexbytes: str) -> None:
        assert img[rva:rva + len(hexbytes) // 2].hex() == hexbytes, f'EDF41.exe 0x{rva:X}: not the code these notes read'
    at(0x28EB44, 'c644244808' 'f30f11835c020000')        # SetLevel: +0x25c = the difficulty-scaled level, then flag 8 ...
    at(0x56B973, 'f6c208740f' '488b01' 'ff5018')         # ... whose first setting calls slot 3
    assert slot(0xA9A868, 3) == 0x261BF0                 # UfoMother401's slot 3:
    at(0x261BF0, '8b815c020000' '898190040000' 'b001c3')  # +0x490 = +0x25c (the notes said +0x490 is never written)
    at(0x25FBB6, 'f30f108e90040000' '498bcf' 'e82afe0000')  # the layout: panel 0x26f9f0(hull +0x490)
    at(0x26FA0A, 'f30f5e057e6d8400')                     # level / 10.0 ...
    at(0x26FA25, 'f30f598360010000')                     # ... x durability, then the rows, max and current, slot 29
    at(0x26FA90, 'f30f118368010000' 'f30f11836c010000' '4883c4205b' '48ffa0e8000000')
    assert slot(0xA9BDA0, 29) == 0x26EBB0                # Panel01's slot 29 hands the level to each battery ...
    at(0x26EC19, '0f28ce' 'e85faeffff')                  # ... 0x269a80, the batteries' own: / 10, x durability, slot 28
    at(0x269B20, 'f30f118368010000' 'f30f11836c010000' '4883c4205b' '48ffa0e0000000')
    for vtable, setup in ((0xA9B8A8, 0x26A560), (0xA9BBF8, 0x26AFC0), (0xA9BA50, 0x26BA30), (0xA9B558, 0x26C6A0)):
        assert slot(vtable, 28) == setup, hex(vtable)   # SmallLaser / Sniper / PlasmaCannon / RapidCannon bullet blocks

    if not os.path.exists(os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')):
        print('skip  edf41_mother401_parts_take_the_hull_level (rows against EDF6): EDF6 not installed')
        return
    import struct
    import sgo
    import make_edf41_objects as m

    def story_rows(game: str) -> list[tuple[float, float]]:
        cfg = sgo.read(rootcpk.Game(game).read('DEFAULTPACKAGE', 'CONFIG.SGO'))[1]['ModeList']
        mode = next(e for e in cfg if e[0] == 'GameMode_Scenario')
        rows = next(x for x in mode if isinstance(x, list) and x and isinstance(x[0], list) and isinstance(x[0][0], list))
        f = lambda v: struct.unpack('<f', v.raw)[0] if isinstance(v, sgo.Float) else float(v)
        return [(f(r[0][0]), f(r[0][1])) for r in rows]
    g41 = os.path.dirname(exe)
    r41, r6 = story_rows(g41), story_rows(rootcpk.DEFAULT_GAME)
    order = [r[1] for r in sgo.plain(sgo.read(rootcpk.Game(g41).read('MISSION', 'MISSIONLIST.SGO'))[1])['table']]
    with open(os.path.join(ROOT, 'edf41port', 'missions.json'), encoding='utf-8') as fh:
        assert [x['id'] for x in json.load(fh)['missions']] == order, 'the 4.1 pack is not in 4.1\'s order: its progress differs'
    eater = ('M009', 'M012', 'M018', 'M019', 'M132', 'M142', 'M145', 'M149', 'M201', 'M308', 'M312', 'M412', 'M516', 'M518', 'M602')
    assert len(m.MOTHER401_ROOTS) == len(eater), 'the Earth Eater missions changed'
    for mid in eater:
        t = order.index(mid) / (len(order) - 1)
        for (a41, b41), (a6, b6) in zip(r41, r6):
            l41, l6 = a41 + (b41 - a41) * t, a6 + (b6 - a6) * t
            assert abs(l6 / l41 - 1.0) <= 0.10, (mid, l41, l6)


@test
def edf41_anthill_voxel_body() -> None:
    """4.1's ant nest under EDF6's AntHill (2026-10-13, M115 on the real game): the class builds a voxel-destruction
    body from animation_model[3] and the first hit dereferences it unchecked (EDF+0x4EB8D7 loads this+0x848, 0x6CAF22
    writes through it), so the shipped nest has the block: E517's grid and tuning around Anthill301.mdb's own extent,
    no debris (E517's chunks are metal), the keys of E517's mech look left out, 4.1's own values kept. check_fracture
    refuses the nest 4.1 shipped (no block), a box that is not one, tuning of the wrong length and a debris name the
    archive lacks."""
    import copy
    import edf41_objects as eo
    import make_edf41_objects as m
    import mdb
    import sgo
    rel = 'OBJECT/ANTHILL301.SGO'
    files = {rel: eo.read(rel), 'OBJECT/ANTHILL301.RAB': eo.read('OBJECT/ANTHILL301.RAB')}
    top = sgo.load(data=files[rel])
    assert top['xgs_scene_object_class'] == 'AntHill' and 'AntHill' in m.FRACTURE_CLASSES
    m.check_fracture(top, files, rel)
    block = top['animation_model'][3]
    rab = mdb.rab_read(files['OBJECT/ANTHILL301.RAB'])
    body = mdb.mdb_read(next(f for f in rab.files if f.name.lower() == 'anthill301.mdb').data)
    box = [mdb.rigid_extent(body, ax) for ax in range(3)]
    near = lambda a, b: all(abs(x - y) < 1e-5 for x, y in zip(a, b))   # stored as 4-byte floats, loaded to 6 places
    assert near(block[1], [lo for lo, _ in box]) and near(block[2], [hi for _, hi in box]), 'the box is not the model\'s extent'
    assert block[0] == [16, 16, 16] and len(block[3]) == m.FRACTURE_TUNING + 1 and block[3][-1] == [], block
    # 4.1's own values stay; E517's mech look does not come over; the score adjust both stock nests carry does
    assert top['game_object_durability'] == 1500.0 and top['game_object_kill_counter'] == 'edf41_NestKillCount'
    assert top['game_object_destroy_score_adjust'] == 0.5
    assert not {'ant_hill_material_type', 'lighting', 'material_animation'} & set(top), sorted(top)
    # the refusals
    def refuses(edit, why: str) -> None:
        bad = copy.deepcopy(top)
        edit(bad)
        try:
            m.check_fracture(bad, files, rel)
        except ValueError:
            return
        raise AssertionError(f'check_fracture passed {why}')
    refuses(lambda t: t['animation_model'].pop(3), 'a nest with no block (4.1\'s)')
    refuses(lambda t: t['animation_model'].__setitem__(3, 5), 'a block that is not a list')
    refuses(lambda t: t['animation_model'][3][1].__setitem__(0, 99.0), 'a box with min above max')
    refuses(lambda t: t['animation_model'][3][0].__setitem__(0, 0), 'a grid of zero cells')
    refuses(lambda t: t['animation_model'][3][3].pop(), 'tuning without its debris list')
    refuses(lambda t: t['animation_model'][3][3][-1].append('nowhere.mdb'), 'a debris model the archive lacks')
    # a block of three entries (E615_HORNETNEST_SMALL's form) is the class's own; other classes are not asked
    short = copy.deepcopy(top)
    short['animation_model'][3] = short['animation_model'][3][:3]
    m.check_fracture(short, files, rel)
    other = copy.deepcopy(top)
    other['xgs_scene_object_class'] = 'GiantAnt'
    other['animation_model'].pop(3)
    m.check_fracture(other, files, rel)
    # every shipped SGO of such a class passes
    reg = eo.registry()
    for obj in reg['objects']:
        data = eo.read(obj['object'])
        t = sgo.load(data=data)
        if t.get('xgs_scene_object_class') in m.FRACTURE_CLASSES:
            m.check_fracture(t, {r: eo.read(r) for r in obj['files']}, obj['object'])


@test
def edf41_class_keys() -> None:
    """Every converted object has the top-level keys every stock EDF6 SGO of its class has (the classes read keys by
    name and several dereference a miss unchecked: Hector's weak_point, the AlienTrailer's se_list at EDF+0x30A4D7,
    M015 2026-10-13). make_edf41_objects.class_keys / fill_class_keys / check_class_keys: on stand-in stock data the
    intersection is what all have, a missing key is filled from the nearest-durability stock SGO (only that key; the
    object's own values stay) and then passes, an unfillable class is refused; on the shipped objects (EDF6 installed)
    none lacks one."""
    import make_edf41_objects as m
    import sgo
    import edf41_objects as eo

    def make(**kw: object) -> bytes:
        return sgo.write_depth_first(0x102, kw)

    stock = {'Box': {'OBJECT/A.SGO': sgo.load(data=make(xgs_scene_object_class='Box', game_object_durability=100.0, se=[1], only_a=[2])),
                     'OBJECT/B.SGO': sgo.load(data=make(xgs_scene_object_class='Box', game_object_durability=900.0, se=[7]))}}
    blobs = {'OBJECT/A.SGO': make(xgs_scene_object_class='Box', game_object_durability=100.0, se=[1], only_a=[2]),
             'OBJECT/B.SGO': make(xgs_scene_object_class='Box', game_object_durability=900.0, se=[7])}
    assert m.class_keys(stock, 'Box') == {'xgs_scene_object_class', 'game_object_durability', 'se'}
    assert m.class_keys(stock, 'Nothing') == set()
    mine = make(xgs_scene_object_class='Box', game_object_durability=800.0, own=[5])
    files = {'OBJECT/X.SGO': mine}
    try:
        m.check_class_keys(stock, 'OBJECT/X.SGO', mine)
    except ValueError:
        pass
    else:
        raise AssertionError('check_class_keys passed an object lacking a key every stock SGO has')
    note = m.fill_class_keys(stock, blobs.__getitem__, 'OBJECT/X.SGO', files)
    assert note == {'template': 'OBJECT/B.SGO', 'keys': ['se']}, note    # 800 is nearer 900 than 100
    out = sgo.load(data=files['OBJECT/X.SGO'])
    assert out['se'] == [7] and out['own'] == [5] and 'only_a' not in out, out
    m.check_class_keys(stock, 'OBJECT/X.SGO', files['OBJECT/X.SGO'])
    assert m.fill_class_keys(stock, blobs.__getitem__, 'OBJECT/X.SGO', files) is None
    # the shipped objects
    reg = eo.registry()
    for o in reg['objects']:
        if o['object'].endswith('.SGO'):
            assert 'class_keys_from' not in o or o['class_keys_from']['keys'], o
    if not _e5c_have_game():
        return
    import rootcpk
    games = m.Games(rootcpk.DEFAULT_GAME, rootcpk.DEFAULT_GAME)
    real = games.stock_by_class()
    flagged = set()
    for o in reg['objects']:
        if o['object'].endswith('.SGO'):
            try:
                m.check_class_keys(real, o['object'], eo.read(o['object']))
            except ValueError:
                flagged.add(o['object'].split('/')[-1])
    assert not flagged, sorted(flagged)


@test
def edf41_stale_stock() -> None:
    """make_edf41_objects.stale_stock / script_sgos: an SGO EDF6's archives serve that the 4.1 missions spawn, whose class
    lacks no key every OTHER stock SGO of it has, passes; one that does (a 4.1 leftover EDF6's classes cannot load:
    Transporter401_Formation without se_preset_list crashed MP003 at EDF+0x240AD3, 2026-10-13) is refused unless this
    port replaces it or EDF6's own missions spawn it (the game loads it); a class with a single member needs that proof.
    On the real archives (EDF6 and EDF4.1 installed): every stock SGO the 4.1 missions spawn passes or is replaced."""
    import make_edf41_objects as m
    import edf41_objects as eo

    # the scripts' strings: BVM text is UTF-16, AC / JSON text is UTF-8
    wide = 'x app:/Object/Foo.sgo y'.encode('utf-16le') + b'\0\0' + 'app:/object/Bar_2.SGO'.encode('utf-16le')
    assert m.script_sgos(wide) == {'OBJECT/FOO.SGO', 'OBJECT/BAR_2.SGO'}, m.script_sgos(wide)
    assert m.script_sgos(b'{"o": "app:/Object/Baz.sgo;app:/Object/Qux.sgo"}') == {'OBJECT/BAZ.SGO', 'OBJECT/QUX.SGO'}
    assert m.script_sgos(b'app:/object/a.rab') == set()

    full = {'xgs_scene_object_class': 'Box', 'game_object_durability': 1.0, 'se_preset_list': []}
    stock = {'Box': {'OBJECT/A.SGO': full, 'OBJECT/B.SGO': {k: v for k, v in full.items() if k != 'se_preset_list'}},
             'Solo': {'OBJECT/S.SGO': {'xgs_scene_object_class': 'Solo'}}}
    everything = {'OBJECT/A.SGO', 'OBJECT/B.SGO', 'OBJECT/S.SGO'}
    out = m.stale_stock(stock, everything, set(), set())
    assert len(out) == 2 and out[0].startswith('OBJECT/B.SGO') and "['se_preset_list']" in out[0], out
    assert out[1].startswith('OBJECT/S.SGO') and 'no other stock SGO' in out[1], out
    assert m.stale_stock(stock, everything, {'OBJECT/B.SGO'}, {'OBJECT/S.SGO'}) == [], 'replaced / proven passes'
    assert m.stale_stock(stock, {'OBJECT/A.SGO'}, set(), set()) == [], 'only what the 4.1 missions spawn is tested'
    # keys as its sibling's, but naming a file only 4.1 has (Monster501_King's model archive: EDF+0x1100303, MP004
    # 2026-10-17): refused unless replaced
    king = {**full, 'animation_model': [['app:/object/A_King.rab', 'A_King.mdb'], 'app:/object/A.cas']}
    named = {'Box': {'OBJECT/A.SGO': full, 'OBJECT/K.SGO': king}}
    only41 = {'OBJECT/A_KING.RAB'}.__contains__
    out = m.stale_stock(named, {'OBJECT/K.SGO'}, set(), set(), only41)
    assert len(out) == 1 and out[0].startswith('OBJECT/K.SGO') and "['OBJECT/A_KING.RAB']" in out[0], out
    assert m.stale_stock(named, {'OBJECT/K.SGO'}, set(), set()) == [], 'without lacks only the keys are tested'
    assert m.stale_stock(named, {'OBJECT/K.SGO'}, {'OBJECT/K.SGO'}, set(), only41) == [], 'replaced passes'

    # animation_model without a MAB block where every sibling has one (Transporter401_Formation: boosts looked its
    # locators up through the null MAB, EDF+0x6BAE06, MP003 2026-10-13) is refused unless MAB_LESS says why it works
    import sgo
    mab = 'raw:4d414200' + '00' * 8
    sib = {'xgs_scene_object_class': 'Box', 'animation_model': [['a.mrab', 'a.mdb'], 'a.cas', mab]}
    stock_mab = {'Box': {'OBJECT/A.SGO': sib}}
    plain = sgo.write_depth_first(0x102, {'xgs_scene_object_class': 'Box', 'animation_model': [['b.rab', 'b.mdb'], 'b.cas', 0]})
    with_mab = sgo.write_depth_first(0x102, {'xgs_scene_object_class': 'Box', 'animation_model': [['b.rab', 'b.mdb'], 'b.cas', mab]})
    m.check_model_mab(stock_mab, 'OBJECT/B.SGO', with_mab)
    m.check_model_mab({}, 'OBJECT/B.SGO', plain)     # no sibling: nothing to compare
    try:
        m.check_model_mab(stock_mab, 'OBJECT/B.SGO', plain)
    except ValueError:
        pass
    else:
        raise AssertionError('check_model_mab passed an animation_model without a MAB block')
    m.check_model_mab(stock_mab, 'OBJECT/TRANSPORTER401_FORMATION.SGO', plain)   # MAB_LESS: explained

    # a classic SGO of a class whose stock SGOs are all DSGO is refused (Vehicle501_FortressRobo: slot 6 read the root
    # through the dictionary API a classic SGO leaves empty, EDF+0xD74D9, MP004 2026-10-13); as_dsgo keeps the values
    import dsgo
    classic = sgo.write_depth_first(0x102, {'xgs_scene_object_class': 'Box', 'n': [1, 2], 's': 'x', 'f': 1.5})
    m.check_class_format(set(), 'OBJECT/B.SGO', classic)
    try:
        m.check_class_format({'Box'}, 'OBJECT/B.SGO', classic)
    except ValueError:
        pass
    else:
        raise AssertionError('check_class_format passed a classic SGO of a DSGO-only class')
    converted = m.as_dsgo(classic)
    assert converted[:4] == b'DSGO' and m.as_dsgo(converted) == converted
    m.check_class_format({'Box'}, 'OBJECT/B.SGO', converted)
    back = dsgo.to_py(dsgo.parse(converted).root)
    assert back == {'xgs_scene_object_class': 'Box', 'n': [1.0, 2.0], 's': 'x', 'f': 1.5}, back

    # the shipped replacement: registered as replacing the stock file, built from 4.1's SGO under EDF6's class
    reg = eo.registry()
    done = {o['object']: o for o in reg['objects']}
    for root, why in m.STOCK_OVERRIDES.items():
        o = done.get('OBJECT/' + root)
        assert o is not None and o.get('replaces_stock') == why, root
    formation = sgo.load(data=eo.read('OBJECT/TRANSPORTER401_FORMATION.SGO'))
    assert formation['boosts'] == [] and not m.has_mab(formation), 'MAB_LESS is only sound with no booster locators'
    assert 'OBJECT/TRANSPORTER401.CAS' in done['OBJECT/TRANSPORTER401_FORMATION.SGO']['files']
    # the completed stock SGO: its own file stays EDF6's, only the model archive it names is shipped
    completed = {o['object']: o for o in reg['stock_completions']}
    for root, why in m.STOCK_COMPLETIONS.items():
        o = completed.get('OBJECT/' + root)
        assert o is not None and o['completes_stock'] == why and o['object'] not in o['files'], root
    assert completed['OBJECT/MONSTER501_KING.SGO']['files'] == ['OBJECT/MONSTER501_KING.RAB']
    import mdb
    import rabpool
    try:   # five of its textures are EDF6's own (members_from): it is never put together without the game
        eo.read('OBJECT/MONSTER501_KING.RAB')
    except rabpool.Unsourced:
        pass
    else:
        raise AssertionError("the King's model archive was put together without the player's EDF6")
    if not _e5c_have_game():
        print('skip  edf41_stale_stock (the King model, real archives): needs EDF6 installed')
        return
    import rootcpk
    king_rab = eo.read('OBJECT/MONSTER501_KING.RAB', eo.game_source(rootcpk.DEFAULT_GAME))
    king = mdb.rab_read(king_rab)
    assert any(f.name == 'Monster501_King.mdb' for f in king.files), [f.name for f in king.files]
    if not _edf41_root():
        print('skip  edf41_stale_stock (real archives): needs EDF6 and EDF4.1 installed')
        return
    games = m.Games(_edf41_root(), rootcpk.DEFAULT_GAME)
    spawned = m.spawned_objects(games)
    assert len(spawned) > 100, len(spawned)     # the scan found the missions' objects, not nothing
    assert 'OBJECT/TRANSPORTER401_FORMATION.SGO' in spawned
    left = [s for s in m.stale_stock(games.stock_by_class(), spawned, set(done) | set(completed), m.proven_stock(games),
                                     games.lacks)
            if s.split(':')[0] not in m.STOCK_EXEMPT]
    assert not left, left
    # the King's model is the skeleton EDF6's stock King CAS animates: every bone it names but the rig nodes, the
    # same two its sibling's model leaves to the CAS (Monster501_Rig / Scene_Root)
    import cas_pose
    bones = set(m.mdb_bones(king_rab, 'Monster501_King.mdb'))
    named = set(cas_pose.CasPose(games.read6('OBJECT/MONSTER501_KING.CAS')).names)
    assert named - bones == {'Monster501_King_Rig', 'Scene_Root'}, sorted(named - bones)
    real = games.stock_by_class()
    for o in reg['objects']:   # every shipped SGO has the MAB block its class's stock SGOs have, or MAB_LESS says why not
        if o['object'].endswith('.SGO'):
            m.check_model_mab(real, o['object'], eo.read(o['object']))
    dsgo_only = games.dsgo_only_classes()
    assert 'Vehicle501_FortressRobo' in dsgo_only, sorted(dsgo_only)[:5]
    for o in reg['objects']:   # no shipped classic SGO sits in a class EDF6 only ships as DSGO
        if o['object'].endswith('.SGO'):
            m.check_class_format(dsgo_only, o['object'], eo.read(o['object']))
    # the formation's CAS has the states the class looks up by name (EDF+0x5E4A6A..0x5E4AB0)
    import cas_graft
    import cas_legacy as cl
    cas = eo.read('OBJECT/TRANSPORTER401.CAS')
    main = [[cas_graft._name(cas, mc.at) for mc in ms] for at, ms in cl.cas_layout(cas).tree.anmgroups
            if cas_graft._name(cas, at) == 'main']
    assert main and {'hover_start', 'hover_end', 'fly'} <= set(main[0]), main
    # and without the replacement the formation is what this test exists to catch
    bad = m.stale_stock(games.stock_by_class(), spawned, set(), m.proven_stock(games), games.lacks)
    assert any(s.startswith('OBJECT/TRANSPORTER401_FORMATION.SGO') and 'se_preset_list' in s for s in bad), bad
    assert any(s.startswith('OBJECT/MONSTER501_KING.SGO') and 'OBJECT/MONSTER501_KING.RAB' in s for s in bad), bad


@test
def edf41_objects_install() -> None:
    """tools/edf41_objects.py on a stand-in game: install writes every object's files as the ledger owner edf41 and
    remove deletes them; a file of the same name another mod put in Mods is never written over, every object needing
    it is left out (said why), the others installed. An object whose model archive takes members from the player's
    EDF6 (members_from: the King) is left out on a game without them (the stand-in has no Root.cpk), said why, and
    on one whose archive holds other bytes there."""
    import edf41_objects as eo
    import ledger
    reg = eo.registry()
    from_game = {o['object'] for o in eo.entries(reg) if eo.stock_members(o)}
    assert from_game == {'OBJECT/MONSTER501_KING.SGO'}, from_game
    theirs_only = {rel for o in eo.entries(reg) if o['object'] in from_game for rel in o['files']}
    with tempfile.TemporaryDirectory(prefix='edf6vc-e41o-') as game:
        os.makedirs(os.path.join(game, 'Mods'))
        files, skipped = eo.build(game)
        assert {obj for obj, _why in skipped} == from_game and all('Root.cpk' in why for _o, why in skipped), skipped
        assert set(files) == set(reg['files']) - theirs_only, sorted(set(reg['files']) ^ set(files))[:5]
        eo.install(game, files)
        led = ledger.Ledger(game)
        assert {ledger.key(r) for r in files} == set(led.owned_by(eo.OWNER))
        assert all(os.path.isfile(led.disk(r)) for r in files)
        assert eo.install(game, files) == [], 'a second install rewrote files it already holds'
        deleted, kept = eo.remove(game)
        assert not kept and not any(os.path.exists(led.disk(r)) for r in files), 'remove left files'
        anthill = next(o for o in reg['objects'] if o['object'] == 'OBJECT/ANTHILL301.SGO')
        theirs = anthill['files'][1]
        modfiles.atomic_write(os.path.join(game, 'Mods', *theirs.split('/')), b'another mod')
        files, skipped = eo.build(game)
        users = {o['object'] for o in reg['objects'] if theirs in o['files']}
        assert {obj for obj, _why in skipped} == users | from_game and theirs not in files, skipped
        eo.install(game, files)
        with open(os.path.join(game, 'Mods', *theirs.split('/')), 'rb') as f:
            assert f.read() == b'another mod', "another mod's file was written over"
    # a file another of our tools ships with the same bytes (the Blacker No.4.1 model, EDF5 weapons) is shared;
    # rewritten by that tool with other bytes, it is not written over: the objects needing it wait
    shared = next(iter(reg.get('shared', {})), None)
    if shared:
        with tempfile.TemporaryDirectory(prefix='edf6vc-e41s-') as game:
            os.makedirs(os.path.join(game, 'Mods'))
            with open(eo.bundled(shared), 'rb') as f:
                ledger.Ledger(game).put('calls', shared, f.read())
            files, skipped = eo.build(game)
            assert {obj for obj, _why in skipped} == from_game and shared in files, skipped
            eo.install(game, files)
            assert set(ledger.Ledger(game).owners(shared)) == {'calls', eo.OWNER}
            ledger.Ledger(game).put('calls', shared, b'calls changed it')
            files, skipped = eo.build(game)
            users = {o['object'] for o in reg['objects'] if shared in o['files']}
            assert {obj for obj, _why in skipped} == users | from_game and shared not in files, skipped
    # the EDF6 source itself: there as generated -> no problem; an archive missing, or the member's bytes other -> why
    king = next(o for o in eo.entries(reg) if o['object'] in from_game)
    sourced = {sha: src for srcs in eo.stock_members(king).values() for sha, src in srcs.items()}

    class FakeEdf6:
        def __init__(self, members: dict | None) -> None:
            self.members = members

        def missing(self, archive: str, path: str) -> str | None:
            return None if self.members is not None else f'没有 {archive}'

        def read(self, archive: str, path: str) -> bytes:
            import mdb
            assert self.members is not None
            return mdb.rab_write(mdb.Rab(1, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], self.members[path]))

    import mdb as _mdb
    stock = eo._Stock('unused')
    stock._e6 = FakeEdf6(None)
    assert stock.problems(sourced) == ['没有 Root.cpk'], stock.problems(sourced)
    wrong = {src[1]: [_mdb.RabFile(f'm{i}', 2, 0, b'x') for i in range(max(s[2] for s in sourced.values()) + 1)]
             for src in sourced.values()}
    for src in sourced.values():
        wrong[src[1]][src[2]] = _mdb.RabFile(src[3], 2, 0, b'not the generated bytes')
    stock = eo._Stock('unused')
    stock._e6 = FakeEdf6(wrong)
    gone = stock.problems(sourced)
    assert len(gone) == len(sourced) and all('不是生成时的那份' in w for w in gone), gone


@test
def edf41_outfits_shipped() -> None:
    """edf41port/outfits (tools/make_edf41_outfits.py, docs/edf41-outfits.md): the Ranger and Air Raider outfits ship (the
    Wing Diver and Fencer too, while the pipeline takes them); every class's files are shipped as registered and nothing
    else is; new names only (OBJECT/EDF41_P_*, MENUOBJECT/EDF41_P_*: no stock file replaced); each archive's models are
    EDF6's (0x20) and sound (pylib/outfit41.py check_archive: re-serialise, textures before the model, inverse binds,
    weights summing to 1, indices inside the skeleton) on at most outfit41.MAX_BONES bones; each battle SGO is the
    class's (CONFIG.SGO SoldierInit order) and names its archive, body and LODs, each menu SGO its archive and body; the
    report says every shared joint ended within 7 cm of EDF6's."""
    import hashlib
    import edf41_outfits as eo
    import mdb
    import outfit41
    import rabpool
    import sgo
    reg = eo.registry()
    classes = {c['class']: c for c in reg['classes']}
    assert {'ranger', 'airraider'} <= set(classes), f'Ranger and Air Raider outfits missing: {sorted(classes)}'
    listed = {rel for c in reg['classes'] for rel in c['files']}
    assert listed == set(reg['files']), 'the registry lists files no class needs, or the reverse'
    shipped = {os.path.relpath(os.path.join(d, f), eo.FILES).replace(os.sep, '/') for d, _, fs in os.walk(eo.FILES) for f in fs}
    own = {s for rel in listed for s in rabpool.stored(rel)}
    for s in sorted(own):
        if s.endswith(rabpool.MANIFEST):
            with open(os.path.join(eo.FILES, *s.split('/')), encoding='utf-8') as f:
                own |= {f'{rabpool.MEMBERS}/{row[-1]}' for row in json.load(f)['files']}
    assert shipped == own, f'shipped but unlisted: {sorted(shipped - own)[:5]}; listed, not shipped: {sorted(own - shipped)[:5]}'
    soldier = {0: 'AssultSoldier', 1: 'PaleWing', 2: 'Engineer', 3: 'HeavyArmor'}
    for key, c in classes.items():
        spec = outfit41.SPECS[key]
        assert c['soldier_class'] == spec.soldier_class and c['battle'] == f'OBJECT/{spec.out}.SGO' and \
            c['menu'] == f'MENUOBJECT/{spec.out}.SGO', key
        assert all(r.split('/', 1)[1].startswith('EDF41_P_') for r in c['files']), f'{key}: a file under a stock name'
        data = {rel: eo.read(rel) for rel in c['files']}
        for rel, b in data.items():
            assert hashlib.sha256(b).hexdigest() == reg['files'][rel], rel
        arch = f'OBJECT/{spec.out}.MRAB'
        models = [f for f in mdb.rab_read(data[arch]).files if f.name.lower().endswith('.mdb')]
        assert len(models) == 3, f'{key}: body and two LODs'
        faults = outfit41.check_archive(data[arch], [f.name for f in models])
        assert not faults, (key, faults[:5])
        for f in models:
            md = mdb.mdb_read(f.data)
            assert md.version == 0x20 and len(md.bones) <= outfit41.MAX_BONES, (key, f.name, len(md.bones))
        battle = sgo.load(data=data[c['battle']])
        assert battle['xgs_scene_object_class'] == soldier[spec.soldier_class], (key, battle['xgs_scene_object_class'])
        archive, body, lods = outfit41.model_entry(battle['animation_model'][0])
        assert outfit41.rel_of(archive) == arch and [body] + [n for n, _ in lods] == [f.name for f in models], key
        menu = sgo.load(data=data[c['menu']])
        assert outfit41.rel_of(menu['animation_model'][0][0]) == arch and menu['animation_model'][0][1] == body, key
        assert c['report']['joint_error_max'] < 0.07, (key, c['report']['joint_error_worst'])


@test
def edf41_outfit_item_label() -> None:
    """The installer names the outfit popup's item by the text the plugin shows (src/hudtext.inc outfit41Item, its
    Simplified Chinese column), taken from tools/edf41_outfits.py ITEM_ZH, never a text of its own (it once said
    「EDF4.1」 while the item reads 「绝望阴影再袭」)."""
    import edf41_outfits as eo
    line = src('src/hudtext.inc').split('HUDTEXT(outfit41Item,', 1)[1].split('\n', 1)[0]
    texts = re.findall(r'L"((?:[^"\\]|\\.)*)"', line)
    assert len(texts) == 4 and texts[1] == eo.ITEM_ZH, (texts, eo.ITEM_ZH)
    installer = src('tools/installer.py')
    assert '{edf41_outfits.ITEM_ZH}' in installer and '选「EDF4.1」' not in installer, 'the installer names the item itself'


@test
def edf41_outfits_pipeline_math() -> None:
    """pylib/outfit41.py's turns (no game): align turns a set of offsets onto their rotated copies exactly (Kabsch), a
    line of offsets (the forearm) by the shortest turn of their direction, shortest_turn handles the opposite direction,
    and the pairing rules every class shares map 4.1's otherFinger* to middle* and kubi to head."""
    import math
    import numpy as np
    import outfit41
    a = math.radians(25.0)
    q = np.array([[math.cos(a), math.sin(a), 0.0], [-math.sin(a), math.cos(a), 0.0], [0.0, 0.0, 1.0]])
    us = [np.array(v) for v in ((0.1, 0.0, 0.02), (0.0, 0.08, -0.03), (0.05, 0.05, 0.05))]
    got = outfit41.align(us, [u @ q for u in us])
    assert np.allclose(got, q, atol=1e-9) and abs(outfit41.turn_angle(got) - 25.0) < 1e-6
    line = [np.array((0.1, 0.0, 0.0)), np.array((0.2, 0.0, 0.0))]
    got = outfit41.align(line, [u @ q for u in line])
    assert np.allclose(np.array((1.0, 0.0, 0.0)) @ got, np.array((1.0, 0.0, 0.0)) @ q, atol=1e-9)
    flip = outfit41.shortest_turn(np.array((0.0, 1.0, 0.0)), np.array((0.0, -1.0, 0.0)))
    assert np.allclose(np.array((0.0, 1.0, 0.0)) @ flip, (0.0, -1.0, 0.0), atol=1e-9) and np.allclose(flip @ flip.T, np.eye(3))
    rules = dict(outfit41.COMMON)
    assert re.sub(r'otherFinger(\d)_([lr])', rules[r'otherFinger(\d)_([lr])'], 'otherFinger2_l') == 'middle2_l'
    assert rules['kubi'] == 'head'


@test
def edf41_outfits_reproducible() -> None:
    """With EDF4.1 and EDF6 on this machine: tools/make_edf41_outfits.py builds today exactly the files shipped (the
    pipeline is deterministic and the committed files are current). Skipped without either game (CI)."""
    import gamedir
    import edf41_outfits as eo
    import make_edf41_outfits as m
    import rootcpk
    edf41 = gamedir.find_other(gamedir.EDF41)
    if not edf41 or not os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        print('      (skipped: no EDF4.1 or EDF6 here)')
        return
    reg = eo.registry()
    data, _files = m.build(edf41, rootcpk.DEFAULT_GAME)
    assert data['files'] == reg['files'], 'rerun tools/make_edf41_outfits.py: the shipped outfits are not what it builds'
    assert [c['class'] for c in data['classes']] == [c['class'] for c in reg['classes']]


@test
def edf41_outfits_install() -> None:
    """tools/edf41_outfits.py on a stand-in game: every class's files written as the ledger owner edf41outfit, a second
    install writes nothing, remove deletes them; a file another mod put at one of a class's names is never written over,
    that class is left out (said why), the others installed. The installer writes them after the 4.1 objects and its
    uninstall removes them; the release bundles the registry and the files; the plugin installs the hooks and the online
    preload hands its member to them."""
    import edf41_outfits as eo
    assert eo.OWNER in ledger.OWNERS
    reg = eo.registry()
    with tempfile.TemporaryDirectory(prefix='edf6vc-e41f-') as game:
        os.makedirs(os.path.join(game, 'Mods'))
        files, skipped = eo.build(game)
        assert not skipped and set(files) == set(reg['files'])
        eo.install(game, files)
        led = ledger.Ledger(game)
        assert {ledger.key(r) for r in files} == set(led.owned_by(eo.OWNER))
        assert eo.install(game, files) == [], 'a second install rewrote files it already holds'
        _deleted, kept = eo.remove(game)
        assert not kept and not any(os.path.exists(led.disk(r)) for r in files), 'remove left files'
        theirs = reg['classes'][0]['files'][0]
        modfiles.atomic_write(os.path.join(game, 'Mods', *theirs.split('/')), b'another mod')
        files, skipped = eo.build(game)
        assert [cls for cls, _why in skipped] == [reg['classes'][0]['class']] and theirs not in files, skipped
        assert set(files) == {r for c in reg['classes'][1:] for r in c['files']}
        eo.install(game, files)
        with open(os.path.join(game, 'Mods', *theirs.split('/')), 'rb') as f:
            assert f.read() == b'another mod', "another mod's file was written over"
    inst, rel = src('tools/installer.py'), src('tools/build_release.py')
    assert inst.index('edf41_objects.install(game, objects41)') < inst.index('edf41_outfits.install(game, outfits41)')
    assert 'edf41_outfits.remove' in inst.split('def uninstall(game: str)', 1)[1]
    assert '"edf41port", "outfits")}{seps}edf41port/outfits' in rel and '"edf41port", "outfits.json")}{seps}edf41port' in rel
    plugin, mission = src('src/plugin.cpp'), src('src/mission.cpp')
    assert 'InstallOutfit41()' in plugin and 'Outfit41SessionPreload(sessionOrig,' in mission


@test
def edf41_dlc_objects() -> None:
    """EDF4.1's DLC objects (make_edf41_objects.DLC_ROOTS: what the two mission packs' listed missions spawn, mp01:/ and
    mp02:/ in 4.1, under OBJECT/EDF41MP01/ and OBJECT/EDF41MP02/ here, pylib/edf41dlc.py): every root registered under its
    pack's folder and nothing else there, no shipped file naming a DLC mount (mp0N:/ would resolve to nothing in EDF6), no
    pack object left out (DLC_EXCLUDED is empty: the 401 mothership of M809 / MP503 is the plugin's class), and every
    file a DLC object names is shipped by the registry or (games installed) EDF6's own, with the models it names inside
    the archive the game will load (DragonSmall401.mrab is EDF5's frog dragon in EDF6: the DLC's dragons ship 4.1's, moved
    into the pack's folder). With EDF4.1 installed, DLC_ROOTS is what the packs' mission scripts name."""
    import edf41dlc
    import rootcpk
    import edf41_objects as eo
    import make_edf41_objects as m
    import mdb
    import sgo
    reg = eo.registry()
    objects = {o['object']: o for o in reg['objects']}
    folders = {f for _, f in edf41dlc.PACKS.values()}
    for root in m.DLC_ROOTS:
        assert root.split('/')[0] in folders, root
        assert 'OBJECT/' + root in objects, f'DLC root {root} is not registered'
    pack_objects = {o for o in objects if m.pack_folder(o)}
    assert pack_objects == {'OBJECT/' + r for r in m.DLC_ROOTS}, sorted(pack_objects ^ {'OBJECT/' + r for r in m.DLC_ROOTS})
    assert not m.DLC_EXCLUDED, m.DLC_EXCLUDED
    assert {'EDF41MP02/UFOMOTHER401_DLC_M809.SGO', 'EDF41MP02/UFOMOTHER401_DLC_MP503.SGO'} <= set(m.DLC_ROOTS)
    assert not set(m.DLC_EXCLUDED) & {o.split('/', 1)[1] for o in objects}
    mount = re.compile(rb'(?i)mp0\d:/|m\x00p\x000\x00\d\x00:\x00/\x00')
    for rel in reg['files']:
        data = _edf41_object(rel)
        assert data is None or not mount.search(data), f'{rel} names a DLC mount'
    # the references of each DLC object
    games = m.Games(_edf41_root(), rootcpk.DEFAULT_GAME) if _e5c_have_game() and _edf41_root() else None
    for root in m.DLC_ROOTS:
        rel = 'OBJECT/' + root
        top = sgo.load(data=eo.read(rel))
        refs = {m.rel_of(s) for s in m.strings(top) if s.lower().startswith('app:/') and os.path.splitext(s)[1]}
        for r in sorted(refs):
            if r in reg['files']:
                assert r in objects[rel]['files'] or r.endswith('.SGO'), f'{rel} names {r}, not among its files'
                continue
            assert not m.pack_folder(r), f'{rel} names {r} in a pack folder that is not shipped'
            if games is None:
                continue
            ok = (r in games.have6 or (r.endswith('.ACB') and games.sound(r))
                  or not games.has4(r))   # a file 4.1 itself lacks (GiantBee_light.mrab ...): the game runs without it
            assert ok, f'{rel} names {r}: neither shipped nor EDF6\'s'
        if games is None:
            continue
        am = top.get('animation_model')
        for e in m.model_entries(am[0] if isinstance(am, list) and am else []):
            r = m.rel_of(e[0])
            if r in reg['files']:
                data = eo.read(r)
            elif r in games.have6:
                data = games.read6(r)
            else:
                assert not games.has4(r), f'{rel}: model archive {r} is nowhere'
                continue
            names = {f.name.lower() for f in mdb.rab_read(data).files}
            want = [e[1]] + [lod[0] for lod in (e[2] if len(e) > 2 and isinstance(e[2], list) else [])]
            assert all(str(n).lower() in names for n in want), f'{rel}: {r} as the game loads it lacks {want}'
    if games is None:
        print('skip  edf41_dlc_objects (real archives): needs EDF6 and EDF4.1 with its DLC installed')
        return
    found = m.dlc_mission_roots(games)
    assert {r: n for r, n in found.items() if r not in m.DLC_EXCLUDED} == m.DLC_ROOTS, found
    assert set(m.DLC_EXCLUDED) <= set(found), found


@test
def edf41_dlc_insects() -> None:
    """The DLC's insects and nests on EDF6's classes (make_edf41_objects.DLC_INSECT_PATCHES, NEST_41_KEYS): Nephila02L's
    thread gravity and spread as 4.1's setup defaults them (3.0, 0.1) where EDF6's would be 0.5, 03L's own four kept;
    every Nephila and the queen carry the plugin keys src/edf41insects.cpp reads (4.1's random_speed, 4.1's needle base
    0.4), and only they; ragdoll_contact as EDF6's own copies of these objects; the nests count into the port's nests
    counter (edf41_NestKillCount, pylib/edf41kills.py) with 4.1's score. The plugin's key names and constants are the converter's."""
    import edf41_objects as eo
    import make_edf41_objects as m
    import sgo
    reg = eo.registry()
    objects = {o['object'] for o in reg['objects']}

    def top(root: str) -> dict:
        return {k: sgo.plain(v) for k, v in sgo.load(data=eo.read('OBJECT/' + root)).items()}

    assert set(m.DLC_INSECT_PATCHES) <= set(m.DLC_ROOTS) and set(m.DLC_INSECT_PATCHES) <= set(m.PATCHES)
    by_class: dict[str, list[str]] = {}
    for root in m.DLC_ROOTS:
        assert 'OBJECT/' + root in objects, root
        by_class.setdefault(str(top(root)['xgs_scene_object_class']), []).append(root)
    # every DLC object of the audited classes is handled (SpiderNet: nothing differs)
    for cls in ('GiantAnt', 'GiantBee', 'Nephila', 'BigNest'):
        assert set(by_class[cls]) <= set(m.DLC_INSECT_PATCHES), (cls, by_class[cls])
    for root in by_class['Nephila']:
        t = top(root)
        assert t['edf41_random_speed'] == [0.01], root
        ap = t['nephila_ex_attack_param']
        assert len(ap) == 4 and ap[2:] == ([3.0, 0.1] if 'NEPHILA02L' in root else [0.5, 0.025]), (root, ap)
    for root in by_class['GiantBee']:
        t = top(root)
        assert t['insectbase_Type'] == 1 and t['edf41_needle_base'] == [0.4], root
        assert t['ragdoll_contact'][1] == 8000.0 and t['game_object_ai_route_update_distance'] == 160.0, root
    for root in by_class['GiantAnt']:
        want = 12000.0 if 'SB' in root else 8000.0
        assert top(root)['ragdoll_contact'][1] == want, root
    for root in by_class['AntHill'] + by_class['BigNest']:
        t = top(root)
        assert t['game_object_kill_counter'] == 'edf41_NestKillCount', root
        assert t.get('game_object_destroy_score_adjust', 1.0) == 1.0, root
    # the plugin keys appear on no other shipped object
    for o in reg['objects']:
        rel = o['object']
        if rel.endswith('.SGO') and rel.split('/', 1)[1] not in m.DLC_INSECT_PATCHES:
            data = eo.read(rel)
            assert 'edf41_random_speed'.encode('utf-16le') not in data and b'edf41_random_speed' not in data, rel
            assert 'edf41_needle_base'.encode('utf-16le') not in data and b'edf41_needle_base' not in data, rel
    with open(os.path.join(ROOT, 'src', 'edf41insects.cpp'), encoding='utf-8') as f:
        cpp = f.read()
    assert 'L"edf41_random_speed"' in cpp and 'L"edf41_needle_base"' in cpp
    assert m.DLC_NEEDLE_BASE == 0.4 and m.DLC_NEPHILA_GRAVITY == 3.0 and m.DLC_NEPHILA_SPREAD == 0.1
    for site in ('0x422830', '0x41B66B', '0x41EBA0', '0x4E3805', '0x27C362', '0x27C3AF', '0x10D0', '0x1764'):
        assert site in cpp, site
    with open(os.path.join(ROOT, 'src', 'plugin.cpp'), encoding='utf-8') as f:
        assert 'InstallEdf41Insects();' in f.read()
    with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
        assert 'src/edf41insects.cpp' in f.read()


@test
def edf41_class_names() -> None:
    """make_edf41_objects.check_class_names: an object whose class looks a name up unchecked (CLASS_NAMES: a CAS state or
    variable, a MAB locator, a ragdoll body, the bone wakeup_node names) has it in its files. DLC M805 2026-10-14: EDF6's
    DragonSmall hands the ragdoll bodies koshi and mune to 0x4A8570 on a hit and dereferences a miss (EDF+0x6E42EB); 4.1's
    small-dragon ragdolls have koshi, spine1, spine2, no mune. The DLC's three _HS dragons ship 4.1's own model, CAS, MAB
    and ragdoll with edf41_ragdoll_bodies = 4.1's own list for that reaction (koshi, spine1, spine2: EDF41.exe 0x1fb06b..
    0x1fb0eb), which src/edf41smalldragon.cpp hands the class instead (its key name, limits and hooked site agree with the
    converter's). The shipped DragonSmall objects (DragonBig401 and the three) pass; the same dragon without the key (what
    crashed), a key naming a body the ragdoll lacks or too many names, a wakeup_node no bone has, a ragdoll body named only
    as a prefix, and a missing state are refused. capture_* are needed only by an object whose MAB raises 捕食判定開始
    (DragonBig401's does not: it never bites)."""
    import edf41_objects as eo
    import make_edf41_objects as m
    import sgo
    assert m.ragdoll_body(b'xRagDollProxys.mune\r\x02', 'mune')          # a 2014 packfile: a tag byte after the name
    assert m.ragdoll_body(b'RagDollProxys.mune\0RagDo', 'mune')          # EDF6's own: NUL-terminated
    assert not m.ragdoll_body(b'RagDollProxys.mune2\0', 'mune')          # a longer name is another body
    assert not m.ragdoll_body(b'RagDollProxys.koshi\0', 'mune')
    plugin = src('src/edf41smalldragon.cpp')   # the plugin reads the key the converter writes, with the same limits
    spec = m.CLASS_NAMES['DragonSmall']
    assert f'L"{spec.bodies_key}"' in plugin, spec.bodies_key
    assert f'kMostBodies={m.SMALL_DRAGON_MOST_BODIES};' in plugin and f'kLongestName={m.SMALL_DRAGON_LONGEST_BODY};' in plugin
    assert 'kSite=0x33962C;' in plugin and 'kPartialRagdoll=0x4A8570;' in plugin
    assert re.search(r'^\s*InstallEdf41SmallDragon\(\);', src('src/plugin.cpp'), re.M), 'the hook is not installed'
    assert 'src/edf41smalldragon.cpp' in src('CMakeLists.txt')
    assert 'mune' not in m.SMALL_DRAGON_BODIES and len(m.SMALL_DRAGON_BODIES) <= m.SMALL_DRAGON_MOST_BODIES
    reg = eo.registry()
    dragons = [o for o in reg['objects'] if o['class'] == 'DragonSmall']
    hs = ('OBJECT/EDF41MP01/DRAGONSMALL401_HS.SGO', 'OBJECT/EDF41MP01/REDSMALLDRAGON_HS.SGO',
          'OBJECT/EDF41MP01/YERROWSMALLDRAGON_HS.SGO')
    assert {o['object'] for o in dragons} >= {'OBJECT/DRAGONBIG401.SGO', *hs}, dragons
    models = {hs[0]: 'SmallDragon401Green.mdb', hs[1]: 'RedSmallDragon.mdb', hs[2]: 'YerrowSmallDragon.mdb'}
    import cas_graft
    for rel in hs:   # 4.1's look and the plugin's key; the random reactions name states of the CAS shipped with them
        top = sgo.load(data=eo.read(rel))
        assert top.get(spec.bodies_key) == list(m.SMALL_DRAGON_BODIES), (rel, top.get(spec.bodies_key))
        assert m.model_entries(top['animation_model'][0])[0][1] == models[rel], top['animation_model'][0]
        assert 'smalldragon' in top['ragdoll'][0].lower() and 'frogdragon' not in top['ragdoll'][0].lower(), top['ragdoll']
        cas = eo.read(m.rel_of(top['animation_model'][1]))
        reactions = set(m.strings(top['RandomReactionTable']))
        assert reactions and reactions <= cas_graft.states(cas)['random_reaction'], (rel, reactions)
    if not _e5c_have_game() or not _edf41_root():
        print('skip  edf41_class_names (real archives): needs EDF6 and EDF4.1 installed')
        return
    import rootcpk
    games = m.Games(_edf41_root(), rootcpk.DEFAULT_GAME)
    for o in reg['objects']:     # every shipped object of a class CLASS_NAMES knows
        if o['object'].endswith('.SGO'):
            m.check_class_names(games, {r: eo.read(r) for r in o['files']}, o['object'])

    def refused(files: dict[str, bytes], rel: str, why: str) -> None:
        try:
            m.check_class_names(games, files, rel)
        except ValueError as e:
            assert why in str(e), e
            return
        raise AssertionError(f'check_class_names passed {rel} without {why}')

    rel = hs[0]
    shipped = {r: eo.read(r) for r in next(o for o in reg['objects'] if o['object'] == rel)['files']}
    assert 'OBJECT/RAGDOLL_SMALLDRAGON401_HAS_HKA.SHKT' in shipped   # 4.1's ragdoll: koshi, spine1, spine2, no mune

    def variant(change: 'Callable[[dict], None]') -> dict[str, bytes]:
        version, members = sgo.read(shipped[rel])
        change(members)
        return {**shipped, rel: sgo.write_depth_first(version, members)}

    refused(variant(lambda mem: mem.pop(spec.bodies_key)), rel, 'ragdoll body mune')    # the crash, without the plugin's key
    refused(variant(lambda mem: mem.__setitem__(spec.bodies_key, ['koshi', 'mune'])), rel, 'ragdoll body mune')
    refused(variant(lambda mem: mem.__setitem__(spec.bodies_key, ['koshi'] * (m.SMALL_DRAGON_MOST_BODIES + 1))), rel,
            spec.bodies_key)
    refused(variant(lambda mem: mem.__setitem__('wakeup_node', 'mune')), rel, "bone 'mune' (wakeup_node)")
    version, members = sgo.read(eo.read(rel))
    members['xgs_scene_object_class'] = 'DragonSmall'
    cas = 'OBJECT/EDF41MP01/BARE.CAS'                       # a CAS with none of the class's states
    members['animation_model'] = [members['animation_model'][0], 'app:/object/EDF41MP01/bare.cas',
                                  *members['animation_model'][2:]]
    import cas_graft
    bare = cas_graft.hollow(games.read6('OBJECT/DRAGONSMALL401.CAS'))
    names = set().union(*cas_graft.states(bare).values())
    if 'wakeup' in names:   # hollow keeps the states: rename one away to have a missing state
        bare = cas_graft.rename_state(bare, next(p for p, s in cas_graft.states(bare).items() if 'wakeup' in s),
                                      'wakeup', 'wakeup_gone')
    refused({rel: sgo.write_depth_first(version, members), cas: bare}, rel, 'state wakeup')


@test
def edf41_story_small_dragons() -> None:
    """4.1's small dragons have distinct EDF41_ root SGOs: stock DragonSmall401 is EDF5/6's frog dragon. Original
    and old EDF41/ script paths converge on the prefixed name; only model/CAS dependencies stay in EDF41/. The old
    namespaced SGO remains a byte-identical compatibility file of each object, not another catalog object. Check
    mapping, generator roots, every committed BVM, the shipped objects, and (with both games) a real rebuild."""
    import glob
    import bvm41
    import edf41_objects as eo
    import edf41dlc
    import make_edf41_objects as m
    import mdb
    import sgo
    f = edf41dlc.STORY_FOLDER
    dragons = {'DRAGONSMALL401.SGO': 'EDF41_DRAGONSMALL401.SGO',
               'REDSMALLDRAGON.SGO': 'EDF41_REDSMALLDRAGON.SGO',
               'YERROWSMALLDRAGON.SGO': 'EDF41_YERROWSMALLDRAGON.SGO'}
    assert edf41dlc.STORY_DRAGONS == dragons, edf41dlc.STORY_DRAGONS
    for name, target in dragons.items():
        canonical = 'app:/Object/' + target
        for old in ('app:/Object/' + name, f'app:/Object/{f}/' + name, canonical):
            for path in (old, old.lower(), old.swapcase()):
                assert edf41dlc.story_path(path) == canonical, path
                assert edf41dlc.rewrite(path) == canonical, path
                assert edf41dlc.rewrite(edf41dlc.rewrite(path)) == canonical, path
    for other in ('app:/Object/DragonBig401.sgo', 'app:/Object/DragonSmall401_HS.sgo',
                  'app:/Object/DragonSmall401.mrab', f'app:/Object/{f}/DragonSmall401.cas',
                  'app:/Object/EDF41MP01/DragonSmall401_HS.sgo', 'DragonSmall401'):
        assert edf41dlc.story_path(other) is None, other
        assert edf41dlc.rewrite(other) == other, other
    mixed = ('app:/Object/DragonSmall401.sgo;app:/object/edf41/redsmalldragon.sgo;'
             'app:/object/edf41_yerrowsmalldragon.sgo;mp01:/Object/DragonSmall401_HS.sgo;app:/Object/DragonBig401.sgo')
    expected = ('app:/Object/EDF41_DRAGONSMALL401.SGO;app:/Object/EDF41_REDSMALLDRAGON.SGO;'
                'app:/Object/EDF41_YERROWSMALLDRAGON.SGO;app:/Object/EDF41MP01/DragonSmall401_HS.sgo;'
                'app:/Object/DragonBig401.sgo')
    assert edf41dlc.rewrite(mixed) == expected
    assert edf41dlc.rewrite(expected) == expected
    # Canonical root identities remain DragonSmall aliases with the original template and 4.1 ragdoll bodies.
    assert set(m.STORY_ROOTS) == set(dragons), m.STORY_ROOTS
    assert f not in {folder for _, folder in edf41dlc.PACKS.values()}
    for name, target in dragons.items():
        a = m.ALIASES[target]
        assert m.ROOTS[target] == m.STORY_ROOTS[name] > 0 and a.cls == 'DragonSmall' and a.template == name, (target, a)
        assert name not in m.ROOTS and f'{f}/{name}' not in m.ROOTS, name
        assert dict(a.override)['edf41_ragdoll_bodies'] == list(m.SMALL_DRAGON_BODIES), a.override
        assert m.own_folder('OBJECT/' + target) == f and m.pack_folder('OBJECT/' + target) == '', target
    assert m.own_folder('OBJECT/EDF41MP01/X.SGO') == 'EDF41MP01' and m.own_folder('OBJECT/X.SGO') == ''
    old_path = re.compile(r'(?i)^app:/object/(?:' + re.escape(f) + r'/)?('
                          + '|'.join(re.escape(n) for n in dragons) + r')$')
    canonical_paths = {'app:/object/' + target.lower(): name for name, target in dragons.items()}

    def pushed(data: bytes) -> list[str]:
        prog = bvm41.parse(data)
        return [p for code in [prog.init] + [g.code for g in prog.funcs] for x in code
                if x.op == bvm41.OP_PUSHS for p in x.v.split(';')]

    def restore(text: str) -> str:
        return ';'.join('app:/Object/' + canonical_paths[p.lower()] if p.lower() in canonical_paths else p
                        for p in text.split(';'))

    with open(os.path.join(ROOT, 'edf41port', 'missions', 'M165', 'MISSION.BVM'), 'rb') as fh:
        script = fh.read()
    assert bvm41.rewritten(script, lambda s: s) == script
    mapped = bvm41.rewritten(script, edf41dlc.rewrite)
    assert bvm41.rewritten(mapped, edf41dlc.rewrite) == mapped
    names = pushed(mapped)
    assert not [s for s in names if old_path.match(s)], names
    assert set(canonical_paths) <= {s.lower() for s in names}, names
    restored = bvm41.rewritten(mapped, restore)
    assert bvm41.rewritten(restored, edf41dlc.rewrite) == mapped
    assert not [s for s in pushed(restored) if s.lower() in canonical_paths]
    # Every shipped story/DLC script uses the prefixed identity, never the stock or old namespace name.
    referenced: set[str] = set()
    for path in glob.glob(os.path.join(ROOT, 'edf41port', '**', '*.BVM'), recursive=True):
        with open(path, 'rb') as fh:
            names = pushed(fh.read())
        assert not [s for s in names if old_path.match(s)], f'{path} retains an old small-dragon path'
        for name in names:
            if name.lower() in canonical_paths:
                assert name == edf41dlc.rewrite(name), (path, name)
                referenced.add(name.lower())
    assert referenced == set(canonical_paths), referenced
    models = {'DRAGONSMALL401.SGO': 'SmallDragon401Green.mdb', 'REDSMALLDRAGON.SGO': 'RedSmallDragon.mdb',
              'YERROWSMALLDRAGON.SGO': 'YerrowSmallDragon.mdb'}
    source_of = {target: name for name, target in dragons.items()}

    def check_object(obj: dict, read: Callable[[str], bytes]) -> None:
        rel = obj['object']
        source = source_of[rel.removeprefix('OBJECT/')]
        legacy = f'OBJECT/{f}/{source}'
        assert obj['class'] == 'DragonSmall' and legacy in obj['files'], obj
        assert read(legacy) == read(rel), (rel, 'old namespace SGO differs')
        top = sgo.load(data=read(rel))
        assert top['xgs_scene_object_class'] == 'DragonSmall', (rel, top['xgs_scene_object_class'])
        model = m.model_entries(top['animation_model'][0])[0]
        model_rel = m.rel_of(model[0])
        assert model_rel.startswith(f'OBJECT/{f}/') and model[1] == models[source], model
        assert model_rel in obj['files'], (rel, model_rel)
        assert model[1].lower() in {x.name.lower() for x in mdb.rab_read(read(model_rel)).files}, model
        cas_rel = m.rel_of(top['animation_model'][1])
        assert cas_rel.startswith(f'OBJECT/{f}/') and cas_rel in obj['files'], cas_rel
        assert 'frogdragon' not in top['ragdoll'][0].lower(), top['ragdoll']
        assert m.rel_of(top['ragdoll'][0]) in obj['files'], top['ragdoll']
        assert top['edf41_ragdoll_bodies'] == list(m.SMALL_DRAGON_BODIES), top.get('edf41_ragdoll_bodies')

    objects = {o['object']: o for o in eo.registry()['objects']}
    for name, target in dragons.items():
        rel = 'OBJECT/' + target
        assert rel in objects and f'OBJECT/{f}/{name}' not in objects, rel
        assert 'OBJECT/' + name not in objects, (name, 'would replace EDF5/6 frog dragons')
        check_object(objects[rel], eo.read)
    if not _e5c_have_game() or not _edf41_root():
        print('skip  edf41_story_small_dragons (real archives): needs EDF6 and EDF4.1 installed')
        return
    import rootcpk
    games = m.Games(_edf41_root(), rootcpk.DEFAULT_GAME)
    frog = sgo.load(data=games.read6('OBJECT/DRAGONSMALL401.SGO'))
    assert 'frogdragon' in m.model_entries(frog['animation_model'][0])[0][1].lower(), frog['animation_model'][0]
    for name, target in dragons.items():
        assert games.moved['OBJECT/' + target] == 'OBJECT/' + name, target
        assert games.moved[f'OBJECT/{f}/{name}'] == 'OBJECT/' + name, name
    data, files = m.build(_edf41_root(), rootcpk.DEFAULT_GAME, tuple(dragons.values()))
    assert not data['skipped'] and {o['object'] for o in data['objects']} == {'OBJECT/' + r for r in dragons.values()}, data['skipped']
    for obj in data['objects']:
        assert not [r for r in obj['files'] if r in games.have6], (obj['object'], obj['files'])
        check_object(obj, files.__getitem__)


@test
def edf41_story_small_dragons_upgrade() -> None:
    """An existing edf41-owned EDF41/ dragon install upgrades to the three prefixed roots without foreign-file
    refusals, dependency rewrites or stock frog overrides; both entry names uninstall without leaving resources."""
    import edf41_objects as eo
    import edf41dlc
    reg = eo.registry()
    roots = {'OBJECT/' + n for n in edf41dlc.STORY_DRAGONS.values()}
    aliases = {f'OBJECT/{edf41dlc.STORY_FOLDER}/{n}' for n in edf41dlc.STORY_DRAGONS}
    objects = [o for o in reg['objects'] if o['object'] in roots]
    needed = {r for obj in objects for r in obj['files']}
    assert len(objects) == 3 and roots | aliases <= needed
    subset = {**reg, 'objects': objects, 'stock_completions': [],
              'files': {r: reg['files'][r] for r in needed}}
    payloads = {r: eo.read(r) for r in needed}
    old = {r: data for r, data in payloads.items() if r not in roots}
    with tempfile.TemporaryDirectory(prefix='edf6vc-e41dragon-') as game, patched(eo, registry=lambda: subset):
        os.makedirs(os.path.join(game, 'Mods'))
        eo.install(game, old)
        led = ledger.Ledger(game)
        assert set(led.owned_by(eo.OWNER)) == {ledger.key(r) for r in old}
        assert all(os.path.isfile(led.disk(r)) for r in aliases)
        assert not any(os.path.exists(led.disk(r)) for r in roots)
        assert not eo.unavailable(game), eo.unavailable(game)
        files, skipped = eo.build(game)
        assert not skipped and files == payloads, skipped
        written = eo.install(game, files)
        assert {os.path.relpath(p, led.mods).replace('\\', '/').upper() for p in written} == roots, written
        for source, target in edf41dlc.STORY_DRAGONS.items():
            canonical, legacy = 'OBJECT/' + target, f'OBJECT/{edf41dlc.STORY_FOLDER}/{source}'
            assert modfiles.read(led.disk(canonical)) == modfiles.read(led.disk(legacy)) == payloads[canonical]
            assert not os.path.exists(led.disk('OBJECT/' + source)), source
        assert set(ledger.Ledger(game).owned_by(eo.OWNER)) == {ledger.key(r) for r in needed}
        assert not eo.unavailable(game) and eo.install(game, files) == []
        _deleted, kept = eo.remove(game)
        assert not kept and not any(os.path.exists(led.disk(r)) for r in needed), kept
        assert not ledger.Ledger(game).owned_by(eo.OWNER)

@test
def edf41_dragon_catalog_names() -> None:
    """The object list and range distinguish EDF5/6's stock frog dragon, 4.1's three prefixed small dragons, DLC
    dragons and the big dragon, while every row's file path still resolves to its own registry object."""
    sys.path.insert(0, os.path.join(ROOT, 'testrange'))
    import catalog
    rows = {r.sgo: r for r in catalog.rows()}
    frog = rows['dragonsmall401']
    assert frog.source == 'stock' and frog.rel == 'OBJECT/DRAGONSMALL401.SGO'
    assert '蝌蚪' in frog.label and '小龙' not in frog.label, frog
    expected = {'edf41_dragonsmall401': '4.1 小龙（绿，飞）',
                'edf41_redsmalldragon': '4.1 小龙（红，飞）',
                'edf41_yerrowsmalldragon': '4.1 小龙（黄，飞）',
                'edf41mp01/dragonsmall401_hs': '4.1 DLC 小龙（绿，飞）',
                'edf41mp01/redsmalldragon_hs': '4.1 DLC 小龙（红，飞）',
                'edf41mp01/yerrowsmalldragon_hs': '4.1 DLC 小龙（黄，飞）',
                'dragonbig401': '4.1 大龙（飞）'}
    for name, label in expected.items():
        row = rows[name]
        assert row.label.startswith(label + ' ') and row.source == 'edf41' and row.flying, row
        assert row.category == 'enemy41' and row.rel in catalog.edf41_files(row.sgo), row
        assert row.rel == 'OBJECT/' + name.upper() + '.SGO' and '蝌蚪' not in row.label, row
    assert not {f'edf41/{n}' for n in ('dragonsmall401', 'redsmalldragon', 'yerrowsmalldragon')} & set(rows)


@test
def edf41_big_dragon_ragdoll_mass() -> None:
    """M166 2026-10-17 (「大龙好像没ai」): DragonSmall keeps a knocked-down dragon down until its ragdoll's sum of mass x
    (|v| + |w|) is under 2000 (else 60 s); 4.1's DragonBig ragdoll weighs 361,005 kg, 114 times the class's heaviest stock
    ragdoll, so its resting jitter (a sum of 5,000..18,000) never passed. pylib/hk2014.py reads 4.1's hk_2014 tagfiles
    (every one the port ships, to the end tag) and scales a ragdoll's masses, inertias and joint friction torques by one
    factor in place (the same size, nothing else changed); make_edf41_objects.dragon_ragdoll_fix makes the giant's
    ragdoll as heavy as the class's heaviest stock one and scales its hit impulse (dragon_RagdollReactionScale) with it.
    The registry keeps the impulse per kilogram of 4.1's (2.5 / 361,005) whichever mass it ships; with the games, the
    built ragdoll weighs what EDF6's heaviest DragonSmall ragdoll does."""
    import hk2014
    import edf41_objects as eo
    import make_edf41_objects as m
    import sgo
    reg = eo.registry()
    tagfiles = [r for r in reg['files'] if r.endswith('.SHKT') and eo.read(r)[:8] == hk2014.MAGIC]
    assert len(tagfiles) >= 20, tagfiles
    for rel in tagfiles:   # every 4.1 ragdoll reads to its end tag
        hk2014.read(eo.read(rel))
    for rel in ('OBJECT/RAGDOLL_SMALLDRAGON401_HAS_HKA.SHKT', 'OBJECT/RAGDOLL_REDSMALLDRAGON.SHKT'):
        assert abs(hk2014.total_mass(eo.read(rel)) - 1500.0) < 0.5, rel   # 4.1's small dragons: 36 bodies, 1,500 kg
    big = 'OBJECT/RAGDOLL_BIGDRAGON401BLUE_HAS_HKA.SHKT'
    data = eo.read(big)
    before = hk2014.rigid_bodies(hk2014.read(data))
    half = hk2014.scaled_masses(data, 0.5)
    after = hk2014.rigid_bodies(hk2014.read(half))
    assert len(half) == len(data) and len(after) == len(before) == 56
    for a, b in zip(before, after):
        assert a.name == b.name and abs(b.mass - a.mass * 0.5) <= 1e-3 * a.mass, (a, b)
        assert all(abs(y - x * 2.0) <= 1e-5 * x for x, y in zip(a.inertia_inv, b.inertia_inv)), (a, b)
        assert (a.linear_damping, a.angular_damping, a.deactivation_class) == (b.linear_damping, b.angular_damping,
                                                                                b.deactivation_class)

    def frictions(blob: bytes) -> list[float]:
        return [v['maxFrictionTorque'] for o in hk2014.read(blob).objects for v in (o.get('atoms') or {}).values()
                if isinstance(v, dict) and 'maxFrictionTorque' in v]
    assert frictions(data) and all(abs(y - x * 0.5) <= 1e-3 * x for x, y in zip(frictions(data), frictions(half)))
    changed = sum(x != y for x, y in zip(data, half))
    assert 0 < changed <= 4 * (4 * len(before) + len(frictions(data))), changed   # only those floats' bytes
    for bad in (0.0, -1.0):
        try:
            hk2014.scaled_masses(data, bad)
        except ValueError:
            continue
        raise AssertionError(f'factor {bad} accepted')
    top = sgo.load(data=eo.read('OBJECT/DRAGONBIG401.SGO'))
    per_kg = m.num(top['dragon_RagdollReactionScale']) / hk2014.total_mass(data)
    assert abs(per_kg - 2.5 / 361005.0) <= 1e-3 * per_kg, per_kg
    assert m.NATIVE_FIXES['DRAGONBIG401.SGO'] is m.dragon_ragdoll_fix
    if not _e5c_have_game() or not _edf41_root():
        print('skip  edf41_big_dragon_ragdoll_mass (real archives): needs EDF6 and EDF4.1 installed')
        return
    import rootcpk
    games = m.Games(_edf41_root(), rootcpk.DEFAULT_GAME)
    limit = m.class_ragdoll_mass(games, 'DragonSmall')
    assert abs(limit - 3180.0) < 0.5, limit       # e518_frogDragon_large
    built, files = m.build(_edf41_root(), rootcpk.DEFAULT_GAME, ('DRAGONBIG401.SGO',))
    assert not built['skipped'], built['skipped']
    assert abs(hk2014.total_mass(files[big]) - limit) < 0.5
    top = sgo.load(data=files['OBJECT/DRAGONBIG401.SGO'])
    assert abs(m.num(top['dragon_RagdollReactionScale']) - 2.5 * limit / 361005.0) < 1e-4, top['dragon_RagdollReactionScale']


# EDF4.1's mission maps (plan P6): the maps EDF4.1's 98 missions load (pylib/map_legacy.py converts their MAC).
EDF41_MISSION_MAPS = ('IG_CAVE01', 'IG_HORNETCAVE01', 'IG_WRECKTOWN01', 'NW_HILLYCITY_LIGHT', 'NW_KASENJIKI01',
                      'NW_KOUSOUBLD01', 'NW_SINSUIKOUEN', 'NW_SOUKOGAI01', 'NW_UNDERGROUND01', 'SK_CRATER01',
                      'SK_HEIGEN02', 'SK_KAIGAN01', 'SK_SANGAKU01', 'SK_SANGAKU02', 'SK_STEPMOUNTAIN01',
                      'SK_VALLEY01')


def _legacy_games() -> tuple[dict, dict, dict] | None:
    """(EDF4.1, EDF5, EDF6) archives by path, or None off a developer's machine (CI has no game)."""
    import make_edf41_objects as m
    import rootcpk
    steam = os.path.dirname(rootcpk.DEFAULT_GAME)
    roots = [os.path.join(steam, 'Earth Defense Force 4.1'), os.path.join(steam, 'EARTH DEFENSE FORCE 5'),
             rootcpk.DEFAULT_GAME]
    if not all(os.path.isfile(os.path.join(r, 'Root.cpk')) for r in roots):
        return None
    g41, g5, g6 = (m.archives(r) for r in roots)
    return g41, g5, g6


def _game_read(games: dict, rel: str) -> bytes:
    return games[rel.upper()].read(*rel.rsplit('/', 1))


def _rmpa_points(tree: object) -> list[tuple]:
    """Every point of a decoded RMPA (rmpa_legacy.decode5 / decode6): (name, id, position, facing), in order."""
    out: list[tuple] = []
    if isinstance(tree, dict):
        if 'pos' in tree and 'face' in tree:
            out.append((tree['name'], tree['id'], tuple(tree['pos']), tuple(tree['face'])))
        for v in tree.values():
            out += _rmpa_points(v)
    elif isinstance(tree, list):
        for v in tree:
            out += _rmpa_points(v)
    return out


@test
def map_legacy_real() -> None:
    """pylib/map_legacy.py on the games: where EDF6 kept an EDF5 map's content its re-laid MAPO / MAPB is EDF6's byte for
    byte (IG_CAVE501/503/504 MAPO; IG_CAVE503 and IG_TESTLIGHTMAP (EDF5's) MAPB with EDF6's empty scene slots), and every EDF4.1
    mission map converts to a MAC bigmap reads and verifies, one MAPO entry per MAPB record. The other pairs differ in
    content EDF6 changed (docs/edf41-map-mac-format.md)."""
    import bigmap
    import map_legacy
    games = _legacy_games()
    if games is None:
        return
    g41, g5, g6 = games

    def members(mac: bytes) -> dict[str, bytes]:
        return dict(bigmap.Marc.parse(mac).files)

    for name in ('IG_CAVE501', 'IG_CAVE503', 'IG_CAVE504'):
        new = members(map_legacy.mac_from_legacy(_game_read(g5, f'MAP/{name}.MAC')))
        assert new['map.mapo'] == members(_game_read(g6, f'MAP/{name}.MAC'))['map.mapo'], name
    for name, games_from in (('IG_CAVE503', g5), ('IG_TESTLIGHTMAP', g5)):
        new = members(map_legacy.mac_from_legacy(_game_read(games_from, f'MAP/{name}.MAC'), 'edf6'))
        assert new['map.mapb'] == members(_game_read(g6, f'MAP/{name}.MAC'))['map.mapb'], name
    for name in EDF41_MISSION_MAPS:
        out = map_legacy.mac_from_legacy(_game_read(g41, f'MAP/{name}.MAC'))
        assert not bigmap.verify(out), (name, bigmap.verify(out)[:3])
        m = members(out)
        assert bigmap.MapO(m['map.mapo']).ent_n == bigmap.MapB(m['map.mapb']).rec_n, name


@test
def rmpa_legacy_real() -> None:
    """pylib/rmpa_legacy.py on the games. EDF6 ships EDF5's missions converted (MISSION/EDF5_OLD_SCRIPT): 65 of the 146
    come out of rmpa_from_legacy byte for byte, the rest differ only in content EDF6's editor changed
    (docs/edf41-map-rmpa-format.md), and every output is a full EDF6 RMPA. EDF6's own files pass through unchanged. All
    of EDF4.1's mission RMPAs (EDF5's MISSION/EDF4.1) and its mission maps' map.rmpa convert keeping every point's
    name, id, position and facing."""
    import map_legacy
    import rmpa_legacy
    games = _legacy_games()
    if games is None:
        return
    g41, g5, g6 = games
    pairs = [k for k in g6 if k.startswith('MISSION/EDF5_OLD_SCRIPT/') and k.endswith('.RMPA')
             and 'MISSION/' + k[len('MISSION/EDF5_OLD_SCRIPT/'):] in g5]
    assert len(pairs) == 146, len(pairs)
    same = 0
    for k in pairs:
        e6 = _game_read(g6, k)
        out = rmpa_legacy.rmpa_from_legacy(_game_read(g5, 'MISSION/' + k[len('MISSION/EDF5_OLD_SCRIPT/'):]))
        assert rmpa_legacy.is_edf6(out), k
        assert rmpa_legacy.rmpa_from_legacy(e6) == e6, k
        same += out == e6
    assert same == 65, same

    def kept(old: bytes) -> bytes:
        new = rmpa_legacy.rmpa_from_legacy(old)
        assert rmpa_legacy.is_edf6(new)
        before, after = _rmpa_points(rmpa_legacy.decode5(old)[0]), _rmpa_points(rmpa_legacy.decode6(new)[0])
        assert sorted(before) == sorted(after), 'a point was lost or changed'
        return new

    missions = [k for k in g5 if k.startswith('MISSION/EDF4.1/') and k.endswith('/MISSION.RMPA')]
    assert len(missions) == 126, len(missions)
    for k in missions:
        kept(_game_read(g5, k))
    for name in EDF41_MISSION_MAPS:
        kept(dict(map_legacy.parse_marc_lenient(_game_read(g41, f'MAP/{name}.MAC'))[1])['map.rmpa'])



@test
def edf41_maps_cover_the_scripts() -> None:
    """Every map and weather EDF4.1's bundled mission scripts (the story's and the DLC packs') load (Map('app:/Map/<map>.mac', '<weather>')) that
    tools/edf41_maps.py installs has its weather pair: a weather left out would make the game look for a MAE that is not
    there. The SK_* maps are installed too (their heightfield becomes an FMB piece, pylib/sk_map.py), so every map the scripts
    load is covered but NW_TESTMAP, a test map no story mission needs."""
    import re
    import edf41_maps
    import make_edf5_campaign as c
    used: dict[str, set[str]] = {}
    # every 4.1 pack: the story and its two DLC mission packs (the DLC's M203 asked SK_HEIGEN02 for cloudy2, which the
    # list, made from the story's scripts alone, lacked: the game faulted at EDF+0x161AE2 parsing the missing weather,
    # 2026-10-14)
    for p41, m, script in ((p41, m, s) for p41 in c.PACKS41 for m in c.edf41_text(p41.data)['missions']
                           for s in ('MISSION.BVM', 'BEFORE.BVM')
                           if os.path.exists(os.path.join(p41.data, 'missions', m['id'], s))):
        # both scripts the pack ships load the map (BEFORE.BVM too: M190's asks NW_KOUSOUBLD01 for FineCloud,
        # which the list lacked; the game's map::Environment faulted on the missing weather, 2026-10-13)
        strings = c.utf16_strings(c.edf41_script(m['id'], script, p41.data))
        for i, s in enumerate(strings):
            hit = re.fullmatch(r'(?i)app:/Map/(\w+)\.mac', s.strip())
            if hit and i + 1 < len(strings):
                used.setdefault(hit.group(1).upper(), set()).add(strings[i + 1].strip().upper())
    assert set(edf41_maps.MAPS) <= set(used), sorted(set(edf41_maps.MAPS) - set(used))
    for name, (_donor, weathers) in edf41_maps.MAPS.items():
        assert used[name] <= set(weathers), (name, sorted(used[name] - set(weathers)))
    assert {n for n in used if n not in edf41_maps.MAPS} <= {'NW_TESTMAP'}, sorted(n for n in used if n not in edf41_maps.MAPS)


def _edf41_friend_spawns() -> dict[tuple[str, str], list]:
    """{(pack folder, mission id): its scripts' friendly creation calls (pylib/edf41friends.py)} over every committed 4.1
    pack (the story and the two DLC packs)."""
    import edf41friends as ef
    import make_edf5_campaign as c
    out: dict[tuple[str, str], list] = {}
    for p41 in c.PACKS41:
        for m in c.edf41_text(p41.data)['missions']:
            out[(p41.folder, m['id'])] = [sp for s in ('MISSION.BVM', 'BEFORE.BVM') if s in m['files']
                                          for sp in ef.spawns(c.edf41_script(m['id'], s, p41.data))]
    return out


@test
def edf41_friends_spawn_as_named() -> None:
    """EDF4.1's friendly NPCs (feedback 2026-10-17 「npc好像也没搬过来」, docs/feedback-2026-10-17-edf41-npc.md): every
    CreateFriend / CreateFriendSquad / CreateFriendGroup / transporter call the committed 4.1 scripts make names an SGO
    pylib/edf41friends.py FRIENDS accounts for (EDF6's own file at that path, or one tools/make_edf41_objects.py ships:
    4.1's soldiers at OBJECT/EDF41/, never the story path EDF6 holds for its own), and a point the mission's RMPA holds
    (the executor creates nothing for a name it does not find, 0x21A3E1). The story's counts are pinned (the scripts are
    EDF5's, only 4.1's soldiers renamed), so a regeneration that loses a friend fails here. No game needed (the scripts,
    RMPA and objects.json are committed)."""
    import collections
    import edf41friends as ef
    import make_edf5_campaign as c
    import rmpa_legacy
    spawns = _edf41_friend_spawns()
    ported = set(json.load(open(os.path.join(c.EDF41, 'objects.json'), encoding='utf-8'))['files'])
    missing_points = []
    for (folder, mid), found in spawns.items():
        assert not ef.unknown(found), (folder, mid, ef.unknown(found))
        data = next(p.data for p in c.PACKS41 if p.folder == folder)
        with open(os.path.join(data, 'missions', mid, 'MISSION.RMPA'), 'rb') as f:
            points = {p[0] for p in _rmpa_points(rmpa_legacy.decode6(f.read())[0])}
        missing_points += [(folder, mid, sp.point) for sp in found if sp.native in ef.AT_POINT and sp.point not in points]
    assert not missing_points, missing_points[:10]
    assert all(rel in ported for rel, fr in ef.FRIENDS.items() if fr.source in ('ported', 'look')), \
        sorted(r for r, fr in ef.FRIENDS.items() if fr.source in ('ported', 'look') and r not in ported)
    # 4.1's soldiers are renamed everywhere (tools/make_edf41_missions.py): no pack's script names a story path of one,
    # which is EDF6's own soldier (left as it was, the friend the player meets is EDF6's)
    stale = sorted({(folder, mid, s) for (folder, mid), found in spawns.items() for sp in found for s in sp.sgos
                    if s in ef.LOOKS})
    assert not stale, stale[:10]
    story = {mid: found for (folder, mid), found in spawns.items() if folder == c.EDF41_FOLDER}
    calls = collections.Counter(sp.native for found in story.values() for sp in found)
    assert dict(calls) == {1010: 145, 1011: 264, 1012: 26, 1022: 5, 1023: 15}, dict(calls)
    assert sum(1 for found in story.values() if found) == 86, sum(1 for found in story.values() if found)
    # every FRIENDS entry is named by the story (a stale table entry would vouch for nothing)
    assert {s for found in story.values() for sp in found for s in sp.sgos} == set(ef.FRIENDS)


@test
def edf41_friends_rewrite() -> None:
    """4.1's soldiers go through the one story rename pylib/edf41dlc.py keeps (STORY_SHADOWED, with the small dragons;
    no game): a story path of LOOKS, in any case and inside a ';' list, becomes its OBJECT/EDF41/ path with the script's
    case kept; anything else (another object, the new path itself) is unchanged; edf41friends.look_rel agrees with it
    and the folder is the story folder."""
    import edf41dlc
    import edf41friends as ef
    rw = edf41dlc.rewrite
    assert edf41dlc.STORY_FOLDER == ef.LOOK_FOLDER == 'EDF41'
    assert {r.split('/', 1)[1] for r in ef.LOOKS} <= set(edf41dlc.STORY_SHADOWED)
    assert rw('app:/Object/AiArmySoldier_AF.sgo') == 'app:/Object/EDF41/AiArmySoldier_AF.sgo'
    assert rw('app:/object/aipalewing_lr_leader.SGO') == 'app:/object/EDF41/aipalewing_lr_leader.SGO'
    assert rw('app:/Object/GiantAnt01.sgo;app:/Object/AiHeavyArmor_SP.sgo') == \
        'app:/Object/GiantAnt01.sgo;app:/Object/EDF41/AiHeavyArmor_SP.sgo'
    for same in ('app:/Object/EDF41/AiArmySoldier_AF.sgo', 'app:/Object/AiArmySoldier_OMG.sgo', '小隊01',
                 'app:/Object/Vehicle403_Tank_AI.sgo'):
        assert rw(same) == same, same
    assert all(ef.rel_of(rw('app:/Object/' + r.split('/', 1)[1].lower())) == ef.look_rel(r) for r in ef.LOOKS)
    assert len(ef.LOOKS) == 26 and {ef.FRIENDS[ef.look_rel(r)].source for r in ef.LOOKS} == {'look'}


@test
def edf41_friend_looks_shipped() -> None:
    """4.1's soldiers as tools/make_edf41_objects.py ships them (FRIEND_LOOKS; no game: the registry and its files are
    committed): each LOOKS soldier is an object at OBJECT/EDF41/<name> of its class, look_of its story path, whose
    model, CAS and ragdoll are files of the registry; EDF6's own soldier at the story path is never shipped over; the
    SGO carries 4.1's model / CAS / weapons and the shapes the class reads (FRIEND_OVERRIDE); each 4.1 CAS has every
    player EDF6's CAS of the class has and the states aliased for the class (FRIEND_CAS, CLASS_NAMES), as the Omega
    team's soldiers too (they share the rangers' CAS); the bones the class finds by name are in the model."""
    import cas_graft
    import edf41_objects as eo
    import edf41friends as ef
    import make_edf41_objects as m
    import sgo
    reg = eo.registry()
    files = set(reg['files'])
    by_object = {o['object']: o for o in reg['objects']}
    for story, cls in ef.LOOKS.items():
        rel = ef.look_rel(story)
        assert story not in files, f'{story} is EDF6\'s own soldier: never shipped over'
        o = by_object.get(rel)
        assert o and o['class'] == cls and o.get('look_of') == story and o['missions'] > 0, (rel, o)
        assert set(o['files']) <= files and o['files'][0] == rel, o['files']
        d = sgo.load(data=eo.read(rel))
        assert d['xgs_scene_object_class'] == cls
        am = d['animation_model']
        model, cas = m.rel_of(am[0][0]), m.rel_of(am[1])
        assert {model, cas, m.rel_of(d['ragdoll'][0])} <= set(o['files']), (rel, model, cas)
        assert model.startswith('OBJECT/') and 'EDF6' not in model and 'P50' not in model, model
        assert all(w.lower().startswith('app:/weapon/') for w in d['soldier_load_weapon'])
        assert d['soldier_sub_weapon_slot'] == 0.0
        for k, v in m.FRIEND_OVERRIDE[cls]:
            assert d[k] == v, (rel, k, d[k])
        bones = m.model_bones(lambda r: eo.read(m.rel_of(r)), am)
        assert all(d[k] in bones for k in m.FRIEND_BONE_KEYS if k in d), (rel, [d[k] for k in m.FRIEND_BONE_KEYS if k in d])
    for cas_rel, spec in m.FRIEND_CAS.items():
        states = cas_graft.states(eo.read(cas_rel))
        for player, new, _old in spec.states:
            assert new in states[player], (cas_rel, player, new)
        assert {'knockout', 'noWeapon_stand', 'noWeapon_walk_f'} <= states['main'], cas_rel
    omg = sgo.load(data=eo.read('OBJECT/AIARMYSOLDIER_OMG.SGO'))
    assert m.rel_of(omg['animation_model'][1]) == 'OBJECT/ARMYSOLDIER.CAS'


def _rotation_degrees(rot: list) -> float:
    """How far the rotation part of the 4x4 `rot` turns, in degrees."""
    import math
    return math.degrees(math.acos(max(-1.0, min(1.0, (rot[0] + rot[5] + rot[10] - 1) / 2))))


EDF41_SOLDIERS = ('OBJECT/AIARMYSOLDIER_OMG.SGO', 'OBJECT/AIARMYSOLDIER_OMG_LEADER.SGO')   # + LOOKS' (look_rel)


@test
def edf41_friend_grip_shipped() -> None:
    """All 28 source soldiers carry private source-model weapons, without a bind-pose IK approximation."""
    import dsgo
    import edf41_objects as eo
    import edf41friends as ef
    import mab_legacy
    import make_edf41_objects as m
    import sgo
    reg = eo.registry()
    units = [ef.look_rel(r) for r in ef.LOOKS] + list(EDF41_SOLDIERS)
    weapon_paths = set()
    for rel in units:
        d = sgo.load(data=eo.read(rel))
        md = m.model_mdb(lambda r: eo.read(m.rel_of(r)), d['animation_model'])
        hda = d['human_direction_adjust']
        assert hda[0] == m.spine_mode(md, hda[1]) == 1, (rel, hda)
        assert d['soldier_weapon_grab_adjust'] == [[0.0] * 3, [0.0] * 3], rel
        for path in d['soldier_load_weapon']:
            private = m.rel_of(path)
            assert private.startswith('WEAPON/EDF41_NPC/'), (rel, private)
            assert private in reg['files'], private
            weapon_paths.add(private)
    assert len(units) == 28 and len(weapon_paths) == 19, (len(units), len(weapon_paths))
    for rel in weapon_paths:
        doc = dsgo.parse(eo.read(rel))
        assert all('name.' + lang in doc.root.names.values() for lang in ('ja', 'en', 'cn', 'sc', 'kr')), rel
        am = doc.root.get('animation_model')
        ref = dsgo.to_py(am.items[0])
        assert m.rel_of(ref[0]).startswith('WEAPON/EDF41_NPC/'), (rel, ref)
        md = m.model_mdb(lambda r: eo.read(m.rel_of(r)), dsgo.to_py(am))
        assert md is not None, rel
        bones = {md.name_of(b.name) for b in md.bones}
        assert all(loc.node in bones for _kind, loc in mab_legacy.mab_locators(am.items[2].data)), rel
        own_cas = am.items[1]
        if isinstance(own_cas, str) and own_cas.lower().startswith('app:/'):
            assert m.rel_of(own_cas).startswith('WEAPON/EDF41_NPC/'), (rel, own_cas)


@test
def edf41_friend_climb_shipped() -> None:
    """No shipped soldier carries human_climb_settings (EDF6's climb over a ledge) unless its CAS's main player has every
    climb state the climb plays (make_edf41_objects SOLDIER_CLIMB_STATES): the climb state leaves only when that state is done,
    so a CAS without them froze the soldier in place for good (2026-10-19: 「NPC有的会立正」). 4.1's three soldier CAS
    have none, so none of the 28 4.1 soldiers climbs; every other shipped soldier keeps its climb only with the states."""
    import cas_graft
    import edf41_objects as eo
    import edf41friends as ef
    import make_edf41_objects as m
    import sgo
    reg = eo.registry()
    soldiers = 0
    for rel in sorted(r for r in reg['files'] if r.endswith('.SGO')):
        d = sgo.load(data=eo.read(rel))
        if str(d.get('xgs_scene_object_class')) not in m.NULL_CHECKED_KEYS:
            continue
        soldiers += 1
        cas_rel = m.rel_of(d['animation_model'][1])
        assert cas_rel in reg['files'], (rel, cas_rel)
        missing = m.climb_states_missing(eo.read(cas_rel))
        assert m.SOLDIER_CLIMB_KEY not in d or not missing, (rel, cas_rel, missing)
    for cas_rel in m.FRIEND_CAS:
        assert m.climb_states_missing(eo.read(cas_rel)) == list(m.SOLDIER_CLIMB_STATES), cas_rel
    units = [ef.look_rel(r) for r in ef.LOOKS] + list(EDF41_SOLDIERS)
    assert all(m.SOLDIER_CLIMB_KEY not in sgo.load(data=eo.read(r)) for r in units)
    assert soldiers >= len(units) == 28, (soldiers, len(units))


@test
def edf41_friend_grip_matches_weapons() -> None:
    """Actual 4.1 source MAB/IK/model/CAS and HOLD fields are preserved exactly, not only close in bind pose."""
    import dsgo
    import edf5port
    import edf41_objects as eo
    import edf41friends as ef
    import legacy_assets
    import mab_legacy
    import make_edf41_objects as m
    import sgo
    edf41, edf6 = os.path.join(m.STEAM, 'Earth Defense Force 4.1'), os.path.join(m.STEAM, 'EARTH DEFENSE FORCE 6')
    if not all(os.path.isfile(os.path.join(r, 'Root.cpk')) for r in (edf41, edf6)):
        print('skip  edf41_friend_grip_matches_weapons: needs EDF4.1 and EDF6 installed')
        return
    games = m.Games(edf41, edf6)
    _presets, banks = m.sound_cues(games)
    resident = set().union(*(banks[name] for name in m.RESIDENT_BANKS))
    seen = set()
    for rel in [ef.look_rel(r) for r in ef.LOOKS] + list(EDF41_SOLDIERS):
        original = sgo.load(data=games.read4(rel))
        current = sgo.load(data=eo.read(rel))
        assert [m.friend_weapon_rel(w) for w in original['soldier_load_weapon']] == [
            m.rel_of(w) for w in current['soldier_load_weapon']], rel
        old_missing = m.friend_hold_missing(original, games.read4(m.rel_of(original['animation_model'][1])),
                                             lambda p: games.read4(m.rel_of(p)))
        new_missing = m.friend_hold_missing(current, eo.read(m.rel_of(current['animation_model'][1])),
                                             lambda p: eo.read(m.rel_of(p)))
        assert not new_missing - old_missing, (rel, sorted(new_missing - old_missing))
        for path in original['soldier_load_weapon']:
            source = m.rel_of(path)
            if source in seen:
                continue
            seen.add(source)
            _version, own = sgo.read(games.read4(source))
            doc = dsgo.parse(eo.read(m.friend_weapon_rel(source)))
            native = dsgo.parse(games.read6(source)).root
            assert set(native.names.values()) <= set(doc.root.names.values()), source
            assert doc.root.get('xgs_scene_object_class') == native.get('xgs_scene_object_class'), source
            # All holding keys including class-specific nested attacks, not a
            # whitelist borrowed from other soldier classes or weapon models.
            for key in ('ModelConstraint', 'BaseAnimation', 'AimAnimation', 'ChangeAnimation',
                        'ReloadAnimation', 'custom_parameter'):
                assert dsgo.to_py(doc.root.get(key)) == dsgo.to_py(edf5port.to_value(own[key])), (source, key)
            for key in m.FRIEND_WEAPON_SE_FIELDS:
                if key not in own:
                    continue
                expected = edf5port.to_value(own[key])
                actual = doc.root.get(key)
                if isinstance(expected, dsgo.Node) and len(expected.items) > 1 and expected.items[1]:
                    if expected.items[1] not in resident:
                        expected.items[1] = native.get(key).items[1]
                    assert actual.items[1] in resident, (source, key, actual.items[1])
                assert dsgo.to_py(actual) == dsgo.to_py(expected), (source, key)
            original_am = own['animation_model']
            assert doc.root.get('animation_model').items[2].data == mab_legacy.mab_from_edf5(original_am[2]), source
            for path in m.strings(sgo.load(data=games.read4(source))):
                if not path.lower().startswith('app:/weapon/'):
                    continue
                asset = m.rel_of(path)
                if not asset.endswith(('.RAB', '.MRAB', '.CAS', '.SHKT')):
                    continue
                expected = games.read4(asset)
                if not asset.endswith('.SHKT'):
                    expected = legacy_assets.convert(asset, expected)
                assert eo.read(m.friend_weapon_rel(asset)) == expected, (source, asset)
    assert len(seen) == 19, len(seen)


@test
def edf41_friends_load_in_edf6() -> None:
    """The friends' SGOs as the installed game loads them (needs EDF4.1, EDF5 and EDF6: _legacy_games): each 'stock' one is in EDF6's
    archives with its FRIENDS class and every file it names (recursively) there too; each 'ported' one is the port's
    file of that class; and each story mission's 4.1 original names the same friends its shipped (EDF5) script creates."""
    import edf41_objects as eo
    import edf41friends as ef
    import make_edf41_objects as m
    import make_edf5_campaign as c
    import sgo
    games = _legacy_games()
    if games is None:
        print('skip  edf41_friends_load_in_edf6: needs EDF4.1, EDF5 and EDF6 installed')
        return
    g41, _g5, g6 = games
    for rel, fr in ef.FRIENDS.items():
        if fr.source == 'stock':
            members = sgo.load(data=_game_read(g6, rel))
            seen = m.sgo_closure({rel}, lambda r: _game_read(g6, r) if r in g6 else None)
            refs = set()
            for r in seen:
                if r in g6:
                    refs |= {m.rel_of(s) for s in m.strings(sgo.load(data=_game_read(g6, r)))
                             if s.lower().startswith('app:/')}
            lacking = sorted(r for r in refs if os.path.splitext(r)[1] and not r.endswith('.ACB') and r not in g6)
            assert not lacking, (rel, lacking)
        else:
            members = sgo.load(data=eo.read(rel))
            if fr.source == 'look':   # EDF6's own soldier at the story path stays as it is, of the same class
                story = next(r for r in ef.LOOKS if ef.look_rel(r) == rel)
                assert sgo.load(data=_game_read(g6, story)).get('xgs_scene_object_class') == fr.cls, story
        assert members.get('xgs_scene_object_class') == fr.cls, (rel, members.get('xgs_scene_object_class'), fr.cls)
    # each story mission's 4.1 original names the same friends its shipped script creates, 4.1's soldiers renamed
    shipped = {mid: {s for sp in found for s in sp.sgos}
               for (folder, mid), found in _edf41_friend_spawns().items() if folder == c.EDF41_FOLDER}
    original_names = set(ef.FRIENDS) | set(ef.LOOKS)
    for mid, names in shipped.items():
        original = set()
        for rel in g41:
            if rel.startswith(f'MISSION/{mid}/') and rel.endswith('.BVM'):
                original |= m.script_sgos(_game_read(g41, rel)) & original_names
        renamed = {ef.look_rel(r) if r in ef.LOOKS else r for r in original}
        assert renamed == names, (mid, sorted(renamed ^ names))


@test
def edf41_maps_install() -> None:
    """tools/edf41_maps.py on a stand-in game (conversion stubbed): writes every file of every map as edf41maps', a
    second install converts nothing it already converted from the same EDF4.1 files, a map one of whose files another
    mod placed is left out and that file untouched, no EDF4.1 writes nothing, removal deletes what it wrote."""
    import tempfile
    import edf41_maps
    import ledger

    class Fake:
        def fingerprint(self, name: str) -> list[int]:
            return [len(name), 1]

    converted: list[str] = []

    def convert(name: str, _e41: object, _e6: object) -> dict[str, bytes]:
        converted.append(name)
        return {rel: rel.encode() for rel in edf41_maps.files_of(name)}

    with tempfile.TemporaryDirectory() as game, \
            patched(edf41_maps, convert=convert, Archives=lambda _root: Fake(), workers=lambda: 1):
        os.makedirs(os.path.join(game, 'Mods'))
        written, skipped = edf41_maps.install(game, None)
        assert not written and [n for n, _ in skipped] == list(edf41_maps.MAPS) and not converted
        assert not os.listdir(os.path.join(game, 'Mods'))
        written, skipped = edf41_maps.install(game, 'edf41')
        every = [rel for name in edf41_maps.MAPS for rel in edf41_maps.files_of(name)]
        assert not skipped and converted == list(edf41_maps.MAPS) and len(written) == len(set(every))
        led = ledger.Ledger(game)
        assert all(led.owners(rel) == [edf41_maps.OWNER] for rel in every)
        converted.clear()
        assert edf41_maps.install(game, 'edf41') == ([], []) and not converted, converted
        edf41_maps.remove(game)
        other = os.path.join(game, 'Mods', 'MAP', 'IG_CAVE01.RAB')
        os.makedirs(os.path.dirname(other), exist_ok=True)
        with open(other, 'wb') as f:
            f.write(b'another mod')
        written, skipped = edf41_maps.install(game, 'edf41')
        assert [n for n, _ in skipped] == ['IG_CAVE01'] and 'IG_CAVE01' not in converted
        with open(other, 'rb') as f:
            assert f.read() == b'another mod'
        edf41_maps.remove(game)
        assert os.path.exists(other) and not os.path.exists(os.path.join(game, 'Mods', 'MAP', 'NW_SOUKOGAI01.MAC'))


@test
def edf41_maps_real() -> None:
    """tools/edf41_maps.py on the games: IG_CAVE01 converts to the files files_of names; the MAC re-parses with every
    member kept and its map.rmpa in EDF6's format; the weather pair and navmesh are the donor's bytes."""
    import bigmap
    import edf41_maps
    import rmpa_legacy
    games = _legacy_games()
    if games is None:
        return
    import rootcpk
    steam = os.path.dirname(rootcpk.DEFAULT_GAME)
    e41 = edf41_maps.Archives(os.path.join(steam, 'Earth Defense Force 4.1'))
    e6 = edf41_maps.Archives(rootcpk.DEFAULT_GAME)
    files = edf41_maps.convert('IG_CAVE01', e41, e6)
    assert list(files) == edf41_maps.files_of('IG_CAVE01')
    members = dict(bigmap.Marc.parse(files['MAP/IG_CAVE01.MAC']).files)
    assert set(members) == {'map.mapb', 'elec_wire_default.erb', 'map.rmpa', 'map.mapo'}
    assert rmpa_legacy.is_edf6(members['map.rmpa'])
    assert files['MAP/IG_CAVE01_FINE.MAE'] == e6.read('IG_CAVE501_FINE.MAE')
    assert files['MAP/IG_CAVE01.NAVMESH.HKT'] == e6.read('IG_CAVE501.NAVMESH.HKT')



@test
def sk_terrain_real() -> None:
    """pylib/sk_terrain.py on EDF4.1's seven SK maps: the field is 1001 x 1001 samples of 2 m, its half float heights span
    exactly the header's lowest / highest (the shader's f16tof32 on the low half), every vertex's stored material is the
    layer of its largest blend weight and the weights sum to one (a handful of vertices a little off, normalised by the
    converter), every layer paints with a colour (not a normal map) of
    the names the field lists and the slice count of its colour array is the number of such names; and the objects the
    map places stand on the decoded ground (median distance under half a metre, at the field's own placement: the
    record's origin and height included: SK_VALLEY01's is 118.4 m up) - which also pins the axes: row = z, column = x."""
    import struct
    import bigmap
    import map_legacy
    import mdb
    import numpy as np
    import sk_map
    import sk_terrain
    games = _legacy_games()
    if games is None:
        return
    g41 = games[0]
    for name in ('SK_CRATER01', 'SK_HEIGEN02', 'SK_KAIGAN01', 'SK_SANGAKU01', 'SK_SANGAKU02', 'SK_STEPMOUNTAIN01',
                 'SK_VALLEY01'):
        members = dict(map_legacy.parse_marc_lenient(_game_read(g41, f'MAP/{name}.MAC'))[1])
        shfb = [n for n in members if n.endswith('.shfb')]
        assert len(shfb) == 1, name
        s = sk_terrain.parse(members[shfb[0]])
        assert (s.width, s.height, s.cell) == (1001, 1001, (2.0, 2.0)) and s.extent == (2000.0, 2000.0), name
        assert abs(float(s.heights.min()) - s.lowest) < 0.3 and abs(float(s.heights.max()) - s.highest) < 0.3, name
        assert s.blend is not None and s.blend.shape == (1001, 1001, len(s.layers))
        tot = s.blend.sum(axis=2)
        assert tot.max() < 1.2 and (tot < 0.5).mean() < 1e-3 and tot.min() > 0, name   # normalised where they are used
        assert (s.blend.argmax(axis=2) == s.material).mean() > 0.9999, name
        slice_of = sk_map.color_slices(s.layer_names)
        assert all(l.color in slice_of and (l.multip < 0 or l.multip in slice_of) for l in s.layers), name
        cols = [f for f in mdb.rab_read(_game_read(g41, f'MAP/{name}.RAB')).files if f.name.endswith('.cols.dds')]
        assert len(cols) == 1
        slices = struct.unpack_from('<I', cols[0].data, 128 + 12)[0]        # the DX10 header's arraySize
        assert slices == len(slice_of), (name, slices, len(slice_of))
        assert sk_terrain.lattice(s).shape == (501, 501)
        if name not in ('SK_HEIGEN02', 'SK_VALLEY01'):
            continue
        mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(_game_read(g41, f'MAP/{name}.MAC')))
        mb = bigmap.MapB(mac.get('map.mapb'))
        ox, oy, oz = sk_map.field_origin(mac.get('map.mapb'), shfb[0])
        dy = []
        for i in range(mb.rec_n):
            if mb.def_name(mb.def_index(i)) == shfb[0]:
                continue
            x, y, z = mb.pos(i)
            c, r = (x - ox) / s.cell[0], (z - oz) / s.cell[1]
            if 0 <= c < s.width - 1 and 0 <= r < s.height - 1:
                c0, r0, fx, fz = int(c), int(r), c - int(c), r - int(r)
                h = (s.heights[r0, c0] * (1 - fx) * (1 - fz) + s.heights[r0, c0 + 1] * fx * (1 - fz)
                     + s.heights[r0 + 1, c0] * (1 - fx) * fz + s.heights[r0 + 1, c0 + 1] * fx * fz) + oy
                dy.append(abs(y - h))
        assert len(dy) > 200 and float(np.median(dy)) < 0.5 and float(np.mean(np.array(dy) < 1.0)) > 0.8, (name, np.median(dy))


@test
def fmb_build_real() -> None:
    """pylib/fmb_build.py on SK_HEIGEN02's field (a 100 x 100 cell corner of it, 5 x 5 leaves) with EDF6's IG_HAIKYOPLANE01
    as the donor: pylib/fmb.py parses it back with its layout checks and its selftest passes (leaf and node AABBs bit exact,
    the reference CMPL decoders agree, edits round trip); every leaf has what a stock regular grid leaf has (the same
    header and 0x3C mesh record fields in the same places, 441 vertices of unit normals, 800 triangles wound as the
    donor's, one 0x50 splat record per cell whose four corners each sum to 255 over the leaf's layer slots, the layer list
    of 16 bytes, its shader the pass-0 record of its layer count in the file's shader table), the trailing tables are
    the donor's two constant ones plus the layer table and shader table in the stock layout (the 4-layer pieces' 21
    shader records, the single layer ones' 9), and a leaf lists at most the layers a stock leaf lists."""
    import struct
    import fmb
    import fmb_build
    import map_legacy
    import numpy as np
    import sk_map
    import sk_terrain
    games = _legacy_games()
    if games is None:
        return
    g41, _g5, g6 = games
    members = dict(map_legacy.parse_marc_lenient(_game_read(g41, 'MAP/SK_HEIGEN02.MAC'))[1])
    s = sk_terrain.parse(members['sk_heigen02_fld2mgrid.shfb'])
    heights = sk_terrain.lattice(s)[200:301, 200:301]
    weights = s.blend[::2, ::2][200:301, 200:301]
    weights = weights / np.maximum(weights.sum(axis=2, keepdims=True), 1e-9)
    donor = _game_read(g6, 'MAP/' + sk_map.DONOR_FMB)
    layers = sk_map.layers_of(s)
    out = fmb_build.build(donor, heights, weights, -200.0, -200.0, 4.0, layers)
    f = fmb.parse(out)
    stock = fmb.parse(donor)
    assert len(f.leaves) == 25 and f.count == 49
    assert fmb.selftest(out)      # raises on any mismatch, returns what it checked
    sl = stock.leaves[0]
    sm = sl.meshes[0]
    for lf in f.leaves.values():
        assert struct.unpack_from('<I', lf.blob, 0)[0] == 1 and struct.unpack_from('<I', lf.blob, 0x28)[0] == 1
        assert struct.unpack_from('<i', lf.blob, 0x2C)[0] == 0x30 == sm.base and len(lf.verts) == 441
        assert struct.unpack_from('<I', lf.blob, 0x1C)[0] == 800
        m = lf.meshes[0]
        n = struct.unpack_from('<I', lf.blob, m.base)[0]
        assert 1 <= n <= fmb_build.MAX_LAYERS and m.grid[:4] == (4.0, 4.0, 20, 20) and m.grid[6] == n * 0x101
        # +0x28: the pass-0 draw record of an n-layer leaf in the file's own shader table (as every stock mesh)
        t4 = struct.unpack_from('<I', out, 0x20)[0]
        assert struct.unpack_from('<7I', out, t4 + 28 * m.grid[7]) == (n, 1, 0, 0, 0, 0, 0)
        assert m.weight_count == 400 and m.layermap_count == 1 and len(m.indices) == 2400
        assert (m.weight_off - m.base, m.layermap_off - m.base) == (0x3C + 4800, 0x3C + 4800 + 0x50 * 400)
        nrm = np.array([v[3:] for v in lf.verts])
        assert abs(np.linalg.norm(nrm, axis=1) - 1).max() < 1e-4 and (nrm[:, 1] > 0).all()
        for a, b, c in lf.triangles()[:50]:
            p = [lf.verts[i] for i in (a, b, c)]
            assert (p[1][2] - p[0][2]) * (p[2][0] - p[0][0]) - (p[1][0] - p[0][0]) * (p[2][2] - p[0][2]) < 0   # the donor's winding
        rec = np.frombuffer(lf.blob, dtype=np.uint8, count=0x50 * 400, offset=m.weight_off).reshape(-1, 5, 16)
        assert (rec[:, 0] == 0).all() and (rec[:, 1:, :].sum(axis=2) == 255).all() and (rec[:, 1:, n:] == 0).all()
        lm = lf.blob[m.layermap_off:m.layermap_off + 16]
        assert len(set(lm) - {255}) == n and lm[n:] == bytes([255]) * (16 - n)
    for chunk in f.chunks:
        assert chunk.kind in (fmb.KIND_NODE, fmb.KIND_LEAF_CMPL)
    ho, hd = struct.unpack_from('<11I', out, 0), struct.unpack_from('<11I', donor, 0)
    assert ho[:2] == hd[:2] and ho[3] == hd[3] and ho[7] == hd[7] == 0x100
    assert out[ho[5]:ho[6]] == donor[hd[5]:hd[6]] and out[ho[6]:ho[8]] == donor[hd[6]:hd[8]]   # tables 2 and 3: the donor's
    n_layers, off, n_ori, off2 = struct.unpack_from('<4I', out, ho[4])
    assert (n_layers, off, off2) == (len(layers), 16, 16 + 0x50 * len(layers))
    assert ho[5] - ho[4] == 16 + 0x50 * n_layers + 0x20 * n_ori
    counts = sorted({struct.unpack_from('<I', lf.blob, f.leaves[k].meshes[0].base)[0] for k, lf in f.leaves.items()})
    assert ho[9] == 3 * (2 * len(counts) + 1)
    sn = struct.unpack_from('<I', donor, 0x24)[0]
    assert sn == 9      # the donor: one layer, 3 passes x 3 records
    # the layer table's records are the stock layer record's fields (20 dwords) with the layers' slices and tile orientations
    rec0 = struct.unpack_from('<20i', out, ho[4] + 16)
    drec = struct.unpack_from('<20i', donor, hd[4] + 16)
    assert [rec0[k] == drec[k] for k in (2, 3, 4, 5, 6, 8, 9, 11, 12, 14, 15)] == [True] * 11
    assert struct.unpack_from('<4f', out, ho[4] + 16 + 0x40) == struct.unpack_from('<4f', donor, hd[4] + 16 + 0x40)


@test
def hkcms_build_real() -> None:
    """pylib/hkcms_build.py on EDF6's own collision (IG_HEIGEN601's as the donor): the writer re-emits the donor and the
    stock MADs it was checked on byte for byte, a built terrain / object collision parses with pylib/hktag.py and
    pylib/hkcms.py, decodes to its input within float rounding, passes hkcms.bound_problems and has the stock ground
    tile's item structure (selftest in the module: its own lines say what was compared)."""
    import bigmap
    import hkcms_build
    games = _legacy_games()
    if games is None:
        return
    g6 = games[2]
    mad = _game_read(g6, 'MAP/IG_HEIGEN601.MAD')
    donor = bigmap.Marc.parse(mad).get('collision.hkt')
    assert hkcms_build.write_mad(donor) == mad
    assert len(hkcms_build.selftest(donor)) >= 5


@test
def edf41_sk_maps_real() -> None:
    """tools/edf41_maps.py on SK_HEIGEN02 from the real games: the files files_of names; the MAC is a map bigmap verifies
    whose ground def is `sk_heigen02_fld2mgrid.fmep` placed at the origin with the stock .fmep cull distances, its
    externals.sgo lists the FMB, the field's members are gone and map.rmpa is EDF6's, its mapo entry holds one height per
    plant cell of the FMB's plant groups (the field's three grass textures, then its trees, bushes and small
    objects as the stock groups, each model one of the MAC's, its .trb in EDF6's form); the FMB is a piece of 625 leaves
    covering -1000..1000 whose heights are the field's; the MAD holds the ground body (named for the piece, a compound
    of 2500 tiles) and the objects' bodies, and every compound passes hkcms.bound_problems; the RAB has the colour array
    under the name EDF6's terrain looks up (<map>_color.dds) and the flat normal map; the weather pair and navmesh are the
    donor's."""
    import struct
    import bigmap
    import edf41_maps
    import fmb
    import fmex
    import hkcms
    import hktag
    import map_legacy
    import mdb
    import numpy as np
    import rmpa_legacy
    import rootcpk
    import sgo
    import sk_terrain
    games = _legacy_games()
    if games is None:
        return
    steam = os.path.dirname(rootcpk.DEFAULT_GAME)
    e41 = edf41_maps.Archives(os.path.join(steam, 'Earth Defense Force 4.1'))
    e6 = edf41_maps.Archives(rootcpk.DEFAULT_GAME)
    name = 'SK_HEIGEN02'
    files = edf41_maps.convert(name, e41, e6)
    assert list(files) == edf41_maps.files_of(name) and 'SK_HEIGEN02' in edf41_maps.SK_MAPS
    mac = bigmap.Marc.parse(files['MAP/SK_HEIGEN02.MAC'])
    members = dict(mac.files)
    assert not bigmap.verify(files['MAP/SK_HEIGEN02.MAC'])
    assert not [n for n in members if n.endswith(('.shfb', '.cols.sgo', '.norm.sgo'))]
    assert rmpa_legacy.is_edf6(members['map.rmpa'])
    assert sgo.plain(sgo.read(members['externals.sgo'])[1]) == {'resource': ['sk_heigen02_fld2mgrid.fmb']}
    mb = bigmap.MapB(members['map.mapb'])
    ground = [i for i in range(mb.rec_n) if mb.def_name(mb.def_index(i)) == 'sk_heigen02_fld2mgrid.fmep']
    assert len(ground) == 1 and mb.pos(ground[0]) == (0.0, 0.0, 0.0)
    d = mb.def_off + mb.def_index(ground[0]) * bigmap.DEF
    assert struct.unpack_from('<2f', mb.raw, d + 0x18) == (100.0, 1000.0) and mb.def_type(mb.def_index(ground[0])) == 3
    assert not any(n.endswith('.shfb') for n in (mb.def_name(k) for k in range(mb.def_n)))
    mo = bigmap.MapO(members['map.mapo'])
    ent = mo.ent_off + ground[0] * bigmap.ENT
    pair_at = ent + struct.unpack_from('<i', mo.raw, ent)[0]
    # the plants' heights: one per occupied cell of the FMB's grass group (pylib/fmex.py), the ground under it
    count, rel = struct.unpack_from('<2i', mo.raw, pair_at)
    assert pair_at > ent and rel == 8
    plants = fmex.read_table2(files['MAP/SK_HEIGEN02_FLD2MGRID.FMB'])
    assert [g.name for g in plants] == ['草・花', '樹木', '低木', '小物ランダム']
    assert count == sum(int((g.kind != 0xFF).sum()) for g in plants) and int((plants[0].kind != 0xFF).sum()) > 100000
    assert {t.file for t in plants[0].types} == {'sk_grassleaf01_1.dds', 'sk_grassleaf01_2.dds', 'sk_flower01.dds'}
    for g in plants[1:]:      # the models: kind 1, <model>.tree / .mls for the MAC's <model>.trb / .mlb (in EDF6's form)
        for t in g.types:
            stem, ext = t.file.rsplit('.', 1)
            assert t.kind == 1 and ext in ('tree', 'mls') and stem + {'tree': '.trb', 'mls': '.mlb'}[ext] in members
    assert all(len(sgo.plain(sgo.read(d)[1])['rules'][0]) == 8 for n, d in members.items() if n.endswith('.trb'))
    # the FMB
    f = fmb.parse(files['MAP/SK_HEIGEN02_FLD2MGRID.FMB'])
    assert len(f.leaves) == 625 and f.count == 1249
    pts = np.array(fmb.points(f))
    assert pts[:, 0].min() == -1000.0 and pts[:, 0].max() == 1000.0 and pts[:, 2].min() == -1000.0
    s = sk_terrain.parse(dict(map_legacy.parse_marc_lenient(_game_read(games[0], 'MAP/SK_HEIGEN02.MAC'))[1])['sk_heigen02_fld2mgrid.shfb'])
    lat = sk_terrain.lattice(s)
    ix, iz = np.rint((pts[:, 0] + 1000) / 4).astype(int), np.rint((pts[:, 2] + 1000) / 4).astype(int)
    assert np.abs(pts[:, 1] - lat[iz, ix]).max() < 1e-4
    # the MAD
    raw = bigmap.Marc.parse(files['MAP/SK_HEIGEN02.MAD']).get('collision.hkt')
    tag = hktag.Tag(raw)
    bodies = hkcms.bodies(tag)
    ground_name = 'sk_heigen02_fld2mgrid.sk_heigen02_fld2mgrid'
    assert ground_name in bodies and {'sk_rockm01.sk_rockm01', 'sk_knoll01.sk_knoll01'} <= set(bodies)
    comp = hkcms.Compound(tag, bytearray(raw), bodies[ground_name])
    assert len(comp.instances) == 2500
    for k in bodies.values():
        if tag.item(k)[0] == 'hknpCompoundShape':
            assert hkcms.bound_problems(tag, raw, k) == []
    # the RAB
    rab = {x.name: x for x in mdb.rab_read(files['MAP/SK_HEIGEN02.RAB']).files}
    assert {'sk_heigen02_color.dds', 'sk_heigen02_normal.dds'} <= set(rab) and not [n for n in rab if '.cols.' in n]
    color = mdb.cmpl_decompress(rab['sk_heigen02_color.dds'].stored)
    assert color[:4] == b'DDS ' and struct.unpack_from('<I', color, 128 + 12)[0] == 8
    assert files['MAP/SK_HEIGEN02_CLOUDY.MAE'] == e6.read('IG_HEIGEN507_CLOUDY.MAE')
    assert files['MAP/SK_HEIGEN02.NAVMESH.HKT'] == e6.read('IG_HEIGEN507.NAVMESH.HKT')


def _stand_in_fmb(stray: bytes = b'') -> bytes:
    """An FMB's header and trailing tables without chunks: a 16-byte table 1, an empty table 2, 0x100 jitter records,
    two shader records and `stray` bytes after them."""
    import struct
    t1 = 0x2C
    t2 = t1 + 16
    t3 = t2 + 8
    t4 = t3 + 0x1000
    head = struct.pack('<4sIII7I', b'FMB\0', 0x110, 0, 0x2C, t1, t2, t3, 0x100, t4, 2, 0)
    jit = bytes((k * 37 + 11) & 0xFF for k in range(0x1000))
    return head + bytes([0xAA]) * 16 + struct.pack('<Ii', 0, 8) + jit + bytes(range(56)) + stray


@test
def fmex_plants_offline() -> None:
    """pylib/fmex.py without the game: with_plants writes the plant groups as table 2 (read_table2 reads back every
    group, type, string, SGO and both grids; the jitter of an empty cell is 0), puts the new jitter table at table 3 and
    the strings after table 4's records, keeps table 1 and table 4's records where and as they were and drops what the
    old table 2 had after them; heights_at interpolates in pylib/fmb_build.py's two triangles of a quad (A B D / A D C)
    and is exact on a plane; plant_heights lists the occupied cells row major at their jittered positions."""
    import struct
    import fmex
    import numpy as np
    import sgo
    old = _stand_in_fmb(b'stray strings')
    params = sgo.write(258, {'colors': [[255, 255, 255], [192, 192, 192]]})
    g1 = fmex.Group('草・花', 1.0, (-2.0, -3.0), [
        fmex.PlantType(0, 'sk_grassleaf01_1', 'sk_grassleaf01_1.dds', (0.0, 1.0, 0.5, 0.0), (2.8, 0.7, 2.8), (4.8, 1.3, 4.8), params),
        fmex.PlantType(0, 'sk_flower01', 'sk_flower01.dds', (1.0, 0.0, 1.0, 0.0), (2.8, 0.7, 2.8), (2.8, 0.7, 2.8))],
        np.array([[0, 255, 1, 0], [255, 255, 0, 1], [1, 0, 255, 255]], dtype=np.uint8),
        np.array([[5, 9, 200, 7], [1, 2, 3, 4], [250, 0, 6, 8]], dtype=np.uint8))
    g2 = fmex.Group('樹木', 2.0, (10.0, 20.0), [fmex.PlantType(1, 'tree', 'sk_tree10m01.tree', (0.0,) * 4, (1.0,) * 3, (1.0,) * 3)],
                    np.array([[0, 255], [255, 0], [0, 0], [255, 255], [0, 255]], dtype=np.uint8),
                    np.arange(10, dtype=np.uint8).reshape(5, 2))
    jit = bytes((k * 7) & 0xFF for k in range(0x1000))
    new = fmex.with_plants(old, [g1, g2], jit)
    t1, t2, t3, n3, t4, n4 = struct.unpack_from('<6I', new, 0x10)
    assert (t1, t2, n3, n4) == (0x2C, 0x2C + 16, 0x100, 2) and new[t1:t2] == bytes([0xAA]) * 16
    assert new[t3:t4] == jit and new[t4:t4 + 56] == bytes(range(56)) and b's\0t\0r\0a\0y' not in new
    back = fmex.read_table2(new)
    assert [g.name for g in back] == ['草・花', '樹木']
    for a, b in zip(back, [g1, g2]):
        assert (a.cell, a.origin) == (b.cell, b.origin) and len(a.types) == len(b.types)
        for ta, tb in zip(a.types, b.types):
            assert (ta.kind, ta.name, ta.file, ta.color) == (tb.kind, tb.name, tb.file, tb.color)
            assert np.allclose(ta.size_min, tb.size_min) and np.allclose(ta.size_max, tb.size_max)
            assert (ta.params is None) == (tb.params is None) and (not ta.params or sgo.read(ta.params) == sgo.read(tb.params))
        assert (a.kind == b.kind).all() and (a.jitter == np.where(b.kind == 0xFF, 0, b.jitter)).all()
    assert fmex.with_plants(new, [g1, g2], jit) == new        # the stray strings of a table 2 being replaced are dropped
    lat = np.array([[0.0, 0.0], [0.0, 1.0]])                   # A = B = C = 0, D = 1
    h = fmex.heights_at(lat, 0.0, 0.0, 4.0, np.array([3.0, 1.0, 2.0]), np.array([1.0, 3.0, 2.0]))
    assert np.allclose(h, [0.25, 0.25, 0.5])
    zz, xx = np.mgrid[0:6, 0:7] * 4.0                           # lattice vertex (i, j) at x = -100 + 4 j, z = 50 + 4 i
    plane = 2.0 * (xx - 100.0) + 3.0 * (zz + 50.0) + 1.0
    x, z = np.array([-98.7, -82.1, -76.01]), np.array([51.0, 59.5, 69.8])
    assert np.allclose(fmex.heights_at(plane, -100.0, 50.0, 4.0, x, z), 2.0 * x + 3.0 * z + 1.0)
    got = fmex.plant_heights(g1, jit, lambda x, z: x * 1000.0 + z)
    rows, cols = np.nonzero(g1.kind != 0xFF)
    j = np.frombuffer(jit, dtype='<f4').reshape(256, 4)[g1.jitter[rows, cols]]
    want = ((cols + j[:, 1]) * 1.0 - 2.0) * 1000.0 + ((rows + j[:, 2]) * 1.0 - 3.0)
    assert len(got) == 7 and np.allclose(got, want.astype('<f4'))


@test
def rmpa_wire_links_offline() -> None:
    """pylib/rmpa_legacy.py's wire links without the game: in a 4.1 map.rmpa a wire waypoint's link to another route
    holds the index of the target in the mapb's wire point list; with the wires given (map_legacy.cable_graph) such a
    link becomes the section index of the waypoint at that point, whether it is >= the route's count or below it (where
    the old reading took it as a route index and joined unrelated waypoints), a route link the wires pair stays one, and
    without the wires the old behaviour stays (links past the route's end dropped)."""
    import struct
    import rmpa_legacy

    def wp(i: int, links: list[int], xyz: tuple[float, float, float]) -> dict:
        return {'index': i, 'links': links, 'id': 100 + i, 'namelen': 0, 'name': '', 'sgo': None,
                'pos': tuple(struct.pack('>f', v) for v in xyz) + (struct.pack('>f', 1.0),)}

    a = [(0.0, 5.0, 0.0), (30.0, 5.0, 0.0), (60.0, 5.0, 0.0)]
    b = [(0.0, 5.0, 40.0), (30.0, 5.0, 40.0)]
    # the wire points in another order than the routes: [dummy, B0, B1, A0, A1, A2]; A0 hangs a wire to B1
    pts = [(0.0, 0.0, 0.0, 0)] + [p + (1,) for p in b] + [p + (0,) for p in a]
    pairs = [(3, 4), (4, 5), (1, 2), (3, 2)]
    sec = {'namelen': 0, 'name': '', 'sets': [
        {'namelen': 1, 'name': 'cable_elec_wire_default_01', 'items': [wp(0, [1, 2], a[0]), wp(1, [0, 2], a[1]), wp(2, [1], a[2])]},
        {'namelen': 1, 'name': 'cable_elec_wire_twin_01', 'items': [wp(0, [1], b[0]), wp(1, [0, 3], b[1])]}]}
    dropped: list = []
    remapped: list = []
    out = rmpa_legacy._routes6([sec], dropped, (pts, pairs), remapped)[0]
    links = [w['links'] for w in out['wps']]
    assert links == [[1, 4], [0, 2], [1], [4], [3, 0]], links      # A0 -> B1 (section index 4), B1 -> A0 (0)
    assert dropped == [] and sorted(r[3] for r in remapped) == [0, 4]
    dropped = []
    old = rmpa_legacy._routes6([sec], dropped)[0]
    assert [w['links'] for w in old['wps']] == [[1, 2], [0, 2], [1], [4], [3]] and dropped == [('cable_elec_wire_twin_01', 1, 3)]


@test
def fmb_plants_stock_real() -> None:
    """The plant format pylib/fmex.py writes, on EDF6's own IG_HEIGEN507: read_table2 reads its 4 groups (grass and
    flowers, trees, bushes, small objects: 2500 x 2500 cells of 1 m from (-1250, -1250)), the heights of the ground
    record's mapo entry are one per occupied cell of all 4 groups (159441), each the ground under the cell's jittered
    position (sampled: the FMB's own triangles, within 5 cm); and every leaf mesh of it and of IG_HAIKYOPLANE01 names
    at +0x28 the pass-0 record of its layer count in the file's shader table (pylib/fmb_build.py shader_index)."""
    import struct
    import bigmap
    import fmb
    import fmex
    import numpy as np
    games = _legacy_games()
    if games is None:
        print('skip  fmb_plants_stock_real: no EDF4.1 / EDF5 / EDF6 here')
        return
    g6 = games[2]
    for name in ('IG_HAIKYOPLANE01', 'IG_HEIGEN507'):
        raw = _game_read(g6, f'MAP/{name}.FMB')
        f = fmb.parse(raw)
        t4, n4 = struct.unpack_from('<2I', raw, 0x20)
        recs = [struct.unpack_from('<7I', raw, t4 + 28 * k) for k in range(n4)]
        for lf in f.leaves.values():
            for m in lf.meshes:
                r = recs[m.grid[7]]
                assert (r[0], r[2], r[3]) == (m.grid[6] >> 8, 0, 0), (name, m.grid, r)
    groups = fmex.read_table2(raw)
    assert len(groups) == 4 and all((g.cell, g.origin, g.kind.shape) == (1.0, (-1250.0, -1250.0), (2500, 2500)) for g in groups)
    assert [len(g.types) for g in groups] == [5, 15, 4, 1] and {t.kind for t in groups[0].types} == {0}
    assert all(t.kind == 1 for g in groups[1:] for t in g.types)
    mac = dict(bigmap.Marc.parse(_game_read(g6, 'MAP/IG_HEIGEN507.MAC')).files)
    mo = bigmap.MapO(mac['map.mapo'])
    pair = mo.ent_off + struct.unpack_from('<i', mo.raw, mo.ent_off)[0]
    count, rel = struct.unpack_from('<2i', mo.raw, pair)
    assert count == sum(int((g.kind != 0xFF).sum()) for g in groups) == 159441 and rel == 8
    heights = np.frombuffer(mo.raw, dtype='<f4', count=count, offset=pair + 8)
    jit = raw[struct.unpack_from('<I', raw, 0x18)[0]:][:0x1000]
    tris = np.array([[lf.verts[i][:3] for i in t] for lf in f.leaves.values() for t in lf.triangles()])
    lo, hi = tris.min(1), tris.max(1)

    def ground(x: np.ndarray, z: np.ndarray) -> np.ndarray:
        out = []
        for px, pz in zip(x, z):
            for a, b, c in tris[(lo[:, 0] <= px) & (hi[:, 0] >= px) & (lo[:, 2] <= pz) & (hi[:, 2] >= pz)]:
                d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
                l1 = ((b[2] - c[2]) * (px - c[0]) + (c[0] - b[0]) * (pz - c[2])) / d
                l2 = ((c[2] - a[2]) * (px - c[0]) + (a[0] - c[0]) * (pz - c[2])) / d
                if min(l1, l2, 1 - l1 - l2) >= -1e-6:
                    out.append(l1 * a[1] + l2 * b[1] + (1 - l1 - l2) * c[1])
                    break
            else:
                out.append(np.nan)
        return np.array(out)

    start = 0
    for g in groups:
        n = int((g.kind != 0xFF).sum())
        pick = np.linspace(0, n - 1, 30).astype(int)
        sample = fmex.Group(g.name, g.cell, g.origin, g.types, np.full(g.kind.shape, 0xFF, np.uint8), g.jitter)
        rows, cols = np.nonzero(g.kind != 0xFF)
        sample.kind[rows[pick], cols[pick]] = 0
        assert np.allclose(fmex.plant_heights(sample, jit, ground), heights[start + pick], atol=0.05), g.name
        start += n


@test
def edf41_sk_grass_real() -> None:
    """The vegetation table of EDF4.1's seven SK fields (pylib/sk_terrain.py vegetation): a square grid over the 2000 m
    field (2000 x 2000 cells of 1 m, or 1000 x 1000 of 2 m), every cell's type one of the table's or 0xFF, kinds 1
    (grass / flower cards, a .dds the map's grass.merge.sgo names; one unused tree entry) and 2 (.trb / .mlb models),
    256 jitter records; and
    the grid's rows are z: on the steep ground of SK_VALLEY01 and SK_SANGAKU01 grass grows in cells of a third the
    average slope this way round and not transposed. sk_map.grass_group keeps exactly the grass cells."""
    import map_legacy
    import numpy as np
    import sgo
    import sk_map
    import sk_terrain
    games = _legacy_games()
    if games is None:
        print('skip  edf41_sk_grass_real: no EDF4.1 / EDF5 / EDF6 here')
        return
    for name in ('SK_CRATER01', 'SK_HEIGEN02', 'SK_KAIGAN01', 'SK_SANGAKU01', 'SK_SANGAKU02', 'SK_STEPMOUNTAIN01',
                 'SK_VALLEY01'):
        members = dict(map_legacy.parse_marc_lenient(_game_read(games[0], f'MAP/{name}.MAC'))[1])
        shfb = [d for n, d in members.items() if n.endswith('.shfb')][0]
        s = sk_terrain.parse(shfb)
        veg = sk_terrain.vegetation(shfb, s.extent[0])
        side = veg.kind.shape[0]
        assert veg.kind.shape == (side, side) and side * veg.cell == 2000.0 and side in (1000, 2000), name
        assert len(veg.jitters) == 0x1000 and set(np.unique(veg.kind).tolist()) <= set(range(len(veg.types))) | {0xFF}
        merge = {k.lower() for k in sgo.plain(sgo.read(members['grass.merge.sgo'])[1])}
        for k, t in enumerate(veg.types):
            assert t.kind in (1, 2), (name, t)
            if t.kind == 1 and t.name.endswith('.dds'):
                assert t.name[:-4].lower() in merge, (name, t.name, merge)
            elif t.kind == 1:      # SK_CRATER01's sk_tree10m01.trb of kind 1: no cell uses it
                assert not (veg.kind == k).any(), (name, t.name)
            else:
                assert t.name.endswith(('.trb', '.mlb')), (name, t.name)
        g = sk_map.grass_group(veg, -1000.0, -1000.0)
        grass = [k for k, t in enumerate(veg.types) if t.kind == 1 and t.name.endswith('.dds')]
        assert int((g.kind != 0xFF).sum()) == int(np.isin(veg.kind, grass).sum()) > 0
        if name in ('SK_VALLEY01', 'SK_SANGAKU01'):
            step = (s.height - 1) / side
            idx = np.minimum((np.arange(side) * step).astype(int), s.height - 1)
            h = s.heights[np.ix_(idx, idx)].astype(np.float64)
            gz, gx = np.gradient(h, veg.cell)
            slope = np.hypot(gx, gz)
            here = np.isin(veg.kind, grass)
            assert slope[here].mean() < slope.mean() / 3 and slope[here.T].mean() > slope.mean() / 2, name


@test
def edf41_map_wires_real() -> None:
    """The wires of EDF4.1's maps on the real files: every wire route link of NW_HILLYCITY_LIGHT / NW_KASENJIKI01 /
    NW_SINSUIKOUEN that the old conversion dropped (41 / 22 / 44) or took for a route index while it named a wire point
    is resolved through the mapb's wire points (156 / 22 / 140), each to a waypoint the wire block pairs with the linking
    one; and with_wire_textures puts the textures of NW_UNDERGROUND01's wire types (wire_test, nw_wire_black) into its
    MAC as the RAB's DDS bytes before map.mapo, and nothing into SK_HEIGEN02's (no wire pairs)."""
    import bigmap
    import edf41_maps
    import map_legacy
    import mdb
    import rmpa_legacy
    games = _legacy_games()
    if games is None:
        print('skip  edf41_map_wires_real: no EDF4.1 / EDF5 / EDF6 here')
        return
    g41 = games[0]
    for name, fixed, left in (('NW_HILLYCITY_LIGHT', 156, 0), ('NW_KASENJIKI01', 22, 0), ('NW_SINSUIKOUEN', 140, 2)):
        mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(_game_read(g41, f'MAP/{name}.MAC')))
        cables = map_legacy.cable_graph(mac.get('map.mapb'))
        t5, _ = rmpa_legacy.decode5(mac.get('map.rmpa'))
        assert len(rmpa_legacy.tree_from_legacy(t5)['dropped_links']) in (41, 22, 44)
        tree = rmpa_legacy.tree_from_legacy(t5, cables)
        assert len(tree['remapped_links']) == fixed and len(tree['dropped_links']) == left, (name, len(tree['remapped_links']), len(tree['dropped_links']))
        pts, pairs = cables
        at: dict = {}
        for i, p in enumerate(pts):
            at.setdefault(rmpa_legacy._pos_key(p[:3]), []).append(i)
        edges = {frozenset(p) for p in pairs}
        wps = tree['routes']['list'][0]['wps']
        by_index = {w['index']: w for w in wps}
        for route, index, link, target in tree['remapped_links']:
            base = [r for r in tree['routes']['list'][0]['routes'] if r['name'] == route][0]['idx'][0]
            a = at[rmpa_legacy._pos_key(rmpa_legacy._wp_xyz(by_index[base + index]))]
            b = at[rmpa_legacy._pos_key(rmpa_legacy._wp_xyz(wps[target]))]
            assert any(frozenset((x, y)) in edges for x in a for y in b), (name, route, index, link)
        assert rmpa_legacy.is_edf6(rmpa_legacy.encode6(tree))
    mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(_game_read(g41, 'MAP/NW_UNDERGROUND01.MAC')))
    rab = _game_read(g41, 'MAP/NW_UNDERGROUND01.RAB')
    edf41_maps.with_wire_textures(mac, rab)
    names = [n for n, _ in mac.files]
    tex = {f.name: f.data for f in mdb.rab_read(rab).files}
    assert names[-3:] == ['wire_test.dds', 'nw_wire_black.dds', 'map.mapo'], names[-4:]
    assert mac.get('wire_test.dds') == tex['wire_test.dds'] and mac.get('nw_wire_black.dds')[:4] == b'DDS '
    mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(_game_read(g41, 'MAP/SK_HEIGEN02.MAC')))
    before = list(mac.files)
    edf41_maps.with_wire_textures(mac, _game_read(g41, 'MAP/SK_HEIGEN02.RAB'))
    assert mac.files == before


@test
def edf41_maps_valley_files_real() -> None:
    """tools/edf41_maps.py converts SK_VALLEY01 (grass on 2 m cells, wires on an SK map) into a temporary folder, and the
    files read back from it: the MAC verifies and carries wire_test.dds; every leaf of the FMB draws with the pass-0
    shader record of its layer count; the FMB's grass group covers the field from its origin and the mapo entry of the
    ground has one height per plant cell of all its groups, each the FMB's ground under the cell's jittered position."""
    import struct
    import tempfile
    import bigmap
    import edf41_maps
    import fmb
    import fmex
    import numpy as np
    import rootcpk
    games = _legacy_games()
    if games is None:
        print('skip  edf41_maps_valley_files_real: no EDF4.1 / EDF5 / EDF6 here')
        return
    steam = os.path.dirname(rootcpk.DEFAULT_GAME)
    e41 = edf41_maps.Archives(os.path.join(steam, 'Earth Defense Force 4.1'))
    e6 = edf41_maps.Archives(rootcpk.DEFAULT_GAME)
    with tempfile.TemporaryDirectory() as tmp:
        for rel, data in edf41_maps.convert('SK_VALLEY01', e41, e6).items():
            os.makedirs(os.path.join(tmp, os.path.dirname(rel)), exist_ok=True)
            with open(os.path.join(tmp, rel), 'wb') as f:
                f.write(data)

        def disk(rel: str) -> bytes:
            with open(os.path.join(tmp, rel), 'rb') as f:
                return f.read()
        mac_raw = disk('MAP/SK_VALLEY01.MAC')
        raw = disk('MAP/SK_VALLEY01_FLD2MGRID.FMB')
    assert not bigmap.verify(mac_raw)
    mac = bigmap.Marc.parse(mac_raw)
    assert mac.get('wire_test.dds')[:4] == b'DDS '
    f = fmb.parse(raw)
    t4, n4 = struct.unpack_from('<2I', raw, 0x20)
    for lf in f.leaves.values():
        m = lf.meshes[0]
        assert struct.unpack_from('<7I', raw, t4 + 28 * m.grid[7]) == (m.grid[6] >> 8, 1, 0, 0, 0, 0, 0)
    plants = fmex.read_table2(raw)
    assert all((g.cell, g.origin, g.kind.shape) == (2.0, (-1000.0, -1000.0), (1000, 1000)) for g in plants)
    grass = fmex.Group('', 2.0, (-1000.0, -1000.0), [], np.full((1000, 1000), 0xFF, np.uint8), plants[0].jitter)
    mb, mo = bigmap.MapB(mac.get('map.mapb')), bigmap.MapO(mac.get('map.mapo'))
    ground = [i for i in range(mb.rec_n) if mb.def_name(mb.def_index(i)) == 'sk_valley01_fld2mgrid.fmep'][0]
    ent = mo.ent_off + ground * bigmap.ENT
    pair = ent + struct.unpack_from('<i', mo.raw, ent)[0]
    count, rel = struct.unpack_from('<2i', mo.raw, pair)
    assert rel == 8 and count == sum(int((g.kind != 0xFF).sum()) for g in plants) > 10000
    heights = np.frombuffer(mo.raw, dtype='<f4', count=count, offset=pair + 8)
    pts = np.array(fmb.points(f))
    lat = np.full((501, 501), np.nan)
    lat[np.rint((pts[:, 2] + 1000) / 4).astype(int), np.rint((pts[:, 0] + 1000) / 4).astype(int)] = pts[:, 1]
    assert not np.isnan(lat).any()
    jit = raw[struct.unpack_from('<I', raw, 0x18)[0]:][:0x1000]
    want = np.concatenate([fmex.plant_heights(g, jit, lambda x, z: fmex.heights_at(lat, -1000.0, -1000.0, 4.0, x, z))
                           for g in plants])
    assert np.abs(want - heights).max() < 1e-3


@test
def edf41_trees_real() -> None:
    """Trees on the real files: map_legacy.trb_from_legacy turns 4.1's sk_tree10m01.trb / sk_tree3m01.trb into EDF6's files
    of the same names byte for byte (the first rule's appended 0), leaves an EDF6 .trb as it is; and EDF6's stock plant
    groups name their models <model>.tree / <model>.mls for the MAC's <model>.trb / <model>.mlb (IG_HEIGEN507,
    IG_KAIGAN502: every model type), the form sk_map.model_groups writes."""
    import bigmap
    import fmex
    import map_legacy
    games = _legacy_games()
    if games is None:
        print('skip  edf41_trees_real: no EDF4.1 / EDF5 / EDF6 here')
        return
    g41, _g5, g6 = games
    trb41 = dict(map_legacy.parse_marc_lenient(_game_read(g41, 'MAP/NW_KASENJIKI01.MAC'))[1])
    for name, host in (('sk_tree10m01.trb', 'IG_2000MCITY'), ('sk_tree3m01.trb', 'NW_KITAGUNICITY')):
        six = bigmap.Marc.parse(_game_read(g6, f'MAP/{host}.MAC')).get(name)
        src = trb41[name] if name in trb41 else dict(map_legacy.parse_marc_lenient(_game_read(g41, 'MAP/SK_HEIGEN02.MAC'))[1])[name]
        assert src != six and map_legacy.trb_from_legacy(src) == six, name
        assert map_legacy.trb_from_legacy(six) == six
    for name in ('IG_HEIGEN507', 'IG_KAIGAN502'):
        members = {n.lower() for n, _ in bigmap.Marc.parse(_game_read(g6, f'MAP/{name}.MAC')).files}
        models = [t for g in fmex.read_table2(_game_read(g6, f'MAP/{name}.FMB')) for t in g.types if t.kind == 1]
        assert len(models) > 15
        for t in models:
            stem, ext = t.file.rsplit('.', 1)
            assert stem.lower() + {'tree': '.trb', 'mls': '.mlb'}[ext] in members, (name, t.file)


@test
def edf41_story_pack_real() -> None:
    """EDF4.1's story pack from the real games (make_edf5_campaign PACK41): with EDF4.1 given it comes after the test
    range with the next content id, so no installed pack's id moves; an offline and an online mode on its own save
    file; one row per mission whose map and objects will be there (the maps tools/edf41_maps.py converts), each
    row's script written under Mods/MISSION/EDF41/<id>/ as bundled, its thumbnail 4.1's picture of it (the 16 maps
    tools/edf41_maps.py converts, the SK_* ones included: 9 missions on them have a row); a mission naming an object not
    ported is left out with the reason. Without EDF4.1, no pack."""
    import dsgo
    import edf41_maps
    import gamedir
    import mdb
    import rootcpk
    import sgo
    if not _e5c_have_game():
        return
    root41 = gamedir.find_other(gamedir.EDF41, near=rootcpk.DEFAULT_GAME)
    if root41 is None:
        return
    files, content, p = e5c.build(rootcpk.DEFAULT_GAME, root41=root41)
    plain, plain_content, _ = e5c.build(rootcpk.DEFAULT_GAME)
    assert (content.campaign, content.range) == (plain_content.campaign, plain_content.range) and not plain_content.edf41
    assert content.edf41 == content.range + 1
    modes = sgo.plain(sgo.read(files[e5c.CONFIG])[1]['ModeList'])
    ours = [m for m in modes if m[e5c.M_CONTENT] == content.edf41]
    assert [(m[e5c.M_ONLINE], m[e5c.M_TYPE], m[e5c.M_MST]) for m in ours] == [(0, 0, 'E41M.MST'), (1, 1, 'E41M.MST')]
    rows41 = [r for r in p['rows'] if r['group'] == e5c.PACK41.group]
    assert len(rows41) >= 40 and {r['map'] for r in rows41} <= {f'{n}.MAC' for n in edf41_maps.MAPS}
    assert sum(r['map'].startswith('SK_') for r in rows41) >= 9
    assert any(r['path'] == 'M126' for r in rows41)
    skipped = {path: why for g, path, why in p['skipped'] if g == e5c.PACK41.group}
    # 2026-10-15: the motherships (301, 401) and UfoRobo are the plugin's own classes: no story mission is left out any more
    assert not skipped and len(rows41) == len(e5c.edf41_text()['missions']) == 89, skipped
    assert [m[e5c.M_BGM] for m in ours] == [e5c.BGM41] * 2, "the modes' HQ music is not EDF4.1's"
    listed = dsgo.parse(files[e5c.PACK41.list]).root.get('table').items
    assert [r.items[2] for r in listed] == [f'EDF41/{r["path"]}' for r in rows41]
    assert {(r.items[7], r.items[8]) for r in listed} == {(0.0, e5c.BGM41)}, "not EDF4.1's HQ music"
    for r in rows41:
        for name in r['files']:
            assert files[f'MISSION/EDF41/{r["path"]}/{name}'] == e5c.edf41_script(r['path'], name)
    with open(e5c.EDF41_THUMBS, 'rb') as f:
        own = {x.name.upper(): x.stored for x in mdb.rab_read(f.read()).files}
    thumbs = mdb.rab_read(files[e5c.PACK41.image]).files
    assert [t.stored for t in thumbs] == [own[f'{r["path"].upper()}.DDS'] for r in rows41]
    assert all(e5c.managed(rel) for rel in files)
@test
def cas_hollow_real() -> None:
    """pylib/cas_graft.hollow on the real E515 CAS (UFO301's under UfoSmall515: 4.1's UFO has no CAS): the same size,
    players and clips (names, durations, frames) as E515's, every clip with no track; and the converted UFO301 / its
    ace ship exactly that, under the class, with E515's material_animation (an empty one crashed UfoSmall515's
    constructor on the real game: slot 0 enabled unchecked, EDF+0x6dc844)."""
    import struct
    import cas_graft
    import cas_legacy as cl
    import cas_pose
    import sgo
    if not _e5c_have_game():
        return
    import rootcpk
    import make_edf41_objects as m
    g6 = m.archives(rootcpk.DEFAULT_GAME)
    donor = g6['OBJECT/E515_IMPERIALUFO.CAS'].read('OBJECT', 'E515_IMPERIALUFO.CAS')
    out = cas_graft.hollow(donor)
    assert len(out) == len(donor)
    groups = [cas_graft._name(out, at) for at, _ in cl.cas_layout(out).tree.anmgroups]
    assert groups == [cas_graft._name(donor, at) for at, _ in cl.cas_layout(donor).tree.anmgroups] == ['main']
    a, b = cas_pose.CasPose(donor), cas_pose.CasPose(out)
    assert [c.name for c in a.clips] == [c.name for c in b.clips] and len(b.clips) == 5
    fields = [(struct.unpack_from('<IiffIII', a.data, x.at), struct.unpack_from('<IiffIII', b.data, y.at))
              for x, y in zip(a.clips, b.clips)]
    assert all(before[5] > 0 and after[5] == 0 and before[:5] == after[:5] for before, after in fields)
    with open(os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT', 'UFO301.CAS'), 'rb') as f:
        assert f.read() == out
    for name in ('UFO301', 'UFO301ACE'):
        with open(os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT', name + '.SGO'), 'rb') as f:
            d = sgo.load(data=f.read())
        assert d['xgs_scene_object_class'] == 'UfoSmall515' and len(d['material_animation']) >= 1
        assert d['animation_model'][1] == f'app:/object/{name.lower()}.cas'



@test
def edf41_objects_after_weapons() -> None:
    """The installer builds EDF4.1's objects after the weapons are written: a model both ship (EDF5's Blacker No.4.1,
    OBJECT/V505_TANKEDF4.MRAB, the 4.1 AI tank's look) counts as shared only once call_weapons.install has put it in
    the ledger as 'calls'. A version whose weapons recorded it in their own manifest only (.edf6vc_calls.json, before
    the ledger) left it unrecorded; built first, the upgrade's edf41_objects.build saw another mod's file and left the
    tank out (2026-10-12, the real install)."""
    text = src('tools/installer.py')
    body = text[text.index('def install(game: str'):]
    assert body.index('call_weapons.install(game, weapons)') < body.index('edf41_objects.build(game, material_compat=material_compat)')



@test
def mdb_insert_bone() -> None:
    """mdb.insert_bone on a small skinned model: the new bone goes in depth-first file order after its parent's
    subtree (depth_delta encodes that order: appended at the end it would read as the last bone's child), as the
    parent's last child, weightless with the parent's bind; every index past it moves up by one in the bones' links,
    the objects' bones and the skinned vertices' blend indices; inv_bind stays the inverse of the bind and
    depth_delta the stock rule. rename_bone gives the bone a name-table entry of its own."""
    import struct
    import mdb
    ident = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]

    def bone(i: int, parent: int, sib: int, child: int, name: int, kids: int, y: float) -> mdb.Bone:
        local = list(ident)
        local[13] = y
        return mdb.Bone(i, parent, sib, child, name, kids, 3, 0, 0, 0, 0, local, list(ident), [0.0, 0, 0, 1], [0.0, 0, 0, 1])

    # root(0) -> a(1) -> a1(2); root -> b(3)
    bones = [bone(0, -1, -1, 1, 0, 2, 0.0), bone(1, 0, 3, 2, 1, 1, 1.0), bone(2, 1, -1, -1, 2, 0, 2.0),
             bone(3, 0, -1, -1, 3, 0, 3.0)]
    w = mdb.bind_world(mdb.Mdb(0x20, ['root', 'a', 'a1', 'b'], bones, [], [], []))
    for b in bones:
        b.inv_bind = mdb.inverse_affine(w[b.index])
    d = mdb.depth_of(mdb.Mdb(0x20, [], bones, [], [], []))
    for i, b in enumerate(bones):
        b.depth_delta = (d[i + 1] - d[i]) if i + 1 < len(bones) else d[i]
    vdata = bytes([3, 2, 1, 0]) + struct.pack('<4f', 0.25, 0.25, 0.25, 0.25)
    mesh = mdb.Mesh(bytes((0, 1, 4, 0)), 0, 0, 20, [mdb.VElem(21, 0, 0, 'BLENDINDICES'), mdb.VElem(1, 4, 0, 'BLENDWEIGHT')],
                    0, vdata, b'')
    md = mdb.Mdb(0x20, ['root', 'a', 'a1', 'b'], bones, [mdb.Object(0, 3, [mesh])], [], [])
    at = mdb.insert_bone(md, 'g', 'a')
    assert at == 3 and [md.name_of(b.name) for b in md.bones] == ['root', 'a', 'a1', 'g', 'b']
    assert [(b.parent, b.sibling, b.child) for b in md.bones] == [(-1, -1, 1), (0, 4, 2), (1, 3, -1), (1, -1, -1),
                                                                   (0, -1, -1)]
    assert md.bones[1].child_count == 2 and md.objects[0].bone == 4
    assert list(md.objects[0].meshes[0].vdata[:4]) == [4, 2, 1, 0]
    w = mdb.bind_world(md)
    assert all(max(abs(x - y) for x, y in zip(mdb.inverse_affine(w[b.index]), b.inv_bind)) < 1e-6 for b in md.bones)
    d = mdb.depth_of(md)
    assert [b.depth_delta for b in md.bones] == [d[i + 1] - d[i] for i in range(4)] + [d[4]]
    mdb.rename_bone(md, 'b', 'body')
    assert md.name_of(md.bones[4].name) == 'body' and md.names[3] == 'b'


@test
def edf41_ufo401_real() -> None:
    """The shipped UFO401 under UfoSmall507: every MDB of its model has the bones the class finds by name (body,
    gun_side, gun: a missing one, wrong aim or EDF+0x529067), keeps every bone its ragdoll and its MAB locators name
    (center: kept for them; core is not renamed and the added bones are weightless), its 8 sounds (4.1's 7, then
    E507's eighth), E507's aim keys, the muzzle locator 銃口 on gun, and 4.1 Ufo10's pattern / ai_info / bullet_info."""
    import json
    import re
    import mdb
    import sgo
    base = os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT')
    with open(os.path.join(base, 'UFO401.SGO'), 'rb') as f:
        d = sgo.load(data=f.read())
    assert d['xgs_scene_object_class'] == 'UfoSmall507' and len(d['se_table']) == 8
    assert d['se_table'][-1] == '小型UFO507戦闘モード' and 'aim_range_info' in d and 'move_turn_info' in d
    assert 'center'.encode('utf-16le') in bytes.fromhex(d['animation_model'][2][4:])
    with open(os.path.join(base, 'RAGDOLL_UFO401_HAS_HKA.SHKT'), 'rb') as f:
        ragdoll = {s.decode() for s in re.findall(rb'[A-Za-z_][A-Za-z0-9_]{2,}', f.read())}
    import edf41_objects
    models = [x for x in mdb.rab_read(edf41_objects.read('OBJECT/UFO401.MRAB')).files if x.name.lower().endswith('.mdb')]
    assert len(models) == 3
    for x in models:
        md = mdb.mdb_read(x.data)
        names = [md.name_of(b.name) for b in md.bones]
        assert {'body', 'gun_side', 'gun', 'center'} <= set(names), (x.name, names)
        assert md.bones[names.index('gun')].parent == names.index('gun_side')
        assert md.bones[names.index('gun_side')].parent == names.index('center')
        assert md.bones[names.index('body')].parent == names.index('center')
        # 4.1's core mesh never turns toward the target: core is still core, and the three added bones are weightless
        assert 'core' in names and names.count('core') == 1
        added = {names.index(n) for n in ('body', 'gun_side', 'gun')}
        assert all(md.bones[i].kind == 0 for i in added)
        for ob in md.objects:
            for me in ob.meshes:
                idx, weight = mdb.read_elem(me, 'BLENDINDICES'), mdb.read_elem(me, 'BLENDWEIGHT')
                if idx and weight:
                    assert not any(w > 0 and int(i) in added for ws, iv in zip(weight, idx) for w, i in zip(ws, iv)), x.name
        assert {'center', 'partsA_r'} & ragdoll <= set(names)
    # the muzzle locator is on gun (507 fires along its bone: the aim shows in the shots), the others stay on center
    import mab_legacy as ml
    mab = d['animation_model'][2]
    nodes = {l.name: l.node for _i, l in ml.mab_locators(bytes.fromhex(mab[4:]) if isinstance(mab, str) else mab)}
    assert nodes['銃口'] == 'gun' and set(nodes.values()) == {'gun', 'center'}, nodes
    # 4.1 Ufo10's attack in 507's keys: pattern 0 (its attack pass), the tint formula 1, speed / interval / damage / turn
    def plain(v: object) -> object:
        return [plain(x) for x in v] if isinstance(v, list) else round(v.value, 6) if isinstance(v, sgo.Float) else v
    assert d['ai_attack_pattern'] == 0 and d['bullet_pattern'] == 1
    assert plain(d['ai_info']) == [[[2, 0.00375], [1, 0.2625]], 200.0, [600, 1200], [40.0, 20.0], [0.1, 1.5], [150, 210]]
    bi = plain(d['bullet_info'])
    assert bi[0] == -1 and bi[2] == 1 and bi[3] == [-1, 0.0] and bi[4:6] == [0.4, 1800.0] and bi[7] == 30, bi
    assert bi[1] == [[2, 0.02], [1, 0.8], [3, 10.0], [1, 1.0], [5, 2.0]] and bi[6] == [[2, 0.071429], [5, 1.0]]
    assert bi[10] == [[2, 0.0008], [1, -0.0018], [2, 0.06], [1, 0.0018]] and bi[11] == 0.0 and bi[13] == 0.0
    assert d['bullet_class'] == 'LaserBullet01' and len(d['bullet_custom_param']) == 11
    reg = json.load(open(os.path.join(ROOT, 'edf41port', 'objects.json'), encoding='utf-8'))
    assert any(o['object'] == 'OBJECT/UFO401.SGO' for o in reg['objects'])


@test
def edf41_ufo401ace_real() -> None:
    """The shipped DLC ace UFO401 (EDF41MP02/UFO401ACE.SGO) under UfoSmall507: UFO401's model and recipe with 4.1 Ufo10's
    two ace keys in 507's own keys (tools/make_edf41_objects.py UFO401ACE_ALIAS): the red tint as
    animation_model_param_change_table (every parameter it sets is one every material of each ufo401 MDB has), the
    flight speed 0.001875 L + 0.63125, engage distance 600, bolts down to every frame, reaction_scale 0.3; its own
    durability 300 / level_adjust 5; ufo401_ex_* gone. ufo401_ex_keys refuses a shape or a part 507 cannot carry, and
    Ufo10's defaults give UFO401's recipe."""
    import json
    import mdb
    import sgo
    import edf41_objects
    import make_edf41_objects as m

    def plain(v: object) -> object:
        return [plain(x) for x in v] if isinstance(v, list) else round(v.value, 6) if isinstance(v, sgo.Float) else v
    base = os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT')
    with open(os.path.join(base, 'EDF41MP02', 'UFO401ACE.SGO'), 'rb') as f:
        ace = sgo.load(data=f.read())
    with open(os.path.join(base, 'UFO401.SGO'), 'rb') as f:
        story = sgo.load(data=f.read())
    assert ace['xgs_scene_object_class'] == 'UfoSmall507'
    assert not [k for k in ace if k.startswith('ufo401_ex')]
    tint = plain(ace['animation_model_param_change_table'])
    assert tint == [[0, 'fall_off_offset', [0.0]], [0, 'fall_off_scale', [1.0]], [0, 'diffuse', [0.8, 0.05, 0.1, 1.0]]]
    assert ace['animation_model'][0][0].lower() == 'app:/object/ufo401.mrab'
    models = [x for x in mdb.rab_read(edf41_objects.read('OBJECT/UFO401.MRAB')).files if x.name.lower().endswith('.mdb')]
    for x in models:
        md = mdb.mdb_read(x.data)
        assert md.materials and all({e[1] for e in tint} <= {p.name for p in mt.params} for mt in md.materials), x.name
    ai, bi = plain(ace['ai_info']), plain(ace['bullet_info'])
    assert ai[0] == [[2, 0.001875], [1, 0.63125]] and ai[1] == 600.0 and ai[2:] == plain(story['ai_info'])[2:]
    assert bi[1] == [[2, 0.02], [1, 0.8], [3, 10.0], [1, 1.0], [5, 1.0]]
    assert bi[0] == plain(story['bullet_info'])[0] and bi[2:] == plain(story['bullet_info'])[2:]
    assert plain(ace['reaction_scale']) == 0.3 and 'reaction_scale' not in story
    assert plain(ace['game_object_durability']) == 300.0 and plain(ace['game_object_level_adjust']) == 5.0
    for k in ('ai_attack_pattern', 'bullet_pattern', 'bullet_class', 'se_table', 'aim_range_info', 'move_turn_info'):
        assert ace[k] == story[k], k
    # the converter's mapping: Ufo10's defaults are UFO401's recipe; the ace's values; refusals
    m.ufo401_recipe_is_default()
    assert m.ufo401_ex_keys([[0.5, 0.15], [1.0, 0.002, 5.0], 600.0, [10.0, 0.2, 1], 0.3]) == (
        [[2, 0.001875], [1, 0.63125]], 600.0, [[2, 0.02], [1, 0.8], [3, 10.0], [1, 1.0], [5, 1.0]], 0.3)
    for bad in ([[0.5, 0.15], [1.0, 0.004, 5.0], 600.0, [10.0, 0.2, 1], 0.3], [[0.5], [1.0, 0.002, 5.0], 600.0,
                [10.0, 0.2, 1], 0.3], [[0.5, 0.15], [1.0, 0.002, 5.0], 600.0, [10.0, 0.2, 1]]):
        try:
            m.ufo401_ex_keys(bad)
        except ValueError:
            continue
        raise AssertionError(f'ufo401_ex_keys took {bad}')
    for bad_members in ({'ufo401_ex_ChangeColor': [[0, 'diffuse', [1.0] * 5]]}, {'ufo401_ex_ChangeColor': [[0, 'diffuse', [1.0]]]}):
        try:
            m.ufo401_ace(dict(bad_members))
        except ValueError:
            continue
        raise AssertionError(f'ufo401_ace took {bad_members}')
    reg = json.load(open(os.path.join(ROOT, 'edf41port', 'objects.json'), encoding='utf-8'))
    assert any(o['object'] == 'OBJECT/EDF41MP02/UFO401ACE.SGO' and o['class'] == 'UfoSmall507' for o in reg['objects'])


# ---------------------------------------------------------------- EDF4.1 sounds (tools/edf41_sounds.py)


def _awe1(cues: list[tuple[str, int]]) -> bytes:
    """A version 1 AWE of (name, wave id) cues in the order given, laid out as EDF4.1 writes it."""
    import struct
    n = len(cues)
    ids_at = 0x14 + 4 * n
    at = ids_at + 2 * n
    offs = []
    for i, (name, _w) in enumerate(cues):
        offs.append(at - (0x14 + 4 * i))
        at += len(name) + 1
    return (b'AWBE' + struct.pack('<HHIII', 0x100, 0, n, 0x14, ids_at) + struct.pack(f'<{n}I', *offs)
            + struct.pack(f'<{n}H', *(w for _, w in cues)) + b''.join(name.encode() + b'\0' for name, _ in cues))


@test
def awe_v1_to_v2_synthetic() -> None:
    """pylib/awe.py: a version 1 list read and written as version 2 keeps every name and wave id, sorts by name, gains the
    float table at the offsets the header gives, and is padded to an even size."""
    import struct
    import awe
    v1 = _awe1([('B002', 7), ('A001_E', 3), ('A001', 5)])
    cues = awe.read_awe(v1)
    assert [(c.name, c.wave, c.seconds) for c in cues] == [('B002', 7, 0.0), ('A001_E', 3, 0.0), ('A001', 5, 0.0)]
    timed = [awe.Cue(c.name, c.wave, 1.5 + i) for i, c in enumerate(cues)]
    v2 = awe.write_awe2(timed)
    assert v2[:4] == b'AWBE' and struct.unpack_from('<HHIIII', v2, 4) == (0x200, 0, 3, 0x18, 0x18 + 12, 0x18 + 18)
    back = awe.read_awe(v2)
    assert [c.name for c in back] == ['A001', 'A001_E', 'B002'] and [c.wave for c in back] == [5, 3, 7]
    assert [c.seconds for c in back] == [3.5, 2.5, 1.5]
    assert len(v2) % 2 == 0 and awe.write_awe2(back) == v2
    assert len(awe.write_awe2([awe.Cue('AB', 1, 1.0)])) % 2 == 0 and len(awe.write_awe2([awe.Cue('A', 1, 1.0)])) % 2 == 0


@test
def awb_hca_seconds_synthetic() -> None:
    """pylib/awe.py: wave ranges from an AFS2 header (aligned starts, ends as stored) and a HCA's playing time (blocks'
    samples less the two mutes), with plain or high-bit-masked chunk tags."""
    import struct
    import awe

    def hca(blocks: int, mute_head: int, mute_tail: int, mask: int = 0) -> bytes:
        tag = bytes(c | mask for c in b'fmt') + b'\0'
        return b'HCA\0\x03\0\0\x60' + tag + b'\x01' + (48000).to_bytes(3, 'big') + struct.pack('>IHH', blocks, mute_head, mute_tail)

    assert awe.hca_seconds(hca(100, 0, 0)) == 102400 / 48000
    assert awe.hca_seconds(hca(100, 128, 654, 0x80)) == (102400 - 782) / 48000
    header = (b'AFS2' + bytes([2, 4, 2, 0]) + struct.pack('<IHH', 2, 0x20, 0) + struct.pack('<2H', 4, 9)
              + struct.pack('<3I', 0x30, 0x61, 0x90))
    assert awe.afs2_waves(header) == {4: (0x40, 0x61), 9: (0x80, 0x90)}
    try:
        awe.hca_seconds(b'HCA\0')
    except ValueError:
        pass
    else:
        raise AssertionError('a header without fmt is refused')


@test
def edf41_sounds_captions_synthetic() -> None:
    """tools/edf41_sounds.py: the caption key removes one terminal _E / _S; add_rows leaves every row of the table
    as it was, gives a missing key a row with EDF6's own member names (name = '' , caption = text, else the key: the game
    reads a row BY those names, an unnamed list is a null node and crashed M126) and keeps the row of a key the table has;
    a table whose rows are not laid out name / caption is refused."""
    import dsgo
    import edf41_sounds as s
    assert [s.cut_key(c) for c in ('A001AA', 'A001AA_E', 'Z105H_S', 'A_E_S', 'VT_B1')] == ['A001AA', 'A001AA', 'Z105H', 'A_E', 'VT_B1']

    def row(name: str, caption: str) -> dsgo.Node:
        return dsgo.Node([name, caption], {0: 'name', 1: 'caption'})

    rows = dsgo.Node([row('ス', '日本語'), row('ス', 'hold')], {0: 'K1', 1: 'K2'})
    table = dsgo.write(dsgo.Document(dsgo.Node([rows], {0: 'table'}), []))
    out = dsgo.parse(s.add_rows(table, ['K2', 'N1', 'N2'], {'N1': 'text one', 'K2': 'new'}))
    t = out.root.items[0]
    by = {t.names[i]: [str(x) for x in t.items[i].items] for i in t.names}
    # the radio's beeps (MusenBegin / MusenEnd, played through the voice path by every EDF5-era script) get a row marked
    # with the engine's no-subtitle text: without one the game showed '＊＊不明な再生キー＊＊' for 5 s after every radio line
    silent = {k: ['', s.NO_SUBTITLE] for k in s.SILENT_KEYS}
    assert s.SILENT_KEYS == ('MusenBegin', 'MusenEnd') and s.NO_SUBTITLE == '***no_subtitle***'
    assert by == {'K1': ['ス', '日本語'], 'K2': ['ス', 'hold'], 'N1': ['', 'text one'], 'N2': ['', 'N2'], **silent}, by
    assert [t.names[0], t.names[1]] == ['K1', 'K2']   # the rows kept their places
    # a table that has them keeps its own rows (no second row of a key)
    again = dsgo.parse(s.add_rows(s.add_rows(table, ['N1'], {}), ['N1'], {})).root.items[0]
    assert sorted(again.names.values()) == sorted(['K1', 'K2', 'N1', *s.SILENT_KEYS]), sorted(again.names.values())
    # 0x5DD5F8 compares the caption with exactly this text: the engine must still carry it (when the game is installed)
    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if os.path.isfile(dll):
        with open(dll, 'rb') as f:
            assert s.NO_SUBTITLE.encode('utf-16le') in f.read(), 'EDF.dll no longer knows the no-subtitle mark'
    # the invariant the game depends on: every row, ours included, answers get('name') and get('caption')
    assert all(r.names == {0: 'name', 1: 'caption'} and r.get('name') == r.items[0] and r.get('caption') == r.items[1] for r in t.items)
    unnamed = dsgo.Node([dsgo.Node(['ス', 'x'])], {0: 'K1'})
    bad = dsgo.write(dsgo.Document(dsgo.Node([unnamed], {0: 'table'}), []))
    try:
        s.add_rows(bad, ['N1'], {})
    except ValueError:
        pass
    else:
        raise AssertionError('rows without name / caption members are refused')


@test
def edf41_sounds_install() -> None:
    """tools/edf41_sounds.py on a stand-in game (conversion stubbed): writes every file of every group as edf41sound's, a
    second install converts nothing it converted already, a group one of whose files another mod placed is left out
    and that file untouched, no EDF4.1 writes nothing, a group with a source missing is left out, removal deletes what it wrote."""
    import tempfile
    import edf41_sounds as s
    import ledger
    made: list[str] = []

    def make(group: str, _r6: str, _r41: str) -> dict[str, bytes]:
        made.append(group)
        return {rel: group.encode() + rel.encode() for rel in s.GROUPS[group]}

    with tempfile.TemporaryDirectory() as game, \
            patched(s, make=make, missing=lambda g, *_: ['X.AWB'] if g == 'voice.EN' else [], sources_of=lambda *_: [],
                    fingerprint=lambda _p, _r41=None: {'f': [1, 2]}):
        os.makedirs(os.path.join(game, 'Mods'))
        written, skipped = s.install(game, None)
        assert not written and [g for g, _ in skipped] == list(s.GROUPS) and not os.listdir(os.path.join(game, 'Mods'))
        written, skipped = s.install(game, 'edf41')
        assert [g for g, _ in skipped] == ['voice.EN'] and 'voice.EN' not in made
        every = [rel for g, rels in s.GROUPS.items() if g != 'voice.EN' for rel in rels]
        assert len(written) == len(every) and set(made) == set(s.GROUPS) - {'voice.EN'}
        led = ledger.Ledger(game)
        assert all(led.owners(rel) == [s.OWNER] for rel in every)
        made.clear()
        assert s.install(game, 'edf41')[0] == [] and not made, made
        stamp_path = os.path.join(game, 'Mods', s.STAMP)
        with open(stamp_path, encoding='utf-8') as f:
            stamp = json.load(f)
        # Version 1 lacked named row members; version 3 cut embedded suffixes.
        # Both old installations must rebuild only the table, even with identical sources.
        for old_version in (1, 3):
            stamp['table']['version'] = old_version
            with open(stamp_path, 'w', encoding='utf-8') as f:
                json.dump(stamp, f)
            s.install(game, 'edf41')
            assert made == ['table'], (old_version, made)
            with open(stamp_path, encoding='utf-8') as f:
                assert json.load(f)['table']['version'] == s.VERSIONS['table']
            made.clear()
        s.remove(game)
        other = os.path.join(game, 'Mods', 'SOUND', 'PC', 'EDF41_WEAPON_SE.ACB')
        os.makedirs(os.path.dirname(other), exist_ok=True)
        with open(other, 'wb') as f:
            f.write(b'another mod')
        written, skipped = s.install(game, 'edf41')
        assert 'weapon' in [g for g, _ in skipped] and 'weapon' not in made
        with open(other, 'rb') as f:
            assert f.read() == b'another mod'
        s.remove(game)
        assert os.path.exists(other) and not os.path.exists(os.path.join(game, 'Mods', 'MISSION', 'EDF6_VOICETABLE.SC.SGO'))


@test
def edf41_asset_pack() -> None:
    """pylib/edf41src.py + tools/export_edf41_assets.py on a stand-in EDF4.1 and EDF6 (map archives and conversions
    stubbed): the export holds exactly what the converters read, with a manifest; from the pack the map archive reads
    the same bytes and both the maps and the sounds fingerprint as from EDF4.1 (a change of source converts nothing
    again); find prefers $EDF41_DIR, then $EDF41_ASSETS, the player's EDF4.1, the pack beside the installer; a file
    missing or of another size leaves its map / group out before converting (unavailable, the campaign plan's skip41;
    missing), one changed in place is refused when converting it (no half group); installer check (menu 5) reports the
    pack's faults; removal needs no source; a second export rewrites nothing and drops files the manifest leaves out."""
    import contextlib
    import io
    import tempfile
    from unittest import mock
    import edf41_maps
    import edf41_sounds as s
    import edf41src
    import export_edf41_assets as exp

    class FakeCpk:   # 4.1's map archives: each MAP file's bytes are made of its name
        def __init__(self, _root: str) -> None:
            pass

        def read(self, name: str) -> bytes:
            return f'map:{name.upper()}'.encode() * 3

        def fingerprint(self, name: str) -> list[int]:
            return [len(self.read(name)), 7]

    class FakeMenu:   # 4.1's MENU files in Root.cpk (tools/edf41_menu_bg.py Source)
        def __init__(self, _root: str) -> None:
            pass

        def read(self, rel: str) -> bytes:
            return f'menu:{rel}'.encode()

        def fingerprint(self, rel: str) -> list[int]:
            return [len(self.read(rel)), 9]

    def put(path: str, data: bytes) -> None:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)

    def get(path: str) -> bytes:
        with open(path, 'rb') as f:
            return f.read()

    def flip(path: str) -> None:   # one byte changed, the size kept
        data = bytearray(get(path))
        data[0] ^= 1
        put(path, bytes(data))

    with tempfile.TemporaryDirectory() as tmp:
        r41, r6, pack = (os.path.join(tmp, n) for n in ('edf41', 'edf6', 'pack'))
        put(os.path.join(r41, 'EDF41.exe'), b'')
        put(os.path.join(r41, 'Root.cpk'), b'')
        for rel in s.sources41():
            if not rel.endswith(('subtitles_zh.txt', 'subtitles_en.txt')):   # the subtitle text is optional
                put(os.path.join(r41, *rel.split('/')), f'4.1 {rel}'.encode())
        for rel in ('Root.cpk', f'SOUND/PC/{s.SE_BANK}'):
            put(os.path.join(r6, *rel.split('/')), b'6')
        os.makedirs(os.path.join(r6, 'Mods'))
        # what the converters read is what the pack holds
        assert [r for n in edf41_maps.MAPS for r in edf41_maps.sources(n)] == \
            [f'MAP/{f}' for n in edf41_maps.MAPS for f in edf41_maps._source(n, FakeCpk(''))]
        for g in s.GROUPS:
            assert {s.rel41(p, r41) for p in s.sources_of(g, r6, r41)} - {None} <= set(s.sources41()), g
        with patched(exp.edf41_maps, Archives=FakeCpk), patched(exp.edf41_menu_bg, Source=FakeMenu):
            exp.export(r41, pack, say=lambda _line: None)
        listed = edf41src.pack(pack).names
        assert len(listed) == 3 * len(edf41_maps.MAPS) + len(s.sources41()) - 2 + len(exp.edf41_menu_bg.SOURCES), len(listed)
        real_menu = exp.edf41_menu_bg.Source(pack)   # the HQ background's files from the pack: the same bytes and stamp
        assert all(real_menu.read(r) == FakeMenu('').read(r) and real_menu.fingerprint(r) == FakeMenu('').fingerprint(r)
                   for r in exp.edf41_menu_bg.SOURCES)
        assert edf41src.check(pack, full=True) == [] and os.path.isfile(os.path.join(pack, exp.README))
        # the same reads and fingerprints from the pack as from EDF4.1
        a = edf41_maps.Archives(pack)
        assert a.read('IG_CAVE01.MAC') == FakeCpk('').read('IG_CAVE01.MAC')
        assert edf41_maps._source('SK_CRATER01', a) == edf41_maps._source('SK_CRATER01', FakeCpk(''))
        for g in s.GROUPS:
            def fp(root: str, g: str = g) -> dict:
                return s.fingerprint([p for p in s.sources_of(g, r6, root) if os.path.isfile(p)], root)
            assert fp(pack) == fp(r41), g
            assert s.missing(g, r6, pack) == [] and s.corrupt(g, r6, pack) == [], g
        # where it is found
        env = {k: v for k, v in os.environ.items() if k not in ('EDF41_DIR', edf41src.ENV)}
        with mock.patch.dict(os.environ, env, clear=True), \
                patched(edf41src, beside_installer=lambda: pack), \
                patched(edf41src.gamedir, find_other=lambda *a, **k: None):
            assert edf41src.find(r6) == pack and e5c.find_edf41(r6) == pack and installer.find_edf41(r6) == pack
            assert '素材包' in edf41src.describe(pack) and edf41src.describe(None).startswith('没有找到')
            with patched(edf41src.gamedir, find_other=lambda *a, **k: r41):
                assert edf41src.find(r6) == r41   # the player's EDF4.1 before the pack beside the installer
                with mock.patch.dict(os.environ, {edf41src.ENV: pack}):
                    assert edf41src.find(r6) == pack
                    with mock.patch.dict(os.environ, {'EDF41_DIR': r41}):
                        assert edf41src.find(r6) == r41
            with patched(edf41src, beside_installer=lambda: os.path.join(tmp, 'none')):
                assert edf41src.find(r6) is None
            with contextlib.redirect_stdout(io.StringIO()) as out:
                assert installer.check_edf41_source(r6)
            assert '清单里的文件齐全' in out.getvalue(), out.getvalue()
        # the frozen installer: the pack next to its exe, else next to the installer whose menu 3 downloaded it
        # (unpacked into <that installer's folder>/<build>/: tools/testhub.py download_latest); none: the exe's own
        shell = os.path.join(tmp, 'share')
        os.makedirs(os.path.join(shell, 'build-2'))
        with mock.patch.object(sys, 'frozen', True, create=True):
            with mock.patch.object(sys, 'executable', os.path.join(shell, 'build-2', 'x.exe')):
                assert edf41src.beside_installer() == os.path.join(shell, 'build-2', edf41src.FOLDER)
                os.rename(pack, os.path.join(shell, edf41src.FOLDER))
                try:
                    assert edf41src.beside_installer() == os.path.join(shell, edf41src.FOLDER)
                    with mock.patch.object(sys, 'executable', os.path.join(shell, 'x.exe')):
                        assert edf41src.beside_installer() == os.path.join(shell, edf41src.FOLDER)
                finally:
                    os.rename(os.path.join(shell, edf41src.FOLDER), pack)
        # EDF6 on another drive than the pack (a share package unpacked to C:, Steam on D:): EDF6's files are not
        # the pack's, and os.path.relpath of such a pair raises; it must not stop the install (2026-10-17 review)
        if os.name == 'nt':
            other = 'Q:' if os.path.splitdrive(os.path.abspath(pack))[0].upper() != 'Q:' else 'R:'
            assert s.rel41(other + r'\EDF6\SOUND\PC\TIKYUUX_SE.ACB', pack) is None
            assert s.rel41(os.path.join(pack, 'SOUND', 'PC', s.SE_BANK), pack) == f'SOUND/PC/{s.SE_BANK}'
            for g in s.GROUPS:
                assert not [w for w in s.missing(g, other + r'\EDF6', pack) if '素材包' in w], g   # only EDF6's own
        # maps from the pack: written; current again from EDF4.1's same files; removed without a source
        converted: list[str] = []

        def convert(name: str, e41: object, _e6: object) -> dict[str, bytes]:
            converted.append(name)
            data = b''.join(e41.read(r.split('/', 1)[1]) for r in edf41_maps.sources(name))
            return {rel: data for rel in edf41_maps.files_of(name)}

        real_archives = edf41_maps.Archives
        e6 = FakeCpk(r6)   # the stand-in EDF6 has no archives: convert is stubbed, the donor's files are not read
        with patched(edf41_maps, convert=convert, Archives=lambda root: e6 if root == r6 else real_archives(root),
                     workers=lambda: 1):   # a stubbed convert runs here only
            written, skipped = edf41_maps.install(r6, pack)
            assert not skipped and converted == list(edf41_maps.MAPS), skipped
            converted.clear()
            with patched(edf41_maps, Archives=lambda root: FakeCpk(root) if root in (r41, r6) else real_archives(root)):
                assert edf41_maps.install(r6, r41) == ([], []) and not converted, converted
            assert edf41_maps.install(r6, pack) == ([], []) and not converted
            # a file missing: its map is left out before converting, and the campaign plan is told (skip41)
            gone = os.path.join(pack, 'MAP', 'SK_VALLEY01.RAB')
            keep = get(gone)
            os.remove(gone)
            why = dict(edf41_maps.unavailable(r6, pack))
            assert list(why) == ['SK_VALLEY01'] and '素材包缺少' in why['SK_VALLEY01'], why
            written, skipped = edf41_maps.install(r6, pack)
            assert [n for n, _ in skipped] == ['SK_VALLEY01'] and 'SK_VALLEY01' not in converted
            assert not os.path.exists(os.path.join(r6, 'Mods', 'MAP', 'SK_VALLEY01.MAC'))   # no half map left
            with contextlib.redirect_stdout(io.StringIO()) as out, patched(installer, find_edf41=lambda _g: pack):
                assert not installer.check_edf41_source(r6)
            assert 'SK_VALLEY01.RAB' in out.getvalue(), out.getvalue()
            put(gone, keep)
            # a file changed in place (same size): refused when it is read to convert, nothing of that map written
            city = os.path.join(pack, 'MAP', 'NW_SOUKOGAI01.MAD')
            flip(city)
            stamp = os.path.join(r6, 'Mods', edf41_maps.STAMP)
            with open(stamp, encoding='utf-8') as f:
                st = json.load(f)
            del st['NW_SOUKOGAI01']
            with open(stamp, 'w', encoding='utf-8') as f:
                json.dump(st, f)
            converted.clear()
            written, skipped = edf41_maps.install(r6, pack)
            assert [n for n, _ in skipped] == ['NW_SOUKOGAI01'] and '被改过' in skipped[0][1], skipped
            assert not os.path.exists(os.path.join(r6, 'Mods', 'MAP', 'NW_SOUKOGAI01.MAC'))
            assert edf41src.check(pack) == [] and edf41src.check(pack, full=True), 'the size alone does not see it'
            flip(city)
            edf41_maps.remove(r6)
            assert not ledger.Ledger(r6).owned_by(edf41_maps.OWNER)
        # sounds from the pack (conversion stubbed): written; current from EDF4.1's same files; changed / missing
        made: list[str] = []

        def make(group: str, _r6: str, _r41: str) -> dict[str, bytes]:
            made.append(group)
            return {rel: group.encode() for rel in s.GROUPS[group]}

        with patched(s, make=make):
            written, skipped = s.install(r6, pack)
            assert not skipped and made == list(s.GROUPS), (skipped, made)
            made.clear()
            assert s.install(r6, r41) == ([], []) and not made, made
            assert s.install(r6, pack) == ([], []) and not made
            awb = os.path.join(pack, 'SOUND', 'PC', s.VOICES['EN'][0])
            flip(awb)
            os.remove(os.path.join(r6, 'Mods', s.STAMP))   # not current: converted again, from the changed file
            written, skipped = s.install(r6, pack)
            assert [g for g, _ in skipped] == ['voice.EN'] and '被改过' in skipped[0][1] and 'voice.EN' not in made, skipped
            assert not any(os.path.exists(os.path.join(r6, 'Mods', *r.split('/'))) for r in s.GROUPS['voice.EN'])
            flip(awb)
            os.remove(os.path.join(pack, 'mods', 'Plugins', 'subtitles.txt'))   # listed: a pack is used whole
            written, skipped = s.install(r6, pack)
            assert [g for g, _ in skipped] == ['table'] and '素材包缺少' in skipped[0][1], skipped
            s.remove(r6)
            assert not ledger.Ledger(r6).owned_by(s.OWNER)
        # a second export: puts back what was removed, rewrites nothing else, drops an unlisted file
        put(os.path.join(pack, 'MAP', 'EXTRA.MAC'), b'x')
        lines: list[str] = []
        with patched(exp.edf41_maps, Archives=FakeCpk), patched(exp.edf41_menu_bg, Source=FakeMenu):
            exp.export(r41, pack, say=lines.append)
        assert [x for x in lines if x.startswith('  导出')] == ['  导出 mods/Plugins/subtitles.txt'], lines
        assert not os.path.exists(os.path.join(pack, 'MAP', 'EXTRA.MAC')) and edf41src.check(pack, full=True) == []
        with contextlib.redirect_stdout(io.StringIO()):
            assert exp.main(['--check', pack, '--full']) == 0


def _sound_games() -> tuple[str, str] | None:
    """(EDF6, EDF4.1) folders, or None off a developer's machine (CI has no game)."""
    import gamedir
    import rootcpk
    r41 = gamedir.find_other(gamedir.EDF41, rootcpk.DEFAULT_GAME)
    sound = os.path.join(rootcpk.DEFAULT_GAME, 'SOUND', 'PC', 'TIKYUUX_SE.ACB')
    return (rootcpk.DEFAULT_GAME, r41) if r41 and os.path.isfile(sound) else None


@test
def edf6_awe_lists_round_trip() -> None:
    """pylib/awe.py reads and writes EDF6's own lists: every version 2 AWE in SOUND/PC comes back byte for byte, and
    EDF6's version 1 TIKYUU6_VOICE_LIST.AWE with the JA list's seconds becomes the JA list (the 4.1 -> 6 conversion)."""
    import awe
    games = _sound_games()
    if games is None:
        return
    folder = os.path.join(games[0], 'SOUND', 'PC')
    checked = 0
    for name in sorted(os.listdir(folder)):
        if name.upper().endswith('.AWE'):
            with open(os.path.join(folder, name), 'rb') as f:
                data = f.read()
            if data[4:6] == b'\x00\x02':
                assert awe.write_awe2(awe.read_awe(data)) == data, name
                checked += 1
    assert checked >= 10, checked
    with open(os.path.join(folder, 'TIKYUU6_VOICE_LIST.AWE'), 'rb') as f:
        v1 = awe.read_awe(f.read())
    with open(os.path.join(folder, 'TIKYUU6_VOICE.JA_LIST.AWE'), 'rb') as f:
        ja = f.read()
    secs = {c.name: c.seconds for c in awe.read_awe(ja)}
    assert awe.write_awe2([awe.Cue(c.name, c.wave, secs[c.name]) for c in v1]) == ja


@test
def edf41_weapon_bank_real() -> None:
    """tools/edf41_sounds.py weapon bank from the real games: parses with pylib/acb.py, keeps 4.1's 1469 cue names, has
    EDF6's AcfMd5Hash, a Name and AcbGuid unlike EDF6's SE bank, and differs from 4.1's file in exactly those three places."""
    import acb
    import edf41_sounds as s
    games = _sound_games()
    if games is None:
        return
    r6, r41 = games
    out = s.weapon_bank(r6, r41)
    assert list(out) == [s.WEAPON_REL]
    new = out[s.WEAPON_REL]
    with open(s.sound_path(r41, s.SE_BANK), 'rb') as f:
        old = f.read()
    with open(s.sound_path(r6, s.SE_BANK), 'rb') as f:
        e6 = f.read()
    assert len(new) == len(old) and acb.cue_names(new) == acb.cue_names(old) and len(acb.cue_names(new)) == 1469
    _, new_data, row = s.utf_fields(new)
    _, e6_data, row6 = s.utf_fields(e6)

    def field(buf: bytes, base: int, r: dict, key: str) -> bytes:
        return buf[base + r[key][0]:base + r[key][0] + 16]

    assert field(new, new_data, row, 'AcfMd5Hash') == field(e6, e6_data, row6, 'AcfMd5Hash')
    assert row['Name'] == 'tikyuu41_W' and row['Name'] != row6['Name']
    assert field(new, new_data, row, 'AcbGuid') != field(e6, e6_data, row6, 'AcbGuid')
    pool = s.utf_fields(old)[0]
    name_at = pool + old[pool:].find(s.OLD_NAME)
    allowed = set(range(name_at, name_at + 10)) | {new_data + row[k][0] + j for k in ('AcfMd5Hash', 'AcbGuid') for j in range(16)}
    diff = [i for i in range(len(old)) if old[i] != new[i]]
    assert diff and set(diff) <= allowed, len(diff)


@test
def edf41_voice_real() -> None:
    """tools/edf41_sounds.py EN voice bank and the five tables from the real games: the AWB is 4.1's with only the version
    byte changed (2, as EDF6 writes it); the list is a version 2 AWE with 4.1's names and wave ids, sorted, and a length
    for each cue taken from its wave's HCA header; every table keeps all EDF6 rows and gains a row per 4.1 key."""
    import dsgo
    import awe
    import edf41_sounds as s
    import rootcpk
    games = _sound_games()
    if games is None:
        return
    r6, r41 = games
    awb_name, awe_name = s.VOICES['EN']
    bank = s.voice_bank(r41, 'EN')
    awb_rel, awe_rel = s.voice_rels('EN')
    awb = bank[awb_rel]
    with open(s.sound_path(r41, awb_name), 'rb') as f:
        head = f.read(1 << 20)
        f.seek(-(1 << 20), 2)
        tail = f.read()
    assert awb[4] == 2 and head[4] == 1 and bytes(awb[:4]) + bytes(awb[5:1 << 20]) == head[:4] + head[5:]
    assert bytes(awb[-(1 << 20):]) == tail
    with open(s.sound_path(r41, awe_name), 'rb') as f:
        v1 = awe.read_awe(f.read())
    v2 = awe.read_awe(bank[awe_rel])
    assert sorted((c.name, c.wave) for c in v1) == [(c.name, c.wave) for c in v2]
    assert awe.write_awe2(v2) == bank[awe_rel] and all(0.1 < c.seconds < 120 for c in v2)
    waves = awe.afs2_waves(awb)
    c = v2[len(v2) // 2]
    start = waves[c.wave][0]
    assert abs(c.seconds - awe.hca_seconds(bytes(awb[start:start + 0x100]))) < 1e-5   # stored as a float32
    del bank, awb
    tables = s.voice_tables(r6, r41)
    keys = s.cue_keys(r41)
    game = rootcpk.Game(r6)
    for lang in s.CAPTIONS:
        orig = dsgo.parse(game.read('MISSION', f'EDF6_VOICETABLE.{lang}.SGO')).root.items[0]
        got = dsgo.parse(tables[s.table_rel(lang)]).root.items[0]
        by = {got.names[i]: got.items[i] for i in got.names}
        added = set(keys) | (set(s.SILENT_KEYS) - set(orig.names.values()))
        assert len(got.items) == len(orig.items) + len(added) and set(by) == set(orig.names.values()) | added
        assert all(by[orig.names[i]].items == orig.items[i].items for i in orig.names), lang
        assert all(by[k].items == ['', s.NO_SUBTITLE] for k in s.SILENT_KEYS if k not in orig.names.values()), lang
        assert all(r.names == {0: 'name', 1: 'caption'} for r in got.items), lang   # the game reads a row by these names
    en = dsgo.parse(tables[s.table_rel('EN')]).root.items[0]
    row = next(en.items[i] for i in en.names if en.names[i] == 'A001AA')
    line = s.read_captions(s.plugin_path(r41, s.CAPTIONS['EN'])).get('A001AA')
    assert row.items == ['', line or 'A001AA'], row.items
@test
def edf41_rest_behaviour_real() -> None:
    """The shipped carriers, AI tank, Deiroi, AlienTrailer, Fortress robo and AI rangers read back with the keys
    the EDF6 classes need in the shape they read them (jobs/4bf89026/tmp/behav_rest/NOTES_*.md): the carrier's and the
    trailer's material animations as [blob, slot] pairs (a bare blob reached EDF+0x6dc050 as a null), Deiroi's
    shot / battery / moving keys (unchecked reads at EDF+0x319566, 0x31981c) and its beam foot under
    DeiroiFootParts_Battery, the trailer's se_list, the robo's booster keys, the rangers' talk setting, the tank's
    4.1 route radius and water line."""
    import sgo
    base = os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT')

    def load(name: str) -> dict:
        with open(os.path.join(base, name + '.SGO'), 'rb') as f:
            return sgo.load(data=f.read())

    def plain(v: object) -> object:   # a classic SGO's Float or a DSGO's double (V301 is a DSGO: Vehicle601_Tank's form)
        return ([plain(x) for x in v] if isinstance(v, list) else round(v.value, 6) if isinstance(v, sgo.Float) else
                round(v, 6) if isinstance(v, float) else v)

    def pairs(v: list) -> list[int]:
        assert all(isinstance(x, list) and len(x) == 2 and isinstance(x[1], int) for x in v), 'not [blob, slot] pairs'
        return [x[1] for x in v]

    assert pairs(load('UFOCARRIER301')['material_animation']) == [0, 2, 2]
    assert pairs(load('UFOCARRIER401')['material_animation']) == [0, 2, 2]
    assert load('UFOCARRIER301')['xgs_scene_object_class'] == 'UfoCarrier508'
    for name in ('DEIROI401', 'DEIROI401S', 'DEIROI401_2', 'DEIROI401L', 'DEIROI401_3'):
        d = load(name)
        assert d['xgs_scene_object_class'] == 'Deiroi'
        shot = plain(d['bullet_object'])
        assert shot[0] == 'PlasmaBullet01' and shot[1][1][:8] == [0, 0.5, 1, 1800, 8, 6.0, 2, 10.0], shot
        assert shot[1][1][9] == [0.4, 0.4, 3.0, 1.0] and shot[1][2] == [0, 60]
        assert plain(d['deiroi_moving_settings']) == [1.2, 0.533333, 0.25]
        row = plain(d['deiroi_battery_config'])[0]
        assert row[1][0] == 'bullet_object' and row[2] == 'charge_param' and row[3] == 180, row
        assert 'charge_param' in d and (('game_object_level_adjust' in d) == (name == 'DEIROI401S')), name
    beam = load('DEIROIFOOTPARTS_BEAM')
    assert beam['xgs_scene_object_class'] == 'DeiroiFootParts_Battery'
    bolt = plain(beam['bullet_object'])
    assert bolt[0] == 'LaserBullet02' and bolt[1][1][:8] == [0, 20.0, 1, 30, 8, 1.2, 10, 0.001], bolt
    assert plain(beam['deiroi_battery_config'])[1][1] == 600 and plain(beam['deiroi_battery_config'])[3] == 240
    assert plain(beam['deiroi_fire_config']) == [200.0, 1.0, 400.0, 0, [600.0, 0.02]]
    for name in ('ALIENTRAILER401', 'ALIENTRAILER401_L', 'ALIENTRAILER401_FIXED', 'ALIENTRAILER401_FIXEDXL',
                 'ALIENTRAILER401_XL'):
        d = load(name)
        assert len(d['se_list']) == 6 and pairs(d['material_animation']) == pairs(d['unit_material_animation']) == [0, 2, 2]
        assert d['game_object_kill_counter'] == 'edf41_AlienTrailerKillCount'
    for name in ('VEHICLE501_FORTRESSROBO', 'VEHICLE501_FORTRESSROBO_AI'):
        d = load(name)
        assert len(d['booster_setting']) == 4 and all(len(x) == 2 and isinstance(x[1], float) for x in d['booster_setting'])
        assert plain(d['booster_param_list']) == [[40.0, 40.0], [10.0, 10.0]] and len(d['vehicle_riding_position'][0]) == 7
        assert plain(d['mission_setup'])[2] == [] and len(d['vehicle_setup'][2]) == 2
    for name, talk in (('AIARMYSOLDIER_OMG', 0), ('AIARMYSOLDIER_OMG_LEADER', 1)):
        t = load(name)['game_object_talk_setting']
        assert t[1] == talk and t[2] > 1 and t[3] in (0, 1) and t[2] == {0: 6, 1: 4}[talk], t
    # Berserker's base class enables material slots 0..3 by number (EDF+0x6dc830 via 0x348d1d, 0x33ef81, 0x34b2de, 0x37acde):
    # every Hector variant carries the template's four (M113: 2 entries, a null player; M131: the memory past them), and
    # no more: a fifth, the 301 cannon's charge light, washed the whole screen out (2026-10-17 M113, X1)
    import dsgo
    for name in ('HECTOR301CC', 'HECTOR301GL', 'HECTOR401LS'):
        with open(os.path.join(base, name + '.SGO'), 'rb') as f:
            model = dsgo.parse(f.read()).root.get('animation_model')
        entries = model.get('material_animation').items
        assert [e.items[1] for e in entries] == [0.0, 0.0, 2.0, 2.0], name
        assert all(isinstance(e.items[0], dsgo.Blob) for e in entries)
    assert load('UFOCARRIER401')['game_sound'] == ['app:/sound/adx/tikyuu4_en_UFOcarrier401.acb',
                                                   'app:/sound/adx/tikyuu4_en_UFOcarrier301.acb']
    assert len(load('DEIROI401')['material_animation']) == 4 and len(load('UFO301')['material_animation']) == 4
    assert all(n.startswith('小型UFO507') for n in load('UFO401')['se_table'])
    tank = load('VEHICLE301_TANK_AI')
    assert plain(tank['game_object_ai_route_update_distance']) == 3.0 and plain(tank['car_base_submerge_height']) == 1.7
    assert tank['xgs_scene_object_class'] == 'Vehicle601_Tank' and plain(tank['game_object_durability']) == 700.0


def _mini_mab(flag: int) -> bytes:
    """A small MAB block in the layout of pylib/mab_legacy.py's docstring, with every part a block can have: two
    records in list 0 (a kind 0 and a kind 2), one in list 1, a track of two keys, vec4s, SGO blocks, strings. `flag`
    0x03: offsets from the block's start; 0x83: from the struct holding them."""
    import struct
    sgo_block = b'SGO\0' + struct.pack('<7I', 0x102, 0, 0x20, 0, 0x20, 0, 0x20)
    heads, nlists = 0x24, 4
    rec0 = heads + 8 * nlists                      # list 0: 2 records, list 1: 1 record
    tracks = rec0 + 3 * 0x20
    keys = tracks + 0x10
    keys_end = keys + 2 * 0x10
    vecs = (keys_end + 15) // 16 * 16
    vec = [(0.0, 7.0, 0.0, 1.0), (0.0, 1.0, 2.0, 1.0), (0.5, 0.5, 1.0, 1.0), (0.1, 0.2, 0.3, 1.0), (1.0, 2.0, 3.0, 1.0)]
    vec_end = vecs + 16 * len(vec)
    blob_at = [vec_end, vec_end + 0x20]            # one for the records, one for the keys
    strs = vec_end + 0x40
    texts = ['chest', 'bang', 'waist', 'react', 'head', 'aim', 'trk', 'k0', 'k1']
    text_at: dict[str, int] = {}
    pool = bytearray()
    for t in texts:
        text_at[t] = strs + len(pool)
        pool += t.encode('utf-16le') + b'\0\0'
    out = bytearray(strs + len(pool))
    struct.pack_into('<4sII4H4I', out, 0, b'MAB\0', 0x0F, flag, nlists, 1, len(vec), len(pool), heads, tracks, vecs, strs)
    rel = flag == 0x83

    def put(at: int, base: int, to: int) -> None:
        struct.pack_into('<i', out, at, to - base if rel else to)

    for i, (n, first) in enumerate([(2, rec0), (1, rec0 + 0x40), (0, tracks), (0, tracks)]):
        struct.pack_into('<HH', out, heads + 8 * i, i, n)
        put(heads + 8 * i + 4, heads + 8 * i, first)
    recs = [('bang', 'chest', 0, [0], None), ('react', 'waist', 2, [1, 2, 3], None), ('aim', 'head', 0, [4], None)]
    for k, (name, node, kind, vi, _x) in enumerate(recs):
        r = rec0 + 0x20 * k
        put(r, r, text_at[name])
        put(r + 4, r, text_at[node])
        struct.pack_into('<i', out, r + 8, kind)
        put(r + 0xC, r, vecs + 16 * vi[0])
        if kind == 2:
            put(r + 0x10, r, vecs + 16 * vi[1])
            put(r + 0x14, r, vecs + 16 * vi[2])
        else:
            struct.pack_into('<f', out, r + 0x10, 1.0)
            struct.pack_into('<i', out, r + 0x14, -1)
        put(r + 0x1C, r, blob_at[0])
    put(tracks, tracks, keys)
    put(tracks + 4, tracks, keys_end)
    put(tracks + 8, tracks, text_at['trk'])
    struct.pack_into('<i', out, tracks + 0xC, 2)
    for k in range(2):
        key = keys + 0x10 * k
        put(key, key, text_at[f'k{k}'])
        struct.pack_into('<f', out, key + 4, 0.5 * k)
        put(key + 0xC, key, blob_at[1])
    out[keys_end:vecs] = bytes([0xBA]) * (vecs - keys_end)
    for i, v in enumerate(vec):
        struct.pack_into('<4f', out, vecs + 16 * i, *v)
    for at in blob_at:
        out[at:at + 0x20] = sgo_block
    for t, at in text_at.items():
        out[at:at + 2 * len(t) + 2] = t.encode('utf-16le') + b'\0\0'
    return bytes(out)


def _f32_plain(x: float) -> float:
    """`x` rounded to float32, as a plain float (_f32 above gives an sgo value)."""
    import struct
    return struct.unpack('<f', struct.pack('<f', x))[0]


@test
def mab_add_locators_synthetic() -> None:
    """pylib/mab_legacy.mab_add_locators on a small block of either flag (0x03: absolute offsets, 0x83: relative to
    the struct): the added records come last in list 0 with the vec4s they were given, a name or node the block
    holds is shared and a new one appended; every record, track and key that was there reads back the same (they only
    moved), the other lists keep their records, the block passes mab_layout, and adding nothing is the block itself.
    The next added block is built on the result, so the header's counts and offsets are right for a second run."""
    import dataclasses
    import mab_legacy as ml
    for flag in (0x03, 0x83):
        raw = _mini_mab(flag)
        before = ml.mab_locators(raw)
        assert [(i, l.name, l.node) for i, l in before] == [(0, 'bang', 'chest'), (0, 'react', 'waist'), (1, 'aim', 'head')]
        assert ml.mab_add_locators(raw, []) == raw
        eye = ml.Locator('eye', 'chest', 0, (0.0, 29.0, 0.0, 1.0), 1.0)
        muzzle = ml.Locator('muzzle', 'arm', 2, (0.0, 0.0, 26.9, 1.0), (0.1, 0.1, 5.0, 1.0), (0.0, 0.0, 0.0, 1.0))
        out = ml.mab_add_locators(raw, [eye, muzzle])
        ml.mab_layout(out)
        got = ml.mab_locators(out)
        assert [(i, l.name) for i, l in got] == [(0, 'bang'), (0, 'react'), (0, 'eye'), (0, 'muzzle'), (1, 'aim')], flag
        assert got[0] == before[0] and got[1] == before[1] and got[4] == before[2]
        assert got[2][1].a == eye.a and got[2][1].b == 1.0 and got[2][1].c is None
        assert got[3][1].node == 'arm' and got[3][1].a == tuple(_f32_plain(x) for x in muzzle.a) and got[3][1].c == muzzle.c
        # 'chest' is a string the block has: the node shares it; 'arm' is new and is the last string
        assert out.count('chest'.encode('utf-16le') + b'\0\0') == 1 and out.endswith('arm'.encode('utf-16le') + b'\0\0')
        # the track and its keys are in the layout the reader checks (mab_layout above), their strings unchanged
        assert all(s.encode('utf-16le') + b'\0\0' in out for s in ('k0', 'k1', 'trk'))
        again = ml.mab_add_locators(out, [dataclasses.replace(eye, name='eye2')])
        assert [l.name for _i, l in ml.mab_locators(again)][-2:] == ['eye2', 'aim'], 'a second run'
        after_again = ml.mab_locators(again)
        assert after_again[:4] == got[:4] and after_again[5] == got[4]


@test
def mab_add_locators_real() -> None:
    """mab_add_locators on every MAB block of EDF6's OBJECT SGOs (DSGO and SGO, 0x83) and of EDF4.1's (0x03 once
    converted is 0x83; 0x03 as it stands): the result passes mab_layout, the old records are the first of their lists
    in order, and a list-0 locator added reads back. These blocks have tracks, keys and every record kind."""
    import dsgo
    import legacy_sgo
    import mab_legacy as ml
    import make_edf41_objects as m
    import rootcpk
    import sgo
    if not _e5c_have_game():
        return
    g6 = m.archives(rootcpk.DEFAULT_GAME)
    blocks: list[bytes] = []
    for rel, game in sorted(g6.items()):
        if not (rel.startswith('OBJECT/') and rel.endswith('.SGO')) or len(blocks) >= 60:
            continue
        data = game.read(*rel.rsplit('/', 1))
        try:
            if data[:4] == b'DSGO':
                block = dsgo.parse(data).root.get('animation_model').items[2].data
            else:
                block = sgo.read(data)[1]['animation_model'][2]
        except (KeyError, IndexError, AttributeError, ValueError):
            continue
        if isinstance(block, bytes) and block[:4] == b'MAB\0':
            blocks.append(block)
    assert len(blocks) >= 40, len(blocks)
    new = [ml.Locator('テスト', 'zz_node', 2, (1.0, 2.0, 3.0, 1.0), (0.1, 0.1, 5.0, 1.0), (0.0, 0.0, 0.0, 1.0))]
    for block in blocks:
        before = ml.mab_locators(block)
        if not before:
            continue
        out = ml.mab_add_locators(block, new)
        after = ml.mab_locators(out)
        n0 = sum(1 for i, _l in before if i == 0)
        assert after[:n0] == before[:n0] and after[n0][1].name == 'テスト' and after[n0 + 1:] == before[n0:], 'records'
    edf41 = _edf41_root()
    if edf41:
        g4 = m.archives(edf41)
        seen = 0
        for rel, game in sorted(g4.items()):
            if rel.startswith('OBJECT/') and rel.endswith('.SGO') and seen < 40:
                data = game.read(*rel.rsplit('/', 1))
                for raw in (data, legacy_sgo.convert(data)):    # 4.1's own block (0x03), and EDF6's form of it
                    block = sgo.read(raw)[1].get('animation_model')
                    block = block[2] if isinstance(block, list) and len(block) > 2 else None
                    if isinstance(block, bytes) and block[:4] == b'MAB\0' and ml.mab_locators(block):
                        out = ml.mab_add_locators(block, new)
                        ml.mab_layout(out)
                        assert 'テスト' in [l.name for _i, l in ml.mab_locators(out)]
                        seen += 1
        assert seen >= 20, seen


def _edf41_root() -> str | None:
    import gamedir
    import rootcpk
    return gamedir.find_other(gamedir.EDF41, near=rootcpk.DEFAULT_GAME) or None


@test
def cas_graft_variables_and_states_real() -> None:
    """pylib/cas_graft: variables and state aliases. EDF4.1 Hector's CAS (converted) gets the Berserker class's
    players damage and random from the Large CAS and additive_action from the TypeA CAS, the variables its constructor
    sets (appended after Hector's own, so no index of its own moves; the initial values are the donor's), and
    walk_battle_* as copies of the walks. Grafting nothing is the CAS byte for byte; the result is again a CAS the
    layout reader and the pose reader take, with the clips of its own as they were and a trackless clip per grafted
    tcontrol; a variable that follows a tcontrol (vt_*) is refused, and so is a group holding a data command whose
    arguments are not known to be a tcontrol index."""
    import struct
    import cas_graft
    import cas_legacy as cl
    import cas_pose
    import make_edf41_objects as m
    import rootcpk
    edf41 = _edf41_root()
    if not _e5c_have_game() or not edf41:
        print('skip  cas_graft_variables_and_states_real: needs EDF6 and EDF4.1 installed')
        return
    g6, g4 = m.archives(rootcpk.DEFAULT_GAME), m.archives(edf41)
    rd = lambda rel: g6[rel].read(*rel.rsplit('/', 1))   # noqa: E731
    hector = cl.cas_from_legacy(g4['OBJECT/HECTOR.CAS'].read('OBJECT', 'HECTOR.CAS'))
    large, type_a = rd('OBJECT/E602_BERSERKER_LARGE.CAS'), rd('OBJECT/E602_BERSERKER.CAS')
    assert cas_graft.graft(hector, large, [], ()) == hector
    own_vars = cas_graft.variable_names(hector)
    names = ('v_move_angle', 'v_tilt_dir', 'v_tilt_lerp', 'v_damage_reaction_ratio', 'v_tilt_factor', 'v_tilt_lerp_factor')
    out = cas_graft.graft(hector, large, ['damage', 'random'], names)
    out = cas_graft.graft(out, type_a, ['additive_action'], ('v_move_velocity',))
    out = cas_graft.alias_states(out, {'main': [('walk_battle_f', 'walk_f'), ('walk_battle_b', 'walk_b')]})
    lay = cl.cas_layout(out)
    groups = {cas_graft._name(out, at): ms for at, ms in lay.tree.anmgroups}
    assert {'main', 'damage', 'random', 'additive_action'} <= set(groups), sorted(groups)
    state = lambda g: [cas_graft._name(out, mc.at) for mc in groups[g]]   # noqa: E731
    assert state('damage') == ['damageA', 'damageB', 'damageC']
    own_main = [cas_graft._name(hector, mc.at) for at, ms in cl.cas_layout(hector).tree.anmgroups
                if cas_graft._name(hector, at) == 'main' for mc in ms]
    assert state('main') == own_main + ['walk_battle_f', 'walk_battle_b']
    new_vars = cas_graft.variable_names(out)
    assert new_vars[:len(own_vars)] == own_vars and new_vars[len(own_vars):] == list(names) + ['v_move_velocity']
    # the donor's initial values, and the +4 word 0 (a plain variable)
    donor_init = {cas_graft._name(large, at): struct.unpack_from('<I', large, at + 0xC)[0] for at in cl.cas_layout(large).tree.vcontrols}
    for at in lay.tree.vcontrols[len(own_vars):]:
        n = cas_graft._name(out, at)
        assert struct.unpack_from('<I', out, at + 4)[0] == 0
        if n in donor_init:
            assert struct.unpack_from('<I', out, at + 0xC)[0] == donor_init[n], n
    # a walk alias is the walk: the same tcontrol word, the same first-anmdata bytes, a name of its own
    by = {cas_graft._name(out, mc.at): mc for mc in groups['main']}
    for a_, b_ in (('walk_f', 'walk_battle_f'), ('walk_b', 'walk_battle_b')):
        x, y = by[a_], by[b_]
        assert out[x.at + 0x18:x.at + 0x24] == out[y.at + 0x18:y.at + 0x24] and out[x.first.at + 4:x.first.at + 0x20] == out[y.first.at + 4:y.first.at + 0x20]
    p0, p1 = cas_pose.CasPose(hector), cas_pose.CasPose(out)
    assert [(c.name, [t.name for t in c.tracks]) for c in p1.clips[:len(p0.clips)]] == [(c.name, [t.name for t in c.tracks]) for c in p0.clips]
    assert all(not c.tracks for c in p1.clips[len(p0.clips):]) and len(p1.clips) > len(p0.clips)
    assert cas_graft.graft(out, out, [], ()) == out, 'the result is a stable CAS'
    # refusals
    for v in ('vt_walk_battle_f', 'v_no_such'):
        try:
            cas_graft.graft(hector, large, [], (v,))
        except ValueError:
            pass
        else:
            raise AssertionError(f'{v} was grafted')
    # a group of the donor with a data command other than 0 / 4 is refused (its arguments are not known)
    refused = 0
    has = {cas_graft._name(hector, at) for at, _ms in cl.cas_layout(hector).tree.anmgroups}
    for rel in ('OBJECT/DIRTY_MAN01.CAS', 'OBJECT/E503_FROG.CAS', 'OBJECT/EDF6ARMYSOLDIER.CAS'):
        donor = rd(rel)
        for at, ms in cl.cas_layout(donor).tree.anmgroups:
            ops = {struct.unpack_from('<I', donor, d)[0] for mc in ms for d in cas_graft._data_of(mc)}
            if ops - set(cas_graft.TCONTROL_OPS) and cas_graft._name(donor, at) not in has:
                try:
                    cas_graft.graft(hector, donor, [cas_graft._name(donor, at)], ())
                except ValueError:
                    refused += 1
                else:
                    raise AssertionError(f'{rel}: a group with data commands {sorted(ops)} was grafted')
    assert refused >= 1, refused


def _mini_mdb(rigid: bool) -> object:
    """A tiny EDF6 model: root mdl, a skinned root 'body' bone and an arm chain (rigid=False), or mdl and one rigid
    bone 'part' holding one unskinned triangle (rigid=True)."""
    import struct
    import mdb
    ident = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]

    def bone(i: int, parent: int, sib: int, child: int, name: int, kids: int, kind: int, dd: int, y: float) -> object:
        local = list(ident)
        local[13] = y
        return mdb.Bone(i, parent, sib, child, name, kids, kind, dd, 1 if kind else 0, 0, 0, local, list(ident),
                        [1.0, 1.0, 1.0, 1.0], [0.0, 0.0, 0.0, 1.0])

    def half4(x: float, y: float, z: float) -> bytes:
        return b''.join(struct.pack('<e', v) for v in (x, y, z, 1.0))

    tri = half4(0, 0, 0) + half4(1, 0, 0) + half4(0, 0, 4)
    mesh = mdb.Mesh(bytes(4), 0, 0, 8, [mdb.VElem(7, 0, 0, 'position')], 0, tri, struct.pack('<3H', 0, 1, 2))
    mat = mdb.Material(0, 0, 0, 1, 'snd_Test', [mdb.MatParam([1.0, 1.0, 1.0, 0.0], (0, 0), 'diffuse', 0x0201)],
                       [mdb.MatTex(0, 'albedo', (0, 0, 0, 0, 0))], 3)
    tex = [mdb.Texture(0, 'part_df_dds', 'part_df.dds', 0)]
    if rigid:
        bones = [bone(0, -1, -1, 1, 0, 1, 0, 1, 0.0), bone(1, 0, -1, -1, 2, 0, 1, 0, 0.0)]
        return mdb.Mdb(0x20, ['mdl', 'ufosurface', 'part'], bones, [mdb.Object(2, 1, [mesh])], [mat], tex)
    # mdl(0) -> body(1) -> arm(2); mdl -> other(3)
    bones = [bone(0, -1, -1, 1, 0, 2, 0, 1, 0.0), bone(1, 0, 3, 2, 1, 1, 2, 1, 2.0), bone(2, 1, -1, -1, 2, 0, 0, -1, 5.0),
             bone(3, 0, -1, -1, 3, 0, 3, 0, 0.0)]
    names = ['mdl', 'body', 'arm', 'other']
    own = mdb.Mesh(bytes(4), 0, 0, 8, [mdb.VElem(7, 0, 0, 'position')], 0, tri, struct.pack('<3H', 0, 1, 2))
    return mdb.Mdb(0x20, names, bones, [mdb.Object(1, 3, [own])], [mdb.Material(0, 0, 0, 1, 'snd_Test', [], [], 3)], [])


@test
def mdb_graft_rigid() -> None:
    """pylib/mdb.graft_rigid on a tiny model: the donor's rigid bone becomes a rigid-mesh bone (kind 1, the donor's
    bounds, identity inverse bind as every stock rigid bone has) under the named parent, last child, in depth-first
    order; its texture, material, object and mesh are copied with indices remapped (a texture the target already has
    by filename is shared; a second graft of the same weapon shares the first one's material); the target's own
    object and bones keep their meaning; the file writes and reads back; a skinned or multi-bone donor is refused.
    rigid_extent measures the donor's barrel."""
    import mdb
    body, donor = _mini_mdb(False), _mini_mdb(True)
    assert mdb.rigid_extent(donor, 2) == (0.0, 4.0)
    a = mdb.graft_rigid(body, donor, 'arm', 'gun_arm')
    b = mdb.graft_rigid(body, donor, 'body', 'gun_body')
    assert body.name_of(body.bones[a].name) == 'gun_arm' and body.bones[a].kind == 1 and body.bones[a].parent == 2
    assert body.bones[a].inv_bind == mdb.ident() and body.bones[a].bounded == 1 and body.bones[a].half == [1.0, 1.0, 1.0, 1.0]
    assert body.bones[2].child == a and body.bones[2].child_count == 1
    assert [body.name_of(x.name) for x in body.bones] == ['mdl', 'body', 'arm', 'gun_arm', 'gun_body', 'other']
    assert body.objects[0].bone == 5, 'the skinned object follows its bone to its new index'
    assert [(o.bone, o.meshes[0].material) for o in body.objects[1:]] == [(a, 1), (b, 1)]
    assert len(body.materials) == 2 and len(body.textures) == 1 and body.materials[1].textures[0].texture == 0
    depth = mdb.depth_of(body)
    assert depth == [0, 1, 2, 3, 2, 1] and [x.depth_delta for x in body.bones] == [1, 1, 1, -1, -1, 1]
    out = mdb.mdb_write(body)
    back = mdb.mdb_read(out)
    assert mdb.mdb_write(back) == out and len(back.objects) == 3
    skinned = _mini_mdb(True)
    skinned.objects[0].meshes[0].flags = bytes([0, 1, 1, 0])
    try:
        mdb.graft_rigid(_mini_mdb(False), skinned, 'arm', 'x')
    except ValueError:
        pass
    else:
        raise AssertionError('a skinned donor was grafted')
    try:
        mdb.graft_rigid(_mini_mdb(False), _mini_mdb(False), 'arm', 'x')
    except ValueError:
        pass
    else:
        raise AssertionError('a donor of several bones was grafted')


@test
def edf41_alias_dsgo_helpers() -> None:
    """tools/make_edf41_objects: the DSGO path's small parts. dsgo_value (numbers become doubles, lists and dicts
    Nodes, raw blocks Blobs, a MAB still in 4.1's form converted), node_path / set_path (names and list indices, a name
    not there yet is added), remove_key (the other members keep their names), prune (only the files the root names,
    through the SGOs it names, stay) and Gun / Alias defaults (the old aliases are untouched)."""
    import dsgo
    import make_edf41_objects as m
    import mab_legacy as ml
    import sgo
    v = m.dsgo_value({'a': [1, sgo.Float(b'\0\0\x80\x3f'), 'x', b'abc'], 'b': 2.5})
    assert isinstance(v, dsgo.Node) and v.names == {0: 'a', 1: 'b'}
    assert v.get('a').items[:3] == [1.0, 1.0, 'x'] and v.get('a').items[3] == dsgo.Blob(b'abc', 2)
    raw03 = _mini_mab(0x03)
    assert m.dsgo_value(raw03).data == ml.mab_from_edf5(raw03) and m.dsgo_value(_mini_mab(0x83)).data == _mini_mab(0x83)
    m.set_path(v, 'a/0', 5.0)
    m.set_path(v, 'c', dsgo.Node([1.0]))
    assert m.node_path(v, 'a/0') == 5.0 and m.node_path(v, 'c/0') == 1.0
    m.remove_key(v, 'a')
    assert v.names == {0: 'b', 1: 'c'} and v.get('b') == 2.5 and v.get('c').items == [1.0]
    m.remove_key(v, 'nothing')
    files = {'OBJECT/X.SGO': sgo.write_depth_first(0x102, {'xgs_scene_object_class': 'C', 'm': ['app:/object/y.cas', 'app:/Object/Z.SGO'],
                                                           'r': 'app:/object/gone.mrab'}),
             'OBJECT/Z.SGO': sgo.write_depth_first(0x102, {'t': 'app:/object/ragdoll.shkt'}),
             'OBJECT/Y.CAS': b'cas', 'OBJECT/RAGDOLL.SHKT': b's', 'OBJECT/UNUSED.SGO': sgo.write_depth_first(0x102, {'t': 'app:/object/never.cas'}),
             'OBJECT/NEVER.CAS': b'n', 'OBJECT/GONE.MRAB': b'g'}
    del files['OBJECT/GONE.MRAB']
    m.prune(files, 'OBJECT/X.SGO')
    assert set(files) == {'OBJECT/X.SGO', 'OBJECT/Z.SGO', 'OBJECT/Y.CAS', 'OBJECT/RAGDOLL.SHKT'}, sorted(files)
    old = [a for k, a in m.ALIASES.items() if k.startswith(('UFO', 'UFOCARRIER'))]
    assert old and all(not a.guns and not a.drop and not a.variables and not a.cas_donors and not a.prune for a in old)


@test
def edf41_shipped_textures_load_before_models() -> None:
    """Every model in every EDF4.1 archive the tools ship has each texture it names before it in the archive (the
    engine binds a texture only if it loaded before the model: mdb.insert_member). The Hectors' once had their guns'
    and shield's textures after the body that carries those meshes: black guns, no shield (docs/feedback-2026-10-17-
    edf41-map-hector.md H2 / H5)."""
    import edf41_objects as eo
    import make_edf41_objects as m
    import mdb
    checked = 0
    for rel in sorted(eo.registry()['files']):
        if not rel.upper().endswith('RAB'):
            continue
        data = _edf41_object(rel)
        if data is None:   # put together from the player's EDF6 (the King), not here
            continue
        for f in mdb.rab_read(data).files:
            if f.name.lower().endswith('.mdb'):
                assert not m.missing_textures(data, f.name), (rel, f.name, m.missing_textures(data, f.name))
                checked += 1
    assert checked >= 21, checked


@test
def edf41_hector_alias() -> None:
    """HECTOR301CC under EDF6_Berserker_Large, built from the games (tools/make_edf41_objects.py build, only that
    root): not skipped; and every name the class looks up that is unchecked in EDF.dll (jobs/4bf89026/tmp/hector41
    NOTES.md) is satisfied: the CAS players main / damage / random / additive_action, the variables set every frame and
    the getters' tunings, the states the class asks for in a fight, the damage reaction's states, the locators
    (eye, one muzzle per gun_control entry) on bones the model has, the bone and ragdoll-body names the SGO gives,
    the damage map's debris models and every texture the model binds. The weak point is gone and the damage reaction
    is the capped one (one entry, min 0.05); nothing knocks it down and it does not climb (both move the object and
    play states Hector has none of); it walks and turns at 4.1's rates."""
    import struct
    import cas_graft
    import cas_legacy as cl
    import dsgo
    import mab_legacy as ml
    import make_edf41_objects as m
    import mdb
    import rootcpk
    import sgo
    edf41 = _edf41_root()
    if not _e5c_have_game() or not edf41:
        print('skip  edf41_hector_alias: needs EDF6 and EDF4.1 installed')
        return
    data, files = m.build(edf41, rootcpk.DEFAULT_GAME, ('HECTOR301CC.SGO',))
    assert not data['skipped'] and [o['object'] for o in data['objects']] == ['OBJECT/HECTOR301CC.SGO'], data['skipped']
    assert set(files) == {'OBJECT/HECTOR301CC.SGO', 'OBJECT/HECTOR.CAS', m.HECTOR_MODEL,
                          'OBJECT/RAGDOLL_HECTOR301_HAS_HKA.SHKT'}, sorted(files)
    root = dsgo.parse(files['OBJECT/HECTOR301CC.SGO']).root
    top = sgo.load(data=files['OBJECT/HECTOR301CC.SGO'])
    assert top['xgs_scene_object_class'] == 'EDF6_Berserker_Large'
    # weak_point present with every member the class's setup dereferences (0x34C580: a missing one crashed M153 on
    # spawn, 2026-10-13), and neutral: no hit is 0.2x or 1.5x
    wp = top['weak_point']
    assert {'damage_esb', 'body', 'noweak_damage_multiply', 'weak_damage_multiply'} <= set(wp), wp
    assert wp['noweak_damage_multiply'] == 1.0 and wp['weak_damage_multiply'] == 1.0, wp
    # the state played on first sight is one of Hector's (an encount_appeal_* of the template's faulted M153)
    assert top['BerserkerSettings']['EncountAnimations'] == ['wakeup'], top['BerserkerSettings']
    m.check_states(root, files['OBJECT/HECTOR.CAS'], 'HECTOR301CC')
    try:
        m.check_states(root, None, 'no CAS')
    except ValueError:
        pass
    else:
        raise AssertionError('check_states passed a state against no CAS')
    # the CAS
    cas = files['OBJECT/HECTOR.CAS']
    lay = cl.cas_layout(cas)
    players = {cas_graft._name(cas, at): [cas_graft._name(cas, mc.at) for mc in ms] for at, ms in lay.tree.anmgroups}
    assert {'main', 'damage', 'random', 'additive_action'} <= set(players)
    variables = set(cas_graft.variable_names(cas))
    assert {'v_angle', 'v_turn_speed', 'v_move_angle', 'v_move_velocity', 'v_tilt_dir', 'v_tilt_lerp', 'v_damage_reaction_ratio',
            'v_tilt_factor', 'v_tilt_lerp_factor', 'v_walk_animation_speed', 'v_turn_animation_speed'} <= variables
    assert {'stand', 'turn_l', 'turn_r', 'walk_f', 'walk_b', 'walk_l', 'walk_r', 'walk_battle_f', 'walk_battle_b',
            'walk_battle_l', 'walk_battle_r'} <= set(players['main'])
    reaction = top['BasicAnimationCharacterSettings']['DamageReaction']
    assert len(reaction) == 1 and reaction[0]['user_param'][0] == 'damage'
    assert set(reaction[0]['user_param'][1]) <= set(players['damage'])
    adjust = top['BerserkerSettings']['DamageReactionAdjust']
    assert adjust['min'] == 0.05 and adjust['scale'] == 10.0
    # nothing knocks it down: the class's get-up puts the object at its ragdoll's waist and plays wakeup_f / _b, which
    # Hector has none of (the 2026-10-17 report: it vanished off the map)
    assert top['BerserkerSettings']['ExplosionDownBorder'] >= 1e30 and not {'wakeup_f', 'wakeup_b'} & set(players['main'])
    # no climb: the template's climb moves the object onto the obstacle and plays climb_over_*, which Hector has none of
    assert 'ClimbOver' not in top['WalkingEnemyBase'], top['WalkingEnemyBase']
    climbing = dsgo.parse(files['OBJECT/HECTOR301CC.SGO']).root
    m.set_path(climbing, 'WalkingEnemyBase/ClimbOver', m.dsgo_value({'animations': [[20.0, 'climb_over_small']]}))
    try:
        m.check_states(climbing, cas, 'HECTOR301CC with the template climb')
    except ValueError:
        pass
    else:
        raise AssertionError('check_states passed a climb state Hector has not')
    # 4.1's walk (6 units/s, eased 10 %) and turn (0.03 rad/frame at most, the yaw eased 10 %): EDF6's turn is
    # TurnSpeed[0] x clamp(angle, +-0.1) a frame, so 0.3; the speed itself comes from src/hector_ai.h Steer
    assert top['WalkingEnemyBase']['MoveSpeed'] == [6.0, 0.1], top['WalkingEnemyBase']['MoveSpeed']
    assert top['WalkingEnemyBase']['TurnSpeed'] == [0.3, 0.1], top['WalkingEnemyBase']['TurnSpeed']
    assert top['WalkingEnemyBase']['RandomAnimationTrack'] == [], 'its variables are not in the CAS'
    # the model, the MAB, the ragdoll
    # one archive for every variant (a player's report, 2026-10-18: 21 archives each with the same body and textures):
    # the textures once, an armed body per variant under its root's name, the SGO naming its own
    rab = files[m.HECTOR_MODEL]
    archive = mdb.rab_read(rab)
    member = {f.name.lower(): f for f in archive.files}
    am = root.get('animation_model')
    assert am.items[0].items[0].lower() == 'app:/object/hector_variants.mrab' and am.items[1] == 'app:/object/Hector.cas'
    assert am.items[0].items[1] == 'HECTOR301CC.mdb', am.items[0].items[1]
    model = mdb.mdb_read(member[am.items[0].items[1].lower()].data)
    bodies = sorted(f.name for f in archive.files if f.name.lower().endswith('.mdb') and 'fragment' not in f.name.lower())
    assert bodies == sorted(m.shared_member(r) for r in m.HECTOR_VARIANTS), bodies
    names = [f.name.lower() for f in archive.files]
    assert len(names) == len(set(names)) and mdb.folder_order_ok(archive)
    bones = {model.name_of(b.name) for b in model.bones}
    assert {'waist', 'arms_l', 'arms_r', 'Hector301', 'gun_arms_l', 'gun_arms_r'} <= bones
    for body in bodies:
        assert not m.missing_textures(rab, body), (body, m.missing_textures(rab, body))
    for frag in am.items[3].items[3].items[6].items:
        assert frag.lower() in member, frag
        assert not m.missing_textures(rab, frag)
    # the class enables slots 0..3 by number (EDF+0x6dc830): as many entries as the template's four, not one per material
    # (two crashed the real game, M113 / M131); the model's materials are the two the glow blob names
    # and no fifth: 301 cannon's charge light (light0_color up to 10 / 10 / 100) turned M113 white (2026-10-17 real run)
    assert len(am.items[4].items) == 4 and len(model.materials) == 2 and 'edf41_hector_light' not in top
    # each material its own name (EDF6 binds an MTAB entry to the first of a name: the guns' glow was never played),
    # every slot animates the body's and the guns', slot 3 (played once at death) is 4.1's 180-frame death block
    mats = [model.name_of(x.name) for x in model.materials]
    assert mats == ['ufosurface', 'ufosurface.hector301cannon'], mats
    for i, slot in enumerate(am.items[4].items[:4]):
        blob = slot.items[0].data
        assert [n for n, _c, _t in m.mtab_entries(blob)] == mats, (i, m.mtab_entries(blob))
        assert struct.unpack_from('<if', blob, 0) == ((0, 180.0) if i == m.BERSERKER_DEATH_SLOT else (1, 480.0)), i
    # 4.1's time to the first shot: the 301 cannon charges 180 frames (EDF41.exe 0x225A7A, 3 s)
    assert [c['gun_battery']['charge']['time'] for c in top['gun_control']] == [180.0, 180.0], top['gun_control']
    located = ml.mab_locators(am.items[2].data)
    names = {l.name: l for _i, l in located}
    assert {'目の基準位置', '砲台１', '砲台２'} <= set(names) and all(l.node in bones for _i, l in located)
    assert [c['gun_battery']['muzzle']['shape'] for c in top['gun_control']] == ['砲台１', '砲台２']
    assert names['砲台１'].node == 'arms_l' and names['砲台２'].node == 'arms_r' and names['砲台１'].a[2] > 20.0
    wk = top['WalkingEnemyBase']['InterpolateNode']
    assert wk in bones and top['BerserkerSettings']['WakeupNode'][0] in bones
    shkt = files['OBJECT/RAGDOLL_HECTOR301_HAS_HKA.SHKT']
    for body in top['BerserkerSettings']['DownRagdollBodies'] + top['BerserkerSettings']['NoSeparationBodies']:
        assert body.encode() in shkt, body
    # the file is a DSGO the writer reproduces
    assert dsgo.write(dsgo.parse(files['OBJECT/HECTOR301CC.SGO'])) == files['OBJECT/HECTOR301CC.SGO']
    assert struct.unpack_from('<I', cas, 4)[0] == 0x204
    # the head player the plugin opens and closes (src/hector_ai.h StepHead), and no weapon animation on 301's guns
    assert {'open', 'close'} <= cas_graft.states(cas)['head'], cas_graft.states(cas)
    assert top['edf41_hector_anim'] == [[0.0, [], [], [], []]] * 2, top['edf41_hector_anim']


@test
def edf41_hector_gait() -> None:
    """A 4.1 Hector's legs play 4.1's animation (docs/feedback-2026-10-18-edf41-hector-walk.md; src/hector_ai.h PickGait /
    GaitRate, src/edf41ai.cpp GaitStateHook / GaitRateHook): 4.1's rule (EDF41.exe 0x2220E0 / 0x2224E0) picks stand,
    turn_l / turn_r or walk_f / b / l / r from the yaw step, the eased velocity in the body's frame and the walk command,
    every frame; EDF6's WalkingEnemyBase never plays a turn while a Berserker's walking state stands it still (the stand
    state turns only past the clamp those states give FaceTarget). The plugin's slot table follows hector::Gait and names
    the slots the class's constructor binds; the call sites it redirects are the stand / walk states' own state and rate
    calls. With the game: EDF.dll binds those names at those slots and calls those functions at those sites. With both
    games: the CAS's v_walk_animation_speed / v_turn_animation_speed are 4.1's 10 / 112 (the rates multiply them)."""
    code = src('src/edf41ai.cpp')
    head = src('src/hector_ai.h')
    order = [w.strip() for w in re.search(r'enum class Gait \{([^}]*)\}', head).group(1).split(',')]
    assert order == ['Stand', 'WalkF', 'WalkB', 'WalkL', 'WalkR', 'TurnL', 'TurnR'], order
    table = [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', re.search(r'kGaitStateAt\[\]=\{([^}]*)\}', code).group(1))]
    name = {'Stand': 'stand', 'WalkF': 'walk_f', 'WalkB': 'walk_b', 'WalkL': 'walk_l', 'WalkR': 'walk_r', 'TurnL': 'turn_l',
            'TurnR': 'turn_r'}
    binds = {'stand': 0x1000, 'turn_l': 0x1004, 'turn_r': 0x1008, 'walk_f': 0x101C, 'walk_b': 0x1020, 'walk_l': 0x1024,
             'walk_r': 0x1028}
    assert len(table) == len(order) and all(binds[name[g]] == at for g, at in zip(order, table)), table
    sites = [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', re.search(r'kGaitStateSites\[\]=\{([^}]*)\}', code).group(1))]
    rates = [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', re.search(r'kGaitRateSites\[\]=\{([^}]*)\}', code).group(1))]
    assert sites == [0x540697, 0x5406C1, 0x5406DF, 0x540821] and rates == [0x5406F5, 0x54083E], (sites, rates)
    install = code.split('bool InstallEdf41Ai() noexcept {', 1)[1]
    assert 'gaits==4 && rates==2' in install and '&GaitStateHook' in install and '&GaitRateHook' in install
    # only a Hector's (the table's entries are Hectors): any other walking enemy keeps the class's choice
    for hook in ('bool __fastcall GaitStateHook(', 'void __fastcall GaitRateHook('):
        body = code.split(hook, 1)[1].split('\n}\n', 1)[0]
        assert 'if(Find(self) && ' in body, hook
    # the walk itself (4.1 0x2234B6..0x2238BF): with a target, at any distance, the body faces the target and walks along
    # its own facing only; every walking state's MoveTo and FaceTarget is taken over (the follow state's slot beside
    # the target made it slide sideways, 2026-10-18 M113)
    moves = [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', re.search(r'kMoveToSites\[\]=\{([^}]*)\}', code).group(1))]
    faces = [int(x, 16) for x in re.findall(r'0x[0-9A-F]+', re.search(r'kFaceSites\[\]=\{([^}]*)\}', code).group(1))]
    assert moves == [0x34D8D8, 0x34D955, 0x34DA65] and faces == [0x34D8FD, 0x34D96A, 0x34DA7A], (moves, faces)
    assert 'walks==3 && faces==3' in install
    steer = head.split('inline Walk Steer(', 1)[1].split('\n}\n', 1)[0]
    assert steer.index('w.takeover=true;') < steer.index('if(dist>=c.closeIn)return w;'), 'far away too: 4.1 has no route'
    face = code.split('void __fastcall FaceHook(', 1)[1].split('\n}\n', 1)[0]
    assert 'e->haveTarget' in face and 'aim[0]=e->target[0]' in face
    move = code.split('bool __fastcall MoveToHook(', 1)[1].split('\n}\n', 1)[0]
    assert 'kAxisZAt' in move and 'e->target' not in move, 'the walk is along the facing, never at a point'

    import rootcpk
    dll = os.path.join(rootcpk.DEFAULT_GAME, 'EDF.dll')
    if not os.path.exists(dll):
        print('skip  edf41_hector_gait (EDF.dll part): needs EDF6 installed')
        return
    import struct
    import edfre
    play = int(re.search(r'kCasPlayState=(0x[0-9A-F]+)', code).group(1), 16)
    rate = int(re.search(r'kCasSetRate=(0x[0-9A-F]+)', code).group(1), 16)
    move_fn = int(re.search(r'kMoveTo=(0x[0-9A-F]+)', code).group(1), 16)
    face_fn = int(re.search(r'kFaceTarget=(0x[0-9A-F]+)', code).group(1), 16)
    for at, want in ([(a, play) for a in sites] + [(a, rate) for a in rates] + [(a, move_fn) for a in moves] +
                     [(a, face_fn) for a in faces]):
        assert edfre.img[at] == 0xE8 and at + 5 + struct.unpack_from('<i', edfre.img, at + 1)[0] == want, hex(at)
    # the constructor (0x53CD15..0x53CF05): each state looked up by name (lea r8, name) lands in its slot (mov [r14+slot], eax)
    found: dict[str, int] = {}
    last = ''
    for ins in edfre.md.disasm(edfre.img[0x53CD15:0x53CF05], 0x53CD15):
        m = re.match(r'r8, \[rip \+ (0x[0-9a-f]+)\]', ins.op_str)
        if ins.mnemonic == 'lea' and m:
            to = ins.address + ins.size + int(m.group(1), 16)
            last = edfre.img[to:to + 64].decode('utf-16le', 'replace').split('\0')[0]
        m = re.match(r'dword ptr \[r14 \+ (0x[0-9a-f]+)\], eax', ins.op_str)
        if ins.mnemonic == 'mov' and m and last:
            found[last] = int(m.group(1), 16)
    assert all(found.get(n) == at for n, at in binds.items()), found
    edf41 = _edf41_root()
    if not edf41:
        print('skip  edf41_hector_gait (CAS part): needs EDF4.1 installed')
        return
    # Native 4.1 retreat is the forward command multiplied by xmm7=-1, not a reduced speed. Verify the
    # executable contract so a mistaken interpretation of this register cannot pass a mirrored formula test.
    import pefile
    import capstone
    with pefile.PE(os.path.join(edf41, 'EDF41.exe'), fast_load=True) as pe41:
        assert pe41.FILE_HEADER.TimeDateStamp == 0x57AAB039, 'unsupported EDF4.1 executable for gait audit'
        image41 = pe41.get_memory_mapped_image()
    assert image41[0x2237FF:0x223807] == bytes.fromhex('f30f103d51338900')
    factor_at = 0x223807 + struct.unpack_from('<i', image41, 0x223803)[0]
    assert factor_at == 0xAB6B58 and struct.unpack_from('<f', image41, factor_at)[0] == -1.0
    assert image41[0x22389E:0x2238A6] == bytes.fromhex('f30f108b78020000')
    assert image41[0x2238AE:0x2238B6] == bytes.fromhex('f30f118b38030000')
    assert image41[0x2238BB:0x2238C7] == bytes.fromhex('f30f59cff30f118b38030000')
    native = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    native.detail = True
    instructions = list(native.disasm(image41[0x223807:0x2238BB], 0x223807))
    assert instructions and instructions[-1].address + instructions[-1].size == 0x2238BB
    assert all(capstone.x86.X86_REG_XMM7 not in ins.regs_access()[1] for ins in instructions)
    walk_speed = float(re.search(r'kWalkSpeed=([0-9.]+)f', head).group(1))
    assert 'kRetreatSpeed=kWalkSpeed;' in head and walk_speed == -struct.unpack_from('<f', image41, factor_at)[0]
    import cas_graft
    import cas_legacy as cl
    import make_edf41_objects as m41
    _data, files = m41.build(edf41, rootcpk.DEFAULT_GAME, ('HECTOR301CC.SGO',))
    cas = files['OBJECT/HECTOR.CAS']
    value = {cas_graft._name(cas, at): struct.unpack_from('<f', cas, at + 12)[0] for at in cl.cas_layout(cas).tree.vcontrols}
    assert value['v_walk_animation_speed'] == 10.0 and value['v_turn_animation_speed'] == 112.0, value


@test
def edf41_hector_weapon_anim() -> None:
    """tools/make_edf41_objects.py hector_anim on HECTOR401LS (launcher / shield) and HECTOR401GG (two gatlings), built from
    the games: each 401 weapon's own CAS clips (4.1 plays them from the weapon's state machine; the class has nothing for
    them) come in the SGO key edf41_hector_anim for the plugin, on the merged model's bones: the launcher's open / close
    (its open clip ends on the model's bind pose, the rotation convention of src/hector_ai.h PoseMatrix, checked by the
    generator and again here), the gatling's barrel roll (one turn a loop), nothing for the shield."""
    import make_edf41_objects as m
    import mdb
    import rootcpk
    import sgo
    edf41 = _edf41_root()
    if not _e5c_have_game() or not edf41:
        print('skip  edf41_hector_weapon_anim: needs EDF6 and EDF4.1 installed')
        return
    data, files = m.build(edf41, rootcpk.DEFAULT_GAME, ('HECTOR401LS.SGO', 'HECTOR401GG.SGO'))
    assert not data['skipped'], data['skipped']
    ls = sgo.load(data=files['OBJECT/HECTOR401LS.SGO'])['edf41_hector_anim']
    gg = sgo.load(data=files['OBJECT/HECTOR401GG.SGO'])['edf41_hector_anim']
    kind, names, opened, closed, roll = ls[0]
    assert kind == 1.0 and names == ['gun_arms_l_gun_bottom_l', 'gun_arms_l_gun_bottom_r', 'gun_arms_l_gun_top'], ls[0]
    assert roll == [] and ls[1] == [0.0, [], [], [], []], ls[1]
    # the first shot when 4.1's would come: the launcher once open (120 frames), the shield and gatlings at once; no light
    top_ls = sgo.load(data=files['OBJECT/HECTOR401LS.SGO'])
    top_gg = sgo.load(data=files['OBJECT/HECTOR401GG.SGO'])
    assert [c['gun_battery']['charge']['time'] for c in top_ls['gun_control']] == [120.0, 1.0]
    assert [c['gun_battery']['charge']['time'] for c in top_gg['gun_control']] == [1.0, 1.0]
    assert 'edf41_hector_light' not in top_ls and len(top_ls['animation_model'][4]) == 4
    for clip in (opened, closed):
        frames, step = int(clip[0]), clip[1]
        assert frames == 31 and step == 4.0 and len(clip) == 2 + frames * len(names) * 7, (frames, step, len(clip))
    rab = mdb.rab_read(files[m.HECTOR_MODEL])
    md = mdb.mdb_read(next(f for f in rab.files if f.name.lower() == 'hector401ls.mdb').data)
    local = {md.name_of(b.name): b.local for b in md.bones}
    last = opened[2 + 30 * len(names) * 7:]
    for i, n in enumerate(names):
        got = m.pose_matrix(last[i * 7:i * 7 + 7])
        assert max(abs(a - b) for a, b in zip(got, local[n])) < 2e-3, (n, got, local[n])
    # the close clip ends where the open one starts (closed)
    first_open, last_close = opened[2:2 + len(names) * 7], closed[2 + 30 * len(names) * 7:]
    assert max(abs(a - b) for a, b in zip(first_open, last_close)) < 1e-3
    for arm, node in zip(gg, ('arms_l', 'arms_r')):
        kind, names, opened, closed, roll = arm
        assert kind == 2.0 and names == [f'gun_{node}_gun_rolling'] and opened == [] and closed == [], arm
        frames, step = int(roll[0]), roll[1]
        q0, q1 = roll[2 + 3:2 + 7], roll[2 + (frames - 1) * 7 + 3:2 + frames * 7]
        assert frames == 8 and abs(abs(q0[3]) - 1.0) < 1e-3 and abs(abs(q1[3]) - 1.0) < 1e-3, (q0, q1)   # a whole turn


@test
def edf41_hector_material_merge() -> None:
    """tools/make_edf41_objects.py mtab_merge on hand-made MTAB blobs: the merged blob names every part's materials (a
    part's map renames them), its header sums the channels on the shared timeline, and every material's track, channel
    and keys resolve to the same bytes as in its source (all offsets below the table are self-relative). Blobs on
    different timelines are refused. Why: EDF6 binds an entry to the first material of its name (EDF+0x110D570), so the
    Hector's guns (materials named like the body's) were only lit once they had names and entries of their own."""
    import struct
    import make_edf41_objects as m

    def blob(loop: int, length: float, material: str, key: float) -> bytes:
        b = struct.pack('<ifii', loop, length, 1, 1) + b'MTAB' + struct.pack('<ii', 0x200, 0x1c)
        b += struct.pack('<iii', 1, 0x5c - 0x1c, 0x28 - 0x1c)            # 0x1c entry: 1 track, its name, its tracks
        b += struct.pack('<iii', 1, 0x4c - 0x28, 0x34 - 0x28)            # 0x28 track: 1 channel, its name, its channels
        b += struct.pack('<iiiii', 1, 1, 16, 0, 0x48 - 0x34)             # 0x34 channel: its keys
        b += struct.pack('<f', key)                                       # 0x48 keys
        b += b'light0_color\0\0\0\0'                                      # 0x4c track name
        return b + material.encode('utf-16-le') + bytes(2)                # 0x5c material name

    def resolve(b: bytes, tracks: int) -> tuple[bytes, float]:
        count, name, chans = struct.unpack_from('<iii', b, tracks)
        key_at = tracks + chans + struct.unpack_from('<i', b, tracks + chans + 16)[0]
        return b[tracks + name:b.index(b'\0', tracks + name)], struct.unpack_from('<f', b, key_at)[0]

    body, gun = blob(1, 480.0, 'ufosurface', 0.25), blob(1, 480.0, 'ufosurface', 0.75)
    assert [(n, c) for n, c, _t in m.mtab_entries(body)] == [('ufosurface', 1)]
    merged = m.mtab_merge([(body, {}), (gun, {'ufosurface': 'ufosurface.hector401cannon'})])
    assert merged[:8] == body[:8] and struct.unpack_from('<ii', merged, 8) == (2, 2) and merged[16:20] == b'MTAB'
    entries = m.mtab_entries(merged)
    assert [n for n, _c, _t in entries] == ['ufosurface', 'ufosurface.hector401cannon'], entries
    assert [resolve(merged, t) for _n, _c, t in entries] == [(b'light0_color', 0.25), (b'light0_color', 0.75)]
    assert all(t % 4 == 0 for _n, _c, t in entries) and len(merged) % 4 == 0
    for other in (blob(0, 480.0, 'ufosurface', 0.0), blob(1, 180.0, 'ufosurface', 0.0)):
        try:
            m.mtab_merge([(body, {}), (other, {})])
        except ValueError:
            continue
        raise AssertionError('blobs on different timelines were merged')
    assert m.BERSERKER_DEATH_SLOT == 3
    import inspect
    src_alias = inspect.getsource(m.alias_dsgo)
    assert 'hector_materials(' in src_alias and 'BERSERKER_DEATH_SLOT' in src_alias


@test
def edf41_hector_guns() -> None:
    """Every generated Hector variant (edf41port/objects, read back) fires what 4.1's arms hold: each gun_control
    entry's bullet is the factory of its HectorWeapon row with the row's speed, lifetime, damage coefficient
    (`coef * V11`, the form EDF6's own bullets use) and blast, fire.count / interval / accuracy are the row's, the
    AI's gun range is the longer of the two guns' hector_weapon_range, the common walking numbers are 4.1's, and
    no variant has melee. Shields fire a 0-damage bullet. With the games installed the table is also checked
    against 4.1's own SGOs (the arms' content, the weapon classes, ranges and custom parameters)."""
    import struct
    import dsgo
    import make_edf41_objects as m
    import mdb
    factory = {'plasma': 'PlasmaBullet01', 'solid': 'SolidBullet02', 'rocket': 'RocketBullet01'}
    rocket_damage = {'C': 40.0, 'L': 10.0}               # 4.1's bullet_custom_param[1] of the rocket weapons
    reach = {('301', 'C'): 2000.0, ('301', 'G'): 150.0, ('301', 'L'): 150.0,
             ('401', 'C'): 2000.0, ('401', 'G'): 250.0, ('401', 'L'): 250.0, ('401', 'S'): 0.0}
    spread_shape = {'301': [0.5, 1.0, 0.5, 1.0], '401': [0.25, 0.5, 0.25, 1.0]}
    base = os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT')

    def coef(blob: object) -> float:
        assert isinstance(blob, dsgo.Blob) and len(blob.data) == 28, blob
        kind, value, var, idx, mul, end = struct.unpack('<IdIIII', blob.data)
        assert (kind, var, idx, mul, end) == (1, 3, 11, 7, 0)     # coefficient * V11
        return value

    # 21: the 20 the story's missions name and HECTOR401SS (shields on both arms; the story's M202 and DLC pack 1's M203)
    assert len(m.HECTOR_VARIANTS) == 21 and all(r in m.ROOTS and r in m.ALIASES for r in m.HECTOR_VARIANTS)
    for name, (gen, left, right, _missions) in m.HECTOR_VARIANTS.items():
        with open(os.path.join(base, name), 'rb') as f:
            top = dsgo.parse(f.read()).root
        entries = top.get('gun_control').items
        assert len(entries) == 2, name
        assert top.get('xgs_scene_object_class') == 'EDF6_Berserker_Large'
        assert top.get('WalkingEnemyBase').get('MoveSpeed').items == [6.0, 0.1], name
        assert top.get('WalkingEnemyBase').get('TurnSpeed').items == [0.3, 0.1], name   # 4.1's 0.03 rad/frame
        ai = top.get('AiSettings')
        assert ai.get('attack').get('range') == 0.0, name          # 4.1 has no melee
        assert ai.get('gun_attack').get('range') == max(reach[(gen, left)], reach[(gen, right)]), name
        assert ai.get('gun_attack').get('interval').get('min') == 300.0, name
        # the key src/edf41ai.cpp reads: each arm's own range, same weapon class on both arms, 4.1's hector_ai_setting
        key = top.get('edf41_hector')
        assert key.get('ranges').items == [reach[(gen, left)], reach[(gen, right)]], name
        assert key.get('same_weapon') == (1.0 if left == right else 0.0), name
        assert key.get('ai').items == [720.0, 1440.0, 540.0, 720.0, 150.0, 20.0, 80.0], name
        assert top.get('resource').items == [], name               # the weapon objects are not loaded
        for entry, letter in zip(entries, (left, right)):
            w = m.HECTOR_WEAPONS[(gen, letter)]
            battery = entry.get('gun_battery')
            bullet, fire = battery.get('bullet'), battery.get('fire')
            # 4.1's charge (EDF41.exe 0x225A7A / 0x227E2E): the 301 cannon 3 s, the 401 cannon open 60 + 5 s, the 401
            # launcher open 120; the rest fire at once
            want = {'Hector301_Cannon': 180.0, 'Hector401_Cannon': 360.0, 'Hector401_Launcher': 120.0}.get(w.cls, 1.0)
            assert battery.get('charge').get('time') == want, (name, w.cls)
            if w.bullet == 'none':                                   # a shield: nothing hurts
                assert bullet.get('factory') == 'SolidBullet02' and coef(bullet.get('damage')) == 0.0, name
                continue
            assert bullet.get('factory') == factory[w.bullet], name
            assert bullet.get('speed') == w.speed and bullet.get('alive_time') == w.alive, (name, letter)
            assert fire.get('count') == w.count and fire.get('interval') == w.interval, (name, letter)
            want = w.damage if w.damage is not None else rocket_damage[letter]
            got = bullet.get('damage')
            assert coef(got.items[0] if isinstance(got, dsgo.Node) else got) == want, (name, letter)
            if w.spread:
                assert fire.get('accuracy') == w.spread, (name, letter)
            if w.bullet == 'solid':
                assert bullet.get('custom_parameter').items == spread_shape[gen], (name, letter)
            if w.bullet == 'rocket':
                assert bullet.get('explosion_radius') == w.radius[0] and bullet.get('custom_parameter').items[3] == 0.0
            if w.bullet == 'plasma':
                assert bullet.get('gravity_factor') == 0.0 and isinstance(bullet.get('explosion_radius'), dsgo.Blob)
    # 401's weapons are skinned: their bones come over with blend indices that stay inside the bone table
    for name in ('HECTOR401CS.SGO', 'HECTOR401LG.SGO'):
        import edf41_objects
        archive = mdb.rab_read(edf41_objects.read(m.HECTOR_MODEL))
        body = next(x for x in archive.files if x.name.lower() == m.shared_member(name).lower())
        md = mdb.mdb_read(body.data)
        names = {md.name_of(b.name) for b in md.bones}
        assert {'gun_arms_l_gun_arms', 'gun_arms_r_gun_arms'} <= names, name
        for ob in md.objects:
            for me in ob.meshes:
                for e in (x for x in me.elems if x.name.lower() == 'blendindices'):
                    top_index = max(me.vdata[k * me.vsize + e.offset + j] for k in range(me.nverts) for j in range(4))
                    assert top_index < len(md.bones), name
    edf41 = _edf41_root()
    if not _e5c_have_game() or not edf41:
        print('skip  edf41_hector_guns (table against 4.1): needs EDF6 and EDF4.1 installed')
        return
    import rootcpk
    import sgo
    games = m.Games(edf41, rootcpk.DEFAULT_GAME)
    for name, (gen, left, right, _missions) in m.HECTOR_VARIANTS.items():
        mine = sgo.load(data=games.read4('OBJECT/' + name))
        for arm, letter in zip(mine['hector_weapon'], (left, right)):
            w = m.HECTOR_WEAPONS[(gen, letter)]
            assert m.rel_of(arm[3]) == 'OBJECT/' + w.sgo, (name, arm[3])
            weapon = sgo.load(data=games.read4('OBJECT/' + w.sgo))
            assert weapon['xgs_scene_object_class'] == w.cls
            assert m.num(weapon['hector_weapon_range']) == reach[(gen, letter)], (name, letter)
            if w.bullet == 'solid':
                assert [m.num(x) for x in weapon['bullet_custom_param']] == spread_shape[gen]
            if w.bullet == 'rocket':
                assert m.num(weapon['bullet_custom_param'][1]) == rocket_damage[letter]


@test
def edf41_ufo301_behaviour() -> None:
    """The shipped UFO301 / UFO301ACE (edf41port, read back) fire as 4.1's UfoSmall301 does through UfoSmall515's
    keys: six muzzles (3 per side), 4 volleys one every 3 frames; bullet_object BeamBullet01 (the ace LaserBullet02) with
    speed 0.65 per frame and 300 frames of life; config [60, 1.0, [10, 130], [10, 30]]; the sound table; the ace's
    level_adjust; and E515's four material_animation entries (an empty list crashed the constructor)."""
    import sgo
    base = os.path.join(ROOT, 'edf41port', 'objects', 'OBJECT')
    for name, cls, sounds in (('UFO301', 'BeamBullet01', 7), ('UFO301ACE', 'LaserBullet02', 8)):
        with open(os.path.join(base, name + '.SGO'), 'rb') as f:
            d = sgo.load(data=f.read())
        assert d['xgs_scene_object_class'] == 'UfoSmall515', name
        battery = d['battery_config']
        assert [b[0] for b in battery] == ['muzzleL01', 'muzzleL02', 'muzzleL03', 'muzzleR01', 'muzzleR02', 'muzzleR03']
        for _muzzle, slot, delay in battery:
            key, shots, _a, _cool, _b, _c, period, per_fire, spread = slot
            assert (key, shots, period, per_fire, spread, delay) == ('bullet_object', 4, 3, 1, 0, 0), name
        assert d['bullet_object'][0] == cls, name
        pairs = dict(zip(d['bullet_object'][1][1][0::2], d['bullet_object'][1][1][1::2]))
        assert abs(pairs[0] - 0.65) < 1e-6 and pairs[1] == 300 and abs(pairs[8] - 0.33) < 1e-6, (name, pairs)
        assert [round(x, 3) for x in pairs[12]] == [1.1, 0.051, 0.051, 1.0], name
        assert d['config'] == [60, 1.0, [10.0, 130.0], [10.0, 30.0]], name
        assert len(d['ufo_se']) == sounds, name
        assert len(d['material_animation']) == 4, name
    with open(os.path.join(base, 'UFO301ACE.SGO'), 'rb') as f:
        ace = sgo.load(data=f.read())
    assert [round(float(x), 6) for x in ace['level_adjust']] == [1.5, 0.038462, 4.5, 0.0, 1.0]
    assert ace['bullet_object'][1][2] == [1]


@test
def rabpool_round_trip() -> None:
    """pylib/rabpool.py: two archives sharing a member ship it once and both come back byte for byte; a file that
    is not an archive passes as it is; an archive mdb.rab_write would lay out otherwise is refused; a member read from
    the player's EDF6 is not stored, and comes back from there only with its own bytes."""
    import hashlib
    import mdb
    import rabpool
    shared = mdb.RabFile('tex.dds', 0, 0, b'T' * 64)
    a = mdb.rab_write(mdb.Rab(1, ['TEXTURE', 'MODEL'], [shared, mdb.RabFile('a.mdb', 1, 0, b'A' * 9)]))
    b = mdb.rab_write(mdb.Rab(1, ['TEXTURE', 'MODEL'], [shared, mdb.RabFile('b.mdb', 1, 0, b'B' * 7, (0, 3))]))
    files = {'OBJECT/A.MRAB': a, 'OBJECT/B.RAB': b, 'OBJECT/X.SGO': b'sgo'}
    packed = rabpool.pack(files)
    members = [k for k in packed if k.startswith(rabpool.MEMBERS + '/')]
    assert len(members) == 3, members   # tex.dds once
    assert set(packed) - set(members) == {'OBJECT/A.MRAB.members.json', 'OBJECT/B.RAB.members.json', 'OBJECT/X.SGO'}
    for rel, data in files.items():
        assert rabpool.unpack(rel, packed.__getitem__) == data, rel
        assert all(s in packed for s in rabpool.stored(rel)), rel
    try:
        rabpool.pack({'OBJECT/C.MRAB': a + b'tail'})
    except ValueError:
        pass
    else:
        raise AssertionError('an archive rab_write does not rebuild was packed')
    # a member the player's EDF6 ships (the King's Monster501 textures): not stored, its row names the source, and
    # unpack reads it there, checked by its SHA-256; without a source it refuses (Unsourced), never rebuilds without it
    tex_sha = hashlib.sha256(shared.stored).hexdigest()
    src = ['Root.cpk', 'OBJECT/STOCK.MRAB', 0, 'tex.dds']
    packed = rabpool.pack({'OBJECT/A.MRAB': a}, {'OBJECT/A.MRAB': {tex_sha: src}})
    assert f'{rabpool.MEMBERS}/{tex_sha}' not in packed and len([k for k in packed if k.startswith(rabpool.MEMBERS)]) == 1
    rows = json.loads(packed['OBJECT/A.MRAB.members.json'])['files']
    assert rows[0][5:] == [tex_sha, src] and len(rows[1]) == 6, rows
    assert rabpool.sources('OBJECT/A.MRAB', packed.__getitem__) == [src]
    assert rabpool.unpack('OBJECT/A.MRAB', packed.__getitem__, lambda s: shared.stored if s == src else b'') == a
    for source in (None, lambda s: b'other bytes'):
        try:
            rabpool.unpack('OBJECT/A.MRAB', packed.__getitem__, source)
        except (rabpool.Unsourced if source is None else ValueError):
            pass
        else:
            raise AssertionError(f'an archive with a sourced member came back with source {source}')
    try:
        rabpool.pack({'OBJECT/A.MRAB': a}, {'OBJECT/A.MRAB': {'0' * 64: src}})
    except ValueError:
        pass
    else:
        raise AssertionError('a source for a member the archive lacks was accepted')


@test
def mab_retarget_synthetic() -> None:
    """pylib/mab_legacy.mab_retarget on the small block of either flag: only the named record's node changes, onto a
    string the block holds or one appended at its end (the block stays a valid layout); a name it lacks is refused."""
    import mab_legacy as ml
    for flag in (0x03, 0x83):
        mini = _mini_mab(flag)
        before = {l.name: l for _i, l in ml.mab_locators(mini)}
        for node in ('head', 'gun'):                    # one the block has, one it does not
            out = ml.mab_retarget(mini, 'bang', node)
            after = {l.name: l for _i, l in ml.mab_locators(out)}
            assert after['bang'].node == node and set(after) == set(before)
            assert all(after[n] == before[n] for n in before if n != 'bang')
        assert ml.mab_retarget(mini, 'bang', 'chest') == mini
        try:
            ml.mab_retarget(mini, 'nothing', 'gun')
        except ValueError:
            continue
        raise AssertionError('a locator the block lacks was retargeted')


@test
def edf41_dlc_packs() -> None:
    """EDF4.1's two DLC mission packs (tools/make_edf41_missions.py --dlc -> edf41port/mp01, mp02): packs of their own
    slotted right after the 4.1 story so the three 4.1 packs' content ids run EDF41Content .. +2, which the plugin owns
    as a run (src/edf5campaign.cpp kPacks41) and autopilot can enter (edf41mp01:ROW); their committed scripts are 4.1's
    converted (pylib/bvm41.py): no DLC mount (mp01:/) left, the stack balanced under EDF6's natives, every object they
    name under the packs' folders registered (edf41port/objects.json); the release bundles them; a mission the
    converter refused is listed with its reason, never shipped."""
    import hashlib
    import bvm41
    import edf41_objects
    tags = [p.tag for p in e5c.SLOTS]
    assert tags[-3:] == ['EDF41', 'EDF41MP01', 'EDF41MP02'], tags   # appended: no installed pack's content id moves
    assert [p.pack for p in e5c.PACKS41] == [e5c.PACK41, e5c.PACK41_MP01, e5c.PACK41_MP02]
    assert e5c.edf41_content({'EDF41MP02': 71}) == 69 and e5c.edf41_content({'EDF41': 69}) == 69
    assert e5c.edf41_content({}) == 0
    assert e5c.managed('MISSION/EDF41MP01/M401/MISSION.BVM') and e5c.managed(e5c.PACK41_MP02.files()[0])
    plugin = src('src/edf5campaign.cpp')
    assert 'constexpr int kPacks41=3;' in plugin and 'id>=packs::kEdf41 && id<packs::kEdf41+kPacks41' in plugin
    assert 'RunOf(71)==69 && !RunOf(72)' in plugin
    auto = src('tests/autopilot/autopilot.cpp')
    assert '"edf41mp01:",10' in auto and 'first41+pack41' in auto
    release = src('tools/build_release.py')
    assert "for pack in ('mp01', 'mp02'):" in release
    registry = {rel.upper() for rel in edf41_objects.registry()['files']}
    # left out on purpose, with the reason (the 401 mothership): its missions are skipped by the planner instead
    excluded = {s['object'].upper() for s in edf41_objects.registry().get('skipped', []) if s.get('reason')}
    for p41 in e5c.PACKS41[1:]:
        text = e5c.edf41_text(p41.data)
        assert text['missions'], p41.data
        for r in text.get('refused', []):
            assert r['id'] not in {m['id'] for m in text['missions']} and r['why'], r
        for m in text['missions']:
            for name in m['files']:
                data = e5c.edf41_script(m['id'], name, p41.data)
                assert hashlib.sha256(data).hexdigest() == m['sha'][name], (p41.folder, m['id'], name)
                if not name.endswith('.BVM'):
                    continue
                strings = e5c.utf16_strings(data)
                assert not [s for s in strings if re.search(r'(?i)mp0\d:/', s)], (m['id'], name)
                assert bvm41.stack_check(bvm41.parse(data)) == [], (m['id'], name)
                for ref in e5c.asset_refs(data):
                    if re.match(r'(?i)Object/EDF41MP0\d/', ref):
                        assert ref.upper() in registry | excluded, (m['id'], ref)


@test
def edf41_zh_patch_rows() -> None:
    """pylib/edf41zh.py on synthetic data: the Chinese patch's MISSIONTEXT rows are taken only when they are 4.1's rows
    (the same count, each row's JA pair 4.1's, a Chinese title), its files found whatever their case
    (411380/mission/MissionText.SGO), and Traditional Chinese (CN) in EDF6's own forms (鎗, 砲, 回復, 雷射, 「」)."""
    import edf41zh
    orig = [['遮風地帯', 'JA brief', ' Wind Shear', 'EN brief', 'x', 'y'], ['追跡', 'JA 2', 'Tracking', 'EN 2', 'x', 'y']]
    patch = [['遮風地帯', 'JA brief', ' 风切变 ', '简报', 'x', 'y'], ['追跡', 'JA 2', '追踪', '简报二', 'x', 'y']]
    assert edf41zh.merge_rows(orig, patch, 't') == [('风切变', '简报'), ('追踪', '简报二')]
    refusals = (patch[:1],                                                    # a row short
                [patch[0], ['追跡', 'another JA brief', '追踪', 'b']],          # another version's row
                [patch[1], patch[0]],                                         # rows in another order
                [patch[0], ['追跡', 'JA 2', 'Tracking', 'EN 2']])               # left in English
    for bad in refusals:
        try:
            edf41zh.merge_rows(orig, bad, 't')
        except SystemExit:
            continue
        raise AssertionError(f'taken: {bad}')
    with tempfile.TemporaryDirectory() as zh:
        os.makedirs(os.path.join(zh, '411380', 'mission'))
        with open(os.path.join(zh, '411380', 'mission', 'MissionText.SGO'), 'wb') as f:
            f.write(b'x')
        found = edf41zh.locate(zh, '411380', 'mission', 'MISSIONTEXT.SGO')
        assert found.endswith(os.path.join('411380', 'mission', 'MissionText.SGO')), found
        assert edf41zh.note(zh, found).startswith('mods/411380/mission/MissionText.SGO (sha256 ')
        try:
            edf41zh.locate(zh, 'mission', 'MISSIONTEXT.SGO')
            raise AssertionError('a file the patch lacks was found')
        except SystemExit as e:
            assert '--zh' in str(e), e
    if not edf41zh.available():
        print('skip  edf41_zh_patch_rows (Traditional): no OpenCC (pip install opencc-python-reimplemented)')
        return
    for sc, cn in (('火焰枪', '火焰鎗'), ('高周波回复枪', '高週波回復鎗'), ('激光炮', '雷射砲'), ('“风切变”', '「風切變」')):
        assert edf41zh.traditional(sc) == cn, (sc, edf41zh.traditional(sc))


@test
def edf41_zh_mission_texts() -> None:
    """The 4.1 port's committed mission texts (tools/make_edf41_missions.py, story and --dlc): SC the Simplified
    Chinese patch's (story M104 风切变, Mission Pack 1's first mission 空中攻势), CN its Traditional, KR English, and
    'source' naming the patch file; JA / EN 4.1's own, row for row (where EDF4.1 is installed); the scripts the ones
    each sha names (only the text came from the patch)."""
    import hashlib
    import edf41dlc
    import edf41zh
    import gamedir
    import rootcpk
    import sgo
    packs = {p41.folder: e5c.edf41_text(p41.data) for p41 in e5c.PACKS41}
    by_id = {f: {m['id']: m for m in t['missions']} for f, t in packs.items()}
    m104 = by_id['EDF41']['M104']
    assert (m104['title']['JA'], m104['title']['EN'], m104['title']['SC']) == ('遮風地帯', 'Wind Shear', '风切变'), m104['title']
    assert packs['EDF41MP01']['missions'][0]['title']['SC'] == '空中攻势', packs['EDF41MP01']['missions'][0]['title']
    where = {'EDF41': 'mods/mission/MISSIONTEXT.SGO', 'EDF41MP01': 'mods/410780/mission/MISSIONTEXT.SGO',
             'EDF41MP02': 'mods/411380/mission/MissionText.SGO'}
    for p41 in e5c.PACKS41:
        text = packs[p41.folder]
        assert where[p41.folder] in text['source'] and edf41zh.PATCH in text['source'], text['source']
        for m in text['missions']:
            for k in ('title', 'brief'):
                assert edf41zh.CJK.search(m[k]['SC']), (p41.folder, m['id'], k)
                assert m[k]['KR'] == m[k]['EN'], (p41.folder, m['id'], k)
                if edf41zh.available():
                    assert m[k]['CN'] == edf41zh.traditional(m[k]['SC']), (p41.folder, m['id'], k)
            for name in m['files']:
                data = e5c.edf41_script(m['id'], name, p41.data)
                assert hashlib.sha256(data).hexdigest() == m['sha'][name], (p41.folder, m['id'], name)
    root41 = gamedir.find_other(gamedir.EDF41, near=rootcpk.DEFAULT_GAME)
    if root41 is None:
        print('skip  edf41_zh_mission_texts (JA / EN against 4.1): EDF4.1 not installed')
        return
    from make_edf41_objects import archives
    g41 = archives(root41)
    tables = {'EDF41': g41['MISSION/MISSIONTEXT.SGO'].read('MISSION', 'MISSIONTEXT.SGO')}
    for dlc in edf41dlc.find(root41).values():
        tables[dlc.folder] = dlc.read('MISSION/MISSIONTEXT.SGO')
    for folder, blob in tables.items():
        rows = sgo.plain(sgo.read(blob)[1])['table']
        shipped = packs[folder]['missions']
        refused = {r['id'] for r in packs[folder].get('refused', [])}
        assert len(shipped) + len(refused) == len(rows), (folder, len(shipped), len(rows))
        if refused:   # rows of refused missions are not in missions.json: match the rest by their JA title
            rows = [r for r in rows if r[0] in {m['title']['JA'] for m in shipped}]
        for m, row in zip(shipped, rows):
            assert (m['title']['JA'], m['brief']['JA'], m['title']['EN'], m['brief']['EN']) == \
                (row[0], row[1], row[2].strip(), row[3]), (folder, m['id'])


@test
def edf41_zh_weapon_names() -> None:
    """The 4.1 weapons' committed names (tools/make_edf41_weapons.py -> edf41port/weapons.json, calls.json): SC the
    Simplified Chinese patch's where it names the weapon (フュージョンブラスター 融合爆裂枪), CN its Traditional
    (融合爆裂鎗), the converted weapon named as its texts (name.sc / name.cn), 'source' naming the patch; JA / EN
    4.1's _WEAPONTEXT names (where EDF4.1 is installed)."""
    import edf41zh
    import gamedir
    import rootcpk
    import sgo
    weapons = []
    for name in ('weapons.json', 'calls.json'):
        with open(os.path.join(ROOT, 'edf41port', name), encoding='utf-8') as f:
            data = json.load(f)
        assert "mods/weapon/*.SGO 'name'[1]" in data['source'] and edf41zh.PATCH in data['source'], data['source']
        weapons += data['weapons']
    with open(os.path.join(ROOT, 'edf41port', 'translations.json'), encoding='utf-8') as f:
        translated = json.load(f)
    by_sgo = {w['sgo']: w for w in weapons}
    fusion = by_sgo['Weapon003.sgo']['text']
    assert (fusion['JA'][0], fusion['SC'][0]) == ('フュージョンブラスター', '融合爆裂枪'), fusion['SC']
    patched = 0
    for w in weapons:
        t = w['text']
        assert t['KR'][0] == translated[w['sgo']]['KR'][0], w['id']
        if t['SC'][0] != translated[w['sgo']]['SC'][0]:
            patched += 1
            assert edf41zh.CJK.search(t['SC'][0]), w['id']
            if edf41zh.available():
                assert t['CN'][0] == edf41zh.traditional(t['SC'][0]), w['id']
        doc = w['weapon']   # a dsgo dump: {'items': [...], 'names': {index: key}}
        names = {k: doc['items'][int(i)] for i, k in doc['names'].items() if k in ('name.sc', 'name.cn')}
        assert names == {'name.sc': t['SC'][0], 'name.cn': t['CN'][0]}, (w['id'], names)
    assert patched > 250, patched
    root41 = gamedir.find_other(gamedir.EDF41, near=rootcpk.DEFAULT_GAME)
    if root41 is None:
        print('skip  edf41_zh_weapon_names (JA / EN against 4.1): EDF4.1 not installed')
        return
    texts = sgo.load(data=rootcpk.Game(root41).read('WEAPON', '_WEAPONTEXT.SGO'))['text_table']
    for w in weapons:
        row = texts[w['edf41_row']]
        assert (w['text']['JA'][0], w['text']['EN'][0]) == (row[0], row[2]), w['id']


# ------------------------------------------------------------------ EDF5 Mission Pack 1 maps and fortress (tools/edf5_dlc.py)
class _FakeEdf6:
    """tools/edf5_dlc.Edf6 over in-memory archives {archive: {Mods path: bytes}} (no .cpk to build)."""
    archives: dict[str, dict[str, bytes]] = {}

    def __init__(self, root: str) -> None:
        self.where = {rel.upper(): a for a, files in self.archives.items() for rel in files}

    def missing(self, archive: str, rel: str) -> str | None:
        return None if rel in self.archives.get(archive, {}) else f'no {rel} in {archive}'

    def read(self, archive: str, rel: str) -> bytes:
        return self.archives[archive][rel]

    def stamp(self, archive: str) -> tuple[str, int, int]:   # changes with the content: the hash caches see an update
        import hashlib
        return archive, int(hashlib.sha256(repr(sorted(self.archives[archive].items())).encode()).hexdigest()[:12], 16), 0


def _edf5dlc_data(out: str) -> tuple[dict[str, bytes], dict[str, dict[str, bytes]]]:
    """A small data set written to `out` with tools/edf5_dlc.pack: map MAP/TESTMAP (a RAB with one member of its own
    and one EDF6 ships, a file stored whole, a weather taken whole from EDF6) and an object group. Returns (the final
    bytes of the files made here, the stand-in EDF6 archives)."""
    import hashlib
    import edf5_dlc
    import mdb
    shared = b'edf6 texture' * 50
    noise = b''.join(hashlib.sha256(bytes([i])).digest() for i in range(100))   # xz keeps it: several parts
    edf6 = {'Chunk01.cpk': {'MAP/DONOR.RAB': mdb.rab_write(mdb.Rab(272, ['TEXTURE'], [
        mdb.RabFile('pad.dds', 0, 0, b'pad'), mdb.RabFile('ground.dds', 0, 0, shared)])),
        'MAP/DONOR_FINE.MAE': b'donor weather'},
        'Root.cpk': {'OBJECT/STOCK.MRAB': b'stock model'}}
    rab = mdb.rab_write(mdb.Rab(272, ['TEXTURE', 'MODEL'], [
        mdb.RabFile('own.dds', 0, 0, b'only in EDF5' * 40), mdb.RabFile('ground.dds', 0, 0, shared),
        mdb.RabFile('part.mdb', 1, 0, b'a model' * 30)]))
    made = {'MAP/TESTMAP.RAB': rab, 'MAP/TESTMAP.FMB': noise + bytes(30000), 'OBJECT/TEST_DLC.SGO': b'sgo'}
    sources = {modfiles.sha256(shared): ['Chunk01.cpk', 'MAP/DONOR.RAB', 1, 'ground.dds']}
    whole = {'MAP/TESTMAP_FINE.MAE': {'kind': 'edf6', 'archive': 'Chunk01.cpk', 'path': 'MAP/DONOR_FINE.MAE',
                                      'sha': modfiles.sha256(b'donor weather')}}
    groups = {'TESTMAP': {'kind': 'map', 'missions': ['DLC/DM018'], 'needs': [],
                          'files': ['MAP/TESTMAP.FMB', 'MAP/TESTMAP.RAB', 'MAP/TESTMAP_FINE.MAE']},
              'OBJECT/TEST_DLC.SGO': {'kind': 'object', 'missions': ['DLC/DM015'], 'needs': ['OBJECT/STOCK.MRAB'],
                                      'files': ['OBJECT/TEST_DLC.SGO']}}
    with patched(edf5_dlc, PART=1000):   # the FMB's blob in several parts
        stored, stats = edf5_dlc.pack(groups, made, whole, sources)
    assert stats['shared'] == len(shared), stats
    for rel, data in stored.items():
        path = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
    return made, edf6


@contextlib.contextmanager
def _edf5dlc_game() -> Iterator[tuple[str, dict[str, bytes], dict[str, dict[str, bytes]]]]:
    """A stand-in game (Mods empty) with _edf5dlc_data as the tools' data and its EDF6 archives as the player's."""
    import edf5_dlc
    with tempfile.TemporaryDirectory(prefix='edf6vc-e5d-') as tmp:
        data, game = os.path.join(tmp, 'data'), os.path.join(tmp, 'game')
        os.makedirs(os.path.join(game, 'Mods'))
        made, edf6 = _edf5dlc_data(data)
        _FakeEdf6.archives = edf6
        with patched(edf5_dlc, DLC=data, Edf6=_FakeEdf6):
            yield game, made, edf6


@test
def edf5_dlc_round_trip() -> None:
    """tools/edf5_dlc.py rebuilds what pack stored byte for byte: a RAB from its manifest (a member from the store, one
    read from the player's EDF6 and SHA-checked), a file stored compressed in several parts, a file taken whole from
    EDF6; the RAB's EDF6 member is not in the store, and a stored part damaged is refused, not installed."""
    import edf5_dlc
    with _edf5dlc_game() as (game, made, edf6):
        reg = edf5_dlc.registry()
        blob = reg['blobs'][modfiles.sha256(made['MAP/TESTMAP.FMB'])]
        assert reg['files']['MAP/TESTMAP.FMB']['kind'] == 'blob' and blob['parts'] > 1 and blob['codec'] == 'xz', blob
        rows = edf5_dlc.manifest('MAP/TESTMAP.RAB')['files']
        assert [r[6] is None for r in rows] == [True, False, True], rows
        assert modfiles.sha256(edf6['Chunk01.cpk']['MAP/DONOR.RAB']) not in reg['blobs']
        files, skipped = edf5_dlc.build(game)
        assert not skipped and set(files) == {*made, 'MAP/TESTMAP_FINE.MAE'}, (skipped, list(files))
        for rel, data in made.items():
            assert files[rel] == data, rel
        assert files['MAP/TESTMAP_FINE.MAE'] == b'donor weather'
        sha = modfiles.sha256(made['MAP/TESTMAP.FMB'])
        part = os.path.join(edf5_dlc.DLC, *edf5_dlc.blob_name(sha, reg['blobs'][sha]['codec'], 0).split('/'))
        with open(part, 'r+b') as f:
            f.write(b'\0\0\0\0')
        try:
            files['MAP/TESTMAP.FMB']
        except (lzma.LZMAError, ValueError):   # the xz stream or the store's own check refuses it
            pass
        else:
            raise AssertionError('a damaged blob part was put together')


@test
def edf5_dlc_edf6_changed() -> None:
    """A member the data reads from the player's EDF6 that holds other bytes there now (EDF6 updated), or an EDF6 file a
    group needs that is gone, leaves that group out with the reason; the other group is installed. A group already
    installed with its final bytes needs no source any more."""
    import edf5_dlc
    import mdb
    with _edf5dlc_game() as (game, made, edf6):
        donor = mdb.rab_read(edf6['Chunk01.cpk']['MAP/DONOR.RAB'])
        donor.files[1].stored = b'patched by an update'
        edf6['Chunk01.cpk']['MAP/DONOR.RAB'] = mdb.rab_write(donor)
        why = dict(edf5_dlc.unavailable(game))
        assert set(why) == {'TESTMAP'} and 'ground.dds' in why['TESTMAP'], why
        files, skipped = edf5_dlc.build(game)
        assert 'MAP/TESTMAP.RAB' not in files and 'OBJECT/TEST_DLC.SGO' in files, list(files)
        edf5_dlc.install(game, files)
        del edf6['Root.cpk']['OBJECT/STOCK.MRAB']
        why = dict(edf5_dlc.unavailable(game))
        assert 'OBJECT/STOCK.MRAB' in why['OBJECT/TEST_DLC.SGO'], why
    with _edf5dlc_game() as (game, made, edf6):
        edf5_dlc.install(game, edf5_dlc.build(game)[0])
        edf6['Chunk01.cpk']['MAP/DONOR_FINE.MAE'] = b'another weather'
        assert edf5_dlc.unavailable(game) == [], 'an installed map needs its EDF6 sources again'
        os.remove(os.path.join(game, 'Mods', 'MAP', 'TESTMAP_FINE.MAE'))
        why = dict(edf5_dlc.unavailable(game))
        assert 'DONOR_FINE.MAE' in why.get('TESTMAP', ''), why


@test
def edf5_dlc_ownership() -> None:
    """tools/edf5_dlc.py install writes every file as the ledger owner edf5dlc, a second install writes nothing, remove
    deletes them; a file of the same name another mod put in Mods is never written over: its group is left out (said
    why), the other installed."""
    import edf5_dlc
    with _edf5dlc_game() as (game, made, edf6):
        files, skipped = edf5_dlc.build(game)
        written = edf5_dlc.install(game, files)
        led = ledger.Ledger(game)
        assert len(written) == len(files) and {ledger.key(r) for r in files} == set(led.owned_by(edf5_dlc.OWNER))
        assert edf5_dlc.install(game, edf5_dlc.build(game)[0]) == [], 'a second install rewrote files it already holds'
        deleted, kept = edf5_dlc.remove(game)
        assert not kept and len(deleted) == len(files) and not ledger.Ledger(game).owned_by(edf5_dlc.OWNER)
        theirs = os.path.join(game, 'Mods', 'MAP', 'TESTMAP.FMB')
        modfiles.atomic_write(theirs, b'another mod')
        files, skipped = edf5_dlc.build(game)
        assert [g for g, _ in skipped] == ['TESTMAP'] and 'MAP/TESTMAP.FMB' in skipped[0][1], skipped
        edf5_dlc.install(game, files)
        assert modfiles.read(theirs) == b'another mod', "another mod's file was written over"
        assert set(ledger.Ledger(game).owned_by(edf5_dlc.OWNER)) == {'OBJECT/TEST_DLC.SGO'}
        ours = os.path.join(game, 'Mods', 'OBJECT', 'TEST_DLC.SGO')   # ours, edited by someone since: kept
        modfiles.atomic_write(ours, b'edited by hand')
        files, skipped = edf5_dlc.build(game)
        assert {g for g, _ in skipped} == {'TESTMAP', 'OBJECT/TEST_DLC.SGO'} and not list(files), skipped
        edf5_dlc.install(game, files)
        assert modfiles.read(ours) == b'edited by hand', 'a file someone edited since our write was written over'


@test
def edf5_dlc_campaign_offers_the_four() -> None:
    """make_edf5_campaign.plan offers DM015 / DM018 / DM019 / DM020 (Mission Pack 1) when tools/edf5_dlc.py will write
    their maps and fortress, and skips one whose part it leaves out, saying why (as the EDF4.1 packs' Skip41)."""
    import edf5_dlc
    if not _e5c_have_game():   # the plan reads the real game's Root.cpk: a developer's machine (CI has no game)
        return
    reg = edf5_dlc.registry()
    maps = {f'{g}.MAC' for g, x in reg['groups'].items() if x['kind'] == 'map'}
    names = {tuple(rel.split('/', 1)) for x in reg['groups'].values() if x['kind'] == 'object' for rel in x['files']}
    four = {'DLC/DM015', 'DLC/DM018', 'DLC/DM019', 'DLC/DM020'}
    with _e5c_game() as (root, _game):
        with patched(edf5_dlc, offered=lambda _root: (maps, names, {})):
            p = e5c.plan(root)
        dlc1 = {r['path'] for r in p['rows'] if r['group'] == 'dlc1' and e5c.available(r)}
        assert four <= dlc1, sorted(four - dlc1)
        assert not [s for s in p['skipped'] if s[0] == 'dlc1'], p['skipped']
        assert {m for x in reg['groups'].values() for m in x['missions']} == four, reg['groups']
        with patched(edf5_dlc, offered=lambda _root: (maps - {'IG_DESERT508.MAC'}, names, {'IG_DESERT508': '测试'})):
            p = e5c.plan(root)
        why = {s[1]: s[2] for s in p['skipped'] if s[0] == 'dlc1'}
        assert set(why) == {'DLC/DM018', 'DLC/DM020'} and all('未安装：测试' in w for w in why.values()), why
        # ...as placeholder rows, at their place (a room's MISSION_NO is the row on every machine)
        rows = [r for r in p['rows'] if r['group'] == 'dlc1']
        assert [r['path'] for r in rows] == [m['path'] for m in json.load(open(e5c.TEXT, encoding='utf-8'))['dlc1']]
        assert {r['path'] for r in rows if not e5c.available(r)} == set(why)


@test
def edf5_dlc_shipped() -> None:
    """The committed data (tools/make_edf5_dlc.py -> edf5port/dlc): every file its groups name has an entry, every blob
    every part, every RAB a manifest whose rows name a stored blob or an EDF6 source, and no committed file is 50 MB or
    more (no git LFS)."""
    import edf5_dlc
    reg = edf5_dlc.registry()
    assert set(reg['groups']) == {'IG_DESERT508', 'NG_DLC_EDFROOM03', 'OBJECT/E510_DLC_FORTRESS.SGO'}, list(reg['groups'])
    shipped = set()
    for top, _dirs, names in os.walk(edf5_dlc.DLC):
        for n in names:
            path = os.path.join(top, n)
            assert os.path.getsize(path) < 50 * 2**20, path
            shipped.add(os.path.relpath(path, edf5_dlc.DLC).replace(os.sep, '/'))
    want = {edf5_dlc.REGISTRY}
    for sha, b in reg['blobs'].items():
        want |= {edf5_dlc.blob_name(sha, b['codec'], i) for i in range(b['parts'])}
    for g in reg['groups'].values():
        for rel in g['files']:
            entry = reg['files'][rel]
            if entry['kind'] == 'rab':
                want.add(rel + edf5_dlc.MEMBERS)
                for row in edf5_dlc.manifest(rel)['files']:
                    assert row[6] or row[5] in reg['blobs'], (rel, row)
            elif entry['kind'] == 'blob':
                assert entry['sha'] in reg['blobs'], rel
            else:
                assert entry['kind'] == 'edf6' and entry['path'].startswith('MAP/'), (rel, entry)
    assert shipped == want, (sorted(shipped - want)[:5], sorted(want - shipped)[:5])


@test
def sent_crash_dump_is_this_runs() -> None:
    """docs/feedback-2026-10-12-testhub.md 旁支问题: report #6 sent another run's dump with its logs. collect() keeps a
    dump only of this install (dump_matches) and of the run the logs end with (dump_in_session: written after the
    plugins' last loading line, by a process that started then); the cases are in tests/testhub_dump_source_test.py."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('testhub_dump_source_test',
                                                  os.path.join(ROOT, 'tests', 'testhub_dump_source_test.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        failed = module.main()
    assert failed == 0, out.getvalue()


@test
def edf41_kill_counters_shipped() -> None:
    """The user, 2026-10-18: 「4.1导入单位的sgo里的gameobjectkillcount写入单独的类别」. Every shipped 4.1 object that counts
    its kills counts into one of the port's own counters (pylib/edf41kills.py; docs/kill-counter-re.md), never into an
    EDF6 category or a name EDF6's counters lack: the Hectors, the quadruped, the Deroys, the dragons, the motherships
    and the Earth Eater's core (4.1's Brain) each their own; every counter is used. No game needed (CI)."""
    import edf41_objects as eo
    import edf41kills as kills
    import make_edf41_objects as m
    reg = eo.registry()
    counted: dict[str, str] = {}
    for rel in reg['files']:
        if rel.endswith('.SGO'):
            name = m.kill_counter(eo.read(rel))
            if name is not None:
                counted[rel] = name
    assert counted and set(counted.values()) <= set(kills.KEYS), sorted(set(counted.values()) - set(kills.KEYS))
    assert set(counted.values()) == set(kills.KEYS), sorted(set(kills.KEYS) - set(counted.values()))
    want = {'OBJECT/HECTOR301CC.SGO': 'HectorKillCount', 'OBJECT/HECTOR401SS.SGO': 'HectorKillCount',
            'OBJECT/ALIEN4LEGTANK301.SGO': '4LegTankKillCount', 'OBJECT/DEIROI401L.SGO': 'DeiroiKillCount',
            'OBJECT/UFOMOTHER401_COREPANEL.SGO': 'MotherShip401KillCount',
            'OBJECT/EDF41MP02/UFOMOTHER401_DLC_COREPANEL.SGO': 'MotherShip401KillCount',
            'OBJECT/UFOMOTHER301.SGO': 'MotherShip301KillCount', 'OBJECT/UFOROBO401.SGO': 'UfoRoboKillCount',
            'OBJECT/DRAGONBIG401.SGO': 'BigDragonKillCount', 'OBJECT/EDF41/DRAGONSMALL401.SGO': 'DragonKillCount',
            'OBJECT/EDF41MP01/REDSMALLDRAGON_HS.SGO': 'DragonKillCount', 'OBJECT/ANTHILL301.SGO': 'NestKillCount',
            'OBJECT/EDF41MP02/ANTHILL301LARGEMP02.SGO': 'NestKillCount', 'OBJECT/UFOCARRIER301.SGO': 'Carrier1KillCount',
            'OBJECT/UFOCARRIER401.SGO': 'Carrier2KillCount', 'OBJECT/EDF41MP01/GIANTANT04.SGO': 'GiantAntKillCount'}
    for rel, name in want.items():
        assert counted.get(rel) == kills.PREFIX + name, (rel, counted.get(rel))
    # the parts count nothing (as in 4.1): one Earth Eater / quadruped / Deroy is one kill, of its core or body
    for rel in ('OBJECT/UFOMOTHER401_PANEL01.SGO', 'OBJECT/ALIEN4LEGTANK301_CANNON.SGO', 'OBJECT/DEIROIFOOTPARTS_TOE.SGO'):
        assert rel not in counted, rel
    # the generator's pass: 4.1's names and the nests' EDF6 template name to the port's; anything else refused
    files = {'OBJECT/A.SGO': _sgo_bytes({'game_object_kill_counter': 'HectorKillCount'}),
             'OBJECT/B.SGO': _sgo_bytes({'game_object_kill_counter': 'AntHillKillCount'}),
             'OBJECT/C.SGO': _sgo_bytes({'xgs_scene_object_class': 'X'})}
    files['OBJECT/D.SGO'] = m.as_dsgo(_sgo_bytes({'game_object_kill_counter': 'DragonKillCount'}))
    before_c = files['OBJECT/C.SGO']
    m.kill_counters(files)
    assert [m.kill_counter(files[r]) for r in ('OBJECT/A.SGO', 'OBJECT/B.SGO', 'OBJECT/D.SGO')] == \
        ['edf41_HectorKillCount', 'edf41_NestKillCount', 'edf41_DragonKillCount']
    assert files['OBJECT/C.SGO'] == before_c and files['OBJECT/D.SGO'][:4] == b'DSGO'
    try:
        m.kill_counters({'OBJECT/E.SGO': _sgo_bytes({'game_object_kill_counter': 'BerserkerAKillCount'})})
    except ValueError:
        pass
    else:
        raise AssertionError('an EDF6-only counter on a 4.1 object passed')


def _sgo_bytes(members: dict) -> bytes:
    import sgo
    return sgo.write_depth_first(258, dict(members))


@test
def edf41_kill_counters_plugin_table() -> None:
    """The plugin's list of the counters (src/edf41_kill_counters.h, the battle record's added rows in this order) is
    pylib/edf41kills.py's; the page fits its bounds (90 stock rows + ours <= 127, an imm8 compare); the plugin builds
    and installs it (src/killcount.cpp, CMake, plugin.cpp); every name is a *KillCount row (shown when not 0)."""
    import edf41kills as kills
    header = src('src/edf41_kill_counters.h')
    names = re.findall(r'L"([^"]+)"', header)
    assert tuple(names) == kills.KEYS, (names, kills.KEYS)
    assert 90 + len(names) <= kills.TABLE_ROWS
    assert all(n.endswith('KillCount') and n.startswith(kills.PREFIX) for n in names)
    assert 'target_sources(EDF6VehicleCrew PRIVATE src/killcount.cpp)' in src('CMakeLists.txt')
    assert re.search(r'^\s+InstallKillCounters\(\);', src('src/plugin.cpp'), re.M)
    assert 'killcount_native_audit.py' in src('CMakeLists.txt')


@test
def edf41_kill_counters_save_slots() -> None:
    """TROPHY.DAT keeps the counters' values by position in the store's order (the names' UTF-16 order; 200 slots;
    docs/kill-counter-re.md): every port counter sorts after every name an Achievement.sgo may hold (upper-case
    initials), so the stock variables keep their slots and a save read without them loses nothing of EDF6's;
    add_variables appends them once, refuses a variable that would sort after them and a table over 200. With the
    game: the real Achievement.sgo and text tables as the installer writes them (stock rows untouched)."""
    import edf41kills as kills
    import sgo
    assert all(k[0].islower() for k in kills.KEYS) and len(set(kills.KEYS)) == len(kills.KEYS)
    stockish = [['FrameCount', 'int', 0], ['WingDiverPlayCount', 'int', 0], ['ZzzKillCount', 'int', 0]]
    members = {'AchievementEvent': [], 'AchievementVariable': _rows_copy(stockish)}
    kills.add_variables(members)
    rows = members['AchievementVariable']
    assert rows[:3] == stockish and [r[0] for r in rows[3:]] == list(kills.KEYS)
    assert all(r[1:] == ['int', 0] for r in rows[3:])
    kills.add_variables(members)
    assert len(members['AchievementVariable']) == 3 + len(kills.KEYS), 'added twice'
    assert sorted(r[0] for r in rows)[:3] == sorted(r[0] for r in stockish), 'a stock slot moved'
    for bad in ([['edf5_KillCount', 'int', 0]], [[f'V{i}', 'int', 0] for i in range(kills.SLOTS - len(kills.KEYS) + 1)]):
        try:
            kills.add_variables({'AchievementVariable': _rows_copy(bad)})
        except ValueError:
            continue
        raise AssertionError(f'accepted {bad[:2]}...')
    for c in kills.COUNTERS:
        assert set(c.text) == set(kills.LANGS) and all(c.text.values()), c.key
        assert kills.label(c, 'EN').endswith(' (4.1):') and kills.label(c, 'SC').endswith('（4.1）')
    # another mod's table that cannot take them: left as it is (the packs install all the same), said in the notes
    foreign = sgo.write_depth_first(258, {'AchievementEvent': [], 'AchievementVariable': [['zzz', 'int', 0]]})
    plan: dict = {}
    assert e5c.achievement_or_base(foreign, plan) == foreign and len(plan['notes']) == 1
    assert e5c.achievement_or_base(b'not an sgo', plan) == b'not an sgo' and len(plan['notes']) == 2
    assert '击杀计数' in e5c.summary(({}, e5c.Contents(0, 68), plan))
    if not _e5c_have_game():   # the real data: a developer's machine (CI has no game)
        return
    import rootcpk
    game = rootcpk.default()
    data = game.read('ETC', 'ACHIEVEMENT.SGO')
    stock = sgo.plain(sgo.read(data)[1])
    out = sgo.plain(sgo.read(e5c.achievement_table(data))[1])
    n = len(stock['AchievementVariable'])
    assert out['AchievementEvent'] == stock['AchievementEvent']
    assert out['AchievementVariable'][:n] == stock['AchievementVariable']
    names = [r[0] for r in out['AchievementVariable']]
    assert len(names) <= kills.SLOTS and sorted(names)[:n] == sorted(r[0] for r in stock['AchievementVariable'])
    for lang, rel in e5c.TEXTS.items():
        raw = game.read(*rel.split('/'))
        base = sgo.read(raw)[1]
        text = sgo.read(e5c.text_table(raw, lang, ()))[1]
        assert {k: v for k, v in text.items() if k in base} == base, (rel, 'stock text changed')
        assert all(k not in base for k in kills.text_keys()), (rel, 'EDF6 already has a 4.1 counter text')
        for c in kills.COUNTERS:
            assert text[kills.TEXT_PREFIX + c.key] == kills.label(c, lang), (rel, c.key)


def _rows_copy(rows: list) -> list:
    return [list(r) for r in rows]


@test
def plugin_crash_dump_is_collected() -> None:
    """docs/feedback-2026-10-18-crash-dump.md: reports #19 / #20 of a crash came with no dump. The plugin now writes
    its own (src/crash_dump.cpp) as <exe>.<pid>.plugin.dmp in %LOCALAPPDATA%'s CrashDumps; collect() finds it by the
    'EDF6*.dmp' glob and the .plugin.dmp ending, and reads the plugin's log lines to say why there is none. Both sides
    checked against each other here, so a renamed file or log line cannot silently stop the dump from being sent."""
    import fnmatch
    import inspect
    import testhub
    with open(os.path.join(ROOT, 'src', 'crash_dump.cpp'), encoding='utf-8') as f:
        cpp = f.read()
    fmt = re.search(r'swprintf_s\(dumpPath,L"([^"]+)"', cpp)
    assert fmt, 'crash_dump.cpp: the dump path format'
    # The C++ literal "%ls\\%ls.%lu.plugin.dmp": folder, escaped backslash, exe name, pid.
    name = fmt.group(1).replace(r'%ls\\', '', 1).replace('%ls', 'EDF6.exe', 1).replace('%lu', '12345', 1)
    assert name == 'EDF6.exe.12345.plugin.dmp', name
    assert fnmatch.fnmatch(name, 'EDF6*.dmp') and name.lower().endswith('.plugin.dmp'), name
    assert 'GetEnvironmentVariableW(L"LOCALAPPDATA"' in cpp and r'L"\\CrashDumps"' in cpp, 'the dump goes to CrashDumps'
    src = inspect.getsource(testhub.plugin_crash_note)
    for line in ('CRASH dump written', 'CRASHDUMP installed'):
        assert line in cpp and line in src, line
    assert 'line.Text("CRASH ").Text(request.kind).Text(": exception ")' in cpp and 'CRASH ' in src, 'the crash line'
    assert 'CrashLog(line.text,line.size);' in cpp, 'crash diagnostics bypass the ordinary logger locks'
    with tempfile.TemporaryDirectory() as scratch:
        plugins = os.path.join(scratch, 'Mods', 'Plugins')
        os.makedirs(plugins)
        path = os.path.join(plugins, 'EDF6VehicleCrew.log')
        for status in ('CRASH dump written: EDF6.exe.12345.plugin.dmp', 'CRASH dump failed: MiniDumpWriteDump error 5'):
            with open(path, 'w', encoding='utf-8') as stream:
                stream.write('[12:00:00.000] EDF6VehicleCrew 0.9.0 loading\nCRASH unhandled exception: exception C0000005 at EDF.dll+0x4C3A30\n' + status + '\n')
            note = testhub.plugin_crash_note(scratch)
            assert status in note, (status, note)


@test
def edf41_ui_names() -> None:
    """4.1's UI names EDF6 registers under another name (pylib/bvm41.py UI_NAMES) never reach a committed 4.1 script
    (no game): edf6_strings maps them after the object rewrite and leaves everything else; no BVM of the story or the DLC
    packs pushes a 4.1 name; the pre-mission scenes that fade out at their end (M015, M016, M190, M192) create EDF6's fade
    screen, which src/edf41before.cpp's scenes then show (tests/edf41_before_native_audit.py: the name is EDF.dll's)."""
    import bvm41
    import edf41dlc
    text = bvm41.edf6_strings(edf41dlc.rewrite)
    assert text('ui_fade_screen_test') == 'ui_fade_screen_simple'
    assert text('ui_fade_screen_simple') == 'ui_fade_screen_simple' and text('Jingle_MissionCleared') == 'Jingle_MissionCleared'
    assert text('app:/Object/DragonSmall401.sgo') == edf41dlc.rewrite('app:/Object/DragonSmall401.sgo')
    assert bvm41.edf6_strings()('ui_fade_screen_test') == 'ui_fade_screen_simple'
    fades: dict[str, int] = {}
    seen = 0
    for base, _dirs, files in os.walk(os.path.join(ROOT, 'edf41port')):
        for name in files:
            if not name.upper().endswith('.BVM'):
                continue
            with open(os.path.join(base, name), 'rb') as f:
                prog = bvm41.parse(f.read())
            seen += 1
            pushed = [x.v for code in [prog.init] + [g.code for g in prog.funcs] for x in code if x.op == bvm41.OP_PUSHS]
            assert not set(pushed) & set(bvm41.UI_NAMES), (base, name)
            if name.upper() == 'BEFORE.BVM':
                main_code = next(g.code for g in prog.funcs if g.name == 'Main')
                fades[os.path.basename(base)] = sum(1 for x in main_code if x.op == bvm41.OP_PUSHS
                                                    and x.v == 'ui_fade_screen_simple')
    assert seen > 89, seen
    assert {k for k, n in fades.items() if n} == {'M015', 'M016', 'M190', 'M192'}, fades


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
