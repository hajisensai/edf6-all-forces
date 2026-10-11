"""EDF5 / EDF4.1 RMPA -> EDF6 RMPA (MISSION.RMPA, and a MAC's map.rmpa).

rmpa_from_legacy(old) converts; decode5() / decode6() read both formats completely; encode6() writes EDF6's
layout byte for byte. The format tables are in docs/edf41-map-rmpa-format.md.
"""
from __future__ import annotations

import struct
from typing import Any

import sgo

MAGIC = b'\0PMR'
Node = dict[str, Any]


class Reader:
    """Big-endian reads that remember which bytes were read (so a decode can prove it saw everything)."""

    def __init__(self, data: bytes) -> None:
        self.d = data
        self.seen = bytearray(len(data))

    def mark(self, off: int, n: int) -> None:
        if off < 0 or off + n > len(self.d):
            raise ValueError(f'read past the file: {off:#x}+{n}')
        for i in range(off, off + n):
            self.seen[i] = 1

    def i(self, off: int) -> int:
        self.mark(off, 4)
        return struct.unpack_from('>i', self.d, off)[0]

    def u(self, off: int) -> int:
        self.mark(off, 4)
        return struct.unpack_from('>I', self.d, off)[0]

    def le(self, off: int) -> int:
        self.mark(off, 4)
        return struct.unpack_from('<i', self.d, off)[0]

    def f(self, off: int) -> bytes:
        """A float, kept as its 4 big-endian bytes (exact round trip)."""
        self.mark(off, 4)
        return self.d[off:off + 4]

    def v(self, off: int) -> tuple[bytes, ...]:
        return tuple(self.f(off + 4 * k) for k in range(4))

    def s(self, off: int) -> str:
        end = off
        while True:
            if end + 2 > len(self.d):
                raise ValueError(f'unterminated string at {off:#x}')
            if self.d[end:end + 2] == b'\0\0':
                break
            end += 2
        self.mark(off, end + 2 - off)
        return self.d[off:end].decode('utf-16-be', 'surrogatepass')

    def ints(self, n: int, off: int) -> list[int]:
        return [self.i(off + 4 * k) for k in _cnt(self, n)]

    def unread_nonzero(self) -> list[tuple[int, int]]:
        """Ranges of bytes no decode step read that are not zero padding."""
        out: list[list[int]] = []
        for k, (b, s) in enumerate(zip(self.d, self.seen)):
            if s or not b:
                continue
            if out and out[-1][1] == k:
                out[-1][1] = k + 1
            else:
                out.append([k, k + 1])
        return [(a, b) for a, b in out]


def _cnt(r: Reader, n: int) -> range:
    """range(n) for a count read from the file; a count no file this size can hold means a misparse."""
    if not 0 <= n <= len(r.d) // 4:
        raise ValueError(f'implausible count {n}')
    return range(n)


# ---------------------------------------------------------------- EDF5 / EDF4.1 ----------------------------------

def _sgo5(r: Reader, size: int, off: int) -> Node | None:
    """The little-endian SGO an EDF5 record carries (size, offset from the record), or None."""
    if not size:
        return None
    r.mark(off, size)
    blob = r.d[off:off + size]
    ver, members = sgo.read(blob)
    return {'size': size, 'version': ver, 'members': members, 'raw': blob}


def _wp5(r: Reader, at: int) -> Node:
    return {
        'index': r.i(at), 'links': r.ints(r.i(at + 4), at + r.i(at + 8)), 'links_off': r.i(at + 8),
        'refs': r.ints(r.i(at + 0xC), at + r.i(at + 0x10)), 'refs_off': r.i(at + 0x10),
        'id': r.i(at + 0x14),
        'sgo': _sgo5(r, r.i(at + 0x18), at + r.i(at + 0x1C)), 'sgo_off': r.i(at + 0x1C),
        'namelen': r.i(at + 0x20), 'name': r.s(at + r.i(at + 0x24)),
        'pos': r.v(at + 0x28), 'raw38': r.u(at + 0x38),
    }


