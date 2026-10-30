"""An unarmed car on the V512's chassis (Vehicle_Car: the kei truck's class, handling and physics, as the sedan's,
tools/make_sedan.py) from a car model folder (tools/prep_xmorph.py car: the unit's mesh, its wheels on the V512's
wheel bones): pylib/xm_units.py's 'car' family, built by tools/make_xmorph.py.

    load(kind) -> wt_tracked.Spec        the model folder (models/xm_<key>: model.json, model.npz, textures/)
    build(game, kind) -> {path: bytes}   its SGO, MRAB and SHKT under Mods, checked (check); no cockpit yet
    check(files, kind)

The V512 keeps its class, controllers, sounds and physical topology (bodies, joints, the chassis's tuned
massDistribution); what follows the model:
  wheels     the front pair (tireF) and the rear pair (tireB) are the model's front and rear axles: their bones moved to
             the wheel centres (graft_pure.set_bone_origin, rotations kept), each car_base_wheel's radius its tyre's;
             the front pair steers and the rear one drives, as on the V512. An axle between them is a visual leaf
             (tireM<k>: tracked_combat_model.add_visual_bones) drawn at its own radius (car_base_draw_wheel_info
             [[-1, bone], [radius]], the V603's idlers' rows) with no physics of its own.
  model      the folder's parts on their bones in the V512 body material's layout (the folder's textures:
             wt_tracked.material_textures); nothing of the kei truck's geometry is left
  collision  the ragdoll refitted to the moved bones (ragdoll_fit.fit), then each proxy's hulls mapped onto boxes of the
             model (collision: the body's lengths, the tyres' wheels; the doors, which the model has not, a
             millimetre inside the chassis, as the sedan's)
  locators   the driver's seat under the eye, the way out beside it, the chase camera over the roof behind it, the
             headlights at the front (the MAB points vehicle_riding_position's first seat and car_base_head_light name)
  seats      the driver's alone (the kei truck's four bed seats dropped), no weapon (vehicle_weapon_setting empty)
"""
from __future__ import annotations

import json
import os
import struct
from dataclasses import replace

import numpy as np

import graft_pure as g
import obj_model as om
import ragdoll_fit as rf
import sgo
import shkt_check as sc
import tracked_collision as tc
import tracked_combat_model as t
import vehicle_check
import vcobjects as vc
import wt_tracked as wt
import xm_units
from mdb import bind_world, cmpl_compress, mdb_read, mdb_write, rab_read, rab_write

FORMAT = 1                        # tools/prep_xmorph.py CAR_FORMAT
HOST = 'V512_KEITRUCK'
HOST_MDB = 'v512_keitruck.mdb'
HOST_SHKT = 'Ragdoll_v512_keiTruck.shkt'
TEMPLATE = 'KT01L'                # the V512's body material (its glass, win000B, is the other)
FRONT, REAR = 'tireF', 'tireB'    # the steered pair, the driven pair (car_base_wheel [10..12])
DOORS = ('door_l', 'door_r')
SEAT_DROP = 0.70                  # m: the driver's locator under the eye (make_sedan)
EXIT_OUT = 0.20                   # m: the way out beside the body
CAMERA_UP, CAMERA_BACK = 0.25, 3.3   # m: the chase camera over the roof, behind the tail (make_sedan's 4.4 m sedan's)
LAMP_FROM = 0.3                   # m: the headlights' slice of the nose
DOOR_SCALE = 1e-3                 # an inert door's hulls, of their size


class CarModelError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise CarModelError(message)


# ------------------------------------------------------------------------------------------ the model folder

def folder(kind: str) -> str | None:
    d = om.model_dir(xm_units.folder(kind))
    ok = d is not None and all(os.path.isfile(os.path.join(d, f)) for f in ('model.json', 'model.npz'))
    return d if ok else None


def load(kind: str) -> wt.Spec:
    d = folder(kind)
    _req(d is not None, f'{kind}: model folder {xm_units.folder(kind)} not found ({", ".join(om.model_roots())})')
    with open(os.path.join(d, 'model.json'), encoding='utf-8') as f:     # type: ignore[arg-type]
        info = json.load(f)
    _req(info.get('format') == FORMAT and info.get('kind') == kind, f'{d}: not a format-{FORMAT} {kind} car folder')
    with np.load(os.path.join(d, 'model.npz'), allow_pickle=False) as z:  # type: ignore[arg-type]
        arrays = {k: z[k] for k in z.files}
    return wt.Spec(d, info, arrays)  # type: ignore[arg-type]


