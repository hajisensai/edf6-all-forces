"""Optional private native Human same-seat snapshot repair audit.

Executes 5765E0 -> 633C10 -> 633FE0 and native weak-reference accounting.
Only action-state equality and the RTTI conversion are fixture boundaries.
No live game, installation writes, DllMain, or network execution.
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
assert hashlib.sha256(p.get_data(0x5765e0,891)).hexdigest()=='8c26e81606b4e78842cba9bf4acb49d79884025d9de9e2bd12253249759dd600'
assert hashlib.sha256(p.get_data(0x633c10,418)).hexdigest()=='a28e7f696886b018d0411c22cb4935251cfb8b1f1cc116f958434f6fe31621a7'
assert hashlib.sha256(p.get_data(0x633fe0,477)).hexdigest()=='3bd2087151c8ed294afe5b8cb130a5cb840809ab89ed570e4aad7aa0caa4ba9b'
assert hashlib.sha256(p.get_data(0x6313f0,174)).hexdigest()=='0af4d568eb31f97459ccb57de653fdd56160cd895dd6eb16767e608b700941e4'
assert hashlib.sha256(p.get_data(0x8df40,74)).hexdigest()=='1b2da1aff9e9675f57af9ee3d894fa446245a3ce1ee7d3159c9047849eab6c4c'
k=C.WinDLL('kernel32');k.LoadLibraryExW.argtypes=[C.c_wchar_p,C.c_void_p,C.c_uint];k.LoadLibraryExW.restype=C.c_void_p;k.VirtualProtect.argtypes=[C.c_void_p,C.c_size_t,C.c_ulong,C.POINTER(C.c_ulong)]
b=k.LoadLibraryExW(str(Path(sys.argv[1]).resolve()),None,1);assert b
keepers=[]
def patch(a,data):
 old=C.c_ulong();assert k.VirtualProtect(b+a,len(data),0x40,C.byref(old));C.memmove(b+a,data,len(data))
# Only the action-equality predicate and RTTI boundary are fixtures; seat reservation,
# both current/last-rider weak references and same-seat Human RideVehicle are native.
patch(0x56FAD0,b'\xb0\x01\xc3');patch(0x12DA7AA,b'\x48\x89\xc8\xc3')
h=C.create_string_buffer(0x1900);v=C.create_string_buffer(0x700);seat=C.create_string_buffer(0x340);hc=C.create_string_buffer(16);vc=C.create_string_buffer(16);arg=C.create_string_buffer(16)
def put(buf,off,value):C.c_uint64.from_buffer(buf,off).value=value
put(h,0x28,C.addressof(h));put(h,0x30,C.addressof(hc));put(h,0x1540,C.addressof(seat));put(h,0x1548,C.addressof(v));put(h,0x1550,C.addressof(vc));put(v,0x608,C.addressof(seat));put(v,0x618,1);put(arg,0,C.addressof(v));put(arg,8,C.addressof(vc))
C.c_int32.from_buffer(hc,8).value=2;C.c_int32.from_buffer(hc,12).value=2;C.c_int32.from_buffer(vc,8).value=3;C.c_int32.from_buffer(vc,12).value=2
C.c_float.from_buffer(h,0x90).value=123
ride=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_int)(b+0x5765E0)
assert C.c_uint64.from_buffer(seat,0x260).value==0
ride(C.addressof(h),C.addressof(arg),0)
assert C.c_uint64.from_buffer(seat,0x260).value==C.addressof(h)
assert C.c_uint64.from_buffer(seat,0x300).value==C.addressof(h)
assert C.c_uint64.from_buffer(h,0x1540).value==C.addressof(seat)
assert C.c_uint16.from_buffer(v,0x628).value&1
assert C.c_float.from_buffer(h,0x90).value==123
assert C.c_int32.from_buffer(vc,8).value==2 and C.c_int32.from_buffer(hc,8).value==2
assert C.c_int32.from_buffer(hc,12).value==4
print('PASS real Human RideVehicle same-car same-seat repairs empty seat, updates occupancy and weak refs without exit/warp')
C.c_int32.from_buffer(vc,8).value+=1
ride(C.addressof(h),C.addressof(arg),0)
assert C.c_int32.from_buffer(hc,12).value==4 and C.c_int32.from_buffer(vc,8).value==2
print('PASS repeated native same-seat restore is idempotent and balances consumed vehicle reference')