def _set5(r: Reader, at: int, child: Any, stride: int) -> Node:
    """A route / shape set / point set entry (0x20)."""
    n, off = r.i(at + 0x18), r.i(at + 0x1C)
    return {
        'raw0': r.u(at), 'refs': r.ints(r.i(at + 4), at + r.i(at + 8)), 'refs_off': r.i(at + 8),
        'id': r.i(at + 0xC), 'namelen': r.i(at + 0x10), 'name': r.s(at + r.i(at + 0x14)),
        'items': [child(r, at + off + k * stride) for k in _cnt(r, n)], 'items_off': off,
    }


def _section5(r: Reader, at: int, child: Any, stride: int) -> Node:
    """A route / shape / point section header (0x20)."""
    n, off = r.i(at), r.i(at + 4)
    return {
        'sets': [child(r, at + off + k * stride) for k in _cnt(r, n)], 'sets_off': off,
        'refs': r.ints(r.i(at + 8), at + r.i(at + 0xC)), 'refs_off': r.i(at + 0xC),
        'id': r.i(at + 0x10), 'namelen': r.i(at + 0x14), 'name': r.s(at + r.i(at + 0x18)), 'raw1c': r.u(at + 0x1C),
    }


def _shapedata5(r: Reader, at: int) -> Node:
    return {'v0': r.v(at), 'v1': r.v(at + 0x10), 'v2': r.v(at + 0x20), 'f30': r.f(at + 0x30), 'f34': r.f(at + 0x34),
            'raw38': r.u(at + 0x38)}


def _shape5(r: Reader, at: int) -> Node:
    return {
        'raw0': r.u(at), 'typelen': r.i(at + 4), 'type': r.s(at + r.i(at + 8)),
        'namelen': r.i(at + 0xC), 'name': r.s(at + r.i(at + 0x10)),
        'refs': r.ints(r.i(at + 0x14), at + r.i(at + 0x18)), 'refs_off': r.i(at + 0x18), 'id': r.i(at + 0x1C),
        'data': [_shapedata5(r, at + r.i(at + 0x24) + k * 0x3C) for k in _cnt(r, r.i(at + 0x20))],
        'data_off': r.i(at + 0x24),
        'sgo': _sgo5(r, r.le(at + 0x28), at + r.le(at + 0x2C)), 'sgo_off': r.le(at + 0x2C),
    }


def _point5(r: Reader, at: int) -> Node:
    return {
        'refs': r.ints(r.i(at), at + r.i(at + 4)), 'refs_off': r.i(at + 4), 'id': r.i(at + 8),
        'pos': r.v(at + 0xC), 'face': r.v(at + 0x1C), 'raw2c': r.u(at + 0x2C),
        'namelen': r.i(at + 0x30), 'name': r.s(at + r.i(at + 0x34)),
        'sgo': _sgo5(r, r.le(at + 0x38), at + r.le(at + 0x3C)), 'sgo_off': r.le(at + 0x3C),
    }


def _timeline5(r: Reader, at: int) -> Node:
    n, off = r.i(at + 4), r.i(at + 8)
    keys = [tuple(r.f(at + off + k * 0x1C + 4 * j) if j != 2 else r.i(at + off + k * 0x1C + 8) for j in range(7))
            for k in _cnt(r, n)]
    return {'f0': r.f(at), 'keys': keys, 'keys_off': off}


def _camera5(r: Reader, at: int) -> Node:
    return {
        'i0': r.i(at), 'i4': r.i(at + 4), 'refs': r.ints(r.i(at + 8), at + r.i(at + 0xC)), 'refs_off': r.i(at + 0xC),
        'i10': r.i(at + 0x10), 'f14': r.f(at + 0x14), 'f18': r.f(at + 0x18),
        'v1c': r.v(at + 0x1C), 'v2c': r.v(at + 0x2C), 'v3c': r.v(at + 0x3C), 'v4c': r.v(at + 0x4C),
        'f5c': r.f(at + 0x5C), 'raw60': r.u(at + 0x60), 'namelen': r.i(at + 0x64), 'name': r.s(at + r.i(at + 0x68)),
        'sgo': _sgo5(r, r.le(at + 0x6C), at + r.le(at + 0x70)), 'sgo_off': r.le(at + 0x70),
    }


