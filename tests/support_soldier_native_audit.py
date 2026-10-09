"""Optional Windows x64 EDF.dll/Root.cpk support-soldier contract audit.

Requires pefile and repository pylib. Pass the supported EDF.dll path explicitly;
missing input/platform/Root.cpk returns 77. No DllMain, game process or installation
writes. The native child-ID functions 776790/776AB0/776450 execute unmodified.
Root.cpk is read-only and both exact Ranger templates and their weapon are checked.
The real registration function 781950 is also executed with recording manager/
network-interface/ID-store/logging stubs: its own host selection, weak argument
consumption and returned ID address remain native. No actual network is opened.
This is not a complete Soldier constructor/world/AI or multiplayer end-to-end test.
"""
import argparse
import ctypes as C
from pathlib import Path
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('edf_dll',nargs='?',type=Path)
a=p.parse_args()
if sys.platform!='win32' or C.sizeof(C.c_void_p)!=8 or not a.edf_dll or not a.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll on Windows x64');raise SystemExit(77)
if not (a.edf_dll.parent/'Root.cpk').is_file():
    print('SKIP: sibling Root.cpk required for real soldier resource checks');raise SystemExit(77)
if not __debug__:raise RuntimeError('Do not disable native preconditions with -O')
import pefile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
import dsgo
from rootcpk import Game
pe=pefile.PE(str(a.edf_dll),fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46 and pe.FILE_HEADER.Machine==0x8664
assert pe.get_data(0x776790,16)==bytes.fromhex('48895c2408574883ec2033c00f57c048')
assert pe.get_data(0x781950,16)==bytes.fromhex('48895c242055565741564157488d6c24')
k=C.WinDLL('kernel32')
k.LoadLibraryExW.argtypes=[C.c_wchar_p,C.c_void_p,C.c_uint]
k.LoadLibraryExW.restype=C.c_void_p
base=k.LoadLibraryExW(str(a.edf_dll.resolve()),None,1)
assert base
derive=C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_uint32)(base+0x776790)
parent=C.create_string_buffer(0x48)
for i in range(0x20):parent[8+i]=bytes([i+1])
before=bytes(parent)
seen=set();checks=0
for ordinal in (0,1,65535,65536,0x7FFFFFFF,0xE0000001,0xFFFFFFFF):
    out=C.create_string_buffer(32);again=C.create_string_buffer(32)
    derive(C.addressof(out),C.addressof(parent),ordinal)
    derive(C.addressof(again),C.addressof(parent),ordinal)
    assert bytes(out)==bytes(again) and bytes(parent)==before
    assert C.c_uint32.from_buffer(out,4).value==ordinal
    assert C.c_uint32.from_buffer(out,12).value==5
    assert bytes(out) not in seen
    seen.add(bytes(out));checks+=1
print('PASS native child IDs: deterministic, full uint32 ordinals, distinct sample IDs, parent unchanged')
padding=C.create_string_buffer(bytes([0xA5])*32,32)
derive(C.addressof(padding),C.addressof(parent),1)
assert bytes(padding)[20:24]==bytes([0xA5])*4, 'native padding contract changed'
checks+=1
empty=C.create_string_buffer(32)
derive(C.addressof(empty),None,1)
assert bytes(empty)==bytes(32);checks+=1

# Execute native 781950 with stand-in manager services and lifetime control blocks.
# Counts stay above zero: no game allocator/destructor or unresolved import executes.
k.VirtualProtect.argtypes=[C.c_void_p,C.c_size_t,C.c_uint,C.POINTER(C.c_uint)]
k.FlushInstructionCache.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t]
k.GetCurrentProcess.restype=C.c_void_p
callbacks=[]
def patch_stub(rva,signature,fn):
    cb=signature(fn);callbacks.append(cb)
    data=b'\x48\xB8'+C.cast(cb,C.c_void_p).value.to_bytes(8,'little')+b'\xFF\xE0'
    old=C.c_uint();assert k.VirtualProtect(base+rva,len(data),0x40,C.byref(old))
    C.memmove(base+rva,data,len(data))
    assert k.FlushInstructionCache(k.GetCurrentProcess(),base+rva,len(data))
