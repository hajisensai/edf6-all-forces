"""hknpEventDispatcher's lists run past 0xFFFF nodes, under unicorn: the real EDF.dll code (read as a file, never
loaded), patched with src/havok_props_logic.h's sites, makes a dispatcher (the derived constructor 0xE76640),
subscribes 3 event types on each of NB bodies and some global ones (getBodySignal 0xE76180 / getGlobalSignal
0xE76120: 0x10000+ nodes), removes half of the bodies (onBodyRemoved 0xE76460 -> freeChain 0xE76010), subscribes them
again from the free list, removes bodies while dispatching (the pending list +0x68, then freeChain), removes the rest
and destroys it (0xE76680). After each step every list is walked in the widened layout: every node in exactly one of
the free list, the global list and the body lists, no list closing on itself, each body's event types as made.
Every free has to name the size its block was allocated with (the element sizes 2 -> 4 of the destructors).
Reverse case: the stock code, walked in the stock layout, corrupts its lists past 0xFFFF nodes (the stress:100 hang).
The Havok heap, the hkArray realloc 0x97C890, the free 0x97B150 and the hkSignal helpers are Python stubs.
An EDF.dll path as the argument, else gamedir finds one; exit 77 (skipped) without one or without unicorn."""
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
STOCK = bytes(pe.get_memory_mapped_image())
BASE = pe.OPTIONAL_HEADER.ImageBase
logic = open(os.path.join(ROOT, 'src', 'havok_props_logic.h'), encoding='utf-8').read()
num = lambda t: int(t, 0)  # noqa: E731
SITES = [(num(r), int(n), bytes(num(b) for b in s.split(',')), bytes(num(b) for b in p.split(',')))
         for r, n, s, p in re.findall(r'\{(0x[0-9A-F]+),(\d),\{([^}]*)\},\{([^}]*)\}\}', logic)]
if len(SITES) != 137:
    print(f'FAIL: {len(SITES)} sites in the header')
    raise SystemExit(1)

CTOR_DERIVED, DTOR_BODY = 0xE76640, 0xE76680
GET_BODY, GET_GLOBAL, ON_REMOVED, FREE_CHAIN = 0xE76180, 0xE76120, 0xE76460, 0xE76010
ALLOCATOR_GLOBAL = 0x1F826E0           # the Havok heap allocator the dispatcher's reserves pass (0xE75C84, 0xE761CC)
REALLOC, FREE, SIG_RELEASE, SIG_DTOR, BASE_DTOR = 0x97C890, 0x97B150, 0x97B7C0, 0x97B440, 0x12FE7A0
ERRORS = (0x97C750, 0x9E08E0)          # hkArray capacity overflow, the Havok error report

HEAP, HEAP_SIZE = 0x100_0000_0000, 0x1000_0000
STACK, STACK_SIZE = 0x200_0000_0000, 0x10_0000
STUB = 0x300_0000_0000
STUB_ALLOC, STUB_FREE, STUB_BODYCAP, RET_MAGIC = STUB + 0x0, STUB + 0x10, STUB + 0x20, STUB + 0xF00
ALLOC_OBJ, ALLOC_VT, WORLD, WORLD_SUB_VT = STUB + 0x100, STUB + 0x200, STUB + 0x400, STUB + 0x700
MAX_INSNS = 50_000_000   # per call: a list closed into a cycle never returns


class Corrupt(Exception):
    pass


