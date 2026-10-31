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
root=Path(__file__).resolve().parents[1]
# firstperson.cpp and the muzzle layout it reads through common/edf/weapon.h (MuzzleAt / MuzzleWorld)
source=(root/'src/firstperson.cpp').read_text(encoding='utf-8')+(root/'common/edf/weapon.h').read_text(encoding='utf-8')
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
# The laser's start (firstperson.cpp LaserMuzzle): slot 2 0x6890A0 reads [weapon+0x1D0]'s +0x90 (the first muzzle entry's
# local x bone) less kLaserBelow of its up row; slot 3 copies it to the draw's +0x120 (the main object's +0x130).
assert binary[0x6890DB:0x6890EE]==bytes.fromhex('488b41084c8b80d0010000410f108090000000'),'laser start from the first muzzle entry'
assert value('kMuzzles')==0x1D0 and value('kMuzzleCount')==0x1E0 and value('kMuzzleLocal')==0x10 and value('kLaserStart')==0x120
assert binary[0x68910E:0x689111]==bytes.fromhex('0f5915')
assert 0x689115+struct.unpack_from('<i',binary,0x689111)[0]==0x17AA990
below=float(re.search(r'kLaserBelow=([0-9.]+)f',source).group(1))
assert all(abs(x-below)<1e-7 for x in struct.unpack_from('<4f',binary,0x17AA990)),'the start sits kLaserBelow under the muzzle'
assert binary[0x688B98:0x688BA6]==bytes.fromhex('0f1087900000000f118730010000'),'slot 3 copies the start to the draw field'
# 0x6969A0 (the muzzle entry's matrices): +0x90 = local (+0x10) x the bone record [+0] +0xB0..
assert binary[0x696A03:0x696A06]==bytes.fromhex('488b11'),'the entry +0 is its bone record'
assert binary[0x696A2F:0x696A36]==bytes.fromhex('0f10b2b0000000'),'the bone record +0xB0 world matrix'
print('laser start source (muzzle entry, 0.02 below, slot 3 copy) passed')
# The dot's ray (firstperson.cpp LaserCastHook / LaserRayHook): slot 2 of the attachment's main vtable is 0x6890A0, whose
# cast 0x11A70B0 at 0x68934C takes the start [rsp+0x50] and end [rsp+0x40] (r8 / r9), the same two locals the second query
# reads (0x68935E); the hit goes to +0xC0 and the dot object [+0xD0] (0x689514); slot 3 draws the beam kLaserBeam m on.
rvt=value('kLaserRayVtable')
assert struct.unpack_from('<Q',binary,rvt+8*2)[0]-pe.OPTIONAL_HEADER.ImageBase==value('kLaserRay'),'the laser ray is slot 2'
block=re.search(r'\bkLaserRayCode\[\]=\{([^}]+)\}',source).group(1)
code=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',block))
assert binary[value('kLaserRay'):value('kLaserRay')+len(code)]==code,'the laser ray prologue'
cast=value('kLaserCastCall')
assert binary[cast]==0xE8 and cast+5+struct.unpack_from('<i',binary,cast+1)[0]==value('kLaserCast'),'the laser cast call'
assert binary[cast-14:cast]==bytes.fromhex('4c8d4c24404c8d442450488d4d40'),'r9 = &end [rsp+0x40], r8 = &start [rsp+0x50]'
assert binary[0x68935E:0x689374]==bytes.fromhex('0f28442450660f7f4424600f284c2440660f7f4c2470'),'the second query reads the same locals'
assert binary[0x6890DB:0x6890DF]==bytes.fromhex('488b4108'),'slot 2 reads its weapon at +8 (kLaserRayWeapon)'
assert value('kLaserRayWeapon')==8 and value('kLaserEnd')==0x130
assert binary[0x68950D:0x689518]==bytes.fromhex('0f1083c00000000f114010'),'the dot object at the hit'
assert binary[0x688B83:0x688B87]==bytes.fromhex('f30f100d') and struct.unpack_from('<f',binary,0x688B8B+struct.unpack_from('<i',binary,0x688B87)[0])[0]==float(re.search(r'kLaserBeam=([0-9.]+)f',source).group(1))
print('laser dot ray (slot 2, the cast and its locals, the beam length) passed')
# Every flash class uses MuzzleFlashBase's +0x120 borrowed matrix. Check the two destructor routes before
# trusting a matrix until destruction (secondary Umbra thunks jump directly to the primary body).
assert binary[0x2BA563:0x2BA56E]==bytes.fromhex('488b433848898720010000')
classes=re.search(r'FlashClass flashClasses\[\]=\{(.*?)\n\};',source,re.S).group(1)
entries=re.findall(r'\{(0x[0-9A-F]+),(0x[0-9A-F]+),(0x[0-9A-F]+|0),(0x[0-9A-F]+|0)\}',classes)
assert len(entries)==10
for values in entries:
    primary,dtor,secondary,thunk=(int(x,0) for x in values)
    assert struct.unpack_from('<Q',binary,primary+8)[0]-pe.OPTIONAL_HEADER.ImageBase==dtor,(hex(primary),'primary destructor')
    if secondary:
        assert struct.unpack_from('<Q',binary,secondary)[0]-pe.OPTIONAL_HEADER.ImageBase==thunk
        assert binary[thunk:thunk+7]==bytes.fromhex('4881e930010000')
        at=thunk+7
        assert binary[at]==0xE9 and at+5+struct.unpack_from('<i',binary,at+1)[0]==dtor,(hex(thunk),'secondary destructor')