def _camset5(r: Reader, at: int) -> Node:
    return {
        'refs': r.ints(r.i(at), at + r.i(at + 4)), 'refs_off': r.i(at + 4), 'i8': r.i(at + 8), 'rawc': r.u(at + 0xC),
        'namelen': r.i(at + 0x10), 'name': r.s(at + r.i(at + 0x14)),
        'cams': [_camera5(r, at + r.i(at + 0x1C) + k * 0x74) for k in _cnt(r, r.i(at + 0x18))], 'cams_off': r.i(at + 0x1C),
        'i20': r.i(at + 0x20), 'tl1': _timeline5(r, at + r.i(at + 0x24)), 'tl1_off': r.i(at + 0x24),
        'i28': r.i(at + 0x28), 'tl2': _timeline5(r, at + r.i(at + 0x2C)), 'tl2_off': r.i(at + 0x2C),
    }


def _camsection5(r: Reader, at: int) -> Node:
    return {
        'refs': r.ints(r.i(at), at + r.i(at + 4)), 'refs_off': r.i(at + 4), 'id': r.i(at + 8), 'rawc': r.u(at + 0xC),
        'namelen': r.i(at + 0x10), 'name': r.s(at + r.i(at + 0x14)),
        'sets': [_camset5(r, at + r.i(at + 0x1C) + k * 0x30) for k in _cnt(r, r.i(at + 0x18))], 'sets_off': r.i(at + 0x1C),
    }


def decode5(data: bytes) -> tuple[Node, Reader]:
    """An EDF5 / EDF4.1 RMPA as a tree, and the reader (for coverage)."""
    r = Reader(data)
    if data[:4] != MAGIC:
        raise ValueError('not an RMPA')
    r.mark(0, 4)
    h = [r.i(4 + 4 * k) for k in range(11)]
    secs = []
    kinds = [
        lambda a: _section5(r, a, lambda rr, b: _set5(rr, b, _wp5, 0x3C), 0x20),
        lambda a: _section5(r, a, lambda rr, b: _set5(rr, b, _shape5, 0x30), 0x20),
        lambda a: _camsection5(r, a),
        lambda a: _section5(r, a, lambda rr, b: _set5(rr, b, _point5, 0x40), 0x20),
    ]
    for k in range(4):
        n, off = h[1 + 2 * k], h[2 + 2 * k]
        secs.append({'off': off, 'list': [kinds[k](off + j * 0x20) for j in _cnt(r, n)]})
    return {'version': h[0], 'routes': secs[0], 'shapes': secs[1], 'cameras': secs[2], 'points': secs[3],
            'h28': h[9], 'h2c': h[10]}, r


# ---------------------------------------------------------------- EDF6 ----------------------------------------

def _props6(r: Reader, at: int) -> Node:
    """A property list header {count, offset from the header}; entries are two LITTLE-endian offsets (from the
    entry) to a name and a value string."""
    n, off = r.i(at), r.i(at + 4)
    items = []
    for k in _cnt(r, n):
        e = at + off + 8 * k
        items.append((r.s(e + r.le(e)), r.s(e + r.le(e + 4))))
    return {'items': items, 'off': off}


def _wp6(r: Reader, at: int) -> Node:
    return {
        'index': r.i(at), 'links': r.ints(r.i(at + 4), at + r.i(at + 8)), 'links_off': r.i(at + 8),
        'id': r.i(at + 0xC), 'namelen': r.i(at + 0x10), 'name': r.s(at + r.i(at + 0x14)),
        'pos': r.v(at + 0x18), 'props': _props6(r, at + 0x28),
    }


def _route6(r: Reader, at: int) -> Node:
    return {'i0': r.i(at), 'namelen': r.i(at + 4), 'name': r.s(at + r.i(at + 8)),
            'idx': r.ints(r.i(at + 0xC), at + r.i(at + 0x10)), 'idx_off': r.i(at + 0x10), 'wp_off': r.i(at + 0x14)}


