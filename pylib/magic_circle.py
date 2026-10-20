"""The ultimate strike's magic circles (src/ultimate.cpp; docs/plan-2026-10-19-ultimate.md; the user, 2026-10-19: 「魔法阵是必须有，
原版的东西如果不合适也要换，反正做到最好」): flat glowing discs the plugin spawns over the marked point, spins, opens and
closes. Each layer is its own model and object (EDF6VC_ULT_CIRCLE_<LAYER>.MRAB / .SGO), so each has its own material.

    pattern(layer)   -> (lines, glow)   the layer's two greyscale images (PIL 'L'), drawn here (no third-party art)
    build(game, layer) -> bytes          the finished archive (game: rootcpk.Game, read only)
    sgo(game, layer) -> bytes            the object that shows it (a stock FarEventObject's SGO, its model ours)
    check(arc, layer) -> None            raises CircleCheckError on any self-check failure

The material is the stock mothership barrier's (OBJECT/E511_MOTHERSHIP.MRAB, e511_barrier_energy.mdb, shader snd_BeamShield):
two textures lit by two colours, a transparent pass (docs/plan-2026-10-19-ultimate.md §魔法阵). light0_tex carries the
sharp lines, light1_tex their wide glow; light0_color / light1_color are the layer's colours, the scrolls stopped (the plugin
turns the object instead). Its mesh layout (position / normal half4, three texcoords) is the donor's; every texcoord is the
disc's planar map. Two quads, one facing up and one down, so the circle shows from either side; the object on a kind-1 bone
as the donor's (no skin). Like the parachute (pylib/chute_model.py) the object is a FarEventObject with no ragdoll: it
collides with nothing.

The images are drawn with Pillow at install time (the installer bundles it), seeded, so every install draws the same circles.
"""
from __future__ import annotations

import math
import random
import struct
from dataclasses import dataclass, replace

from mdb import (Mdb, Mesh, Object, Rab, RabFile, Texture, MatParam, cmpl_compress, cmpl_decompress, mdb_read, mdb_write,
                 rab_read, rab_write)
from mdb_jet import pack_vertex

DONOR_ARC, DONOR_MDB = 'E511_MOTHERSHIP.MRAB', 'e511_barrier_energy.mdb'
STOCK_SGO = 'EV601_PLANT.SGO'   # a stock FarEventObject (the far-off plant): its SGO, its model ours
SIZE = 2048                      # texture side (px)
SUPER = 2                        # drawn at SUPER x and filtered down
MARGIN = 1.04                    # the quad's half side over the circle's radius (the glow's room)
ALPHA = 2.0                      # the material's opacity factor (alpha.x: a = sat((glow + lines) x glow x ALPHA), only x read)


@dataclass(frozen=True)
class Layer:
    key: str                         # file name part: EDF6VC_ULT_CIRCLE_<KEY>
    radius: float                    # m: the pattern's outer circle
    light0: tuple[float, float, float, float]   # the lines' colour (HDR: over 1 glows)
    light1: tuple[float, float, float, float]   # the glow's colour
    seed: int


# src/ultimate_plan.h CircleModel and kModelRadius (tools/selftest.py holds them equal): the ground's three, the sky's one,
# and the shock waves' ring (scaled out by the plugin as a wave runs: 「最后的爆炸加个冲击波」, 2026-10-20).
LAYERS: dict[str, Layer] = {
    'OUTER': Layer('OUTER', 220.0, (4.0, 0.55, 0.25, 0.0), (3.2, 1.3, 0.25, 0.0), 0x0E1),
    'STAR': Layer('STAR', 176.0, (4.0, 0.35, 0.9, 0.0), (2.6, 0.3, 1.6, 0.0), 0x57A),
    'CORE': Layer('CORE', 72.0, (4.0, 3.2, 2.4, 0.0), (4.0, 1.4, 0.5, 0.0), 0xC0E),
    'SKY': Layer('SKY', 160.0, (2.4, 0.7, 4.0, 0.0), (1.2, 0.4, 3.6, 0.0), 0x5C1),
    'SHOCK': Layer('SHOCK', 100.0, (4.0, 3.4, 4.0, 0.0), (2.4, 1.0, 3.6, 0.0), 0x5D0),
}


def out_arc(layer: str) -> str:
    return f'EDF6VC_ULT_CIRCLE_{layer}.MRAB'


def out_mdb(layer: str) -> str:
    return f'edf6vc_ult_circle_{layer.lower()}.mdb'


def sgo_file(layer: str) -> str:
    return f'EDF6VC_ULT_CIRCLE_{layer}.SGO'


