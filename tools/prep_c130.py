"""Developer-only: the support gunship's and the paratroop plane's models (docs/feature-support-jet-models.md §6) out of
FlightGear's Lockheed C-130 Hercules (FGAddon trunk/Aircraft/C130, GPL v2: its COPYING) into the model folder the
installer builds them from (pylib/c130_model.py):

    <models>/c130/<model>.obj / .mtl     one model in game space and metres (+Y up, +Z the nose, +X its left), one OBJ
                                         object per bone (c130.json), every triangle rigid on it:
                                           gunship    the AC-130: the C-130 with the FlightGear AC-130's guns on its left
                                                      side (two GAU-12 25 mm gatlings forward, the 40 mm Bofors aft of
                                                      the wing), as its ac130.xml places them
                                           transport  the C-130 as it is
    <models>/c130/<material>_df.png      its colour (the source's texture, as it is)
    <models>/c130/<material>_nm.png      a flat normal map (the source has none)
    <models>/c130/<material>_rml.png     R roughness, G metal, B 0 (no lights)
    <models>/c130/c130.json              every model's bones (pivot, parent, frame), materials and size
    <models>/c130/SOURCE.txt             where it came from

    python tools/prep_c130.py <FGAddon C130 folder> [<models folder>]

The source folder is the aircraft's FGAddon folder as svn holds it (Models/c130.ac, Models/texture.png,
Models/Propellers/propeller.ac + .png, Models/Interior/ac130/armament/{40,gau12}/*.ac + .png and their placing XML);
https://svn.code.sf.net/p/flightgear/fgaddon/trunk/Aircraft/C130 (downloaded 2026-11-10 into
D:/APP/edf6-models/downloads/flightgear_c130). Read as data (pylib/ac3d.py): nothing in it runs.

Flying, not parked: the landing gear (wheels, struts, the nose leg's linkage) is left out, its doors kept shut as the
source has them; the HDR copies of the windows are left out (the plain ones are kept, opaque dark glass); the
propellers are the source's blades and spinner, standing (the plugin turns no propeller; a bone each, c130_prop_<i>,
for a later spin). The FlightGear frame: x aft, y right, z up; its .ac files x aft, y up, z left (FlightGear reads an
.ac as (x, -z, y)); a part's placing XML offsets (x-m, y-m, z-m, pitch-deg, heading-deg) are in the FlightGear frame,
pitch about y then heading about z (right-handed: heading -90 turns the Bofors' barrel, +x, to the left, -y).
Game space from the .ac: (x, y, z)_game = (z, y, -x)_ac (a rotation: windings kept).
"""
from __future__ import annotations

import json
import math
import os
import shutil
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ac3d  # noqa: E402

Vec3 = tuple[float, float, float]
DEV_MODELS = r'D:\APP\edf6-models'
OUT_SUBDIR = 'c130'
MANIFEST = 'c130.json'
SOURCE_URL = 'https://svn.code.sf.net/p/flightgear/fgaddon/trunk/Aircraft/C130'
MODELS = ('gunship', 'transport')
# The airframe's objects left out (Models/c130.xml names them): the gear, the windows' HDR copies.
GEAR = ('roue', 'temp', 'articule', 'rotationH')
LEFT_OUT = ('HDRvitres', 'HDRhublotBD', 'HDRhublotBG')
GLASS = ('vitres', 'hublotBD', 'hublotBG')     # the Transparent material's: opaque dark glass here
PROP_PARTS = ('bol', 'pal1', 'pal2', 'pal3', 'pal4')   # the spinner and the four blades (not the blur discs)
CREASE = 30.0   # degrees: the source's crease (every object's): normals smoothed across edges under it
# Materials: (texture file in the source, roughness 0..255, metal 0..255); glass is a solid colour.
MATERIALS = {'c130': ('Models/texture.png', 150, 0), 'prop': ('Models/Propellers/propeller.png', 120, 40),
             'bofor': ('Models/Interior/ac130/armament/40/bofor.png', 110, 120),
             'gau12': ('Models/Interior/ac130/armament/gau12/gau12.png', 110, 120)}
GLASS_COLOUR = (38, 46, 56)
GLASS_RML = (40, 0, 0)
ARMAMENT = 'Models/Interior/ac130/armament'


class PrepError(Exception):
    pass


def to_game(p: Vec3) -> Vec3:
    return (p[2], p[1], -p[0])


