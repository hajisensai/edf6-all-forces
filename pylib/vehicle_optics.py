"""Optical eye markers measured from authored lenses or native gunner cameras.

No generated position is a muzzle offset. The source faces below were inspected
with the model's albedo texture; hashes guard those exact mesh selections. Markers
are transform-only bones. Existing bones and skin indices are reordered together
into DFS order, as the native partial subtree updater requires.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
from pathlib import Path
import struct

import dsgo
import sgo
from mdb import Bone, Mdb, bind_world, ident, inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write, read_elem, cmpl_compress
from mdb_jet import link


@dataclass(frozen=True)
class Lens:
    marker: str
    bone: str
    material: str = ''
    faces: tuple[int, ...] = ()
    camera: str = ''


@dataclass(frozen=True)
class Model:
    stem: str
    digest: str
    lenses: tuple[Lens, ...]


MODELS = (
    Model('V505_TANK', '672fa72b16a723acd1ac87fb8e8473d10c7e2be9036f6013ff7f1eb3aa6d932f',
          (Lens('vc_optic_00', 'cannon_main', 'Light02'),)),
    Model('V601_TANK', '40b34d84fe525dc29553132e02afcdd5af7a860f300a24fe526a12a8269a8935',
          (Lens('vc_optic_00', 'cannon_main', 'v601_body', tuple(range(475, 483))),)),
    Model('VEHICLE403_TANK', '6de3f5b56bc7acb0d80b2d05a395d9ab4970a931f9966f69cb511055a9cf1baa',
          (Lens('vc_optic_01', 'MachineGun_A_aim', camera='カメラ２'),
           Lens('vc_optic_02', 'MachineGun_B_aim', camera='カメラ３'))),
    Model('VEHICLE404_BIGTANK', '23cfe876e973a16b46a41e40eb8c78eac363f1593af2f5d385545c270a055753',
          (Lens('vc_optic_00', 'cannon_aim', 'barbette8', (768, 769)),
           Lens('vc_optic_01', 'subCannon_A_roll', 'barbette8', (1220, 1221, 1222, 1223)),
           Lens('vc_optic_02', 'subCannon_B_roll', 'barbette8', (3100, 3101, 3102, 3103)))),
)


def output(spec: Model) -> str:
    return 'EDF6VC_OPTIC_' + spec.stem + '.MRAB'


def unit(v):
    size = sum(x*x for x in v) ** .5
    if size < 1e-10:
        raise ValueError('degenerate optic surface')
    return tuple(x/size for x in v)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def elem(mesh, name):
    e = next((e for e in mesh.elems if e.name.lower() == name.lower() and e.channel == 0), None)
    return read_elem(mesh, e.name, e.channel) if e else None


def surface(md: Mdb, lens: Lens):
    """Area-weighted model-space centroid and outward normal of the real glass."""
    bone = md.bone_index(lens.bone)
    candidates = []
    for obj in md.objects:
        for mesh in obj.meshes:
            if md.name_of(md.materials[mesh.material].name) != lens.material:
                continue
            positions, indices, weights = elem(mesh, 'position'), elem(mesh, 'BLENDINDICES'), elem(mesh, 'BLENDWEIGHT')
            triangles = list(struct.iter_unpack('<3H', mesh.indices))
            chosen = []
            for ti, tri in enumerate(triangles):
                if lens.faces and ti not in lens.faces:
                    continue
                if not indices or not weights or not all(indices[j][0] == bone and abs(weights[j][0]-1) < 1e-6 for j in tri):
                    if lens.faces and ti in lens.faces:
                        raise ValueError('optic surface skin owner changed')
                    continue
                p = [tuple(positions[j][:3]) for j in tri]
                normal = cross(tuple(p[1][k]-p[0][k] for k in range(3)), tuple(p[2][k]-p[0][k] for k in range(3)))
                area = sum(x*x for x in normal) ** .5 / 2
                if area > 1e-10:
                    chosen.append((ti, p, normal, area))
            if lens.faces:
                if {t[0] for t in chosen} == set(lens.faces):
                    candidates.append(chosen)
                continue
            # 505: the largest forward-facing connected Light02 glass surface
            # on cannon_main is the gunner's rectangular front periscope pane.
            remaining = {i for i, t in enumerate(chosen) if unit(t[2])[2] > .98}
            while remaining:
                group = {remaining.pop()}
                points = {p for i in group for p in chosen[i][1]}
                while True:
                    more = {i for i in remaining if any(p in points for p in chosen[i][1])}
                    if not more:
                        break
                    group |= more
                    remaining -= more
                    points |= {p for i in more for p in chosen[i][1]}
                candidates.append([chosen[i] for i in group])
    if not candidates:
        raise ValueError('authored optic glass was not found: ' + lens.bone)
    faces = max(candidates, key=lambda group: sum(t[3] for t in group))
    total = sum(t[3] for t in faces)
    centre = tuple(sum(sum(p[k] for p in t[1])/3*t[3] for t in faces)/total for k in range(3))
    normal = unit(tuple(sum(t[2][k] for t in faces) for k in range(3)))
    if normal[2] < .95 or total < .005:
        raise ValueError('optic surface no longer faces forward or is too small')
    return centre, normal


def plain(node):
    if isinstance(node, dsgo.Node):
        return {n: plain(node.items[i]) for i, n in node.names.items()} if node.names else [plain(x) for x in node.items]
    if isinstance(node, dsgo.Blob):
        return node.data
    return node


def camera_local(data: bytes, lens: Lens) -> list[float]:
    m = plain(dsgo.parse(data).root) if data[:4] == b'DSGO' else sgo.read(data)[1]
    b = m['animation_model'][2]
    table, end = struct.unpack_from('<2I', b, 0x14)
    def text(at):
        stop = at
        while b[stop:stop+2] != b'\0\0':
            stop += 2
        return b[at:stop].decode('utf-16-le')
    for r in range(table+0x20, end, 0x20):
        if text(r+struct.unpack_from('<i', b, r)[0]) != lens.camera:
            continue
        if text(r+struct.unpack_from('<i', b, r+4)[0]) != lens.bone:
            raise ValueError('native gunner camera changed parent')
        matrix = ident()
        matrix[12:15] = struct.unpack_from('<3f', b, r+struct.unpack_from('<i', b, r+12)[0])
        return matrix
    raise ValueError('native gunner eye locator missing')


def preorder(md: Mdb) -> Mdb:
    """Reindex every bone reference, preserving all original geometry/skin semantics."""
    children = {i: [] for i in range(-1, len(md.bones))}
    for b in md.bones:
        children[b.parent].append(b.index)
    order = []
    def walk(i):
        order.append(i)
        for child in children[i]:
            walk(child)
    for root in children[-1]:
        walk(root)
    if len(order) != len(md.bones) or len(set(order)) != len(order):
        raise ValueError('invalid optic skeleton hierarchy')
    remap = {old: new for new, old in enumerate(order)}
    bones = [replace(md.bones[old], index=i, parent=remap.get(md.bones[old].parent, -1)) for i, old in enumerate(order)]
    link(bones)
    objects = []
    for obj in md.objects:
        meshes = []
        for mesh in obj.meshes:
            data = bytearray(mesh.vdata)
            for e in mesh.elems:
                if e.name.upper() != 'BLENDINDICES':
                    continue
                if e.fmt != 21 or len(bones) > 256:
                    raise ValueError('unsupported skin index format')
                for v in range(mesh.nverts):
                    at = v*mesh.vsize+e.offset
                    data[at:at+4] = bytes(remap[i] for i in data[at:at+4])
            meshes.append(replace(mesh, vdata=bytes(data)))
        objects.append(replace(obj, bone=remap[obj.bone], meshes=meshes))
    return replace(md, bones=bones, objects=objects)


def mark_model(md: Mdb, spec: Model, sgo_data: bytes) -> Mdb:
    names, bones = list(md.names), list(md.bones)
    world = bind_world(md)
    for lens in spec.lenses:
        parent = md.bone_index(lens.bone)
        if parent < 0 or md.bone_index(lens.marker) >= 0:
            raise ValueError('unexpected source optic skeleton')
        if lens.camera:
            local = camera_local(sgo_data, lens)
            frame = mmul(local, world[parent])
        else:
            eye, forward = surface(md, lens)
            right = unit(cross(tuple(world[parent][4:7]), forward))
            up = cross(forward, right)
            frame = [*right, 0., *up, 0., *forward, 0., *eye, 1.]
            local = mmul(frame, inverse_affine(world[parent]))
        names.append(lens.marker)
        bones.append(Bone(len(bones), parent, -1, -1, len(names)-1, 0, 0, 0, 0, 0, 0,
                          local, inverse_affine(frame), [0.,0.,0.,1.], [0.,0.,0.,1.]))
    return preorder(replace(md, names=names, bones=bones))


def build_model(game, spec: Model) -> bytes:
    """Preserve loose texture changes only when the model geometry contract matches."""
    name = spec.stem + '.MRAB'
    loose = Path(game.root, 'Mods', 'OBJECT', name)
    arc = rab_read(loose.read_bytes() if loose.is_file() else game.read('OBJECT', name))
    model = next(f for f in arc.files if f.name.lower() == spec.stem.lower()+'.mdb')
    raw = model.data
    if hashlib.sha256(raw).hexdigest() != spec.digest:
        raise ValueError(f'{name}: model geometry differs from the audited optic profile; preserving the other mod')
    marked = mark_model(mdb_read(raw), spec, game.read('OBJECT', spec.stem+'.SGO'))
    model.stored = cmpl_compress(mdb_write(marked))
    return rab_write(arc)


def redirect(data: bytes) -> tuple[bytes, tuple[str, ...]]:
    """Change only an audited model path; other-mod/custom models have no fallback."""
    doc = dsgo.parse(data) if data[:4] == b'DSGO' else None
    version, m = sgo.read(data) if doc is None else (None, None)
    try:
        animation = doc.root.get('animation_model').items if doc else m['animation_model']
        ref = animation[0].items if doc else animation[0]
    except (KeyError, TypeError, AttributeError):
        return data, ()
    for spec in MODELS:
        if (ref[0].lower() in (f'app:/object/{spec.stem.lower()}.mrab', f'app:/object/{output(spec).lower()}')
                and ref[1].lower() == spec.stem.lower()+'.mdb'):
            ref[0] = 'app:/object/' + output(spec).lower()
            return (dsgo.write(doc) if doc else sgo.write(version, m)), ('OBJECT/'+output(spec),)
    return data, ()
