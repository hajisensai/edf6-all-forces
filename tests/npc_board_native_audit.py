"""Optional native NPC boarding/exit serialization audit.

python tests/npc_board_native_audit.py /path/to/EDF.dll
Private DONT_RESOLVE mapping only; no game/install writes or DllMain.
Original native Human senders and stream serialization execute. Registration,
vehicle reference lookup, logging and final network send are fixture services.
This does not exercise peer receipt, world objects or live multiplayer.
"""
import sys
from pathlib import Path
import hashlib
import ctypes as C,struct
if not __debug__: raise RuntimeError('Run without -O')
if sys.platform!='win32' or C.sizeof(C.c_void_p)!=8 or len(sys.argv)<2 or not Path(sys.argv[1]).is_file():
 print('SKIP: Windows x64 and supported EDF.dll path required');raise SystemExit(77)
import pefile
p=pefile.PE(sys.argv[1],fast_load=True)
assert p.FILE_HEADER.TimeDateStamp==0x678CCB46 and p.OPTIONAL_HEADER.SizeOfImage==0x22CE000
assert hashlib.sha256(p.get_data(0x5763e0,452)).hexdigest()=='65696120506e70af4ecbc70ed7d74729ea2f0068c635e1fbb562226bb9d5dbd4'
assert hashlib.sha256(p.get_data(0x576310,203)).hexdigest()=='3ff7480ba64d5979166b9162ccd1f291cedfe689cde4e86374a31acf7f609206'
k=C.WinDLL('kernel32');k.LoadLibraryExW.argtypes=[C.c_wchar_p,C.c_void_p,C.c_uint];k.LoadLibraryExW.restype=C.c_void_p;k.VirtualProtect.argtypes=[C.c_void_p,C.c_size_t,C.c_ulong,C.POINTER(C.c_ulong)]
b=k.LoadLibraryExW(str(Path(sys.argv[1]).resolve()),None,1);assert b
keepers=[]
def patch(a,data):
 old=C.c_ulong();assert k.VirtualProtect(b+a,len(data),0x40,C.byref(old));C.memmove(b+a,data,len(data))
def callback(a,ctype,fn):
 f=ctype(fn);keepers.append(f);patch(a,b'\x48\xb8'+struct.pack('<Q',C.cast(f,C.c_void_p).value)+b'\xff\xe0')
patch(0x7748F0,b'\xb0\x01\xc3');patch(0x50F00,b'\xc3')
def reference(out,weak):
 C.c_int32.from_address(out).value=42
 ctrl=C.c_uint64.from_address(weak+8).value
 if ctrl:C.c_int32.from_address(ctrl+12).value-=1
 return out
callback(0x785050,C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p),reference)
sent=[]
def send(net,writer,mode):
 n=C.c_size_t.from_address(writer+0x5f0).value
 sent.append(C.string_at(writer+0x10,n));return True
f=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_void_p,C.c_int)(send);keepers.append(f)
h=C.create_string_buffer(0x1900);v=C.create_string_buffer(0x700);seat=C.create_string_buffer(0x680);ctrl=C.create_string_buffer(16);vt=C.create_string_buffer(0x100)
def put(buf,off,value):C.c_uint64.from_buffer(buf,off).value=value
put(vt,0x70,C.cast(f,C.c_void_p).value);put(h,0x120,C.addressof(vt));put(h,0x1540,C.addressof(seat)+0x340);put(h,0x1548,C.addressof(v));put(h,0x1550,C.addressof(ctrl));put(v,0x608,C.addressof(seat));put(v,0x28,C.addressof(v));put(v,0x30,C.addressof(ctrl));C.c_int32.from_buffer(ctrl,8).value=2;C.c_int32.from_buffer(ctrl,12).value=3
board=C.CFUNCTYPE(None,C.c_void_p)(b+0x5763E0);leave=C.CFUNCTYPE(None,C.c_void_p)(b+0x576310)
board(C.addressof(h)); print('native NPC board',len(sent),sent[-1].hex(),flush=True);assert sent[-1]==bytes.fromhex('06202a1f01')
h[0x128]=b'\x01';board(C.addressof(h));assert len(sent)==1;print('native remote board suppressed',flush=True)
h[0x128]=b'\x00';leave(C.addressof(h));print('native NPC exit',len(sent),sent[-1].hex(),flush=True);assert sent[-1]==bytes.fromhex('0702')
assert C.c_int32.from_buffer(h,0x1824).value==2
print('PASS real native boarding and exit serializers accept NPC without pad/player flags; sequence1/2')

assert C.c_int32.from_buffer(ctrl,8).value==2 and C.c_int32.from_buffer(ctrl,12).value==3
print('PASS temporary reference accounting preserved')
