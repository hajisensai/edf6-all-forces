"""EDF4.1's HQ (sortie screen) background on EDF6, for the EDF4.1 mission packs only (the user, 2026-10-18: 「在游玩
4.1代本篇和4.1代DLC1+DLC2任务包时出击背景应修改成4.1同样的背景」). Converted at install time from the player's own EDF4.1
(or an EDF4.1 asset pack, pylib/edf41src.py) into new files under Mods/UI; EDF6's own four backgrounds stay as they are.

What 4.1 shows (EDF4.1 MAINSCRIPT.AS MainMenu: g_bg.Play("app:/Menu/Menu_BG01.sgo")): one animated model, MENU/MENU_BG01
.RAB (23 objects on rigid bones, shaders snd_UI_Simple / snd_UI_Simple_UVAnim, which EDF6's BG*.mdb use too) with
MENU_BG01.CAS (one looping clip: five circles spinning) and a material animation (scrolling UVs), seen through 4.1's
UiBG camera: a right-handed perspective (EDF41.exe 0x419A70 -> 0x51B230, near 0.01, far 4000) of vertical field of view
0.96 rad (the UiBG constructor 0x409190 writes it; [this+0x90] at 0x409D5D for the other UiBG kinds) from the origin
(position (0, 0, 0) unless its SGO names one; Menu_BG01.sgo does not). The layers sit at z = -8.8 .. -31 and fill
that frustum exactly (BG_Stripe at z = -31.03 is 58.04 x 32.66; 2 * 31.03 * tan(0.48) = 32.4).

What EDF6 shows: lyt_bg.sgo (HUiBG, EDF.dll 0x8BE0E0) holds four full-screen layouts, BG_BASE .. BG_BASE_D, each a
Skin_Model; Skin_Model's transform (0x7F4D30) is diag(scale.x * 100, scale.y * 100, scale.z) placed at the layout's
centre in pixels: model units are 1/100 px of the 1920 x 1080 UI, drawn without perspective (EDF6's BG layers at
z = 0 .. 1.6 are all exactly 19.2 x 10.8), y down the screen. So 4.1's model is reprojected (reproject): every point
to where 4.1's camera shows it, X = S x / -z, Y = -S y / -z with S = 5.4 / tan(0.48) (the screen's half height in model units over
4.1's), its depth the projective one (depth(): planes stay planes, so what hides what is 4.1's), within 0 .. 1.5:
  - a bone the clip leaves still: its vertices projected one by one in the pose the clip holds it (geometry crossing
    the frustum's sides, the panoramic WorldMap band, clipped first);
  - a turning bone: the three big circles turn about the view axis from the origin, so each vertex is scaled by its
    own depth's s (the turn then turns the projection: exact); the two small discs off the axis (BGCircle_map3 /
    _map4, tilted 52 degrees) turn about their own normal, so they are scaled by s in their own plane and their
    position projected (first order: the perspective's shear across a 2.5-unit disc at depth 20 is dropped).
The CAS keeps every key (only those two bones' fixed positions move); the material animation is 4.1's. What draws
over what is the materials' priority byte (+2: 4.1's layers at one depth, z = -17.5, carry 40 .. 90; EDF6's BG_B
0 .. 30 rising with its layers), kept as 4.1's; the depths only keep their order.

Files (UI/, under Mods): EDF41_MENU_BG01.RAB / .CAS / .SGO (the model, its clip, 4.1's material animation),
EDF41_MENU_BG01_SKIN.SGO (a Skin_Model on it, as UI/BG_B_SKIN.SGO is on BG_B), LYT_BG.SGO: EDF6's own with one
layout more, LAYOUT (= that skin, first in the tree so the stock four draw over it), and the three resource groups
that preload the backgrounds (GROUPS) with our four files added. The plugin (src/edf5campaign.cpp
InstallEdf41MenuBg) shows LAYOUT and hides the four while an EDF4.1 pack is the current mode, and hides it
otherwise; without the plugin the stock four cover it. One of these stock files in Mods that the ledger does not
record as ours (another mod's) is never written over: then nothing is installed and that is said.

    build(root, root41) -> {rel under Mods: bytes}          ValueError / KeyError when 4.1's files are not usable
    install(root, root41, say) -> (written, skipped)
    remove(root) -> (deleted, kept)
"""
from __future__ import annotations