obj=C.create_string_buffer(0x200);ctrl=C.create_string_buffer(16)
network_ctrl=C.create_string_buffer(16);entry=C.create_string_buffer(0x48)
weak=C.create_string_buffer(16);is_host=True
def word(at,value):C.c_uint32.from_address(at).value=value
def pointer(at,value):C.c_void_p.from_address(at).value=value
oa=C.addressof(obj);ca=C.addressof(ctrl);na=C.addressof(network_ctrl);ea=C.addressof(entry)
pointer(C.addressof(weak),oa);pointer(C.addressof(weak)+8,ca)
pointer(na+8,ea)
P2=C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p)
V2=C.WINFUNCTYPE(None,C.c_void_p,C.c_void_p)
def get_interface(out,arg):
    pointer(out,oa+0x120);pointer(out+8,ca)
    word(ca+8,C.c_uint32.from_address(ca+8).value+1)
    return out
def format_id(_id,out):
    C.memset(out,0,32);pointer(out+24,7);return out
def store_id(iface,_id):
    C.memmove(ea+8,_id,20);C.memmove(ea+8+24,_id+24,8);pointer(iface+16,na)
def insert(_mgr,out,arg,_unused,remote,_known):
    pointer(out,0);pointer(out+8,0)
    word(oa+0x128,1 if remote else 2)
    ctl=C.c_void_p.from_address(arg+8).value
    word(ctl+12,C.c_uint32.from_address(ctl+12).value-1)
    return out
patch_stub(0x22FCA0,P2,get_interface)
patch_stub(0x777470,P2,format_id)
patch_stub(0x50F00,V2,lambda _fmt,_text:None)
patch_stub(0x774BD0,V2,store_id)
patch_stub(0x784210,C.WINFUNCTYPE(C.c_bool,C.c_void_p),lambda _mgr:is_host)
patch_stub(0x782720,C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p,C.c_bool,C.c_bool),insert)
register=C.WINFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p)(base+0x781950)
for is_host in (True,False):
    word(ca+8,4);word(ca+12,10)
    word(na,4);word(na+4,4)
    C.memset(ea+28,0xA5,4) # Native ID-store leaves its padding unspecified.
    result=register(1,C.addressof(weak),C.addressof(out))
    assert result==ea+8 and C.string_at(result,20)==bytes(out)[:20] and C.string_at(result+24,8)==bytes(out)[24:]
    assert C.c_uint32.from_address(oa+0x128).value==(2 if is_host else 1)
    assert C.c_uint32.from_address(ca+8).value==4
    assert C.c_uint32.from_address(ca+12).value==9, 'exactly one input weak ref consumed'
    assert C.c_uint32.from_address(na).value==4 and C.c_uint32.from_address(na+4).value==4
    checks+=1
    print('PASS native registration:', 'host' if is_host else 'client', 'owner, returned ID, balanced temporary refs, one consumed weak')

g=Game(str(a.edf_dll.parent))
# Every weapon template support_soldier.cpp kBodies creates (support_call.h SupportWeapon order): one class, model, CAS.
import re
source=(Path(__file__).resolve().parents[1]/'src'/'support_soldier.cpp').read_text(encoding='utf-8')
bodies=re.findall(r'L"app:/object/(N601_COMMON_RANGER_\w+)\.sgo"',source)
assert len(bodies)==10,bodies
weapons={'AF':'AiSoldierRifle01','FL':'AiSoldierFlameThrower01','RL':'AiSoldierRocketLauncher01','SG':'AiSoldierShotgun01',
         'SN':'AiSoldierSniperRifle02'}
for body in bodies:
    name=body+'.SGO'
    r=dsgo.to_py(dsgo.parse(g.read('OBJECT',name)).root)
    assert r['xgs_scene_object_class']=='AssultSoldier'
    assert r['soldier_load_weapon']==['app:/weapon/%s.sgo'%weapons[body.split('_')[3]]],(name,r['soldier_load_weapon'])
    assert r['animation_model'][1]=='app:/Object/EDF6ArmySoldier.cas'
    assert r['soldier_weapon_slot'] and r['soldier_config']
    for path in (r['animation_model'][0][0],r['animation_model'][1],r['soldier_load_weapon'][0]):
        folder,file=path.removeprefix('app:/').split('/',1)
        assert g.read(folder,file)
    checks+=1
    print('PASS real Root template',name,': AssultSoldier, model/CAS/rifle dependencies present')
print(checks,'native/resource checks passed; complete game constructor and live co-op remain unverified')
