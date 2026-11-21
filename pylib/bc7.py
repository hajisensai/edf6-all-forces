"""BC7 blocks (DXGI_FORMAT_BC7_UNORM, 98), pure Python: the reference encoder for the installer's colour maps and a
decoder of every mode (the stock BC7 maps read back). native/bc7.cpp is the encoder's port (the same bytes: tests/
test_native_bc7.py); texfile_accel.bc7_dds drives it.

What the game takes (docs/model-quality-1121-bc7.md): 179 stock model textures in OBJECT / WEAPON archives are BC7,
every one DXGI 98 (BC7_UNORM, never the _SRGB 99), albedo maps among them; EDF.dll's format table (0x1471990 builds it)
pairs internal format 180 = DXGI 98 with its sRGB twin 181 exactly as 120 = BC1_UNORM (71, what a legacy 'DXT1'
header maps to, 0x146B74E) with 121, so a BC7_UNORM colour map is read as a DXT1 one is.

Encoder (opaque: alpha is always 255, as a DXT1 colour map's 4-colour blocks decode): modes 6 (one subset, 4-bit
indices, 7-bit endpoints + p-bits; both p-bits 1, which alpha 255 needs), 1 (two subsets of the 64 partitions, 3-bit
indices, 6-bit endpoints + a p-bit per subset) and 3 (two subsets, 2-bit indices, 7-bit endpoints + a p-bit each),
the least squared RGB error kept (the first of equals, in that order). Integers only, so that the C++ port gives the
same bytes. Per subset: the principal axis (POWER power iterations of the covariance from the colour box's longest
axis, in 1/UNIT steps), the extremes of the pixels' projections on the line through the mean as endpoints, every p-bit
choice quantized (nearest code, the lower of equals), the nearest palette entry per pixel (the lower index of equals),
then up to REFITS least-squares refits of the endpoints to the indices while they lower the error. A block mode 6
already gives within GOOD keeps it; otherwise each partition is scored by the unquantized 3-bit fit of both subsets and
the CANDIDATES best (the lower partition of equals) are encoded in modes 1 and 3. A subset of one colour skips all
that: every p-bit choice and index with the codes near its own that interpolate nearest (_solid; exact but for
black in mode 6, whose p-bit 1 makes every code odd).

    encode_block(px) -> bytes            16 (r, g, b) pixels, row-major -> one 16-byte block
    encode_level(rgba, w, h) -> bytes    a mip level, blocks row by row, edge pixels repeated past the image
    decode_block(block) -> list          one block of any mode -> 16 (r, g, b, a)
"""
from __future__ import annotations

WEIGHTS = {2: (0, 21, 43, 64), 3: (0, 9, 18, 27, 37, 46, 55, 64),
           4: (0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64)}

# Partition tables (the BC7 format's; Pillow's BcnDecode.c has the same): bit i of P2 = subset of pixel i,
# bits 2i..2i+1 of P3 = subset of pixel i; A2 = subset 1's anchor (two subsets), A31 / A32 subsets 1 / 2's (three).
P2 = (0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8, 0xff00,
      0xfff0, 0xf000, 0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce, 0x088c, 0x3110, 0x6666, 0x366c,
      0x17e8, 0x0ff0, 0x718e, 0x399c, 0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696, 0xa55a, 0x73ce, 0x13c8,
      0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660, 0x0272, 0x04e4, 0x4e40, 0x2720, 0xc936, 0x936c, 0x39c6, 0x639c,
      0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22)
P3 = (0xaa685050, 0x6a5a5040, 0x5a5a4200, 0x5450a0a8, 0xa5a50000, 0xa0a05050, 0x5555a0a0, 0x5a5a5050, 0xaa550000,
      0xaa555500, 0xaaaa5500, 0x90909090, 0x94949494, 0xa4a4a4a4, 0xa9a59450, 0x2a0a4250, 0xa5945040, 0x0a425054,
      0xa5a5a500, 0x55a0a0a0, 0xa8a85454, 0x6a6a4040, 0xa4a45000, 0x1a1a0500, 0x0050a4a4, 0xaaa59090, 0x14696914,
      0x69691400, 0xa08585a0, 0xaa821414, 0x50a4a450, 0x6a5a0200, 0xa9a58000, 0x5090a0a8, 0xa8a09050, 0x24242424,
      0x00aa5500, 0x24924924, 0x24499224, 0x50a50a50, 0x500aa550, 0xaaaa4444, 0x66660000, 0xa5a0a5a0, 0x50a050a0,
      0x69286928, 0x44aaaa44, 0x66666600, 0xaa444444, 0x54a854a8, 0x95809580, 0x96969600, 0xa85454a8, 0x80959580,
      0xaa141414, 0x96960000, 0xaaaa1414, 0xa05050a0, 0xa0a5a5a0, 0x96000000, 0x40804080, 0xa9a8a9a8, 0xaaaaaa44,
      0x2a4a5254)