def _routesection6(r: Reader, at: int) -> Node:
    return {
        'i0': r.i(at), 'namelen': r.i(at + 4), 'name': r.s(at + r.i(at + 8)),
        'routes': [_route6(r, at + r.i(at + 0x10) + k * 0x18) for k in _cnt(r, r.i(at + 0xC))], 'routes_off': r.i(at + 0x10),
        'wps': [_wp6(r, at + r.i(at + 0x18) + k * 0x30) for k in _cnt(r, r.i(at + 0x14))], 'wps_off': r.i(at + 0x18),
    }


def _shapedata6(r: Reader, at: int) -> Node:
    return {'v0': r.v(at), 'v1': r.v(at + 0x10), 'v2': r.v(at + 0x20), 'f30': r.f(at + 0x30), 'f34': r.f(at + 0x34)}


def _shape6(r: Reader, at: int) -> Node:
    return {
        'typelen': r.i(at), 'type': r.s(at + r.i(at + 4)), 'namelen': r.i(at + 8), 'name': r.s(at + r.i(at + 0xC)),
        'id': r.i(at + 0x10),
        'data': [_shapedata6(r, at + r.i(at + 0x18) + k * 0x38) for k in _cnt(r, r.i(at + 0x14))], 'data_off': r.i(at + 0x18),
        'props': _props6(r, at + 0x1C),
    }


def _point6(r: Reader, at: int) -> Node:
    return {'id': r.i(at), 'pos': r.v(at + 4), 'face': r.v(at + 0x14), 'namelen': r.i(at + 0x24),
            'name': r.s(at + r.i(at + 0x28)), 'props': _props6(r, at + 0x2C)}


def _set6(r: Reader, at: int, child: Any, stride: int) -> Node:
    """A shape set / point set entry (0x14)."""
    return {'i0': r.i(at), 'namelen': r.i(at + 4), 'name': r.s(at + r.i(at + 8)),
            'items': [child(r, at + r.i(at + 0x10) + k * stride) for k in _cnt(r, r.i(at + 0xC))],
            'items_off': r.i(at + 0x10)}


def _section6(r: Reader, at: int, child: Any, stride: int) -> Node:
    """A shape / camera / point section header (0x14): like a set, its items are sets."""
    return _set6(r, at, child, stride)


def _timeline6(r: Reader, at: int) -> Node:
    return _timeline5(r, at)


def _camera6(r: Reader, at: int) -> Node:
    return {
        'i0': r.i(at), 'i4': r.i(at + 4), 'i8': r.i(at + 8), 'fc': r.f(at + 0xC), 'f10': r.f(at + 0x10),
        'v14': r.v(at + 0x14), 'v24': r.v(at + 0x24), 'v34': r.v(at + 0x34), 'v44': r.v(at + 0x44),
        'f54': r.f(at + 0x54), 'namelen': r.i(at + 0x58), 'name': r.s(at + r.i(at + 0x5C)), 'props': _props6(r, at + 0x60),
    }


def _camset6(r: Reader, at: int) -> Node:
    return {
        'i0': r.i(at), 'namelen': r.i(at + 4), 'name': r.s(at + r.i(at + 8)),
        'cams': [_camera6(r, at + r.i(at + 0x10) + k * 0x68) for k in _cnt(r, r.i(at + 0xC))], 'cams_off': r.i(at + 0x10),
        'i14': r.i(at + 0x14), 'tl1': _timeline6(r, at + r.i(at + 0x18)), 'tl1_off': r.i(at + 0x18),
        'i1c': r.i(at + 0x1C), 'tl2': _timeline6(r, at + r.i(at + 0x20)), 'tl2_off': r.i(at + 0x20),
    }


