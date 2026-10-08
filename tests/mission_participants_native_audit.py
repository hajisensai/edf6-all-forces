"""Optional native all-team traversal and safe player-admission consumer audit.

Windows x64 Python + pefile, explicit supported EDF.dll path. Missing input/platform
returns 77. Maps DONT_RESOLVE_DLL_REFERENCES, never DllMain or a live game. Executes
native 5E0C80 on seven private team trees (only its mutex lock/unlock are stubs), and
the exact 22B62B..22B670 weak-output consumer with an ABI adapter. This does not test
EOS, a real scene transition, participant quorum, or the entire player constructor.
"""
import argparse
import ctypes as C
from pathlib import Path
import sys
p=argparse.ArgumentParser(description=__doc__);p.add_argument('edf_dll',nargs='?',type=Path);a=p.parse_args()
if sys.platform!='win32' or C.sizeof(C.c_void_p)!=8 or not a.edf_dll or not a.edf_dll.is_file():
    print('SKIP: explicit supported EDF.dll on Windows x64 required');raise SystemExit(77)
if not __debug__:raise RuntimeError('Run without -O')
import pefile
pe=pefile.PE(str(a.edf_dll),fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46 and pe.FILE_HEADER.Machine==0x8664
assert pe.get_data(0x5E0C80,16)==bytes.fromhex('48895c241048896c2418565741564883')
assert pe.get_data(0x5E0D24,17)==bytes.fromhex('4883c6384881fe880100000f857bffffff')
assert pe.get_data(0x22B626,5)==bytes.fromhex('e865f5ffff')
k=C.WinDLL('kernel32')
k.LoadLibraryExW.argtypes=[C.c_wchar_p,C.c_void_p,C.c_uint];k.LoadLibraryExW.restype=C.c_void_p
k.VirtualProtect.argtypes=[C.c_void_p,C.c_size_t,C.c_uint,C.POINTER(C.c_uint)]
k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_uint,C.c_uint];k.VirtualAlloc.restype=C.c_void_p
k.FlushInstructionCache.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t];k.GetCurrentProcess.restype=C.c_void_p
base=k.LoadLibraryExW(str(a.edf_dll.resolve()),None,1);assert base
keep=[]
def patch(rva,fn):
    cb=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)(fn);keep.append(cb)
    code=b'\x48\xB8'+C.cast(cb,C.c_void_p).value.to_bytes(8,'little')+b'\xFF\xE0'
    old=C.c_uint();assert k.VirtualProtect(base+rva,len(code),0x40,C.byref(old));C.memmove(base+rva,code,len(code))
    assert k.FlushInstructionCache(k.GetCurrentProcess(),base+rva,len(code))
patch(0x50BC0,lambda out,mutex:None);patch(0x50BE0,lambda lock,unused:None)
def ptr(at,value):C.c_void_p.from_address(at).value=value
mgr=C.create_string_buffer(0x50);teams=C.create_string_buffer(7*0x38)
heads=[C.create_string_buffer(0x30) for _ in range(7)]
nodes=[C.create_string_buffer(0x30) for _ in range(7)]
actors=[C.create_string_buffer(0x1EE0) for _ in range(7)]
ptr(C.addressof(mgr)+0x38,C.addressof(teams))
for i,(head,node,actor) in enumerate(zip(heads,nodes,actors)):
    h=C.addressof(head);n=C.addressof(node)
    ptr(C.addressof(teams)+i*0x38,h)
    ptr(h,n);ptr(h+8,n);ptr(h+16,n);C.c_ubyte.from_address(h+0x19).value=1
    ptr(n,h);ptr(n+8,h);ptr(n+16,h);ptr(n+0x20,C.addressof(actor))
    C.c_ubyte.from_buffer(actor,0x2E8).value=1 if i%2 else 0