import copy
import json
import math
import os
import struct
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import cas_legacy  # noqa: E402
import edf41src  # noqa: E402
import ledger  # noqa: E402
import mdb  # noqa: E402
import mdb_legacy  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from modfiles import load_json, sha256, sha256_file  # noqa: E402

OWNER = 'edf41menubg'   # pylib/ledger.py OWNERS
VERSION = 1             # raise when the output changes: installed files are converted again
STAMP = '.edf41menubg.json'   # Mods/: {'version': VERSION, 'source': {4.1 file: fingerprint}}
SOURCES = ('MENU/MENU_BG01.SGO', 'MENU/MENU_BG01.RAB', 'MENU/MENU_BG01.CAS')   # an asset pack's paths
STEM = 'EDF41_MENU_BG01'
MODEL = 'Menu_BG01.mdb'                 # the RAB member (4.1's name)
RAB, CAS, SGO, SKIN = (f'UI/{STEM}.RAB', f'UI/{STEM}.CAS', f'UI/{STEM}.SGO', f'UI/{STEM}_SKIN.SGO')
LYT = 'UI/LYT_BG.SGO'
LAYOUT = 'BG_BASE_EDF41'                # the layout's name: src/edf5campaign.cpp looks it up by this
# The scenes that preload lyt_bg.sgo and its four backgrounds (MAINSCRIPT g_resource.Entry: the HQ, the online lobby /
# room, the result screen): a layout's model not in the scene's group is not loaded and draws nothing (real game,
# 2026-10-18: the fifth layout shown, the screen black). Each gets our four files after the stock BG_D ones.
GROUPS = ('UI/UIRESOURCEGROUP_HQ.SGO', 'UI/UIRESOURCEGROUP_ONLINE.SGO', 'UI/UIRESOURCEGROUP_RESULT.SGO')
FILES = (RAB, CAS, SGO, SKIN, LYT, *GROUPS)
STOCK_LAYOUTS = ('BG_BASE', 'BG_BASE_B', 'BG_BASE_C', 'BG_BASE_D')   # EDF.dll 0x8BE0E0's four

FOV_Y = struct.unpack('<f', struct.pack('<I', 0x3F75C28F))[0]   # 0.96, EDF41.exe 0x409190
TAN_Y = math.tan(FOV_Y / 2)
HALF_H = 5.4                            # EDF6: 1080 px / 2 / 100
S = HALF_H / TAN_Y                      # model units at depth 1 in 4.1 -> EDF6 model units
ASPECT = 16 / 9
NEAR_KEEP = 1.0                         # geometry nearer than this (in front of the frustum's useful part) is cut
SIDE_KEEP = 1.15                        # and beyond this many half-widths / half-heights of the screen
DEPTH_NEAR, DEPTH_FAR = 8.0, 32.0       # 4.1's nearest / farthest layer distances, a little beyond
DEPTH_RANGE = 1.5                       # EDF6's layers sit at z = 0 .. 1.6


def depth(z: float) -> float:
    """EDF6 z of a point at 4.1 view z (< 0): projective (a + b / -z), nearer is larger, 0 at DEPTH_FAR."""
    d = max(-z, 1e-3)
    return DEPTH_RANGE * (1 / d - 1 / DEPTH_FAR) / (1 / DEPTH_NEAR - 1 / DEPTH_FAR)


def depth_slope(z: float) -> float:
    """d depth / d z at z."""
    d = max(-z, 1e-3)
    return DEPTH_RANGE / (1 / DEPTH_NEAR - 1 / DEPTH_FAR) / (d * d)