print('native muzzle matrix ownership and 10 flash classes destructor routes passed')

# The flash's creation (firstperson.cpp FlashCreateHook, docs/fp-render-re.md §5): the shot entry 0x696FD0 hands it the
# muzzle's matrix through weapon+0x4C0 (0x697FB4) and calls the factory with r9 = weapon+0x488 (0x697FD0). The only
# other +0x4C0 written just before a factory call is the vehicle weapons' 0x6B124C.
imagebase=pe.OPTIONAL_HEADER.ImageBase
text=next(x for x in pe.sections if x.Name.startswith(b'.text'))
tva,tsz=text.VirtualAddress,text.Misc_VirtualSize
code=binary[tva:tva+tsz]
site=value('kFlashCall')
assert binary[site]==0xE8 and site+5+struct.unpack_from('<i',binary,site+1)[0]==value('kEffectCreate')
assert binary[0x697FB4:0x697FC9]==bytes.fromhex('498997c0040000'+'4d8d8f88040000'+'4d8b8780040000'),'+0x4C0, r9 = +0x488, r8 = the class +0x480'
assert value('kFlashParam')==0x488 and value('kFlashMatrix')==0x4C0
def makes(at):   # a call of the effect factory within 0x40 bytes after `at`
    return any(binary[c]==0xE8 and c+5+struct.unpack_from('<i',binary,c+1)[0]==value('kEffectCreate') for c in range(at,at+0x40))
writers=sorted(tva+m.start() for m in re.finditer(rb'[\x48\x49\x4c\x4d]\x89[\x80-\xbf]\xc0\x04\x00\x00',code)
               if (code[m.start()+2]&7)!=4 and makes(tva+m.start()))
assert writers==[0x697FB4,0x6B124C],[hex(x) for x in writers]
# Every round of every weapon class goes through 0x696FD0 (the kick's shot source): exactly these five calls, local fire
# 0x690CC0 (0x690D54), network rebuilds 0x692540 and the count replays 0x690420 / 0x6947E0. Its head is heli.cpp's hook,
# which tells the first person after each round.
calls=sorted(tva+m.start() for m in re.finditer(rb'[\xe8\xe9]',code)
             if m.start()+5<=len(code) and tva+m.start()+5+struct.unpack_from('<i',code,m.start()+1)[0]==0x696FD0)
assert calls==[0x6904F7,0x690603,0x690D54,0x692A24,0x694894],[hex(x) for x in calls]
heli=(root/'src/heli.cpp').read_text(encoding='utf-8')
assert re.search(r'\bkMedicShot=0x696FD0\b',heli) and 'FirstPersonShot(weapon);' in heli
sig=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',re.search(r'kMedicShotSig\[\]=\{([^}]+)\}',heli).group(1)))
assert binary[0x696FD0:0x696FD0+len(sig)]==sig
def rtti(vt):
    locator=struct.unpack_from('<Q',binary,vt-8)[0]-imagebase
    td=struct.unpack_from('<I',binary,locator+12)[0]
    return binary[td+0x10:td+0x80].split(b'\0')[0].decode()
def reaches(fn,at,target):
    assert binary[at] in (0xE8,0xE9) and at+5+struct.unpack_from('<i',binary,at+1)[0]==target,(hex(fn),hex(at))
