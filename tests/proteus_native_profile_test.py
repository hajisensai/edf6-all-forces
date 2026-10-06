"""Read-only verification of the Proteus tick against the installed EDF.dll (no process or game writes)."""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "pylib"))
import gamedir
import pefile

source = (ROOT / "src/crew.cpp").read_text(encoding="utf-8")
m = re.search(r'\{0x17DEC40,0x([0-9A-Fa-f]+),"BigBegaruta",kFindSeat,(\d+)\}', source)
assert m, "Proteus must use an explicit per-frame slot, not the default AI task slot"
expected, slot = int(m[1], 16), int(m[2])
assert slot == 4 and expected == 0x644350
path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev()) / "EDF.dll"
if not path.exists():
    print("proteus_native_profile_test: source tick checked; native EDF.dll unavailable (skipped)")
    raise SystemExit(0)
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, "unsupported EDF.dll profile"
img = pe.get_memory_mapped_image()
actual = struct.unpack_from("<Q", img, 0x17DEC40 + slot * 8)[0] - pe.OPTIONAL_HEADER.ImageBase
assert actual == expected
# Native ABI: save step in rsi, object in rdi, then forward (vehicle,step) to input/aim and physics/fire.
assert img[0x64435F:0x644365] == bytes.fromhex("48 8b f2 48 8b f9")
for call, target in [(0x6443A2, 0x645790), (0x6443F6, 0x645190)]:
    assert img[call] == 0xE8 and call + 5 + struct.unpack_from("<i", img, call + 1)[0] == target
print("proteus_native_profile_test: source registration, native slot 4, two-argument ABI and input/physics calls passed")
