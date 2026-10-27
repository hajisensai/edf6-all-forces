"""Cockpit texture acceleration with byte-identical DDS and CMPL outputs.

The pure texfile codec remains available to lightweight tools. NumPy is imported
only when a cockpit image needs encoding. Caches retain at most 64 MiB each and
use source content, never paths or timestamps, as identity.
"""
from __future__ import annotations

from collections import OrderedDict
import hashlib
from io import BytesIO
from pathlib import Path
from typing import Any

import texfile
from mdb import cmpl_compress


class _ByteCache:
    def __init__(self, limit: int) -> None:
        self.limit = limit
        self.size = 0
        self.items: OrderedDict[str, bytes] = OrderedDict()

    def get(self, key: str) -> bytes | None:
        value = self.items.get(key)
        if value is not None:
            self.items.move_to_end(key)
        return value

    def put(self, key: str, value: bytes) -> bytes:
        previous = self.items.pop(key, None)
        if previous is not None:
            self.size -= len(previous)
        if len(value) <= self.limit:
            while self.size + len(value) > self.limit:
                _, old = self.items.popitem(last=False)
                self.size -= len(old)
            self.items[key] = value
            self.size += len(value)
        return value


_dds_cache = _ByteCache(64 * 1024 * 1024)
_packed_cache = _ByteCache(64 * 1024 * 1024)


