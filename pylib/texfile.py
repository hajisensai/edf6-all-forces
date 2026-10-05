"""Texture files for imported models, pure Python (struct, zlib; no PIL / numpy): read PNG and baseline JPEG images,
read / slice DDS files, and write the DDS files the game's model archives hold.

What the game's archives hold (every stock OBJECT/*.MRAB checked, pylib/mdb.py RAB members): each texture twice,
`<stem>.dds` in the HD-TEXTURE folder (flag 1) and `<stem>.lod.dds` in the TEXTURE folder (flag 0), both CMPL-compressed
DDS files with the legacy 124-byte header (no DX10 header), a full mip chain (flags 0xA1007, caps 0x401008), in DXT1 /
DXT3 / BC5U. The `.lod` member is the same picture 16x smaller per side (2048 -> 128, 1024x512 -> 64x32), but never
below 16 pixels on its shorter side (64 -> 16): exactly the tail of the HD file's mip chain.

    load_image(path) -> Image                     PNG (8 / 16 bit, gray / RGB / palette / alpha, not interlaced) or
                                                  baseline JPEG (Huffman, any sampling, restart markers), as RGBA8
    dds_info(data) -> DdsInfo                     header facts of a DDS file
    dds_tail(data, level) -> bytes                the DDS made of mip levels `level`.. of `data` (block / plain formats)
    lod_level(width, height) -> int               the mip level the .lod member starts at (16x smaller, >= 16 px)
    dxt1_dds(image) -> bytes                      a DXT1 DDS with a full box-filtered mip chain (a simple range-fit
                                                  encoder; DXT1 is what every stock albedo map is, and an uncompressed
                                                  DDS has no stock precedent the game is known to read)
    solid_dxt1(rgb, size) -> bytes                a one-colour DXT1 DDS (flat normal maps, neutral parameter maps)
    texture_pair(data) -> (hd, lod)               the two DDS files of one texture, from a DDS (sliced, not re-encoded)
"""
from __future__ import annotations

import math
import struct
import zlib
from dataclasses import dataclass

DDS_MAGIC = b'DDS '
DDSD = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000          # caps, height, width, pixel format, mip count
DDSD_LINEARSIZE, DDSD_PITCH = 0x80000, 0x8
CAPS = 0x401008                                     # complex, texture, mipmap (every stock file)
BLOCK_BYTES = {b'DXT1': 8, b'BC4U': 8, b'ATI1': 8, b'DXT3': 16, b'DXT5': 16, b'BC5U': 16, b'ATI2': 16}


class TextureError(Exception):
    pass


@dataclass
class Image:
    width: int
    height: int
    rgba: bytearray          # width * height * 4, row-major from the top-left


# ------------------------------------------------------------------------------------------ PNG

def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def _unfilter(raw: bytes, width: int, height: int, bpp: int) -> bytearray:
    """PNG scanline filters 0..4 undone; `bpp` bytes per pixel (>= 1)."""
    stride = width * bpp
    out = bytearray(stride * height)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        ft = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ft == 2:
            line = bytearray((a + b) & 0xFF for a, b in zip(line, prev))
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], up_left)) & 0xFF
        elif ft != 0:
            raise TextureError(f'PNG: unknown filter {ft}')
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return out


