"""EDF6VC_ARTILLERY.MRAB: the user's twin-gun tank model (twin_tank.obj, pylib/obj_model.py) on the KG6 Kepler's
skeleton, so the stock Vehicle603_Flak class and V603_FLAK.SGO drive it (turret, two guns, recoil, tracks, wheels).

    model_dir() -> str | None        the folder holding twin_tank.obj (obj_model.model_dir('twin_tank'))
    build(game, folder) -> bytes     the finished archive (game: rootcpk.Game, read only)
    check(arc)  -> None              raises ArtilleryCheckError on any self-check failure

    python pylib/artillery_model.py OUTDIR   build from Root.cpk (read only) into OUTDIR: the archive, artillery.obj
                                             (the bind pose, one object per bone, for any viewer), and a report
                                             (repairs, barrels, bores, gun clearance, each bone's vertex box)

The OBJ is a kitbash of EDF6 parts, already in game space (+Y up, +Z forward, +X the vehicle's left): every vertex of
its hull, tracks and lights coincides with a vertex of the E551 (OBJECT/V505_TANK.MRAB, checked: a converted vertex
with no E551 vertex within 1 mm is an error). So those parts take, from the E551:
  - their skin: each vertex the E551 bones of the E551 vertex there, renamed to the Kepler's (E551_TO_HOST: the
    E551 has 7 road wheels and 2 end wheels a side, the Kepler 6 + 2: its seventh road wheel (and that track station)
    rides on the hull / track body, its sprocket on the Kepler's tire_moveH);
  - the repair of what the OBJ lost: every E551 triangle of the same material whose corners are all OBJ points, that
    the OBJ lacks and that borders one of its open holes is put back (the hull lost 5 triangles: see HULL_FILLS);
  - their normal / roughness-metal-occlusion maps (same UVs), with the user's albedo textures.
The turret (object fortressrobo_bottom: the turret body and two barrels as three separate meshes, a Balam part with
its own texture) has no stock counterpart: a flat normal map and a plain parameter map. Its body is skinned to
cannon_main; each barrel is cut where its thick breech sleeve steps down to the tube: the sleeve on cannon_l / _r
(elevation), the tube on cannon_slide_l / _r (recoil, it slides back into the sleeve). The barrel at +x is the left
gun. The barrels sit in two open slots in the turret's front: each elevates about the point where its axis leaves
the turret (the slot's back wall), the 0.25 m of sleeve behind it swinging inside the turret.

Bones (rotations stock, binds and inverse binds moved together, children's world binds kept): cannon_main on the
E551's turret axis (the hull is the E551's); cannon_l / _r on each barrel's axis where it leaves the turret's front
face (the trunnion: the sleeve behind it swings inside the turret); cannon_slide_l / _r on the axis in the muzzle's
mouth plane (where the shells come out); the wheel and track bones on the E551's wheel stations.
"""
from __future__ import annotations

import hashlib
import math
import os
import struct
from dataclasses import dataclass, replace

import graft_pure as g
import obj_model as om
import texfile
from graft_pure import Vec3
from mdb import Mdb, Rab, RabFile, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, mmul, rab_read, \
    rab_write, read_elem

HOST_ARC, HOST_MDB = 'V603_FLAK.MRAB', 'v603_flak.mdb'
REF_ARC, REF_MDB = 'V505_TANK.MRAB', 'v505_tank.mdb'
MODEL = 'twin_tank'                    # model folder name and OBJ file stem
OBJ_FILE = 'twin_tank.obj'

# OBJ object names (prefixes): the turret, and the E551 parts (hull, lights, tracks)
TURRET_OBJECT = 'fortressrobo_bottom'
E551_OBJECTS = ('v505_tank', 'Caterpi')
# E551 material -> the new material made from it (its shader, parameters, normal / parameter maps; the albedo is the
# user's texture of the OBJ triangles that coincide with that material's). The tracks keep the Kepler's names:
# V603_FLAK.SGO tank_caterpillar_animation scrolls 'v603_kyata_L' / '_R'.
HULL_MATERIALS = {'v505_tank': 'v505_tank', 'Light01': 'Light01', 'Light02': 'Light02', 'Caterpi_l': 'v603_kyata_L',
                  'Caterpi_r': 'v603_kyata_R'}
TURRET_MATERIAL = ('fortressx_02', 'v505_tank')       # (name, E551 template material) of the turret's material
TEXTURE_FILES = {'v505_tank.001': 'npc_tank.png'}     # OBJ material -> its file (the MTL names it NPC坦克.png)
FLAT_NORMAL = ('edf6vc_flat_nor.dds', (128, 128, 255))      # x, y = 0: the shader's normal is the surface's
PLAIN_RMO = ('edf6vc_plain_rmo.dds', (140, 90, 200))        # ~ the E551 / Kepler body maps' mean (roughness, metal, occ)
# E551 bone -> Kepler bone (by name). cannon_main: a plate of the E551's turret floor the OBJ hull still has (under the
# new turret, x +-0.29, y 1.309), turning with the turret as in the E551.
E551_TO_HOST = {'body': 'body', 'cannon_main': 'cannon_main', 'mudguard_l': 'body', 'mudguard_r': 'body',
                'catapi_body': 'catapi_body', 'catapiH_l': 'catapi_body', 'catapiH_r': 'catapi_body',
                'tire_moveH_l': 'body', 'tire_moveH_r': 'body', 'tire_moveI_l': 'tire_moveH_l', 'tire_moveI_r': 'tire_moveH_r'}
