"""The generated vehicles' last model pass (docs/model-lod.md): their models' meshes merged by material, and two
lower-detail models (LODs) the game draws instead from farther away.

    finish(files) -> files          a generator's output with every generated vehicle model merged and given its LODs
    check_files(files) -> list      what is wrong with them (empty: fine)
    pack_rows(template, rows, tris, material) -> meshes      one material's packed vertices as meshes (u16 indices)
    merge_meshes(md) -> Mdb         one mesh per (object, material, vertex layout), split only at 65535 vertices
    lods(md) -> [(Mdb, distance)]   the LOD models of `md` and the camera distances that select them

The engine (docs/model-lod.md; EDF.dll 0x6BB890 / 0x6BF9F0): every class built on VehicleBase (0x629450) loads
its model through the generic loader, which reads the SGO's animation_model[0] = [archive, model, [[lod1, d1], [lod2,
d2]]]: each LOD is another model in the same archive, drawn instead of the base one when the camera is more than d
metres from the model's bounding sphere (d ascending). A LOD shares the base model's skeleton instance, so it is the
base model with fewer triangles: the same bones (count, order, binds), the same material and texture tables, the same
vertex layouts. Nothing here re-encodes a vertex: a LOD's vertices are a subset of the base model's packed rows
(mesh_simplify's half-edge collapses), and merging only concatenates rows and indices.

What each LOD keeps and drops (Level): it aims at `ratio` of the base model's triangles but never goes past an error of
one screen line at its switching distance (a 60-degree vertical view on 1080 lines: PIXEL metres per metre), and first
drops the pieces smaller than that line (bolts, lamps). The distances scale with the model: the stock soldiers (about
1 m from their centre to their bounding box's corner) switch at 15 and 30 m (docs/edf41-outfits.md), so a model
`reach` times its own half diagonal away looks as big as a soldier at that distance. A level that would keep more than
KEEP_AT_MOST of the one before it is not written (it would draw the same, for another model's memory); the next level
then starts from the same triangles. The
first-person cockpits (materials edf6vc_cpit_*, pylib/cockpits.py) are left out of the LODs: the camera inside one is
at distance 0, where the base model is drawn.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, replace

import numpy as np

import dsgo
import mesh_simplify
from mdb import (Mdb, Mesh, RabFile, bind_world, cmpl_compress, insert_member, mdb_read, mdb_write, rab_read,
                 rab_write)

MAX_VERTS = 0xFFFF                 # u16 index buffers: fewer than 65536 vertices per mesh
# The classes built on VehicleBase, which load their model through the generic loader that reads the LOD table
# (docs/model-lod.md: VehicleBase 0x629450). Every class EDF.dll's RTTI derives from VehicleBase (its
# class hierarchy descriptors, 2026-10-27), by the name an SGO's xgs_scene_object_class gives it. Not in it, so left
# alone: FarEventObject (make_chute's parachute) and every other non-vehicle class.
VEHICLE_CLASSES = frozenset((
    'Vehicle402_Rocket', 'Vehicle403_Tank', 'Vehicle404_Tank', 'Vehicle501_FortressRobo', 'Vehicle502_GroundRobo',
    'Vehicle503_Bike', 'Vehicle504_begaruta', 'Vehicle505_Tank', 'Vehicle506_Helicopter', 'Vehicle507_Rescuetank',
    'Vehicle510_Maser', 'Vehicle511_Bike', 'Vehicle601_Tank', 'Vehicle603_Flak', 'Vehicle607_RoboTruck',
    'Vehicle60X_Truck', 'Vehicle612_nix', 'VehicleBegaruta', 'VehicleBigBegaruta', 'VehicleHelicopter409',
    'VehicleHelicopter410', 'VehicleHelicopterBase', 'Vehicle_BikeBase', 'Vehicle_Car', 'Vehicle_CarBase',
    'Vehicle_TankBase'))
COCKPIT_PREFIX = 'edf6vc_cpit_'
PIXEL = 2 * math.tan(math.radians(30)) / 1080      # m per m of distance: one line of a 60-degree view on 1080 lines
KEEP_AT_MOST = 0.8
TOLERANCE = 0.05                   # a LOD's box may be this part of the base box's diagonal smaller on a side


@dataclass(frozen=True)
class Level:
    ratio: float      # of the base model's triangles, aimed at
    reach: float      # the switching distance, in the model's half diagonals (a soldier's: about 1 m)


LEVELS = (Level(0.35, 15.0), Level(0.10, 30.0))


class LodError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise LodError(message)


# ------------------------------------------------------------------------------------------ rows

def _elem(me: Mesh, name: str) -> tuple[int, int] | None:
    """(offset, bytes) of the layout element `name` (either case), channel 0."""
    sizes = {1: 16, 4: 12, 7: 8, 12: 8, 21: 4}
    e = next((x for x in me.elems if x.name.lower() == name.lower() and x.channel == 0), None)
    if e is None:
        return None
    _req(e.fmt in sizes, f'vertex element {e.name}: format {e.fmt} is not known')
    return e.offset, sizes[e.fmt]


def _positions(me: Mesh, rows: np.ndarray, name: str = 'position') -> np.ndarray:
    e = next(x for x in me.elems if x.name.lower() == name and x.channel == 0)
    _req(e.fmt in (1, 4, 7), f'vertex element {e.name}: format {e.fmt} is no float vector')
    dtype, width = {1: ('<f4', 4), 4: ('<f4', 3), 7: ('<f2', 4)}[e.fmt]
    size = np.dtype(dtype).itemsize * width
    raw = np.ascontiguousarray(rows[:, e.offset:e.offset + size])
    return raw.view(dtype).reshape(len(rows), width)[:, :3].astype(np.float64)


def _normals_attrs(me: Mesh, rows: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(unit normals, the rows' other columns as one id): rows equal but for their normal and tangent frame are one
    attribute class to mesh_simplify (a crease, not a seam)."""
    frame = [x for x in me.elems if x.name.lower() in ('normal', 'tangent', 'binormal')]
    if not any(x.name.lower() == 'normal' and x.channel == 0 for x in frame):
        return np.zeros((len(rows), 3)), np.arange(len(rows))
    n = _positions(me, rows, 'normal')
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    rest = rows.copy()
    for x in frame:
        a, size = _elem_at(x)
        rest[:, a:a + size] = 0
    _u, inv = np.unique(np.ascontiguousarray(rest).view(np.dtype((np.void, rest.shape[1]))).ravel(),
                        return_inverse=True)
    return n, inv.reshape(-1).astype(np.int64)


