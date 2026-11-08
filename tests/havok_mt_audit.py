"""hknp 物理多线程（src/havok_mt_logic.h、docs/havok-mt-re.md）对真 EDF.dll 的静态审计（按文件读，不加载）：
1. 头文件里三个调用点的原字节、全部指纹在 DLL 里逐字节一致；三个 vtable 槽（pool 4 / 8 / 10，队列子对象 13）指向
   头文件说的函数；三个 vtable 的 RTTI 名是 hkCpuThreadPool / hkDefaultTaskQueue；
2. 调用点唯一：整个 .text 里以 rel32（call / jmp / lea / mov 等任何 rip 相对引用）指向 0x118F270 的只有 0x119932F，
   指向 0x118F0A0 的只有 0x70E38E、0x70FEC6；整个映像里没有这两个函数的绝对地址（函数指针 / vtable）；
3. 复位 0x118F0A0 在 0x118F121 / 0x118F138 释放 pool 与 queue 之前没有任何间接调用（没有 close），之后用 0xA13540 /
   0x130F380 新建两者；正常析构 0x118EC70 在释放 pool（0x118ED07）之前经 vt+0x68 close 了队列；
   setNumThreads 0x130FF70 的顺序是 [pool+0x140] 子对象 vt+0x68 close → pool vt+0x40 → ... → vt+0x70 reopen →
   pool vt+0x20(pool, queue)，插件照抄的就是它的前两步与最后一步；
4. 另外建 pool / queue 的 0x118F2E0 只由持有者构造 0x118EB10 调用，0x118F8B0 / 0x118F930 没有任何引用；
   持有者全局 0x20B30A0 只在 0x11A3575 / 0x11A359D / 0x11A38FF 被写；
5. 0x119932F 在同一个 System::update 0x1198DA0 里两次步进（0x1199359、0x1199479 调 0x11AD780）之前；
   System::update 里唯一的临界区（0x1199006 的 scoped lock 0x50BC0）在 0x119908B 由 0x50BE0 释放，0x50BE0 是
   LeaveCriticalSection，0x119908B..0x1199479 之间没有再加锁：步进期间游戏线程不持有会被 worker 上的回调重入的临界区；
6. 三个世界级任务（PostCollide sim+0x6A8 / PreSolve +0x6B8 / PostSolve +0x6D0，在 0xE7D0C0 创建）都写了
   [task+0x10]=1；collide 图（0xE7E570）里新建的 18 个任务全都是 PostCollide（sim+0x6A8，句柄 +0x4D6）的祖先，
   PreSolve（+0x4D8）以 +0x4D6 为前驱，PostSolve（+0x4E0）以 +0x4DC 为前驱、+0x4DC 以 +0x4DA 为前驱；
7. hknpUFMShapeTagCodec（vt 0x1AE3388）的 decode 0x11B8800 只写 out 参数（[r9]）；
反向：把复位调用点表换成假地址、把 0x118F270 的期望改成别的，检查必须失败。
EDF.dll 路径作参数，否则用 gamedir 找；没有 EDF.dll 或 capstone 时退出码 77（跳过）。"""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))

path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if not path:
    try:
        import gamedir
        path = os.path.join(gamedir.find_or_dev(), 'EDF.dll')
    except Exception:
        path = None
if not path or not os.path.isfile(path):
    print('SKIP: no EDF.dll')
    raise SystemExit(77)
try:
    import capstone
    import numpy as np
    import pefile
except ImportError as e:
    print(f'SKIP: {e.name} not installed')
    raise SystemExit(77)

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the sites are for')
    raise SystemExit(77)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
