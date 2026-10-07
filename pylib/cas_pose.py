"""Retarget EDF6 CAS absolute animation translations after changing an MDB bind pose.

The embedded CANM 0x300 stores local transforms, not deltas from the MDB. Moving only
MDB bones leaves the stock animation moving geometry back to the old skeleton.
Only explicitly named absolute clips are rebased; additive clips keep their bytes.
Channel layout checked against Smileynator/blender-mdb-addon import_canm.py
(parse_anm_point6); all edits preserve file size, names, key data and CAS state graph.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from mdb import Mdb


@dataclass(frozen=True)
class Track:
    name: str
    at: int
    translation: int
    rotation: int
    scale: int


@dataclass(frozen=True)
class Clip:
    name: str
    at: int
    tracks_at: int
    tracks: tuple[Track, ...]


class CasPose:
    """Read the known EDF6 CAS/CANM layout with every followed offset bounded."""
    def __init__(self, data: bytes):
        self.data = data
        if data[:8] != b'CAS\0\x04\x02\0\0':
            raise ValueError('not an EDF6 CAS 0x204')
        self.canm = self.unpack('<I', 8)[0]
        if data[self.canm:self.canm + 8] != b'CANM\0\x03\0\0':
            raise ValueError('not an embedded CANM 0x300')
        nc, co, self.channel_count, po, nn, no = self.unpack('<6I', self.canm + 8)
        self.points = self.canm + po
        self.unpack(f'<{self.channel_count * 12}I', self.points)
        names = []
        for i in range(nn):
            at = self.canm + no + i * 4
            names.append(self.text(at + self.unpack('<i', at)[0]))
        self.names = tuple(names)
        clips = []
        for i in range(nc):
            at = self.canm + co + i * 28
            _loop, name, _duration, _step, _frames, count, offset = self.unpack('<IiffIII', at)
            tracks = []
            for j in range(count):
                row = at + offset + j * 8
                bone, t, r, s = self.unpack('<Hhhh', row)
                if bone >= len(names) or any(k < -1 or k >= self.channel_count for k in (t, r, s)):
                    raise ValueError('CANM track index outside its table')
                tracks.append(Track(names[bone], row, t, r, s))
            clips.append(Clip(self.text(at + name), at, at + offset, tuple(tracks)))
        self.clips = tuple(clips)

    def unpack(self, fmt: str, at: int) -> tuple:
        if at < 0 or at + struct.calcsize(fmt) > len(self.data):
            raise ValueError('CAS offset outside the file')
        return struct.unpack_from(fmt, self.data, at)

    def text(self, at: int) -> str:
        end = at
        while self.unpack('<H', end)[0]:
            end += 2
        return self.data[at:end].decode('utf-16le')

    def translation(self, channel: int, frame: int = 0) -> tuple[float, float, float]:
        """Decode an actual stored translation key (clamped at its last key)."""
        if not 0 <= channel < self.channel_count:
            raise ValueError('CANM translation index outside its table')
        at = self.points + channel * 48
        *values, offset, kind, count, _reserved = self.unpack('<8f4i', at)
        if count <= 0 or kind not in (0, 1):
            raise ValueError('not a supported CANM translation channel')
        if count == 1:
            return tuple(values[:3])
        if kind != 1:
            raise ValueError('animated translation is not quantized xyz')
        key = self.unpack('<3H', at + offset + min(max(frame, 0), count - 1) * 6)
        return tuple(values[c] + key[c] * values[4 + c] for c in range(3))


def retarget(data: bytes, source: Mdb, target: Mdb, absolute: set[str], free: set[str] | None = None) -> bytes:
    """Rebase absolute local translations; omit tracks owned by the runtime plugin.

    A channel shared by consumers requiring different offsets is refused, rather than
    silently moving an unrelated bone. Delta addition leaves animated recoil intact.
    Free bones remain in CANM's name table, so the renderer still knows their names.
    """
    pose = CasPose(data)
    if not absolute <= {c.name for c in pose.clips}:
        raise ValueError('CAS lacks an expected absolute clip')
    free = free or set()
    old = {source.name_of(b.name): b for b in source.bones}
    new = {target.name_of(b.name): b for b in target.bones}
    delta = {n: tuple(new[n].local[12 + c] - b.local[12 + c] for c in range(3))
             for n, b in old.items() if n in new}
    shifts: dict[int, tuple[float, ...]] = {}
    out = bytearray(data)
    freed = set()
    for clip in pose.clips:
        kept = []
        for track in clip.tracks:
            if track.name in free:
                freed.add(track.name)
                continue
            kept.append(data[track.at:track.at + 8])
            for channel in (track.translation, track.rotation, track.scale):
                if channel < 0:
                    continue
                shift = delta.get(track.name, (0.0, 0.0, 0.0)) if (
                    channel == track.translation and clip.name in absolute) else (0.0, 0.0, 0.0)
                prev = shifts.setdefault(channel, shift)
                if max(abs(a - b) for a, b in zip(prev, shift)) > 1e-5:
                    raise ValueError(f'CANM channel {channel} is shared by incompatible retargets')
        if len(kept) != len(clip.tracks):
            struct.pack_into('<I', out, clip.at + 20, len(kept))
            rows = b''.join(kept)
            out[clip.tracks_at:clip.tracks_at + len(rows)] = rows
    if free != freed:
        raise ValueError(f'CAS lacks runtime-owned bone tracks: {free - freed}')
    for channel, shift in shifts.items():
        if max(abs(x) for x in shift) < 1e-5:
            continue
        pose.translation(channel)   # validate before changing only the base, never keys / speeds
        at = pose.points + channel * 48
        base = pose.unpack('<3f', at)
        struct.pack_into('<3f', out, at, *(base[c] + shift[c] for c in range(3)))
    return bytes(out)