def _elem_at(e) -> tuple[int, int]:  # noqa: ANN001 - mdb.VElem
    sizes = {1: 16, 4: 12, 7: 8, 12: 8, 21: 4}
    _req(e.fmt in sizes, f'vertex element {e.name}: format {e.fmt} is not known')
    return e.offset, sizes[e.fmt]


def _layout(me: Mesh) -> tuple:
    return (me.material, bytes(me.flags), me.vsize, tuple((e.fmt, e.offset, e.channel, e.name) for e in me.elems))


def _rows(me: Mesh) -> tuple[np.ndarray, np.ndarray]:
    rows = np.frombuffer(me.vdata, dtype=np.uint8).reshape(-1, me.vsize)
    tris = np.frombuffer(me.indices, dtype='<u2').astype(np.int64).reshape(-1, 3)
    return rows, tris


@dataclass
class _Group:
    """One object's meshes of one (material, flags, layout): their rows and triangles, concatenated."""
    template: Mesh
    rows: np.ndarray
    tris: np.ndarray


def _groups(meshes: list[Mesh]) -> list[_Group]:
    by: dict[tuple, list[Mesh]] = {}
    for me in meshes:
        by.setdefault(_layout(me), []).append(me)
    out = []
    for ms in by.values():
        rows, tris, base = [], [], 0
        for me in ms:
            r, t = _rows(me)
            rows.append(r)
            tris.append(t + base)
            base += len(r)
        out.append(_Group(ms[0], np.concatenate(rows), np.concatenate(tris)))
    return out