IMG = bytes(pe.get_memory_mapped_image())
BASE = pe.OPTIONAL_HEADER.ImageBase
TEXT = next((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Name.startswith(b'.text'))
IAT = {imp.address - BASE: (imp.name or b'?').decode() for e in pe.DIRECTORY_ENTRY_IMPORT for imp in e.imports}
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.skipdata = True

failures = 0
checks = 0


def check(ok: bool, what: str) -> bool:
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', what)
    return ok


def q(rva: int) -> int:
    return struct.unpack_from('<Q', IMG, rva)[0]


def dis(a: int, b: int):
    return list(md.disasm(IMG[a:b], a))


def call_target(ins) -> int | None:
    if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
        return int(ins.op_str, 16)
    return None


# ---- 头文件
logic = open(os.path.join(ROOT, 'src', 'havok_mt_logic.h'), encoding='utf-8').read()
CONST = {k: int(v, 16) for k, v in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)', logic)}
CONST.update({k: int(v) for k, v in re.findall(r'\b(k\w+)=(\d+)[,;]', logic)})


def val(tok: str) -> int:
    return CONST[tok] if tok.startswith('k') else int(tok, 0)


def blist(t: str) -> bytes:
    return bytes(int(x, 0) for x in t.replace('\n', ' ').split(',') if x.strip())


SITES = [(int(c, 16), int(s, 16), val(t), blist(b))
         for c, s, t, b in re.findall(r'\{(0x[0-9A-F]+),(0x[0-9A-F]+),(k\w+|0x[0-9A-F]+),\s*\{([^}]*)\}\}', logic)]
FPS = [(val(r), int(n), blist(b)) for r, n, b in re.findall(r'\{(k\w+|0x[0-9A-F]+),(\d+),\{([^}]*)\}\}', logic)]
check(len(SITES) == 3, f'3 call sites in the header ({len(SITES)})')
check(len(FPS) == 6, f'6 fingerprints in the header ({len(FPS)})')

# ---- 1. 字节、vtable 槽、RTTI
for ctx, call, target, stock in SITES:
    check(IMG[ctx:ctx + len(stock)] == stock, f'{ctx:#x}: stock bytes')
    rel = struct.unpack_from('<i', IMG, call + 1)[0]
    check(IMG[call] == 0xE8 and call + 5 + rel == target, f'{call:#x}: call {target:#x}')
for rva, n, b in FPS:
    check(len(b) == n and IMG[rva:rva + n] == b, f'{rva:#x}: fingerprint ({n} bytes)')
SLOTS = [(CONST['kPoolVtable'], CONST['kPoolProcessSlot'], CONST['kPoolProcess']),
         (CONST['kPoolVtable'], CONST['kPoolWaitSlot'], CONST['kPoolWait']),
         (CONST['kPoolVtable'], CONST['kPoolSetThreadsSlot'], CONST['kSetNumThreads']),
         (CONST['kQueueSubVtable'], CONST['kQueueCloseSlot'], CONST['kQueueClose'])]
for vt, slot, fn in SLOTS:
    check(q(vt + 8 * slot) - BASE == fn, f'vtable {vt:#x} slot {slot} = {fn:#x} ({q(vt + 8 * slot) - BASE:#x})')


def rtti(vt: int) -> str:
    col = q(vt - 8) - BASE
    td = struct.unpack_from('<I', IMG, col + 12)[0]
    return IMG[td + 16:td + 120].split(b'\0')[0].decode()


check(rtti(CONST['kPoolVtable']) == '.?AVhkCpuThreadPool@@', f'pool vtable RTTI {rtti(CONST["kPoolVtable"])}')
check(rtti(CONST['kQueueVtable']) == '.?AVhkDefaultTaskQueue@@', f'queue vtable RTTI {rtti(CONST["kQueueVtable"])}')
check(CONST['kQueueSubVtable'] == CONST['kQueueVtable'] + 0x30, 'the sub-object vtable follows the queue vtable')
check(CONST['kWorkerJob'] == 0x13102A0 and IMG[0x13102A0:0x13102AF] == bytes.fromhex('488b41184883c11848ffa080000000'),
      '0x13102A0: the worker job jumps to the queue sub-object vt+0x80 (its process loop)')

# ---- 2. 引用唯一
lo, hi = TEXT
arr = np.frombuffer(IMG, dtype=np.uint8)
V = (arr[lo:hi].astype(np.int64) | (arr[lo + 1:hi + 1].astype(np.int64) << 8) |
     (arr[lo + 2:hi + 2].astype(np.int64) << 16) | (arr[lo + 3:hi + 3].astype(np.int64) << 24))
V = np.where(V >= 2 ** 31, V - 2 ** 32, V)
POS = np.arange(lo, hi, dtype=np.int64)


def rel_refs(target: int) -> set[int]:
    """每条以 rel32 / rip 相对位移指到 target 的指令地址（候选位移再按指令边界解码确认）。"""
    found = set()
    for k in range(5):   # 位移后面可能跟 0..4 字节立即数
        for h in np.nonzero(V + POS + 4 + k == target)[0]:
            p = int(POS[h])
            for back in range(1, 12):
                s = p - back
                ins = next(iter(capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64).disasm(IMG[s:s + 16], s)), None)
                if ins and ins.address + ins.size == p + 4 + k and s < p:
                    if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x') and int(ins.op_str, 16) == target:
                        found.add(s)
                    elif 'rip +' in ins.op_str or 'rip -' in ins.op_str:
                        found.add(s)
    return found


