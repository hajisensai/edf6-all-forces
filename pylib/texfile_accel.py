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
