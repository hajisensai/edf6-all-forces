"""Read-only native HUiHudRader observer contracts for enemy body control.

No game DLL is loaded/executed. The production fixture executes the installed
hook ABI separately; this audit verifies its real supported native input sites.
"""
from pathlib import Path
import argparse
import re
import struct

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll',nargs='?',type=Path)
args=parser.parse_args()
if not args.edf_dll or not args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll');raise SystemExit(77)
import pefile
import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
pe=pefile.PE(str(args.edf_dll),fast_load=True)
assert pe.FILE_HEADER.Machine==0x8664 and pe.FILE_HEADER.TimeDateStamp==0x678CCB46,'unsupported native profile'
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True

def expect(at,pattern):
    raw=bytes.fromhex(pattern)
    assert pe.get_data(at,len(raw))==raw,f'native instruction changed at {at:#x}'

def ins(at):
    return next(md.disasm(pe.get_data(at,16),at))

def call(at,target):
    i=ins(at)
    assert i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==target,(hex(at),i.op_str)

def rip(at):
    i=ins(at)
    assert i.operands[1].type==X86_OP_MEM and i.operands[1].mem.base==X86_REG_RIP
    return at+i.size+i.operands[1].mem.disp

base=pe.OPTIONAL_HEADER.ImageBase
assert struct.unpack('<Q',pe.get_data(0x17FCF10+16,8))[0]==base+0x82B4D0,'radar update slot changed'
expect(0x82B541,'48 8b 91 80 07 00 00 48 85 d2 74 cf 83 7a 08 00')
expect(0x82B553,'4c 8d b9 78 07 00 00')
for at,name in [(0x82B585,b'.?AVSoldierBase@@\0'),(0x82B58C,b'.?AVSceneObject@game@xgs@@\0')]:
    assert pe.get_data(rip(at)+16,len(name))==name,'native HUD owner cast changed'
expect(0x82B59D,'48 8b f0 48 89 85 70 01 00 00')
expect(0x82B671,'0f 10 76 70 0f 10 86 80 00 00 00')
expect(0x82B681,'48 8b 86 50 15 00 00')
call(0x82B6A8,0x6BB5A0);call(0x82B6C6,0x6BB5A0)
call(0x82B6DB,0x4E100)
assert struct.unpack('<f',pe.get_data(rip(0x82B6E5),4))[0]==0.75
assert struct.unpack('<f',pe.get_data(rip(0x82B6FD),4))[0]==-0.75
expect(0x82B7A5,'0f 10 86 90 00 00 00 66 0f 7f 44 24 50')
# All categories consume the one native center and heading matrix. No helper
# call is replaced: range clipping, viewport/scale, markers and shapes stay native.
for at,target in [(0x82B8F6,0x82A890),(0x82B932,0x82A890),(0x82B975,0x82A890),
                  (0x82BB3D,0x82A890),(0x82BB7B,0x82A890),(0x82BBDC,0x82A890),
                  (0x82BC18,0x82A890),(0x82BCB2,0x82A030),(0x82BD2E,0x82A890),
                  (0x82BDB2,0x829780),(0x82BE14,0x828D10),(0x82BE43,0x829BE0)]:
    call(at,target)
for at in [0x82B89A,0x82BB80]:expect(at,'8b 96 14 03 00 00')
call(0x82B8CA,0x5E0F20);call(0x82BBB0,0x5E0F20)
expect(0x82BB42,'41 b8 02 00 00 00 48 8d 54 24 30 49 8b ce')
call(0x82BB50,0x82B220)
# Exact native hostile/friendly colour groups: changing observer teams dispatches
# real native rows into these existing styles; colour constants are never patched.
for at,rgba in [(0x82BBC0,(1.,0.,0.,1.)),(0x82BB5F,(0.,.75,1.,1.)),
                (0x82BBFC,(1.,1.,0.,1.)),(0x82B959,(1.,1.,1.,1.))]:
    assert struct.unpack('<4f',pe.get_data(rip(at),16))==rgba
expect(0x82C69A,'41 0f 10 10 48 8b da')
expect(0x82C6A9,'41 0f 5c 19') # target position minus center
expect(0x82C773,'f3 0f 59 bf b0 08 00 00') # original radar scale
expect(0x82C74E,'0f 2f c1 76 1e') # original native distance/range clipping
# Verify every production installation fingerprint against the actual DLL.
source=(Path(__file__).resolve().parents[1]/'src/enemy_control_radar.cpp').read_text(encoding='utf-8-sig')
prints=re.findall(r'\{(0x[0-9A-Fa-f]+),\{([^}]+)\},(\d+)\}',source)
assert len(prints)>=10,'production fingerprints not found'
for at,values,size in prints:
    raw=bytes(int(v.strip(),0) for v in values.split(','))[:int(size)]
    assert len(raw)==int(size) and pe.get_data(int(at,0),len(raw))==raw,f'production guard mismatch at {at}'
print('enemy radar native audit: HUD weak Soldier owner, source-centered bug, native center/rotation stack, both hostile teams, friendly colour and unchanged native range verified')
