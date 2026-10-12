"""The vehicles' cockpits for the first person (the user, 2026-10-13: every seat its own view, every vehicle a real
cockpit, its information on the panels; src/fp_vehicle.cpp, src/firstperson.cpp).

A seat's cockpit is a kit (pylib/cockpit_kits.py: meshes and their textures, in the eye's frame: +x left, +y up, +z
ahead, the eye at the origin) fitted to the vehicle's model (FITS: the seat, the bone it rides, where the eye is at
rest, the kit's heading). graft() puts each fitted kit's meshes into the model, skinned rigidly to that bone (a
gunner's turret, a heli's body: the cockpit moves with it), and a transform-only marker bone `vc_eye_<seat>` at the
eye under the same bone: the plugin puts the camera there (fp_vehicle.cpp). The model is the one place that holds
where a seat's eye and cockpit are.

The stock vehicles get theirs through vehicle_optics.py (the audited model, redirected SGOs, its journal and undo);
the plugin's own vehicles in their generators. The kits' materials are copies of the Blacker hull's (v505_tank: the
shader the drill tank's imported meshes use) with the kit's own albedo, a flat normal and a neutral RMO.

The kits' textures and imported parts live in the model directory (obj_model.model_roots(): `cockpits/`), not in git:
without it no cockpit is built and the vehicles keep the stock camera (CockpitAssetsMissing names what is missing).
"""
from __future__ import annotations

import math
import os
from dataclasses import dataclass, replace
from typing import Callable

import graft_pure as g
import obj_model as om
import texfile
from mdb import Mdb, Rab, cmpl_compress, mdb_read, mdb_write, rab_read, rab_write

Vec3 = tuple[float, float, float]

TEMPLATE_ARC, TEMPLATE_MDB, TEMPLATE_MATERIAL = 'V505_TANK.MRAB', 'v505_tank.mdb', 'v505_tank'
FLAT_NORMAL, NEUTRAL_RMO = (128, 128, 255), (150, 110, 255)   # drill_model.py's: tangent-space up; roughness 0.6
NORMAL_TEX, RMO_TEX = 'edf6vc_cpit_n.dds', 'edf6vc_cpit_rmo.dds'
ASSET_DIR = 'cockpits'          # under a model root: textures/, parts/<asset>/, CREDITS.txt
MARKER = 'vc_eye_{}'


class CockpitAssetsMissing(Exception):
    """The model directory's cockpit assets are not there: no cockpit is built (the stock camera stays)."""


@dataclass(frozen=True)
class Fit:
    """A seat's cockpit in a vehicle model: the kit `kit` with its eye at `eye` (model space at rest), turned `yaw`
    degrees toward +x (the vehicle's left) and pitched `pitch` up, scaled `scale`, riding the bone `bone`."""
    seat: int
    bone: str
    eye: Vec3
    kit: str
    yaw: float = 0.0
    pitch: float = 0.0
    scale: float = 1.0


@dataclass(frozen=True)
class KitMaterial:
    """A kit material's albedo: an image under `cockpits/` (PNG / JPEG / DDS), or one colour."""
    albedo: str | tuple[int, int, int]


SCREEN_KINDS = ('flight', 'weapons', 'status', 'hud', 'map', 'sensor')
SCREEN = 'vc_scr_{}_{}_{}x{}'   # seat, kind, width and height in mm: the plugin draws that kind of live read-out there


@dataclass(frozen=True)
class Screen:
    """A face of a kit where the plugin draws live information (src/hud.cpp; the user: "信息全上面板"): `kind` one of
    SCREEN_KINDS (flight: speed, height, heading, attitude; weapons: arms, rounds, reload; status: armour, fuel,
    warnings; hud: a head-up glass, its symbols collimated on the world; map; sensor: the whole view is a remote
    weapon station's monitor, the eye at the gun's camera), a `width` x `height` face at `centre`
    whose front looks along `facing` (toward the eye), `up` its top, all in the eye's frame. The kit's own part under
    it should be a dark screen texture (textures/screen.png)."""
    kind: str
    centre: Vec3
    facing: Vec3
    width: float
    height: float
    up: Vec3 = (0.0, 1.0, 0.0)


