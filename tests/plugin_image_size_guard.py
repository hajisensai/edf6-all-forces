"""The built EDF6VehicleCrew.dll keeps its writable data section small (mem-4).

A writable image section is committed in full when the game maps the DLL, whether or not the
plugin ever touches it. In 2026-11 the .data section had grown to 120 MB (RawSize 15 MB, the DLL
25 MB) because the enemy-control and support streams were static arrays; they now live in
src/lazy_rows.h tables made on first use. This guard reads the PE header of the DLL that was just
built and fails when .data's VirtualSize passes the limit, so a new multi-megabyte static table is
caught before it ships. It also prints .data's RawSize and the file size.

Large per-session state belongs in a crew::LazyRows (or another first-use allocation), not in a
namespace-scope array.

Note: RelWithDebInfo links /INCREMENTAL, which keeps an old, larger section layout across relinks.
If this fails right after a table was removed, delete build/Mods/Plugins/EDF6VehicleCrew.ilk and
relink.

Usage: plugin_image_size_guard.py <path to EDF6VehicleCrew.dll>
"""
import os
import struct
import sys

LIMIT_DATA_VIRTUAL = 28 * 1024 * 1024
MB = 1024 * 1024


def sections(path):
    with open(path, "rb") as f:
        image = f.read(4096)
    if image[:2] != b"MZ":
        raise ValueError("not a PE image (no MZ)")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image (no PE signature)")
    count = struct.unpack_from("<H", image, pe + 6)[0]
    optional = struct.unpack_from("<H", image, pe + 20)[0]
    table = pe + 24 + optional
    for i in range(count):
        base = table + i * 40
        name = image[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
        virtual_size, _, raw_size = struct.unpack_from("<III", image, base + 8)
        yield name, virtual_size, raw_size


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    if not os.path.isfile(path):
        print(f"FAIL: {path} was not built")
        return 1
    data = [(v, r) for n, v, r in sections(path) if n == ".data"]
    if len(data) != 1:
        print(f"FAIL: {path} has {len(data)} .data sections")
        return 1
    virtual_size, raw_size = data[0]
    size = os.path.getsize(path)
    print(f"{os.path.basename(path)}: .data VirtualSize {virtual_size / MB:.2f} MB "
          f"(limit {LIMIT_DATA_VIRTUAL / MB:.0f} MB), RawSize {raw_size / MB:.2f} MB, file {size / MB:.2f} MB")
    if virtual_size > LIMIT_DATA_VIRTUAL:
        print("FAIL: a large static table was added; make it a crew::LazyRows (src/lazy_rows.h) "
              "or allocate it on first use. (After removing one under /INCREMENTAL, delete the .ilk and relink.)")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
