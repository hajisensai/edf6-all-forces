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
import time
from dataclasses import dataclass, replace
from typing import Callable

import graft_pure as g
import obj_model as om
import texfile
from mdb import Mdb, Mesh, Rab, cmpl_compress, mdb_read, mdb_write, rab_read, rab_write

Vec3 = tuple[float, float, float]

TEMPLATE_ARC, TEMPLATE_MDB, TEMPLATE_MATERIAL = 'V505_TANK.MRAB', 'v505_tank.mdb', 'v505_tank'
FLAT_NORMAL, NEUTRAL_RMO = (128, 128, 255), (190, 0, 255)   # matte nonmetal interior, no occlusion baked twice
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
    shell_ahead: float | None = None  # measured native cab nose beyond its first axial pane, metres from the eye


@dataclass(frozen=True)
class KitMaterial:
    """A kit material's albedo: an image under `cockpits/` (PNG / JPEG / DDS), or one colour."""
    albedo: str | tuple[int, int, int]
    emissive: bool = False


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


@dataclass(frozen=True)
class InstrumentControl:
    part: str
    control: str
    centre: Vec3
    facing: Vec3
    up: Vec3
    extent: tuple[float,float] = (1.0,1.0)


@dataclass
class Kit:
    """A cockpit: parts in the eye's frame (obj_model Parts whose `material` names a key of `materials`). `outside`:
    the names of the parts meant to be seen from outside the vehicle too (a gun shield, a canopy frame); every other
    part must lie inside the hull (fitted()). `screens`: where its live read-outs are drawn. `sill`: a closed tub's
    (an aircraft cockpit's) canopy sill, degrees of pitch from the eye: every line of sight further down than that
    meets the kit (leaks()); above it the canopy (the world, through the hull the game does not draw from inside).
    None: no such promise (an open gun mount, a cab whose windows reach the floor). `windows`: the real glass under
    the sill the eye may see out through (a helicopter's chin window; eye-frame triangles, cockpit_tub): the tub leaves
    those lines open and leaks() lets them through. `conform`: the names of parts (a tub) whose corners fitted() may pull
    toward the eye where they would show outside the hull: along its line of sight a corner covers the same view."""
    parts: list[om.Part]
    materials: dict[str, KitMaterial]
    outside: frozenset[str] = frozenset()
    screens: tuple[Screen, ...] = ()
    sill: float | None = None
    windows: tuple[tuple[Vec3, Vec3, Vec3], ...] = ()
    conform: frozenset[str] = frozenset()
    controls: tuple[InstrumentControl, ...] = ()  # physical instruments replacing source-model dial faces
    ranges: tuple[tuple[int,int], ...] = ()


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
    from texfile_accel import texture_dds
    return texture_dds(os.path.join(root, spec.albedo))


def template(game) -> tuple[Mdb, Mesh]:  # noqa: ANN001 - rootcpk.Game
    """The Blacker hull's model and its skinned mesh in the v505_tank material (the kits' layout and shader)."""
    host = mdb_read(next(f for f in rab_read(game.read('OBJECT', TEMPLATE_ARC)).files
                         if f.name.lower() == TEMPLATE_MDB).data)
    mesh = next(me for o in host.objects for me in o.meshes
                if host.name_of(host.materials[me.material].name) == TEMPLATE_MATERIAL)
    return host, mesh


