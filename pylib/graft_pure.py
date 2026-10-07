"""Model grafting helpers over pylib/mdb.py (EDF6 MDB0 + RAB/MRAB), pure Python (math, lists, tuples; no numpy),
so the mod installer (PyInstaller, pure-Python pylib) can build grafted models from the player's own Root.cpk.

Conventions (same as pylib/mdb.py): every bone matrix is a row-major 4x4 list of 16 floats in row-vector convention
(p' = p * M, row 3 = translation); skinned meshes store model-space bind positions; `inv_bind` is the inverse of the
bone's bind (model-space) matrix; bone bounds (half/centre) are in the bone's own bind frame.

Functions:
  subtree(md, root)                         bone indices of a subtree
  strip_geometry(md, keep)                  drop every triangle with a vertex influenced by a bone outside `keep`
  move_bones(md, bones, delta, ...)         translate the bind pose of a bone set (local / inv_bind consistent),
                                            optionally carrying the skinned vertices bound to it
  set_bone_origin(md, bone, origin)         move one bone's bind origin (children's binds stay put)
  extract_meshes(donor, ...)                donor meshes re-skinned to host bones, transformed into host space
  merge_materials(host, donor, used)        append donor materials + texture refs (+ names), index map back
  append_meshes(md, meshes, mat_map, ...)   add meshes to an existing object (or a new object on a bone)
  recompute_bounds(md)                      half/centre of the bones carrying geometry and of the object bones
  relink(md)                                sibling / child / depth links from the parent links
  copy_texture_members(host_rab, donor_rab, filenames)   copy .DDS / .lod.DDS members between archives
"""
from __future__ import annotations

import struct
from dataclasses import replace
from typing import Callable, Iterable

from mdb import Bone, Mat, Mdb, Mesh, Object, Rab, RabFile, Texture, bind_world, insert_member, mdb_read, mmul, read_elem
from mdb_jet import link, pack_vertex, vertex_table

Vec3 = tuple[float, float, float]
Row = list[tuple[float, ...]]


# ------------------------------------------------------------------------------------------ matrices

def translation(d: Vec3) -> Mat:
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, float(d[0]), float(d[1]), float(d[2]), 1.0]


def inverse_affine_general(m: Mat) -> Mat:
    """Inverse of an affine row-vector matrix (any invertible 3x3 part, not only rotations)."""
    a, b, c = m[0], m[1], m[2]
    d, e, f = m[4], m[5], m[6]
    g, h, i = m[8], m[9], m[10]
    co = [e * i - f * h, -(d * i - f * g), d * h - e * g,
          -(b * i - c * h), a * i - c * g, -(a * h - b * g),
          b * f - c * e, -(a * f - c * d), a * e - b * d]
    det = a * co[0] + b * co[1] + c * co[2]
    assert abs(det) > 1e-12, 'singular bone matrix'
    r = [co[0] / det, co[3] / det, co[6] / det,
         co[1] / det, co[4] / det, co[7] / det,
         co[2] / det, co[5] / det, co[8] / det]
    t = m[12:15]
    nt = [-(t[0] * r[0 + k] + t[1] * r[3 + k] + t[2] * r[6 + k]) for k in range(3)]
    return [r[0], r[1], r[2], 0.0, r[3], r[4], r[5], 0.0, r[6], r[7], r[8], 0.0, nt[0], nt[1], nt[2], 1.0]


def xform(p: Iterable[float], m: Mat) -> Vec3:
    """Point (w = 1) times a row-vector 4x4."""
    x, y, z = tuple(p)[:3]
    return (x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14])


# ------------------------------------------------------------------------------------------ small helpers

def subtree(md: Mdb, root: int) -> set[int]:
    """`root` and all its descendants (bones are stored parent-first)."""
    s = {root}
    for b in md.bones:
        if b.parent in s:
            s.add(b.index)
    return s