for _s in 'lr':
    for _k in 'BCDEFG':
        E551_TO_HOST[f'catapi{_k}_{_s}'] = f'catapi{_k}_{_s}'
        E551_TO_HOST[f'tire_move{_k}_{_s}'] = f'tire_move{_k}_{_s}'
    E551_TO_HOST[f'tire_moveA_{_s}'] = f'tire_moveA_{_s}'
# Kepler bone -> the E551 bone whose bind position it takes
HOST_AT_E551 = {'cannon_main': 'cannon_main', 'catapi_body': 'catapi_body'}
for _s in 'lr':
    for _k in 'ABCDEFG':
        HOST_AT_E551[f'tire_move{_k}_{_s}'] = f'tire_move{_k}_{_s}'
        if _k != 'A':
            HOST_AT_E551[f'catapi{_k}_{_s}'] = f'catapi{_k}_{_s}'
    HOST_AT_E551[f'tire_moveH_{_s}'] = f'tire_moveI_{_s}'
GUN_BONES = ['cannon_l', 'cannon_r', 'cannon_slide_l', 'cannon_slide_r']
MOVED = sorted(set(HOST_AT_E551) | set(GUN_BONES))
# The triangles the repair puts back into the user's hull. The OBJ lacks 5 of the E551 hull's triangles, in two holes on
# the deck's right side (x < 0) whose mirror images on the left are present: a quad (2 triangles) at x -0.784..-0.150,
# y 1.227..1.289, z 1.639..1.688 (the sloped strip just ahead of the turret ring) and a strip of 3 triangles at
# x -0.913..-0.878, y 1.270..1.302, z -1.390..0.646 (the chamfer along the deck's right edge beside the turret).
HULL_FILLS = 5
BARREL_MIN_LENGTH = 2.0      # m: a turret piece this long along z and narrower than BARREL_MAX_WIDTH is a barrel
BARREL_MAX_WIDTH = 0.8
STEP_RATIO = 0.7             # the sleeve ends where the barrel's radius first drops below this fraction
ELEVATIONS = (-5.0, 0.0, 15.0, 30.0, 45.0, 60.0)     # deg checked: the guns stay clear of the hull and turret floor
HALF_TOL = 5e-3             # m: stored positions are half floats (2^-8 m steps at 2..4 m)
MIRROR_TOL = 0.03            # m: the user's turret sits ~1.3 cm right of the centre line; the guns follow it


class ArtilleryCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O (assert would vanish)."""
    if not ok:
        raise ArtilleryCheckError(msg)


def member(rab: Rab, name: str) -> RabFile:
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def model_dir() -> str | None:
    """The twin tank's folder (obj_model.model_dir), or None when it is not installed."""
    d = om.model_dir(MODEL)
    return d if d and os.path.isfile(os.path.join(d, OBJ_FILE)) else None


# ------------------------------------------------------------------------------------------ the barrels

@dataclass
class Barrel:
    side: str                # 'l' (+x) / 'r'
    sleeve: om.Part          # on cannon_<side>
    tube: om.Part            # on cannon_slide_<side>
    axis: tuple[float, float]           # x, y of the barrel's axis
    step_z: float            # where the sleeve ends (the tube starts)
    pivot: Vec3              # the trunnion: the axis at the turret's front face
    muzzle: Vec3             # the axis in the mouth plane
    bore: float              # the mouth's inscribed diameter (m)
    mouth: float             # the mouth ring's mean diameter (m)


def _rings(part: om.Part, axis: tuple[float, float]) -> list[tuple[float, float]]:
    """(z, largest distance from the axis) per vertex ring (distinct z), front to back sorted by z."""
    r: dict[float, float] = {}
    for p in part.positions():
        z = round(p[2], 4)
        r[z] = max(r.get(z, 0.0), math.hypot(p[0] - axis[0], p[1] - axis[1]))
    return sorted(r.items())


def _mouth(part: om.Part, z: float) -> tuple[Vec3, float, float]:
    """(centre, inscribed diameter, mean diameter) of the ring of vertices at z (the muzzle's mouth)."""
    ring = list({(round(p[0], 5), round(p[1], 5)) for p in part.positions() if abs(p[2] - z) < 1e-4})
    _req(len(ring) >= 6, f'muzzle ring at z {z:.3f}: {len(ring)} vertices')
    cx, cy = sum(p[0] for p in ring) / len(ring), sum(p[1] for p in ring) / len(ring)
    ring.sort(key=lambda p: math.atan2(p[1] - cy, p[0] - cx))
    inner = min(abs((b[0] - a[0]) * (cy - a[1]) - (b[1] - a[1]) * (cx - a[0])) / math.hypot(b[0] - a[0], b[1] - a[1])
                for a, b in zip(ring, ring[1:] + ring[:1]))
    mean = sum(math.hypot(p[0] - cx, p[1] - cy) for p in ring) / len(ring)
    return (cx, cy, z), 2 * inner, 2 * mean