@dataclass
class Kit:
    """A cockpit: parts in the eye's frame (obj_model Parts whose `material` names a key of `materials`). `outside`:
    the names of the parts meant to be seen from outside the vehicle too (a gun shield, a canopy frame); every other
    part must lie inside the hull (fitted()). `screens`: where its live read-outs are drawn."""
    parts: list[om.Part]
    materials: dict[str, KitMaterial]
    outside: frozenset[str] = frozenset()
    screens: tuple[Screen, ...] = ()


KitFn = Callable[[dict[str, float]], Kit]   # the room round the eye (clearance) -> the kit laid out in it


def assets_present() -> bool:
    try:
        asset_root()
        return True
    except CockpitAssetsMissing:
        return False


def model_digest(game, stem: str) -> str:  # noqa: ANN001 - rootcpk.Game
    """The sha-256 of `stem`.mdb in `stem`.MRAB (a loose Mods copy first, as vehicle_optics.build_model reads it): the
    geometry a fit was measured on (pylib/cockpit_fits.py DIGESTS)."""
    import hashlib
    from pathlib import Path
    name = stem + '.MRAB'
    loose = Path(game.root, 'Mods', 'OBJECT', name)
    arc = rab_read(loose.read_bytes() if loose.is_file() else game.read('OBJECT', name))
    return hashlib.sha256(next(f for f in arc.files if f.name.lower() == stem.lower() + '.mdb').data).hexdigest()


def asset_root() -> str:
    """The cockpits' asset directory (the first model root that has one)."""
    for root in om.model_roots():
        path = os.path.join(root, ASSET_DIR)
        if os.path.isdir(path):
            return path
    raise CockpitAssetsMissing(f'no {ASSET_DIR}/ in the model directories {om.model_roots()}')


def frame(fit: Fit) -> list[float]:
    """The eye's model-space matrix (rows: +x left, +y up, +z ahead, the eye)."""
    y, p = math.radians(fit.yaw), math.radians(fit.pitch)
    left = (math.cos(y), 0.0, -math.sin(y))
    ahead = (math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p))
    up = (ahead[1] * left[2] - ahead[2] * left[1], ahead[2] * left[0] - ahead[0] * left[2],
          ahead[0] * left[1] - ahead[1] * left[0])
    return [*left, 0.0, *up, 0.0, *ahead, 0.0, *fit.eye, 1.0]


def to_model(p: Vec3, m: list[float], scale: float = 1.0) -> Vec3:
    """An eye-frame point into model space through the eye matrix `m` (scaled by `scale` about the eye)."""
    return tuple(scale * (p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c]) + m[12 + c] for c in range(3))  # type: ignore[return-value]


def place(part: om.Part, m: list[float], scale: float) -> om.Part:
    """`part` (eye frame) into model space through the eye matrix `m` (positions scaled by `scale`)."""
    def turn(n: Vec3) -> Vec3:
        return tuple(n[0] * m[c] + n[1] * m[4 + c] + n[2] * m[8 + c] for c in range(3))  # type: ignore[return-value]
    verts = [om.Vertex(to_model(v.pos, m, scale), turn(v.normal), v.uv) for v in part.verts]
    return om.Part(part.name, part.material, verts, list(part.tris))


def screen_marker(seat: int, scr: Screen) -> str:
    if scr.kind not in SCREEN_KINDS:
        raise ValueError(f'screen kind {scr.kind} not one of {SCREEN_KINDS}')
    return SCREEN.format(seat, scr.kind, round(scr.width * 1000), round(scr.height * 1000))


