"""Compare the first-person render hooks with the supported native binary, including instruction prefixes."""
from pathlib import Path
import argparse
import re
import struct
import sys

ap=argparse.ArgumentParser()
ap.add_argument('--dll',default='')
args=ap.parse_args()
if not args.dll or not Path(args.dll).is_file():
    print('SKIP: EDF6_NATIVE_DLL required')
    raise SystemExit(77)
import pefile
pe=pefile.PE(args.dll,fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
binary=pe.get_memory_mapped_image()
source=(Path(__file__).resolve().parents[1]/'src/firstperson.cpp').read_text(encoding='utf-8')
value=lambda name:int(re.search(r'\b'+name+r'=0x([0-9A-Fa-f]+)',source).group(1),16)
for name,signature in (('kMeshDraw','kMeshDrawCode'),('kLaserDraw','kLaserDrawCode'),('kModelDraw','kModelDrawCode')):
    block=re.search(r'\b'+signature+r'\[\]=\{([^}]+)\}',source).group(1)
    expected=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',block))
    rva=value(name)
    assert binary[rva:rva+len(expected)]==expected,(name,hex(rva))
# Main Umbra camera submission: rdx = camera+0x220, rcx = camera+0x70.
site=value('kCameraSubmitCall')
assert binary[site]==0xE8 and site+5+struct.unpack_from('<i',binary,site+1)[0]==value('kCameraSubmit')
assert binary[site-11:site]==bytes.fromhex('498d9620020000498d4e70')
print('native main-camera submission and camera ownership offsets passed')
call=value('kMeshCall')
assert binary[call]==0xE8 and call+5+struct.unpack_from('<i',binary,call+1)[0]==value('kMeshDraw')
for site,target in ((0x10FF4F7,'kMeshPrepare'),(0x10FF59C,'kMeshPass'),(0x10FF5AF,'kFlushDrawState')):
    assert binary[site]==0xE8 and site+5+struct.unpack_from('<i',binary,site+1)[0]==value(target),(hex(site),target)
assert binary[0x10FF559:0x10FF55C]==b'\xff\x50\x60'
assert binary[0x10FF5C4:0x10FF5C7]==b'\xff\x50\x60'
vt=value('kLaserDrawVtable')
assert struct.unpack_from('<Q',binary,vt+8)[0]-pe.OPTIONAL_HEADER.ImageBase==value('kLaserDraw')
locator=struct.unpack_from('<Q',binary,vt-8)[0]-pe.OPTIONAL_HEADER.ImageBase
assert struct.unpack_from('<I',binary,locator+4)[0]==0x10,'laser draw this is its umbra subobject'
print('native render call, signatures, laser draw slot and owner adjustment passed')
game=Path(args.dll).resolve().parent
if (game/'Root.cpk').is_file():
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
    import rootcpk
    from mdb import rab_read,mdb_read
    stock=rootcpk.Game(str(game))
    for stem in ('P505_RANGER','P506_WINGDIVER','P507_FENCER','P508_AIRRADER'):
        models=[(f.name,mdb_read(f.data)) for f in rab_read(stock.read('OBJECT',stem+'.MRAB')).files if f.name.lower().endswith('.mdb')]
        base=next(m for name,m in models if name.lower()==stem.lower()+'.mdb')
        for name,m in models:
            names=[m.name_of(b.name) for b in m.bones]
            assert len(names)<=256 and 'te_r' in names and 'te_l' in names
            for obj in m.objects:
                for mesh in obj.meshes:
                    weights=next(e for e in mesh.elems if e.name.lower()=='blendweight')
                    indices=next(e for e in mesh.elems if e.name.lower()=='blendindices')
                    assert mesh.vsize in (60,64) and weights.fmt==1 and weights.offset==mesh.vsize-20
                    assert indices.fmt==21 and indices.offset==mesh.vsize-4
        print(stem,'base/LOD arm bones and skin declarations passed (each draw uses its own palette ordering)')
