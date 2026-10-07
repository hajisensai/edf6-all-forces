"""The Begaruta family's per-frame chain, against the source and (when present) the installed EDF.dll, read-only.

504, the 612 Nix, Begaruta and BigBegaruta (Proteus) share slot 4 0x644350, the only way to their player input and fire;
their slot 55 is the AI's think (a callback the AI component calls), so crew.cpp must chain slot 4 for every one of them
and slot 55's 0x63C1C0 for none (docs/proteus-re.md, docs/nix-re.md section 2)."""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "pylib"))
import gamedir

FAMILY = {0x17DA960: "504_begaruta", 0x17DD440: "612_nix", 0x17DE0A8: "Begaruta", 0x17DEC40: "BigBegaruta"}
UPDATE, AI_THINK = 0x644350, 0x63C1C0

source = (ROOT / "src/crew.cpp").read_text(encoding="utf-8")
table = source.split("const VehicleClass kClasses[]={", 1)[1].split("};", 1)[0]
for vt, name in FAMILY.items():
    m = re.search(rf'\{{0x{vt:X},0x([0-9A-Fa-f]+),"{name}",kFindSeat,(\d+)\}}', table)
    assert m, f"{name} must chain an explicit per-frame slot"
    assert int(m[1], 16) == UPDATE and int(m[2]) == 4, f"{name} chains slot 4 0x644350, not its AI think"
assert f"0x{AI_THINK:X}" not in table, "no class chains the family's AI think (slot 55): it is not the frame"

path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev()) / "EDF.dll"
if not path.exists():
    print("begaruta_family_native_profile_test: source registration checked; native EDF.dll unavailable (skipped)")
    raise SystemExit(0)
import pefile   # only with the game's EDF.dll at hand (CI installs numpy alone)

pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, "unsupported EDF.dll profile"
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
slot = lambda vt, i: struct.unpack_from("<Q", img, vt + i * 8)[0] - base
for vt, name in FAMILY.items():
    assert slot(vt, 4) == UPDATE, name
    assert slot(vt, 6) == 0x642970, name   # registers slot 55 on the AI component (0x642CC5), not as the frame
assert all(slot(vt, 55) == AI_THINK for vt in (0x17DA960, 0x17DD440, 0x17DE0A8))
# Native ABI: save step in rsi, object in rdi, then forward (vehicle,step) to input/aim and physics/fire.
assert img[0x64435F:0x644365] == bytes.fromhex("48 8b f2 48 8b f9")
for call, target in [(0x6443A2, 0x645790), (0x6443F6, 0x645190), (0x645890, 0x641800)]:
    assert img[call] == 0xE8 and call + 5 + struct.unpack_from("<i", img, call + 1)[0] == target
print("begaruta_family_native_profile_test: family registration, native slot 4/6/55 and input/fire calls passed")