def screen_frame(scr: Screen, fit: Fit) -> list[float]:
    """A screen's model-space matrix: rows its right (as seen from its front), its up, its facing, its centre."""
    import cockpit_shapes
    right, up, facing = cockpit_shapes.basis(scr.facing, scr.up)
    m = frame(fit)

    def turn(d: Vec3) -> Vec3:
        return tuple(d[0] * m[c] + d[1] * m[4 + c] + d[2] * m[8 + c] for c in range(3))  # type: ignore[return-value]
    return [*turn(right), 0.0, *turn(up), 0.0, *turn(facing), 0.0, *to_model(scr.centre, m, fit.scale), 1.0]


def texture_name(kit: str, key: str) -> str:
    return f'edf6vc_cpit_{kit}_{key}.dds'.lower()


def albedo_dds(spec: KitMaterial, root: str) -> bytes:
    if isinstance(spec.albedo, tuple):
        return texfile.solid_dxt1(spec.albedo)
    return om.texture_dds(os.path.join(root, spec.albedo))


def template(game) -> tuple[Mdb, object]:  # noqa: ANN001 - rootcpk.Game
    """The Blacker hull's model and its skinned mesh in the v505_tank material (the kits' layout and shader)."""
    host = mdb_read(next(f for f in rab_read(game.read('OBJECT', TEMPLATE_ARC)).files
                         if f.name.lower() == TEMPLATE_MDB).data)
    mesh = next(me for o in host.objects for me in o.meshes
                if host.name_of(host.materials[me.material].name) == TEMPLATE_MATERIAL)
    return host, mesh


def graft(md: Mdb, fits: tuple[Fit, ...], kits: dict[str, KitFn], game,  # noqa: ANN001 - rootcpk.Game
          preorder: Callable[[Mdb], Mdb]) -> tuple[Mdb, dict[str, bytes]]:
    """`md` with each fit's marker bone and cockpit meshes, and the textures {file name: DDS} the archive must add
    (add_textures). Each kit is laid out in the room its eye has in `md` as it was (clearance against the vehicle's
    own surfaces, before any cockpit). `preorder` puts the skeleton in DFS order after the markers
    (vehicle_optics.preorder)."""
    if not fits:
        return md, {}
    if len({f.seat for f in fits}) != len(fits):
        raise ValueError('two cockpits for one seat')
    import vehicle_optics   # the marker bone is the optics' kind (late: vehicle_optics imports this module)
    root = asset_root()
    hull = hull_triangles(md)
    kit_of = {fit.seat: fitted(hull, fit, kits[fit.kit]) for fit in fits}
    for fit in fits:
        md = vehicle_optics.add_marker(md, fit.bone, MARKER.format(fit.seat), frame(fit))
        for scr in kit_of[fit.seat].screens:
            md = vehicle_optics.add_marker(md, fit.bone, screen_marker(fit.seat, scr), screen_frame(scr, fit))
    md = preorder(md)
    tmpl_md, tmpl_mesh = template(game)
    by_material: dict[tuple[str, str], list[tuple[om.Part, list]]] = {}
    materials: dict[tuple[str, str], KitMaterial] = {}
    for fit in fits:
        kit, bone, m = kit_of[fit.seat], md.bone_index(fit.bone), frame(fit)
        for part in kit.parts:
            if part.material not in kit.materials:
                raise ValueError(f'{fit.kit}: part {part.name} has no material {part.material}')
            placed = place(part, m, fit.scale)
            by_material.setdefault((fit.kit, part.material), []).append((placed, om.rigid(placed, bone)))
            materials[(fit.kit, part.material)] = kit.materials[part.material]
    textures = {NORMAL_TEX: texfile.solid_dxt1(FLAT_NORMAL), RMO_TEX: texfile.solid_dxt1(NEUTRAL_RMO)}
    meshes = []
    for (kit_name, key), pieces in sorted(by_material.items()):
        tex = texture_name(kit_name, key)
        if tex not in textures:
            textures[tex] = albedo_dds(materials[(kit_name, key)], root)
        md, mi = om.add_material(md, tmpl_md, TEMPLATE_MATERIAL, f'edf6vc_cpit_{kit_name}_{key}',
                                 {'albedo': tex, 'normal': NORMAL_TEX, 'param_r_m_occ_hr': RMO_TEX})
        meshes += om.build_meshes(tmpl_mesh, pieces, mi)
    host = md.objects[0]
    meshes = [replace(me, mesh_index=len(host.meshes) + k) for k, me in enumerate(meshes)]
    md = replace(md, objects=[replace(host, meshes=host.meshes + meshes)] + list(md.objects[1:]), buffer_order=None)
    return g.recompute_bounds(md), textures