def project(p: tuple[float, float, float]) -> tuple[float, float, float]:
    """A 4.1 view-space point -> EDF6 model units (X, Y as 4.1's camera shows it, Z = depth). EDF6's UI is y down: a
    model's +Y is down the screen (UI/BG.RAB's world map strip, y -3.1 .. 0.9, shows above the middle; EDF6's BG faces
    wind clockwise in x/y, 4.1's anticlockwise), so Y is negated: the model mirrored, the picture not."""
    x, y, z = p
    d = max(-z, 1e-3)
    return S * x / d, -S * y / d, depth(z)


# ------------------------------------------------------------------------------------------- reading 4.1
class Source:
    """4.1's MENU files: from an EDF4.1 install (Root.cpk) or an asset pack (pylib/edf41src.py)."""

    def __init__(self, root41: str) -> None:
        self.root41 = root41
        self.pack = edf41src.pack(root41) if edf41src.is_pack(root41) else None
        self.game = None if self.pack is not None else rootcpk.Game(root41)

    def read(self, rel: str) -> bytes:
        if self.pack is not None:
            return self.pack.read(rel)
        folder, name = rel.split('/')
        return self.game.read(folder, name)

    def fingerprint(self, rel: str) -> list[int]:
        """The archive's record of the file ([unpacked size, CRC]); a pack's manifest records the same."""
        if self.pack is not None:
            why = edf41src.problems(self.root41, [rel])
            if why:
                raise ValueError(why[0])
            return self.pack.source(rel)
        folder, name = rel.split('/')
        entry = self.game.cpk.index[self.game._key(folder, name)]
        return [int(entry['ExtractSize']), int(entry['CRC'])]


# ------------------------------------------------------------------------------------------- the clip's pose
Mat = list[float]


def quat_matrix(q: tuple[float, float, float, float]) -> Mat:
    """xyzw -> 4x4 row-vector matrix (p' = p * M, as pylib/mdb.py): the transpose of the column-vector rotation."""
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [r[0][0], r[1][0], r[2][0], 0.0, r[0][1], r[1][1], r[2][1], 0.0,
            r[0][2], r[1][2], r[2][2], 0.0, 0.0, 0.0, 0.0, 1.0]


def euler_matrix(e: tuple[float, float, float]) -> Mat:
    return quat_matrix(cas_legacy.euler_to_quat(e))


class Pose:
    """Per bone name: the clip's translation channel index, rotation channel index and its Euler keys."""

    def __init__(self, cas: bytes) -> None:
        layout = cas_legacy.cas_layout(cas)
        canm = layout.canm
        if layout.version != cas_legacy.CAS_41 or canm is None or len(canm.clips) != 1:
            raise ValueError('MENU_BG01.CAS is not the one-clip 4.1 file read here')
        self.canm = canm
        self.tracks: dict[str, tuple[int, int, int]] = {}
        for bone, t, r, s in canm.clips[0].tracks:
            self.tracks[canm.names[bone]] = (t, r, s)
        self.euler = {name: cas_legacy._decode_euler(canm.channels[r], cas) if r >= 0 else [(0.0, 0.0, 0.0)]
                      for name, (t, r, s) in self.tracks.items()}

    def translation(self, name: str) -> tuple[float, float, float] | None:
        t = self.tracks.get(name, (-1, -1, -1))[0]
        if t < 0:
            return None
        ch = self.canm.channels[t]
        if ch.keys_at is not None:
            raise ValueError(f'MENU_BG01.CAS moves {name}: only still positions are reprojected')
        return tuple(ch.base[:3])

    def spins(self, name: str) -> bool:
        """The clip turns the bone (its Euler keys change)."""
        keys = self.euler.get(name, [(0.0, 0.0, 0.0)])
        return any(k != keys[0] for k in keys)

    def spins_about_normal(self, name: str) -> bool:
        """The bone's local Z axis stays where the first key puts it all clip long (a turn about its own normal)."""
        keys = self.euler.get(name, [(0.0, 0.0, 0.0)])
        z0 = euler_matrix(keys[0])[8:11]
        return all(max(abs(a - b) for a, b in zip(euler_matrix(k)[8:11], z0)) < 2e-3 for k in keys)

    def axis_spin(self, name: str) -> bool:
        """The clip turns the bone about the view axis only (its X / Y Euler angles stay 0)."""
        return all(abs(k[0]) < 1e-4 and abs(k[1]) < 1e-4 for k in self.euler.get(name, [(0.0, 0.0, 0.0)]))

    def matrix(self, name: str, bind: Mat) -> Mat:
        """The bone's local matrix at the clip's first key (the bind's where the clip has no track)."""
        if name not in self.tracks:
            return bind
        m = euler_matrix(self.euler[name][0])
        t = self.translation(name)
        m[12:15] = list(t) if t is not None else bind[12:15]
        return m