# The classes' fire slots and the local fire 0x690CC0 they reach (docs/fp-render-re.md §5.1).
for cls,vt,slot,fn,at,target in (
        ('Weapon_BasicShoot',0x17E36E0,16,0x69AA10,0x69AA27,0x690CC0),('Weapon_HeavyShoot',0x17E3F18,16,0x69E050,0x69E073,0x69AA10),
        ('Weapon_ChargeShoot',0x17E3AF0,16,0x69C260,0x69C277,0x690CC0),('Weapon_Gatling',0x17E3D50,16,0x69D320,0x69D337,0x690CC0),
        ('Weapon_HomingShoot',0x17E40A0,16,0x69E7C0,0x69E7D7,0x690CC0),('Weapon_PileBanker',0x17E4A80,16,0x6A6F80,0x6A6F97,0x690CC0),
        ('Weapon_Shield',0x17E51A0,16,0x6AAB20,0x6AAB3C,0x690CC0),('Weapon_ImpactHammer',0x17E42A0,15,0x69FD80,0x69FDC6,0x690CC0),
        ('Weapon_Throw',0x17E5B60,23,0x6B0300,0x6B0345,0x690CC0),('Weapon_Sub',0x17E5440,16,0x6AD610,0x6ADA04,0x690CC0)):
    assert rtti(vt)=='.?AV'+cls+'@@',(cls,rtti(vt))
    assert struct.unpack_from('<Q',binary,vt+8*slot)[0]-imagebase==fn,(cls,'fire slot')
    reaches(fn,at,target)
reaches(0x690CC0,0x690D54,0x696FD0)
print('the shot entry: five callers, the fire classes reach it, the flash parameter block and its two +0x4C0 writers passed')
# The flash factories (kFlashFactories): a class object's vtable is Factory@<class>, its slot 2 makes one with that
# class's constructor; the constructors read MuzzleFlash_CustomParameter at the parameter block's +0x40 (= weapon+0x4C8,
# a document handle: tag 2, 0x68DBE5) and fp_flash.h scales the elements of these indices.
factories=re.findall(r'\{(0x[0-9A-F]+),"(\w+)",(?:&fpflash::k(\w+)|nullptr)\}',re.search(r'kFlashFactories\[\]=\{(.*?)\n\};',source,re.S).group(1))
assert len(factories)==10,factories
ctor={'MuzzleFlash01':0x2BEC90,'MuzzleFlash02':0x2BFE60,'LaserMuzzleFlash02':0x2BBE90,'LaserMuzzleFlash01':0x2BB300,
      'LaserMuzzleFlash03':0x2BD9B0,'EfsMuzzleFlash':0x2BA6D0,'MuzzleFlashRailGun':0x2C0F90,'PileBunkerMuzzleFlash01':0x2C2640,
      'TestMuzzleFlash':0x2C3710,'MuzzleSmoke01':0x2C1BB0}
for vt,name,layout in factories:
    vt=int(vt,16)
    assert rtti(vt)=='.?AVFactory@'+name+'@@',(name,rtti(vt))
    make=struct.unpack_from('<Q',binary,vt+16)[0]-imagebase
    assert any(binary[a]==0xE8 and a+5+struct.unpack_from('<i',binary,a+1)[0]==ctor[name] for a in range(make,make+0x40)),name
    assert (name in ('MuzzleFlash01','MuzzleFlash02','LaserMuzzleFlash02'))==(layout=='Beams'),name
    assert (name in ('LaserMuzzleFlash01','LaserMuzzleFlash03'))==(layout=='Sprite'),name
assert binary[0x68DBE5:0x68DBF1]==bytes.fromhex('b802000000'+'66898570100000'),'the handle is tagged 2'
assert binary[0x68DBFB:0x68DC02]==bytes.fromhex('488d8ec8040000'),'the handle at weapon+0x4C8'
for at,reg in ((0x2BFF54,'4883c740'),(0x2BED81,'4883c740'),(0x2BBFA4,'4883c640'),(0x2BB39E,'4883c340'),(0x2BDA97,'4883c740')):
    assert binary[at:at+4]==bytes.fromhex(reg),hex(at)
# The element indices each constructor asks for (mov dword [its index slot], i): the beams' [1] [2] [5] [6] lengths and
# widths and [8] colour; the sprites' [1] size and [2] colour.
for at,imm in ((0x2C0193,1),(0x2C0230,2),(0x2C0459,5),(0x2C04E9,6),(0x2C05EC,8),                 # MuzzleFlash02
               (0x2BEF75,1),(0x2BF016,2),(0x2BF1BF,5),(0x2BF254,6),(0x2BF2E9,8),                 # MuzzleFlash01
               (0x2BC09E,1),(0x2BC142,2),(0x2BC2FB,5),(0x2BC3AF,6),(0x2BC500,8),                 # LaserMuzzleFlash02
               (0x2BB443,1),(0x2BB3BD,2),(0x2BDB51,1),(0x2BDABC,2)):                             # LaserMuzzleFlash01, 03
    at_imm=at+4 if binary[at+1]==0x44 else at+3                                                 # [rsp+d8] / [rbp+d8]
    assert binary[at]==0xC7 and struct.unpack_from('<I',binary,at_imm)[0]==imm,hex(at)