def add_textures(rab: Rab, textures: dict[str, bytes]) -> None:
    for name, dds in sorted(textures.items()):
        om.add_texture(rab, name, dds)


# ------------------------------------------------------------------------------------------ the room round an eye
DIRECTIONS = {'left': (1.0, 0.0, 0.0), 'right': (-1.0, 0.0, 0.0), 'up': (0.0, 1.0, 0.0), 'down': (0.0, -1.0, 0.0),
              'ahead': (0.0, 0.0, 1.0), 'back': (0.0, 0.0, -1.0)}


def hull_triangles(md: Mdb):  # noqa: ANN201 - numpy (n, 3, 3)
    """Every skinned mesh's triangles in model space at rest (the vehicle's hull and parts as drawn standing still)."""
    import numpy as np
    tris = []
    for o in md.objects:
        for me in o.meshes:
            if not me.flags[1]:
                continue
            pos = np.array(g.mesh_positions(me), dtype=np.float64)
            idx = np.array(g.triangles(me), dtype=np.int64)
            if len(idx):
                tris.append(pos[idx])
    return np.concatenate(tris) if tris else np.zeros((0, 3, 3))


def clearance(tris, eye: Vec3, m: list[float] | None = None) -> dict[str, float]:  # noqa: ANN001 - numpy
    """How far the eye can see the model's surfaces in each of DIRECTIONS (in the eye's frame `m`: rows left, up,
    ahead; None: the model's axes), metres; inf where nothing is hit (an open side)."""
    import numpy as np
    o = np.array(eye, dtype=np.float64)
    v0, e1, e2 = tris[:, 0], tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    out = {}
    for name, d in DIRECTIONS.items():
        if m is not None:
            d = tuple(d[0] * m[c] + d[1] * m[4 + c] + d[2] * m[8 + c] for c in range(3))
        d = np.array(d, dtype=np.float64)
        p = np.cross(d, e2)
        det = np.einsum('ij,ij->i', e1, p)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        s = o - v0
        u = np.einsum('ij,ij->i', s, p) * inv
        q = np.cross(s, e1)
        v = (q @ d) * inv
        t = np.einsum('ij,ij->i', e2, q) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4)
        out[name] = float(t[hit].min()) if hit.any() else math.inf
    return out


RAYS = [(x, y, z) for x, y, z in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))] +        [(x * 0.57735, y * 0.57735, z * 0.57735) for x in (1, -1) for y in (1, -1) for z in (1, -1)]


def exposed(tris, points):  # noqa: ANN001, ANN201 - numpy in, a bool per point out
    """Which of `points` (model space) can be seen from outside the vehicle: a ray from the point along one of RAYS
    (the axes and the diagonals) meets no surface of the model. A point enclosed every way (inside the turret, behind
    the gun's cradle, under the deck) is hidden from any outside viewer."""
    import numpy as np
    pts, back = np.unique(np.round(np.asarray(points, dtype=np.float64), 4), axis=0, return_inverse=True)
    v0, e1, e2 = tris[:, 0], tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    exposed = np.zeros(len(pts), dtype=bool)   # noqa: F811 - the result, by unique point
    for d in RAYS:
        d = np.array(d, dtype=np.float64)
        pv = np.cross(d, e2)
        det = np.einsum('ij,ij->i', e1, pv)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        for k in np.flatnonzero(~exposed):
            s = pts[k] - v0
            u = np.einsum('ij,ij->i', s, pv) * inv
            q = np.cross(s, e1)
            v = (q @ d) * inv
            t = np.einsum('ij,ij->i', e2, q) * inv
            if not (ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4)).any():
                exposed[k] = True
    return exposed[back.reshape(-1)]