def files() -> list[str]:
    """Every OBJECT file the circles are: each layer's archive and object."""
    return [n for k in LAYERS for n in (out_arc(k), sgo_file(k))]


class CircleCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise CircleCheckError(msg)


# ------------------------------------------------------------------------------------------ the patterns

class _Pen:
    """Drawing in the circle's own units (1 = its outer circle) on a SIZE x SUPER canvas."""

    def __init__(self, draw, side: int) -> None:  # noqa: ANN001 - PIL ImageDraw
        self.d = draw
        self.side = side
        self.c = side / 2.0
        self.k = side / 2.0 / MARGIN    # px per unit

    def px(self, x: float, y: float) -> tuple[float, float]:
        return self.c + x * self.k, self.c + y * self.k

    def w(self, width: float) -> int:
        return max(1, round(width * self.k))

    def circle(self, r: float, width: float) -> None:
        x0, y0 = self.px(-r, -r)
        x1, y1 = self.px(r, r)
        self.d.ellipse((x0, y0, x1, y1), outline=255, width=self.w(width))

    def disc(self, x: float, y: float, r: float) -> None:
        x0, y0 = self.px(x - r, y - r)
        x1, y1 = self.px(x + r, y + r)
        self.d.ellipse((x0, y0, x1, y1), fill=255)

    def ring_at(self, x: float, y: float, r: float, width: float) -> None:
        x0, y0 = self.px(x - r, y - r)
        x1, y1 = self.px(x + r, y + r)
        self.d.ellipse((x0, y0, x1, y1), outline=255, width=self.w(width))

    def line(self, a: tuple[float, float], b: tuple[float, float], width: float) -> None:
        self.d.line((*self.px(*a), *self.px(*b)), fill=255, width=self.w(width))

    def polar(self, r: float, a: float) -> tuple[float, float]:
        return r * math.cos(a), r * math.sin(a)

    def polygon(self, r: float, n: int, step: int, turn: float, width: float) -> None:
        """The star polygon {n/step} on radius r (step 1: the plain polygon)."""
        pts = [self.polar(r, turn + 2.0 * math.pi * i / n) for i in range(n)]
        for i in range(n):
            self.line(pts[i], pts[(i + step) % n], width)

    def ticks(self, r0: float, r1: float, n: int, width: float, turn: float = 0.0) -> None:
        for i in range(n):
            a = turn + 2.0 * math.pi * i / n
            self.line(self.polar(r0, a), self.polar(r1, a), width)

    def runes(self, r0: float, r1: float, n: int, width: float, rnd: random.Random, turn: float = 0.0) -> None:
        """A band of n made-up runes between radii r0 and r1, written round the circle (the cell's u along it, v outwards):
        each a stem across the band with one to three branches from points on it to the cell's sides, now and then a
        small ring or a dot at a free end; a dot between runes."""
        da = 2.0 * math.pi / n
        for i in range(n):
            a0 = turn + i * da

            def at(u: float, v: float) -> tuple[float, float]:
                return self.polar(r0 + (r1 - r0) * v, a0 + da * (0.18 + 0.64 * u))

            stem = rnd.choice((0.5, 0.5, 0.3, 0.7))
            self.line(at(stem, 0.08), at(stem, 0.92), width)
            ends = []
            for _ in range(rnd.randint(1, 3)):
                v0 = rnd.choice((0.08, 0.3, 0.5, 0.7, 0.92))
                side = rnd.choice((0.0, 1.0))
                v1 = min(0.92, max(0.08, v0 + rnd.choice((-0.35, -0.2, 0.0, 0.2, 0.35))))
                self.line(at(stem, v0), at(side, v1), width)
                ends.append((side, v1))
            if ends and rnd.random() < 0.35:
                u, v = rnd.choice(ends)
                x, y = at(u, v)
                self.ring_at(x, y, (r1 - r0) * 0.12, width * 0.8)
            elif rnd.random() < 0.3:
                x, y = at(stem, rnd.choice((0.08, 0.92)))
                self.disc(x, y, (r1 - r0) * 0.06)
            x, y = self.polar((r0 + r1) / 2.0, a0)
            self.disc(x, y, (r1 - r0) * 0.035)