seen=[]
visit=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)(lambda context,obj:seen.append(obj));keep.append(visit)
vt=C.create_string_buffer(16);functor=C.create_string_buffer(8)
ptr(C.addressof(vt)+8,C.cast(visit,C.c_void_p).value);ptr(C.addressof(functor),C.addressof(vt))
C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)(base+0x5E0C80)(C.addressof(mgr),C.addressof(functor))
assert seen==[C.addressof(x) for x in actors]
print('PASS native all-team enumeration: all seven teams, including dead actors, visited exactly once')

# Native upstream caller receives a weak output address. Preserve its complete weak-lock block,
# all internal branches and exact native cmpxchg; supply only the surrounding Win64 stack adapter.
block=pe.get_data(0x22B62B,0x22B671-0x22B62B)
assert block[:16]==bytes.fromhex('4c8bc00f57c0660f7f442460488b5008')
adapter=bytes.fromhex('4883ec784889c8')+block+bytes.fromhex('488b4424604883c478c3')
code=k.VirtualAlloc(None,len(adapter),0x3000,0x40);assert code;C.memmove(code,adapter,len(adapter))
assert k.FlushInstructionCache(k.GetCurrentProcess(),code,len(adapter))
consume=C.WINFUNCTYPE(C.c_void_p,C.c_void_p)(code)
weak=C.create_string_buffer(16);ctrl=C.create_string_buffer(16)
assert consume(C.addressof(weak)) is None
print('PASS native upper caller accepts empty weak output without dereferencing a null Soldier')
ptr(C.addressof(weak),C.addressof(actors[0]));ptr(C.addressof(weak)+8,C.addressof(ctrl));C.c_long.from_buffer(ctrl,8).value=1
assert consume(C.addressof(weak))==C.addressof(actors[0]) and C.c_long.from_buffer(ctrl,8).value==2
print('PASS native upper caller preserves successful weak lock and increments its strong count once')

# The actual AS-bound Network_SetLocation(void* context,int location) stores its EDX value
# under the network manager's spin lock. An uncontended private manager needs no imports.
assert pe.get_data(0x70F51A,7)==bytes.fromhex('488b35a7359a01')
assert pe.get_data(0x70F566,6)==bytes.fromhex('89ae40160000')
network=C.create_string_buffer(0x1650)
old=C.c_uint();assert k.VirtualProtect(base+0x20B2AC8,8,4,C.byref(old))
ptr(base+0x20B2AC8,C.addressof(network))
set_location=C.WINFUNCTYPE(None,C.c_void_p,C.c_int)(base+0x70F500)
for phase in range(6):
    set_location(None,phase)
    assert C.c_uint32.from_buffer(network,0x1640).value==phase
    assert C.c_long.from_buffer(network,0x1644).value==0
print('PASS native Network_SetLocation: all six states stored exactly and lock released')
root=a.edf_dll.parent/'Root.cpk'
if root.is_file():
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
    from rootcpk import Game
    game=Game(str(root.parent))
    script=game.read('MAINSCRIPT','MAINSCRIPT.AS').decode('utf-8',errors='replace')
    types=game.read('MAINSCRIPT','COMMONTYPES.H').decode('utf-8',errors='replace')
    common=script.split('int PlayMission_Common()',1)[1].split('string PlayMission_Offline()',1)[0]
    assert common.index('Network_SetLocation(GAME_LOADING)')<common.index('result = Mission()')
    assert common.index('result = Mission()')<common.index('Network_Session_End()')<common.index('Network_SetLocation(MENU_ROOM)')
    names=('BOOTING = 0','BOOT_COMPLETE','MENU_LOBBY','MENU_ROOM','GAME_PLAYING','GAME_LOADING')
    enum=types.split('enum NetworkLocation',1)[1].split('};',1)[0]
    assert sorted(enum.index(name) for name in names)==[enum.index(name) for name in names]
    print('PASS actual Root MainScript: loading before Mission, room only after session end; native enum 0..5')
else:
    raise RuntimeError('Root.cpk required to verify mission-end and location enum semantics')
print('10 native/script checks passed; no live scene, EOS or constructor execution')