def abs_refs(target: int) -> list[int]:
    t = struct.pack('<Q', BASE + target)
    out, i = [], IMG.find(t)
    while i >= 0:
        out.append(i)
        i = IMG.find(t, i + 1)
    return out


def callers_ok(target: int, want: set[int]) -> bool:
    got = rel_refs(target)
    ok = check(got == want, f'rel32 references of {target:#x}: {sorted(map(hex, got))}, expected {sorted(map(hex, want))}')
    return check(not abs_refs(target), f'no absolute pointer to {target:#x}') and ok


step_sites = {s for c, s, t, b in SITES if t == CONST['kStepNop']}
reset_sites = {s for c, s, t, b in SITES if t == CONST['kReset']}
callers_ok(CONST['kStepNop'], step_sites)
callers_ok(CONST['kReset'], reset_sites)
# 反向：表里少一个调用点或多一个假的，比较必须不等
check(rel_refs(CONST['kReset']) != reset_sites - {0x70FEC6}, 'reverse: a missing reset caller is caught')
check(IMG[CONST['kStepNop']:CONST['kStepNop'] + 3] != b'\xC3\xCC\xCC', 'reverse: another ret form would not match the fingerprint')

# ---- 3. 复位 / 析构 / setNumThreads 的顺序
reset = dis(0x118F0A0, 0x118F270)
frees = [i.address for i in reset if call_target(i) == 0x978FB0]
indirect_before = [i.address for i in reset if i.mnemonic == 'call' and not i.op_str.startswith('0x') and i.address < 0x118F121]
check(frees[:2] == [0x118F121, 0x118F138], f'reset frees pool then queue first: {list(map(hex, frees[:2]))}')
check(all(a > 0x118F110 or a in (0x118F0F7, 0x118F10C) for a in indirect_before),
      f'reset: before freeing the pool only the +0x10 member\'s own release calls ({list(map(hex, indirect_before))})')
check(not any('0x68]' in i.op_str for i in reset if i.mnemonic == 'call'), 'reset never calls vt+0x68 (no close)')
ctors = [call_target(i) for i in reset if call_target(i) in (0xA13540, 0x130F380)]
check(ctors == [0xA13540, 0x130F380], f'reset makes a new queue then a new pool: {list(map(hex, ctors))}')
dtor = dis(0x118EC70, 0x118ED40)
close_at = next((i.address for i in dtor if i.mnemonic == 'call' and i.op_str == 'qword ptr [rax + 0x68]'), None)
pool_free = next((i.address for i in dtor if call_target(i) == 0x978FB0 and i.address > 0x118ECF0), None)
check(close_at == 0x118ECB8 and pool_free == 0x118ED07 and close_at < pool_free,
      f'destructor closes ({close_at and hex(close_at)}) before freeing the pool ({pool_free and hex(pool_free)})')