def _outer(pen: _Pen, rnd: random.Random) -> None:
    """The great circle: a double rim, a band of runes, a ring of ticks, an inner double rim and twelve small seals on it."""
    pen.circle(1.0, 0.010)
    pen.circle(0.975, 0.004)
    pen.runes(0.888, 0.962, 64, 0.0045, rnd)
    pen.circle(0.875, 0.006)
    pen.ticks(0.835, 0.862, 180, 0.0022)
    pen.ticks(0.815, 0.862, 36, 0.0045)
    pen.circle(0.80, 0.004)
    pen.circle(0.785, 0.008)
    for i in range(12):
        x, y = pen.polar(0.785, 2.0 * math.pi * (i + 0.5) / 12)
        pen.disc(x, y, 0.010)
        pen.ring_at(x, y, 0.045, 0.004)
        pen.ring_at(x, y, 0.030, 0.0025)
    pen.runes(0.70, 0.765, 48, 0.0035, rnd, turn=0.05)
    pen.circle(0.69, 0.003)


def _star(pen: _Pen, rnd: random.Random) -> None:
    """The star: a hexagram on its circle, a dodecagram inside, small circles on the points, an inner rune ring."""
    pen.circle(1.0, 0.007)
    pen.polygon(1.0, 6, 2, -math.pi / 2, 0.010)
    pen.polygon(1.0, 6, 1, -math.pi / 2, 0.004)
    pen.polygon(0.74, 12, 5, 0.0, 0.0035)
    for i in range(6):
        x, y = pen.polar(1.0, -math.pi / 2 + 2.0 * math.pi * i / 6)
        pen.ring_at(x, y, 0.085, 0.006)
        pen.ring_at(x, y, 0.06, 0.003)
        for k in range(3):
            a = 2.0 * math.pi * k / 3 + i
            pen.line((x + 0.06 * math.cos(a), y + 0.06 * math.sin(a)),
                     (x + 0.06 * math.cos(a + 2.1), y + 0.06 * math.sin(a + 2.1)), 0.003)
    pen.circle(0.577, 0.006)     # the hexagram's inner hexagon's circle
    pen.runes(0.47, 0.56, 36, 0.004, rnd)
    pen.circle(0.46, 0.004)


def _core(pen: _Pen, rnd: random.Random) -> None:
    """The heart: concentric rims, a rune ring, radial rays, a triangle and an eye."""
    pen.circle(1.0, 0.016)
    pen.circle(0.95, 0.006)
    pen.runes(0.78, 0.93, 24, 0.009, rnd)
    pen.circle(0.76, 0.010)
    pen.ticks(0.40, 0.74, 24, 0.006, turn=0.13)
    pen.polygon(0.70, 3, 1, -math.pi / 2, 0.012)
    pen.polygon(0.70, 3, 1, math.pi / 2, 0.012)
    pen.circle(0.38, 0.012)
    pen.circle(0.30, 0.006)
    pen.disc(0.0, 0.0, 0.12)
    pen.circle(0.20, 0.010)


def _sky(pen: _Pen, rnd: random.Random) -> None:
    """The sky's ring: a broad band of runes between rims, ticks inside, open in the middle (the beam comes through)."""
    pen.circle(1.0, 0.012)
    pen.circle(0.97, 0.004)
    pen.runes(0.84, 0.955, 56, 0.006, rnd)
    pen.circle(0.83, 0.008)
    pen.ticks(0.76, 0.81, 72, 0.003)
    pen.circle(0.74, 0.004)
    pen.polygon(0.74, 8, 3, 0.0, 0.004)
    pen.circle(0.40, 0.008)


def _shock(pen: _Pen, rnd: random.Random) -> None:
    """A shock wave's front: a broad bright rim, its ripples falling behind it, streaks thrown outwards; empty inside."""
    pen.circle(0.985, 0.03)
    pen.circle(0.94, 0.012)
    pen.circle(0.90, 0.006)
    pen.circle(0.85, 0.003)
    pen.circle(0.78, 0.002)
    for i in range(160):
        a = 2.0 * math.pi * (i + rnd.random() * 0.8) / 160
        r0 = 0.80 + 0.08 * rnd.random()
        pen.line(pen.polar(r0, a), pen.polar(min(1.0, r0 + 0.08 + 0.1 * rnd.random()), a), 0.002 + 0.002 * rnd.random())


_DRAW = {'OUTER': _outer, 'STAR': _star, 'CORE': _core, 'SKY': _sky, 'SHOCK': _shock}


def pattern(layer: str):  # noqa: ANN201 - (PIL.Image, PIL.Image)
    """The layer's lines (sharp, white on black) and their glow (the lines widened and blurred), SIZE x SIZE 'L' images."""
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
    side = SIZE * SUPER
    img = Image.new('L', (side, side), 0)
    pen = _Pen(ImageDraw.Draw(img), side)
    _DRAW[layer](pen, random.Random(LAYERS[layer].seed))
    lines = img.resize((SIZE, SIZE), Image.LANCZOS)
    wide = lines.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(SIZE / 256))
    glow = ImageChops.lighter(wide.point(lambda v: min(255, int(v * 0.85))), lines.filter(ImageFilter.GaussianBlur(SIZE / 90))
                              .point(lambda v: min(255, int(v * 2.2))))
    return lines, glow