def front_z(tris: list[tuple[Vec3, Vec3, Vec3]], x: float, y: float) -> float | None:
    """The largest z where the line (x, y, z) crosses a triangle: where a barrel's axis leaves the turret's front."""
    best = None
    for a, b, c in tris:
        d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
        if abs(d) < 1e-12:
            continue
        u = ((x - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (y - a[1])) / d
        v = ((b[0] - a[0]) * (y - a[1]) - (x - a[0]) * (b[1] - a[1])) / d
        if u >= 0 and v >= 0 and u + v <= 1:
            z = a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2])
            best = z if best is None else max(best, z)
    return best


def split_turret(part: om.Part) -> tuple[om.Part, dict[str, Barrel]]:
    """(turret body, {'l', 'r': Barrel}) of the turret object: its connected pieces, the two long narrow ones the
    barrels, each cut at its sleeve's step and placed against the body's front face."""
    pieces = om.components(part)
    barrels = [p for p in pieces if (lambda lo, hi: hi[2] - lo[2] >= BARREL_MIN_LENGTH and
                                     hi[0] - lo[0] <= BARREL_MAX_WIDTH)(*p.box())]
    body_parts = [p for p in pieces if p not in barrels]
    _req(len(barrels) == 2 and body_parts, f'turret: {len(barrels)} barrels, {len(body_parts)} other pieces')
    body = om.merge(body_parts, part.name)
    out: dict[str, Barrel] = {}
    for b in barrels:
        lo, hi = b.box()
        side = 'l' if lo[0] + hi[0] > 0 else 'r'
        axis = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2)
        rings = _rings(b, axis)
        step = next((z1 for (z0, r0), (z1, r1) in zip(rings, rings[1:]) if r1 < STEP_RATIO * r0), None)
        _req(step is not None, f'barrel {side}: no step from a sleeve to a thinner tube')
        z0 = max(z for z, _r in rings if z < step)                  # the sleeve's last ring
        cut = (z0 + step) / 2
        halves = om.split_part(b, lambda t: min(p[2] for p in t) < cut)
        sleeve, tube = halves[True], halves[False]
        front = front_z([tuple(body.verts[i].pos for i in t) for t in body.tris], axis[0], axis[1])  # type: ignore[misc]
        _req(front is not None, f'barrel {side}: the turret body has no face across its axis')
        muzzle, bore, mouth = _mouth(b, hi[2])
        out[side] = Barrel(side, sleeve, tube, axis, step, (axis[0], axis[1], front), (axis[0], axis[1], muzzle[2]), bore,
                           mouth)  # type: ignore[arg-type]
    _req(set(out) == {'l', 'r'}, 'turret: the barrels are not one each side of the centre line')
    return body, out


# ------------------------------------------------------------------------------------------ reference (the E551)

def reference(ref: Mdb) -> dict[str, list[om.RefTri]]:
    """Per E551 material name: its triangles (position, normal, influences per corner)."""
    out: dict[str, list[om.RefTri]] = {}
    for o in ref.objects:
        for me in o.meshes:
            P = g.mesh_positions(me)
            N = read_elem(me, g.elem_name(me, 'normal') or '') or []
            bi, bw = g.skin_columns(me)
            skins = [tuple((int(b), x) for b, x in zip(i, w) if x > 0) or ((int(i[0]), 1.0),) for i, w in zip(bi, bw)]
            corners = [om.RefCorner(p, n[:3], sk) for p, n, sk in zip(P, N, skins)]  # type: ignore[arg-type]
            out.setdefault(ref.name_of(ref.materials[me.material].name), []).extend(
                (corners[a], corners[b], corners[c]) for a, b, c in g.triangles(me))
    return out


# ------------------------------------------------------------------------------------------ build

