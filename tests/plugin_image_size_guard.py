"""The built EDF6VehicleCrew.dll keeps its writable data section small (mem-4).

A writable image section is committed in full when the game maps the DLL, whether or not the
plugin ever touches it. In 2026-11 the .data section had grown to 120 MB (RawSize 15 MB, the DLL
25 MB) because the enemy-control and support streams were static arrays; they now live in
src/lazy_rows.h tables made on first use. On 2026-11-27 it was back at the 28 MB limit (per-vehicle /
per-heli / per-jet slot tables, the kill-count and damage-stats rows, ...); those moved to LazyRows
too. This guard reads the PE header of the DLL that was just built and fails when .data's
VirtualSize passes the limit, so a new multi-megabyte static table is caught before it ships. It also
prints .data's RawSize and the file size.

When .data is within WARN_MARGIN of the limit (or over it) it prints the ten biggest objects in .data
from the linker map the build writes (/MAP, build/EDF6VehicleCrew.map): a symbol's size is the gap to
the next symbol in the section, so a table and its object file are named without a separate dumpbin
run.

Large per-session state belongs in a crew::LazyRows (or another first-use allocation), not in a
namespace-scope array.

Note: RelWithDebInfo links /INCREMENTAL, which keeps an old, larger section layout across relinks.
If this fails right after a table was removed, delete build/Mods/Plugins/EDF6VehicleCrew.ilk and
relink.

Usage: plugin_image_size_guard.py <path to EDF6VehicleCrew.dll> <path to its linker .map>
       plugin_image_size_guard.py --top <path to EDF6VehicleCrew.dll> <path to its linker .map>
         (always print the ten biggest objects)
"""
import os
import re
import struct
import sys

LIMIT_DATA_VIRTUAL = 28 * 1024 * 1024
MB = 1024 * 1024
WARN_MARGIN = 2 * MB
TOP = 10

# ' 0003:00b47030       ?rows@...@3PAU...@A    0000000180b4f030 f i  crew.cpp.obj'
_SYMBOL = re.compile(r'^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+[0-9a-fA-F]{16}\s+(?:[fi]\s+)*(\S.*?)\s*$')


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


def top_objects(map_path, section, virtual_size, raw_size, count=TOP):
    """The `count` biggest symbols of PE section number `section` (1-based) in the linker map:
    (size, name, object file, 'bss' or 'init'). A symbol runs to the next symbol of the section
    (the last one to the section's end)."""
    found = {}
    with open(map_path, encoding="ascii", errors="replace") as f:
        for line in f:
            m = _SYMBOL.match(line)
            if m and int(m.group(1), 16) == section:
                offset = int(m.group(2), 16)
                found.setdefault(offset, (m.group(3), m.group(4).split()[-1]))
    offsets = sorted(found)
    rows = []
    for i, offset in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < len(offsets) else virtual_size
        name, obj = found[offset]
        rows.append((end - offset, name, obj, "init" if offset < raw_size else "bss"))
    rows.sort(reverse=True)
    return rows[:count]


def main():
    args = sys.argv[1:]
    always = bool(args) and args[0] == "--top"
    if always:
        args = args[1:]
    if len(args) != 2:
        print(__doc__)
        return 2
    path, map_path = args
    if not os.path.isfile(path):
        print(f"FAIL: {path} was not built")
        return 1
    found = [(i + 1, v, r) for i, (n, v, r) in enumerate(sections(path)) if n == ".data"]
    if len(found) != 1:
        print(f"FAIL: {path} has {len(found)} .data sections")
        return 1
    section, virtual_size, raw_size = found[0]
    size = os.path.getsize(path)
    print(f"{os.path.basename(path)}: .data VirtualSize {virtual_size / MB:.2f} MB "
          f"(limit {LIMIT_DATA_VIRTUAL / MB:.0f} MB), RawSize {raw_size / MB:.2f} MB, file {size / MB:.2f} MB")
    if always or virtual_size > LIMIT_DATA_VIRTUAL - WARN_MARGIN:
        if not os.path.isfile(map_path):
            print(f"FAIL: {map_path} was not written (the plugin links with /MAP)")
            return 1
        print(f"The {TOP} biggest objects in .data ({map_path}):")
        for bytes_, name, obj, kind in top_objects(map_path, section, virtual_size, raw_size):
            print(f"  {bytes_ / MB:8.2f} MB  {kind:4}  {obj:36}  {name}")
    if virtual_size > LIMIT_DATA_VIRTUAL:
        print("FAIL: a large static table was added; make it a crew::LazyRows (src/lazy_rows.h) "
              "or allocate it on first use. (After removing one under /INCREMENTAL, delete the .ilk and relink.)")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
