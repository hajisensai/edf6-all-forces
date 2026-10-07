"""Optional Windows x64 native medic healing audit (Python + pefile).

Usage: python tests/medic_native_heal_audit.py X:/path/to/EDF.dll
Missing DLL/platform returns skip code 77. Never downloads a DLL, runs DllMain,
attaches to a game process, or modifies the game installation.

Executes the full native GameObjectBase damage handler and soldier online-healing
overrides. The RideAi holder loop and GDI assignment are exact copied native code
blocks with register-saving adapters, not reimplemented arithmetic. World-service
data, armor acceptance and notification virtuals are fixtures. The vehicle owner
uses native Vehicle410 RTTI and the real CRT dynamic cast.

18 healing/authority checks establish the cleared friendly-hit flag's effect on
HP. Two additional ABI checks exercise the copied shot-entry prologue, and four
checks execute the native shot prefix up to a private snapshot exit to verify
local/remote parameter selection. Full weapon/bullet construction, Havok queries,
projectile flight, network transport and actual gameplay are NOT executed.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path)
args = parser.parse_args()
if not __debug__:
    raise RuntimeError('Run without -O: native precondition checks must remain enabled')
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8:
    print('SKIP: Windows x64 Python is required')
    raise SystemExit(77)
if args.edf_dll is None or not args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll path')
    raise SystemExit(77)
import pefile

path = str(args.edf_dll.resolve())
pe = pefile.PE(path, fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664, 'EDF.dll must be x64'
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported EDF.dll profile'
assert pe.OPTIONAL_HEADER.ImageBase == 0x180000000, 'unsupported image base'
# Check whole copied blocks and native functions before executing any game code.
for start, end, digest in (
    (0x6330A2, 0x6330D9, 'd6ba9d7a90f7fcb22b8c4f19561119d4477305fa7c75e57cc5a29aabe5dc006d'),
    (0x23206C, 0x2320D5, '9a4a6fa618628c61ec7346b88bfa18707ccefe0502d77fd8d8dc148e0b343fe8'),
    (0x547C30, 0x548660, '04635d8bfb8f2841d1defb9e6314b2fa3f8c078d25f20451610e5a7d35250b0a'),
    (0x5A3600, 0x5A3623, '482a8e173526d004345590a8db856338a8b22b56a957a48a5191eb61139579d3'),
    (0x5A3630, 0x5A378B, '62f64a87f142b7532e3640d925aeb01f7a684a1176fe5f5b46b837ac9ac7ea07'),
    (0x2307F0, 0x230B20, '7d9e1949dc4edecc7d7b678223f9a934f1c3903391f4eb751e276a99828c52c0'),
    (0x696FD0, 0x69725D, '786050b23c9d1455d983c617bf25af736ae04c41a436d10308e83fd7d7c23c86'),
):
    assert hashlib.sha256(pe.get_data(start, end-start)).hexdigest() == digest, hex(start)
shot_prologue = bytes.fromhex('488bc4555356574154415541564157')
assert pe.get_data(0x696FD0, len(shot_prologue)) == shot_prologue
assert pe.get_data(0x7748F0, 7) == bytes.fromhex('488b0599df9301')
for table in (0x17CDF28, 0x17D0FF8, 0x17CF5B8, 0x17CF100):
    assert struct.unpack('<Q', pe.get_data(table+0x110, 8))[0] == 0x1805A3600
    assert struct.unpack('<Q', pe.get_data(table+0x118, 8))[0] == 0x1805A3630
# Both local fire and network bullet reconstruction call the same shot entry.
for call in (0x690D54, 0x692A24):
    assert pe.get_data(call, 1) == b'\xe8'
    assert call+5+struct.unpack('<i', pe.get_data(call+1, 4))[0] == 0x696FD0
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
k.VirtualAlloc.argtypes = [C.c_void_p,C.c_size_t,C.c_uint,C.c_uint]
k.VirtualAlloc.restype = C.c_void_p
base = k.LoadLibraryExW(path, None, 1)  # DONT_RESOLVE_DLL_REFERENCES
assert base
keep = []
def buf(n):
    x=C.create_string_buffer(n); keep.append(x); return C.addressof(x)
def u32(p,v): C.c_uint32.from_address(p).value=v
def ptr(p,v): C.c_void_p.from_address(p).value=v
def flt(p,v): C.c_float.from_address(p).value=v
def global_ptr(rva,p):
    old=C.c_uint(); assert k.VirtualProtect(base+rva,8,4,C.byref(old))
    ptr(base+rva,p)
    rest=C.c_uint(); assert k.VirtualProtect(base+rva,8,old.value,C.byref(rest))

# Fake world services are data only: native team lookup, offline-session query and difficulty lookup execute unchanged.
team=buf(0x100); teams=buf(0x80); relation=buf(0x20)
ptr(team+0x38,teams); ptr(teams+0x18,relation); u32(relation,1)
global_ptr(0x20B2978,team)
session=buf(0x16000); u32(session+0x38,0xffffffff)
# Positive-damage attribution casts a null attacker through the real CRT.
crt=C.CDLL('VCRUNTIME140.dll'); global_ptr(0x1756098,C.cast(crt.__RTDynamicCast,C.c_void_p).value)
# 0x7748F0 loads this singleton.
global_ptr(0x7748F7 + struct.unpack('<i', pe.get_data(0x7748F3,4))[0], session)
yes=C.WINFUNCTYPE(C.c_bool,C.c_void_p,C.c_void_p,C.c_void_p)(lambda *_: True)
noop=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_void_p)(lambda *_:None)
vt=buf(0x200)
for off in range(0,0x200,8): ptr(vt+off,C.cast(noop,C.c_void_p).value)
for off in (0x80,0x88): ptr(vt+off,C.cast(yes,C.c_void_p).value)
target=buf(0x2400); ptr(target,vt); flt(target+0x2f0,0); flt(target+0x2f4,1000); flt(target+0x394,1)
# Empty native unordered-map used by the armor multiplier query, team zero skips player difficulty parameters.
buckets=buf(16); ptr(target+0x3c0,buckets)
gdi=buf(0x120)
damage=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)(base+0x547c30)
assert C.string_at(base+0x548109,6)==bytes.fromhex('f3410f107550')
for bits, amount, expected in ((0,-150,400),(0x20,-150,550),(0x20,-900,1000),(0x20,150,385)):
    flt(target+0x2f8,400); flt(gdi+0x50,amount); C.c_uint16.from_address(gdi+0x60).value=bits
    damage(target,gdi)
    got=C.c_float.from_address(target+0x2f8).value
    assert got==expected,(bits,amount,got,expected)
print('PASS full native damage handler: friendly healing filter, healing, cap, positive damage control')

def adapter(start,end,prefix,suffix):
    code=bytes.fromhex(prefix)+pe.get_data(start,end-start)+bytes.fromhex(suffix)
    p=k.VirtualAlloc(None,len(code),0x3000,0x40); assert p
    C.memmove(p,code,len(code)); return C.WINFUNCTYPE(None,C.c_void_p)(p)
# Preserve RSI / RBX around exact native blocks. All branches stay inside their
# copied block; neither block contains calls or RIP-relative operands.
ride_ai=adapter(0x6330a2,0x6330d9,'564889ce','5ec3')
gdi_init=adapter(0x23206c,0x2320d5,'534889cb','5bc3')
copy=C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p)(base+0x2307f0)
vehicle=buf(0x700); holder=buf(0x48); weapon=buf(0xc00); core=buf(0xe00)
ptr(vehicle+0x638,holder); ptr(vehicle+0x648,1); ptr(holder+0x10,weapon)
C.c_uint16.from_address(weapon+0x830+0xc8).value=0xffff
C.c_uint16.from_address(core+0x9a0+0xc8).value=0xffff
flt(weapon+0x830,1); flt(weapon+0x89c,-150); flt(weapon+0x8b0,8)
for repaired,expected in ((False,400),(True,550)):
    C.c_uint8.from_address(weapon+0x8b6).value=1 # native SetWeaponObject's value
    ride_ai(vehicle)
    assert C.c_uint8.from_address(weapon+0x8b6).value==0
    if repaired: C.c_uint8.from_address(weapon+0x8b6).value=1
    copy(core+0x9a0,weapon+0x830)
    C.c_uint16.from_address(core+0x790).value=0
    gdi_init(core)
    assert C.c_float.from_address(core+0x780).value==-150
    assert C.c_uint16.from_address(core+0x790).value==(0x20 if repaired else 0)
    flt(target+0x2f8,400)
    damage(target,core+0x730)
    got=C.c_float.from_address(target+0x2f8).value
    assert got==expected,(repaired,got)
    print('PASS RideAi weapon->native param copy->native GDI->full damage, repaired=',repaired,'HP=',got)

# Actual player override decision (shared by all four soldier classes). A player's HP is
# accepted on the target player's owning machine, independently of the vehicle bullet owner's side.
for off,rva in ((0x108,0x550890),(0x110,0x5a3600),(0x118,0x5a3630)):
    ptr(vt+off,base+rva)
ptr(target+0x1ed0,1)
authority=C.WINFUNCTYPE(C.c_bool,C.c_void_p,C.c_bool,C.c_bool)(base+0x5a3600)
for remote_target in (False,True):
    u32(target+0x128,int(remote_target))
    for remote_attacker in (False,True):
        assert authority(target,remote_attacker,False)==(not remote_target)
        assert authority(target,remote_attacker,True)
print('PASS native player healing authority: target-local owns HP, target-remote skips (8 checks)')

# Full online damage entry using native soldier healing overrides and a real Vehicle410 RTTI
# owner; its notification virtuals alone are observation stubs, not the damage/authority code.
session_entries=buf(8); session_entry=buf(0x20); session_data=buf(0x80)
ptr(session+0x20,session_entries); ptr(session_entries,session_entry); ptr(session_entry+0x10,session_data)
u32(session+0x38,0); u32(session_data+0x68,1)
owner=buf(0x2400); owner_ctrl=buf(0x20); owner_vt=buf(0x300)
C.memmove(owner_vt,base+0x17df338-8,0x280)
ptr(owner,owner_vt+8); ptr(owner+0x120,base+0x17df530)
for off in (0x48,0x50,0x58): ptr(owner_vt+8+off,C.cast(noop,C.c_void_p).value)
u32(owner_ctrl+8,1); u32(owner_ctrl+0xc,1)
ptr(gdi+0x10,owner); ptr(gdi+0x18,owner_ctrl)
flt(gdi+0x50,-150); C.c_uint16.from_address(gdi+0x60).value=0x20
for remote_target in (False,True):
    u32(target+0x128,int(remote_target))
    for remote_attacker in (False,True):
        u32(owner+0x128,int(remote_attacker)); flt(target+0x2f8,400)
        damage(target,gdi)
        hp=C.c_float.from_address(target+0x2f8).value
        assert hp==(400 if remote_target else 550),(remote_target,remote_attacker,hp)
        print('PASS native online targetRemote=',remote_target,'vehicleRemote=',remote_attacker,'HP=',hp)

# Entry-hook ABI check only: execute the exact 15-byte shot prologue (eight
# pushes), restore the registers, then read the fifth argument from its original
# stack slot. This is not a full shot invocation or a production hook test.
code = shot_prologue + bytes.fromhex('415f415e415d415c5f5e5b5d0fb6442428c3')
p = k.VirtualAlloc(None,len(code),0x3000,0x40)
assert p
C.memmove(p,code,len(code))
shot_entry = C.WINFUNCTYPE(C.c_uint8,C.c_void_p,C.c_uint32,C.c_void_p,C.c_void_p,C.c_bool)(p)
assert shot_entry(weapon,0,None,gdi,False) == 0
assert shot_entry(weapon,1,core,gdi,True) == 1

# Execute the actual shot-entry parameter selection and override copy. Stop at
# 0x69725D before world/trajectory access: a private-image instrumentation jump
# records R12 (selected InitParam base), then uses the original cookie-check and
# epilogue. No production DLL bytes are modified, and no shot is spawned.
snapshot = buf(8)
snapshot_code = (b'\x48\xb8'+struct.pack('<Q', snapshot)+b'\x4c\x89\x20'
                 +b'\x48\xb8'+struct.pack('<Q', base+0x6982CC)+b'\xff\xe0')
snapshot_stub = k.VirtualAlloc(None,len(snapshot_code),0x3000,0x40)
assert snapshot_stub
C.memmove(snapshot_stub,snapshot_code,len(snapshot_code))
snapshot_jump = b'\x48\xb8'+struct.pack('<Q', snapshot_stub)+b'\xff\xe0'
assert pe.get_data(0x69725D,16) == bytes.fromhex('498b87880e00000f1080e0000000660f')
assert pe.get_data(0x6982CC,0x33) == bytes.fromhex(
    '488b8de00100004833cce89504c4000f28b424000300000f28bc24f0020000'
    '4881c418030000415f415e415d415c5f5e5b5dc3')
old=C.c_uint()
assert k.VirtualProtect(base+0x69725D,len(snapshot_jump),0x40,C.byref(old))
C.memmove(base+0x69725D,snapshot_jump,len(snapshot_jump))
rest=C.c_uint()
assert k.VirtualProtect(base+0x69725D,len(snapshot_jump),old.value,C.byref(rest))
shot_weapon=buf(0x1600); muzzle=buf(0xf0); empty_list=buf(16); override=buf(0x30)
ptr(shot_weapon+0x1e0,1); ptr(shot_weapon+0x1d0,muzzle)
ptr(shot_weapon+0xc60,empty_list); ptr(empty_list,empty_list)
for off in (0x830+0xc8,0xa10+0xc8,0x9b0+0x10,0xb90+0x10):
    C.c_uint16.from_address(shot_weapon+off).value=0xffff
for off in (0,4,8): flt(override+off,1)
shot_prefix=C.WINFUNCTYPE(None,C.c_void_p,C.c_uint32,C.c_void_p,C.c_void_p,C.c_bool)(base+0x696FD0)
for remote_override in (False,True):
    for permit in (0,1):
        C.c_uint8.from_address(shot_weapon+0x8b6).value=permit
        shot_prefix(shot_weapon,0,override if remote_override else None,gdi,remote_override)
        selected=C.c_void_p.from_address(snapshot).value
        assert selected == shot_weapon+(0x9e0 if remote_override else 0x800)
        assert C.c_uint8.from_address(selected+0xb6).value==permit
print('PASS actual native shot prefix: local template and remote override both preserve the permit (4 checks)')
print('18 native healing checks, 2 shot-entry ABI checks and 4 native shot-prefix checks passed')
print('LIMIT: no full weapon construction, Havok collision query, live transport or gameplay was executed.')
