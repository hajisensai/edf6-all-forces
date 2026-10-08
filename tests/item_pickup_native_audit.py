"""Private native EDF item-pickup ABI/overlap audit (Windows x64; explicit EDF.dll).

Executes original Collect, Notify, Broadcast, Apply, HP update, follower traversal,
position accessor and stream serializers. Only world services (audio, host query,
RTTI cast, final packet send) are bounded fixture adapters. Does not load DllMain,
attach to a game or modify installed files. This is not live multiplayer evidence.
"""
import argparse
import ctypes as C
from pathlib import Path
import struct
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('edf_dll', nargs='?', type=Path)
a = p.parse_args()
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or a.edf_dll is None or not a.edf_dll.is_file():
    print('SKIP: Windows x64 and an explicit supported EDF.dll are required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Native precondition assertions require Python without -O')
import pefile
pe = pefile.PE(str(a.edf_dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
base = k.LoadLibraryExW(str(a.edf_dll.resolve()), None, 1)
assert base
P, I, F = C.c_void_p, C.c_int32, C.c_float
keep = []
checks = 0

def buf(size):
    b = C.create_string_buffer(size)
    keep.append(b)
    return C.addressof(b)

def put(addr, fmt, *values):
    value = struct.pack(fmt, *values)
    C.memmove(addr, value, len(value))

def get(addr, fmt):
    return struct.unpack(fmt, C.string_at(addr, struct.calcsize(fmt)))[0]

def patch(addr, data):
    old = C.c_uint()
    assert k.VirtualProtect(addr, len(data), 0x40, C.byref(old))
    C.memmove(addr, data, len(data))
    restored = C.c_uint()
    assert k.VirtualProtect(addr, len(data), old.value, C.byref(restored))

def callback(rva, result, params, fn):
    cb = C.WINFUNCTYPE(result, *params)(fn)
    keep.append(cb)
    # Absolute jump touches only this process's private image pages.
    patch(base+rva, b'\x48\xb8'+struct.pack('<Q', C.cast(cb,P).value)+b'\xff\xe0')

# Check native entrypoints before supplying bounded world-service adapters.
for rva, sig in [(0x2C8AC0,'4c8bdc55415641574883ec60'),
                 (0x2C7540,'48895c24084889742410574883ec30'),
                 (0x2C7D50,'405355565741564881ec60060000'),
                 (0x2C7B50,'405356574881ec40060000'),
                 (0x547870,'80b9e802000000')]:
    assert C.string_at(base+rva,len(bytes.fromhex(sig))) == bytes.fromhex(sig), hex(rva)
state = {'host': False, 'sounds': 0, 'packets': [], 'cast': True, 'errors': []}

def sound(service, name, unused):
    if C.wstring_at(name) != 'アイテム取得':
        state['errors'].append('unexpected native sound name')
    state['sounds'] += 1

def send(service, stream, destination):
    n = get(stream+0x5F0, '<Q')
    if not 0 < n <= 32:
        state['errors'].append('unexpected native packet length')
        return
    state['packets'].append(C.string_at(stream+0x10,n))
    if get(destination,'<i') != -1:
        state['errors'].append('unexpected native packet destination')

callback(0x7B2860, None, [P,P,I], sound)
callback(0x784210, C.c_bool, [P], lambda _: state['host'])
callback(0x12DA7AA, P, [P,I,P,P,I], lambda obj,*_: obj if state['cast'] else 0)
callback(0x72EBD0, None, [P,P,P], send)
# Native security-cookie check is retained; initialize the loader-owned cookie privately.
# The cookie RVA is read directly from the original RIP-relative instruction.
cookie_rva = 0x2C7D65 + struct.unpack('<i',pe.get_data(0x2C7D61,4))[0]
patch(base+cookie_rva, struct.pack('<Q',0x12345678))
mode = buf(0x40)
slots, slot, descriptor, network = buf(8),buf(0x20),buf(0x80),buf(0x1600)
put(mode+0x20,'<Q',slots); put(slots,'<Q',slot); put(slot+0x10,'<Q',descriptor)
put(descriptor+8,'<i',0); put(descriptor+0x68,'<i',1)
patch(base+0x20B2890, struct.pack('<Q',mode))
patch(base+0x20B2AC8, struct.pack('<Q',network))
manager, player, player_info = buf(0xE60),buf(0x2100),buf(0x60)
put(player+0x340,'<Q',1);put(player+0x1ED0,'<Q',player_info);put(player_info+0x48,'<i',3)
put(player+0x2F4,'<f',1000)
# Empty follower list, retaining real recursive follower function.
fhead = buf(0x18);put(fhead,'<Q',fhead);put(player+0x550,'<Q',fhead)
head,n1,n2 = buf(0x20),buf(0x20),buf(0x20)
u1,u2 = buf(0xE0),buf(0xE0)
put(manager+0xDE0,'<Q',head);put(manager+0xDE8,'<Q',2)
put(head,'<Q',n1);put(n1,'<Q',n2);put(n2,'<Q',head)
put(n1+0x18,'<Q',u1);put(n2+0x18,'<Q',u2)
# Real RenderNode position accessor reaches this minimal matrix provider.
mat = buf(0x40);put(mat+0x30,'<4f',1,2,3,1)
mat_cb = C.WINFUNCTYPE(P,P,I)(lambda _obj,_index: mat);keep.append(mat_cb)
vt,component,owner,node = buf(0x88),buf(0x30),buf(0x60),buf(0x110)
put(vt+0x80,'<Q',C.cast(mat_cb,P).value);put(component+0x20,'<Q',vt)
put(owner+0x58,'<Q',component);put(node+0x100,'<Q',owner)
for unit,kind,uid in [(u1,0,7),(u2,2,8)]:
    put(unit,'<Q',base+0x17A6C18);put(unit+0xB0,'<Q',node)
    put(unit+0xC0,'<i',kind);put(unit+0xC8,'<i',uid);put(unit+0xD0,'<Q',manager)
functor,functor_vt = buf(16),buf(0x30)
false_cb=C.WINFUNCTYPE(C.c_bool,P)(lambda _:False);keep.append(false_cb)
put(functor,'<Q',functor_vt);put(functor_vt+0x28,'<Q',C.cast(false_cb,P).value)
collect=C.WINFUNCTYPE(None,P,P,P,F,F,P)(base+0x2C8AC0)
notify=C.WINFUNCTYPE(None,P,I,I,P)(base+0x2C7D50)
apply=C.WINFUNCTYPE(None,P,P,I,F)(base+0x2C7540)

def reset(online=False,host=False):
    put(mode+0x38,'<i',0 if online else -1)
    state.update(host=host,sounds=0,packets=[],cast=True,errors=[])
    for off in (0xE04,0xE08,0xE0C):put(manager+off,'<I',0)
    for unit in (u1,u2):put(unit+0xC4,'<B',0)
    put(player+0x128,'<I',0);put(player+0x2F8,'<f',400)

def check(ok,label):
    global checks
    assert not state['errors'], state['errors']
    assert ok,label
    checks+=1

reset()
collect(manager,player,mat+0x30,0.05,0.35,functor)
check(get(u1+0xC4,'<B')==get(u2+0xC4,'<B')==1,'tiny radius collects both overlapping boxes')
check(get(player+0x2F8,'<f')==550,'unselected health box was consumed and healed')
check(get(manager+0xE08,'<I')==1 and state['sounds']==2,'original sound/count control')
print('PASS negative control: original Collect(radius=.05) also consumes overlapping health box')
# This is the proposed single-unit operation: two original native entrypoints,
# then the same final mark as Collect. Production policy/ownership guards are separate.
for online,host in [(False,False),(True,True),(True,False)]:
 for kind in range(4):
    reset(online,host)
    put(u1+0xC0,'<i',kind)
    before=C.string_at(u1,0xE0)
    notify(manager,7,kind,player)
    apply(manager,player,kind,0.35)
    check(C.string_at(u1,0xE0)==before,'notify/apply leave unit alive and unmodified')
    put(u1+0xC4,'<B',1)
    check(get(u2+0xC4,'<B')==0,'exact operation preserves overlapping neighbor')
    hp=400+(150 if kind==2 else 300 if kind==3 else 0)
    check(get(player+0x2F8,'<f')==hp,'native health result')
    count=int(kind<2 and (not online or host))
    check(get(manager+0xE04,'<I')==count,'offline/host only weapon-armor counts')
    check(state['sounds']==1,'one native pickup audio request')
    expected=[] if not online else [bytes([2 if host else 1,kind,7]+([3] if host else []))]
    check(state['packets']==expected,'native serialized request/broadcast ABI')
print('PASS exact native path: kinds 0..3 across offline, host and client')
# Signed ID representation and host fallback when no Soldier RTTI match.
reset(True,False);notify(manager,-2147483647,1,player)
check(state['packets']==[bytes.fromhex('01016401000080')],'signed int32 ID encoded unchanged')
reset(True,True);state['cast']=False;notify(manager,7,0,player)
check(state['packets']==[bytes([1,0,7])],'host without Soldier cast uses request fallback')
reset(True,True);put(player+0x1ED0,'<Q',0);notify(manager,7,0,player)
check(state['packets']==[bytes([1,0,7])],'host without player info uses request fallback')
put(player+0x1ED0,'<Q',player_info)
# Native sound suppression on a remote replica, HP max clamp and dead guard.
reset();put(player+0x128,'<I',1);apply(manager,player,0,0)
check(state['sounds']==0,'remote replica suppresses pickup sound')
put(player+0x2F8,'<f',950);apply(manager,player,2,0)
check(get(player+0x2F8,'<f')==1000,'health clamps to max')
put(player+0x2E8,'<B',1);put(player+0x2F8,'<f',0);apply(manager,player,3,0)
check(get(player+0x2F8,'<f')==0,'dead player is not healed')
# The caller can intentionally suppress follower healing with xmm3=0.  Check a
# nonempty list against the real recursive traversal as a positive control.
follower,fnode,follower_head = buf(0x600),buf(0x18),buf(0x18)
put(fhead,'<Q',fnode);put(fnode,'<Q',fhead);put(fnode+0x10,'<Q',follower)
put(follower+0x550,'<Q',follower_head);put(follower_head,'<Q',follower_head)
put(follower+0x2F4,'<f',2000);put(follower+0x2F8,'<f',200)
put(player+0x2E8,'<B',0)
reset();apply(manager,player,2,0)
check(get(follower+0x2F8,'<f')==200,'zero teammate factor suppresses follower healing')
apply(manager,player,2,0.35)
check(abs(get(follower+0x2F8,'<f')-305)<1e-3,'native nonzero teammate factor heals follower')
print(f'{checks} native checks passed; no game process or installed-file changes')
print('LIMIT: policy helper, game world lifecycle and actual network transport are not exercised.')