A2 = (15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
      15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15)
A31 = (3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
       8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3)
A32 = (15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
       15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8)

# mode -> (subsets, partition bits, rotation bits, index-selection bit, colour bits, alpha bits, p-bit per endpoint,
#          p-bit per subset, index bits, second index bits)
MODES = ((3, 4, 0, 0, 4, 0, 1, 0, 3, 0), (2, 6, 0, 0, 6, 0, 0, 1, 3, 0), (3, 6, 0, 0, 5, 0, 0, 0, 2, 0),
         (2, 6, 0, 0, 7, 0, 1, 0, 2, 0), (1, 0, 2, 1, 5, 6, 0, 0, 2, 3), (1, 0, 2, 0, 7, 8, 0, 0, 2, 2),
         (1, 0, 0, 0, 7, 7, 1, 0, 4, 0), (2, 6, 0, 0, 5, 5, 1, 0, 2, 0))

# Encoder constants (native/bc7.cpp has the same)
POWER = 4
UNIT = 1024
REFITS = 2
# mode 6 within GOOD (a mean squared error of 2/3 per channel) skips the two-subset search; below 48, the error of a
# flat block mode 6 misses by one step everywhere (black: its codes are all odd), which mode 1 / 3 hit
GOOD = 32
CANDIDATES = 1
SOLID_REACH = 2       # a one-colour subset tries codes this far either side of the nearest


def subset(subsets: int, partition: int, i: int) -> int:
    if subsets == 1:
        return 0
    if subsets == 2:
        return (P2[partition] >> i) & 1
    return (P3[partition] >> (2 * i)) & 3


def anchors(subsets: int, partition: int) -> tuple[int, ...]:
    if subsets == 1:
        return (0,)
    if subsets == 2:
        return (0, A2[partition])
    return (0, A31[partition], A32[partition])


def _expand(v: int, bits: int) -> int:
    """A `bits`-bit endpoint value (p-bit included) to 8 bits: the high bits repeated below."""
    v <<= 8 - bits
    return v | (v >> bits)


# ------------------------------------------------------------------------------------------ decoder

