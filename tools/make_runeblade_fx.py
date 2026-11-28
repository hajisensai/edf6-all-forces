"""The Rune Blade's (ZwRuneBlade, type 5) shared effect art (docs/plan-2026-11-28-zero-worldline-gear.md §2: 「所有武器共用一套
光阵贴图和光痕材质，按武器换尺寸和颜色强度」; docs/newclass-re/runeblade-look-plan.md): one rune circle drawn once and one
light-trail strip drawn once, each put in as a few objects that differ only in size and colour strength:

  Mods/OBJECT/ZW_RUNE_<VARIANT>.MRAB / .SGO      (VARIANTS: FOOT, BACK, HIT, BURST: the circle; TRAIL: the strip)

The circle's two textures (CIRCLE_LINES / CIRCLE_GLOW) and the trail's two (TRAIL_LINES / TRAIL_GLOW) carry the same names
and bytes in every archive that shows them: one set of art, the variants only the material's two colours and the quad's
size. Material, mesh layout and object are the ultimate strike's circles' (pylib/magic_circle.py: the stock mothership
barrier, shader snd_BeamShield, two light textures lit by two colours, a FarEventObject without ragdoll): the plugin spawns
an object, places, turns and scales it (the size per weapon is the object's scale; the colour strength per weapon is the
variant). Ether blue-violet, the edge whiter (light0 the sharp lines, light1 their glow).

    build(game) -> {path under Mods: bytes}      game: rootcpk.Game (only read)
    check(arc, variant) -> None                  raises magic_circle.CircleCheckError on a self-check failure
    python tools/make_runeblade_fx.py [out dir]  writes the files there (a look at them; the install is make_zw_classes')
"""
from __future__ import annotations

import math
import os
import random
import struct
import sys
from dataclasses import dataclass, replace

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import magic_circle as mc  # noqa: E402
from mdb import (Mdb, Mesh, Object, Rab, RabFile, Texture, MatParam, cmpl_compress, mdb_read, mdb_write,  # noqa: E402
                 rab_read, rab_write)
from mdb_jet import pack_vertex  # noqa: E402

CIRCLE_SIZE = 1024      # px: the circle shows at most a few metres wide (a hit's mark, the feet's circle)
TRAIL_SIZE = 512        # px: the strip's texture (square: the stock texture layout; stretched by the quad)
CIRCLE_SEED = 0x2B1     # the circle is drawn seeded: every install draws the same runes
STEM = 'zw_rune'
CIRCLE_LINES, CIRCLE_GLOW = f'{STEM}_circle_lines.dds', f'{STEM}_circle_glow.dds'
TRAIL_LINES, TRAIL_GLOW = f'{STEM}_trail_lines.dds', f'{STEM}_trail_glow.dds'


@dataclass(frozen=True)
class Variant:
    key: str                                    # ZW_RUNE_<KEY>
    art: str                                    # 'circle' or 'trail'
    half: tuple[float, float]                   # m: the quad's half extents (x, z) at scale 1
    light0: tuple[float, float, float, float]   # the lines' colour (HDR: over 1 glows)
    light1: tuple[float, float, float, float]   # the glow's colour
    use: str


# Sizes at scale 1 (docs/newclass-re/runeblade-look-plan.md §2 lists who uses which): the plugin scales them per weapon.
VARIANTS: dict[str, Variant] = {
    'FOOT': Variant('FOOT', 'circle', (1.3, 1.3), (2.4, 2.2, 4.0, 0.0), (1.2, 0.5, 3.4, 0.0), '踏阵：脚下光阵（起飞、每次踏阵）'),
    'BACK': Variant('BACK', 'circle', (1.0, 1.0), (2.0, 1.8, 3.6, 0.0), (1.0, 0.4, 3.0, 0.0), '飞行时背后光阵'),
    'HIT': Variant('HIT', 'circle', (0.6, 0.6), (3.6, 3.4, 4.0, 0.0), (1.8, 0.7, 4.0, 0.0), '命中点小刻印光阵（所有武器）'),
    'BURST': Variant('BURST', 'circle', (6.0, 6.0), (3.2, 2.6, 4.0, 0.0), (1.6, 0.6, 4.0, 0.0),
                     '大光阵：坠星落地冲击波、骑枪刺中、炮击信标落点'),
    'TRAIL': Variant('TRAIL', 'trail', (2.0, 0.25), (3.0, 3.0, 4.0, 0.0), (1.2, 0.45, 3.6, 0.0),
                     '光痕：刀弧、枪尾、弹道（沿运动方向拉伸，x 为长）'),
}