def build_model(game, folder: str) -> tuple[Mdb, Rab, dict]:  # noqa: ANN001 - rootcpk.Game
    """(new model, the archive with the textures (model member not yet replaced), info)."""
    host_rab = rab_read(game.read('OBJECT', HOST_ARC))
    ref_rab = rab_read(game.read('OBJECT', REF_ARC))
    host0 = mdb_read(member(host_rab, HOST_MDB).data)
    ref = mdb_read(member(ref_rab, REF_MDB).data)
    obj = om.read_obj(os.path.join(folder, OBJ_FILE))
    info: dict = {'fills': []}
    hb = {host0.name_of(b.name): b.index for b in host0.bones}
    rb = {ref.name_of(b.name): b.index for b in ref.bones}
    for n in set(E551_TO_HOST.values()) | set(HOST_AT_E551) | set(GUN_BONES):
        _req(n in hb, f'host bone {n} missing')
    for n in set(E551_TO_HOST) | set(HOST_AT_E551.values()):
        _req(n in rb, f'E551 bone {n} missing')
    ref_name = {i: n for n, i in rb.items()}

    def to_host(b: int) -> int:
        n = ref_name[b]
        _req(n in E551_TO_HOST, f'an OBJ vertex lies on E551 bone {n}, which has no Kepler counterpart')
        return hb[E551_TO_HOST[n]]

    # 1. the parts: the hull / tracks / lights cut by the E551 material each triangle is in (an OBJ object can mix
    #    them), holes repaired from that material, skinned from the E551; the turret split into body and barrels
    ref_tris = reference(ref)
    family = [ref_tris[m] for m in HULL_MATERIALS]
    pieces: dict[str, list[tuple[om.Part, list[om.Skin]]]] = {}
    albedo: dict[str, str] = {}
    turret: om.Part | None = None
    for part in om.obj_parts(obj):
        _req(part.name.startswith((TURRET_OBJECT,) + E551_OBJECTS), f'{OBJ_FILE}: object {part.name} is not known')
        if part.name.startswith(TURRET_OBJECT):
            _req(turret is None, f'{OBJ_FILE}: a second turret part {part.name}')
            turret = part
            continue
        tex = om.texture_path(obj, part.material, TEXTURE_FILES)
        for stock, sub in om.split_by_reference(part, {m: ref_tris[m] for m in HULL_MATERIALS}).items():
            _req(stock != '', f'{part.name}: {len(sub.tris)} triangles the E551 does not have')
            new = HULL_MATERIALS[stock]
            _req(albedo.setdefault(new, tex) == tex, f'{new}: two albedo textures ({albedo[new]}, {tex})')
            info['fills'] += om.restore_from_reference(sub, ref_tris[stock])
            skins = om.skins_by_reference(sub, [t for f in family for t in f], to_host)
            pieces.setdefault(new, []).append((sub, skins))
    _req(turret is not None, f'{OBJ_FILE}: no turret object')
    body, barrels = split_turret(turret)  # type: ignore[arg-type]
    info['barrels'] = barrels
    tm = TURRET_MATERIAL[0]
    albedo[tm] = om.texture_path(obj, turret.material, TEXTURE_FILES)  # type: ignore[union-attr]
    pieces[tm] = [(body, om.rigid(body, hb['cannon_main']))]
    for s, b in barrels.items():
        pieces[tm] += [(b.sleeve, om.rigid(b.sleeve, hb[f'cannon_{s}'])), (b.tube, om.rigid(b.tube, hb[f'cannon_slide_{s}']))]
    _req(sum(len(f.tris) for f in info['fills']) == HULL_FILLS,
         f'repair put back {sum(len(f.tris) for f in info["fills"])} triangles, expected {HULL_FILLS}')

    # 2. the skeleton: the Kepler's, bones moved onto this model (stock rotations)
    md = replace(host0, objects=[replace(host0.objects[0], meshes=[])], materials=[], textures=[], buffer_order=None)
    rw = bind_world(ref)
    for n in sorted(HOST_AT_E551, key=lambda n: hb[n]):      # parents first (bones are stored parent-first)
        md = g.set_bone_origin(md, hb[n], tuple(rw[rb[HOST_AT_E551[n]]][12:15]))  # type: ignore[arg-type]
    for s, b in barrels.items():
        md = g.set_bone_origin(md, hb[f'cannon_{s}'], b.pivot)
        md = g.set_bone_origin(md, hb[f'cannon_slide_{s}'], b.muzzle)

    # 3. materials and textures: the user's albedo files (DDS as they are, PNG / JPEG converted), the E551's other maps
    keep = {member(host_rab, HOST_MDB).name.lower()}
    host_rab.files = [f for f in host_rab.files if f.name.lower() in keep]
    templates = {v: k for k, v in HULL_MATERIALS.items()}
    templates[tm] = TURRET_MATERIAL[1]
    stock_files: set[str] = set()
    made: dict[str, bytes] = {}
    mat_index: dict[str, int] = {}
    for new in pieces:
        src = os.path.basename(albedo[new])
        tex = {'albedo': src.rsplit('.', 1)[0].lower() + '.dds'}
        if tex['albedo'] not in made:
            made[tex['albedo']] = om.texture_dds(albedo[new])
        if new == tm:
            tex['normal'], tex['param_r_m_occ_hr'] = FLAT_NORMAL[0], PLAIN_RMO[0]
            made[FLAT_NORMAL[0]] = texfile.solid_dxt1(FLAT_NORMAL[1])
            made[PLAIN_RMO[0]] = texfile.solid_dxt1(PLAIN_RMO[1])
        md, mat_index[new] = om.add_material(md, ref, templates[new], new, tex)
        m = next(m for m in ref.materials if ref.name_of(m.name) == templates[new])
        stock_files |= {ref.textures[x.texture].filename for x in m.textures if x.kind not in tex}
    for fn, dds in sorted(made.items()):
        om.add_texture(host_rab, fn, dds)
    g.copy_texture_members(host_rab, ref_rab, sorted(stock_files))

    # 4. meshes (hull layout: the Kepler body mesh's; tracks: the Kepler track mesh's, with its second uv)
    body_layout, track_layout = host0.objects[0].meshes[0], host0.objects[0].meshes[1]
    meshes = []
    for new, ps in pieces.items():
        layout = track_layout if new in (HULL_MATERIALS['Caterpi_l'], HULL_MATERIALS['Caterpi_r']) else body_layout
        meshes += om.build_meshes(layout, ps, mat_index[new])
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    md = replace(md, objects=[replace(md.objects[0], meshes=meshes)])
    md = g.recompute_bounds(md)
    info['tex_files'] = sorted(made) + sorted(stock_files)
    return md, host_rab, info


