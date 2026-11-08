"""Summarise tools/rip_sampler.cpp output: CPU by thread, self / inclusive by module and by EDF.dll function (its .pdata).

  python tools/rip_analyze.py PROFILE.txt [TOP] [EDF.dll]     (EDF.dll: default the game pylib/gamedir.py finds)
"""
import bisect, collections, os, sys
import pefile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'pylib'))
prof = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
if len(sys.argv) > 3:
    dll = sys.argv[3]
else:
    import gamedir
    dll = os.path.join(gamedir.find_or_dev(), 'EDF.dll')
pe = pefile.PE(dll, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
pdata = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
starts = [b for b, _ in pdata]
IMAGE = pe.OPTIONAL_HEADER.ImageBase


def fn(frame):
    mod, off = frame.rsplit('+', 1)
    off = int(off, 16)
    if mod.lower() == 'edf.dll':
        i = bisect.bisect_right(starts, off) - 1
        if i >= 0 and pdata[i][0] <= off < pdata[i][1]:
            return f'EDF.dll!{IMAGE + pdata[i][0]:#x}'
        return f'EDF.dll!?{IMAGE + off:#x}'
    return mod


thread_cpu = collections.Counter(); thread_n = collections.Counter()
self_mod = collections.Counter(); self_fn = collections.Counter(); incl_fn = collections.Counter()
callers = collections.defaultdict(collections.Counter)
mem = []
n = 0
for line in open(prof, encoding='utf-8', errors='replace'):
    p = line.split()
    if not p:
        continue
    if p[0] == 'M':
        mem.append(line.strip()); continue
    if p[0] != 'T' or len(p) < 4:
        continue
    tid, cpu, frames = p[1], int(p[2]), p[3:]
    thread_cpu[tid] += cpu; thread_n[tid] += 1; n += 1
    leaf = frames[0]
    self_mod[leaf.rsplit('+', 1)[0]] += 1
    # self in the first EDF.dll frame (time inside system / driver code charged to the EDF function that called it)
    first_edf = next((fn(f) for f in frames if f.lower().startswith('edf.dll+')), None)
    if first_edf:
        self_fn[first_edf] += 1
    seen = set()
    names = [fn(f) for f in frames]
    for i, f in enumerate(names):
        if f not in seen:
            seen.add(f); incl_fn[f] += 1
    if first_edf:
        i = names.index(first_edf)
        if i + 1 < len(names):
            callers[first_edf][names[i + 1]] += 1

print('samples', n)
if not n:
    sys.exit('no samples')
print('--- threads (samples, cpu ms)')
for t, c in thread_n.most_common(12):
    print(f'  {t:>7} {c:6} {thread_cpu[t]:8}')
print('--- leaf module')
for m, c in self_mod.most_common(15):
    print(f'  {c/n*100:5.1f}% {m}')
print('--- first EDF.dll frame (self incl. callees outside EDF)')
for f, c in self_fn.most_common(top):
    cs = ', '.join(f'{k}:{v}' for k, v in callers[f].most_common(3))
    print(f'  {c/n*100:5.1f}% {f}   <- {cs}')
print('--- inclusive')
for f, c in incl_fn.most_common(top):
    print(f'  {c/n*100:5.1f}% {f}')
print('--- memory (first, max private, last)')
if mem:
    priv = lambda s: int(s.split('private=')[1].split()[0])
    print(' ', mem[0]); print(' ', max(mem, key=priv)); print(' ', mem[-1])