snt = dis(CONST['kSetNumThreads'], 0x1310056)
seq = [i.op_str for i in snt if i.mnemonic == 'call' and i.op_str.startswith('qword ptr [rax')]
check(seq[:2] == ['qword ptr [rax + 0x68]', 'qword ptr [rax + 0x40]'] and seq[-2:] == ['qword ptr [rax + 0x70]', 'qword ptr [rax + 0x20]'],
      f'setNumThreads: close, wait ... reopen, processTaskQueue ({seq})')
check(any(i.op_str == 'rbp, qword ptr [rcx + 0x140]' for i in snt[:5]), 'setNumThreads takes the queue from [pool+0x140]')
check(any(i.op_str == 'rdx, rbp' for i in snt), 'and hands processTaskQueue the full queue pointer')

# ---- 4. 其它建 pool / queue 的地方与持有者全局
for fn, want in ((0x118F2E0, {0x118EB67}), (0x118F8B0, set()), (0x118F930, set())):
    got = {a for a in rel_refs(fn)}
    check(got == want and not abs_refs(fn), f'{fn:#x} referenced from {sorted(map(hex, got))}, expected {sorted(map(hex, want))}')
holder_writes = set()
for a in rel_refs(CONST['kHolderGlobal']):
    ins = next(iter(md.disasm(IMG[a:a + 16], a)))
    if ins.mnemonic == 'mov' and ins.op_str.startswith('qword ptr [rip'):
        holder_writes.add(a)
check(holder_writes == {0x11A3575, 0x11A359D, 0x11A38FF}, f'holder global written at {sorted(map(hex, holder_writes))}')

# ---- 5. 步进前、无锁
upd = dis(0x1198DA0, 0x119979B)
steps = [i.address for i in upd if call_target(i) == 0x11AD780]
check(steps == [0x1199359, 0x1199479], f'two steps: {list(map(hex, steps))}')
check(all(s > min(step_sites) for s in steps), 'the attach call comes before both steps')
locks = [i.address for i in upd if call_target(i) == 0x50BC0]
unlocks = [i.address for i in upd if call_target(i) == 0x50BE0]
imports = [IAT.get(i.address + i.size + int(i.op_str.split('rip + ')[1].rstrip(']'), 16))
           for i in upd if i.mnemonic in ('call', 'jmp') and 'rip + ' in i.op_str]
check(locks == [0x1199006] and 0x119908B in unlocks, f'System::update locks at {list(map(hex, locks))}, unlocks at {list(map(hex, unlocks))}')
check(not any(n and ('Critical' in n or 'SRW' in n or 'Mutex' in n) for n in imports), f'no lock import in System::update ({imports})')
leave = dis(0x50BE0, 0x50C04)
check(any(i.mnemonic == 'call' and 'rip + ' in i.op_str and
          IAT.get(i.address + i.size + int(i.op_str.split('rip + ')[1].rstrip(']'), 16)) == 'LeaveCriticalSection' for i in leave),
      '0x50BE0 is LeaveCriticalSection')
enter = dis(0x50BC0, 0x50BDE)
check(any(i.mnemonic == 'call' and 'rip + ' in i.op_str and
          IAT.get(i.address + i.size + int(i.op_str.split('rip + ')[1].rstrip(']'), 16)) == 'EnterCriticalSection' for i in enter),
      '0x50BC0 is EnterCriticalSection')

# ---- 6. 世界级任务与任务图
for at, vt in ((0xE7D23B, 0x18C1D08), (0xE7D29D, 0x18C1CA0), (0xE7D2FF, 0x18C21C0)):
    check(IMG[at:at + 8] == bytes.fromhex('48c7401001000000'), f'{at:#x}: [task+0x10]=1')
    lea = next((i for i in dis(at - 0x30, at) if i.mnemonic == 'lea' and i.op_str.startswith('rcx, [rip + ')), None)
    check(lea is not None and lea.address + lea.size + int(lea.op_str.split('rip + ')[1].rstrip(']'), 16) == vt,
          f'{at:#x}: the task made there has vtable {vt:#x}')