def build_with_info(game, folder: str) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001 - rootcpk.Game
    """(archive bytes, model, info): build() plus what the report prints."""
    md, rab, info = build_model(game, folder)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(rab, HOST_MDB).stored = stored
    return rab_write(rab), md, info


def build(game, folder: str) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished EDF6VC_ARTILLERY.MRAB (the Kepler's archive layout: its skeleton, this model, its textures)."""
    return build_with_info(game, folder)[0]


# ------------------------------------------------------------------------------------------ check

def _skeleton_row(md: Mdb, b) -> tuple:  # noqa: ANN001 - mdb.Bone
    return (md.name_of(b.name), b.parent, b.sibling, b.child, b.child_count, b.kind, b.depth_delta, b.bounded)


def make_fingerprint(host0: Mdb) -> list[tuple]:
    """STOCK_BONES of the stock host model (used once to generate the constant below): per bone its skeleton row and
    the sha256[:16] of its float32 model-space bind (all 16)."""
    w = bind_world(host0)
    return [_skeleton_row(host0, b) + (hashlib.sha256(struct.pack('<16f', *w[b.index])).hexdigest()[:16],)
            for b in host0.bones]


def _mesh_ok(md: Mdb, o, files: set[str]) -> None:  # noqa: ANN001 - mdb.Object
    nb = len(md.bones)
    _req(0 <= o.bone < nb and md.bones[o.bone].kind == 2, f'object {md.name_of(o.name)} not on a kind-2 bone')
    _req(len(o.meshes) > 0, f'object {md.name_of(o.name)} has no mesh')
    _req(sorted(me.mesh_index for me in o.meshes) == list(range(len(o.meshes))), 'mesh_index not a permutation')
    for j, me in enumerate(o.meshes):
        nv = me.nverts
        tag = f'{md.name_of(o.name)} mesh {j}'
        _req(me.vsize > 0 and len(me.vdata) == nv * me.vsize, f'{tag}: vertex buffer size')
        _req(0 < nv < 0x10000, f'{tag}: {nv} vertices')
        _req(len(me.indices) % 6 == 0 and me.indices, f'{tag}: index count')
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(max(idx) < nv, f'{tag}: index {max(idx)} >= {nv} vertices')
        _req(0 <= me.material < len(md.materials), f'{tag}: material {me.material}')
        bi, bw = g.skin_columns(me)
        _req(len(bi) == nv and len(bw) == nv, f'{tag}: skin columns')
        for r, wt in zip(bi, bw):
            _req(all(int(i) < nb for i, x in zip(r, wt) if x > 0), f'{tag}: blend index >= {nb}')
            _req(all(md.bones[int(i)].kind == 3 for i, x in zip(r, wt) if x > 0), f'{tag}: skinned to a non-skin bone')
            _req(abs(sum(wt) - 1.0) < 1e-3, f'{tag}: weights sum {sum(wt)}')
        for p in g.mesh_positions(me):
            _req(all(abs(x) < 50.0 for x in p), f'{tag}: vertex {p} out of range')
    for m in md.materials:
        for x in m.textures:
            _req(0 <= x.texture < len(md.textures), f'material {md.name_of(m.name)}: texture {x.texture}')
            fn = md.textures[x.texture].filename
            stem, ext = fn.rsplit('.', 1)
            for want in (fn, f'{stem}.lod.{ext}'):
                _req(want.lower() in files, f'material {md.name_of(m.name)}: texture member {want} missing')


def pitched(P: list[Vec3], o: Vec3, deg: float) -> list[Vec3]:
    """`P` pitched muzzle-up (+z towards +y) by `deg` about the x axis through `o` (a gun bone, stock rotation)."""
    c, sn = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(p[0], o[1] + (p[1] - o[1]) * c + (p[2] - o[2]) * sn, o[2] - (p[1] - o[1]) * sn + (p[2] - o[2]) * c)
            for p in P]


def box(P: list[Vec3]) -> tuple[Vec3, Vec3]:
    return (tuple(min(p[c] for p in P) for c in range(3)),  # type: ignore[return-value]
            tuple(max(p[c] for p in P) for c in range(3)))


def turret_tris(md: Mdb) -> list[tuple[Vec3, Vec3, Vec3]]:
    """The turret body's triangles: cannon_main-skinned, in the turret material."""
    main, mat = md.bone_index('cannon_main'), next(m.index for m in md.materials if md.name_of(m.name) == TURRET_MATERIAL[0])
    out = []
    for o in md.objects:
        for me in o.meshes:
            if me.material != mat:
                continue
            P, bi = g.mesh_positions(me), g.skin_columns(me)[0]
            out += [tuple(P[i] for i in t) for t in g.triangles(me) if all(int(bi[i][0]) == main for i in t)]
    return out  # type: ignore[return-value]