# ------------------------------------------------------------------------------------------- vertices
_FMT = {1: '<4f', 4: '<3f', 7: '<4e', 12: '<2f', 21: '<4B'}


def decode(me: mdb.Mesh) -> list[list[tuple[float, ...]]]:
    """Every vertex as one tuple per element (me.elems order)."""
    out = []
    for v in range(me.nverts):
        base = v * me.vsize
        out.append([struct.unpack_from(_FMT[e.fmt], me.vdata, base + e.offset) for e in me.elems])
    return out


def encode(me: mdb.Mesh, verts: list[list[tuple[float, ...]]]) -> bytes:
    out = bytearray(me.vsize * len(verts))
    for v, elems in enumerate(verts):
        for e, val in zip(me.elems, elems):
            if e.fmt == 21:
                val = tuple(max(0, min(255, int(round(x)))) for x in val)
            struct.pack_into(_FMT[e.fmt], out, v * me.vsize + e.offset, *val)
    return bytes(out)


def _pos_index(me: mdb.Mesh) -> int:
    return next(i for i, e in enumerate(me.elems) if e.name.lower() == 'position')


def _lerp(a: list[tuple[float, ...]], b: list[tuple[float, ...]], t: float) -> list[tuple[float, ...]]:
    return [tuple(x + (y - x) * t for x, y in zip(ea, eb)) for ea, eb in zip(a, b)]


def _transform(p: tuple[float, ...], m: Mat) -> tuple[float, float, float]:
    x, y, z = p[0], p[1], p[2]
    return (x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14])


def _planes() -> list[Callable[[tuple[float, float, float]], float]]:
    """The kept region in 4.1 view space, each plane >= 0 inside."""
    kx, ky = SIDE_KEEP * TAN_Y * ASPECT, SIDE_KEEP * TAN_Y
    return [lambda p: -p[2] - NEAR_KEEP,
            lambda p: kx * -p[2] - p[0], lambda p: kx * -p[2] + p[0],
            lambda p: ky * -p[2] - p[1], lambda p: ky * -p[2] + p[1]]


def clip_polygon(poly: list[tuple[list[tuple[float, ...]], tuple[float, float, float]]]) -> list:
    """Sutherland-Hodgman against _planes(): poly = [(vertex elements, view position)], attributes interpolated
    linearly in space (they are linear on the triangle)."""
    for plane in _planes():
        out = []
        for i, cur in enumerate(poly):
            prev = poly[i - 1]
            dc, dp = plane(cur[1]), plane(prev[1])
            if dc >= 0:
                if dp < 0:
                    t = dp / (dp - dc)
                    out.append((_lerp(prev[0], cur[0], t), tuple(a + (b - a) * t for a, b in zip(prev[1], cur[1]))))
                out.append(cur)
            elif dp >= 0:
                t = dp / (dp - dc)
                out.append((_lerp(prev[0], cur[0], t), tuple(a + (b - a) * t for a, b in zip(prev[1], cur[1]))))
        poly = out
        if not poly:
            break
    return poly