class Machine:
    def __init__(self, image: bytes):
        self.uc = uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
        size = (len(image) + 0xFFF) & ~0xFFF
        uc.mem_map(BASE, size, unicorn.UC_PROT_ALL)
        uc.mem_write(BASE, image)
        for at, n in ((HEAP, HEAP_SIZE), (STACK, STACK_SIZE), (STUB, 0x1000)):
            uc.mem_map(at, n, unicorn.UC_PROT_ALL)
        uc.mem_write(STUB, b'\xC3' * 0x1000)
        self.top = HEAP
        self.blocks: dict[int, int] = {}      # live allocation -> bytes
        self.bad_frees: list[str] = []
        self.released = 0
        self.failed: Exception | None = None
        self.body_count = 0
        self.q(ALLOCATOR_GLOBAL + BASE, ALLOC_OBJ)
        self.q(ALLOC_OBJ, ALLOC_VT)
        self.q(ALLOC_VT + 8, STUB_ALLOC)
        self.q(ALLOC_VT + 0x10, STUB_FREE)
        self.q(WORLD + 0x20, WORLD_SUB_VT)     # world+0x20: an embedded object whose slot +0x60 gives the body capacity
        self.q(WORLD_SUB_VT + 0x60, STUB_BODYCAP)
        stubs = {STUB_ALLOC: self.alloc_vf, STUB_FREE: self.free_vf, STUB_BODYCAP: self.bodycap,
                 BASE + REALLOC: self.realloc, BASE + FREE: self.free, BASE + SIG_RELEASE: self.sig_release,
                 BASE + SIG_DTOR: self.nothing, BASE + BASE_DTOR: self.nothing}
        for e in ERRORS:
            stubs[BASE + e] = self.error
        for at, fn in stubs.items():
            uc.mem_write(at, bytes([0xC3]))   # the stub's effect in the hook, then this ret returns to the caller
            uc.hook_add(unicorn.UC_HOOK_CODE, self._stub(fn), begin=at, end=at)

    # memory
    def q(self, at: int, v: int | None = None) -> int:
        if v is not None:
            self.uc.mem_write(at, struct.pack('<Q', v))
            return v
        return struct.unpack('<Q', self.uc.mem_read(at, 8))[0]

    def d(self, at: int, v: int | None = None) -> int:
        if v is not None:
            self.uc.mem_write(at, struct.pack('<I', v & 0xFFFFFFFF))
            return v
        return struct.unpack('<I', self.uc.mem_read(at, 4))[0]

    def reg(self, r: int, v: int | None = None) -> int:
        if v is not None:
            self.uc.reg_write(r, v)
            return v
        return self.uc.reg_read(r)

    def heap_alloc(self, n: int) -> int:
        at = (self.top + 15) & ~15
        self.top = at + max(n, 16)
        if self.top > HEAP + HEAP_SIZE:
            raise MemoryError('sim heap')
        self.uc.mem_write(at, b'\xCD' * n)   # not zeroed: nothing may read what it did not write
        self.blocks[at] = n
        return at

    def heap_free(self, at: int, n: int, who: str) -> None:
        if at == 0:
            return
        had = self.blocks.pop(at, None)
        if had != n:
            self.bad_frees.append(f'{who}: free {at:#x} of {n:#x} bytes, allocated {had}')

    # stubs: the hook does the call's work, the ret written over the function's first byte returns
    def _stub(self, fn):
        def hook(uc, address, size, user):
            try:
                fn()
            except Exception as e:   # unicorn swallows a hook's exception: stop and report it from call()
                self.failed = e
                uc.emu_stop()
        return hook

    def nothing(self) -> None:
        pass

    def error(self) -> None:
        raise Corrupt(f'Havok error path at {self.reg(U.UC_X86_REG_RIP) - BASE:#x}')

    def bodycap(self) -> None:
        self.reg(U.UC_X86_REG_RAX, 16)

    def alloc_vf(self) -> None:          # allocator slot 1: (allocator, bytes, &bytes) -> block
        n = self.reg(U.UC_X86_REG_RDX)
        self.reg(U.UC_X86_REG_RAX, self.heap_alloc(n))

    def free_vf(self) -> None:           # allocator slot 2: (allocator, block, bytes)
        self.heap_free(self.reg(U.UC_X86_REG_RDX), self.reg(U.UC_X86_REG_R8), 'allocator')

    def realloc(self) -> None:           # 0x97C890(allocator, capacity, elementSize, old, oldSize, &capAndFlags)
        cap = self.reg(U.UC_X86_REG_EDX)
        elem = self.reg(U.UC_X86_REG_R8D)
        old = self.reg(U.UC_X86_REG_R9)
        rsp = self.reg(U.UC_X86_REG_RSP)
        old_size = self.d(rsp + 0x28)
        cap_ptr = self.q(rsp + 0x30)
        flags = self.d(cap_ptr)
        new = self.heap_alloc(cap * elem)
        if old_size:
            self.uc.mem_write(new, bytes(self.uc.mem_read(old, old_size * elem)))
        if old and not flags & 0x80000000:
            self.heap_free(old, (flags & 0x3FFFFFFF) * elem, 'realloc')
        self.d(cap_ptr, cap)
        self.reg(U.UC_X86_REG_RAX, new)

    def free(self) -> None:              # 0x97B150(allocator, block, elementSize, capacity)
        self.heap_free(self.reg(U.UC_X86_REG_RDX), self.reg(U.UC_X86_REG_R8D) * self.reg(U.UC_X86_REG_R9D), '0x97B150')

    def sig_release(self) -> None:
        self.released += 1

    def call(self, fn: int, rcx: int = 0, rdx: int = 0, r8: int = 0, r9: int = 0) -> int:
        rsp = STACK + STACK_SIZE - 0x1008
        self.q(rsp, RET_MAGIC)
        for r, v in ((U.UC_X86_REG_RSP, rsp), (U.UC_X86_REG_RCX, rcx), (U.UC_X86_REG_RDX, rdx), (U.UC_X86_REG_R8, r8),
                     (U.UC_X86_REG_R9, r9)):
            self.reg(r, v)
        try:
            self.uc.emu_start(BASE + fn, RET_MAGIC, count=MAX_INSNS)
        except unicorn.UcError as e:
            raise Corrupt(f'{fn:#x}: {e} at {self.reg(U.UC_X86_REG_RIP):#x}')
        if self.failed:
            e, self.failed = self.failed, None
            raise Corrupt(f'{fn:#x}: {e}')
        if self.reg(U.UC_X86_REG_RIP) != RET_MAGIC:
            raise Corrupt(f'{fn:#x} did not return in {MAX_INSNS} instructions (a list closed into a cycle)')
        return self.reg(U.UC_X86_REG_RAX)


