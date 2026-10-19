"""Read-only supported EDF.dll movement-coordinate audit; no live game.

Verifies the exact native control basis used by every admitted family and the
Deiroi exception. On Windows also executes the pure native quaternion-vector
routine in a DONT_RESOLVE_DLL_REFERENCES private mapping, never DllMain.
"""
from pathlib import Path
import math
import struct
import sys

if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O')
import pefile
pe=pefile.PE(sys.argv[1],fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46 and pe.FILE_HEADER.Machine==0x8664
img=pe.get_memory_mapped_image()
checks=1

def check(ok,why):
    global checks
    assert ok,why
    checks+=1

# Actual native consumers, including current-vs-target distinctions. Each byte
# string is an independently identified instruction, not a generated snapshot.
reads={
    0x53D767:'f30f108f44140000', # Walking actual yaw1444, not target1454
    0x4721E4:'f30f10b7e4100000', # Humanoid actual yaw10E4, not target10D4
    0x3B40F3:'f30f108bc4100000', # FlyingDyno actual10C4
    0x3A5021:'f30f108f54170000', # Merman actual1754
    0x36CB05:'f30f1089c40f0000', # DangoLarge actualFC4
    0x3C0BC6:'f30f108f04100000', # ShellFish actual1004
    0x3D626D:'f30f108ec40f0000', # Spinner actualFC4
    0x3E24AB:'f30f108fd4120000', # Squid actual12D4
    0x4BCEDA:'f30f10b704150000', # Monster actual1504
    0x30E4CB:'f30f10b784100000', # AlienTrailer current1084
    0x4F75EA:'f30f108ff4050000', # Carrier current5F4
    0x3EE42F:'488d96b00f0000',   # TimeShip movement FB0 quaternion, not targetFC0
    0x4A933A:'4c8d83300e0000',   # Insect E30 target quaternion, not rendered E40
    0x4E03CE:'4c8d87100e0000',   # Nephila E10 cling quaternion
    0x52F358:'0f595770',0x52F365:'0f594760',0x52F376:'0f598f80000000', # UFO507 native actor rows
    0x5375B8:'0f594f70',0x5375C3:'0f594760',0x5375D5:'0f598780000000', # UFO515 native actor rows
    0x322939:'0f5855a7',0x32296B:'0f5855c7', # Deiroi world command -> world foot target
}
for at,want in reads.items():
    raw=bytes.fromhex(want)
    check(img[at:at+len(raw)]==raw,f'native movement basis {at:X}')
check(struct.unpack_from('<4f',img,0x1765B90)==(1.,1.,1.,1.),'Deiroi movement has positive identity scale')
for at,sign in ((0x17C5570,-1),(0x17B0A10,1),(0x17AC460,1),(0x1768E20,1),
                (0x17A3040,1),(0x1C36AD0,-1),(0x17CC780,-1),(0x17CCB20,-1)):
    values=struct.unpack_from('<4f',img,at)
    check(all(x*sign>0 for x in values),f'native local movement scale sign {at:X}')
source=(Path(__file__).resolve().parents[1]/'src/enemy_control_native_move.cpp').read_text(encoding='utf-8-sig')
for value in ('0x1444','0x10E4','0xE30','0xE10','0xFB0','0x1084','0x1754','0x10C4','0x1004','0xFC4','0x12D4','0x1504','0x5F4'):
    check(value in source,'production native basis '+value)
check('case 0x31ED90:' in source and 'right[0]=1;right[1]=0;right[2]=0;' in source,'Deiroi world-frame exception stays explicit')

if sys.platform=='win32':
    import ctypes as C
    k=C.WinDLL('kernel32')
    k.LoadLibraryExW.argtypes=[C.c_wchar_p,C.c_void_p,C.c_uint];k.LoadLibraryExW.restype=C.c_void_p
    k.FreeLibrary.argtypes=[C.c_void_p];k.FreeLibrary.restype=C.c_int
    mapped=k.LoadLibraryExW(str(Path(sys.argv[1]).resolve()),None,1)
    check(bool(mapped),'private native mapping without DllMain')
    transform=C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p)(mapped+0x4D5C0)
    for yaw in (0,.3,-.3,math.pi/2,-math.pi/2,math.pi,2*math.pi+1):
        q=(C.c_float*4)(0,math.sin(yaw/2),0,math.cos(yaw/2))
        for raw in ((1,0,0),(0,0,1),(.2,.4,-.6)):
            vec=(C.c_float*4)(*raw,1)
            transform(C.addressof(vec),C.addressof(q))
            want=(raw[0]*math.cos(yaw)+raw[2]*math.sin(yaw),raw[1],-raw[0]*math.sin(yaw)+raw[2]*math.cos(yaw))
            check(all(abs(vec[i]-want[i])<.00001 for i in range(3)),'real quaternion primitive agrees with production inverse basis')
    check(bool(k.FreeLibrary(mapped)),'release private mapping')
print(f'{checks} native movement-coordinate checks passed; no live game execution')