def project_static(me: mdb.Mesh, world: Mat) -> None:
    """A still bone's mesh: every vertex where 4.1's camera shows it (clipped to the kept region), back in the bone's
    frame (`world` = the bone's matrix in the clip's pose)."""
    pi = _pos_index(me)
    verts = decode(me)
    inv = mdb.inverse_affine(world)
    tris = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
    out_verts: list[list[tuple[float, ...]]] = []
    out_idx: list[int] = []
    cache: dict[int, int] = {}

    def emit(elems: list[tuple[float, ...]], view: tuple[float, float, float], key: int | None) -> int:
        if key is not None and key in cache:
            return cache[key]
        local = _transform(project(view), inv)
        e = list(elems)
        w = e[pi][3:] if len(e[pi]) > 3 else ()
        e[pi] = (*local, *w)
        out_verts.append(e)
        if key is not None:
            cache[key] = len(out_verts) - 1
        return len(out_verts) - 1
    views = [_transform(v[pi], world) for v in verts]
    for k in range(0, len(tris) - 2, 3):
        idx = tris[k:k + 3]
        if all(min(pl(views[i]) for pl in _planes()) >= 0 for i in idx):
            out_idx += [emit(verts[i], views[i], i) for i in idx]
            continue
        poly = clip_polygon([(verts[i], views[i]) for i in idx])
        if len(poly) < 3:
            continue
        ids = [emit(e, p, None) for e, p in poly]
        for j in range(1, len(ids) - 1):
            out_idx += [ids[0], ids[j], ids[j + 1]]
    if len(out_verts) > 0xFFFF:
        raise ValueError('a clipped mesh needs more than 65535 vertices')
    me.vdata = encode(me, out_verts)
    me.indices = struct.pack(f'<{len(out_idx)}H', *out_idx)


def scale_on_axis(me: mdb.Mesh) -> None:
    """A bone on the view axis that the clip turns about that axis (the three big circles): each vertex (its frame is
    view space but for the turn) scaled by s of its own depth, its z the depth: the turn then turns the projection,
    exactly."""
    pi = _pos_index(me)
    verts = decode(me)
    for e in verts:
        p = e[pi]
        x, y, z = project((p[0], p[1], p[2]))
        e[pi] = (x, y, z, *p[3:])
    me.vdata = encode(me, verts)


def scale_off_axis(me: mdb.Mesh, s: float) -> None:
    """A flat disc off the axis that the clip turns about its own normal under a fixed tilt (BGCircle_map3 / _map4:
    their local z is 0 and the clip's local Z axis never moves): scaled by s in its own plane; its position is
    projected (reproject). First order: the perspective's shear across a disc 2.5 units wide at depth 20 is dropped."""
    pi = _pos_index(me)
    verts = decode(me)
    for e in verts:
        p = e[pi]
        if abs(p[2]) > 1e-4:
            raise ValueError('a turning disc off the axis is not flat in its own frame')
        e[pi] = (p[0] * s, -p[1] * s, 0.0, *p[3:])   # mirrored in y, as project(); its turn is mirrored too
    me.vdata = encode(me, verts)


def _bounds(md: mdb.Mdb) -> None:
    """Each mesh bone's half extent and centre (+0xA0 / +0xB0) from its vertices, in its frame."""
    lo: dict[int, list[float]] = {}
    hi: dict[int, list[float]] = {}
    for o in md.objects:
        for me in o.meshes:
            pi = _pos_index(me)
            for e in decode(me):
                p = e[pi]
                a, b = lo.setdefault(o.bone, [math.inf] * 3), hi.setdefault(o.bone, [-math.inf] * 3)
                for k in range(3):
                    a[k], b[k] = min(a[k], p[k]), max(b[k], p[k])
    for i, b in enumerate(md.bones):
        if i in lo:
            b.centre = [(lo[i][k] + hi[i][k]) / 2 for k in range(3)] + [1.0]
            b.half = [(hi[i][k] - lo[i][k]) / 2 for k in range(3)] + [1.0]