class Layout:
    def __init__(self, wide: bool):
        self.wide = wide
        self.end = 0xFFFFFFFF if wide else 0xFFFF
        self.free, self.glob = (0x3C, 0x64) if wide else (0x38, 0x60)
        self.next = 4 if wide else 0
        self.fmt = '<I' if wide else '<H'
        self.head = 4 if wide else 2


def walk_all(m: Machine, obj: int, lay: Layout, keys: dict[int, set[int]], global_keys: set[int]) -> dict[str, int]:
    """Every list of the dispatcher: each node in exactly one, each body's event types as made."""
    n = m.d(obj + 0x48)
    nodes = bytes(m.uc.mem_read(m.q(obj + 0x40), 16 * n)) if n else b''
    bodies = m.d(obj + 0x58)
    heads = bytes(m.uc.mem_read(m.q(obj + 0x50), lay.head * bodies)) if bodies else b''
    owner = [-1] * n

    def walk(start: int, who: int) -> list[int]:
        out, i = [], start
        while i != lay.end:
            if i >= n:
                raise Corrupt(f'list {who}: index {i:#x} past the {n} nodes')
            if owner[i] != -1:
                raise Corrupt(f'list {who}: node {i:#x} already in list {owner[i]} ({"a cycle" if owner[i] == who else "two lists"})')
            owner[i] = who
            out.append(i)
            i = struct.unpack_from(lay.fmt, nodes, 16 * i + lay.next)[0]
        return out

    free = walk(struct.unpack(lay.fmt, m.uc.mem_read(obj + lay.free, lay.head))[0], -2)
    glob = walk(struct.unpack(lay.fmt, m.uc.mem_read(obj + lay.glob, lay.head))[0], -3)
    if {struct.unpack_from('<H', nodes, 16 * i + 2)[0] for i in glob} != global_keys or len(glob) != len(global_keys):
        raise Corrupt('the global list does not hold the global event types')
    for b in range(bodies):
        chain = walk(struct.unpack_from(lay.fmt, heads, lay.head * b)[0], b)
        got = [struct.unpack_from('<H', nodes, 16 * i + 2)[0] for i in chain]
        if sorted(got) != sorted(keys.get(b, ())):
            raise Corrupt(f'body {b}: event types {got}, made {sorted(keys.get(b, ()))}')
    lost = owner.count(-1)
    if lost:
        raise Corrupt(f'{lost} nodes in no list')
    return {'nodes': n, 'free': len(free)}


