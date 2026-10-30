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

import cockpit_fits
import cockpit_kits
import cockpits
import dsgo
import goldcoat
import sgo
from mdb import Bone, Mdb, bind_world, ident, inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write, read_elem, cmpl_compress
from mdb_jet import link


# What the gunner looks through at a marker, by its name's prefix (src/optic_mount.h kFamilies, sightzoom.h Eyepiece;
# the user, 2026-10-30: "有些瞄具周围不应该是全黑的，有些是单眼看的瞄具"): a biocular eyepiece (both eyes in its cups:
# black round the field), a monocular telescope (one eye at its cup: the view round it dimmed, not black), an unmanned
# station's gun camera (its operator's monitor: no eyepiece). docs/feedback-2026-10-30-sights.md lists every lens's.
EYEPIECES = {'vc_optic_': 'binocular', 'vc_monoc_': 'monocular', 'vc_camera_': 'camera'}


def eyepiece(marker: str) -> str:
    """The eyepiece marker `marker` names (<prefix><nn>, nn 00..15: optic_mount.h kMaxAnchors)."""
    for prefix, kind in EYEPIECES.items():
        tail = marker[len(prefix):]
        if marker.startswith(prefix) and len(tail) == 2 and tail.isdigit():
            return kind
    raise ValueError(f'not an optic marker: {marker}')


@dataclass(frozen=True)
class Lens:
    marker: str   # its name says its eyepiece (EYEPIECES)
    bone: str
    material: str = ''
    faces: tuple[int, ...] = ()
    camera: str = ''
    # A window set at an angle to the bore (a turret dome's vision block, its pane on a facet of the dome): the
    # marker's origin is still the pane's centroid, its +z the parent bone's own +z (the turret's bore line), not the
    # pane's normal, which only has to face the front (within 37 degrees of it). The runtime reads only the origin.
    bore: bool = False


@dataclass(frozen=True)
class Model:
    stem: str
    digest: str
    lenses: tuple[Lens, ...]
    cockpits: tuple[cockpits.Fit, ...] = ()   # the seats' first-person cockpits (pylib/cockpit_fits.py)
    # Where the stock archive is: OBJECT/<stem>.MRAB for the vehicles; a stock gold weapon model (pylib/goldcoat.py
    # STOCK_WEAPONS) is WEAPON/<stem>.RAB. Its built copy is a private file of its own folder (output()).
    folder: str = 'OBJECT'
    ext: str = '.MRAB'


MODELS = (
    Model('V505_TANK', '672fa72b16a723acd1ac87fb8e8473d10c7e2be9036f6013ff7f1eb3aa6d932f',
          (Lens('vc_optic_00', 'cannon_main', 'Light02'),)),
    Model('V601_TANK', '40b34d84fe525dc29553132e02afcdd5af7a860f300a24fe526a12a8269a8935',
          (Lens('vc_optic_00', 'cannon_main', 'v601_body', tuple(range(475, 483))),)),
    # E551 (EDF6 preorder paint): the same turret geometry and Light02 periscope pane as V505_TANK.
    Model('V505_TANK_EDF6BENEFITS', '430b46f3686ab37283e4dabbb9a2a39741553db397184c314f572a7a2a65fe2f',
          (Lens('vc_optic_00', 'cannon_main', 'Light02'),)),
    # Blacker main gun: the left (+x, the gunner's side) vision block on the turret dome's roof edge, Material4
    # object/mesh 2/0 triangles 1586-1587 (its pane: the atlas's framed silver block at uv (0.23-0.28, 0.33-0.35)),
    # on the turret's yaw bone; the pane faces 30 degrees outboard of the bore, so the marker looks along the bore.
    Model('VEHICLE403_TANK', '6de3f5b56bc7acb0d80b2d05a395d9ab4970a931f9966f69cb511055a9cf1baa',
          (Lens('vc_optic_00', 'cannon_main', 'Material4', (1586, 1587), bore=True),
           # The side machine guns: unmanned stations (cockpit_fits remote_display), their native gun cameras.
           Lens('vc_camera_01', 'MachineGun_A_aim', camera='カメラ２'),
           Lens('vc_camera_02', 'MachineGun_B_aim', camera='カメラ３'))),
    Model('VEHICLE404_BIGTANK', '23cfe876e973a16b46a41e40eb8c78eac363f1593af2f5d385545c270a055753',
          (Lens('vc_optic_00', 'cannon_aim', 'barbette8', (768, 769)),
           # The sub-turrets: unmanned (cockpit_fits_ground remote_display), their sight glass a gun camera's window.
           Lens('vc_camera_01', 'subCannon_A_roll', 'barbette8', (1220, 1221, 1222, 1223)),
           Lens('vc_camera_02', 'subCannon_B_roll', 'barbette8', (3100, 3101, 3102, 3103)))),
    # Grape remote turret: the sight head's window on the turret roof (the cylinder left of the gun, its tinted pane
    # at uv (0.973-0.991, 0.522-0.555)), Material6 object/mesh 0/3 triangles 4905-4906, on the turret's yaw bone. The
    # turret is remote (no room in it: cockpit_fits_ground): the head is its camera, the driver sees its monitor.
    Model('VEHICLE401_STRIKER', 'f54ebc238331b9b357ee6b01ad74ec8f4d442695a3db66bfeea69ff5f81fe0f9',
          (Lens('vc_camera_00', 'Battery', 'Material6', (4905, 4906)),)),
)