def bone_by_name(md: Mdb, name: str) -> int:
    i = md.bone_index(name)
    assert i >= 0, f'no bone {name}'
    return i


def elem_name(me: Mesh, name: str) -> str | None:
    """The layout element called `name` in either case (stock files use both 'position' and 'POSITION')."""
    return next((e.name for e in me.elems if e.name.lower() == name.lower()), None)


def mesh_positions(me: Mesh) -> list[Vec3]:
    return [(p[0], p[1], p[2]) for p in read_elem(me, elem_name(me, 'position') or '') or []]


def skin_columns(me: Mesh) -> tuple[list[tuple[float, ...]], list[tuple[float, ...]]]:
    bi, bw = elem_name(me, 'BLENDINDICES'), elem_name(me, 'BLENDWEIGHT')
    assert bi and bw and me.flags[1], 'mesh is not skinned'
    return read_elem(me, bi) or [], read_elem(me, bw) or []


def influences(idx: tuple[float, ...], w: tuple[float, ...]) -> set[int]:
    """Bones with a non-zero weight (the first index always counts: 1-influence meshes store weight 1 there)."""
    s = {int(i) for i, x in zip(idx, w) if x > 0}
    return s or {int(idx[0])}


def _pos_key(keys: list[str]) -> int:
    return next(k for k, s in enumerate(keys) if s.split(':')[0].lower() == 'position')


def _bi_key(keys: list[str]) -> int:
    return next(k for k, s in enumerate(keys) if s.split(':')[0].upper() == 'BLENDINDICES')


def triangles(me: Mesh) -> list[tuple[int, int, int]]:
    return list(struct.iter_unpack('<3H', me.indices))


def rebuild_mesh(me: Mesh, rows: list[Row], tris: list[tuple[int, int, int]]) -> Mesh | None:
    """`me` with only the vertices used by `tris` (compacted, original order kept), None when no triangle is left."""
    if not tris:
        return None
    used = sorted({i for t in tris for i in t})
    assert len(used) < 0x10000, 'too many vertices for a u16 index buffer'
    remap = {v: k for k, v in enumerate(used)}
    vdata = b''.join(pack_vertex(me.elems, me.vsize, rows[v]) for v in used)
    idx = [remap[i] for t in tris for i in t]
    return replace(me, vdata=vdata, indices=struct.pack(f'<{len(idx)}H', *idx))


# ------------------------------------------------------------------------------------------ stripping

def strip_geometry(md: Mdb, keep: set[int], drop_empty_objects: bool = True) -> Mdb:
    """`md` with every skinned triangle removed that has a vertex influenced by a bone outside `keep` (rigid
    meshes are kept or dropped whole by their object's bone). Meshes left empty are dropped, and objects left with
    no mesh too (`drop_empty_objects`). Bones, materials and textures are untouched."""
    objects: list[Object] = []
    for o in md.objects:
        meshes: list[Mesh] = []
        for me in o.meshes:
            if not me.flags[1]:
                if o.bone in keep:
                    meshes.append(me)
                continue
            _keys, rows = vertex_table(me)
            bi, bw = skin_columns(me)
            ok = [influences(i, w) <= keep for i, w in zip(bi, bw)]
            tris = [t for t in triangles(me) if all(ok[i] for i in t)]
            new = rebuild_mesh(me, rows, tris)
            if new is not None:
                meshes.append(new)
        if meshes or not drop_empty_objects:
            objects.append(replace(o, meshes=meshes))
    return replace(md, objects=objects, buffer_order=None)


# ------------------------------------------------------------------------------------------ bone moves