SHRINK, SHRINK_ROUNDS = 0.9, 8


def _sides(p: Vec3, room: dict[str, float]) -> list[str]:
    """The walls of `room` a kit point (eye frame) presses on: past NEAR of the room that way."""
    out = []
    for value, plus, minus in ((p[0], 'left', 'right'), (p[1], 'up', 'down'), (p[2], 'ahead', 'back')):
        side = plus if value > 0 else minus
        if math.isfinite(room[side]) and abs(value) > NEAR * room[side]:
            out.append(side)
    return out


NEAR = 0.7


def fitted(tris, fit: Fit, kit_fn: KitFn) -> Kit:  # noqa: ANN001 - numpy
    """The kit for `fit` laid out in its eye's room; while a vertex of its inside parts can be seen from outside
    (exposed), the walls it presses on are pulled in SHRINK (at most SHRINK_ROUNDS rounds): the room is measured
    straight left, up, ahead ...; a round turret's corners are nearer than that."""
    m = frame(fit)
    room = clearance(tris, fit.eye, m)
    for _ in range(SHRINK_ROUNDS + 1):
        kit = kit_fn(room)
        local = [v.pos for p in kit.parts if p.name not in kit.outside for v in p.verts]
        if not local:   # nothing inside the hull to keep in (a remote station's display)
            return kit
        bad = exposed(tris, [to_model(q, m, fit.scale) for q in local])
        if not bad.any():
            return kit
        press = {side for q, b in zip(local, bad) if b for side in _sides(q, room)}
        if not press:
            raise ValueError(f'seat {fit.seat} ({fit.kit}): parts near the eye show outside: the eye is not inside the hull')
        room = {k: v * SHRINK if k in press else v for k, v in room.items()}
    raise ValueError(f'seat {fit.seat} ({fit.kit}): the kit still crosses the hull after {SHRINK_ROUNDS} rounds')


def graft_files(game, files: dict[str, bytes]) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    """A generator's files (tools/make_<x>.py build(): {path under Mods: bytes}) with the cockpits of its vehicles'
    seats grafted into their models: each OBJECT/<STEM>.MRAB fitted in pylib/cockpit_fits.py gets its model member
    (<stem>.mdb, else its only .mdb) grafted. Without the cockpit assets the files are as they were (the seats keep
    the stock camera), with a message."""
    import cockpit_fits
    import cockpit_kits
    import vehicle_optics
    out = dict(files)
    for rel, data in files.items():
        stem = rel.rsplit('/', 1)[-1].rsplit('.', 1)[0].upper()
        fits = cockpit_fits.FITS.get(stem)
        if not rel.upper().endswith('.MRAB') or not fits:
            continue
        if not assets_present():
            print(f'{rel}: no cockpits (the model folder has no {ASSET_DIR}/)')
            continue
        rab = rab_read(data)
        mdbs = [f for f in rab.files if f.name.lower().endswith('.mdb')]
        member = next((f for f in mdbs if f.name.lower() == stem.lower() + '.mdb'), mdbs[0] if len(mdbs) == 1 else None)
        if member is None:
            raise ValueError(f'{rel}: which of its models {[f.name for f in mdbs]} has the seats?')
        md, textures = graft(mdb_read(member.data), fits, cockpit_kits.KITS, game, vehicle_optics.preorder)
        member.stored = cmpl_compress(mdb_write(md))
        add_textures(rab, textures)
        out[rel] = rab_write(rab)
    return out
