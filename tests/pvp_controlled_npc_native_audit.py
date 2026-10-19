"""Read-only supported EDF.dll NPC contracts; never loads the game module."""
from pathlib import Path
import argparse
import re
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("edf_dll", nargs="?", type=Path)
args = parser.parse_args()
if not args.edf_dll or not args.edf_dll.is_file():
    print("SKIP: pass supported EDF.dll")
    raise SystemExit(77)
import pefile
pe = pefile.PE(str(args.edf_dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
source = (Path(__file__).resolve().parents[1] / "src/pvp_controlled_npc.cpp").read_text(encoding="utf-8")
anchors = re.findall(r"\{(0x[0-9A-F]+),(\d+),\{([^}]+)\}\}", source)
for address, length, values in anchors:
    address, length = int(address, 16), int(length)
    expected = bytes(int(v.strip(), 0) for v in values.split(","))
    assert len(expected) == length
    assert pe.get_data(address, length) == expected, hex(address)
base = pe.OPTIONAL_HEADER.ImageBase
for name, vtable in re.findall(r'\{"([A-Za-z_][A-Za-z0-9_]*)",(0x[0-9A-F]+)(?:,|\})', source):
    vtable = int(vtable, 16)
    col = struct.unpack("<Q", pe.get_data(vtable - 8, 8))[0] - base
    signature, offset, cd_offset, descriptor, _, self_rva = struct.unpack("<6I", pe.get_data(col, 24))
    assert (signature, offset, cd_offset, self_rva) == (1, 0, 0, col)
    expected = (".?AV" + name + "@@").encode() + b"\0"
    assert pe.get_data(descriptor + 16, len(expected)) == expected, name
slot_count = 0
table_count = 0
for name, values in re.findall(r"constexpr std::uint32_t (k[A-Za-z0-9]+Slots)\[\]=\{([^}]+)\}", source):
    expected = [int(v.strip(), 0) for v in values.split(",") if v.strip()]
    slot_count += len(expected)
    table_count += 1
    vtable = int(re.search(r"(0x[0-9A-F]+)," + name + r",", source)[1], 16)
    assert list(struct.unpack("<" + "Q" * len(expected), pe.get_data(vtable, len(expected) * 8))) == [base + v for v in expected], name
assert pe.get_data(0x68CDA2 + 0x1155D06, 20) == "FireCount\0".encode("utf-16le")
for address, target in [(0x697498, 0x690EF0), (0x69799F, 0x1194280), (0x58F5DA, 0x11945E0), (0x68AD94, 0x118AF20)]:
    code = pe.get_data(address, 5)
    assert code[0] == 0xE8 and address + 5 + struct.unpack("<i", code[1:])[0] == target, hex(address)
print(f"NPC native audit: {len(anchors)} anchors, {table_count} exact primary tables/{slot_count} slots, RTTI, FireCount key, four calls verified")