def decode_block(block: bytes) -> list[tuple[int, int, int, int]]:
    """One BC7 block -> 16 RGBA pixels (row-major). A block with no mode bit (reserved) decodes to zeros (D3D)."""
    bits = int.from_bytes(block[:16], 'little')
    pos = 0

    def take(n: int) -> int:
        nonlocal pos
        v = (bits >> pos) & ((1 << n) - 1)
        pos += n
        return v
    mode = 0
    while mode < 8 and not (bits >> mode) & 1:
        mode += 1
    if mode == 8:
        return [(0, 0, 0, 0)] * 16
    pos = mode + 1
    ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2 = MODES[mode]
    partition, rotation, sel = take(pb), take(rb), take(isb)
    ends = [[0, 0, 0, 255] for _ in range(2 * ns)]
    for c in range(3):
        for e in ends:
            e[c] = take(cb)
    if ab:
        for e in ends:
            e[3] = take(ab)
    if epb:
        pbits = [take(1) for _ in range(2 * ns)]
    elif spb:
        shared = [take(1) for _ in range(ns)]
        pbits = [shared[k // 2] for k in range(2 * ns)]
    else:
        pbits = None
    for k, e in enumerate(ends):
        for c in range(4 if ab else 3):
            n = cb if c < 3 else ab
            if pbits is not None:
                e[c] = _expand((e[c] << 1) | pbits[k], n + 1)
            else:
                e[c] = _expand(e[c], n)
    anchor = anchors(ns, partition)
    idx = [take(ib - 1 if i in anchor else ib) for i in range(16)]
    idx2 = [take(ib2 - 1 if i == 0 else ib2) for i in range(16)] if ib2 else None
    out = []
    for i in range(16):
        s = subset(ns, partition, i)
        e0, e1 = ends[2 * s], ends[2 * s + 1]
        if idx2 is None:
            ci, cbits, ai, abits = idx[i], ib, idx[i], ib
        elif sel:
            ci, cbits, ai, abits = idx2[i], ib2, idx[i], ib
        else:
            ci, cbits, ai, abits = idx[i], ib, idx2[i], ib2
        wc, wa = WEIGHTS[cbits][ci], WEIGHTS[abits][ai]
        px = [((64 - wc) * e0[c] + wc * e1[c] + 32) >> 6 for c in range(3)]
        px.append(((64 - wa) * e0[3] + wa * e1[3] + 32) >> 6)
        if rotation:
            px[3], px[rotation - 1] = px[rotation - 1], px[3]
        out.append(tuple(px))
    return out


def decode_level(data: bytes, width: int, height: int) -> bytearray:
    """A BC7 mip level -> width x height RGBA8, row-major."""
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    out = bytearray(width * height * 4)
    for by in range(bh):
        for bx in range(bw):
            k = by * bw + bx
            px = decode_block(data[16 * k:16 * k + 16])
            for y in range(4):
                for x in range(4):
                    X, Y = bx * 4 + x, by * 4 + y
                    if X < width and Y < height:
                        o = (Y * width + X) * 4
                        out[o:o + 4] = bytes(px[y * 4 + x])
    return out


# ------------------------------------------------------------------------------------------ encoder

# endpoint kinds the encoder writes: (colour bits, how p-bits go: 'shared' one per subset, 'each' one per endpoint,
# 'one' both 1 (mode 6: alpha 255 is 7-bit 127 with p-bit 1), index bits)
_KIND = {6: (7, 'one', 4), 1: (6, 'shared', 3), 3: (7, 'each', 2)}
_PBITS = {'one': ((1, 1),), 'shared': ((0, 0), (1, 1)), 'each': ((0, 0), (0, 1), (1, 0), (1, 1))}


def _quant_table(cbits: int, p: int) -> list[int]:
    """8-bit value -> the nearest `cbits`-bit code with p-bit `p` (the lower code of equals)."""
    out = []
    for t in range(256):
        best, code = 1 << 30, 0
        for c in range(1 << cbits):
            d = abs(_expand((c << 1) | p, cbits + 1) - t)
            if d < best:
                best, code = d, c
        out.append(code)
    return out


_QUANT = {(cb, p): _quant_table(cb, p) for cb in (6, 7) for p in (0, 1)}


def _floordiv(a: int, b: int) -> int:
    return a // b


def _round_div(a: int, b: int) -> int:
    """a / b to the nearest integer, halves up (b > 0)."""
    return (2 * a + b) // (2 * b)


def _clamp(v: int) -> int:
    return 0 if v < 0 else (255 if v > 255 else v)


def _line_ends(px: list[tuple[int, int, int]], members: list[int]) -> tuple[list[int], list[int]]:
    """Unquantized endpoints of the pixels `members`: their projections' extremes on the principal axis through the
    mean."""
    n = len(members)
    s = [sum(px[i][c] for i in members) for c in range(3)]
    d = [[n * px[i][c] - s[c] for c in range(3)] for i in members]
    cov = [[sum(q[a] * q[b] for q in d) for b in range(3)] for a in range(3)]
    span = [max(px[i][c] for i in members) - min(px[i][c] for i in members) for c in range(3)]
    axis = 0
    for c in (1, 2):
        if span[c] > span[axis]:
            axis = c
    v = [0, 0, 0]
    v[axis] = UNIT
    for _ in range(POWER):
        u = [cov[a][0] * v[0] + cov[a][1] * v[1] + cov[a][2] * v[2] for a in range(3)]
        m = max(abs(u[0]), abs(u[1]), abs(u[2]))
        if m == 0:
            break
        v = [_floordiv(u[a] * UNIT, m) for a in range(3)]
    vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]
    t = [px[i][0] * v[0] + px[i][1] * v[1] + px[i][2] * v[2] for i in members]
    total = sum(t)
    lo, hi = min(t), max(t)
    e0 = [_clamp(_round_div(s[c] * vv + (n * lo - total) * v[c], n * vv)) for c in range(3)]
    e1 = [_clamp(_round_div(s[c] * vv + (n * hi - total) * v[c], n * vv)) for c in range(3)]
    return e0, e1


def _assign(px: list[tuple[int, int, int]], members: list[int], e0: list[int], e1: list[int],
            ibits: int) -> tuple[int, list[int]]:
    """(squared error, index per member) for the palette between 8-bit endpoints e0 / e1."""
    pal = [[((64 - w) * e0[c] + w * e1[c] + 32) >> 6 for c in range(3)] for w in WEIGHTS[ibits]]
    err, idx = 0, []
    for i in members:
        p = px[i]
        best, k = 1 << 62, 0
        for j, q in enumerate(pal):
            d = (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2
            if d < best:
                best, k = d, j
        err += best
        idx.append(k)
    return err, idx


def _quantize(px, members, e0, e1, mode):  # noqa: ANN001, ANN202
    """The best p-bit choice for unquantized endpoints e0 / e1: (error, codes0, codes1, p0, p1, indices)."""
    cbits, how, ibits = _KIND[mode]
    best = None
    for p0, p1 in _PBITS[how]:
        q0 = [_QUANT[cbits, p0][e0[c]] for c in range(3)]
        q1 = [_QUANT[cbits, p1][e1[c]] for c in range(3)]
        x0 = [_expand((q << 1) | p0, cbits + 1) for q in q0]
        x1 = [_expand((q << 1) | p1, cbits + 1) for q in q1]
        err, idx = _assign(px, members, x0, x1, ibits)
        if best is None or err < best[0]:
            best = (err, q0, q1, p0, p1, idx)
    return best


def _refit(px, members, idx, ibits):  # noqa: ANN001, ANN202
    """Least-squares endpoints (8-bit, unquantized) for these indices, None when every index is the same weight."""
    ws = WEIGHTS[ibits]
    a = b = c = 0
    x = [0, 0, 0]
    y = [0, 0, 0]
    for i, k in zip(members, idx):
        w = ws[k]
        a += (64 - w) * (64 - w)
        b += (64 - w) * w
        c += w * w
        for ch in range(3):
            x[ch] += (64 - w) * 64 * px[i][ch]
            y[ch] += w * 64 * px[i][ch]
    det = a * c - b * b
    if det == 0:
        return None
    e0 = [_clamp(_round_div(c * x[ch] - b * y[ch], det)) for ch in range(3)]
    e1 = [_clamp(_round_div(a * y[ch] - b * x[ch], det)) for ch in range(3)]
    return e0, e1


def _solid(px, members, mode):  # noqa: ANN001, ANN202
    """A subset of one colour: per p-bit choice and palette index, each channel's pair of codes (within SOLID_REACH of
    the nearest one) whose interpolation comes nearest (the first of equals), the least error kept."""
    t = px[members[0]]
    cbits, how, ibits = _KIND[mode]
    top = (1 << cbits) - 1
    best = None
    for p0, p1 in _PBITS[how]:
        for j, w in enumerate(WEIGHTS[ibits]):
            err, q0, q1 = 0, [0, 0, 0], [0, 0, 0]
            for c in range(3):
                b0, b1 = _QUANT[cbits, p0][t[c]], _QUANT[cbits, p1][t[c]]
                got = None
                for a0 in range(max(0, b0 - SOLID_REACH), min(top, b0 + SOLID_REACH) + 1):
                    x0 = _expand((a0 << 1) | p0, cbits + 1)
                    for a1 in range(max(0, b1 - SOLID_REACH), min(top, b1 + SOLID_REACH) + 1):
                        x1 = _expand((a1 << 1) | p1, cbits + 1)
                        d = abs((((64 - w) * x0 + w * x1 + 32) >> 6) - t[c])
                        if got is None or d < got[0]:
                            got = (d, a0, a1)
                err += got[0] * got[0]
                q0[c], q1[c] = got[1], got[2]
            err *= len(members)
            if best is None or err < best[0]:
                best = (err, q0, q1, p0, p1, [j] * len(members))
    return best


def _fit(px, members, mode):  # noqa: ANN001, ANN202
    if all(px[i] == px[members[0]] for i in members):
        return _solid(px, members, mode)
    e0, e1 = _line_ends(px, members)
    best = _quantize(px, members, e0, e1, mode)
    for _ in range(REFITS):
        ends = _refit(px, members, best[5], _KIND[mode][2])
        if ends is None:
            # one palette entry for all: a flat colour between two codes (mode 6's odd-only values), the endpoints
            # one step apart around it reach it by interpolation
            if best[0]:
                lo = [_clamp(min(e0[c], e1[c]) - 1) for c in range(3)]
                hi = [_clamp(max(e0[c], e1[c]) + 1) for c in range(3)]
                got = _quantize(px, members, lo, hi, mode)
                if got[0] < best[0]:
                    best = got
            break
        got = _quantize(px, members, ends[0], ends[1], mode)
        if got[0] >= best[0]:
            break
        best = got
    return best


class _Bits:
    def __init__(self) -> None:
        self.v = 0
        self.n = 0

    def put(self, value: int, bits: int) -> None:
        self.v |= value << self.n
        self.n += bits


def _pack(mode: int, partition: int, fits: list) -> bytes:  # noqa: ANN001
    """One block of `mode` from per-subset fits (error, codes0, codes1, p0, p1, indices over the subset's pixels in
    pixel order); an anchor whose index has its top bit set swaps its subset's endpoints (and p-bits) and inverts
    its indices (the weights are symmetric: the same palette)."""
    ns, pb, _rb, _isb, cb, ab, epb, spb, ib, _ib2 = MODES[mode]
    top = 1 << (ib - 1)
    anchor = anchors(ns, partition)
    idx = [0] * 16
    ends = []
    pbits = []
    for s in range(ns):
        _err, q0, q1, p0, p1, sidx = fits[s]
        members = [i for i in range(16) if subset(ns, partition, i) == s]
        for i, k in zip(members, sidx):
            idx[i] = k
        if idx[anchor[s]] & top:
            q0, q1, p0, p1 = q1, q0, p1, p0
            for i in members:
                idx[i] = (1 << ib) - 1 - idx[i]
        ends += [q0, q1]
        pbits += [p0, p1]
    out = _Bits()
    out.put(1 << mode, mode + 1)
    out.put(partition, pb)
    for c in range(3):
        for e in ends:
            out.put(e[c], cb)
    if ab:                                   # mode 6: alpha 127 (with p-bit 1: 255)
        for _ in ends:
            out.put(127, ab)
    if epb:
        for p in pbits:
            out.put(p, 1)
    elif spb:
        for s in range(ns):
            out.put(pbits[2 * s], 1)
    for i in range(16):
        out.put(idx[i], ib - 1 if i in anchor else ib)
    assert out.n == 128
    return out.v.to_bytes(16, 'little')


def encode_block(px: list[tuple[int, int, int]]) -> bytes:
    allpx = list(range(16))
    best = _fit(px, allpx, 6)
    choice = (best[0], 6, 0, [best])
    if best[0] > GOOD:
        scores = []
        for p in range(64):
            score = 0
            for s in range(2):
                members = [i for i in allpx if (P2[p] >> i) & 1 == s]
                e0, e1 = _line_ends(px, members)
                score += _assign(px, members, e0, e1, 3)[0]
            scores.append((score, p))
        scores.sort()
        for _score, p in scores[:CANDIDATES]:
            groups = [[i for i in allpx if (P2[p] >> i) & 1 == s] for s in range(2)]
            for mode in (1, 3):
                fits = [_fit(px, g, mode) for g in groups]
                err = fits[0][0] + fits[1][0]
                if err < choice[0]:
                    choice = (err, mode, p, fits)
    return _pack(choice[1], choice[2], choice[3])


def block_pixels(rgba: bytes, width: int, height: int, bx: int, by: int) -> list[tuple[int, int, int]]:
    px = []
    for y in range(4):
        row = min(by * 4 + y, height - 1) * width
        for x in range(4):
            o = (row + min(bx * 4 + x, width - 1)) * 4
            px.append((rgba[o], rgba[o + 1], rgba[o + 2]))
    return px


def encode_level(rgba: bytes, width: int, height: int) -> bytes:
    out = bytearray()
    for by in range((height + 3) // 4):
        for bx in range((width + 3) // 4):
            out += encode_block(block_pixels(rgba, width, height, bx, by))
    return bytes(out)