def fg_to_ac(p: Vec3) -> Vec3:
    """A FlightGear-frame point (x aft, y right, z up) in the .ac frame (x aft, y up, z left)."""
    return (p[0], p[2], -p[1])


def ac_to_fg(p: Vec3) -> Vec3:
    return (p[0], -p[2], p[1])


def _rot(v: Vec3, axis: int, deg: float) -> Vec3:
    """Right-handed rotation of `v` about axis 0 / 1 / 2 by `deg`."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    x, y, z = v
    if axis == 1:
        return (c * x + s * z, y, -s * x + c * z)
    if axis == 2:
        return (c * x - s * y, s * x + c * y, z)
    return (x, c * y - s * z, s * y + c * z)


def placed(obj: ac3d.AcObject, offset: dict[str, float]) -> ac3d.AcObject:
    """`obj` (a part's .ac) placed on the airframe by its XML offsets (FlightGear frame: pitch, heading, then x / y / z)."""
    out = []
    for v in obj.verts:
        p = ac_to_fg(v)
        p = _rot(p, 1, offset.get('pitch-deg', 0.0))
        p = _rot(p, 2, offset.get('heading-deg', 0.0))
        p = (p[0] + offset.get('x-m', 0.0), p[1] + offset.get('y-m', 0.0), p[2] + offset.get('z-m', 0.0))
        out.append(fg_to_ac(p))
    return ac3d.AcObject(obj.name, obj.kind, obj.texture, out, obj.surfs, obj.texrep, obj.texoff)


def offsets(xml_path: str) -> list[tuple[str, dict[str, float]]]:
    """(model path, offsets) of every <model> in a FlightGear placing XML."""
    root = ET.parse(xml_path).getroot()
    out = []
    for m in root.findall('model'):
        off = {}
        o = m.find('offsets')
        if o is not None:
            for k in ('x-m', 'y-m', 'z-m', 'pitch-deg', 'heading-deg', 'roll-deg'):
                e = o.find(k)
                if e is not None and e.text:
                    off[k] = float(e.text)
        if off.get('roll-deg', 0.0):
            raise PrepError(f'{xml_path}: a roll offset (not handled)')
        out.append((m.findtext('path', '').strip(), off))
    return out


def model_ac(xml_path: str) -> str:
    """The .ac a part's own XML (<path>) names."""
    p = ET.parse(xml_path).getroot().findtext('path', '').strip()
    if not p.endswith('.ac'):
        raise PrepError(f'{xml_path}: no .ac path')
    return os.path.join(os.path.dirname(xml_path), p)


# ------------------------------------------------------------------------------------------ the pieces

class Piece:
    """One OBJ object's share of one material: polygons of (position, uv, normal) corners in game space."""

    def __init__(self, bone: str, material: str) -> None:
        self.bone, self.material = bone, material
        self.polys: list[list[tuple[Vec3, tuple[float, float], Vec3]]] = []


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(a: Vec3) -> Vec3:
    n = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-12 else (0.0, 0.0, 0.0)


def _face_normal(pts: list[Vec3]) -> Vec3:
    """Newell's normal of a polygon (counter-clockwise front)."""
    n = [0.0, 0.0, 0.0]
    for i, a in enumerate(pts):
        b = pts[(i + 1) % len(pts)]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return _unit((n[0], n[1], n[2]))


def add_object(piece: Piece, obj: ac3d.AcObject) -> int:
    """`obj`'s polygons into `piece` (game space), normals smoothed across edges under CREASE degrees (the source's); a
    two-sided surface twice, the second reversed. Returns the polygons added."""
    pts_game = [to_game(v) for v in obj.verts]
    faces = []
    for s in obj.surfs:
        if s.kind != 0 or len(s.refs) < 3:
            continue
        idx = [r[0] for r in s.refs]
        if len(set(idx)) < 3:
            continue
        n = _face_normal([pts_game[i] for i in idx])
        if n == (0.0, 0.0, 0.0):
            continue
        uvs = [(r[1] * obj.texrep[0] + obj.texoff[0], r[2] * obj.texrep[1] + obj.texoff[1]) for r in s.refs]
        faces.append((idx, uvs, n, s.two_sided))
    by_point: dict[tuple[int, int, int], list[Vec3]] = {}

    def key(p: Vec3) -> tuple[int, int, int]:
        return (round(p[0] * 1e4), round(p[1] * 1e4), round(p[2] * 1e4))
    for idx, _uv, n, _two in faces:
        for i in idx:
            by_point.setdefault(key(pts_game[i]), []).append(n)
    cos_crease = math.cos(math.radians(CREASE))
    added = 0
    for idx, uvs, n, two in faces:
        poly = []
        for i, uv in zip(idx, uvs):
            near = [m for m in by_point[key(pts_game[i])] if m[0] * n[0] + m[1] * n[1] + m[2] * n[2] >= cos_crease]
            s = (sum(m[0] for m in near), sum(m[1] for m in near), sum(m[2] for m in near))
            poly.append((pts_game[i], uv, _unit(s) if s != (0.0, 0.0, 0.0) else n))
        piece.polys.append(poly)
        added += 1
        if two:
            piece.polys.append([(p, uv, (-nn[0], -nn[1], -nn[2])) for p, uv, nn in reversed(poly)])
            added += 1
    return added


def _centre(objs: list[ac3d.AcObject]) -> Vec3:
    pts = [to_game(v) for o in objs for v in o.verts]
    return tuple((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2.0 for k in range(3))  # type: ignore[return-value]


def build_models(src: str) -> dict[str, dict]:
    """{model: {'pieces': [Piece], 'bones': {bone: {parent, pivot, frame?}}, 'materials': [...]}} in game space."""
    plane = ac3d.read_ac(os.path.join(src, 'Models', 'c130.ac'))
    names = {o.name for o in plane.objects}
    for want in ('fuselage', 'ailes', 'derive', 'moteurs', *GLASS, *LEFT_OUT):
        if want not in names:
            raise PrepError(f'c130.ac: no object {want} (not the FGAddon C-130 this was made for)')
    airframe = [o for o in plane.objects if o.name not in LEFT_OUT and not o.name.startswith(GEAR)]
    prop = ac3d.read_ac(os.path.join(src, 'Models', 'Propellers', 'propeller.ac'))
    blades = [o for o in prop.objects if o.name in PROP_PARTS]
    if len(blades) != len(PROP_PARTS):
        raise PrepError(f'propeller.ac: {sorted(o.name for o in blades)}, not {PROP_PARTS}')
    props = offsets(os.path.join(src, 'Models', 'Propellers', 'all-propellers.xml'))
    if len(props) != 4:
        raise PrepError(f'all-propellers.xml: {len(props)} propellers')
    # the AC-130's guns (Models/Interior/ac130.xml: armament/gau12/all-gau12.xml, armament/40/all-bofors.xml)
    guns: list[tuple[ac3d.AcObject, str]] = []
    for group, material in (('gau12', 'gau12'), ('40', 'bofor')):
        folder = os.path.join(src, *ARMAMENT.split('/'), group)
        xml = os.path.join(folder, 'all-bofors.xml' if group == '40' else 'all-gau12.xml')
        for path, off in offsets(xml):
            part = ac3d.read_ac(model_ac(os.path.join(folder, path)))
            guns += [(placed(o, off), material) for o in part.objects]
    if not any(o.name == 'barrel' for o, m in guns if m == 'bofor'):
        raise PrepError('the Bofors has no barrel')
    out = {}
    for model in MODELS:
        hull = f'{model}'
        pieces: dict[tuple[str, str], Piece] = {}

        def piece(bone: str, material: str) -> Piece:
            return pieces.setdefault((bone, material), Piece(bone, material))
        for o in airframe:
            add_object(piece(hull, 'glass' if o.name in GLASS else 'c130'), o)
        bones: dict[str, dict] = {hull: {'parent': None, 'pivot': [0.0, 0.0, 0.0]}}
        for i, (_path, off) in enumerate(props, 1):
            bone = f'prop_{i}'
            here = [placed(o, off) for o in blades]
            for o in here:
                add_object(piece(bone, 'prop'), o)
            hub = _centre([placed(o, off) for o in blades if o.name == 'bol'])
            bones[bone] = {'parent': hull, 'pivot': [round(x, 5) for x in hub]}
        if model == 'gunship':
            for o, material in guns:
                add_object(piece(hull, material), o)
        mats = sorted({m for _b, m in pieces})
        out[model] = {'pieces': list(pieces.values()), 'bones': bones, 'materials': mats}
    return out


def write_obj(folder: str, model: str, entry: dict) -> dict:
    """<model>.obj / .mtl; the bones' pivots and the size (game space, as written)."""
    lines = [f'# {model}: tools/prep_c130.py from FlightGear\'s C-130 ({SOURCE_URL}, GPL v2)', f'mtllib {model}.mtl']
    nv = nt = nn = 0
    pts = []
    for bone in entry['bones']:
        lines.append(f'o {bone}')
        for p in (q for q in entry['pieces'] if q.bone == bone):
            lines.append(f'usemtl {p.material}')
            for poly in p.polys:
                f = []
                for pos, uv, n in poly:
                    lines.append(f'v {pos[0]:.5f} {pos[1]:.5f} {pos[2]:.5f}')
                    lines.append(f'vt {uv[0]:.6f} {uv[1]:.6f}')
                    lines.append(f'vn {n[0]:.5f} {n[1]:.5f} {n[2]:.5f}')
                    pts.append(pos)
                    nv += 1
                    nt += 1
                    nn += 1
                    f.append(f'{nv}/{nt}/{nn}')
                lines.append('f ' + ' '.join(f))
    with open(os.path.join(folder, f'{model}.obj'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines) + '\n')
    mtl = []
    for m in entry['materials']:
        mtl += [f'newmtl {m}', f'map_Kd {m}_df.png', '']
    with open(os.path.join(folder, f'{model}.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(mtl))
    size = [round(max(p[k] for p in pts) - min(p[k] for p in pts), 2) for k in range(3)]
    return {'bones': entry['bones'], 'materials': entry['materials'], 'size': size,
            'polygons': sum(len(p.polys) for p in entry['pieces'])}


def write_textures(src: str, folder: str) -> None:
    from PIL import Image
    for name, (path, rough, metal) in MATERIALS.items():
        im = Image.open(os.path.join(src, *path.split('/'))).convert('RGB')
        im.save(os.path.join(folder, f'{name}_df.png'))
        Image.new('RGB', im.size, (128, 128, 255)).save(os.path.join(folder, f'{name}_nm.png'))
        Image.new('RGB', im.size, (rough, metal, 0)).save(os.path.join(folder, f'{name}_rml.png'))
    Image.new('RGB', (16, 16), GLASS_COLOUR).save(os.path.join(folder, 'glass_df.png'))
    Image.new('RGB', (16, 16), (128, 128, 255)).save(os.path.join(folder, 'glass_nm.png'))
    Image.new('RGB', (16, 16), GLASS_RML).save(os.path.join(folder, 'glass_rml.png'))


SOURCE_TEXT = f"""C-130 Hercules / AC-130 models used by EDF6VehicleCrew (free, non-commercial mod): the support gunship
(gunship.obj) and the paratroop plane (transport.obj).

Source: FlightGear's Lockheed C-130 Hercules (FGAddon, {SOURCE_URL}; downloaded 2026-11-10),
  Models/c130.ac and Models/texture.png (the airframe and its default livery),
  Models/Propellers/propeller.ac and propeller.png (the propellers, placed by all-propellers.xml),
  Models/Interior/ac130/armament/gau12 and armament/40 (the AC-130's GAU-12 gatlings and 40 mm Bofors, placed by their
  all-gau12.xml / all-bofors.xml).
Licence: GNU General Public License v2 (the aircraft's COPYING, copied here as COPYING).
Changes: the landing gear and the windows' HDR copies left out, the guns placed on the airframe, converted from AC3D to
OBJ in the game's axes (metres, +Y up, +Z the nose), flat normal maps and roughness maps added; converted to the game's
model format by the mod's installer, which adds its bones and collision.
Made by tools/prep_c130.py.
"""


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    src = argv[0]
    out_root = argv[1] if len(argv) > 1 else DEV_MODELS
    folder = os.path.join(out_root, OUT_SUBDIR)
    os.makedirs(folder, exist_ok=True)
    models = build_models(src)
    manifest = {'source': SOURCE_URL, 'models': {}}
    for model, entry in models.items():
        manifest['models'][model] = write_obj(folder, model, entry)
        print(model, json.dumps({k: v for k, v in manifest['models'][model].items() if k != 'bones'}))
    write_textures(src, folder)
    with open(os.path.join(folder, MANIFEST), 'w', encoding='utf-8', newline='\n') as h:
        json.dump(manifest, h, indent=1)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(SOURCE_TEXT)
    shutil.copyfile(os.path.join(src, 'COPYING'), os.path.join(folder, 'COPYING'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