def decode_png(data: bytes) -> Image:
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise TextureError('not a PNG file')
    pos, idat, plte, trns = 8, bytearray(), b'', b''
    width = height = depth = ctype = interlace = 0
    while pos < len(data):
        n, kind = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            width, height, depth, ctype, _comp, _filt, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            plte = body
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
    if interlace:
        raise TextureError('PNG: interlaced images are not supported (save it without interlacing)')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth not in (8, 16) or (ctype == 3 and depth != 8):
        raise TextureError(f'PNG: colour type {ctype} at {depth} bits is not supported')
    bpp = channels * depth // 8
    px = _unfilter(zlib.decompress(bytes(idat)), width, height, bpp)
    if depth == 16:
        px = px[0::2]                                   # the high byte of each 16-bit sample
    out = bytearray(width * height * 4)
    n = width * height
    if ctype == 6:
        out[:] = px
    elif ctype == 2:
        out[0::4], out[1::4], out[2::4], out[3::4] = px[0::3], px[1::3], px[2::3], b'\xff' * n
    elif ctype == 0:
        out[0::4], out[1::4], out[2::4], out[3::4] = px, px, px, b'\xff' * n
    elif ctype == 4:
        out[0::4], out[1::4], out[2::4], out[3::4] = px[0::2], px[0::2], px[0::2], px[1::2]
    else:
        pal = [tuple(plte[3 * i:3 * i + 3]) + ((trns[i] if i < len(trns) else 255),) for i in range(len(plte) // 3)]
        for i, k in enumerate(px):
            out[4 * i:4 * i + 4] = bytes(pal[k])
    return Image(width, height, out)


# ------------------------------------------------------------------------------------------ JPEG (baseline)

ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14,
          21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60,
          61, 54, 47, 55, 62, 63]
# IDCT basis: COS[x][u] = C(u) / 2 * cos((2x + 1) u pi / 16)
COS = [[(math.sqrt(0.5) if u == 0 else 1.0) / 2 * math.cos((2 * x + 1) * u * math.pi / 16) for u in range(8)]
       for x in range(8)]
PEEK = 9                                            # Huffman fast-table width (bits)


class _Huffman:
    """A JPEG Huffman table: a PEEK-bit lookup for short codes, (maxcode, valptr) per length for the rest."""

    def __init__(self, counts: bytes, symbols: bytes) -> None:
        self.fast: list[tuple[int, int] | None] = [None] * (1 << PEEK)
        self.maxcode = [-1] * 18
        self.valptr = [0] * 17
        self.mincode = [0] * 17
        self.symbols = symbols
        code = k = 0
        for length in range(1, 17):
            self.valptr[length] = k
            self.mincode[length] = code
            for _ in range(counts[length - 1]):
                if length <= PEEK:
                    pad = PEEK - length
                    for f in range(code << pad, (code + 1) << pad):
                        self.fast[f] = (length, symbols[k])
                code += 1
                k += 1
            self.maxcode[length] = code - 1 if counts[length - 1] else -1
            code <<= 1
        self.maxcode[17] = 1 << 30


class _Bits:
    """MSB-first bit reader over one entropy-coded segment (byte stuffing already removed)."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0
        self.acc = 0
        self.n = 0

    def fill(self) -> None:
        while self.n <= 24:
            b = self.data[self.pos] if self.pos < len(self.data) else 0     # past the end: zero padding
            self.pos += 1
            self.acc = ((self.acc << 8) | b) & 0xFFFFFFFFFF
            self.n += 8

    def bits(self, k: int) -> int:
        if k == 0:
            return 0
        if self.n < k:
            self.fill()
        self.n -= k
        return (self.acc >> self.n) & ((1 << k) - 1)

    def decode(self, h: _Huffman) -> int:
        if self.n < 16:
            self.fill()
        hit = h.fast[(self.acc >> (self.n - PEEK)) & ((1 << PEEK) - 1)]
        if hit is not None:
            self.n -= hit[0]
            return hit[1]
        code = (self.acc >> (self.n - PEEK)) & ((1 << PEEK) - 1)
        self.n -= PEEK
        length = PEEK
        while True:
            length += 1
            code = (code << 1) | self.bits(1)
            if length > 16:
                raise TextureError('JPEG: bad Huffman code')
            if code <= h.maxcode[length]:
                return h.symbols[h.valptr[length] + code - h.mincode[length]]


def _extend(v: int, k: int) -> int:
    return v - (1 << k) + 1 if k and v < (1 << (k - 1)) else v


def _idct(c: list[float], out: bytearray, at: int, stride: int) -> None:
    """8x8 inverse DCT of natural-order coefficients `c`, +128, clamped, into `out` (row stride `stride`)."""
    rows = []
    for v in range(8):
        r = c[v * 8:v * 8 + 8]
        if not any(r[1:]):
            rows.append([r[0] * COS[0][0]] * 8)
            continue
        nz = [(u, r[u]) for u in range(8) if r[u]]
        rows.append([sum(k * cx[u] for u, k in nz) for cx in COS])
    for x in range(8):
        col = [rows[v][x] for v in range(8)]
        nz = [(v, k) for v, k in enumerate(col) if k]
        for y in range(8):
            s = sum(k * COS[y][v] for v, k in nz) + 128.0
            out[at + y * stride + x] = 0 if s < 0 else (255 if s > 255 else int(s + 0.5))


def decode_jpeg(data: bytes) -> Image:
    """Baseline sequential JPEG (SOF0 / SOF1, 8-bit, Huffman) of 1 or 3 components (YCbCr -> RGB, JFIF)."""
    if data[:2] != b'\xff\xd8':
        raise TextureError('not a JPEG file')
    qt: dict[int, list[int]] = {}
    hts: dict[tuple[int, int], _Huffman] = {}
    comps: list[dict] = []
    width = height = restart = 0
    pos = 2
    while pos < len(data):
        if data[pos] != 0xFF:
            raise TextureError('JPEG: marker expected')
        m = data[pos + 1]
        if m == 0xFF:
            pos += 1
            continue
        if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
            pos += 2
            continue
        n = struct.unpack_from('>H', data, pos + 2)[0]
        body = data[pos + 4:pos + 2 + n]
        pos += 2 + n
        if m in (0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            raise TextureError('JPEG: only baseline (sequential Huffman) images are supported; '
                               'save it as a baseline JPEG or as PNG')
        if m == 0xDB:
            i = 0
            while i < len(body):
                pq, tq = body[i] >> 4, body[i] & 15
                vals = struct.unpack_from('>64H', body, i + 1) if pq else tuple(body[i + 1:i + 65])
                q = [0] * 64
                for k, z in enumerate(ZIGZAG):
                    q[z] = vals[k]
                qt[tq] = q
                i += 1 + (128 if pq else 64)
        elif m == 0xC4:
            i = 0
            while i < len(body):
                tc, th = body[i] >> 4, body[i] & 15
                counts = body[i + 1:i + 17]
                total = sum(counts)
                hts[(tc, th)] = _Huffman(counts, body[i + 17:i + 17 + total])
                i += 17 + total
        elif m in (0xC0, 0xC1):
            if body[0] != 8:
                raise TextureError('JPEG: only 8-bit samples are supported')
            height, width = struct.unpack_from('>HH', body, 1)
            comps = [{'id': body[6 + 3 * k], 'h': body[7 + 3 * k] >> 4, 'v': body[7 + 3 * k] & 15,
                      'q': body[8 + 3 * k]} for k in range(body[5])]
        elif m == 0xDD:
            restart = struct.unpack_from('>H', body)[0]
        elif m == 0xDA:
            ns = body[0]
            for k in range(ns):
                cid, t = body[1 + 2 * k], body[2 + 2 * k]
                c = next(c for c in comps if c['id'] == cid)
                c['dc'], c['ac'] = hts[(0, t >> 4)], hts[(1, t & 15)]
            end = data.find(b'\xff\xd9', pos)
            return _decode_scan(data[pos:end if end >= 0 else len(data)], width, height, comps, qt, restart)
    raise TextureError('JPEG: no image data')


def _segments(scan: bytes) -> list[bytes]:
    """The entropy-coded data split at restart markers, byte stuffing (FF 00) removed."""
    out, start, i = [], 0, 0
    while True:
        i = scan.find(b'\xff', i)
        if i < 0 or i + 1 >= len(scan):
            break
        if 0xD0 <= scan[i + 1] <= 0xD7:
            out.append(scan[start:i])
            start = i = i + 2
        else:
            i += 2
    out.append(scan[start:])
    return [s.replace(b'\xff\x00', b'\xff') for s in out]


def _decode_scan(scan: bytes, width: int, height: int, comps: list[dict], qt: dict[int, list[int]],
                 restart: int) -> Image:
    hmax, vmax = max(c['h'] for c in comps), max(c['v'] for c in comps)
    mcux, mcuy = (width + 8 * hmax - 1) // (8 * hmax), (height + 8 * vmax - 1) // (8 * vmax)
    for c in comps:
        c['w'] = mcux * c['h'] * 8
        c['plane'] = bytearray(c['w'] * mcuy * c['v'] * 8)
    segs = _segments(scan)
    seg = 0
    bits = _Bits(segs[0])
    pred = [0] * len(comps)
    left = restart
    for my in range(mcuy):
        for mx in range(mcux):
            if restart and left == 0:
                seg += 1
                bits = _Bits(segs[seg])
                pred = [0] * len(comps)
                left = restart
            left -= 1
            for ci, c in enumerate(comps):
                q = qt[c['q']]
                for by in range(c['v']):
                    for bx in range(c['h']):
                        coef = [0.0] * 64
                        t = bits.decode(c['dc'])
                        pred[ci] += _extend(bits.bits(t), t)
                        coef[0] = pred[ci] * q[0]
                        k = 1
                        while k < 64:
                            rs = bits.decode(c['ac'])
                            r, s = rs >> 4, rs & 15
                            if s == 0:
                                if r != 15:
                                    break
                                k += 16
                                continue
                            k += r
                            z = ZIGZAG[k]
                            coef[z] = _extend(bits.bits(s), s) * q[z]
                            k += 1
                        x0 = (mx * c['h'] + bx) * 8
                        y0 = (my * c['v'] + by) * 8
                        _idct(coef, c['plane'], y0 * c['w'] + x0, c['w'])
    return _to_rgba(width, height, comps, hmax, vmax)


def _to_rgba(width: int, height: int, comps: list[dict], hmax: int, vmax: int) -> Image:
    out = bytearray(width * height * 4)
    out[3::4] = b'\xff' * (width * height)
    planes = []
    for c in comps:      # each plane at full resolution, nearest-neighbour upsampled, one row per image row
        sx, sy = hmax // c['h'], vmax // c['v']
        rows = []
        for y in range(height):
            src = c['plane'][(y // sy) * c['w']:(y // sy) * c['w'] + c['w']]
            rows.append(src[:width] if sx == 1 else bytes(b for b in src[:(width + sx - 1) // sx] for _ in range(sx))[:width])
        planes.append(rows)
    if len(comps) == 1:
        for y in range(height):
            g = planes[0][y]
            o = y * width * 4
            out[o:o + 4 * width:4], out[o + 1:o + 4 * width:4], out[o + 2:o + 4 * width:4] = g, g, g
        return Image(width, height, out)
    clamp = bytes(range(256))
    for y in range(height):
        Y, Cb, Cr = planes[0][y], planes[1][y], planes[2][y]
        r = bytes(clamp[min(255, max(0, int(yy + 1.402 * (cr - 128) + 0.5)))] for yy, cr in zip(Y, Cr))
        g = bytes(clamp[min(255, max(0, int(yy - 0.344136 * (cb - 128) - 0.714136 * (cr - 128) + 0.5)))]
                  for yy, cb, cr in zip(Y, Cb, Cr))
        b = bytes(clamp[min(255, max(0, int(yy + 1.772 * (cb - 128) + 0.5)))] for yy, cb in zip(Y, Cb))
        o = y * width * 4
        out[o:o + 4 * width:4], out[o + 1:o + 4 * width:4], out[o + 2:o + 4 * width:4] = r, g, b
    return Image(width, height, out)


def load_image(path: str) -> Image:
    """A PNG or JPEG file (by its content, not its extension) as RGBA8."""
    with open(path, 'rb') as h:
        data = h.read()
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        return decode_png(data)
    if data[:2] == b'\xff\xd8':
        return decode_jpeg(data)
    raise TextureError(f'{path}: neither PNG nor JPEG')


# ------------------------------------------------------------------------------------------ DDS

@dataclass
class DdsInfo:
    width: int
    height: int
    mips: int
    fourcc: bytes            # b'' for an uncompressed file
    bits: int                # bits per pixel of an uncompressed file (0 for block formats)


def dds_info(data: bytes) -> DdsInfo:
    if data[:4] != DDS_MAGIC or struct.unpack_from('<I', data, 4)[0] != 124:
        raise TextureError('not a DDS file')
    height, width = struct.unpack_from('<II', data, 12)
    mips = max(1, struct.unpack_from('<I', data, 28)[0])
    pf_flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    if pf_flags & 0x4:
        if fourcc == b'DX10':
            raise TextureError('DDS: DX10 headers are not supported (save it as DXT1 / DXT5 / BC5 or uncompressed)')
        if fourcc not in BLOCK_BYTES:
            raise TextureError(f'DDS: unsupported format {fourcc!r}')
        return DdsInfo(width, height, mips, fourcc, 0)
    return DdsInfo(width, height, mips, b'', bits)


def level_size(info: DdsInfo, w: int, h: int) -> int:
    if info.fourcc:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * BLOCK_BYTES[info.fourcc]
    return w * h * info.bits // 8


def lod_level(width: int, height: int) -> int:
    """The mip level a .lod member starts at: 16x smaller per side, its shorter side not below 16 px (stock rule)."""
    k = 0
    while k < 4 and min(width, height) >> (k + 1) >= 16:
        k += 1
    return k


def _header(width: int, height: int, mips: int, fourcc: bytes, bits: int, masks: tuple[int, int, int, int],
            top: int) -> bytes:
    pf = struct.pack('<II4sI4I', 32, 0x4 if fourcc else 0x41, fourcc, bits, *masks)
    flags = DDSD | (DDSD_LINEARSIZE if fourcc else DDSD_PITCH)
    return (DDS_MAGIC + struct.pack('<7I', 124, flags, height, width, top, 0, mips) + bytes(44) + pf +
            struct.pack('<5I', CAPS, 0, 0, 0, 0))


def dds_tail(data: bytes, level: int) -> bytes:
    """A DDS of mip levels `level`.. of `data` (same format; the header's size / pitch / mip count updated)."""
    info = dds_info(data)
    if level >= info.mips:
        raise TextureError(f'DDS has {info.mips} mip levels, level {level} asked')
    pos, w, h = 128, info.width, info.height
    for _ in range(level):
        pos += level_size(info, w, h)
        w, h = max(1, w // 2), max(1, h // 2)
    masks = struct.unpack_from('<4I', data, 92)
    top = level_size(info, w, h) if info.fourcc else w * info.bits // 8
    return _header(w, h, info.mips - level, info.fourcc, info.bits, masks, top) + data[pos:]


def texture_pair(data: bytes) -> tuple[bytes, bytes]:
    """(HD, .lod) DDS files of a DDS texture: the file itself, and its mip chain from lod_level() on. A file without
    the mips that needs is refused (re-save it with mipmaps)."""
    info = dds_info(data)
    k = lod_level(info.width, info.height)
    if info.mips <= k:
        raise TextureError(f'DDS {info.width}x{info.height} has {info.mips} mip level(s): save it with mipmaps')
    return data, dds_tail(data, k)


def _half(img: Image) -> Image:
    """A 2x2 box-filtered half-size copy (odd sizes: the last row / column repeated)."""
    w, h = max(1, img.width // 2), max(1, img.height // 2)
    src, sw = img.rgba, img.width * 4
    out = bytearray(w * h * 4)
    for y in range(h):
        r0 = min(2 * y, img.height - 1) * sw
        r1 = min(2 * y + 1, img.height - 1) * sw
        o = y * w * 4
        for x in range(w):
            a = 8 * x if img.width > 1 else 0
            b = a + 4 if img.width > 1 else 0
            for ch in range(4):
                out[o + 4 * x + ch] = (src[r0 + a + ch] + src[r0 + b + ch] + src[r1 + a + ch] + src[r1 + b + ch] + 2) >> 2
    return Image(w, h, out)


def mip_chain(img: Image) -> list[Image]:
    chain = [img]
    while chain[-1].width > 1 or chain[-1].height > 1:
        chain.append(_half(chain[-1]))
    return chain


def _rgb565(r: int, g: int, b: int) -> int:
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def _dxt1_block(px: list[tuple[int, int, int]]) -> bytes:
    """One DXT1 block of 16 RGB pixels: the endpoints are the extremes along the colour box's longest axis
    (four-colour mode, colour0 > colour1), each pixel the nearest of the four palette colours on that line."""
    lo = [min(p[c] for p in px) for c in range(3)]
    hi = [max(p[c] for p in px) for c in range(3)]
    ax = max(range(3), key=lambda c: hi[c] - lo[c])
    a = max(px, key=lambda p: p[ax])
    b = min(px, key=lambda p: p[ax])
    c0, c1 = _rgb565(*a), _rgb565(*b)
    if c0 == c1:
        return struct.pack('<HHI', c0, c1, 0)
    if c0 < c1:
        c0, c1, a, b = c1, c0, b, a
    d = [a[c] - b[c] for c in range(3)]
    dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] or 1
    idx = 0
    for i, p in enumerate(px):
        t = ((p[0] - b[0]) * d[0] + (p[1] - b[1]) * d[1] + (p[2] - b[2]) * d[2]) * 3 / dd   # 0 = colour1, 3 = colour0
        k = (1, 3, 2, 0)[0 if t < 0.5 else (1 if t < 1.5 else (2 if t < 2.5 else 3))]
        idx |= k << (2 * i)
    return struct.pack('<HHI', c0, c1, idx)


def _dxt1_level(img: Image) -> bytes:
    w, h, src = img.width, img.height, img.rgba
    out = bytearray()
    for by in range(0, max(h, 4), 4):
        for bx in range(0, max(w, 4), 4):
            px = []
            for y in range(4):
                row = min(by + y, h - 1) * w
                for x in range(4):
                    o = (row + min(bx + x, w - 1)) * 4
                    px.append((src[o], src[o + 1], src[o + 2]))
            out += _dxt1_block(px)
    return bytes(out)


def dxt1_dds(img: Image) -> bytes:
    """A DXT1 DDS of `img` (alpha dropped) with its whole mip chain."""
    chain = mip_chain(img)
    body = b''.join(_dxt1_level(m) for m in chain)
    top = max(1, (img.width + 3) // 4) * max(1, (img.height + 3) // 4) * 8
    return _header(img.width, img.height, len(chain), b'DXT1', 0, (0, 0, 0, 0), top) + body


def solid_dxt1(rgb: tuple[int, int, int], size: int = 16) -> bytes:
    """A size x size DXT1 DDS of one colour, full mip chain."""
    return dxt1_dds(Image(size, size, bytearray(bytes((rgb[0], rgb[1], rgb[2], 255)) * (size * size))))