def out_arc(key: str) -> str:
    return f'ZW_RUNE_{key}.MRAB'


def out_sgo(key: str) -> str:
    return f'ZW_RUNE_{key}.SGO'


def out_mdb(key: str) -> str:
    return f'{STEM}_{key.lower()}.mdb'


def files() -> list[str]:
    """Every path under Mods this tool's build writes."""
    return [f'OBJECT/{n}' for k in VARIANTS for n in (out_arc(k), out_sgo(k))]


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise mc.CircleCheckError(msg)


# ------------------------------------------------------------------------------------------ the art

def _draw_circle(pen: mc._Pen, rnd: random.Random) -> None:
    """The rune circle: a double rim, a rune band, a clock-face of ticks (the 「刻时」 motif), a seven-pointed star (the
    seven circles of 「七星」), an inner rune band and a small seal in the middle."""
    pen.circle(1.0, 0.014)
    pen.circle(0.965, 0.005)
    pen.runes(0.85, 0.95, 36, 0.007, rnd)
    pen.circle(0.835, 0.008)
    pen.ticks(0.78, 0.825, 60, 0.004)
    pen.ticks(0.75, 0.825, 12, 0.009)
    pen.circle(0.74, 0.005)
    pen.polygon(0.74, 7, 3, -math.pi / 2, 0.008)
    pen.polygon(0.74, 7, 1, -math.pi / 2, 0.004)
    for i in range(7):
        x, y = pen.polar(0.74, -math.pi / 2 + 2.0 * math.pi * i / 7)
        pen.ring_at(x, y, 0.06, 0.007)
        pen.disc(x, y, 0.018)
    pen.circle(0.42, 0.008)
    pen.runes(0.30, 0.41, 14, 0.008, rnd, turn=0.2)
    pen.circle(0.29, 0.006)
    pen.polygon(0.22, 4, 1, 0.0, 0.010)
    pen.polygon(0.22, 4, 1, math.pi / 4, 0.010)
    pen.disc(0.0, 0.0, 0.06)


def circle_images():  # noqa: ANN201 - (PIL.Image, PIL.Image)
    """The circle's lines (sharp, white on black) and their glow, CIRCLE_SIZE square 'L' images."""
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
    side = CIRCLE_SIZE * mc.SUPER
    img = Image.new('L', (side, side), 0)
    _draw_circle(mc._Pen(ImageDraw.Draw(img), side), random.Random(CIRCLE_SEED))
    lines = img.resize((CIRCLE_SIZE, CIRCLE_SIZE), Image.LANCZOS)
    wide = lines.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(CIRCLE_SIZE / 256))
    glow = ImageChops.lighter(wide.point(lambda v: min(255, int(v * 0.85))),
                              lines.filter(ImageFilter.GaussianBlur(CIRCLE_SIZE / 90)).point(lambda v: min(255, int(v * 2.2))))
    return lines, glow


def trail_images():  # noqa: ANN201 - (PIL.Image, PIL.Image)
    """The trail: u along it (0 its tail, 1 its head), v across. The lines are a thin bright core (the edge's white) that
    grows from the tail to the head; the glow is a wide soft band under it, fading out to the tail."""
    from PIL import Image
    n = TRAIL_SIZE
    lines = bytearray(n * n)
    glow = bytearray(n * n)
    for y in range(n):
        d = abs((y + 0.5) / n * 2.0 - 1.0)            # 0 the middle, 1 the strip's edge
        for x in range(n):
            u = (x + 0.5) / n
            fade = u ** 1.6
            core_w = 0.06 + 0.10 * u
            core = max(0.0, 1.0 - d / core_w) if d < core_w else 0.0
            band = math.exp(-(d / (0.22 + 0.25 * u)) ** 2)
            lines[y * n + x] = min(255, int(255 * core * (0.3 + 0.7 * fade)))
            glow[y * n + x] = min(255, int(255 * band * fade))
    return Image.frombytes('L', (n, n), bytes(lines)), Image.frombytes('L', (n, n), bytes(glow))


# ------------------------------------------------------------------------------------------ the model

def quads(hx: float, hz: float) -> list[tuple]:
    """Two quads (half extents hx, hz) on y = 0: the first faces up, the second down. (position, normal, uv): x along u."""
    corners = [(-hx, -hz), (hx, -hz), (-hx, hz), (hx, hz)]
    verts = []
    for ny in (1.0, -1.0):
        for x, z in corners:
            verts.append(((x, 0.0, z), (0.0, ny, 0.0), ((x / hx + 1.0) / 2.0, (z / hz + 1.0) / 2.0)))
    return verts