def move_bones(md: Mdb, bones: set[int], delta: Vec3, carry_vertices: bool = True) -> Mdb:
    """Translate the bind pose of `bones` (typically a subtree) by `delta` in model space. Each member's bind
    becomes bind * T(delta) and its inverse bind T(-delta) * inv_bind; locals are rewritten so world = local *
    parent stays consistent (a member whose parent is also a member keeps its local exactly; children
    outside the set keep their world bind). With `carry_vertices`, skinned vertices whose influences all lie in
    `bones` move with them (a vertex shared with an outside bone is an error). Bounds are unchanged."""
    w0 = bind_world(md)
    t = translation(delta)
    tneg = translation((-delta[0], -delta[1], -delta[2]))
    new_world = [mmul(w0[i], t) if i in bones else w0[i] for i in range(len(md.bones))]
    out: list[Bone] = []
    for b in md.bones:
        if (b.index in bones) != (b.parent in bones):     # a member under a member keeps its local exactly
            pw_inv = inverse_affine_general(new_world[b.parent]) if b.parent >= 0 else translation((0.0, 0.0, 0.0))
            local = mmul(new_world[b.index], pw_inv)
        else:
            local = list(b.local)
        inv = mmul(tneg, b.inv_bind) if b.index in bones else list(b.inv_bind)
        out.append(replace(b, local=local, inv_bind=inv))
    res = replace(md, bones=out)
    if not carry_vertices:
        return res
    objects = []
    for o in md.objects:
        meshes = []
        for me in o.meshes:
            if not me.flags[1]:
                assert o.bone not in bones, 'rigid mesh on a moved bone is not handled'
                meshes.append(me)
                continue
            keys, rows = vertex_table(me)
            bi, bw = skin_columns(me)
            pk = _pos_key(keys)
            for v, (i, w) in enumerate(zip(bi, bw)):
                inf = influences(i, w)
                if inf & bones:
                    assert inf <= bones, f'vertex {v} straddles moved and fixed bones'
                    p = list(rows[v][pk])
                    p[0] += delta[0]
                    p[1] += delta[1]
                    p[2] += delta[2]
                    rows[v][pk] = tuple(p)
            meshes.append(rebuild_mesh(me, rows, triangles(me)) or me)
        objects.append(replace(o, meshes=meshes))
    return replace(res, objects=objects)


def set_bone_origin(md: Mdb, bone: int, origin: Vec3) -> Mdb:
    """Bone `bone`'s bind origin moved to `origin` (model space, rotation kept). Its children keep their world bind;
    vertices are not moved (use this for bones whose geometry is added afterwards, or that have none)."""
    t = bind_world(md)[bone][12:15]
    return move_bones(md, {bone}, (origin[0] - t[0], origin[1] - t[1], origin[2] - t[2]), carry_vertices=False)


# ------------------------------------------------------------------------------------------ donor meshes

def extract_meshes(donor: Mdb, mesh_sel: Callable[[int, int, Mesh], bool], bone_map: dict[int, int],
                   scale: float = 1.0, offset: Vec3 = (0.0, 0.0, 0.0),
                   tri_filter: Callable[[list[Vec3]], bool] | None = None) -> list[tuple[int, Mesh]]:
    """Donor skinned meshes picked by `mesh_sel(object, mesh, Mesh)`, as (donor material, Mesh) in host terms:
    triangles with a vertex influenced by a bone missing from `bone_map` are dropped (and those `tri_filter`
    rejects: it gets the triangle's 3 donor positions), blend indices are rewritten through `bone_map`, positions
    become p * scale + offset (normals / tangents / uvs / weights unchanged: uniform scale + translation).
    The donor layout is kept (layouts are per mesh)."""
    out: list[tuple[int, Mesh]] = []
    for k, o in enumerate(donor.objects):
        for j, me in enumerate(o.meshes):
            if not mesh_sel(k, j, me):
                continue
            assert me.flags[1], 'only skinned donor meshes are handled'
            keys, rows = vertex_table(me)
            bi, bw = skin_columns(me)
            pk, bk = _pos_key(keys), _bi_key(keys)
            ok = [influences(i, w) <= bone_map.keys() for i, w in zip(bi, bw)]
            P = [(r[pk][0], r[pk][1], r[pk][2]) for r in rows]
            tris = [t for t in triangles(me)
                    if all(ok[i] for i in t) and (tri_filter is None or tri_filter([P[i] for i in t]))]
            for v, r in enumerate(rows):
                p = list(r[pk])
                p[0], p[1], p[2] = p[0] * scale + offset[0], p[1] * scale + offset[1], p[2] * scale + offset[2]
                r[pk] = tuple(p)
                if ok[v]:
                    r[bk] = tuple(bone_map[int(x)] if wt > 0 or n == 0 else 0
                                  for n, (x, wt) in enumerate(zip(r[bk], bw[v])))
            new = rebuild_mesh(me, rows, tris)
            if new is not None:
                out.append((me.material, new))
    return out


