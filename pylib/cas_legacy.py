"""EDF4.1 / EDF5 CAS files (u32 0x04 = 0x200 / 0x203, embedded CANM 0x200) turned into EDF6's (0x204, CANM 0x300).

    cas_layout(b) -> CasLayout      every struct of a 0x200 / 0x203 / 0x204 CAS, checked as below; ValueError otherwise
    cas_from_legacy(b) -> bytes     the 0x204 file; ValueError on anything not laid out as below (never guesses)
    euler_to_quat(e) -> xyzw        EDF.dll's own Euler -> quaternion, bit for bit

EDF.dll (EDF6) accepts only {'CAS\\0', 0x204} (RVA 0x1180191) and CANM 0x300 (0x1160A82). Struct names follow the
KCreator EDF wiki (CAS-Format / CANM-Format); i32 offsets count from the start of the struct holding them, the
header's from the file's start.

CAS (all three versions; only the version word, the data struct's size and some values differ):
  header 0x30: 'CAS\\0', u32 version, u32 CANM (16-aligned, zero padding before it), then (count, offset) of the
       tcontrols (0xC: name, n, words; a word = u32 CANM clip index), vcontrols (0x14: name, ...), anmgroups
       (0xC: name, n, mcanms), bones (4: name), and u32 the unnamed datagroup;
  mcanm 0x24: name, first anmdata, n more, more (0x20 each, contiguous), three datagroups (0 = none), u32 type, value;
  anmdata 0x20: i32, f32, datagroup (0 = none), u32 type, value, u32 type +0x14, +0x18 value, +0x1C;
  datagroup 8: n, data (always 8: right after it); data: u32 command id + arguments, 0x24 bytes in 4.1, 0x34 in 5/6;
  serialisation order (re-emitting every CAS of the three games from its tree gives its bytes back, 836 / 836):
       header, tcontrols, the tcontrols' words (tcontrols taken in name order), vcontrols, bones, the unnamed group,
       anmgroups, then per anmgroup in name order: its mcanm array, then per mcanm: first anmdata, its group, the
       more-array, their groups, the three groups; an empty array points where it would start; pad to 16; the CANM;
  the strings (CANM's: Scene_Root first then sorted; then CAS's) run from the end of the CANM's bone-name table to
       the end of the file; converted, that blob is copied byte for byte and every offset into it re-aimed.
CANM (both): header 0x20 'CANM', u32 version, (n, off) of clips, channels (off 0x20), bone names; then channels,
  key blocks, zero pad to 4, clips (28: loop, name, f32 duration, f32 step, frames, n tracks, tracks), the tracks
  (8: u16 bone, i16 translation / rotation / scale channel, -1 none) clip after clip, the bone-name table, strings.
  0x200 channel 0x20: u16 keyed, u16 count, f32 base xyz, f32 step xyz, i32 keys; keys u16 xyz each, value =
       base + k * step; a rotation is Euler radians, R = Rz Ry Rx.
  0x300 channel 0x30: f32 base xyzw, f32 step xyzw, i32 keys, i32 kind, i32 count, 0. kind 0 static vector (w 1, step
       0), 1 quantized vector (EDF5's keys unchanged; base w 1, step w 1), 2 static quaternion (in base), 3 one f32
       xyzw per key (base and step all 0). EDF.dll's track evaluator (0x1160500) reads kind-2 bases and kind-3 keys
       with MOVAPS, so channel table and quaternion blocks are 16-byte aligned. (It would also take kinds 0 / 1 in a
       rotation slot as Euler through 0x649A0, but no EDF6 file does.)

Conversion (EDF6's own converter's choices, recovered from the 230 EDF5 / EDF6 same-named pairs):
  CAS: version 0x204; data grows to 0x34 (4.1: 16 zero bytes); command ids renumbered (_OP_5_TO_6, _OP_41_TO_5:
       each version inserted ids); zero where EDF6 leaves a slot unused and the old tools left stale bytes: data
       commands 0 / 1 of type 2 (+0x14) at +0x18, command 2 at +0x28 (EDF5 repeated +0x20 there), anmdata +0x1C
       unless +0x14 is 1 (4.1 held stack addresses there).
  CANM: one 0x300 channel per (role, old channel), numbered by first use over clips -> tracks -> translation,
       rotation, scale (EDF5 shares one zero channel between a translation and a rotation; EDF6 can't); vector
       blocks first in EDF5's block order, then the quaternion blocks (16-aligned), then pad to 4; an Euler key is
       decoded in float32 (k * step, + base: EDF.dll's own order) and converted by euler_to_quat (EDF.dll 0x649A0,
       UCRT sinf / cosf, which EDF.dll imports).

Evidence (2026-10-10, jobs/4bf89026/tmp/cas: verify.py, masked.py, verify41.py, native.py, roundtrip.py):
  cas_layout reads every CAS in the three Root.cpk (4.1 205, EDF5 256, EDF6 375); every one round-trips.
  EDF5 -> 0x204 against the 230 EDF6 files of the same name: 0 raise; 66 byte for byte; 21 EDF6 re-authored
  (bone names / clips / CAS commands / channel layout 18, vector values 3); the other 143 are byte for byte except
  quaternion floats (and, in 69 of them, the order of the quaternion key blocks): every static quaternion is bit
  exact (1487 / 1487), while of 1958796 animated quaternion keys 88038 are bit exact and the rest differ by less
  than the Euler key's quantization step (+ float spacing of the angle): EDF6 converted the unquantized source
  angles, which an EDF5 file no longer holds - no converter can reproduce those bits.
  EDF4.1 -> 0x204: 204 / 205 convert (GIANTANT_HOKAO uses 4.1 commands 2 / 3, which no EDF5 / EDF6 file shows:
  ValueError) and read back (cas_layout, cas_pose.CasPose; same clips, tracks, names, data count; vector keys
  byte for byte, every quaternion = euler_to_quat of the Euler key); 141 of the 159 also in EDF5 give exactly the
  bytes EDF5's file gives (the other 18 differ in content: names / clips / commands), 47 exactly EDF6's file.
  EDF.dll's evaluator (0x1160500, private DONT_RESOLVE mapping; the running game untouched) on every converted
  file: no fault but the one its slerp helper also hits on stock EDF6 files without CRT init; values as stored.

Not reproduced: animated quaternions bit for bit (see above); the quaternion key blocks' order in 69 files (EDF6's
order is neither EDF5's, by bytes, by hash FNV-1a, by channel nor by count; offsets stay consistent, the game reads
them by offset). Off Windows (no UCRT) sinf / cosf fall back to correctly rounded math, which differs from UCRT by an
ulp near rounding ties (4 of the 1487 static quaternions here).
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from typing import Callable

CAS_41 = 0x200
CAS_5 = 0x203
CAS_6 = 0x204
CANM_OLD = 0x200
CANM_6 = 0x300

COMMON_SIZE = {CAS_41: 0x24, CAS_5: 0x34, CAS_6: 0x34}
TCTL, VCTL, GRP, MC, AD, CGRP, BONE = 12, 0x14, 12, 0x24, 0x20, 8, 4
CLIP, TRACK = 28, 8
CHAN = {CANM_OLD: 0x20, CANM_6: 0x30}

# data command id (u32 +0 of a 'data' struct) across versions: each version inserted new ids, shifting the rest.
# Only ids every EDF5 (resp. EDF4.1) CAS actually uses, each matched to the EDF6 (EDF5) one at the same place in the
# same-named file (same anmgroup / mcanm / slot / index); anything else raises (where it lands is unknown).
_OP_5_TO_6: dict[int, int] = {0: 0, 1: 1, 2: 2, 6: 8, 9: 11, 10: 12, 11: 13, 12: 14, 13: 15, 14: 16, 15: 17, 16: 18,
                              17: 19, 22: 24, 28: 30, 29: 31, 30: 32, 31: 33, 33: 35}
_OP_41_TO_5: dict[int, int] = {0: 0, 1: 1, 4: 6, 7: 9, 8: 10, 9: 11, 10: 12, 11: 13, 12: 14, 13: 15, 14: 16, 16: 22,
                               22: 28, 23: 29, 24: 30, 25: 31, 27: 33}
_OPS: dict[int, dict[int, int]] = {CAS_5: _OP_5_TO_6, CAS_41: {o: _OP_5_TO_6[n] for o, n in _OP_41_TO_5.items()}}


def _u(b: bytes, fmt: str, at: int) -> tuple:
    if at < 0 or at + struct.calcsize(fmt) > len(b):
        raise ValueError(f'CAS read {fmt} at {at:#x} outside the file')
    return struct.unpack_from(fmt, b, at)


def _text_end(b: bytes, at: int) -> int:
    end = at
    while True:
        if end + 2 > len(b):
            raise ValueError(f'unterminated string at {at:#x}')
        if b[end:end + 2] == b'\0\0':
            return end + 2
        end += 2


def _text(b: bytes, at: int) -> str:
    return b[at:_text_end(b, at) - 2].decode('utf-16le')


@dataclass
class Channel:
    at: int
    kind: int                 # 0x300 kinds: 0 static vec, 1 quantized vec, 2 static quat, 3 float quat; 0x200: flag
    count: int
    base: tuple[float, ...]
    mul: tuple[float, ...]
    keys_at: int | None       # absolute
    raw: bytes


@dataclass
class Clip:
    at: int
    loop: int
    name: str
    name_at: int
    duration: float
    step: float
    frames: int
    tracks_at: int
    tracks: list[tuple[int, int, int, int]]


@dataclass
class Canm:
    at: int
    version: int
    clips: list[Clip]
    channels: list[Channel]
    names: list[str]
    name_ats: list[int]
    channels_at: int
    clips_at: int
    names_at: int


@dataclass
class Group:
    at: int
    data: list[int]                    # each datum's start


@dataclass
class AnmData:
    at: int
    group: Group | None


@dataclass
class McAnm:
    at: int
    first: AnmData
    more: list[AnmData]
    groups: list[Group | None]         # +0x10, +0x14, +0x18


@dataclass
class Tree:
    tcontrols: list[tuple[int, list[int]]]       # (tcontrol, its animation-index words)
    vcontrols: list[int]
    bones: list[int]
    anmgroups: list[tuple[int, list[McAnm]]]
    unnamed: Group


@dataclass
class CasLayout:
    version: int
    canm_at: int
    structs: dict[int, tuple[str, int]] = field(default_factory=dict)   # CAS struct start -> (kind, size)
    string_fields: list[int] = field(default_factory=list)              # CAS fields: i32 string offset from field
    strings: set[int] = field(default_factory=set)
    canm: Canm | None = None
    strings_at: int = 0       # the string blob (CANM strings, then CAS strings) runs from here to the file's end
    tree: Tree | None = None  # the struct tree, for re-serialising


class _Walk:
    def __init__(self, b: bytes, version: int) -> None:
        self.b = b
        self.common = COMMON_SIZE[version]
        self.lay = CasLayout(version, _u(b, '<I', 8)[0])
        if not 0x30 <= self.lay.canm_at <= len(b):
            raise ValueError(f'CAS CANM offset {self.lay.canm_at:#x} outside the file ({len(b):#x} bytes)')
        self.tree: Tree | None = None

    def mark(self, at: int, kind: str, size: int) -> None:
        if at < 0x30 or at + size > self.lay.canm_at:
            raise ValueError(f'CAS {kind} at {at:#x} outside the struct area')
        old = self.lay.structs.get(at)
        if old is not None:
            raise ValueError(f'CAS {kind} at {at:#x} reached twice (already a {old[0]})')
        self.lay.structs[at] = (kind, size)

    def string(self, field_at: int) -> None:
        to = field_at + _u(self.b, '<i', field_at)[0]
        _text_end(self.b, to)
        self.lay.string_fields.append(field_at)
        self.lay.strings.add(to)

    def run(self) -> CasLayout:
        b = self.b
        _canm, tn, to, vn, vo, gn, go, bn, bo, uc = _u(b, '<10I', 8)
        tcs = []
        for i in range(tn):
            at = to + i * TCTL
            self.mark(at, 'tcontrol', TCTL)
            self.string(at)
            n, no = _u(b, '<ii', at + 4)
            words = []
            for j in range(n):
                self.mark(at + no + j * 4, 'tcontrol_anim', 4)
                words.append(at + no + j * 4)
            tcs.append((at, words))
        vcs = []
        for i in range(vn):
            at = vo + i * VCTL
            self.mark(at, 'vcontrol', VCTL)
            self.string(at)
            vcs.append(at)
        grps = []
        for i in range(gn):
            at = go + i * GRP
            self.mark(at, 'anmgroup', GRP)
            self.string(at)
            n, mo = _u(b, '<ii', at + 4)
            grps.append((at, [self.mcanm(at + mo + j * MC) for j in range(n)]))
        bones = []
        for i in range(bn):
            at = bo + i * BONE
            self.mark(at, 'bone', BONE)
            self.string(at)
            bones.append(at)
        self.tree = Tree(tcs, vcs, bones, grps, self.group(uc))
        return self.lay

    def group(self, at: int) -> Group:
        self.mark(at, 'datagroup', CGRP)
        n, o = _u(self.b, '<ii', at)
        data = []
        for j in range(n):
            self.mark(at + o + j * self.common, 'data', self.common)
            data.append(at + o + j * self.common)
        return Group(at, data)

    def mcanm(self, at: int) -> McAnm:
        self.mark(at, 'mcanm', MC)
        self.string(at)
        a1, n2, o2, f1, f2, f3 = _u(self.b, '<6i', at + 4)
        first = self.anmdata(at + a1)
        more = [self.anmdata(at + o2 + j * AD) for j in range(n2)]
        return McAnm(at, first, more, [self.group(at + f) if f else None for f in (f1, f2, f3)])

    def anmdata(self, at: int) -> AnmData:
        self.mark(at, 'anmdata', AD)
        g = _u(self.b, '<i', at + 8)[0]
        return AnmData(at, self.group(at + g) if g else None)


def _parse_canm(b: bytes, base: int) -> Canm:
    sig, ver, nc, co, nch, po, nn, no = _u(b, '<4sI6I', base)
    if sig != b'CANM' or ver not in CHAN:
        raise ValueError(f'not a CANM 0x200 / 0x300 at {base:#x}')
    clips = []
    for i in range(nc):
        at = base + co + i * CLIP
        loop, so, dur, step, frames, cnt, off = _u(b, '<IiffIIi', at)
        tracks = [_u(b, '<Hhhh', at + off + j * TRACK) for j in range(cnt)]
        clips.append(Clip(at, loop, _text(b, at + so), at + so, dur, step, frames, at + off, tracks))
    size = CHAN[ver]
    chans = []
    for i in range(nch):
        at = base + po + i * size
        if ver == CANM_OLD:
            flag, cnt = _u(b, '<HH', at)
            vals = _u(b, '<6f', at + 4)
            ko = _u(b, '<i', at + 0x1C)[0]
            if flag not in (0, 1) or (flag == 0 and (ko != 0 or cnt != 1)) or (flag and cnt < 1):
                raise ValueError(f'CANM 0x200 channel {i} not static (flag 0, 1 key) or keyed (flag 1)')
            kat = at + ko if flag else None
            if kat is not None:
                _u(b, f'<{3 * cnt}H', kat)
            chans.append(Channel(at, flag, cnt, vals[:3], vals[3:], kat, b[at:at + size]))
        else:
            vals = _u(b, '<8f', at)
            ko, kind, cnt, res = _u(b, '<4i', at + 0x20)
            if kind not in (0, 1, 2, 3) or res != 0 or cnt < 1 or (kind in (0, 2) and (ko or cnt != 1)):
                raise ValueError(f'CANM 0x300 channel {i} has an unknown encoding')
            kat = at + ko if kind in (1, 3) else None
            if kat is not None:
                _u(b, f'<{3 * cnt}H' if kind == 1 else f'<{4 * cnt}f', kat)
            chans.append(Channel(at, kind, cnt, vals[:4], vals[4:], kat, b[at:at + size]))
    for cl in clips:
        for bone, *chs in cl.tracks:
            if bone >= nn or any(c < -1 or c >= nch for c in chs):
                raise ValueError(f'CANM clip {cl.name!r} points outside its tables')
    name_ats = []
    for i in range(nn):
        at = base + no + i * 4
        name_ats.append(at + _u(b, '<i', at)[0])
    names = [_text(b, a) for a in name_ats]
    return Canm(base, ver, clips, chans, names, name_ats, base + po, base + co, base + no)


def cas_layout(b: bytes) -> CasLayout:
    """Every struct of a CAS 0x200 / 0x203 / 0x204 (wiki layout, checked: no overlaps, every byte of the struct area
    covered or zero padding, the CANM's tables in bounds, strings NUL-terminated)."""
    if len(b) < 0x30 or b[:4] != b'CAS\0':
        raise ValueError('not a CAS file')
    version = _u(b, '<I', 4)[0]
    if version not in COMMON_SIZE:
        raise ValueError(f'unknown CAS version {version:#x}')
    walk = _Walk(b, version)
    lay = walk.run()
    lay.tree = walk.tree
    cover = bytearray(lay.canm_at)
    cover[:0x30] = b'\1' * 0x30
    for at, (kind, n) in sorted(lay.structs.items()):
        if any(cover[at:at + n]):
            raise ValueError(f'CAS {kind} at {at:#x} overlaps another struct')
        cover[at:at + n] = b'\1' * n
    if any(b[i] for i in range(lay.canm_at) if not cover[i]):
        raise ValueError('CAS struct area holds bytes no struct accounts for')
    if lay.canm_at % 16:
        raise ValueError('CANM not 16-byte aligned')
    lay.canm = _parse_canm(b, lay.canm_at)
    want = CANM_OLD if version in (CAS_41, CAS_5) else CANM_6
    if lay.canm.version != want:
        raise ValueError(f'CAS {version:#x} embeds CANM {lay.canm.version:#x}')
    canm_strings = {c.name_at for c in lay.canm.clips} | set(lay.canm.name_ats)
    every = canm_strings | lay.strings
    lay.strings_at = min(every) if every else len(b)
    if lay.strings_at != lay.canm.names_at + 4 * len(lay.canm.names):
        raise ValueError('CAS strings do not start right after the CANM bone-name table')
    return lay


# ---------------------------------------------------------------------------------------------- conversion

def _f32(x: float) -> float:
    """`x` rounded to a float32; ValueError when it does not fit one (a channel's base / step out of any range)."""
    try:
        return struct.unpack('<f', struct.pack('<f', x))[0]
    except OverflowError as e:
        raise ValueError(f'{x!r} does not fit a float32') from e


def _crt_sincosf() -> tuple[Callable[[float], float], Callable[[float], float]] | None:
    """The UCRT's sinf / cosf - what EDF.dll imports (api-ms-win-crt-math sinf / cosf) - when on Windows."""
    try:
        import ctypes
        crt = ctypes.CDLL('ucrtbase')
        out = []
        for name in ('sinf', 'cosf'):
            fn = getattr(crt, name)
            fn.restype, fn.argtypes = ctypes.c_float, [ctypes.c_float]
            out.append(fn)
        return out[0], out[1]
    except (OSError, AttributeError):
        return None


_CRT = _crt_sincosf()


def _sinf(h: float) -> float:
    return float(_CRT[0](h)) if _CRT else _f32(math.sin(h))


def _cosf(h: float) -> float:
    return float(_CRT[1](h)) if _CRT else _f32(math.cos(h))


def euler_to_quat(e: tuple[float, float, float]) -> tuple[float, float, float, float]:
    """EDF5 Euler radians (R = Rz Ry Rx) -> EDF6 xyzw, exactly as EDF.dll's own Euler->quaternion routine (RVA
    0x649A0, which the runtime also uses for Euler-coded rotation channels): rotate about X, then Y, then Z, each
    step in float32, an axis skipped when its angle squared is not > 0, sinf / cosf of angle * 0.5. Skipping and
    the explicit negation in the Y step decide the sign of the zero components."""
    f = _f32
    x, y, z = (f(v) for v in e)

    def sc(a: float) -> tuple[float, float]:
        h = f(a * 0.5)
        return _sinf(h), _cosf(h)
    if f(x * x) > 0:
        qx, qw = sc(x)
    else:
        qx, qw = 0.0, 1.0
    if f(y * y) > 0:
        s, c = sc(y)
        qx, qw, qz, qy = f(qx * c), f(qw * c), -f(qx * s), f(qw * s)
    else:
        qy, qz = 0.0, 0.0
    if f(z * z) > 0:
        s, c = sc(z)
        qx, qy, qz, qw = (f(f(qx * c) - f(qy * s)), f(f(qy * c) + f(qx * s)), f(f(qw * s) + f(qz * c)),
                          f(f(qw * c) - f(qz * s)))
    return qx, qy, qz, qw


def _decode_euler(c: Channel, b: bytes) -> list[tuple[float, float, float]]:
    if c.keys_at is None:
        return [c.base]
    out = []
    for k in range(c.count):
        q = _u(b, '<3H', c.keys_at + 6 * k)
        out.append(tuple(_f32(c.base[i] + _f32(q[i] * c.mul[i])) for i in range(3)))
    return out


def _canm_300(b: bytes, old: Canm, old_strings_at: int) -> tuple[bytearray, int]:
    """CANM 0x200 -> 0x300 tables (everything but the strings) and where, relative to the CANM, the string blob
    starts. Old string `s` lands at (returned start) + (s - old_strings_at)."""
    # channels: one per (role, old channel), numbered in first use over clips -> tracks -> translation, rotation,
    # scale (EDF5 lets a zero translation and a zero Euler rotation share one channel; EDF6 can't: vector vs quat)
    order: list[tuple[str, int]] = []
    index: dict[tuple[str, int], int] = {}
    tracks: list[list[tuple[int, int, int, int]]] = []
    for cl in old.clips:
        rows = []
        for bone, t, r, s in cl.tracks:
            row = [bone]
            for role, ch in (('v', t), ('q', r), ('v', s)):
                if ch < 0:
                    row.append(-1)
                    continue
                key = (role, ch)
                if key not in index:
                    index[key] = len(order)
                    order.append(key)
                row.append(index[key])
            rows.append(tuple(row))
        tracks.append(rows)
    if len(order) > 0x7FFF:
        raise ValueError('too many CANM channels for an i16 index')

    # key blocks: EDF5's own order, vectors first, then the (16-byte aligned) quaternions
    blocks: dict[str, list[tuple[int, int]]] = {'v': [], 'q': []}
    for i, (role, ch) in enumerate(order):
        c = old.channels[ch]
        if c.keys_at is not None:
            blocks[role].append((c.keys_at, i))
    for role in blocks:
        blocks[role].sort()
        ats = [a for a, _ in blocks[role]]
        if len(set(ats)) != len(ats):
            raise ValueError('two CANM channels share one key block')

    head = 0x20 + 0x30 * len(order)
    keys = bytearray()
    where: dict[int, int] = {}
    for at, i in blocks['v']:
        c = old.channels[order[i][1]]
        where[i] = head + len(keys)
        keys += b[at:at + 6 * c.count]
    if blocks['q']:
        keys += bytes(-(head + len(keys)) % 16)
    for at, i in blocks['q']:
        c = old.channels[order[i][1]]
        where[i] = head + len(keys)
        for e in _decode_euler(c, b):
            keys += struct.pack('<4f', *euler_to_quat(e))
    keys += bytes(-(head + len(keys)) % 4)

    out = bytearray(struct.pack('<4sI6I', b'CANM', CANM_6, len(old.clips), head + len(keys), len(order), 0x20,
                                len(old.names), 0))
    for i, (role, ch) in enumerate(order):
        c = old.channels[ch]
        at = 0x20 + 0x30 * i
        ko = where[i] - at if i in where else 0
        if role == 'v':
            kind = 1 if c.keys_at is not None else 0
            out += struct.pack('<8f4i', *c.base, 1.0, *c.mul, 1.0 if kind else 0.0, ko, kind, c.count, 0)
        elif c.keys_at is None:
            out += struct.pack('<8f4i', *euler_to_quat(c.base), 0, 0, 0, 0, 0, 2, 1, 0)
        else:
            out += struct.pack('<8f4i', 0, 0, 0, 0, 0, 0, 0, 0, ko, 3, c.count, 0)
    out += keys

    clips_at = len(out)
    tracks_at = clips_at + CLIP * len(old.clips)
    names_at = tracks_at + TRACK * sum(len(t) for t in tracks)
    strings_at = names_at + 4 * len(old.names)
    pos = tracks_at
    for i, cl in enumerate(old.clips):
        at = clips_at + CLIP * i
        name = strings_at + (cl.name_at - old_strings_at)
        out += struct.pack('<IiffIIi', cl.loop, name - at, cl.duration, cl.step, cl.frames, len(cl.tracks), pos - at)
        pos += TRACK * len(cl.tracks)
    for rows in tracks:
        for row in rows:
            out += struct.pack('<Hhhh', *row)
    for i, s in enumerate(old.name_ats):
        at = names_at + 4 * i
        out += struct.pack('<i', strings_at + (s - old_strings_at) - at)
    struct.pack_into('<I', out, 0x1C, names_at)
    assert len(out) == strings_at
    return out, strings_at


class _Emit:
    """Re-serialises a CAS struct tree in the order EDF5's / EDF6's files use (round-trips every one of them)."""

    def __init__(self, b: bytes, lay: CasLayout, common: int) -> None:
        self.b = b
        self.lay = lay
        self.old_common = COMMON_SIZE[lay.version]
        self.common = common
        self.out = bytearray(b[:0x30])
        self.strings: list[tuple[int, int]] = []     # (new field, old string)
        self.kinds: dict[int, str] = {}              # new struct start -> kind
        self.sources: dict[int, int] = {}            # new struct start -> where it was copied from

    def put(self, old: int, size: int, kind: str, grow: int = 0) -> int:
        at = len(self.out)
        self.out += self.b[old:old + size] + bytes(grow)
        self.kinds[at] = kind
        self.sources[at] = old
        if old in self.lay.string_fields and kind in ('tcontrol', 'vcontrol', 'anmgroup', 'bone', 'mcanm'):
            self.strings.append((at, old + _u(self.b, '<i', old)[0]))
        return at

    def name(self, old: int) -> str:
        return _text(self.b, old + _u(self.b, '<i', old)[0])

    def rel(self, field_at: int, target: int, base: int) -> None:
        struct.pack_into('<i', self.out, field_at, target - base)

    def group(self, g: Group) -> int:
        at = self.put(g.at, CGRP, 'datagroup')
        self.rel(at + 4, len(self.out), at)
        for d in g.data:
            self.put(d, self.old_common, 'data', self.common - self.old_common)
        return at

    def anmdata(self, a: AnmData) -> int:
        at = self.put(a.at, AD, 'anmdata')
        return at

    def anmdata_group(self, a: AnmData, at: int) -> None:
        if a.group is not None:
            self.rel(at + 8, self.group(a.group), at)

    def run(self) -> bytearray:
        t: Tree = self.lay.tree
        o = self.out
        struct.pack_into('<I', o, 0x10, len(o))
        tcs = [self.put(at, TCTL, 'tcontrol') for at, _w in t.tcontrols]
        for new, (_at, words) in sorted(zip(tcs, t.tcontrols), key=lambda r: self.name(r[1][0])):   # by name
            self.rel(new + 8, len(o), new)
            for w in words:
                self.put(w, 4, 'tcontrol_anim')
        struct.pack_into('<I', o, 0x18, len(o))
        for at in t.vcontrols:
            self.put(at, VCTL, 'vcontrol')
        struct.pack_into('<I', o, 0x28, len(o))
        for at in t.bones:
            self.put(at, BONE, 'bone')
        struct.pack_into('<I', o, 0x2C, self.group(t.unnamed))
        struct.pack_into('<I', o, 0x20, len(o))
        grps = [self.put(at, GRP, 'anmgroup') for at, _m in t.anmgroups]
        for gnew, (_at, mcs) in sorted(zip(grps, t.anmgroups), key=lambda r: self.name(r[1][0])):   # by name
            self.rel(gnew + 8, len(o), gnew)
            news = [self.put(m.at, MC, 'mcanm') for m in mcs]
            for new, m in zip(news, mcs):
                first = self.anmdata(m.first)
                self.rel(new + 4, first, new)
                self.anmdata_group(m.first, first)
                self.rel(new + 0xC, len(o), new)
                more = [self.anmdata(a) for a in m.more]
                for a, at in zip(m.more, more):
                    self.anmdata_group(a, at)
                for i, g in enumerate(m.groups):
                    if g is not None:
                        self.rel(new + 0x10 + 4 * i, self.group(g), new)
        o += bytes(-len(o) % 16)
        struct.pack_into('<I', o, 8, len(o))
        return o


def _fix_cas_structs(area: bytearray, kinds: dict[int, str], ops: dict[int, int]) -> None:
    """EDF6's values in the emitted structs: renumbered data commands, and zero in the slots EDF6 leaves unused
    (where EDF4.1 / EDF5 kept stale bytes)."""
    for at, kind in kinds.items():
        if kind == 'data':
            op = struct.unpack_from('<I', area, at)[0]
            if op not in ops:
                raise ValueError(f'CAS data command {op} at {at:#x}: no known EDF6 id')
            struct.pack_into('<I', area, at, ops[op])
            if op in (0, 1) and struct.unpack_from('<I', area, at + 0x14)[0] == 2:
                struct.pack_into('<I', area, at + 0x18, 0)        # value slot unused by type 2
            if ops[op] == 2:
                struct.pack_into('<I', area, at + 0x28, 0)        # EDF5 repeated +0x20 here; EDF6 always 0
        elif kind == 'anmdata':
            if struct.unpack_from('<I', area, at + 0x14)[0] != 1:
                struct.pack_into('<I', area, at + 0x1C, 0)        # only type 1 uses +0x1C


def cas_from_legacy(b: bytes) -> bytes:
    """A CAS 0x203 (EDF5) or 0x200 (EDF4.1) as EDF6's 0x204; ValueError on anything not laid out as documented."""
    lay = cas_layout(b)
    if lay.version == CAS_6:
        raise ValueError('already a CAS 0x204')
    emit = _Emit(b, lay, COMMON_SIZE[CAS_6])
    area = emit.run()
    struct.pack_into('<I', area, 4, CAS_6)
    _fix_cas_structs(area, emit.kinds, _OPS[lay.version])
    canm_at = len(area)
    canm, rel_strings = _canm_300(b, lay.canm, lay.strings_at)
    shift = canm_at + rel_strings - lay.strings_at
    for field_at, old in emit.strings:
        struct.pack_into('<i', area, field_at, old + shift - field_at)
    return bytes(area + canm + b[lay.strings_at:])