def decode6(data: bytes) -> tuple[Node, Reader]:
    """An EDF6 RMPA as a tree, and the reader (for coverage)."""
    r = Reader(data)
    if data[:4] != MAGIC:
        raise ValueError('not an RMPA')
    r.mark(0, 4)
    h = [r.i(4 + 4 * k) for k in range(11)]
    kinds = [
        (lambda a: _routesection6(r, a), 0x1C),
        (lambda a: _section6(r, a, lambda rr, b: _set6(rr, b, _shape6, 0x24), 0x14), 0x14),
        (lambda a: _section6(r, a, _camset6, 0x24), 0x14),
        (lambda a: _section6(r, a, lambda rr, b: _set6(rr, b, _point6, 0x34), 0x14), 0x14),
    ]
    secs = []
    for k, (fn, stride) in enumerate(kinds):
        n, off = h[1 + 2 * k], h[2 + 2 * k]
        secs.append({'off': off, 'list': [fn(off + j * stride) for j in _cnt(r, n)]})
    return {'version': h[0], 'routes': secs[0], 'shapes': secs[1], 'cameras': secs[2], 'points': secs[3],
            'h28': h[9], 'h2c': h[10]}, r


# ---------------------------------------------------------------- EDF6 writer ----------------------------------

class Writer:
    """EDF6's layout: structs in walk order, 16-aligned groups, one sorted string pool at the end."""

    def __init__(self) -> None:
        self.b = bytearray()
        self.strs: list[tuple[int, int, str, str]] = []   # (patch at, relative to, string, endianness)

    def align(self) -> None:
        self.b += bytes(-len(self.b) % 16)

    def alloc(self, n: int) -> int:
        at = len(self.b)
        self.b += bytes(n)
        return at

    def i(self, at: int, v: int) -> None:
        struct.pack_into('>i', self.b, at, v)

    def f(self, at: int, raw: bytes) -> None:
        self.b[at:at + 4] = raw

    def v(self, at: int, vec: tuple[bytes, ...]) -> None:
        for k, raw in enumerate(vec):
            self.f(at + 4 * k, raw)

    def s(self, at: int, base: int, text: str, endian: str = '>') -> None:
        self.strs.append((at, base, text, endian))

    def finish(self) -> bytes:
        pool = sorted({t for _, _, t, _ in self.strs}, key=lambda t: t.encode('utf-16-be', 'surrogatepass'))
        where: dict[str, int] = {}
        for t in pool:
            where[t] = len(self.b)
            self.b += t.encode('utf-16-be', 'surrogatepass') + b'\0\0'
        for at, base, t, e in self.strs:
            struct.pack_into(e + 'i', self.b, at, where[t] - base)
        return bytes(self.b)


def _w_props(w: Writer, hdr: int, items: list[tuple[str, str]]) -> None:
    """Fills the property list header at `hdr`; the entries go at the cursor."""
    w.i(hdr, len(items))
    w.i(hdr + 4, len(w.b) - hdr)
    if not items:
        return
    for k, v in items:
        e = w.alloc(8)
        w.s(e, e, k, '<')
        w.s(e + 4, e, v, '<')
    w.align()


def _w_ints(w: Writer, cnt: int, off: int, base: int, vals: list[int]) -> None:
    w.i(cnt, len(vals))
    w.i(off, len(w.b) - base)
    if not vals:
        return
    at = w.alloc(4 * len(vals))
    for k, x in enumerate(vals):
        w.i(at + 4 * k, x)
    w.align()


def _w_array(w: Writer, cnt: int, off: int, base: int, n: int, stride: int) -> list[int]:
    """Allocates n records of `stride` at the cursor (then aligns), fills the (count, offset) pair and returns
    the records' addresses."""
    w.i(cnt, n)
    w.i(off, len(w.b) - base)
    if not n:
        return []
    at = w.alloc(n * stride)
    w.align()
    return [at + k * stride for k in range(n)]


def _w_named(w: Writer, at: int, rec: Node, lenoff: int, stroff: int) -> None:
    w.i(at + lenoff, rec['namelen'])
    w.s(at + stroff, at, rec['name'])


