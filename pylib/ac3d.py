"""AC3D (.ac) models, pure Python: FlightGear's aircraft are in it (tools/prep_c130.py reads the C-130's).

    objs = read_ac(path)       every `poly` OBJECT, its world-space vertices (its and its parents' `rot` / `loc`
                               applied), surfaces (polygons: vertex indices and UVs, the material, the two-sided flag)
                               and texture; `group` objects only carry their transforms down

The format (inivis.com AC3D 'AC3Db'): a header line, MATERIAL lines, then a tree of OBJECT blocks, each with optional
name, data (a byte count, then that many bytes on the next lines), texture, texrep, texoff, rot (3 x 3, row-major: the
object's axes in its parent's), loc, crease, url, numvert (lines `x y z`), numsurf (each SURF flags, mat, refs n, then
n lines `index u v`) and `kids n` closing the block, its n children following. A vertex in its parent's space is
v x rot + loc (row vector: AC3D's loaders apply rot then loc). +Y is up; FlightGear's aircraft have +X aft and +Z
the left wing (its x, y, z = ac x, -ac z, ac y). The file is read as data: nothing in it runs.
"""
from __future__ import annotations

from dataclasses import dataclass, field

Vec3 = tuple[float, float, float]


class AcError(Exception):
    pass


@dataclass
class AcSurface:
    refs: list[tuple[int, float, float]]   # (vertex index, u, v); UV (0, 0) the texture's bottom-left
    mat: int
    flags: int

    @property
    def two_sided(self) -> bool:
        return bool(self.flags & 0x20)

    @property
    def kind(self) -> int:
        """0 a polygon, 1 a closed line, 2 a line (lines carry no faces)."""
        return self.flags & 0x0F


@dataclass
class AcObject:
    name: str
    kind: str                       # 'poly' / 'group' / 'world' / 'light'
    texture: str | None
    verts: list[Vec3]               # world space (every transform above applied)
    surfs: list[AcSurface] = field(default_factory=list)
    texrep: tuple[float, float] = (1.0, 1.0)
    texoff: tuple[float, float] = (0.0, 0.0)


@dataclass
class AcMaterial:
    name: str
    rgb: Vec3
    trans: float


@dataclass
class AcFile:
    materials: list[AcMaterial]
    objects: list[AcObject]

    def named(self, name: str) -> list[AcObject]:
        return [o for o in self.objects if o.name == name]


Mat3 = tuple[Vec3, Vec3, Vec3]
IDENTITY: Mat3 = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def _apply(v: Vec3, rot: Mat3, loc: Vec3) -> Vec3:
    return tuple(v[0] * rot[0][k] + v[1] * rot[1][k] + v[2] * rot[2][k] + loc[k] for k in range(3))  # type: ignore[return-value]


def _compose(inner_rot: Mat3, inner_loc: Vec3, outer_rot: Mat3, outer_loc: Vec3) -> tuple[Mat3, Vec3]:
    """The transform `inner` then `outer` (row vectors)."""
    rot = tuple(tuple(sum(inner_rot[i][j] * outer_rot[j][k] for j in range(3)) for k in range(3)) for i in range(3))
    return rot, _apply(inner_loc, outer_rot, outer_loc)  # type: ignore[return-value]


def _material(line: str) -> AcMaterial:
    q = line.split('"')
    if len(q) < 3:
        raise AcError(f'MATERIAL line: {line!r}')
    t = q[2].split()
    vals = {t[i]: t[i + 1:i + 4] for i in range(len(t)) if t[i] in ('rgb', 'amb', 'emis', 'spec', 'shi', 'trans')}
    rgb = tuple(float(x) for x in vals['rgb'][:3])
    return AcMaterial(q[1], rgb, float(vals.get('trans', ['0'])[0]))  # type: ignore[arg-type]


def read_ac(path: str) -> AcFile:
    with open(path, encoding='latin-1') as h:
        lines = h.read().split('\n')
    if not lines or not lines[0].startswith('AC3D'):
        raise AcError(f'{path}: not an AC3D file')
    i = 1
    materials: list[AcMaterial] = []
    while i < len(lines) and lines[i].startswith('MATERIAL'):
        materials.append(_material(lines[i]))
        i += 1
    objects: list[AcObject] = []

    def block(i: int, rot: Mat3, loc: Vec3) -> int:
        head = lines[i].split()
        if len(head) != 2 or head[0] != 'OBJECT':
            raise AcError(f'{path}:{i + 1}: OBJECT expected, {lines[i]!r}')
        kind = head[1]
        i += 1
        name, texture = '', None
        my_rot, my_loc = IDENTITY, (0.0, 0.0, 0.0)
        texrep, texoff = (1.0, 1.0), (0.0, 0.0)
        raw: list[Vec3] = []
        surfs: list[AcSurface] = []
        while True:
            if i >= len(lines):
                raise AcError(f'{path}: OBJECT {name!r} not closed by kids')
            t = lines[i].split()
            if not t:
                i += 1
                continue
            key = t[0]
            if key == 'name':
                name = lines[i].split('"')[1] if '"' in lines[i] else t[1]
                i += 1
            elif key == 'data':
                n = int(t[1])
                i += 1
                got = 0
                while got < n:   # its bytes, then the newline after them
                    got += len(lines[i]) + 1
                    i += 1
            elif key == 'texture':
                texture = lines[i].split('"')[1] if '"' in lines[i] else t[1]
                i += 1
            elif key == 'texrep':
                texrep = (float(t[1]), float(t[2]))
                i += 1
            elif key == 'texoff':
                texoff = (float(t[1]), float(t[2]))
                i += 1
            elif key == 'rot':
                r = [float(x) for x in t[1:10]]
                my_rot = ((r[0], r[1], r[2]), (r[3], r[4], r[5]), (r[6], r[7], r[8]))
                i += 1
            elif key == 'loc':
                my_loc = (float(t[1]), float(t[2]), float(t[3]))
                i += 1
            elif key in ('crease', 'url', 'subdiv', 'hidden', 'locked', 'folded'):
                i += 1
            elif key == 'numvert':
                n = int(t[1])
                for k in range(n):
                    x, y, z = lines[i + 1 + k].split()[:3]
                    raw.append((float(x), float(y), float(z)))
                i += 1 + n
            elif key == 'numsurf':
                n = int(t[1])
                i += 1
                for _ in range(n):
                    flags = int(lines[i].split()[1], 0)
                    mat = int(lines[i + 1].split()[1])
                    refs_n = int(lines[i + 2].split()[1])
                    refs = []
                    for r in lines[i + 3:i + 3 + refs_n]:
                        a = r.split()
                        refs.append((int(a[0]), float(a[1]), float(a[2])))
                    surfs.append(AcSurface(refs, mat, flags))
                    i += 3 + refs_n
            elif key == 'kids':
                kids = int(t[1])
                i += 1
                world_rot, world_loc = _compose(my_rot, my_loc, rot, loc)
                if kind == 'poly':
                    objects.append(AcObject(name, kind, texture, [_apply(v, world_rot, world_loc) for v in raw], surfs,
                                            texrep, texoff))
                for _ in range(kids):
                    i = block(i, world_rot, world_loc)
                return i
            else:
                raise AcError(f'{path}:{i + 1}: unknown token {key!r}')

    while i < len(lines) and not lines[i].strip():
        i += 1
    i = block(i, IDENTITY, (0.0, 0.0, 0.0))
    return AcFile(materials, objects)