def precise_template(mesh: Mesh) -> Mesh:
    """Vertex layout for newly built cockpit meshes: float32 positions, retaining all other native elements.

    The hull template uses half4 positions. At a cab's model-space offset its quantisation can exceed the 1-2 mm
    gap between a glyph and its display, bending their triangles through each other. Widen the declaration before
    packing new vertices; merely moving the screen cannot fix the glyphs fighting their own backing.
    """
    position = next(e for e in mesh.elems if e.name.lower()=='position')
    if position.fmt!=7: return mesh
    elems = [replace(e,fmt=1) if e is position else replace(e,offset=e.offset+8 if e.offset>position.offset else e.offset)
             for e in mesh.elems]
    return replace(mesh,vsize=mesh.vsize+8,elems=elems,vdata=b'',indices=b'')


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
    import cockpit_instruments
    import cockpit_shell
    root = asset_root()
    print('      座舱：提取车壳三角形……', flush=True)
    hull = hull_triangles(md)
    thumbnail = cockpit_instruments.silhouette(hull, 'front' if any(f.kit.startswith('mech_') for f in fits) else 'top')
    kit_of: dict[int, Kit] = {}
    controls: dict[int, dict[str, str]] = {}
    for fit in fits:
        started = time.perf_counter()
        print(f'      席位 {fit.seat}（{fit.kit}）：开始适配座舱……', flush=True)
        kit_of[fit.seat] = fitted(hull, fit, kits[fit.kit])
        kit = kit_of[fit.seat]
        controls[fit.seat] = {}
        shared = {}

        def bind(control: InstrumentControl, index: int) -> None:
            nonlocal md
            # Visibility-only geometry can share a bone even at different positions.
            # Needles and bars keep their own pivots; their transforms are not visibility-only.
            # Parts of one moving control about one pivot (an attitude card's sky, ground and lines) share it too.
            key = (control.control if control.control[0] in 'dDplcuveZ' else
                   (control.control, control.centre, control.facing, control.up, control.extent))
            name = shared.get(key)
            if name is None:
                name = f'vc_i_{fit.seat}_{index}_{control.control}'
                at = Screen('status', control.centre, control.facing, .01, .01, control.up)
                transform = screen_frame(at, fit)
                if control.control.startswith(('t_','T_','g_')):   # moved in units of the extent (g: screen heights)
                    transform[:3] = [v*control.extent[0]*fit.scale for v in transform[:3]]
                    transform[4:7] = [v*control.extent[1]*fit.scale for v in transform[4:7]]
                md = vehicle_optics.add_marker(md, fit.bone, name, transform)
                shared[key] = name
            controls[fit.seat][control.part] = name

        for index, scr in enumerate(kit.screens):
            if scr.kind in ('hud', 'sensor'):
                continue
            parts, moving = cockpit_instruments.instrument(scr, index, numbers=fit.seat == 0, thumbnail=thumbnail,
                                                           ranges=dict(kit.ranges))
            # Replace the kit's old display quad, rather than adding a second coincident dark face.
            kit.parts = [p for p in kit.parts if not (p.name==f'screen_{scr.kind}' and p.material=='screen')]
            kit.parts.extend(parts)
            kit.materials.update(cockpit_instruments.MATERIALS)
            for control in moving:
                bind(control,index)
        for index,control in enumerate(kit.controls,len(kit.screens)):
            bind(control,index)
        print(f'      席位 {fit.seat}：适配完成（{time.perf_counter()-started:.1f} 秒）', flush=True)
    md = cockpit_shell.separate(md, fits, hull)
    md = clear_cab_interior(md, fits)
    for fit in fits:
        md = vehicle_optics.add_marker(md, fit.bone, MARKER.format(fit.seat), frame(fit))
        seen=set()
        for index,scr in enumerate(kit_of[fit.seat].screens):
            marker=screen_marker(fit.seat,scr)
            if marker in seen:marker+=f'_{index}'  # mirrored displays may have the same kind and size
            seen.add(marker)
            md = vehicle_optics.add_marker(md, fit.bone, marker, screen_frame(scr, fit))
    print('      座舱：生成骨骼与网格……', flush=True)
    md = preorder(md)
    if len(md.bones) > 256:
        raise ValueError(f'cockpit needs {len(md.bones)} bones, beyond the MDB ubyte skin-index limit')
    tmpl_md, tmpl_mesh = template(game)
    tmpl_mesh = precise_template(tmpl_mesh)
    by_material: dict[tuple[str, str], list[tuple[om.Part, list]]] = {}
    materials: dict[tuple[str, str], KitMaterial] = {}
    for fit in fits:
        kit, bone, m = kit_of[fit.seat], md.bone_index(fit.bone), frame(fit)
        for part in kit.parts:
            if part.material not in kit.materials:
                raise ValueError(f'{fit.kit}: part {part.name} has no material {part.material}')
            placed = place(part, m, fit.scale)
            part_bone = md.bone_index(controls[fit.seat][part.name]) if part.name in controls[fit.seat] else bone
            by_material.setdefault((fit.kit, part.material), []).append((placed, om.rigid(placed, part_bone)))
            materials[(fit.kit, part.material)] = kit.materials[part.material]
    textures = {NORMAL_TEX: texfile.solid_dxt1(FLAT_NORMAL), RMO_TEX: texfile.solid_dxt1(NEUTRAL_RMO)}
    meshes = []
    for (kit_name, key), pieces in sorted(by_material.items()):
        tex = texture_name(kit_name, key)
        if tex not in textures:
            started = time.perf_counter()
            print(f'      贴图 {kit_name}/{key}：转换中……', flush=True)
            textures[tex] = albedo_dds(materials[(kit_name, key)], root)
            print(f'      贴图 {kit_name}/{key}：完成（{time.perf_counter()-started:.1f} 秒）', flush=True)
        spec = materials[(kit_name, key)]
        md, mi = om.add_material(md, tmpl_md, 'Light01' if spec.emissive else TEMPLATE_MATERIAL,
                                 f'edf6vc_cpit_{kit_name}_{key}',
                                 {'albedo': tex, 'normal': NORMAL_TEX,
                                  'param_r_m_occ_light' if spec.emissive else 'param_r_m_occ_hr': RMO_TEX})
        # Source albedos already contain their base colour. The tank template's 0.6 diffuse multiplier and metallic
        # mask made imported cockpits muddy; the display's painted glyphs also need the stock light shader.
        params = []
        for p in md.materials[mi].params:
            v = list(p.value)
            if p.name == 'diffuse': v[:3] = [1.0,1.0,1.0]
            elif p.name == 'light_color':
                # The native light shader adds this term independently of albedo.
                # White here washes every health state out to the same bloom.
                v[:3] = [(c/255.)**2.2 for c in spec.albedo] if isinstance(spec.albedo, tuple) else [1.,1.,1.]
            elif p.name == 'specular_color': v[:3] = [.04,.04,.04]
            elif p.name == 'metallic': v[0] = 0.0
            params.append(replace(p,value=v))
        md.materials[mi] = replace(md.materials[mi],params=params)
        meshes += om.build_meshes(tmpl_mesh, pieces, mi)
    host = md.objects[0]
    meshes = [replace(me, mesh_index=len(host.meshes) + k) for k, me in enumerate(meshes)]
    md = replace(md, objects=[replace(host, meshes=host.meshes + meshes)] + list(md.objects[1:]), buffer_order=None)
    return g.recompute_bounds(md), textures