def axles(spec: wt.Spec) -> list[str]:
    """The wheel bones without their side, front to rear (tireF, tireM1.., tireB)."""
    names = sorted({b[:-2] for b in spec.info['wheels']}, key=lambda n: -spec.info['wheels'][n + '_l']['centre'][2])
    _req(names[0] == FRONT and names[-1] == REAR and all(n.startswith('tireM') for n in names[1:-1]),
         f'the wheels are not front, middle and rear axles: {names}')
    return names


def visual_bones(spec: wt.Spec) -> tuple[t.VisualBone, ...]:
    return tuple(t.VisualBone(f'{a}_{s}', 'body', tuple(spec.info['wheels'][f'{a}_{s}']['centre']))
                 for a in axles(spec)[1:-1] for s in 'lr')


# ------------------------------------------------------------------------------------------ model

def stock(game):  # noqa: ANN001, ANN201 - rootcpk.Game -> (Mdb, Rab)
    rab = rab_read(game.read('OBJECT', HOST + '.MRAB'))
    md = mdb_read(t.member(rab, HOST_MDB).data)
    _req(len(md.objects) == 1, 'the V512 must have one skinned object')
    return md, rab


def skeleton(host, spec: wt.Spec):  # noqa: ANN001, ANN201 - Mdb
    """The V512's skeleton (no meshes, materials or textures) with its four wheel bones on the model's front and rear
    wheels, then the middle axles' visual leaves."""
    md = replace(host, objects=[replace(host.objects[0], meshes=[])], materials=[], textures=[], buffer_order=None)
    for a in (FRONT, REAR):
        for s in 'lr':
            md = g.set_bone_origin(md, md.bone_index(f'{a}_{s}'), tuple(spec.info['wheels'][f'{a}_{s}']['centre']))
    extra = visual_bones(spec)
    return t.add_visual_bones(md, extra) if extra else md


def pieces(md, spec: wt.Spec) -> dict[str, wt.Piece]:  # noqa: ANN001
    out = {}
    for k, p in enumerate(spec.info['parts']):
        pos, _n, _uv, tris = spec.part(k)
        out[p['name']] = wt.Piece(p['name'], pos.astype(np.float64), tris.astype(np.int64), md.bone_index(p['bone']),
                                  p['material'])
    return out


def model(game, kind: str):  # noqa: ANN001, ANN201 - (MRAB bytes, Mdb)
    host, rab = stock(game)
    spec = load(kind)
    md = skeleton(host, spec)
    rab.files = [f for f in rab.files if f.name.lower() == HOST_MDB]
    mat_index = []
    added: set[str] = set()
    for m in spec.info['materials']:
        tex = wt.material_textures(spec, {**m, 'template': TEMPLATE})
        md, mi = om.add_material(md, host, TEMPLATE, m['name'], {k: fn for k, (fn, _d) in tex.items()})
        mat_index.append(mi)
        for fn, dds in tex.values():
            if fn not in added:
                om.add_texture(rab, fn, dds)
                added.add(fn)
    template = next(me for me in host.objects[0].meshes if host.name_of(host.materials[me.material].name) == TEMPLATE)
    packed = []
    for k, p in enumerate(spec.info['parts']):
        pos, nrm, uv, tris = spec.part(k)
        rows, kept = wt.part_rows(template, pos, nrm, uv, tris.astype(np.int64), md.bone_index(p['bone']))
        packed.append((template, mat_index[p['material']], rows, kept))
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(wt.pack(packed))]
    md = g.recompute_bounds(replace(md, objects=[replace(md.objects[0], meshes=meshes)]))
    data = mdb_write(md)
    t.member(rab, HOST_MDB).stored = cmpl_compress(data)
    return rab_write(rab), md


# ------------------------------------------------------------------------------------------ collision

