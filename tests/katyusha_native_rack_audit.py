"""Optional native Katyusha weapon-state audit. Requires Windows x64 Python and pefile.

Usage: python tests/katyusha_native_rack_audit.py "X:/path/to/EDF.dll"
No DLL argument, a missing file, or an unsupported platform returns skip code 77.
An explicitly supplied unsupported DLL fails before any native code executes.

Maps the supported EDF.dll with DONT_RESOLVE_DLL_REFERENCES (no DllMain). Executes
the original network-shot receiver 0x692540 and the original common shot-state tail
0x6981D6..0x6982CB, including ammo decrement and interval selection. Packet readers,
muzzle-transform update, virtual custom-payload reader, and world-dependent bullet
creation are stubs; the spawn stub invokes the real state tail and advances the
shot sequence as 0x697E77 does. Local first-shot comparison supplies fire-start's
known burst-left value 15 and executes the same native state tail.

This verifies weapon-state arithmetic after 160 remote shots and one local shot;
it does not validate real packets, bullet rendering, bone animation, or live co-op.
All code changes affect this process's private mapping, never the installed file
or a game process. The receiver and state-tail bytes are hash checked first.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path)
args = parser.parse_args()
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8:
    print('SKIP: this native fixture requires Windows x64 Python')
    raise SystemExit(77)
if args.edf_dll is None or not args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll path')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: native safety assertions must remain enabled')
import pefile

P = str(args.edf_dll.resolve())
pe=pefile.PE(P,fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46, 'unsupported EDF.dll profile'
assert pe.FILE_HEADER.Machine==0x8664, 'EDF.dll must be x64'
assert hashlib.sha256(pe.get_data(0x692540,0x692A55-0x692540)).hexdigest() == \
    'c931cfdd69f7a1f1a6d04f4067acf095b1f149f74bd5c43aaace34ced74494a7', 'native receiver changed'
assert hashlib.sha256(pe.get_data(0x6981D6,0x6982CC-0x6981D6)).hexdigest() == \
    '284857b07662700844cd57738912091883bc065bb2dee9b54472b619ff3e112d', 'native state tail changed'
k=C.WinDLL('kernel32')
k.LoadLibraryExW.argtypes=[C.c_wchar_p,C.c_void_p,C.c_uint];k.LoadLibraryExW.restype=C.c_void_p
k.VirtualProtect.argtypes=[C.c_void_p,C.c_size_t,C.c_uint,C.POINTER(C.c_uint)]
k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_uint,C.c_uint];k.VirtualAlloc.restype=C.c_void_p
k.FlushInstructionCache.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t]
k.GetCurrentProcess.restype=C.c_void_p
base=k.LoadLibraryExW(P,None,1);assert base
def patch(rva,data):
    old=C.c_uint();assert k.VirtualProtect(base+rva,len(data),0x40,C.byref(old));C.memmove(base+rva,data,len(data))
    assert k.FlushInstructionCache(k.GetCurrentProcess(),base+rva,len(data))
def absolute(fn):return b'\x48\xB8'+int(fn).to_bytes(8,'little')+b'\xFF\xE0'
keep=[]
def stub(rva,signature,fn):
    cb=signature(fn);keep.append(cb);patch(rva,absolute(C.cast(cb,C.c_void_p).value))
U8=C.WINFUNCTYPE(C.c_ubyte,C.c_void_p)
U32=C.WINFUNCTYPE(C.c_uint32,C.c_void_p)
VOID2=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)
weapon=C.create_string_buffer(0x1700);vt=C.create_string_buffer(0x200);owner=C.create_string_buffer(0x400)
wa=C.addressof(weapon)
def wi(off,v):C.c_int.from_buffer(weapon,off).value=v
def ri(off):return C.c_int.from_buffer(weapon,off).value
def rf(off):return C.c_float.from_buffer(weapon,off).value
C.c_void_p.from_buffer(weapon).value=C.addressof(vt)
C.c_void_p.from_buffer(weapon,0x120).value=C.addressof(owner)
C.c_int.from_buffer(owner,0x314).value=-1
C.c_uint64.from_buffer(weapon,0x1E0).value=16
wi(0x1540,2);wi(0x248,160);wi(0xBE8,160);wi(0x370,16);wi(0x36C,240);wi(0x374,4);wi(0x20C,-1)
C.c_ubyte.from_buffer(weapon,0xBE4).value=1
muzzle=0;sequence=0;received=[]
stub(0x12B4BB0,U8,lambda p:muzzle)
stub(0x12B4B90,U32,lambda p:sequence)
stub(0x698500,VOID2,lambda w,m:None) # only updates muzzle transforms, absent in fixture
consume=VOID2(lambda w,p:None);keep.append(consume)
C.c_void_p.from_buffer(vt,0xF8).value=C.cast(consume,C.c_void_p).value
# Retain the original native weapon-state tail in place so all RIP-relative constants
# and branches are exact. Replace only its following function epilogue with this private
# adapter's register restoration. Never writes the installed image on disk.
tail_epilogue=bytes.fromhex('0f103c244883c418415e415fc3')
patch(0x6982CC,tail_epilogue)
adapter=bytes.fromhex('415741564883ec180f113c244989cf4531f60f57ff')+absolute(base+0x6981D6)
code=k.VirtualAlloc(None,len(adapter),0x3000,0x40);assert code;C.memmove(code,adapter,len(adapter))
assert k.FlushInstructionCache(k.GetCurrentProcess(),code,len(adapter))
state_tail=C.WINFUNCTYPE(None,C.c_void_p)(code)
SPAWN=C.WINFUNCTYPE(None,C.c_void_p,C.c_int,C.c_void_p,C.c_void_p,C.c_bool)
def spawn(w,m,data,seed,remote):
    received.append((m,remote))
    state_tail(w)
    # Actual native spawn advances the shot sequence at 697E77 before this state tail.
    C.c_uint32.from_address(seed).value+=1
stub(0x696FD0,SPAWN,spawn) # rendering, BulletControl creation and world services omitted
receive=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)(base+0x692540)
checks=0
for n in range(1,161):
    muzzle=(n-1)%16;sequence=n-1
    receive(wa,None)
    assert received[-1]==(muzzle,True)
    assert ri(0xBE8)==160-n,(n,ri(0xBE8))
    assert ri(0xE18)==0,(n,ri(0xE18))
    assert ri(0xBD0)==n
    assert rf(0xE0C)==(240.0 if n<160 else 0.0),(n,rf(0xE0C))
    assert (ri(0x248)-ri(0xBE8))%16 == n%16
    checks+=1
    if n in (1,2,15,16,17,159,160):
        print('REMOTE',n,'ammo',ri(0xBE8),'burstLeft',ri(0xE18),'wait',rf(0xE0C),'seq',ri(0xBD0),'rackPartial',(160-ri(0xBE8))%16)
# Same exact state tail with local fire-start's burst-left value.
wi(0xBE8,160);wi(0xE18,15);state_tail(wa)
assert ri(0xBE8)==159 and ri(0xE18)==15 and rf(0xE0C)==4.0
assert (ri(0x248)-ri(0xBE8))%16 == 1, 'local and remote first shots use the same rack progress'
checks+=1
print('LOCAL first shot ammo',ri(0xBE8),'burstLeft',ri(0xE18),'wait',rf(0xE0C))
print(checks,'native checks passed; original receive function plus original state tail; packet readers, geometry/spawn services stubbed; no game process/DllMain/install writes.')