def clear_cab_interior(md: Mdb, fits: tuple[Fit, ...]) -> Mdb:
    """Tame the two old trucks' noisy glazing; shell visibility preserves their native geometry."""
    replacements = [f for f in fits if f.kit in ('kei_cab', 'trailer_cab')]
    if not replacements:
        return md
    materials = []
    for mat in md.materials:
        if 'Alpha' not in mat.shader:
            materials.append(mat)
            continue
        params = []
        for p in mat.params:
            v = list(p.value)
            if p.name == 'normal_amount': v[0] = 0.0  # no full-screen Window_Normal_Noise distortion
            elif p.name == 'diffuse': v = [1.0,1.0,1.0,.08]
            elif p.name == 'specular_color': v[:3] = [.08,.08,.08]
            elif p.name == 'translucent_fall_off_offset': v[0] = .03
            params.append(replace(p,value=v))
        materials.append(replace(mat,params=params))
    return replace(md,materials=materials)


def add_textures(rab: Rab, textures: dict[str, bytes]) -> None:
    from texfile_accel import compress_texture
    for name, dds in sorted(textures.items()):
        om.add_texture(rab, name, dds, compressor=compress_texture)


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
    the gun's cradle, under the deck) is hidden from any outside viewer.

    Each ray is tested (Moller-Trumbore, as a ray meets a triangle) only against the triangles whose shadow on the
    plane across the ray covers the point's cell of a grid on that plane (_escapes): the same answer as testing every
    triangle, fast enough for a cockpit cut from a real model (tens of thousands of vertices)."""
    import numpy as np
    pts, back = np.unique(np.round(np.asarray(points, dtype=np.float64), 4), axis=0, return_inverse=True)
    tris = np.asarray(tris, dtype=np.float64).reshape(-1, 3, 3)
    out = np.zeros(len(pts), dtype=bool)   # by unique point
    for d in RAYS:
        rest = np.flatnonzero(~out)
        if not len(rest):
            break
        out[rest[_escapes(tris, pts[rest], d)]] = True
    return out[back.reshape(-1)]


GRID_CELLS, GRID_CHUNK = 128, 4_000_000   # cells a side of exposed()'s grid; (point, triangle) pairs tested at once


def _escapes(tris, pts, d):  # noqa: ANN001, ANN201 - numpy in, a bool per point out
    """For each of `pts` (n, 3): its ray along `d` meets no triangle of `tris` (m, 3, 3) a little way off (t > 1e-4).
    Only the triangles whose shadow's bounding box (on the plane across `d`, a little grown) covers the point's grid
    cell are tested: a triangle the ray meets has the point's shadow in its own, so no hit is missed."""
    import numpy as np
    d = np.asarray(d, dtype=np.float64)
    if not len(tris):
        return np.ones(len(pts), dtype=bool)
    a = np.cross(d, np.array([1.0, 0.0, 0.0]) if abs(d[0]) < 0.9 else np.array([0.0, 1.0, 0.0]))
    a /= np.linalg.norm(a)
    b = np.cross(d, a)
    tu, tv, pu, pv = tris @ a, tris @ b, pts @ a, pts @ b
    lo_u, lo_v = min(tu.min(), pu.min()), min(tv.min(), pv.min())
    span = max(tu.max() - lo_u, tv.max() - lo_v, pu.max() - lo_u, pv.max() - lo_v, 1e-6)
    h, eps, top = span / GRID_CELLS * (1 + 1e-9), 1e-7 * span + 1e-9, GRID_CELLS - 1

    def cell(x, lo):  # noqa: ANN001, ANN202
        return np.floor((x - lo) / h).astype(np.int64).clip(0, top)
    u0, u1 = cell(tu.min(1) - eps, lo_u), cell(tu.max(1) + eps, lo_u)
    v0, v1 = cell(tv.min(1) - eps, lo_v), cell(tv.max(1) + eps, lo_v)
    nu = u1 - u0 + 1
    count = nu * (v1 - v0 + 1)
    k = np.arange(count.sum()) - np.repeat(np.cumsum(count) - count, count)
    cells = (np.repeat(u0, count) + k % np.repeat(nu, count)) * GRID_CELLS + np.repeat(v0, count) + k // np.repeat(nu, count)
    order = np.argsort(cells, kind='stable')
    cells, tri_of = cells[order], np.repeat(np.arange(len(tris)), count)[order]
    pc = cell(pu, lo_u) * GRID_CELLS + cell(pv, lo_v)
    start = np.searchsorted(cells, pc, 'left')
    n = np.searchsorted(cells, pc, 'right') - start
    v0w, e1, e2 = tris[:, 0], tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    pvec = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, pvec)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    hit = np.zeros(len(pts), dtype=bool)
    idx = np.flatnonzero(n)
    ends = np.cumsum(n[idx])
    first = 0
    while first < len(idx):
        last = max(first + 1, int(np.searchsorted(ends, ends[first] - n[idx[first]] + GRID_CHUNK, 'right')))
        take = idx[first:last]
        first = last
        cnt = n[take]
        pi = np.repeat(take, cnt)
        ti = tri_of[np.repeat(start[take], cnt) + np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)]
        s = pts[pi] - v0w[ti]
        u = np.einsum('ij,ij->i', s, pvec[ti]) * inv[ti]
        q = np.cross(s, e1[ti])
        v = (q @ d) * inv[ti]
        t = np.einsum('ij,ij->i', e2[ti], q) * inv[ti]
        hit[pi[ok[ti] & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4)]] = True
    return ~hit