def inside(tris: list[tuple[Vec3, Vec3, Vec3]], p: Vec3) -> bool:
    """`p` inside the closed mesh `tris` (crossings of the ray +z, odd = inside)."""
    hits = 0
    for a, b, c in tris:
        d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
        if abs(d) < 1e-12:
            continue
        u = ((p[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (p[1] - a[1])) / d
        v = ((b[0] - a[0]) * (p[1] - a[1]) - (p[0] - a[0]) * (b[1] - a[1])) / d
        if u >= 0 and v >= 0 and u + v <= 1 and a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]) > p[2]:
            hits += 1
    return hits % 2 == 1


def gun_clearance(md: Mdb, side: str) -> list[tuple[float, float, float, int]]:
    """Per ELEVATIONS angle, cannon_<side>'s geometry pitched about the bone: (deg, its lowest vertex y, the lowest y
    of its vertices ahead of the turret body's front, how many of its vertices ahead of the pivot are inside the
    turret body)."""
    pts = g.skinned_points(md)
    gi = md.bone_index(f'cannon_{side}')
    o = bind_world(md)[gi][12:15]
    P = [p for b in g.subtree(md, gi) for p in pts.get(b, [])]
    body = turret_tris(md)
    front = max(p[2] for t in body for p in t)
    out = []
    for e in ELEVATIONS:
        Q = pitched(P, (o[0], o[1], o[2]), e)
        ahead = [q[1] for q in Q if q[2] > front]
        hit = sum(1 for p, q in zip(P, Q) if p[2] > o[2] + 1e-6 and inside(body, q))
        out.append((e, min(q[1] for q in Q), min(ahead) if ahead else math.inf, hit))
    return out