def pack_rows(template: Mesh, rows: np.ndarray, tris: np.ndarray, material: int | None = None) -> list[Mesh]:
    """`tris` (rows' indices) as meshes in `template`'s layout and flags (material `material`, else the template's;
    mesh_index 0: the caller numbers them), in triangle order, each under 65536 vertices (a new mesh when the next
    triangle would need the 65536th), its vertices in order of first use. Rows no triangle uses are left out."""
    out: list[Mesh] = []
    tris = np.asarray(tris, dtype=np.int64)
    s = 0
    while s < len(tris):
        flat = tris[s:].ravel()
        _u, first = np.unique(flat, return_index=True)
        new = np.zeros(len(flat), dtype=np.int64)
        new[first] = 1
        k = int(np.searchsorted(np.cumsum(new.reshape(-1, 3).sum(axis=1)), MAX_VERTS, side='right'))
        _req(k > 0, 'a triangle needs more than 65535 vertices')
        chunk = tris[s:s + k].ravel()
        used, first, inverse = np.unique(chunk, return_index=True, return_inverse=True)
        order = np.argsort(first, kind='stable')
        rank = np.empty(len(used), dtype=np.int64)
        rank[order] = np.arange(len(used))
        out.append(replace(template, material=template.material if material is None else material, mesh_index=0,
                           vdata=np.ascontiguousarray(rows[used[order]]).tobytes(),
                           indices=rank[inverse.reshape(-1)].astype('<u2').tobytes()))
        s += k
    return out


def merge_meshes(md: Mdb) -> Mdb:
    """`md` with each object's meshes of one material, flags and vertex layout made one (pack_rows: split only at the
    u16 index limit), in the order of their first mesh. Rows and triangles are kept as they are, in their order: the
    same thing is drawn, in fewer draw calls (each mesh is one per pass: depth, G-buffer, every shadow cascade)."""
    objects = []
    changed = False
    for o in md.objects:
        meshes: list[Mesh] = []
        for g in _groups(o.meshes):
            same = [me for me in o.meshes if _layout(me) == _layout(g.template)]
            if len(same) == 1:
                meshes.append(same[0])
                continue
            changed = True
            meshes += pack_rows(g.template, g.rows, g.tris)
        objects.append(replace(o, meshes=[replace(me, mesh_index=k) for k, me in enumerate(meshes)]))
    return replace(md, objects=objects, buffer_order=None) if changed else md


