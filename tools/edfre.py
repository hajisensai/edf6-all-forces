"""Tiny RE helpers over EDF.dll: string lookup, RIP-relative xrefs, disassembly."""
import re
import struct
import sys

import capstone
import numpy as np
import pefile

P = r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6\EDF.dll'
pe = pefile.PE(P, fast_load=True)
img: bytes = pe.get_memory_mapped_image()
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
TVA: int = text.VirtualAddress
TSZ: int = text.Misc_VirtualSize
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
_t = np.frombuffer(img[TVA:TVA + TSZ], dtype=np.uint8)
_d32 = None


def strs(s: str, wide: bool = False) -> list[int]:
    b = s.encode('utf-16le') + b'\0\0' if wide else s.encode() + b'\0'
    return [m.start() for m in re.finditer(re.escape(b), img)]


def _disp() -> np.ndarray:
    global _d32
    if _d32 is None:
        n = len(_t) - 4
        _d32 = (_t[0:n].astype(np.int64) | (_t[1:n + 1].astype(np.int64) << 8) |
                (_t[2:n + 2].astype(np.int64) << 16) | (_t[3:n + 3].astype(np.int64) << 24))
        _d32 = np.where(_d32 >= 2**31, _d32 - 2**32, _d32)
    return _d32


def xrefs(rva: int) -> list[int]:
    """Instruction start RVAs whose rip-relative disp32 (imm 0/1/4 after it) hits rva."""
    d = _disp()
    pos = np.arange(len(d), dtype=np.int64)
    out = set()
    for imm in (0, 1, 4):
        tgt = TVA + pos + 4 + imm + d
        for p in np.nonzero(tgt == rva)[0]:
            # find an instruction starting a few bytes before that decodes with this disp
            for back in range(2, 9):
                st = TVA + int(p) - back
                for ins in md.disasm(img[st:st + 16], st):
                    if ins.address + ins.size == TVA + int(p) + 4 + imm:
                        out.add(st)
                    break
    return sorted(out)


def dis(rva: int, n: int = 40) -> None:
    for i, ins in enumerate(md.disasm(img[rva:rva + n * 16], rva)):
        print(f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}')
        if i + 1 >= n or ins.mnemonic in ('ret', 'int3'):
            break


def q(rva: int) -> int:
    return struct.unpack_from('<Q', img, rva)[0]


def vtable_of(cls: str) -> list[int]:
    """RVAs of vtables whose complete-object locator names .?AV<cls>@@ (offset 0)."""
    name = ('.?AV' + cls + '@@').encode() + b'\0'
    res = []
    base = pe.OPTIONAL_HEADER.ImageBase
    for td in [m.start() - 0x10 for m in re.finditer(re.escape(name), img)]:
        # COL: sig(1) offset cdoffset typedesc_rva hierarchy_rva self_rva
        packed = struct.pack('<I', td)
        for m in re.finditer(re.escape(packed), img):
            col = m.start() - 12
            if col < 0:
                continue
            sig, off, cdo, tdr, hr, selfr = struct.unpack_from('<IIIIII', img, col)
            if sig != 1 or tdr != td or selfr != col or off != 0:
                continue
            colva = struct.pack('<Q', base + col)
            for v in re.finditer(re.escape(colva), img):
                res.append(v.start() + 8)
    return res


if __name__ == '__main__':
    print(sys.argv)


def callers(target: int) -> list[int]:
    """RVAs of direct `call rel32` / `jmp rel32` to target."""
    d = _disp()
    pos = np.arange(len(d), dtype=np.int64)
    hit = np.nonzero(TVA + pos + 4 + d == target)[0]
    out = []
    for p in hit:
        op = img[TVA + int(p) - 1]
        if op in (0xE8, 0xE9):
            out.append(TVA + int(p) - 1)
    return out


def func_start(rva: int) -> int:
    """Walk back to the function start using .pdata."""
    import bisect
    global _pdata
    try:
        _pdata
    except NameError:
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
        _pdata = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
    i = bisect.bisect_right(_pdata, (rva, 1 << 40)) - 1
    while i >= 0:
        b, e = _pdata[i]
        if b <= rva < e:
            return b
        i -= 1
    return -1


def vt_owner(addr: int) -> tuple[str, int, int]:
    """Class name, vtable start and slot index for a vtable entry address."""
    base = pe.OPTIONAL_HEADER.ImageBase
    a = addr
    while a > addr - 0x2000:
        col = q(a - 8) - base
        if 0 < col < len(img) - 24:
            sig, off, cdo, tdr, hr, selfr = struct.unpack_from('<IIIIII', img, col)
            if sig == 1 and selfr == col:
                name = img[tdr + 0x10:tdr + 0x80].split(b'\0')[0].decode()
                return name, a, (addr - a) // 8
        a -= 8
    return '?', 0, 0


def find_disp(value: int, mnem: tuple = (), must: str = '') -> list[tuple[int, str]]:
    """Instructions using memory displacement `value` (e.g. 0xe30); scans only functions whose bytes contain it."""
    func_start(0)
    pat = struct.pack('<I', value)
    hits = set()
    for m in re.finditer(re.escape(pat), img[TVA:TVA + TSZ]):
        f = func_start(TVA + m.start())
        if f >= 0:
            hits.add(f)
    out = []
    tag = f'+ {value:#x}]'
    for b, e in _pdata:
        if b not in hits:
            continue
        for ins in md.disasm(img[b:e], b):
            s = f'{ins.mnemonic} {ins.op_str}'
            if tag in s and (not mnem or ins.mnemonic in mnem) and must in s:
                out.append((ins.address, s))
    return out


def script_impl(decl: str) -> list[int]:
    """Native function registered for an AngelScript declaration (lea rdx,fn ... lea r8,decl ... call)."""
    res = []
    for s in strs(decl):
        for x in xrefs(s):
            ins_list = list(md.disasm(img[x - 0x60:x], x - 0x60))
            # take the last 'lea rdx, [rip ...]' that precedes a 'call' building the func ptr
            cand = None
            for ins in ins_list:
                if ins.mnemonic == 'lea' and ins.op_str.startswith('rdx, [rip'):
                    m = re.search(r'rip ([+-]) (0x[0-9a-f]+)', ins.op_str)
                    d = int(m.group(2), 16) * (1 if m.group(1) == '+' else -1)
                    cand = ins.address + ins.size + d
            if cand is not None:
                res.append(cand)
    return sorted(set(res))