def reproject(model: bytes, pose: Pose) -> tuple[bytes, dict[str, tuple[float, float, float]]]:
    """4.1's model (MDB 0x20, already converted) reprojected (module doc); the new fixed positions of the spinning
    bones placed off the axis ({bone: xyz}, for the CAS)."""
    md = mdb.mdb_read(model)
    names = [md.name_of(b.name) for b in md.bones]
    world: list[Mat] = []
    for i, b in enumerate(md.bones):
        local = pose.matrix(names[i], b.local)
        world.append(local if b.parent < 0 else mdb.mmul(local, world[b.parent]))
    for i, b in enumerate(md.bones):
        if b.parent >= 0 and any(abs(x - y) > 1e-6 for x, y in zip(world[b.parent], mdb.ident())):
            if any(o.bone == i for o in md.objects):
                raise ValueError(f'{names[i]} hangs under a moved bone: not the layout reprojected here')
    moved: dict[str, tuple[float, float, float]] = {}
    for o in md.objects:
        name = names[o.bone]
        if not pose.spins(name):
            for me in o.meshes:
                project_static(me, world[o.bone])
            continue
        t = world[o.bone][12:15]
        if all(abs(x) < 1e-6 for x in t) and pose.axis_spin(name):
            for me in o.meshes:
                scale_on_axis(me)
            continue
        if not pose.spins_about_normal(name) or t[2] >= -NEAR_KEEP:
            raise ValueError(f'{name}: a turning bone neither on the axis nor a disc turning about its normal')
        for me in o.meshes:
            scale_off_axis(me, S / -t[2])
        moved[name] = project(tuple(t))
    for name, p in moved.items():   # the bind follows the clip's new fixed position (the clip sets it anyway)
        b = md.bones[names.index(name)]
        b.local[12:15] = list(p)
    bind = mdb.bind_world(md)
    for b, w in zip(md.bones, bind):
        b.inv_bind = mdb.inverse_affine(w)
    _bounds(md)
    return mdb.mdb_write(md), moved


def patch_cas(cas: bytes, pose: Pose, moved: dict[str, tuple[float, float, float]]) -> bytes:
    """4.1's CAS with the moved bones' fixed translations rewritten in place (each its own channel), and every turning
    bone's rotation mirrored in y as its model is (project): M R M with M = diag(1, -1, 1) is the Euler angles' X and Z
    negated (R = Rz Ry Rx), i.e. their base and step (the quantized keys stay)."""
    out = bytearray(cas)
    users: dict[int, int] = {}
    for t, r, _s in pose.tracks.values():
        for c in (t, r):
            if c >= 0:
                users[c] = users.get(c, 0) + 1
    for name, (_t, r, _s) in pose.tracks.items():
        if r < 0 or not pose.spins(name):
            continue
        if users[r] != 1:
            raise ValueError(f'{name} shares its rotation channel: cannot mirror it alone')
        ch = pose.canm.channels[r]
        base = struct.unpack_from('<3f', out, ch.at + 4)
        step = struct.unpack_from('<3f', out, ch.at + 16)   # 0x200 channel: ..., f32 base xyz, f32 step xyz, i32 keys
        struct.pack_into('<3f', out, ch.at + 4, -base[0], base[1], -base[2])
        struct.pack_into('<3f', out, ch.at + 16, -step[0], step[1], -step[2])
    for name, p in moved.items():
        t = pose.tracks[name][0]
        if users[t] != 1:
            raise ValueError(f'{name} shares its translation channel: cannot move it alone')
        ch = pose.canm.channels[t]
        struct.pack_into('<3f', out, ch.at + 4, *p)   # 0x200 channel: u16 keyed, u16 count, f32 base xyz
    return bytes(out)