# ------------------------------------------------------------------------------------------ materials

def _name_index(names: list[str | None], s: str) -> int:
    """Index of `s` in the name table, appended when missing."""
    if s in names:
        return names.index(s)
    names.append(s)
    return len(names) - 1


def merge_materials(host: Mdb, donor: Mdb, used: Iterable[int]) -> tuple[Mdb, dict[int, int]]:
    """Host with the donor materials `used` appended (with their texture references, deduplicated by file name,
    and their names added to the name table). Returns the model and donor material -> host material index."""
    names = list(host.names)
    textures = list(host.textures)
    tex_map: dict[int, int] = {}
    materials = list(host.materials)
    mat_map: dict[int, int] = {}
    for m in sorted(set(used)):
        src = donor.materials[m]
        slots = []
        for x in src.textures:
            t = donor.textures[x.texture]
            if x.texture not in tex_map:
                hit = next((h.index for h in textures if h.filename.lower() == t.filename.lower()), None)
                if hit is None:
                    hit = len(textures)
                    textures.append(Texture(hit, t.name, t.filename, t.unk))
                tex_map[x.texture] = hit
            slots.append(replace(x, texture=tex_map[x.texture]))
        mat_map[m] = len(materials)
        materials.append(replace(src, index=len(materials), name=_name_index(names, donor.name_of(src.name)),
                                 textures=slots, params=[replace(p) for p in src.params]))
    return replace(host, names=names, textures=textures, materials=materials), mat_map


def append_meshes(md: Mdb, meshes: list[tuple[int, Mesh]], mat_map: dict[int, int], obj: int | None = None,
                  new_object: tuple[str, int] | None = None) -> Mdb:
    """Meshes (donor material, Mesh) added to object `obj`, or to a new object (name, kind-2 bone) appended, with
    their material indices mapped through `mat_map`."""
    fixed = [replace(me, material=mat_map[m]) for m, me in meshes]
    objects = list(md.objects)
    names = list(md.names)
    if new_object is not None:
        objects.append(Object(_name_index(names, new_object[0]), new_object[1], fixed))
    else:
        assert obj is not None
        objects[obj] = replace(objects[obj], meshes=objects[obj].meshes + fixed)
    return replace(md, names=names, objects=objects, buffer_order=None)


# ------------------------------------------------------------------------------------------ bounds

def skinned_points(md: Mdb) -> dict[int, list[Vec3]]:
    """Model-space bind positions per primary bone (first influence) over every skinned mesh."""
    out: dict[int, list[Vec3]] = {}
    for o in md.objects:
        for me in o.meshes:
            if not me.flags[1]:
                continue
            bi, _bw = skin_columns(me)
            for p, i in zip(mesh_positions(me), bi):
                out.setdefault(int(i[0]), []).append(p)
    return out


def bounds(points: list[Vec3], inv: Mat) -> tuple[list[float], list[float]]:
    """(half, centre) of `points` in the frame `inv` maps model space into (w = 1 appended)."""
    q = [xform(p, inv) for p in points]
    lo = [min(v[c] for v in q) for c in range(3)]
    hi = [max(v[c] for v in q) for c in range(3)]
    return [(h - l) / 2 for l, h in zip(lo, hi)] + [1.0], [(h + l) / 2 for l, h in zip(lo, hi)] + [1.0]


