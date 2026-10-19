"""Read the supported EDF.dll and prove the thermal hooks' ABI, pass and HUD timing anchors.

No binary is loaded or executed. Missing --dll skips with 77.
"""
from pathlib import Path
import argparse
import re
import struct

parser=argparse.ArgumentParser()
parser.add_argument('--dll',default='')
args=parser.parse_args()
if not args.dll or not Path(args.dll).is_file():
    print('SKIP: EDF6_NATIVE_DLL required')
    raise SystemExit(77)
import pefile
pe=pefile.PE(args.dll,fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
binary=pe.get_memory_mapped_image()
source=(Path(__file__).resolve().parents[1]/'src/thermal.cpp').read_text(encoding='utf-8')
def value(name):
    return int(re.search(r'\b'+name+r'=0x([0-9A-Fa-f]+)',source).group(1),16)
def signature(name):
    body=re.search(r'\b'+name+r'\[\]=\{([^}]+)\}',source).group(1)
    return bytes(int(token.strip(),0) for token in body.split(','))
def call(at,target):
    assert binary[at]==0xE8 and at+5+struct.unpack_from('<i',binary,at+1)[0]==target,hex(at)
for at,name in ((0x11E222B,'kBeginGuard'),(0x11E3AD4,'kEndGuard'),(0x11E2550,'kSubmitGuard')):
    expected=signature(name)
    assert binary[at:at+len(expected)]==expected,(name,hex(at))
for site,target in (('kViewBeginCall','kViewBegin'),('kViewEndCall','kViewEnd'),('kSubmitCall','kSubmit')):
    call(value(site),value(target))
for name in ('kViewBegin','kViewEnd'):
    at=value(name);expected=signature('kStageCode')
    assert binary[at:at+len(expected)]==expected,name
# The captured D3D context is nativeCtx+8; this verified native code invokes OMSetRenderTargets on it.
assert binary[0x113E7D3:0x113E7D7]==bytes.fromhex('488b4d08')
assert binary[0x113E7F1:0x113E7F7]==bytes.fromhex('ff9008010000')
# World end hook is immediately followed by RTV/DSV unbinding; composite must precede that call.
call(0x11E3AF2,0x112C970)
# The System render is recorded in update; the original screen HUD is recorded later in the main loop.
call(0x1198FA4,0x11978D0)
call(0x705715,0x1198DA0)
call(0x7058B7,0x11977F0)
call(0x1197854,0x118E500)
# Normal opaque geometry uses pass 1. Umbra's separate query loop closes Begin/End around only query geometry.
assert binary[0x11E2550:0x11E2556]==bytes.fromhex('41b801000000')
call(0x11E41BF,0x113D470)
print('thermal native begin/end/submit ABI, individual opaque pass, GPU context and world-before-HUD anchors passed')