ADD_TASK = {0x9EC400, 0x9EC4F0, 0x9EC630, 0x9EC6E0}
SUB = {'eax': 'rax', 'ax': 'rax', 'ebx': 'rbx', 'bx': 'rbx', 'ecx': 'rcx', 'cx': 'rcx', 'edx': 'rdx', 'dx': 'rdx',
       'esi': 'rsi', 'si': 'rsi', 'edi': 'rdi', 'di': 'rdi'}


def reg(s: str):
    s = s.strip()
    if s in SUB:
        return SUB[s]
    m = re.fullmatch(r'(r\d+)[dwb]?', s)
    if m:
        return m.group(1)
    return s if s in ('rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi') else None


def slot(m: str):
    mm = re.fullmatch(r'(?:word|dword|qword)ptr\[(\w+)([+-]0x[0-9a-f]+)?\]', m.replace(' ', ''))
    if not mm:
        return None
    base = mm.group(1)
    return ('S' if base in ('rbp', 'rsp') else 'G', base if base in ('rbp', 'rsp') else '', int(mm.group(2) or '0', 16))


def graph(a: int, b: int):
    """线性走一遍任务图构造函数：addTask（0x9EC400 一族）的句柄经寄存器 / 栈槽 / 对象字段流到 graph+0x328 的
    word 对数组（[rax+rcx*4] = 前驱，+2 = 后继）。返回（新任务，边，对象字段里存的句柄）。"""
    st, mem, out, tasks, pend = {}, {}, [], [], None
    for ins in md.disasm(IMG[a:b], a):
        m, o = ins.mnemonic, ins.op_str
        ops = [x.strip() for x in o.split(',', 1)] if o else []
        if m == 'lea' and len(ops) == 2 and reg(ops[0]) == 'rdx':
            pend = slot(ops[1].replace('[', 'word ptr [', 1))
            st['rdx'] = ('LEA', ops[1])
            continue
        if m == 'call':
            t = int(o, 16) if o.startswith('0x') else None
            if t in ADD_TASK:
                name = f'T{ins.address:x}'
                tasks.append(name)
                st['rax'] = ('PTR', name)
                if pend:
                    mem[pend] = name
            else:
                st['rax'] = None
            pend = None
            for r in ('rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'):
                st[r] = None
            continue
        if m in ('mov', 'movzx') and len(ops) == 2:
            dst, src = ops
            dr, sr = reg(dst), reg(src)
            mm = re.fullmatch(r'word ptr \[(\w+)\]', src)
            if dr and mm and isinstance(st.get(reg(mm.group(1)) or ''), tuple) and st[reg(mm.group(1))][0] == 'PTR':
                st[dr] = st[reg(mm.group(1))][1]
                continue
            if dr and 'ptr [' in src:
                k = slot(src)
                st[dr] = mem.get(k, ('?', k)) if k else None
                continue
            mm = re.fullmatch(r'word ptr \[(\w+) \+ (\w+)\*4( \+ 2)?\]', dst)
            if mm and sr:
                out.append(('to' if mm.group(3) else 'from', st.get(sr), ins.address))
                continue
            mm = re.fullmatch(r'word ptr \[(\w+)\]', dst)
            if mm and sr and isinstance(st.get(reg(mm.group(1))), tuple) and st[reg(mm.group(1))][0] == 'LEA' \
                    and '*4' in st[reg(mm.group(1))][1]:
                out.append(('to' if '+ 2' in st[reg(mm.group(1))][1] else 'from', st.get(sr), ins.address))
                continue
            if 'word ptr [' in dst and sr:
                k = slot(dst)
                if k:
                    mem[k] = st.get(sr)
                continue
            if dr and sr:
                st[dr] = st.get(sr)
                continue
            if dr:
                st[dr] = None
            continue
        if m == 'lea' and len(ops) == 2 and reg(ops[0]):
            st[reg(ops[0])] = ('LEA', ops[1])
            continue
        if ops and reg(ops[0]) and m not in ('cmp', 'test') and not m.startswith('cmov'):
            st[reg(ops[0])] = None
    edges, cur = [], {}
    for kind, v, at in out:
        cur[kind] = v
        if 'from' in cur and 'to' in cur:
            edges.append((cur['from'], cur['to']))
            cur = {}
    return tasks, edges, mem


