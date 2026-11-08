"""hknp 物理多线程（src/havok_mt_logic.h）在 unicorn 下跑真 EDF.dll 的代码（按文件读，不加载）：
1. 三个调用点按插件的做法改写：rel32 指向近处一页里的 12 字节 thunk（mov rax,imm64; jmp rax，common/memory.cpp 的
   AllocateNearThunk），thunk 跳到桩函数；从调用点前的 mov rcx,[rip→0x20B30A0] 跑起，桩看到的 rcx 是持有者，
   返回后停在调用点的下一条指令、rsp 不变；
2. 接上：processTaskQueue 0x130FE20(pool, queue) 在假 pool（3 个 worker）上写 [pool+0x140]=queue、
   [queue+0x380]=3、[pool+0x128]=0x13102A0、[pool+0x130]=queue、[pool+0x155]=1，对每个 worker 的信号量
   ReleaseSemaphore 一次；
3. 复位前的 close + 等待：close 0xA15050(queue+0x18) 在锁内置 +0x348、对每个睡着的 worker 释放它的信号量；
   waitForCompletion 0x1310630(pool) 对 [pool+0x148] 等 3 次，清 +0x154 / +0x155 / +0x138 / +0x140 / [queue+0x380]；
   之后 pool 析构 0x130F670 不再等 worker；
反向：接上而不 close / 等待，pool 析构一上来就对 +0x148 等 3 次——真机上 worker 停在没关的队列里，永远等不到（死锁）。
WaitForSingleObject / ReleaseSemaphore 的导入槽指向记录参数的桩。
EDF.dll 路径作参数，否则用 gamedir 找；没有 EDF.dll 或 unicorn 时退出码 77（跳过）。"""
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
    import pefile
    import unicorn
    import unicorn.x86_const as U
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
IAT = {(imp.name or b'').decode(): imp.address - BASE for e in pe.DIRECTORY_ENTRY_IMPORT for imp in e.imports}

logic = open(os.path.join(ROOT, 'src', 'havok_mt_logic.h'), encoding='utf-8').read()
CONST = {k: int(v, 16) for k, v in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)', logic)}
SITES = [(int(c, 16), int(s, 16)) for c, s in re.findall(r'\{(0x[0-9A-F]+),(0x[0-9A-F]+),k\w+,\s*\{', logic)]

failures = 0
checks = 0


def check(ok: bool, what: str) -> bool:
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', what)
    return ok


IMAGE_SIZE = (len(IMG) + 0xFFFF) & ~0xFFFF
STACK, STACK_SIZE = 0x7000_0000, 0x10_0000
HEAP, HEAP_SIZE = 0x100_0000_0000, 0x10_0000
STUBS = 0x7100_0000            # 桩：每个 1 字节 ret，按地址认
NEAR = BASE - 0x1000_0000      # 近处 thunk 页（±2 GB 内）
END = STUBS + 0xFF0            # 顶层返回地址：跑到这里停