def _w_routesection(w: Writer, at: int, sec: Node) -> None:
    w.i(at, sec['i0'])
    _w_named(w, at, sec, 4, 8)
    wps = _w_array(w, at + 0x14, at + 0x18, at, len(sec['wps']), 0x30)
    for a, wp in zip(wps, sec['wps']):
        w.i(a, wp['index'])
        w.i(a + 0xC, wp['id'])
        _w_named(w, a, wp, 0x10, 0x14)
        w.v(a + 0x18, wp['pos'])
        _w_ints(w, a + 4, a + 8, a, wp['links'])
        _w_props(w, a + 0x28, wp['props']['items'])
    wp_base = wps[0] if wps else len(w.b)
    routes = _w_array(w, at + 0xC, at + 0x10, at, len(sec['routes']), 0x18)
    for a, rt in zip(routes, sec['routes']):
        w.i(a, rt['i0'])
        _w_named(w, a, rt, 4, 8)
        w.i(a + 0x14, wp_base - a)
        _w_ints(w, a + 0xC, a + 0x10, a, rt['idx'])


def _w_shapeset(w: Writer, at: int, st: Node) -> None:
    w.i(at, st['i0'])
    _w_named(w, at, st, 4, 8)
    shapes = _w_array(w, at + 0xC, at + 0x10, at, len(st['items']), 0x24)
    for a, sh in zip(shapes, st['items']):
        w.i(a, sh['typelen'])
        w.s(a + 4, a, sh['type'])
        _w_named(w, a, sh, 8, 0xC)
        w.i(a + 0x10, sh['id'])
        datas = _w_array(w, a + 0x14, a + 0x18, a, len(sh['data']), 0x38)
        for d, dt in zip(datas, sh['data']):
            w.v(d, dt['v0'])
            w.v(d + 0x10, dt['v1'])
            w.v(d + 0x20, dt['v2'])
            w.f(d + 0x30, dt['f30'])
            w.f(d + 0x34, dt['f34'])
        _w_props(w, a + 0x1C, sh['props']['items'])


def _w_pointset(w: Writer, at: int, st: Node) -> None:
    w.i(at, st['i0'])
    _w_named(w, at, st, 4, 8)
    pts = _w_array(w, at + 0xC, at + 0x10, at, len(st['items']), 0x34)
    for a, pt in zip(pts, st['items']):
        w.i(a, pt['id'])
        w.v(a + 4, pt['pos'])
        w.v(a + 0x14, pt['face'])
        _w_named(w, a, pt, 0x24, 0x28)
        _w_props(w, a + 0x2C, pt['props']['items'])


def _w_timeline(w: Writer, ptr: int, base: int, tl: Node) -> None:
    a = w.alloc(0xC)
    w.align()
    w.i(ptr, a - base)
    w.f(a, tl['f0'])
    keys = _w_array(w, a + 4, a + 8, a, len(tl['keys']), 0x1C)
    for k, key in zip(keys, tl['keys']):
        for j, x in enumerate(key):
            if j == 2:
                w.i(k + 8, x)
            else:
                w.f(k + 4 * j, x)


def _w_camset(w: Writer, at: int, cs: Node) -> None:
    w.i(at, cs['i0'])
    _w_named(w, at, cs, 4, 8)
    w.i(at + 0x14, cs['i14'])
    w.i(at + 0x1C, cs['i1c'])
    cams = _w_array(w, at + 0xC, at + 0x10, at, len(cs['cams']), 0x68)
    for a, c in zip(cams, cs['cams']):
        w.i(a, c['i0'])
        w.i(a + 4, c['i4'])
        w.i(a + 8, c['i8'])
        w.f(a + 0xC, c['fc'])
        w.f(a + 0x10, c['f10'])
        for k, name in enumerate(('v14', 'v24', 'v34', 'v44')):
            w.v(a + 0x14 + 0x10 * k, c[name])
        w.f(a + 0x54, c['f54'])
        _w_named(w, a, c, 0x58, 0x5C)
        _w_props(w, a + 0x60, c['props']['items'])
    _w_timeline(w, at + 0x18, at, cs['tl1'])
    _w_timeline(w, at + 0x20, at, cs['tl2'])