def collision(game, md, spec: wt.Spec, blob: bytes) -> tuple[bytes, bytes]:  # noqa: ANN001
    """(SHKT, the ragdoll binding blob): the V512's ragdoll on the moved bones (ragdoll_fit.fit), each proxy's compound
    of hulls mapped instance by instance onto boxes of the model (make_sedan.collision's way: an affine map a hull,
    its convex radius kept; sidecar_model._Shkt rebuilds planes, mass properties and the compound's tree):
      body     its hulls on as many lengths of the body's mesh (front to rear), each one's box from the
               axles' height up (the wheels carry it: the floor no lower than the lowest wheel centre)
      tyres    the wheel's box (its width x its diameter)
      doors    a millimetre at the chassis's middle (the model has none; as the sedan's)
    The chassis keeps the kei truck's tuned massDistribution (centre of mass, inertia: its handling), as the sedan's."""
    import sidecar_model
    host, _rab = stock(game)
    raw, blob = rf.fit(md, game.read('OBJECT', HOST_SHKT), blob, host)
    wheels = spec.info['wheels']
    floor = min(w['centre'][1] for w in wheels.values())
    pts = np.vstack([spec.part(k)[0] for k, p in enumerate(spec.info['parts']) if p['bone'] == 'body']).astype(float)
    pts = pts[pts[:, 1] >= floor]
    reader = sc.Shkt(raw)
    frames = {b.name.removeprefix('RagDollProxys.'): b for b in reader.systems[0].bodies}
    _req(set(frames) == {'body', *DOORS, *(f'{a}_{s}' for a in (FRONT, REAR) for s in 'lr')},
         f'the V512 ragdoll changed: {sorted(frames)}')

    def fit(data: bytes, name: str, boxes: list[np.ndarray]) -> bytes:
        s = sidecar_model._Shkt(data, 'RagDollProxys.' + name)
        _req(s.inst_n == len(boxes) and all(s.identity(k) for k in range(s.inst_n)), f'{name}: its hulls changed')
        b = frames[name]
        for k, box in enumerate(boxes):
            r = s.radius(k)
            old = np.asarray(s.vertices(k))
            lo, hi = old.min(0), old.max(0)
            local = tc._world_target_to_proxy(box, b.position, b.orientation)   # the box in the proxy's own frame
            want_lo, want_hi = local[0] + r, local[1] - r
            scale = (want_hi - want_lo) / (hi - lo)
            _req(bool((scale > 0).all()), f'{name}: hull {k} would turn inside out')
            s.replace(k, np.diag(scale).tolist(), (want_lo - lo * scale).tolist(), radius=r)
        s.refresh()
        return s.bytes()

    n = sidecar_model._Shkt(raw, 'RagDollProxys.body').inst_n
    cuts = np.linspace(pts[:, 2].min(), pts[:, 2].max(), n + 1)
    slabs = []
    for z0, z1 in zip(cuts[:-1], cuts[1:]):
        p = pts[(pts[:, 2] >= z0) & (pts[:, 2] <= z1)]
        _req(len(p) > 0, f'no body between z {z0:.2f} and {z1:.2f}')
        lo, hi = p.min(0), p.max(0)
        slabs.append(np.asarray(((lo[0], lo[1], z0), (hi[0], hi[1], z1))))
    raw = fit(raw, 'body', slabs)
    for name in (f'{a}_{s}' for a in (FRONT, REAR) for s in 'lr'):
        c, r, h = np.asarray(wheels[name]['centre']), wheels[name]['radius'], wheels[name]['half']
        raw = fit(raw, name, [np.asarray((c - (h, r, r), c + (h, r, r)))])
    # each door hull shrunk to a millimetre-scale copy (its vertices and radius x DOOR_SCALE) at the chassis's middle,
    # in the door's frame (make_sedan.collision: no instance scale, the game keeps a scaled hull's radius)
    middle = np.asarray((0.0, floor, (pts[:, 2].min() + pts[:, 2].max()) / 2))
    for d in DOORS:
        s = sidecar_model._Shkt(raw, 'RagDollProxys.' + d)
        point = tc._world_target_to_proxy(np.asarray((middle, middle)), frames[d].position, frames[d].orientation)[0]
        for k in range(s.inst_n):
            s.replace(k, (np.eye(3) * DOOR_SCALE).tolist(), point.tolist())
        s.refresh()
        raw = s.bytes()
    return raw, blob


# ------------------------------------------------------------------------------------------ the vehicle

def _locate(mab: bytearray, name: str, point) -> None:  # noqa: ANN001
    at, _radius = vc.mab_locator(bytes(mab), name)
    struct.pack_into('<3f', mab, at, *(float(c) for c in point))


def out_names(kind: str) -> tuple[str, str, str]:
    """(its SGO, MRAB, SHKT stems under OBJECT/)."""
    stem = vc.GROUND_VEHICLES[kind].sgo
    return stem, stem, stem