class Sim:
    def __init__(self):
        uc = self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
        uc.mem_map(BASE, IMAGE_SIZE)
        uc.mem_write(BASE, IMG)
        for at, size in ((STACK, STACK_SIZE), (HEAP, HEAP_SIZE), (STUBS, 0x1000), (NEAR, 0x1000)):
            uc.mem_map(at, size)
        uc.mem_write(STUBS, b'\xC3' * 0x1000)
        self.calls: list[tuple[str, int, int]] = []
        self.names: dict[int, str] = {END: 'end'}
        self.heap = HEAP
        uc.hook_add(unicorn.UC_HOOK_CODE, self.on_stub, begin=STUBS, end=STUBS + 0xFFF)
        self.stub('WaitForSingleObject', iat='WaitForSingleObject')
        self.stub('ReleaseSemaphore', iat='ReleaseSemaphore')

    def stub(self, name: str, iat: str | None = None) -> int:
        at = STUBS + 0x10 * (len(self.names) + 1)
        self.names[at] = name
        if iat:
            self.uc.mem_write(BASE + IAT[iat], struct.pack('<Q', at))
        return at

    def on_stub(self, uc, address, size, _):
        name = self.names.get(address)
        if name and name != 'end':
            self.calls.append((name, uc.reg_read(U.UC_X86_REG_RCX), uc.reg_read(U.UC_X86_REG_RDX)))
        if name == 'end':
            uc.emu_stop()

    def alloc(self, size: int) -> int:
        at = self.heap
        self.heap += (size + 0xF) & ~0xF
        self.uc.mem_write(at, b'\0' * size)
        return at

    def w64(self, at, v): self.uc.mem_write(at, struct.pack('<Q', v))
    def w32(self, at, v): self.uc.mem_write(at, struct.pack('<i', v))
    def w8(self, at, v): self.uc.mem_write(at, bytes([v]))
    def r64(self, at): return struct.unpack('<Q', self.uc.mem_read(at, 8))[0]
    def r32(self, at): return struct.unpack('<i', self.uc.mem_read(at, 4))[0]
    def r16(self, at): return struct.unpack('<H', self.uc.mem_read(at, 2))[0]
    def r8(self, at): return self.uc.mem_read(at, 1)[0]

    def call(self, rva: int, *args: int, until: int | None = None):
        rsp = STACK + STACK_SIZE - 0x1000
        self.w64(rsp, END)
        for reg, v in zip((U.UC_X86_REG_RCX, U.UC_X86_REG_RDX, U.UC_X86_REG_R8, U.UC_X86_REG_R9), args):
            self.uc.reg_write(reg, v)
        self.uc.reg_write(U.UC_X86_REG_RSP, rsp)
        self.calls.clear()
        self.uc.emu_start(BASE + rva, BASE + until if until else 0, count=200000)


# ---- 1. 改写后的调用点
for ctx, site in SITES:
    s = Sim()
    holder = s.alloc(0xB8)
    s.w64(BASE + CONST['kHolderGlobal'], holder)
    hook = s.stub('hook')
    thunk = NEAR + 0x100
    s.uc.mem_write(thunk, b'\x48\xB8' + struct.pack('<Q', hook) + b'\xFF\xE0')   # AllocateNearThunk 的 12 字节
    rel = thunk - (BASE + site + 5)
    check(-2 ** 31 <= rel < 2 ** 31, f'{site:#x}: thunk in rel32 reach')
    s.uc.mem_write(BASE + site, b'\xE8' + struct.pack('<i', rel))
    rsp0 = STACK + 0x8000
    s.uc.reg_write(U.UC_X86_REG_RSP, rsp0)
    s.calls.clear()
    s.uc.emu_start(BASE + ctx, BASE + site + 5, count=100)
    check(s.calls == [('hook', holder, s.calls[0][2] if s.calls else 0)], f'{site:#x}: the hook ran once with rcx = holder ({s.calls})')
    check(s.uc.reg_read(U.UC_X86_REG_RIP) == BASE + site + 5 and s.uc.reg_read(U.UC_X86_REG_RSP) == rsp0,
          f'{site:#x}: back at the next instruction, stack balanced')

# ---- 2/3. 接上、close + 等待、析构
N = 3
POOL_SEM, WORKER_SEM, SLEEP_SEM = 0x5000, 0x6000, 0x7000