def _w_section(w: Writer, at: int, sec: Node, stride: int, child: Any) -> None:
    w.i(at, sec['i0'])
    _w_named(w, at, sec, 4, 8)
    for a, st in zip(_w_array(w, at + 0xC, at + 0x10, at, len(sec['items']), stride), sec['items']):
        child(w, a, st)


def encode6(t: Node) -> bytes:
    """An EDF6 tree (decode6's shape; its offsets are ignored and recomputed) as EDF6's bytes."""
    w = Writer()
    w.alloc(0x28)
    w.b[0:4] = MAGIC
    w.i(4, t['version'])
    w.align()
    kinds = [
        ('routes', 0x1C, _w_routesection),
        ('shapes', 0x14, lambda ww, a, s: _w_section(ww, a, s, 0x14, _w_shapeset)),
        ('cameras', 0x14, lambda ww, a, s: _w_section(ww, a, s, 0x24, _w_camset)),
        ('points', 0x14, lambda ww, a, s: _w_section(ww, a, s, 0x14, _w_pointset)),
    ]
    for k, (name, stride, fn) in enumerate(kinds):
        secs = t[name]['list']
        heads = _w_array(w, 8 + 8 * k, 0xC + 8 * k, 0, len(secs), stride)
        for a, s in zip(heads, secs):
            fn(w, a, s)
    return w.finish()


# ---------------------------------------------------------------- EDF5 -> EDF6 ----------------------------------

USAGE = ('Usage', '通常')   # every EDF6 shape carries Usage = 通常 ("normal")


def _num(v: Any) -> str:
    """An SGO value as EDF6 writes a property value: integral floats without a fraction ('-1', '10')."""
    if isinstance(v, sgo.Float):
        x = v.value
    elif isinstance(v, (int, float)):
        x = float(v)
    else:
        return str(v)
    if x == int(x):
        return str(int(x))
    return repr(struct.unpack('<f', struct.pack('<f', x))[0])


def _sgo_props(s: Node | None) -> list[tuple[str, str]]:
    if s is None:
        return []
    return [(k, _num(v)) for k, v in s['members'].items()]


def _empty6(name: str = '', namelen: int = 0) -> Node:
    return {'i0': -1, 'namelen': namelen, 'name': name, 'items': []}


def _routes6(secs: list[Node], dropped: list[tuple[str, int, int]]) -> list[Node]:
    """EDF5 numbers waypoints and links per route; EDF6 numbers them across the section (route k's waypoints
    follow route k-1's). A link >= the route's own waypoint count (only map.rmpa's cable_elec_wire_* /
    grass_* routes have them) names a waypoint of another route by a numbering the file does not record, so
    it is dropped and listed in `dropped` (route name, waypoint index, link)."""
    if not secs:
        return [{'i0': -1, 'namelen': 0, 'name': '', 'routes': [], 'wps': []}]
    out = []
    for sec in secs:
        routes, wps = [], []
        for st in sec['sets']:
            base, n = len(wps), len(st['items'])
            for wp in st['items']:
                dropped += [(st['name'], wp['index'], x) for x in wp['links'] if not 0 <= x < n]
                links = [base + x for x in wp['links'] if 0 <= x < n]
                wps.append({'index': base + wp['index'], 'links': links, 'id': wp['id'],
                            'namelen': wp['namelen'], 'name': wp['name'], 'pos': wp['pos'],
                            'props': {'items': _sgo_props(wp['sgo'])}})
            routes.append({'i0': -1, 'namelen': st['namelen'], 'name': st['name'],
                           'idx': [base + wp['index'] for wp in st['items']]})
        out.append({'i0': -1, 'namelen': sec['namelen'], 'name': sec['name'], 'routes': routes, 'wps': wps})
    return out