def _tris(verts: list[tuple]) -> list[tuple[int, int, int]]:
    tris = []
    for first, ny in ((0, 1.0), (4, -1.0)):
        for t in ((0, 2, 1), (1, 2, 3)):
            a, b, c = (first + i for i in t)
            pa, pb, pc = verts[a][0], verts[b][0], verts[c][0]
            cy = (pb[2] - pa[2]) * (pc[0] - pa[0]) - (pb[0] - pa[0]) * (pc[2] - pa[2])
            tris.append((a, b, c) if cy * ny > 0 else (a, c, b))   # front faces: the stock winding (magic_circle.quads)
    return tris


def model(donor: Mdb, v: Variant, lines_name: str, glow_name: str) -> Mdb:
    """The variant's model from the donor's barrier material and mesh layout (magic_circle.model, any quad size)."""
    _req(len(donor.materials) == 1 and donor.materials[0].shader == 'snd_BeamShield', f'{mc.DONOR_MDB}: not the barrier material')
    mat = donor.materials[0]
    _req(len(donor.objects) == 1 and len(donor.objects[0].meshes) == 1, f'{mc.DONOR_MDB}: not one mesh')
    tmpl = donor.objects[0].meshes[0]
    _req([e.name.lower() for e in tmpl.elems] == ['position', 'normal', 'texcoord', 'texcoord', 'texcoord'],
         f'{mc.DONOR_MDB}: layout {[e.name for e in tmpl.elems]}')
    _req(tmpl.flags == bytes(4) and [b.kind for b in donor.bones] == [0, 1], f'{mc.DONOR_MDB}: skinned or other bones')
    params = []
    for p in mat.params:
        value = {'light0_color': list(v.light0), 'light1_color': list(v.light1), 'scroll_light0': [0.0, 0.0, 0.0, 0.0],
                 'scroll_light1': [0.0, 0.0, 0.0, 0.0], 'alpha': [mc.ALPHA, 0.0, 0.0, 0.0]}.get(p.name)
        _req(value is not None, f'{mc.DONOR_MDB}: material parameter {p.name}')
        params.append(MatParam(value, p.unk, p.name, p.type))
    kinds = {x.kind: x for x in mat.textures}
    _req(set(kinds) == {'light0_tex', 'light1_tex'}, f'{mc.DONOR_MDB}: texture slots {sorted(kinds)}')
    textures = [Texture(0, lines_name.replace('.', '_'), lines_name, donor.textures[0].unk),
                Texture(1, glow_name.replace('.', '_'), glow_name, donor.textures[0].unk)]
    slots = [replace(kinds['light0_tex'], texture=0), replace(kinds['light1_tex'], texture=1)]
    verts = quads(*v.half)
    tris = _tris(verts)
    vdata = b''.join(pack_vertex(tmpl.elems, tmpl.vsize, mc._row(tmpl.elems, x)) for x in verts)
    mesh = Mesh(tmpl.flags, 0, tmpl.unk08, tmpl.vsize, list(tmpl.elems), 0, vdata,
                struct.pack(f'<{3 * len(tris)}H', *(i for t in tris for i in t)))
    name = f'{STEM}_{v.key.lower()}'
    md = Mdb(donor.version, ['mdl', name, 'Material', name], [replace(b) for b in donor.bones], [Object(3, 1, [mesh])],
             [replace(mat, index=0, name=2, params=params, textures=slots)], textures)
    import graft_pure as g
    return g.recompute_bounds(g.relink(md))


def _names(v: Variant) -> tuple[str, str]:
    return (CIRCLE_LINES, CIRCLE_GLOW) if v.art == 'circle' else (TRAIL_LINES, TRAIL_GLOW)


def archive(donor_rab: Rab, v: Variant, art: dict[str, tuple[bytes, bytes]]) -> bytes:
    """The variant's archive: its model and the shared art's (HD, .lod) pairs, in the donor's folder order."""
    hits = [f for f in donor_rab.files if f.name.lower() == mc.DONOR_MDB.lower()]
    _req(len(hits) == 1, f'{mc.DONOR_ARC}: {len(hits)} x {mc.DONOR_MDB}')
    folders = list(donor_rab.folders)
    _req(folders == ['TEXTURE', 'MODEL', 'HD-TEXTURE'], f'{mc.DONOR_ARC}: folders {folders}')
    lines_name, glow_name = _names(v)
    data = mdb_write(model(mdb_read(hits[0].data), v, lines_name, glow_name))
    lod_members, hd_members = [], []
    for name in (lines_name, glow_name):
        hd, lod = art[name]
        lod_members.append(RabFile(name.replace('.dds', '.lod.dds'), 0, 0, cmpl_compress(lod)))
        hd_members.append(RabFile(name, 2, 1, cmpl_compress(hd)))
    return rab_write(Rab(donor_rab.version, folders, lod_members + [RabFile(out_mdb(v.key), 1, 0, cmpl_compress(data))]
                         + hd_members))