def make(s: Sim):
    pool, queue = s.alloc(0x168), s.alloc(0x390)
    s.w64(pool, BASE + CONST['kPoolVtable'])
    s.w64(queue, BASE + CONST['kQueueVtable'])
    s.w64(queue + 0x18, BASE + CONST['kQueueSubVtable'])
    workers = s.alloc(8 * N)
    for i in range(N):
        w = s.alloc(0x40)
        s.w64(w + 0x20, WORKER_SEM + i)       # 0x9EAE70: mov rcx,[rcx] -> 句柄
        s.w64(workers + 8 * i, w)
    s.w64(pool + 0x18, workers)
    s.w32(pool + 0x20, N)
    s.w64(pool + 0x148, POOL_SEM)
    # 队列子对象：+8 锁（vt 槽 1 加锁、槽 2 解锁），+0x34 线程记录个数，+0x38.. 记录（+0x10 睡着，+0x150 信号量）
    sub = queue + 0x18
    lock, lvt = s.alloc(0x10), s.alloc(0x20)
    s.w64(lvt + 8, s.stub('lock'))
    s.w64(lvt + 0x10, s.stub('unlock'))
    s.w64(lock, lvt)
    s.w64(sub + 8, lock)
    s.w32(sub + 0x34, N)
    for i in range(N):
        rec = s.alloc(0x158)
        s.w8(rec + 0x10, 1)
        s.w64(rec + 0x150, SLEEP_SEM + i)
        s.w64(sub + 0x38 + 8 * i, rec)
    return pool, queue


s = Sim()
pool, queue = make(s)
s.call(CONST['kPoolProcess'], pool, queue)
check(s.r64(pool + 0x140) == queue and s.r64(pool + 0x130) == queue, 'processTaskQueue: [pool+0x140] / [pool+0x130] = queue')
check(s.r32(queue + 0x380) == N, f'[queue+0x380] = {s.r32(queue + 0x380)} workers')
check(s.r64(pool + 0x128) == BASE + CONST['kWorkerJob'], 'the worker job is 0x13102A0')
check(s.r8(pool + 0x155) == 1, '[pool+0x155] = 1')
wakes = [c[1] for c in s.calls if c[0] == 'ReleaseSemaphore']
check(sorted(wakes) == [WORKER_SEM + i for i in range(N)], f'every worker woken once: {list(map(hex, wakes))}')

# 反向：接上后不 close，原版复位释放 pool -> 析构对 +0x148 等 N 次（真机上永远等不到）
r = Sim()
rpool, rqueue = make(r)
r.call(CONST['kPoolProcess'], rpool, rqueue)
r.call(0x130F670, rpool, 1, until=0x130F6ED)
waits = [c[1] for c in r.calls if c[0] == 'WaitForSingleObject']
check(waits == [POOL_SEM] * N, f'reverse: the destructor of an attached pool waits {len(waits)} times for its workers')

# 插件复位前的顺序：close(queue+0x18) -> waitForCompletion(pool)
s.call(CONST['kQueueClose'], queue + 0x18)
names = [c[0] for c in s.calls]
check(names[0] == 'lock' and names[-1] == 'unlock', f'close runs under the queue lock ({names})')
check('kQueueClosed=kQueueSub+0x348' in logic and s.r8(queue + 0x18 + 0x348) == 1, 'close sets queue+0x18+0x348 (kQueueClosed)')
woken = [c[1] for c in s.calls if c[0] == 'ReleaseSemaphore']
check(sorted(woken) == [SLEEP_SEM + i for i in range(N)], f'close wakes every sleeping worker: {list(map(hex, woken))}')
s.call(CONST['kPoolWait'], pool)
waits = [c[1] for c in s.calls if c[0] == 'WaitForSingleObject']
check(waits == [POOL_SEM] * N, f'waitForCompletion waits once per worker on +0x148 ({len(waits)})')
check(s.r16(pool + 0x154) == 0 and s.r64(pool + 0x140) == 0 and s.r64(pool + 0x138) == 0 and s.r32(queue + 0x380) == 0,
      'waitForCompletion clears +0x154/+0x155, +0x138, +0x140 and [queue+0x380]')
s.call(0x130F670, pool, 1, until=0x130F6ED)
check(not [c for c in s.calls if c[0] == 'WaitForSingleObject'], 'after close + wait the destructor waits for no worker')
# 第二次 close 是空操作（析构 0x118EC70 自己还会再 close 一次时不出事）
s.call(CONST['kQueueClose'], queue + 0x18)
check([c[0] for c in s.calls] == ['lock', 'unlock'], f'a second close only takes the lock ({[c[0] for c in s.calls]})')

print(f'havok_mt_sim: {checks} checks, {failures} failures')
raise SystemExit(1 if failures else 0)