def _shapes6(secs: list[Node]) -> list[Node]:
    if not secs:
        return [_empty6()]
    out = []
    for sec in secs:
        sets = []
        for st in sec['sets']:
            shapes = [{'typelen': sh['typelen'], 'type': sh['type'], 'namelen': sh['namelen'], 'name': sh['name'],
                       'id': sh['id'], 'data': [{k: d[k] for k in ('v0', 'v1', 'v2', 'f30', 'f34')} for d in sh['data']],
                       'props': {'items': [USAGE] + _sgo_props(sh['sgo'])}}
                      for sh in st['items']]
            sets.append({'i0': 0, 'namelen': st['namelen'], 'name': st['name'], 'items': shapes})
        out.append({'i0': -1, 'namelen': sec['namelen'], 'name': sec['name'], 'items': sets})
    return out


def _points6(secs: list[Node]) -> list[Node]:
    if not secs:
        return [_empty6()]
    out = []
    for sec in secs:
        sets = []
        for st in sec['sets']:
            pts = [{'id': p['id'], 'pos': p['pos'], 'face': p['face'], 'namelen': p['namelen'], 'name': p['name'],
                    'props': {'items': _sgo_props(p['sgo'])}} for p in st['items']]
            sets.append({'i0': -1, 'namelen': st['namelen'], 'name': st['name'], 'items': pts})
        out.append({'i0': -1, 'namelen': sec['namelen'], 'name': sec['name'], 'items': sets})
    return out


def _cameras6(secs: list[Node]) -> list[Node]:
    """No EDF6 file has a camera, so this follows EDF.dll's reader (0x6F3470 / 0x6F2600 / 0x6F2270) only."""
    if not secs:
        return [_empty6()]
    out = []
    for sec in secs:
        sets = []
        for cs in sec['sets']:
            cams = [{'i0': c['i0'], 'i4': c['i4'], 'i8': c['i10'], 'fc': c['f14'], 'f10': c['f18'],
                     'v14': c['v1c'], 'v24': c['v2c'], 'v34': c['v3c'], 'v44': c['v4c'], 'f54': c['f5c'],
                     'namelen': c['namelen'], 'name': c['name'], 'props': {'items': _sgo_props(c['sgo'])}}
                    for c in cs['cams']]
            sets.append({'i0': -1, 'namelen': cs['namelen'], 'name': cs['name'], 'cams': cams,
                         'i14': cs['i20'], 'tl1': cs['tl1'], 'i1c': cs['i28'], 'tl2': cs['tl2']})
        out.append({'i0': -1, 'namelen': sec['namelen'], 'name': sec['name'], 'items': sets})
    return out


def tree_from_legacy(t5: Node) -> Node:
    """decode5's tree as an EDF6 tree (encode6's input)."""
    dropped: list[tuple[str, int, int]] = []
    return {
        'version': t5['version'],
        'routes': {'list': _routes6(t5['routes']['list'], dropped)},
        'shapes': {'list': _shapes6(t5['shapes']['list'])},
        'cameras': {'list': _cameras6(t5['cameras']['list'])},
        'points': {'list': _points6(t5['points']['list'])},
        'dropped_links': dropped,
    }


def is_edf6(data: bytes) -> bool:
    """Whether `data` already is an EDF6 RMPA (a full decode6 that accounts for every byte)."""
    try:
        _, r = decode6(data)
    except Exception:  # noqa: BLE001 - any decode failure means "not this format"
        return False
    return not r.unread_nonzero()


def is_legacy(data: bytes) -> bool:
    """Whether `data` is an EDF5 / EDF4.1 RMPA (a full decode5 that accounts for every byte)."""
    try:
        _, r = decode5(data)
    except Exception:  # noqa: BLE001
        return False
    return not r.unread_nonzero()


def rmpa_from_legacy(old: bytes) -> bytes:
    """An EDF5 / EDF4.1 RMPA (MISSION.RMPA or a MAC's map.rmpa) in EDF6's format. An RMPA that already is
    EDF6's is returned unchanged (no file of either game decodes cleanly as both: rmpa_legacy_real)."""
    if not is_legacy(old) and is_edf6(old):
        return old
    t5, r = decode5(old)
    rest = r.unread_nonzero()
    if rest:
        raise ValueError(f'EDF5 RMPA has bytes the decoder does not know: {rest[:4]}')
    return encode6(tree_from_legacy(t5))