def sgo(game, key: str) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The stock FarEventObject's SGO with this variant's model, a unit scale (magic_circle.sgo)."""
    import dsgo
    doc = dsgo.parse(game.read('OBJECT', mc.STOCK_SGO))
    r = doc.root
    _req(r.get('xgs_scene_object_class') == 'FarEventObject', f'{mc.STOCK_SGO} is no FarEventObject')
    r.set('animation_model', dsgo.Node([dsgo.Node([f'app:/Object/{out_arc(key).lower()}', out_mdb(key)]), 0.0, 0.0]))
    r.set('setting', dsgo.Node([dsgo.Node([1.0, 1.0, 1.0])], {0: 'scale'}))
    return dsgo.compact(doc)


def art() -> dict[str, tuple[bytes, bytes]]:
    """{texture name: (HD, .lod)}: the shared art, drawn once."""
    import texfile
    out = {}
    for (ln, gn), imgs in (((CIRCLE_LINES, CIRCLE_GLOW), circle_images()), ((TRAIL_LINES, TRAIL_GLOW), trail_images())):
        for name, img in zip((ln, gn), imgs):
            out[name] = texfile.texture_pair(mc.dds(img))
    return out


def build(game) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    donor_rab = rab_read(game.read('OBJECT', mc.DONOR_ARC))
    shared = art()
    out = {}
    for key, v in VARIANTS.items():
        out[f'OBJECT/{out_arc(key)}'] = archive(donor_rab, v, shared)
        out[f'OBJECT/{out_sgo(key)}'] = sgo(game, key)
    return out


def check(arc: bytes, key: str) -> None:
    """Re-read `arc`: archive and model round-trip; one unskinned mesh of 8 vertices / 4 triangles spanning the variant's
    quad on y = 0; both shared textures (and their .lod) are members, DXT1 with mips, of the art's size."""
    import texfile
    import graft_pure as g
    v = VARIANTS[key]
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    hits = [f for f in rab.files if f.name.lower() == out_mdb(key)]
    _req(len(hits) == 1, f'{out_mdb(key)}: {len(hits)} members')
    md = mdb_read(hits[0].data)
    _req(mdb_write(md) == hits[0].data, 'model does not round-trip')
    _req([b.kind for b in md.bones] == [0, 1] and len(md.objects) == 1 and md.objects[0].bone == 1, 'bones / object')
    me = md.objects[0].meshes[0]
    _req(me.nverts == 8 and len(me.indices) == 4 * 3 * 2, f'{me.nverts} vertices, {len(me.indices) // 6} triangles')
    p = g.mesh_positions(me)
    sx = max(q[0] for q in p) - min(q[0] for q in p)
    sz = max(q[2] for q in p) - min(q[2] for q in p)
    _req(abs(sx - 2 * v.half[0]) < 0.01 and abs(sz - 2 * v.half[1]) < 0.01 and all(abs(q[1]) < 1e-3 for q in p),
         f'span {sx:.2f} x {sz:.2f} m')
    names = {f.name.lower(): f for f in rab.files}
    size = CIRCLE_SIZE if v.art == 'circle' else TRAIL_SIZE
    _req(sorted(t.filename.lower() for t in md.textures) == sorted(_names(v)), f'textures {[t.filename for t in md.textures]}')
    for t in md.textures:
        _req(t.filename.lower() in names and t.filename.lower().replace('.dds', '.lod.dds') in names, f'{t.filename} missing')
        info = texfile.dds_info(names[t.filename.lower()].data)
        _req(info.fourcc == b'DXT1' and info.width == size and info.mips > 1, f'{t.filename}: {info}')
    _req(md.materials[0].shader == 'snd_BeamShield', 'material shader')


def main(argv: list[str]) -> int:
    import vcobjects as vc
    out_dir = argv[0] if argv else os.path.join(HERE, '..', 'tmp', 'runeblade_fx')
    built = build(vc.Game(vc.DEFAULT_GAME))
    for rel, data in built.items():
        if rel.endswith('.MRAB'):
            check(data, rel.split('ZW_RUNE_', 1)[1][:-len('.MRAB')])
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
        print('写出', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
