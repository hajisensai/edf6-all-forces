import sys
from edfre import *
def refs(f,maxn=3000):
    calls=set();datas=set()
    for n,ins in enumerate(md.disasm(img[f:f+maxn*8],f)):
        if ins.mnemonic=='call' and ins.op_str.startswith('0x'): calls.add(int(ins.op_str,16))
        if 'rip' in ins.op_str:
            import re as _r
            m=_r.search(r'rip ([+-]) (0x[0-9a-f]+)',ins.op_str)
            if m:
                d=int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
                datas.add(ins.address+ins.size+d)
        if ins.mnemonic in('int3',) or n>maxn: break
    return calls,datas
def show(f):
    c,d=refs(f)
    print(f'== {f:#x} calls={len(c)}')
    for x in sorted(d):
        s=img[x:x+80]
        w=s.decode('utf-16le','ignore').split('\0')[0]
        a=s.split(b'\0')[0]
        if len(a)>=4 and all(32<=b<127 for b in a): print('  A',hex(x),a.decode())
        elif len(w)>=3 and all(32<=ord(ch)<0x9fff for ch in w): print('  W',hex(x),w)
for a in sys.argv[1:]: show(int(a,16))