def check(arc: bytes) -> None:
    """Re-read `arc` and raise ArtilleryCheckError unless: archive and model round-trip; < 256 bones; every mesh's
    vertex buffer, indices, blend indices (skin bones only), weights, material and numbering are valid; every
    material texture (HD and .lod) is a member, before the model in folder-table order, in the stock HD / .lod
    layout (obj_model.texture_problems); every bind x inverse bind is the identity; the bones are the
    Kepler's (names, parents, links, kinds), unmoved ones at their stock model-space bind, moved ones with their stock
    rotation; every skin bone of the stock wheels / tracks / turret / guns carries geometry and the radars none; each
    wheel bone sits at its wheel's centre and each track station on its wheel; the guns are mirror images (to
    MIRROR_TOL), each pivot on its sleeve's axis at the turret's front face, each slide bone on the tube's axis in the
    mouth plane, the tube ahead of the sleeve; at every ELEVATIONS angle no gun vertex goes below the turret body's
    floor, none ahead of the turret below the hull's top, and none ahead of the pivot into the turret body."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    _req(len(md.bones) < 256, f'{len(md.bones)} bones')
    files = {f.name.lower() for f in rab.files}
    for o in md.objects:
        _mesh_ok(md, o, files)
    bad = om.texture_problems(rab, md, HOST_MDB)
    _req(not bad, 'textures: ' + '; '.join(bad))
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    _req(len(md.bones) == len(STOCK_BONES), f'{len(md.bones)} bones, stock {len(STOCK_BONES)}')
    for b, s in zip(md.bones, STOCK_BONES):
        n = md.name_of(b.name)
        _req(_skeleton_row(md, b) == tuple(s[:8]), f'bone {b.index} {n}: name / links / kind differ from stock {s[:8]}')
        if n in MOVED:
            _req(max(abs(w[b.index][k] - (1.0 if k in (0, 5, 10) else 0.0)) for k in range(12)) < 1e-5,
                 f'bone {n}: rotation differs from stock (identity)')
        else:
            _req(hashlib.sha256(struct.pack('<16f', *w[b.index])).hexdigest()[:16] == s[8],
                 f'bone {n}: model-space bind differs from stock')
    pts = g.skinned_points(md)
    pos = {md.name_of(b.name): w[b.index][12:15] for b in md.bones}
    for n in ('doppler_radar', 'tracking_radar'):
        _req(md.bone_index(n) not in pts, f'{n} carries geometry')
    for n in MOVED + ['body']:
        _req(len(pts.get(md.bone_index(n), [])) >= 8, f'{n}: no geometry')
    for s in 'lr':
        for k in 'ABCDEFGH':
            t, (lo, hi) = pos[f'tire_move{k}_{s}'], box(pts[md.bone_index(f'tire_move{k}_{s}')])
            _req(all(abs((lo[c] + hi[c]) / 2 - t[c]) < 0.03 for c in (1, 2)), f'tire_move{k}_{s} off its wheel centre')
            if k not in 'AH':
                _req(max(abs(pos[f'catapi{k}_{s}'][c] - t[c]) for c in range(3)) < 1e-4,
                     f'catapi{k}_{s} not at tire_move{k}_{s}')
    for c, k in ((0, -1.0), (1, 1.0), (2, 1.0)):
        for n in ('cannon', 'cannon_slide'):
            _req(abs(pos[f'{n}_l'][c] - k * pos[f'{n}_r'][c]) < MIRROR_TOL, f'{n}_l / _r not mirrored')
    body = turret_tris(md)
    floor = min(p[1] for t in body for p in t)
    hull_top = max(p[1] for n in ('body', 'catapi_body') for p in pts[md.bone_index(n)])
    for s in 'lr':
        t, sl = pos[f'cannon_{s}'], pos[f'cannon_slide_{s}']
        slo, shi = box(pts[md.bone_index(f'cannon_{s}')])
        tlo, thi = box(pts[md.bone_index(f'cannon_slide_{s}')])
        _req(all(slo[c] < t[c] < shi[c] for c in range(3)), f'cannon_{s}: pivot outside its sleeve')
        _req(abs(t[0] - (slo[0] + shi[0]) / 2) < 0.01 and abs(t[1] - (slo[1] + shi[1]) / 2) < 0.01,
             f'cannon_{s}: pivot off the sleeve axis')
        fz = front_z(body, t[0], t[1])
        _req(fz is not None and abs(fz - t[2]) < HALF_TOL, f'cannon_{s}: pivot not where its axis leaves the turret')
        _req(abs(sl[2] - thi[2]) < HALF_TOL and abs(sl[0] - (tlo[0] + thi[0]) / 2) < 0.01
             and abs(sl[1] - (tlo[1] + thi[1]) / 2) < 0.01, f'cannon_slide_{s}: not on the axis in the mouth plane')
        _req(tlo[2] > slo[2] and thi[2] > shi[2], f'cannon_{s}: the tube does not run ahead of the sleeve')
        for e, low, ahead, hit in gun_clearance(md, s):
            _req(hit == 0, f'cannon_{s} at {e:g} deg: {hit} vertices ahead of the pivot inside the turret')
            _req(low >= floor - 1e-3, f'cannon_{s} at {e:g} deg: a vertex at y {low:.3f}, below the turret floor {floor:.3f}')
            _req(ahead > hull_top, f'cannon_{s} at {e:g} deg: ahead of the turret down to y {ahead:.3f} (hull {hull_top:.3f})')


# ------------------------------------------------------------------------------------------ debug dump

def dump_obj(md: Mdb, path: str) -> dict[str, tuple[Vec3, Vec3]]:
    """Write the model's bind-pose triangles as an OBJ (one group per primary bone) and return the vertex box per
    bone (a visual check: open it in any viewer)."""
    names = {b.index: md.name_of(b.name) for b in md.bones}
    groups: dict[str, list[tuple[Vec3, Vec3, Vec3]]] = {}
    for o in md.objects:
        for me in o.meshes:
            P = g.mesh_positions(me)
            bi, _bw = g.skin_columns(me)
            for t in g.triangles(me):
                groups.setdefault(names[int(bi[t[0]][0])], []).append(tuple(P[i] for i in t))  # type: ignore[arg-type]
    lines = []
    n = 0
    for name, tris in sorted(groups.items()):
        lines.append(f'o {name}')
        for t in tris:
            lines += [f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}' for p in t]
            lines.append(f'f {n + 1} {n + 2} {n + 3}')
            n += 3
    with open(path, 'w', encoding='ascii', newline='\n') as h:
        h.write('\n'.join(lines) + '\n')
    return {k: box([p for t in v for p in t]) for k, v in groups.items()}


# make_fingerprint() of the stock OBJECT/V603_FLAK.MRAB (every stock bone has an identity rotation)
STOCK_BONES: list[tuple] = [
    ('v603_flak', -1, -1, 1, 2, 0, 1, 0, 'dcf129dd07da5f7a'),
    ('globalSRT', 0, 39, 2, 1, 0, 1, 0, '5998e9d1dd9bf48a'),
    ('body', 1, -1, 3, 18, 3, 1, 1, '5998e9d1dd9bf48a'),
    ('cannon_main', 2, 10, 4, 4, 3, 1, 1, '3bdd9c3498de1f0c'),
    ('cannon_l', 3, 6, 5, 1, 3, 1, 1, '7115b0b0a5f873bf'),
    ('cannon_slide_l', 4, -1, -1, 0, 3, -1, 1, '16f9e9aabd3793f1'),
    ('cannon_r', 3, 8, 7, 1, 3, 1, 1, 'ada0643d4e47214c'),
    ('cannon_slide_r', 6, -1, -1, 0, 3, -1, 1, 'a38a6041969035cc'),
    ('doppler_radar', 3, 9, -1, 0, 3, 0, 1, '873085c6a8bf4046'),
    ('tracking_radar', 3, -1, -1, 0, 3, -1, 1, 'eaa71daa684998aa'),
    ('catapi_body', 2, 23, 11, 12, 3, 1, 1, '5f00902d7fe1a3fb'),
    ('catapiB_l', 10, 12, -1, 0, 3, 0, 1, '25a41aa65512b1a4'),
    ('catapiB_r', 10, 13, -1, 0, 3, 0, 1, 'cd753aae0f1a26bb'),
    ('catapiC_l', 10, 14, -1, 0, 3, 0, 1, '12f34a2ec418eb73'),
    ('catapiC_r', 10, 15, -1, 0, 3, 0, 1, '77cf6d4ef1e08aba'),
    ('catapiD_l', 10, 16, -1, 0, 3, 0, 1, '77c35bb2aa40339d'),
    ('catapiD_r', 10, 17, -1, 0, 3, 0, 1, 'bb0a01a767db909b'),
    ('catapiE_l', 10, 18, -1, 0, 3, 0, 1, 'ff8459502b3d0a6d'),
    ('catapiE_r', 10, 19, -1, 0, 3, 0, 1, '7c527cc135f499cd'),
    ('catapiF_l', 10, 20, -1, 0, 3, 0, 1, 'eb8e58a8e5f545aa'),
    ('catapiF_r', 10, 21, -1, 0, 3, 0, 1, 'e9f6cc1651a06674'),
    ('catapiG_l', 10, 22, -1, 0, 3, 0, 1, 'a2d6828e8cca01e5'),
    ('catapiG_r', 10, -1, -1, 0, 3, -1, 1, '09cf00130fa50aee'),
    ('tire_moveA_l', 2, 24, -1, 0, 3, 0, 1, 'fb2cc12ed40d996d'),
    ('tire_moveA_r', 2, 25, -1, 0, 3, 0, 1, 'e8f50d293c128ca7'),
    ('tire_moveB_l', 2, 26, -1, 0, 3, 0, 1, '35aef7f0e5528aa2'),
    ('tire_moveB_r', 2, 27, -1, 0, 3, 0, 1, '6d20371ab8302d7f'),
    ('tire_moveC_l', 2, 28, -1, 0, 3, 0, 1, '63e3adf402b79c6a'),
    ('tire_moveC_r', 2, 29, -1, 0, 3, 0, 1, 'f1fb54fb1013ddf2'),
    ('tire_moveD_l', 2, 30, -1, 0, 3, 0, 1, '49fe9a21e5ec3164'),
    ('tire_moveD_r', 2, 31, -1, 0, 3, 0, 1, 'e242e67340adf82f'),
    ('tire_moveE_l', 2, 32, -1, 0, 3, 0, 1, 'bf5b4e5970a1d147'),
    ('tire_moveE_r', 2, 33, -1, 0, 3, 0, 1, 'dcc720e84b5cdd49'),
    ('tire_moveF_l', 2, 34, -1, 0, 3, 0, 1, 'f4099e9ce66116c3'),
    ('tire_moveF_r', 2, 35, -1, 0, 3, 0, 1, 'e57c1cad9ed3b9d1'),
    ('tire_moveG_l', 2, 36, -1, 0, 3, 0, 1, 'e59db9a5f565bc27'),
    ('tire_moveG_r', 2, 37, -1, 0, 3, 0, 1, '966534435ad101af'),
    ('tire_moveH_l', 2, 38, -1, 0, 3, 0, 1, '38e5992be1f8edbd'),
    ('tire_moveH_r', 2, -1, -1, 0, 3, -2, 1, '8925b9ebe34956ed'),
    ('polymesh', 0, -1, -1, 0, 2, 1, 0, '5998e9d1dd9bf48a'),
]


def main(argv: list[str]) -> int:
    import sys
    from rootcpk import Game, DEFAULT_GAME
    if len(argv) != 1:
        print(__doc__)
        return 2
    folder = model_dir()
    if folder is None:
        print(f'no {MODEL} model folder ({", ".join(om.model_roots())})')
        return 1
    os.makedirs(argv[0], exist_ok=True)
    arc, md, info = build_with_info(Game(DEFAULT_GAME), folder)
    check(arc)
    with open(os.path.join(argv[0], 'EDF6VC_ARTILLERY.MRAB'), 'wb') as h:
        h.write(arc)
    for f in info['fills']:
        print('repaired', f.describe())
    for s, b in sorted(info['barrels'].items()):
        print(f'cannon_{s}: axis x {b.axis[0]:.4f} y {b.axis[1]:.4f}, sleeve to tube at z {b.step_z:.4f}, pivot z '
              f'{b.pivot[2]:.4f}, muzzle z {b.muzzle[2]:.4f}, bore {b.bore:.3f} m (mouth ring {b.mouth:.3f} m)')
        for e, low, ahead, hit in gun_clearance(md, s):
            print(f'  {e:5.1f} deg: lowest y {low:.3f}, ahead of the turret {ahead:.3f}, inside the turret {hit}')
    for name, (lo, hi) in sorted(dump_obj(md, os.path.join(argv[0], 'artillery.obj')).items()):
        print(f'{name:16} {" ".join(f"{x:7.3f}" for x in lo)}   {" ".join(f"{x:7.3f}" for x in hi)}')
    print(f'{len(arc)} bytes, checks passed', file=sys.stderr)
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
