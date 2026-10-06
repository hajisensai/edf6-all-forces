"""CRILAYLA decompression (CRI's LZ variant used for compressed CPK entries). Read-only.

Layout: 'CRILAYLA', u32 uncompressed size, u32 compressed size; the compressed bit stream follows
and is read backwards from its end, and the first 0x100 bytes of the output are stored verbatim
after it.
"""
import struct

_LENGTH_BITS = (2, 3, 5, 8)


def decompress(data: bytes) -> bytes:
    if data[:8] != b'CRILAYLA':
        raise ValueError('not CRILAYLA data')
    size, compressed = struct.unpack_from('<II', data, 8)
    prefix = data[0x10 + compressed:0x10 + compressed + 0x100]
    out = bytearray(0x100 + size)
    out[:0x100] = prefix
    position = 0x10 + compressed - 1   # last byte of the bit stream
    pool = 0
    left = 0

    def bits(count: int) -> int:
        nonlocal position, pool, left
        while left < count:
            pool = (pool << 8) | data[position]
            position -= 1
            left += 8
        left -= count
        value = pool >> left
        pool &= (1 << left) - 1
        return value

    end = 0x100 + size - 1
    written = 0
    while written < size:
        if bits(1):
            source = end - written + bits(13) + 3
            length = 3
            for width in _LENGTH_BITS:
                level = bits(width)
                length += level
                if level != (1 << width) - 1:
                    break
            else:
                while True:
                    level = bits(8)
                    length += level
                    if level != 255:
                        break
            # Copy back-references in non-overlapping slices. Doubling the available
            # span handles repeating runs without one Python iteration per byte.
            dest = end - written
            distance = source - dest
            remaining = length
            while remaining:
                take = min(distance, remaining)
                out[dest - take + 1:dest + 1] = out[dest + distance - take + 1:dest + distance + 1]
                dest -= take
                remaining -= take
                distance += take
            written += length
        else:
            out[end - written] = bits(8)
            written += 1
    return bytes(out)
