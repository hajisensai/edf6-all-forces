"""Compare the compiled production Proteus gate to EDF.dll's native HP handler.

Usage: python tests/proteus_damage_native_audit.py X:/EDF.dll --gate-dll X:/gate.dll
Build tests/proteus_damage_gate_bridge.cpp as a Windows x64 shared library first.
Missing paths return 77. The reused medic audit enforces platform/profile/code
signatures, rejects -O, and maps only a private DONT_RESOLVE image. No game runs.
This exercises native HP acceptance with fixture world data/notification virtuals;
it does not execute collision, stock message prefilter, or transport.
"""
import argparse
import contextlib
import io
from pathlib import Path
import runpy
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll',nargs='?',type=Path)
parser.add_argument('--gate-dll',type=Path)
args=parser.parse_args()
if not __debug__:
    raise RuntimeError('Run without -O: native preconditions must stay enabled')
if not args.edf_dll or not args.edf_dll.is_file() or not args.gate_dll or not args.gate_dll.is_file():
    print('SKIP: existing EDF.dll and compiled --gate-dll paths are required')
    raise SystemExit(77)
bridge_path=str(args.gate_dll.resolve())
fixture=Path(__file__).with_name('medic_native_heal_audit.py')
sys.argv=[str(fixture),str(args.edf_dll)]
with contextlib.redirect_stdout(io.StringIO()):
    env=runpy.run_path(str(fixture))
globals().update({key:value for key,value in env.items() if not key.startswith('__')})
bridge=C.CDLL(bridge_path)
eligible=bridge.ProteusDamageEligible
eligible.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p]
eligible.restype=C.c_bool
assert pe.get_data(0x6347C0,20)==bytes.fromhex('f6812801000001b901000000410fb6c00f44c1c3')
for off,rva in ((0x80,0x54AA40),(0x88,0x54AA30),(0x110,0x6347C0),(0x118,0x6347E0)):
    assert struct.unpack('<Q',pe.get_data(0x17DEC40+off,8))[0]==0x180000000+rva
    ptr(vt+off,base+rva)
ptr(vt+0x108,base+0x266960)
u32(target+0x128,2);ptr(target+0x1ed0,0);ptr(target+0x340,0)
ptr(gdi+0x10,owner);ptr(gdi+0x18,owner_ctrl)
flt(gdi+0x50,150)
checks=0
cases=(
    ('enemy accepted',2,0,0,0,True,250),
    ('friend rejected',1,0,0,0,False,400),
    ('friend GDI permission',1,0x20,0,0,True,385),
    ('friend target permission',1,0,0x100000,0,True,385),
    ('scene veto',2,0,0,4,False,400),
    ('scene veto survives network override',2,0x40,0,4,False,400),
    ('invulnerability',2,0,1,0,False,400),
    ('network bypasses invulnerability',2,0x40,1,0,True,250),
    ('damage-disabled',2,0,0x800,0,False,400),
    ('network bypasses damage-disabled',2,0x40,0x800,0,True,250),
    ('GDI veto',2,0x200,0,0,False,400),
    ('GDI veto survives network override',2,0x240,0,0,False,400),
)
for name,relation_value,gdi_flags,object_flags,scene,accepted,hp in cases:
    u32(relation,relation_value);u32(target+0x380,object_flags)
    C.c_uint8.from_address(target+0x18).value=scene
    C.c_uint16.from_address(gdi+0x60).value=gdi_flags
    flt(target+0x2f8,400)
    assert eligible(target,gdi,team)==accepted,name
    damage(target,gdi)
    assert C.c_float.from_address(target+0x2f8).value==hp,name
    checks+=1
    print('PASS production/native:',name)

# Self-hit requires locked weak identities, not the raw stored attacker pointer.
u32(relation,2);u32(target+0x380,0);C.c_uint8.from_address(target+0x18).value=0
C.c_uint8.from_address(target+0x1A).value=8
target_ctrl=buf(0x20);u32(target_ctrl+8,1);u32(target_ctrl+0xc,1)
ptr(target+0x28,target);ptr(target+0x30,target_ctrl)
ptr(gdi+0x10,target);ptr(gdi+0x18,target_ctrl)
for flags in (0,0x40):
    C.c_uint16.from_address(gdi+0x60).value=flags
    flt(target+0x2f8,400)
    assert not eligible(target,gdi,team)
    damage(target,gdi)
    assert C.c_float.from_address(target+0x2f8).value==400
    checks+=1
print('PASS production/native: self-hit veto, including network override')
# An expired attacker lock produces null even if its stale raw pointer equals
# the target; the live target lock does not. Avoid passing it through native
# callbacks after this read check; the gate's weak-lock contract is the subject.
expired=buf(0x20);u32(expired+0xc,1);ptr(gdi+0x18,expired)
assert eligible(target,gdi,team)
checks+=1
ptr(target+0x30,0);ptr(gdi+0x18,0)
assert not eligible(target,gdi,team)  # native compares the two null lock results
checks+=1
print('PASS production weak identity: expired attacker and two null locks')

# Accepted full absorption has zero HP delta: admission must use the original
# positive incoming hit, not infer acceptance from HP movement after absorbing.
C.c_uint8.from_address(target+0x1A).value=0
ptr(gdi+0x10,owner);ptr(gdi+0x18,owner_ctrl)
C.c_uint16.from_address(gdi+0x60).value=0
flt(gdi+0x50,150);flt(target+0x2f8,400)
assert eligible(target,gdi,team)
flt(gdi+0x50,0)
damage(target,gdi)
assert C.c_float.from_address(target+0x2f8).value==400
checks+=1
print('PASS full absorption: original hit admitted while native HP delta stays zero')
print(f'{checks} production/native Proteus gate checks passed; no collision or transport executed')