def run(image: bytes, lay: Layout, nb: int, log) -> dict[str, int]:
    m = Machine(image)
    m.d(WORLD + 0x40, nb)                    # [world+0x40]: the body count getBodySignal grows +0x50 to
    obj = m.heap_alloc(0x90)
    m.call(CTOR_DERIVED, obj, WORLD)
    keys: dict[int, set[int]] = {}
    global_keys = {7, 8}

    def subscribe(b: int, ks) -> None:
        for k in ks:
            p = m.call(GET_BODY, obj, k, b)
            if m.call(GET_BODY, obj, k, b) != p:
                raise Corrupt(f'body {b} type {k}: a second getBodySignal made another node')
            keys.setdefault(b, set()).add(k)

    for k in global_keys:
        m.call(GET_GLOBAL, obj, k)
    for b in range(nb):
        subscribe(b, (1, 2, 3))
    s = walk_all(m, obj, lay, keys, global_keys)
    log(f'made: {s["nodes"]} nodes for {nb} bodies')
    made = s['nodes']
    for b in range(0, nb, 2):
        m.call(ON_REMOVED, obj, b)
        keys.pop(b, None)
    s = walk_all(m, obj, lay, keys, global_keys)
    log(f'half removed: {s["free"]} free')
    if s['free'] != 3 * ((nb + 1) // 2):
        raise Corrupt(f'{s["free"]} free nodes after removing half')
    for b in range(0, nb, 2):
        subscribe(b, (4, 5))
    s = walk_all(m, obj, lay, keys, global_keys)
    log(f'resubscribed from the free list: {s["nodes"]} nodes, {s["free"]} free')
    if s['nodes'] != made:
        raise Corrupt('resubscribing grew the nodes while the free list had room')
    # removal while dispatching: the old heads wait in +0x68 until the dispatch frees them
    m.uc.mem_write(obj + 0x62, b'\x01')
    pend = list(range(1, min(nb, 4001), 2))
    for b in pend:
        m.call(ON_REMOVED, obj, b)
        keys.pop(b, None)
    count = m.d(obj + 0x70)
    if count != len(pend):
        raise Corrupt(f'{count} pending heads, {len(pend)} removed')
    pending = struct.unpack(f'<{count}{"I" if lay.wide else "H"}', m.uc.mem_read(m.q(obj + 0x68), lay.head * count))
    for h in pending:
        m.call(FREE_CHAIN, obj, h)
    m.d(obj + 0x70, 0)
    m.uc.mem_write(obj + 0x62, b'\x00')
    s = walk_all(m, obj, lay, keys, global_keys)
    log(f'removed while dispatching, then freed: {s["free"]} free')
    for b in list(keys):
        m.call(ON_REMOVED, obj, b)
        keys.pop(b)
    s = walk_all(m, obj, lay, keys, global_keys)
    if s['free'] != s['nodes'] - len(global_keys):
        raise Corrupt(f'{s["free"]} free of {s["nodes"]} after removing every body')
    log(f'all removed: {s["free"]} free of {s["nodes"]}, {m.released} signals released')
    m.call(DTOR_BODY, obj)
    if m.bad_frees:
        raise Corrupt('frees not matching their allocation: ' + '; '.join(m.bad_frees[:4]))
    live = {a: n for a, n in m.blocks.items() if a != obj}
    if live:
        raise Corrupt(f'{len(live)} blocks left after the destructor: {sorted(live.values())[:4]}')
    return {'nodes': made}


def patched_image(skip: int | None = None) -> bytes:
    img = bytearray(STOCK)
    for rva, n, s, p in SITES:
        if img[rva:rva + n] != s:
            print(f'FAIL: {rva:#x} is not stock')
            raise SystemExit(1)
        if rva != skip:
            img[rva:rva + n] = p
    return bytes(img)


NB = 24000    # 3 types per body + 2 global: 72002 nodes, past 0xFFFF
quiet = lambda _: None  # noqa: E731
try:
    small = run(patched_image(), Layout(True), 300, quiet)
    big = run(patched_image(), Layout(True), NB, lambda t: print('  patched:', t))
except Corrupt as e:
    print('FAIL patched:', e)
    raise SystemExit(1)
if big['nodes'] <= 0xFFFF:
    print(f'FAIL: only {big["nodes"]} nodes, not past 0xFFFF')
    raise SystemExit(1)
# reverse: the stock code is sound below 0x10000 nodes and corrupt past it
try:
    run(STOCK, Layout(False), 300, quiet)
except Corrupt as e:
    print('FAIL stock below 0xFFFF nodes:', e)
    raise SystemExit(1)
try:
    run(STOCK, Layout(False), NB, quiet)
    print('FAIL: the stock code kept its lists past 0xFFFF nodes: the simulation does not reach the bug')
    raise SystemExit(1)
except Corrupt as e:
    stock_fail = str(e)
# reverse: one site left stock is caught (the truncation, the realloc's copy of the new next, a destructor's size)
partial = []
for skip in (0xE75C5E, 0xE758EF, 0xE766F7):
    try:
        run(patched_image(skip), Layout(True), NB, quiet)
        print(f'FAIL: with {skip:#x} left stock the simulation still passes')
        raise SystemExit(1)
    except Corrupt as e:
        partial.append(f'{skip:#x}: {e}')
for p in partial:
    print('  one site stock:', p[:140])
print(f'havok_props_sim: patched sound at {small["nodes"]} and {big["nodes"]} nodes; stock corrupt past 0xFFFF ({stock_fail})')