def dds(img) -> bytes:  # noqa: ANN001 - PIL 'L' image
    """A DXT1 DDS of a greyscale image (r = g = b) with its mip chain."""
    import texfile
    import texfile_accel
    rgba = img.convert('RGBA').tobytes()
    return texfile_accel.dxt1_dds(texfile.Image(img.width, img.height, bytearray(rgba)))


# ------------------------------------------------------------------------------------------ the model


def quads(radius: float) -> tuple[list[tuple], list[tuple[int, int, int]]]:
    """Two quads (MARGIN x radius half side) on y = 0: the first faces up, the second down. (position, normal, uv)."""
    h = radius * MARGIN
    corners = [(-h, -h), (h, -h), (-h, h), (h, h)]
    verts = []
    tris = []
    for ny in (1.0, -1.0):
        first = len(verts)
        for x, z in corners:
            verts.append(((x, 0.0, z), (0.0, ny, 0.0), ((x / h + 1.0) / 2.0, (z / h + 1.0) / 2.0)))
        for t in ((0, 2, 1), (1, 2, 3)):
            a, b, c = (first + i for i in t)
            # front faces: cross(b - a, c - a) . n > 0 (the stock winding)
            pa, pb, pc = verts[a][0], verts[b][0], verts[c][0]
            cy = (pb[2] - pa[2]) * (pc[0] - pa[0]) - (pb[0] - pa[0]) * (pc[2] - pa[2])
            tris.append((a, b, c) if cy * ny > 0 else (a, c, b))
    return verts, tris


def _row(elems: list, v: tuple) -> list[tuple[float, ...]]:
    p, n, uv = v
    out = []
    for e in elems:
        k = e.name.lower()
        out.append({'position': (*p, 1.0), 'normal': (*n, 1.0), 'texcoord': uv}[k])
    return out


def model(donor: Mdb, layer: str, lines_name: str, glow_name: str) -> Mdb:
    """The layer's model from the donor's barrier material and mesh layout (see the module doc)."""
    _req(len(donor.materials) == 1 and donor.materials[0].shader == 'snd_BeamShield', f'{DONOR_MDB}: not the barrier material')
    mat = donor.materials[0]
    _req(len(donor.objects) == 1 and len(donor.objects[0].meshes) == 1, f'{DONOR_MDB}: not one mesh')
    tmpl = donor.objects[0].meshes[0]
    _req([e.name.lower() for e in tmpl.elems] == ['position', 'normal', 'texcoord', 'texcoord', 'texcoord'],
         f'{DONOR_MDB}: layout {[e.name for e in tmpl.elems]}')
    _req(tmpl.flags == bytes(4) and [b.kind for b in donor.bones] == [0, 1], f'{DONOR_MDB}: skinned or other bones')
    lay = LAYERS[layer]
    params = []
    for p in mat.params:
        value = {'light0_color': list(lay.light0), 'light1_color': list(lay.light1), 'scroll_light0': [0.0, 0.0, 0.0, 0.0],
                 'scroll_light1': [0.0, 0.0, 0.0, 0.0], 'alpha': [ALPHA, 0.0, 0.0, 0.0]}.get(p.name)
        _req(value is not None, f'{DONOR_MDB}: material parameter {p.name}')
        params.append(MatParam(value, p.unk, p.name, p.type))
    kinds = {x.kind: x for x in mat.textures}
    _req(set(kinds) == {'light0_tex', 'light1_tex'}, f'{DONOR_MDB}: texture slots {sorted(kinds)}')
    textures = [Texture(0, lines_name.replace('.', '_'), lines_name, donor.textures[0].unk),
                Texture(1, glow_name.replace('.', '_'), glow_name, donor.textures[0].unk)]
    slots = [replace(kinds['light0_tex'], texture=0), replace(kinds['light1_tex'], texture=1)]
    verts, tris = quads(lay.radius)
    vdata = b''.join(pack_vertex(tmpl.elems, tmpl.vsize, _row(tmpl.elems, v)) for v in verts)
    mesh = Mesh(tmpl.flags, 0, tmpl.unk08, tmpl.vsize, list(tmpl.elems), 0, vdata,
                struct.pack(f'<{3 * len(tris)}H', *(i for t in tris for i in t)))
    name = f'edf6vc_ult_circle_{layer.lower()}'
    bones = [replace(b) for b in donor.bones]
    md = Mdb(donor.version, ['mdl', name, 'Material', name], bones, [Object(3, 1, [mesh])],
             [replace(mat, index=0, name=2, params=params, textures=slots)], textures)
    import graft_pure as g
    return g.recompute_bounds(g.relink(md))