# ------------------------------------------------------------------------------------------- building
def texture_names(model: bytes, members: list[str]) -> bytes:
    """The model's texture file names spelled as the RAB's members are: 4.1's MDB names BG_WorldMap.dds where its RAB
    holds bg_worldmap.dds (4.1 matched them ignoring case; EDF6's own files always spell them alike, and EDF6 crashed
    on this model with the 4.1 spelling: real game, 2026-10-18)."""
    md = mdb.mdb_read(model)
    by_lower = {m.lower(): m for m in members}
    for t in md.textures:
        if t.filename.lower() not in by_lower:
            raise ValueError(f'Menu_BG01.mdb names {t.filename}, which its RAB does not hold')
        t.filename = by_lower[t.filename.lower()]
    return mdb.mdb_write(md)


def lyt(stock: bytes) -> bytes:
    """EDF6's lyt_bg.sgo with LAYOUT added: a copy of BG_BASE on our skin, first in the tree (drawn under the four)."""
    ver, m = sgo.read(stock)
    if LAYOUT in m:
        raise ValueError(f'lyt_bg.sgo already has {LAYOUT}')
    if not all(name in m for name in STOCK_LAYOUTS) or m.get('xgs_ui_object_class') != 'HUiBG':
        raise ValueError('lyt_bg.sgo is not the stock HUiBG layout read here')
    entry = copy.deepcopy(m['BG_BASE'])
    entry[1] = f'app:/UI/{STEM}_skin.sgo'
    m[LAYOUT] = entry
    tree = m['layout_tree']
    children = tree[1]
    if [children[k][0] for k in range(0, len(children), 2)] != list(STOCK_LAYOUTS):
        raise ValueError('lyt_bg.sgo layout_tree is not the stock one')
    tree[1] = [[LAYOUT], 0, *children]
    return sgo.write_depth_first(ver, m)


def resource_group(stock: bytes) -> bytes:
    """A scene's resource group with our files listed right after the stock backgrounds' (APP:/UI/BG_D.CAS)."""
    ver, m = sgo.read(stock)
    names = m.get('resource')
    anchor = 'APP:/UI/BG_D.CAS'
    if not isinstance(names, list) or anchor not in names or 'APP:/UI/LYT_BG.SGO' not in names:
        raise ValueError("the resource group does not load lyt_bg.sgo's backgrounds as read here")
    ours = [f'APP:/{rel}' for rel in (SKIN, SGO, RAB, CAS)]
    if any(n in names for n in ours):
        raise ValueError('the resource group already lists the 4.1 background')
    at = names.index(anchor) + 1
    m['resource'] = names[:at] + ours + names[at:]
    return sgo.write_depth_first(ver, m)


def build(root: str, root41: str) -> dict[str, bytes]:
    """The files (rel under Mods -> bytes) from the EDF4.1 (or asset pack) at root41 and EDF6's own lyt_bg.sgo."""
    import legacy_assets
    src = Source(root41)
    sgo41 = src.read(SOURCES[0])
    rab41 = mdb.rab_read(src.read(SOURCES[1]))
    cas41 = src.read(SOURCES[2])
    pose = Pose(cas41)
    member = next((f for f in rab41.files if f.name == MODEL), None)
    if member is None:
        raise ValueError(f'MENU_BG01.RAB has no {MODEL}')
    model = mdb_legacy.mdb_from_legacy(mdb.cmpl_decompress(member.stored))
    model, moved = reproject(model, pose)
    model = texture_names(model, [f.name for f in rab41.files])
    stored = mdb.cmpl_compress(model) if member.stored[:4] == b'CMPL' else model
    rab41.files = [mdb.RabFile(f.name, f.folder, f.flag, stored if f is member else f.stored, f.unk)
                   for f in rab41.files]
    cas6 = legacy_assets.convert('MENU/MENU_BG01.CAS', patch_cas(cas41, pose, moved))
    ver, m41 = sgo.read(sgo41)
    play = m41.get('default_play_animation')
    if not isinstance(play, list) or 'material_animation' not in m41:
        raise ValueError('Menu_BG01.sgo is not the 4.1 file read here')
    model_sgo = {'animation_model': [[[f'app:/UI/{STEM}.rab', MODEL]], f'app:/UI/{STEM}.cas', 0],
                 'default_play_animation': play, 'material_animation': m41['material_animation']}
    skin = {'base': f'app:/UI/{STEM}.sgo', 'default': [[0, 2], list(play)], 'object_class': 'Model'}
    game = rootcpk.Game(root)
    out = {RAB: mdb.rab_write(rab41), CAS: cas6, SGO: sgo.write_depth_first(ver, model_sgo),
           SKIN: sgo.write_depth_first(ver, skin), LYT: lyt(game.read('UI', 'LYT_BG.SGO'))}
    for rel in GROUPS:
        out[rel] = resource_group(game.read(*rel.split('/')))
    return out


