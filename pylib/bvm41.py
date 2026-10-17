"""EDF4.1 mission scripts (BVM) as EDF5 ships them, which EDF6's EDF5_OLD_SCRIPT runtime runs.

    to_edf5(bvm41, rewrite=None) -> bytes      a 4.1 MISSION.BVM / BEFORE.BVM as EDF5's recompile of the same mission
                                               (EDF5 Root.cpk MISSION/EDF4.1/<id>/); rewrite maps the string constants
    parse(data) -> Program, build(prog) -> bytes   the container; parse + build reproduces every 4.1 and EDF5 BVM byte
                                               for byte (270 base-game files, 55 DLC files)
    convert(prog, rewrite=None, notes=None) -> Program   the 4.1 -> EDF5 step on the parsed form

Format (all little-endian; offsets are file offsets)
  0x00 'BVM '   0x04 0x50 (header size)   0x08 0, 0   0x10 0x01150138   0x14 2
  0x18 global count      0x1C global table (0x50; u32 name offset each)
  0x20 function count    0x24 function table (after the globals; 16 bytes each: code offset from [0x34], name offset,
                         parameter type bytes offset, parameter count)
  0x28 0x200, 0x2C 0x200 (stacks)
  0x30 code start (after the function table)      0x34 function code base (the initialiser ends here)
  0x38 string pool start (right after the code)   0x3C script name offset ('Mission', 'Before', ...: the file stem)
  0x40 0, 0x44 end of the name rounded up to 4, 0x48 0, 0x4C that rounded up to 16 = file size (padding 0xBA)
  Code: the initialiser (global = constant; ... ; end), then each function: its epilogue (pushl 0; fp+ N; ret) and
  body (fp- N; storel 0; storel <params> ...; ends jumping back to the epilogue), in table order; then one stub per
  function (call <body>; end) that the table points at.
  String pool, unaligned UTF-16LE with a 0 terminator each: the literals (each once, in the order the code first
  pushes them; pushs takes the byte offset from the pool start), then the global names, the function names, all the
  parameter type bytes back to back (01 int, 02 float, 03 string), the script name.
  Instruction: byte op | size << 6 (size 0/1/2/3 = 0/1/2/4 operand bytes; no operand reads as 0), the operand, and for
  the typed ops (TYPED) one type byte. Operands: signed for push / pushla / jumps / call (relative to the opcode),
  unsigned otherwise. Natives are op 0x2C..0x2F (four tables, ids are per table) with the id as operand.
  EDF's compiler sizes operands minimally, except jumps and calls: it starts every operand at 4 bytes (a 0x34 / 0x35
  case jump 2 more), then fixes the instructions front to back; a jump takes 1 byte if, with what follows still at
  the starting sizes, the target is <= 127 bytes ahead (126 for a conditional jump or call), or, backward, >= -122.

4.1 -> EDF5 (EDF5's converter decompiled 4.1's scripts and recompiled them; all 135 pairs in the two Root.cpk reproduce
byte for byte): EDF5's common functions replace 4.1's (ahead of Main); the event handlers FuncNNN become Func_1.. in
table order (their counters and the name strings too); four globals g_sergent, g_sergent_follower1..3 (= -1) follow
the counters, so the script's own globals move up 4 (pushg / storeg and the push <index> that starts an assignment
through a pointer); natives EDF5 renumbered (NATIVES); RegisterEvent (native 2) takes one more argument (0), and 100
for the handler that clears the mission; Main preloads EDF5's three result screens after native 10; every handler,
after its run-once check, reports the mission's progress (native 9, the share of the way from Main to the clearing
handler, see _progress) and goes on only while the mission runs (table 1, native 2; the clearing handler table 1,
native 1 == 0). Jumps to an unconditional jump are threaded to its target.

Not covered by the pairs (only the DLC calls them): table-0 native 4007 and table-1 natives 100, 101, 1000, 2000,
2001, 3000, 10000, matched by reading the three engines' script native dispatchers (ENGINE_EVIDENCE, TABLE1):
4007 -> 4011, 1/100 -> 1/102, 1/101 -> 0/104, 1/1000 and 1/10000 kept, 1/2000 -> 0/2022, 1/2001 -> 0/2000 + 0/3019 on
the new object; 1/3000 (SetEncountReportRadius) has no EDF5 / EDF6 script counterpart: it is written as table 1 id
3003, which only the plugin gives EDF6 (src/edf41natives.cpp). An unknown table 1..3 native raises UnsupportedNative.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Callable

MAGIC = b'BVM '
HEADER_SIZE = 0x50
VERSION = 0x01150138
PAD = 0xBA

SIZES = (0, 1, 2, 4)
TYPED = frozenset({0x01, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
                   0x31})
SIGNED = frozenset({0x15, 0x18, 0x26, 0x27, 0x28, 0x29, 0x34, 0x35})
OP_STOREI, OP_POP, OP_NEG, OP_AND = 0x01, 0x02, 0x09, 0x1D
OP_PUSHG, OP_PUSH, OP_STOREG, OP_PUSHL, OP_PUSHLA, OP_STOREL, OP_PUSHS = 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A
OP_FPADD, OP_FPSUB, OP_JZ, OP_JMP, OP_CALL, OP_RET, OP_END = 0x1B, 0x1C, 0x26, 0x28, 0x29, 0x2A, 0x30
OP_PUSH1, OP_JNE, OP_STOREIND, OP_NAT0, OP_NAT1 = 0x33, 0x34, 0x36, 0x2C, 0x2D
JUMPS = frozenset({0x26, 0x27, 0x28, 0x34, 0x35})
OP_NATIVE = frozenset({0x2C, 0x2D, 0x2E, 0x2F})
OPERAND = frozenset({0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x2B, 0x37, OP_CALL}) | OP_NATIVE | JUMPS


@dataclass
class Ins:
    """One instruction. v: the operand; for pushs the string, for a jump the index of its target in the same code
    list, for a call the index of the function. n: operand size in bytes (build() lays jumps and calls out itself).
    t: the type byte of a TYPED op."""
    op: int
    v: int | str = 0
    n: int = 0
    t: int | None = None


@dataclass
class Function:
    name: str
    types: bytes
    code: list[Ins]          # epilogue (pushl 0; fp+ N; ret), then the body
    entry: int = 3           # the body's first instruction


@dataclass
class Program:
    name: str
    globals: list[str]
    init: list[Ins]
    funcs: list[Function] = field(default_factory=list)


def _wstr(data: bytes, off: int) -> str:
    end = off
    while data[end:end + 2] != b'\0\0':
        end += 2
    return data[off:end].decode('utf-16le')


def _decode(data: bytes, start: int, end: int) -> list[tuple[int, int, int, int, int | None]]:
    """(offset, op, size, operand, type) of each instruction in data[start:end]."""
    out = []
    o = start
    while o < end:
        c = data[o]
        op, n = c & 0x3F, SIZES[c >> 6]
        if op > 0x38:
            raise ValueError(f'BVM: bad opcode {c:#x} at {o:#x}')
        v = int.from_bytes(data[o + 1:o + 1 + n], 'little', signed=op in SIGNED) if n else 0
        t = data[o + 1 + n] if op in TYPED else None
        out.append((o, op, n, v, t))
        o += 1 + n + (op in TYPED)
    if o != end:
        raise ValueError(f'BVM: code runs past {end:#x}')
    return out


def parse(data: bytes) -> Program:
    """The script in data (a 4.1 or an EDF5 BVM)."""
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ValueError('BVM: not a BVM file')
    h = struct.unpack_from('<20I', data, 0)
    if h[1] != HEADER_SIZE or h[4] != VERSION:
        raise ValueError(f'BVM: unknown version {h[4]:#x}')
    ng, og, nf, of, code0, fbase, pool = h[6], h[7], h[8], h[9], h[12], h[13], h[14]
    gnames = [_wstr(data, struct.unpack_from('<I', data, og + 4 * i)[0]) for i in range(ng)]
    rows = [struct.unpack_from('<4I', data, of + 16 * i) for i in range(nf)]
    ins = _decode(data, code0, pool)
    at = {o: i for i, (o, *_) in enumerate(ins)}
    stubs, bodies = [], []
    for c, _, _, _ in rows:
        s = at[fbase + c]
        o, op, n, v, t = ins[s]
        if op != OP_CALL or ins[s + 1][1] != OP_END or o + v not in at:
            raise ValueError(f'BVM: no call stub at {o:#x}')
        stubs.append(s)
        bodies.append(at[o + v])
    if stubs and (stubs != list(range(stubs[0], stubs[0] + 2 * nf, 2)) or stubs[-1] + 2 != len(ins)):
        raise ValueError('BVM: the call stubs are not the end of the code')
    if bodies != sorted(bodies) or any(b < 3 for b in bodies):
        raise ValueError('BVM: function bodies out of table order')
    starts = [b - 3 for b in bodies] + [stubs[0] if stubs else len(ins)]
    init_end = at.get(fbase, len(ins))
    if nf and starts[0] != init_end:
        raise ValueError('BVM: code between the initialiser and the first function')
    fn_of_body = {b: i for i, b in enumerate(bodies)}

    def convert_code(lo: int, hi: int) -> list[Ins]:
        out = []
        for i in range(lo, hi):
            o, op, n, v, t = ins[i]
            if op in JUMPS:
                j = at.get(o + v)
                if j is None or not lo <= j < hi:
                    raise ValueError(f'BVM: jump at {o:#x} leaves its function')
                out.append(Ins(op, j - lo, n, t))
            elif op == OP_CALL:
                j = at.get(o + v)
                if j not in fn_of_body:
                    raise ValueError(f'BVM: call at {o:#x} is not to a function')
                out.append(Ins(op, fn_of_body[j], n, t))
            elif op == OP_PUSHS:
                out.append(Ins(op, _wstr(data, pool + v), n, t))
            else:
                out.append(Ins(op, v, n, t))
        return out

    prog = Program(_wstr(data, h[15]), gnames, convert_code(0, init_end))
    for i, (c, name, tp, k) in enumerate(rows):
        code = convert_code(starts[i], starts[i + 1])
        if [x.op for x in code[:3]] != [OP_PUSHL, OP_FPADD, OP_RET]:
            raise ValueError(f'BVM: function {i} has no epilogue')
        prog.funcs.append(Function(_wstr(data, name), data[tp:tp + k], code))
    return prog


def _usize(v: int) -> int:
    return 0 if v == 0 else 1 if v < 0x100 else 2 if v < 0x10000 else 4


def _ssize(v: int) -> int:
    return 0 if v == 0 else 1 if -0x80 <= v < 0x80 else 2 if -0x8000 <= v < 0x8000 else 4


def _layout(seq: list[Ins], tgt: list[int | None]) -> list[int]:
    """Operand sizes of seq, picked as EDF's compiler picks them (see the format notes). tgt: the index in seq of each
    jump's or call's target, None for the other instructions (their size is the one they carry)."""
    start = [1 + (4 if x.op in OPERAND else x.n) + (x.op in TYPED) + (2 if x.op in (0x34, 0x35) else 0) for x in seq]
    ahead = [0]
    for v in start:
        ahead.append(ahead[-1] + v)
    done = [0]
    out = []
    for i, x in enumerate(seq):
        g = tgt[i]
        if g is None:
            n = x.n
        elif g > i:
            d = ahead[g] - ahead[i] + (x.op != OP_JMP)
            n = 1 if d <= 127 else 2 if d <= 32767 else 4
        else:
            d = done[g] - done[i]
            n = 1 if d >= -122 else 2 if d >= -32762 else 4
        out.append(n)
        done.append(done[-1] + 1 + n + (x.op in TYPED))
    return out


def _align(v: int, a: int) -> int:
    return (v + a - 1) // a * a


def _wz(s: str) -> bytes:
    if '\0' in s:
        raise ValueError(f'BVM: string with a NUL: {s!r}')
    return s.encode('utf-16le') + b'\0\0'


def build(prog: Program) -> bytes:
    """The BVM file of prog."""
    nf, ng = len(prog.funcs), len(prog.globals)
    seq: list[Ins] = list(prog.init)      # the whole code: initialiser, functions, call stubs
    tgt: list[int | None] = [None] * len(seq)
    starts = []
    for f in prog.funcs:
        base = len(seq)
        starts.append(base)
        for x in f.code:
            seq.append(x)
            tgt.append(base + x.v if x.op in JUMPS else None)
    stub0 = len(seq)
    for i in range(nf):
        seq += [Ins(OP_CALL, i), Ins(OP_END)]
        tgt += [None, None]
    for k, x in enumerate(seq):
        if x.op == OP_CALL:
            tgt[k] = starts[x.v] + prog.funcs[x.v].entry
    pool: dict[str, int] = {}
    off = 0
    for x in seq:
        if x.op == OP_PUSHS and x.v not in pool:
            pool[x.v] = off
            off += 2 * len(x.v) + 2
    sizes = _layout([Ins(x.op, x.v, _usize(pool[x.v])) if x.op == OP_PUSHS else x for x in seq], tgt)
    code0 = HEADER_SIZE + 4 * ng + 16 * nf
    addr = [code0]
    for k, x in enumerate(seq):
        addr.append(addr[-1] + 1 + sizes[k] + (x.op in TYPED))
    code = bytearray()
    for k, x in enumerate(seq):
        n = sizes[k]
        v = addr[tgt[k]] - addr[k] if tgt[k] is not None else pool[x.v] if x.op == OP_PUSHS else x.v
        code.append(x.op | SIZES.index(n) << 6)
        if n:
            code += int(v).to_bytes(n, 'little', signed=x.op in SIGNED or v < 0)
        if x.op in TYPED:
            code.append(x.t)
    pool_at = code0 + len(code)
    blob = bytearray(b''.join(_wz(s) for s in pool))
    gptr, nptr, tptr = [], [], []
    for s in prog.globals:
        gptr.append(pool_at + len(blob))
        blob += _wz(s)
    for f in prog.funcs:
        nptr.append(pool_at + len(blob))
        blob += _wz(f.name)
    for f in prog.funcs:
        tptr.append(pool_at + len(blob))
        blob += f.types
    name_at = pool_at + len(blob)
    blob += _wz(prog.name)
    end4 = _align(pool_at + len(blob), 4)
    end16 = _align(end4, 16)
    fbase = addr[starts[0]] if nf else addr[len(seq)]
    head = struct.pack('<4s19I', MAGIC, HEADER_SIZE, 0, 0, VERSION, 2, ng, HEADER_SIZE, nf, HEADER_SIZE + 4 * ng,
                       0x200, 0x200, code0, fbase, pool_at, name_at, 0, end4, 0, end16)
    table = struct.pack(f'<{ng}I', *gptr)
    for i, f in enumerate(prog.funcs):
        table += struct.pack('<4I', addr[stub0 + 2 * i] - fbase, nptr[i], tptr[i], len(f.types))
    data = head + table + bytes(code) + bytes(blob)
    return data + bytes([PAD]) * (end16 - len(data))


# ---- 4.1 -> EDF5 --------------------------------------------------------------------------------------------------

# EDF5's common functions, the same in all its recompiled 4.1 scripts, ahead of Main: (name, parameter types, code as
# (op, operand size, operand, type)); a call names the function.
EDF5_LIBRARY: tuple[tuple[str, str, tuple[tuple[int, int, int | str, int | None], ...]], ...] = (
    ('Voice2', '0302', (
        (0x17, 0, 0, None), (0x1B, 1, 3, None), (0x2A, 0, 0, None), (0x1C, 1, 3, None), (0x19, 0, 0, None),
        (0x19, 1, 2, None), (0x19, 1, 1, None), (0x17, 1, 1, None), (0x2C, 2, 4000, None), (0x2C, 2, 4002, None),
        (0x17, 1, 2, None), (0x2C, 1, 200, None), (0x28, 1, 0, None),
    )),
    ('RadioBegin', '', (
        (0x17, 0, 0, None), (0x1B, 1, 1, None), (0x2A, 0, 0, None), (0x1C, 1, 1, None), (0x19, 0, 0, None),
        (0x1A, 0, 'MusenBegin', None), (0x2C, 2, 4000, None), (0x28, 1, 0, None),
    )),
    ('RadioEnd', '', (
        (0x17, 0, 0, None), (0x1B, 1, 1, None), (0x2A, 0, 0, None), (0x1C, 1, 1, None), (0x19, 0, 0, None),
        (0x1A, 1, 'MusenEnd', None), (0x2C, 2, 4000, None), (0x28, 1, 0, None),
    )),
    ('RadioVoice', '0302', (
        (0x17, 0, 0, None), (0x1B, 1, 3, None), (0x2A, 0, 0, None), (0x1C, 1, 3, None), (0x19, 0, 0, None),
        (0x19, 1, 2, None), (0x19, 1, 1, None), (0x2C, 2, 4002, None), (0x17, 1, 2, None), (0x2C, 1, 200, None),
        (0x29, 1, 'RadioBegin', None), (0x17, 1, 1, None), (0x2C, 2, 4000, None), (0x29, 1, 'RadioEnd', None),
        (0x2C, 2, 4002, None), (0x28, 1, 0, None),
    )),
    ('EconomyMode', '01', (
        (0x17, 0, 0, None), (0x1B, 1, 2, None), (0x2A, 0, 0, None), (0x1C, 1, 2, None), (0x19, 0, 0, None),
        (0x19, 1, 1, None), (0x17, 1, 1, None), (0x2C, 1, 17, None), (0x28, 1, 0, None),
    )),
    ('WaitAiMoveEnd', '01', (
        (0x17, 0, 0, None), (0x1B, 1, 2, None), (0x2A, 0, 0, None), (0x1C, 1, 2, None), (0x19, 0, 0, None),
        (0x19, 1, 1, None), (0x17, 1, 1, None), (0x2C, 2, 3103, None), (0x1F, 0, 0, 0), (0x26, 1, 0, None),
        (0x2D, 0, 0, None), (0x28, 1, 6, None), (0x28, 1, 0, None),
    )),
    ('InitializeCommon', '', (
        (0x17, 0, 0, None), (0x1B, 1, 1, None), (0x2A, 0, 0, None), (0x1C, 1, 1, None), (0x19, 0, 0, None),
        (0x2C, 1, 5, None), (0x28, 1, 0, None),
    )),
    ('MissionClear_Common', '02', (
        (0x17, 0, 0, None), (0x1B, 1, 4, None), (0x2A, 0, 0, None), (0x1C, 1, 4, None), (0x19, 0, 0, None),
        (0x19, 1, 1, None), (0x2D, 2, 10000, None), (0x26, 1, 9, None), (0x2C, 2, 10003, None), (0x18, 1, 2, None),
        (0x1A, 1, 'app:/ui/lyt_HUiMissionCleared.sgo', None), (0x2C, 1, 39, None), (0x36, 0, 0, None),
        (0x17, 1, 1, None), (0x2C, 1, 200, None), (0x18, 1, 3, None), (0x1A, 1, 'ui_fade_screen_simple', None),
        (0x2C, 1, 30, None), (0x36, 0, 0, None), (0x17, 1, 3, None), (0x15, 1, 3, None), (0x15, 4, 1077936128, None),
        (0x2C, 1, 50, None), (0x17, 1, 3, None), (0x2C, 1, 51, None), (0x17, 1, 2, None), (0x2C, 1, 31, None),
        (0x17, 1, 3, None), (0x2C, 1, 31, None), (0x33, 0, 0, None), (0x2C, 1, 3, None), (0x28, 1, 0, None),
    )),
    ('MissionClear', '', (
        (0x17, 0, 0, None), (0x1B, 1, 1, None), (0x2A, 0, 0, None), (0x1C, 1, 1, None), (0x19, 0, 0, None),
        (0x15, 4, 1073741824, None), (0x2C, 2, 301, None), (0x15, 4, 1069547520, None), (0x2C, 1, 200, None),
        (0x15, 0, 0, None), (0x2C, 1, 52, None), (0x1A, 1, 'Jingle_MissionCleared', None), (0x2C, 2, 300, None),
        (0x15, 4, 1086324736, None), (0x29, 1, 'MissionClear_Common', None), (0x28, 1, 0, None),
    )),
    ('FinalMissionClear', '', (
        (0x17, 0, 0, None), (0x1B, 1, 1, None), (0x2A, 0, 0, None), (0x1C, 1, 1, None), (0x19, 0, 0, None),
        (0x15, 4, 1073741824, None), (0x2C, 2, 301, None), (0x15, 4, 1069547520, None), (0x2C, 1, 200, None),
        (0x15, 0, 0, None), (0x2C, 1, 52, None), (0x1A, 1, 'Jingle_MissionClearedFinal', None), (0x2C, 2, 300, None),
        (0x15, 4, 1092616192, None), (0x29, 2, 'MissionClear_Common', None), (0x28, 1, 0, None),
    )),
    ('MissionEscapeClear', '', (
        (0x17, 0, 0, None), (0x1B, 1, 1, None), (0x2A, 0, 0, None), (0x1C, 1, 1, None), (0x19, 0, 0, None),
        (0x15, 4, 1073741824, None), (0x2C, 2, 301, None), (0x15, 4, 1069547520, None), (0x2C, 1, 200, None),
        (0x15, 0, 0, None), (0x2C, 1, 52, None), (0x1A, 1, 'Jingle_MissionEscape', None), (0x2C, 2, 300, None),
        (0x15, 4, 1088421888, None), (0x29, 2, 'MissionClear_Common', None), (0x28, 1, 0, None),
    )),
    ('MissionGameOverEvent', '', (
        (0x17, 0, 0, None), (0x1B, 1, 6, None), (0x2A, 0, 0, None), (0x1C, 1, 6, None), (0x19, 0, 0, None),
        (0x15, 0, 0, None), (0x19, 1, 3, None), (0x15, 1, 2, None), (0x2D, 1, 1, None), (0x15, 0, 0, None),
        (0x34, 1, 12, None), (0x28, 1, 0, None), (0x2C, 2, 10001, None), (0x15, 4, 1077936128, None),
        (0x2C, 1, 200, None), (0x15, 0, 0, None), (0x2C, 1, 52, None), (0x15, 4, 1073741824, None),
        (0x2C, 2, 301, None), (0x15, 4, 1069547520, None), (0x2C, 1, 200, None), (0x2D, 2, 10000, None),
        (0x26, 1, 24, None), (0x2C, 2, 10003, None), (0x18, 1, 2, None),
        (0x1A, 2, 'app:/ui/lyt_HUiMissionFailed.sgo', None), (0x2C, 1, 39, None), (0x36, 0, 0, None),
        (0x1A, 2, 'Jingle_MissionFailed', None), (0x2C, 2, 300, None), (0x15, 4, 1084227584, None),
        (0x2C, 1, 200, None), (0x2C, 2, 10002, None), (0x2D, 2, 10000, None), (0x26, 1, 36, None),
        (0x2C, 2, 10003, None), (0x1A, 2, 'app:/ui/lyt_HUiFailedResult.sgo', None), (0x2C, 1, 39, None),
        (0x19, 1, 5, None), (0x17, 1, 5, None), (0x2C, 1, 33, None), (0x18, 1, 3, None), (0x1A, 2, '', None),
        (0x1A, 2, '', None), (0x15, 0, 0, None), (0x2C, 1, 38, None), (0x36, 0, 0, None), (0x18, 1, 4, None),
        (0x1A, 1, 'ui_fade_screen_simple', None), (0x2C, 1, 30, None), (0x36, 0, 0, None), (0x17, 1, 4, None),
        (0x15, 1, 3, None), (0x15, 4, 1056964608, None), (0x2C, 1, 50, None), (0x17, 1, 4, None),
        (0x2C, 1, 51, None), (0x17, 1, 2, None), (0x2C, 1, 31, None), (0x17, 1, 4, None), (0x2C, 1, 31, None),
        (0x17, 1, 3, None), (0x33, 0, 0, None), (0x34, 1, 67, None), (0x15, 1, 3, None), (0x2C, 1, 3, None),
        (0x28, 2, 0, None), (0x17, 1, 3, None), (0x15, 1, 2, None), (0x34, 2, 0, None), (0x15, 1, 2, None),
        (0x2C, 1, 3, None), (0x28, 2, 0, None),
    )),
    ('SceneEffect_Snow', '02020102', (
        (0x17, 0, 0, None), (0x1B, 1, 5, None), (0x2A, 0, 0, None), (0x1C, 1, 5, None), (0x19, 0, 0, None),
        (0x19, 1, 4, None), (0x19, 1, 3, None), (0x19, 1, 2, None), (0x19, 1, 1, None), (0x15, 4, 1092616192, None),
        (0x15, 4, 1036831949, None), (0x15, 4, 1028443341, None), (0x17, 1, 2, None), (0x17, 1, 1, None),
        (0x15, 4, 1056964608, None), (0x15, 4, 1056964608, None), (0x15, 4, 1056964608, None),
        (0x15, 4, 1073741824, None), (0x15, 4, 1073741824, None), (0x15, 4, 1073741824, None),
        (0x15, 4, 1120403456, None), (0x17, 1, 3, None), (0x15, 4, 1109393408, None), (0x17, 1, 4, None),
        (0x2C, 2, 5100, None), (0x28, 1, 0, None),
    )),
    ('SceneEffect_Rain', '020202010202', (
        (0x17, 0, 0, None), (0x1B, 1, 7, None), (0x2A, 0, 0, None), (0x1C, 1, 7, None), (0x19, 0, 0, None),
        (0x19, 1, 6, None), (0x19, 1, 5, None), (0x19, 1, 4, None), (0x19, 1, 3, None), (0x19, 1, 2, None),
        (0x19, 1, 1, None), (0x17, 1, 1, None), (0x17, 1, 2, None), (0x15, 4, 1056964608, None), (0x17, 1, 3, None),
        (0x15, 4, 1065353216, None), (0x15, 4, 1073741824, None), (0x15, 4, 1073741824, None),
        (0x15, 4, 1073741824, None), (0x15, 4, 1128792064, None), (0x17, 1, 4, None), (0x15, 4, 1109393408, None),
        (0x17, 1, 5, None), (0x17, 1, 6, None), (0x2C, 2, 5101, None), (0x28, 1, 0, None),
    )),
    ('SceneEffect_RainEx', '02020202010202', (
        (0x17, 0, 0, None), (0x1B, 1, 8, None), (0x2A, 0, 0, None), (0x1C, 1, 8, None), (0x19, 0, 0, None),
        (0x19, 1, 7, None), (0x19, 1, 6, None), (0x19, 1, 5, None), (0x19, 1, 4, None), (0x19, 1, 3, None),
        (0x19, 1, 2, None), (0x19, 1, 1, None), (0x17, 1, 1, None), (0x17, 1, 2, None), (0x15, 4, 1056964608, None),
        (0x17, 1, 3, None), (0x17, 1, 4, None), (0x15, 4, 1073741824, None), (0x15, 4, 1073741824, None),
        (0x15, 4, 1073741824, None), (0x15, 4, 1128792064, None), (0x17, 1, 5, None), (0x15, 4, 1109393408, None),
        (0x17, 1, 6, None), (0x17, 1, 7, None), (0x2C, 2, 5101, None), (0x28, 1, 0, None),
    )),
    ('SceneEffect_FugitiveDust', '020102020202', (
        (0x17, 0, 0, None), (0x1B, 1, 7, None), (0x2A, 0, 0, None), (0x1C, 1, 7, None), (0x19, 0, 0, None),
        (0x19, 1, 6, None), (0x19, 1, 5, None), (0x19, 1, 4, None), (0x19, 1, 3, None), (0x19, 1, 2, None),
        (0x19, 1, 1, None), (0x17, 1, 1, None), (0x17, 1, 2, None), (0x15, 4, 1092616192, None), (0x17, 1, 3, None),
        (0x17, 1, 4, None), (0x17, 1, 5, None), (0x17, 1, 6, None), (0x2C, 2, 5102, None), (0x28, 1, 0, None),
    )),
    ('SceneEffect_Fog', '020102020202', (
        (0x17, 0, 0, None), (0x1B, 1, 7, None), (0x2A, 0, 0, None), (0x1C, 1, 7, None), (0x19, 0, 0, None),
        (0x19, 1, 6, None), (0x19, 1, 5, None), (0x19, 1, 4, None), (0x19, 1, 3, None), (0x19, 1, 2, None),
        (0x19, 1, 1, None), (0x17, 1, 1, None), (0x17, 1, 2, None), (0x15, 4, 1092616192, None), (0x17, 1, 3, None),
        (0x17, 1, 4, None), (0x17, 1, 5, None), (0x17, 1, 6, None), (0x2C, 2, 5103, None), (0x28, 1, 0, None),
    )),
)

# 4.1 natives of table 0 (op 0x2C) EDF5 numbers differently: 4.1 id -> EDF5 id. All seen in the 135 pairs except
# 4007 (DLC only), established from the engines (ENGINE_EVIDENCE).
NATIVES: dict[int, int] = {38: 50, 39: 51, 1001: 1003, 2004: 2006, 3025: 3040, 3026: 3025, 3110: 3150, 3111: 3151,
                           3112: 3152, 4006: 4010, 4007: 4011, 9203: 9120}

# 4.1's table-1 natives (op 0x2D; no base-game script uses one, the DLC does): what each becomes in EDF5 / EDF6.
#   ('as', op, id)       the same function there, same arguments
#   ('create_scaled',)   CreateEnemy with a durability percentage: table 0 2000 (CreateEnemy), dup, the percentage,
#                        table 0 3019 (scale durability); the last argument must be a single push
#   ('plugin', op, id, name)  nothing in EDF5 / EDF6 dispatches to it; the plugin gives EDF6 that id
#                        (src/edf41natives.cpp): such a script runs only with the plugin
#   ('none', why)        nothing there does it: converting refuses (UnsupportedNative)
TABLE1: dict[int, tuple] = {
    100: ('as', 0x2D, 102),
    101: ('as', 0x2C, 104),
    1000: ('as', 0x2D, 1000),
    2000: ('as', 0x2C, 2022),
    2001: ('create_scaled',),
    3000: ('plugin', 0x2D, 3003, 'SetEncountReportRadius'),
    10000: ('as', 0x2D, 10000),
}
NAT_CREATE_ENEMY, NAT_SCALE_DURABILITY = 2000, 3019

# How the natives the base-game pairs do not cover were matched (static analysis plus emulating the dispatchers of
# EDF41.exe, EDF5.exe and EDF6's EDF.dll; addresses are RVAs). Script native dispatchers: 4.1 0x14B220, EDF5
# 0x114D50, EDF6 0x20FF70 (r9 = table; table 0 switches on the id's low 7 bits in 4.1, its low byte in EDF5 / EDF6;
# table 1 on its low 3 bits).
ENGINE_EVIDENCE: dict[tuple[int, int], str] = {
    (0x2C, 4007): "float into +0x3C of the object 4006 writes bytes +0x39..3B of (4.1 0x14BDBD, global 0xCC84F0); "
                  "EDF5 4011 writes +0x5C of the object its 4010 writes +0x59..5B of (0x11681A); EDF6 4011 -> "
                  "0x5DD3D0 (+0x64; its 4010 -> 0x5DD220, +0x5D..5F). One float",
    (0x2D, 100): "two floats into the map object's +0xD4 / +0xD8 (4.1 0x14EEE0; set to 1e6 / 1e6+1 at 0xBCB83: likely "
                 "the fog begin / end distances); EDF6 table 1 low bits 6 -> 0x228520 writes +0x150 / +0x154 of the "
                 "same map object (1e6 / 1e6+1 at 0x9DA01), EDF5 -> 0x11B8E0 (+0x154 / +0x158). Written as 102",
    (0x2D, 101): "MapObjectDestroy3(name) (4.1 0x14E6A0, message 'MapObjectDestroy3( not found'); EDF5 table 0 104 "
                 "-> 0x11AC20, EDF6 104 -> 0x2236E0, same message; EDF5's own scripts call 104",
    (0x2D, 1000): "PointAriStrike(name, float, sgo, float, int, float) (4.1 0x1528F0, EDF5 0x121820, EDF6 0x225470: "
                  "same message, same six arguments). Same id",
    (0x2D, 2000): "CreateFlyingEnemyGroup_OnRoute(route, float, sgo, int, float, bool) (4.1 0x153600; EDF5 table 0 "
                  "2022 -> 0x124010, EDF6 2022 -> 0x2191B0, same message; EDF5's own scripts call 2022)",
    (0x2D, 2001): "CreateEnemy(name, sgo, float, int) (4.1 0x152CA0, the same as table 0 2000) returning the object, "
                  "its durability (+0x168 max, +0x16C current) then scaled by the fifth argument / 100 as table 0 "
                  "3019 (0x153E80) does it; EDF5 / EDF6 keep 2000 (0x121F70 / 0x2154C0) and 3019 (0x1254C0 / "
                  "0x228850)",
    (0x2D, 3000): "SetEncountReportRadius(object, float) (4.1 inline at 0x14B2E5: +0x240, which game_object_encount "
                  "fills at 0x28CF66); EDF5: no native writes its +0x2E0; EDF6: 0x228120 writes +0x470 (filled from "
                  "game_object_encount at 0x546118) but no table entry leads to it (AngelScript 'void "
                  "SetEncountReportRadius(::Object , float )' only). Written as table 1 id 3003, which the plugin "
                  "routes to 0x228120 (src/edf41natives.cpp: table 1's jump entry 3 of 0x2148BC, a no-op before)",
    (0x2D, 10000): "whether the current scene has its flag set (4.1 0x14B30E, EDF6 0x223010: the same test, the "
                   "field moved); EDF5's own common functions call 10000",
}


class UnsupportedNative(ValueError):
    """A 4.1 script calls a native nothing in EDF5 / EDF6 does."""


# stack effect (pushes - pops) of the ops; a call pops its function's parameters, a native as NET41 says
STACK: dict[int, int] = {0x01: -1, 0x02: -1, 0x03: 1, 0x04: -1, 0x05: -1, 0x06: -1, 0x07: -1, 0x08: -1, 0x09: 0,
                         0x0A: 0, 0x0B: 0, 0x0C: -1, 0x0D: -1, 0x0E: -1, 0x0F: -1, 0x10: -1, 0x11: 0, 0x12: 0, 0x13: 0,
                         0x14: 1, 0x15: 1, 0x16: -1, 0x17: 1, 0x18: 1, 0x19: -1, 0x1A: 1, 0x1B: 0, 0x1C: 0, 0x1D: -1,
                         0x1E: -1, 0x1F: 0, 0x20: -1, 0x21: -1, 0x22: -1, 0x23: -1, 0x24: -1, 0x25: -1, 0x26: -1,
                         0x27: -1, 0x28: 0, 0x2A: -1, 0x30: 0, 0x31: -1, 0x32: -1, 0x33: 1, 0x34: -2, 0x35: -2,
                         0x36: -2}
# stack effect of the 4.1 natives (op, id), solved from the stack balance of every 4.1 script (base game and DLC)
NET41: dict[tuple[int, int], int] = {(0x2C, 1): -1, (0x2C, 2): -2, (0x2C, 3): -1, (0x2C, 5): 0, (0x2C, 13): -2,
    (0x2C, 17): -1, (0x2C, 20): -1, (0x2C, 21): 1, (0x2C, 37): -2, (0x2C, 102): -1, (0x2C, 103): -1, (0x2C, 110): -1,
    (0x2C, 200): -1, (0x2C, 201): -1, (0x2C, 300): -1, (0x2C, 301): -1, (0x2C, 302): -2, (0x2C, 420): -6,
    (0x2C, 900): -2, (0x2C, 901): -4, (0x2C, 902): -4, (0x2C, 910): 0, (0x2C, 1001): 1, (0x2C, 1010): -3,
    (0x2C, 1011): -6, (0x2C, 1012): -6, (0x2C, 1020): -2, (0x2C, 1021): -1, (0x2C, 1022): -5, (0x2C, 1023): -5,
    (0x2C, 1100): -1, (0x2C, 1140): -4, (0x2C, 1141): -4, (0x2C, 2000): -3, (0x2C, 2001): -3, (0x2C, 2002): -6,
    (0x2C, 2003): -6, (0x2C, 2004): -5, (0x2C, 2100): -8, (0x2C, 2101): -7, (0x2C, 3000): -1, (0x2C, 3019): -2,
    (0x2C, 3020): -2, (0x2C, 3021): -2, (0x2C, 3022): -1, (0x2C, 3023): -1, (0x2C, 3024): -2, (0x2C, 3025): -2,
    (0x2C, 3026): -2, (0x2C, 3051): -1, (0x2C, 3100): -2, (0x2C, 3101): -2, (0x2C, 3102): -2, (0x2C, 3110): -2,
    (0x2C, 3111): -2, (0x2C, 3112): -2, (0x2C, 3200): -2, (0x2C, 3201): -2, (0x2C, 3202): -1, (0x2C, 3300): -2,
    (0x2C, 4000): -1, (0x2C, 4002): 0, (0x2C, 4006): -1, (0x2C, 4007): -1, (0x2C, 4102): -4, (0x2C, 4103): -4,
    (0x2C, 4104): -5, (0x2C, 4105): -5, (0x2C, 4106): -6, (0x2C, 9000): -1, (0x2C, 9001): -2, (0x2C, 9100): -1,
    (0x2C, 9110): -2, (0x2C, 9112): -3, (0x2C, 9113): -2, (0x2C, 9114): -1, (0x2C, 9115): -2, (0x2C, 9200): -2,
    (0x2C, 9201): -1, (0x2C, 9202): -1, (0x2C, 9203): -1, (0x2C, 9300): -1, (0x2C, 9301): -1, (0x2C, 9310): -1,
    (0x2C, 9400): -1, (0x2C, 10000): 0, (0x2D, 100): -2, (0x2D, 101): -1, (0x2D, 1000): -6, (0x2D, 2000): -6,
    (0x2D, 2001): -4, (0x2D, 3000): -2, (0x2D, 10000): 1}

# stack effect in EDF6's executor of the natives EDF5's scripts and the converted 4.1 scripts call (op, id): measured by
# emulating EDF.dll's dispatcher 0x20FF70 (the BVM stack pointer it hands back); 0/10000..10003 take nothing (read no
# argument); 1/3003 is the plugin's (src/edf41natives.cpp: pops the handle and the radius).
NET6: dict[tuple[int, int], int] = {(0x2C, 1): -1, (0x2C, 2): -3, (0x2C, 3): -1, (0x2C, 5): 0, (0x2C, 9): -1,
    (0x2C, 10): 0, (0x2C, 11): 0, (0x2C, 12): 0, (0x2C, 13): -2, (0x2C, 14): -3, (0x2C, 16): 0, (0x2C, 17): -1,
    (0x2C, 18): -4, (0x2C, 20): -1, (0x2C, 21): 1, (0x2C, 30): 0, (0x2C, 31): -1, (0x2C, 33): -1, (0x2C, 38): -2,
    (0x2C, 39): 0, (0x2C, 50): -3, (0x2C, 51): -1, (0x2C, 52): -1, (0x2C, 100): -2, (0x2C, 102): -1, (0x2C, 103): -1,
    (0x2C, 104): -1, (0x2C, 110): -1, (0x2C, 200): -1, (0x2C, 201): -1, (0x2C, 300): -1, (0x2C, 301): -1,
    (0x2C, 302): -2, (0x2C, 350): -1, (0x2C, 351): -2, (0x2C, 401): -1, (0x2C, 410): 0, (0x2C, 420): -6,
    (0x2C, 800): -3, (0x2C, 900): -2, (0x2C, 901): -4, (0x2C, 902): -4, (0x2C, 910): 0, (0x2C, 1000): -1,
    (0x2C, 1003): 1, (0x2C, 1010): -3, (0x2C, 1011): -6, (0x2C, 1012): -6, (0x2C, 1020): -2, (0x2C, 1021): -1,
    (0x2C, 1022): -5, (0x2C, 1023): -5, (0x2C, 1100): -1, (0x2C, 1140): -4, (0x2C, 1141): -4, (0x2C, 2000): -3,
    (0x2C, 2001): -3, (0x2C, 2002): -6, (0x2C, 2003): -6, (0x2C, 2006): -5, (0x2C, 2022): -6, (0x2C, 2100): -8,
    (0x2C, 2101): -7, (0x2C, 3000): -1, (0x2C, 3019): -2, (0x2C, 3020): -2, (0x2C, 3021): -2, (0x2C, 3022): -1,
    (0x2C, 3023): -1, (0x2C, 3024): -2, (0x2C, 3025): -2, (0x2C, 3040): -2, (0x2C, 3051): -1, (0x2C, 3100): -2,
    (0x2C, 3101): -2, (0x2C, 3102): -2, (0x2C, 3103): 0, (0x2C, 3150): -2, (0x2C, 3151): -2, (0x2C, 3152): -2,
    (0x2C, 3200): -2, (0x2C, 3201): -2, (0x2C, 3202): -1, (0x2C, 3300): -2, (0x2C, 4000): -1, (0x2C, 4002): 0,
    (0x2C, 4010): -1, (0x2C, 4011): -1, (0x2C, 4100): 0, (0x2C, 4101): 0, (0x2C, 4102): -4, (0x2C, 4103): -4,
    (0x2C, 4104): -5, (0x2C, 4105): -5, (0x2C, 4106): -6, (0x2C, 5100): -15, (0x2C, 5101): -13, (0x2C, 5102): -7,
    (0x2C, 5103): -7, (0x2C, 9000): -1, (0x2C, 9001): -2, (0x2C, 9100): -1, (0x2C, 9110): -2, (0x2C, 9112): -3,
    (0x2C, 9113): -2, (0x2C, 9114): -1, (0x2C, 9115): -2, (0x2C, 9120): -1, (0x2C, 9200): -2, (0x2C, 9201): -1,
    (0x2C, 9202): -1, (0x2C, 9300): -1, (0x2C, 9301): -1, (0x2C, 9310): -1, (0x2C, 9400): -1, (0x2C, 10000): 0,
    (0x2C, 10001): 0, (0x2C, 10002): 0, (0x2C, 10003): 0, (0x2D, 0): 0, (0x2D, 1): 0, (0x2D, 2): 1, (0x2D, 102): -2,
    (0x2D, 1000): -6, (0x2D, 3003): -2, (0x2D, 10000): 1}


def stack_check(prog: Program, effects: dict[tuple[int, int], int] | None = None) -> list[str]:
    """Where prog's stack does not balance under `effects` (default NET6): a jump target reached at two depths, the
    epilogue reached with values left, a native of unknown effect. Empty when every path balances."""
    effects = NET6 if effects is None else effects
    out = []
    for f in prog.funcs:
        depth = {f.entry: 1 + len(f.types)}
        work = [f.entry]
        while work:
            i = work.pop()
            x = f.code[i]
            if x.op in OP_NATIVE:
                e = effects.get((x.op, x.v))
                if e is None:
                    out.append(f'{f.name}: native {x.op - OP_NAT0}/{x.v} of unknown stack effect')
                    continue
            elif x.op == OP_CALL:
                e = -len(prog.funcs[x.v].types)
            else:
                e = STACK.get(x.op, 0)
            d = depth[i] + e
            succ = [x.v] if x.op == OP_JMP else [x.v, i + 1] if x.op in JUMPS else [] if x.op in (OP_RET, OP_END) \
                else [i + 1]
            for j in succ:
                if j == 0:
                    if d != 0:
                        out.append(f'{f.name}: {d} values left at the epilogue (from {i})')
                elif j >= len(f.code):
                    out.append(f'{f.name}: runs off its end at {i}')
                elif j not in depth:
                    depth[j] = d
                    work.append(j)
                elif depth[j] != d:
                    out.append(f'{f.name}: {j} reached at depths {depth[j]} and {d}')
    return out


# what EDF6's executor reads for each native's arguments, bottom to top, and how many values it leaves: p a pointer (a
# string), f a float, i an int (a number, an object handle, a bool), ? not read in the emulated path. Measured by
# emulating the dispatcher 0x20FF70 (the instruction reading each argument slot); 1/3003 is the plugin's; 0/20 copies
# its argument's 32 bits as they are (0x210DB5: into the executor's +0x46C), so takes either.
SIG6: dict[tuple[int, int], tuple[str, int]] = {(0x2C, 1): ('?', 0), (0x2C, 2): ('pfi', 0), (0x2C, 3): ('i', 0),
    (0x2C, 5): ('', 0), (0x2C, 9): ('f', 0), (0x2C, 10): ('', 0), (0x2C, 11): ('', 0), (0x2C, 12): ('', 0),
    (0x2C, 13): ('pi', 0), (0x2C, 14): ('ppi', 0), (0x2C, 16): ('', 0), (0x2C, 17): ('i', 0), (0x2C, 18): ('????', 0),
    (0x2C, 20): ('?', 0), (0x2C, 21): ('', 1), (0x2C, 30): ('p', 1), (0x2C, 31): ('i', 0), (0x2C, 33): ('i', 0),
    (0x2C, 38): ('ppi', 1), (0x2C, 39): ('p', 1), (0x2C, 50): ('iif', 0), (0x2C, 51): ('i', 0), (0x2C, 52): ('i', 0),
    (0x2C, 100): ('pp', 0), (0x2C, 102): ('p', 0), (0x2C, 103): ('p', 0), (0x2C, 104): ('p', 0), (0x2C, 110): ('p', 0),
    (0x2C, 200): ('f', 0), (0x2C, 201): ('i', 0), (0x2C, 300): ('p', 0), (0x2C, 301): ('f', 0), (0x2C, 302): ('pf', 0),
    (0x2C, 350): ('p', 0), (0x2C, 351): ('pp', 0), (0x2C, 401): ('p', 0), (0x2C, 410): ('', 0),
    (0x2C, 420): ('ifffff', 0), (0x2C, 800): ('pff', 0), (0x2C, 900): ('pf', 0), (0x2C, 901): ('pfii', 0),
    (0x2C, 902): ('piff', 0), (0x2C, 910): ('', 0), (0x2C, 1000): ('p', 0), (0x2C, 1003): ('', 1),
    (0x2C, 1010): ('ppfi', 1), (0x2C, 1011): ('pfppifi', 1), (0x2C, 1012): ('pfpifi', 0), (0x2C, 1020): ('ppf', 1),
    (0x2C, 1021): ('pp', 1), (0x2C, 1022): ('pppppf', 1), (0x2C, 1023): ('pppppf', 1), (0x2C, 1100): ('i', 0),
    (0x2C, 1140): ('ifpf', 0), (0x2C, 1141): ('ifpf', 0), (0x2C, 2000): ('ppfi', 1), (0x2C, 2001): ('ppfi', 1),
    (0x2C, 2002): ('pfpifi', 0), (0x2C, 2003): ('pfpifi', 0), (0x2C, 2006): ('ppifi', 0), (0x2C, 2022): ('pfpifi', 0),
    (0x2C, 2100): ('iipifffi', 0), (0x2C, 2101): ('iipiffi', 0), (0x2C, 3000): ('i', 0), (0x2C, 3019): ('if', 0),
    (0x2C, 3020): ('if', 0), (0x2C, 3021): ('ii', 0), (0x2C, 3022): ('i', 0), (0x2C, 3023): ('i', 0),
    (0x2C, 3024): ('if', 0), (0x2C, 3025): ('if', 0), (0x2C, 3040): ('ii', 0), (0x2C, 3051): ('i', 0),
    (0x2C, 3100): ('if', 0), (0x2C, 3101): ('ip', 0), (0x2C, 3102): ('ip', 0), (0x2C, 3103): ('i', 1),
    (0x2C, 3150): ('if', 0), (0x2C, 3151): ('ii', 0), (0x2C, 3152): ('ii', 0), (0x2C, 3200): ('ii', 0),
    (0x2C, 3201): ('ii', 0), (0x2C, 3202): ('i', 0), (0x2C, 3300): ('iii', 1), (0x2C, 4000): ('p', 0),
    (0x2C, 4002): ('', 0), (0x2C, 4010): ('i', 0), (0x2C, 4011): ('f', 0), (0x2C, 4100): ('', 0), (0x2C, 4101): ('', 0),
    (0x2C, 4102): ('ippf', 0), (0x2C, 4103): ('ippf', 0), (0x2C, 4104): ('ippff', 0), (0x2C, 4105): ('iippf', 0),
    (0x2C, 4106): ('iippff', 0), (0x2C, 5100): ('ffffffffffffiff', 0), (0x2C, 5101): ('fffffffffifff', 0),
    (0x2C, 5102): ('fifffff', 0), (0x2C, 5103): ('fifffff', 0), (0x2C, 9000): ('f', 0), (0x2C, 9001): ('if', 0),
    (0x2C, 9100): ('f', 0), (0x2C, 9110): ('ii', 0), (0x2C, 9112): ('pii', 0), (0x2C, 9113): ('ii', 0),
    (0x2C, 9114): ('i', 0), (0x2C, 9115): ('ii', 0), (0x2C, 9120): ('i', 0), (0x2C, 9200): ('if', 0),
    (0x2C, 9201): ('i', 0), (0x2C, 9202): ('i', 0), (0x2C, 9300): ('i', 0), (0x2C, 9301): ('i', 0),
    (0x2C, 9310): ('i', 0), (0x2C, 9400): ('p', 0), (0x2C, 10000): ('', 0), (0x2C, 10001): ('', 0),
    (0x2C, 10002): ('', 0), (0x2C, 10003): ('', 0), (0x2D, 0): ('', 0), (0x2D, 1): ('i', 1), (0x2D, 2): ('', 1),
    (0x2D, 102): ('ff', 0), (0x2D, 1000): ('pfpfif', 0), (0x2D, 3003): ('if', 0), (0x2D, 10000): ('', 1)}


def _typed_result(x: Ins) -> str:
    """What an op leaves on the stack: f float, i int, s string, ? unknown (a variable, a call's result)."""
    if x.op == OP_PUSHS:
        return 's'
    if x.op == OP_PUSH:
        return 'f' if x.n == 4 else 'i'
    if x.op in (OP_PUSH1, OP_PUSHLA, 0x1D, 0x1E, 0x1F, 0x08, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12):
        return 'i'
    if x.op == 0x13:
        return 'f'
    if x.op in (0x04, 0x05, 0x06, 0x07, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25):
        return 'f' if x.t & 3 else 'i'
    if x.op in (0x09, 0x0A, 0x0B):
        return 'f' if x.t & 2 else 'i'
    return '?'


_ACCEPTS = {'p': 's?', 'f': 'f?', 'i': 'i?', '?': 'sfi?'}


@dataclass(frozen=True)
class Arg:
    """A value on the script's stack as the walk (_walk) sees it: its type (_typed_result's letters) and, for a string
    literal pushed as it is (pushs), the string (None for anything else: a variable, a native's result)."""
    t: str
    s: str | None = None


_UNKNOWN = Arg('?')


def _walk(prog: Program, sigs: dict[tuple[int, int], tuple[str, int]],
          visit: Callable[[Function, int, Ins, list[Arg] | None], None]) -> None:
    """Every native call of every function on each path through it, with the values it is passed: `visit(function,
    index, instruction, args)`, args the signature's (`sigs`) count of values off the stack, None when the native has
    no signature or the stack holds fewer values than it reads (the walk then stops on that path)."""
    for f in prog.funcs:
        stacks: dict[int, list[Arg]] = {f.entry: [_UNKNOWN] * (1 + len(f.types))}
        work = [f.entry]
        while work:
            i = work.pop()
            st = list(stacks[i])
            x = f.code[i]
            if x.op in OP_NATIVE:
                sig = sigs.get((x.op, x.v))
                if sig is None:
                    visit(f, i, x, None)
                    continue
                want, results = sig
                got = st[len(st) - len(want):] if want else []
                if len(got) < len(want):
                    visit(f, i, x, None)
                    continue
                visit(f, i, x, got)
                del st[len(st) - len(want):]
                st += [_UNKNOWN] * results
            elif x.op == OP_CALL:
                del st[len(st) - len(prog.funcs[x.v].types):]
            elif x.op == 0x03:
                st.append(st[-1])
            elif x.op == OP_STOREI:
                st[-2:] = [st[-1]]
            else:
                e = STACK.get(x.op, 0)
                pops = {0: 1, 1: 0, -1: 2, -2: 2}.get(e, 0) if x.op not in (OP_PUSHG, OP_PUSH, OP_PUSHL, OP_PUSHLA,
                                                                             OP_PUSHS, OP_PUSH1) else 0
                if x.op in (OP_STOREG, OP_STOREL, OP_POP, OP_JZ, 0x27, OP_RET, 0x31, 0x32):
                    pops = 1
                elif x.op in (OP_JNE, 0x35, OP_STOREIND):
                    pops = 2
                elif x.op in (OP_FPADD, OP_FPSUB, OP_JMP, OP_END):
                    pops = 0
                if pops:
                    del st[len(st) - pops:]
                if e + pops > 0:
                    st.append(Arg(_typed_result(x), x.v if x.op == OP_PUSHS and isinstance(x.v, str) else None))
            succ = [x.v] if x.op == OP_JMP else [x.v, i + 1] if x.op in JUMPS else [] if x.op in (OP_RET, OP_END) \
                else [i + 1]
            for j in succ:
                if 0 < j < len(f.code) and j not in stacks:
                    stacks[j] = st
                    work.append(j)


def type_check(prog: Program, sigs: dict[tuple[int, int], tuple[str, int]] | None = None) -> list[str]:
    """Each native call whose arguments, as the script pushes them, are not of the types EDF6's handler reads (SIG6):
    a string literal where a number is read, a float where an int is, and so on. A value whose type the script does not
    show (a variable, a native's result) fits anything."""
    sigs = SIG6 if sigs is None else sigs
    out: list[str] = []

    def visit(f: Function, i: int, x: Ins, got: list[Arg] | None) -> None:
        if got is None:
            why = 'has no signature' if (x.op, x.v) not in sigs else 'with too few values on the stack'
            out.append(f'{f.name} {i}: native {x.op - OP_NAT0}/{x.v} {why}')
            return
        want = sigs[(x.op, x.v)][0]
        for k, (w, g) in enumerate(zip(want, got)):
            if g.t not in _ACCEPTS[w]:
                out.append(f'{f.name} {i}: native {x.op - OP_NAT0}/{x.v} argument {k + 1} of {len(want)}: '
                           f'reads {w}, the script passes {g.t}')
    _walk(prog, sigs, visit)
    return out


def native_calls(prog: Program, wanted: set[tuple[int, int]],
                 sigs: dict[tuple[int, int], tuple[str, int]] | None = None) -> list[tuple[str, int, Ins, list[Arg]]]:
    """(function name, index, instruction, arguments) of every call of a native in `wanted` ((op, id) pairs), each once,
    with the values the script passes it (Arg: a string literal's text when the script pushes one). A wanted native
    without a signature in `sigs` (SIG6), or reached with too few values on the stack, raises ValueError: the caller
    asked for arguments this walk cannot give."""
    sigs = SIG6 if sigs is None else sigs
    out: dict[tuple[str, int], tuple[str, int, Ins, list[Arg]]] = {}

    def visit(f: Function, i: int, x: Ins, got: list[Arg] | None) -> None:
        if (x.op, x.v) not in wanted:
            return
        if got is None:
            raise ValueError(f'{f.name} {i}: native {x.op - OP_NAT0}/{x.v}: no arguments to read (signature / stack)')
        out.setdefault((f.name, i), (f.name, i, x, list(got)))
    _walk(prog, sigs, visit)
    return [out[k] for k in sorted(out, key=lambda k: ([g.name for g in prog.funcs].index(k[0]), k[1]))]


SERGENT = ('g_sergent', 'g_sergent_follower1', 'g_sergent_follower2', 'g_sergent_follower3')
RESULT_SCREENS = ('app:/ui/lyt_HUiMissionCleared.sgo', 'app:/ui/lyt_HUiMissionFailed.sgo',
                  'app:/ui/lyt_HUiFailedResult.sgo')
CLEARS = ('MissionClear', 'FinalMissionClear')
NAT_REGISTER, NAT_PROGRESS, NAT_PRELOAD, NAT_SETUP_DONE = 2, 9, 13, 10    # table 0
NAT_RUNNING, NAT_CLEAR_STATE = 2, 1                                       # table 1 (EDF5)
F_100 = 0x42C80000


def _float_bits(v: float) -> int:
    return struct.unpack('<i', struct.pack('<f', v))[0]


def _progress_bits(v: float) -> int:
    """v as EDF5's converter wrote it: a 32-bit float printed with 7 significant digits and read back."""
    return _float_bits(float('%.7g' % struct.unpack('<f', struct.pack('<f', v))[0]))


def _events(prog: Program, mission: list[Function]) -> tuple[dict[str, list[str]], set[str]]:
    """The mission's event graph: function -> the handlers it names, each once; and the handlers that clear the
    mission (call MissionClear / FinalMissionClear)."""
    names = {f.name for f in mission}
    graph = {f.name: list(dict.fromkeys(x.v for x in f.code if x.op == OP_PUSHS and x.v in names)) for f in mission}
    clear = {f.name for f in mission[1:] if any(x.op == OP_CALL and prog.funcs[x.v].name in CLEARS for x in f.code)}
    return graph, clear


def _progress(graph: dict[str, list[str]], clear: set[str]) -> dict[str, float]:
    """Each handler's share of the way from Main to clearing the mission, as EDF5's converter worked it out: over
    every simple path from Main to a clearing handler (the first one it meets), the largest steps-to-the-handler /
    steps-of-the-path. Handlers on no such path get none."""
    best: dict[str, float] = {}
    path = ['Main']

    def walk(n: str) -> None:
        if n in clear:
            for i in range(1, len(path)):
                best[path[i]] = max(best.get(path[i], -1.0), i / (len(path) - 1))
            return
        for c in graph.get(n, ()):
            if c not in path:
                path.append(c)
                walk(c)
                path.pop()

    walk('Main')
    return best


_STATEMENT_END = frozenset({OP_STOREIND, OP_POP, OP_STOREG, OP_STOREL, OP_CALL}) | JUMPS | OP_NATIVE
_EXPR_END = frozenset({OP_STOREIND, OP_STOREI, OP_STOREL, OP_STOREG, OP_POP, OP_CALL, OP_RET, OP_END}) | JUMPS


def _const(x: Ins) -> int | None:
    """The integer a push / push1 puts on the stack (None for any other instruction, or a 4-byte float push)."""
    if x.op == OP_PUSH1:
        return 1
    return x.v if x.op == OP_PUSH and x.n < 4 else None


def _global_addresses(code: list[Ins], first: int, nglobals: int, unknown: set[tuple[int, int]]) -> set[int]:
    """Indexes of the constants in code that are the index of a global from first on: 'g = expr' through a pointer is
    the statement push <index of g>; expr; storeind (storei), the index still the slot under the value when it
    stores. A native of unknown stack effect on the way leaves the constant alone and goes into unknown."""
    out = set()
    for j, x in enumerate(code):
        k = _const(x)
        if k is None or not first <= k < nglobals or j == 0 or code[j - 1].op not in _STATEMENT_END:
            continue
        depth = 1
        for y in code[j + 1:]:
            if y.op in (OP_STOREIND, OP_STOREI):
                if depth == 2:
                    out.add(j)
                break
            if y.op in _EXPR_END:
                break
            if y.op in OP_NATIVE:
                e = NET41.get((y.op, y.v))
                if e is None:
                    unknown.add((y.op, y.v))
                    break
                depth += e
            else:
                depth += STACK.get(y.op, 0)
            if depth < 1:
                break
    return out


def _run_once(code: list[Ins]) -> bool:
    """A handler's opening (counter += 1; go on only the first time): and; jz <body>; jmp <epilogue> at 14..16."""
    return len(code) > 17 and code[14].op == OP_AND and code[15].op == OP_JZ and code[16].op == OP_JMP \
        and code[16].v == 0


def _thread(code: list[Ins]) -> list[Ins]:
    """Jumps to an unconditional jump go where that one goes, as EDF's compiler emits them."""
    for x in code:
        if x.op in JUMPS:
            seen = set()
            while code[x.v].op == OP_JMP and x.v not in seen and code[x.v] is not x:
                seen.add(x.v)
                x.v = code[x.v].v
    return code


class _Edit:
    """A function's new code made from its old code. A jump operand ('old', i) goes to whatever was emitted in front
    of old instruction i, ('new', i) to new instruction i."""

    def __init__(self) -> None:
        self.out: list[Ins] = []
        self.at: dict[int, int] = {}

    def mark(self, old: int) -> None:
        self.at.setdefault(old, len(self.out))

    def emit(self, *xs: Ins) -> None:
        self.out.extend(xs)

    def done(self, old_len: int) -> list[Ins]:
        self.mark(old_len)
        for x in self.out:
            if x.op in JUMPS:
                kind, i = x.v
                x.v = self.at[i] if kind == 'old' else i
        return _thread(self.out)


_SIMPLE_PUSH = frozenset({OP_PUSHG, OP_PUSH, OP_PUSHL, OP_PUSHS, OP_PUSH1})


def _table1(where: str, x: Ins, deferred: Ins | None, e: _Edit, note: Callable[[str], None]) -> Ins | None:
    """A 4.1 call of a table 1..3 native as EDF5 / EDF6 has it (TABLE1): the instruction to emit, or None when it is
    emitted here already."""
    act = TABLE1.get(x.v) if x.op == OP_NAT1 else None
    if act is None:
        raise UnsupportedNative(f'BVM: {where}: native table {x.op - OP_NAT0} id {x.v} is not known in EDF5 / EDF6')
    if act[0] == 'none':
        raise UnsupportedNative(f'BVM: {where}: table 1 native {x.v} has no EDF5 / EDF6 counterpart: {act[1]}')
    note(f'{where}: native 1/{x.v} -> ' + ('0/2000 + 0/3019' if act[0] == 'create_scaled' else
                                          f'{act[1] - OP_NAT0}/{act[2]}') + ' (from the engines, not the pairs)')
    if act[0] == 'plugin':
        note(f'{where}: native 1/{x.v} ({act[3]}) needs the plugin (EDF6 runs it only through src/edf41natives.cpp)')
    if act[0] in ('as', 'plugin'):
        return Ins(act[1], act[2], _usize(act[2]))
    e.emit(Ins(OP_NAT0, NAT_CREATE_ENEMY, 2), Ins(0x03), deferred, Ins(OP_NAT0, NAT_SCALE_DURABILITY, 2))
    return None


def convert(src: Program, rewrite: Callable[[str], str] | None = None, notes: list[str] | None = None) -> Program:
    """src (a 4.1 script) as EDF5's recompile of it. rewrite maps each string constant except the handler names.
    notes, when given, gets a line for each thing in src the 135 base-game pairs do not show how EDF5 converts."""
    note = notes.append if notes is not None else (lambda s: None)
    names = [f.name for f in src.funcs]
    if 'Main' not in names:
        raise ValueError('BVM: the script has no Main')
    mission = src.funcs[names.index('Main'):]
    renamed: dict[str, str] = {}
    for f in mission[1:]:
        if f.name.startswith('Func') and f.name[4:].isdigit():
            renamed[f.name] = f'Func_{len(renamed) + 1}'
        else:
            note(f'function {f.name} is not a FuncNNN event handler (kept as it is)')

    def rename(s: str) -> str:
        if s.endswith('___Counter') and s[:-10] in renamed:
            return renamed[s[:-10]] + '___Counter'
        return renamed.get(s, s)

    def text(s: str) -> str:
        return renamed[s] if s in renamed else rewrite(s) if rewrite else s

    # globals: the handlers' counters, EDF5's sergent slots, the script's own
    old_globals = [rename(g) for g in src.globals]
    ncount = 0
    while ncount < len(old_globals) and old_globals[ncount].endswith('___Counter'):
        ncount += 1
    globals5 = old_globals[:ncount] + list(SERGENT) + old_globals[ncount:]
    gmap = [i if i < ncount else i + len(SERGENT) for i in range(len(old_globals))]

    lib = [Function(n, bytes.fromhex(t), [Ins(op, v, sz, ty) for op, sz, v, ty in code]) for n, t, code in EDF5_LIBRARY]
    fidx = {n: i for i, n in enumerate([f.name for f in lib] + [rename(f.name) for f in mission])}
    for f in lib:
        for x in f.code:
            if x.op == OP_CALL:
                x.v = fidx[x.v]
            elif x.op == OP_PUSHS:
                x.v = text(x.v)
    for f in src.funcs[:len(src.funcs) - len(mission)]:
        if f.name not in fidx:
            note(f'4.1 common function {f.name} has no EDF5 counterpart (dropped)')

    def operand(x: Ins) -> Ins:
        if x.op in (OP_PUSHG, OP_STOREG):
            return Ins(x.op, gmap[x.v], _usize(gmap[x.v]))
        if x.op == OP_CALL:
            name = rename(src.funcs[x.v].name)
            if name not in fidx:
                raise ValueError(f'BVM: call to {name}, which EDF5 does not have')
            return Ins(x.op, fidx[name], x.n)
        if x.op == OP_PUSHS:
            return Ins(x.op, text(x.v), x.n)
        if x.op == OP_NAT0:
            return Ins(x.op, NATIVES.get(x.v, x.v), _usize(NATIVES.get(x.v, x.v)))
        if x.op in JUMPS:
            return Ins(x.op, ('old', x.v), x.n)
        return Ins(x.op, x.v, x.n, x.t)

    # the initialiser: the sergent slots start at -1, right after the counters
    init = [operand(x) for x in src.init]
    cut = max((i + 1 for i, x in enumerate(init) if x.op == OP_STOREG and x.v < ncount), default=0)
    sergent = []
    for k in range(ncount, ncount + len(SERGENT)):
        sergent += [Ins(OP_PUSH, 1, 1), Ins(OP_NEG, 0, 0, 0), Ins(OP_STOREG, k, _usize(k))]
    init[cut:cut] = sergent

    graph, clear = _events(src, mission)
    progress = _progress(graph, clear)
    unknown: set[tuple[int, int]] = set()
    funcs5 = list(lib)
    for f in mission:
        code = f.code
        once = f is not mission[0] and _run_once(code)
        if f is not mission[0] and not once:
            note(f'{f.name} does not open with the run-once check (no progress / running check added)')
        addresses = _global_addresses(code, ncount, len(src.globals), unknown)
        preloaded = f is not mission[0]
        e = _Edit()
        deferred: Ins | None = None
        for i, x in enumerate(code):
            e.mark(i)
            if once and i == 15:            # the run-once jz goes to the body (4.1 threads it when that is empty)
                e.emit(Ins(OP_JZ, ('old', 17), x.n))
                continue
            if once and i == 17:
                if f.name in progress:
                    e.emit(Ins(OP_PUSH, _progress_bits(progress[f.name]), 4), Ins(OP_NAT0, NAT_PROGRESS, 1))
                k = len(e.out)
                if f.name in clear:
                    e.emit(Ins(OP_PUSH1), Ins(OP_NAT1, NAT_CLEAR_STATE, 1), Ins(OP_PUSH),
                           Ins(OP_JNE, ('new', k + 5), 1), Ins(OP_JMP, ('new', 0), 1))
                else:
                    e.emit(Ins(OP_NAT1, NAT_RUNNING, 1), Ins(OP_JZ, ('new', k + 3), 1), Ins(OP_JMP, ('new', 0), 1))
            if x.op == OP_NAT0 and x.v == NAT_REGISTER:
                e.emit(Ins(OP_PUSH))
                prev = e.out[-2]
                if i >= 2 and code[i - 2].op == OP_PUSHS and code[i - 2].v in clear and prev.op == OP_PUSH \
                        and prev.n == 4 and prev.v == 0:
                    prev.v = F_100
                elif i >= 2 and code[i - 2].op == OP_PUSHS and code[i - 2].v in clear:
                    note(f'{f.name} registers the clearing handler with {prev} (EDF5 passes 100.0 where 4.1 has 0.0)')
            y = operand(x)
            if i in addresses:
                v = gmap[_const(x)]
                y = Ins(OP_PUSH, v, _ssize(v))
            if i + 1 < len(code) and code[i + 1].op == OP_NAT1 and TABLE1.get(code[i + 1].v) == ('create_scaled',):
                if x.op not in _SIMPLE_PUSH:
                    raise UnsupportedNative(f'BVM: {f.name}: table 1 native {code[i + 1].v} whose last argument is '
                                            'not a single push (cannot move it behind CreateEnemy)')
                deferred = y
                continue
            if x.op in OP_NATIVE and x.op != OP_NAT0:
                y = _table1(f.name, x, deferred, e, note)
                deferred = None
                if y is None:
                    continue
            elif x.op == OP_NAT0 and (x.op, x.v) in ENGINE_EVIDENCE:
                note(f'{f.name}: native 0/{x.v} -> 0/{NATIVES[x.v]} (from the engines, not the base-game pairs)')
            e.emit(y)
            if not preloaded and x.op == OP_NAT0 and x.v == NAT_SETUP_DONE:
                preloaded = True
                for s in RESULT_SCREENS:
                    e.emit(Ins(OP_PUSHS, text(s)), Ins(OP_PUSH, -1, 1), Ins(OP_NAT0, NAT_PRELOAD, 1))
        if not preloaded:
            note('Main never calls native 10 (the result screens are not preloaded)')
        funcs5.append(Function(rename(f.name), f.types, e.done(len(code))))
    for op, nid in sorted(unknown):
        note(f'native {op:#x}/{nid} of unknown stack effect follows a constant that may be a global index')
    return Program(src.name, globals5, init, funcs5)


def to_edf5(bvm41: bytes, rewrite: Callable[[str], str] | None = None) -> bytes:
    """A 4.1 MISSION.BVM / BEFORE.BVM as EDF5 ships it. rewrite, when given, maps every string constant of the script
    (except the event handler names it registers) to the one to write, e.g. a DLC's 'mp01:/object/x.sgo' to a path
    EDF6 has; the string pool and every offset into it are rebuilt. Raises UnsupportedNative for a script calling a
    native EDF5 / EDF6 has no counterpart for (TABLE1)."""
    return build(convert(parse(bvm41), rewrite))