flash=(root/'src/fp_flash.h').read_text(encoding='utf-8')
assert 'kBeams{"beams",10,{1,2,5,6},4,8}' in flash and 'kSprite{"sprite",4,{1,0,0,0},1,2}' in flash
# The document reads fp_flash.h mirrors: the element (0x52DE0), the type (0x52E50) and the double (0x52F50).
for at,want,what in ((0x52DE0,'4c8b09448b5108','the document [+0], the node index [+8]'),
                     (0x52DF2,'4d395118','the override map count +0x18'),(0x52DF8,'498b4108','the override map +0x08'),
                     (0x52E09,'498b4928','the override slots +0x28'),(0x52E22,'498b4950','the file +0x50'),
                     (0x52E30,'4863510c','the nodes at i32 [file+0xC]'),(0x52E37,'48630a','a container block at node + i32 [node]'),
                     (0x52E3D,'48634108','its child indices at block + [block+8]'),(0x52E9D,'8b440108','a node type at +8'),
                     (0x52FC0,'f20f100401','a node double at +0')):
    assert binary[at:at+len(want)//2]==bytes.fromhex(want),what
# The flashes' own update and draws (fp-render-re.md §5.2): the update copies the borrowed matrix (flash+0x120) to its
# world matrix flash+0x60; pass 1 is a light at its position row with its colour; pass 2 draws the geometry through
# flash+0x60 (lea r8,[umbra-0xD0]), which in first person is shot·T with the view model's scale: the geometry already
# shrinks with the gun, so CreateFlash scales the sizes by FirstPersonFlashScale alone.
assert struct.unpack_from('<Q',binary,0x17A6578+8*3)[0]-imagebase==0x2C0D50,'MuzzleFlash02 update'
assert struct.unpack_from('<Q',binary,0x17A65E0+8)[0]-imagebase==0x2C0BF0,'MuzzleFlash02 draw'
assert struct.unpack_from('<Q',binary,0x17A64E0+8)[0]-imagebase==0x2BFAC0,'MuzzleFlash01 draw'
assert binary[0x2C0D5E:0x2C0D65]==bytes.fromhex('488b8320010000'),'MuzzleFlash02 update reads +0x120'
for at,want,what in ((0x2C0CFF,'4c8d8330ffffff','MuzzleFlash02'),(0x2BFBCF,'4c8d8330ffffff','MuzzleFlash01'),
                     (0x2BD351,'4c8d8330ffffff','LaserMuzzleFlash02'),(0x2BBBB1,'4c8d8130ffffff','LaserMuzzleFlash01')):
    assert binary[at:at+7]==bytes.fromhex(want),what+' pass 2 draws through the world matrix'
for at,target in ((0x2C0D32,0x11548F0),(0x2C0C44,0x112C860)):
    assert binary[at]==0xE8 and at+5+struct.unpack_from('<i',binary,at+1)[0]==target,hex(at)
source_size=re.search(r'const double size=([^;]+);',source).group(1).strip()
assert source_size=='Cfg().fpFlashScale',source_size
print('flash factories, their constructors, the parameter handle, the element indices and the document reads passed')

game=Path(args.dll).resolve().parent
if (game/'Root.cpk').is_file():
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
    import rootcpk
    from mdb import rab_read,mdb_read
    stock=rootcpk.Game(str(game))
    # Every stock weapon whose flash class has a layout (kFlashFactories) carries that layout: none falls back.
    import dsgo
    want={'MuzzleFlash01':(10,(1,2,5,6),8),'MuzzleFlash02':(10,(1,2,5,6),8),'LaserMuzzleFlash02':(10,(1,2,5,6),8),
          'LaserMuzzleFlash01':(4,(1,),2),'LaserMuzzleFlash03':(4,(1,),2)}
    seen={k:0 for k in want}
    needle='MuzzleFlash'.encode('utf-16le')
    for name in stock.names('WEAPON'):
        if not name.upper().endswith('.SGO'):
            continue
        data=stock.read('WEAPON',name)
        if needle not in data:
            continue
        r=dsgo.parse(data).root
        try:
            cls=r.get('MuzzleFlash')
        except KeyError:
            continue
        if cls not in want:
            continue
        least,sizes,colour=want[cls]
        items=r.get('MuzzleFlash_CustomParameter').items
        if not items and name.upper()=='V_FUEL01.SGO':
            continue   # a vehicle's empty list (no flash drawn); never a soldier's weapon
        assert len(items)>=least and all(isinstance(items[k],float) for k in sizes),(name,cls)
        c=items[colour]
        assert isinstance(c,dsgo.Node) and len(c.items)==4 and all(isinstance(v,float) for v in c.items),(name,cls)
        seen[cls]+=1
    assert all(seen.values()),seen
    print('stock flash parameters fit their layouts:',seen)
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