EDGE = 1e-6   # leaks(): how far past a face's edge (in its barycentric terms) a line still meets it
LEAK_STEP, SILL_MARGIN = 5.0, 5.0   # degrees between leaks()'s lines of sight; how far under the sill they start


def sight_lines(sill: float, step: float = LEAK_STEP) -> list[tuple[float, float]]:
    """(yaw left, pitch up) in degrees: every `step` round the eye, from straight down to SILL_MARGIN under `sill`."""
    top = sill - SILL_MARGIN
    pitches = [-90.0 + step * k for k in range(int((top + 90.0) // step) + 1)]
    yaws = [-180.0 + step * k for k in range(int(360 // step))]
    return [(yaw, p) for p in pitches for yaw in ([0.0] if p <= -90.0 else yaws)]


def leaks(kit: Kit, step: float = LEAK_STEP) -> list[tuple[float, float]]:
    """The lines of sight from the eye (sight_lines: yaw, pitch) under the kit's sill that meet no front face of the
    kit (the game draws only front faces, and none of the hull from inside): there the world shows through the floor
    or the walls of a cockpit meant to be closed (Kit.sill; the user, 2026-10-14: "固定翼的底部是空白，完全能看过去").
    A line out through the kit's real glass (Kit.windows: a chin window) is a view, not a leak. Empty for a kit
    without a sill."""
    import numpy as np
    if kit.sill is None:
        return []
    lines = sight_lines(kit.sill, step)
    tris = [np.asarray([v.pos for v in p.verts], dtype=np.float64)[np.asarray(p.tris, dtype=np.int64)]
            for p in kit.parts if p.tris]
    if not tris:
        return lines
    t3 = np.concatenate(tris)
    v0, e1, e2 = t3[:, 0], t3[:, 1] - t3[:, 0], t3[:, 2] - t3[:, 0]
    normal = np.cross(e1, e2)
    yaw, pitch = (np.radians([ln[k] for ln in lines]) for k in (0, 1))
    dirs = np.stack([np.sin(yaw) * np.cos(pitch), np.sin(pitch), np.cos(yaw) * np.cos(pitch)], 1)
    met = np.zeros(len(dirs), dtype=bool)
    chunk = max(1, 2_000_000 // len(t3))
    s = -v0                                   # the eye (the origin) from each triangle's first corner
    q = np.cross(s, e1)
    for a in range(0, len(dirs), chunk):
        d = dirs[a:a + chunk]                 # (n, 3) against (m, ...) -> (n, m)
        p = np.cross(d[:, None, :], e2[None, :, :])
        det = np.einsum('mj,nmj->nm', e1, p)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        u = np.einsum('mj,nmj->nm', s, p) * inv
        v = (d @ q.T) * inv
        t = (e2 * q).sum(1)[None, :] * inv
        front = (d @ normal.T) < 0
        e = EDGE   # a line along the edge two faces share meets them (rounding must not open a crack)
        met[a:a + chunk] = (ok & front & (u >= -e) & (v >= -e) & (u + v <= 1 + e) & (t > 1e-4)).any(1)
    if kit.windows:
        from cockpit_tub import through
        met |= through(dirs, kit.windows)
    return [ln for ln, m in zip(lines, met) if not m]


SHRINK, SHRINK_ROUNDS = 0.9, 8
CONFORM_ROUNDS = 12   # rounds of pulling a conforming part's exposed corners SHRINK toward the eye


def _conform(tris, kit: Kit, m: list[float], scale: float) -> None:  # noqa: ANN001 - numpy
    """Pulls the corners of the kit's conforming parts (Kit.conform: a tub laid on the room's box, whose corners
    stand outside a hull that narrows) that can be seen from outside toward the eye, SHRINK a round, until they are
    inside. From the eye nothing changes: a triangle covers the view its corners' directions span, at any distance."""
    parts = [p for p in kit.parts if p.name in kit.conform]
    for _ in range(CONFORM_ROUNDS):
        verts = [(p, i) for p in parts for i in range(len(p.verts))]
        if not verts:
            return
        bad = exposed(tris, [to_model(p.verts[i].pos, m, scale) for p, i in verts])
        if not bad.any():
            return
        for (p, i), b in zip(verts, bad):
            if b:
                v = p.verts[i]
                p.verts[i] = om.Vertex(tuple(c * SHRINK for c in v.pos), v.normal, v.uv)


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
    print(f'        席位 {fit.seat}：测量座舱空间……', flush=True)
    room = clearance(tris, fit.eye, m)
    for round_index in range(SHRINK_ROUNDS + 1):
        print(f'        席位 {fit.seat}：适配第 {round_index+1}/{SHRINK_ROUNDS+1} 轮，读取座舱部件……', flush=True)
        kit = kit_fn(room)
        if kit.conform:
            _conform(tris, kit, m, fit.scale)
        inside = [p for p in kit.parts if p.name not in kit.outside]
        local = [v.pos for p in inside for v in p.verts]
        if not local:   # nothing inside the hull to keep in (a remote station's display)
            return kit
        print(f'        席位 {fit.seat}：检查 {len(local)} 个顶点是否露出车壳……', flush=True)
        bad = exposed(tris, [to_model(q, m, fit.scale) for q in local])
        if not bad.any():
            return kit
        press = {side for q, b in zip(local, bad) if b for side in _sides(q, room)}
        if not press:   # what shows is not at a wall: a part placed apart from the room's walls (or the eye outside)
            owners, k = [], 0
            for p in inside:
                if bad[k:k + len(p.verts)].any():
                    owners.append(p.name)
                k += len(p.verts)
            raise ValueError(f'seat {fit.seat} ({fit.kit}): parts away from the walls show outside the hull: {owners}')
        room = {k: v * SHRINK if k in press else v for k, v in room.items()}
    raise ValueError(f'seat {fit.seat} ({fit.kit}): the kit still crosses the hull after {SHRINK_ROUNDS} rounds')


def _graft_one(root: str, rel: str, data: bytes) -> bytes:
    import rootcpk
    import cockpit_fits
    import cockpit_kits
    import vehicle_optics
    game = rootcpk.Game(root)
    stem = rel.rsplit('/', 1)[-1].rsplit('.', 1)[0].upper()
    fits = cockpit_fits.FITS[stem]
    rab = rab_read(data)
    mdbs = [f for f in rab.files if f.name.lower().endswith('.mdb')]
    member = next((f for f in mdbs if f.name.lower() == stem.lower() + '.mdb'), mdbs[0] if len(mdbs) == 1 else None)
    if member is None:
        raise ValueError(f'{rel}: which of its models {[f.name for f in mdbs]} has the seats?')
    md, textures = graft(mdb_read(member.data), fits, cockpit_kits.KITS, game, vehicle_optics.preorder)
    print(f'{rel}：压缩模型与打包贴图……', flush=True)
    member.stored = cmpl_compress(mdb_write(md))
    add_textures(rab, textures)
    return rab_write(rab)


def graft_files(game, files: dict[str, bytes]) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    """A generator's files (tools/make_<x>.py build(): {path under Mods: bytes}) with the cockpits of its vehicles'
    seats grafted into their models: each OBJECT/<STEM>.MRAB fitted in pylib/cockpit_fits.py gets its model member
    (<stem>.mdb, else its only .mdb) grafted. Without the cockpit assets the files are as they were (the seats keep
    the stock camera), with a message."""
    import cockpit_fits
    from model_workers import run_jobs
    out = dict(files)
    jobs = []
    paths = []
    for rel, data in files.items():
        stem = rel.rsplit('/', 1)[-1].rsplit('.', 1)[0].upper()
        fits = cockpit_fits.FITS.get(stem)
        if not rel.upper().endswith('.MRAB') or not fits:
            continue
        if not assets_present():
            print(f'{rel}: no cockpits (the model folder has no {ASSET_DIR}/)')
            continue
        paths.append(rel)
        jobs.append((_graft_one, (game.root, rel, data), rel))
    for rel, data in zip(paths, run_jobs(jobs)):
        out[rel] = data
    return out
