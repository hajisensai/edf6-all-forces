"""The menu's model of a loaded support vehicle (EDF6VC_LO_*): its body with what it carries grafted on, one merged
archive the unified file's menu key names, and the points the armory's callouts point at.

Why (docs/unified-model-re.md §3.1): the game draws a mech's weapons and a jet's stores as models of their own, hung on
the body's bones by the battle code (vehicle_weapon_setting); the menu draws one model, the menu key's
(`menu_animation_model`), and nothing else. A loaded vehicle's preview showed the bare body. So the menu model is the
body's model with each weapon / store model grafted onto the bone the battle code hangs it on: a rigid model as a
rigid-mesh bone under that bone (pylib/mdb.py graft_rigid), a skinned one (the 504 missile pods) with its skin bones
under it (graft_skinned). Only the menu key points at the merged archive: the battle keys stay the battle file's,
byte for byte (tests/menu_graft_test.py).

  mech  every filled slot's weapon (pylib/mech_parts.py weapons_of: its row of vehicle_weapon_setting names the bone)
        on that bone, the weapon's own model (its SGO's animation_model) placed as the battle code places it: the
        model's frame on the bone's.
  jet   one store model a pylon (vcobjects STORES[kind].weapon.look, scaled to AmmoSize as it flies), under the row bone
        the store hangs from (hp_store, or `body` on a jet without one). The battle code launches its rounds from
        that one bone, so where a pylon hangs is a menu choice: the first of an odd count under the centre line, the
        rest in mirrored pairs from the root of the wing outwards (PYLON_SPREAD of the half span), each one's top on
        the airframe's underside there (pylib/jet_hardpoints.py airframe: the landing gear left out), at the holder's
        z. A jet whose airframe the line misses keeps the holder's height.
  tank  nothing: its rounds leave from the main gun (support_loadout.tank_sgo), so it has no pylon to show.

The callout points (model space, bind pose: the plugin projects them through the placed preview's matrix,
src/menu_callouts.h) go to a small text file beside the archive: `weapon<slot>` for the four slots (the weapon's
middle, an empty slot's row bone), `frame` and `core` for a mech, `pylon<i>` for a jet's pylons.

Pure Python (no numpy): the slim loadout generator runs it.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Callable

import graft_pure as g
import jet_gear
import jet_hardpoints
from mdb import (Mdb, Rab, RabFile, bind_world, bone_named, cmpl_compress, graft_rigid, graft_skinned, insert_member,
                 mdb_read, mdb_write, mmul, rab_read, rab_write)
from mdb_jet import pack_vertex, vertex_table

Read = Callable[[str, str], 'bytes | None']   # (folder, name) -> the file as the game loads it (None: nowhere)
Vec3 = g.Vec3

HEADER = '# EDF6VC callouts 1'
CALLOUTS_SUFFIX = '_CALLOUTS.TXT'
MECH_SLOT_ROWS = (2, 3, 4, 5)       # an empty slot's callout: its arm / shoulder row bone (mech_parts.row_of)
FRAME_BONES = ('koshi',)            # the mech's waist: the frame callout
CORE_BONES = ('body', 'mune')       # the mech's chest (the 608 has `mune`, no `body`): the core callout
PYLON_SPREAD = (0.15, 0.75)         # inner and outer pylon pair, fractions of the airframe's half span
PYLON_GAP = 0.05                    # m between a store's top and the skin it hangs under
PYLON_SEARCH = (0.0, 0.25, -0.25, 0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 2.0, -2.0)   # z offsets tried for the underside


class GraftError(ValueError):
    pass


def _req(ok: bool, what: str) -> None:
    if not ok:
        raise GraftError(what)


# ------------------------------------------------------------------------------------------------ names

def stem_of(file: str) -> str:
    """'EDF6VC_LO_MECH_….SGO' -> 'EDF6VC_LO_MECH_…'."""
    _req(file.upper().endswith('.SGO'), f'{file} 不是 .SGO')
    return file[:-4].upper()


def archive_rel(file: str) -> str:
    return f'OBJECT/{stem_of(file)}.MRAB'


def callouts_rel(file: str) -> str:
    return f'OBJECT/{stem_of(file)}{CALLOUTS_SUFFIX}'


def menu_model_ref(file: str) -> list[str]:
    """The menu key's [archive, member] of `file`'s merged model."""
    s = stem_of(file).lower()
    return [f'app:/object/{s}.mrab', f'{s}.mdb']


def app_ref(path: str) -> tuple[str, str]:
    """'app:/Weapon/x.rab' -> ('WEAPON', 'X.RAB')."""
    head = 'app:/'
    _req(path.lower().startswith(head) and path.count('/') == 2, f'{path!r} 不是 app:/<目录>/<文件>')
    folder, name = path[len(head):].split('/')
    return folder.upper(), name.upper()


# ------------------------------------------------------------------------------------------------ archives

@dataclass
class Model:
    rab: Rab
    member: str
    md: Mdb


def _model(read: Read, ref: list) -> Model:
    """[archive, member] read as the game loads it."""
    _req(isinstance(ref, list) and len(ref) >= 2 and all(isinstance(x, str) for x in ref[:2]),
         f'animation_model[0] 不是 [archive, model]：{ref!r}')
    raw = read(*app_ref(ref[0]))
    _req(raw is not None, f'{ref[0]} 不存在')
    rab = rab_read(raw)
    f = next((x for x in rab.files if x.name.lower() == ref[1].lower()), None)
    _req(f is not None, f'{ref[0]} 没有 {ref[1]}')
    return Model(rab, f.name, mdb_read(f.data))


def _add_textures(host: Rab, donor: Model) -> None:
    """The donor's textures (and their .lod variants) as members of `host`: one `host` has already must be the same
    bytes (the 504's weapons share theirs), never two textures of one name."""
    have = {f.name.lower(): f for f in host.files}
    for t in donor.md.textures:
        found = g.texture_members(donor.rab, t.filename)
        _req(bool(found), f'{donor.member}: 纹理 {t.filename} 不在它的档案里')
        for f in found:
            old = have.get(f.name.lower())
            if old is not None:
                _req(old.data == f.data, f'同名纹理 {f.name} 内容不同：合并后会绑错')
    g.copy_texture_members(host, donor.rab, [t.filename for t in donor.md.textures])


def _host(read: Read, ref: list) -> Model:
    """The body's archive cut to its one model and the textures that model names (prune_members): the donors'
    textures are added to it after, the model member replaced (_files)."""
    body = _model(read, ref)
    rab = Rab(body.rab.version, list(body.rab.folders), list(body.rab.files))
    g.prune_members(rab, [body.member])
    return Model(rab, body.member, body.md)


def _files(file: str, host: Model, points: dict[str, Vec3]) -> dict[str, bytes]:
    """{Mods path: bytes}: the merged archive (the host's model member replaced by its grafted model, named as the menu
    key names it) and the callouts."""
    rab = host.rab
    model = next(f for f in rab.files if f.name.lower() == host.member.lower())
    rab.files.remove(model)
    insert_member(rab, RabFile(menu_model_ref(file)[1], model.folder, model.flag, cmpl_compress(mdb_write(host.md)),
                               model.unk))
    return {archive_rel(file): rab_write(rab), callouts_rel(file): callouts_text(points)}


# ------------------------------------------------------------------------------------------------ geometry

def model_points(md: Mdb) -> list[Vec3]:
    """Every vertex of `md` in model space, bind pose."""
    return jet_gear.model_tris(md)[0]


def _box(points: list[Vec3]) -> tuple[Vec3, Vec3]:
    _req(bool(points), 'a model without vertices')
    lo = tuple(min(p[i] for p in points) for i in range(3))
    hi = tuple(max(p[i] for p in points) for i in range(3))
    return lo, hi   # type: ignore[return-value]


def _mid(points: list[Vec3]) -> Vec3:
    lo, hi = _box(points)
    return tuple((a + b) / 2 for a, b in zip(lo, hi))   # type: ignore[return-value]


def _origin(m: list[float]) -> Vec3:
    return (m[12], m[13], m[14])


def _rigid(donor: Mdb) -> int:
    rigid = [b.index for b in donor.bones if b.kind == 1]
    return rigid[0] if len(rigid) == 1 and len(donor.objects) == 1 and donor.objects[0].bone == rigid[0] else -1


def _skinned(donor: Mdb) -> bool:
    return all(me.flags[1] for o in donor.objects for me in o.meshes)


def graft(md: Mdb, donor: Mdb, parent: str, name: str) -> None:
    """`donor` on bone `parent` of `md`, its model frame on the bone's (what the battle code does with a weapon's
    model): rigid as one rigid-mesh bone (its local the donor's bone's bind world: identity in every stock weapon),
    skinned with its bones (prefix `name`)."""
    k = _rigid(donor)
    if k >= 0:
        at = graft_rigid(md, donor, parent, name)
        md.bones[at].local = list(bind_world(donor)[k])
        return
    _req(_skinned(donor), f'{name}: 既不是单个刚体也不是蒙皮模型')
    graft_skinned(md, donor, parent, name)


# ------------------------------------------------------------------------------------------------ mech

def weapon_model(read: Read, weapon: str) -> Model:
    """A weapon SGO's own model (its animation_model, either container)."""
    import menu_model
    data = read(*app_ref(weapon))
    _req(data is not None, f'{weapon} 不存在')
    am = menu_model.members_of(data).get('animation_model')
    _req(isinstance(am, list) and am, f'{weapon} 没有 animation_model')
    return _model(read, am[0])


def mech(read: Read, file: str, members: dict, slots: list[tuple[int, int, str]]) -> dict[str, bytes]:
    """`file`'s merged archive and callouts: its body (members' animation_model) with each `slots` entry (slot, row,
    weapon SGO path) grafted on that row's bone. {Mods path: bytes}."""
    rows = members.get('vehicle_weapon_setting')
    _req(isinstance(rows, list) and len(rows) >= max(MECH_SLOT_ROWS) + 1, 'vehicle_weapon_setting 不是机甲的 8 行')
    host = _host(read, members['animation_model'][0])
    md, rab = host.md, host.rab
    world0 = bind_world(md)
    names = [md.name_of(b.name) for b in md.bones]
    points: dict[str, Vec3] = {}
    for slot, row, weapon in slots:
        bone = rows[row][0]
        _req(bone in names, f'{file}: 机体没有挂点骨骼 {bone}')
        donor = weapon_model(read, weapon)
        _add_textures(rab, donor)
        graft(md, donor.md, bone, f'edf6vc_slot{slot}')
        points[f'weapon{slot}'] = g.xform(_mid(model_points(donor.md)), world0[names.index(bone)])
    for slot, row in enumerate(MECH_SLOT_ROWS):
        if f'weapon{slot}' not in points:
            bone = rows[row][0]
            _req(bone in names, f'{file}: 机体没有挂点骨骼 {bone}')
            points[f'weapon{slot}'] = _origin(world0[names.index(bone)])
    for tag, bones in (('frame', FRAME_BONES), ('core', CORE_BONES)):
        bone = next((b for b in bones if b in names), None)
        _req(bone is not None, f'{file}: 机体没有 {"/".join(bones)} 骨骼')
        points[tag] = _origin(world0[names.index(bone)])
    return _files(file, host, points)


# ------------------------------------------------------------------------------------------------ jet

def store_model(read: Read, look_model: str, size: float) -> Model:
    """A store's round model (WEAPON/<model>.rab, one rigid object) baked to AmmoSize `size` in model space: its
    middle on x / z, its top at y 0 (hung from there)."""
    raw = read('WEAPON', f'{look_model.upper()}.RAB')
    _req(raw is not None, f'WEAPON/{look_model}.rab 不存在')
    rab = rab_read(raw)
    f = next((x for x in rab.files if x.name.lower().endswith('.mdb')), None)
    _req(f is not None, f'{look_model}.rab 没有模型')
    md = mdb_read(f.data)
    k = _rigid(md)
    _req(k >= 0, f'{look_model}: 不是单个刚体模型')
    w = bind_world(md)[k]
    pts = [p for me in md.objects[0].meshes for p in g.mesh_positions(me)]
    lo, hi = _box([tuple(c * size for c in g.xform(p, w)) for p in pts])
    shift = (-(lo[0] + hi[0]) / 2, -hi[1], -(lo[2] + hi[2]) / 2)
    meshes = []
    for me in md.objects[0].meshes:
        keys, rows = vertex_table(me)
        pi = next(i for i, e in enumerate(me.elems) if e.name.lower() == 'position' and e.channel == 0)
        out = bytearray()
        for r in rows:
            p = g.xform(r[pi], w)
            q = tuple(p[c] * size + shift[c] for c in range(3))
            r = list(r)
            r[pi] = q + tuple(r[pi][3:])
            out += pack_vertex(me.elems, me.vsize, r)
        meshes.append(replace(me, vdata=bytes(out)))
    bones = [replace(b) for b in md.bones]
    bones[k] = replace(bones[k], local=list(g.translation((0.0, 0.0, 0.0))))
    for i, b in enumerate(bones):   # every bone at the origin: the baked vertices are model space
        if b.index != k:
            bones[i] = replace(b, local=list(g.translation((0.0, 0.0, 0.0))))
    md = replace(md, bones=bones, objects=[replace(md.objects[0], meshes=meshes)])
    return Model(rab, f.name, md)


def pylon_xs(n: int, half_span: float) -> list[float]:
    """Each pylon's x: odd n the first on the centre line, then mirrored pairs (+x, then -x) from the inner one out."""
    out = [0.0] if n % 2 else []
    pairs = n // 2
    lo, hi = PYLON_SPREAD
    for j in range(pairs):
        f = (lo + hi) / 2 if pairs == 1 else lo + (hi - lo) * j / (pairs - 1)
        out += [f * half_span, -f * half_span]
    return out


def jet(read: Read, file: str, members: dict, stores: list[tuple[int, str]]) -> dict[str, bytes]:
    """`file`'s merged archive and callouts: its body with one store model a pylon (`stores`: (row, kind) in pylon
    order), see the module."""
    import vcobjects as vc
    rows = members.get('vehicle_weapon_setting')
    _req(isinstance(rows, list), 'vehicle_weapon_setting 不是列表')
    host = _host(read, members['animation_model'][0])
    md, rab = host.md, host.rab
    world0 = bind_world(md)
    names = [md.name_of(b.name) for b in md.bones]
    P, T = jet_hardpoints.airframe(md, False)
    _req(bool(T), f'{file}: 机体没有三角形')
    half_span = max(abs(p[0]) for t in T for i in t for p in (P[i],))
    lines = jet_hardpoints._Lines(P, T)
    xs = pylon_xs(len(stores), half_span)
    points: dict[str, Vec3] = {}
    models: dict[str, Model] = {}
    for i, ((row, kind), x) in enumerate(zip(stores, xs)):
        bone = rows[row][0]
        _req(bone in names, f'{file}: 机体没有挂点骨骼 {bone}')
        look = vc.STORES[kind].weapon.look
        key = f'{look.model}@{look.size}'
        if key not in models:
            models[key] = store_model(read, look.model, look.size)
        donor = models[key]
        parent = world0[names.index(bone)]
        z = parent[14]
        y = None
        for dz in PYLON_SEARCH:
            y = lines.underside(x, z + dz)
            if y is not None:
                break
        top = parent[13] if y is None else y - PYLON_GAP
        _add_textures(rab, donor)
        at = graft_rigid(md, donor.md, bone, f'edf6vc_pylon{i}')
        hang = g.translation((x, top, z))
        md.bones[at].local = mmul(hang, g.inverse_affine_general(parent))
        lo, hi = _box(model_points(donor.md))
        md.bones[at].half = [(b - a) / 2 for a, b in zip(lo, hi)] + [1.0]
        md.bones[at].centre = [(a + b) / 2 for a, b in zip(lo, hi)] + [1.0]
        points[f'pylon{i}'] = (x, top + (lo[1] + hi[1]) / 2, z + (lo[2] + hi[2]) / 2)
    return _files(file, host, points)


# ------------------------------------------------------------------------------------------------ callouts

def callouts_text(points: dict[str, Vec3]) -> bytes:
    lines = [HEADER] + [f'{k} {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}' for k, p in points.items()]
    return ('\r\n'.join(lines) + '\r\n').encode('ascii')


def parse_callouts(data: bytes) -> dict[str, Vec3]:
    """callouts_text back (the plugin's parser's rules: src/menu_callouts.h)."""
    lines = data.decode('ascii').splitlines()
    _req(bool(lines) and lines[0] == HEADER, '不是 callouts 文件')
    out: dict[str, Vec3] = {}
    for ln in lines[1:]:
        if ln.strip():
            tag, x, y, z = ln.split()
            out[tag] = (float(x), float(y), float(z))
    return out