def recompute_bounds(md: Mdb, bones: set[int] | None = None) -> Mdb:
    """half/centre (bone frame) of every bounded bone that carries skinned vertices (restricted to `bones` when
    given), and of every object's bone (the object's vertices, through that bone's inverse bind)."""
    pts = skinned_points(md)
    out = list(md.bones)
    for i, p in pts.items():
        if out[i].bounded and (bones is None or i in bones):
            h, c = bounds(p, out[i].inv_bind)
            out[i] = replace(out[i], half=h, centre=c)
    for o in md.objects:
        allp = [p for me in o.meshes for p in mesh_positions(me)]
        if allp:
            h, c = bounds(allp, out[o.bone].inv_bind)
            out[o.bone] = replace(out[o.bone], half=h, centre=c)
    return replace(md, bones=out)


def relink(md: Mdb) -> Mdb:
    """sibling / child / child_count / depth_delta recomputed from the parent links (mdb_jet.link)."""
    bones = [replace(b) for b in md.bones]
    link(bones)
    return replace(md, bones=bones)


# ------------------------------------------------------------------------------------------ archives

def texture_members(rab: Rab, filename: str) -> list[RabFile]:
    """Every archive member that is texture `filename` (e.g. 'x.DDS'): itself and its 'x.lod.DDS' variant."""
    stem = filename.rsplit('.', 1)[0].lower()
    return [f for f in rab.files if f.name.lower().rsplit('.', 1)[0] in (stem, stem + '.lod')]


def is_texture_member(name: str) -> bool:
    return name.lower().endswith('.dds')


def prune_members(rab: Rab, models: Iterable[str]) -> list[str]:
    """`rab` with every model member (.mdb) but `models` taken out, and every texture member (HD and .lod) that no
    kept model's texture table names: what nothing loads once the archive's SGOs name only those models (the stock
    archive a model is written into also carries the stock creature's LODs, debris and colour variants, which no
    generated SGO names: they only took space). Members of any other kind stay; the rest keep their order. Returns
    the names taken out, in archive order."""
    keep = {m.lower() for m in models}
    kept = [f for f in rab.files if f.name.lower() in keep]
    assert len(kept) == len(keep), f'models {sorted(keep)}: {len(kept)} members'
    need: set[str] = set()
    for f in kept:
        for t in mdb_read(f.data).textures:
            found = texture_members(rab, t.filename)
            assert found, f'{f.name}: texture {t.filename} is no member'
            need |= {x.name.lower() for x in found}

    def dead(f: RabFile) -> bool:
        n = f.name.lower()
        return (n.endswith('.mdb') and n not in keep) or (is_texture_member(n) and n not in need)
    gone = [f.name for f in rab.files if dead(f)]
    rab.files = [f for f in rab.files if not dead(f)]
    return gone


def copy_texture_members(host: Rab, donor: Rab, filenames: Iterable[str]) -> list[str]:
    """Copy the donor members of `filenames` (and their .lod variants) into `host` (stored bytes untouched, folder
    mapped by folder name, flag kept), each placed by mdb.insert_member (folder-table order: a .lod before the
    model). Members the host already has (same name) are skipped. Returns the names copied."""
    have = {f.name.lower() for f in host.files}
    copied: list[str] = []
    for fn in filenames:
        found = texture_members(donor, fn)
        assert found, f'donor archive has no member for texture {fn}'
        for f in found:
            if f.name.lower() in have:
                continue
            folder = donor.folders[f.folder]
            if folder not in host.folders:
                host.folders.append(folder)
            insert_member(host, RabFile(f.name, host.folders.index(folder), f.flag, f.stored, f.unk))
            have.add(f.name.lower())
            copied.append(f.name)
    return copied
