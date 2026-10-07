"""Optional Windows x64 checks of supported EDF.dll gunner contracts.

Pass an explicit installed DLL path. Missing input/platform returns skip code 77.
Uses a private DONT_RESOLVE_DLL_REFERENCES mapping, never DllMain or the game process.
Only the session query is stubbed; seat-local, fire-start, and aim functions execute native code.
"""
import ctypes as C
from pathlib import Path
import sys

if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass a supported EDF.dll path on Windows x64')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: native safety assertions must remain enabled')
import pefile
path = str(Path(sys.argv[1]).resolve())
pe = pefile.PE(path, fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
assert pe.FILE_HEADER.Machine == 0x8664
assert pe.get_data(0x6525DD,23) == bytes.fromhex('ba01000000488bcee8d6c0fdff488b8d700600004833cc')
assert pe.get_data(0x62E6C0,16) == bytes.fromhex('48895c241848896c242041564883ec30')
assert int.from_bytes(pe.get_data(0x17DF338+51*8,8),'little') == pe.OPTIONAL_HEADER.ImageBase+0x651F90
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
base = k.LoadLibraryExW(path, None, 1)
assert base
# Private process mapping only; DllMain never runs. Stub only the session query.
old = C.c_uint()
assert k.VirtualProtect(base + 0x7748F0, 6, 0x40, C.byref(old))
C.memmove(base + 0x7748F0, bytes.fromhex('b801000000c3'), 6)
vehicle = C.create_string_buffer(0x3000)
seats = C.create_string_buffer(0x680)
rider = C.create_string_buffer(0x200)
ctrl = C.create_string_buffer(0x20)
seat = C.addressof(seats) + 0x340
def ptr(buf, off, value): C.c_void_p.from_address(C.addressof(buf)+off).value = value
ptr(vehicle, 0x608, C.addressof(seats))
local = C.WINFUNCTYPE(C.c_bool,C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_bool)(base+0x630DF0)
count=0
for state,flags,live in [('dummy',0,True),('localNPC',2,True),('remoteNPC',1,True),('empty',0,False)]:
    C.c_void_p.from_address(seat+0x260).value=C.addressof(rider) if live else None
    C.c_void_p.from_address(seat+0x268).value=C.addressof(ctrl) if live else None
    C.c_int.from_buffer(ctrl,8).value=2
    C.c_ubyte.from_buffer(rider,0x128).value=flags
    for mode in (0,1):
        for want in (False,True):
            got=local(C.addressof(vehicle),1,0,mode,want)
            expected=False if mode==1 and not live else ((live and flags&1==0)==want)
            assert got==expected,(state,mode,want,got,expected)
            assert C.c_int.from_buffer(ctrl,8).value==2
            count+=1
    print('PASS native seat-local:',state)

# Isolated native fire-start: only calls the supplied owner and weapon callbacks.
weapon = C.create_string_buffer(0x1800)
owner = C.create_string_buffer(0x200)
iface_vt = C.create_string_buffer(0x100)
weapon_vt = C.create_string_buffer(0x100)
user = C.create_string_buffer(0x20)
calls=[]
current_user=0
@C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p)
def get_user(iface,w): return current_user
@C.WINFUNCTYPE(None,C.c_void_p,C.c_int)
def on_fire(w,idx): calls.append(idx)
ptr(owner,0x120,C.addressof(iface_vt))
ptr(iface_vt,0x58,C.cast(get_user,C.c_void_p).value)
ptr(weapon,0,C.addressof(weapon_vt))
ptr(weapon,0x120,C.addressof(owner))
ptr(weapon_vt,0x80,C.cast(on_fire,C.c_void_p).value)
C.c_int.from_buffer(weapon,0xBE8).value=1
C.c_float.from_buffer(weapon,0xE0C).value=-1.0
fire=C.WINFUNCTYPE(None,C.c_void_p)(base+0x690BB0)
for state,flags,exists,expected in [('null',0,False,0),('dummy',0,True,1),('local',2,True,1),('remote',1,True,0)]:
    current_user=C.addressof(user) if exists else 0
    C.c_ubyte.from_buffer(user,8).value=flags
    calls.clear()
    fire(C.addressof(weapon))
    assert len(calls)==expected,(state,calls)
    print('PASS native fire-start operator:',state)
    count+=1

# Native local-vs-network aim switch: local erases received data, remote preserves it.
aim=C.create_string_buffer(0xD0)
for off,val in [(0x58,1.0),(0x18,2.0),(0xA0,3.0),(0xA4,4.0),(0xB0,5.0),(0xB4,6.0)]:
    C.c_float.from_buffer(aim,off).value=val
C.c_int.from_buffer(aim,0xC4).value=30
network=C.WINFUNCTYPE(None,C.c_void_p)(base+0x5FBA20)
localaim=C.WINFUNCTYPE(None,C.c_void_p)(base+0x5FB880)
network(C.addressof(aim))
assert C.c_float.from_buffer(aim,0xA0).value==3.0
assert C.c_float.from_buffer(aim,0xB0).value==5.0
assert C.c_ubyte.from_buffer(aim,0xC0).value==1
localaim(C.addressof(aim))
assert C.c_float.from_buffer(aim,0xA0).value==1.0
assert C.c_float.from_buffer(aim,0xA4).value==2.0
assert C.c_float.from_buffer(aim,0xB0).value==0.0
assert C.c_int.from_buffer(aim,0xC4).value==0
assert C.c_ubyte.from_buffer(aim,0xC0).value==0
count+=2
# Run the complete native per-seat mode selector: stock 410 mode 1 erases the received
# aim of its empty remote gunner seat; our replacement mode 0 preserves it. A flagged
# vehicle makes seat 0's authority independent of session-manager globals in this fixture.
C.c_ubyte.from_buffer(vehicle,0x1A).value=8
C.c_ubyte.from_buffer(vehicle,0x128).value=2
C.c_uint64.from_buffer(vehicle,0x618).value=2
select=C.WINFUNCTYPE(None,C.c_void_p,C.c_int)(base+0x62E6C0)
for mode in (1,0):
    C.c_void_p.from_address(seat+0x260).value=None
    C.c_void_p.from_address(seat+0x268).value=None
    for off,val in [(0x58,1.0),(0x18,2.0),(0xA0,3.0),(0xA4,4.0),(0xB0,5.0),(0xB4,6.0)]:
        C.c_float.from_address(seat+0xE0+off).value=val
    C.c_int.from_address(seat+0xE0+0xC4).value=30
    select(C.addressof(vehicle),mode)
    expected=1.0 if mode==1 else 3.0
    assert C.c_float.from_address(seat+0xE0+0xA0).value==expected
    assert C.c_ubyte.from_address(seat+0xE0+0xC0).value==(mode==0)
    count+=1
    print(f'PASS native whole aim selector: mode {mode}, empty seat received angle {expected}')
count+=3  # callsite, target and 410 vtable profile checks above
print(f'{count} native checks passed. No DllMain, no game process or installation writes; session query stubbed in private mapping.')
