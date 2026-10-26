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


def rotation(pose: CasPose, channel: int, frame: int = 0) -> tuple[float, float, float, float]:
    """A stored rotation key (xyzw) of a CANM 0x300 channel: kind 2 static (the base), kind 3 one f32 xyzw per key
    (clamped at its last key); cas_legacy's module docstring has the layout."""
    if not 0 <= channel < pose.channel_count:
        raise ValueError('CANM rotation index outside its table')
    at = pose.points + channel * 48
    *values, offset, kind, count, _reserved = pose.unpack('<8f4i', at)
    if kind == 2:
        return tuple(values[:4])
    if kind != 3 or count <= 0:
        raise ValueError('not a quaternion CANM channel')
    return pose.unpack('<4f', at + offset + min(max(frame, 0), count - 1) * 16)


def clip_info(pose: CasPose, clip: Clip) -> tuple[float, float, int]:
    """(duration, step, frames) of a clip (CANM clip header +8 / +0xC / +0x10)."""
    _loop, _name, duration, step, frames = pose.unpack('<IiffI', clip.at)
    return duration, step, frames


def quat_rows(q: tuple[float, float, float, float]) -> list[list[float]]:
    """The 3x3 rotation of a CANM quaternion (xyzw) in EDF's row-vector convention (v' = v R): the transpose of the
    usual column-vector matrix. With it the Ranger's te_l in vehicle503_bike_driver is V503's own te_ik_l bind to 1e-4
    (tests/test_edf41bike.py test_v503_grip_bone_is_its_riders_hand)."""
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]


def pose_world(pose: CasPose, clip: Clip, md: Mdb, frame: int = 0) -> list[list[float]]:
    """Model-space matrix (mdb's 16-float row-vector layout, row 3 the translation) of every bone of `md` at key `frame`
    of `clip`: a tracked bone's local rotation / translation is its key, every other local part the bind's, chained
    parent first as mdb.bind_world does. Scale tracks are refused rather than ignored."""
    tracks = {t.name: t for t in clip.tracks}
    world: list[list[float]] = []
    for b in md.bones:
        local = list(b.local)
        track = tracks.get(md.name_of(b.name))
        if track is not None:
            if track.scale >= 0:
                raise ValueError(f'{clip.name}: {track.name} has a scale track')
            if track.rotation >= 0:
                rows = quat_rows(rotation(pose, track.rotation, frame))
                for r in range(3):
                    local[r * 4:r * 4 + 3] = rows[r]
            if track.translation >= 0:
                local[12:15] = pose.translation(track.translation, frame)
        if b.parent < 0:
            world.append(local)
        else:
            parent = world[b.parent]
            world.append([sum(local[r * 4 + k] * parent[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)])
    return world


def root_motion(pose: CasPose, clip: Clip, bone: str) -> list[tuple[float, float, float]] | None:
    """The bone's ground motion over the clip, one (x, z, yaw) per key, relative to its first key: x / z the
    translation in the clip's start frame, yaw = atan2(forward x, forward z) of the bone's +Z turned by its rotation
    (radians, the angle from the start). None when the clip has no track for the bone or the bone does not move."""
    import math
    track = next((t for t in clip.tracks if t.name == bone), None)
    if track is None:
        return None
    _duration, _step, frames = clip_info(pose, clip)
    out = []
    for k in range(max(frames, 1)):
        x, _y, z = pose.translation(track.translation, k) if track.translation >= 0 else (0.0, 0.0, 0.0)
        if track.rotation >= 0:
            qx, qy, qz, qw = rotation(pose, track.rotation, k)
            fx, fz = 2.0 * (qx * qz + qw * qy), 1.0 - 2.0 * (qx * qx + qy * qy)   # +Z turned by q
            yaw = math.atan2(fx, fz)
        else:
            yaw = 0.0
        out.append((x, z, yaw))
    x0, z0, y0 = out[0]
    out = [(x - x0, z - z0, y - y0) for x, z, y in out]
    # yaw unwrapped (a 180 degree turn passes +-pi)
    for i in range(1, len(out)):
        x, z, y = out[i]
        while y - out[i - 1][2] > math.pi:
            y -= 2.0 * math.pi
        while y - out[i - 1][2] < -math.pi:
            y += 2.0 * math.pi
        out[i] = (x, z, y)
    if max(max(abs(x), abs(z), abs(y)) for x, z, y in out) < 1e-4:
        return None
    return out


def strip_bone(data: bytes, clips: set[str], bone: str) -> bytes:
    """`data` with the bone's translation and rotation tracks of the named clips dropped (their channel indices -1: the
    bone keeps its bind pose there); every other byte as it was. Why: the clips' root motion is played by the plugin
    moving the object (edf41robo.cpp), so the model must not move it a second time."""
    pose = CasPose(data)
    out = bytearray(data)
    done = set()
    for clip in pose.clips:
        if clip.name not in clips:
            continue
        for t in clip.tracks:
            if t.name == bone:
                struct.pack_into('<hh', out, t.at + 2, -1, -1)
                done.add(clip.name)
    if done != clips:
        raise ValueError(f'CAS clips without a {bone} track: {sorted(clips - done)}')
    return bytes(out)