# ------------------------------------------------------------------------------------------- installing
NO_EDF41 = '没有找到 EDF4.1 也没有 EDF4.1 素材包（4.1 的本部背景要从其中之一转换）'


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    return not owners or any(o != OWNER for o in owners) or led.changed(rel)


def unavailable(root: str, root41: str | None) -> str | None:
    """Why nothing is installed (before converting), None when it can be."""
    if not root41:
        return NO_EDF41
    foreign = [rel for rel in FILES if _foreign(ledger.Ledger(root), rel)]
    if foreign:
        return f"Mods 里已有别的 MOD 的文件：{', '.join(foreign)}"
    bad = edf41src.problems(root41, list(SOURCES))
    return '；'.join(bad) if bad else None


def install(root: str, root41: str | None, say: Callable[[str], None] = print) -> tuple[list[str], list[str]]:
    """The background written as OWNER's (paths written, [why not]); current files from the same 4.1 files and this
    VERSION are not converted again; when it cannot be installed, what an earlier install wrote is taken out."""
    led = ledger.Ledger(root)
    stamp_path = os.path.join(root, 'Mods', STAMP)
    why = unavailable(root, root41)
    if why is None:
        try:
            src = Source(root41)
            source = {rel: src.fingerprint(rel) for rel in SOURCES}
        except (ValueError, KeyError, OSError) as e:
            why = f'源文件不可用：{type(e).__name__}: {e}'
    if why is not None:
        led.release(OWNER, sorted(set(led.owned_by(OWNER))))
        if os.path.exists(stamp_path):
            os.remove(stamp_path)
        return [], [why]
    stamp = load_json(stamp_path, {})
    mine = set(led.owned_by(OWNER))
    if stamp.get('version') == VERSION and stamp.get('source') == source and \
            all(ledger.key(rel) in mine and os.path.exists(led.disk(rel)) for rel in FILES):
        return [], []
    say('  转换 4.1 的本部（出击界面）背景……')
    try:
        files = build(root, root41)
    except (ValueError, KeyError, IndexError, AssertionError, NotImplementedError, struct.error) as e:
        led.release(OWNER, sorted(mine))
        return [], [f'转换失败：{type(e).__name__}: {e}']
    written = []
    for rel, data in files.items():
        if ledger.key(rel) in mine and sha256_file(led.disk(rel)) == sha256(data):
            continue
        written.append(led.put(OWNER, rel, data))
    led.release(OWNER, sorted(mine - {ledger.key(rel) for rel in files}))
    with open(stamp_path, 'w', encoding='utf-8') as f:
        json.dump({'version': VERSION, 'source': source}, f, indent=1)
    return written, []


def remove(root: str) -> tuple[list[str], list[str]]:
    stamp = os.path.join(root, 'Mods', STAMP)
    if os.path.exists(stamp):
        os.remove(stamp)
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main(argv: list[str]) -> int:
    """python tools/edf41_menu_bg.py OUT_DIR [GAME_DIR] [EDF41_DIR]: the files written to OUT_DIR (a preview; nothing
    in the game is touched)."""
    import gamedir
    out = argv[0]
    root = argv[1] if len(argv) > 1 else rootcpk.DEFAULT_GAME
    root41 = argv[2] if len(argv) > 2 else edf41src.find(near=root)
    if not root41:
        print(NO_EDF41)
        return 1
    del gamedir
    for rel, data in build(root, root41).items():
        path = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
        print(path, len(data))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
