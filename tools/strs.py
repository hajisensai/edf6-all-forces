import re,sys
d=open(sys.argv[1],'rb').read()
pat=re.compile(sys.argv[2],re.I)
out=set()
for m in re.finditer(rb'[\x20-\x7e]{5,}',d):
    s=m.group().decode()
    if pat.search(s): out.add(('A',s))
for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}',d):
    s=m.group().decode('utf-16le')
    if pat.search(s): out.add(('W',s))
for k,s in sorted(out,key=lambda x:x[1]): print(k,s)
