import sys,struct,os
sys.path.insert(0,r'D:\APP\edf-coop-stable-multislot\multislot\tools')
import cpk,sgo
c=cpk.Cpk(r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6\Root.cpk')
out=r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6\Mods\WEAPON'
import crilayla
def read(d,n):
    e=c.index[(d,n)]
    with open(c.path,'rb') as h:
        h.seek(c.base+int(e['FileOffset'])); data=h.read(int(e['FileSize']))
    return crilayla.decompress(data) if int(e['ExtractSize'])!=int(e['FileSize']) else data
def top_member_node(buf,key):
    table,count,size=struct.unpack_from('<III',buf,4)
    raw,typ=struct.unpack_from('<QI',buf,table)
    assert typ==3
    d=table+raw
    name_off,name_count,idx_off,n=struct.unpack_from('<IIII',buf,d)
    idx=struct.unpack_from(f'<{n}I',buf,d+idx_off)
    for k in range(name_count):
        e=d+name_off+k*8; so,member=struct.unpack_from('<II',buf,e)
        if sgo._utf16(buf,e+so)==key: return table+idx[member]*size
for d,n in c.names('WEAPON'):
    if not n.upper().startswith('EWEAPON'): continue
    data=read(d,n)
    if 'retrobalam' not in data.decode('utf-16-le','ignore') and b'retrobalam' not in data: continue
    pos=top_member_node(data,'ReloadInit')
    raw,typ=struct.unpack_from('<QI',data,pos); assert typ==0,typ
    old=struct.unpack_from('<d',data,pos)[0]
    buf=bytearray(data); struct.pack_into('<d',buf,pos,1.0)
    v=sgo.load(data=bytes(buf))
    print(n,v.get('name.cn'),'ReloadInit',old,'->',v['ReloadInit'],'| ReloadTime',v['ReloadTime'][0])
    os.makedirs(out,exist_ok=True)
    open(os.path.join(out,n),'wb').write(buf)