def _half(pixels: Any) -> Any:
    import numpy as np
    height, width = pixels.shape[:2]
    y = np.arange(max(1, height // 2)) * 2
    x = np.arange(max(1, width // 2)) * 2
    y1, x1 = np.minimum(y + 1, height - 1), np.minimum(x + 1, width - 1)
    total = pixels[y[:, None], x].astype(np.uint16)
    total += pixels[y1[:, None], x]
    total += pixels[y[:, None], x1]
    total += pixels[y1[:, None], x1]
    return ((total + 2) >> 2).astype(np.uint8)


def _level(pixels: Any) -> bytes:
    import numpy as np
    height, width = pixels.shape[:2]
    padded = np.pad(pixels, ((0, (-height) % 4), (0, (-width) % 4), (0, 0)), mode='edge')
    blocks = padded.reshape(padded.shape[0] // 4, 4, padded.shape[1] // 4, 4, 4)
    blocks = blocks.transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)
    output = []
    # Bound the temporary int64 arrays independently of texture resolution.
    for start in range(0, len(blocks), 4096):
        px = blocks[start:start + 4096, :, :3].astype(np.int64)
        axis = (px.max(1) - px.min(1)).argmax(1)
        rows = np.arange(len(px))
        along = px[rows[:, None], np.arange(16)[None, :], axis[:, None]]
        a, b = px[rows, along.argmax(1)], px[rows, along.argmin(1)]

        def rgb565(rgb: Any) -> Any:
            return (((rgb[:, 0] * 31 + 127) // 255) << 11 |
                    ((rgb[:, 1] * 63 + 127) // 255) << 5 |
                    (rgb[:, 2] * 31 + 127) // 255)

        c0, c1 = rgb565(a), rgb565(b)
        flat = c0 == c1
        swap = c0 < c1
        a, b = np.where(swap[:, None], b, a), np.where(swap[:, None], a, b)
        c0, c1 = np.maximum(c0, c1), np.minimum(c0, c1)
        delta = a - b
        denominator = np.maximum((delta * delta).sum(1), 1)
        t = ((px - b[:, None]) * delta[:, None]).sum(2) * 3 / denominator[:, None]
        palette = np.where(t < 0.5, 1, np.where(t < 1.5, 3, np.where(t < 2.5, 2, 0)))
        indices = np.bitwise_or.reduce(palette.astype(np.uint32) << (np.arange(16, dtype=np.uint32) * 2), axis=1)
        indices[flat] = 0
        packed = np.empty(len(px), dtype=[('c0', '<u2'), ('c1', '<u2'), ('indices', '<u4')])
        packed['c0'], packed['c1'], packed['indices'] = c0, c1, indices
        output.append(packed.tobytes())
    return b''.join(output)


def dxt1_dds(image: texfile.Image) -> bytes:
    """Exact texfile.dxt1_dds range-fit and box-filter semantics, in batches."""
    import numpy as np
    pixels = np.frombuffer(image.rgba, dtype=np.uint8).reshape(image.height, image.width, 4)
    levels = []
    while True:
        levels.append(_level(pixels))
        if pixels.shape[0] == 1 and pixels.shape[1] == 1:
            break
        pixels = _half(pixels)
    top = max(1, (image.width + 3) // 4) * max(1, (image.height + 3) // 4) * 8
    return texfile._header(image.width, image.height, len(levels), b'DXT1', 0, (0, 0, 0, 0), top) + b''.join(levels)


# ------------------------------------------------------------------------------------------ BC5 normal maps
#
# What the stock vehicles' normal maps are (V505 / V601 / V603 / V614 body, kyata, light; V505 / V506 EDF6BENEFITS):
# BC5U, the legacy 124-byte header with FourCC 'BC5U' (no DX10 header), flags 0xA1007, depth 1, caps 0x401008, a full mip chain
# to 1x1, the same HD / .lod pair as every other stock texture (texfile.texture_pair slices it). Their shader samples
# `t1.xywz` and takes X = R x A, Y = G, Z = sqrt(1 - X^2 - Y^2) (tmp/model-quality-re.md 3): BC5 gives R = X, G = Y,
# A = 1, the same channels DXT1 has, each with its own 8-bit endpoints and 3-bit indices (DXT1: X 5 bits, Y 6, both
# on one line through RGB space).

BC5_FOURCC = b'BC5U'


def _bc4_error(v: Any, e0: Any, e1: Any) -> tuple[Any, Any]:
    """(squared error per block, palette position per pixel 0..7: 0 = e0, 7 = e1) of BC4's 8-value mode, e0 >= e1.
    The 8-value palette is evenly spaced (D3D's decode: ((7 - p) e0 + p e1) / 7), so the nearest entry is a rounding,
    not a search; e0 == e1 is the 6-value mode's code 0, e0 itself."""
    import numpy as np
    span = (e0 - e1).astype(np.float32)
    safe = np.maximum(span, 1.0)
    pos = np.clip(np.rint((e0[:, None] - v) * 7.0 / safe[:, None]), 0, 7)
    pos[span == 0] = 0
    recon = e0[:, None] - pos * (span[:, None] / 7.0)
    return ((v - recon) ** 2).sum(1), pos


REFINE_ERROR = 4.0      # a block's squared error (16 pixels, 8-bit steps) under which max / min endpoints are kept


def _bc4_refine(v: Any, best0: Any, best1: Any, best: Any, pos: Any) -> None:
    """Better endpoints for the blocks `v` (in place): two least-squares refits of the endpoints to the indices, then a
    +-1 local search around the best; a candidate is taken only where its squared error is lower."""
    import numpy as np

    def take(e0: Any, e1: Any) -> None:
        a, b = np.maximum(e0, e1), np.minimum(e0, e1)
        err, p = _bc4_error(v, a, b)
        better = err < best
        best0[better], best1[better], best[better] = a[better], b[better], err[better]
        pos[better] = p[better]

    for _ in range(2):              # least squares: v ~ (1 - w) e0 + w e1, w = position / 7
        w = pos / 7.0
        u = 1.0 - w
        aa, bb, cc = (u * u).sum(1), (u * w).sum(1), (w * w).sum(1)
        d0, d1 = (u * v).sum(1), (w * v).sum(1)
        det = aa * cc - bb * bb
        ok = np.abs(det) > 1e-6
        det = np.where(ok, det, 1.0)
        e0 = np.where(ok, np.rint((cc * d0 - bb * d1) / det), best0)
        e1 = np.where(ok, np.rint((aa * d1 - bb * d0) / det), best1)
        take(np.clip(e0, 0, 255).astype(np.int32), np.clip(e1, 0, 255).astype(np.int32))
    b0, b1 = best0.copy(), best1.copy()
    for d0 in (-1, 0, 1):
        for d1 in (-1, 0, 1):
            if d0 or d1:
                take(np.clip(b0 + d0, 0, 255), np.clip(b1 + d1, 0, 255))


def _bc4_blocks(values: Any) -> bytes:
    """BC4 (unsigned) blocks of `values` (N x 16 uint8, pixel i = row i // 4, column i % 4): the block's max / min as
    endpoints, refined (_bc4_refine) where they leave a squared error over REFINE_ERROR (RMS half a step: the rest gain
    nothing visible, and most of a smooth map's blocks are there); e0 > e1 (the 8-value mode) unless the block is
    flat. Deterministic."""
    import numpy as np
    out = np.empty((len(values), 8), dtype=np.uint8)
    for start in range(0, len(values), 16384):
        v = values[start:start + 16384].astype(np.float32)
        best0, best1 = v.max(1).astype(np.int32), v.min(1).astype(np.int32)
        best, pos = _bc4_error(v, best0, best1)
        todo = np.flatnonzero(best > REFINE_ERROR)
        if len(todo):
            sub = [a[todo] for a in (best0, best1, best, pos)]
            _bc4_refine(v[todo], *sub)
            best0[todo], best1[todo], best[todo], pos[todo] = sub
        flat = best0 == best1
        code = np.where(pos == 0, 0, np.where(pos == 7, 1, pos + 1)).astype(np.uint64)
        code[flat] = 0
        bits = np.bitwise_or.reduce(code << (np.arange(16, dtype=np.uint64) * np.uint64(3)), axis=1)
        block = out[start:start + len(v)]
        block[:, 0], block[:, 1] = best0, best1
        for k in range(6):
            block[:, 2 + k] = (bits >> np.uint64(8 * k)) & np.uint64(0xFF)
    return out.tobytes()


def _blocks16(plane: Any) -> Any:
    """An H x W uint8 plane as (blocks, 16), blocks row-major, the edges repeated to whole blocks."""
    import numpy as np
    h, w = plane.shape
    padded = np.pad(plane, ((0, (-h) % 4), (0, (-w) % 4)), mode='edge')
    return padded.reshape(padded.shape[0] // 4, 4, padded.shape[1] // 4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)


def bc5_level(xy: Any) -> bytes:
    """One BC5 mip level of an H x W x 2 uint8 array (R, G): per block R's BC4 block, then G's."""
    import numpy as np
    r = np.frombuffer(_bc4_blocks(_blocks16(xy[..., 0])), np.uint8).reshape(-1, 8)
    g = np.frombuffer(_bc4_blocks(_blocks16(xy[..., 1])), np.uint8).reshape(-1, 8)
    return np.hstack((r, g)).tobytes()


def _half_float(v: Any) -> Any:
    """texfile._half's 2x2 box (odd sizes: the last row / column repeated), in float."""
    import numpy as np
    height, width = v.shape[:2]
    y = np.arange(max(1, height // 2)) * 2
    x = np.arange(max(1, width // 2)) * 2
    y1, x1 = np.minimum(y + 1, height - 1), np.minimum(x + 1, width - 1)
    return (v[y[:, None], x] + v[y1[:, None], x] + v[y[:, None], x1] + v[y1[:, None], x1]) * 0.25


def _unit(n: Any) -> Any:
    import numpy as np
    length = np.sqrt((n * n).sum(-1, keepdims=True))
    flat = np.zeros_like(n)
    flat[..., 2] = 1.0
    return np.where(length > 1e-6, n / np.maximum(length, 1e-6), flat)


def normal_vectors(image: texfile.Image) -> Any:
    """An RGB(A)8 tangent-space normal map (R = X, G = Y, B = Z, 128 = 0) as H x W x 3 unit float32 vectors (a
    zero-length texel: straight up)."""
    import numpy as np
    px = np.frombuffer(image.rgba, dtype=np.uint8).reshape(image.height, image.width, 4)
    return _unit(px[..., :3].astype(np.float32) * (2.0 / 255.0) - 1.0)


def _xy8(n: Any) -> Any:
    import numpy as np
    return np.clip(np.rint(n[..., :2] * 127.5 + 127.5), 0, 255).astype(np.uint8)


def bc5_normal_dds(image: texfile.Image) -> bytes:
    """A BC5U DDS of a tangent-space normal map (R = X, G = Y; B and alpha dropped: the shader rebuilds Z), the stock
    layout (see above), its whole mip chain: each level the 2x2 box of the level above's unit vectors (texfile._half's
    footprint), renormalised, so a mip's normals stay unit and its X / Y are not shortened towards flat."""
    n = normal_vectors(image)
    levels = []
    while True:
        levels.append(bc5_level(_xy8(n)))
        if n.shape[0] == 1 and n.shape[1] == 1:
            break
        n = _unit(_half_float(n))
    top = max(1, (image.width + 3) // 4) * max(1, (image.height + 3) // 4) * 16
    return texfile._header(image.width, image.height, len(levels), BC5_FOURCC, 0, (0, 0, 0, 0), top, 1) + b''.join(levels)


def bc5_normal_file(path: str) -> bytes:
    """A PNG / JPEG normal map file as bc5_normal_dds (Pillow decodes it)."""
    from PIL import Image
    with Image.open(path) as source:
        rgba = source.convert('RGBA')
        return bc5_normal_dds(texfile.Image(rgba.width, rgba.height, bytearray(rgba.tobytes())))


def green_down_score(rgb: Any) -> float:
    """Which way a tangent-space normal map's green points, measured: the correlation of dX/d(row) with dY/d(column)
    (R = X, G = Y). A height field's normals are its gradient, whose curl is zero: with +G the texture's down (DirectX,
    the stock vehicles' maps and the binormal our meshes carry: +v) the two are equal, with +G up (OpenGL) opposite.
    > 0: DirectX; < 0: OpenGL; near 0: no relief to tell by (stock vehicle maps +0.4 .. +0.9)."""
    import numpy as np
    x = rgb[..., 0].astype(np.float32)
    y = rgb[..., 1].astype(np.float32)
    a = (x[2:, 1:-1] - x[:-2, 1:-1]).ravel()
    b = (y[1:-1, 2:] - y[1:-1, :-2]).ravel()
    if a.std() == 0 or b.std() == 0:
        return 0.0
    return float(np.corrcoef(a, b)[0, 1])


def directx_normals(rgb: Any, green: str, label: str) -> Any:
    """An RGB normal map (H x W x 3 uint8) in DirectX green (the game's), from a source whose convention `green` the
    caller declares ('down': DirectX, as it is; 'up': OpenGL, G inverted). The declaration is checked against the
    measurement (green_down_score): a clear disagreement is an error, not a guess."""
    import numpy as np
    if green not in ('down', 'up'):
        raise texfile.TextureError(f'{label}: normal map green {green!r} is neither "down" nor "up"')
    score = green_down_score(rgb)
    if score * (1 if green == 'down' else -1) < -0.05:
        raise texfile.TextureError(f'{label}: declared green {green}, measured {score:+.3f} (the other way)')
    out = np.array(rgb[..., :3], dtype=np.uint8, copy=True)
    if green == 'up':
        out[..., 1] = 255 - out[..., 1]
    return out


def decode_bc4(data: bytes, width: int, height: int) -> Any:
    """A BC4U level (as the D3D decoder: 8-value mode when e0 > e1, else 6 values plus 0 and 255) as H x W float32
    in 0..255."""
    import numpy as np
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    raw = np.frombuffer(data, np.uint8, bw * bh * 8).reshape(-1, 8)
    e0, e1 = raw[:, 0].astype(np.float32), raw[:, 1].astype(np.float32)
    bits = np.zeros(len(raw), np.uint64)
    for k in range(6):
        bits |= raw[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    code = ((bits[:, None] >> (np.arange(16, dtype=np.uint64) * np.uint64(3))) & np.uint64(7)).astype(np.int32)
    eight = (e0 > e1)[:, None]
    i = code.astype(np.float32)
    val8 = np.where(code == 0, e0[:, None], np.where(code == 1, e1[:, None],
                                                    ((8 - i) * e0[:, None] + (i - 1) * e1[:, None]) / 7.0))
    val6 = np.where(code == 0, e0[:, None], np.where(code == 1, e1[:, None],
                    np.where(code == 6, 0.0, np.where(code == 7, 255.0,
                                                      ((6 - i) * e0[:, None] + (i - 1) * e1[:, None]) / 5.0))))
    val = np.where(eight, val8, val6).reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(bh * 4, bw * 4)
    return val[:height, :width]


def decode_bc5(data: bytes, width: int, height: int) -> Any:
    """A BC5U level as H x W x 2 float32 (R, G) in 0..255."""
    import numpy as np
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    raw = np.frombuffer(data, np.uint8, bw * bh * 16).reshape(-1, 16)
    r = decode_bc4(raw[:, :8].tobytes(), width, height)
    g = decode_bc4(raw[:, 8:].tobytes(), width, height)
    return np.stack((r, g), -1)


def decode_dxt1(data: bytes, width: int, height: int) -> Any:
    """A DXT1 level as H x W x 3 float32 in 0..255 (the 4-colour mode as D3D; 3-colour mode: index 3 black)."""
    import numpy as np
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    raw = np.frombuffer(data, np.dtype([('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')]), bw * bh)

    def rgb(c: Any) -> Any:
        c = c.astype(np.int32)
        return np.stack(((c >> 11) * 255.0 / 31, ((c >> 5) & 63) * 255.0 / 63, (c & 31) * 255.0 / 31), -1)
    a, b = rgb(raw['c0']), rgb(raw['c1'])
    four = (raw['c0'] > raw['c1'])[:, None]
    pal = np.stack((a, b, np.where(four, (2 * a + b) / 3, (a + b) / 2), np.where(four, (a + 2 * b) / 3, 0.0)), 1)
    idx = ((raw['i'][:, None].astype(np.int64) >> (np.arange(16) * 2)) & 3)
    px = pal[np.arange(len(raw))[:, None], idx]
    return px.reshape(bh, bw, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 3)[:height, :width]


def texture_dds(path: str) -> bytes:
    data = Path(path).read_bytes()
    key = hashlib.sha256(data).hexdigest()
    cached = _dds_cache.get(key)
    if cached is not None:
        return cached
    if data[:4] == texfile.DDS_MAGIC:
        texfile.texture_pair(data)
        encoded = data
    elif data[:8] == b'\x89PNG\r\n\x1a\n':
        # The authored cockpit PNGs are non-interlaced RGB/RGBA8. Pillow's C
        # decoder avoids the Python unfilter loop; other PNG forms keep the
        # reference decoder (notably its exact high-byte rule for 16-bit PNGs).
        if len(data) >= 29 and data[12:16] == b'IHDR' and data[24] == 8 and data[25] in (2, 6) and data[28] == 0:
            from PIL import Image
            with Image.open(BytesIO(data)) as source:
                image = texfile.Image(source.width, source.height, bytearray(source.convert('RGBA').tobytes()))
        else:
            image = texfile.decode_png(data)
        encoded = dxt1_dds(image)
    elif data[:2] == b'\xff\xd8':
        encoded = dxt1_dds(texfile.decode_jpeg(data))
    else:
        raise texfile.TextureError(f'{path}: neither PNG nor JPEG')
    return _dds_cache.put(key, encoded)


def compress_texture(data: bytes) -> bytes:
    key = hashlib.sha256(data).hexdigest()
    cached = _packed_cache.get(key)
    if cached is not None:
        return cached
    return _packed_cache.put(key, cmpl_compress(data))