def build(game, layer: str, images=None) -> bytes:  # noqa: ANN001 - rootcpk.Game, (lines, glow) to reuse
    """The finished archive: the model, its two textures (low and high detail) in the donor's folder order."""
    import texfile
    donor_rab = rab_read(game.read('OBJECT', DONOR_ARC))
    hits = [f for f in donor_rab.files if f.name.lower() == DONOR_MDB.lower()]
    _req(len(hits) == 1, f'{DONOR_ARC}: {len(hits)} x {DONOR_MDB}')
    donor = mdb_read(hits[0].data)
    stem = f'edf6vc_ult_circle_{layer.lower()}'
    lines_name, glow_name = f'{stem}_lines.dds', f'{stem}_glow.dds'
    md = model(donor, layer, lines_name, glow_name)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    folders = list(donor_rab.folders)
    _req(folders == ['TEXTURE', 'MODEL', 'HD-TEXTURE'], f'{DONOR_ARC}: folders {folders}')
    lines, glow = images if images is not None else pattern(layer)
    members_lod, members_hd = [], []
    for name, img in ((lines_name, lines), (glow_name, glow)):
        hd, lod = texfile.texture_pair(dds(img))
        members_lod.append(RabFile(name.replace('.dds', '.lod.dds'), 0, 0, cmpl_compress(lod)))
        members_hd.append(RabFile(name, 2, 1, cmpl_compress(hd)))
    files = members_lod + [RabFile(out_mdb(layer), 1, 0, stored)] + members_hd
    return rab_write(Rab(donor_rab.version, folders, files))


def sgo(game, layer: str) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The stock FarEventObject's SGO with this layer's model, a unit scale, no ragdoll (nothing to collide with)."""
    import dsgo
    doc = dsgo.parse(game.read('OBJECT', STOCK_SGO))
    r = doc.root
    _req(r.get('xgs_scene_object_class') == 'FarEventObject', f'{STOCK_SGO} is no FarEventObject')
    try:
        r.get('ragdoll')
        _req(False, f'{STOCK_SGO} has a ragdoll')
    except KeyError:
        pass
    r.set('animation_model', dsgo.Node([dsgo.Node([f'app:/Object/{out_arc(layer).lower()}', out_mdb(layer)]), 0.0, 0.0]))
    r.set('setting', dsgo.Node([dsgo.Node([1.0, 1.0, 1.0])], {0: 'scale'}))
    return dsgo.compact(doc)


def check(arc: bytes, layer: str) -> None:
    """Re-read `arc`: the archive and model round-trip; one object on the kind-1 bone, one unskinned mesh of 8 vertices and
    4 triangles spanning 2 x MARGIN x radius on y = 0; both textures (and their .lod) are members, DXT1 with mips."""
    import texfile
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    hits = [f for f in rab.files if f.name.lower() == out_mdb(layer)]
    _req(len(hits) == 1, f'{out_mdb(layer)}: {len(hits)} members')
    data = hits[0].data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    _req([b.kind for b in md.bones] == [0, 1] and len(md.objects) == 1 and md.objects[0].bone == 1, 'bones / object')
    me = md.objects[0].meshes[0]
    _req(me.nverts == 8 and len(me.indices) == 4 * 3 * 2, f'{me.nverts} vertices, {len(me.indices) // 6} triangles')
    import graft_pure as g
    p = g.mesh_positions(me)
    span = max(v[0] for v in p) - min(v[0] for v in p)
    _req(abs(span - 2 * MARGIN * LAYERS[layer].radius) < 0.5 and all(abs(v[1]) < 1e-3 for v in p), f'span {span:.1f} m')
    names = {f.name.lower(): f for f in rab.files}
    for t in md.textures:
        _req(t.filename.lower() in names and t.filename.lower().replace('.dds', '.lod.dds') in names, f'{t.filename} missing')
        info = texfile.dds_info(names[t.filename.lower()].data)
        _req(info.fourcc == b'DXT1' and info.width == SIZE and info.mips > 1, f'{t.filename}: {info}')
    _req(md.materials[0].shader == 'snd_BeamShield', 'material shader')
