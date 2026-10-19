"""Read-only constructor/RTTI/render audit; never loads or executes EDF.dll.

python -B tests/thermal_model_native_audit.py /path/to/EDF.dll
Missing input returns skip code 77. Requires pefile and capstone.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

if not __debug__:
    raise RuntimeError("Run without -O")
parser=argparse.ArgumentParser()
parser.add_argument('path',nargs='?',default='')
parser.add_argument('--dll',default='')
args=parser.parse_args()
dll=args.dll or args.path
if not dll or not Path(dll).is_file():
    print("SKIP: pass the supported EDF.dll path")
    raise SystemExit(77)

import capstone
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP, X86_REG_RCX
import pefile

pe = pefile.PE(dll)
assert pe.FILE_HEADER.Machine == 0x8664
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
image_base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/thermal_models.cpp").read_text(encoding="utf-8")
u32 = lambda at: struct.unpack_from("<I", img, at)[0]
u64 = lambda at: struct.unpack_from("<Q", img, at)[0]


def type_name(rva):
    return img[rva + 16:img.index(b"\0", rva + 16)].decode("ascii")


def rtti_locator(col):
    assert u32(col) == 1 and u32(col + 20) == col
    hierarchy = u32(col + 16)
    bases = []
    for i in range(u32(hierarchy + 8)):
        desc = u32(u32(hierarchy + 12) + i * 4)
        bases.append((type_name(u32(desc)), struct.unpack_from("<iii", img, desc + 8)))
    return type_name(u32(col + 12)), u32(col + 4), bases


def rtti(vtable):
    return rtti_locator(u64(vtable - 8) - image_base)


def instruction(at):
    return next(md.disasm(img[at:at + 16], at, count=1))


def call_target(at):
    x = instruction(at)
    assert x.mnemonic == "call" and x.size == 5 and img[at] == 0xE8, hex(at)
    return at + 5 + struct.unpack_from("<i", img, at + 1)[0]


def member_lea(at, offset):
    x = instruction(at)
    assert x.mnemonic == "lea" and x.operands[0].type == X86_OP_REG, hex(at)
    mem = x.operands[1]
    assert mem.type == X86_OP_MEM and mem.mem.base != X86_REG_RIP and mem.mem.disp == offset, (hex(at), x.op_str)
    return x


# Each entry independently pins the native LEA of this member and call of the
# AnimationModel constructor. A production offset/name drift fails this audit.
constructors = {
    "InsectBase": (0x4A68B9, 0x4A68C0, 0x630),
    "HumanBase": (0x56A6E6, 0x56A6ED, 0x860),
    "VehicleBase": (0x629583, 0x62958A, 0xE40),
    "BasicAnimationCharacter": (0x314CA4, 0x314CAE, 0x8A0),
    "MonsterBase": (0x4B540E, 0x4B5415, 0x8C0),
    "Deiroi": (0x318DE4, 0x318DEE, 0x600),
    "Nephila": (0x4DBD5E, 0x4DBD65, 0x620),
    "UfoSmall507": (0x528777, 0x528781, 0xC00),
    "UfoSmall515": (0x5339F8, 0x533A02, 0x620),
    "UfoCarrier": (0x4F5B5B, 0x4F5B65, 0x650),
    "MovingFortress": (0x4C0C24, 0x4C0C2B, 0x5E0),
    "MovingFortress_Battery": (0x4CEF2B, 0x4CEF32, 0x610),
    "MovingFortress_Debris": (0x4D59DF, 0x4D59E6, 0x5C0),
    "UfoMother511PartsBase": (0x51D95D, 0x51D964, 0x5E0),
    "Humanoid_BigGreyBoss_PartsBase": (0x44C508, 0x44C50F, 0x5C0),
    "Humanoid_BigGreyBoss_Meteor": (0x449F72, 0x449F79, 0x5C0),
    "AlienTrailer": (0x309D1E, 0x309D25, 0x5C0),
    "GiantAntEgg": (0x410DA6, 0x410DAD, 0x5C0),
    "AntHill": (0x4E8EBB, 0x4E8EC2, 0x5C0),
    "BigNest": (0x4ED6DE, 0x4ED6E5, 0x5C0),
    "BombingPlane": (0x5AA6BD, 0x5AA6C4, 0x5C0),
    "ShootingTarget": (0x5CADEF, 0x5CADF6, 0x5C0),
    "EventAnimationObject": (0x5B6007, 0x5B600E, 0x5C0),
    "FarEventObject": (0x5C3A5B, 0x5C3A62, 0x5C0),
    "Transporter_Container": (0x5E7948, 0x5E794F, 0x5C0),
    "GeneratorPoll": (0x3FF928, 0x3FF92F, 0x7C0),
}
rules = {n: (int(off, 16), array == "true") for n, off, array in re.findall(
    r'\{"\.\?AV([A-Za-z0-9_]+)@@",(0x[0-9A-F]+),(true|false)\}', source)}
assert set(rules) == set(constructors) | {"HumanoidBase"}
for name, (lea, call, offset) in constructors.items():
    assert rules[name] == (offset, False), name
    x = member_lea(lea, offset)
    if x.operands[0].reg != X86_REG_RCX:
        move = instruction(lea + x.size)
        assert move.mnemonic == "mov" and move.operands[0].reg == X86_REG_RCX
        assert move.operands[1].reg == x.operands[0].reg
        assert move.address + move.size == call
    else:
        assert x.address + x.size == call
    assert call_target(call) == 0x6B8740, name

# HumanoidBase owns a *vector* of 0x710-byte entries, each beginning with a
# real AnimationModel. This is the body AND detachable limbs, not only item zero.
assert rules["HumanoidBase"] == (0x6C0, True)
member_lea(0x465927, 0x6C0)
assert call_target(0x46592E) == 0x4931B0
assert call_target(0x493299) == 0x49C6A0
assert call_target(0x49C6EF) == 0x6B8740
assert img[0x49C6BE:0x49C6C2] == bytes.fromhex("488b4118")  # count [manager+0x18]
assert img[0x49C6C7:0x49C6CE] == bytes.fromhex("4c69f010070000")
assert img[0x49C6E0:0x49C6E7] == bytes.fromhex("488b5f084903de")  # [manager+8]+index*0x710
assert img[0x493204:0x49320B] == bytes.fromhex("48899f80000000")  # manager+0x80 = owning humanoid
assert "kHumanoidStride=0x710" in source

# The extra native models on inherited/main-model families.
for lea, call, off in ((0x37BB59, 0x37BB60, 0x1160), (0x44A077, 0x44A07E, 0xF00)):
    member_lea(lea, off)
    assert call_target(call) == 0x6B8740
member_lea(0x309E0D, 0x10D0)
member_lea(0x309E19, 0x10)
assert call_target(0x309E1D) == 0x6B8740  # AlienTrailer +0x10D0+0x10

# Parse the actual native RTTI for representative major families and subclasses.
representatives = {
    "InsectBase": (0x17BB798, 0x17BE120, 0x17BCEE0, 0x17BDAC0, 0x17ABA40),
    "BasicAnimationCharacter": (0x17B5370, 0x17B8898, 0x17B8100, 0x17AE968, 0x17B6270),
    "HumanoidBase": (0x17C29C0, 0x17C4D70, 0x17C0BD0, 0x17C24B0, 0x17BEC68),
    "HumanBase": (0x17CDF28, 0x17D0FF8, 0x17CF5B8, 0x17CF100),
    "VehicleBase": (0x17DADB0, 0x17DC620, 0x17DEC40),
    "MonsterBase": (0x17C5628, 0x17C5A60),
}
for family, vtables in representatives.items():
    for vt in vtables:
        name, off, bases = rtti(vt)
        assert off == 0 and (f".?AV{family}@@", (0, -1, 0)) in bases, (hex(vt), name, family)
assert rtti(0x17C4030)[0] == ".?AVAnimationModel@@"
assert u64(0x17C4030 + 8) - image_base == 0x6BF9F0
assert img[0x6B875D:0x6B8764] == bytes.fromhex("488d05ccb81001")
# EDF4.1's plugin-owned clone preserves the native locator, so rejecting its
# external vtable allocation would lose the entire ported-enemy model family.
clone_source = (ROOT / "src/edf41classes.cpp").read_text(encoding="utf-8")
assert "c.block[0]=const_cast<void*>(vt[-1]);" in clone_source
assert "Readable(vt-8,24)" in source and "InImage(vt," not in source
for native in (0x17D4D20, 0x17C9DC0):  # EventAnimationObject and UfoCarrier508
    cloned_table = bytes(img[native - 8:native + 16])
    col = struct.unpack_from("<Q", cloned_table)[0] - image_base
    assert rtti_locator(col) == rtti(native)

# Render submissions with +0x18 batch skip slot 1; the batch flush does not
# retain the currently submitted object outside its native per-pass queue.
assert img[0x11E1638:0x11E164B] == bytes.fromhex("4c8b72184c8bd2488bd94d85f60f842b010000")
assert img[0x11E1776:0x11E1792] == bytes.fromhex("488b024c8d89e0000000488b5110498bca4883c428415e5b48ff6008")
assert img[0x11E15D0:0x11E15E9] == bytes.fromhex("488b1f4c8d8ee0000000488b5610458bc4488bcb488b03ff10")
assert img[0x11E2550:0x11E2561] == bytes.fromhex("41b801000000488b13498bcde8cff0ffff")
assert call_target(0x11E255C) == 0x11E1630
assert call_target(0x11E3A3F) == 0x11E1630  # translucent pass 2
# Exactly two direct mesh helper calls: the shared normal/LOD path (already
# intercepted by FP) and per-mesh sorted translucent path.
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
calls = [lo + m.start() for m in re.finditer(b"\xE8", img[lo:hi - 5])
         if lo + m.start() + 5 + struct.unpack_from("<i", img, lo + m.start() + 1)[0] == 0x10FF4E0]
assert calls == [0x6C30CB, 0x1103FD1], list(map(hex, calls))
assert img[0x6C30B5:0x6C30D0] == bytes.fromhex("48c7442420000000004533c9458d4102488bd7488bcee810c4a300")
print(f"thermal model native audit: {len(constructors)} embedded-model families, humanoid array, RTTI and batch/mesh paths passed")