def build(game, kind: str) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    unit = xm_units.UNITS[kind]
    spec = load(kind)
    arc, md = model(game, kind)
    version, m = sgo.read(game.read('OBJECT', HOST + '.SGO'))
    _req(m['xgs_scene_object_class'] == 'Vehicle_Car', 'the V512 is not a Vehicle_Car')
    stem, _arc, _shkt = out_names(kind)
    m['animation_model'][0] = [f'app:/object/{stem.lower()}.mrab', HOST_MDB]
    world = bind_world(md)
    body_y = world[md.bone_index('body')][13]
    lo, hi = (np.asarray(v) for v in spec.info['bounds'])
    pts = np.vstack([spec.part(k)[0] for k in range(len(spec.info['parts']))])
    nose = pts[pts[:, 2] >= hi[2] - LAMP_FROM]
    eye = np.asarray(unit.eye, dtype=float)
    seat = m['vehicle_riding_position'][0]
    mab = bytearray(m['animation_model'][2])
    for name, point in ((seat[1], (eye[0], eye[1] - body_y - SEAT_DROP, eye[2])),
                        (seat[2][0], (0.0, hi[1] + CAMERA_UP - body_y, lo[2] - CAMERA_BACK)),
                        (seat[2][0] + '注目', (0.0, 0.69 * hi[1] - body_y, hi[2] / 4)),
                        (seat[0], (-(max(-lo[0], hi[0]) + EXIT_OUT), -0.55, eye[2]))):
        _locate(mab, name, point)
    lamp_y = float(nose[:, 1].min() + 0.25 * (nose[:, 1].max() - nose[:, 1].min()))
    for k, light in enumerate(m['car_base_head_light']):
        _locate(mab, light[0], ((1 if k == 0 else -1) * 0.6 * float(nose[:, 0].max()), lamp_y - body_y, hi[2] - 0.1))
    m['animation_model'][2] = bytes(mab)
    m['vehicle_riding_position'] = m['vehicle_riding_position'][:1]
    m['vehicle_weapon_setting'] = []
    m['vehicle_setup'][-1] = []
    m['game_object_durability'] = unit.durability
    # the wheels: the front and rear pairs' radii, the middle axles drawn at their own
    wheels = spec.info['wheels']
    for row in m['car_base_wheel']:
        row[8] = sgo.Float(struct.pack('<f', wheels[row[0]]['radius']))
    for a in axles(spec)[1:-1]:
        for s in 'lr':
            m['car_base_draw_wheel_info'].append([[-1, f'{a}_{s}'], [sgo.Float(struct.pack('<f', wheels[f'{a}_{s}']['radius']))]])
    shkt, blob = collision(game, md, spec, m['ragdoll'][1])
    m['ragdoll'] = [f'app:/object/{stem.lower()}.shkt', blob]
    files = {f'OBJECT/{stem}.MRAB': arc, f'OBJECT/{stem}.SHKT': shkt, f'OBJECT/{stem}.SGO': sgo.write(version, m)}
    check(files, kind)
    return files


def check(files: dict[str, bytes], kind: str) -> None:
    """The SHKT valid and its ragdoll on the model's bones; the car_base_* settings sound against them
    (pylib/vehicle_check.py); every wheel bone where its wheel is; unarmed, one seat."""
    stem, _a, _s = out_names(kind)
    raw, arc_bytes = files[f'OBJECT/{stem}.SHKT'], files[f'OBJECT/{stem}.MRAB']
    doc = sc.load_plain(files[f'OBJECT/{stem}.SGO'])
    _req(doc is not None, 'the SGO does not parse')
    bad = [str(x) for x in sc.problems(raw) if x.severity == 'error']
    _req(not bad, 'SHKT: ' + '; '.join(bad))
    rab = rab_read(arc_bytes)
    md = mdb_read(t.member(rab, HOST_MDB).data)
    bad = rf.problems(md, raw, doc['ragdoll'][1])
    _req(not bad, 'ragdoll binding: ' + '; '.join(bad))
    world = bind_world(md)
    pos = {md.name_of(b.name): tuple(world[b.index][12:15]) for b in md.bones}
    bad = [str(x) for x in vehicle_check.vehicle_problems(doc, raw, pos.get, stem) if x.severity == 'error']
    _req(not bad, 'vehicle settings: ' + '; '.join(bad))
    spec = load(kind)
    for name, w in spec.info['wheels'].items():
        _req(name in pos and np.allclose(pos[name], w['centre'], atol=1e-4), f'{name}: not at its wheel')
    _req(doc['vehicle_weapon_setting'] == [] and doc['vehicle_setup'][-1] == [] and
         len(doc['vehicle_riding_position']) == 1, 'a car is unarmed, its driver alone')
