"""CAS animation sets (OBJECT/*.CAS): read the embedded CANM clips and add bones to them, pure Python.

Formats (EDF6: CAS version 0x00020004, CANM version 0x300), after KCreator's Earth-Defence-Force-Documentation wiki
(CAS-Format, CANM-Format) and checked against V506_HELI.CAS / V505_TANK.CAS:

  CAS header      'CAS\\0', version, CANM offset (+0x08), then the controller / group tables (left as they are here)
  CANM header     'CANM', version, clip count / table offset, channel count / table offset, bone-name count /
                  table offset (offsets from the CANM header)
  clip            28 bytes: loop, name (self-relative UTF-16 offset), life, sample interval, sample count,
                  bone-data count, bone-data table offset (from the clip entry)
  bone data       8 bytes: bone-name index, position / rotation / scale channel (0xFFFF none)
  channel         48 bytes: base xyzw, quantisation multiplier xyzw, key data offset (from the channel entry),
                  type (0 static vector, 1 quantised vector, 2 static quaternion, 3 float quaternion), sample count, 0
  bone name       4 bytes: self-relative offset of its UTF-16 name

What the engine does with them (docs/drill-re.md §5.4, docs/sazabi-re.md): a model bone is bound to a CANM bone by
name (CASController::Initialize 0x1167520); every clip carries a channel pair for every named bone, the `default`
clip the bind pose (V506: `body` at (0, 1.637, 0)), the `*_add` clips additive (zero position, identity rotation).
The drawn pose covers the bones the CAS names; a model bone it does not name is drawn at its bind pose whatever
its record says. So a model with bones of its own needs them added (add_bones).

add_bones appends only: the old bytes stay where they are and keep their meaning (every offset in them is
relative to its own table), the new bone-name table, the clips' new bone-data tables, the new channels and the
new strings go at the end and the CANM header / clip entries point at them.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

CAS_MAGIC, CANM_MAGIC = b'CAS\0', b'CANM'
CHANNEL_SIZE, CLIP_SIZE, BONE_DATA_SIZE = 48, 28, 8
NONE = 0xFFFF
STATIC_VECTOR, QUANT_VECTOR, STATIC_QUAT, FLOAT_QUAT = 0, 1, 2, 3
ADDITIVE_SUFFIX = '_add'


class CasError(Exception):
    pass


def _u32(b: bytes, o: int) -> int:
    return struct.unpack_from('<I', b, o)[0]


def _i32(b: bytes, o: int) -> int:
    return struct.unpack_from('<i', b, o)[0]


def _wstr(b: bytes, o: int) -> str:
    e = o
    while b[e:e + 2] != b'\0\0':
        e += 2
        if e >= len(b):
            raise CasError(f'unterminated string at {o:#x}')
    return b[o:e].decode('utf-16le')


@dataclass
class Channel:
    at: int                 # file offset of the entry
    base: tuple[float, float, float, float]
    mul: tuple[float, float, float, float]
    kind: int
    samples: int
    keys_at: int            # file offset of its key data


@dataclass
class Clip:
    at: int
    name: str
    loop: int
    life: float
    interval: float
    samples: int
    bones: list[tuple[int, int, int, int]]   # (name index, position, rotation, scale channel)


@dataclass
class Canm:
    at: int                 # file offset of the CANM header
    version: int
    clips: list[Clip]
    channels: list[Channel]
    bone_names: list[str]


def read(data: bytes) -> Canm:
    """The CANM inside a CAS file (decompressed)."""
    if data[:4] != CAS_MAGIC:
        raise CasError('not a CAS file')
    c = _u32(data, 0x08)
    if data[c:c + 4] != CANM_MAGIC:
        raise CasError(f'no CANM at {c:#x}')
    version = _u32(data, c + 4)
    nclip, oclip, nchan, ochan, nbone, obone = struct.unpack_from('<6I', data, c + 8)
    names = []
    for i in range(nbone):
        e = c + obone + 4 * i
        names.append(_wstr(data, e + _i32(data, e)))
    channels = []
    for i in range(nchan):
        e = c + ochan + CHANNEL_SIZE * i
        base = struct.unpack_from('<4f', data, e)
        mul = struct.unpack_from('<4f', data, e + 16)
        ko, kind, n, _ = struct.unpack_from('<4i', data, e + 32)
        channels.append(Channel(e, base, mul, kind, n, e + ko))
    clips = []
    for i in range(nclip):
        e = c + oclip + CLIP_SIZE * i
        loop, so, life, interval, samples, nbd, obd = struct.unpack_from('<Iiffiii', data, e)
        bones = [struct.unpack_from('<4H', data, e + obd + BONE_DATA_SIZE * k) for k in range(nbd)]
        clips.append(Clip(e, _wstr(data, e + so), loop, life, interval, samples, [tuple(b) for b in bones]))
    return Canm(c, version, clips, channels, names)


def _align(n: int, a: int = 16) -> int:
    return (n + a - 1) // a * a


def add_bones(data: bytes, bones: list[tuple[str, tuple[float, float, float]]]) -> bytes:
    """`data` (a CAS file) with `bones` (name, bind-local position) added to its CANM's bone names and to every
    clip: in the `default`-like (non-additive) clips at that position with the identity rotation, in the additive
    (`*_add`) clips at zero / identity, like the stock bones that clip does not move. Names already there are an
    error (a model bone bound twice)."""
    canm = read(data)
    clash = sorted({n for n, _ in bones} & set(canm.bone_names))
    if clash:
        raise CasError(f'bones already in the CAS: {clash}')
    if len({n for n, _ in bones}) != len(bones):
        raise CasError('duplicate bone names')
    c = canm.at
    out = bytearray(data)

    def tail(n: int) -> int:
        """Pad to 16 and reserve n bytes at the end; returns their offset."""
        out.extend(b'\0' * (_align(len(out)) - len(out)))
        at = len(out)
        out.extend(b'\0' * n)
        return at

    # channels: the old table copied to the end (each entry's key offset re-based), then ours
    old_n = len(canm.channels)
    zero_pos = identity = None
    for i, ch in enumerate(canm.channels):
        if zero_pos is None and ch.kind == STATIC_VECTOR and ch.base[:3] == (0.0, 0.0, 0.0):
            zero_pos = i
        if identity is None and ch.kind == STATIC_QUAT and ch.base == (0.0, 0.0, 0.0, 1.0):
            identity = i
    new_channels: list[tuple[tuple[float, float, float, float], int]] = []
    if zero_pos is None:
        zero_pos = old_n + len(new_channels)
        new_channels.append(((0.0, 0.0, 0.0, 1.0), STATIC_VECTOR))
    if identity is None:
        identity = old_n + len(new_channels)
        new_channels.append(((0.0, 0.0, 0.0, 1.0), STATIC_QUAT))
    pos_of: dict[str, int] = {}
    for name, p in bones:
        pos_of[name] = old_n + len(new_channels)
        new_channels.append(((float(p[0]), float(p[1]), float(p[2]), 1.0), STATIC_VECTOR))
    total = old_n + len(new_channels)
    if total > NONE:
        raise CasError('too many channels')
    ctab = tail(CHANNEL_SIZE * total)
    for i, ch in enumerate(canm.channels):
        e = ctab + CHANNEL_SIZE * i
        out[e:e + CHANNEL_SIZE] = data[ch.at:ch.at + CHANNEL_SIZE]
        struct.pack_into('<i', out, e + 32, ch.keys_at - e)
    for k, (base, kind) in enumerate(new_channels):
        e = ctab + CHANNEL_SIZE * (old_n + k)
        struct.pack_into('<4f4f4i', out, e, *base, 0.0, 0.0, 0.0, 0.0, 0, kind, 1, 0)
    # bone names: the old table copied (offsets re-based), then ours; strings at the very end
    first = len(canm.bone_names)
    ntab = tail(4 * (first + len(bones)))
    old_tab = c + _u32(data, c + 0x1C)
    for i in range(first):
        target = old_tab + 4 * i + _i32(data, old_tab + 4 * i)
        struct.pack_into('<i', out, ntab + 4 * i, target - (ntab + 4 * i))
    # clips: each one's bone data copied with ours appended
    for clip in canm.clips:
        add = clip.name.endswith(ADDITIVE_SUFFIX)
        rows = list(clip.bones) + [(first + k, zero_pos if add else pos_of[n], identity, NONE)
                                   for k, (n, _) in enumerate(bones)]
        btab = tail(BONE_DATA_SIZE * len(rows))
        for k, r in enumerate(rows):
            struct.pack_into('<4H', out, btab + BONE_DATA_SIZE * k, *r)
        struct.pack_into('<ii', out, clip.at + 0x14, len(rows), btab - clip.at)
    for k, (name, _) in enumerate(bones):
        s = tail(0)
        out.extend(name.encode('utf-16le') + b'\0\0')
        e = ntab + 4 * (first + k)
        struct.pack_into('<i', out, e, s - e)
    out.extend(b'\0' * (_align(len(out)) - len(out)))
    struct.pack_into('<II', out, c + 0x10, total, ctab - c)
    struct.pack_into('<II', out, c + 0x18, first + len(bones), ntab - c)
    return bytes(out)


def check_added(old: bytes, new: bytes, bones: list[tuple[str, tuple[float, float, float]]]) -> None:
    """`new` (add_bones(old, bones)) reads back as `old` plus `bones`: every old clip row and channel unchanged
    (including its key data), ours bound as asked; the old bytes untouched."""
    a, b = read(old), read(new)
    if len(new) < len(old):
        raise CasError('add_bones shortened the file')
    # The only old bytes it may rewrite: the CANM header's channel and bone-name table pointers, and each clip's
    # bone-data pointer.
    repointed = [(a.at + 0x10, 16)] + [(cl.at + 0x14, 8) for cl in a.clips]
    masked_old, masked_new = bytearray(old), bytearray(new[:len(old)])
    for at, n in repointed:
        masked_old[at:at + n] = masked_new[at:at + n] = b'\0' * n
    if masked_old != masked_new:
        raise CasError('add_bones changed old bytes it should only append to')
    if b.bone_names != a.bone_names + [n for n, _ in bones]:
        raise CasError('bone names did not come out as asked')
    for ca, cb in zip(a.clips, b.clips):
        if (ca.name, ca.loop, ca.life, ca.interval, ca.samples) != (cb.name, cb.loop, cb.life, cb.interval, cb.samples):
            raise CasError(f'clip {ca.name} changed')
        if cb.bones[:len(ca.bones)] != ca.bones:
            raise CasError(f'clip {ca.name}: old rows changed')
        for k, (name, p) in enumerate(bones):
            ni, pi, ri, si = cb.bones[len(ca.bones) + k]
            want = (0.0, 0.0, 0.0) if ca.name.endswith(ADDITIVE_SUFFIX) else tuple(float(x) for x in p)
            pos = b.channels[pi]
            rot = b.channels[ri]
            if (ni != len(a.bone_names) + k or si != NONE or pos.kind != STATIC_VECTOR
                    or any(abs(x - y) > 1e-5 for x, y in zip(pos.base[:3], want))
                    or rot.kind != STATIC_QUAT or rot.base != (0.0, 0.0, 0.0, 1.0)):
                raise CasError(f'clip {ca.name}: bone {name} not bound as asked')
    for i, (x, y) in enumerate(zip(a.channels, b.channels)):
        if (x.base, x.mul, x.kind, x.samples) != (y.base, y.mul, y.kind, y.samples):
            raise CasError(f'channel {i} changed')
        n = {QUANT_VECTOR: 6, FLOAT_QUAT: 16}.get(x.kind, 0) * x.samples
        if old[x.keys_at:x.keys_at + n] != new[y.keys_at:y.keys_at + n]:
            raise CasError(f'channel {i}: key data moved or changed')


if __name__ == '__main__':
    import sys
    raw = open(sys.argv[1], 'rb').read()
    cn = read(raw)
    print(f'CANM {cn.version:#x} at {cn.at:#x}: {len(cn.clips)} clips, {len(cn.channels)} channels, '
          f'{len(cn.bone_names)} bones')
    print('bones:', ', '.join(cn.bone_names))
    for cl in cn.clips:
        print(f'  {cl.name}: loop {cl.loop}, life {cl.life:g}, {cl.samples} samples, {len(cl.bones)} bone rows')