def triangles(md: Mdb) -> int:
    return sum(len(me.indices) // 6 for o in md.objects for me in o.meshes)


def _model_points(md: Mdb, exterior: bool = True) -> np.ndarray:
    """Every drawn vertex in model space (a skinned mesh's are; an unskinned one's through its object's bone),
    the cockpits' left out when `exterior`."""
    world = bind_world(md)
    pts = []
    for o in md.objects:
        for me in o.meshes:
            if exterior and _cockpit(md, me):
                continue
            rows, tris = _rows(me)
            p = _positions(me, rows)[np.unique(tris)] if len(tris) else np.zeros((0, 3))
            if not me.flags[1]:
                m = np.asarray(world[o.bone], dtype=np.float64).reshape(4, 4)
                p = p @ m[:3, :3] + m[3, :3]
            pts.append(p)
    return np.vstack(pts) if pts else np.zeros((0, 3))


def _cockpit(md: Mdb, me: Mesh) -> bool:
    return (md.name_of(md.materials[me.material].name) or '').startswith(COCKPIT_PREFIX)


def radius(md: Mdb) -> float:
    """Half the diagonal of the model's (exterior's) box: what the LOD distances scale with."""
    p = _model_points(md)
    _req(len(p) > 0, 'the model draws nothing')
    return float(np.linalg.norm(p.max(0) - p.min(0)) / 2)


# ------------------------------------------------------------------------------------------ LODs

def lods(md: Mdb, levels: tuple[Level, ...] = LEVELS, stats: list | None = None) -> list[tuple[Mdb, float]]:
    """The LOD models of `md` (its exterior's triangles collapsed, mesh_simplify) with their distances, nearest first;
    a level that would not save enough (KEEP_AT_MOST) is left out."""
    r = radius(md)
    work = []          # per object: groups, all rows' points, group key, triangles
    for o in md.objects:
        gs = [g for g in _groups([me for me in o.meshes if not _cockpit(md, me)])]
        if not gs:
            work.append((gs, None, None, None, None, None))
            continue
        pts, key, tris, nrm, att, base, abase = [], [], [], [], [], 0, 0
        for gi, g in enumerate(gs):
            pts.append(_positions(g.template, g.rows))
            n, a = _normals_attrs(g.template, g.rows)
            nrm.append(n)
            att.append(a + abase)
            abase += int(a.max()) + 1 if len(a) else 0
            skin = [x for x in (_elem(g.template, 'blendindices'), _elem(g.template, 'blendweight')) if x]
            cols = (np.concatenate([g.rows[:, at:at + size] for at, size in skin], axis=1) if skin
                    else np.zeros((len(g.rows), 1), dtype=np.uint8))
            _u, sk = np.unique(np.ascontiguousarray(cols).view(np.dtype((np.void, cols.shape[1]))).ravel(),
                               return_inverse=True)
            key.append(gi * (1 << 32) + sk.reshape(-1).astype(np.int64))
            tris.append(g.tris + base)
            base += len(g.rows)
        work.append((gs, np.vstack(pts), np.concatenate(key), np.concatenate(tris), np.vstack(nrm), np.concatenate(att)))
    hd = sum(len(w[3]) for w in work if w[3] is not None)
    out: list[tuple[Mdb, float]] = []
    current = [w[3] for w in work]
    before = hd
    for level in levels:
        distance = level.reach * r
        error = PIXEL * distance
        nxt = []
        for (gs, pts, key, _t, nrm, att), cur in zip(work, current):
            if cur is None or not len(cur):
                nxt.append(cur)
                continue
            kept = mesh_simplify.drop_small(pts, cur, error)
            target = int(math.ceil(level.ratio * len(_t)))
            nxt.append(mesh_simplify.simplify(pts, kept, key, target, error, nrm, att))
        count = sum(len(c) for c in nxt if c is not None)
        if stats is not None:
            stats.append({'distance': distance, 'error': error, 'triangles': count, 'kept': count <= KEEP_AT_MOST * before})
        if count > KEEP_AT_MOST * before or count == 0:
            continue        # not worth a model: the next level tries from the same triangles, farther away
        objects = []
        for o, (gs, _p, key, _t, _n, _a), tri in zip(md.objects, work, nxt):
            meshes: list[Mesh] = []
            if tri is not None and len(tri):
                grp = (key[tri[:, 0]] >> 32).astype(np.int64)
                starts = np.concatenate(([0], np.cumsum([len(g.rows) for g in gs])))
                for gi, g in enumerate(gs):
                    mine = tri[grp == gi] - starts[gi]
                    if len(mine):
                        meshes += pack_rows(g.template, g.rows, mine)
            objects.append(replace(o, meshes=[replace(me, mesh_index=k) for k, me in enumerate(meshes)]))
        out.append((replace(md, objects=objects, buffer_order=None), distance))
        current, before = nxt, count
    return out


def lod_name(model: str, level: int) -> str:
    stem = model.rsplit('.', 1)[0]
    return f'{stem}_lod{level}.mdb'


def check_lods(hd: Mdb, models: list[tuple[Mdb, float]]) -> list[str]:
    """What breaks the engine's LOD contract: the same skeleton, names, material and texture tables as the base
    model, vertex layouts the base model has for that material, fewer triangles at each level, the box within the base
    model's (TOLERANCE), distances ascending."""
    bad = []
    layouts = {_layout(me) for o in hd.objects for me in o.meshes}
    lo, hi = _model_points(hd).min(0), _model_points(hd).max(0)
    diag = float(np.linalg.norm(hi - lo))
    prev_tris, prev_d = triangles(hd), 0.0
    for k, (md, d) in enumerate(models, 1):
        if md.bones != hd.bones or md.names != hd.names:
            bad.append(f'LOD {k}: its skeleton or name table differs from the base model')
        if md.materials != hd.materials or md.textures != hd.textures:
            bad.append(f'LOD {k}: its material or texture table differs from the base model')
        if len(md.objects) != len(hd.objects) or any((a.name, a.bone) != (b.name, b.bone)
                                                     for a, b in zip(md.objects, hd.objects)):
            bad.append(f'LOD {k}: its objects differ from the base model')
        if any(_layout(me) not in layouts for o in md.objects for me in o.meshes):
            bad.append(f'LOD {k}: a mesh layout the base model has not')
        for o in md.objects:
            for me in o.meshes:
                ix = np.frombuffer(me.indices, dtype='<u2')
                if len(ix) % 3 or me.nverts > MAX_VERTS or (len(ix) and int(ix.max()) >= me.nverts):
                    bad.append(f'LOD {k}: a mesh with bad indices')
        n = triangles(md)
        if not 0 < n < prev_tris:
            bad.append(f'LOD {k}: {n} triangles, not fewer than the {prev_tris} before it')
        if not d > prev_d:
            bad.append(f'LOD {k}: distance {d} not beyond {prev_d}')
        p = _model_points(md)
        if len(p):
            l2, h2 = p.min(0), p.max(0)
            if (l2 < lo - 1e-4).any() or (h2 > hi + 1e-4).any():
                bad.append(f'LOD {k}: outside the base model\'s box')
            if (l2 > lo + TOLERANCE * diag).any() or (h2 < hi - TOLERANCE * diag).any():
                bad.append(f'LOD {k}: box {l2.round(3).tolist()}..{h2.round(3).tolist()} shrank from '
                           f'{lo.round(3).tolist()}..{hi.round(3).tolist()}')
        prev_tris, prev_d = n, d
    return bad


# ------------------------------------------------------------------------------------------ files

def _archive_key(files: dict[str, bytes], path: str) -> str | None:
    """The files key of an SGO's archive path (app:/Object/x.mrab), None when the output has no such file."""
    rel = path.split(':/', 1)[-1].replace('\\', '/').upper()
    return next((k for k in files if k.replace('\\', '/').upper() == rel), None)


class _Sgo:
    """A vehicle SGO's animation_model[0] in either file format (DSGO: pylib/dsgo.py; SGO: pylib/sgo.py, written back
    with the writer that reproduces the file, so nothing else in it moves)."""

    def __init__(self, data: bytes) -> None:
        self.dsgo = data[:4] == b'DSGO'
        if self.dsgo:
            self.doc = dsgo.parse(data)
            r = self.doc.root
            names = set(r.names.values())
            self.cls = r.get('xgs_scene_object_class') if 'xgs_scene_object_class' in names else None
            self.entry = r.get('animation_model').items[0] if 'animation_model' in names else None
            items = self.entry.items if isinstance(self.entry, dsgo.Node) else None
        else:
            import sgo
            self.version, self.members = sgo.read(data)
            self.writer = next((w for w in (sgo.write, sgo.write_depth_first) if w(self.version, self.members) == data),
                               None)
            self.cls = self.members.get('xgs_scene_object_class')
            am = self.members.get('animation_model')
            self.entry = am[0] if isinstance(am, list) and am else None
            items = self.entry if isinstance(self.entry, list) else None
        self.items = items

    def model(self) -> tuple[str, str] | None:
        it = self.items
        if not it or len(it) < 2 or not isinstance(it[0], str) or not isinstance(it[1], str):
            return None
        return it[0], it[1]

    def table(self) -> list[tuple[str, float]] | None:
        if not self.items or len(self.items) < 3:
            return None
        rows = self.items[2].items if self.dsgo else self.items[2]
        out = []
        for row in rows:
            name, d = (row.items if self.dsgo else row)[:2]
            out.append((name, float(d) if not hasattr(d, 'value') else float(d.value)))
        return out

    def with_table(self, table: list[tuple[str, float]]) -> bytes:
        arc, model = self.model()  # type: ignore[misc]
        if self.dsgo:
            self.entry.items = [arc, model, dsgo.Node([dsgo.Node([n, float(d)]) for n, d in table])]
            return dsgo.compact(self.doc)
        _req(self.writer is not None, 'an SGO neither SGO writer reproduces')
        self.entry[:] = [arc, model, [[n, float(d)] for n, d in table]]
        return self.writer(self.version, self.members)


def _vehicle_models(files: dict[str, bytes]):  # noqa: ANN202 - iterator of (sgo key, _Sgo, archive key)
    for rel in sorted(files):
        up = rel.replace('\\', '/').upper()
        data = files[rel]
        if not (up.startswith('OBJECT/') and up.endswith('.SGO')) or data[:4] not in (b'DSGO', b'SGO\0'):
            continue
        doc = _Sgo(data)
        if doc.cls not in VEHICLE_CLASSES or doc.model() is None:
            continue
        arc = _archive_key(files, doc.model()[0])
        if arc is not None:
            yield rel, doc, arc


def _member(rab, name: str):  # noqa: ANN001, ANN202
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def finish(files: dict[str, bytes], report: dict | None = None) -> dict[str, bytes]:
    """`files` (a generator's output: Mods path -> bytes) with every model a vehicle SGO in it draws from an archive
    in it merged (merge_meshes) and given its LODs (lods: members <model>_lod<k>.mdb next to it, the SGO's
    animation_model[0] = [archive, model, [[lod1, d1], [lod2, d2]]]). Checked (check_files) before it returns."""
    out = dict(files)
    done: dict[tuple[str, str], list] = {}
    for rel, doc, arc in list(_vehicle_models(out)):
        _arc_path, model = doc.model()  # type: ignore[misc]
        _req(doc.table() is None, f'{rel}: animation_model[0] already has a LOD table')
        key = (arc, model.lower())
        if key not in done:
            rab = rab_read(out[arc])
            member = _member(rab, model)
            hd = mdb_read(member.data)
            merged = merge_meshes(hd)
            stats: list = []
            models = lods(merged, stats=stats)
            bad = check_lods(merged, models)
            _req(not bad, f'{arc} {model}: ' + '; '.join(bad))
            pack = (lambda raw, like=member.stored: cmpl_compress(raw) if like[:4] == b'CMPL' else raw)  # noqa: E731
            member.stored = pack(mdb_write(merged))
            table = []
            for k, (md, d) in enumerate(models, 1):
                name = lod_name(member.name, k)
                _req(not any(f.name.lower() == name.lower() for f in rab.files), f'{arc}: {name} already there')
                insert_member(rab, RabFile(name, member.folder, member.flag, pack(mdb_write(md))))
                table.append((name, round(d, 1)))
            out[arc] = rab_write(rab)
            done[key] = table
            if report is not None:
                report[f'{arc}:{model}'] = {
                    'meshes': [sum(len(o.meshes) for o in hd.objects), sum(len(o.meshes) for o in merged.objects)],
                    'triangles': [triangles(merged)] + [triangles(md) for md, _d in models],
                    'exterior': sum(len(me.indices) // 6 for o in merged.objects for me in o.meshes
                                    if not _cockpit(merged, me)),
                    'lod_meshes': [sum(len(o.meshes) for o in md.objects) for md, _d in models],
                    'distances': [d for _n, d in table], 'radius': radius(merged), 'levels': stats}
        if done[key]:
            out[rel] = doc.with_table(done[key])
    bad = check_files(out)
    _req(not bad, '; '.join(bad))
    return out


def check_files(files: dict[str, bytes]) -> list[str]:
    """Every vehicle SGO's LOD table in `files` against its archive: the members are there, their distances ascend,
    and each is a LOD of the base model (check_lods)."""
    bad = []
    for rel, doc, arc in _vehicle_models(files):
        try:
            table = doc.table()
            if table is None:
                continue
            rab = rab_read(files[arc])
            hd = mdb_read(_member(rab, doc.model()[1]).data)  # type: ignore[index]
            models = [(mdb_read(_member(rab, n).data), d) for n, d in table]
        except (LodError, AttributeError, IndexError, TypeError, ValueError) as e:
            bad.append(f'{rel}: LOD table unreadable ({e})')
            continue
        bad += [f'{rel}: {b}' for b in check_lods(hd, models)]
    return bad