# The stock models with cockpits: the optics' own, and those fitted with an audited digest (cockpit_fits DIGESTS). A
# generated model's stem (the plugin's own vehicles) has no digest: its generator grafts it (cockpits.graft_files).
MODELS = tuple(replace(m, cockpits=cockpit_fits.FITS.get(m.stem, ())) for m in MODELS) + tuple(
    Model(stem, cockpit_fits.DIGESTS[stem], (), fits) for stem, fits in sorted(cockpit_fits.FITS.items())
    if stem not in {m.stem for m in MODELS} and stem in cockpit_fits.DIGESTS) + tuple(
    # The stock gold weapons (the Depth Crawler gold's arm cannons and gatling): only their gold coat converted, in a
    # private archive their SGOs are redirected to like the vehicles' (new files; no stock file overwritten).
    Model(stem, digest, (), (), folder, ext) for (folder, stem, ext), digest in sorted(goldcoat.STOCK_WEAPONS.items()))


def ready(spec: Model) -> bool:
    """Whether `spec`'s model is built and its SGOs redirected now: it has optics, or its cockpits' assets are there
    (without them a cockpit-only model would be the stock one under another name), or it has a gold coat to convert
    (pylib/goldcoat.py: a stock gold vehicle EDF6 renders off-white is gold with or without its cockpits). MODELS
    stays whole for undoing the redirects an earlier install made."""
    return bool(spec.lenses) or goldcoat.covers(spec.stem) or (bool(spec.cockpits) and cockpits.assets_present())


def output(spec: Model) -> str:
    return ('EDF6VC_OPTIC_' if spec.folder == 'OBJECT' else 'EDF6VC_GOLD_') + spec.stem + spec.ext


def rel(spec: Model) -> str:
    """The built archive's Mods path, e.g. OBJECT/EDF6VC_OPTIC_V505_TANK.MRAB, WEAPON/EDF6VC_GOLD_<stem>.RAB."""
    return spec.folder + '/' + output(spec)


def app_path(spec: Model, built: bool) -> str:
    """The archive as an SGO names it (lower case): the stock one, or the built one."""
    return f'app:/{spec.folder.lower()}/{(output(spec) if built else spec.stem + spec.ext).lower()}'


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
    if normal[2] < (.8 if lens.bore else .95) or total < .005:
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


def add_marker(md: Mdb, parent_name: str, marker: str, frame: list[float]) -> Mdb:
    """`md` with a transform-only bone `marker` under `parent_name` whose model-space bind matrix is `frame` (rows: its
    +x, +y, +z, origin), appended (preorder() puts the skeleton back in DFS order)."""
    parent = md.bone_index(parent_name)
    if parent < 0 or md.bone_index(marker) >= 0:
        raise ValueError(f'unexpected source skeleton for marker {marker} under {parent_name}')
    local = mmul(frame, inverse_affine(bind_world(md)[parent]))
    names, bones = list(md.names) + [marker], list(md.bones)
    bones.append(Bone(len(bones), parent, -1, -1, len(names)-1, 0, 0, 0, 0, 0, 0,
                      local, inverse_affine(frame), [0.,0.,0.,1.], [0.,0.,0.,1.]))
    return replace(md, names=names, bones=bones)


def mark_model(md: Mdb, spec: Model, sgo_data: bytes) -> Mdb:
    world = bind_world(md)
    for lens in spec.lenses:
        eyepiece(lens.marker)   # a name the runtime reads (EYEPIECES), or the build stops here
        parent = md.bone_index(lens.bone)
        if parent < 0:
            raise ValueError('unexpected source optic skeleton')
        if lens.camera:
            frame = mmul(camera_local(sgo_data, lens), world[parent])
        else:
            eye, forward = surface(md, lens)
            if lens.bore:
                forward = unit(tuple(world[parent][8:11]))
            right = unit(cross(tuple(world[parent][4:7]), forward))
            up = cross(forward, right)
            frame = [*right, 0., *up, 0., *forward, 0., *eye, 1.]
        md = add_marker(md, lens.bone, lens.marker, frame)
    return preorder(md)