def handle(mem, off, base=None):
    return mem.get(('S', base, off) if base else ('G', '', off))


def ancestors(edges, node):
    preds = {}
    for f, t in edges:
        preds.setdefault(t, set()).add(f)
    seen, stack = set(), [node]
    while stack:
        for p in preds.get(stack.pop(), ()):
            if p not in seen:
                seen.add(p)
                stack.append(p)
    return seen


tasks, edges, mem = graph(0xE7E570, 0xE7FD7F)
post_collide = handle(mem, 0x4D6)
check(len(tasks) == 18 and post_collide == 'Te7fc52', f'collide graph: {len(tasks)} tasks, PostCollide {post_collide}')
check(IMG[0xE7FC37:0xE7FC3E] == bytes.fromhex('498b86a8060000'), 'the PostCollide node is the task at sim+0x6A8')
unresolved = [e for e in edges if not all(isinstance(x, str) or (isinstance(x, tuple) and x[0] == '?' and x[1][0] == 'G') for x in e)]
check(not unresolved, f'every collide edge resolved: {unresolved}')
anc = ancestors(edges, post_collide)
loose = [t for t in tasks if t != post_collide and t not in anc]
check(not loose, f'every collide task precedes PostCollide (not ordered: {loose})')
# 反向：去掉 f79d -> PostCollide 这条边，就有任务不在 PostCollide 之前
check(set(tasks) - {post_collide} - ancestors([e for e in edges if e != ('Te7f79d', post_collide)], post_collide),
      'reverse: dropping one edge leaves tasks unordered')
_, sedges, smem = graph(0xE82390, 0xE82DF3)
# 这里 rbp 是任务图对象（0xE82390 不用 rbp 作栈帧），句柄存在 [rbp+0x4D8] 等处
pre_solve, post_solve, h4dc = handle(smem, 0x4D8, 'rbp'), handle(smem, 0x4E0, 'rbp'), handle(smem, 0x4DC, 'rbp')
check(IMG[0xE82476:0xE8247D] == bytes.fromhex('66899dd8040000') and not any(
    i.mnemonic == 'mov' and i.op_str.startswith('rbp, rsp') for i in dis(0xE82390, 0xE823C0)), 'rbp is the graph in 0xE82390')
check((('?', ('S', 'rbp', 0x4D6)), pre_solve) in sedges, f'PreSolve {pre_solve} after PostCollide (+0x4D6)')
check((h4dc, post_solve) in sedges and h4dc == 'Te82cad', f'PostSolve {post_solve} after +0x4DC {h4dc}')
check(('Te825bc', h4dc) in sedges, '+0x4DC after +0x4DA (0xE825BC)')

# ---- 7. shape tag codec
vt_codec = [q(0x1AE3388 + 8 * i) - BASE for i in range(5)]
check(vt_codec[4] == 0x11B8800 and vt_codec[3] == 0xDF9DE0 and IMG[0xDF9DE0:0xDF9DE3] == b'\xC2\x00\x00',
      f'codec vtable {list(map(hex, vt_codec))}')
writes = [i for i in dis(0x11B8800, 0x11B885F) if i.mnemonic == 'mov' and i.op_str.split(',')[0].strip().endswith(']')]
check(writes and all('[r9' in i.op_str.split(',')[0] for i in writes), f'codec decode writes only [r9...]: {[i.op_str for i in writes]}')

print(f'havok_mt_audit: {checks} checks, {failures} failures')
raise SystemExit(1 if failures else 0)