def build_model(game, spec: Model) -> bytes:
    """Preserve loose texture changes only when the model geometry contract matches."""
    name = spec.stem + spec.ext
    print(f'    {name}：读取模型资源……', flush=True)
    loose = Path(game.root, 'Mods', spec.folder, name)
    arc = rab_read(loose.read_bytes() if loose.is_file() else game.read(spec.folder, name))
    model = next(f for f in arc.files if f.name.lower() == spec.stem.lower()+'.mdb')
    raw = model.data
    if hashlib.sha256(raw).hexdigest() != spec.digest:
        raise ValueError(f'{name}: model geometry differs from the audited optic profile; preserving the other mod')
    # The SGO only for a camera lens's locator (a cockpit-only skin's model may have no SGO of its own name).
    sgo_data = game.read('OBJECT', spec.stem+'.SGO') if any(lens.camera for lens in spec.lenses) else b''
    print(f'    {name}：解析几何与生成瞄具骨骼……', flush=True)
    marked = mark_model(mdb_read(raw), spec, sgo_data)
    textures: dict[str, bytes] = {}
    if spec.cockpits:
        try:
            marked, textures = cockpits.graft(marked, spec.cockpits, cockpit_kits.KITS, game, preorder)
        except cockpits.CockpitAssetsMissing as e:   # the optics still; the seats keep the stock camera
            print(f'{name}: no cockpits ({e})')
    if goldcoat.covers(spec.stem):
        print(f'    {name}：金色涂装转为 EDF6 金属金材质……', flush=True)
        marked, notes = goldcoat.apply(arc, marked, model.name)
        for note in notes:
            print(f'    {note}')
    print(f'    {name}：压缩模型与打包贴图……', flush=True)
    model.stored = cmpl_compress(mdb_write(marked))
    cockpits.add_textures(arc, textures)
    return rab_write(arc)


def _model_reference(animation: object) -> list[object] | None:
    """The primary archive/model pair, including stock model-list wrappers."""
    items = animation.items if isinstance(animation, dsgo.Node) else animation
    if not isinstance(items, list) or not items:
        return None
    value = items[0]
    while True:
        ref = value.items if isinstance(value, dsgo.Node) else value
        if not isinstance(ref, list) or not ref:
            return None
        if len(ref) >= 2 and isinstance(ref[0], str) and isinstance(ref[1], str):
            return ref
        value = ref[0]


def redirect(data: bytes) -> tuple[bytes, tuple[str, ...]]:
    """Change only an audited model path; other-mod/custom models have no fallback. Only a ready() model is pointed
    at (an already redirected path is recognised for any)."""
    doc = dsgo.parse(data) if data[:4] == b'DSGO' else None
    version, m = sgo.read(data) if doc is None else (None, None)
    try:
        animation = doc.root.get('animation_model') if doc else m['animation_model']
    except (KeyError, TypeError, AttributeError):
        return data, ()
    ref = _model_reference(animation)
    if ref is None:
        return data, ()
    for spec in MODELS:
        if not ready(spec) and ref[0].lower() != app_path(spec, True):
            continue
        if ref[0].lower() in (app_path(spec, False), app_path(spec, True)) and ref[1].lower() == spec.stem.lower()+'.mdb':
            ref[0] = app_path(spec, True)
            return (dsgo.write(doc) if doc else sgo.write(version, m)), (rel(spec),)
    return data, ()


def model_path(data: bytes) -> str | None:
    doc = dsgo.parse(data) if data[:4] == b'DSGO' else None
    try:
        if doc:
            animation = doc.root.get('animation_model')
        else:
            animation = sgo.read(data)[1]['animation_model']
        ref = _model_reference(animation)
        return ref[0] if ref is not None else None
    except (KeyError, TypeError, AttributeError):
        return None


def restore_path(data: bytes, path: str | None = None) -> bytes:
    """Undo only our model redirect; preserve every current non-model field."""
    current = model_path(data)
    spec = next((s for s in MODELS if current and current.lower() == app_path(s, True)), None)
    if spec is None:
        return data
    path = path or app_path(spec, False)
    if data[:4] == b'DSGO':
        doc = dsgo.parse(data)
        _model_reference(doc.root.get('animation_model'))[0] = path
        return dsgo.write(doc)
    version, m = sgo.read(data)
    _model_reference(m['animation_model'])[0] = path
    return sgo.write(version, m)
